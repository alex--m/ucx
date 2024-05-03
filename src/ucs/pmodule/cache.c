/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See object LICENSE for terms.
 */

#include "cache.h"

#include <ucs/datastruct/queue.h>
#include <ucs/debug/memtrack_int.h>
#include <ucs/debug/log.h>

#ifdef ENABLE_STATS
/**
 * Cache statistics
 */
enum {
    UCS_PMODULE_CACHE_STAT_HITS,
    UCS_PMODULE_CACHE_STAT_MISSES,
    UCS_PMODULE_CACHE_STAT_OBJECT_MAX,
    UCS_PMODULE_CACHE_STAT_OBJECT_COUNT,
    UCS_PMODULE_CACHE_STAT_LOOKUP_SUM,
    UCS_PMODULE_CACHE_STAT_LOOKUP_COUNT,

    UCS_PMODULE_CACHE_STAT_LAST
};

static ucs_stats_class_t ucs_pmodule_cache_stats_class = {
    .name           = "ucs_pmodule_cache",
    .num_counters   = UCS_PMODULE_CACHE_STAT_LAST,
    .counter_names  = {
        [UCS_PMODULE_CACHE_STAT_HITS]         = "cache_hits",
        [UCS_PMODULE_CACHE_STAT_MISSES]       = "cache_misses",
        [UCS_PMODULE_CACHE_STAT_OBJECT_MAX]   = "objects_max",
        [UCS_PMODULE_CACHE_STAT_OBJECT_COUNT] = "objects_count"
    }
};
#endif /* ENABLE_STATS */

ucs_status_t ucs_pmodule_cache_init(unsigned hash_max, unsigned evict_thresh,
                                    ucs_pmodule_cache_evict_cb_f evict_cb
                                    UCS_STATS_ARG(ucs_stats_node_t *parent),
                                    ucs_pmodule_cache_t *cache)
{
    unsigned i;
    ucs_status_t status = UCS_STATS_NODE_ALLOC(&cache->stats,
                                               &ucs_pmodule_cache_stats_class,
                                               parent, "-%p", cache);
    if (status != UCS_OK) {
        return status;
    }

    cache->evict_cb     = evict_cb;
    cache->evict_thresh = evict_thresh;
    cache->table_size   = hash_max;
    cache->elem_count   = 0;
    cache->hash_table   = ucs_malloc(hash_max * sizeof(ucs_list_link_t),
                                     "pmodule_cache");
    if (cache->hash_table == NULL) {
        UCS_STATS_NODE_FREE(cache->stats);
        return UCS_ERR_NO_MEMORY;
    }

    for (i = 0; i < hash_max; i++) {
        ucs_list_head_init(&cache->hash_table[i]);
    }

#if ENABLE_MT
    ucs_recursive_spinlock_init(&cache->lock, 0);
#endif

    return UCS_OK;
}

void ucs_pmodule_cache_cleanup(ucs_pmodule_cache_t *cache)
{
    unsigned i;
    ucs_pmodule_cached_t *cached;

    for (i = 0; i < cache->table_size; ++i) {
        while (!ucs_list_is_empty(&cache->hash_table[i])) {
            cached = ucs_list_extract_head(&cache->hash_table[i],
                                           ucs_pmodule_cached_t, list);
            cache->evict_cb(cached);
        }
    }

#if ENABLE_MT
    ucs_recursive_spinlock_destroy(&cache->lock);
#endif

    UCS_STATS_NODE_FREE(cache->stats);
    ucs_free(cache->hash_table);
}

static inline void ucs_pmodule_cache_evict(ucs_pmodule_cache_t *cache,
                                           unsigned hash)
{
    ucs_pmodule_cached_t *cached;
    unsigned i, length;
    unsigned longest_hash = hash;
    unsigned longest_hash_length = ucs_list_length(&cache->hash_table[hash]);

    /* Find the hash value with the longest list */
    for (i = 0; i < cache->table_size; i++) {
        length = ucs_list_length(&cache->hash_table[i]);
        if (longest_hash_length < length) {
            longest_hash_length = length;
            longest_hash        = i;
        }
    }

    /* Evict the cached object at the end of that list (~LRU) */
    ucs_assert(!ucs_list_is_empty(&cache->hash_table[longest_hash]));
    cached = ucs_list_tail(&cache->hash_table[longest_hash],
                           ucs_pmodule_cached_t, list);
    cache->evict_cb(cached);
}

void ucs_pmodule_cache_add(ucs_pmodule_cache_t *cache,
                           ucs_pmodule_cached_t *key,
                           unsigned hash)
{
    UCS_PMODULE_CACHE_THREAD_CS_ENTER(cache)

    ucs_list_add_head(&cache->hash_table[hash], &key->list);
    ucs_assert_always((uintptr_t)key % UCS_SYS_CACHE_LINE_SIZE == 0);

    UCS_STATS_UPDATE_COUNTER(cache->stats,
                             UCS_PMODULE_CACHE_STAT_OBJECT_COUNT, 1);
    UCS_STATS_UPDATE_MAX(cache->stats,
                         UCS_PMODULE_CACHE_STAT_OBJECT_MAX,
                         cache->elem_count);

    if (cache->evict_thresh && (cache->elem_count == cache->evict_thresh)) {
        ucs_pmodule_cache_evict(cache, hash);
        ucs_assert(cache->elem_count == (cache->evict_thresh - 1));
    }

    cache->elem_count++;

    UCS_PMODULE_CACHE_THREAD_CS_EXIT(cache)
}

void ucs_pmodule_cache_del(ucs_pmodule_cache_t *cache,
                           ucs_pmodule_cached_t *key)
{
    ucs_assert(cache->elem_count > 0);
    cache->elem_count--;
    ucs_list_del(&key->list);
}

void ucs_pmodule_cache_report_lookup(ucs_pmodule_cache_t *cache,
                                     int is_cache_hit, unsigned lookup_cnt)
{
    UCS_STATS_UPDATE_COUNTER(cache->stats,
                             UCS_PMODULE_CACHE_STAT_LOOKUP_SUM, lookup_cnt);
    UCS_STATS_UPDATE_COUNTER(cache->stats,
                             UCS_PMODULE_CACHE_STAT_LOOKUP_COUNT, 1);
    UCS_STATS_UPDATE_COUNTER(cache->stats,
                             is_cache_hit ? UCS_PMODULE_CACHE_STAT_HITS :
                                            UCS_PMODULE_CACHE_STAT_MISSES,
                             1);
}