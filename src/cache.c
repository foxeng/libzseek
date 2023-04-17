#include <assert.h>     // assert

#include "uthash.h"
#undef uthash_malloc
#undef uthash_free
#define uthash_malloc(sz) cache->mm->malloc(sz, false, cache->mm->user_data)
#define uthash_free(ptr, sz) cache->mm->free(ptr, cache->mm->user_data)

#include "cache.h"

typedef struct zseek_hm_item {
    zseek_frame_t frame;
    UT_hash_handle hh;
} zseek_hm_item_t;

struct zseek_cache {
    // TODO OPT: Lock independently from zseek_reader

    // HashMap doubling as insertion-order list
    zseek_hm_item_t *map;

    zseek_mm_t *mm;
    size_t size;
    size_t capacity;
    size_t entries_memory;
};

zseek_cache_t *zseek_cache_new(zseek_mm_t *mm, size_t capacity)
{
    if (capacity == 0)
        return NULL;

    zseek_cache_t *cache = mm->malloc(sizeof(*cache), true, mm->user_data);
    if (!cache)
        return NULL;

    cache->capacity = capacity;
    cache->mm = mm;

    return cache;
}

void zseek_cache_free(zseek_cache_t *cache)
{
    if (!cache)
        return;

    zseek_hm_item_t *item;
    zseek_hm_item_t *tmp;
    HASH_ITER(hh, cache->map, item, tmp) {
        HASH_DELETE(hh, cache->map, item);
        cache->mm->free(item->frame.data, cache->mm->user_data);
        cache->mm->free(item, cache->mm->user_data);
    }

    zseek_mm_t *mm = cache->mm;
    mm->free(cache, mm->user_data);
}

zseek_frame_t zseek_cache_find(zseek_cache_t *cache, size_t frame_idx)
{
    if (!cache)
        return (zseek_frame_t){NULL, 0, 0};

    zseek_hm_item_t *found = NULL;
    HASH_FIND(hh, cache->map, &frame_idx, sizeof(frame_idx), found);
    if (!found)
        return (zseek_frame_t){NULL, 0, 0};

    // Re-insert, to make MRU
    zseek_hm_item_t *replaced = NULL;
    HASH_REPLACE(hh, cache->map, frame.idx, sizeof(cache->map->frame.idx),
        found, replaced);
    assert(replaced == found);

    return found->frame;
}

bool zseek_cache_insert(zseek_cache_t *cache, zseek_frame_t frame)
{
    if (!cache)
        return false;

    zseek_hm_item_t *existing = NULL;
    HASH_FIND(hh, cache->map, &frame.idx, sizeof(frame.idx), existing);
    if (existing)
        return false;

    zseek_hm_item_t *lru = NULL;
    if (cache->size == cache->capacity) {
        // Evict LRU (by definition the first in the HashMap, which is sorted
        // by insertion order)
        lru = cache->map;
        HASH_DELETE(hh, cache->map, lru);
        cache->size--;
        cache->entries_memory -= lru->frame.len;
        cache->mm->free(lru->frame.data, cache->mm->user_data);
    }

    // Insert to HashMap
    zseek_hm_item_t *new = lru ? lru : cache->mm->malloc(sizeof(*new), false,
        cache->mm->user_data);
    if (!new)
        return false;
    new->frame = frame;
    HASH_ADD(hh, cache->map, frame.idx, sizeof(cache->map->frame.idx), new);

    cache->size++;
    cache->entries_memory += frame.len;

    return true;
}

size_t zseek_cache_memory_usage(const zseek_cache_t *cache)
{
    if (!cache)
        return 0;

    return sizeof(*cache) +
        cache->size * sizeof(*(cache->map)) +
        HASH_OVERHEAD(hh, cache->map) +
        cache->entries_memory;
}

size_t zseek_cache_entries(const zseek_cache_t *cache)
{
    if (!cache)
        return 0;

    return cache->size;
}
