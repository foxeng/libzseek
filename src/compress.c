#include <stdbool.h>    // bool
#include <stddef.h>     // size_t
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, free
#include <errno.h>      // errno
#include <string.h>     // memset
#include <pthread.h>    // pthread_setaffinity_np

#include <zstd.h>

#include "zseek.h"
#include "seek_table.h"
#include "common.h"

#define COMPRESSION_LEVEL ZSTD_CLEVEL_DEFAULT
#define COMPRESSION_STRATEGY ZSTD_fast

struct zseek_writer {
    zseek_write_file_t user_file;
    ZSTD_CCtx *cctx;

    size_t frame_uc;    // Current frame bytes (uncompressed)
    size_t frame_cm;    // Current frame bytes (compressed)
    size_t min_frame_size;
    ZSTD_frameLog *fl;
};

static bool default_write(const void *data, size_t size, void *user_data)
{
    FILE *fout = user_data;
    if (fwrite(data, 1, size, fout) != size) {
        // perror("write to file");
        return false;
    }
    return true;
}

zseek_writer_t *zseek_writer_open_full(zseek_write_file_t user_file,
    zseek_mt_param_t mt, size_t min_frame_size, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_writer_t *writer = malloc(sizeof(*writer));
    if (!writer) {
        set_error_with_errno(errbuf, "allocate writer", errno);
        goto fail;
    }
    memset(writer, 0, sizeof(*writer));

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx) {
        set_error(errbuf, "context creation failed");
        goto fail_w_writer;
    }
    size_t r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel,
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

    // Declared here to be in scope at fail_w_cpuset
    pthread_t self_tid = pthread_self();
    cpu_set_t prev_cpuset;

    if (mt.nb_workers > 1) {
        r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, mt.nb_workers);
        if (ZSTD_isError(r)) {
            set_error(errbuf, "%s: %s", "set nb of workers",
                ZSTD_getErrorName(r));
            goto fail_w_cctx;
        }

        if (mt.cpuset) {
            // Save current cpu set
            int pr = pthread_getaffinity_np(self_tid, sizeof(prev_cpuset),
                &prev_cpuset);
            if (pr) {
                set_error_with_errno(errbuf, "get thread affinity", pr);
                goto fail_w_cctx;
            }

            // Set requested cpu set
            pthread_setaffinity_np(self_tid, mt.cpusetsize, mt.cpuset);
            if (pr) {
                set_error_with_errno(errbuf, "set thread affinity", pr);
                goto fail_w_cpuset;
            }

            // Trigger thread pool creation (as per zstd 1.5.0 at least)
            ZSTD_inBuffer buffin = {NULL, 0, 0};
            ZSTD_outBuffer buffout = {NULL, 0, 0};
            r = ZSTD_compressStream2(cctx, &buffout, &buffin, ZSTD_e_continue);
            if (ZSTD_isError(r)) {
                set_error(errbuf, "%s: %s", "create threads",
                    ZSTD_getErrorName(r));
                goto fail_w_cpuset;
            }

            // Reset previous cpu set
            pr = pthread_setaffinity_np(self_tid, sizeof(prev_cpuset),
                &prev_cpuset);
            if (pr) {
                set_error_with_errno(errbuf, "reset thread affinity", pr);
                goto fail_w_cctx;
            }
        }
    }
    writer->cctx = cctx;
    writer->min_frame_size = min_frame_size;

    ZSTD_frameLog *fl = ZSTD_seekable_createFrameLog(0);
    if (!fl) {
        set_error(errbuf, "framelog creation failed");
        goto fail_w_cpuset;
    }
    writer->fl = fl;

    writer->user_file = user_file;

    return writer;

fail_w_cpuset:
    if (mt.cpuset)
        pthread_setaffinity_np(self_tid, sizeof(prev_cpuset), &prev_cpuset);
fail_w_cctx:
    ZSTD_freeCCtx(cctx);
fail_w_writer:
    free(writer);
fail:
    return NULL;
}

zseek_writer_t *zseek_writer_open(FILE *cfile, zseek_mt_param_t mt,
    size_t min_frame_size, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_write_file_t user_file = {cfile, default_write};
    return zseek_writer_open_full(user_file, mt, min_frame_size, errbuf);
}

/**
 * Flush, close and write current frame. This will block.
 */
static bool end_frame(zseek_writer_t *writer)
{
    // TODO: Communicate error info?

    // Allocate output buffer
    size_t buf_len = ZSTD_CStreamOutSize();
    void *buf = malloc(buf_len);
    if (!buf) {
        // perror("allocate output buffer");
        goto fail;
    }

    ZSTD_inBuffer buffin = {NULL, 0, 0};
    size_t rem = 0;
    do {
        // Flush and end frame
        ZSTD_outBuffer buffout = {buf, buf_len, 0};
        rem = ZSTD_compressStream2(writer->cctx, &buffout, &buffin,
            ZSTD_e_end);
        if (ZSTD_isError(rem)) {
            // fprintf(stderr, "compress: %s\n", ZSTD_getErrorName(rem));
            goto fail_w_buf;
        }

        // Update current frame compressed bytes
        writer->frame_cm += buffout.pos;

        // Write output
        if (!writer->user_file.write(buffout.dst, buffout.pos,
            writer->user_file.user_data)) {

            // TODO OPT: Use errno if user_file.write sets it
            // fprintf(stderr, "write to file failed");
            goto fail_w_buf;
        }
    } while (rem > 0);

    // Log frame
    size_t r = ZSTD_seekable_logFrame(writer->fl, writer->frame_cm,
        writer->frame_uc, 0);
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
    bool is_error = false;

    if (writer->frame_uc > 0) {
        // End final frame
        if (!end_frame(writer)) {
            set_error(errbuf, "end_frame failed");
            is_error = true;
        }
    }

    size_t buf_len = 4096;
    void *buf = malloc(buf_len);
    if (!buf && !is_error) {
        set_error_with_errno(errbuf, "allocate output buffer", errno);
        is_error = true;
    }

    // Write seek table
    size_t rem = 0;
    do {
        ZSTD_outBuffer buffout = {buf, buf_len, 0};
        rem = ZSTD_seekable_writeSeekTable(writer->fl, &buffout);
        if (ZSTD_isError(rem) && !is_error) {
            set_error(errbuf, "%s: %s", "write seek table",
                ZSTD_getErrorName(rem));
            is_error = true;
        }

        bool written = writer->user_file.write(buffout.dst, buffout.pos,
            writer->user_file.user_data);
        if (!written && !is_error) {
            // TODO OPT: Use errno if user_file.write sets it
            set_error(errbuf, "write to file failed");
            is_error = true;
        }
    } while (rem > 0);
    free(buf);

    size_t r = ZSTD_seekable_freeFrameLog(writer->fl);
    if (ZSTD_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free frame log", ZSTD_getErrorName(r));
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
    if (writer->frame_uc >= writer->min_frame_size) {
        // End current frame
        // NOTE: This blocks, flushing data dispatched for compression in
        // previous calls.
        if (!end_frame(writer)) {
            set_error(errbuf, "end_frame failed");
            goto fail;
        }
    }

    // Allocate output buffer
    size_t bout_len = ZSTD_CStreamOutSize();    // TODO OPT: Tune this according to input len? (see ZSTD_compressBound)
    void *bout = malloc(bout_len);
    if (!bout) {
        set_error_with_errno(errbuf, "allocate output buffer", errno);
        goto fail;
    }

    ZSTD_inBuffer buffin = {buf, len, 0};
    do {
        // Dispatch for compression
        ZSTD_outBuffer buffout = {bout, bout_len, 0};
        size_t rem = ZSTD_compressStream2(writer->cctx, &buffout, &buffin,
            ZSTD_e_continue);
        if (ZSTD_isError(rem)) {
            set_error(errbuf, "%s: %s", "compress", ZSTD_getErrorName(rem));
            goto fail_w_bout;
        }

        // Update current frame compressed bytes
        writer->frame_cm += buffout.pos;

        // Write output
        if (!writer->user_file.write(buffout.dst, buffout.pos,
                writer->user_file.user_data)) {
            // TODO OPT: Use errno if user_file.pread sets it
            set_error(errbuf, "write to file failed");
            goto fail_w_bout;
        }
    } while (buffin.pos < buffin.size);

    // Update current frame uncompresed bytes
    writer->frame_uc += len;

    free(bout);

    return true;

fail_w_bout:
    free(bout);
fail:
    return false;
}
