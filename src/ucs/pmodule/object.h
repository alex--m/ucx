/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCS_PMODULE_OBJECT_H_
#define UCS_PMODULE_OBJECT_H_

#include "cache.h"

#include <ucs/stats/stats.h>
#include <ucs/type/spinlock.h>
#include <ucs/datastruct/ptr_array.h>

#define UCS_PMODULE_NAME_MAX (16)

typedef unsigned (*ucs_pmodule_action_progress_f)(ucs_pmodule_action_t *action);
typedef struct ucs_pmodule_object_params     ucs_pmodule_object_params_t;
typedef struct ucs_pmodule_object            ucs_pmodule_object_t;
typedef uint32_t                             ucs_pmodule_op_type_t;
typedef uint32_t                             ucs_pmodule_op_id_t;
typedef struct ucs_pmodule_op               *ucs_pmodule_op_h;

#include "component.h" /* uses some of the above definitions */
#include "framework.h" /* uses some of the above definitions */

enum ucs_pmodule_object_params_field {
    UCS_PMODULE_OBJECT_PARAM_FIELD_SUPER  = UCS_BIT(0),
    UCS_PMODULE_OBJECT_PARAM_FIELD_NAME   = UCS_BIT(1),
    UCS_PMODULE_OBJECT_PARAM_FIELD_CACHES = UCS_BIT(2),

    UCS_PMODULE_OBJECT_PARAM_FIELD_MASK   = UCS_MASK(3)
};

enum ucs_pmodule_object_attr_field {
    UCS_PMODULE_OBJECT_ATTR_FIELD_NAME       = UCS_BIT(0),
    UCS_PMODULE_OBJECT_ATTR_FIELD_CACHE_SIZE = UCS_BIT(1),

    UCS_PMODULE_OBJECT_ATTR_FIELD_MASK       = UCS_MASK(2)
};

typedef struct ucs_pmodule_object_attr {
    uint64_t field_mask;
    char     name[UCS_PMODULE_NAME_MAX];
    unsigned cache_size;
} ucs_pmodule_object_attr_t;

struct ucs_pmodule_object_params {
    uint64_t field_mask;

    /*
     * Parent object to be used.
     */
    ucs_pmodule_object_t *super;

    /*
     * Name for this object, to be used for profiling/analysis.
     */
    char                  name[UCS_PMODULE_NAME_MAX];

    /*
     * Cache parameters
     */
    struct {
        enum ucs_pmodule_cache_type type;
        size_t                      size;
    } plan_cache, action_cache;
};

struct ucs_pmodule_op {
    ucs_pmodule_action_t *action;
};

struct ucs_pmodule_object {
    /*
     * Whether a current lock is waited upon. If so, a new operation cannot
     * start until this lock is cleared, so it is put in the pending queue.
     */
    volatile uint32_t                is_lock_outstanding;

#if ENABLE_MT
    ucs_recursive_spinlock_t         lock;
#endif

    ucs_pmodule_op_id_t              next_op;
    ucs_pmodule_object_t            *parent;  /**< parent object */
    ucs_pmodule_framework_context_t *context; /**< Back-reference to UCP context */
    ucs_queue_head_t                 pending; /**< requests currently pending execution */
    ucs_list_link_t                  list;    /**< object list membership */
    ucs_pmodule_cache_t              cache;
    enum ucs_pmodule_cache_type      action_cache_type;
    unsigned                         action_cache_size;
    char                             name[UCS_PMODULE_NAME_MAX];

    UCS_STATS_NODE_DECLARE(stats);

    /* Below this point - the private per-object data is allocated/stored */
};

ucs_status_t
ucs_pmodule_object_create(ucs_pmodule_framework_context_t *framework_ctx,
                          const ucs_pmodule_object_params_t *params
                          UCS_STATS_ARG(ucs_stats_node_t *stats_parent),
                          ucs_pmodule_object_t **object_p);

ucs_status_t ucs_pmodule_object_query(ucs_pmodule_object_t *object,
                                      ucs_pmodule_object_attr_t *attr);

void ucs_pmodule_object_destroy(ucs_pmodule_object_t *object);

ucs_pmodule_action_progress_f ucs_pmodule_op_get_progress(ucs_pmodule_op_h op);

ucs_status_t ucs_pmodule_op_cancel(ucs_pmodule_op_h op);

ucs_status_t ucs_pmodule_action_create(ucs_pmodule_object_t *object,
                                       const ucs_pmodule_action_params_t *params,
                                       ucs_pmodule_action_t **action_p);

ucs_status_t ucs_pmodule_action_start(ucs_pmodule_action_t *action, void *req);

void ucs_pmodule_action_destroy(ucs_pmodule_action_t *action);

#endif /* UCS_PMODULE_OBJECT_H_ */
