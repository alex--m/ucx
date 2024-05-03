/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_ucb.h"


#define UCG_OVER_UCB_DISABLE_DOORBELL(plan)
#define UCG_OVER_UCB_RESTORE_DOORBELL(plan)

static int ucg_over_ucb_supports(ucg_over_uct_plan_phase_t *phase)
{
    // TODO!
    return 0;
}

static void ucg_over_uct_execute_op_wrapper(uint64_t id, void *over_uct_op)
{
    (void) ucg_over_uct_execute_op((ucg_over_uct_op_t*)over_uct_op);
}

ucs_status_t ucg_over_ucb_phase_batchify(ucg_over_ucb_group_ctx_t *bctx,
                                         ucg_over_uct_plan_phase_t *phase)
{
    ucb_batch_h batch;
    ucs_status_t status;
    ucb_batch_params_t params;

    if (!ucg_over_ucb_supports(phase)) {
        return UCS_OK; /* fall-back */
    }

    params.recorder_cb = ucg_over_uct_execute_op_wrapper;

    status = ucb_batch_create(bctx->pipes, &params, &batch);
    if (status != UCS_OK) {
        return status;
    }

    phase->reserved    = batch;
    phase->tx.send = ucb_batch_start;
    return UCS_OK;
}

ucs_status_t ucg_over_ucb_plan_create(ucg_group_ctx_h ctx,
                                      ucs_pmodule_target_action_params_h params,
                                      ucs_pmodule_target_plan_t **plan_p)
{
    ucg_step_idx_t i;
    ucs_status_t status;
    ucg_step_idx_t phs_cnt;
    ucg_over_ucb_plan_t *plan;
    ucg_over_ucb_group_ctx_t *bctx = ctx;

    /* Start with a UCT-based plan */
    status = ucg_over_uct_plan_create(&bctx->super, params,
                                      (ucg_over_uct_plan_t**)plan_p);
    if (status != UCS_OK) {
        return status;
    }

    /* modify the plans to use batches where possible */
    plan    = ucs_derived_of(*plan_p, ucg_over_uct_plan_t);
    phs_cnt = plan->super.phs_cnt;
    for (i = 0; (i < phs_cnt) && (status == UCS_OK); i++) {
        status = ucg_over_ucb_phase_batchify(bctx, &plan->phss[i]);
    }

    if (status == UCS_OK) {
        return UCS_OK;
    }

alloc_failed:
    ucg_over_uct_plan_destroy(plan);
    return status;
}

void ucg_over_ucb_plan_phase_destroy(ucg_over_uct_plan_phase_t *phase)
{
    ucb_batch_destroy(&phase->reserved);
    ucg_over_uct_phase_destroy(phase, NULL, NULL, 0);
}

void ucg_over_ucb_plan_destroy(ucg_over_ucb_plan_t *plan)
{
    int i;
    for (i = 0; i < plan->super.phs_cnt; i++) {
        ucg_over_ucb_plan_phase_destroy(&plan->phss[i]);
    }

    ucg_over_uct_plan_destroy(plan);
}

ucs_status_t
ucg_over_ucb_plan_estimate(ucg_group_ctx_h ctx,
                           ucs_pmodule_target_action_params_h params,
                           double *estimate_p)
{
    //const ucg_collective_params_t *coll_params = params;
    *estimate_p = 3.0; // TODO: a real estimate
    return UCS_OK;
}

ucs_status_ptr_t ucg_over_ucb_plan_execute(ucg_over_ucb_op_t *op)
{
    // TODO: implement
    return UCS_STATUS_PTR(UCS_ERR_NOT_IMPLEMENTED);
}

ucs_status_ptr_t ucg_over_ucb_plan_trigger(ucs_pmodule_target_plan_t *plan,
                                           uint16_t id, void *request)
{
    // TODO: implement
    return UCS_STATUS_PTR(UCS_ERR_NOT_IMPLEMENTED);
}

unsigned ucg_over_ucb_plan_progress(ucs_pmodule_target_action_t *action)
{
    // TODO: implement
    return 0;
}

void ucg_over_ucb_plan_discard(ucs_pmodule_target_plan_t *plan)
{
    // TODO: implement
    return;
}