/**
 * Copyright (c) UT-Battelle, LLC. 2014-2015. ALL RIGHTS RESERVED.
 * Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
 * Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCT_MM_COLL_IFACE_H
#define UCT_MM_COLL_IFACE_H

#include <ucs/sys/compiler.h>
#include <ucs/datastruct/ptr_array.h>
#include <uct/sm/mm/base/mm_iface.h>

/*
 * The following shared memory aggregation methods are supported: LOCKED,
 * ATOMIC, BATCHED and CENTRALIZED. These many-to-one communication methods also
 * apply to one-to-many communication, since acknowledgement is sent backwards.
 * The intent is to accommodate different kinds of constraints - resulting in
 * different performance profiles. For example, LOCKED should fit large buffers
 * reduced by a small amount of processes, but not for other cases.
 *
 * Basically, here's how the three work for reduce (1/2/3 are buffers from the
 * respective ranks, and 'p' stands for padding to cache-line size):
 *
 * 1. LOCKED mode, where the reduction is done by the sender:
 *
 *   | element->pending = 0 |             |
 *   | element->pending = 1 | 222         |
 *   | element->pending = 2 | 222+111     |
 *   | element->pending = 3 | 222+111+333 |
 *
 * 2. ATOMIC mode, same as LOCKED but using atomic operations to reduce:
 *
 *   | element->pending = 0 |             |
 *   | element->pending = 1 | 222         |
 *   | element->pending = 2 | 222+111     |
 *   | element->pending = 3 | 222+111+333 |
 *
 * 3.COUNTED_SLOTS mode, where buffers are written in separate cache-lines:
 *
 *   | element->pending = 0 |      |      |      |      |
 *   | element->pending = 1 |      |      | 222p |      |
 *   | element->pending = 2 |      | 111p | 222p |      |
 *   | element->pending = 3 |      | 111p | 222p | 333p |
 *
 * 4. FLAGGED_SLOTS mode, like counted slots but with root checking each slot:
 *
 *                                            Dummy slot, always empty
 *                                                    VVVVV
 *   | element->pending = 0 | ???-0 | ???-0 | ???-0 | ...-0 |
 *   | element->pending = 0 | ???-0 | 222-1 | ???-0 | ...-0 |
 *   | element->pending = 0 | 111-1 | 222-1 | ???-0 | ...-0 |
 *   | element->pending = 3 | 111-1 | 222-1 | 333-1 | ...-0 |
 *                        ^       ^       ^       ^
 *                        ^      #1      #2      #3  -> the last byte is polled
 *                        ^                             by the receiver process.
 *                        ^
 *                        the receiver process polls all these last bytes, and
 *                        once all the bytes have been set - the receiver knows
 *                        this operation is complete (none of the senders know).
 *
 * 5. COLLABORATIVE mode, like flagged slots but with some reduction by peers:
 *
 *                                            Dummy slot, always empty
 *                                                    VVVVV
 *   | element->pending = 0 | ???-0 | ???-0 | ???-0 | ...-0 |
 *   | element->pending = 0 | ???-0 | 222-1 | ???-0 | ...-0 |
 *   | element->pending = 0 | 111-2 | 222-0 | ???-0 | ...-0 | < #1 sets #2 to 0
 *   | element->pending = 3 | 111-0 | 222-0 | 333-1 | ...-0 |
 *                        ^       ^       ^       ^
 *                        ^      #1      #2      #3  -> the last byte is polled
 *                        ^                             by the receiver process.
 *                        ^
 *                        the receiver process polls all these last bytes, and
 *                        once all the bytes have been set - the receiver knows
 *                        this operation is complete (none of the senders know).
 *
 *   The CENTRALIZED algorithm is slightly more complicated than the rest:
 *   - Each writer N checks if writer N+1 has a non-zero counter:
 *       > If writer N+1 has counter=0 - then set writer N counter to 1
 *       > If writer N+1 has counter=X - then set writer N+1 counter to 0 and
 *         writer N counter to X+1.
 *   - When polling, the reader checks counters starting from element->pending:
 *       > If element->pending is X - start by checking the counter of writer X
 *       > If writer X counter is Y - skip to writer X+Y (and continue checking
 *         from there), and also set the counter of writer X to 0.
 *       > If writer X counter is 0 - set element->pending=X
 *
 *   *Note: if element->pending is X - it means that all the writers 0,1,...,X-1
 *          have finished, and their counters should be 0 (so that those are
 *          ready for the next usage of this element).
 *
 * What is the size of each slot?
 * - Short messages have data-length-based slot size:
 *       ucs_align_up(data_length + 1, UCS_SYS_CACHE_LINE_SIZE);
 * - Bcopy messages have a fixed slot size: the segment_size / num_senders
 *   This is because you need to pass the slot offset to the packer callback -
 *   before you know the length actually written.
 * - Zcopy messages are not supported yet...
 *
 * The text above mostly focuses on incast (many-to-one communication), but
 * broadcast is also supported. In the broadcast case, each receiver needs to
 * indicate completion - which turns into incast again. For broadcast, the
 * layout is slightly different, and all the completion flags are grouped at the
 * end of the element (short/long sends) or segment (medium sends):
 *
 * 4. CENTRALIZED mode, like "batched" but with root checking each slot:
 *                            No flag     Padding   Dummy slot, always empty
 *                               V           V       VVVVV
 *   | element->pending = 0 | 0000 | ...-0 | ...-0 | ...-0 |
 *   | element->pending = 0 | 0000 | ...-0 | ...-1 | ...-0 | < #2 ACK-s
 *                                   ^^^^^   ^^^^^   ^^^^^
 *                                   Each ACK flag is in a separate cache-line
 *
 * To summarize the differences:
 *
 * name | does the reduction |     mutual exclusion     | typically good for
 * ----------------------------------------------------------------------------
 * LOCKED      |   sender    | element access uses lock | large size
 * ----------------------------------------------------------------------------
 * ATOMIC      |   sender    | element access is atomic | imbalance + some ops
 * ----------------------------------------------------------------------------
 * BATCHED     |   receiver  | "pending" is atomic      | small size, low PPN
 * ----------------------------------------------------------------------------
 * CENTRALIZED |   receiver  | not mutually excluding   | small size, high PPN
 *
 */
enum uct_mm_coll_type {
#if HAVE_SM_COLL_EXTRA
    UCT_MM_COLL_TYPE_LOCKED,
    UCT_MM_COLL_TYPE_ATOMIC,
    UCT_MM_COLL_TYPE_HYPOTHETIC,
    UCT_MM_COLL_TYPE_COUNTED_SLOTS,
#endif
    UCT_MM_COLL_TYPE_FLAGGED_SLOTS,
    UCT_MM_COLL_TYPE_COLLABORATIVE,

    UCT_MM_COLL_TYPE_LAST
} UCS_S_PACKED;

typedef uct_reduction_internal_cb_t uct_mm_coll_iface_cb_t;

typedef struct uct_mm_base_incast_ext_cb {
    uct_reduction_external_cb_t cb;
    unsigned                    operand_count;
    size_t                      total_size;
} uct_mm_base_incast_ext_cb_t;

typedef struct uct_mm_base_incast_ext {
    uct_md_h                    md;
    void                       *operator;
    void                       *datatype;
    uct_mm_base_incast_ext_cb_t def, aux;
} uct_mm_base_incast_ext_t;

typedef struct uct_mm_coll_iface_cb_arg {
    uct_pack_callback_t       pack_cb;
    void                     *pack_cb_arg;
    uct_mm_base_incast_ext_t *rctx;
} UCS_S_PACKED uct_mm_coll_iface_cb_ctx_t;

typedef struct uct_mm_coll_iface_addr {
    uint32_t            coll_id;
    uct_mm_iface_addr_t super;
} UCS_S_PACKED uct_mm_coll_iface_addr_t;

typedef struct uct_mm_coll_fifo_element {
    uct_mm_fifo_element_t super;
    ucs_spinlock_t        lock;
    volatile uint32_t     pending;

    UCS_CACHELINE_PADDING(uct_mm_fifo_element_t,
                          ucs_spinlock_t,
                          uint32_t,
                          uint64_t);

    uint64_t              header;
} UCS_S_PACKED UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE) uct_mm_coll_fifo_element_t;

#define UCT_MM_COLL_NEXT_FIFO_ELEM(_obj, _elem, _index, _init, _size) \
    (ucs_unlikely(((_index)++ & (_obj)->fifo_mask) == (_obj)->fifo_mask)) ? \
            (_init) : UCS_PTR_BYTE_OFFSET((_elem), (_size))

#define UCT_MM_COLL_IFACE_GET_FIFO_ELEM(_iface, _index) \
   ucs_container_of(UCT_MM_IFACE_GET_FIFO_ELEM(&(_iface)->super, \
                    (_iface)->super.recv_fifo_elems, _index), \
                    uct_mm_coll_fifo_element_t, super)

#if ENABLE_DEBUG_DATA /* Reduce built-time for debug builds... */
#define UCT_MM_COLL_MAX_COUNT_SUPPORTED 2
#else
#define UCT_MM_COLL_MAX_COUNT_SUPPORTED 17
#endif

#define UCT_MM_COLL_TX_LENGTH_USAGE (2)
#define UCT_MM_COLL_OFFSET_ID_USAGE (2)
#define UCT_MM_COLL_TIMESTAMP_USAGE (2)
#define UCT_MM_COLL_CACHELINE_USAGE (2)

/**
 * To accelerate some instances of the transport, where the send/reduction size
 * is known in advance, these per-operator per-operand functions contain pre-defined
*/
extern uct_mm_coll_iface_cb_t uct_mm_coll_ep_memcpy_basic_callback_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED];

extern uct_mm_coll_iface_cb_t uct_mm_coll_ep_reduce_basic_callback_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED];

extern uct_mm_coll_iface_cb_t uct_mm_coll_ep_memcpy_extra_callback_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]; /* Note: in this case it's process count */

extern uct_mm_coll_iface_cb_t uct_mm_coll_ep_reduce_extra_callback_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]; /* Note: in this case it's process count */

/**
 * These are actual active-message short send functions to be called by UCP/UCG.
 */
extern typeof(uct_ep_am_short_func_t) uct_mm_incast_ep_am_short_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_OFFSET_ID_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]
    [UCT_MM_COLL_TYPE_LAST];

/**
 * These are actual active-message bcopy send functions to be called by UCP/UCG.
 */
extern typeof(uct_ep_am_bcopy_func_t) uct_mm_incast_ep_am_bcopy_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_OFFSET_ID_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]
    [UCT_MM_COLL_TYPE_LAST];

/**
 * These are actual progress functions to be called by UCP/UCG.
 */
extern typeof(uct_iface_progress_func_t) uct_mm_incast_iface_progress_short_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_OFFSET_ID_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]
    [UCT_MM_COLL_TYPE_LAST];

/**
 * These are actual progress functions to be called by UCP/UCG.
 */
extern typeof(uct_iface_progress_func_t) uct_mm_incast_iface_progress_bcopy_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_OFFSET_ID_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]
    [UCT_MM_COLL_TYPE_LAST];

typedef struct uct_mm_coll_ep uct_mm_coll_ep_t;
typedef struct uct_mm_bcast_ep uct_mm_bcast_ep_t;
typedef struct uct_mm_incast_ep uct_mm_incast_ep_t;

typedef struct uct_mm_coll_iface {
    uct_mm_base_iface_t   super;
    uint8_t               is_incast      :1;
    uint8_t               use_timestamps :1;
    enum uct_mm_coll_type type;           /**< Type of collective interface */
    uint8_t               my_coll_id;     /**< my (unique) index in the group */
    uint8_t               sm_proc_cnt;    /**< number of processes in the group */
    uint16_t              elem_slot_size; /**< Slot size for short messages */
    uint32_t              seg_slot_size;  /**< Slot size for bcopy messages */
    ucs_ptr_array_t       ep_ptrs;        /**< endpoints to other connections */
    uct_mm_coll_ep_t     *loopback_ep;    /**< endpoint connected to this iface */
} uct_mm_coll_iface_t;

typedef struct uct_mm_coll_iface_config {
    uct_mm_iface_config_t super;
} uct_mm_coll_iface_config_t;

struct uct_mm_bcast_iface_config {
    uct_mm_coll_iface_config_t super;
};

struct uct_mm_incast_iface_config {
    uct_mm_coll_iface_config_t super;
};

typedef struct uct_mm_base_bcast_iface {
    uct_mm_coll_iface_t super;
    uct_mm_bcast_ep_t  *last_nonzero_ep;
    uint32_t            poll_iface_idx;
    uint32_t            poll_ep_idx;
    uct_mm_bcast_ep_t  *dummy_ep;
} uct_mm_base_bcast_iface_t;

UCS_CLASS_DECLARE(uct_mm_coll_iface_t, uct_iface_ops_t*, uct_iface_internal_ops_t*,
                  uct_md_h, uct_worker_h, const uct_iface_params_t*,
                  const uct_iface_config_t*, int, enum uct_mm_coll_type,
                  uint16_t, uint32_t);

typedef struct uct_mm_base_incast_iface {
    uct_mm_coll_iface_t      super;
    uct_mm_incast_ep_t      *dummy;
    uct_mm_base_incast_ext_t ext_reduce;
} uct_mm_base_incast_iface_t;

#define UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_FULL(_name, _dir, _cl_suffix, _ts_suffix, _id_suffix, _len_suffix, _ext_suffix) \
ucs_status_t \
uct_mm_##_dir##_ep_am_short##_ext_suffix##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_name \
    (uct_ep_h ep, uint8_t id, uint64_t header, const void *payload, unsigned length); \
\
ssize_t \
uct_mm_##_dir##_ep_am_bcopy##_ext_suffix##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_name \
    (uct_ep_h ep, uint8_t id, uct_pack_callback_t pack_cb, void *arg, unsigned flags); \
\
unsigned \
uct_mm_##_dir##_iface_progress##_ext_suffix##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_name \
    (uct_iface_h tl_iface);

#define UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_LEN(_name, _dir, _cl_suffix, _ts_suffix, _id_suffix, _len_suffix) \
          UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_FULL(_name, _dir, _cl_suffix, _ts_suffix, _id_suffix, _len_suffix, ) \
          UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_FULL(_name, _dir, _cl_suffix, _ts_suffix, _id_suffix, _len_suffix, _ext_cb)

#define UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_ID(_name, _dir, _cl_suffix, _ts_suffix, _id_suffix) \
       UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_LEN(_name, _dir, _cl_suffix, _ts_suffix, _id_suffix, _len_zero) \
       UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_LEN(_name, _dir, _cl_suffix, _ts_suffix, _id_suffix, _len_nonzero)

#define UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_TS(_name, _dir, _cl_suffix, _ts_suffix) \
        UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_ID(_name, _dir, _cl_suffix, _ts_suffix, _id_zero) \
        UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_ID(_name, _dir, _cl_suffix, _ts_suffix, _id_nonzero)

#define UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_CL(_name, _dir, _cl_suffix) \
        UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_TS(_name, _dir, _cl_suffix, _ts) \
        UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_TS(_name, _dir, _cl_suffix, _no_ts)

#define UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_DIR(_name, _dir) \
         UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_CL(_name, _dir, _cl) \
         UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_CL(_name, _dir, _no_cl)

#define    UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE(_name) \
    UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_DIR(_name, bcast) \
    UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE_BY_DIR(_name, incast)

#define MM_COLL_IFACE_DEFINE(_name) \
typedef struct uct_mm_##_name##_bcast_iface { \
    uct_mm_base_bcast_iface_t super; \
    uct_mm_bcast_ep_t        *last_nonzero_ep; \
    unsigned                  poll_ep_idx; \
} uct_mm_##_name##_bcast_iface_t; \
\
typedef struct uct_mm_##_name##_incast_iface { \
    uct_mm_base_incast_iface_t super; \
} uct_mm_##_name##_incast_iface_t; \
\
UCS_CLASS_DECLARE(uct_mm_##_name##_bcast_iface_t, uct_md_h, uct_worker_h, \
                  const uct_iface_params_t*, const uct_iface_config_t*); \
UCS_CLASS_DECLARE(uct_mm_##_name##_incast_iface_t, uct_md_h, uct_worker_h, \
                  const uct_iface_params_t*, const uct_iface_config_t*); \
UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_##_name##_bcast_iface_t, uct_iface_t, \
                           uct_md_h, uct_worker_h, const uct_iface_params_t*, \
                           const uct_iface_config_t*); \
UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_##_name##_incast_iface_t, uct_iface_t, \
                           uct_md_h, uct_worker_h, const uct_iface_params_t*, \
                           const uct_iface_config_t*); \
UCT_MM_COLL_IFACE_PROGRESS_FUNCS_DEFINE(_name)

#if HAVE_SM_COLL_EXTRA
MM_COLL_IFACE_DEFINE(locked)
MM_COLL_IFACE_DEFINE(atomic)
MM_COLL_IFACE_DEFINE(hypothetic)
MM_COLL_IFACE_DEFINE(counted_slots)
#endif
MM_COLL_IFACE_DEFINE(flagged_slots)
MM_COLL_IFACE_DEFINE(collaborative)

ucs_status_t uct_mm_coll_iface_query(uct_iface_h tl_iface,
                                     uct_iface_attr_t *iface_attr);

ucs_status_t uct_mm_coll_iface_get_address(uct_iface_t *tl_iface,
                                           uct_iface_addr_t *addr);

int uct_mm_coll_iface_is_reachable_v2(const uct_iface_h tl_iface,
                                      const uct_iface_is_reachable_params_t *params);

void uct_mm_coll_ep_imbalanced_reset_incast_desc(uct_mm_coll_fifo_element_t* elem);

ucs_status_t uct_mm_coll_iface_query_empty(uct_iface_h tl_iface,
                                           uct_iface_attr_t *iface_attr);

ucs_status_t mm_coll_iface_elems_prepare(uct_mm_coll_iface_t *iface,
                                         enum uct_mm_coll_type type,
                                         int for_termination);

#endif
