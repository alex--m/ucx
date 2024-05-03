/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCB_CONTEXT_H_
#define UCB_CONTEXT_H_

#include <ucb/api/ucb.h>
#include <ucp/core/ucp_context.h>
#include <ucs/pmodule/framework.h>

typedef struct ucb_config {
    ucs_pmodule_framework_config_t super;
} ucb_context_config_t;

/*
 * To enable the "Groups" feature in UCX - it's registered as part of the UCX
 * context - and allocated a context slot in each UCP Worker at a certain offset.
 */
typedef struct ucb_context {
    ucs_pmodule_framework_context_t super;
    ucb_context_config_t            config;

    ucp_context_t                   ucp_ctx; /* last, for ABI compatibility */
} ucb_context_t;

#endif /* UCB_CONTEXT_H_ */
