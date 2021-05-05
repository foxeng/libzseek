#include <stdbool.h>    // bool
#include <stddef.h>     // size_t
#include <stdint.h>     // uint*_t
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, free
#include <errno.h>      // errno
#include <string.h>     // memset
#include <pthread.h>    // pthread_mutex*

#include <zstd.h>

#include "zseek.h"
#include "seek_table.h"
#include "common.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// TODO: Spin this off into a "proper" type (i.e. add its own methods).
struct zseek_frame_cache {
    void *data;
    size_t frame_idx;
    size_t frame_len;
    size_t capacity;
};

struct zseek_reader {
    FILE *fin;
    ZSTD_DCtx *dctx;    // TODO: If there's contention, use one dctx per read().
    pthread_rwlock_t lock;

    ZSTD_seekTable *st;
    struct zseek_frame_cache cache;   // TODO: Parameterize # of cached frames.
    size_t pos;
};

zseek_reader_t *zseek_reader_open(const char *filename,
    char errbuf[ZSEEK_ERRBUF_SIZE])
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
    FILE *fin = fopen(filename, "rb");
    if (!fin) {
        set_error_with_errno(errbuf, "open file", errno);
        goto fail_w_lock;
    }
    reader->fin = fin;

    ZSTD_seekTable *st = read_seek_table(fin);
    if (!st) {
        set_error(errbuf, "read_seek_table failed");
        goto fail_w_fin;
    }
    reader->st = st;

    return reader;

fail_w_fin:
    fclose(fin);
fail_w_lock:
    pthread_rwlock_destroy(&reader->lock);
fail_w_dctx:
    ZSTD_freeDCtx(dctx);
fail_w_reader:
    free(reader);
fail:
    return NULL;
}

bool zseek_reader_close(zseek_reader_t *reader, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    bool is_error = false;

    if (fclose(reader->fin) == EOF) {
        set_error_with_errno(errbuf, "close file", errno);
        is_error = true;
    }

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

    free(reader->cache.data);
    seek_table_free(reader->st);
    free(reader);

    return !is_error;
}

ssize_t zseek_pread(zseek_reader_t *reader, void *buf, size_t count,
    size_t offset, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    void *cbuf = NULL;  // Declared here to be in scope at fail_w_cbuf

    // TODO: Try to return as much as possible (multiple frames).

    ssize_t frame_idx = offset_to_frame_idx(reader->st, offset);
    if (frame_idx == -1)
        return 0;

    int pr = pthread_rwlock_rdlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "lock for reading", pr);
        goto fail;
    }

    if (!reader->cache.data || reader->cache.frame_idx != (size_t)frame_idx) {
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

        if (!reader->cache.data || reader->cache.frame_idx != (size_t)frame_idx) {
            // Read compressed frame
            size_t cbuf_len = frame_size_c(reader->st, frame_idx);
            cbuf = malloc(cbuf_len);
            if (!cbuf) {
                set_error_with_errno(errbuf, "allocate buffer", errno);
                goto fail_w_lock;
            }
            if (fread(cbuf, 1, cbuf_len, reader->fin) != cbuf_len) {
                if (feof(reader->fin))
                    set_error(errbuf, "unexpected EOF");
                else
                    set_error_with_errno(errbuf, "read file", errno);
                goto fail_w_cbuf;
            }

            // If necessary, resize the cache
            size_t frame_dsize = frame_size_d(reader->st, frame_idx);
            if (reader->cache.capacity < frame_dsize) {
                // TODO OPT: Or free / malloc instead, since we don't care about old data?
                void *new_data = realloc(reader->cache.data, frame_dsize);
                if (!new_data) {
                    set_error_with_errno(errbuf, "grow cache", errno);
                    goto fail_w_cbuf;
                }
                reader->cache.data = new_data;
                reader->cache.capacity = frame_dsize;
            }

            // Decompress and cache frame
            size_t r = ZSTD_decompressDCtx(reader->dctx, reader->cache.data,
                reader->cache.capacity, cbuf, cbuf_len);
            if (ZSTD_isError(r)) {
                set_error(errbuf, "%s: %s", "decompress frame",
                    ZSTD_getErrorName(r));
                goto fail_w_cache;
            }
            reader->cache.frame_idx = frame_idx;
            reader->cache.frame_len = frame_dsize;

            free(cbuf);
        }
    }

    size_t offset_in_frame = offset - frame_offset_d(reader->st, frame_idx);
    size_t to_copy = MIN(count, reader->cache.frame_len - offset_in_frame);
    memcpy(buf, (uint8_t*)reader->cache.data + offset_in_frame, to_copy);

    pr = pthread_rwlock_unlock(&reader->lock);
    if (pr) {
        set_error_with_errno(errbuf, "unlock", pr);
        goto fail;
    }

    return to_copy;

fail_w_cache:
    // Make sure to leave the cache in a consistent state
    reader->cache.data = NULL;
    reader->cache.frame_len = 0;
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
