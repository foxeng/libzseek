#include <stdbool.h>    // bool
#include <stddef.h>     // size_t
#include <stdint.h>     // uint*_t
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, free
#include <errno.h>      // errno
#include <string.h>     // memset
#include <pthread.h>    // pthread_mutex*
#include <assert.h>     // assert

#include <sys/stat.h>   // fstat
#include <endian.h>     // le32toh
#include <zstd.h>
#include <lz4frame.h>

#include "zseek.h"
#include "seek_table.h"
#include "common.h"
#include "cache.h"
#include "buffer.h"

#define ZSTD_MAGIC 0xFD2FB528
#define LZ4_MAGIC 0x184D2204

#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct zseek_reader {
    zseek_read_file_t user_file;
    zseek_compression_type_t type;
    union {
        // TODO: If there's contention, use one dctx per read()
        struct {
            ZSTD_DCtx *dctx_zstd;
            ZSTD_DStream *dstream_zstd;
        };
        LZ4F_dctx *dctx_lz4;
    };
    pthread_rwlock_t lock;

    ZSTD_seekTable *st;
    zseek_cache_t *cache;
    size_t pos;
    zseek_buffer_t *cbuf;
    zseek_buffer_t *dbuf;   // discard buffer
};

static ssize_t default_pread(void *data, size_t size, size_t offset,
    void *user_data, void *call_data)
{
    (void)call_data;

    FILE *fin = user_data;

    // Save current file position
    long prev_pos = ftell(fin);
    if (prev_pos == -1) {
        // perror("get file position");
        return -1;
    }

    if (fseek(fin, offset, SEEK_SET) == -1) {
        // perror("set file position");
        return -1;
    }
    size_t _read = fread(data, 1, size, fin);
    if (_read != size && ferror(fin)) {
        // perror("read from file");
        return -1;
    }

    // Restore previous file position
    if (fseek(fin, prev_pos, SEEK_SET) == -1) {
        // perror("restore file position");
        return -1;
    }

    return _read;
}

static ssize_t default_fsize(void *user_data, void *call_data)
{
    (void)call_data;

    FILE *f = user_data;
    int fd = fileno(f);
    if (fd == -1) {
        // perror("get file descriptor");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        // perror("get file size");
        return -1;
    }

    return st.st_size;
}

static zseek_reader_t *zseek_reader_open_full_zstd(zseek_read_file_t user_file,
    size_t cache_size, void *call_data, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_reader_t *reader = malloc(sizeof(*reader));
    if (!reader) {
        set_error_with_errno(errbuf, "allocate reader", errno);
        goto fail;
    }
    memset(reader, 0, sizeof(*reader));
    reader->type = ZSEEK_ZSTD;

    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!dctx) {
        set_error(errbuf, "context creation failed");
        goto fail_w_reader;
    }
    reader->dctx_zstd = dctx;

    ZSTD_DStream *dstream = ZSTD_createDStream();
    if (!dstream) {
        set_error(errbuf, "dstream creation failed");
        goto fail_w_dctx;
    }
    reader->dstream_zstd = dstream;

    int pr = pthread_rwlock_init(&reader->lock, NULL);
    if (pr) {
        set_error_with_errno(errbuf, "initialize lock", pr);
        goto fail_w_dstream;
    }

    reader->user_file = user_file;

    ZSTD_seekTable *st = read_seek_table(user_file, call_data);
    if (!st) {
        set_error(errbuf, "read_seek_table failed");
        goto fail_w_lock;
    }
    reader->st = st;

    zseek_cache_t *cache = NULL;
    if (cache_size > 0) {
        cache = zseek_cache_new(cache_size);
        if (!cache) {
            set_error(errbuf, "cache creation failed");
            goto fail_w_st;
        }
    }
    reader->cache = cache;

    zseek_buffer_t *cbuf = zseek_buffer_new(0);
    if (!cbuf) {
        set_error(errbuf, "buffer creation failed");
        goto fail_w_cache;
    }
    reader->cbuf = cbuf;

    zseek_buffer_t *dbuf = zseek_buffer_new(0);
    if (!dbuf) {
        set_error(errbuf, "discard buffer creation failed");
        goto fail_w_cbuf;
    }
    reader->dbuf = dbuf;

    return reader;

fail_w_cbuf:
    zseek_buffer_free(cbuf);
fail_w_cache:
    zseek_cache_free(cache);
fail_w_st:
    seek_table_free(st);
fail_w_lock:
    pthread_rwlock_destroy(&reader->lock);
fail_w_dstream:
    ZSTD_freeDStream(dstream);
fail_w_dctx:
    ZSTD_freeDCtx(dctx);
fail_w_reader:
    free(reader);
fail:
    return NULL;
}

static zseek_reader_t *zseek_reader_open_full_lz4(zseek_read_file_t user_file,
    size_t cache_size, void *call_data, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_reader_t *reader = malloc(sizeof(*reader));
    if (!reader) {
        set_error_with_errno(errbuf, "allocate reader", errno);
        goto fail;
    }
    memset(reader, 0, sizeof(*reader));
    reader->type = ZSEEK_LZ4;

    LZ4F_dctx *dctx;
    LZ4F_errorCode_t r = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
    if (LZ4F_isError(r)) {
        set_error(errbuf, "%s: %s", "context creation failed",
            LZ4F_getErrorName(r));
        goto fail_w_reader;
    }
    reader->dctx_lz4 = dctx;

    int pr = pthread_rwlock_init(&reader->lock, NULL);
    if (pr) {
        set_error_with_errno(errbuf, "initialize lock", pr);
        goto fail_w_dctx;
    }

    reader->user_file = user_file;

    ZSTD_seekTable *st = read_seek_table(user_file, call_data);
    if (!st) {
        set_error(errbuf, "read_seek_table failed");
        goto fail_w_lock;
    }
    reader->st = st;

    zseek_cache_t *cache = NULL;
    if (cache_size > 0) {
        cache = zseek_cache_new(cache_size);
        if (!cache) {
            set_error(errbuf, "cache creation failed");
            goto fail_w_st;
        }
    }
    reader->cache = cache;

    zseek_buffer_t *cbuf = zseek_buffer_new(0);
    if (!cbuf) {
        set_error(errbuf, "buffer creation failed");
        goto fail_w_cache;
    }
    reader->cbuf = cbuf;

    zseek_buffer_t *dbuf = zseek_buffer_new(0);
    if (!dbuf) {
        set_error(errbuf, "discard buffer creation failed");
        goto fail_w_cbuf;
    }
    reader->dbuf = dbuf;

    return reader;

fail_w_cbuf:
    zseek_buffer_free(cbuf);
fail_w_cache:
    zseek_cache_free(cache);
fail_w_st:
    seek_table_free(st);
fail_w_lock:
    pthread_rwlock_destroy(&reader->lock);
fail_w_dctx:
    LZ4F_freeDecompressionContext(dctx);
fail_w_reader:
    free(reader);
fail:
    return NULL;
}

zseek_reader_t *zseek_reader_open_full(zseek_read_file_t user_file,
    size_t cache_size, void *call_data, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    // Look for magic number in the file
    uint32_t magic_le;
    ssize_t _read = user_file.pread(&magic_le, sizeof(magic_le), 0,
        user_file.user_data, call_data);
    if (_read != (ssize_t)sizeof(magic_le)) {
        if (_read >= 0)
            set_error(errbuf, "unexpected EOF");
        else
            // TODO OPT: Use errno if user_file.pread sets it
            set_error(errbuf, "read file failed");
        return NULL;
    }

    switch (le32toh(magic_le)) {
    case ZSTD_MAGIC:
        return zseek_reader_open_full_zstd(user_file, cache_size, call_data,
            errbuf);
    case LZ4_MAGIC:
        return zseek_reader_open_full_lz4(user_file, cache_size, call_data,
            errbuf);
    default:
        set_error(errbuf, "unrecognized file format");
        return NULL;
    }
}

zseek_reader_t *zseek_reader_open(FILE *cfile, size_t cache_size,
    void *call_data, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_read_file_t user_file = {cfile, default_pread, default_fsize};
    return zseek_reader_open_full(user_file, cache_size, call_data, errbuf);
}

static bool zseek_reader_close_zstd(zseek_reader_t *reader,
    void *call_data __attribute__((unused)), char errbuf[ZSEEK_ERRBUF_SIZE])
{
    (void)call_data;

    bool is_error = false;

    int pr = pthread_rwlock_destroy(&reader->lock);
    if (pr && !is_error) {
        set_error_with_errno(errbuf, "destroy lock", pr);
        is_error = true;
    }

    size_t r = ZSTD_freeDStream(reader->dstream_zstd);
    if (ZSTD_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free dstream", ZSTD_getErrorName(r));
        is_error = true;
    }

    r = ZSTD_freeDCtx(reader->dctx_zstd);
    if (ZSTD_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free context", ZSTD_getErrorName(r));
        is_error = true;
    }

    zseek_buffer_free(reader->dbuf);
    zseek_buffer_free(reader->cbuf);
    zseek_cache_free(reader->cache);
    seek_table_free(reader->st);
    free(reader);

    return !is_error;
}

static bool zseek_reader_close_lz4(zseek_reader_t *reader, void *call_data,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    (void)call_data;

    bool is_error = false;

    int pr = pthread_rwlock_destroy(&reader->lock);
    if (pr && !is_error) {
        set_error_with_errno(errbuf, "destroy lock", pr);
        is_error = true;
    }

    LZ4F_errorCode_t r = LZ4F_freeDecompressionContext(reader->dctx_lz4);
    if (LZ4F_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free context", LZ4F_getErrorName(r));
        is_error = true;
    }

    zseek_buffer_free(reader->dbuf);
    zseek_buffer_free(reader->cbuf);
    zseek_cache_free(reader->cache);
    seek_table_free(reader->st);
    free(reader);

    return !is_error;
}

bool zseek_reader_close(zseek_reader_t *reader, void *call_data,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    if (!reader)
        return true;

    switch (reader->type) {
    case ZSEEK_ZSTD:
        return zseek_reader_close_zstd(reader, call_data, errbuf);
    case ZSEEK_LZ4:
        return zseek_reader_close_lz4(reader, call_data, errbuf);
    default:
        // BUG
        assert(false);
        return false;
    }
}

static ssize_t zseek_pread_zstd_no_cache(zseek_reader_t *reader, void *buf,
    size_t count, size_t offset, void *call_data,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    // TODO OPT: Use the cache, only for reading?

    ssize_t frame_idx = offset_to_frame_idx(reader->st, offset);
    if (frame_idx == -1)
        return 0;

    int pr = pthread_rwlock_wrlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "lock for writing", pr);
        goto fail;
    }

    // Resize compressed buffer
    size_t frame_csize = frame_size_c(reader->st, frame_idx);
    if (!zseek_buffer_resize(reader->cbuf, frame_csize)) {
        set_error(errbuf, "resize compressed buffer");
        goto fail_w_lock;
    }
    void *cbuf_data = zseek_buffer_data(reader->cbuf);
    assert(cbuf_data);
    // Read compressed frame
    off_t frame_offset = frame_offset_c(reader->st, frame_idx);
    ssize_t _read = reader->user_file.pread(cbuf_data, frame_csize,
        (size_t)frame_offset, reader->user_file.user_data, call_data);
    if (_read != (ssize_t)frame_csize) {
        if (_read >= 0)
            set_error(errbuf, "unexpected EOF");
        else
            // TODO OPT: Use errno if user_file.pread sets it
            set_error(errbuf, "read file failed");
        goto fail_w_lock;
    }

    size_t r = ZSTD_initDStream(reader->dstream_zstd);
    if (ZSTD_isError(r)) {
        set_error(errbuf, "%s: %s", "initialize dstream", ZSTD_getErrorName(r));
        goto fail_w_lock;
    }
    // Discard any excess leading data
    ZSTD_inBuffer inb = { .src = cbuf_data, .size = frame_csize };
    size_t offset_in_frame = offset - frame_offset_d(reader->st, frame_idx);
    if (offset_in_frame > 0) {
        // Resize discard buffer
        if (!zseek_buffer_resize(reader->dbuf, offset_in_frame)) {
            set_error(errbuf, "resize discard buffer");
            goto fail_w_lock;
        }
        void *dbuf_data = zseek_buffer_data(reader->dbuf);
        assert(dbuf_data);
        // Decompress discard data
        size_t to_decompress = offset_in_frame;
        ZSTD_outBuffer outb = { .dst = dbuf_data, .size = to_decompress };
        while (outb.pos < outb.size) {
            r = ZSTD_decompressStream(reader->dstream_zstd, &outb, &inb);
            if (ZSTD_isError(r)) {
                set_error(errbuf, "%s: %s", "decompress discard data",
                    ZSTD_getErrorName(r));
                goto fail_w_lock;
            }
        }
    }

    // Decompress user data
    size_t frame_dsize = frame_size_d(reader->st, frame_idx);
    size_t to_decompress = MIN(count, frame_dsize - offset_in_frame);
    ZSTD_outBuffer outb = { .dst = buf, .size = to_decompress };
    while (outb.pos < outb.size) {
        r = ZSTD_decompressStream(reader->dstream_zstd, &outb, &inb);
        if (ZSTD_isError(r)) {
            set_error(errbuf, "%s: %s", "decompress user data",
                ZSTD_getErrorName(r));
            goto fail_w_lock;
        }
    }

    pr = pthread_rwlock_unlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "unlock", pr);
        goto fail;
    }

    return to_decompress;

fail_w_lock:
    pthread_rwlock_unlock(&reader->lock);
fail:
    return -1;
}

static ssize_t zseek_pread_zstd(zseek_reader_t *reader, void *buf, size_t count,
    size_t offset, void *call_data, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    // TODO OPT: Try to return as much as possible (multiple frames), to avoid
    // the repeated fs read and zseek_read overhead?

    if (!reader->cache)
        return zseek_pread_zstd_no_cache(reader, buf, count, offset, call_data,
            errbuf);

    ssize_t frame_idx = offset_to_frame_idx(reader->st, offset);
    if (frame_idx == -1)
        return 0;

    int pr = pthread_rwlock_rdlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "lock for reading", pr);
        goto fail;
    }

    void *dbuf = NULL;
    zseek_frame_t frame = zseek_cache_find(reader->cache, frame_idx);
    if (!frame.data) {
        // Upgrade to write lock
        pr = pthread_rwlock_unlock(&reader->lock);
        if (pr) {
            set_error_with_errno(errbuf, "unlock to upgrade", pr);
            goto fail;
        }
        pr = pthread_rwlock_wrlock(&reader->lock);
        if (pr) {
            set_error_with_errno(errbuf, "lock for writing", pr);
            goto fail;
        }

        frame = zseek_cache_find(reader->cache, frame_idx);
        if (!frame.data) {
            // Resize compressed buffer
            size_t frame_csize = frame_size_c(reader->st, frame_idx);
            if (!zseek_buffer_resize(reader->cbuf, frame_csize)) {
                set_error(errbuf, "resize compressed buffer");
                goto fail_w_lock;
            }
            void *cbuf_data = zseek_buffer_data(reader->cbuf);
            assert(cbuf_data);

            // Read compressed frame
            off_t frame_offset = frame_offset_c(reader->st, frame_idx);
            ssize_t _read = reader->user_file.pread(cbuf_data, frame_csize,
                (size_t)frame_offset, reader->user_file.user_data, call_data);
            if (_read != (ssize_t)frame_csize) {
                if (_read >= 0)
                    set_error(errbuf, "unexpected EOF");
                else
                    // TODO OPT: Use errno if user_file.pread sets it
                    set_error(errbuf, "read file failed");
                goto fail_w_lock;
            }

            // Decompress frame
            size_t frame_dsize = frame_size_d(reader->st, frame_idx);
            dbuf = malloc(frame_dsize);
            if (!dbuf) {
                set_error_with_errno(errbuf, "allocate decompressed buffer",
                    errno);
                goto fail_w_lock;
            }
            size_t r = ZSTD_decompressDCtx(reader->dctx_zstd, dbuf, frame_dsize,
                cbuf_data, frame_csize);
            if (ZSTD_isError(r)) {
                set_error(errbuf, "%s: %s", "decompress frame",
                    ZSTD_getErrorName(r));
                goto fail_w_dbuf;
            }

            // Cache frame
            frame.data = dbuf;
            frame.idx = frame_idx;
            frame.len = frame_dsize;
            if (!zseek_cache_insert(reader->cache, frame)) {
                set_error(errbuf, "frame caching failed");
                goto fail_w_dbuf;
            }
        }
    }

    size_t offset_in_frame = offset - frame_offset_d(reader->st, frame_idx);
    size_t to_copy = MIN(count, frame.len - offset_in_frame);
    memcpy(buf, (uint8_t*)frame.data + offset_in_frame, to_copy);

    pr = pthread_rwlock_unlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "unlock", pr);
        goto fail;
    }

    return to_copy;

fail_w_dbuf:
    free(dbuf);
fail_w_lock:
    pthread_rwlock_unlock(&reader->lock);
fail:
    return -1;
}

static ssize_t zseek_pread_lz4_no_cache(zseek_reader_t *reader, void *buf,
    size_t count, size_t offset, void *call_data,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    // TODO OPT: Use the cache, only for reading?

    ssize_t frame_idx = offset_to_frame_idx(reader->st, offset);
    if (frame_idx == -1)
        return 0;

    int pr = pthread_rwlock_wrlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "lock for writing", pr);
        goto fail;
    }

    // Resize compressed buffer
    size_t frame_csize = frame_size_c(reader->st, frame_idx);
    if (!zseek_buffer_resize(reader->cbuf, frame_csize)) {
        set_error(errbuf, "resize compressed buffer");
        goto fail_w_lock;
    }
    void *cbuf_data = zseek_buffer_data(reader->cbuf);
    assert(cbuf_data);
    // Read compressed frame
    off_t frame_offset = frame_offset_c(reader->st, frame_idx);
    ssize_t _read = reader->user_file.pread(cbuf_data, frame_csize,
        (size_t)frame_offset, reader->user_file.user_data, call_data);
    if (_read != (ssize_t)frame_csize) {
        if (_read >= 0)
            set_error(errbuf, "unexpected EOF");
        else
            // TODO OPT: Use errno if user_file.pread sets it
            set_error(errbuf, "read file failed");
        goto fail_w_lock;
    }

    // Discard any excess leading data
    size_t cbuf_offset = 0;
    size_t offset_in_frame = offset - frame_offset_d(reader->st, frame_idx);
    if (offset_in_frame > 0) {
        // Resize discard buffer
        if (!zseek_buffer_resize(reader->dbuf, offset_in_frame)) {
            set_error(errbuf, "resize discard buffer");
            goto fail_w_lock;
        }
        void *dbuf_data = zseek_buffer_data(reader->dbuf);
        assert(dbuf_data);
        // Decompress discard data
        size_t dbuf_offset = 0;
        size_t to_decompress = offset_in_frame;
        do {
            size_t csize = frame_csize - cbuf_offset;
            size_t dsize = to_decompress - dbuf_offset;
            LZ4F_decompressOptions_t opts = { .stableDst = 0 };
            size_t r = LZ4F_decompress(reader->dctx_lz4,
                (uint8_t*)dbuf_data + dbuf_offset, &dsize,
                (uint8_t*)cbuf_data + cbuf_offset, &csize,
                &opts); // NOTE: Overwrites dsize, csize.
            if (LZ4F_isError(r)) {
                set_error(errbuf, "%s: %s", "decompress discard data",
                    LZ4F_getErrorName(r));
                goto fail_w_lock;
            }
            cbuf_offset += csize;
            dbuf_offset += dsize;
        } while (dbuf_offset < to_decompress);
    }

    // Decompress user data
    size_t buf_offset = 0;
    size_t frame_dsize = frame_size_d(reader->st, frame_idx);
    size_t to_decompress = MIN(count, frame_dsize - offset_in_frame);
    do {
        size_t csize = frame_csize - cbuf_offset;
        size_t dsize = to_decompress - buf_offset;
        LZ4F_decompressOptions_t opts = { .stableDst = 0 };
        size_t r = LZ4F_decompress(reader->dctx_lz4,
            (uint8_t*)buf + buf_offset, &dsize,
            (uint8_t*)cbuf_data + cbuf_offset, &csize,
            &opts); // NOTE: Overwrites dsize, csize.
        if (LZ4F_isError(r)) {
            set_error(errbuf, "%s: %s", "decompress user data",
                LZ4F_getErrorName(r));
            goto fail_w_lock;
        }
        cbuf_offset += csize;
        buf_offset += dsize;
    } while (buf_offset < to_decompress);

    if (cbuf_offset < frame_csize) {
        // Did not consume the whole frame, clean up decompression context
        LZ4F_resetDecompressionContext(reader->dctx_lz4);
    }

    pr = pthread_rwlock_unlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "unlock", pr);
        goto fail;
    }

    return to_decompress;

fail_w_lock:
    pthread_rwlock_unlock(&reader->lock);
fail:
    return -1;
}

static ssize_t zseek_pread_lz4(zseek_reader_t *reader, void *buf, size_t count,
    size_t offset, void *call_data, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    // TODO OPT: Try to return as much as possible (multiple frames), to avoid
    // the repeated fs read and zseek_read overhead?

    if (!reader->cache)
        return zseek_pread_lz4_no_cache(reader, buf, count, offset, call_data,
            errbuf);

    ssize_t frame_idx = offset_to_frame_idx(reader->st, offset);
    if (frame_idx == -1)
        return 0;

    int pr = pthread_rwlock_rdlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "lock for reading", pr);
        goto fail;
    }

    void *dbuf = NULL;
    zseek_frame_t frame = zseek_cache_find(reader->cache, frame_idx);
    if (!frame.data) {
        // Upgrade to write lock
        pr = pthread_rwlock_unlock(&reader->lock);
        if (pr) {
            set_error_with_errno(errbuf, "unlock to upgrade", pr);
            goto fail;
        }
        pr = pthread_rwlock_wrlock(&reader->lock);
        if (pr) {
            set_error_with_errno(errbuf, "lock for writing", pr);
            goto fail;
        }

        frame = zseek_cache_find(reader->cache, frame_idx);
        if (!frame.data) {
            // Resize compressed buffer
            size_t frame_csize = frame_size_c(reader->st, frame_idx);
            if (!zseek_buffer_resize(reader->cbuf, frame_csize)) {
                set_error(errbuf, "resize compressed buffer");
                goto fail_w_lock;
            }
            void *cbuf_data = zseek_buffer_data(reader->cbuf);
            assert(cbuf_data);

            // Read compressed frame
            off_t frame_offset = frame_offset_c(reader->st, frame_idx);
            ssize_t _read = reader->user_file.pread(cbuf_data, frame_csize,
                (size_t)frame_offset, reader->user_file.user_data, call_data);
            if (_read != (ssize_t)frame_csize) {
                if (_read >= 0)
                    set_error(errbuf, "unexpected EOF");
                else
                    // TODO OPT: Use errno if user_file.pread sets it
                    set_error(errbuf, "read file failed");
                goto fail_w_lock;
            }

            // Decompress frame
            size_t frame_dsize = frame_size_d(reader->st, frame_idx);
            dbuf = malloc(frame_dsize);
            if (!dbuf) {
                set_error_with_errno(errbuf, "allocate decompressed buffer",
                    errno);
                goto fail_w_lock;
            }
            size_t cbuf_offset = 0;
            size_t dbuf_offset = 0;
            size_t to_decompress = frame_dsize;
            do {
                size_t csize = frame_csize - cbuf_offset;
                size_t dsize = to_decompress - dbuf_offset;
                LZ4F_decompressOptions_t opts = { .stableDst = 0 };
                // NOTE: In theory, LZ4F_decompress may not finish the whole
                // frame in one call (r > 0). In practice, this does not happen
                // given enough room in the output buffer (e.g. here).
                size_t r = LZ4F_decompress(reader->dctx_lz4,
                    (uint8_t*)dbuf + dbuf_offset, &dsize,
                    (uint8_t*)cbuf_data + cbuf_offset, &csize,
                    &opts); // NOTE: Overwrites dsize, csize.
                if (LZ4F_isError(r)) {
                    set_error(errbuf, "%s: %s", "decompress frame",
                        LZ4F_getErrorName(r));
                    goto fail_w_dbuf;
                }
                cbuf_offset += csize;
                dbuf_offset += dsize;
            } while (dbuf_offset < to_decompress);

            // Cache frame
            frame.data = dbuf;
            frame.idx = frame_idx;
            frame.len = frame_dsize;
            if (!zseek_cache_insert(reader->cache, frame)) {
                set_error(errbuf, "frame caching failed");
                goto fail_w_dbuf;
            }
        }
    }

    size_t offset_in_frame = offset - frame_offset_d(reader->st, frame_idx);
    size_t to_copy = MIN(count, frame.len - offset_in_frame);
    memcpy(buf, (uint8_t*)frame.data + offset_in_frame, to_copy);

    pr = pthread_rwlock_unlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "unlock", pr);
        goto fail;
    }

    return to_copy;

fail_w_dbuf:
    free(dbuf);
fail_w_lock:
    pthread_rwlock_unlock(&reader->lock);
fail:
    return -1;
}

ssize_t zseek_pread(zseek_reader_t *reader, void *buf, size_t count,
    size_t offset, void *call_data, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    if (!reader) {
        set_error(errbuf, "invalid reader");
        return false;
    }

    switch (reader->type) {
    case ZSEEK_ZSTD:
        return zseek_pread_zstd(reader, buf, count, offset, call_data, errbuf);
    case ZSEEK_LZ4:
        return zseek_pread_lz4(reader, buf, count, offset, call_data, errbuf);
    default:
        // BUG
        assert(false);
        return -1;
    }
}

ssize_t zseek_read(zseek_reader_t *reader, void *buf, size_t count,
    void *call_data, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    ssize_t ret = zseek_pread(reader, buf, count, reader->pos, call_data,
        errbuf);
    if (ret > 0)
        reader->pos += ret;

    return ret;
}

bool zseek_reader_stats(zseek_reader_t *reader, zseek_reader_stats_t *stats,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    if (!reader) {
        set_error(errbuf, "invalid reader");
        return false;
    }

    if (!stats) {
        set_error(errbuf, "invalid stats pointer");
        return false;
    }

    int pr = pthread_rwlock_rdlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "lock for reading", pr);
        return false;
    }

    size_t seek_table_memory = seek_table_memory_usage(reader->st);

    size_t frames = seek_table_entries(reader->st);

    size_t decompressed_size = seek_table_decompressed_size(reader->st);

    size_t cache_memory = zseek_cache_memory_usage(reader->cache);

    size_t cached_frames = zseek_cache_entries(reader->cache);

    // NOTE: This is an _estimate_ because the underlying compression lib may
    // buffer too in its context object.
    size_t buffer_size = zseek_buffer_capacity(reader->cbuf);
    buffer_size += zseek_buffer_capacity(reader->dbuf);
    if (reader->type == ZSEEK_ZSTD) {
        buffer_size += ZSTD_sizeof_DCtx(reader->dctx_zstd);
        buffer_size += ZSTD_sizeof_DStream(reader->dstream_zstd);
    }

    pr = pthread_rwlock_unlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "unlock", pr);
        return false;
    }

    *stats = (zseek_reader_stats_t) {
        .seek_table_memory = seek_table_memory,
        .frames = frames,
        .decompressed_size = decompressed_size,
        .cache_memory = cache_memory,
        .cached_frames = cached_frames,
        .buffer_size = buffer_size,
    };

    return true;
}
