/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <stddef.h>
#include <ucs/sys/compiler_def.h>

#include "over_ucp.h"
#include "over_ucp_comp.inl"

/******************************************************************************
 *                                                                            *
 *                         Operation Step Creation                            *
 *                                                                            *
 ******************************************************************************/

static ucs_status_t ucg_over_ucp_connect(ucg_over_ucp_group_ctx_t *ctx,
                                         ucg_over_ucp_plan_phase_t *phase,
                                         const ucs_int_array_t *dest_array,
                                         int is_mock, ucp_ep_h **ep_base_p,
                                         ucg_group_member_index_t **idx_base_p)
{
    ucp_ep_h *ucp_eps;

#if ENABLE_DEBUG_DATA
    unsigned idx;
    ucg_group_member_index_t peer;

    if ((phase->dest.ep_cnt = ucs_int_array_get_elem_count(dest_array)) == 1) {
        ucs_int_array_lookup(dest_array, 0, phase->dest.index);
    } else {
        phase->dest.indexes = *idx_base_p;
        ucs_int_array_for_each(peer, idx, dest_array) {
            **idx_base_p = peer;
            ++*idx_base_p;
        }
    }
#endif

    if (ucs_int_array_is_empty(dest_array) || is_mock) {
        return UCS_OK;
    }

    if ((phase->dest.ep_cnt = ucs_int_array_get_elem_count(dest_array)) == 1) {
        ucp_eps = &phase->dest.single_ep;
    } else {
        ucp_eps     = phase->dest.multi_eps = *ep_base_p;
        *ep_base_p += phase->dest.ep_cnt;
    }

    return ucg_plan_connect_p2p(ctx->group, dest_array, NULL, ucp_eps,
                                NULL, NULL, NULL);
}

static ucs_status_t ucg_over_ucp_convert_datatype(void *param_datatype,
                                                  ucp_datatype_t *ucp_datatype)
{
    if (ucs_unlikely(param_datatype == NULL)) {
        *ucp_datatype = ucp_dt_make_contig(1);
        return UCS_OK;
    }

    if (ucs_likely(ucg_global_params.field_mask & UCG_PARAM_FIELD_DATATYPE_CB)) {
        int ret = ucg_global_params.datatype.convert_f(param_datatype, ucp_datatype);
        if (ucs_unlikely(ret != 0)) {
            ucs_error("Datatype conversion callback failed");
            return UCS_ERR_INVALID_PARAM;
        }
    } else {
        *ucp_datatype = (ucp_datatype_t)param_datatype;
    }

    return UCS_OK;
}

static ucs_status_t
ucg_over_ucp_plan_phase_rx(ucg_over_ucp_plan_phase_t *phase,
                           const ucg_collective_params_t *params,
                           ucg_group_member_index_t my_index,
                           enum ucg_collective_modifiers modifiers,
                           int8_t **current_data_buffer,
                           const ucg_topo_desc_step_t *step)
{
    size_t rx_dt_size;
    ucs_status_t status;
    int i, rx_from_every_peer       = step->flags &
                                      UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER;
    ucg_group_member_index_t rx_cnt = rx_from_every_peer ? step->rx.count - 1 : 1;

    int not_a2a_v_or_w = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID;
    int is_mock        = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS;
    int is_v           = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC;
    int is_reduction   = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE;
    phase->rx_cnt      = rx_cnt;
    phase->rx.step_idx = step->rx.step_idx;

    if (!not_a2a_v_or_w && !is_v) {
        phase->rx.var_dts = UCS_ALLOC_CHECK(rx_cnt * sizeof(ucp_datatype_t),
                                            "ucg_over_ucp_plan_rx_dt_array");
        status = UCS_OK;
        for (i = 0; (i < rx_cnt) && (status == UCS_OK); i++) {
            status = ucg_over_ucp_convert_datatype(params->recv.dtypes[i],
                                                   &phase->rx.var_dts[i]);
        }
    } else {
        status = ucg_over_ucp_convert_datatype(params->recv.dtype,
                                               &phase->rx.dt);
    }
    if (status != UCS_OK) {
        return status;
    }

    if (is_v) {
        phase->rx.var_counts = params->recv.counts;
    } else {
        rx_dt_size      = ucp_dt_length(phase->rx.dt, 1, NULL, NULL);
        phase->rx.count = rx_dt_size ? step->rx.msg_size / rx_dt_size : 0;
    }

    if (!not_a2a_v_or_w) {
        phase->rx.var_displs = params->recv.displs;
    } else if (is_v) {
        phase->rx.stride = UCG_OVER_UCP_PLAN_PHASE_STRIDE_VARIADIC;
    } else {
        phase->rx.stride = is_reduction ? 0 : step->rx.msg_size;
    }

    /* Check for barrier - no need for an RX buffer in that case */
    if (!step->rx.msg_size) {
        return UCS_OK;
    }

    if ((params->recv.buffer == NULL) ||
        ((phase->rx.count > params->recv.count) && !is_reduction)) {
        /**
         * A temporary buffer of a larger size is needed, for example for
         * scattering and gathering.
         */
        ucs_assert(!is_v);
        ucs_assert(UCP_DT_IS_CONTIG(phase->rx.dt));

        if (!*current_data_buffer) {
            if (is_mock) {
                *current_data_buffer = ucs_strdup("temp-buffer",
                                                  "ucg_over_ucp_plan_temp_buffer");
            } else if (ucs_posix_memalign((void**)current_data_buffer,
                                          UCS_SYS_CACHE_LINE_SIZE,
                                          step->rx.msg_size,
                                          "ucg_over_ucp_plan_temp_buffer")) {
                return UCS_ERR_NO_MEMORY;
            }
        }
    } else {
        *current_data_buffer = (int8_t*)params->recv.buffer;
    }

    phase->rx.buffer = *current_data_buffer;
    ucs_assert(*current_data_buffer != NULL);
    if (is_reduction && step->rx.msg_size) {
        phase->init_act = UCG_OVER_UCP_PLAN_INIT_ACTION_COPY_SEND_TO_RECV;
    }

    return UCS_OK;
}

static ucs_status_t
ucg_over_ucp_plan_phase_tx(ucg_over_ucp_plan_phase_t *phase,
                           const ucg_collective_params_t *params,
                           ucg_group_member_index_t my_index,
                           enum ucg_collective_modifiers modifiers,
                           int8_t **current_data_buffer,
                           const ucg_topo_desc_step_t *step)
{
    unsigned i;
    size_t tx_dt_size;
    ucs_status_t status;
    ucg_group_member_index_t proc_cnt = phase->dest.ep_cnt + 1;

    int is_a2a_v_or_w  = !(modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID);
    int is_v           = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC;
    int is_bcast       = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST;
    phase->tx.step_idx = step->tx.step_idx;

    if (is_a2a_v_or_w && !is_v) {
        status            = UCS_OK;
        phase->tx.var_dts = UCS_ALLOC_CHECK(proc_cnt * sizeof(ucp_datatype_t),
                                            "ucg_over_ucp_plan_tx_dt_array");
        for (i = 0; (i < proc_cnt) && (status == UCS_OK); i++) {
            status = ucg_over_ucp_convert_datatype(params->send.dtypes[i],
                                                   &phase->tx.var_dts[i]);
        }
    } else {
        status = ucg_over_ucp_convert_datatype(params->send.dtype,
                                               &phase->tx.dt);
    }
    if (status != UCS_OK) {
        return status;
    }

    if (is_v) {
        phase->tx.var_counts = params->send.counts;
    } else {
        tx_dt_size      = ucp_dt_length(phase->tx.dt, 1, NULL, NULL);
        phase->tx.count = tx_dt_size ? step->tx.msg_size / tx_dt_size : 0;
    }

    if (is_a2a_v_or_w) {
        phase->tx.var_displs = params->send.displs;
        phase->tx.stride     = UCG_OVER_UCP_PLAN_PHASE_STRIDE_VARIADIC;
    } else if (is_v) {
        phase->tx.stride = UCG_OVER_UCP_PLAN_PHASE_STRIDE_VARIADIC;
    } else {
        phase->tx.stride = is_bcast ? 0 : step->tx.msg_size;
    }

    if (*current_data_buffer) {
        phase->tx.buffer = *current_data_buffer;
    } else if (params->send.buffer == ucg_global_params.mpi_in_place) {
        phase->tx.buffer = (int8_t*)params->recv.buffer;
    } else {
        phase->tx.buffer = (int8_t*)params->send.buffer;
    }

    return UCS_OK;
}

ucs_status_t ucg_over_ucp_phase_create(ucg_over_ucp_plan_t *plan, int8_t **buf,
                                       const ucg_group_params_t *group_params,
                                       const ucg_collective_params_t *coll_params,
                                       const ucg_topo_desc_step_t *step,
                                       enum ucg_group_member_distance topo_level,
                                       unsigned *req_cnt_p, ucp_ep_h **ep_base_p,
                                       ucg_group_member_index_t **idx_base_p)
{
    ucs_status_t status;
    ucg_over_ucp_group_ctx_t *gctx     = plan->gctx;
    ucg_over_ucp_plan_phase_t *phase   = &plan->phss[plan->phs_cnt];
    enum ucg_collective_modifiers mods = UCG_PARAM_MODIFIERS(coll_params);
    ucg_group_member_index_t my_index  = group_params->member_index;

    /* Start with no action during phase initialization and finalization */
    phase->init_act = UCG_OVER_UCP_PLAN_INIT_ACTION_NONE;
    phase->fini_act = UCG_OVER_UCP_PLAN_FINI_ACTION_NONE;

    /* Setup RX-side of this phase, if applicable */
    if (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) {
        status = ucg_over_ucp_plan_phase_rx(phase, coll_params, my_index, mods,
                                            buf, step);
        if (status != UCS_OK) {
            return status;
        }
    } else {
        phase->rx_cnt    = 0;
        phase->rx.stride = 0; /* for @ref ucg_over_ucp_phase_discard() */
    }

    /* Setup TX-side of this phase, if applicable */
    if (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) {
        status = ucg_over_ucp_plan_phase_tx(phase, coll_params, my_index, mods,
                                            buf, step);
        if (status != UCS_OK) {
            return status;
        }
    } else {
        phase->dest.ep_cnt = 0;
        phase->tx.stride   = 0; /* for @ref ucg_over_ucp_phase_discard() */
    }

    *req_cnt_p = ucs_max(*req_cnt_p, phase->rx_cnt + phase->dest.ep_cnt);

    /* Finish by connecting the endpoint according to the send/recv pattern */
    return ucg_over_ucp_connect(gctx, phase, &step->tx.tx_send_dests,
                                mods & UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS,
                                ep_base_p, idx_base_p);
}

void ucg_over_ucp_phase_discard(ucg_over_ucp_plan_phase_t *phase)
{
    if (phase->tx.stride == UCG_OVER_UCP_PLAN_PHASE_STRIDE_VARIADIC) {
        ucs_free(phase->tx.var_dts);
    }

    if (phase->rx.stride == UCG_OVER_UCP_PLAN_PHASE_STRIDE_VARIADIC) {
        ucs_free(phase->rx.var_dts);
    }
}
