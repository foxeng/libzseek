#include <stdbool.h>    // bool
#include <stddef.h>     // size_t
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, free
#include <errno.h>      // errno
#include <string.h>     // memset
#include <pthread.h>    // pthread_mutex*

#include <zstd.h>

#include "zseek.h"
#include "seek_table.h"
#include "common.h"

#define COMPRESSION_LEVEL ZSTD_CLEVEL_DEFAULT
#define COMPRESSION_STRATEGY ZSTD_fast

struct zseek_writer {
    FILE *fout;
    ZSTD_CCtx *cctx;
    pthread_mutex_t lock;

    size_t frame_uc;    // Current frame bytes (uncompressed)
    size_t frame_cm;    // Current frame bytes (compressed)
    size_t min_frame_size;
    ZSTD_frameLog *fl;
};

zseek_writer_t *zseek_writer_open(const char *filename, int nb_workers,
    size_t min_frame_size, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_writer_t *writer = NULL;
    ZSTD_CCtx *cctx = NULL;
    size_t r = 0;
    int pr = 0;
    ZSTD_frameLog *fl = NULL;
    FILE *fout = NULL;

    writer = malloc(sizeof(*writer));
    if (!writer) {
        set_error_with_errno(errbuf, "allocate writer", errno);
        goto fail;
    }
    memset(writer, 0, sizeof(*writer));

    cctx = ZSTD_createCCtx();
    if (!cctx) {
        set_error(errbuf, "context creation failed");
        goto fail_w_writer;
    }
    r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel,
        COMPRESSION_LEVEL);
    if (ZSTD_isError(r)) {
        set_error(errbuf, "%s: %s", "set compression level",
            ZSTD_getErrorName(r));
        goto fail_w_cctx;
    }
    // TODO OPT: Don't set strategy?
    r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_strategy, COMPRESSION_STRATEGY);
    if (ZSTD_isError(r)) {
        set_error(errbuf, "%s: %s", "set strategy", ZSTD_getErrorName(r));
        goto fail_w_cctx;
    }

    if (nb_workers > 1) {
        r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, nb_workers);
        if (ZSTD_isError(r)) {
            set_error(errbuf, "%s: %s", "set nb of workers",
                ZSTD_getErrorName(r));
            goto fail_w_cctx;
        }
    }
    writer->cctx = cctx;
    writer->min_frame_size = min_frame_size;

    pr = pthread_mutex_init(&writer->lock, NULL);
    if (pr) {
        set_error_with_errno(errbuf, "initialize mutex", pr);
        goto fail_w_cctx;
    }

    fl = ZSTD_seekable_createFrameLog(0);
    if (!fl) {
        set_error(errbuf, "framelog creation failed");
        goto fail_w_lock;
    }
    writer->fl = fl;

    fout = fopen(filename, "wb");
    if (!fout) {
        set_error_with_errno(errbuf, "open file", errno);
        goto fail_w_framelog;
    }
    writer->fout = fout;

    return writer;

fail_w_framelog:
    ZSTD_seekable_freeFrameLog(fl);
fail_w_lock:
    pthread_mutex_destroy(&writer->lock);
fail_w_cctx:
    ZSTD_freeCCtx(cctx);
fail_w_writer:
    free(writer);
fail:
    return NULL;
}

/**
 * Flush, close and write current frame. This will block. Should be called with
 * the writer lock held.
 */
static bool end_frame(zseek_writer_t *writer)
{
    // TODO: Communicate error info?

    size_t buf_len = 0;
    void *buf = NULL;
    ZSTD_inBuffer buffin;
    size_t rem = 0;
    size_t r = 0;

    // Allocate output buffer
    buf_len = ZSTD_CStreamOutSize();
    buf = malloc(buf_len);
    if (!buf) {
        // perror("allocate output buffer");
        goto fail;
    }

    buffin = (ZSTD_inBuffer){NULL, 0, 0};
    rem = 0;
    do {
        ZSTD_outBuffer buffout = {buf, buf_len, 0};
        size_t written = 0;

        // Flush and end frame
        rem = ZSTD_compressStream2(writer->cctx, &buffout, &buffin,
            ZSTD_e_end);
        if (ZSTD_isError(rem)) {
            // fprintf(stderr, "compress: %s\n", ZSTD_getErrorName(rem));
            goto fail_w_buf;
        }

        // Update current frame compressed bytes
        writer->frame_cm += buffout.pos;

        // Write output
        written = fwrite(buffout.dst, 1, buffout.pos, writer->fout);
        if (written < buffout.pos) {
            // perror("write to file");
            goto fail_w_buf;
        }
    } while (rem > 0);

    // Log frame
    r = ZSTD_seekable_logFrame(writer->fl, writer->frame_cm, writer->frame_uc,
        0);
    if (ZSTD_isError(r)) {
        // fprintf(stderr, "log frame: %s\n", ZSTD_getErrorName(r));
        goto fail_w_buf;
    }

    // Reset current frame bytes
    writer->frame_uc = 0;
    writer->frame_cm = 0;

    free(buf);
    return true;

fail_w_buf:
    free(buf);
fail:
    return false;
}

bool zseek_writer_close(zseek_writer_t *writer, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    size_t buf_len = 0;
    void *buf = NULL;
    size_t rem = 0;
    size_t r = 0;
    int pr = 0;
    bool is_error = false;

    if (writer->frame_uc > 0) {
        // End final frame
        if (!end_frame(writer)) {
            set_error(errbuf, "end_frame failed");
            is_error = true;
        }
    }

    buf_len = 4096;
    buf = malloc(buf_len);
    if (!buf && !is_error) {
        set_error_with_errno(errbuf, "allocate output buffer", errno);
        is_error = true;
    }

    // Write seek table
    rem = 0;
    do {
        ZSTD_outBuffer buffout = {buf, buf_len, 0};
        size_t written = 0;

        rem = ZSTD_seekable_writeSeekTable(writer->fl, &buffout);
        if (ZSTD_isError(rem) && !is_error) {
            set_error(errbuf, "%s: %s", "write seek table",
                ZSTD_getErrorName(rem));
            is_error = true;
        }

        written = fwrite(buffout.dst, 1, buffout.pos, writer->fout);
        if (written < buffout.pos && !is_error) {
            set_error_with_errno(errbuf, "write to file", errno);
            is_error = true;
        }
    } while (rem > 0);
    free(buf);

    if (fclose(writer->fout) == EOF && !is_error) {
        set_error_with_errno(errbuf, "close file", errno);
        is_error = true;
    }

    r = ZSTD_seekable_freeFrameLog(writer->fl);
    if (ZSTD_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free frame log", ZSTD_getErrorName(r));
        is_error = true;
    }

    pr = pthread_mutex_destroy(&writer->lock);
    if (pr && !is_error) {
        set_error_with_errno(errbuf, "destroy mutex", pr);
        is_error = true;
    }

    r = ZSTD_freeCCtx(writer->cctx);
    if (ZSTD_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free context", ZSTD_getErrorName(r));
        is_error = true;
    }

    free(writer);

    return !is_error;
}

bool zseek_write(zseek_writer_t *writer, const void *buf, size_t len,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    int pr;
    size_t bout_len;
    void *bout;
    ZSTD_inBuffer buffin;

    pr = pthread_mutex_lock(&writer->lock);
    if (pr) {
        set_error_with_errno(errbuf, "lock mutex", pr);
        goto fail;
    }

    if (writer->frame_uc >= writer->min_frame_size) {
        // End current frame
        // NOTE: This blocks, flushing data dispatched for compression in
        // previous calls.
        if (!end_frame(writer)) {
            set_error(errbuf, "end_frame failed");
            goto fail_w_lock;
        }
    }

    // Allocate output buffer
    bout_len = ZSTD_CStreamOutSize();    // TODO OPT: Tune this according to input len? (see ZSTD_compressBound)
    bout = malloc(bout_len);
    if (!bout) {
        set_error_with_errno(errbuf, "allocate output buffer", errno);
        goto fail_w_lock;
    }

    buffin = (ZSTD_inBuffer){buf, len, 0};
    do {
        ZSTD_outBuffer buffout = {bout, bout_len, 0};
        size_t rem = 0;
        size_t written = 0;

        // Dispatch for compression
        rem = ZSTD_compressStream2(writer->cctx, &buffout, &buffin,
            ZSTD_e_continue);
        if (ZSTD_isError(rem)) {
            set_error(errbuf, "%s: %s", "compress", ZSTD_getErrorName(rem));
            goto fail_w_bout;
        }

        // Update current frame compressed bytes
        writer->frame_cm += buffout.pos;

        // Write output
        written = fwrite(buffout.dst, 1, buffout.pos, writer->fout);
        if (written < buffout.pos) {
            set_error_with_errno(errbuf, "write to file", errno);
            goto fail_w_bout;
        }
    } while (buffin.pos < buffin.size);

    // Update current frame uncompresed bytes
    writer->frame_uc += len;

    free(bout);

    pr = pthread_mutex_unlock(&writer->lock);
    if (pr) {
        set_error_with_errno(errbuf, "unlock mutex", pr);
        goto fail;
    }

    return true;

fail_w_bout:
    free(bout);
fail_w_lock:
    pthread_mutex_unlock(&writer->lock);
fail:
    return false;
}
