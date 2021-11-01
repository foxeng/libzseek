#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>     // size_t
#include <stdint.h>     // uint*_t
#include <stdbool.h>    // bool

typedef struct zseek_buffer zseek_buffer_t;

/**
 * Creates a new buffer with a capacity of at least @p capacity bytes.
 */
zseek_buffer_t *zseek_buffer_new(size_t capacity);

/**
 * Frees the buffer pointed to by @p buffer.
 */
void zseek_buffer_free(zseek_buffer_t *buffer);

/**
 * Returns the current size of @p buffer in bytes.
 *
 * @attention Not safe to call concurrently (unlocked).
 */
size_t zseek_buffer_size(zseek_buffer_t *buffer);

/**
 * Returns the current capacity of @p buffer in bytes.
 *
 * @attention Not safe to call concurrently (unlocked).
 */
size_t zseek_buffer_capacity(zseek_buffer_t *buffer);

/**
 * Returns a pointer to the underlying data of @p buffer.
 *
 * @attention Not safe to call concurrently (unlocked).
 */
void *zseek_buffer_data(zseek_buffer_t *buffer);

/**
 * Pushes @p len bytes from @p data to the end of @p buffer.
 * Returns @a false on error.
 *
 * @note Copies from @p data
 *
 * @attention Not safe to call concurrently (unlocked).
 */
bool zseek_buffer_push(zseek_buffer_t *buffer, const void *data, size_t len);

/**
 * Ensure the capacity of @p buffer is >= @p capacity.
 * Returns @a false on error.
 *
 * @note Capacity is not changed if @p capacity is less than current capacity
 *
 * @attention Not safe to call concurrently (unlocked).
 */
bool zseek_buffer_reserve(zseek_buffer_t *buffer, size_t capacity);

/**
 * Resizes @p buffer to @p size bytes.
 * If @p size is greater than current size, the additional space is
 * zero-initialized. If @p size is less than current size, the first @p size
 * bytes are kept.
 *
 * Returns @a false on error.
 *
 * @note Capacity is not changed if @p size if less than current size
 *
 * @attention Not safe to call concurrently (unlocked).
 */
bool zseek_buffer_resize(zseek_buffer_t *buffer, size_t size);

/**
 * Resets @p buffer to zero-size.
 *
 * @note Capacity is not changed
 *
 * @attention Not safe to call concurrently (unlocked).
 */
void zseek_buffer_reset(zseek_buffer_t *buffer);

#endif  // BUFFER_H
