/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See object LICENSE for terms.
 */

#include "object.h"

#include <ucs/datastruct/queue.h>
#include <ucs/datastruct/list.h>
#include <ucs/profile/profile.h>
#include <ucs/debug/memtrack.h>
#include <ucs/sys/string.h>

#define UCS_PMODULE_OBJECT_DEFAULT_PLAN_CACHE_TYPE   (UCS_PMODULE_CACHE_TYPE_FIXED_ARRAY)
#define UCS_PMODULE_OBJECT_DEFAULT_PLAN_CACHE_SIZE   (100)
#define UCS_PMODULE_OBJECT_DEFAULT_ACTION_CACHE_TYPE (UCS_PMODULE_CACHE_TYPE_ACTION_LIST)
#define UCS_PMODULE_OBJECT_DEFAULT_ACTION_CACHE_SIZE (100)

#if ENABLE_MT
#define UCS_PMODULE_OBJECT_THREAD_CS_ENTER(_obj) \
    ucs_recursive_spin_lock(&(_obj)->lock);
#define UCS_PMODULE_OBJECT_THREAD_CS_EXIT(_obj) \
    ucs_recursive_spin_unlock(&(_obj)->lock);
#else
#define UCS_PMODULE_OBJECT_THREAD_CS_ENTER(_obj)
#define UCS_PMODULE_OBJECT_THREAD_CS_EXIT(_obj)
#endif

#if ENABLE_STATS
enum {
    UCS_PMODULE_STAT_PLANS_CREATED,
    UCS_PMODULE_STAT_PLANS_USED,

    UCS_PMODULE_STAT_ACTIONS_CREATED,
    UCS_PMODULE_STAT_ACTIONS_USED,

    UCS_PMODULE_STAT_ACTIONS_TRIGGERED,
    UCS_PMODULE_STAT_ACTIONS_IMMEDIATE,

    UCS_PMODULE_STAT_LAST
};

static ucs_stats_class_t ucs_pmodule_object_stats_class = {
    .name           = "ucs_pmodule",
    .num_counters   = UCS_PMODULE_STAT_LAST,
    .counter_names  = {
        [UCS_PMODULE_STAT_PLANS_CREATED] = "plans_created",
        [UCS_PMODULE_STAT_PLANS_USED]    = "plans_reused",
        [UCS_PMODULE_STAT_ACTIONS_CREATED]   = "ops_created",
        [UCS_PMODULE_STAT_ACTIONS_USED]      = "ops_started",
        [UCS_PMODULE_STAT_ACTIONS_IMMEDIATE] = "ops_immediate"
    }
};
#endif /* ENABLE_STATS */

static inline ucs_status_t
ucs_pmodule_object_plan(ucs_pmodule_object_t *object,
                        const ucs_pmodule_action_params_t *params,
                        ucs_pmodule_plan_t **plan_p)
{
    ucs_status_t status;
    ucs_pmodule_desc_t *desc;
    ucs_pmodule_plan_t *plan;
    ucs_pmodule_object_ctx_h octx;

    UCS_PROFILE_CODE("ucs_pmodule_choose") {
        status = ucs_pmodule_component_choose(params, object, &desc, &octx);
    }
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    UCS_PROFILE_CODE("ucs_pmodule_plan") {
        status = desc->component->plan(octx, params, &plan);
    }
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    status = ucs_pmodule_cache_init(&plan->cache, object->action_cache_type,
                                    object->action_cache_size);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

#if ENABLE_MT
    status = ucs_recursive_spinlock_init(&plan->lock, 0);
    if (ucs_unlikely(status != UCS_OK)) {
        ucs_pmodule_cache_cleanup(&plan->cache);
        return status;
    }
#endif


    plan->type   = params->type;
    plan->desc   = desc;
    plan->object = object;
    *plan_p      = plan;

    return UCS_OK;
}

ucs_status_t
ucs_pmodule_object_create(ucs_pmodule_framework_context_t *framework_ctx,
                          const ucs_pmodule_object_params_t *params
                          UCS_STATS_ARG(ucs_stats_node_t *stats_parent),
                          ucs_pmodule_object_t **object_p)
{
    size_t cache_size;
    ucs_status_t status;
    ucs_pmodule_object_t *object;
    enum ucs_pmodule_cache_type cache_type;

    object = UCS_ALLOC_CHECK(sizeof(*object), "communicator object");

    /* Fill in the object fields */
    object->is_lock_outstanding  = 0;
    object->next_op              = 0;
    object->context              = framework_ctx;

    if (params->field_mask & UCS_PMODULE_OBJECT_PARAM_FIELD_NAME) {
        ucs_strncpy_safe(object->name, params->name, UCS_PMODULE_NAME_MAX);
    }

    if (params->field_mask & UCS_PMODULE_OBJECT_PARAM_FIELD_CACHES) {
        cache_type                = params->plan_cache.type;
        cache_size                = params->plan_cache.size;
        object->action_cache_type = params->action_cache.type;
        object->action_cache_size = params->action_cache.size;
    } else {
        cache_type                = UCS_PMODULE_OBJECT_DEFAULT_PLAN_CACHE_TYPE;
        cache_size                = UCS_PMODULE_OBJECT_DEFAULT_PLAN_CACHE_SIZE;
        object->action_cache_type = UCS_PMODULE_OBJECT_DEFAULT_ACTION_CACHE_TYPE;
        object->action_cache_size = UCS_PMODULE_OBJECT_DEFAULT_ACTION_CACHE_SIZE;
    }

#if ENABLE_MT
    ucs_recursive_spinlock_init(&object->lock, 0);
#endif

    ucs_queue_head_init(&object->pending);

    status = UCS_STATS_NODE_ALLOC(&object->stats,
                                  &ucs_pmodule_object_stats_class, stats_parent,
                                  "-%p", object);
    if (status != UCS_OK) {
        goto cleanup_object;
    }

    /* Initialize the framework (loadable modules) */
    status = ucs_pmodule_component_create(object, params);
    if (status != UCS_OK) {
        goto cleanup_stats;
    }

    ucs_pmodule_cache_init(&object->cache, cache_type, cache_size);
    ucs_pmodule_framework_add_object(framework_ctx, object);

    *object_p = object;
    return UCS_OK;

cleanup_stats:
    UCS_STATS_NODE_FREE(object->stats);
cleanup_object:
    ucs_free(object);
    return status;
}

ucs_status_t ucs_pmodule_object_query(ucs_pmodule_object_t *object,
                                      ucs_pmodule_object_attr_t *attr)
{
    if (attr->field_mask & UCS_PMODULE_OBJECT_ATTR_FIELD_NAME) {
        ucs_strncpy_safe(attr->name, object->name, UCS_PMODULE_NAME_MAX);
    }

    if (attr->field_mask & UCS_PMODULE_OBJECT_ATTR_FIELD_CACHE_SIZE) {
        attr->cache_size = ucs_pmodule_cache_get_size(&object->cache);
    }

    return UCS_OK;
}

void ucs_pmodule_object_destroy(ucs_pmodule_object_t *object)
{
    /* First - make sure all the op are completed */
    while (!ucs_queue_is_empty(&object->pending)) {
        ucs_pmodule_op_cancel((ucs_pmodule_op_h)
                              ucs_queue_pull_non_empty(&object->pending));
    }

    UCS_PMODULE_OBJECT_THREAD_CS_ENTER(object)

    UCS_STATS_NODE_FREE(object->stats);
    ucs_pmodule_cache_cleanup(&object->cache);
    ucs_pmodule_component_destroy(object);
    ucs_list_del(&object->list);

    UCS_PMODULE_OBJECT_THREAD_CS_EXIT(object)

#if ENABLE_MT
    ucs_recursive_spinlock_destroy(&object->lock);
#endif

    ucs_free(object);
}

UCS_PROFILE_FUNC(ucs_status_t, ucs_pmodule_action_create,
                 (object, params, action_p),
                 ucs_pmodule_object_t *object,
                 const ucs_pmodule_action_params_t *params,
                 ucs_pmodule_action_t **action_p)
{
    ucs_status_t status;
    ucs_pmodule_plan_t *plan;
    ucs_pmodule_action_t *action;

    ucs_assert(object != NULL);
    ucs_assert(params != NULL);
    ucs_assert(action != NULL);

    /* Look for an existing plan according to the given parameters */
    status = ucs_pmodule_cache_lookup(&object->cache, params, (void**)&plan);
    if (ucs_unlikely(status != UCS_OK)) {
        goto out;
    }

    if (plan == NULL) {
        /* Make a new plan */
        status = ucs_pmodule_object_plan(object, params, &plan);
        if (status != UCS_OK) {
            goto out;
        }

        UCS_STATS_UPDATE_COUNTER(object->stats, UCS_PMODULE_STAT_PLANS_CREATED, 1);
        ucs_pmodule_cache_update(&object->cache, params, plan);
    } else {
        UCS_STATS_UPDATE_COUNTER(object->stats, UCS_PMODULE_STAT_PLANS_USED, 1);
    }

    UCS_PMODULE_OBJECT_THREAD_CS_ENTER(plan);

    /* Look for an existing action according to the given parameters */
    status = ucs_pmodule_cache_lookup(&plan->cache, params, (void**)&action);
    if (ucs_unlikely(status != UCS_OK)) {
        UCS_PMODULE_OBJECT_THREAD_CS_EXIT(plan);
        goto plan_out;
    }

    if (action == NULL) {
        UCS_PROFILE_CODE("ucs_pmodule_prepare") {
            status = plan->desc->component->prepare(plan, params, &action);
        }

        if (status != UCS_OK) {
            goto plan_out;
        }

        UCS_STATS_UPDATE_COUNTER(object->stats, UCS_PMODULE_STAT_ACTIONS_CREATED, 1);
        ucs_pmodule_cache_update(&plan->cache, params, action);
    } else {
        UCS_STATS_UPDATE_COUNTER(object->stats, UCS_PMODULE_STAT_ACTIONS_USED, 1);
    }

    *action_p = action;

plan_out:
    UCS_PMODULE_OBJECT_THREAD_CS_EXIT(plan);
out:
    return status;
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucs_pmodule_action_trigger(ucs_pmodule_object_t *object, ucs_pmodule_action_t *action, void *req)
{
    ucs_status_t ret;

    /* Start the first step of the action */
    UCS_PROFILE_CODE("ucs_pmodule_trigger") {
        ret = action->trigger_f(action, ++object->next_op, req);
    }

    if (ret != UCS_INPROGRESS) {
        UCS_STATS_UPDATE_COUNTER(object->stats, UCS_PMODULE_STAT_ACTIONS_IMMEDIATE, 1);
    }

    return ret;
}

ucs_status_t ucs_pmodule_action_acquire_lock(ucs_pmodule_object_t *object)
{
    ucs_assert(object->is_lock_outstanding == 0);
    object->is_lock_outstanding = 1;
    return UCS_OK;
}

ucs_status_t ucs_pmodule_action_release_lock(ucs_pmodule_object_t *object)
{
    ucs_status_t status;
    ucs_pmodule_action_t *action;

    ucs_assert(object->is_lock_outstanding == 1);
    object->is_lock_outstanding = 0;

    UCS_PMODULE_OBJECT_THREAD_CS_ENTER(object)

    if (!ucs_queue_is_empty(&object->pending)) {
        do {
            /* Start the next pending ioeration */
            action = (ucs_pmodule_action_t*)ucs_queue_pull_non_empty(&object->pending);
            status = ucs_pmodule_action_trigger(object, action, action->pending_req);
        } while ((!ucs_queue_is_empty(&object->pending)) &&
                 (!object->is_lock_outstanding) &&
                 (status == UCS_OK));
    } else {
        status = UCS_OK;
    }

    UCS_PMODULE_OBJECT_THREAD_CS_EXIT(object)

    return status;
}

UCS_PROFILE_FUNC(ucs_status_t, ucs_pmodule_action_start, (action, req), ucs_pmodule_action_t *action, void *req)
{
    ucs_status_t ret;
    ucs_pmodule_object_t *object = action->plan->object;

    UCS_PMODULE_OBJECT_THREAD_CS_ENTER(object)

    if (ucs_unlikely(object->is_lock_outstanding)) {
        ucs_queue_push(&object->pending, &action->queue);
        action->pending_req = req;
        ret = UCS_INPROGRESS;
    } else {
        ret = ucs_pmodule_action_trigger(object, action, req);
    }

    ucs_pmodule_cache_inspect(&object->cache);

    UCS_STATS_UPDATE_COUNTER(object->stats, UCS_PMODULE_STAT_ACTIONS_TRIGGERED, 1);

    UCS_PMODULE_OBJECT_THREAD_CS_EXIT(object)

    return ret;
}

void ucs_pmodule_action_destroy(ucs_pmodule_action_t *action)
{
    ucs_pmodule_plan_t *plan = action->plan;

    UCS_PMODULE_OBJECT_THREAD_CS_ENTER(plan);

    ucs_pmodule_cache_update(&plan->cache, NULL, action);

    UCS_PMODULE_OBJECT_THREAD_CS_EXIT(plan);
}

ucs_pmodule_action_progress_f ucs_pmodule_op_get_progress(ucs_pmodule_op_h op)
{
    return op->action->plan->desc->component->progress;
}

ucs_status_t ucs_pmodule_op_cancel(ucs_pmodule_op_h op)
{
    // TODO: implement
    return UCS_ERR_UNSUPPORTED;
}
