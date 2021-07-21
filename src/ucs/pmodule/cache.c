/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See object LICENSE for terms.
 */

#include "cache.h"
#include "component.h"

ucs_status_t ucs_pmodule_cache_init(ucs_pmodule_cache_t *cache,
                                    enum ucs_pmodule_cache_type type,
                                    unsigned size)
{
    cache->type = type;
    cache->size = size;

    switch (type) {
    case UCS_PMODULE_CACHE_TYPE_FIXED_ARRAY:
        cache->fixed_array = ucs_calloc(1, size,
                                        "ucs_pmodule_cache_fixed_array");
        if (cache->fixed_array == NULL) {
            return UCS_ERR_NO_MEMORY;
        }
        break;

    case UCS_PMODULE_CACHE_TYPE_PTR_ARRAY:
        ucs_ptr_array_init(&cache->ptr_array, "ucs_pmodule_cache_ptr_array");
        break;


    case UCS_PMODULE_CACHE_TYPE_ACTION_LIST:
        ucs_list_head_init(&cache->action_head);

    default:
        return UCS_ERR_INVALID_PARAM;
    }

    return UCS_OK;
}

void ucs_pmodule_cache_cleanup(ucs_pmodule_cache_t *cache)
{
    switch (cache->type) {
    case UCS_PMODULE_CACHE_TYPE_FIXED_ARRAY:
        ucs_free(cache->fixed_array);
        break;

    case UCS_PMODULE_CACHE_TYPE_PTR_ARRAY:
        ucs_ptr_array_cleanup(&cache->ptr_array, 1);
        break;

    case UCS_PMODULE_CACHE_TYPE_ACTION_LIST:
        ucs_assert(ucs_list_is_empty(&cache->action_head));
        break;
    }
}

size_t ucs_pmodule_cache_get_size(ucs_pmodule_cache_t *cache)
{
    return cache->size;
}

void ucs_pmodule_cache_trim(ucs_pmodule_cache_t *cache)
{
    // TODO
}

ucs_status_t ucs_pmodule_cache_lookup(ucs_pmodule_cache_t *cache,
                                      const ucs_pmodule_action_params_t *params,
                                      void **cached_p)
{
    return UCS_ERR_NOT_IMPLEMENTED;
}

void ucs_pmodule_cache_update(ucs_pmodule_cache_t *cache,
                              const ucs_pmodule_action_params_t *params,
                              void *cached)
{
    switch (cache->type) {
    case UCS_PMODULE_CACHE_TYPE_FIXED_ARRAY:
        ucs_free(cache->fixed_array);
        break;

    case UCS_PMODULE_CACHE_TYPE_PTR_ARRAY:
        ucs_ptr_array_cleanup(&cache->ptr_array, 1);
        break;

    case UCS_PMODULE_CACHE_TYPE_ACTION_LIST:
        ucs_list_add_head(&cache->action_head,
                          &((ucs_pmodule_action_t*)cached)->list);
        break;
    }
}
