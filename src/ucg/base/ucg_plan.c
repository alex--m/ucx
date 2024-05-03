/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_plan.h"
#include "ucg_group.h"
#include "ucg_context.h"

#include <ucg/api/ucg.h>
#include <ucp/core/ucp_ep.inl>
#include <ucp/core/ucp_ep.inl>
#include <ucp/core/ucp_proxy_ep.h>
#include <uct/base/uct_component.h>
#include <uct/base/uct_md.h>
#include <ucs/config/parser.h>
#include <ucs/debug/log.h>
#include <ucs/debug/assert.h>
#include <ucs/debug/memtrack.h>
#include <ucs/type/class.h>
#include <ucs/sys/module.h>
#include <ucs/sys/string.h>
#include <ucs/arch/cpu.h>

void ucg_group_store_ep(khash_t(ucg_group_ep) *khash,
                        ucg_group_member_index_t index,
                        ucp_ep_h ep)
{
    int ret;
    khiter_t iter = kh_put(ucg_group_ep, khash, index, &ret);
    if (ret != UCS_KH_PUT_KEY_PRESENT) {
        kh_value(khash, iter) = ep;
    }
}

ucs_status_t ucg_plan_choose_reduction_cb(void *external_op, void *external_dt,
                                          size_t dt_size, uint64_t dt_count,
                                          int does_zero_dt_count_means_unknown,
                                          int input_buffers_are_cache_aligned,
                                          uct_incast_operand_t *operand_p,
                                          uct_incast_operator_t *operator_p,
                                          uct_reduction_internal_cb_t *reduce_p)
{
    int is_signed;
    int want_location;
    int is_commutative;
    enum ucg_operator operator;
    UCS_V_UNUSED uct_reduction_internal_cb_t memcpy;
    UCS_V_UNUSED uct_reduction_internal_cb_t reduce;

    if (!reduce_p) {
        reduce_p = &reduce;
    }

    /* Detect Barrier operations (no data) */
    if (!external_op) {
        *operator_p = UCT_COLL_OPERATOR_SUM;
        *operand_p  = UCT_COLL_OPERAND_UINT8_T;
        ucs_assert(input_buffers_are_cache_aligned);
        ucs_assert(!dt_count);
        goto skip_to_callbacks;
    }

    /* Detect the operator */
    if (!ucg_global_params.reduce_op.get_operator_f(external_op, &operator,
                                                   &want_location,
                                                   &is_commutative) &&
        !want_location) {
        ucs_assert(is_commutative);
        switch (operator) {
        case UCG_OPERATOR_MAX:
            *operator_p = UCT_COLL_OPERATOR_MAX;
            break;
        case UCG_OPERATOR_MIN:
            *operator_p = UCT_COLL_OPERATOR_MIN;
            break;
        case UCG_OPERATOR_SUM:
            *operator_p = UCT_COLL_OPERATOR_SUM;
            break;
        default:
            return UCS_ERR_UNSUPPORTED;
        }
    } else {
        return UCS_ERR_UNSUPPORTED;
    }

    /* Detect the operand */
    if (ucg_global_params.datatype.is_integer_f(external_dt, &is_signed)) {
        switch (dt_size) {
        case sizeof(uint8_t):
            *operand_p = is_signed ? UCT_COLL_OPERAND_INT8_T :
                                     UCT_COLL_OPERAND_UINT8_T;
            break;
        case sizeof(uint16_t):
            *operand_p = is_signed ? UCT_COLL_OPERAND_INT16_T :
                                     UCT_COLL_OPERAND_UINT16_T;
            break;
        case sizeof(uint32_t):
            *operand_p = is_signed ? UCT_COLL_OPERAND_INT32_T :
                                     UCT_COLL_OPERAND_UINT32_T;
            break;
        case sizeof(uint64_t):
            *operand_p = is_signed ? UCT_COLL_OPERAND_INT64_T :
                                     UCT_COLL_OPERAND_UINT64_T;
            break;
        default:
            return UCS_ERR_UNSUPPORTED;
        }
    } else if (ucg_global_params.datatype.is_floating_point_f(external_dt)) {
        switch (dt_size) {
        case sizeof(float):
            *operand_p = UCT_COLL_OPERAND_FLOAT;
            break;
        case sizeof(double):
            *operand_p = UCT_COLL_OPERAND_DOUBLE;
            break;
        default:
            return UCS_ERR_UNSUPPORTED;
        }
    } else {
        return UCS_ERR_UNSUPPORTED;
    }

skip_to_callbacks:
#if HAVE_SM_COLL
    return uct_reduction_get_callbacks(*operator_p, *operand_p, dt_count, 0,
                                       input_buffers_are_cache_aligned,
                                       does_zero_dt_count_means_unknown,
                                       &memcpy, reduce_p);
#else
    return UCS_ERR_UNSUPPORTED;
#endif
}

ucp_ep_h ucp_plan_get_p2p_ep_by_index(ucg_group_h group,
                                      ucg_group_member_index_t group_idx)
{
    khash_t(ucg_group_ep) *khash = &group->p2p_eps;
    khiter_t iter                = kh_get(ucg_group_ep, khash, group_idx);

    ucs_assert(iter != kh_end(khash));
    return kh_value(khash, iter);
}

static ucs_status_t ucg_plan_connect_by_hash_key(ucg_group_h group,
                                                 ucg_group_member_index_t peer,
                                                 ucp_ep_h *ep_p)
{
    ucp_ep_h ucp_ep;
    ucs_status_t status;
    size_t remote_addr_len;
    ucp_address_t *remote_addr   = NULL;
    khash_t(ucg_group_ep) *khash = &group->p2p_eps;

    /* Look-up the UCP endpoint based on the index */
    khiter_t iter = kh_get(ucg_group_ep, khash, peer);
    if (iter != kh_end(khash)) {
        /* Use the cached connection */
        *ep_p = kh_value(khash, iter);
        return UCS_OK;
    }

    /* fill-in UCP connection parameters */
    ucs_assert(peer != group->params.member_index);
    ucs_assert(peer <  group->params.member_count);
    status = ucg_global_params.address.lookup_f(group->params.cb_context,
                                                peer,
                                                &remote_addr,
                                                &remote_addr_len);
    if (status != UCS_OK) {
        ucs_error("failed to obtain a UCP endpoint from the external callback");
        return status;
    }

    /* special case: connecting to a zero-length address means it's debugging */
    if (ucs_unlikely(remote_addr_len == 0)) {
        *ep_p = NULL;
        return UCS_OK;
    }

    /* create an endpoint for communication with the remote member */
    ucp_ep_params_t ep_params = {
            .field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS,
            .address = remote_addr
    };

    status = ucp_ep_create(group->worker, &ep_params, &ucp_ep);
    ucg_global_params.address.release_f(remote_addr);
    if (status != UCS_OK) {
        return status;
    }

    ucg_group_store_ep(khash, peer, ucp_ep);
    *ep_p = ucp_ep;

    return UCS_OK;
}

ucs_status_t
ucg_plan_await_lane_connection(ucp_worker_h worker, ucp_ep_h ucp_ep,
                               ucp_lane_index_t lane, uct_ep_h uct_ep)
{
    if (uct_ep == NULL) {
        ucs_status_t status = ucp_wireup_connect_remote(ucp_ep, lane);
        return (status != UCS_OK) ? status : UCS_INPROGRESS;
    }

    ucs_assert(uct_ep->iface != NULL);
    if (uct_ep->iface->ops.ep_am_short ==
            (typeof(uct_ep->iface->ops.ep_am_short))
            ucs_empty_function_return_no_resource) {
        ucp_worker_progress(worker);
        return UCS_INPROGRESS;
    }

    return UCS_OK;
}

static ucs_status_t ucg_plan_connect_p2p_internal(ucg_group_h group,
                                                  ucg_group_member_index_t peer,
                                                  ucp_lane_index_t *lane_p,
                                                  ucp_ep_h *ucp_ep_p,
                                                  uct_ep_h *uct_ep_p)

{
    int ret;
    ucs_status_t status;
    ucg_context_t *gctx;

    if (ucg_global_params.field_mask & UCG_PARAM_FIELD_GLOBAL_INDEX) {
        ret = ucg_global_params.get_global_index_f(group->params.cb_context,
                                                   peer, &peer);
        if (ret) {
            ucs_error("Failed to get global index for group #%u member #%u",
                      group->params.id, peer);
            return UCS_ERR_UNREACHABLE;
        }

        /* use global context for lookup, e.g. MPI_COMM_WORLD, not mine */
        gctx  = ucs_derived_of(group->super.context, ucg_context_t);
        group = ucs_list_head(&gctx->super.targets_head, ucg_group_t,
                              super.list);
    }

    status = ucg_plan_connect_by_hash_key(group, peer, ucp_ep_p);
    if ((status != UCS_OK) || (lane_p == NULL) || (uct_ep_p == NULL)) {
        return status;
    }

    do {
        *lane_p   = ucp_ep_get_am_lane(*ucp_ep_p);
        *uct_ep_p = ucp_ep_get_am_uct_ep(*ucp_ep_p);
        status    = ucg_plan_await_lane_connection(group->worker, *ucp_ep_p,
                                                   *lane_p, *uct_ep_p);
    } while (status == UCS_INPROGRESS);

    return status;
}

ucs_status_t ucg_plan_connect_p2p_single(ucg_group_h group,
                                         ucg_group_member_index_t peer,
                                         uct_ep_h *uct_ep_p, ucp_ep_h *ucp_ep_p,
                                         uct_iface_h *iface_p, uct_md_h *md_p,
                                         const uct_iface_attr_t **iface_attr_p)
{
    ucp_lane_index_t lane = UCP_NULL_LANE;
    ucs_status_t status   = ucg_plan_connect_p2p_internal(group, peer, &lane,
                                                          ucp_ep_p, uct_ep_p);

    if (md_p) {
        *md_p = ucp_ep_md(*ucp_ep_p, lane);
    }

    if (iface_p) {
        *iface_p = *uct_ep_p ? (*uct_ep_p)->iface : NULL;
    }

    if (iface_attr_p) {
        *iface_attr_p = ucp_ep_get_iface_attr(*ucp_ep_p, lane);
        ucs_assert(*iface_attr_p != NULL);
    }

    return status;
}

ucs_status_t ucg_plan_connect_p2p(ucg_group_h group,
                                  const ucs_int_array_t *destinations,
                                  uct_ep_h *uct_ep_p, ucp_ep_h *ucp_ep_p,
                                  uct_iface_h *iface_p, uct_md_h *md_p,
                                  const uct_iface_attr_t **iface_attr_p)
{
    unsigned i;
    ucs_status_t status;
    ucg_group_member_index_t peer;

    /* Connect the UCP endpoints */
    ucs_int_array_for_each(peer, i, destinations) {
        status = ucg_plan_connect_p2p_single(group, peer, uct_ep_p, ucp_ep_p,
                                             iface_p, md_p, iface_attr_p);
        if (status != UCS_OK) {
            return status;
        }

        if (ucp_ep_p) {
            ucp_ep_p++;
        }

        if (uct_ep_p) {
            uct_ep_p++;

            /* Ensure all endpoints belong to one interface (assumed everywhere) */
            ucs_assert(!i || (uct_ep_p[-1]->iface == uct_ep_p[-2]->iface));
        }
    }

    return UCS_OK;
}

int ucg_plan_md_can_register_memory(uct_md_h md)
{
    return md && (md->ops->mem_reg != ucs_empty_function_return_unsupported);
}

ucs_status_t ucg_plan_connect_coll(ucg_group_h group, uint64_t wireup_uid,
                                   const ucg_topo_desc_step_t *step,
                                   int is_leader, int is_incast,
                                   uint8_t wireup_am_id,
                                   uct_incast_operator_t operator_,
                                   uct_incast_operand_t operand,
                                   unsigned operand_count,
                                   int is_operand_cache_aligned, size_t msg_size,
                                   uct_reduction_external_cb_t ext_aux_cb,
                                   void *ext_operator, void *ext_datatype,
                                   ucg_group_member_index_t coll_threshold,
                                   ucg_group_member_index_t *coll_index_p,
                                   ucg_group_member_index_t *coll_count_p,
                                   uct_ep_h *uct_ep_p, uint16_t *ep_cnt_p,
                                   uct_iface_h *iface_p, uct_md_h *md_p,
                                   const uct_iface_attr_t **iface_attr_p,
                                   int *requires_optimization_p,
                                   unsigned *iface_id_base_p,
                                   ucp_tl_bitmap_t *coll_tl_bitmap_p)
{
    ucp_ep_h ucp_ep;
    uct_ep_h uct_ep;
    ucs_status_t status;
    ucp_lane_index_t lane;
    ucp_rsc_index_t rsc_index;
    ucp_worker_iface_t *wiface;

    status = ucg_group_wireup_coll_ifaces(group, wireup_uid, wireup_am_id,
                                          is_leader, is_incast,
                                          &step->level_members, coll_threshold,
                                          operator_, operand, operand_count,
                                          is_operand_cache_aligned, msg_size,
                                          ext_aux_cb, ext_operator, ext_datatype,
                                          iface_id_base_p, coll_tl_bitmap_p,
                                          coll_index_p, coll_count_p, &ucp_ep);
    if ((status != UCS_OK) || (!*coll_count_p)) {
        return status;
    }
    lane = is_incast ? ucp_ep_get_incast_lane(ucp_ep) :
                       ucp_ep_get_bcast_lane(ucp_ep);
    if (ucs_unlikely(lane == UCP_NULL_LANE)) {
        /* Note: this may happen because debug builds support less options! */
        return UCS_ERR_UNREACHABLE;
    }

    do {
        uct_ep = is_incast ? ucp_ep_get_incast_uct_ep(ucp_ep) :
                             ucp_ep_get_bcast_uct_ep(ucp_ep);
        status = ucg_plan_await_lane_connection(group->worker, ucp_ep, lane, uct_ep);
    } while (status == UCS_INPROGRESS);

    if (status != UCS_OK) {
        return status;
    }

    rsc_index = ucp_ep_get_rsc_index(ucp_ep, lane);
    wiface    = ucp_worker_iface_with_offset(group->worker, rsc_index,
                                             *coll_tl_bitmap_p, *iface_id_base_p);

    if (wiface->attr.cap.am.max_bcopy < msg_size) {
        /*
         * In some cases - an optimization is mandatory for successful execution.
         * One such case is a multi-packet buffer-copy (bcopy), where using a remote
         * key and zero-copy reductions is the only supported way (because the code
         * for combining full and partial bcopy calls is tricky and has not been
         * implemented yet). As a result, only a memory-mapping zero-copy reduction-
         * capable packer function can be used here - otherwise planning fails.
         */
        if (ucg_plan_md_can_register_memory(ucp_ep_md(ucp_ep, lane))) {
            *requires_optimization_p = 1;
        }
    }

    /* Start setting output arguments */
    *ep_cnt_p = 1;
    *uct_ep_p = uct_ep;

    if (md_p) {
        *md_p = ucp_ep_md(ucp_ep, lane);
    }

    if (iface_p) {
        *iface_p = wiface->iface;
        ucs_assert(((*iface_p)->ops.ep_am_short != NULL) ||
                   ((*iface_p)->ops.ep_am_bcopy != NULL));
    }

    if (iface_attr_p) {
        *iface_attr_p = &wiface->attr;
        ucs_assert((*iface_attr_p)->cap.flags &
                   (is_incast ? UCT_IFACE_FLAG_INCAST : UCT_IFACE_FLAG_BCAST));
    }

    return UCS_OK;
}

void ucg_plan_disconnect_coll(ucg_group_h group, unsigned iface_id_base,
                              ucp_tl_bitmap_t coll_tl_bitmap)
{
    ucg_group_cleanup_coll_ifaces(group, iface_id_base, coll_tl_bitmap);
}

ucp_worker_h ucg_plan_get_group_worker(ucg_group_h group)
{
    return group->worker;
}

static ucp_ep_h mock_ep = NULL;
static ucp_worker_h mock_worker;

static void ucg_plan_connect_mock_collective(int is_incast,
                                             uct_iface_attr_t *iface_attr)
{
    iface_attr->cap.flags                |= is_incast ? UCT_IFACE_FLAG_INCAST :
                                                        UCT_IFACE_FLAG_BCAST;
    iface_attr->cap.am.coll_mode_flags    = UCS_BIT(UCT_COLL_LENGTH_INFO_DEFAULT) |
                                            UCS_BIT(UCT_COLL_LENGTH_INFO_PACKED);
    iface_attr->cap.coll_mode.short_flags = UCS_BIT(UCT_COLL_LENGTH_INFO_DEFAULT) |
                                            UCS_BIT(UCT_COLL_LENGTH_INFO_PACKED);
    iface_attr->cap.coll_mode.bcopy_flags = UCS_BIT(UCT_COLL_LENGTH_INFO_DEFAULT) |
                                            UCS_BIT(UCT_COLL_LENGTH_INFO_PACKED);
}

ucs_status_t ucg_plan_connect_mock(ucg_group_h group, int is_collective,
                                   int is_incast, uct_iface_h *iface_p,
                                   const uct_iface_attr_t **iface_attr_p,
                                   uct_md_h *md_p)
{
    uct_ep_h uct_ep;
    ucs_status_t status;
    ucp_lane_index_t lane;
    UCS_V_UNUSED size_t length;
    ucp_worker_params_t aux_params = {0};
    ucp_ep_params_t ep_params = {
        .field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS
    };

    if (mock_ep == NULL) {
        /* Create another worker within the same process (and UCP context) */
        status    = ucp_worker_create(ucg_plan_get_group_worker(group)->context,
                                      &aux_params, &mock_worker);
        if (status != UCS_OK) {
            return status;
        }

        /* Create a shared-memory endpoint - for reasonable attribute values */
        status = ucp_worker_get_address(mock_worker,
                                        (ucp_address_t**)&ep_params.address,
                                        &length);
        if (status == UCS_OK) {
            status = ucp_ep_create(group->worker, &ep_params, &mock_ep);
            ucp_worker_release_address(mock_worker,
                                       (ucp_address_t*)ep_params.address);
        }
        if (status != UCS_OK) {
            return status;
        }
    }

    lane   = ucp_ep_get_am_lane(mock_ep);
    uct_ep = ucp_ep_get_am_uct_ep(mock_ep);

    if (md_p) {
        *md_p = ucp_ep_md(mock_ep, lane);
    }

    if (iface_p) {
        *iface_p = uct_ep->iface;
    }

    if (iface_attr_p) {
        *iface_attr_p = ucp_ep_get_iface_attr(mock_ep, lane);
        ucs_assert(*iface_attr_p != NULL);
        if (is_collective) {
            ucg_plan_connect_mock_collective(is_incast,
                                             *(uct_iface_attr_t**)iface_attr_p);
        }
    }

    return UCS_OK;
}

void ucg_plan_connect_mock_cleanup()
{
    ucs_status_ptr_t *request;
    if (!mock_ep) {
        return;
    }

    /* Disconnect "manually", because "mock_worker" needs concurrent progress */
    request = ucp_disconnect_nb(mock_ep);
    do {
        ucp_worker_progress(mock_worker);
        ucp_worker_progress(mock_ep->worker);
    } while (ucp_request_check_status(request) == UCS_INPROGRESS);
    ucp_request_release(request);

    ucp_worker_destroy(mock_worker);
    mock_ep = NULL;
}
