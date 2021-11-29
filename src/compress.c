#include <stdbool.h>    // bool
#include <stddef.h>     // size_t
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, free
#include <errno.h>      // errno
#include <string.h>     // memset
#include <pthread.h>    // pthread_setaffinity_np
#include <assert.h>     // assert

#include <zstd.h>
#include <lz4.h>
#include <lz4frame.h>

#include "zseek.h"
#include "seek_table.h"
#include "common.h"
#include "buffer.h"

struct zseek_writer {
    zseek_write_file_t user_file;
    zseek_compression_type_t type;
    union {
        ZSTD_CCtx *cctx_zstd;
        struct {
            zseek_buffer_t *ubuf;
            LZ4F_preferences_t preferences;
        };
    };

    size_t frame_uc;    // Current frame bytes (uncompressed)
    size_t frame_cm;    // Current frame bytes (compressed)
    size_t min_frame_size;
    size_t total_cm;    // Total file compressed bytes _excluding_ frame_cm
    ZSTD_frameLog *fl;
    zseek_buffer_t *cbuf;
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

static zseek_writer_t *zseek_writer_open_full_zstd(zseek_write_file_t user_file,
	zseek_compression_param_t* zsp, size_t min_frame_size,
	char errbuf[ZSEEK_ERRBUF_SIZE])
{
    int compression_level = ZSTD_CLEVEL_DEFAULT;
    ZSTD_strategy strategy = ZSTD_fast;
    if (zsp) {
        compression_level = zsp->params.zstd_params.compression_level;
        strategy = zsp->params.zstd_params.strategy;
    }

    zseek_writer_t *writer = malloc(sizeof(*writer));
    if (!writer) {
        set_error_with_errno(errbuf, "allocate writer", errno);
        goto fail;
    }
    memset(writer, 0, sizeof(*writer));
    writer->type = ZSEEK_ZSTD;

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx) {
        set_error(errbuf, "context creation failed");
        goto fail_w_writer;
    }
    size_t r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel,
        compression_level);
    if (ZSTD_isError(r)) {
        set_error(errbuf, "%s: %s", "set compression level",
            ZSTD_getErrorName(r));
        goto fail_w_cctx;
    }
    // TODO OPT: Don't set strategy?
    r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_strategy, strategy);
    if (ZSTD_isError(r)) {
        set_error(errbuf, "%s: %s", "set strategy", ZSTD_getErrorName(r));
        goto fail_w_cctx;
    }

    // Declared here to be in scope at fail_w_cpuset
    pthread_t self_tid = pthread_self();
    cpu_set_t prev_cpuset;

    if (zsp && zsp->params.zstd_params.nb_workers > 1) {
        r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers,
            zsp->params.zstd_params.nb_workers);
        if (ZSTD_isError(r)) {
            set_error(errbuf, "%s: %s", "set nb of workers",
                ZSTD_getErrorName(r));
            goto fail_w_cctx;
        }

        if (zsp->params.zstd_params.cpuset) {
            // Save current cpu set
            int pr = pthread_getaffinity_np(self_tid, sizeof(prev_cpuset),
                &prev_cpuset);
            if (pr) {
                set_error_with_errno(errbuf, "get thread affinity", pr);
                goto fail_w_cctx;
            }

            // Set requested cpu set
            pthread_setaffinity_np(self_tid, zsp->params.zstd_params.cpusetsize,
                zsp->params.zstd_params.cpuset);
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
    writer->cctx_zstd = cctx;
    writer->min_frame_size = min_frame_size;

    ZSTD_frameLog *fl = ZSTD_seekable_createFrameLog(0);
    if (!fl) {
        set_error(errbuf, "framelog creation failed");
        goto fail_w_cpuset;
    }
    writer->fl = fl;

    zseek_buffer_t *cbuf = zseek_buffer_new(0);
    if (!cbuf) {
        set_error(errbuf, "buffer creation failed");
        goto fail_w_fl;
    }
    writer->cbuf = cbuf;

    writer->user_file = user_file;

    return writer;

fail_w_fl:
    ZSTD_seekable_freeFrameLog(fl);
fail_w_cpuset:
    if (zsp && zsp->params.zstd_params.cpuset)
        pthread_setaffinity_np(self_tid, sizeof(prev_cpuset), &prev_cpuset);
fail_w_cctx:
    ZSTD_freeCCtx(cctx);
fail_w_writer:
    free(writer);
fail:
    return NULL;
}

static zseek_writer_t *zseek_writer_open_full_lz4(zseek_write_file_t user_file,
	zseek_compression_param_t* zsp, size_t min_frame_size,
	char errbuf[ZSEEK_ERRBUF_SIZE])
{
    int compression_level = 0;
    if (zsp)
        compression_level = zsp->params.lz4_params.compression_level;

    zseek_writer_t *writer = malloc(sizeof(*writer));
    if (!writer) {
        set_error_with_errno(errbuf, "allocate writer", errno);
        goto fail;
    }
    memset(writer, 0, sizeof(*writer));
    writer->type = ZSEEK_LZ4;
    writer->preferences.compressionLevel = compression_level;
    // Avoid unnecessary copies since we compress all at once anyway
    writer->preferences.autoFlush = 1;
    // Use smaller block sizes to reduce buffering
    writer->preferences.frameInfo.blockSizeID = LZ4F_max64KB;

    zseek_buffer_t *ubuf = zseek_buffer_new(min_frame_size);
    if (!ubuf) {
        set_error(errbuf, "input buffer creation failed");
        goto fail_w_writer;
    }
    writer->ubuf = ubuf;
    writer->min_frame_size = min_frame_size;

    ZSTD_frameLog *fl = ZSTD_seekable_createFrameLog(0);
    if (!fl) {
        set_error(errbuf, "framelog creation failed");
        goto fail_w_ubuf;
    }
    writer->fl = fl;

    size_t cbuf_len = LZ4F_compressFrameBound(min_frame_size,
        &writer->preferences);
    zseek_buffer_t *cbuf = zseek_buffer_new(cbuf_len);
    if (!cbuf) {
        set_error(errbuf, "output buffer creation failed");
        goto fail_w_fl;
    }
    writer->cbuf = cbuf;

    writer->user_file = user_file;

    return writer;

fail_w_fl:
    ZSTD_seekable_freeFrameLog(fl);
fail_w_ubuf:
    zseek_buffer_free(ubuf);
fail_w_writer:
    free(writer);
fail:
    return NULL;
}

zseek_writer_t *zseek_writer_open_full(zseek_write_file_t user_file,
	zseek_compression_param_t* zsp, size_t min_frame_size,
	char errbuf[ZSEEK_ERRBUF_SIZE])
{
    // TODO OPT: Don't hard-code the default (zstd)?
    if (!zsp) {
        return zseek_writer_open_full_zstd(user_file, zsp, min_frame_size,
            errbuf);
    }

    switch (zsp->type) {
    case ZSEEK_ZSTD:
        return zseek_writer_open_full_zstd(user_file, zsp, min_frame_size,
            errbuf);
    case ZSEEK_LZ4:
        return zseek_writer_open_full_lz4(user_file, zsp, min_frame_size,
            errbuf);
    default:
        set_error(errbuf, "wrong compression type (%d)", zsp->type);
        return NULL;
    }
}

zseek_writer_t *zseek_writer_open(FILE *cfile, zseek_compression_param_t *zsp,
    size_t min_frame_size, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_write_file_t user_file = {cfile, default_write};
    return zseek_writer_open_full(user_file, zsp, min_frame_size, errbuf);
}

/**
 * Flush, close and write current frame. This will block.
 */
static bool end_frame_zstd(zseek_writer_t *writer)
{
    // TODO: Communicate error info?

    // Resize output buffer
    size_t cbuf_len = ZSTD_CStreamOutSize();
    if (!zseek_buffer_resize(writer->cbuf, cbuf_len)) {
        // fprintf(stderr, "resize output buffer failed");
        return false;
    }
    void *cbuf_data = zseek_buffer_data(writer->cbuf);
    assert(cbuf_data);

    ZSTD_inBuffer buffin = {NULL, 0, 0};
    size_t rem = 0;
    do {
        // Flush and end frame
        ZSTD_outBuffer buffout = {cbuf_data, cbuf_len, 0};
        rem = ZSTD_compressStream2(writer->cctx_zstd, &buffout, &buffin,
            ZSTD_e_end);
        if (ZSTD_isError(rem)) {
            // fprintf(stderr, "compress: %s\n", ZSTD_getErrorName(rem));
            return false;
        }

        // Update current frame compressed bytes
        writer->frame_cm += buffout.pos;

        // Write output
        if (!writer->user_file.write(buffout.dst, buffout.pos,
            writer->user_file.user_data)) {

            // TODO OPT: Use errno if user_file.write sets it
            // fprintf(stderr, "write to file failed");
            return false;
        }
    } while (rem > 0);

    // Log frame
    size_t r = ZSTD_seekable_logFrame(writer->fl, writer->frame_cm,
        writer->frame_uc, 0);
    if (ZSTD_isError(r)) {
        // fprintf(stderr, "log frame: %s\n", ZSTD_getErrorName(r));
        return false;
    }

    // Reset current frame bytes
    writer->total_cm += writer->frame_cm;
    writer->frame_uc = 0;
    writer->frame_cm = 0;

    return true;
}

static bool zseek_writer_close_zstd(zseek_writer_t *writer,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    bool is_error = false;

    if (writer->frame_uc > 0) {
        // End final frame
        if (!end_frame_zstd(writer)) {
            set_error(errbuf, "end_frame_zstd failed");
            is_error = true;
        }
    }

    size_t cbuf_len = 4096;
    if (!zseek_buffer_resize(writer->cbuf, cbuf_len) && !is_error) {
        set_error(errbuf, "resize output buffer failed");
        is_error = true;
    }
    void *cbuf_data = zseek_buffer_data(writer->cbuf);
    assert(cbuf_data);

    // Write seek table
    size_t rem = 0;
    do {
        ZSTD_outBuffer buffout = {cbuf_data, cbuf_len, 0};
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

    zseek_buffer_free(writer->cbuf);

    size_t r = ZSTD_seekable_freeFrameLog(writer->fl);
    if (ZSTD_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free frame log", ZSTD_getErrorName(r));
        is_error = true;
    }

    r = ZSTD_freeCCtx(writer->cctx_zstd);
    if (ZSTD_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free context", ZSTD_getErrorName(r));
        is_error = true;
    }

    free(writer);

    return !is_error;
}

/**
 * Flush, close and write current frame. This will block.
 */
static bool end_frame_lz4(zseek_writer_t *writer)
{
    // TODO: Communicate error info?

    size_t ubuf_len = zseek_buffer_size(writer->ubuf);
    void *ubuf_data = zseek_buffer_data(writer->ubuf);
    assert(ubuf_data);

    // Resize output buffer
    writer->preferences.frameInfo.contentSize = writer->frame_uc;
    size_t cbuf_len = LZ4F_compressFrameBound(writer->frame_uc,
        &writer->preferences);
    if (!zseek_buffer_resize(writer->cbuf, cbuf_len)) {
        // fprintf(stderr, "resize output buffer failed");
        return false;
    }
    void *cbuf_data = zseek_buffer_data(writer->cbuf);
    assert(cbuf_data);

    // Compress frame
    size_t cdata_len = LZ4F_compressFrame(cbuf_data, cbuf_len, ubuf_data,
        ubuf_len, &writer->preferences);
    if (LZ4F_isError(cdata_len)) {
        // fprintf(stderr, "%s: %s", "compress", LZ4F_getErrorName(cdata_len));
        return false;
    }
    // Correct buffer size (shrinks it, should not fail)
    assert(zseek_buffer_resize(writer->cbuf, cdata_len));
    writer->frame_cm += cdata_len;

    // Write output
    if (!writer->user_file.write(cbuf_data, cdata_len,
        writer->user_file.user_data)) {

        // TODO OPT: Use errno if user_file.pread sets it
        // fprintf(stderr, "write to file failed");
        return false;
    }

    // Log frame
    size_t r = ZSTD_seekable_logFrame(writer->fl, writer->frame_cm,
        writer->frame_uc, 0);
    if (ZSTD_isError(r)) {
        // fprintf(stderr, "log frame: %s\n", ZSTD_getErrorName(r));
        return false;
    }

    // Reset buffers and counters
    writer->total_cm += writer->frame_cm;
    writer->frame_uc = 0;
    writer->frame_cm = 0;
    zseek_buffer_reset(writer->ubuf);
    zseek_buffer_reset(writer->cbuf);

    return true;
}

static bool zseek_writer_close_lz4(zseek_writer_t *writer,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    bool is_error = false;

    if (writer->frame_uc > 0) {
        // End final frame
        if (!end_frame_lz4(writer)) {
            set_error(errbuf, "end_frame_lz4 failed");
            is_error = true;
        }
    }

    size_t cbuf_len = zseek_buffer_capacity(writer->cbuf);
    if (cbuf_len < 4096)
        cbuf_len = 4096;
    if (!zseek_buffer_resize(writer->cbuf, cbuf_len) && !is_error) {
        set_error(errbuf, "resize output buffer failed");
        is_error = true;
    }
    void *cbuf_data = zseek_buffer_data(writer->cbuf);
    assert(cbuf_data);

    // Write seek table
    size_t rem = 0;
    do {
        ZSTD_outBuffer buffout = {cbuf_data, cbuf_len, 0};
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

    zseek_buffer_free(writer->cbuf);

    size_t r = ZSTD_seekable_freeFrameLog(writer->fl);
    if (ZSTD_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free frame log", ZSTD_getErrorName(r));
        is_error = true;
    }

    zseek_buffer_free(writer->ubuf);

    free(writer);

    return !is_error;
}

bool zseek_writer_close(zseek_writer_t *writer, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    if (!writer)
        return true;

    switch (writer->type) {
    case ZSEEK_ZSTD:
        return zseek_writer_close_zstd(writer, errbuf);
    case ZSEEK_LZ4:
        return zseek_writer_close_lz4(writer, errbuf);
    default:
        // BUG
        assert(false);
        return false;
    }
}

static bool zseek_write_zstd(zseek_writer_t *writer, const void *buf,
    size_t len, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    if (writer->frame_uc >= writer->min_frame_size) {
        // End current frame
        // NOTE: This blocks, flushing data dispatched for compression in
        // previous calls.
        if (!end_frame_zstd(writer)) {
            set_error(errbuf, "end_frame_zstd failed");
            return false;
        }
    }

    // Resize output buffer
    size_t cbuf_len = ZSTD_CStreamOutSize();    // TODO OPT: Tune this according to input len? (see ZSTD_compressBound)
    if (!zseek_buffer_resize(writer->cbuf, cbuf_len)) {
        set_error(errbuf, "resize output buffer failed");
        return false;
    }
    void *cbuf_data = zseek_buffer_data(writer->cbuf);
    assert(cbuf_data);

    ZSTD_inBuffer buffin = {buf, len, 0};
    do {
        // Dispatch for compression
        ZSTD_outBuffer buffout = {cbuf_data, cbuf_len, 0};
        size_t rem = ZSTD_compressStream2(writer->cctx_zstd, &buffout, &buffin,
            ZSTD_e_continue);
        if (ZSTD_isError(rem)) {
            set_error(errbuf, "%s: %s", "compress", ZSTD_getErrorName(rem));
            return false;
        }

        // Update current frame compressed bytes
        writer->frame_cm += buffout.pos;

        // Write output
        if (!writer->user_file.write(buffout.dst, buffout.pos,
                writer->user_file.user_data)) {
            // TODO OPT: Use errno if user_file.pread sets it
            set_error(errbuf, "write to file failed");
            return false;
        }
    } while (buffin.pos < buffin.size);

    // Update current frame uncompresed bytes
    writer->frame_uc += len;

    return true;
}

static bool zseek_write_lz4(zseek_writer_t *writer, const void *buf, size_t len,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    // Buffer uncompressed data
    if (!zseek_buffer_push(writer->ubuf, buf, len)) {
        set_error(errbuf, "failed to buffer uncompressed data");
        return false;
    }
    writer->frame_uc += len;

    if (writer->frame_uc >= writer->min_frame_size) {
        // End current frame
        if (!end_frame_lz4(writer)) {
            set_error(errbuf, "end_frame_lz4 failed");
            return false;
        }
    }

    return true;
}

bool zseek_write(zseek_writer_t *writer, const void *buf, size_t len,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    if (!writer) {
        set_error(errbuf, "invalid writer");
        return false;
    }

    switch (writer->type) {
    case ZSEEK_ZSTD:
        return zseek_write_zstd(writer, buf, len, errbuf);
    case ZSEEK_LZ4:
        return zseek_write_lz4(writer, buf, len, errbuf);
    default:
        // BUG
        assert(false);
        return false;
    }
}

bool zseek_writer_stats(zseek_writer_t *writer, zseek_writer_stats_t *stats,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    if (!writer) {
        set_error(errbuf, "invalid writer");
        return false;
    }

    if (!stats) {
        set_error(errbuf, "invalid stats pointer");
        return false;
    }

    size_t frames = framelog_entries(writer->fl);
    if (writer->frame_uc > 0)
        frames++;

    size_t seek_table_size = framelog_size(writer->fl);
    if (writer->frame_uc > 0) {
        const size_t SIZE_PER_FRAME = 8; // assume no checksum
        seek_table_size += SIZE_PER_FRAME;
    }

    size_t seek_table_memory = framelog_memory_usage(writer->fl);

    // NOTE: This is an _estimate_ because frame_cm is <= final frame size,
    // since there may be still data to flush from the compressor.
    size_t compressed_size = writer->total_cm + writer->frame_cm +
        seek_table_size;

    // NOTE: This is an _estimate_ because the underlying compression lib may
    // buffer too in its context object.
    size_t buffer_size = zseek_buffer_capacity(writer->cbuf);
    if (writer->type == ZSEEK_LZ4)
        buffer_size += zseek_buffer_capacity(writer->ubuf);

    *stats = (zseek_writer_stats_t) {
        .seek_table_size = seek_table_size,
        .seek_table_memory = seek_table_memory,
        .frames = frames,
        .compressed_size = compressed_size,
        .buffer_size = buffer_size,
    };

    return true;
}
