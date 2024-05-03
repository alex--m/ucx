/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCF_OVER_UCG_H_
#define UCF_OVER_UCG_H_

#include <ucg/api/ucg.h>
#include <ucf/api/ucf.h>
#include <ucs/arch/cpu.h>
#include <ucs/pmodule/target.h>

/******************************************************************************
 *                                                                            *
 *                               Per-process context                          *
 *                                                                            *
 ******************************************************************************/

typedef struct ucf_over_ucg_config {
    // TODO?
} ucf_over_ucg_config_t;

typedef struct ucf_over_ucg_ctx {
    ucf_over_ucg_config_t config;
    // TODO?
} ucf_over_ucg_ctx_t;

/******************************************************************************
 *                                                                            *
 *                               File-related context                         *
 *                                                                            *
 ******************************************************************************/

typedef struct ucf_over_ucg_file_ctx {
    ucg_group_h group;
} UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE) ucf_over_ucg_file_ctx_t;

typedef struct ucf_over_ucg_plan {
    ucs_pmodule_target_plan_t super;
    ucf_over_ucg_file_ctx_t   *fctx;
    ucg_coll_h                coll_by_io_type[UCF_IO_LAST_TYPE];
} ucf_over_ucg_plan_t;


/******************************************************************************
 *                                                                            *
 *                             I/O Operation Execution                        *
 *                                                                            *
 ******************************************************************************/

typedef struct ucf_over_ucg_op {
    ucs_pmodule_target_action_t super;
    ucg_coll_h ucg_op;
    ucg_collective_progress_t progress_f;
} ucf_over_ucg_op_t;

#endif