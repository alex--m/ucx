/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_uct.h"
#include <uct/base/uct_md.h> /* For struct uct_md */

#include "../over_ucp/over_ucp_comp.inl"

/******************************************************************************
 *                                                                            *
 *                         Operation Step Completion                          *
 *                                                                            *
 ******************************************************************************/

#define first_comp_label _first_comp_label
#define last_comp_label   _last_comp_label

#define gen_comp_label(_set_async_comp, _get_imabalance, _is_delay, \
                       _is_volatile_dt, _is_recv_unpack, _is_send_pack, \
                       _is_dst_offset, _is_src_offset, _is_copy, _is_opt, \
                       _is_pipelined, _is_barrier) \
    comp_ ## _set_async_comp ## _get_imabalance ## _is_delay ## _is_volatile_dt ## _is_recv_unpack ## _is_send_pack ## _is_dst_offset ## _is_src_offset ## _is_copy ## _is_opt ## _is_pipelined ## _is_barrier

#define case_comp_diff(_set_async_comp, _get_imabalance, _is_delay, \
                       _is_volatile_dt, _is_recv_unpack, _is_send_pack, \
                       _is_dst_offset, _is_src_offset, _is_copy, _is_opt, \
                       _is_pipelined, _is_barrier) \
    &&gen_comp_label(_set_async_comp, _get_imabalance, _is_delay, \
                     _is_volatile_dt, _is_recv_unpack, _is_send_pack, \
                     _is_dst_offset, _is_src_offset, _is_copy, _is_opt, \
                     _is_pipelined, _is_barrier) - &&first_comp_label,

#define case_comp_visibility(_set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, \
                             _is_recv_unpack, _is_send_pack, _is_dst_offset, \
                             _is_src_offset, _is_copy, _is_opt, _is_pipelined, \
                             _is_barrier) \
    gen_comp_label(_set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, \
                   _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy, \
                   _is_opt, _is_pipelined, _is_barrier) ,

#define case_completion(_set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, \
                        _is_recv_unpack, _is_send_pack, _is_dst_offset, \
                        _is_src_offset, _is_copy, _is_opt, _is_pipelined, \
                        _is_barrier) \
    UCS_JUMPTBL_LABEL(gen_comp_label(_set_async_comp, _get_imabalance, _is_delay, \
                                     _is_volatile_dt, _is_recv_unpack, _is_send_pack, \
                                     _is_dst_offset, _is_src_offset, _is_copy, _is_opt, \
                                     _is_pipelined, _is_barrier)) \
    ucs_assert(flags == ((_set_async_comp ? UCG_OVER_UCT_PLAN_FLAG_ASYNC_COMPLETION : 0) | \
                         (_get_imabalance ? UCG_OVER_UCT_PLAN_FLAG_IMBALANCE_INFO   : 0) | \
                         (_is_delay       ? UCG_OVER_UCT_PLAN_FLAG_BARRIER_DELAY    : 0) | \
                         (_is_volatile_dt ? UCG_OVER_UCT_PLAN_FLAG_VOLATILE_DT      : 0) | \
                         (_is_recv_unpack ? UCG_OVER_UCT_PLAN_FLAG_RECV_UNPACK      : 0) | \
                         (_is_send_pack   ? UCG_OVER_UCT_PLAN_FLAG_SEND_PACK        : 0) | \
                         (_is_dst_offset  ? UCG_OVER_UCT_PLAN_FLAG_COPY_DST_OFFSET  : 0) | \
                         (_is_src_offset  ? UCG_OVER_UCT_PLAN_FLAG_COPY_SRC_OFFSET  : 0) | \
                         (_is_copy        ? UCG_OVER_UCT_PLAN_FLAG_COPY_SRC_BUFFER  : 0) | \
                         (_is_opt         ? UCG_OVER_UCT_PLAN_FLAG_OPTIMIZE_CB      : 0) | \
                         (_is_pipelined   ? UCG_OVER_UCT_PLAN_FLAG_PIPELINED        : 0) | \
                         (_is_barrier     ? UCG_OVER_UCT_PLAN_FLAG_BARRIER          : 0))); \
                                                                               \
        if (_is_barrier) {                                                     \
            gctx  = plan->super.gctx;                                          \
            group = (ucs_pmodule_target_t*)gctx->super.group;                  \
            if (is_init) {                                                     \
                ucg_over_ucp_comp_barrier_lock(group, &op->super);             \
            } else {                                                           \
                ucg_over_ucp_comp_barrier_unlock(group, &op->super);           \
            }                                                                  \
        }                                                                      \
                                                                               \
        if (_is_opt && is_init && !--(plan->opt_cnt)) {                        \
            params = ucg_plan_get_params(&plan->super.super);                  \
            status = ucg_over_uct_optimize_now(plan, params);                  \
            if (ucs_unlikely(status != UCS_OK)) {                              \
                goto op_error;                                                 \
            }                                                                  \
        }                                                                      \
                                                                               \
        if (_is_delay && !(is_init)) {                                         \
            gctx = plan->super.gctx;                                           \
            ucg_context_barrier_delay(gctx->super.group);                      \
        }                                                                      \
                                                                               \
        if (_is_copy && is_init) {                                             \
            params   = ucg_plan_get_params(&plan->super.super);                \
            send_buf = params->send.buffer;                                    \
            recv_buf = plan->copy_dst;                                         \
            length   = params->recv.count *                                    \
                       ucp_dt_length(plan->recv_dt, 1, NULL, NULL);            \
            if (_is_src_offset || _is_dst_offset) {                            \
                my_index = plan->my_index;                                     \
            }                                                                  \
            if (_is_src_offset) {                                              \
                send_buf += length * my_index;                                 \
            }                                                                  \
            if (_is_dst_offset) {                                              \
                recv_buf += length * my_index;                                 \
            }                                                                  \
            memcpy(recv_buf, send_buf, length);                                \
            /* TODO: support non-contig DTs by moving it down this macro */    \
        }                                                                      \
                                                                               \
        if (_is_pipelined && is_init) {                                       \
            if (ucs_unlikely(op->frags_allocd < plan->max_frag)) {             \
                op->fragment_pending = ucs_realloc((void*)op->fragment_pending,\
                                                   plan->max_frag,             \
                                                   "ucg_over_uct_fragments");  \
                if (ucs_unlikely(op->fragment_pending == NULL)) {              \
                    return UCS_ERR_NO_MEMORY;                                  \
                }                                                              \
                                                                               \
                memset((void*)op->fragment_pending, 0, plan->max_frag);        \
                op->frags_allocd = plan->max_frag;                             \
            }                                                                  \
        }                                                                      \
                                                                               \
        if (_is_volatile_dt && is_init) {                                      \
            if (_is_send_pack) {                                               \
                params = ucg_plan_get_params(&plan->super.super);              \
                dt     = params->send.dtype;                                   \
                if (ucg_global_params.datatype.convert_f(dt, &plan->send_dt)) {\
                    ucs_error("failed to convert external send-datatype");     \
                    status = UCS_ERR_INVALID_PARAM;                            \
                    goto op_error;                                             \
                }                                                              \
            }                                                                  \
                                                                               \
            if (_is_recv_unpack) {                                             \
                params = ucg_plan_get_params(&plan->super.super);              \
                dt     = params->recv.dtype;                                   \
                if (ucg_global_params.datatype.convert_f(dt, &plan->recv_dt)) {\
                    ucs_error("failed to convert external receive-datatype");  \
                    status = UCS_ERR_INVALID_PARAM;                            \
                    goto op_error;                                             \
                }                                                              \
            }                                                                  \
        }                                                                      \
                                                                               \
        if (_is_send_pack) {                                                   \
            dt_gen = ucp_dt_to_generic(plan->send_dt);                         \
            if (is_init) {                                                     \
                params = ucg_plan_get_params(&plan->super.super);              \
                op->send_pack = dt_gen->ops.start_pack(dt_gen->context,        \
                                                       params->send.buffer,    \
                                                       params->send.count);    \
            } else {                                                           \
                dt_gen->ops.finish(op->send_pack);                             \
            }                                                                  \
        }                                                                      \
                                                                               \
        if (_is_recv_unpack) {                                                 \
            dt_gen = ucp_dt_to_generic(plan->recv_dt);                         \
            if (is_init) {                                                     \
                params = ucg_plan_get_params(&plan->super.super);              \
                op->recv_unpack = dt_gen->ops.start_unpack(dt_gen->context,    \
                                                           params->recv.buffer,\
                                                           params->recv.count);\
            } else {                                                           \
                dt_gen->ops.finish(op->recv_unpack);                           \
            }                                                                  \
        }                                                                      \
                                                                               \
        if (_get_imabalance)  {                                                \
            if (is_init) {                                                     \
                op->first_timestamp = ucs_arch_read_hres_clock();              \
            } else {                                                           \
                gctx  = plan->super.gctx;                                      \
                group_params = ucg_group_get_params(gctx->super.group);        \
                ucg_global_params.set_imbalance_cb_f(group_params->cb_context, \
                                                     (ucs_arch_read_hres_clock()\
                                                      - op->first_timestamp) / \
                                                     ucs_time_sec_value());    \
            }                                                                  \
        }                                                                      \
                                                                               \
        if (_set_async_comp && is_init) {                                      \
            op->super.flags = UCS_PMODULE_TARGET_ACTION_FLAG_ASYNC_COMPLETION; \
        }                                                                      \
                                                                               \
        goto last_comp_label;

#define case_is_pipelined(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy, _is_opt, _is_pipelined) \
                          _macro( _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy, _is_opt, _is_pipelined, 0) \
                          _macro( _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy, _is_opt, _is_pipelined, 1)

#define case_is_opt(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy, _is_opt) \
  case_is_pipelined(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy, _is_opt, 0) \
  case_is_pipelined(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy, _is_opt, 1)

#define case_is_copy(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy) \
         case_is_opt(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy, 0) \
         case_is_opt(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, _is_copy, 1)

#define case_src_offset(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset) \
           case_is_copy(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, 0) \
           case_is_copy(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, _is_src_offset, 1)

#define case_dst_offset(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset) \
        case_src_offset(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, 0) \
        case_src_offset(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, _is_dst_offset, 1)

#define case_send_pack(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack) \
       case_dst_offset(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, 0) \
       case_dst_offset(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, _is_send_pack, 1)

#define case_recv_unpack(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack) \
          case_send_pack(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, 0) \
          case_send_pack(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, _is_recv_unpack, 1)

#define case_volatile_dt(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt) \
        case_recv_unpack(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, 0) \
        case_recv_unpack(_macro, _set_async_comp, _get_imabalance, _is_delay, _is_volatile_dt, 1)

#define case_is_delay(_macro, _set_async_comp, _get_imabalance, _is_delay) \
     case_volatile_dt(_macro, _set_async_comp, _get_imabalance, _is_delay, 0) \
     case_volatile_dt(_macro, _set_async_comp, _get_imabalance, _is_delay, 1)

#define case_get_imabalance(_macro, _set_async_comp, _get_imabalance) \
              case_is_delay(_macro, _set_async_comp, _get_imabalance, 0) \
              case_is_delay(_macro, _set_async_comp, _get_imabalance, 1)

#define case_set_async_comp(_macro, _set_async_comp) \
        case_get_imabalance(_macro, _set_async_comp, 0) \
        case_get_imabalance(_macro, _set_async_comp, 1)

#define ALL_COMP_CASES_IN_ASCENDING_ORDER(_macro) \
    case_set_async_comp(_macro, 0) \
    case_set_async_comp(_macro, 1)

static ucs_status_t
#if !defined(ENABLE_JUMP_TABLES) || (!ENABLE_JUMP_TABLES)
UCS_F_DEBUG_OR_INLINE
#endif
ucg_over_uct_comp_by_flags(ucg_over_uct_plan_t *plan, ucg_over_uct_op_t *op,
                           int is_init)
{
#if ENABLE_JUMP_TABLES
    __label__ ALL_COMP_CASES_IN_ASCENDING_ORDER(case_comp_visibility)
              first_comp_label, last_comp_label;
#endif

    void *dt;
    size_t length;
    void *send_buf;
    void *recv_buf;
    ucs_status_t status;
    ucp_dt_generic_t *dt_gen;
    ucs_pmodule_target_t *group;
    ucg_over_uct_group_ctx_t *gctx;
    ucg_group_member_index_t my_index;
    const ucg_collective_params_t *params;
    const ucg_group_params_t* group_params;

    uint16_t flags = plan->op_flags;
    ucs_assert(!(plan->op_flags & ~UCG_OVER_UCT_PLAN_FLAG_SWITCH_MASK));

    if (ucs_unlikely(!is_init && !ucs_list_is_empty(&op->resend_list))) {
        ucg_over_uct_cancel_resend(op);
    }

    if (ucs_likely(!flags)) {
        goto last_comp_label;
    }

#ifndef ENABLE_FAULT_TOLERANCE
#  define is_ft_ongoing (0)
#endif

    UCS_JUMPTBL_INIT(ucg_over_uct_comp_init,
                     ALL_COMP_CASES_IN_ASCENDING_ORDER(case_comp_diff))
    UCS_JUMPTBL_JUMP(ucg_over_uct_comp_init, flags, first_comp_label)

    ALL_COMP_CASES_IN_ASCENDING_ORDER(case_completion)

    UCS_JUMPTBL_LABEL_TERMINATOR(last_comp_label)

    if (is_init) {
        ucs_assert(ucs_list_is_empty(&op->resend_list));
    } else {
        /* This is the best time to prepare for the next collective call */
        UCS_STATIC_ASSERT(offsetof(ucg_over_uct_comp_slot_t, op) == 0);
        op = &(ucs_container_of(op, ucg_over_uct_comp_slot_t, op) + 1)->op;
        ucs_prefetch_write(op);
    }

    return UCS_OK;

op_error:
    return status;
}

static UCS_F_DEBUG_OR_INLINE void
ucg_over_uct_comp_generic_dt(ucg_over_uct_op_t *op, int is_init, int is_rx,
                             int is_pack)
{
    void *buf;
    unsigned cnt;
    void* (*dt_f)(void*, const void*, size_t);

    ucg_over_uct_plan_phase_t *phase = op->phase;
    ucg_over_uct_plan_t *plan        = ucs_derived_of(op->super.plan,
                                                      ucg_over_uct_plan_t);
    ucp_datatype_t dt                = is_rx ? plan->recv_dt : plan->send_dt;
    ucp_dt_generic_t *dt_g           = ucp_dt_to_generic(dt);
    ucp_dt_state_t **dt_s            = is_rx ? &op->recv_unpack : &op->send_pack;

    ucs_assert(is_rx == !is_pack);

    if (is_init) {
        cnt   = 1; // TODO: fix!
        buf   = is_rx   ? phase->rx.buffer : phase->tx.buffer;
        dt_f  = is_pack ? (typeof(dt_f))dt_g->ops.start_pack :
                          (typeof(dt_f))dt_g->ops.start_unpack;
        *dt_s = dt_f(dt_g->context, buf, cnt);
    } else {
        dt_g->ops.finish(*dt_s);
    }
}

static UCS_F_DEBUG_OR_INLINE int
ucg_over_uct_comp_check_pending(ucg_over_uct_op_t *op, int step_only)
{
    unsigned i;
    ucp_recv_desc_t *rdesc;
    ucg_over_uct_header_t *header;
    ucg_over_uct_header_step_t *id = UCG_OVER_UCT_OP_EXPECTED(op);
    ucg_over_uct_comp_slot_t *slot = ucs_container_of(op,
                                                      ucg_over_uct_comp_slot_t,
                                                      op);
#if ENABLE_MT
    ucs_ptr_array_locked_for_each(rdesc, i, &slot->messages) {
#else
    ucs_ptr_array_for_each(rdesc, i, &slot->messages) {
#endif
        header = (ucg_over_uct_header_t*)(rdesc + 1);

        if (!step_only || ((header->msg.coll_id == id->coll_id) &&
                           (header->msg.step_idx <= id->step_idx))) {
            ucs_warn("Collective operation #%u still has a pending message for "
                     "step #%u (Group #%u rdesc %p)", header->msg.coll_id,
                     header->msg.step_idx, header->group_id, rdesc);
            return 0;
        }
    }

    return 1;
}

static UCS_F_DEBUG_OR_INLINE void
ucg_over_uct_comp_last_step_cb(ucg_over_uct_op_t *op, ucs_status_t status)
{
    /* Finalize the operation */
    ucg_over_uct_comp_by_flags(ucs_derived_of(op->super.plan,
                                              ucg_over_uct_plan_t), op, 0);

    /* Sanity check: no more messages stored for this operation */
    ucs_assert((status != UCS_OK) || (ucg_over_uct_comp_check_pending(op, 0)));

    /* Mark (per-group) slot as available */
    UCG_OVER_UCT_OP_EXPECTED(op)->local_id = 0;

    ucg_over_ucp_comp_last_step_cb(&op->super, 0, status);
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_comp_ft_end_step(ucg_over_uct_plan_phase_t *phase)
{
#ifdef ENABLE_FAULT_TOLERANCE
    ucg_over_uct_plan_t *plan = op->super.plan;
    if (ucs_unlikely(op->flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_FT_ONGOING))) {
        ucg_over_uct_plan_phase_t *phase = step->phase;
        if (phase->tx.ep_cnt == 1) {
            ucg_ft_end(phase->handles[0], phase->indexes[0]);
        } else {
            /* Removing in reverse order is good for FT's timerq performance */
            unsigned peer_idx = phase->tx.ep_cnt;
            while (peer_idx--) {
                ucg_ft_end(phase->handles[peer_idx], phase->indexes[peer_idx]);
            }
        }
    }
#endif
    return UCS_OK;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_ptr_t
ucg_over_uct_comp_step_cb(ucg_over_uct_op_t *op)
{
    ucg_over_uct_plan_phase_t *phase = op->phase;

    /* Sanity checks */
    if (phase->flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_PIPELINED) {
        unsigned frag_idx;
        unsigned frag_per_ep = phase->rx.frags_cnt / phase->tx.ep_cnt;
        ucs_assert(phase->rx.frags_cnt % phase->tx.ep_cnt == 0);
        ucs_assert(op->fragment_pending != NULL);
        for (frag_idx = 0; frag_idx < frag_per_ep; frag_idx++) {
            ucs_assert(op->fragment_pending[frag_idx] == 0);
        }
    }

    /* Sanity check: no more messages stored for this step of the operation */
#if ENABLE_ASSERT
    ucg_over_uct_comp_check_pending(op, 1);
#endif

    ucg_over_uct_comp_ft_end_step(phase);

    /* Start on the next step for this collective operation */
    phase                                  = ++(op->phase);
    op->comp.count                         = phase->rx.frags_cnt;
    op->iter_frag                          = 0;
    op->iter_ep                            = 0;
    UCG_OVER_UCT_OP_EXPECTED(op)->step_idx = phase->rx.step_idx;

    return ucg_over_uct_execute_op(op);
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_comp_use_rkey(uct_md_h md, const ucg_over_uct_rkey_msg_t *msg,
                           void **local_ptr, uct_rkey_bundle_t *rkey_p,
                           uct_component_h *component_p)
{
    ucs_status_t status;
    uct_component_h component = md->component;

    ucs_assert(msg->magic == RKEY_MAGIC);

    /* Unpack the information from the payload to feed the next (0-copy) step */
    status = uct_rkey_unpack(component, msg->packed_rkey, rkey_p);\
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    *component_p = component;
    return uct_rkey_ptr(component, rkey_p, msg->remote_address, local_ptr);
}

static UCS_F_DEBUG_OR_INLINE void
ucg_over_uct_comp_discard_rkey(uct_rkey_bundle_t *rkey_p,
                               uct_component_h component)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    uct_rkey_release(component, rkey_p);
#pragma GCC diagnostic pop
}

static UCS_F_DEBUG_OR_INLINE int
ucg_over_uct_comp_send_check_frag_by_offset(ucg_over_uct_op_t *op,
                                            uint64_t offset, uint8_t batch_cnt)
{
    unsigned frag_idx = offset / op->phase->tx.frag_len;

    ucs_assert(offset < op->phase->tx.length);
    ucs_assert((offset % op->phase->tx.frag_len) == 0);
    ucs_assert(op->fragment_pending[frag_idx] >= batch_cnt);

    op->fragment_pending[frag_idx] -= batch_cnt;

    if (op->fragment_pending[frag_idx] == 0) {
        if (ucs_unlikely(op->iter_frag == UCG_OVER_UCT_FRAG_PIPELINE_PENDING)) {
            op->fragment_pending[frag_idx] = UCG_OVER_UCT_FRAG_PENDING;
        } else {
            op->iter_frag = frag_idx;
            return 1;
        }
    }

    return op->iter_frag != UCG_OVER_UCT_FRAG_PIPELINE_READY;
}

static ucs_status_t UCS_F_DEBUG_OR_INLINE
ucg_over_uct_comp_recv_handle_chunk(enum ucg_over_uct_plan_phase_comp_aggregation ag,
                                    uint8_t *dst, uint8_t *src, size_t length,
                                    ucg_over_uct_header_t header,
                                    int is_fragmented, int is_generic_dt,
                                    ucg_over_uct_op_t *op)
{
    int ret;
    uct_md_h md;
    ptrdiff_t gap;
    ptrdiff_t dsize;
    void *gen_state;
    ucs_status_t status;
    uint8_t *reduce_buf;
    ucs_status_ptr_t res;
    uct_rkey_bundle_t rkey;
    ucp_dt_generic_t *gen_dt;
    ucg_over_uct_plan_t *plan;
    uct_component_h component;
    ucg_over_uct_rkey_msg_t *rkey_msg;
    const ucg_collective_params_t *params;

    if (ag & UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY) {
        /* zero-copy prepares the key for the next step */
        ucs_assert(((ucg_over_uct_rkey_msg_t*)src)->magic == RKEY_MAGIC);
        rkey_msg  = (ucg_over_uct_rkey_msg_t*)src;
        length    = rkey_msg->remote_length;
        md        = op->phase->info[UCG_OVER_UCT_PHASE_EXTRA_INFO_RX].md;
        status    = ucg_over_uct_comp_use_rkey(md, rkey_msg, (void**)&src,
                                               &rkey, &component);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }
    }

    switch ((int)ag) {
    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_NOP:
    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_NOP |
         UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY:
        status = UCS_OK;
        break;

    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_NO_OFFSET:
    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_NO_OFFSET |
         UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY:
    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_WITH_OFFSET:
    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_WITH_OFFSET |
         UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY:
        if (is_generic_dt) {
            ucs_assert(!(ag & UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY));
            plan   = ucs_derived_of(op->super.plan, ucg_over_uct_plan_t);
            gen_dt = ucp_dt_to_generic(plan->recv_dt);
            status = gen_dt->ops.unpack(op->recv_unpack, header.remote_offset,
                                        src, length);
        } else {
#ifdef USE_NONTEMPORAL_MEMCPY
            ucs_memcpy_nontemporal(dst, src, length);
#else
            memcpy(dst, src, length);
#endif
            status = UCS_OK;
        }
        break;


    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_PIPELINE: /* Brigand mode! :) */
        ucs_assert(!is_generic_dt);
#ifdef USE_NONTEMPORAL_MEMCPY
        ucs_memcpy_nontemporal(dst, src, length);
#else
        memcpy(dst, src, length);
#endif
        if (ucg_over_uct_comp_send_check_frag_by_offset(op, header.remote_offset, 1)) {
            res = ucg_over_uct_execute_op(op);
            if (UCS_PTR_IS_ERR(res)) {
                status = UCS_PTR_STATUS(res);
            } else {
                status = UCS_OK;
            }
        } else {
            status = UCS_OK;
        }
        break;

    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_GATHER:
        // ucg_over_uct_comp_gather(op->phase->tx.buffer, header.remote_offset,
        //                         src, ucg_over_uct_plan_phase_length(op,
        //                                                      &op->super.plan->params,
        //                                                      0), length,
        //                         ucg_plan_get_root(&op->super.plan));
        status = UCS_OK;
        break;

    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_INTERNAL:
    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_INTERNAL |
         UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY:
        ucs_assert(is_generic_dt == 0);

        op->phase->rx.reduce_f(dst, src, length);

        status = UCS_OK;
        break;

    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_EXTERNAL:
    case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_EXTERNAL |
         UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY:
        plan   = ucs_derived_of(op->super.plan, ucg_over_uct_plan_t);
        params = ucg_plan_get_params(&plan->super.super);

        if (is_generic_dt) {
            gen_dt = ucp_dt_to_generic(plan->recv_dt);
            ret    = ucg_global_params.datatype.get_span_f(params->recv.dtype,
                                                           params->recv.count,
                                                           &dsize, &gap);
            if (ret) {
                ucs_error("failed to get the span of a datatype");
                return UCS_ERR_INVALID_PARAM;
            }

            reduce_buf = (uint8_t*)ucs_alloca(dsize);
            gen_state  = gen_dt->ops.start_unpack(gen_dt->context,
                                                  reduce_buf - gap,
                                                  params->recv.count);

            gen_dt->ops.unpack(gen_state, 0, params->send.buffer, length);
            gen_dt->ops.finish(gen_state);
            src = reduce_buf - gap;
            // TODO: (alex) offset = (offset / dt_len) * params->recv.dt_len;
        }

        ucs_assert((length + header.remote_offset) <=
                   (ucg_plan_get_params(&plan->super.super)->recv.count *
                    ucp_dt_length(plan->recv_dt, 1 , NULL, NULL)));
        ucs_assert(length % ucp_dt_length(plan->recv_dt, 1 , NULL, NULL) == 0);

        ucg_global_params.reduce_op.reduce_cb_f(UCG_PARAM_OP(params), src, dst,
                                                length / ucp_dt_length(plan->recv_dt,
                                                                       1, NULL, NULL),
                                                params->recv.dtype, NULL);

        status = UCS_OK;
        break;

    default:
        status = UCS_ERR_INVALID_PARAM;
        break;
    }

    if (ag & UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY) {
        ucg_over_uct_comp_discard_rkey(&rkey, component);
    }

    return status;
}


/******************************************************************************
 *                                                                            *
 *                         Handling Incoming Messages                         *
 *                                                                            *
 ******************************************************************************/

#define first_data_label _first_data_label
#define last_data_label _last_data_label

#define gen_data_label(_aggregation, _imbalance_info, _is_gen_dt, \
                       _is_fragmented, _is_slotted) \
    data_ ## _aggregation ## _imbalance_info ## _is_gen_dt ## _is_fragmented ## _is_slotted

#define case_data_diff(_is_offset_used, _aggregation, _imbalance_info, \
                       _is_gen_dt, _is_fragmented, _is_slotted) \
    &&gen_data_label(_aggregation, _imbalance_info, _is_gen_dt, \
                     _is_fragmented, _is_slotted) - &&first_data_label,

#define case_data_visibility(_is_offset_used, _aggregation, _imbalance_info, \
                             _is_gen_dt, _is_fragmented, _is_slotted) \
    gen_data_label(_aggregation, _imbalance_info, _is_gen_dt, \
                   _is_fragmented, _is_slotted) ,

#define case_handling(_is_offset_used, _aggregation, _imbalance_info, \
                      _is_gen_dt, _is_fragmented, _is_slotted) \
    UCS_JUMPTBL_LABEL(gen_data_label(_aggregation, _imbalance_info, \
                                     _is_gen_dt, _is_fragmented, \
                                     _is_slotted)) \
    ucs_assert(flags == (((_aggregation) << 4) | \
                         (_imbalance_info ? UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_IMBALANCE_INFO   : 0) | \
                         (_is_gen_dt      ? UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_GENERIC_DATATYPE : 0) | \
                         (_is_fragmented  ? UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_FRAGMENTED_DATA  : 0) | \
                         (_is_slotted     ? UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_SLOT_LENGTH      : 0))); \
    \
    if ((_aggregation) == UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_NOP) { \
        return; \
    } \
    \
    if ((_is_offset_used) && (_is_fragmented)) { \
        dest_buffer += header.remote_offset; \
    } else { \
        ucs_assert(header.remote_offset == 0); \
    } \
    \
    if (_is_slotted) { \
        ucs_assert((length % phase->rx.batch_cnt) == 0); \
        length      = length / phase->rx.batch_cnt; \
        actual_size = phase->rx.batch_len; \
        \
        for (index = 0; index < phase->rx.batch_cnt; index++) { \
            status = ucg_over_uct_comp_recv_handle_chunk((_aggregation), \
                                                         dest_buffer, data, \
                                                         actual_size, header, \
                                                         0, (_is_gen_dt), op); \
            if (ucs_unlikely(status != UCS_OK)) { \
                goto recv_handle_error; \
            } \
            data = UCS_PTR_BYTE_OFFSET(data, length); \
        } \
    } else { \
        status = ucg_over_uct_comp_recv_handle_chunk((_aggregation), \
                                                     dest_buffer, data, \
                                                     length, header, \
                                                     (_is_fragmented), \
                                                     (_is_gen_dt), op); \
        if (ucs_unlikely(status != UCS_OK)) { \
            goto recv_handle_error; \
        } \
    } \
    \
    if (_imbalance_info) {\
        if (!_is_slotted) { \
            data = UCS_PTR_BYTE_OFFSET(data, length); \
        } \
        timestamp = *(uint64_t*)data; \
        if (op->first_timestamp < timestamp) { \
            op->first_timestamp = timestamp; \
        } \
    } \
    \
    goto last_data_label;

#define case_recv_fragmented(_macro, _is_offset_used, agg, _imbalance_info, _is_gen_dt, _is_fragmented) \
                             _macro( _is_offset_used, agg, _imbalance_info, _is_gen_dt, _is_fragmented, 0) \
                             _macro( _is_offset_used, agg, _imbalance_info, _is_gen_dt, _is_fragmented, 1)

#define  case_recv_dt_packed(_macro, _is_offset_used, agg, _imbalance_info, _is_gen_dt) \
        case_recv_fragmented(_macro, _is_offset_used, agg, _imbalance_info, _is_gen_dt, 0) \
        case_recv_fragmented(_macro, _is_offset_used, agg, _imbalance_info, _is_gen_dt, 1)

#define case_recv_imbalance(_macro, _is_offset_used, agg, _imbalance_info) \
        case_recv_dt_packed(_macro, _is_offset_used, agg, _imbalance_info, 0) \
        case_recv_dt_packed(_macro, _is_offset_used, agg, _imbalance_info, 1)

#define       case_recv(_macro, agg, _is_offset_used) \
    case_recv_imbalance(_macro, _is_offset_used, agg, 0) \
    case_recv_imbalance(_macro, _is_offset_used, agg, 1)

#define ALL_DATA_CASES_IN_ASCENDING_ORDER(_macro) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_NOP,                    0) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_NO_OFFSET,        0) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_WITH_OFFSET,      1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_PIPELINE,               1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_GATHER,                 1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_INTERNAL,        1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_EXTERNAL,        1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_RESERVED,               0) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY,             0) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_NO_OFFSET_RKEY,   0) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_WITH_OFFSET_RKEY, 1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_PIPELINE_RKEY,          1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_GATHER_RKEY,            1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_INTERNAL_RKEY,   1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_EXTERNAL_RKEY,   1) \
    case_recv(_macro, UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_RESERVED_RKEY,          0)

static void
#if !defined(ENABLE_JUMP_TABLES) || (!ENABLE_JUMP_TABLES)
UCS_F_DEBUG_OR_INLINE
#endif
ucg_over_uct_comp_recv_handle_data(ucg_over_uct_op_t *op,
                                   ucg_over_uct_plan_phase_t *phase,
                                   ucg_over_uct_header_t header,
                                   uint8_t *data, size_t length,
                                   unsigned am_flags)
{
#if ENABLE_JUMP_TABLES
    __label__ ALL_DATA_CASES_IN_ASCENDING_ORDER(case_data_visibility)
              first_data_label, last_data_label;
#endif

    ucs_status_t status;
    UCS_V_UNUSED uint8_t index;
    UCS_V_UNUSED size_t actual_size;
    UCS_V_UNUSED uint64_t timestamp;
    UCS_V_UNUSED uint8_t *dest_buffer = phase->rx.buffer;
    uint8_t flags                     = phase->rx.comp_switch;

    UCS_JUMPTBL_INIT(ucg_over_uct_comp_handle,
                     ALL_DATA_CASES_IN_ASCENDING_ORDER(case_data_diff))
    UCS_JUMPTBL_JUMP(ucg_over_uct_comp_handle, flags, first_data_label)

    ALL_DATA_CASES_IN_ASCENDING_ORDER(case_handling)

    UCS_JUMPTBL_LABEL_TERMINATOR(last_data_label)

    return;

recv_handle_error:
    ucs_assert(UCS_STATUS_IS_ERR(status));
    ucg_over_uct_comp_last_step_cb(op, status);
}

static ucs_status_ptr_t UCS_F_DEBUG_OR_INLINE
ucg_over_uct_comp_recv_handle_comp(ucg_over_uct_op_t *op,
                                   ucg_over_uct_plan_phase_t *phase,
                                   ucg_over_uct_header_t header)
{
    ucs_status_ptr_t ret;

    /* Check according to the requested completion criteria */
    switch (phase->rx.comp_criteria) {
    case UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_SINGLE_MESSAGE:
        op->comp.count  = 0;
        break;

    case UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES:
        ucs_assert(op->comp.count > 0);
        op->comp.count--;

        if (ucs_unlikely(op->comp.count > 0)) {
            /* note: not really likely, but we optimize for the positive case */
            return op;
        }
        break;

    case UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES_ZCOPY:
        ucs_assert(op->comp.count >= phase->rx.frags_cnt);
        op->comp.count -= phase->rx.frags_cnt;
        if (ucs_unlikely(op->comp.count >= phase->rx.frags_cnt)) {
            /* note: not really likely, but we optimize for the positive case */
            return op;
        }
        break;
    }

    /* Act according to the requested completion action */
    if ((phase->rx.comp_action & UCG_OVER_UCT_PLAN_PHASE_COMP_SEND) &&
        (op->iter_ep < phase->tx.ep_cnt)) {
        return ucg_over_uct_execute_op(op);
    }

    switch (phase->rx.comp_action & 0x1) {
    case UCG_OVER_UCT_PLAN_PHASE_COMP_OP:
        ucg_over_uct_comp_last_step_cb(op, UCS_OK);
        ret = UCS_STATUS_PTR(UCS_OK);
        break;

    case UCG_OVER_UCT_PLAN_PHASE_COMP_STEP:
        ret = ucg_over_uct_comp_step_cb(op);
        break;
    }

    return ret;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_ptr_t
ucg_over_uct_comp_recv_cb(ucg_over_uct_op_t *op, ucg_over_uct_header_t header,
                          uint8_t *data, size_t length, unsigned am_flags,
                          ucp_recv_desc_t *rdesc, uct_iface_h iface)
{
    ucg_over_ucp_group_ctx_t *gctx;
    ucg_over_uct_plan_phase_t *phase = op->phase;

    ucg_over_uct_comp_recv_handle_data(op, phase, header, data, length, am_flags);

    if (rdesc) {
        gctx = ucs_derived_of(op->super.plan, ucg_over_ucp_plan_t)->gctx;
        ucg_group_am_msg_discard(rdesc, gctx->group);
    }

    return ucg_over_uct_comp_recv_handle_comp(op, phase, header);
}

static UCS_F_DEBUG_OR_INLINE void
ucg_over_uct_comp_count_add(ucg_over_uct_op_t *op, int addition)
{
    ucs_atomic_add32(&op->comp.count, addition);
}

/*
 * To prevent race-conditions with a potential async. progress thread during
 * transmission, below is a "safety mechanism" for the completion counter.
 * Before starting multiple zero-copy sends, the safety is engaged by adding a
 * large value to the counter, thus ensuring it will not complete the step
 * before all the sends have been posted. Once all the steps have been posted,
 * the value is subtracted back and the countdown can reach 0 and call that
 * step's completion callback function. Also, during that transmission period,
 * the completion counter must be modified atomically (again, due to possible
 * completions in a parallel async. thread, employed for UD for example).
 */
#define UCG_OVER_UCT_COMP_COUNT_SAFE (2000000000) /* like 1<<31 but readable */

static UCS_F_DEBUG_OR_INLINE void
ucg_over_uct_comp_count_set_safety(ucg_over_uct_op_t *op, int set_on)
{
    int currently_on = (op->comp.count >= UCG_OVER_UCT_COMP_COUNT_SAFE);
    ucs_assert(set_on || currently_on);
    if (currently_on != set_on) {
        if (set_on) {
            ucs_assert(op->comp.count == 0);
        } else {
            ucs_assert(op->comp.count > UCG_OVER_UCT_COMP_COUNT_SAFE);
        }
        ucg_over_uct_comp_count_add(op, set_on ? UCG_OVER_UCT_COMP_COUNT_SAFE :
                                                 -UCG_OVER_UCT_COMP_COUNT_SAFE);
    }
}
