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
};

zseek_reader_t *zseek_reader_open(const char *filename,
    char errbuf[ZSEEK_ERRBUF_SIZE])
{
    zseek_reader_t *reader = NULL;
    ZSTD_DCtx *dctx = NULL;
    int pr = 0;
    FILE *fin = NULL;
    ZSTD_seekTable *st = NULL;

    (void)errbuf;

    reader = malloc(sizeof(*reader));
    if (!reader) {
        // TODO: Return in errbuf instead.
        perror("zseek_reader_open: allocate reader");
        goto fail;
    }
    memset(reader, 0, sizeof(*reader));

    dctx = ZSTD_createDCtx();
    if (!dctx) {
        // TODO: Return in errbuf instead.
        perror("zseek_reader_open: create context");
        goto fail_w_reader;
    }
    reader->dctx = dctx;

    pr = pthread_rwlock_init(&reader->lock, NULL);
    if (pr) {
        // TODO: Return in errbuf instead.
        fprintf(stderr, "zseek_reader_open: initialize mutex: %s\n",
            strerror(pr));
        goto fail_w_dctx;
    }
    fin = fopen(filename, "rb");
    if (!fin) {
        // TODO: Return in errbuf instead.
        perror("zseek_reader_open: open file");
        goto fail_w_lock;
    }
    reader->fin = fin;

    st = read_seek_table(fin);
    if (!st) {
        // TODO: Return in errbuf instead.
        fprintf(stderr, "zseek_reader_open: read seek table failed\n");
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
    int pr = 0;
    size_t r = 0;

    (void)errbuf;

    if (fclose(reader->fin) == EOF) {
        // TODO: Return in errbuf instead.
        perror("zseek_reader_close: close file");
        return false;
    }

    pr = pthread_rwlock_destroy(&reader->lock);
    if (pr) {
        // TODO: Return in errbuf instead.
        fprintf(stderr, "zseek_raeder_close: destroy mutex: %s\n",
            strerror(pr));
        return false;
    }

    r = ZSTD_freeDCtx(reader->dctx);
    if (ZSTD_isError(r)) {
        // TODO: Return in errbuf instead.
        fprintf(stderr, "zseek_reader_close: free context: %s\n",
            ZSTD_getErrorName(r));
        return false;
    }

    free(reader->cache.data);
    seek_table_free(reader->st);
    free(reader);

    return true;
}

ssize_t zseek_pread(zseek_reader_t *reader, void *buf, size_t count,
    size_t offset, char errbuf[ZSEEK_ERRBUF_SIZE])
{
    ssize_t frame_idx = 0;
    int pr = 0;
    void *cbuf = NULL; // Declared here to be in scope at fail_w_cbuf
    size_t offset_in_frame = 0;
    size_t to_copy = 0;

    (void)errbuf;

    // TODO: Try to return as much as possible (multiple frames).

    frame_idx = offset_to_frame_idx(reader->st, offset);
    if (frame_idx == -1)
        return 0;

    pr = pthread_rwlock_rdlock(&reader->lock);
    if (pr) {
        // TODO: Return in errbuf instead.
        fprintf(stderr, "zseek_pread: lock for reading: %s\n", strerror(pr));
        goto fail;
    }

    if (!reader->cache.data || reader->cache.frame_idx != (size_t)frame_idx) {
        // Upgrade to write lock
        pr = pthread_rwlock_unlock(&reader->lock);
        if (pr) {
            // TODO: Return in errbuf instead.
            fprintf(stderr, "zseek_pread: unlock while upgrading: %s\n",
                strerror(pr));
            goto fail;
        }
        pr = pthread_rwlock_wrlock(&reader->lock);
        if (pr) {
            // TODO: Return in errbuf instead.
            fprintf(stderr, "zseek_pread: lock for writing: %s\n",
                strerror(pr));
            goto fail;
        }

        if (!reader->cache.data || reader->cache.frame_idx != (size_t)frame_idx) {
            size_t cbuf_len = 0;
            size_t frame_dsize = 0;
            size_t r = 0;

            // Read compressed frame
            cbuf_len = frame_size_c(reader->st, frame_idx);
            cbuf = malloc(cbuf_len);
            if (!cbuf) {
                // TODO: Return in errbuf instead.
                perror("zseek_pread: allocate buffer");
                goto fail_w_lock;
            }
            if (fread(cbuf, 1, cbuf_len, reader->fin) != cbuf_len) {
                if (feof(reader->fin))
                    // TODO: Return in errbuf instead.
                    fprintf(stderr, "zseek_pread: unexpected EOF\n");
                else
                    // TODO: Return in errbuf instead.
                    perror("zseek_pread: file read failed\n");
                goto fail_w_cbuf;
            }

            // If necessary, resize the cache
            frame_dsize = frame_size_d(reader->st, frame_idx);
            if (reader->cache.capacity < frame_dsize) {
                // TODO OPT: Or free / malloc instead, since we don't care about old data?
                void *new_data = realloc(reader->cache.data, frame_dsize);
                if (!new_data) {
                    // TODO: Return in errbuf instead.
                    perror("zseek_pread: grow cache");
                    goto fail_w_cbuf;
                }
                reader->cache.data = new_data;
                reader->cache.capacity = frame_dsize;
            }

            // Decompress and cache frame
            r = ZSTD_decompressDCtx(reader->dctx, reader->cache.data,
                reader->cache.capacity, cbuf, cbuf_len);
            if (ZSTD_isError(r)) {
                // TODO: Return in errbuf instead.
                fprintf(stderr, "zseek_pread: decompress frame: %s\n",
                    ZSTD_getErrorName(r));
                goto fail_w_cache;
            }
            reader->cache.frame_idx = frame_idx;
            reader->cache.frame_len = frame_dsize;

            free(cbuf);
        }
    }

    offset_in_frame = offset - frame_offset_d(reader->st, frame_idx);
    to_copy = MIN(count, reader->cache.frame_len - offset_in_frame);
    memcpy(buf, (uint8_t*)reader->cache.data + offset_in_frame, to_copy);

    pr = pthread_rwlock_unlock(&reader->lock);
    if (pr) {
        // TODO: Return in errbuf instead.
        fprintf(stderr, "zseek_pread: unlock: %s\n", strerror(pr));
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
