/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCS_PMODULE_CACHE_H_
#define UCS_PMODULE_CACHE_H_

#include <ucs/stats/stats.h>
#include <ucs/datastruct/ptr_array.h>

typedef struct ucs_pmodule_action        ucs_pmodule_action_t;
typedef struct ucs_pmodule_action_params ucs_pmodule_action_params_t;

enum ucs_pmodule_cache_type {
    UCS_PMODULE_CACHE_TYPE_FIXED_ARRAY,
    UCS_PMODULE_CACHE_TYPE_PTR_ARRAY,
    UCS_PMODULE_CACHE_TYPE_ACTION_LIST,
};

typedef struct ucs_pmodule_cache {
    enum ucs_pmodule_cache_type type;
    unsigned size;
    union {
        void **fixed_array;
        ucs_ptr_array_t ptr_array;
        ucs_list_link_t action_head;
    };

    UCS_STATS_NODE_DECLARE(stats);
} ucs_pmodule_cache_t;

ucs_status_t ucs_pmodule_cache_init(ucs_pmodule_cache_t *cache,
                                    enum ucs_pmodule_cache_type type,
                                    unsigned size);

void ucs_pmodule_cache_cleanup(ucs_pmodule_cache_t *cache);

ucs_status_t ucs_pmodule_cache_lookup(ucs_pmodule_cache_t *cache,
                                      const ucs_pmodule_action_params_t *params,
                                      void **cached_p);

void ucs_pmodule_cache_update(ucs_pmodule_cache_t *cache,
                              const ucs_pmodule_action_params_t *params,
                              void *cached);

size_t ucs_pmodule_cache_get_size(ucs_pmodule_cache_t *cache);

void ucs_pmodule_cache_trim(ucs_pmodule_cache_t *cache);

static UCS_F_ALWAYS_INLINE void
ucs_pmodule_cache_inspect(ucs_pmodule_cache_t *cache)
{
    if (ucs_unlikely(cache->size > 100 /* TODO: configure */ )) {
        ucs_pmodule_cache_trim(cache);
    }
}

#endif /* UCS_PMODULE_CACHE_H_ */
