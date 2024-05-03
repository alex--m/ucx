/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_uct.h"
#include "over_uct_comp.inl"

#include <ucs/arch/atomic.h>

enum over_uct_pack_reduce_method {
    UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION,
    UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION,
    UCG_OVER_UCT_PACK_REDUCE_METHOD_INTERNAL_CB,
    UCG_OVER_UCT_PACK_REDUCE_METHOD_EXTERNAL_CB
};

#define UCG_OVER_UCT_PACKER_NAME(_modifier, _mode) \
    ucg_over_uct_plan_am_bcopy_pack ## _modifier ## _mode

#define UCG_OVER_UCT_PACKER_DECLARE(_modifier, _mode) \
    size_t UCG_OVER_UCT_PACKER_NAME(_modifier, _mode) (void *dest, void *arg)

#ifdef USE_NONTEMPORAL_MEMCPY
#define UCS_MEMCPY ucs_memcpy_nontemporal
#else
#define UCS_MEMCPY memcpy
#endif

#define UCG_OVER_UCT_PACK_CB(_use_frag_offset, _length, _set_header, _is_bcast, \
                             _is_memcpy) { \
    size_t total_offset; \
    UCS_V_UNUSED ucg_over_uct_header_t storage, *header; \
    ucg_over_uct_op_t *op            = (ucg_over_uct_op_t*)arg; \
    ucg_over_uct_plan_phase_t *phase = op->phase; \
    size_t buffer_length             = (_length); \
    \
    header = (_set_header) ? (ucg_over_uct_header_t*)dest : &storage; \
    ucg_over_uct_set_header(op, phase, (_use_frag_offset), header); \
    total_offset = header->remote_offset; \
    ucs_assert(total_offset <= op->phase->tx.length); \
    ucs_assert(header->header != 0); \
    if (_set_header) { \
        dest = UCS_PTR_BYTE_OFFSET(dest, sizeof(header)); \
    } \
    \
    if (!(_is_bcast)) { \
        if (ucs_unlikely(phase->tx.root == op->iter_ep)) { \
            op->iter_ep++; \
        } \
        total_offset += op->iter_ep * phase->tx.length; \
    } \
    \
    if (_is_memcpy) { \
        UCS_MEMCPY(dest, UCS_PTR_BYTE_OFFSET(phase->tx.buffer, total_offset), \
                   buffer_length); \
        return sizeof(*header) + buffer_length; \
    } \
}

UCG_OVER_UCT_PACKER_DECLARE(_, single)
UCG_OVER_UCT_PACK_CB(0, phase->tx.length, 1, 1, 1)

UCG_OVER_UCT_PACKER_DECLARE(_, full)
UCG_OVER_UCT_PACK_CB(1, phase->tx.frag_len, 1, 1, 1)

UCG_OVER_UCT_PACKER_DECLARE(_, part)
UCG_OVER_UCT_PACK_CB(1, phase->tx.length - (op->iter_frag *
                                            phase->tx.frag_len), 1, 1, 1)

UCG_OVER_UCT_PACKER_DECLARE(_scatter_, single)
UCG_OVER_UCT_PACK_CB(0, phase->tx.length, 1, 0, 1)

UCG_OVER_UCT_PACKER_DECLARE(_scatter_, full)
UCG_OVER_UCT_PACK_CB(1, phase->tx.frag_len, 1, 0, 1)

UCG_OVER_UCT_PACKER_DECLARE(_scatter_, part)
UCG_OVER_UCT_PACK_CB(1, phase->tx.length - (op->iter_frag *
                                            phase->tx.frag_len), 1, 0, 1)

#define UCG_OVER_UCT_SLOTTED_PACK_CB(_offset_in_frags, _length) { \
    if (ucs_likely((uintptr_t)arg & UCT_PACK_CALLBACK_REDUCE)) { \
        arg = (void*)((uintptr_t)arg & ~UCT_PACK_CALLBACK_REDUCE); \
        UCG_OVER_UCT_PACK_CB((_offset_in_frags), (_length), 0, 1, 1) \
    } else { \
        UCG_OVER_UCT_PACK_CB((_offset_in_frags), (_length), 1, 1, 1) \
    } \
}

UCG_OVER_UCT_PACKER_DECLARE(_slotted_, single)
UCG_OVER_UCT_SLOTTED_PACK_CB(0, phase->tx.length)

UCG_OVER_UCT_PACKER_DECLARE(_slotted_, full)
UCG_OVER_UCT_SLOTTED_PACK_CB(1, phase->tx.frag_len)

UCG_OVER_UCT_PACKER_DECLARE(_slotted_, part)
UCG_OVER_UCT_SLOTTED_PACK_CB(1, phase->tx.length - (op->iter_frag *
                                                    phase->tx.frag_len))

#define UCG_OVER_UCT_REDUCE_PACK_CB(_method, _integer_bits, _is_rkey, _offset_in_frags, _length) { \
    if (ucs_likely(((uintptr_t)arg & UCT_PACK_CALLBACK_REDUCE) == 0)) { \
        UCG_OVER_UCT_PACK_CB((_offset_in_frags), (_length), 1, 1, \
            ((_method) != UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION)) \
    } \
    { \
        unsigned index; \
        uct_component_h component; \
        uct_rkey_bundle_t src_rkey; \
        uct_rkey_bundle_t dst_rkey; \
        ucg_over_uct_plan_t *plan; \
        UCS_V_UNUSED uct_md_h md; \
        UCS_V_UNUSED ucs_status_t status; \
        const ucg_collective_params_t *params; \
        ucg_over_uct_op_t *op            = (ucg_over_uct_op_t*)((uintptr_t)arg & \
                                                                ~UCT_PACK_CALLBACK_REDUCE); \
        ucg_over_uct_plan_phase_t *phase = op->phase; \
        size_t buffer_length             = (_length); \
        \
        uint##_integer_bits##_t* restrict dst_buf = (uint##_integer_bits##_t*)dest; \
        uint##_integer_bits##_t* restrict src_buf = UCS_PTR_BYTE_OFFSET(phase->tx.buffer, \
                                                                        ((_offset_in_frags) * \
                                                                         phase->tx.frag_len)); \
        \
        if (_is_rkey) { \
            md     = phase->info[UCG_OVER_UCT_PHASE_EXTRA_INFO_TX].md; \
            status = over_uct_phase_rkey_pair_use((void**)&src_buf, &src_rkey, \
                                                  (void**)&dst_buf, &dst_rkey, \
                                                  md, &component, &buffer_length); \
        } \
        \
        switch (_method) { \
        case UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION: \
            for (index = 0; \
                 index < buffer_length / sizeof(uint##_integer_bits##_t); \
                 index++) { \
                dst_buf[index] += src_buf[index]; \
            } \
            break; \
        \
        case UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION: \
            for (index = 0; \
                 index < buffer_length / sizeof(uint##_integer_bits##_t); \
                 index++) { \
                ucs_atomic_add##_integer_bits (&dst_buf[index], src_buf[index]); \
            } \
            break; \
        \
        case UCG_OVER_UCT_PACK_REDUCE_METHOD_INTERNAL_CB: \
            phase->rx.reduce_f(dst_buf, src_buf, (buffer_length)); \
            break; \
        \
        case UCG_OVER_UCT_PACK_REDUCE_METHOD_EXTERNAL_CB: \
            plan   = ucs_derived_of(op->super.plan, ucg_over_uct_plan_t); \
            params = ucg_plan_get_params(&plan->super.super); \
            ucg_global_params.reduce_op.reduce_cb_f(UCG_PARAM_OP(params), src_buf, \
                                                    dst_buf, (buffer_length) / \
                                                        ucp_dt_length(plan->recv_dt, \
                                                                      1, NULL, NULL), \
                                                    params->recv.dtype, NULL); \
            break; \
        } \
        \
        if (_is_rkey) { \
            ucg_over_uct_phase_rkey_pair_discard(&src_rkey, &dst_rkey, component); \
        } \
        \
        return (_length); \
    } \
}

#define UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_name, _method, _integer_bits, _is_rkey) \
    UCG_OVER_UCT_PACKER_DECLARE(_name, single) \
    UCG_OVER_UCT_REDUCE_PACK_CB(_method, _integer_bits, _is_rkey, 0, phase->tx.length) \
    UCG_OVER_UCT_PACKER_DECLARE(_name, full) \
    UCG_OVER_UCT_REDUCE_PACK_CB(_method, _integer_bits, _is_rkey, 1, phase->tx.frag_len) \
    UCG_OVER_UCT_PACKER_DECLARE(_name, part) \
    UCG_OVER_UCT_REDUCE_PACK_CB(_method, _integer_bits, _is_rkey, 1, phase->tx.length - (op->iter_frag * phase->tx.frag_len))

UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_internal_,         UCG_OVER_UCT_PACK_REDUCE_METHOD_INTERNAL_CB,      8,  0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_external_,         UCG_OVER_UCT_PACK_REDUCE_METHOD_EXTERNAL_CB,      8,  0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_summation8_,       UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION,        8,  0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_summation16_,      UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION,        16, 0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_summation32_,      UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION,        32, 0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_summation64_,      UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION,        64, 0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_atomic8_,          UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION, 8,  0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_atomic16_,         UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION, 16, 0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_atomic32_,         UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION, 32, 0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_atomic64_,         UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION, 64, 0)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_internal_rkey_,    UCG_OVER_UCT_PACK_REDUCE_METHOD_INTERNAL_CB,      8,  1)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_external_rkey_,    UCG_OVER_UCT_PACK_REDUCE_METHOD_EXTERNAL_CB,      8,  1)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_summation8_rkey_,  UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION,        8,  1)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_summation16_rkey_, UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION,        16, 1)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_summation32_rkey_, UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION,        32, 1)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_summation64_rkey_, UCG_OVER_UCT_PACK_REDUCE_METHOD_SUMMATION,        64, 1)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_atomic8_rkey_,     UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION, 8,  1)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_atomic16_rkey_,    UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION, 16, 1)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_atomic32_rkey_,    UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION, 32, 1)
UCG_OVER_UCT_PACKER_DECLARE_REDUCTION(_atomic64_rkey_,    UCG_OVER_UCT_PACK_REDUCE_METHOD_ATOMIC_SUMMATION, 64, 1)

#define UCG_OVER_UCT_DATATYPE_PACK_CB(_offset_in_frags, _length) { \
    ucg_over_uct_header_t *header    = (ucg_over_uct_header_t*)dest; \
    ucg_over_uct_op_t *op            = (ucg_over_uct_op_t*)arg; \
    ucg_over_uct_plan_phase_t *phase = op->phase; \
    ucg_over_uct_plan_t *plan        = ucs_derived_of(op->super.plan, \
                                                      ucg_over_uct_plan_t); \
    ucp_dt_generic_t *dt_gen         = ucp_dt_to_generic(plan->send_dt); \
    void *dt_state                   = op->send_pack; \
    size_t packed_offset             = (_offset_in_frags) * phase->tx.frag_len; \
    size_t buffer_length             = (_length); \
    header->header                   = phase->tx.am_header.header; \
    \
    ucs_assert(((uintptr_t)arg & UCT_PACK_CALLBACK_REDUCE) == 0); \
    \
    dt_gen->ops.pack(dt_state, packed_offset, header + 1, buffer_length); \
    \
    return sizeof(*header) + buffer_length; \
}

UCG_OVER_UCT_PACKER_DECLARE(_datatype_, single)
UCG_OVER_UCT_DATATYPE_PACK_CB(0, phase->tx.length)

UCG_OVER_UCT_PACKER_DECLARE(_datatype_, full)
UCG_OVER_UCT_DATATYPE_PACK_CB(1, phase->tx.frag_len)

UCG_OVER_UCT_PACKER_DECLARE(_datatype_, part)
UCG_OVER_UCT_DATATYPE_PACK_CB(1, phase->tx.length - (op->iter_frag *
                                                     phase->tx.frag_len))

ucs_status_t ucg_over_uct_pack_select_cb(ucg_over_uct_plan_phase_t *phase,
                                         const ucg_collective_params_t *params,
                                         const ucg_over_ucp_plan_dt_info_t *dt,
                                         const uct_iface_attr_t *iface_attr,
                                         int is_fragmented)
{
    int is_signed;
    int want_location;
    int is_commutative;
    enum ucg_operator operator;
    uint16_t modifiers = UCG_PARAM_MODIFIERS(params);
    int is_bcast       = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST;
    int is_single_dest = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION;
    int is_reduction   = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE;
    int is_tl_incast   = (iface_attr->cap.flags & UCT_IFACE_FLAG_INCAST) != 0;
    int is_tl_slotted  = (iface_attr->cap.flags & UCT_IFACE_FLAG_INCAST_SLOTTED) != 0;
    int is_unordered   = (iface_attr->cap.flags & UCT_IFACE_FLAG_INCAST_UNORDERED) != 0;
    int is_atomic      = is_unordered && !is_tl_slotted;
    int inplace_reduce = is_tl_incast && is_reduction && !is_tl_slotted;
    ucs_assert(!is_tl_incast || is_reduction);

    if ((inplace_reduce) &&
        /* Check if the operand is an integer */
        (ucg_global_params.field_mask & UCG_PARAM_FIELD_DATATYPE_CB) &&
        (ucg_global_params.datatype.is_integer_f(dt->orig_dt, &is_signed)) &&
        // TODO: consider cases where the signedness needs to be checked
        /* Check if the operator is summation */
        (ucg_global_params.field_mask & UCG_PARAM_FIELD_REDUCE_OP_CB) &&
        (ucg_global_params.reduce_op.get_operator_f != NULL) &&
        (!ucg_global_params.reduce_op.get_operator_f(UCG_PARAM_OP(params),
                                                     &operator, &want_location,
                                                     &is_commutative)) &&
        (operator == UCG_OPERATOR_SUM) && !want_location) {
        switch (dt->dt_size) {
        case 1:
            if (is_atomic) {
                if (is_fragmented) {
                    phase->tx.bcopy_fragmented.pack_full_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic8_, full);
                    phase->tx.bcopy_fragmented.pack_part_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic8_, part);
                } else {
                    phase->tx.bcopy_single.pack_single_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic8_, single);
                }
            } else {
                if (is_fragmented) {
                    phase->tx.bcopy_fragmented.pack_full_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation8_, full);
                    phase->tx.bcopy_fragmented.pack_part_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation8_, part);
                } else {
                    phase->tx.bcopy_single.pack_single_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation8_, single);
                }
            }
            return UCS_OK;

        case 2:
            if (is_atomic) {
                if (is_fragmented) {
                    phase->tx.bcopy_fragmented.pack_full_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic16_, full);
                    phase->tx.bcopy_fragmented.pack_part_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic16_, part);
                } else {
                    phase->tx.bcopy_single.pack_single_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic16_, single);
                }
            } else {
                if (is_fragmented) {
                    phase->tx.bcopy_fragmented.pack_full_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation16_, full);
                    phase->tx.bcopy_fragmented.pack_part_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation16_, part);
                } else {
                    phase->tx.bcopy_single.pack_single_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation16_, single);
                }
            }
            return UCS_OK;

        case 4:
            if (is_atomic) {
                if (is_fragmented) {
                    phase->tx.bcopy_fragmented.pack_full_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic32_, full);
                    phase->tx.bcopy_fragmented.pack_part_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic32_, part);
                } else {
                    phase->tx.bcopy_single.pack_single_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic32_, single);
                }
            } else {
                if (is_fragmented) {
                    phase->tx.bcopy_fragmented.pack_full_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation32_, full);
                    phase->tx.bcopy_fragmented.pack_part_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation32_, part);
                } else {
                    phase->tx.bcopy_single.pack_single_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation32_, single);
                }
            }
            return UCS_OK;

        case 8:
            if (is_atomic) {
                if (is_fragmented) {
                    phase->tx.bcopy_fragmented.pack_full_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic64_, full);
                    phase->tx.bcopy_fragmented.pack_part_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic64_, part);
                } else {
                    phase->tx.bcopy_single.pack_single_cb =
                            UCG_OVER_UCT_PACKER_NAME(_atomic64_, single);
                }
            } else {
                if (is_fragmented) {
                    phase->tx.bcopy_fragmented.pack_full_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation64_, full);
                    phase->tx.bcopy_fragmented.pack_part_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation64_, part);
                } else {
                    phase->tx.bcopy_single.pack_single_cb =
                            UCG_OVER_UCT_PACKER_NAME(_summation64_, single);
                }
            }
            return UCS_OK;

        default:
            ucs_error("unsupported unsigned integer datatype length: %lu",
                      dt->dt_size);
            break; /* fall-back to the MPI reduction callback */
        }
    }

    if (!dt->is_contig) {
        if (is_fragmented) {
            phase->tx.bcopy_fragmented.pack_full_cb = UCG_OVER_UCT_PACKER_NAME(_datatype_, full);
            phase->tx.bcopy_fragmented.pack_part_cb = UCG_OVER_UCT_PACKER_NAME(_datatype_, part);
        } else {
            phase->tx.bcopy_single.pack_single_cb   = UCG_OVER_UCT_PACKER_NAME(_datatype_, single);
        }
    } else if (inplace_reduce) {
        if (is_fragmented) {
            phase->tx.bcopy_fragmented.pack_full_cb = UCG_OVER_UCT_PACKER_NAME(_external_, full);
            phase->tx.bcopy_fragmented.pack_part_cb = UCG_OVER_UCT_PACKER_NAME(_external_, part);
        } else {
            phase->tx.bcopy_single.pack_single_cb   = UCG_OVER_UCT_PACKER_NAME(_external_, single);
        }
    } else if (is_tl_slotted) {
        if (is_fragmented) {
            phase->tx.bcopy_fragmented.pack_full_cb = UCG_OVER_UCT_PACKER_NAME(_slotted_, full);
            phase->tx.bcopy_fragmented.pack_part_cb = UCG_OVER_UCT_PACKER_NAME(_slotted_, part);
        } else {
            phase->tx.bcopy_single.pack_single_cb   = UCG_OVER_UCT_PACKER_NAME(_slotted_, single);
        }
    } else if (is_bcast || is_single_dest) {
        if (is_fragmented) {
            phase->tx.bcopy_fragmented.pack_full_cb = UCG_OVER_UCT_PACKER_NAME(_, full);
            phase->tx.bcopy_fragmented.pack_part_cb = UCG_OVER_UCT_PACKER_NAME(_, part);
        } else {
            phase->tx.bcopy_single.pack_single_cb   = UCG_OVER_UCT_PACKER_NAME(_, single);
        }
    } else {
        if (is_fragmented) {
            phase->tx.bcopy_fragmented.pack_full_cb = UCG_OVER_UCT_PACKER_NAME(_scatter_, full);
            phase->tx.bcopy_fragmented.pack_part_cb = UCG_OVER_UCT_PACKER_NAME(_scatter_, part);
        } else {
            phase->tx.bcopy_single.pack_single_cb   = UCG_OVER_UCT_PACKER_NAME(_scatter_, single);
        }
    }

    if (!is_fragmented) {
        phase->tx.bcopy_single.rkey_memh = NULL;
    }

    return UCS_OK;
}

void ucg_over_uct_pack_check_internal_reduce(ucg_over_uct_plan_phase_t *phase,
                                             int is_fragmented)
{
    if (phase->rx.reduce_f == NULL) {
        return;
    }

    if (is_fragmented) {
        if (phase->tx.bcopy_fragmented.pack_full_cb == UCG_OVER_UCT_PACKER_NAME(_external_, full)) {
            phase->tx.bcopy_fragmented.pack_full_cb = UCG_OVER_UCT_PACKER_NAME(_internal_, full);
        }

        if (phase->tx.bcopy_fragmented.pack_part_cb == UCG_OVER_UCT_PACKER_NAME(_external_, part)) {
            phase->tx.bcopy_fragmented.pack_part_cb = UCG_OVER_UCT_PACKER_NAME(_internal_, part);
        }
    } else {
        if (phase->tx.bcopy_single.pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_external_, single)) {
            phase->tx.bcopy_single.pack_single_cb = UCG_OVER_UCT_PACKER_NAME(_internal_, single);
        }
    }
}

/*
 * This is a special function, tailored for ucg_over_uct_plan_phase_zcopy_rkey()
 * and replace a previously provided packer function (for the last part of a
 * message, a.k.a. pack_part_cb, or to a full message, a.k.a. pack_single_cb)
 * with a different, rkey-based one. This rkey-based callback would unpack the
 * remote key and use the source buffer (over shared memory) to aggregate, and
 * also assume only one such buffer exists (but may be larger than a single packet!).
 *
 * Per the second argument - if it's true, then the callback should reduce when
 * UCT_PACK_CALLBACK_REDUCE is OR-ed with the packer argument. If it's false,
 * and UCT_PACK_CALLBACK_REDUCE is OR-ed with the packer argument the only
 * difference is the lack of header added before the data.
 */
uct_pack_callback_t ucg_over_uct_pack_upgrade_to_use_rkey(uct_pack_callback_t pack_cb,
                                                          int packer_should_reduce)
{
    if (!packer_should_reduce) {
        return UCG_OVER_UCT_PACKER_NAME(_slotted_, single);
    }

    if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_internal_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_internal_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_external_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_external_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_slotted_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_slotted_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_summation8_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_summation8_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_summation16_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_summation16_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_summation32_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_summation32_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_summation64_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_summation64_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_atomic8_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_atomic8_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_atomic16_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_atomic16_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_atomic32_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_atomic32_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_atomic64_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_atomic64_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_internal_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_internal_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_external_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_external_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_slotted_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_slotted_, part);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_summation8_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_summation8_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_summation16_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_summation16_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_summation32_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_summation32_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_summation64_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_summation64_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_atomic8_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_atomic8_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_atomic16_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_atomic16_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_atomic32_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_atomic32_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_atomic64_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_atomic64_rkey_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_, part)) {
        return UCG_OVER_UCT_PACKER_NAME(_, single);
    } else if (pack_cb == UCG_OVER_UCT_PACKER_NAME(_, single)) {
        return UCG_OVER_UCT_PACKER_NAME(_, single);
    }

    return NULL;
}

void ucg_over_uct_print_pack_cb_name(uct_pack_callback_t pack_single_cb)
{
    if (pack_single_cb == NULL) {
        printf("NONE");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_atomic8_, single)) {
        printf("atomic (8 bytes)");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_summation8_, single)) {
        printf("summation (8 bytes)");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_atomic16_, single)) {
        printf("atomic (16 bytes)");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_summation16_, single)) {
        printf("summation (16 bytes)");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_atomic32_, single)) {
        printf("atomic (32 bytes)");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_summation32_, single)) {
        printf("summation (32 bytes)");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_atomic64_, single)) {
        printf("atomic (64 bytes)");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_summation64_, single)) {
        printf("summation (64 bytes)");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_internal_, single)) {
        printf("internal reduction callback");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_external_, single)) {
        printf("external reduction callback");
    } else if (pack_single_cb == UCG_OVER_UCT_PACKER_NAME(_, single)) {
        printf("memory copy");
    }
}
