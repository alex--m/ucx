/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_ucg.h"
#include <ucs/pmodule/component.h>

typedef void ucf_file_ctx_t;
typedef void ucf_comp_config_t;
typedef ucs_pmodule_component_ctx_h ucf_comp_ctx_h;
typedef ucs_pmodule_component_desc_t ucf_plan_desc_t;

static ucs_config_field_t ucf_over_ucg_config_table[] = {
    {NULL}
};

extern ucs_pmodule_component_t ucf_over_ucg_component;

static ucs_status_t ucf_over_ucg_query(ucs_list_link_t *desc_head)
{
    ucf_plan_desc_t *desc = UCS_ALLOC_TYPE_CHECK(ucf_plan_desc_t);
    desc->component       = &ucf_over_ucg_component;

    ucs_list_add_head(desc_head, &desc->list);
    return UCS_OK;
}

static ucs_status_t ucf_over_ucg_init(ucf_comp_ctx_h cctx,
                                      const ucs_pmodule_component_params_t *params,
                                      ucf_comp_config_t *config)
{
    ucf_over_ucg_ctx_t *fctx = cctx;
    ucs_status_t status      = ucs_config_parser_clone_opts(config, &fctx->config,
                                                            ucf_over_ucg_config_table);
    if (status != UCS_OK) {
        return status;
    }

    /* TODO: initialize per-process data-structures */

    return UCS_OK;
}

static void ucf_over_ucg_finalize(ucf_comp_ctx_h cctx)
{
    //ucf_over_ucg_ctx_t *over_ucg_ctx = (ucf_over_ucg_ctx_t*)ctx;
    /* TODO: destroy per-process data-structures */
}

static ucs_status_t ucf_over_ucg_create(ucf_comp_ctx_h cctx,
                                        ucf_file_ctx_t *fctx,
                                        ucs_pmodule_target_t *target,
                                        const ucs_pmodule_target_params_t *params)
{
    //ucf_over_ucg_file_ctx_t *fctx = ...

    //ucf_over_ucg_ctx_t *over_ucg_ctx      = (ucf_over_ucg_ctx_t*)ctx;
    //ucf_over_ucg_ctx_t *over_ucg_fctx     = (ucf_over_ucg_file_ctx_t*)octx;
    //ucf_file_h file                       = ucs_derived_of(object);
    // const ucf_file_params_t *file_params = ucs_derived_of(params);
    /* TODO: create a "parallel file" where I/O could be performed */
    return UCS_OK;
}

static void ucf_over_ucg_destroy(ucf_file_ctx_t *fctx)
{
    //ucf_over_ucg_ctx_t *over_ucg_fctx = (ucf_over_ucg_file_ctx_t*)fctx;
    /* TODO: destroy this instance of a parallel file */

    /* Note: fctx is freed as part of the group object itself */
}

static ucs_status_t ucf_over_ucg_plan_estimate(ucf_file_ctx_t *fctx,
                                               ucs_pmodule_target_action_params_h params,
                                               double *estimate_p)
{
    //const ucf_iop_params_t *iop_params = params;
    *estimate_p = 3.0; // TODO: a real estimate
    return UCS_OK;
}

static ucs_status_t ucf_over_ucg_plan_create(ucf_file_ctx_t *fctx,
                                             ucs_pmodule_target_action_params_h params,
                                             ucs_pmodule_target_plan_t **plan_p)
{
    //ucf_over_ucg_ctx_t *over_ucg_fctx  = (ucf_over_ucg_file_ctx_t*)octx;
    //const ucf_io_params_t *io_params = ucs_derived_of(params);
    /* TODO: prepare an I/O operation for later triggering */
    return UCS_ERR_NOT_IMPLEMENTED;
}

static ucs_status_ptr_t ucf_over_ucg_plan_trigger(ucs_pmodule_target_plan_t *plan,
                                                  uint16_t id, void *request)
{
    // TODO: implement
    return UCS_STATUS_PTR(UCS_ERR_NOT_IMPLEMENTED);
}

static unsigned ucf_over_ucg_plan_progress(ucs_pmodule_target_action_t *action)
{

    ucf_over_ucg_op_t *iop = ucs_derived_of(action, ucf_over_ucg_op_t);
    return iop->progress_f(iop->ucg_op);
}

static void ucf_over_ucg_plan_discard(ucs_pmodule_target_plan_t *plan)
{
    // TODO: implement
}

static void ucf_over_ucg_plan_print(ucs_pmodule_target_plan_t *plan)
{
    // TODO: implement
}

static ucs_status_t ucf_over_ucg_handle_fault(ucf_file_ctx_t *fctx, uint64_t id)
{
    // ucg_group_member_index_t index = id;
    return UCS_ERR_NOT_IMPLEMENTED;
}

UCS_PMODULE_COMPONENT_DEFINE(ucf_over_ucg_component, "over_ucg",
                             sizeof(ucf_over_ucg_ctx_t),
                             sizeof(ucf_over_ucg_file_ctx_t),
                             ucf_over_ucg_query, ucf_over_ucg_init,
                             ucf_over_ucg_finalize, ucf_over_ucg_create,
                             ucf_over_ucg_destroy, ucf_over_ucg_plan_estimate,
                             ucf_over_ucg_plan_create, ucf_over_ucg_plan_trigger,
                             ucf_over_ucg_plan_progress, ucf_over_ucg_plan_discard,
                             ucf_over_ucg_plan_print, ucf_over_ucg_handle_fault,
                             ucf_over_ucg_config_table, ucf_over_ucg_config_t,
                             "UCF_OVER_UCG_", ucf_components_list);
