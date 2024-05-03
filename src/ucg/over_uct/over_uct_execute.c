/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>
#include <ucs/arch/atomic.h>
#include <ucs/profile/profile.h>
#include <uct/base/uct_worker.h>
#include <uct/base/uct_iface.h>

#include "over_uct.h"
#include "over_uct_comp.inl"


/******************************************************************************
 *                                                                            *
 *                           Operation RX Execution                           *
 *                                                                            *
 ******************************************************************************/

static UCS_F_DEBUG_OR_INLINE ucs_status_ptr_t
ucg_over_uct_execute_pending_message(ucg_over_uct_op_t *op,
                                     ucg_over_uct_plan_phase_t *phase,
                                     ucp_recv_desc_t* rdesc,
                                     ucs_ptr_array_t *messages,
                                     unsigned msg_index, uint16_t local_id)
{
    ucs_status_ptr_t ret;
    ucg_over_uct_header_t *header = (ucg_over_uct_header_t*)(rdesc + 1);
    ucs_assert((header->msg.coll_id  !=
                    ((ucg_over_uct_header_step_t*)&local_id)->coll_id) ||
               (header->msg.step_idx >=
                    ((ucg_over_uct_header_step_t*)&local_id)->step_idx));
    /*
     * Note: stored message coll_id can be either larger or smaller than
     * the one currently handled - due to coll_id wrap-around.
     */

    if (ucs_unlikely(header->msg.local_id != local_id)) {
        return op;
    }

    ucs_trace_req("ucg_over_uct_execute_pending FOUND: coll_id %u step_idx %u "
                  "pending %i", header->msg.coll_id, header->msg.step_idx,
                  op->comp.count);

    /* Remove the packet (next call may lead here recursively) */
    ucs_ptr_array_remove(messages, msg_index);

#if ENABLE_MT
    ucs_ptr_array_locked_release_lock((ucs_ptr_array_locked_t*)messages);
#endif

    /* Handle this packet (WARNING: MAY JUMP BACK UP THE STACK!) */
    ret = ucg_over_uct_comp_recv_cb(op, *header, (uint8_t*)(header + 1),
                                    rdesc->length - sizeof(*header),
                                    rdesc->flags, rdesc, phase->rx.iface);

#if ENABLE_MT
    ucs_ptr_array_locked_acquire_lock((ucs_ptr_array_locked_t*)messages);
#endif

    return ret;
}

static unsigned ucg_over_uct_execute_wrapper_progress_send_func(uct_iface_h iface)
{
    uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t);

    if (base_iface->progress_flags & UCT_PROGRESS_SEND) {
        return iface->ops.iface_progress(iface);
    }

    return uct_worker_progress(&base_iface->worker->super);
}

static unsigned ucg_over_uct_execute_wrapper_progress_recv_func(uct_iface_h iface)
{
    uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t);

    if (base_iface->progress_flags & UCT_PROGRESS_RECV) {
        return iface->ops.iface_progress(iface);
    }

    return uct_worker_progress(&base_iface->worker->super);
}

static unsigned ucg_over_uct_execute_wrapper_progress_default_func(uct_iface_h iface)
{
    uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t);

    return uct_worker_progress(&base_iface->worker->super);
}

static UCS_F_DEBUG_OR_INLINE void
ucg_over_uct_execute_set_progress_func(ucg_over_uct_op_t *op, uct_iface_h iface,
                                       enum uct_progress_types progress_type)
{
    uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t);

    if (ucs_likely(base_iface->progress_flags & progress_type)) {
        op->progress_f = iface->ops.iface_progress;
    } else {
        switch (progress_type) {
        case UCT_PROGRESS_SEND:
            op->progress_f = ucg_over_uct_execute_wrapper_progress_send_func;
            break;

        case UCT_PROGRESS_RECV:
            op->progress_f = ucg_over_uct_execute_wrapper_progress_recv_func;
            break;

        default:
            op->progress_f = ucg_over_uct_execute_wrapper_progress_default_func;
            break;
        }
    }

    op->iface = iface;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_ptr_t
ucg_over_uct_execute_pending(ucg_over_uct_op_t *op,
                             ucg_over_uct_plan_phase_t *phase,
                             ucs_ptr_array_t *messages)
{
    ucs_status_ptr_t ret;
    uint16_t local_id;
    unsigned msg_idx;
    void *msg;

    if (ucs_likely(!ucs_ptr_array_is_empty(messages))) {
        local_id = UCG_OVER_UCT_OP_EXPECTED(op)->local_id;
        ucs_assert(local_id != 0);

        ucs_ptr_array_for_each(msg, msg_idx, messages) {
            ret = ucg_over_uct_execute_pending_message(op, phase, msg, messages,
                                                       msg_idx, local_id);
            if (ucs_likely(!UCS_PTR_IS_PTR(ret))) {
                return ret;
            }
        }
    }

    return op;
}


/******************************************************************************
 *                                                                            *
 *                           Operation TX Execution                           *
 *                                                                            *
 ******************************************************************************/

#define UCG_OVER_UCT_GET_PHASE_BCOPY_FLAGS(_phase) \
    (((_phase)->flags) >> UCG_OVER_UCT_PLAN_PHASE_LENGTH_INFO_SHIFT)

#define UCG_OVER_UCT_ASSERT_SEND(_phase, _send_type) \
    ucs_assert((_phase)->tx.send != NULL); \
    ucs_assert((_phase)->tx.am_header.group_id >= UCG_GROUP_FIRST_GROUP_ID); \
    ucs_assert(ucg_over_uct_plan_phase_flags_get_method(phase->flags) == \
               UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_ ## _send_type);

ucs_status_t static UCS_F_DEBUG_OR_INLINE
ucg_over_uct_execute_dummy_send(ucg_over_uct_op_t *op,
                                ucg_over_uct_plan_phase_t *phase,
                                uct_ep_h ep, uint8_t am_id, int is_pipeline,
                                int is_last_step, int is_bcast)
{
    return UCS_OK;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_execute_am_short_common(ucg_over_uct_op_t *op,
                                     ucg_over_uct_plan_phase_t *phase,
                                     uct_ep_h ep, uint8_t am_id, int is_bcast)
{
    ucg_over_uct_header_t header;
    uct_ep_am_short_func_t ep_am_short = phase->tx.send;
    uint8_t *buffer                    = phase->tx.buffer;

    ucg_over_uct_set_header(op, phase, 0, &header);

    if (!is_bcast) {
        if (ucs_unlikely(phase->tx.root == op->iter_ep)) {
            op->iter_ep++;
        }

        buffer += op->iter_ep++ * phase->tx.length;
    }

    UCG_OVER_UCT_ASSERT_SEND(phase, AM_SHORT);

    return ep_am_short(ep, am_id, header.header, buffer, phase->tx.length);
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_execute_am_short_one(ucg_over_uct_op_t *op,
                                  ucg_over_uct_plan_phase_t *phase,
                                  uct_ep_h ep, uint8_t am_id,
                                  int is_pipeline, int is_last_step,
                                  int is_bcast)
{
    return ucg_over_uct_execute_am_short_common(op, phase, ep, am_id, is_bcast);
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_execute_am_short_max(ucg_over_uct_op_t *op,
                                  ucg_over_uct_plan_phase_t *phase,
                                  uct_ep_h ep, uint8_t am_id, int is_pipeline,
                                  int is_last_step, int is_bcast)
{
    ucs_status_t status;
    ucg_over_uct_header_t am_iter;
    ucg_offset_t frag_size         = phase->tx.frag_len;
    uct_coll_length_info_t dt_mode = ucg_over_uct_plan_phase_flags_get_length_info(phase->flags);
    int is_packed                  = dt_mode != UCT_COLL_LENGTH_INFO_DEFAULT;
    unsigned tx_arg                = !is_packed ? frag_size :
                                     UCT_COLL_LENGTH_INFO_PACK(dt_mode, frag_size);
    uint8_t *sbuf                  = phase->tx.buffer;
    if (!is_bcast) {
        if (ucs_unlikely(phase->tx.root == op->iter_ep)) {
            op->iter_ep++;
        }

        sbuf += op->iter_ep++ * phase->tx.length;
    }

    uint8_t *buffer_iter       = sbuf + (op->iter_frag * frag_size);
    size_t buffer_length       = frag_size * phase->rx.frags_cnt;
    uint8_t *buffer_iter_limit = sbuf + buffer_length - frag_size;

    ucg_over_uct_set_header(op, phase, 1, &am_iter);

    UCG_OVER_UCT_ASSERT_SEND(phase, AM_SHORT);
    ucs_assert(op->iter_frag != UCG_OVER_UCT_FRAG_PIPELINE_READY);
    ucs_assert(op->iter_frag != UCG_OVER_UCT_FRAG_PIPELINE_PENDING);
    ucs_assert(frag_size == (is_packed ?
               UCT_COLL_LENGTH_INFO_UNPACK_VALUE(phase->tx.frag_len) :
               phase->tx.frag_len));

    /* send every fragment but the last */
    uct_ep_am_short_func_t ep_am_short = phase->tx.send;
    if (ucs_likely(buffer_iter < buffer_iter_limit)) {
        do {
            status = ep_am_short(ep, am_id, am_iter.header, buffer_iter, tx_arg);

            if (is_pipeline) {
                return status;
            }

            buffer_iter           += frag_size;
            am_iter.remote_offset += frag_size;
        } while ((status == UCS_OK) && (buffer_iter < buffer_iter_limit));

        /* send last fragment of the message */
        if (ucs_unlikely(status != UCS_OK)) {
            /* assuming UCS_ERR_NO_RESOURCE, restore the state for re-entry */
            if (!is_pipeline) {
                op->iter_frag = ((buffer_iter - sbuf) / frag_size) - 1;
            }

            return status;
        }
    }

    tx_arg = sbuf + phase->tx.length - buffer_iter;
    if (is_packed) {
        tx_arg = UCT_COLL_LENGTH_INFO_PACK(dt_mode, tx_arg);
    }

    status        = ep_am_short(ep, am_id, am_iter.header, buffer_iter, tx_arg);
    op->iter_frag = (status == UCS_OK) ? 0 : (buffer_iter - sbuf) / frag_size;

    return status;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_execute_am_bcopy_one(ucg_over_uct_op_t *op,
                                  ucg_over_uct_plan_phase_t *phase,
                                  uct_ep_h ep, uint8_t am_id,
                                  int is_pipeline, int is_last_step,
                                  int is_bcast)
{
    UCG_OVER_UCT_ASSERT_SEND(phase, AM_BCOPY);

    uct_ep_am_bcopy_func_t ep_am_bcopy = phase->tx.send;
    ssize_t len = ep_am_bcopy(ep, am_id, phase->tx.bcopy_single.pack_single_cb,
                              op, UCG_OVER_UCT_GET_PHASE_BCOPY_FLAGS(phase));

    ucs_assert((len < 0) || (phase->tx.bcopy_single.rkey_memh != NULL) ||
               (len == (phase->tx.length + sizeof(ucg_over_uct_header_t))));
    return (ucs_unlikely(len < 0)) ? (ucs_status_t)len : UCS_OK;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_execute_am_bcopy_max(ucg_over_uct_op_t *op,
                                  ucg_over_uct_plan_phase_t *phase,
                                  uct_ep_h ep, uint8_t am_id,
                                  int is_pipeline, int is_last_step,
                                  int is_bcast)
{
    ssize_t len;
    ucg_offset_t frag_size  = phase->tx.frag_len;
    ucg_offset_t iter_limit = phase->tx.length / frag_size;
    packed_send_t send_func = phase->tx.send;
    unsigned flags          = UCG_OVER_UCT_GET_PHASE_BCOPY_FLAGS(phase);
    uct_pack_callback_t cb  = phase->tx.bcopy_fragmented.pack_full_cb;

    /* sanity checks */
    UCG_OVER_UCT_ASSERT_SEND(phase, AM_BCOPY);
    ucs_assert(op->iter_frag <= iter_limit);
    ucs_assert(op->iter_frag != UCG_OVER_UCT_FRAG_PIPELINE_READY);
    ucs_assert(op->iter_frag != UCG_OVER_UCT_FRAG_PIPELINE_PENDING);

    /* check if this is not, by any chance, the last fragment */
    if (ucs_likely(op->iter_frag < iter_limit)) {
        /* send every fragment but the last */
        do {
            len = send_func(ep, am_id, cb, op, flags);
            ucs_assert((len < 0) || (len == (frag_size +
                                             sizeof(ucg_over_uct_header_t))));
            if (is_pipeline) {
                return ucs_unlikely(len < 0) ? (ucs_status_t)len : UCS_OK;
            }

            op->iter_frag++;
        } while ((len >= 0) && (op->iter_frag < iter_limit));

        if (ucs_unlikely(len < 0)) {
            op->iter_frag--;
            return (ucs_status_t)len;
        }
    }

    /* Send last fragment of the message */
    cb  = phase->tx.bcopy_fragmented.pack_part_cb;
    len = send_func(ep, am_id, cb, op, flags);
    if (ucs_unlikely(len < 0)) {
        return (ucs_status_t)len;
    }

    return UCS_OK;
}

static void ucg_over_uct_comp_am_zcopy_last_cb(uct_completion_t *comp)
{
    ucg_over_uct_comp_last_step_cb(ucs_container_of(comp, ucg_over_uct_op_t, comp),
                                   UCS_OK);
}

static void ucg_over_uct_comp_am_zcopy_step_cb(uct_completion_t *comp)
{
    ucg_over_uct_comp_step_cb(ucs_container_of(comp, ucg_over_uct_op_t, comp));
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_execute_zcopy_common(ucg_over_uct_op_t *op,
                                  ucg_over_uct_plan_phase_t *phase,
                                  uct_ep_h ep, uint8_t am_id, int is_last_step,
                                  int is_bcast, unsigned type)
{
    ucs_status_t status;
    ucg_over_uct_header_t header;
    uct_ep_am_zcopy_func_t ep_am_zcopy;

    uct_iov_t iov = {
            .buffer = phase->tx.buffer,
            .length = phase->tx.length,
            .memh   = phase->tx.zcopy.memh,
            .stride = 0,
            .count  = 1
    };

    if (!is_bcast) {
        if (ucs_unlikely(phase->tx.root == op->iter_ep)) {
            op->iter_ep++;
        }

        iov.buffer = (uint8_t*)iov.buffer + op->iter_ep++ * phase->tx.length;
    }

    ucg_over_uct_comp_count_add(op, 1);
    op->comp.func = is_last_step ? ucg_over_uct_comp_am_zcopy_last_cb :
                                   ucg_over_uct_comp_am_zcopy_step_cb;

    switch (type) {
    case UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY:
        ucg_over_uct_set_header(op, phase, 0, &header);
        ep_am_zcopy = phase->tx.send;
        status      = ep_am_zcopy(ep, am_id, &header, sizeof(uint64_t), &iov, 1,
                                  0, &op->comp);
        break;

    // case UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_PUT_ZCOPY:
    //     ep_put_zcopy = phase->tx.send;
    //     status       = ep_put_zcopy(ep, &iov, 1, op->raddr, op->rkey.rkey,
    //                                 &op->comp);
    //     break;

    // case UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_GET_ZCOPY:
    //     ep_get_zcopy = phase->tx.send;
    //     status       = ep_get_zcopy(ep, &iov, 1, op->raddr, op->rkey.rkey,
    //                                 &op->comp);
    //     break;

    default:
        ucs_error("Invalid send type for zero-copy planning");
        return UCS_ERR_INVALID_PARAM;
    }

    return ucs_unlikely(status != UCS_INPROGRESS) ? status : UCS_OK;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_execute_am_zcopy_one(ucg_over_uct_op_t *op,
                                  ucg_over_uct_plan_phase_t *phase,
                                  uct_ep_h ep, uint8_t am_id,
                                  int is_pipeline, int is_last_step,
                                  int is_bcast)
{
    UCG_OVER_UCT_ASSERT_SEND(phase, AM_ZCOPY);

    return ucg_over_uct_execute_zcopy_common(op, phase, ep, am_id, is_last_step, is_bcast,
                                             UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY);
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_uct_execute_am_zcopy_max(ucg_over_uct_op_t *op,
                                  ucg_over_uct_plan_phase_t *phase,
                                  uct_ep_h ep, uint8_t am_id,
                                  int is_pipeline, int is_last_step,
                                  int is_bcast)
{
    ucs_status_t status;
    void* iov_buffer_limit;
    ucg_over_uct_header_t header;
    ucg_offset_t frag_size = phase->tx.frag_len;
    uint8_t *sbuf          = phase->tx.buffer;
    uct_iov_t iov          = {
        .buffer            = sbuf + (op->iter_frag * frag_size),
        .length            = frag_size,
        .memh              = phase->tx.zcopy.memh,
        .stride            = 0,
        .count             = 1
    };

    /* sanity checks */
    UCG_OVER_UCT_ASSERT_SEND(phase, AM_ZCOPY);
    ucs_assert(op->iter_frag != UCG_OVER_UCT_FRAG_PIPELINE_READY);
    ucs_assert(op->iter_frag != UCG_OVER_UCT_FRAG_PIPELINE_PENDING);
    ucs_assert(op->iter_frag <= phase->tx.length / phase->tx.frag_len);

    ucg_over_uct_set_header(op, phase, 1, &header);

    if (!is_bcast) {
        if (ucs_unlikely(phase->tx.root == op->iter_ep)) {
            op->iter_ep++;
        }
        iov.buffer = (uint8_t*)iov.buffer + op->iter_ep++ * phase->tx.length;
    }

    iov_buffer_limit = sbuf + phase->tx.length - frag_size;
    op->comp.func    = is_last_step ? ucg_over_uct_comp_am_zcopy_last_cb :
                                      ucg_over_uct_comp_am_zcopy_step_cb;

    /* check if this is not, by any chance, the last fragment */
    uct_ep_am_zcopy_func_t ep_am_zcopy = phase->tx.send;
    if (ucs_likely(iov.buffer < iov_buffer_limit)) {
        /* send every fragment but the last */
        do {
            ucg_over_uct_comp_count_add(op, 1); /* Expect completion for send */
            status = ep_am_zcopy(ep, am_id, &header, sizeof(uint64_t), &iov, 1,
                                 0, &op->comp);

            if (is_pipeline) {
                if (ucs_unlikely(status != UCS_INPROGRESS)) {
                    ucg_over_uct_comp_count_add(op, -1);
                }
                return status;
            }

            header.remote_offset += frag_size;
            iov.buffer = (void*)((uint8_t*)iov.buffer + frag_size);
        } while ((status == UCS_INPROGRESS) && (iov.buffer < iov_buffer_limit));

        if (ucs_unlikely(status != UCS_INPROGRESS)) {
            op->iter_frag = (((uint8_t*)iov.buffer - sbuf) / frag_size) - 1;
            ucg_over_uct_comp_count_add(op, -1);
            return status;
        }
    }

    /* Send last fragment of the message */
    ucg_over_uct_comp_count_add(op, 1); /* Expect completion for a last send */
    iov.length = sbuf + phase->tx.length - (uint8_t*)iov.buffer;
    status     = ep_am_zcopy(ep, am_id, &header, sizeof(uint64_t), &iov, 1, 0,
                             &op->comp);

    if (ucs_unlikely(status != UCS_INPROGRESS)) {
        op->iter_frag = ((uint8_t*)iov.buffer - sbuf) / frag_size;
        ucg_over_uct_comp_count_add(op, -1);
        return status;
    }

    return UCS_OK;
}

/*
 * Below is a set of macros, generating most bit-field combinations of
 * phase->flags in the switch-case inside @ref ucg_over_uct_execute_op() .
 */
#define first_exec_label _first_exec_label
#define last_exec_label _last_exec_label

#define gen_exec_label(_send_func, _send_flag, _is_fragmented, _is_bcast, \
                       _is_pipeline, _is_variadic, _is_strided, _is_1ep, \
                       _is_last) \
    _send_func ## _send_flag ## _is_fragmented ## _is_bcast ## _is_pipeline ## _is_variadic ## _is_strided ## _is_1ep ## _is_last

#define case_exec_diff(_send_func, _send_flag, _is_fragmented, _is_bcast, \
                       _is_pipeline, _is_variadic, _is_strided, _is_1ep, \
                       _is_last) \
    &&gen_exec_label(_send_func, _send_flag, _is_fragmented, _is_bcast, \
                     _is_pipeline, _is_variadic, _is_strided, _is_1ep, \
                     _is_last) - &&first_exec_label,

#define case_exec_visibility(_send_func, _send_flag, _is_fragmented, _is_bcast, \
                             _is_pipeline, _is_variadic, _is_strided, _is_1ep, \
                             _is_last) \
    gen_exec_label(_send_func, _send_flag, _is_fragmented, _is_bcast, \
                   _is_pipeline, _is_variadic, _is_strided, _is_1ep, _is_last) ,

#define case_execution(_send_func, _send_flag, _is_fragmented, _is_bcast,     \
                       _is_pipeline, _is_variadic, _is_strided, _is_1ep,      \
                       _is_last)                                              \
    UCS_JUMPTBL_LABEL(gen_exec_label(_send_func, _send_flag, _is_fragmented,  \
                                     _is_bcast, _is_pipeline, _is_variadic,   \
                                     _is_strided, _is_1ep, _is_last))                            \
    ucs_assert(flags == ((_is_fragmented ? UCG_OVER_UCT_PLAN_PHASE_FLAG_FRAGMENTED      : 0) | \
                         (_is_bcast      ? UCG_OVER_UCT_PLAN_PHASE_FLAG_BCAST           : 0) | \
                         (_is_pipeline   ? UCG_OVER_UCT_PLAN_PHASE_FLAG_PIPELINED       : 0) | \
                         (_is_variadic   ? UCG_OVER_UCT_PLAN_PHASE_FLAG_SEND_VARIADIC   : 0) | \
                         (_is_strided    ? UCG_OVER_UCT_PLAN_PHASE_FLAG_SEND_STRIDED    : 0) | \
                         (_is_1ep        ? UCG_OVER_UCT_PLAN_PHASE_FLAG_SINGLE_ENDPOINT : 0) | \
                         (_is_last       ? UCG_OVER_UCT_PLAN_PHASE_FLAG_LAST_STEP       : 0) | \
                         (_send_flag << UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS))); \
                                                                               \
        ucs_assert((_send_flag) != 0);                                         \
        if (_is_pipeline) {                                                    \
            frags_per_ep = phase->rx.frags_cnt / phase->tx.ep_cnt;             \
            ucs_assert(!(phase->rx.frags_cnt % phase->tx.ep_cnt));             \
        }                                                                      \
                                                                               \
        /* Perform one or many send operations, unless an error occurs */      \
        if (_is_1ep) {                                                         \
            status = _send_func (op, phase, phase->tx.single_ep,               \
                                 phase->tx.am_id, _is_pipeline, _is_last,      \
                                 _is_bcast);                                   \
            if (ucs_unlikely(status != UCS_OK)) {                              \
                goto phase_execute_error;                                      \
            }                                                                  \
            op->iter_ep = 1;                                                   \
            if (!_is_pipeline) {                                               \
                op->iter_frag = 0;                                             \
            }                                                                  \
        } else {                                                               \
            if ((_is_pipeline) && (ucs_unlikely(op->iter_frag ==               \
                                   UCG_OVER_UCT_FRAG_PIPELINE_PENDING))) {     \
                /* find a pending offset to progress */                        \
                unsigned frag_idx = 0;                                         \
                while ((frag_idx < frags_per_ep) &&                            \
                       (op->fragment_pending[frag_idx] ==                      \
                        UCG_OVER_UCT_FRAG_PENDING)) {                          \
                    frag_idx++;                                                \
                }                                                              \
                ucs_assert(frag_idx < frags_per_ep);                           \
                op->iter_frag = frag_idx;                                      \
            }                                                                  \
                                                                               \
            if (_send_flag == UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY) {  \
                ucg_over_uct_comp_count_set_safety(op, 1);                     \
            }                                                                  \
                                                                               \
            ep_iter = ep_last = phase->tx.multi_eps;                           \
            ep_iter += op->iter_ep;                                            \
            ep_last += phase->tx.ep_cnt;                                       \
            if (ucs_unlikely(ep_iter == ep_last)) {                            \
                /* Sending is still in progress - keep waiting... */           \
                return op;                                                     \
            }                                                                  \
                                                                               \
            do {                                                               \
                status = _send_func (op, phase, *ep_iter, phase->tx.am_id,     \
                                     _is_pipeline, _is_last, _is_bcast);       \
                if (ucs_unlikely(status != UCS_OK)) {                          \
                    op->iter_ep = ep_iter - phase->tx.multi_eps;               \
                    goto phase_execute_error;                                  \
                }                                                              \
                                                                               \
                if (_is_strided) {                                             \
                    op->iter_frag++;                                           \
                } else if (!_is_pipeline) {                                    \
                    op->iter_frag = 0;                                         \
                }                                                              \
            } while (++ep_iter < ep_last);                                     \
                                                                               \
            if (_is_pipeline) {                                                \
                /* Reset the iterator for the next pipelined incoming packet */\
                /* TODO: op->iter_ep = _is_r1s ? 1 : phase->tx.ep_cnt - 1; */  \
                                                                               \
                /* Check if this invocation is a result of a resend attempt */ \
                unsigned idx = op->iter_frag;                                  \
                if (ucs_unlikely(op->fragment_pending[idx] ==                  \
                        UCG_OVER_UCT_FRAG_PENDING)) {                          \
                    op->fragment_pending[idx] = 0;                             \
                                                                               \
                    /* Look for other packets in need of resending */          \
                    for (idx = 0; idx < frags_per_ep; idx++) {                 \
                        if (op->fragment_pending[idx] ==                       \
                                UCG_OVER_UCT_FRAG_PENDING) {                   \
                            /* Found such packets - mark for next resend */    \
                            op->iter_frag = idx;                               \
                            status        = UCS_ERR_NO_RESOURCE;               \
                            goto phase_execute_error;                          \
                        }                                                      \
                    }                                                          \
                } else {                                                       \
                    ucs_assert(op->fragment_pending[idx] == 0);                \
                }                                                              \
                op->iter_frag = UCG_OVER_UCT_FRAG_PIPELINE_READY;              \
            } else {                                                           \
                op->iter_ep = phase->tx.ep_cnt;                                \
            }                                                                  \
                                                                               \
            if (_send_flag == UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY) {  \
                ucg_over_uct_comp_count_set_safety(op, 0);                     \
                goto phase_execute_tx_progress;                                \
            }                                                                  \
        }                                                                      \
                                                                               \
        /* Nothing else to do - complete this step */                          \
        ucs_assert(status == UCS_OK);                                          \
        if (_is_last) {                                                        \
            goto last_step_done;                                               \
        }                                                                      \
        goto step_done;

#define case_send_1ep(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, _is_variadic, _is_strided, _is_1ep) \
                      _m (_send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, _is_variadic, _is_strided, _is_1ep, 0) \
                      _m (_send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, _is_variadic, _is_strided, _is_1ep, 1)

#define case_send_strided(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, _is_variadic, _is_strided) \
            case_send_1ep(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, _is_variadic, _is_strided, 0) \
            case_send_1ep(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, _is_variadic, _is_strided, 1)

#define case_send_variadic(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, _is_variadic) \
         case_send_strided(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, _is_variadic, 0) \
         case_send_strided(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, _is_variadic, 1)

#define case_send_pipeline(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline) \
        case_send_variadic(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, 0) \
        case_send_variadic(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, _is_pipeline, 1)

#define case_send_bcast(_m, _send_func, _send_flag, _is_fragmented, _is_bcast) \
     case_send_pipeline(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, 0) \
     case_send_pipeline(_m, _send_func, _send_flag, _is_fragmented, _is_bcast, 1)

#define case_send_method(_m, _send_func, _send_flag, _is_fragmented) \
         case_send_bcast(_m, _send_func, _send_flag, _is_fragmented, 0) \
         case_send_bcast(_m, _send_func, _send_flag, _is_fragmented, 1)

#define        case_send(_m, _send_flag, _one_send_f, _max_send_f) \
        case_send_method(_m, _one_send_f, _send_flag, 0) \
        case_send_method(_m, _max_send_f, _send_flag, 1) \

#define ALL_EXEC_CASES_IN_ASCENDING_ORDER(_macro) \
    case_send(_macro, UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_NONE, \
              ucg_over_uct_execute_dummy_send, \
              ucg_over_uct_execute_dummy_send) \
    \
    case_send(_macro, UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_SHORT, \
              ucg_over_uct_execute_am_short_one, \
              ucg_over_uct_execute_am_short_max) \
    \
    case_send(_macro, UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_BCOPY, \
              ucg_over_uct_execute_am_bcopy_one, \
              ucg_over_uct_execute_am_bcopy_max) \
    \
    case_send(_macro, UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY, \
              ucg_over_uct_execute_am_zcopy_one, \
              ucg_over_uct_execute_am_zcopy_max)

/*
 * Executing a single step is the heart of the Builtin planner.
 * This function advances to the next step (some invocations negate that...),
 * sends and then receives according to the instructions of this step.
 * The function returns the status, typically one of the following:
 * > UCS_OK - collective operation (not just this step) has been completed
 * > op pointer - if the operation is still in progress, returns a pointer to it
 * > otherwise - an error has occurred
 *
 * For example, a "complex" case is when the message is fragmented, and requires
 * both receiving and sending in a single step, like in REDUCE_WAYPOINT. The
 * first call, coming from @ref ucg_over_uct_plan_trigger() , will enter the first
 * branch ("step_ep" is zero when a new step is starting), will process some
 * potential incoming messages (arriving beforehand) - returning UCS_INPROGRESS.
 * Subsequent calls to "progress()" will handle the rest of the incoming
 * messages for this step, and eventually call this function again from within
 * @ref ucg_over_uct_comp_step_cb() . This call will choose the second branch,
 * the switch-case, which will send the message and
 */
ucs_status_ptr_t ucg_over_uct_execute_op(ucg_over_uct_op_t *op)
{
#if ENABLE_JUMP_TABLES
    __label__ ALL_EXEC_CASES_IN_ASCENDING_ORDER(case_exec_visibility)
              first_exec_label, last_exec_label, phase_execute_error,
              last_step_done, step_done;
#endif

    ucs_status_ptr_t ret;
    unsigned frags_per_ep;
    ucs_ptr_array_t *msg_array;
    uct_ep_h *ep_iter, *ep_last;
    ucg_over_uct_comp_slot_t *slot;
    ucg_over_uct_plan_phase_t *phase = op->phase;
    uint32_t flags = phase->flags;
    ucs_status_t status = UCS_OK;

    ucs_assert(op->super.status == UCS_INPROGRESS);

    /* Check all expected messages arrived before proceeding to send */
    if (ucs_likely(!(flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_EXPECTS_RX) ||
                   !op->comp.count)) {
        flags &= UCG_OVER_UCT_PLAN_PHASE_FLAG_SWITCH_MASK;
        /* This step either starts by sending or contains no send operations */
        UCS_JUMPTBL_INIT(ucg_over_uct_execute,
                         ALL_EXEC_CASES_IN_ASCENDING_ORDER(case_exec_diff))

        UCS_JUMPTBL_JUMP(ucg_over_uct_execute, flags, first_exec_label)

        ALL_EXEC_CASES_IN_ASCENDING_ORDER(case_execution)

        UCS_JUMPTBL_LABEL_TERMINATOR(last_exec_label)
    }

    /*********************** Step completion flows ****************************/

    slot = ucs_container_of(op, ucg_over_uct_comp_slot_t, op);
#if ENABLE_MT
    ucs_ptr_array_locked_acquire_lock(&slot->messages);
    msg_array = &slot->messages.super;
#else
    msg_array = &slot->messages;
#endif

    /* Check the pending messages for what this operation is missing */
    ret = ucg_over_uct_execute_pending(op, phase, msg_array);
    if (ucs_likely(!UCS_PTR_IS_ERR(ret))) {
        if (status == UCS_INPROGRESS) {
            /* This is an incomplete send, e.g. zero-copy */
#if ENABLE_MT
            ucs_ptr_array_locked_release_lock(&slot->messages);
#endif
            goto phase_execute_tx_progress;
        }

        ucg_over_uct_execute_set_progress_func(op, phase->rx.iface,
                                               UCT_PROGRESS_RECV);

        /*
         * If this is still a sync. call (inside the call-stack of the user's
         * original invocation) - call progress once (best effort) and retry.
         */
        if ((!(op->super.flags &
               UCS_PMODULE_TARGET_ACTION_FLAG_ASYNC_COMPLETION)) &&
            (op->progress_f(phase->rx.iface))) {
            ret = ucg_over_uct_execute_pending(op, phase, msg_array);
        }
    }

#if ENABLE_MT
    ucs_ptr_array_locked_release_lock(&slot->messages);
#endif
    return ret;

phase_execute_error:
    if (status == UCS_ERR_NO_RESOURCE) {
        /* Special case: send incomplete - enqueue for resend upon progress */
        if (flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_PIPELINED) {
            op->fragment_pending[op->iter_frag] = UCG_OVER_UCT_FRAG_PENDING;
            op->iter_frag                       = UCG_OVER_UCT_FRAG_PIPELINE_PENDING;
        }

        /* Add this request to the resend-queue */
        ucg_over_uct_schedule_resend(op);

phase_execute_tx_progress:
        ucg_over_uct_execute_set_progress_func(op, phase->tx.iface,
                                               UCT_PROGRESS_SEND);
        return op;
    }

last_step_done:
    ucg_over_uct_comp_last_step_cb(op, status);
    return UCS_STATUS_PTR(status);

step_done:
    return ucg_over_uct_comp_step_cb(op);
}
