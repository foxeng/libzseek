#include <stddef.h>     // size_t
#include <stdlib.h>     // malloc, free
#include <string.h>     // memset
#include <assert.h>     // assert

#include "buffer.h"
#include "buffer_pool.h"

struct zseek_buffer_pool {
    size_t capacity;
    zseek_buffer_t *buffers[];
};

zseek_buffer_pool_t *zseek_buffer_pool_new(size_t capacity)
{
    size_t buffers_size = capacity * sizeof(zseek_buffer_t*);
    size_t alloc_size = sizeof(zseek_buffer_pool_t) + buffers_size;
    zseek_buffer_pool_t *buffer_pool = malloc(alloc_size);
    if (!buffer_pool)
        return NULL;
    buffer_pool->capacity = capacity;
    memset(buffer_pool->buffers, 0, buffers_size);

    return buffer_pool;
}

void zseek_buffer_pool_free(zseek_buffer_pool_t *buffer_pool)
{
    if (!buffer_pool)
        return;

    for (size_t i = 0; i < buffer_pool->capacity; i++)
        zseek_buffer_free(buffer_pool->buffers[i]);

    free(buffer_pool);
}

zseek_buffer_t *zseek_buffer_pool_get(zseek_buffer_pool_t *buffer_pool,
    size_t capacity)
{
    if (!buffer_pool)
        return NULL;

    // Search for existing buffer
    // NOTE: We don't expect to handle many buffers, scanning is fine
    int buffer_idx = -1;
    size_t buffer_capacity = 0;
    for (size_t i = 0; i < buffer_pool->capacity; i++) {
        zseek_buffer_t *buf = buffer_pool->buffers[i];
        if (!buf)
            continue;
        size_t buf_capacity = zseek_buffer_capacity(buf);

        if (buffer_idx == -1) {
            // Any buffer is better than no buffer
            buffer_idx = i;
            buffer_capacity = buf_capacity;
            continue;
        }

        assert(buffer_idx >= 0);
        if ((buffer_capacity < capacity && buf_capacity > buffer_capacity) ||
            (buffer_capacity > capacity && buf_capacity >= capacity &&
            buf_capacity < buffer_capacity)) {
            // A buffer with capacity closer to the requested is better
            buffer_idx = i;
            buffer_capacity = buf_capacity;
        }
    }

    if (buffer_idx >= 0) {
        // Found a buffer
        zseek_buffer_t *buffer = buffer_pool->buffers[buffer_idx];
        if (buffer_capacity < capacity) {
            if (!zseek_buffer_reserve(buffer, capacity))
                return NULL;
        }
        buffer_pool->buffers[buffer_idx] = NULL;
        return buffer;
    }

    assert(buffer_idx < 0);
    zseek_buffer_t *buffer = zseek_buffer_new(capacity);
    if (!buffer)
        return NULL;

    return buffer;
}

void zseek_buffer_pool_ret(zseek_buffer_pool_t *buffer_pool,
    zseek_buffer_t *buffer)
{
    if (!buffer_pool)
        return;

    // Search for a free slot to cache buffer
    for (size_t i = 0; i < buffer_pool->capacity; i++) {
        if (!buffer_pool->buffers[i]) {
            buffer_pool->buffers[i] = buffer;
            return;
        }
    }

    zseek_buffer_free(buffer);
}
