/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCS_PMODULE_CACHE_H_
#define UCS_PMODULE_CACHE_H_

#include <ucs/arch/cpu.h>
#include <ucs/stats/stats.h>
#include <ucs/type/spinlock.h>
#include <ucs/datastruct/list.h>

#if ENABLE_MT
#define UCS_PMODULE_CACHE_THREAD_CS_ENTER(_obj) \
    ucs_recursive_spin_lock(&(_obj)->lock);
#define UCS_PMODULE_CACHE_THREAD_CS_EXIT(_obj) \
    ucs_recursive_spin_unlock(&(_obj)->lock);
#else
#define UCS_PMODULE_CACHE_THREAD_CS_ENTER(_obj)
#define UCS_PMODULE_CACHE_THREAD_CS_EXIT(_obj)
#endif

#if ENABLE_DEBUG_DATA
#define UCS_PMODULE_CACHE_KEY_SIZE (UCS_SYS_CACHE_LINE_SIZE + sizeof(void*))
#else
#define UCS_PMODULE_CACHE_KEY_SIZE UCS_SYS_CACHE_LINE_SIZE
#endif

typedef struct ucs_pmodule_cached {
    uint8_t         key[UCS_PMODULE_CACHE_KEY_SIZE];
    ucs_list_link_t list;
} ucs_pmodule_cached_t;

typedef void (*ucs_pmodule_cache_evict_cb_f)(ucs_pmodule_cached_t *evicted);

typedef struct ucs_pmodule_cache {
    ucs_list_link_t              *hash_table;
    ucs_pmodule_cache_evict_cb_f evict_cb;
    unsigned                     evict_thresh;
    unsigned                     table_size;
    unsigned                     elem_count;

#if ENABLE_MT
    ucs_recursive_spinlock_t lock;
#endif

    UCS_STATS_NODE_DECLARE(stats);
} ucs_pmodule_cache_t;

ucs_status_t ucs_pmodule_cache_init(unsigned hash_max, unsigned evict_thresh,
                                    ucs_pmodule_cache_evict_cb_f evict_cb
                                    UCS_STATS_ARG(ucs_stats_node_t *parent),
                                    ucs_pmodule_cache_t *cache);

void ucs_pmodule_cache_cleanup(ucs_pmodule_cache_t *cache);

void ucs_pmodule_cache_add(ucs_pmodule_cache_t *cache,
                           ucs_pmodule_cached_t *key,
                           unsigned hash);

void ucs_pmodule_cache_del(ucs_pmodule_cache_t *cache,
                           ucs_pmodule_cached_t *key);

void ucs_pmodule_cache_report_lookup(ucs_pmodule_cache_t *cache,
                                     int is_cache_hit, unsigned lookup_cnt);

static UCS_F_ALWAYS_INLINE void
ucs_pmodule_cache_lookup(ucs_pmodule_cache_t *cache, unsigned hash,
                         const uint8_t *key, size_t cmp_size,
                         ucs_pmodule_cached_t **cached_p)
{
    int is_match;
    const void *al_ref;
    ucs_pmodule_cached_t *iter;
    ucs_list_link_t *head = &cache->hash_table[hash];
    const void *al_key = __builtin_assume_aligned(key, UCS_SYS_CACHE_LINE_SIZE);
#ifdef ENABLE_STATS
    unsigned lookup_cnt   = 0;
#endif

    UCS_PMODULE_CACHE_THREAD_CS_ENTER(cache);
    ucs_list_for_each(iter, head, list) {
        al_ref = __builtin_assume_aligned(iter->key, UCS_SYS_CACHE_LINE_SIZE);
#ifdef ENABLE_STATS
        lookup_cnt++;
#endif

        if (cmp_size == UCS_SYS_CACHE_LINE_SIZE) {
            is_match = ucs_cpu_cache_line_is_equal(al_ref, al_key);
        } else {
            is_match = (memcmp(al_ref, al_key, cmp_size) == 0);
        }

        if (ucs_likely(is_match)) {
            if (ucs_unlikely(iter != ucs_list_head(head,
                                                   ucs_pmodule_cached_t,
                                                   list))) {
                ucs_list_del(&iter->list);
                ucs_list_add_head(head, &iter->list);
            }

#ifdef ENABLE_STATS
            ucs_pmodule_cache_report_lookup(cache, 1, lookup_cnt);
#endif
            UCS_PMODULE_CACHE_THREAD_CS_EXIT(cache);
            *cached_p = iter;
            return;
        }
    }

#ifdef ENABLE_STATS
    ucs_pmodule_cache_report_lookup(cache, 0, lookup_cnt);
#endif
    UCS_PMODULE_CACHE_THREAD_CS_EXIT(cache);
    *cached_p = NULL;
}

#endif /* UCS_PMODULE_CACHE_H_ */
