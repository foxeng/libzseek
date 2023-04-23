#include <stdlib.h>     // malloc, free
#include <string.h>     // memset

#include <search.h>     // insque, remque, tsearch

#include "cache.h"

struct zseek_cached_frame {
    struct zseek_cached_frame *next;
    struct zseek_cached_frame *prev;

    zseek_frame_t frame;
};
typedef struct zseek_cached_frame zseek_cached_frame_t;

struct zseek_cache {
    // TODO OPT: Lock independently from zseek_reader

    // BST with nodes linking to the list below
    void *root;

    // Linked list containing the actual cache entries (head is LRU, tail is
    // MRU)
    zseek_cached_frame_t *head;
    zseek_cached_frame_t *tail;

    size_t size;
    size_t capacity;
    size_t entries_memory;
};

static int compare(const void *pa, const void *pb)
{
    const zseek_cached_frame_t *a = pa;
    const zseek_cached_frame_t *b = pb;

    if (a->frame.idx < b->frame.idx)
        return -1;
    if (a->frame.idx > b->frame.idx)
        return 1;
    return 0;
}

static void free_node(void *nodep)
{
    // Free corresponding list element
    zseek_cached_frame_t *f = nodep;
    free(f->frame.data);
    free(f);
}

static void evict_lru(zseek_cache_t *cache)
{
    // Remove from list
    zseek_cached_frame_t *lru = cache->head;
    if (!lru)
        return;
    if (cache->head == cache->tail) {
        // Single item
        cache->head = NULL;
        cache->tail = NULL;
    } else {
        cache->head = cache->head->next;
        remque(lru);
    }

    // Remove from BST
    (void*)tdelete(lru, &cache->root, compare);

    cache->size--;
    cache->entries_memory -= lru->frame.len;

    free(lru->frame.data);
    free(lru);
}

static void make_mru(zseek_cache_t *cache, zseek_cached_frame_t *f)
{
    remque(f);

    if (!cache->tail)
        cache->head = f;
    insque(f, cache->tail);
    cache->tail = f;
}

zseek_cache_t *zseek_cache_new(size_t capacity)
{
    if (capacity == 0)
        return NULL;

    zseek_cache_t *cache = malloc(sizeof(*cache));
    if (!cache)
        return NULL;
    memset(cache, 0, sizeof(*cache));

    cache->capacity = capacity;

    return cache;
}

void zseek_cache_free(zseek_cache_t *cache)
{
    if (!cache)
        return;

    tdestroy(cache->root, free_node);

    free(cache);
}

zseek_frame_t zseek_cache_find(zseek_cache_t *cache, size_t frame_idx)
{
    if (!cache)
        return (zseek_frame_t){NULL, 0, 0};

    zseek_cached_frame_t key;
    key.frame.idx = frame_idx;
    zseek_cached_frame_t **found = tfind(&key, &cache->root, compare);
    if (!found)
        return (zseek_frame_t){NULL, 0, 0};

    make_mru(cache, *found);

    return (*found)->frame;
}

bool zseek_cache_insert(zseek_cache_t *cache, zseek_frame_t frame)
{
    if (!cache)
        return false;

    if (cache->size == cache->capacity)
        evict_lru(cache);

    // Insert to BST
    zseek_cached_frame_t *f = malloc(sizeof(*f));
    if (!f)
        goto cleanup;
    f->frame = frame;
    if (!tsearch(f, &cache->root, compare))
        goto cleanup;

    // Insert to list
    if (!cache->tail)
        cache->head = f;
    insque(f, cache->tail);
    cache->tail = f;

    cache->size++;
    cache->entries_memory += frame.len;

    return true;

cleanup:
    free(f);
    return false;
}

size_t zseek_cache_memory_usage(const zseek_cache_t *cache)
{
    if (!cache)
        return 0;

    return sizeof(*cache) + cache->size * sizeof(*(cache->head)) +
        cache->entries_memory;
}

size_t zseek_cache_entries(const zseek_cache_t *cache)
{
    if (!cache)
        return 0;

    return cache->size;
}
