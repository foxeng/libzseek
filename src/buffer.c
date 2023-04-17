#include <string.h>     // memmove

#include "buffer.h"

struct zseek_buffer {
    zseek_mm_t *mm;
    void *data;
    size_t size;
    size_t capacity;
};

zseek_buffer_t *zseek_buffer_new(zseek_mm_t *mm, size_t capacity)
{
    zseek_buffer_t *buffer = mm->malloc(sizeof(*buffer), true, mm->user_data);
    if (!buffer)
        goto cleanup;

    buffer->mm = mm;
    if (!zseek_buffer_reserve(buffer, capacity))
        goto cleanup;

    return buffer;

cleanup:
    mm->free(buffer, mm->user_data);
    return NULL;
}

void zseek_buffer_free(zseek_buffer_t *buffer)
{
    if (!buffer)
        return;

    zseek_mm_t *mm = buffer->mm;
    mm->free(buffer->data, mm->user_data);
    mm->free(buffer, mm->user_data);
}

size_t zseek_buffer_size(zseek_buffer_t *buffer)
{
    if (!buffer)
        return 0;

    return buffer->size;
}

size_t zseek_buffer_capacity(zseek_buffer_t *buffer)
{
    if (!buffer)
        return 0;

    return buffer->capacity;
}

void *zseek_buffer_data(zseek_buffer_t *buffer)
{
    if (!buffer)
        return NULL;

    return buffer->data;
}

bool zseek_buffer_push(zseek_buffer_t *buffer, const void *data, size_t len)
{
    if (!buffer)
        return false;

    if (!data)
        return len == 0;

    size_t new_size = buffer->size + len;
    if (!zseek_buffer_reserve(buffer, new_size))
        return false;

    memmove((uint8_t*)buffer->data + buffer->size, data, len);
    buffer->size = new_size;

    return true;
}

bool zseek_buffer_reserve(zseek_buffer_t *buffer, size_t capacity)
{
    if (!buffer)
        return false;

    if (capacity <= buffer->capacity)
        return true;

    // TODO OPT: Use different reallocation strategy? (always power of 2?)
    size_t new_capacity = 2 * buffer->capacity;
    if (capacity > new_capacity)
        new_capacity = capacity;

    void *new_data = buffer->mm->realloc(buffer->data, new_capacity,
        buffer->mm->user_data);
    if (!new_data)
        return false;
    buffer->data = new_data;
    buffer->capacity = new_capacity;

    return true;
}

bool zseek_buffer_resize(zseek_buffer_t *buffer, size_t size)
{
    if (!buffer)
        return false;

    if (!zseek_buffer_reserve(buffer, size))
        return false;

    // TODO OPT: zero-init new memory, if new size > old size, in debug builds?

    buffer->size = size;

    return true;
}

void zseek_buffer_reset(zseek_buffer_t *buffer)
{
    if (!buffer)
        return;

    buffer->size = 0;
}
