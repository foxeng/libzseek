#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <stddef.h>     // size_t

#include "buffer.h"

typedef struct zseek_buffer_pool zseek_buffer_pool_t;

/**
 * Creates a new buffer pool with a capacity of @p capacity buffers.
 */
zseek_buffer_pool_t *zseek_buffer_pool_new(size_t capacity);

/**
 * Frees the buffer pool pointed to by @p buffer_pool.
 */
void zseek_buffer_pool_free(zseek_buffer_pool_t *buffer_pool);

/**
 * Returns a buffer with a capacity of at least @p capacity bytes, potentially
 * creating it first.
 *
 * @attention Not safe to call concurrently (unlocked).
 */
zseek_buffer_t *zseek_buffer_pool_get(zseek_buffer_pool_t *buffer_pool,
    size_t capacity);

/**
 * Returns @p buffer to the pool, potentially freeing it.
 *
 * @attention Not safe to call concurrently (unlocked).
 */
void zseek_buffer_pool_ret(zseek_buffer_pool_t *buffer_pool,
    zseek_buffer_t *buffer);

#endif  // BUFFER_POOL_H
