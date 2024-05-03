/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_OVER_UCB_H_
#define UCG_OVER_UCB_H_

#include <ucg/over_uct/over_uct.h>

typedef struct ucg_over_ucb_config {
    ucg_over_uct_config_t super;
} ucg_over_ucb_config_t;

typedef struct ucg_over_ucb_ctx {
    ucg_over_uct_ctx_t    super;
    ucg_over_ucb_config_t config;
    // TODO?
} ucg_over_ucb_ctx_t;

/******************************************************************************
 *                                                                            *
 *                               Sending Messages                             *
 *                                                                            *
 ******************************************************************************/

typedef ucg_over_uct_plan_t ucg_over_ucb_plan_t;

ucs_status_t ucg_over_ucb_plan_estimate(ucg_group_ctx_h ctx,
                                        ucs_pmodule_target_action_params_h params,
                                        double *estimate_p);

ucs_status_t ucg_over_ucb_plan_create(ucg_group_ctx_h ctx,
                                      ucs_pmodule_target_action_params_h params,
                                      ucs_pmodule_target_plan_t **plan_p);

void ucg_over_ucb_plan_destroy(ucg_over_ucb_plan_t *plan);

void ucg_over_ucb_plan_print(ucs_pmodule_target_plan_t *plan);


/******************************************************************************
 *                                                                            *
 *                             Operation Execution                            *
 *                                                                            *
 ******************************************************************************/

typedef struct ucg_over_ucb_op {
    ucg_over_uct_op_t super;
    // TODO?
} ucg_over_ucb_op_t;

typedef struct ucg_over_ucb_group_ctx {
    ucg_over_uct_group_ctx_t super;
    ucb_pipes_h              pipes;
} UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE) ucg_over_ucb_group_ctx_t;


ucs_status_ptr_t ucg_over_ucb_plan_execute(ucg_over_ucb_op_t *op);

ucs_status_ptr_t ucg_over_ucb_plan_trigger(ucs_pmodule_target_plan_t *plan,
                                           uint16_t id, void *request);

unsigned ucg_over_ucb_plan_progress(ucs_pmodule_target_action_t *action);

void ucg_over_ucb_plan_discard(ucs_pmodule_target_plan_t *plan);

#endif
