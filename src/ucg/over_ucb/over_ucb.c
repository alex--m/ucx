/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>
#include <ucs/arch/atomic.h>
#include <ucs/profile/profile.h>
#include <ucp/core/ucp_request.inl>

/* Note: <ucg/api/...> not used because this header is not installed */
#include "../api/ucg_plan_component.h"

#include "over_ucb.h"

extern ucs_config_field_t ucg_over_uct_config_table[];
ucs_config_field_t ucg_over_ucb_config_table[] = {
    {"", "", NULL,
     ucs_offsetof(ucg_over_ucb_config_t, super),
     UCS_CONFIG_TYPE_TABLE(ucg_over_uct_config_table)},

    {NULL}
};

extern ucs_pmodule_component_t ucg_over_ucb_component;

static ucs_status_t ucg_over_ucb_query(ucs_list_link_t *desc_head)
{
    ucg_plan_desc_t *desc     = UCS_ALLOC_TYPE_CHECK(ucg_plan_desc_t);
    desc->super.component     = &ucg_over_ucb_component;
    desc->modifiers_supported = (unsigned)-1; /* supports ANY collective */
    desc->flags               = 0;

    ucs_list_add_head(desc_head, &desc->super.list);
    return UCS_OK;
}

static ucs_status_t ucg_over_ucb_init(ucg_comp_ctx_h cctx,
                                      const ucs_pmodule_component_params_t *params,
                                      ucg_comp_config_t *config)
{
    ucs_status_t status;
    ucg_over_ucb_ctx_t *bctx = cctx;

    status = ucs_config_parser_clone_opts(config, &bctx->config,
                                          ucg_over_ucb_config_table);
    if (status != UCS_OK) {
        return status;
    }

    return ucg_over_uct_init(&bctx->super, params, config);
}

static void ucg_over_ucb_finalize(ucg_comp_ctx_h cctx)
{
    ucg_over_ucb_ctx_t *bctx = cctx;
    ucg_over_uct_finalize(&bctx->super);
}

static void ucg_over_ucb_destroy(ucg_group_ctx_h ctx)
{
    ucg_over_ucb_group_ctx_t *gctx = ctx;
    ucg_over_uct_destroy(&gctx->super);
}

static ucs_status_t ucg_over_ucb_create(ucg_comp_ctx_h cctx,
                                        ucg_group_ctx_h ctx,
                                        ucs_pmodule_target_t *target,
                                        const ucs_pmodule_target_params_t *params)
{
    ucs_status_t status;
    ucb_pipes_params_t pipes_params;

    ucg_over_ucb_group_ctx_t *gctx         = ctx;
    ucg_over_ucb_ctx_t *bctx               = cctx;
    const ucg_group_params_t *group_params = params->per_framework;

    status = ucg_over_uct_create(&bctx->super, &gctx->super, target, params);
    if (status != UCS_OK) {
        return status;
    }

    if (group_params->field_mask & UCG_GROUP_PARAM_FIELD_UCB_PIPES) {
        gctx->pipes = group_params->pipes;
    } else if (group_params->field_mask & UCG_GROUP_PARAM_FIELD_UCP_WORKER) {
        /* set the parameters for creating the pipes */
        pipes_params.field_mask     = UCB_PIPES_PARAM_FIELD_UCP_WORKER |
                                      UCB_PIPES_PARAM_FIELD_CD_MASTER;
        pipes_params.worker         = group_params->worker;
        pipes_params.cd_master_ep_p = &gctx->super.coredirect_master;

        /* create the pipes to be used for this group context */
        status = ucb_pipes_create(&pipes_params, &gctx->pipes);
        if (status != UCS_OK) {
            ucg_over_ucb_destroy(&gctx->super);
        }
    } else {
        ucs_error("ucg_over_ucb needs either a UCP worker or UCB pipes");
        return UCS_ERR_INVALID_PARAM;
    }

    return status;
}

static ucs_status_t ucg_over_ucb_handle_fault(ucg_group_ctx_h gctx, uint64_t id)
{
    // ucg_group_member_index_t index = id;
    return UCS_ERR_NOT_IMPLEMENTED;
}

void ucg_over_ucb_plan_print(ucs_pmodule_target_plan_t *plan) { }

UCS_PMODULE_COMPONENT_DEFINE(ucg_over_ucb_component, "over_ucb",
                             sizeof(ucg_over_ucb_ctx_t),
                             sizeof(ucg_over_ucb_group_ctx_t),
                             ucg_over_ucb_query, ucg_over_ucb_init,
                             ucg_over_ucb_finalize, ucg_over_ucb_create,
                             ucg_over_ucb_destroy, ucg_over_ucb_plan_estimate,
                             ucg_over_ucb_plan_create, ucg_over_ucb_plan_trigger,
                             ucg_over_ucb_plan_progress, ucg_over_ucb_plan_discard,
                             ucg_over_ucb_plan_print, ucg_over_ucb_handle_fault,
                             ucg_over_ucb_config_table, ucg_over_ucb_config_t,
                             "UCG_OVER_UCB_", ucg_components_list);
