/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <uct/api/uct.h>
#include <uct/base/uct_md.h>

#include "over_uct.h"
#include "over_uct_comp.inl"

static const char *uct_coll_operator_names[] = {
    [UCT_COLL_OPERATOR_EXTERNAL]   = "external",
    [UCT_COLL_OPERATOR_MIN]        = "min",
    [UCT_COLL_OPERATOR_MAX]        = "max",
    [UCT_COLL_OPERATOR_SUM]        = "sum",
    [UCT_COLL_OPERATOR_SUM_ATOMIC] = "atomic sum"
};

static const char *uct_coll_operand_names[] = {
    [UCT_COLL_OPERAND_FLOAT]    = "float",
    [UCT_COLL_OPERAND_DOUBLE]   = "double",
    [UCT_COLL_OPERAND_INT8_T]   = "int8_t",
    [UCT_COLL_OPERAND_INT16_T]  = "int16_t",
    [UCT_COLL_OPERAND_INT32_T]  = "int32_t",
    [UCT_COLL_OPERAND_INT64_T]  = "int64_t",
    [UCT_COLL_OPERAND_UINT8_T]  = "uint8_t",
    [UCT_COLL_OPERAND_UINT16_T] = "uint16_t",
    [UCT_COLL_OPERAND_UINT32_T] = "uint32_t",
    [UCT_COLL_OPERAND_UINT64_T] = "uint64_t"
};

#define UCG_OVER_UCT_FORCE_P2P_FLAGS \
    (UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC | \
     UCG_GROUP_COLLECTIVE_INTERNAL_MODIFIER_P2P_INSTEAD_OF_BCAST)

static inline ucg_group_member_index_t
ucg_over_uct_phase_incast_threshold(ucg_over_ucp_group_ctx_t *gctx)
{
    return ((ucg_over_uct_ctx_t*)gctx->ctx)->config.incast_member_thresh;
}

static inline ucg_group_member_index_t
ucg_over_uct_phase_bcast_threshold(ucg_over_ucp_group_ctx_t *gctx)
{
    return ((ucg_over_uct_ctx_t*)gctx->ctx)->config.bcast_member_thresh;
}

static inline ucg_group_member_index_t
ucg_over_uct_phase_zcopy_threshold(ucg_over_ucp_group_ctx_t *gctx)
{
    return ((ucg_over_uct_ctx_t*)gctx->ctx)->config.zcopy_total_thresh;
}

static inline uct_coll_length_info_t
ucg_over_uct_choose_incast_mode(ucg_over_uct_plan_phase_t *phase,
                                const ucg_collective_params_t *params,
                                const uct_iface_attr_t *iface_attr)
{
    uint16_t modifiers = UCG_PARAM_MODIFIERS(params);
    if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE) {
        if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC) {
            return UCT_COLL_LENGTH_INFO_VAR_COUNT;
        }

        return UCT_COLL_LENGTH_INFO_PACKED;
    }

    if (!(modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID) &&
         (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC)) {
        return UCT_COLL_LENGTH_INFO_VAR_DTYPE;
    }

    return UCT_COLL_LENGTH_INFO_DEFAULT;
}

ucs_status_t over_uct_phase_rkey_pair_use(void **src_ptr, uct_rkey_bundle_t *src_rkey_p,
                                          void **dst_ptr, uct_rkey_bundle_t *dst_rkey_p,
                                          uct_md_h md, uct_component_h *component_p,
                                          size_t *len_p)
{
    ucs_status_t status;
    UCS_V_UNUSED uct_component_h component;

    ucs_assert(((ucg_over_uct_rkey_msg_t*)*src_ptr)->magic == RKEY_MAGIC);
    ucs_assert(((ucg_over_uct_rkey_msg_t*)*dst_ptr)->magic == RKEY_MAGIC);
    ucs_assert_always(((ucg_over_uct_rkey_msg_t*)*dst_ptr)->remote_length ==
                      ((ucg_over_uct_rkey_msg_t*)*src_ptr)->remote_length);

    status = ucg_over_uct_comp_use_rkey(md, (ucg_over_uct_rkey_msg_t*)*src_ptr,
                                        src_ptr, src_rkey_p, component_p);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    status = ucg_over_uct_comp_use_rkey(md, (ucg_over_uct_rkey_msg_t*)*dst_ptr,
                                        dst_ptr, dst_rkey_p, &component);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    *len_p = ((ucg_over_uct_rkey_msg_t*)*dst_ptr)->remote_length;
    ucs_assert(component == *component_p);
    return UCS_OK;
}

void ucg_over_uct_phase_rkey_pair_discard(uct_rkey_bundle_t *src_rkey_p,
                                          uct_rkey_bundle_t *dst_rkey_p,
                                          uct_component_h component)
{
    ucg_over_uct_comp_discard_rkey(src_rkey_p, component);
    ucg_over_uct_comp_discard_rkey(dst_rkey_p, component);
}

static int
ucg_over_uct_phase_rkey_reduce_cb(void *operator, const void *src, void *dst,
                                  unsigned count, void *datatype, uct_md_h md)
{
    int ret;
    ucs_status_t status;
    size_t buffer_length;
    uct_component_h component;
    uct_rkey_bundle_t src_rkey;
    uct_rkey_bundle_t dst_rkey;

    /* Treat both source and destination pointers as pointers to rkeys */
    status = over_uct_phase_rkey_pair_use((void**)&src, &src_rkey, (void**)&dst,
                                          &dst_rkey, md, &component,
                                          &buffer_length);
    if (ucs_unlikely(status != UCS_OK)) {
        return -1;
    }

    /* Do the actual reduction (using the original, external callback) */
    ret = ucg_global_params.reduce_op.reduce_cb_f(operator, src, dst, count,
                                                  datatype, NULL);

    /* Discard the rkeys */
    ucg_over_uct_phase_rkey_pair_discard(&src_rkey, &dst_rkey, component);

    return ret;
}

/**
 * This function creates endpoints of two types: point-to-point and collective,
 * corresponding to the main transport used in the endpoint. A collective
 * endpoint would be created even it ends up not being applied, seeing as the
 * creation itself is a collective operation and every peer must participate.
 */
static inline ucs_status_t
ucg_over_uct_phase_connect_direction(ucg_over_uct_plan_t *plan,
                                     ucg_over_uct_plan_phase_t *phase,
                                     const ucg_over_ucp_plan_phase_t *ucp_phase,
                                     const ucg_topo_desc_step_t *step, int use_rx,
                                     const ucg_collective_params_t *params,
                                     const ucg_over_ucp_plan_dt_info_t *dt,
                                     int *requires_optimization_p,
                                     uct_iface_h *iface_p,
                                     const uct_iface_attr_t **iface_attr_p,
                                     uct_ep_h **ep_base_p, int force_p2p)
{
    int is_incast;
    int is_leader;
    ucp_ep_h ucp_ep;
    uct_ep_h uct_ep;
    unsigned op_count;
    uct_md_h *final_md;
    ucp_ep_h *tx_ucp_eps;
    uct_ep_h *tx_uct_eps;
    uct_incast_operand_t operand;
    uct_incast_operator_t operator;
    uct_coll_length_info_t dt_info;
    const ucs_int_array_t *dest_array;
    ucg_group_member_index_t coll_index;
    ucg_group_member_index_t coll_count;
    ucg_over_uct_phase_extra_info_t *info;
    ucg_group_member_index_t coll_threshold;
    ucg_group_member_index_t bcast_threshold;
    ucg_group_member_index_t incast_threshold;
    enum ucg_over_uct_phase_extra_info_slots info_set;

    ucg_over_uct_header_t header   = {0};
    ucg_over_uct_group_ctx_t *gctx = plan->super.gctx;
    ucg_group_h group              = gctx->super.group;
    ucg_over_uct_ctx_t *tctx       = (ucg_over_uct_ctx_t*)gctx->super.ctx;
    uint8_t wireup_am_id           = tctx->wireup_am_id;
    ucs_status_t status            = UCS_OK;
    uint16_t modifiers             = UCG_PARAM_MODIFIERS(params);
    uint8_t is_buf_cache_unaligned = (uintptr_t)(use_rx ?
                                                 ucp_phase->rx.buffer :
                                                 ucp_phase->tx.buffer) %
                                     UCS_SYS_CACHE_LINE_SIZE;
    int is_mock                    = modifiers &
                                     UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS;
    info_set                       = use_rx ? UCG_OVER_UCT_PHASE_EXTRA_INFO_RX :
                                              UCG_OVER_UCT_PHASE_EXTRA_INFO_TX;
    info                           = &phase->info[info_set];
    final_md                       = &info->md;
    info->aux_memh                 = NULL;
    info->is_coll_tl_used          = 0;

    /* Start with P2P connections - low risk low reward... */
    if (use_rx) {
        /* Connect to a peer, to choose the interface to poll for RX messages */
        if (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) {
            if (is_mock) {
                status = ucg_plan_connect_mock(group, 0, 0, iface_p,
                                               iface_attr_p, final_md);
            } else {
                status = ucg_plan_connect_p2p_single(group, step->rx.rx_peer,
                                                     &uct_ep, &ucp_ep, iface_p,
                                                     final_md, iface_attr_p);
            }
        }
    } else if (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) {
        dest_array       = &step->tx.tx_send_dests;
        phase->tx.ep_cnt = ucs_int_array_get_elem_count(dest_array);
        ucs_assert(phase->tx.ep_cnt > 0);
        if (phase->tx.ep_cnt == 1)  {
            tx_uct_eps  = &phase->tx.single_ep;
            tx_ucp_eps  = &ucp_ep;
        } else {
            tx_uct_eps  = phase->tx.multi_eps = *ep_base_p;
            tx_ucp_eps  = ucs_alloca(phase->tx.ep_cnt * sizeof(ucp_ep_h));
            *ep_base_p += phase->tx.ep_cnt;
        }

        if (is_mock) {
            status = ucg_plan_connect_mock(group, 0, 0, iface_p, iface_attr_p,
                                           final_md);

            /* Store the information later about the transmission transport */
            phase->tx.mock.iface_attr = *iface_attr_p;
            ucs_assert(*iface_attr_p != NULL);
        } else {
            status = ucg_plan_connect_p2p(group, dest_array, tx_uct_eps,
                                          tx_ucp_eps, iface_p, final_md,
                                          iface_attr_p);
        }
    }

    if (status != UCS_OK) {
        return status;
    }

    /* Fault-tolerance (currently unstable) */
#ifdef ENABLE_FAULT_TOLERANCE
    if (phase->handles == NULL) {
        phase->handles = UCS_ALLOC_CHECK(sizeof(ucg_ft_h) * phase->tx.ep_cnt,
                                         "ucg_phase_handles");
    }

    /* Send information about any faults that may have happened */
    status = ucg_ft_propagate(ctx->group, ctx->group_params, ep);
    if (status != UCS_OK) {
        return status;
    }
#endif

    if (force_p2p ||
        (( use_rx) && (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_NO_LEADERSHIP)) ||
        ((!use_rx) && (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_NO_LEADERSHIP))) {
        return UCS_OK;
    }

    /* Prepare for reduction operations */
    op_count = dt->msg_size / dt->dt_size;
    if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE) {
        /*
         * Chicken-and-egg problem here:
         * in order to choose the right callback function, need to know if the
         * transport will fragment a message at this length. This information
         * only becomes available after the transport has been created - with a
         * callback function from the user. To address this - a second call to
         * this function may be made after the transport has been selected.
         * This call only checks the operand and operator.
         */
        status = ucg_plan_choose_reduction_cb(UCG_PARAM_OP(params), dt->orig_dt,
                                              dt->dt_size, op_count, 0, 0,
                                              &operand, &operator, NULL);
        if (status == UCS_ERR_UNSUPPORTED) {
            /* No built-in function for this reduction - use the fall-back */
            ucs_info("No internal reduction function available for this datatpye"
                     " - falling back to an external callback function");
            operator = UCT_COLL_OPERATOR_EXTERNAL;
            operand  = UCT_COLL_OPERAND_LAST;
        } else if (status != UCS_OK) {
            return status;
        }
        ucs_debug("Reduction selected for the %s operator and %s operand",
                  uct_coll_operator_names[operator],
                  uct_coll_operand_names[operand]);
    } else {
        operand = UCT_COLL_OPERAND_LAST;
    }

    /* Generate a unique 64-bit identifier for this connection establishment */
    header.group_id     = gctx->super.group_id;
    header.msg.coll_id  = ucg_group_get_next_coll_id(group);
    header.msg.step_idx = use_rx ? step->rx.step_idx : step->tx.step_idx;

    /* Sanity checks */
    ucs_assert(header.group_id     >= UCG_GROUP_FIRST_GROUP_ID);
    ucs_assert(header.msg.step_idx >= UCG_GROUP_FIRST_STEP_IDX);
    ucs_assert((( use_rx) &&  (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID)) ||
               ((!use_rx) &&  (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID)));
    ucs_assert((( use_rx) && !(step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_NO_LEADERSHIP)) ||
               ((!use_rx) && !(step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_NO_LEADERSHIP)));

    /* Now attempt to connect collective transports (overwrite some P2P vars) */
    is_incast        = use_rx ? (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_LEADER) :
                               !(step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_LEADER);
    bcast_threshold  = ucg_over_uct_phase_bcast_threshold(&gctx->super);
    incast_threshold = ucg_over_uct_phase_incast_threshold(&gctx->super);
    coll_threshold   = is_incast ? incast_threshold : bcast_threshold;
    coll_count       = use_rx ? step->rx.count : step->tx.count;

    if (is_mock) {
        status = ucg_plan_connect_mock(group, coll_count >= coll_threshold,
                                       is_incast, iface_p, iface_attr_p, final_md);

        if (!use_rx) {
            /* Store the information later about the transmission transport */
            phase->tx.mock.iface_attr = *iface_attr_p;
            ucs_assert(*iface_attr_p != NULL);
        }
    } else {
        is_leader = use_rx ? (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_LEADER) :
                             (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_LEADER);
        status    = ucg_plan_connect_coll(group, header.header, step,
                                          is_leader, is_incast, wireup_am_id,
                                          operator, operand, op_count,
                                          !is_buf_cache_unaligned, dt->msg_size,
                                          ucg_over_uct_phase_rkey_reduce_cb,
                                          UCG_PARAM_OP(params), dt->orig_dt,
                                          coll_threshold, &coll_index, &coll_count,
                                          &phase->tx.single_ep, &phase->tx.ep_cnt,
                                          iface_p, final_md, iface_attr_p,
                                          requires_optimization_p,
                                          &info->coll_iface_base,
                                          &info->coll_iface_bits);

        ucs_assert_always((status != UCS_OK) ||
                          (coll_count < (typeof(phase->rx.batch_cnt))-1));
        info->is_coll_tl_used = (status == UCS_OK);
    }

    /**
     * Fragmented collective transports are not suported yet. It's difficult
     * because the size of the incoming packet then also depends on the maximal
     * fragment size, not just on the number of operands, so different incoming
     * packets contain different sizes... the logic to handle this might not be
     * cost-effective performance-wise. So, with a heavy heart, we fallback to
     * point-to-point communication in this case.
     */
    if (status == UCS_ERR_EXCEEDS_LIMIT) {
        status = UCS_OK;
    } else if (status != UCS_OK) {
        if (status == UCS_ERR_UNREACHABLE) {
            ucs_info("Peer is unreachable by collective transports");
        }
        goto retry_with_force_p2p;
    }

    /* If this collective transport is not supported better figure it out now */
    if ((*iface_attr_p)->cap.flags & UCT_IFACE_FLAG_INCAST) {
        dt_info = ucg_over_uct_choose_incast_mode(phase, params, *iface_attr_p);
        if (!((*iface_attr_p)->cap.am.coll_mode_flags & UCS_BIT(dt_info))) {
            ucs_warn("The requested datatype mode is not supported by the iface");
            goto retry_with_force_p2p;
        }

        ucs_assert(operand <= UCT_COLL_OPERAND_LAST);
        ucs_assert(operator < UCT_COLL_OPERATOR_LAST);
        ucs_assert(((*iface_attr_p)->cap.coll_mode.short_flags |
                    (*iface_attr_p)->cap.coll_mode.bcopy_flags |
                    (*iface_attr_p)->cap.coll_mode.zcopy_flags) &
                   UCS_BIT(dt_info));


        if ((operand != UCT_COLL_OPERAND_LAST) &&
            !((*iface_attr_p)->cap.coll_mode.operands & UCS_BIT(operand))) {
            ucs_warn("The requested operand is not supported by the transport");
            goto retry_with_force_p2p;
        }

        if ((operator != UCT_COLL_OPERATOR_EXTERNAL) &&
            !((*iface_attr_p)->cap.coll_mode.operators & UCS_BIT(operator))) {
            ucs_warn("The requested operand is not supported by the transport");
            goto retry_with_force_p2p;
        }
    }

    ucs_assert(*final_md != NULL);
    return status;

retry_with_force_p2p:
    ucs_assert(!force_p2p);
    ucs_info("Falling back to P2P communication, no collective transports used "
             "(%u operands)", op_count);
    info->is_coll_tl_used = 0;
    return ucg_over_uct_phase_connect_direction(plan, phase, ucp_phase, step,
                                                use_rx, params, dt,
                                                requires_optimization_p, iface_p,
                                                iface_attr_p, ep_base_p, 1);
}

static inline ucs_status_t
ucg_over_uct_phase_connect(ucg_over_uct_plan_t *plan,
                           ucg_over_uct_plan_phase_t *phase,
                           const ucg_topo_desc_step_t *step,
                           const ucg_over_ucp_plan_phase_t *ucp_phase,
                           const ucg_collective_params_t *params,
                           const ucg_over_ucp_plan_dt_info_t *rx_dt,
                           const ucg_over_ucp_plan_dt_info_t *tx_dt,
                           const uct_iface_attr_t **rx_iface_attr_p,
                           const uct_iface_attr_t **tx_iface_attr_p,
                           int *requires_optimization_p,
                           uct_ep_h **ep_base_p, int force_p2p)
{
    ucs_status_t status;

    if (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) {
        status = ucg_over_uct_phase_connect_direction(plan, phase, ucp_phase,
                                                      step, 1, params, rx_dt,
                                                      requires_optimization_p,
                                                      &phase->rx.iface,
                                                      rx_iface_attr_p,
                                                      ep_base_p, force_p2p);
        if (status != UCS_OK) {
            return status;
        }
    }

    if (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) {
        status = ucg_over_uct_phase_connect_direction(plan, phase, ucp_phase,
                                                      step, 0, params, tx_dt,
                                                      requires_optimization_p,
                                                      &phase->tx.iface,
                                                      tx_iface_attr_p,
                                                      ep_base_p, force_p2p);
        if (status != UCS_OK) {
            return status;
        }
    }

    return UCS_OK;
}

#define UCG_OVER_UCT_PHASE_SET_FRAGMENTED(_max, _tx_len, _dt_len, _frag_len, \
                                          _frag_cnt) \
    _frag_len = (uint32_t)((_max) - ((_max) % (_dt_len))); \
    ucs_assert(_frag_len == ((_max) - ((_max) % (_dt_len)))); \
    _frag_cnt = (((_tx_len) / (_frag_len)) + \
                (((_tx_len) % (_frag_len)) > 0));

static inline ucs_status_t
ucg_over_uct_phase_choose_flags_and_len(const ucg_collective_params_t *params,
                                        const uct_iface_attr_t *iface_attr,
                                        uct_iface_h iface, size_t dt_length,
                                        uct_coll_length_info_t dt_mode,
                                        size_t zcopy_threshold,
                                        unsigned *fragments_per_send_p,
                                        uint32_t *fragment_length_p,
                                        uint16_t *send_flag_p,
                                        size_t *tx_length_p,
                                        int *is_tl_incast_p,
                                        int *is_tl_slotted_p,
                                        int *is_tl_unordered_p,
                                        int *is_tl_bcast_p, void **send_f_p)
{
    int is_coll;
    uint64_t cap_flags = iface_attr->cap.flags;
    uint64_t dt_flag   = UCS_BIT(dt_mode);
    int supports_short = cap_flags & UCT_IFACE_FLAG_AM_SHORT;
    int supports_bcopy = cap_flags & UCT_IFACE_FLAG_AM_BCOPY;
    int supports_zcopy = cap_flags & UCT_IFACE_FLAG_AM_ZCOPY;
    *is_tl_incast_p    = (cap_flags & UCT_IFACE_FLAG_INCAST) != 0;
    *is_tl_slotted_p   = (cap_flags & UCT_IFACE_FLAG_INCAST_SLOTTED) != 0;
    *is_tl_unordered_p = (cap_flags & UCT_IFACE_FLAG_INCAST_UNORDERED) != 0;
    *is_tl_bcast_p     = (cap_flags & UCT_IFACE_FLAG_BCAST) != 0;
    is_coll            = *is_tl_bcast_p || *is_tl_incast_p;
    ucs_assert(!is_coll || (dt_flag & iface_attr->cap.am.coll_mode_flags));

    /*
     * Short messages
     */
    if (ucs_likely(supports_short)) {
        size_t max_short = iface_attr->cap.am.max_short;
        ucs_assert(iface_attr->cap.am.max_short > sizeof(ucg_over_uct_header_t));
        if (ucs_likely(*tx_length_p <= max_short)) {
            /* Short send - single message */
            if (is_coll) {
                *tx_length_p = UCT_COLL_LENGTH_INFO_PACK(dt_mode, *tx_length_p);
            }

            *fragments_per_send_p = 1;
            *send_flag_p          = UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_SHORT <<
                                    UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS;
            *send_f_p             = iface->ops.ep_am_short;

            if (!is_coll || (iface_attr->cap.coll_mode.short_flags & dt_flag)) {
                return UCS_OK;
            }

            ucs_error("The requested datatype mode is not supported in short");
            return UCS_ERR_UNSUPPORTED;
        }

#if SHORT_AND_BCOPY_HAVE_DIFFERENT_OVERHEAD
        size_t max_bcopy       = iface_attr->cap.am.max_bcopy;
        size_t short_msg_count = length / max_short + ((length % max_short) != 0);
        size_t bcopy_msg_count = supports_bcopy ? ((length / max_bcopy) +
                                                   ((length % max_bcopy) != 0)):
                                                  SIZE_MAX;
        int is_short_best = (short_msg_count * iface_attr->overhead_short) <
                            (bcopy_msg_count * iface_attr->overhead_bcopy);

        if (is_short_best || (!supports_bcopy && !supports_zcopy && !is_coll))
#else
        if (!supports_bcopy && !supports_zcopy && !is_coll)
#endif
        {
            /* Short send - multiple messages */
            *send_flag_p  = ((is_coll ? dt_mode : UCT_COLL_LENGTH_INFO_DEFAULT) <<
                             UCG_OVER_UCT_PLAN_PHASE_LENGTH_INFO_SHIFT) |
                            (UCG_OVER_UCT_PLAN_PHASE_FLAG_FRAGMENTED) |
                            (UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_SHORT <<
                              UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS);
            *send_f_p     = iface->ops.ep_am_short;

            UCG_OVER_UCT_PHASE_SET_FRAGMENTED(max_short, *tx_length_p,
                                              dt_length, *fragment_length_p,
                                              *fragments_per_send_p);

            return UCS_OK;
        }
    }

    /*
     * Large messages (zero-copy sends)
     */
    if (supports_zcopy) {
        if (*tx_length_p > zcopy_threshold) {
            size_t max_zcopy = iface_attr->cap.am.max_zcopy -
                               sizeof(ucg_over_uct_header_t);
            ucs_assert(iface_attr->cap.am.max_zcopy >
                       sizeof(ucg_over_uct_header_t));
            if (ucs_likely(*tx_length_p <= max_zcopy)) {
                /* zero-copy send - single message */
                *fragments_per_send_p = 1;
                *send_flag_p          = UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY <<
                                        UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS;
                *send_f_p             = iface->ops.ep_am_zcopy;
            } else {
                /* zero-copy send - single message */
                *send_flag_p = UCG_OVER_UCT_PLAN_PHASE_FLAG_FRAGMENTED |
                               (UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY <<
                                UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS);
                *send_f_p    = iface->ops.ep_am_zcopy;

                UCG_OVER_UCT_PHASE_SET_FRAGMENTED(max_zcopy, *tx_length_p,
                                                  dt_length, *fragment_length_p,
                                                  *fragments_per_send_p);
            }

            if (!is_coll || (iface_attr->cap.coll_mode.zcopy_flags & dt_flag)) {
                return UCS_OK;
            }

            ucs_error("The requested datatype mode is not supported in zcopy");
            return UCS_ERR_UNSUPPORTED;
        }
    }

    if (ucs_unlikely(!supports_bcopy)) {
        ucs_error("collective not supported by any transport type");
        return UCS_ERR_UNSUPPORTED;
    }

    /*
     * Medium messages (buffer-copy)
     */
    size_t max_bcopy = iface_attr->cap.am.max_bcopy -
                       sizeof(ucg_over_uct_header_t);
    ucs_assert(iface_attr->cap.am.max_bcopy >
               sizeof(ucg_over_uct_header_t));
    if (ucs_likely(*tx_length_p <= max_bcopy)) {
        /* BCopy send - single message */
        *fragments_per_send_p = 1;
        *send_flag_p          = UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_BCOPY <<
                                UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS;
        *send_f_p             = iface->ops.ep_am_bcopy;
    } else {
        /* BCopy send - multiple messages */
        *send_flag_p = UCG_OVER_UCT_PLAN_PHASE_FLAG_FRAGMENTED |
                       (UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_BCOPY <<
                        UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS);
        *send_f_p    = iface->ops.ep_am_bcopy;

        UCG_OVER_UCT_PHASE_SET_FRAGMENTED(max_bcopy, *tx_length_p, dt_length,
                                          *fragment_length_p,
                                          *fragments_per_send_p);
    }

    if (!is_coll || (iface_attr->cap.coll_mode.bcopy_flags & dt_flag)) {
        return UCS_OK;
    }

    ucs_error("The requested datatype mode is not supported in bcopy");
    return UCS_ERR_UNSUPPORTED;
}

static inline ucs_status_t
ucg_over_uct_phase_setup_tx(ucg_over_uct_plan_t *plan,
                            ucg_over_uct_plan_phase_t *phase,
                            const ucg_collective_params_t *params,
                            const ucg_topo_desc_step_t *step,
                            const ucg_over_ucp_plan_phase_t *ucp_phase,
                            const ucg_over_ucp_plan_dt_info_t *dt,
                            const uct_iface_attr_t *iface_attr,
                            uint8_t am_id, int force_p2p)
{
    size_t tx_len;
    size_t zthresh;
    int is_tl_bcast;
    int is_tl_incast;
    int is_tl_slotted;
    int is_tl_unordered;
    uint16_t modifiers;
    ucs_status_t status;
    unsigned fragments_per_tx;
    enum uct_msg_flags bcopy_flag;
    uct_coll_length_info_t dt_mode;
    ucg_over_ucp_group_ctx_t *gctx = plan->super.gctx;

    ucs_assert_always(step->tx.count != 0);
    tx_len  = step->tx.msg_size;
    zthresh = ucg_over_uct_phase_zcopy_threshold(gctx) / step->tx.count;
    dt_mode = ucg_over_uct_choose_incast_mode(phase, params, iface_attr);
    status  = ucg_over_uct_phase_choose_flags_and_len(params, iface_attr,
                                                      phase->tx.iface,
                                                      dt->dt_size, dt_mode,
                                                      zthresh, &fragments_per_tx,
                                                      &phase->tx.frag_len,
                                                      &phase->flags, &tx_len,
                                                      &is_tl_incast,
                                                      &is_tl_slotted,
                                                      &is_tl_unordered,
                                                      &is_tl_bcast,
                                                      &phase->tx.send);
    if (status != UCS_OK) {
        if (status == UCS_ERR_UNSUPPORTED) {
            ucs_error("the unsupported datatype was %u", dt_mode);
        }
        return status;
    }

    modifiers                         = UCG_PARAM_MODIFIERS(params);
    phase->tx.am_id                   = am_id;
    phase->tx.am_header.group_id      = gctx->group_id;
    phase->tx.am_header.msg.step_idx  = step->tx.step_idx;
    phase->tx.am_header.remote_offset = 0;
    phase->tx.length                  = tx_len;
    phase->tx.buffer                  = ucp_phase->tx.buffer;
    phase->tx.root                    = step->tx.index;

    /* Make sure local_id is always nonzero ( @ref ucg_over_uct_header_step_t )*/
    ucs_assert_always(ucp_phase->tx.step_idx >= UCG_GROUP_FIRST_STEP_IDX);
    ucs_assert_always(phase->tx.am_header.group_id >= UCG_GROUP_FIRST_GROUP_ID);
    ucs_assert_always(phase->tx.send != (void*)ucs_empty_function_do_assert);
    ucs_assert_always(phase->tx.send != NULL);

    if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST) {
        phase->flags |= UCG_OVER_UCT_PLAN_PHASE_FLAG_BCAST;
    }

    switch (ucg_over_uct_plan_phase_flags_get_method(phase->flags)) {
    case UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_BCOPY:
        bcopy_flag    = UCT_SEND_FLAG_CB_REDUCES;
        phase->flags |= UCT_COLL_LENGTH_INFO_PACK(0, bcopy_flag) <<
                        UCG_OVER_UCT_PLAN_PHASE_LENGTH_INFO_SHIFT;
        status = ucg_over_uct_pack_select_cb(phase, params, dt, iface_attr,
                                             fragments_per_tx > 1);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }
        break;

    case UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY:
        status = uct_md_mem_reg(phase->info[UCG_OVER_UCT_PHASE_EXTRA_INFO_TX].md,
                                phase->tx.buffer, phase->tx.length,
                                UCT_MD_MEM_ACCESS_LOCAL_READ,
                                &phase->tx.zcopy.memh);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }
        break;

    default:
        ucs_assert(ucg_over_uct_plan_phase_flags_get_method(phase->flags) ==
                   UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_SHORT);
        break;
    }

    if ((modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE) &&
        !is_tl_incast && !is_tl_bcast) {
        /* Assume my peers have a higher rank/index for offset calculation */
        phase->tx.am_header.remote_offset = step->tx.index * dt->msg_size;
    }

    if (phase->tx.ep_cnt == 1) {
        phase->flags |= UCG_OVER_UCT_PLAN_PHASE_FLAG_SINGLE_ENDPOINT;
    }

    return UCS_OK;
}

static inline ucs_status_t
ucg_over_uct_phase_setup_comp_flags(ucg_over_uct_plan_t *plan,
                                    ucg_over_uct_plan_phase_t *phase,
                                    const ucg_collective_params_t *params,
                                    const ucg_over_ucp_plan_dt_info_t *rx_dt,
                                    enum ucg_over_uct_plan_phase_flags flags,
                                    int is_incast, int is_tl_slotted,
                                    unsigned fragments_per_send)
{
    ucg_over_ucp_group_ctx_t *gctx;
    ucg_group_member_index_t group_size;
    enum ucg_over_uct_plan_phase_comp_flags comp_flags = 0;


    if (is_incast && is_tl_slotted) {
        comp_flags |= UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_SLOT_LENGTH;
    }

    if (fragments_per_send > 1) {
        /* RX will be fragmented */
        comp_flags |= UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_FRAGMENTED_DATA;

        if (plan->max_frag < fragments_per_send) {
            plan->max_frag = fragments_per_send;
        }
    }

    if (!rx_dt->is_contig) {
        /* The (generic?) datatype will be packed and will require unpacking */
        comp_flags |= UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_GENERIC_DATATYPE;
    }

    gctx       = plan->super.gctx;
    group_size = ucg_group_get_params(gctx->group)->member_count;
    ucs_assert_always((rx_dt->msg_size * group_size) < (ucg_offset_t)-1);

    phase->rx.comp_flags = comp_flags;
    return UCS_OK;
}

static inline ucs_status_t
ucg_over_uct_phase_setup_aggregation(ucg_over_uct_plan_t *plan,
                                     ucg_over_uct_plan_phase_t *phase,
                                     const ucg_collective_params_t *params,
                                     enum ucg_over_uct_plan_phase_flags flags,
                                     int is_incast, unsigned frags_cnt,
                                     int rx_from_every_peer, int ext_reduce_cb)
{
    enum ucg_over_uct_plan_phase_comp_aggregation aggregation;

    uint16_t modifiers = UCG_PARAM_MODIFIERS(params);
    int is_reduce      = (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE);
    int is_concatenate = (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE);
    int is_barrier     = is_reduce && !params->send.dtype;
    int is_pipelined   = (flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_PIPELINED);
    int is_rkey        = ((ucg_over_uct_plan_phase_flags_get_method(flags) ==
                           UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_PUT_ZCOPY) ||
                          (ucg_over_uct_plan_phase_flags_get_method(flags) ==
                           UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_GET_ZCOPY));

    if (is_barrier || !frags_cnt) {
        aggregation = UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_NOP;
    } else if (is_rkey) {
        aggregation = UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY;
    } else if (is_reduce && rx_from_every_peer) {
        aggregation = ext_reduce_cb ?
                      UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_EXTERNAL :
                      UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_INTERNAL;
    } else if (is_pipelined) {
        aggregation = UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_PIPELINE;
    } else if ((frags_cnt == 1) && (!is_concatenate)) {
        aggregation = UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_NO_OFFSET;
    } else {
        aggregation = UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_WITH_OFFSET;
    }

    phase->rx.comp_agg = aggregation;
    return UCS_OK;
}

static inline ucs_status_t
ucg_over_uct_phase_setup_criteria(ucg_over_uct_plan_t *plan,
                                  ucg_over_uct_plan_phase_t *phase,
                                  const ucg_collective_params_t *params,
                                  enum ucg_over_uct_plan_phase_flags flags,
                                  unsigned frags_cnt, int fragments_per_send)
{
    enum ucg_over_uct_plan_phase_comp_criteria criteria;
    int is_fragmented = (fragments_per_send > 1);
    int is_zcopy      = (ucg_over_uct_plan_phase_flags_get_method(flags) ==
                         UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY);

    if ((frags_cnt == 1) && !is_fragmented) {
        criteria = UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_SINGLE_MESSAGE;
    } else {
        criteria = is_zcopy ?
                   UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES_ZCOPY :
                   UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES;
    }

    phase->rx.comp_criteria = criteria;
    return UCS_OK;
}

static inline ucs_status_t
ucg_over_uct_phase_setup_action(ucg_over_uct_plan_t *plan,
                                ucg_over_uct_plan_phase_t *phase)
{
    enum ucg_over_uct_plan_phase_comp_action action;

    if (&plan->phss[plan->super.phs_cnt - 1] == phase) {
        action = UCG_OVER_UCT_PLAN_PHASE_COMP_OP;
    } else {
        action = UCG_OVER_UCT_PLAN_PHASE_COMP_STEP;
    }

    if ((phase->flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_EXPECTS_RX) &&
        (ucg_over_uct_plan_phase_flags_get_method(phase->flags))) {
        action |= UCG_OVER_UCT_PLAN_PHASE_COMP_SEND;
    }

    phase->rx.comp_action = action;
    return UCS_OK;
}

static inline ucs_status_t
ucg_over_uct_phase_set_completion(ucg_over_uct_plan_t *plan,
                                  ucg_over_uct_plan_phase_t *phase,
                                  const ucg_collective_params_t *params,
                                  const ucg_over_ucp_plan_dt_info_t *rx_dt,
                                  uint32_t send_flags, int is_incast,
                                  int is_tl_slotted, unsigned frags_cnt,
                                  unsigned fragments_per_send,
                                  int rx_from_every_peer, int ext_reduce_cb)
{
    ucs_status_t status;

    status = ucg_over_uct_phase_setup_comp_flags(plan, phase, params, rx_dt,
                                                 send_flags, is_incast,
                                                 is_tl_slotted,
                                                 fragments_per_send);
    if (status != UCS_OK) {
        return status;
    }

    status = ucg_over_uct_phase_setup_aggregation(plan, phase, params,
                                                  send_flags, is_incast,
                                                  frags_cnt, rx_from_every_peer,
                                                  ext_reduce_cb);
    if (status != UCS_OK) {
        return status;
    }

    status = ucg_over_uct_phase_setup_criteria(plan, phase, params, send_flags,
                                               frags_cnt, fragments_per_send);
    if (status != UCS_OK) {
        return status;
    }

    return ucg_over_uct_phase_setup_action(plan, phase);
}

static inline ucs_status_t
ucg_over_uct_phase_setup_rx(ucg_over_uct_plan_t *plan,
                            ucg_over_uct_plan_phase_t *phase,
                            const ucg_collective_params_t *params,
                            const ucg_topo_desc_step_t *step,
                            const ucg_over_ucp_plan_phase_t *ucp_phase,
                            const ucg_over_ucp_plan_dt_info_t *rx_dt,
                            const ucg_over_ucp_plan_dt_info_t *tx_dt,
                            const uct_iface_attr_t *iface_attr, int force_p2p,
                            uint16_t *rx_send_flags_p)
{
    size_t tmp_len;
    size_t zthresh;
    unsigned rx_cnt;
    int is_tl_bcast;
    int is_tl_incast;
    int is_tl_slotted;
    int is_tl_unordered;
    ucs_status_t status;
    void *ignored_send_f;
    uint32_t frag_length;
    int rx_from_every_peer;
    unsigned frags_per_send;
    uct_incast_operand_t operand;
    uct_incast_operator_t operator;
    uct_coll_length_info_t dt_mode;
    ucg_over_ucp_group_ctx_t *gctx;
    uint8_t is_buf_cache_unaligned;
    const ucg_over_ucp_plan_dt_info_t *reduction_dt;
    uct_reduction_internal_cb_t reduction_cb = NULL;

#if ENABLE_ASSERT
    phase->rx.reduce_f = (uct_reduction_internal_cb_t)ucs_empty_function_do_assert;
#endif

    if (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) {
        ucs_assert_always(step->rx.step_idx >= UCG_GROUP_FIRST_STEP_IDX);
        gctx    = plan->super.gctx;
        rx_cnt  = step->rx.count - 1;
        tmp_len = frag_length = step->rx.msg_size;
        zthresh = ucg_over_uct_phase_zcopy_threshold(gctx) / rx_cnt;
        dt_mode = ucg_over_uct_choose_incast_mode(phase, params, iface_attr);
        status  = ucg_over_uct_phase_choose_flags_and_len(params, iface_attr,
                                                          phase->rx.iface,
                                                          rx_dt->dt_size,
                                                          dt_mode, zthresh,
                                                          &frags_per_send,
                                                          &frag_length,
                                                          rx_send_flags_p,
                                                          &tmp_len,
                                                          &is_tl_incast,
                                                          &is_tl_slotted,
                                                          &is_tl_unordered,
                                                          &is_tl_bcast,
                                                          &ignored_send_f);
        if (status != UCS_OK) {
            if (status == UCS_ERR_UNSUPPORTED) {
                ucs_error("the unsupported datatype mode was %u", dt_mode);
            }
            return status;
        }

        rx_from_every_peer  = (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER) != 0;
        reduction_dt        = (ucp_phase->rx.stride == 0) ? rx_dt : NULL;
        phase->rx.batch_cnt = (is_tl_incast && is_tl_slotted && !is_tl_unordered)
                              ? rx_cnt : 1;
        phase->rx.frags_cnt = ((rx_from_every_peer ^ is_tl_incast) ?
                              rx_cnt : 1) * frags_per_send;
        phase->rx.batch_len = step->rx.msg_size;
        phase->rx.buffer    = ucp_phase->rx.buffer;
        phase->rx.step_idx  = step->rx.step_idx;
        phase->flags       |= UCG_OVER_UCT_PLAN_PHASE_FLAG_EXPECTS_RX;
        ucs_assert(phase->rx.step_idx >= UCG_GROUP_FIRST_STEP_IDX);

        if (UCG_PARAM_MODIFIERS(params) & UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE) {
            ucs_assert(reduction_dt != NULL);
            phase->rx.reduce_f     = NULL;
            is_buf_cache_unaligned = (phase->rx.batch_len %
                                       UCS_SYS_CACHE_LINE_SIZE) ||
                                     (((uintptr_t)ucp_phase->rx.buffer %
                                       UCS_SYS_CACHE_LINE_SIZE) ||
                                     !is_tl_incast);

            /*
             * If bcopy is selected - alignment is no longer checked or enforced.
             * The main problem is with the  ucg_over_uct_plan_phase_zcopy_rkey()
             * optimization, where rkey might be aligned but no guarantee the
             * remote buffer is too. Another consideration is that the benefit
             * might not be as significant when it comes to large buffers.
             */
            if (*rx_send_flags_p & (UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_BCOPY  <<
                                    UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS)) {
                is_buf_cache_unaligned = 1;
            }

            status = ucg_plan_choose_reduction_cb(UCG_PARAM_OP(params),
                                                  reduction_dt->orig_dt,
                                                  reduction_dt->dt_size,
                                                  frags_per_send > 1 ? 0 :
                                                      step->rx.msg_size /
                                                      reduction_dt->dt_size,
                                                  frags_per_send > 1,
                                                  !is_buf_cache_unaligned,
                                                  &operand, &operator,
                                                  &reduction_cb);
            if ((status != UCS_OK) && (status != UCS_ERR_UNSUPPORTED)) {
                ucs_assert(reduction_cb == NULL);
                return status;
            } else if (status == UCS_OK) {
                phase->rx.reduce_f = reduction_cb;
            }

            if (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) {
                ucg_over_uct_pack_check_internal_reduce(phase, frags_per_send > 1);
            }
        }
    } else {
        status              = UCS_OK;
        *rx_send_flags_p    = 0;
        phase->rx.frags_cnt = 0; /* read during ucg_over_uct_plan_op_init() */
        phase->rx.step_idx  = 0; /* read during ucg_over_uct_plan_op_init() */
        phase->rx.batch_len = 0; /* read during ucg_over_uct_plan_print() */
        frags_per_send      = 0;
        is_tl_incast        = 0;
        is_tl_slotted       = 0;
        rx_cnt              = 0;
        rx_from_every_peer  = 0;
        reduction_dt        = (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) ?
                              tx_dt : NULL;
    }

    return ucg_over_uct_phase_set_completion(plan, phase, params, rx_dt,
                                             *rx_send_flags_p, is_tl_incast,
                                             is_tl_slotted && !is_tl_unordered,
                                             phase->rx.frags_cnt, frags_per_send,
                                             rx_from_every_peer,
                                             status == UCS_ERR_UNSUPPORTED);
}

static inline ucs_status_t
ucg_over_uct_phase_setup(ucg_over_uct_plan_t *plan,
                         ucg_over_uct_plan_phase_t *phase,
                         const ucg_topo_desc_step_t *step,
                         const ucg_over_ucp_plan_phase_t *ucp_phase,
                         const ucg_collective_params_t *params,
                         const ucg_over_ucp_plan_dt_info_t *rx_dt,
                         const ucg_over_ucp_plan_dt_info_t *tx_dt,
                         const uct_iface_attr_t *rx_iface_attr,
                         const uct_iface_attr_t *tx_iface_attr,
                         uint8_t am_id, int force_p2p,
                         uint16_t *rx_send_flags_p)
{
    ucs_status_t status;
    uint16_t mods = UCG_PARAM_MODIFIERS(params);

    /* TX is set up only if there are actual messages to send in this phase */
    if (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) {
        status = ucg_over_uct_phase_setup_tx(plan, phase, params, step,
                                             ucp_phase, tx_dt, tx_iface_attr,
                                             am_id, force_p2p);
        if (status != UCS_OK) {
            return status;
        }
    } else {
        phase->flags = 0;
    }

    /* RX is set up anyway, because this includes the phase's completion flow */
    status = ucg_over_uct_phase_setup_rx(plan, phase, params, step, ucp_phase,
                                         rx_dt, tx_dt, rx_iface_attr, force_p2p,
                                         rx_send_flags_p);
    if (status != UCS_OK) {
        return status;
    }

    /* Re-use any temporary buffer, store it properly for later cleanup */
    if (phase->rx.comp_agg != UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_NOP) {
        if ((step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_LEADER) ||
            (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_NO_LEADERSHIP)) {
            plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_COPY_SRC_BUFFER;
            plan->copy_dst = ucp_phase->rx.buffer;
            ucs_assert(plan->copy_dst != NULL);
            if (plan->copy_dst != params->recv.buffer) {
                ucg_over_uct_phase_set_temp_buffer(phase, plan->copy_dst,
                                                   UCG_OVER_UCT_PHASE_EXTRA_INFO_TX);
            }
        }

        if (mods & UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE) {
            plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_COPY_DST_OFFSET;
        }

        if ((UCG_PARAM_ROOT(params) == plan->my_index) &&
            (mods & UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE) &&
            !(mods & UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST)) {
            plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_COPY_SRC_OFFSET;
        }
    }

    /* Pipelining corner case */
    if (phase->flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_PIPELINED) {
        plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_PIPELINED;
    }

    return UCS_OK;
}

ucs_status_t ucg_over_uct_phase_create(ucg_over_uct_plan_t *plan,
                                       ucg_over_uct_plan_phase_t *phase,
                                       const ucg_topo_desc_step_t *step,
                                       const ucg_over_ucp_plan_phase_t *super,
                                       const ucg_over_uct_config_t *config,
                                       const ucg_over_ucp_plan_dt_info_t *rx_dt,
                                       const ucg_over_ucp_plan_dt_info_t *tx_dt,
                                       const ucg_collective_params_t *params,
                                       int8_t **current_data_buffer,
                                       int *requires_optimization_p,
                                       uct_ep_h **ep_base_p,
                                       uint8_t am_id)
{
    ucs_status_t status;
    uint16_t rx_send_flags;
    const uct_iface_attr_t *rx_iface_attr = NULL;
    const uct_iface_attr_t *tx_iface_attr = NULL;
    uint16_t mods                         = UCG_PARAM_MODIFIERS(params);
    int force_p2p                         = ucs_test_flags(mods,
                                                UCG_OVER_UCT_FORCE_P2P_FLAGS);

    /* Connect the UCT endpoints */
    status = ucg_over_uct_phase_connect(plan, phase, step, super, params, rx_dt,
                                        tx_dt, &rx_iface_attr, &tx_iface_attr,
                                        requires_optimization_p, ep_base_p,
                                        force_p2p);
    if (status != UCS_OK) {
        return status;
    }

    /* Define the parameters for this phase of the collective */
    status = ucg_over_uct_phase_setup(plan, phase, step, super, params, rx_dt,
                                      tx_dt, rx_iface_attr, tx_iface_attr, am_id,
                                      force_p2p, &rx_send_flags);
    if (status != UCS_OK) {
        return status;
    }

    return ucg_over_uct_optimize_plan(phase, config, params, rx_send_flags,
                                      rx_iface_attr, tx_iface_attr,
                                      &plan->opt_cnt);
}

void ucg_over_uct_phase_set_temp_buffer(ucg_over_uct_plan_phase_t *phase, void* buffer,
                                        enum ucg_over_uct_phase_extra_info_slots side)
{
    ucs_assert(side < 2);
    ucs_assert(phase->info[side].temp_buffer == NULL);
    phase->info[side].temp_buffer = buffer;
}

static void
ucg_over_uct_phase_destroy_info(ucg_over_uct_plan_phase_t *phase, ucg_group_h group,
                                enum ucg_over_uct_phase_extra_info_slots side)
{
    if (phase->info[side].is_coll_tl_used) {
        ucg_plan_disconnect_coll(group, phase->info[side].coll_iface_base,
                                 phase->info[side].coll_iface_bits);
    }

    if (phase->info[side].aux_memh) {
        uct_md_mem_dereg(phase->info[side].md, phase->info[side].aux_memh);
    }

    if (phase->info[side].temp_buffer) {
        ucs_free(phase->info[side].temp_buffer);
    }
}


void ucg_over_uct_phase_destroy(ucg_over_uct_plan_phase_t *phase,
                                const ucg_collective_params_t *params,
                                ucg_group_h group, int is_mock)
{
    uct_md_h tx_md = phase->info[UCG_OVER_UCT_PHASE_EXTRA_INFO_TX].md;

    if (ucg_over_uct_plan_phase_flags_get_method(phase->flags) ==
        UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY) {
        uct_md_mem_dereg(tx_md, phase->tx.zcopy.memh);
    }

    ucg_over_uct_phase_destroy_info(phase, group, UCG_OVER_UCT_PHASE_EXTRA_INFO_RX);
    ucg_over_uct_phase_destroy_info(phase, group, UCG_OVER_UCT_PHASE_EXTRA_INFO_TX);
}
