#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>    // bool
#include <stddef.h>     // size_t

typedef struct zseek_cache zseek_cache_t;

typedef struct {
    void *data;
    size_t idx;
    size_t len;
} zseek_frame_t;

/**
 * Creates a new cache with a capacity of @p capacity frames.
 */
zseek_cache_t *zseek_cache_new(size_t capacity);
/**
 * Frees the cache pointed to by @p cache.
 */
void zseek_cache_free(zseek_cache_t *cache);
/**
 * Searches for the frame at index @p frame_idx in @p cache. If not found,
 * zseek_frame_t.data of the return value will be @a NULL.
 *
 * @attention Not safe to call concurrently (unlocked).
 */
zseek_frame_t zseek_cache_find(zseek_cache_t *cache, size_t frame_idx);
/**
 * Inserts @p frame in @p cache as MRU (most recently used). Might evict LRU.
 * Returns @a false on error.
 *
 * @note Assumes ownership of @p frame.data
 *
 * @attention Not safe to call concurrently (unlocked).
 */
bool zseek_cache_insert(zseek_cache_t *cache, zseek_frame_t frame);
/**
 * Returns the memory usage (total heap allocation) of @p cache in bytes.
 */
size_t zseek_cache_memory_usage(const zseek_cache_t *cache);
/**
 * Returns the number of frames currently cached in @p cache.
 */
size_t zseek_cache_entries(const zseek_cache_t *cache);

#endif  // CACHE_H
