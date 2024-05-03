/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCS_PMODULE_TARGET_H_
#define UCS_PMODULE_TARGET_H_

#include "cache.h"

#include <ucs/arch/atomic.h>
#include <ucs/stats/stats.h>
#include <ucs/profile/profile.h>
#include <ucs/datastruct/list.h>
#include <ucs/datastruct/queue.h>
#include <ucs/datastruct/mpool.inl>

#define UCS_PMODULE_NAME_MAX (64)

typedef struct ucs_pmodule_framework_context ucs_pmodule_framework_context_t;
typedef struct ucs_pmodule_component         ucs_pmodule_component_t;
typedef struct ucs_pmodule_target            ucs_pmodule_target_t;
typedef struct ucs_pmodule_target_plan       ucs_pmodule_target_plan_t;
typedef struct ucs_pmodule_target_action     ucs_pmodule_target_action_t;
typedef const void*                          ucs_pmodule_target_action_params_h;

typedef ucs_status_t     (*ucs_pmodule_framework_target_plan_f)
                         (ucs_pmodule_framework_context_t *ctx,
                          ucs_pmodule_target_t *target,
                          ucs_pmodule_target_action_params_h params,
                          ucs_pmodule_target_plan_t **plan_p);
typedef unsigned         (*ucs_pmodule_target_action_progress_f)
                         (ucs_pmodule_target_action_t *action);
typedef ucs_status_ptr_t (*ucs_pmodule_target_action_trigger_f)
                         (ucs_pmodule_target_plan_t *plan, uint16_t id,
                          void *request);

enum ucs_pmodule_target_action_flags {
    UCS_PMODULE_TARGET_ACTION_FLAG_ASYNC_COMPLETION = UCS_BIT(0)
};

struct ucs_pmodule_target_action {
    ucs_pmodule_target_plan_t *plan;  /**< plan which this belongs to */
    ucs_queue_elem_t          queue;  /**< queue member */
    void                      *req;   /**< original invocation request */
    uint16_t                  id;     /**< optional: custom ID */
    uint8_t                   flags;  /**< @ref enum ucs_pmodule_target_action_flags */
    ucs_status_t              status; /**< current status of this action */
} UCS_S_PACKED;

struct ucs_pmodule_target_plan {
    ucs_pmodule_cached_t                 params;     /**< given parameters to the plan */
    ucs_pmodule_target_action_trigger_f  trigger_f;  /**< shortcut for a trigger call */
    ucs_pmodule_target_action_progress_f progress_f; /**< shortcut for a progress call */
    ucs_pmodule_component_t              *component; /**< component which created this */
    ucs_pmodule_target_t                 *target;    /**< target which this belongs to */
} UCS_S_PACKED UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);

enum ucs_pmodule_target_params_field {
    UCS_PMODULE_TARGET_PARAM_FIELD_NAME            = UCS_BIT(0),
    UCS_PMODULE_TARGET_PARAM_FIELD_CACHE_MAX_ELEMS = UCS_BIT(1),
    UCS_PMODULE_TARGET_PARAM_FIELD_CACHE_MAX_HASH  = UCS_BIT(2),
    UCS_PMODULE_TARGET_PARAM_FIELD_HEADROOM        = UCS_BIT(3),
    UCS_PMODULE_TARGET_PARAM_FIELD_LEGROOM         = UCS_BIT(4),
    UCS_PMODULE_TARGET_PARAM_FIELD_PER_FRAMEWORK   = UCS_BIT(5),

    UCS_PMODULE_TARGET_PARAM_FIELD_MASK          = UCS_MASK(6)
};

enum ucs_pmodule_target_attr_field {
    UCS_PMODULE_TARGET_ATTR_FIELD_NAME       = UCS_BIT(0),

    UCS_PMODULE_TARGET_ATTR_FIELD_MASK       = UCS_MASK(1)
};

typedef struct ucs_pmodule_target_params {
    uint64_t   field_mask;

    /**
     * Name for this target, to be used for profiling/analysis.
     */
    const char *name;

    /**
     * Size of the internal cache, and a callback for evicting excess objects.
     */
    struct {
        unsigned limit;
        ucs_pmodule_cache_evict_cb_f evict_cb;
    } cache_max_elems;

    /**
     * Maximal hash value for caching.
     */
    unsigned   cache_max_hash;

    /**
     * Additional memory preceeding of the allocated target.
     */
    size_t     target_headroom;

    /**
     * Additional memory following the allocated target.
     */
    size_t     target_legroom;

    /**
     * This field type varies per framework, and holds custom parameters for it.
     */
    const void *per_framework;
} ucs_pmodule_target_params_t;

struct ucs_pmodule_target {
    ucs_pmodule_framework_target_plan_f plan_f;    /**< shortcut for a plan call */
    ucs_pmodule_framework_context_t     *context;  /**< argument for plan_f */
    ucs_pmodule_cache_t                 cache;     /**< caching past plans */
    ucs_queue_head_t                    pending;   /**< requests currently pending execution */
    ucs_pmodule_target_t                *parent;   /**< parent target */
    ucs_list_link_t                     list;      /**< target list membership */
    size_t                              headroom;  /**< Extra bytes heading the allocation */

    UCS_STATS_NODE_DECLARE(stats);

#if ENABLE_DEBUG_DATA
    char                                name[UCS_PMODULE_NAME_MAX]; /**< name */
#endif
};

ucs_status_t
ucs_pmodule_target_create(const ucs_pmodule_target_params_t *params
                          UCS_STATS_ARG(ucs_stats_node_t *stats_parent),
                          ucs_pmodule_target_t **target_p);

void ucs_pmodule_target_destroy(ucs_pmodule_target_t *target);

ucs_status_t ucs_pmodule_target_plan(ucs_pmodule_target_t *target, unsigned hash,
                                     ucs_pmodule_target_action_params_h params,
                                     size_t params_cmp_size,
                                     ucs_pmodule_target_plan_t **plan_p);

void ucs_pmodule_target_inc_used_counter(ucs_pmodule_target_t *target);

void ucs_pmodule_target_plan_uncache(ucs_pmodule_target_t *target,
                                     ucs_pmodule_target_plan_t *plan);

static UCS_F_ALWAYS_INLINE ucs_status_t
ucs_pmodule_target_get_plan(ucs_pmodule_target_t *target, unsigned hash,
                            ucs_pmodule_target_action_params_h params,
                            size_t params_cmp_size,
                            ucs_pmodule_target_plan_t **plan_p)
{
    ucs_assert(target != NULL);
    ucs_assert(params != NULL);
    ucs_assert(plan_p != NULL);
    ucs_assert(params_cmp_size != 0);

    /* Obtain a plan (from cache or from scratch) */
    ucs_assert(hash < target->cache.table_size);
    ucs_pmodule_cache_lookup(&target->cache, hash, (const uint8_t*)params,
                             params_cmp_size, (ucs_pmodule_cached_t**)plan_p);

    if (ucs_likely(*plan_p != NULL)) {
#ifdef ENABLE_STATS
        ucs_pmodule_target_inc_used_counter(target);
#endif
        return UCS_OK;
    }

    return ucs_pmodule_target_plan(target, hash, params, params_cmp_size, plan_p);
}

static UCS_F_ALWAYS_INLINE ucs_status_ptr_t
ucs_pmodule_target_launch(ucs_pmodule_target_plan_t *plan, unsigned id, void *req,
                          ucs_pmodule_target_action_progress_f *progress_f_p)
{
    ucs_status_ptr_t ret = plan->trigger_f(plan, id, req);
    *progress_f_p        = plan->progress_f;

    return ret;
}

static inline ucs_pmodule_target_action_progress_f
ucs_pmodule_target_get_progress(ucs_pmodule_target_action_t *action)
{
    return action->plan->progress_f;
}

#endif /* UCS_PMODULE_TARGET_H_ */
