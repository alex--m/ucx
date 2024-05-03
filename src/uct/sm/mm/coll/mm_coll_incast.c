/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

#define UCT_MM_COLL_EP_GET_FIFO_ELEM(_ep, _index) \
        ((uct_mm_coll_fifo_element_t*)((char*)(_ep)->super.fifo_elems + \
                (((_index) & (_ep)->fifo_mask) * (_ep)->elem_size)))

ucs_status_t uct_mm_incast_iface_query_empty(uct_iface_h tl_iface,
                                             uct_iface_attr_t *iface_attr)
{
    /* In cases where a collective transport is not possible - avoid usage */
    uct_mm_coll_iface_query_empty(tl_iface, iface_attr);

    iface_attr->cap.flags |= UCT_IFACE_FLAG_INCAST;

    return UCS_OK;
}

static ucs_status_t uct_mm_incast_iface_query(uct_iface_h tl_iface,
                                              uct_iface_attr_t *iface_attr)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    ucs_status_t status        = uct_mm_coll_iface_query(tl_iface, iface_attr);
     if (status != UCS_OK) {
         return status;
     }

    /* Set the message length limits */
    iface_attr->cap.flags       |= UCT_IFACE_FLAG_INCAST;
    iface_attr->cap.am.max_short = iface->elem_slot_size;
    iface_attr->cap.am.max_bcopy = iface->seg_slot_size;

    if (iface->use_timestamps) {
        iface_attr->cap.am.max_short -= sizeof(uint64_t);
        iface_attr->cap.am.max_bcopy -= sizeof(uint64_t);
    }

    return UCS_OK;
}

#if HAVE_SM_COLL_EXTRA
static ucs_status_t uct_mm_incast_iface_query_locked(uct_iface_h tl_iface,
                                                     uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_incast_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->latency  = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead = 11e-9; /* 11 ns */

    return UCS_OK;
}

static ucs_status_t uct_mm_incast_iface_query_atomic(uct_iface_h tl_iface,
                                                     uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_incast_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->cap.flags              |= UCT_IFACE_FLAG_INCAST_UNORDERED;
    iface_attr->cap.coll_mode.operators = UCS_BIT(UCT_COLL_OPERATOR_SUM) |
                                          UCS_BIT(UCT_COLL_OPERATOR_SUM_ATOMIC);
    iface_attr->cap.coll_mode.operands  = UCS_BIT(UCT_COLL_OPERAND_INT8_T) |
                                          UCS_BIT(UCT_COLL_OPERAND_INT16_T) |
                                          UCS_BIT(UCT_COLL_OPERAND_INT32_T) |
                                          UCS_BIT(UCT_COLL_OPERAND_INT64_T) |
                                          UCS_BIT(UCT_COLL_OPERAND_UINT8_T) |
                                          UCS_BIT(UCT_COLL_OPERAND_UINT16_T) |
                                          UCS_BIT(UCT_COLL_OPERAND_UINT32_T) |
                                          UCS_BIT(UCT_COLL_OPERAND_UINT64_T);
    iface_attr->latency                 = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead                = 11e-9; /* 11 ns */

    return UCS_OK;
}

static ucs_status_t uct_mm_incast_iface_query_hypothetic(uct_iface_h tl_iface,
                                                         uct_iface_attr_t *iface_attr)
{
    /*
     * The use of the Atomic query here is only to limit the applicability:
     * since this method only demonstrates performance, without correctness,
     * it should be limited. This is useful for OSU, where measurements are
     * aggregated in floating point values, whereas what's measured is the
     * integer's reduction.
     */
    ucs_status_t status = uct_mm_incast_iface_query_atomic(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->latency    = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead   = 11e-9; /* 11 ns */

    return UCS_OK;
}

static ucs_status_t uct_mm_incast_iface_query_counted_slots(uct_iface_h tl_iface,
                                                            uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_incast_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->cap.flags |= UCT_IFACE_FLAG_INCAST_SLOTTED;
    iface_attr->latency    = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead   = 11e-9; /* 11 ns */

    return UCS_OK;
}
#endif

static ucs_status_t uct_mm_incast_iface_query_flagged_slots(uct_iface_h tl_iface,
                                                            uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_incast_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    /* Leave the last byte of each sender reserved for the comp. flag */
    iface_attr->cap.am.max_short--;
    iface_attr->cap.am.max_bcopy--;
    iface_attr->cap.flags |= UCT_IFACE_FLAG_INCAST_SLOTTED;
    iface_attr->latency    = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead   = 11e-9; /* 11 ns */

    return UCS_OK;
}

static ucs_status_t uct_mm_incast_iface_query_collaborative(uct_iface_h tl_iface,
                                                            uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_incast_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    /* Leave the last byte of each sender reserved for the comp. flag */
    iface_attr->cap.am.max_short--;
    iface_attr->cap.am.max_bcopy--;
    iface_attr->cap.flags |= UCT_IFACE_FLAG_INCAST_SLOTTED |
                             UCT_IFACE_FLAG_INCAST_UNORDERED;
    iface_attr->latency    = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead   = 11e-9; /* 11 ns */

    return UCS_OK;
}

UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_base_incast_iface_t, uct_iface_t);

static uct_iface_ops_t uct_mm_incast_iface_ops = {
    .ep_pending_add            = uct_mm_ep_pending_add,
    .ep_pending_purge          = uct_mm_ep_pending_purge,
    .ep_flush                  = uct_mm_coll_ep_flush,
    .ep_fence                  = uct_sm_ep_fence,
    .ep_create                 = uct_mm_incast_ep_create,
    .ep_destroy                = uct_mm_coll_ep_destroy,
    .iface_flush               = uct_mm_iface_flush,
    .iface_fence               = uct_sm_iface_fence,
    .iface_close               = UCS_CLASS_DELETE_FUNC_NAME(uct_mm_base_incast_iface_t),
    .iface_get_device_address  = uct_sm_iface_get_device_address,
    .iface_get_address         = uct_mm_coll_iface_get_address,
    .iface_is_reachable        = uct_base_iface_is_reachable
};

static uct_iface_internal_ops_t uct_mm_incast_iface_internal_ops = {
    .iface_estimate_perf   = uct_mm_estimate_perf,
    .iface_vfs_refresh     = (uct_iface_vfs_refresh_func_t)ucs_empty_function,
    .ep_query              = (uct_ep_query_func_t)ucs_empty_function,
    .ep_invalidate         = (uct_ep_invalidate_func_t)ucs_empty_function_return_unsupported,
    .ep_connect_to_ep_v2   = ucs_empty_function_return_unsupported,
    .iface_is_reachable_v2 = uct_mm_coll_iface_is_reachable_v2,
    .ep_is_connected       = uct_mm_coll_ep_is_connected
};

static uct_iface_query_func_t uct_mm_incast_iface_query_table[] = {
#if HAVE_SM_COLL_EXTRA
    [UCT_MM_COLL_TYPE_LOCKED]        = uct_mm_incast_iface_query_locked,
    [UCT_MM_COLL_TYPE_ATOMIC]        = uct_mm_incast_iface_query_atomic,
    [UCT_MM_COLL_TYPE_HYPOTHETIC]    = uct_mm_incast_iface_query_hypothetic,
    [UCT_MM_COLL_TYPE_COUNTED_SLOTS] = uct_mm_incast_iface_query_counted_slots,
#endif
    [UCT_MM_COLL_TYPE_FLAGGED_SLOTS] = uct_mm_incast_iface_query_flagged_slots,
    [UCT_MM_COLL_TYPE_COLLABORATIVE] = uct_mm_incast_iface_query_collaborative
};

static uct_ep_am_short_func_t uct_mm_incast_ep_am_short_table
[2/* non-zero length */][2/* non-zero ep->offset_id */][2/* use timestamp */]
[2/* use cache-line per peer */][UCT_MM_COLL_TYPE_LAST] = {
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_zero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_zero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_zero_id_zero_no_ts_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_zero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_zero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_zero_id_zero_no_ts_no_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_zero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_zero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_zero_id_zero_ts_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_zero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_zero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_zero_id_zero_ts_no_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_no_ts_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_ts_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_zero_id_nonzero_ts_no_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                }
            }
        }
    },
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_no_ts_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_ts_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_nonzero_id_zero_ts_no_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_ts_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_flagged_slots,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert
                }
            }
        }
    }
};

/*
 * Note: some functions cannot be implemented here because the receiver cannot
 *       express his reduction function. Specifically for the collaborative case
 *       we force the sender with the lowest ID to perform the reduction.
 */
static uct_ep_am_bcopy_func_t uct_mm_incast_ep_am_bcopy_table
[2/* non-zero length */][2/* non-zero ep->offset_id */][2/* use timestamp */]
[2/* use cache-line per peer */][UCT_MM_COLL_TYPE_LAST] = {
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_zero_id_zero_ts_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                }
            }
        }
    },
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_flagged_slots,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert
                }
            }
        }
    }
};

static uct_iface_progress_func_t uct_mm_incast_iface_progress_table
[2/* non-zero length */][2/* non-zero ep->offset_id */][2/* use timestamp */]
[2/* use cache-line per peer */][UCT_MM_COLL_TYPE_LAST] = {
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_zero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_zero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_zero_id_zero_no_ts_no_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_zero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_zero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_zero_id_zero_no_ts_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_zero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_zero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_zero_id_zero_ts_no_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_zero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_zero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_zero_id_zero_ts_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_no_ts_no_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_no_ts_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_ts_no_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_zero_id_nonzero_ts_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                }
            }
        }
    },
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_no_ts_no_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_no_ts_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_ts_no_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_nonzero_id_zero_ts_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_no_ts_no_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_no_ts_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_ts_no_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                },
                {
#if HAVE_SM_COLL_EXTRA
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_len_nonzero_id_nonzero_ts_cl_flagged_slots,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert
                }
            }
        }
    }
};

/**
 * Incast requires a reduction function (either internal or external) during
 * initialization, except for batched (which puts elements alongside instead of
 * actually reducing them). Consequently, using any of the other transports
 * would result in an error if @ref UCT_IFACE_PARAM_FIELD_COLL_INFO wasn't used.
 */
static uct_ep_am_short_func_t uct_mm_incast_ep_am_short_ext_cb_table
[2/* non-zero length */][2/* non-zero ep->offset_id */][2/* use timestamp */]
[2/* use cache-line per peer */][UCT_MM_COLL_TYPE_LAST] = {
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_no_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_no_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_no_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_no_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_zero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    },
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_no_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_no_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_no_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_cl_locked,
                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_short_ext_cb_len_nonzero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    }
};

static uct_ep_am_bcopy_func_t uct_mm_incast_ep_am_bcopy_ext_cb_table
[2/* non-zero length */][2/* non-zero ep->offset_id */][2/* use timestamp */]
[2/* use cache-line per peer */][UCT_MM_COLL_TYPE_LAST] = {
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_no_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_no_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_no_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_no_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_zero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    },
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_no_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_no_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_no_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_cl_locked,
                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_incast_ep_am_bcopy_ext_cb_len_nonzero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    }
};

static uct_iface_progress_func_t uct_mm_incast_iface_progress_ext_cb_table
[2/* non-zero length */][2/* non-zero ep->offset_id */][2/* use timestamp */]
[2/* use cache-line per peer */][UCT_MM_COLL_TYPE_LAST] = {
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_no_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_no_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_no_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_no_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_zero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    },
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_no_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_no_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_no_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_cl_locked,
                    (uct_iface_progress_func_t)ucs_empty_function_do_assert,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_incast_iface_progress_ext_cb_len_nonzero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    }
};

static ucs_status_t
uct_mm_incast_iface_choose_am_send(enum uct_mm_coll_type type, uint32_t procs,
                                   uct_incast_operator_t operator,
                                   uct_incast_operand_t operand,
                                   unsigned operand_count, size_t msg_size,
                                   size_t max_short, int is_id_non_zero,
                                   int is_length_non_zero, int use_timestamps,
                                   int use_cacheline,
                                   uct_ep_am_short_func_t *ep_am_short_p,
                                   uct_ep_am_bcopy_func_t *ep_am_bcopy_p,
                                   uct_iface_progress_func_t *iface_progress_p)
{
    unsigned extra_index;
    uct_ep_am_short_func_t short_f;
    uct_ep_am_bcopy_func_t bcopy_f;
    uct_iface_progress_func_t progress_f;

    uct_mm_coll_ep_init_incast_cb_arrays();

    if (operator == UCT_COLL_OPERATOR_EXTERNAL) {
        return UCS_ERR_UNSUPPORTED;
    }

    if ((operator >= UCT_COLL_OPERATOR_LAST) ||
        (operand  >= UCT_COLL_OPERAND_LAST)) {
        return UCS_ERR_INVALID_PARAM;
    }

    if (msg_size > max_short) {
        extra_index = uct_mm_coll_ep_get_extra_index(operand_count);
        if (extra_index >= UCT_MM_COLL_MAX_COUNT_SUPPORTED) {
            extra_index = 0; /* Determine the operand count based on arguments */
        }

        use_cacheline  = 0;  /* cache-line assumes alignment and flag @63B */
        *ep_am_short_p = (uct_ep_am_short_func_t)ucs_empty_function_do_assert;
        bcopy_f        = uct_mm_incast_ep_am_bcopy_func_arr[is_length_non_zero]
                                                           [is_id_non_zero]
                                                           [use_timestamps]
                                                           [use_cacheline]
                                                           [operator][operand]
                                                           [extra_index][type];
        progress_f     = uct_mm_incast_iface_progress_bcopy_func_arr[is_length_non_zero]
                                                                    [is_id_non_zero]
                                                                    [use_timestamps]
                                                                    [use_cacheline]
                                                                    [operator][operand]
                                                                    [extra_index][type];
        if (!*bcopy_f) {
            if (type == UCT_MM_COLL_TYPE_COLLABORATIVE) {
                return UCS_ERR_UNSUPPORTED;
            }

            if (*ep_am_bcopy_p ==
                (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert) {
                return UCS_ERR_UNSUPPORTED;
            }
        } else {
            *ep_am_bcopy_p    = bcopy_f;
            *iface_progress_p = progress_f;
        }
    } else {
        if (operand_count >= UCT_MM_COLL_MAX_COUNT_SUPPORTED) {
            operand_count = 0; /* choose functions with length-detection */
        }

        *ep_am_bcopy_p = (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert;
        short_f        = uct_mm_incast_ep_am_short_func_arr[is_length_non_zero]
                                                           [is_id_non_zero]
                                                           [use_timestamps]
                                                           [use_cacheline]
                                                           [operator][operand]
                                                           [operand_count][type];
        progress_f     = uct_mm_incast_iface_progress_short_func_arr[is_length_non_zero]
                                                                    [is_id_non_zero]
                                                                    [use_timestamps]
                                                                    [use_cacheline]
                                                                    [operator][operand]
                                                                    [operand_count][type];
        if (!short_f) {
            if (type == UCT_MM_COLL_TYPE_COLLABORATIVE) {
                return UCS_ERR_UNSUPPORTED;
            }

            if (*ep_am_short_p ==
                (uct_ep_am_short_func_t)ucs_empty_function_do_assert) {
                return UCS_ERR_UNSUPPORTED;
            }
        } else {
            *ep_am_short_p    = short_f;
            *iface_progress_p = progress_f;
        }
    }

    ucs_assert(*iface_progress_p != NULL);
#if HAVE_SM_COLL_EXTRA
    ucs_assert((type != UCT_MM_COLL_TYPE_ATOMIC) ||
               (operator == UCT_COLL_OPERATOR_SUM_ATOMIC));
#endif

    return UCS_OK;
}

static ucs_status_t mm_coll_incast_ep_dummy_init(uct_mm_base_incast_iface_t *self,
                                                 uint32_t procs)
{
    self->dummy = UCS_ALLOC_CHECK(sizeof(*self->dummy), "mm_coll_incast_ep_dummy");

    self->dummy->super.super.super.super.iface = (uct_iface_h)self;
    self->dummy->super.sm_proc_cnt             = procs - 1;
    self->dummy->super.offset_id               = 0;
    self->dummy->super.elem_slot               = self->super.elem_slot_size;
    self->dummy->super.elem_offset             = 0;
    self->dummy->super.elem_size               = self->super.super.config.fifo_elem_size;
    self->dummy->super.seg_slot                = self->super.seg_slot_size;
    self->dummy->super.seg_offset              = 0;
    self->dummy->super.seg_size                = self->super.super.config.seg_size;
    self->dummy->super.fifo_size               = self->super.super.config.fifo_size;
    self->dummy->super.fifo_mask               = self->super.super.fifo_mask;
    self->dummy->super.fifo_elems              = self->super.super.recv_fifo_elems;

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_base_incast_iface_t, uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config,
                    enum uct_mm_coll_type type)
{
    ucs_status_t status;
    uct_incast_operator_t operator;
    size_t dt_size;
    int is_short_msg;
    int use_cacheline                      = 0;
    int is_len_non_zero                    = 1;
    int use_timestamps                     = (params->field_mask &
                                              UCT_IFACE_PARAM_FIELD_FEATURES) &&
                                             (params->features   &
                                              UCT_IFACE_FEATURE_TX_TIMESTAMP);
    int is_collective                      = (params->field_mask &
                                              UCT_IFACE_PARAM_FIELD_GROUP_INFO);
    int is_id_non_zero                     = is_collective ?
                                             params->group_info.proc_idx > 1 : 0;
    uint32_t procs                         = is_collective ?
                                             params->group_info.proc_cnt :
                                             1 + !is_collective;
    uct_mm_base_incast_iface_config_t *cfg = ucs_derived_of(tl_config,
                                                            uct_mm_base_incast_iface_config_t);
    unsigned orig_fifo_elem_size           = cfg->super.super.fifo_elem_size;
    unsigned orig_seg_size                 = cfg->super.super.seg_size;
    int is_collaborative                   = (type == UCT_MM_COLL_TYPE_COLLABORATIVE);
    size_t short_stride                    = ucs_align_up(orig_fifo_elem_size -
                                                          sizeof(uct_mm_coll_fifo_element_t),
                                                          UCS_SYS_CACHE_LINE_SIZE);
    size_t bcopy_stride                    = ucs_align_down(orig_seg_size,
                                                            UCS_SYS_CACHE_LINE_SIZE);
    unsigned new_fifo_elem_size            = sizeof(uct_mm_coll_fifo_element_t) +
                                             ((procs - !is_collaborative) *
                                              short_stride);
    unsigned new_seg_size                  = ((procs - !is_collaborative) *
                                              bcopy_stride) + sizeof(uint64_t);
    cfg->super.super.fifo_elem_size        = new_fifo_elem_size;
    cfg->super.super.seg_size              = new_seg_size;

    ucs_assert(type < UCT_MM_COLL_TYPE_LAST);

    if (is_collective) {
        if (params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) {
            use_cacheline = ((short_stride % UCS_SYS_CACHE_LINE_SIZE) == 0) &&
                            (params->coll_info.is_operand_cache_aligned != 0);
            is_len_non_zero = (params->coll_info.operand_count != 0);
        }

        uct_mm_incast_iface_ops.iface_query    = uct_mm_incast_iface_query_table[type];
        uct_mm_incast_iface_ops.ep_am_short    = uct_mm_incast_ep_am_short_table
                                                 [is_len_non_zero][is_id_non_zero]
                                                 [use_timestamps][use_cacheline][type];
        uct_mm_incast_iface_ops.ep_am_bcopy    = uct_mm_incast_ep_am_bcopy_table
                                                 [is_len_non_zero][is_id_non_zero]
                                                 [use_timestamps][use_cacheline][type];
        uct_mm_incast_iface_ops.iface_progress = uct_mm_incast_iface_progress_table
                                                 [is_len_non_zero][is_id_non_zero]
                                                 [use_timestamps][use_cacheline][type];

        if (params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) {
            /* Atomic transport is only applicable to integer summation */
            operator = params->coll_info.operator_;
#if HAVE_SM_COLL_EXTRA
            if (type == UCT_MM_COLL_TYPE_ATOMIC) {
                if (operator == UCT_COLL_OPERATOR_SUM) {
                    operator = UCT_COLL_OPERATOR_SUM_ATOMIC;
                }

                if ((operator != UCT_COLL_OPERATOR_SUM_ATOMIC) ||
                    (params->coll_info.operand == UCT_COLL_OPERAND_FLOAT) ||
                    (params->coll_info.operand == UCT_COLL_OPERAND_DOUBLE)) {
                    is_collective = 0;
                }
            }

            /* Avoid using hypothetic for barriers */
            if ((type == UCT_MM_COLL_TYPE_HYPOTHETIC) && !is_len_non_zero) {
                is_collective = 0;
            }
#endif

            /* Account for the reserved byte for the flag */
            if ((type == UCT_MM_COLL_TYPE_FLAGGED_SLOTS) ||
                (type == UCT_MM_COLL_TYPE_COLLABORATIVE)) {
                short_stride--;
            }
            is_short_msg = params->coll_info.total_size <= short_stride;

            /* Choose the send and reduction functions */
            status = uct_mm_incast_iface_choose_am_send(type, procs, operator,
                                                        params->coll_info.operand,
                                                        params->coll_info.operand_count,
                                                        params->coll_info.total_size,
                                                        short_stride, is_id_non_zero,
                                                        is_len_non_zero, use_timestamps,
                                                        use_cacheline,
                                                        &uct_mm_incast_iface_ops.ep_am_short,
                                                        &uct_mm_incast_iface_ops.ep_am_bcopy,
                                                        &uct_mm_incast_iface_ops.iface_progress);
            if ((status == UCS_ERR_UNSUPPORTED) &&
#if HAVE_SM_COLL_EXTRA
                (type != UCT_MM_COLL_TYPE_ATOMIC) &&
#endif
                ((is_short_msg  && (uct_mm_incast_iface_ops.ep_am_short ==
                                    (uct_ep_am_short_func_t)ucs_empty_function_do_assert)) ||
                 (!is_short_msg && (uct_mm_incast_iface_ops.ep_am_bcopy ==
                                    (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert)))) {
                /* Prepare to fall-back to the external reduction function */
                self->ext_reduce.md                = md;
                self->ext_reduce.operator          = params->coll_info.ext_operator;
                self->ext_reduce.datatype          = params->coll_info.ext_datatype;
                self->ext_reduce.def.cb            = params->coll_info.ext_cb;
                self->ext_reduce.def.operand_count = params->coll_info.operand_count;
                self->ext_reduce.def.total_size    = params->coll_info.total_size;
                self->ext_reduce.aux.cb            = params->coll_info.ext_aux_cb;
                self->ext_reduce.aux.operand_count = params->coll_info.operand_count;
                self->ext_reduce.aux.total_size    = params->coll_info.total_size;

                if (!is_short_msg && params->coll_info.total_size > bcopy_stride) {
                    /* Calculate the size of a single data-type */
                    dt_size = params->coll_info.total_size /
                              params->coll_info.operand_count;
                    ucs_assert(!(params->coll_info.total_size %
                                 params->coll_info.operand_count));

                    /*
                     * Set the default (fragment) sizes for regular, non-auxiliary
                     * reduction (auxiliary would still use the original size).
                     */
                    // TODO: Take timestamps into account!
                    self->ext_reduce.def.total_size    = ucs_align_down((bcopy_stride - 1 -
                                                                         (use_timestamps *
                                                                          sizeof(uint64_t))),
                                                                        dt_size);
                    self->ext_reduce.def.operand_count = self->ext_reduce.def.total_size /
                                                         dt_size;
                }

                /* Choose the functions which invoke the external callback */
                uct_mm_incast_iface_ops.ep_am_short    = is_short_msg ?
                                                         uct_mm_incast_ep_am_short_ext_cb_table
                                                         [is_len_non_zero][is_id_non_zero]
                                                         [use_timestamps][use_cacheline][type]:
                                                         (uct_ep_am_short_func_t)ucs_empty_function_do_assert;
                uct_mm_incast_iface_ops.ep_am_bcopy    = is_short_msg ?
                                                         (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert:
                                                         uct_mm_incast_ep_am_bcopy_ext_cb_table
                                                         [is_len_non_zero][is_id_non_zero]
                                                         [use_timestamps][use_cacheline][type];
                uct_mm_incast_iface_ops.iface_progress = uct_mm_incast_iface_progress_ext_cb_table
                                                         [is_len_non_zero][is_id_non_zero]
                                                         [use_timestamps][use_cacheline][type];
            } else if (((status != UCS_OK) && (status != UCS_ERR_UNSUPPORTED)) ||
                       (is_short_msg &&
                        (uct_mm_incast_iface_ops.ep_am_short ==
                         (uct_ep_am_short_func_t)ucs_empty_function_do_assert)) ||
                       (!is_short_msg &&
                        (uct_mm_incast_iface_ops.ep_am_bcopy ==
                         (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert))) {
                is_collective = 0;
            }

            /* Counteract the reserved byte for the flag */
            if ((type == UCT_MM_COLL_TYPE_FLAGGED_SLOTS) ||
                (type == UCT_MM_COLL_TYPE_COLLABORATIVE)) {
                short_stride++;
            }
        }
    }

    /* Note that under some circumstances both this and the prev. if conditions are true */
    if (!is_collective) {
        worker                                 = NULL;
        uct_mm_incast_iface_ops.iface_query    = uct_mm_incast_iface_query_empty;
        uct_mm_incast_iface_ops.iface_progress = (uct_iface_progress_func_t)ucs_empty_function_do_assert;
    }

    if (uct_mm_incast_iface_ops.iface_progress == (uct_iface_progress_func_t)ucs_empty_function_do_assert) {
        uct_mm_incast_iface_ops.iface_progress_enable  = (uct_iface_progress_enable_func_t)ucs_empty_function;
        uct_mm_incast_iface_ops.iface_progress_disable = (uct_iface_progress_disable_func_t)ucs_empty_function;
    } else {
        uct_mm_incast_iface_ops.iface_progress_enable  = uct_base_iface_progress_enable;
        uct_mm_incast_iface_ops.iface_progress_disable = uct_base_iface_progress_disable;
    }

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_iface_t, &uct_mm_incast_iface_ops,
                              &uct_mm_incast_iface_internal_ops, md, worker,
                              params, tl_config, 1, type, short_stride,
                              bcopy_stride);

    cfg->super.super.fifo_elem_size = orig_fifo_elem_size;
    cfg->super.super.seg_size       = orig_seg_size;

    if (is_collective) {
        status = mm_coll_incast_ep_dummy_init(self, procs);
        if (status != UCS_OK) {
            return status;
        }
    }

    return UCS_OK;
}

UCS_CLASS_CLEANUP_FUNC(uct_mm_base_incast_iface_t)
{
    if (self->super.super.super.super.worker != NULL) {
        ucs_free(self->dummy);
    }
}

#define MM_COLL_INCAST_IFACE_NAME(_lower_name) uct_mm_##_lower_name##_incast_iface_t

#define MM_COLL_INCAST_CLASS_FUNCS(_lower_name, _base, ...) \
    UCS_CLASS_DEFINE(MM_COLL_INCAST_IFACE_NAME(_lower_name), \
                     MM_COLL_INCAST_IFACE_NAME(_base)) \
    UCS_CLASS_DEFINE_NEW_FUNC(MM_COLL_INCAST_IFACE_NAME(_lower_name), uct_iface_t, \
                              uct_md_h, uct_worker_h, const uct_iface_params_t*, \
                              const uct_iface_config_t*, ##__VA_ARGS__) \
    UCS_CLASS_DEFINE_DELETE_FUNC(MM_COLL_INCAST_IFACE_NAME(_lower_name), \
                                 uct_iface_t)

#define MM_COLL_INCAST_CLASS_INIT(_lower_name, _upper_name) \
    UCS_CLASS_CLEANUP_FUNC(MM_COLL_INCAST_IFACE_NAME(_lower_name)) {} \
    UCS_CLASS_INIT_FUNC(MM_COLL_INCAST_IFACE_NAME(_lower_name), uct_md_h md, \
                        uct_worker_h worker, const uct_iface_params_t *params, \
                        const uct_iface_config_t *tl_config) \
    { \
        UCS_CLASS_CALL_SUPER_INIT(MM_COLL_INCAST_IFACE_NAME(base), md, worker, params, \
                                  tl_config, UCT_MM_COLL_TYPE_##_upper_name); \
    \
        return UCS_OK; \
    } \
    MM_COLL_INCAST_CLASS_FUNCS(_lower_name, coll)

#define uct_mm_coll_incast_iface_t_class uct_mm_coll_iface_t_class

MM_COLL_INCAST_CLASS_FUNCS(base, coll, enum uct_mm_coll_type)
#if HAVE_SM_COLL_EXTRA
MM_COLL_INCAST_CLASS_INIT(locked,        LOCKED)
MM_COLL_INCAST_CLASS_INIT(atomic,        ATOMIC)
MM_COLL_INCAST_CLASS_INIT(hypothetic,    HYPOTHETIC)
MM_COLL_INCAST_CLASS_INIT(counted_slots, COUNTED_SLOTS)
#endif
MM_COLL_INCAST_CLASS_INIT(flagged_slots, FLAGGED_SLOTS)
MM_COLL_INCAST_CLASS_INIT(collaborative, COLLABORATIVE)
