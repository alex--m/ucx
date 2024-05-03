/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "target.h"

#include <ucs/debug/log.h>
#include <ucs/debug/memtrack_int.h>
#include <ucs/sys/string.h>

#define UCS_PMODULE_TARGET_CACHE_MAX_ELEMS_DEFAULT (1024)
#define UCS_PMODULE_TARGET_CACHE_MAX_HASH_DEFAULT  (64)

#ifdef ENABLE_STATS
enum {
    UCS_PMODULE_TARGET_STAT_PLANS_CREATED,
    UCS_PMODULE_TARGET_STAT_PLANS_USED,

    UCS_PMODULE_TARGET_STAT_LAST
};

static ucs_stats_class_t ucs_pmodule_target_stats_class = {
    .name           = "pmodule_target",
    .num_counters   = UCS_PMODULE_TARGET_STAT_LAST,
    .counter_names  = {
        [UCS_PMODULE_TARGET_STAT_PLANS_CREATED]   = "plans_created",
        [UCS_PMODULE_TARGET_STAT_PLANS_USED]      = "plans_reused"
    }
};
#endif /* ENABLE_STATS */

/******************************************************************************
 *                                                                            *
 *                              Group Creation                                *
 *                                                                            *
 ******************************************************************************/

ucs_status_t
ucs_pmodule_target_create(const ucs_pmodule_target_params_t *params
                          UCS_STATS_ARG(ucs_stats_node_t *stats_parent),
                          ucs_pmodule_target_t **target_p)
{
    int ret;
    unsigned hash_max;
    unsigned evict_thresh;
    ucs_status_t status;
    ucs_pmodule_target_t *target;
    ucs_pmodule_cache_evict_cb_f evict_cb;

    size_t alloc_size = sizeof(ucs_pmodule_target_t);

    if (params->field_mask & UCS_PMODULE_TARGET_PARAM_FIELD_HEADROOM) {
        alloc_size += params->target_headroom;
    }
    if (params->field_mask & UCS_PMODULE_TARGET_PARAM_FIELD_LEGROOM) {
        alloc_size += params->target_legroom;
    }

    ret = ucs_posix_memalign((void**)&target, UCS_SYS_CACHE_LINE_SIZE,
                             alloc_size, "ucs_pmodule_target");
    if (ret || (target == NULL)) {
        return UCS_ERR_NO_MEMORY;
    }

    /* Leave the headroom vacant */
    if (params->field_mask & UCS_PMODULE_TARGET_PARAM_FIELD_HEADROOM) {
        target           = UCS_PTR_BYTE_OFFSET(target, params->target_headroom);
        target->headroom = params->target_headroom;
    } else {
        target->headroom = 0;
    }

#if ENABLE_DEBUG_DATA
    if (params->field_mask & UCS_PMODULE_TARGET_PARAM_FIELD_NAME) {
        ucs_strncpy_safe(target->name, params->name, UCS_PMODULE_NAME_MAX);
    } else {
        target->name[0] = 0;
    }
#endif

    ucs_queue_head_init(&target->pending);

    status = UCS_STATS_NODE_ALLOC(&target->stats,
                                  &ucs_pmodule_target_stats_class, stats_parent,
                                  "-%p", target);
    if (status != UCS_OK) {
        goto cleanup_target;
    }

    if (params->field_mask & UCS_PMODULE_TARGET_PARAM_FIELD_CACHE_MAX_ELEMS) {
        evict_thresh = params->cache_max_elems.limit;
        evict_cb     = params->cache_max_elems.evict_cb;
    } else {
        evict_thresh = 0;
        evict_cb     = NULL;
    }

    if (params->field_mask & UCS_PMODULE_TARGET_PARAM_FIELD_CACHE_MAX_HASH) {
        hash_max = params->cache_max_hash;
    } else {
        hash_max = UCS_PMODULE_TARGET_CACHE_MAX_HASH_DEFAULT;
    }

    status = ucs_pmodule_cache_init(hash_max, evict_thresh, evict_cb
                                    UCS_STATS_ARG(stats_parent),
                                    &target->cache);
    if (ucs_unlikely(status != UCS_OK)) {
        goto cleanup_stats;
    }

    *target_p = target;
    return UCS_OK;

cleanup_stats:
    UCS_STATS_NODE_FREE(target->stats);
cleanup_target:
    if (params->field_mask & UCS_PMODULE_TARGET_PARAM_FIELD_HEADROOM) {
        target = UCS_PTR_BYTE_OFFSET(target, -params->target_headroom);
    }
    ucs_free(target);
    return status;
}

void ucs_pmodule_target_destroy(ucs_pmodule_target_t *target)
{
    /* First - make sure all the op are completed */
    ucs_pmodule_target_action_t *action;
    while (!ucs_queue_is_empty(&target->pending)) {
        action = ucs_queue_pull_elem_non_empty(&target->pending,
                                               ucs_pmodule_target_action_t,
                                               queue);
        ucs_warn("target(%p) action id %u is still pending during cleanup: %p",
                  target, action->id, action);
        ucs_mpool_put_inline(action);
    }

    ucs_pmodule_cache_cleanup(&target->cache);
    UCS_STATS_NODE_FREE(target->stats);

    ucs_free(UCS_PTR_BYTE_OFFSET(target, -target->headroom));
}


/******************************************************************************
 *                                                                            *
 *                              Target Planning                               *
 *                                                                            *
 ******************************************************************************/

ucs_status_t ucs_pmodule_target_plan(ucs_pmodule_target_t *target, unsigned hash,
                                     ucs_pmodule_target_action_params_h params,
                                     size_t params_cmp_size,
                                     ucs_pmodule_target_plan_t **plan_p)
{
    ucs_status_t status;
    ucs_pmodule_target_plan_t *plan;

    UCS_PROFILE_CODE("ucs_pmodule_target_plan", {
        status = target->plan_f(target->context, target, params, &plan);
    });
    if (status != UCS_OK) {
        return status;
    }

    memcpy(plan->params.key, params, UCS_PMODULE_CACHE_KEY_SIZE);
    ucs_pmodule_cache_add(&target->cache, &plan->params, hash);
    UCS_STATS_UPDATE_COUNTER(target->stats,
                             UCS_PMODULE_TARGET_STAT_PLANS_CREATED, 1);

    plan->target = target;
    *plan_p      = plan;
    return UCS_OK;
}

void ucs_pmodule_target_inc_used_counter(ucs_pmodule_target_t *target)
{
    UCS_STATS_UPDATE_COUNTER(target->stats,
                             UCS_PMODULE_TARGET_STAT_PLANS_USED, 1);
}

void ucs_pmodule_target_plan_uncache(ucs_pmodule_target_t *target,
                                     ucs_pmodule_target_plan_t *plan)
{
    ucs_pmodule_cache_del(&target->cache, &plan->params);
}
