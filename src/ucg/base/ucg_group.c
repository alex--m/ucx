/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_group.h"
#include "ucg_context.h"
#include <ucg/api/ucg_mpi.h>

#include <ucp/wireup/address.h> /* for ucp_address_pack/unpack */
#include <ucp/core/ucp_request.inl> /* for ucp_recv_desc_release */
#include <ucb/base/ucb_pipes.h>

#if ENABLE_DEBUG_DATA
#define UCG_GROUP_NAME  group->super.name
#else
#define UCG_GROUP_NAME  "<no debug data>"
#endif

/******************************************************************************
 *                                                                            *
 *                              Broadcast Wireup                              *
 *                                                                            *
 ******************************************************************************/

static ucs_status_t
ucg_group_wireup_coll_iface_create(ucg_group_h group,
                                   ucg_group_member_index_t my_rel_index,
                                   ucg_group_member_index_t subgroup_size,
                                   int is_incast, uct_incast_operand_t operand,
                                   uct_incast_operator_t operator,
                                   unsigned operand_count,
                                   int is_operand_cache_aligned, size_t msg_size,
                                   uct_reduction_external_cb_t ext_aux_cb,
                                   void *ext_operator, void *ext_datatype,
                                   unsigned *iface_id_base_p,
                                   ucp_tl_bitmap_t *coll_tl_bitmap_p)
{
    ucs_status_t status;
    uct_iface_params_t iface_params = {
        .field_mask = UCT_IFACE_PARAM_FIELD_GROUP_INFO,
        .group_info = {
            .proc_cnt = subgroup_size,
            .proc_idx = my_rel_index,
        }
    };
    ucs_assert(operand_count <= msg_size);

    if (ucg_group_params_want_timestamp(&group->params)) {
        iface_params.field_mask |= UCT_IFACE_PARAM_FIELD_FEATURES;
        iface_params.features    = UCT_IFACE_FEATURE_TX_TIMESTAMP;
    }

    if (is_incast || !operand_count) {
        iface_params.field_mask                        |= UCT_IFACE_PARAM_FIELD_COLL_INFO;
        iface_params.coll_info.operator_                = operator;
        iface_params.coll_info.operand                  = operand;
        iface_params.coll_info.operand_count            = operand_count;
        iface_params.coll_info.is_operand_cache_aligned = is_operand_cache_aligned;
        iface_params.coll_info.total_size               = msg_size;
        iface_params.coll_info.ext_cb                   = ucg_global_params.reduce_op.reduce_cb_f;
        iface_params.coll_info.ext_aux_cb               = ext_aux_cb;
        iface_params.coll_info.ext_operator             = ext_operator;
        iface_params.coll_info.ext_datatype             = ext_datatype;
    }

    UCS_ASYNC_BLOCK(&group->worker->async);
    status = ucp_worker_add_resource_ifaces(group->worker, &iface_params,
                                            iface_id_base_p, coll_tl_bitmap_p);
    UCS_ASYNC_UNBLOCK(&group->worker->async);

    return status;
}

struct ucg_group_wireup_pack_arg {
    /* This first part is copied into the wireup message verbatim */
    uint64_t                 uid;
    ucg_group_member_index_t coll_index;
    ucg_group_member_index_t coll_count;

    /* This second part is used to copy the address into the wireup message */
    void                     *address;
    size_t                   length;
};

static size_t ucg_group_wireup_send_addr_pack_cb(void *dest, void *arg)
{
    struct ucg_group_wireup_pack_arg *pack_arg = arg;
    size_t header_size = offsetof(struct ucg_group_wireup_pack_arg, address);

    memcpy(dest, pack_arg, header_size);
    memcpy(dest + header_size, pack_arg->address, pack_arg->length);

    return header_size + pack_arg->length;
}

static ucs_status_t
ucg_group_wireup_send_addr(ucg_group_h group, ucg_group_member_index_t dest,
                           ucg_group_member_index_t coll_index,
                           ucg_group_member_index_t coll_count, uint8_t am_id,
                           void *address, size_t addr_len, uint64_t wireup_uid)

{
    ssize_t ret;
    ucp_ep_h ucp_ep;
    uct_ep_h uct_ep;
    ucs_status_t status;
    const uct_iface_attr_t *iface_attr;
    struct ucg_group_wireup_pack_arg pack_arg = {
        .uid        = wireup_uid,
        .coll_index = coll_index,
        .coll_count = coll_count,
        .address    = address,
        .length     = addr_len
    };

    status = ucg_plan_connect_p2p_single(group, dest, &uct_ep, &ucp_ep, NULL,
                                         NULL, &iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    addr_len += offsetof(struct ucg_group_wireup_pack_arg, address);
    if (iface_attr->cap.am.max_bcopy < addr_len) {
        /* Address will typically use bcopy, so quit now if it can't take it */
        return UCS_ERR_EXCEEDS_LIMIT;
    }

    ret = uct_ep_am_bcopy(uct_ep, am_id, ucg_group_wireup_send_addr_pack_cb,
                          &pack_arg, 0);

    return (ret == addr_len) ? UCS_OK : ((ret >= 0) ? UCS_ERR_EXCEEDS_LIMIT :
                                                      (ucs_status_t)ret);
}

static inline ucs_status_t
ucg_group_wireup_address_lookup(uint64_t wireup_uid,
                                ucs_ptr_array_locked_t *messages,
                                ucg_group_member_index_t *coll_index,
                                ucg_group_member_index_t *coll_count,
                                void **address, ucp_recv_desc_t **rdesc)
{
    struct ucg_group_wireup_pack_arg *pack_arg;
    ucp_recv_desc_t *iterator;
    unsigned i, to_remove = 0;
    size_t header_size    = offsetof(struct ucg_group_wireup_pack_arg, address);

    ucs_assert(*address == NULL);
    ucs_ptr_array_locked_for_each(iterator, i, messages) {
        pack_arg = (struct ucg_group_wireup_pack_arg*)(iterator + 1);
        if (pack_arg->uid == wireup_uid) {
            ucs_assert(*address == NULL);
            *rdesc      = iterator;
            *address    = UCS_PTR_BYTE_OFFSET(pack_arg, header_size);
            *coll_index = pack_arg->coll_index;
            *coll_count = pack_arg->coll_count;
            to_remove   = i;
        }
    }

    if (*address != NULL) {
        ucs_ptr_array_locked_remove(messages, to_remove);
        return UCS_OK;
    }

    return UCS_ERR_NO_ELEM;
}

ucs_status_t ucg_group_wireup_coll_ifaces(ucg_group_h group, uint64_t wireup_uid,
                                          uint8_t am_id, int is_leader,
                                          int is_incast,
                                          const ucs_int_array_t *level_members,
                                          ucg_group_member_index_t coll_threshold,
                                          uct_incast_operator_t operator,
                                          uct_incast_operand_t operand,
                                          unsigned operand_count,
                                          int is_operand_cache_aligned,
                                          size_t msg_size,
                                          uct_reduction_external_cb_t ext_aux_cb,
                                          void *ext_operator, void *ext_datatype,
                                          unsigned *iface_id_base_p,
                                          ucp_tl_bitmap_t *coll_tl_bitmap_p,
                                          ucg_group_member_index_t *coll_index,
                                          ucg_group_member_index_t *coll_count,
                                          ucp_ep_h *ep_p)
{
    unsigned i;
    void *address;
    size_t addr_len;
    ucs_status_t status;
    ucp_object_version_t addr_v;
    ucp_unpacked_address_t unpacked;
    ucg_group_member_index_t dest_index;
    unsigned addr_indices[UCP_MAX_LANES];

    ucp_recv_desc_t *rdesc = NULL;
    ucp_worker_h worker    = group->worker;
    unsigned pack_flags    = UCP_ADDRESS_PACK_FLAG_DEVICE_ADDR |
                             UCP_ADDRESS_PACK_FLAG_IFACE_ADDR;

#if ENABLE_DEBUG_DATA
    pack_flags |= UCP_ADDRESS_PACK_FLAG_WORKER_NAME;
#endif

    /* Prepare for network-level address exchange */
    if (is_leader) {
        if (!*coll_count) {
            *coll_count = level_members ?
                          ucs_int_array_get_elem_count(level_members) :
                          group->params.member_count;
        }

        if (*coll_count >= coll_threshold) {
            /* Determine my own index */
            if (level_members) {
                ucs_int_array_for_each(dest_index, i, level_members) {
                    if (dest_index == group->params.member_index) {
                        *coll_index = i;
                        break;
                    }
                }
            } else {
                *coll_index = group->params.member_index;
            }

            /* Create collective interfaces for the new communicator */
            status = ucg_group_wireup_coll_iface_create(group, 0, *coll_count,
                                                        is_incast, operand,
                                                        operator, operand_count,
                                                        is_operand_cache_aligned,
                                                        msg_size, ext_aux_cb,
                                                        ext_operator,
                                                        ext_datatype,
                                                        iface_id_base_p,
                                                        coll_tl_bitmap_p);
            if (status != UCS_OK) {
                return status;
            }

            if (UCS_BITMAP_IS_ZERO_INPLACE(coll_tl_bitmap_p)) {
                ucs_debug("no collective transports available/selected");
                addr_len = *coll_count = 0;
            } else {
                /* Root prepares the address (the rest will be overwritten...) */
                addr_v = worker->context->config.ext.worker_addr_version;
                status = ucp_address_pack(worker, NULL, coll_tl_bitmap_p,
                                          *iface_id_base_p, pack_flags, addr_v,
                                          NULL, UINT_MAX, &addr_len, &address);
                if (status != UCS_OK) {
                    ucs_error("failed to pack the local UCP worker address");
                    return status;
                }
            }
        } else {
            ucs_info("Collective transport waived in group #%u: "
                     "requested size was %u while the threshold is %u",
                     group->params.id, *coll_count, coll_threshold);
            addr_len = *coll_count = 0;
        }

        /* Send the address to every destination on the list */
        if (level_members) {
            ucs_int_array_for_each(dest_index, i, level_members) {
                if (dest_index == group->params.member_index) {
                    continue; /* Skip myself */
                }

                status = ucg_group_wireup_send_addr(group, dest_index, i,
                                                    *coll_count, am_id, address,
                                                    addr_len, wireup_uid);
                if (status != UCS_OK) {
                    goto free_address;
                }
            }
        } else {
            for (dest_index = 0;
                 dest_index < group->params.member_count;
                 dest_index++) {
                if (dest_index == group->params.member_index) {
                    continue; /* Skip myself */
                }

                status = ucg_group_wireup_send_addr(group, dest_index, dest_index,
                                                    *coll_count, am_id, address,
                                                    addr_len, wireup_uid);
                if (status != UCS_OK) {
                    goto free_address;
                }
            }
        }
    } else {
        /* Obtain the remote collective wireup message */
        address = NULL;
        while (!address) {
            /* Check for group-bound addresses matching the current wireup UID */
            status = ucg_group_wireup_address_lookup(wireup_uid,
                                                     &group->addresses.matched,
                                                     coll_index, coll_count,
                                                     &address, &rdesc);
            if (status == UCS_OK) {
                break;
            }

            /* Check for unexpected addresses matching the current wireup UID */
            status = ucg_group_wireup_address_lookup(wireup_uid,
                                                     group->addresses.unmatched,
                                                     coll_index, coll_count,
                                                     &address, &rdesc);
            if (status == UCS_OK) {
                break;
            }

            ucp_worker_progress(worker);
        }

        if (*coll_count >= coll_threshold) {
            /* Create collective interfaces for the new communicator */
            status = ucg_group_wireup_coll_iface_create(group, *coll_index,
                                                        *coll_count, is_incast,
                                                        operand, operator,
                                                        operand_count,
                                                        is_operand_cache_aligned,
                                                        msg_size, ext_aux_cb,
                                                        ext_operator,
                                                        ext_datatype,
                                                        iface_id_base_p,
                                                        coll_tl_bitmap_p);
            if (status != UCS_OK) {
                goto free_address;
            }

            addr_len = 1; /* just a dummy non-zero value to indicate validity */
        } else {
            addr_len = 0; /* no need to unpack address - it's invalid */
        }
    }

    /* Below is a common part for both the collective root and other members */
    if (addr_len) {
        status = ucp_address_unpack(worker, address, pack_flags, &unpacked);
        if (status != UCS_OK) {
            goto free_address;
        }

        /* Connect to the address (for root - loopback) */
        UCS_ASYNC_BLOCK(&worker->async);
        status = ucp_ep_create_to_worker_addr(worker, coll_tl_bitmap_p,
                                              *iface_id_base_p, &unpacked, 0,
                                              "collective ep", addr_indices, ep_p);
        UCS_ASYNC_UNBLOCK(&worker->async);

        if (status == UCS_OK) {
            ucs_info("Collective transport connected on member %u/%u (group #%u)"
                     " as participant %u/%u (managed by %s)",
                     group->params.member_index, group->params.member_count,
                     group->params.id, *coll_index, *coll_count, unpacked.name);
        } else {
            ucs_info("Collective transport failed with member %u/%u (group #%u)",
                     group->params.member_index, group->params.member_count,
                     group->params.id);
        }

        ucs_free(unpacked.address_list);
    } else {
        status = UCS_ERR_NOT_CONNECTED;
    }

free_address:
    if (is_leader && addr_len) {
        ucs_free(address);
    }
    if (rdesc) {
        ucp_recv_desc_release(rdesc);
    }

    return status;
}

void ucg_group_cleanup_coll_ifaces(ucg_group_h group, unsigned iface_id_base,
                                   ucp_tl_bitmap_t coll_tl_bitmap)
{
    ucp_worker_del_resource_ifaces(group->worker, iface_id_base, coll_tl_bitmap);
    /* Note: deactivating progress is not enough - interfaces also take up mem. */
}

ucs_status_t ucg_group_am_msg_store(void *data, size_t length, unsigned am_flags,
                                    ucg_group_h group,
#if ENABLE_MT
                                    ucs_ptr_array_locked_t *msg_array)
{
    ucg_context_t *gctx;
#else
                                    ucs_ptr_array_t *msg_array)
{
#endif
    ucs_status_t status;
    ucp_worker_h worker;
    ucp_recv_desc_t *rdesc;

    if (ucs_likely(am_flags & UCT_CB_PARAM_FLAG_DESC)) {
        rdesc                      = ((ucp_recv_desc_t*)data) - 1;
        rdesc->length              = length;
        rdesc->payload_offset      = 0;
        rdesc->flags               = UCP_RECV_DESC_FLAG_UCT_DESC;
        rdesc->release_desc_offset = UCP_WORKER_HEADROOM_PRIV_SIZE;
        status                     = UCS_INPROGRESS;
    } else {
#if ENABLE_MT
        /* Protect the mpool - which is shared among all groups */
        gctx = ucs_derived_of(group->super.context, ucg_context_t);
        ucs_spin_lock(&gctx->lock);
#endif

        /* Store the incoming packet in the UCP Worker memory pool */
        worker = group->worker;
        status = ucp_recv_desc_init(worker, data, length, 0, 0, 0, 0, 0,
                                    worker->am.alignment, "ucg_am_message",
                                    &rdesc);

#if ENABLE_MT
        ucs_spin_unlock(&gctx->lock);
#endif
        if (ucs_unlikely(UCS_STATUS_IS_ERR(status))) {
            return status;
        }
    }

    /* Store the message pointer (the relevant step hasn't been reached) */
#if ENABLE_MT
    (void) ucs_ptr_array_locked_insert(msg_array, rdesc);
#else
    (void) ucs_ptr_array_insert(msg_array, rdesc);
#endif

    return status;
}

void ucg_group_am_msg_discard(ucp_recv_desc_t *rdesc, ucg_group_h group)
{
#if ENABLE_MT
    ucg_context_t *gctx;

    /* Protect the mpool - which is shared among all groups */
    gctx  = ucs_derived_of(group->super.context, ucg_context_t);
    ucs_spin_lock(&gctx->lock);
#endif

    /* Dispose of the rdesc according to its allocation before proceeding */
    ucp_recv_desc_release(rdesc);

#if ENABLE_MT
    ucs_spin_unlock(&gctx->lock);
#endif
}

ucs_status_t ucg_group_store_wireup_message(ucg_group_h group, void *data,
                                            size_t length, unsigned am_flags)
{
    return ucg_group_am_msg_store(data, length, am_flags, group,
#if ENABLE_MT
                                  &group->addresses.matched);
#else
                                  &group->addresses.matched.super);
#endif
}

/******************************************************************************
 *                                                                            *
 *                                Group Creation                              *
 *                                                                            *
 ******************************************************************************/

static void ucg_group_copy_params(ucg_group_params_t *dst,
                                  const ucg_group_params_t *src,
                                  void *distance, size_t distance_size)
{
    size_t group_params_size = sizeof(src->field_mask) +
                               ucs_offsetof(ucg_params_t, field_mask);

    if (src->field_mask != 0) {
        enum ucg_group_params_field msb_flag = UCS_BIT((sizeof(uint64_t) * 8)
                - 1 - ucs_count_leading_zero_bits(src->field_mask));
        ucs_assert((msb_flag & src->field_mask) == msb_flag);

        switch (msb_flag) {
        case UCG_GROUP_PARAM_FIELD_NAME:
        case UCG_GROUP_PARAM_FIELD_UCP_WORKER:
            group_params_size = ucs_offsetof(ucg_group_params_t, pipes);
            break;

        case UCG_GROUP_PARAM_FIELD_UCB_PIPES:
            group_params_size = ucs_offsetof(ucg_group_params_t, id);
            break;

        case UCG_GROUP_PARAM_FIELD_ID:
            group_params_size = ucs_offsetof(ucg_group_params_t, member_count);
            break;

        case UCG_GROUP_PARAM_FIELD_MEMBER_COUNT:
            group_params_size = ucs_offsetof(ucg_group_params_t, member_index);
            break;

        case UCG_GROUP_PARAM_FIELD_MEMBER_INDEX:
            group_params_size = ucs_offsetof(ucg_group_params_t, cb_context);
            break;

        case UCG_GROUP_PARAM_FIELD_CB_CONTEXT:
            group_params_size = ucs_offsetof(ucg_group_params_t, distance_type);
            break;

        case UCG_GROUP_PARAM_FIELD_DISTANCES:
            group_params_size = ucs_offsetof(ucg_group_params_t, flags);
            break;

        case UCG_GROUP_PARAM_FIELD_FLAGS:
            group_params_size = ucs_offsetof(ucg_group_params_t, wireup_pool);
            break;

        case UCG_GROUP_PARAM_FIELD_WIREUP_POOL:
            group_params_size = ucs_offsetof(ucg_group_params_t, cache_size);
            break;

        case UCG_GROUP_PARAM_FIELD_CACHE_SIZE:
            group_params_size = sizeof(ucg_group_params_t);
            break;
        }
    }

    memcpy(dst, src, group_params_size);

    if (dst->field_mask & UCG_GROUP_PARAM_FIELD_DISTANCES) {
        dst->distance_array = distance;
        if (src->distance_type != UCG_GROUP_DISTANCE_TYPE_FIXED) {
            memcpy(distance, src->distance_array, distance_size);
        }
    } else {
        /* If the user didn't specify the distances - treat as uniform */
        dst->field_mask    |= UCG_GROUP_PARAM_FIELD_DISTANCES;
        dst->distance_type  = UCG_GROUP_DISTANCE_TYPE_FIXED;
        dst->distance_value = UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
    }
}

static inline ucs_status_t ucg_group_calc_size(const ucg_group_params_t *params,
                                               size_t *dist_size_p,
                                               size_t *target_legroom_p)
{
    *dist_size_p = 0;
    if (params->field_mask & UCG_GROUP_PARAM_FIELD_DISTANCES) {
        switch (params->distance_type) {
        case UCG_GROUP_DISTANCE_TYPE_FIXED:
            break;

        case UCG_GROUP_DISTANCE_TYPE_ARRAY:
            *dist_size_p = params->member_count;
            break;

        case UCG_GROUP_DISTANCE_TYPE_TABLE:
            *dist_size_p = params->member_count * params->member_count;
            break;

        case UCG_GROUP_DISTANCE_TYPE_PLACEMENT:
            *dist_size_p = params->member_count * UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
            break;

        default:
            ucs_error("Invalid parameter for UCG group creation");
            return UCS_ERR_INVALID_PARAM;
        }
    }

    *dist_size_p     *= sizeof(enum ucg_group_member_distance);
    *target_legroom_p = *dist_size_p +
                        (sizeof(ucg_group_t) - sizeof(ucs_pmodule_target_t));

    return UCS_OK;
}

#define UCG_GROUP_OUTPUT_DISTANCE_ARRAY(_params) \
({ \
    int idx; \
    enum ucg_group_member_distance distance; \
    char *distance_output = alloca((_params)->member_count + 2); \
    ucs_assert((_params)->field_mask & UCG_GROUP_PARAM_FIELD_DISTANCES); \
    for (idx = 0; idx < (_params)->member_count; idx++) { \
        switch (params->distance_type) { \
        case UCG_GROUP_DISTANCE_TYPE_FIXED: \
            distance = (_params)->distance_value; \
            break; \
        \
        case UCG_GROUP_DISTANCE_TYPE_ARRAY: \
            distance = (_params)->distance_array[idx]; \
            break; \
        \
        case UCG_GROUP_DISTANCE_TYPE_TABLE: \
            distance = (_params)->distance_table[(_params)->member_index][idx]; \
            break; \
        \
        case UCG_GROUP_DISTANCE_TYPE_PLACEMENT: \
            return UCS_ERR_NOT_IMPLEMENTED; \
        \
        default: \
            return UCS_ERR_INVALID_PARAM; \
        } \
        \
        snprintf(distance_output + idx, 3, "%x", distance); \
    } \
    distance_output; \
})

static void
ucg_group_plan_evict_cb(ucs_pmodule_cached_t *evicted)
{
    ucs_pmodule_target_plan_t *plan = ucs_container_of(evicted,
                                                       ucs_pmodule_target_plan_t,
                                                       params);

    /* Avoid evicting barriers because it breaks OMPI's optimization */
     const ucg_collective_params_t* params = ucg_plan_get_params(plan);
    if (UCG_PARAM_MODIFIERS(params) & UCG_GROUP_COLLECTIVE_MODIFIER_PERSISTENT) {
        plan = (typeof(plan))ucs_list_prev(&evicted->list, ucs_pmodule_cached_t, list);
    } // TODO: make sure this hack is not disrupting the caching mechanism

    ucs_pmodule_framework_target_plan_discard(plan);
}

ucs_status_t ucg_group_create(const ucg_group_params_t *params,
                              ucg_group_h *group_p)
{
    size_t dist_size;
    ucs_status_t status;
    ucp_worker_h worker;
    ucb_context_t *ucb_ctx;
    ucg_context_t *ucg_ctx;
    struct ucg_group *group;
    ucg_group_params_t* group_params;

    ucs_pmodule_target_params_t target_params = {
        .field_mask     = UCS_PMODULE_TARGET_PARAM_FIELD_CACHE_MAX_HASH  |
                          UCS_PMODULE_TARGET_PARAM_FIELD_LEGROOM         |
                          UCS_PMODULE_TARGET_PARAM_FIELD_PER_FRAMEWORK,
        .cache_max_hash = UCG_GROUP_CACHE_MODIFIER_MASK,
        .per_framework  = params
    };

    uint64_t field_mask = params->field_mask;
    if (field_mask & UCG_GROUP_PARAM_FIELD_UCP_WORKER) {
        worker = params->worker;
    } else if (field_mask & UCG_GROUP_PARAM_FIELD_UCB_PIPES) {
        field_mask |= UCG_GROUP_PARAM_FIELD_UCB_PIPES;
        worker = params->pipes->worker;
    } else {
        ucs_error("A UCP worker or UCB pipes object is required for a group");
        return UCS_ERR_INVALID_PARAM;
    }

    if (ucs_get_shmmni() <= 1) { //4096) { // TODO: a better threshold...
        if (params->member_index == 0) {
            ucs_warn("Low limit on the number of shared memory segments : %lu",
                     ucs_get_shmmni());
        }
    }

    /* Set the parameters of the collective operations cache */
    if (field_mask & UCG_GROUP_PARAM_FIELD_CACHE_SIZE) {
        target_params.cache_max_elems.limit    = params->cache_size;
        target_params.cache_max_elems.evict_cb = ucg_group_plan_evict_cb;
        target_params.field_mask              |=
            UCS_PMODULE_TARGET_PARAM_FIELD_CACHE_MAX_ELEMS;
    }

    if (params->field_mask & UCG_GROUP_PARAM_FIELD_NAME) {
        target_params.name        = params->name;
        target_params.field_mask |= UCS_PMODULE_TARGET_PARAM_FIELD_NAME;
    }

    status = ucg_group_calc_size(params, &dist_size,
                                 &target_params.target_legroom);
    if (status != UCS_OK) {
        return status;
    }

    if ((ucg_group_params_want_timestamp(params)) &&
        (!ucg_global_params.set_imbalance_cb_f)) {
        ucs_error("Requested timestamps on a group but no callback was set");
        return UCS_ERR_INVALID_PARAM;
    }

    /* Allocate the group as a superset of a target */
    ucb_ctx = ucs_container_of(worker->context, ucb_context_t, ucp_ctx);
    ucg_ctx = ucs_container_of(ucb_ctx,         ucg_context_t, ucb_ctx);
    status  = ucs_pmodule_framework_target_create(&ucg_ctx->super, &target_params,
                                                  (ucs_pmodule_target_t**)&group);
    if (status != UCS_OK) {
        return status;
    }

    /* Fill in the group fields */
    group->worker  = worker;
    group->next_id = UCG_GROUP_FIRST_COLL_ID;
    group_params   = (ucg_group_params_t*)&group->params;

    ucg_group_copy_params((ucg_group_params_t*)&group->params,
                          params, group->distances, dist_size);

    if (params->field_mask & UCG_GROUP_PARAM_FIELD_ID) {
        ucg_ctx->last_group_id = ucs_max(ucg_ctx->last_group_id,
                                         group->params.id);
    } else {
        group_params->field_mask |= UCG_GROUP_PARAM_FIELD_ID;
        group_params->id          = ++ucg_ctx->last_group_id;
    }

    if (!(params->field_mask & UCG_GROUP_PARAM_FIELD_CB_CONTEXT)) {
        group_params->cb_context = group;
    }

    group->addresses.unmatched = (ucs_ptr_array_locked_t*)group->params.wireup_pool;
    ucs_ptr_array_locked_init(&group->addresses.matched, "ucg_group_wireup_addresses");
    kh_init_inplace(ucg_group_ep, &group->p2p_eps);

    ucs_info("Group #%u created on member %u/%u, distance array: %s",
             group_params->id, group_params->member_index,
             group_params->member_count,
             UCG_GROUP_OUTPUT_DISTANCE_ARRAY(group_params));

    *group_p = group;
    return UCS_OK;
}

const ucg_group_params_t* ucg_group_get_params(ucg_group_h group)
{
    return &group->params;
}

ucg_coll_id_t ucg_group_get_next_coll_id(ucg_group_h group)
{
    return group->next_id;
}

ucs_status_t ucg_group_query(ucg_group_h group,
                             ucg_group_attr_t *attr)
{
    if (attr->field_mask & UCG_GROUP_ATTR_FIELD_NAME) {
        ucs_strncpy_safe(attr->name, UCG_GROUP_NAME, UCG_GROUP_NAME_MAX);
    }

    if (attr->field_mask & UCG_GROUP_ATTR_FIELD_ID) {
        attr->id = group->params.id;
    }

    if (attr->field_mask & UCG_GROUP_ATTR_FIELD_MEMBER_COUNT) {
        attr->member_count = group->params.member_count;
    }

    if (attr->field_mask & UCG_GROUP_ATTR_FIELD_MEMBER_INDEX) {
        attr->member_index = group->params.member_index;
    }

    return UCS_OK;
}

void ucg_group_destroy(ucg_group_h group)
{
    ucp_ep_h ep;
    ucp_recv_desc_t *iterator;
    UCS_V_UNUSED ucg_group_member_index_t index;

    kh_foreach(&group->p2p_eps, index, ep, {
        ucp_ep_destroy(ep);
    })

    ucs_ptr_array_locked_for_each(iterator, index, &group->addresses.matched) {
        ucp_recv_desc_release(iterator);
    }
    ucs_ptr_array_locked_cleanup(&group->addresses.matched, 0 /* TODO: restore 1 */);

    kh_destroy_inplace(ucg_group_ep, &group->p2p_eps);
    ucs_pmodule_framework_target_destroy(group->super.context, &group->super);
}


/******************************************************************************
 *                                                                            *
 *                                 Group Usage                                *
 *                                                                            *
 ******************************************************************************/

ucs_status_t ucg_collective_is_supported(const ucg_collective_support_params_t *params)
{
    uint16_t modifiers = 0;

    switch (params->query) {
    case UCG_COLLECTIVE_SUPPORT_QUERY_BY_TYPE:
        modifiers = params->type.modifiers;
        break;

    case UCG_COLLECTIVE_SUPPORT_QUERY_BY_PARAMS:
        modifiers = UCG_PARAM_MODIFIERS(params->params);
        break;

    default:
        return UCS_ERR_INVALID_PARAM;
    }

    switch (modifiers) {
    case UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID | UCG_PRIMITIVE_BCAST:
    case UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID | UCG_PRIMITIVE_REDUCE:
    case UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID | UCG_PRIMITIVE_ALLREDUCE:
        return UCS_OK;

    default:
        break;
    }

    return UCS_ERR_NOT_IMPLEMENTED;
}

UCS_PROFILE_FUNC(ucs_status_t, ucg_collective_create,
        (group, params, coll), ucg_group_h group,
        const ucg_collective_params_t *params, ucg_coll_h *coll)
{
    unsigned hash = (UCG_PARAM_MODIFIERS(params) & UCG_GROUP_CACHE_MODIFIER_MASK);

    UCS_STATIC_ASSERT(offsetof(ucg_collective_params_t, name) ==
                      UCS_SYS_CACHE_LINE_SIZE);

    return ucs_pmodule_target_get_plan(&group->super, hash, params,
                                       offsetof(ucg_collective_params_t, name),
                                       (ucs_pmodule_target_plan_t**)coll);
}

UCS_PROFILE_FUNC(ucs_status_ptr_t, ucg_collective_start,
                 (coll, user_request, progress_f_p), ucg_coll_h coll,
                 void *user_request, ucg_collective_progress_t *progress_f_p)
{
    ucg_coll_id_t coll_id;
    ucs_pmodule_target_plan_t *plan = (ucs_pmodule_target_plan_t*)coll;
    ucg_group_t *group              = ucs_derived_of(plan->target, ucg_group_t);

#if ENABLE_MT
    coll_id = ucs_atomic_fadd8(&group->next_id, 1);
#else
    coll_id = group->next_id++;
#endif

    ucs_trace_req("ucg_collective_start: op=%p req=%p", coll, user_request);

    return ucs_pmodule_target_launch(plan, coll_id, user_request,
        (ucs_pmodule_target_action_progress_f*)progress_f_p);
}

void* ucg_collective_get_request(void *op)
{
    return ((ucs_pmodule_target_action_t*)op)->req;
}

ucs_status_t ucg_collective_check_status(void *op)
{
    return ((ucs_pmodule_target_action_t*)op)->status;
}

volatile ucs_status_t* ucg_collective_get_status_ptr(void *op)
{
    return &((ucs_pmodule_target_action_t*)op)->status;
}

ucg_collective_progress_t ucg_collective_get_progress(ucg_coll_h coll)
{
    return (ucg_collective_progress_t)
        ((ucs_pmodule_target_action_t*)coll)->plan->progress_f;
}

ucs_status_t ucg_collective_cancel(ucg_coll_h coll, void *req)
{
    ucs_error("ucg_collective_cancel is not implemented yet");
    return UCS_ERR_NOT_IMPLEMENTED; // TODO: implement...
}

void ucg_collective_destroy(ucg_coll_h coll)
{
    ucs_pmodule_target_plan_t *plan = (ucs_pmodule_target_plan_t*)coll;

    ucs_trace_req("ucg_collective_destroy: op=%p req=%p", plan,
                  ucg_collective_get_request(plan));

    ucs_pmodule_framework_target_plan_discard(plan);
}
