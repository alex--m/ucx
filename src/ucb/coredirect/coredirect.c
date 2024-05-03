/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucb/api/ucb.h>
#include <ucs/pmodule/component.h>

/**
 *  This file implements batches over CORE-Direct. For more information see -
 *  1. Info: https://www.mellanox.com/related-docs/whitepapers/TB_CORE-Direct.pdf
 *  2. Usage: https://github.com/mellanox-hpc/coredirect/tree/master/examples
 *  3. Prototype: https://github.com/Mellanox/pcx
 *
 */

static ucs_config_field_t ucb_coredirect_config_table[] = {
    {NULL}
};

typedef struct ucb_coredirect_config {
    /* configurable options */
    int dummy; /* To avoid zero-sized allocation warnings... */
} ucb_coredirect_config_t;

typedef struct ucb_coredirect_ctx {
    /* per-process context for using core-direct */
    int dummy; /* To avoid zero-sized allocation warnings... */
} ucb_coredirect_ctx_t;

typedef struct ucb_coredirect_pipes_ctx {
    uint64_t id; // example field
    /* per-use (e.g. collective operation) context */
} ucb_coredirect_pipes_ctx_t;

extern ucs_pmodule_component_t ucb_coredirect_component;
ucs_status_t ucb_coredirect_query(ucs_list_link_t *desc_head)
{
    ucs_pmodule_component_desc_t *desc;

    desc            = UCS_ALLOC_TYPE_CHECK(ucs_pmodule_component_desc_t);
    desc->component = &ucb_coredirect_component;

    ucs_list_add_head(desc_head, &desc->list);
    return UCS_OK;
}

static inline ucs_status_t
ucb_coredirect_once_per_process_init(ucb_coredirect_ctx_t *cd_ctx,
                                     ucb_coredirect_config_t *cd_config,
                                     const ucs_pmodule_component_params_t *params)
{
    return UCS_OK;
}

static inline void
ucb_coredirect_once_per_process_finalize(ucb_coredirect_ctx_t* cd_ctx)
{

}

static ucs_status_t ucb_coredirect_init(ucs_pmodule_component_ctx_h ctx,
                                        const ucs_pmodule_component_params_t *params,
                                        ucs_pmodule_component_config_h config)
{
    ucb_coredirect_ctx_t *cd_ctx       = (ucb_coredirect_ctx_t*)ctx;
    ucb_coredirect_config_t *cd_config = (ucb_coredirect_config_t*)config;

    return ucb_coredirect_once_per_process_init(cd_ctx, cd_config, params);
}

static void ucb_coredirect_finalize(ucs_pmodule_component_ctx_h ctx)
{
    ucb_coredirect_ctx_t *cd_ctx = (ucb_coredirect_ctx_t*)ctx;
    ucb_coredirect_once_per_process_finalize(cd_ctx);
}

static ucs_status_t ucb_coredirect_estimate(ucs_pmodule_target_ctx_h tctx,
                                            ucs_pmodule_target_action_params_h params,
                                            double *estimate_p)
{
    *estimate_p = 1.0; // TODO: a real performance estimate...
    return UCS_OK;
}

static inline ucs_status_t
ucb_coredirect_once_per_group_init(ucb_coredirect_ctx_t *cd_ctx,
                                   ucb_coredirect_pipes_ctx_t *pipes_ctx,
                                   ucb_pipes_h pipes,
                                   const ucb_pipes_params_t *pipes_params)
{
    pipes_ctx->id = 0;
    return UCS_OK;
}

static inline void
ucb_coredirect_once_per_group_finalize(ucb_coredirect_pipes_ctx_t* pipes_ctx)
{

}

/* create a new planner context for a group */
static ucs_status_t ucb_coredirect_create(ucs_pmodule_component_ctx_h cctx,
                                          ucs_pmodule_target_ctx_h tctx,
                                          ucs_pmodule_target_t *target,
                                          const ucs_pmodule_target_params_t *params)
{
    ucb_coredirect_ctx_t *cd_ctx           = (ucb_coredirect_ctx_t*)cctx;
    ucb_coredirect_pipes_ctx_t *pipes_ctx  = (ucb_coredirect_pipes_ctx_t*)tctx;
    ucb_pipes_h pipes                      = (ucb_pipes_h)target;
    const ucb_pipes_params_t *pipes_params = params->per_framework;

    /* TODO: initialize the use of core-direct (over multiple pipes) */
    return ucb_coredirect_once_per_group_init(cd_ctx, pipes_ctx, pipes,
                                              pipes_params);
}

static void ucb_coredirect_destroy(ucs_pmodule_target_ctx_h tctx)
{
    ucb_coredirect_pipes_ctx_t *pipes_ctx = (ucb_coredirect_pipes_ctx_t*)tctx;
    return ucb_coredirect_once_per_group_finalize(pipes_ctx);
}

static void ucb_coredirect_set_doorbell(int is_enabled)
{

}

static ucs_status_t ucb_coredirect_plan(ucs_pmodule_target_ctx_h tctx,
                                        const ucs_pmodule_target_action_params_h params,
                                        ucs_pmodule_target_plan_t **plan_p)
{
    ucb_coredirect_pipes_ctx_t *pipes_ctx  = (ucb_coredirect_pipes_ctx_t*)tctx;
    const ucb_batch_params_t *batch_params = params;

    /* TODO: plan a batch operation. It will be later pass to trigger() */

    ucb_coredirect_set_doorbell(0);

    batch_params->recorder_cb(pipes_ctx->id++, batch_params->recorder_arg);

    ucb_coredirect_set_doorbell(1);

    return UCS_ERR_NOT_IMPLEMENTED;
}

static ucs_status_ptr_t ucb_coredirect_trigger(ucs_pmodule_target_plan_t *plan,
                                               uint16_t id, void *request)
{
    /* TODO: ring the doorbell! */

    return UCS_STATUS_PTR(UCS_ERR_NOT_IMPLEMENTED);
}

static unsigned ucb_coredirect_progress(ucs_pmodule_target_action_t *batch)
{
    /* TODO: progress this request, possibly by calling lower-level progress */
    return 0;
}

static void ucb_coredirect_discard(ucs_pmodule_target_plan_t *plan)
{
    /* TODO: release the resources of a batch... maybe nothing to do here. */
}

static void ucb_coredirect_print(ucs_pmodule_target_plan_t *plan)
{
    /* TODO: print the pipes layout */
}

static ucs_status_t ucb_coredirect_handle_fault(ucs_pmodule_target_ctx_h tctx,
                                                uint64_t id)
{
    // ucb_coredirect_ctx_t *cd_fctx  = (ucb_coredirect_pipes_ctx_t*)tctx;

    return UCS_ERR_NOT_IMPLEMENTED;
}

UCS_PMODULE_COMPONENT_DEFINE(ucb_coredirect_component, "coredirect",
                             sizeof(ucb_coredirect_ctx_t),
                             sizeof(ucb_coredirect_pipes_ctx_t),
                             ucb_coredirect_query, ucb_coredirect_init,
                             ucb_coredirect_finalize, ucb_coredirect_create,
                             ucb_coredirect_destroy, ucb_coredirect_estimate,
                             ucb_coredirect_plan, ucb_coredirect_trigger,
                             ucb_coredirect_progress, ucb_coredirect_discard,
                             ucb_coredirect_print, ucb_coredirect_handle_fault,
                             ucb_coredirect_config_table, ucb_coredirect_config_t,
                             "COREDIRECT_", ucb_components_list);
