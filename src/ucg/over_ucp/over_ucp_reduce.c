/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_ucp.h"

#include <ucp/dt/dt.inl>


static size_t
ucg_over_ucp_reduce(void *dst, const void *src, uintptr_t op)
{
    const ucg_collective_params_t *params =
        ucg_plan_get_params(((ucg_op_t*)op)->plan);

    ucs_assert(params->recv.count < (unsigned)-1);
    ucg_global_params.reduce_op.reduce_cb_f(UCG_PARAM_OP(params),
                                            src, dst, params->recv.count,
                                            params->recv.dtype, NULL);

    return ucp_dt_length(((ucg_over_ucp_op_t*)op)->phase->rx.dt,
                         ((ucg_over_ucp_op_t*)op)->phase->rx.count, NULL, NULL);
}

ucs_status_t ucg_over_ucp_reduce_select_cb(void *reduce_op,
                                           const ucg_over_ucp_plan_dt_info_t *d,
                                           uct_incast_operand_t *operand_p,
                                           uct_incast_operator_t *operator_p,
                                           uct_reduction_internal_cb_t *reduce_p)
{
    ucs_status_t status = UCS_ERR_UNSUPPORTED;

    if (d->is_contig) {
        status = ucg_plan_choose_reduction_cb(reduce_op, d->orig_dt,
                                              d->dt_size, d->msg_size /
                                              d->dt_size, 0, 0, operand_p,
                                              operator_p, reduce_p);
    }

    /* Fall-back to an external reduction callback function */
    if (status == UCS_ERR_UNSUPPORTED) {
        *reduce_p = ucg_over_ucp_reduce;
        status = UCS_OK;
    }

    return status;
}
