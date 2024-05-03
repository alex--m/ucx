/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>
#include <ucs/arch/atomic.h>
#include <ucs/profile/profile.h>

#include "over_ucp.h"
#include "over_ucp_comp.inl"

/******************************************************************************
 *                                                                            *
 *                         Operation Step Execution                           *
 *                                                                            *
 ******************************************************************************/

static inline ucs_status_t ucg_over_ucp_execute_tx(ucg_over_ucp_op_t *op)
{
    ucs_status_ptr_t ret;
    ucp_ep_h *ep_iter;
    uint8_t *tx_iter;
    uint8_t ep_idx;
    int count;

    ucg_over_ucp_plan_phase_t *phase = op->phase;
    ucp_tag_t tag                    = op->base_tag.tag;
    ucp_request_param_t params       = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_REQUEST  |
                        UCP_OP_ATTR_FIELD_CALLBACK |
                        UCP_OP_ATTR_FIELD_DATATYPE,
        .request      = &op->reqs[0],
        .cb.send      = ucg_over_ucp_comp_send_cb
    };

    ep_iter = (phase->dest.ep_cnt == 1) ? &phase->dest.single_ep :
                                          &phase->dest.multi_eps[0];

    for (ep_idx = 0; ep_idx < phase->dest.ep_cnt; ep_idx++, params.request++) {
        tx_iter = ucg_over_ucp_plan_phase_calc_buffer(&phase->tx, ep_idx,
                                                      &params.datatype, &count);
        ret     = ucp_tag_send_nbx(*(ep_iter++), tx_iter, (size_t)count, tag++,
                                   &params);

        /* In UCP immediate completion doesn't trigger the callback, so we do */
        if (!UCS_PTR_IS_PTR(ret)) {
            ucg_over_ucp_comp_send_cb(op, UCS_PTR_STATUS(ret), NULL);
            if (ucs_unlikely(UCS_PTR_STATUS(ret) != UCS_OK)) {
                return UCS_PTR_STATUS(ret);
            }
        }
    }

    return UCS_INPROGRESS;
}

static inline ucs_status_t ucg_over_ucp_execute_rx(ucg_over_ucp_op_t *op)
{
    ucs_status_ptr_t ret;
    uint8_t *rx_iter;
    uint8_t rx_idx;
    ucp_tag_t tag;
    unsigned count;

    ucp_tag_t mask                   = (ucp_tag_t)-1;
    ucg_over_ucp_plan_phase_t *phase = op->phase;
    int is_reduction                 = phase->rx.stride == 0;
    int is_var                       = phase->rx.stride ==
                                       UCG_OVER_UCP_PLAN_PHASE_STRIDE_VARIADIC;
    ucp_request_param_t params       = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_REQUEST  |
                        UCP_OP_ATTR_FIELD_CALLBACK |
                        UCP_OP_ATTR_FIELD_DATATYPE,
        .request      = &op->reqs[0],
        .cb.recv      = is_reduction ? ucg_over_ucp_comp_reduce_cb :
                                       ucg_over_ucp_comp_recv_cb,
        .datatype     = is_var ? phase->rx.dt : phase->rx.var_dts[0]
    };

    tag = op->base_tag.tag + phase->me; // TODO: need to have a tag per RX

    for (rx_idx = 0; rx_idx < phase->rx_cnt; rx_idx++, params.request++) {
        rx_iter = ucg_over_ucp_plan_phase_calc_buffer(&phase->rx, rx_idx,
                                                      &params.datatype, &count);
        ret = ucp_tag_recv_nbx(op->worker, rx_iter, (size_t)count, tag++, mask,
                               &params);

        /* In UCP immediate completion doesn't trigger the callback, so we do */
        if (!UCS_PTR_IS_PTR(ret)) {
            ucg_over_ucp_comp_send_cb(op, UCS_PTR_STATUS(ret), NULL);
            if (ucs_unlikely(UCS_PTR_STATUS(ret) != UCS_OK)) {
                return UCS_PTR_STATUS(ret);
            }
        }
    }

    return UCS_INPROGRESS;
}

UCS_PROFILE_FUNC(ucs_status_ptr_t, ucg_over_ucp_execute, (op, first_invocation),
                 ucg_over_ucp_op_t *op, int first_invocation)
{
    ucs_status_t status;

    if (first_invocation) {
        status = ucg_over_ucp_execute_rx(op);
        if (status != UCS_OK) {
            goto incomplete;
        }
    }

    status = ucg_over_ucp_execute_tx(op);
    if (status == UCS_OK) {
        goto incomplete;
    }

    return op;

incomplete:
    return UCS_STATUS_PTR(status);
}
