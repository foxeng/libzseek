#include <stdbool.h>    // bool
#include <stddef.h>     // size_t
#include <stdint.h>     // uint*_t
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, free
#include <errno.h>      // errno
#include <string.h>     // memset
#include <pthread.h>    // pthread_mutex*

#include <sys/stat.h>   // fstat
#include <zstd.h>

#include "zseek.h"
#include "seek_table.h"
#include "common.h"
#include "cache.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct zseek_reader {
    zseek_read_file_t user_file;
    ZSTD_DCtx *dctx;    // TODO: If there's contention, use one dctx per read().
    pthread_rwlock_t lock;

    ZSTD_seekTable *st;
    zseek_cache_t *cache;
    size_t pos;
};

static ssize_t default_pread(void *data, size_t size, size_t offset,
    void *user_data)
{
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

static ssize_t default_fsize(void *user_data)
{
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

zseek_reader_t *zseek_reader_open_full(zseek_read_file_t user_file,
    size_t cache_size, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_reader_t *reader = malloc(sizeof(*reader));
    if (!reader) {
        set_error_with_errno(errbuf, "allocate reader", errno);
        goto fail;
    }
    memset(reader, 0, sizeof(*reader));

    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!dctx) {
        set_error(errbuf, "context creation failed");
        goto fail_w_reader;
    }
    reader->dctx = dctx;

    int pr = pthread_rwlock_init(&reader->lock, NULL);
    if (pr) {
        set_error_with_errno(errbuf, "initialize lock", pr);
        goto fail_w_dctx;
    }

    reader->user_file = user_file;

    ZSTD_seekTable *st = read_seek_table(user_file);
    if (!st) {
        set_error(errbuf, "read_seek_table failed");
        goto fail_w_lock;
    }
    reader->st = st;

    zseek_cache_t *cache = zseek_cache_new(cache_size);
    if (!cache) {
        set_error(errbuf, "cache creation failed");
        goto fail_w_st;
    }
    reader->cache = cache;

    return reader;

fail_w_st:
    seek_table_free(st);
fail_w_lock:
    pthread_rwlock_destroy(&reader->lock);
fail_w_dctx:
    ZSTD_freeDCtx(dctx);
fail_w_reader:
    free(reader);
fail:
    return NULL;
}

zseek_reader_t *zseek_reader_open(FILE *cfile, size_t cache_size,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_read_file_t user_file = {cfile, default_pread, default_fsize};
    return zseek_reader_open_full(user_file, cache_size, errbuf);
}

bool zseek_reader_close(zseek_reader_t *reader, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    bool is_error = false;

    int pr = pthread_rwlock_destroy(&reader->lock);
    if (pr && !is_error) {
        set_error_with_errno(errbuf, "destroy lock", pr);
        is_error = true;
    }

    size_t r = ZSTD_freeDCtx(reader->dctx);
    if (ZSTD_isError(r) && !is_error) {
        set_error(errbuf, "%s: %s", "free context", ZSTD_getErrorName(r));
        is_error = true;
    }

    zseek_cache_free(reader->cache);
    seek_table_free(reader->st);
    free(reader);

    return !is_error;
}

ssize_t zseek_pread(zseek_reader_t *reader, void *buf, size_t count,
    size_t offset, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    // TODO: Try to return as much as possible (multiple frames).

    ssize_t frame_idx = offset_to_frame_idx(reader->st, offset);
    if (frame_idx == -1)
        return 0;

    int pr = pthread_rwlock_rdlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "lock for reading", pr);
        goto fail;
    }

    void *cbuf = NULL;
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
            // Read compressed frame
            size_t frame_csize = frame_size_c(reader->st, frame_idx);
            cbuf = malloc(frame_csize);
            if (!cbuf) {
                set_error_with_errno(errbuf, "allocate compressed buffer",
                    errno);
                goto fail_w_lock;
            }
            off_t frame_offset = frame_offset_c(reader->st, frame_idx);
            ssize_t _read = reader->user_file.pread(cbuf, frame_csize,
                (size_t)frame_offset, reader->user_file.user_data);
            if (_read != (ssize_t)frame_csize) {
                if (_read >= 0)
                    set_error(errbuf, "unexpected EOF");
                else
                    // TODO OPT: Use errno if user_file.pread sets it
                    set_error(errbuf, "read file failed");
                goto fail_w_cbuf;
            }

            // Decompress frame
            size_t frame_dsize = frame_size_d(reader->st, frame_idx);
            dbuf = malloc(frame_dsize);
            if (!dbuf) {
                set_error_with_errno(errbuf, "allocate decompressed buffer",
                    errno);
                goto fail_w_cbuf;
            }
            size_t r = ZSTD_decompressDCtx(reader->dctx, dbuf, frame_dsize,
                cbuf, frame_csize);
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

            free(cbuf);
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
fail_w_cbuf:
    free(cbuf);
fail_w_lock:
    pthread_rwlock_unlock(&reader->lock);
fail:
    return -1;
}

ssize_t zseek_read(zseek_reader_t *reader, void *buf, size_t count,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    ssize_t ret = zseek_pread(reader, buf, count, reader->pos, errbuf);
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
    };

    return true;
}
