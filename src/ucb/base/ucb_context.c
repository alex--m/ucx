/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "ucb_context.h"
#include "ucb_pipes.h"

#include <ucs/sys/module.h>
#include <ucs/debug/debug_int.h>

extern ucs_config_field_t ucp_config_table[];

ucs_config_field_t ucb_config_table[] = {
  {"UCB_", "", NULL,
   ucs_offsetof(ucb_context_config_t, super),
   UCS_CONFIG_TYPE_TABLE(ucs_pmodule_config_table)},

  {NULL}
};

UCS_PMODULE_FRAMEWORK(UCB, ucb);

static ucs_status_t ucb_context_init(const ucb_context_config_t *config,
                                     void *batches_ctx)
{
    ucs_status_t status;
    ucb_context_t *ctx                    = (ucb_context_t*)batches_ctx;
    ucs_pmodule_component_params_t params = {0};

    status = ucs_pmodule_framework_init(&ctx->super, &params, &config->super,
                                        UCS_PMODULE_COMPONENT_QUERY(ucb)(0),
                                        "ucb context");
    if (status != UCS_OK) {
        return status;
    }

    // TODO: add UCB-specific init

    return UCS_OK;

// out:
//    ucs_pmodule_framework_cleanup(&ctx->super);
//    return status;
}

static void ucb_context_cleanup(void *batch_ctx)
{
    ucb_context_t *ctx = (ucb_context_t*)batch_ctx;
    ucs_pmodule_framework_cleanup(&ctx->super);
}

UCS_PMODULE_FRAMEWORK_CONTEXT_COMMON(UCB, ucb, UCP, ucp, params->super);

void ucb_context_copy_used_params(ucb_params_t *dst, const ucb_params_t *src)
{
    size_t ucb_params_size = sizeof(src->field_mask) +
                             ucs_offsetof(ucb_params_t, field_mask);

    if (src->field_mask != 0) {
        enum ucb_params_field msb_flag = UCS_BIT((sizeof(uint64_t) * 8) - 1 -
                ucs_count_leading_zero_bits(src->field_mask));
        ucs_assert((msb_flag & src->field_mask) == msb_flag);

        switch (msb_flag) {
        case UCB_PARAM_FIELD_NAME:
            ucb_params_size = ucs_offsetof(ucb_params_t, context_headroom);
            break;

        case UCB_PARAM_FIELD_CONTEXT_HEADROOM:
            ucb_params_size = ucs_offsetof(ucb_params_t, completion);
            break;

        case UCB_PARAM_FIELD_COMPLETION_CB:
            ucb_params_size = sizeof(ucb_params_t);
            break;
        }
    }

    memcpy(dst, src, ucb_params_size);
}