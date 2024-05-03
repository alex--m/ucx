/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucs/sys/string.h>
#include <uct/sm/base/sm_ep.h>
#include <uct/sm/base/sm_iface.h>
#include <uct/sm/mm/base/mm_ep.h>
#include <ucs/debug/memtrack_int.h>

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

ucs_config_field_t uct_mm_coll_iface_config_table[] = {
    {"MM_", "", NULL,
     ucs_offsetof(uct_mm_coll_iface_config_t, super),
     UCS_CONFIG_TYPE_TABLE(uct_mm_iface_config_table)},

    {"ERROR_HANDLING", "n", "Expose error handling support capability",
     ucs_offsetof(uct_mm_iface_config_t, error_handling), UCS_CONFIG_TYPE_BOOL},

    {NULL}
};

ucs_status_t uct_mm_coll_iface_query_empty(uct_iface_h tl_iface,
                                           uct_iface_attr_t *iface_attr)
{
    uct_mm_base_iface_t *iface = ucs_derived_of(tl_iface, uct_mm_base_iface_t);
    uct_mm_md_t         *md    = ucs_derived_of(iface->super.super.md, uct_mm_md_t);

    /* In cases where a collective transport is not possible - avoid usage */
    uct_mm_iface_query(tl_iface, iface_attr);

    iface_attr->cap.event_flags = 0;
    iface_attr->cap.flags       = UCT_IFACE_FLAG_CONNECT_TO_IFACE;
    iface_attr->iface_addr_len  = sizeof(uct_mm_coll_iface_addr_t) +
                                  md->iface_addr_len;

    return UCS_OK;
}

ucs_status_t uct_mm_coll_iface_query(uct_iface_h tl_iface,
                                     uct_iface_attr_t *iface_attr)
{
    uct_mm_base_iface_t *iface = ucs_derived_of(tl_iface, uct_mm_base_iface_t);
    uct_mm_md_t         *md    = ucs_derived_of(iface->super.super.md, uct_mm_md_t);

    ucs_status_t status = uct_mm_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->cap.atomic32.op_flags     =
    iface_attr->cap.atomic32.fop_flags    =
    iface_attr->cap.atomic64.op_flags     =
    iface_attr->cap.atomic64.fop_flags    = 0; /* TODO: use in MPI_Accumulate */
    iface_attr->cap.event_flags           = UCT_IFACE_FLAG_EVENT_SEND_COMP;
    iface_attr->iface_addr_len            = sizeof(uct_mm_coll_iface_addr_t) +
                                            md->iface_addr_len;
    iface_attr->cap.flags                 = UCT_IFACE_FLAG_AM_SHORT          |
                                            UCT_IFACE_FLAG_AM_BCOPY          |
                                            UCT_IFACE_FLAG_PENDING           |
                                            UCT_IFACE_FLAG_CB_SYNC           |
                                            UCT_IFACE_FLAG_CONNECT_TO_IFACE;
    iface_attr->cap.am.coll_mode_flags    = UCS_BIT(UCT_COLL_LENGTH_INFO_DEFAULT) |
                                            UCS_BIT(UCT_COLL_LENGTH_INFO_PACKED);
    iface_attr->cap.coll_mode.short_flags = UCS_BIT(UCT_COLL_LENGTH_INFO_DEFAULT) |
                                            UCS_BIT(UCT_COLL_LENGTH_INFO_PACKED);
    iface_attr->cap.coll_mode.bcopy_flags = UCS_BIT(UCT_COLL_LENGTH_INFO_DEFAULT) |
                                            UCS_BIT(UCT_COLL_LENGTH_INFO_PACKED);
    iface_attr->cap.coll_mode.zcopy_flags = 0; /* TODO: implement... */
    iface_attr->cap.coll_mode.operators   = UCS_BIT(UCT_COLL_OPERATOR_EXTERNAL) |
                                            UCS_BIT(UCT_COLL_OPERATOR_MIN) |
                                            UCS_BIT(UCT_COLL_OPERATOR_MAX) |
                                            UCS_BIT(UCT_COLL_OPERATOR_SUM);
    iface_attr->cap.coll_mode.operands    = UCS_BIT(UCT_COLL_OPERAND_FLOAT) |
                                            UCS_BIT(UCT_COLL_OPERAND_DOUBLE) |
                                            UCS_BIT(UCT_COLL_OPERAND_INT8_T) |
                                            UCS_BIT(UCT_COLL_OPERAND_INT16_T) |
                                            UCS_BIT(UCT_COLL_OPERAND_INT32_T) |
                                            UCS_BIT(UCT_COLL_OPERAND_INT64_T) |
                                            UCS_BIT(UCT_COLL_OPERAND_UINT8_T) |
                                            UCS_BIT(UCT_COLL_OPERAND_UINT16_T) |
                                            UCS_BIT(UCT_COLL_OPERAND_UINT32_T) |
                                            UCS_BIT(UCT_COLL_OPERAND_UINT64_T);

    return UCS_OK;
}

ucs_status_t uct_mm_coll_iface_get_address(uct_iface_t *tl_iface,
                                           uct_iface_addr_t *addr)
{
    uct_mm_coll_iface_addr_t *iface_addr = (void*)addr;
    uct_mm_coll_iface_t *coll_iface      = ucs_derived_of(tl_iface,
                                                          uct_mm_coll_iface_t);

    if (!coll_iface->super.super.super.worker) {
        uct_mm_md_t *md = ucs_derived_of(coll_iface->super.super.super.md,
                                         uct_mm_md_t);

        memset(addr, 0, sizeof(uct_mm_coll_iface_addr_t) + md->iface_addr_len);
        iface_addr->coll_id = (uint32_t)-1;

        return UCS_OK;
    }

    iface_addr->coll_id = coll_iface->my_coll_id;

    return uct_mm_iface_get_address(tl_iface,
                                    (uct_iface_addr_t*)&iface_addr->super);
}

int
uct_mm_coll_iface_is_reachable_v2(const uct_iface_h tl_iface,
                                  const uct_iface_is_reachable_params_t *params)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);

    if (!iface->super.super.super.worker) {
        return 0;
    }

    return uct_mm_iface_is_reachable_v2(tl_iface, params);
}

UCS_CLASS_INIT_FUNC(uct_mm_coll_iface_t, uct_iface_ops_t *ops,
                    uct_iface_internal_ops_t *internal_ops, uct_md_h md,
                    uct_worker_h worker, const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config, int is_incast,
                    enum uct_mm_coll_type type, uint16_t elem_slot_size,
                    uint32_t seg_slot_size)
{
    /* check the value defining the size of the FIFO element */
    uct_mm_iface_config_t *mm_config = ucs_derived_of(tl_config,
                                                      uct_mm_iface_config_t);
    if (mm_config->fifo_elem_size < sizeof(uct_mm_coll_fifo_element_t)) {
        ucs_error("The UCX_MM_FIFO_ELEM_SIZE parameter (%u) must be larger "
                  "than, or equal to, the FIFO element header size (%ld bytes).",
                  mm_config->fifo_elem_size, sizeof(uct_mm_coll_fifo_element_t));
        return UCS_ERR_INVALID_PARAM;
    }

    if (params->field_mask & UCT_IFACE_PARAM_FIELD_GROUP_INFO) {
        self->my_coll_id  = params->group_info.proc_idx;
        self->sm_proc_cnt = params->group_info.proc_cnt;
    } else {
        self->my_coll_id  = 0;
        self->sm_proc_cnt = 1;
        worker            = NULL;
    }

    self->is_incast      = is_incast;
    self->type           = type;
    self->loopback_ep    = NULL;
    self->elem_slot_size = elem_slot_size;
    self->seg_slot_size  = seg_slot_size;
    self->use_timestamps = (params->field_mask & UCT_IFACE_PARAM_FIELD_FEATURES) &&
                           (params->features & UCT_IFACE_FEATURE_TX_TIMESTAMP);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_base_iface_t, ops, internal_ops, md, worker,
                              params, tl_config);

    if (worker && (mm_coll_iface_elems_prepare(self, type, 0) != UCS_OK)) {
        return UCS_ERR_INVALID_PARAM;
    }

    ucs_ptr_array_init(&self->ep_ptrs, "mm_coll_eps");

    return UCS_OK;
}

UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_iface_t)
{
    unsigned idx;
    uct_ep_h ep;

    if (self->sm_proc_cnt > 1) {
        ucs_assert_always(mm_coll_iface_elems_prepare(self, self->type, 1) ==
                          UCS_OK);
    }

    while (!ucs_ptr_array_is_empty(&self->ep_ptrs)) {
        ucs_ptr_array_for_each(ep, idx, &self->ep_ptrs) {
            uct_mm_coll_ep_destroy(ep);
            break; /* Since the inner loop is not safe to modify ptr_array */
        }
    }

    ucs_ptr_array_cleanup(&self->ep_ptrs, 1);
}

UCS_CLASS_DEFINE(uct_mm_coll_iface_t, uct_mm_base_iface_t);
