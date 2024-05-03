/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "ucf_context.h"
#include "ucf_file.h"

#include <ucs/sys/module.h>
#include <ucs/debug/debug_int.h>

extern ucs_config_field_t ucg_config_table[];

ucs_config_field_t ucf_config_table[] = {
  {"UCF_", "", NULL,
   ucs_offsetof(ucf_context_config_t, super),
   UCS_CONFIG_TYPE_TABLE(ucs_pmodule_config_table)},

    // TODO: add more...

  {NULL}
};

UCS_PMODULE_FRAMEWORK(UCF, ucf);

static ucs_status_t ucf_context_init(const ucf_context_config_t *config,
                                     void *files_ctx)
{
    ucs_status_t status;
    ucf_context_t *ctx                    = (ucf_context_t*)files_ctx;
    ucs_pmodule_component_params_t params = {0};

    status = ucs_pmodule_framework_init(&ctx->super, &params, &config->super,
                                        UCS_PMODULE_COMPONENT_QUERY(ucf)(0),
                                        "ucf context");
    if (status != UCS_OK) {
        return status;
    }

    // TODO: add UCF-specific init

    return UCS_OK;

// out:
//    ucs_pmodule_framework_cleanup(&ctx->super);
//    return status;
}

static void ucf_context_cleanup(void *files_ctx)
{
    ucf_context_t *ctx = (ucf_context_t*)files_ctx;
    ucs_pmodule_framework_cleanup(&ctx->super);
}

UCS_PMODULE_FRAMEWORK_CONTEXT_COMMON(UCF, ucf, UCG, ucg,
                                     params->super->super->super);

void ucf_context_copy_used_params(ucf_params_t *dst, const ucf_params_t *src)
{
    size_t ucf_params_size = sizeof(src->field_mask) +
                             ucs_offsetof(ucf_params_t, field_mask);

    if (src->field_mask != 0) {
        enum ucf_params_field msb_flag = UCS_BIT((sizeof(uint64_t) * 8) - 1 -
                ucs_count_leading_zero_bits(src->field_mask));
        ucs_assert((msb_flag & src->field_mask) == msb_flag);

        switch (msb_flag) {
        case UCF_PARAM_FIELD_NAME:
            ucf_params_size = ucs_offsetof(ucb_params_t, context_headroom);
            break;

        case UCF_PARAM_FIELD_CONTEXT_HEADROOM:
            ucf_params_size = ucs_offsetof(ucb_params_t, completion);
            break;

        case UCF_PARAM_FIELD_COMPLETION_CB:
            ucf_params_size = offsetof(ucf_params_t, ext_fs);
            break;

        case UCF_PARAM_FIELD_EXTERNAL_FS:
            ucf_params_size = sizeof(ucf_params_t);
            break;
        }
    }

    memcpy(dst, src, ucf_params_size);
}