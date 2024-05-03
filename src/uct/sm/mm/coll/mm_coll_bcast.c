/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

ucs_status_t uct_mm_bcast_iface_query_empty(uct_iface_h tl_iface,
                                            uct_iface_attr_t *iface_attr)
{
    /* In cases where a collective transport is not possible - avoid usage */
    uct_mm_coll_iface_query_empty(tl_iface, iface_attr);

    iface_attr->cap.flags |= UCT_IFACE_FLAG_BCAST;

    return UCS_OK;
}

static ucs_status_t uct_mm_bcast_iface_query(uct_mm_coll_iface_t* iface,
                                             uct_iface_attr_t *iface_attr,
                                             size_t occupied)
{
    ucs_status_t status = uct_mm_coll_iface_query((uct_iface_h)iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->cap.flags |= UCT_IFACE_FLAG_BCAST;

    if (iface->super.config.fifo_elem_size >
        (occupied + sizeof(uct_mm_coll_fifo_element_t))) {
        iface_attr->cap.am.max_short = iface->super.config.fifo_elem_size -
                                       (sizeof(uct_mm_coll_fifo_element_t) +
                                        occupied);
    } else {
        iface_attr->cap.flags &= ~UCT_IFACE_FLAG_AM_SHORT;
    }

    if (iface->super.config.seg_size > (occupied + sizeof(uint64_t))) {
        iface_attr->cap.am.max_bcopy = iface->super.config.seg_size - occupied -
                                       sizeof(uint64_t);
    } else {
        iface_attr->cap.flags &= ~UCT_IFACE_FLAG_AM_BCOPY;
    }

    if (iface->use_timestamps) {
        ucs_assert_always(iface_attr->cap.am.max_short > sizeof(uint64_t));
        ucs_assert_always(iface_attr->cap.am.max_bcopy > sizeof(uint64_t));
        iface_attr->cap.am.max_short -= sizeof(uint64_t);
        iface_attr->cap.am.max_bcopy -= sizeof(uint64_t);
    }

    return UCS_OK;
}

#if HAVE_SM_COLL_EXTRA
static ucs_status_t uct_mm_bcast_iface_query_locked(uct_iface_h tl_iface,
                                                    uct_iface_attr_t *iface_attr)
{
    uct_mm_coll_iface_t* iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    ucs_status_t status        = uct_mm_bcast_iface_query(iface, iface_attr, 0);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->latency  = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead = 11e-9; /* 11 ns */
    return UCS_OK;
}

static ucs_status_t uct_mm_bcast_iface_query_atomic(uct_iface_h tl_iface,
                                                    uct_iface_attr_t *iface_attr)
{
    uct_mm_coll_iface_t* iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    ucs_status_t status        = uct_mm_bcast_iface_query(iface, iface_attr, 0);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->latency  = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead = 11e-9; /* 11 ns */
    return UCS_OK;
}

static ucs_status_t uct_mm_bcast_iface_query_hypothetic(uct_iface_h tl_iface,
                                                        uct_iface_attr_t *iface_attr)
{
    uct_mm_coll_iface_t* iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    ucs_status_t status        = uct_mm_bcast_iface_query(iface, iface_attr, 0);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->latency  = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead = 11e-9; /* 11 ns */
    return UCS_OK;
}

static ucs_status_t uct_mm_bcast_iface_query_counted_slots(uct_iface_h tl_iface,
                                                           uct_iface_attr_t *iface_attr)
{
    uct_mm_coll_iface_t* iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    ucs_status_t status        = uct_mm_bcast_iface_query(iface, iface_attr, 0);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->latency  = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead = 11e-9; /* 11 ns */
    return UCS_OK;
}
#endif

static ucs_status_t uct_mm_bcast_iface_query_flagged_slots(uct_iface_h tl_iface,
                                                           uct_iface_attr_t *iface_attr)
{
    uct_mm_coll_iface_t* iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    size_t occupied            = (iface->sm_proc_cnt - 1) * UCS_SYS_CACHE_LINE_SIZE;
    ucs_status_t status        = uct_mm_bcast_iface_query(iface, iface_attr,
                                                          occupied);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->latency  = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead = 11e-9; /* 11 ns */
    return UCS_OK;
}

static ucs_status_t uct_mm_bcast_iface_query_collaborative(uct_iface_h tl_iface,
                                                           uct_iface_attr_t *iface_attr)
{
    uct_mm_coll_iface_t* iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    size_t occupied            = iface->sm_proc_cnt * UCS_SYS_CACHE_LINE_SIZE;
    ucs_status_t status        = uct_mm_bcast_iface_query(iface, iface_attr,
                                                          occupied);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->latency  = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead = 11e-9; /* 11 ns */
    return UCS_OK;
}

UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_base_bcast_iface_t, uct_iface_t);

static uct_iface_ops_t uct_mm_bcast_iface_ops = {
    .ep_pending_add            = uct_mm_ep_pending_add,
    .ep_pending_purge          = uct_mm_ep_pending_purge,
    .ep_flush                  = uct_mm_coll_ep_flush,
    .ep_fence                  = uct_sm_ep_fence,
    .ep_create                 = uct_mm_bcast_ep_create,
    .ep_destroy                = uct_mm_bcast_ep_destroy,
    .iface_flush               = uct_mm_iface_flush,
    .iface_fence               = uct_sm_iface_fence,
    .iface_close               = UCS_CLASS_DELETE_FUNC_NAME(uct_mm_base_bcast_iface_t),
    .iface_get_device_address  = uct_sm_iface_get_device_address,
    .iface_get_address         = uct_mm_coll_iface_get_address,
    .iface_is_reachable        = uct_base_iface_is_reachable
};

static uct_iface_internal_ops_t uct_mm_bcast_iface_internal_ops = {
    .iface_estimate_perf   = uct_mm_estimate_perf,
    .iface_vfs_refresh     = (uct_iface_vfs_refresh_func_t)ucs_empty_function,
    .ep_query              = (uct_ep_query_func_t)ucs_empty_function,
    .ep_invalidate         = (uct_ep_invalidate_func_t)ucs_empty_function_return_unsupported,
    .ep_connect_to_ep_v2   = ucs_empty_function_return_unsupported,
    .iface_is_reachable_v2 = uct_mm_coll_iface_is_reachable_v2,
    .ep_is_connected       = uct_mm_coll_ep_is_connected
};

static uct_iface_query_func_t uct_mm_bcast_iface_query_table[] = {
#if HAVE_SM_COLL_EXTRA
    [UCT_MM_COLL_TYPE_LOCKED]        = uct_mm_bcast_iface_query_locked,
    [UCT_MM_COLL_TYPE_ATOMIC]        = uct_mm_bcast_iface_query_atomic,
    [UCT_MM_COLL_TYPE_HYPOTHETIC]    = uct_mm_bcast_iface_query_hypothetic,
    [UCT_MM_COLL_TYPE_COUNTED_SLOTS] = uct_mm_bcast_iface_query_counted_slots,
#endif
    [UCT_MM_COLL_TYPE_FLAGGED_SLOTS] = uct_mm_bcast_iface_query_flagged_slots,
    [UCT_MM_COLL_TYPE_COLLABORATIVE] = uct_mm_bcast_iface_query_collaborative
};

static uct_ep_am_short_func_t uct_mm_bcast_ep_am_short_table
[2/* non-zero length */][2/* non-zero ep->offset_id */][2/* use timestamp */]
[2/* use cache-line per peer */][UCT_MM_COLL_TYPE_LAST] = {
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_cl_locked,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_cl_locked,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_zero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_cl_locked,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_cl_locked,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_zero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    },
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_cl_locked,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_cl_locked,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_locked,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_cl_locked,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_cl_atomic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_short_len_nonzero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    }
};

static uct_ep_am_bcopy_func_t uct_mm_bcast_ep_am_bcopy_table
[2/* non-zero length */][2/* non-zero ep->offset_id */][2/* use timestamp */]
[2/* use cache-line per peer */][UCT_MM_COLL_TYPE_LAST] = {
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    },
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_locked,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_atomic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_bcast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    }
};

static uct_iface_progress_func_t uct_mm_bcast_iface_progress_table
[2/* non-zero length */][2/* non-zero ep->offset_id */][2/* use timestamp */]
[2/* use cache-line per peer */][UCT_MM_COLL_TYPE_LAST] = {
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_no_cl_locked,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_no_cl_atomic,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_cl_locked,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_cl_atomic,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_no_cl_locked,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_no_cl_atomic,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_cl_locked,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_cl_atomic,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_zero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_no_cl_locked,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_no_cl_atomic,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_cl_locked,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_cl_atomic,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_no_cl_locked,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_no_cl_atomic,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_cl_locked,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_cl_atomic,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_zero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    },
    {
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_no_cl_locked,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_no_cl_atomic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_cl_locked,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_cl_atomic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_no_cl_locked,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_no_cl_atomic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_no_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_cl_locked,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_cl_atomic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_nonzero_id_zero_ts_cl_collaborative
                }
            }
        },
        {
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_no_cl_locked,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_no_cl_atomic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_no_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_no_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_cl_locked,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_cl_atomic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_no_ts_cl_collaborative
                }
            },
            {
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_no_cl_locked,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_no_cl_atomic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_no_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_no_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_no_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_no_cl_collaborative
                },
                {
#if HAVE_SM_COLL_EXTRA
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_cl_locked,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_cl_atomic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_cl_hypothetic,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_cl_counted_slots,
#endif
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_cl_flagged_slots,
                    uct_mm_bcast_iface_progress_len_nonzero_id_nonzero_ts_cl_collaborative
                }
            }
        }
    }
};

UCS_CLASS_INIT_FUNC(uct_mm_base_bcast_iface_t, uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config,
                    enum uct_mm_coll_type type)
{
    size_t slot_size;
    int use_cacheline;
    uct_mm_fifo_check_t *recv_check;
    uct_mm_base_bcast_iface_config_t *cfg;
    int use_timestamps    = (params->field_mask & UCT_IFACE_PARAM_FIELD_FEATURES) &&
                            (params->features   & UCT_IFACE_FEATURE_TX_TIMESTAMP);
    int is_collective     = (params->field_mask & UCT_IFACE_PARAM_FIELD_GROUP_INFO);
    int is_collaborative  = (type == UCT_MM_COLL_TYPE_COLLABORATIVE);
    int is_id_non_zero    = is_collective ? params->group_info.proc_idx > 1 : 0;
    int is_len_non_zero   = !(params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) ||
                            (params->coll_info.operand_count != 0);
    uint32_t procs        = is_collective ? params->group_info.proc_cnt : 1;
    cfg                   = ucs_derived_of(tl_config, uct_mm_base_bcast_iface_config_t);
    self->poll_iface_idx  = 0;
    self->poll_ep_idx     = 0;

    ucs_assert(type < UCT_MM_COLL_TYPE_LAST);

    if ((type == UCT_MM_COLL_TYPE_FLAGGED_SLOTS) ||
        (type == UCT_MM_COLL_TYPE_COLLABORATIVE)) {
        slot_size     = UCS_SYS_CACHE_LINE_SIZE;
        use_cacheline = 1;
    } else {
        slot_size     = 0;
        use_cacheline = (cfg->super.super.fifo_elem_size ==
                         (2 * UCS_SYS_CACHE_LINE_SIZE));
    }

    cfg->super.super.fifo_elem_size += slot_size * (procs - !is_collaborative);
    cfg->super.super.seg_size       += slot_size * (procs - !is_collaborative);

    if (is_collective) {
        uct_mm_bcast_iface_ops.iface_query    = uct_mm_bcast_iface_query_table[type];
        uct_mm_bcast_iface_ops.iface_progress = uct_mm_bcast_iface_progress_table
                                                [is_len_non_zero][is_id_non_zero]
                                                [use_timestamps][use_cacheline][type];
        uct_mm_bcast_iface_ops.ep_am_short    = uct_mm_bcast_ep_am_short_table
                                                [is_len_non_zero][is_id_non_zero]
                                                [use_timestamps][use_cacheline][type];
        uct_mm_bcast_iface_ops.ep_am_bcopy    = uct_mm_bcast_ep_am_bcopy_table
                                                [is_len_non_zero][is_id_non_zero]
                                                [use_timestamps][use_cacheline][type];

        ucs_assert(uct_mm_bcast_iface_ops.ep_am_short !=
                   (uct_ep_am_short_func_t)ucs_empty_function_do_assert);
        ucs_assert(uct_mm_bcast_iface_ops.ep_am_bcopy !=
                   (uct_ep_am_bcopy_func_t)ucs_empty_function_do_assert);
    } else {
        worker                                = NULL;
        uct_mm_bcast_iface_ops.iface_query    = uct_mm_bcast_iface_query_empty;
        uct_mm_bcast_iface_ops.iface_progress = (uct_iface_progress_func_t)
                                                ucs_empty_function_do_assert;
    }

    if (uct_mm_bcast_iface_ops.iface_progress == (uct_iface_progress_func_t)ucs_empty_function_do_assert) {
        uct_mm_bcast_iface_ops.iface_progress_enable  = (uct_iface_progress_enable_func_t)
                                                        ucs_empty_function;
        uct_mm_bcast_iface_ops.iface_progress_disable = (uct_iface_progress_disable_func_t)
                                                        ucs_empty_function;
    } else {
        uct_mm_bcast_iface_ops.iface_progress_enable  = uct_base_iface_progress_enable;
        uct_mm_bcast_iface_ops.iface_progress_disable = uct_base_iface_progress_disable;
    }


    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_iface_t, &uct_mm_bcast_iface_ops,
                              &uct_mm_bcast_iface_internal_ops, md, worker,
                              params, tl_config, 0, type, slot_size, slot_size);

    /* Allocate a dummy endpoint until the first actual endpoint is created */
    self->dummy_ep =
    self->last_nonzero_ep = UCS_ALLOC_CHECK(sizeof(uct_mm_bcast_ep_t),
                                            "uct_mm_bcast_dummy_ep");
    recv_check            = ucs_unaligned_ptr(&self->dummy_ep->recv_check);
    memset(recv_check, 0, sizeof(uct_mm_fifo_check_t));

    /* Allocate a dummy element which will never be marked as ready or read */
    recv_check->read_elem            = UCS_ALLOC_CHECK(sizeof(uct_mm_fifo_element_t),
                                                       "uct_mm_bcast_dummy_element");
    recv_check->read_elem->flags     = UCT_MM_FIFO_ELEM_FLAG_OWNER;

    /* Restore original configuration values */
    cfg->super.super.fifo_elem_size -= slot_size * (procs - !is_collaborative);
    cfg->super.super.seg_size       -= slot_size * (procs - !is_collaborative);
    return UCS_OK;
}

UCS_CLASS_CLEANUP_FUNC(uct_mm_base_bcast_iface_t)
{
    uct_mm_fifo_check_t *recv_check = ucs_unaligned_ptr(&self->dummy_ep->recv_check);

    ucs_free(recv_check->read_elem);
    ucs_free(self->dummy_ep);
}

#define MM_COLL_BCAST_IFACE_NAME(_lower_name) uct_mm_##_lower_name##_bcast_iface_t

#define MM_COLL_BCAST_CLASS_FUNCS(_lower_name, _base, ...) \
    UCS_CLASS_DEFINE(MM_COLL_BCAST_IFACE_NAME(_lower_name), \
                     MM_COLL_BCAST_IFACE_NAME(_base)) \
    UCS_CLASS_DEFINE_NEW_FUNC(MM_COLL_BCAST_IFACE_NAME(_lower_name), uct_iface_t, \
                              uct_md_h, uct_worker_h, const uct_iface_params_t*, \
                              const uct_iface_config_t*, ##__VA_ARGS__) \
    UCS_CLASS_DEFINE_DELETE_FUNC(MM_COLL_BCAST_IFACE_NAME(_lower_name), \
                                 uct_iface_t)

#define MM_COLL_BCAST_CLASS_INIT(_lower_name, _upper_name) \
    UCS_CLASS_CLEANUP_FUNC(MM_COLL_BCAST_IFACE_NAME(_lower_name)) {} \
    UCS_CLASS_INIT_FUNC(MM_COLL_BCAST_IFACE_NAME(_lower_name), uct_md_h md, \
                        uct_worker_h worker, const uct_iface_params_t *params, \
                        const uct_iface_config_t *tl_config) \
    { \
        UCS_CLASS_CALL_SUPER_INIT(MM_COLL_BCAST_IFACE_NAME(base), md, worker, params, \
                                  tl_config, UCT_MM_COLL_TYPE_##_upper_name); \
    \
        return UCS_OK; \
    } \
    MM_COLL_BCAST_CLASS_FUNCS(_lower_name, coll)

#define uct_mm_coll_bcast_iface_t_class uct_mm_coll_iface_t_class

MM_COLL_BCAST_CLASS_FUNCS(base, coll, enum uct_mm_coll_type)
#if HAVE_SM_COLL_EXTRA
MM_COLL_BCAST_CLASS_INIT(locked,        LOCKED)
MM_COLL_BCAST_CLASS_INIT(atomic,        ATOMIC)
MM_COLL_BCAST_CLASS_INIT(hypothetic,    HYPOTHETIC)
MM_COLL_BCAST_CLASS_INIT(counted_slots, COUNTED_SLOTS)
#endif
MM_COLL_BCAST_CLASS_INIT(flagged_slots, FLAGGED_SLOTS)
MM_COLL_BCAST_CLASS_INIT(collaborative, COLLABORATIVE)
