/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCF_CONTEXT_H_
#define UCF_CONTEXT_H_

#include <ucf/api/ucf.h>
#include <ucg/base/ucg_context.h>

typedef struct ucf_config {
    ucs_pmodule_framework_config_t super;

    // TODO: add more...
} ucf_context_config_t;

/*
 * To enable the "Groups" feature in UCX - it's registered as part of the UCX
 * context - and allocated a context slot in each UCP Worker at a certain offset.
 */
typedef struct ucf_context {
    ucs_pmodule_framework_context_t super;
    ucf_context_config_t            config;

    ucg_context_t                   ucg_ctx; /* last for ABI compatibility */
} ucf_context_t;

#endif /* UCF_CONTEXT_H_ */
