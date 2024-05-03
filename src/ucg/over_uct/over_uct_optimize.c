/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_uct_comp.inl"

#include <stddef.h>
#include <ucs/sys/compiler_def.h>

static int
ucg_over_uct_optimize_phase_can_register_memory(ucg_over_uct_plan_phase_t *phase,
                                                int test_tx)
{
    enum ucg_over_uct_phase_extra_info_slots slot_dir = test_tx ?
        UCG_OVER_UCT_PHASE_EXTRA_INFO_TX : UCG_OVER_UCT_PHASE_EXTRA_INFO_RX;
    return ucg_plan_md_can_register_memory(phase->info[slot_dir].md);
}

static UCS_F_ALWAYS_INLINE size_t
ucg_over_uct_plan_phase_tx_length(ucg_over_uct_plan_phase_t *phase,
                                  const ucg_collective_params_t *params)
{
    size_t base_length = phase->tx.length;
    int is_len_packed  = ucg_over_uct_plan_phase_flags_get_length_info(phase->flags) !=
                         UCT_COLL_LENGTH_INFO_DEFAULT;

    return is_len_packed ? UCT_COLL_LENGTH_INFO_UNPACK_VALUE(base_length) :
                           base_length;
}

static inline int
ucg_over_uct_optimize_is_collaborative(ucg_over_uct_plan_t *plan,
                                         ucg_over_uct_plan_phase_t *phase)
{
    /* Query the TX interface to check if it's collaborative */
    uct_iface_attr_t iface_attr;
    ucs_status_t status = uct_iface_query(phase->tx.iface, &iface_attr);

    /* Determine if this a collaborative transport */
    return ((status == UCS_OK) &&
            (iface_attr.cap.flags & UCT_IFACE_FLAG_INCAST) &&
            (iface_attr.cap.flags & UCT_IFACE_FLAG_INCAST_SLOTTED) &&
            (iface_attr.cap.flags & UCT_IFACE_FLAG_INCAST_UNORDERED));
}

static inline ucs_status_t
ucg_over_uct_optimize_phase_zcopy_rkey(ucg_over_uct_plan_t *plan,
                                       ucg_over_uct_plan_phase_t *phase,
                                       int is_tx_a_remote_key,
                                       int tx_packer_should_reduce,
                                       const ucg_collective_params_t *params)
{
    uct_mem_h memh;
    size_t send_size;
    ucs_status_t status;
    uint8_t *info_buffer;
    uct_md_attr_t md_attr;
    int is_collaborative;
    uct_pack_callback_t pack_cb;
    enum uct_msg_flags bcopy_flag;
    ucg_over_uct_rkey_msg_t *rkey_msg;
    size_t total_size                     = 0;
    uint16_t mods                         = UCG_PARAM_MODIFIERS(params);
    int is_same_tx_buf                    = mods & UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST;
    uint8_t i, rkey_cnt                   = is_same_tx_buf ? 1 : phase->tx.ep_cnt;
    uint8_t *buf_iter                     = phase->tx.buffer;
    size_t buf_len                        = ucg_over_uct_plan_phase_tx_length(phase, params);
    ucg_over_uct_phase_extra_info_t *info = &phase->info[UCG_OVER_UCT_PHASE_EXTRA_INFO_TX];
    uct_md_h md                           = info->md;

    if (is_tx_a_remote_key) {
        /* Test the memory domain for the size of the (packed) remote key */
        status = uct_md_query(md, &md_attr);
        if (ucs_unlikely(status != UCS_OK)) {
            goto cleanup_buf_reg;
        }

        /* Allocate and register the key buffer, to contain the keys themselves */
        send_size  = sizeof(*rkey_msg) + md_attr.rkey_packed_size;
        total_size = send_size * rkey_cnt;

        /*
         * Special case: collaborative.
         * In this case - the non-root ranks do the heavy-lifting (all the peer
         * reductions) so it requires a temporary buffer which is both
         * (a) registered and (b) the source buffer is copied to it before the
         * action starts. This temporary buffer will hold the intermediate
         * result and a remote key to this buffer will be delivered to the
         * root node.
         */
        is_collaborative = ucg_over_uct_optimize_is_collaborative(plan, phase);
        if (is_collaborative) {
            /* Allocate more space */
            total_size += buf_len;
        }

        /* Allocate the temporary buffer */
        info_buffer = UCS_ALLOC_CHECK(total_size, "over_uct_rkey_buffer");
        ucg_over_uct_phase_set_temp_buffer(phase, info_buffer,
                                           UCG_OVER_UCT_PHASE_EXTRA_INFO_TX);

        /* Second part of handling the special case of collaborative rank #1 */
        if (is_collaborative) {
            /* Set the registered buffer to be this temporary one we allocated */
            buf_iter     = info_buffer;

            /* Put all the keys in the second part of the buffer */
            info_buffer += buf_len;
            total_size  -= buf_len;

            /* Plan now includes copying the original buffer into the temporary */
            plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_COPY_SRC_BUFFER;
            plan->copy_dst  = buf_iter;

            /* Make progress recognize the auxiliary mode */
            phase->tx.iface = (uct_iface_h)((uintptr_t)phase->tx.iface |
                                            UCT_PACK_CALLBACK_REDUCE);
        }
    }

    /* Register the buffer, creating a memory handle used in zero-copy sends */
    status = uct_md_mem_reg(md, buf_iter, buf_len, UCT_MD_MEM_ACCESS_ALL, &memh);
    if (status != UCS_OK) {
        ucs_error("Failed to upgrade to zero-copy - cannot register memory with %s",
                  md->component->name);
        return status;
    }

    if (is_tx_a_remote_key) {
        /* Proceed with preparing the transmission of the remote key */
        rkey_msg         = (ucg_over_uct_rkey_msg_t*)info_buffer;
        info->aux_memh   = memh;
        phase->tx.buffer = info_buffer;
        phase->tx.length = total_size;
        bcopy_flag       = UCT_SEND_FLAG_CB_AUX_REDUCE;
        phase->flags    |= UCT_COLL_LENGTH_INFO_PACK(0, bcopy_flag) <<
                           UCG_OVER_UCT_PLAN_PHASE_LENGTH_INFO_SHIFT;
        bcopy_flag       = UCT_SEND_FLAG_CB_REDUCES;
        phase->flags    &= ~(UCT_COLL_LENGTH_INFO_PACK(0, bcopy_flag) <<
                             UCG_OVER_UCT_PLAN_PHASE_LENGTH_INFO_SHIFT);

        /* Replace the packing callback functions (assume it fits one buffer)*/
        if (phase->flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_FRAGMENTED) {
            phase->flags      &= ~UCG_OVER_UCT_PLAN_PHASE_FLAG_FRAGMENTED;
            phase->tx.frag_len = 0;

            /* Hack: use the bcopy callback from the remainder to pack this */
            pack_cb = phase->tx.bcopy_fragmented.pack_part_cb;
            pack_cb = ucg_over_uct_pack_upgrade_to_use_rkey(pack_cb, tx_packer_should_reduce);
        } else {
            pack_cb = phase->tx.bcopy_single.pack_single_cb;
            pack_cb = ucg_over_uct_pack_upgrade_to_use_rkey(pack_cb, tx_packer_should_reduce);
        }
        ucs_assert(pack_cb != NULL);
        if (!pack_cb) {
            goto cleanup_rkey_alloc;
        }
        phase->tx.bcopy_single.pack_single_cb = pack_cb;

        /*
         * Currently, only the simple case is supported, where the size of the
         * rkey(s) is small enough to fit in a single buffer (packet).
         */
        ucs_assert(total_size <= info->max_bcopy);

        for (i = 0; i < rkey_cnt; i++) {
#if ENABLE_ASSERT
            rkey_msg->magic          = RKEY_MAGIC;
#endif
            rkey_msg->remote_length  = buf_len;
            rkey_msg->remote_address = (uintptr_t)buf_iter;
            status = uct_md_mkey_pack(md, memh, rkey_msg->packed_rkey);
            if (status != UCS_OK) {
                goto cleanup_rkey_alloc;
            }

            rkey_msg = UCS_PTR_BYTE_OFFSET(rkey_msg, send_size);
            buf_iter = UCS_PTR_BYTE_OFFSET(buf_iter, send_size);
        }

        /* Register the buffer which will be sent */
        status = uct_md_mem_reg(md, info_buffer, total_size,
                                UCT_MD_MEM_ACCESS_ALL,
                                &phase->tx.bcopy_single.rkey_memh);
        if (status != UCS_OK) {
            goto cleanup_rkey_alloc;
        }
    } else {
        phase->tx.zcopy.memh = memh;
    }

    ucs_debug("collective operation optimized");
    return UCS_OK;

cleanup_rkey_alloc:
    ucs_free(info_buffer);
cleanup_buf_reg:
    uct_md_mem_dereg(md, memh);
    return status;
}

static inline size_t
ucg_over_uct_optimize_rkey_threshold(ucg_over_ucp_group_ctx_t *gctx)
{
    return ((ucg_over_uct_ctx_t*)gctx->ctx)->config.zcopy_rkey_thresh;
}

/**
 * This function was called because we want to "upgrade" a bcopy-send to
 * zcopy, by way of memory registration (costly, but hopefully worth it)
 */
static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_common(ucg_over_uct_plan_t *plan,
                                               ucg_over_uct_plan_phase_t *phase,
                                               const ucg_collective_params_t *params,
                                               int rx_is_bcopy, int rx_has_zcopy,
                                               int tx_is_bcopy, int tx_has_zcopy,
                                               int tx_packer_should_reduce)
{
    ucs_status_t status;
    size_t rkey_threshold;
    int is_rx_a_remote_key;
    int is_tx_a_remote_key;
    int remote_key_condition;
    ucg_over_ucp_group_ctx_t *gctx;
    int is_allreduce = (UCG_PARAM_MODIFIERS(params) ==
                        (UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID |
                         UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE  |
                         UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST));

    /*
     * As an additional TX optimization, a remote key (and address) for
     * accessing the TX buffer can be trasmitted instead of the buffer
     * itself. This kind of optimization makes more sense for collective
     * operations where there's an expected response after the buffer has
     * been sent (In MPI that's only the Allreduce collective), because only
     * there we can be sure that such response indicates that TX buffer has
     * been consumed and can be reused (in collectives like ReduceScatter or
     * Alltoall that's not guaranteed, because one can receive a portion of
     * the result while another rank can still be reading or waiting on my
     * TX buffer. Even for Allreduce, this optimization can only be applied
     * to the first transmission, of non-root (incl. multi- root) ranks, to
     * ensure that the response arrives only after this shared memory has
     * been consumed. This is detected by checking that the RX step right
     * after this has writes and not a reduction (indicates multi-root).
     */
    gctx                 = plan->super.gctx;
    rkey_threshold       = ucg_over_uct_optimize_rkey_threshold(gctx);
    remote_key_condition = is_allreduce && (phase == &plan->phss[0]);
    is_rx_a_remote_key   = rx_is_bcopy && remote_key_condition && !rx_has_zcopy &&
                           (phase->rx.batch_len > rkey_threshold);
    is_tx_a_remote_key   = tx_is_bcopy && remote_key_condition && !tx_has_zcopy &&
                           /* Ensure there will be a response after this TX */
                           (plan->super.phs_cnt > 1) &&
                           (phase->tx.length > rkey_threshold) &&
                           ((phase + 1)->flags &
                            UCG_OVER_UCT_PLAN_PHASE_FLAG_EXPECTS_RX) &&
                           (((phase + 1)->rx.comp_agg ==
                             UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_NO_OFFSET) ||
                            ((phase + 1)->rx.comp_agg ==
                             UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_WITH_OFFSET));

    if (tx_is_bcopy && (tx_has_zcopy || is_tx_a_remote_key) &&
        ucg_over_uct_optimize_phase_can_register_memory(phase, 1)) {
        /* Send zero copy */
        status = ucg_over_uct_optimize_phase_zcopy_rkey(plan, phase,
                                                        is_tx_a_remote_key,
                                                        tx_packer_should_reduce,
                                                        params);
        if (status != UCS_OK) {
            return status;
        }

        if (tx_has_zcopy) {
            ucs_assert(!is_tx_a_remote_key);
            phase->flags  &= ~(UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_BCOPY <<
                               UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS);
            phase->flags  |=  (UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY <<
                               UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS);
            phase->tx.send = phase->tx.iface->ops.ep_am_zcopy;

            if (phase->rx.comp_criteria ==
                    UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES) {
                phase->rx.comp_criteria =
                    UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES_ZCOPY;
            }
        }
    }

    if (rx_is_bcopy && is_rx_a_remote_key &&
        ucg_over_uct_optimize_phase_can_register_memory(phase, 0)) {
        ucs_assert(!rx_has_zcopy);
        phase->rx.comp_agg |= UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY;

        if (phase->rx.comp_flags &   UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_FRAGMENTED_DATA) {
            phase->rx.comp_flags &= ~UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_FRAGMENTED_DATA;
            ucs_assert((phase->rx.frags_cnt % plan->max_frag) == 0);
            phase->rx.frags_cnt /= plan->max_frag;
        }
    }

    return UCS_OK;
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz_txz_reduces(ucg_over_uct_plan_t *plan,
                                                        ucg_over_uct_plan_phase_t *phase,
                                                        const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 1, 1, 1, 1);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz_txz_copies(ucg_over_uct_plan_t *plan,
                                                       ucg_over_uct_plan_phase_t *phase,
                                                       const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 1, 1, 1, 0);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz_txb_reduces(ucg_over_uct_plan_t *plan,
                                                        ucg_over_uct_plan_phase_t *phase,
                                                        const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 1, 1, 0, 1);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz_txb_copies(ucg_over_uct_plan_t *plan,
                                                       ucg_over_uct_plan_phase_t *phase,
                                                       const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 1, 1, 0, 0);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz(ucg_over_uct_plan_t *plan,
                                            ucg_over_uct_plan_phase_t *phase,
                                            const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 1, 0, 0, 0);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb_txz_reduces(ucg_over_uct_plan_t *plan,
                                                        ucg_over_uct_plan_phase_t *phase,
                                                        const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 0, 1, 1, 1);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb_txz_copies(ucg_over_uct_plan_t *plan,
                                                       ucg_over_uct_plan_phase_t *phase,
                                                       const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 0, 1, 1, 0);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb_txb_reduces(ucg_over_uct_plan_t *plan,
                                                        ucg_over_uct_plan_phase_t *phase,
                                                        const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 0, 1, 0, 1);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb_txb_copies(ucg_over_uct_plan_t *plan,
                                                       ucg_over_uct_plan_phase_t *phase,
                                                       const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 0, 1, 0, 0);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb(ucg_over_uct_plan_t *plan,
                                            ucg_over_uct_plan_phase_t *phase,
                                            const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 1, 0, 0, 0, 0);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_txz_reduces(ucg_over_uct_plan_t *plan,
                                                    ucg_over_uct_plan_phase_t *phase,
                                                    const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 0, 0, 1, 1, 1);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_txz_copies(ucg_over_uct_plan_t *plan,
                                                   ucg_over_uct_plan_phase_t *phase,
                                                   const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 0, 0, 1, 1, 0);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_txb_reduces(ucg_over_uct_plan_t *plan,
                                                    ucg_over_uct_plan_phase_t *phase,
                                                    const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 0, 0, 1, 0, 1);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_txb_copies(ucg_over_uct_plan_t *plan,
                                                   ucg_over_uct_plan_phase_t *phase,
                                                   const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 0, 0, 1, 0, 0);
}

static ucs_status_t
ucg_over_uct_optimize_am_bcopy_to_zcopy_none(ucg_over_uct_plan_t *plan,
                                             ucg_over_uct_plan_phase_t *phase,
                                             const ucg_collective_params_t *params)
{
    return ucg_over_uct_optimize_am_bcopy_to_zcopy_common(plan, phase, params, 0, 0, 0, 0, 0);
}

static ucg_over_uct_plan_optm_cb_t
ucg_over_uct_optimize_choose_cb(int rx_is_bcopy, int rx_has_zcopy,
                                int tx_is_bcopy, int tx_has_zcopy,
                                int tx_slotted)
{
    if (rx_is_bcopy) {
        if (rx_has_zcopy) {
            if (tx_is_bcopy) {
                if (tx_has_zcopy) {
                    if (!tx_slotted) {
                        return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz_txz_reduces;
                    } else {
                        return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz_txz_copies;
                    }
                } else {
                    if (!tx_slotted) {
                        return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz_txb_reduces;
                    } else {
                        return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz_txb_copies;
                    }
                }
            } else {
                return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxz;
            }
        } else {
            if (tx_is_bcopy) {
                if (tx_has_zcopy) {
                    if (!tx_slotted) {
                        return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb_txz_reduces;
                    } else {
                        return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb_txz_copies;
                    }
                } else {
                    if (!tx_slotted) {
                        return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb_txb_reduces;
                    } else {
                        return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb_txb_copies;
                    }
                }
            } else {
                return ucg_over_uct_optimize_am_bcopy_to_zcopy_rxb;
            }
        }
    } else {
        if (tx_is_bcopy) {
            if (tx_has_zcopy) {
                if (!tx_slotted) {
                    return ucg_over_uct_optimize_am_bcopy_to_zcopy_txz_reduces;
                } else {
                    return ucg_over_uct_optimize_am_bcopy_to_zcopy_txz_copies;
                }
            } else {
                if (!tx_slotted) {
                    return ucg_over_uct_optimize_am_bcopy_to_zcopy_txb_reduces;
                } else {
                    return ucg_over_uct_optimize_am_bcopy_to_zcopy_txb_copies;
                }
            }
        } else {
            return ucg_over_uct_optimize_am_bcopy_to_zcopy_none;
        }
    }
}

ucs_status_t
ucg_over_uct_optimize_plan(ucg_over_uct_plan_phase_t *phase,
                           const ucg_over_uct_config_t *config,
                           const ucg_collective_params_t *params,
                           uint16_t rx_send_flags,
                           const uct_iface_attr_t *rx_iface_attr,
                           const uct_iface_attr_t *tx_iface_attr,
                           uint16_t *opt_cnt_p)
{
    int rx_is_bcopy  = ucg_over_uct_plan_phase_flags_get_method(rx_send_flags) &
                       UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_BCOPY;
    int tx_is_bcopy  = ucg_over_uct_plan_phase_flags_get_method(phase->flags) &
                       UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_BCOPY;
    int rx_has_zcopy = rx_iface_attr &&
                       (rx_iface_attr->cap.flags & UCT_IFACE_FLAG_AM_ZCOPY);
    int tx_has_zcopy = tx_iface_attr &&
                       (tx_iface_attr->cap.flags & UCT_IFACE_FLAG_AM_ZCOPY);
    int tx_slotted   = tx_iface_attr &&
                       (tx_iface_attr->cap.flags & UCT_IFACE_FLAG_INCAST_SLOTTED) &&
                      !(tx_iface_attr->cap.flags & UCT_IFACE_FLAG_INCAST_UNORDERED);

    /**
     * While some buffers are large enough to be registered (as in memory
     * registration) upon first send, others are "buffer-copied" (BCOPY) -
     * unless it is used repeatedly. If an operation is used this many times -
     * its buffers will also be registered, turning it into a zero-copy (ZCOPY)
     * send henceforth.
     */
    if (config->bcopy_to_zcopy_opt && (rx_is_bcopy || tx_is_bcopy)) {
        phase->info[UCG_OVER_UCT_PHASE_EXTRA_INFO_RX].max_bcopy = rx_iface_attr ?
            rx_iface_attr->cap.am.max_bcopy : 0;
        phase->info[UCG_OVER_UCT_PHASE_EXTRA_INFO_TX].max_bcopy = tx_iface_attr ?
            tx_iface_attr->cap.am.max_bcopy : 0;

        *opt_cnt_p     = ucs_min(config->mem_reg_opt_cnt, *opt_cnt_p);
        phase->optm_cb = ucg_over_uct_optimize_choose_cb(rx_is_bcopy, rx_has_zcopy,
                                                         tx_is_bcopy, tx_has_zcopy,
                                                         tx_slotted);
        ucs_assert(phase->optm_cb != NULL);
        return UCS_OK;
    }

    phase->optm_cb = (ucg_over_uct_plan_optm_cb_t)ucs_empty_function_return_success;
    return UCS_OK;
}

ucs_status_t
ucg_over_uct_optimize_now(ucg_over_uct_plan_t *plan,
                          const ucg_collective_params_t *params)
{
    ucg_step_idx_t idx;
    ucs_status_t status;
    ucg_over_uct_plan_phase_t *phase = &plan->phss[0];

    for (idx = 0; idx < plan->super.phs_cnt; idx++, phase++) {
        status = phase->optm_cb(plan, phase, params);
        if (status != UCS_OK) {
            return status;
        }

#if ENABLE_ASSERT
        phase->optm_cb = (ucg_over_uct_plan_optm_cb_t)ucs_empty_function_do_assert;
#endif
    }

    /* Prevent further optimization calls for this plan */
    plan->op_flags &= ~UCG_OVER_UCT_PLAN_FLAG_OPTIMIZE_CB;
    return UCS_OK;
}
