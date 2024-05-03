/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_CONTEXT_H_
#define UCG_CONTEXT_H_

#include <ucb/base/ucb_context.h>
#include <ucs/pmodule/framework.h>
#include <ucs/datastruct/ptr_array.h>

enum ucg_context_distribution_type {
    UCG_DELAY_DISTRIBUTION_NONE,
    UCG_DELAY_DISTRIBUTION_UNIFORM,
    UCG_DELAY_DISTRIBUTION_NORMAL
};

typedef struct ucg_config {
    ucs_pmodule_framework_config_t super;

    /** Should datatypes be treated as volatile and reloaded on each invocation */
    int is_volatile_datatype;

    /** Distribution function for the delay (in microseconds) after a barrier */
    enum ucg_context_distribution_type delay_distribution;

    /** For the chosen distribution - this is the expected value */
    double delay_expected_value;

    /** For a normal (gaussian) distribution - this is the variance */
    double delay_normal_variance;
} ucg_context_config_t;

/*
 * To enable the "Groups" feature in UCX - it's registered as part of the UCX
 * context - and allocated a context slot in each UCP Worker at a certain offset.
 */
typedef struct ucg_context {
    ucs_pmodule_framework_context_t super;
    ucg_group_id_t                  last_group_id;
    ucg_context_config_t            config;
    ucs_spinlock_t                  lock;

    /**
     * When a message arrives as part of a collective operation - it contains a
     * group ID to indicate the UCG group (and inside it - the UCP worker) it
     * belongs to. Since the groups are created in parallel on different hosts,
     * it is possible a message arrives before the corresponding group has been
     * created - it must then be allocated from this temporary, "dummy" worker
     * and stored in this "unexpected" message array until the matching group is
     * created (and grabs it from here).
     */
    ucg_group_h                     dummy_group;

#ifdef ENABLE_FAULT_TOLERANCE
    ucg_ft_ctx_t                    ft_ctx; /* fault-tolerance context */
#endif
    ucb_context_t                   ucb_ctx; /* last for ABI compatibility */
} ucg_context_t;

#endif /* UCG_CONTEXT_H_ */
