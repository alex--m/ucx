/**
* Copyright (C) UT-Battelle, LLC. 2015. ALL RIGHTS RESERVED.
* Copyright (C) Mellanox Technologies Ltd. 2001-2019.  ALL RIGHTS RESERVED.
* Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
* See file LICENSE for terms.
*/

#ifndef UCT_MM_COLL_EP_H
#define UCT_MM_COLL_EP_H

#include "mm_coll_iface.h"

#include <uct/sm/mm/base/mm_ep.h>

struct uct_mm_coll_ep {
    uct_mm_ep_t                 super;
    unsigned                    ref_count;  /* counts the uses of this ep */
    uint8_t                     remote_id;   /* ID of endpoint's remote peer*/

    UCS_CACHELINE_PADDING(uct_mm_ep_t, unsigned, uint8_t);

    uint8_t                     sm_proc_cnt; /* iface->sm_proc_cnt - 1*/
    uint8_t                     offset_id;   /* relative offset among coll IDs */

    uint16_t                    elem_offset; /* write offset when using slots */
    uint16_t                    elem_slot;   /* slot size for "flagged slots" */
    uint16_t                    elem_size;   /* fifo_elem_size used for RX */

    uint32_t                    seg_offset;  /* bcopy segment/slot size */
    uint32_t                    seg_slot;    /* bcopy segment/slot size */
    uint32_t                    seg_size;    /* bcopy total size */

    /* Used to transition to the next element in the FIFO */
    uint32_t                    fifo_size;   /* shortcut to iface->fifo_size */
    uint32_t                    fifo_mask;   /* shortcut to iface->fifo_mask */
    uint32_t                    tx_index;    /* next TX element index */
    uct_mm_coll_fifo_element_t *tx_elem;     /* next TX element pointer */
    void                       *fifo_elems;  /* Duplicate field from super */
} UCS_S_PACKED;

struct uct_mm_bcast_ep {
    uct_mm_coll_ep_t    super;
    uct_mm_fifo_check_t recv_check;
} UCS_S_PACKED;

struct uct_mm_incast_ep {
    uct_mm_coll_ep_t         super;
    uct_mm_base_incast_ext_t ext_reduce;
} UCS_S_PACKED;

UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_incast_ep_t, uct_mm_coll_ep_t, const uct_ep_params_t*);
UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_bcast_ep_t,  uct_mm_coll_ep_t, const uct_ep_params_t*);

UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_incast_ep_t, uct_ep_t);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_bcast_ep_t,  uct_ep_t);

ucs_status_t uct_mm_bcast_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p);
ucs_status_t uct_mm_incast_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p);
ucs_status_t uct_mm_coll_ep_flush(uct_ep_h tl_ep, unsigned flags, uct_completion_t *comp);
int uct_mm_coll_ep_is_connected(const uct_ep_h tl_ep, const uct_ep_is_connected_params_t *params);
void uct_mm_coll_ep_destroy(uct_ep_h ep);
void uct_mm_bcast_ep_destroy(uct_ep_h ep);

unsigned uct_mm_coll_ep_get_extra_index(unsigned op_count);
void uct_mm_coll_ep_init_incast_cb_arrays();

#endif
