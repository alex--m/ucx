/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_OVER_UCT_H_
#define UCG_OVER_UCT_H_

#include <ucp/dt/dt.inl>
#include <ucg/over_ucp/over_ucp.h>

typedef struct ucg_over_uct_config {
    ucg_over_ucp_config_t super;
    int                   is_dt_volatile;
    unsigned              incast_member_thresh;
    unsigned              bcast_member_thresh;
    size_t                zcopy_rkey_thresh;
    size_t                zcopy_total_thresh;
    unsigned              bcopy_to_zcopy_opt;
    unsigned              mem_reg_opt_cnt;
    double                resend_timer_tick;
#ifdef ENABLE_FAULT_TOLERANCE
    double                ft_timer_tick;
#endif
} ucg_over_uct_config_t;

typedef struct ucg_over_uct_ctx {
    ucg_over_ucp_ctx_t     super;
    uint8_t                coll_am_id;
    uint8_t                wireup_am_id;
    ucg_over_uct_config_t  config;

    /**
     * When a message arrives as part of a collective operation - it contains a
     * group ID to indicate the UCG group (and inside it - the UCP worker) it
     * belongs to. Since the groups are created in parallel on different hosts,
     * it is possible a message arrives before the corresponding group has been
     * created - it must then be allocated from this temporary, "dummy" worker
     * and stored in this "unexpected" message array until the matching group is
     * created (and grabs it from here).
     */
    ucg_group_h            dummy_group;
    ucs_ptr_array_locked_t unexpected;

    /* If set, every barrier is delayed - for benchmarking purposes */
    int                    is_barrier_delayed;
} ucg_over_uct_ctx_t;

ucs_status_t ucg_over_uct_init(ucg_comp_ctx_h cctx,
                               const ucs_pmodule_component_params_t *params,
                               ucg_comp_config_t *config);

void ucg_over_uct_finalize(ucg_comp_ctx_h cctx);

ucs_status_t ucg_over_uct_create(ucg_comp_ctx_h cctx, ucg_group_ctx_h ctx,
                                 ucs_pmodule_target_t *target,
                                 const ucs_pmodule_target_params_t *params);

void ucg_over_uct_destroy(ucg_group_ctx_h ctx);

/******************************************************************************
 *                                                                            *
 *                             Handling RX Messages                           *
 *                                                                            *
 ******************************************************************************/

enum ucg_over_uct_plan_phase_send_methods {
    UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_NONE = 0,
    UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_SHORT,
    UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_BCOPY,
    UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_AM_ZCOPY,
    /* Note: AM_ZCOPY is like BCOPY with registered memory: it uses "eager"
     *       protocol, as opposed to PUT/GET (below) which use "rendezvous" */
    UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_PUT_ZCOPY,
    UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_GET_ZCOPY,
};

enum ucg_over_uct_plan_phase_flags {
    /* General characteristics */
    UCG_OVER_UCT_PLAN_PHASE_FLAG_LAST_STEP         = UCS_BIT(0),
    UCG_OVER_UCT_PLAN_PHASE_FLAG_SINGLE_ENDPOINT   = UCS_BIT(1),
    UCG_OVER_UCT_PLAN_PHASE_FLAG_SEND_STRIDED      = UCS_BIT(2),
    UCG_OVER_UCT_PLAN_PHASE_FLAG_SEND_VARIADIC     = UCS_BIT(3),
    UCG_OVER_UCT_PLAN_PHASE_FLAG_PIPELINED         = UCS_BIT(4),
    UCG_OVER_UCT_PLAN_PHASE_FLAG_BCAST             = UCS_BIT(5),

    /* Alternative Send types */
    UCG_OVER_UCT_PLAN_PHASE_FLAG_FRAGMENTED        = UCS_BIT(6),
#define UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS (7)
#define UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_MASK_BITS   (3)
    UCG_OVER_UCT_PLAN_PHASE_FLAG_SWITCH_MASK       = UCS_MASK(10),

    /* This flag indicates whether incoming messages are expected */
    UCG_OVER_UCT_PLAN_PHASE_FLAG_EXPECTS_RX     = UCS_BIT(10),

    /* The last UCT_COLL_LENGTH_INFO_BITS are used for datatype-related flags */
#define UCG_OVER_UCT_PLAN_PHASE_LENGTH_INFO_SHIFT (11)
#define UCG_OVER_UCT_PLAN_PHASE_LENGTH_INFO_MASK  (UCS_MASK(2))

    /*
     * Next come the 2-bit length information from uct_coll_length_info_t,
     * possibly followed by the first two bits from enum uct_msg_flags (when
     * bcopy is used). For short transmissions, length information is passed as
     * part of the length variable.
     */
};

static UCS_F_ALWAYS_INLINE enum ucg_over_uct_plan_phase_send_methods
ucg_over_uct_plan_phase_flags_get_method(uint16_t flags) {
    return (flags >> UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_OFFSET_BITS) &
                     UCG_OVER_UCT_PLAN_PHASE_SEND_METHOD_MASK_BITS;
}

static UCS_F_ALWAYS_INLINE enum uct_coll_length_info
ucg_over_uct_plan_phase_flags_get_length_info(uint16_t flags) {
    return (flags >> UCG_OVER_UCT_PLAN_PHASE_LENGTH_INFO_SHIFT) &
           UCG_OVER_UCT_PLAN_PHASE_LENGTH_INFO_MASK;
}

enum ucg_over_uct_plan_phase_comp_flags {
    UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_SLOT_LENGTH      = UCS_BIT(0),
    UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_FRAGMENTED_DATA  = UCS_BIT(1),
    UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_GENERIC_DATATYPE = UCS_BIT(2),
    UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_IMBALANCE_INFO   = UCS_BIT(3),
}; /* Note: only 4 bits are allocated for this field in ucg_over_uct_plan_phase_t */

enum ucg_over_uct_plan_phase_comp_aggregation {
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_NOP = 0,

    /* Aggregation of short (Active-)messages */
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_NO_OFFSET,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_WITH_OFFSET,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_PIPELINE,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_GATHER,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_INTERNAL,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_EXTERNAL,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_RESERVED,

    /* Unpacking remote memory keys (for Rendezvous), can be OR-ed with 0-7 */
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY = UCS_BIT(3),

    /* A combination of prev. values with a key - used for jump-table support */
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_NO_OFFSET_RKEY,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_WITH_OFFSET_RKEY,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_PIPELINE_RKEY,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_GATHER_RKEY,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_INTERNAL_RKEY,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_EXTERNAL_RKEY,
    UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_RESERVED_RKEY
}; /* Note: only 4 bits are allocated for this field in ucg_over_uct_plan_phase_t */

enum ucg_over_uct_plan_phase_comp_criteria {
    UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_SINGLE_MESSAGE = 0,
    UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES,
    UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES_ZCOPY
}; /* Note: only 2 bits are allocated for this field in ucg_over_uct_plan_phase_t */

enum ucg_over_uct_plan_phase_comp_action {
    UCG_OVER_UCT_PLAN_PHASE_COMP_OP   = 0,
    UCG_OVER_UCT_PLAN_PHASE_COMP_STEP = 1,
    UCG_OVER_UCT_PLAN_PHASE_COMP_SEND = 2 /* can be OR-ed with 0/1 */
}; /* Note: only 2 bits are allocated for this field in ucg_over_uct_plan_phase_t */


/******************************************************************************
 *                                                                            *
 *                               Sending Messages                             *
 *                                                                            *
 ******************************************************************************/

typedef union ucg_over_uct_header_step {
    struct {
        ucg_coll_id_t  coll_id;
        ucg_step_idx_t step_idx;
    };
    uint16_t local_id;
} ucg_over_uct_header_step_t;

typedef union ucg_over_uct_header {
    struct {
        ucg_offset_t remote_offset;
        ucg_group_id_t group_id;
        ucg_over_uct_header_step_t msg;
    };
    uint64_t header;
} ucg_over_uct_header_t;

enum ucg_over_uct_plan_flags {
    UCG_OVER_UCT_PLAN_FLAG_BARRIER          = UCS_BIT(0),
    UCG_OVER_UCT_PLAN_FLAG_PIPELINED        = UCS_BIT(1),
    UCG_OVER_UCT_PLAN_FLAG_OPTIMIZE_CB      = UCS_BIT(2),
    UCG_OVER_UCT_PLAN_FLAG_COPY_SRC_BUFFER  = UCS_BIT(3),
    UCG_OVER_UCT_PLAN_FLAG_COPY_SRC_OFFSET  = UCS_BIT(4),
    UCG_OVER_UCT_PLAN_FLAG_COPY_DST_OFFSET  = UCS_BIT(5),
    UCG_OVER_UCT_PLAN_FLAG_SEND_PACK        = UCS_BIT(6),
    UCG_OVER_UCT_PLAN_FLAG_RECV_UNPACK      = UCS_BIT(7),
    UCG_OVER_UCT_PLAN_FLAG_VOLATILE_DT      = UCS_BIT(8),
    UCG_OVER_UCT_PLAN_FLAG_BARRIER_DELAY    = UCS_BIT(9),
    UCG_OVER_UCT_PLAN_FLAG_IMBALANCE_INFO   = UCS_BIT(10),
    UCG_OVER_UCT_PLAN_FLAG_ASYNC_COMPLETION = UCS_BIT(11),

    UCG_OVER_UCT_PLAN_FLAG_SWITCH_MASK     = UCS_MASK(12)
};

/* Definitions of several callback functions, used during an operation */
typedef struct ucg_over_uct_plan ucg_over_uct_plan_t;
typedef struct ucg_over_uct_group_ctx ucg_over_uct_group_ctx_t;
typedef struct ucg_over_uct_plan_phase ucg_over_uct_plan_phase_t;
typedef ucs_status_t (*ucg_over_uct_plan_optm_cb_t)(ucg_over_uct_plan_t *plan,
                                                    ucg_over_uct_plan_phase_t *phase,
                                                    const ucg_collective_params_t *params);

enum ucg_over_uct_phase_extra_info_slots {
    UCG_OVER_UCT_PHASE_EXTRA_INFO_RX = 0,
    UCG_OVER_UCT_PHASE_EXTRA_INFO_TX
};

typedef struct ucg_over_uct_phase_extra_info {
    int                         is_coll_tl_used;
    unsigned                    coll_iface_base; /**< First UCT interface ID */
    ucp_tl_bitmap_t             coll_iface_bits; /**< UCT interfaces used */
    uct_md_h                    md;              /**< for zcopy registration */
    uct_mem_h                   aux_memh;        /**< rkey (temp buffer) handle */
    size_t                      max_bcopy;       /**< for rkey fragmentation */
    void                       *temp_buffer;     /**< to release during cleanup */
} ucg_over_uct_phase_extra_info_t;

struct ucg_over_uct_plan_phase {
    uint16_t                        flags; /** @ref enum ucg_over_uct_plan_phase_flags */

    struct {
        union {
            struct {
                enum ucg_over_uct_plan_phase_comp_flags       comp_flags    :4;
                enum ucg_over_uct_plan_phase_comp_aggregation comp_agg      :4;
            };
            uint8_t comp_switch;
        };

        union {
            struct {
                enum ucg_over_uct_plan_phase_comp_criteria    comp_criteria :2;
                enum ucg_over_uct_plan_phase_comp_action      comp_action   :2;
                uint8_t                                       reserved      :4;
            };
            uint8_t comp_misc;
        };

        uint32_t                    frags_cnt; /**< Total amount of fragments */
        uct_iface_h                 iface;     /**< RX interface object */
        uint8_t                     *buffer;
        uct_reduction_internal_cb_t reduce_f;  /**< Reduction callback */
        uint16_t                    batch_len; /**< Data size in a single batch */
        uint16_t                    batch_cnt;
        ucg_step_idx_t              step_idx;  /**< RX step index to expect */
    } rx;

    struct {
        uint8_t                     am_id;     /**< collectives (UCG) AM ID */
        uint16_t                    ep_cnt;    /**< number of (multi-)endpoints */
        union {
            uct_ep_h                *multi_eps; /**< endpoint pointer array */
            uct_ep_h                single_ep;  /**< single endpoint handle */
        };
        ucg_over_uct_header_t       am_header;  /**< initial header value */
        uint8_t                     *buffer;    /**< points to the sent data */

        /* --- 64 bytes --- */

        size_t                      length;     /**< sent to each destination */
        void                        *send;      /**< send function pointer */
        uct_iface_h                 iface;      /**< TX interface object */
        uint32_t                    frag_len;   /**< for fragmented operations */
        ucg_group_member_index_t    root;       /**< phase root, if applicable */

        /* Send-type-specific fields */
        union {
            struct {
                uct_pack_callback_t pack_single_cb;
                uct_mem_h           rkey_memh;
            } bcopy_single;
            struct {
                uct_pack_callback_t pack_full_cb;
                uct_pack_callback_t pack_part_cb;
            } bcopy_fragmented;
            struct {
                uct_mem_h           memh;    /**< data buffer memory handle */
            } zcopy;
            struct {
                const uct_iface_attr_t *iface_attr;
            } mock;
        };
    } tx;

    union {
        ucg_over_uct_plan_optm_cb_t optm_cb;    /**< run-time optimization */
        void                        *reserved;  /**< to be used by subclasses */
    };

    /* This points to an array of two extra info slots: for RX and for TX */
    ucg_over_uct_phase_extra_info_t *info;      /**< collective transport info */
    /* --- 128 bytes --- */
} UCS_S_PACKED UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);

struct ucg_over_uct_plan {
    ucg_over_ucp_plan_t       super;
    uint16_t                  op_flags; /**< from @ref enum ucg_over_uct_plan_flags */
    uint16_t                  opt_cnt;  /**< optimization count-down */
    ucg_group_member_index_t  my_index; /**< for operations calculating offsets */
    ucp_datatype_t            send_dt;  /**< generic send datatype (if non-contig) */
    ucp_datatype_t            recv_dt;  /**< receive datatype (non-contig or reduction) */
    uint8_t                  *copy_dst; /**< destination to copy the input buffer */
    size_t                    max_frag; /**< maximal fragment number across phases */
#if ENABLE_DEBUG_DATA
    ucg_topo_desc_t          *topo_desc;
    ucg_over_ucp_plan_t      *ucp_plan;
#endif
    ucg_over_uct_plan_phase_t phss[0];  /**< the phases of the collective plan */
} UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);

ucs_status_t ucg_over_uct_plan_estimate(ucg_group_ctx_h ctx,
                                       ucs_pmodule_target_action_params_h params,
                                       double *estimate_p);

ucs_status_t ucg_over_uct_plan_create(ucg_over_uct_group_ctx_t *gctx,
                                      const ucg_collective_params_t *coll_params,
                                      ucg_over_uct_plan_t **plan_p);

ucs_status_t ucg_over_uct_plan_wrapper(ucg_group_ctx_h ctx,
                                       ucs_pmodule_target_action_params_h params,
                                       ucs_pmodule_target_plan_t **plan_p);

ucs_status_t ucg_over_uct_phase_create(ucg_over_uct_plan_t *plan,
                                       ucg_over_uct_plan_phase_t *phase,
                                       const ucg_topo_desc_step_t *step,
                                       const ucg_over_ucp_plan_phase_t *super,
                                       const ucg_over_uct_config_t *config,
                                       const ucg_over_ucp_plan_dt_info_t *rx_dt,
                                       const ucg_over_ucp_plan_dt_info_t *tx_dt,
                                       const ucg_collective_params_t *params,
                                       int8_t **current_data_buffer,
                                       int *requires_optimization_p,
                                       uct_ep_h **ep_base_p,
                                       uint8_t am_id);

void ucg_over_uct_phase_set_temp_buffer(ucg_over_uct_plan_phase_t *phase, void* buffer,
                                        enum ucg_over_uct_phase_extra_info_slots side);

ucs_status_t over_uct_phase_rkey_pair_use(void **src_ptr, uct_rkey_bundle_t *src_rkey_p,
                                          void **dst_ptr, uct_rkey_bundle_t *dst_rkey_p,
                                          uct_md_h md, uct_component_h *component_p,
                                          size_t *len_p);

void ucg_over_uct_phase_rkey_pair_discard(uct_rkey_bundle_t *src_rkey_p,
                                          uct_rkey_bundle_t *dst_rkey_p,
                                          uct_component_h component);

void ucg_over_uct_phase_destroy(ucg_over_uct_plan_phase_t *phase,
                                const ucg_collective_params_t *params,
                                ucg_group_h group, int is_mock);

void ucg_over_uct_plan_destroy(ucg_over_uct_plan_t *plan);

void ucg_over_uct_plan_discard(ucs_pmodule_target_plan_t *plan);

void ucg_over_uct_plan_print(ucs_pmodule_target_plan_t *plan);

ucs_status_t ucg_over_uct_pack_select_cb(ucg_over_uct_plan_phase_t *phase,
                                         const ucg_collective_params_t *params,
                                         const ucg_over_ucp_plan_dt_info_t *dt,
                                         const uct_iface_attr_t *iface_attr,
                                         int is_fragmented);

uct_pack_callback_t ucg_over_uct_pack_upgrade_to_use_rkey(uct_pack_callback_t pack_cb,
                                                          int packer_should_reduce);

void ucg_over_uct_pack_check_internal_reduce(ucg_over_uct_plan_phase_t *phase,
                                             int is_fragmented);

ucs_status_t ucg_over_uct_optimize_plan(ucg_over_uct_plan_phase_t *phase,
                                        const ucg_over_uct_config_t *config,
                                        const ucg_collective_params_t *params,
                                        uint16_t rx_send_flags,
                                        const uct_iface_attr_t *rx_iface_attr,
                                        const uct_iface_attr_t *tx_iface_attr,
                                        uint16_t *opt_cnt_p);

ucs_status_t ucg_over_uct_optimize_now(ucg_over_uct_plan_t *plan,
                                       const ucg_collective_params_t *params);

/******************************************************************************
 *                                                                            *
 *                             Operation Execution                            *
 *                                                                            *
 ******************************************************************************/

typedef struct ucg_over_uct_rkey_msg {
    uintptr_t remote_address;
    size_t    remote_length;

#if ENABLE_ASSERT
#define RKEY_MAGIC (0x1337)
    ucg_group_id_t magic;
#endif

    uint8_t   packed_rkey[];
    // TODO: add a "return-rkey" for responses in cases like Allreduce
} ucg_over_uct_rkey_msg_t;

typedef struct ucg_over_uct_op ucg_over_uct_op_t;

struct ucg_over_uct_op {
    ucg_op_t                  super;
    uint16_t                  iter_ep;      /**< iterator over TX endpoints */
#define UCG_OVER_UCT_FRAG_PIPELINE_READY   ((uint16_t)-1)
#define UCG_OVER_UCT_FRAG_PIPELINE_PENDING ((uint16_t)-2)
    uint16_t                  iter_frag;    /**< iterator over TX fragments */

    /* --- 32 bytes --- */

    ucg_over_uct_plan_phase_t *phase;       /**< points to the current phase */
    uct_completion_t          comp;         /**< RX/zcopy completion counter */

    /*ucg_over_uct_header_step_t expected => inside comp->reserved */
#define UCG_OVER_UCT_OP_EXPECTED(_op) \
    ((ucg_over_uct_header_step_t*)&(_op)->comp.reserved)

    /* To enable pipelining of fragmented messages, each fragment has a counter,
     * similar to the request's overall "pending" counter. Once it reaches zero,
     * the fragment can be "forwarded" regardless of the other fragments. */
#define UCG_OVER_UCT_FRAG_PENDING ((uint8_t)-1)
    uint8_t                   *fragment_pending;

    /* --- 64 bytes --- */

    uct_iface_progress_func_t progress_f;   /**< current progress function */
    uct_iface_h               iface;        /**< current progress interface */
    ucp_dt_state_t            *send_pack;   /**< send datatype - pack state */
    ucp_dt_state_t            *recv_unpack; /**< recv datatype - unpack state */
    ucs_list_link_t           resend_list;  /**< membership in resend queue */
    size_t                    frags_allocd; /**< prev. fragment allocation */
    double                    first_timestamp; /**< to compute the imbalance */

    /* --- 128 bytes --- */
} UCS_S_PACKED UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);

/*
 * Incoming messages are processed for one of the collective operations
 * currently outstanding - arranged as a window (think: TCP) of slots.
 */
typedef struct ucg_over_uct_comp_slot {
    ucg_over_uct_op_t      op;
#if ENABLE_MT
    ucs_ptr_array_locked_t messages;
#else
    ucs_ptr_array_t        messages;
#endif
} UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE) ucg_over_uct_comp_slot_t;

/*
 * This number sets the number of slots available for collective operations.
 * Note that this includes collective operations which have started on other
 * members of the group, so any number of "one-way collectives" (e.g. reduction)
 * could be inbound at any given time and each must be allocated a slot. In
 * practice, it's rare that a high number is required here, and exceeding
 * operations could fallback to another component, e.g. UCG over UCP.
 *
 * Each operation occupies a slot, so no more than this number of collectives
 * can take place at the same time. The slot is determined by the collective
 * operation id (ucg_coll_id_t) - modulo this constant. Translating "coll_id"
 * to slot# happens on every incoming packet, so this constant is best kept
 * determinable at compile time, and set to a power of 2 (<= 64, to fit into
 * the resend bit-field). Lower numbers reduce the memory footprint and latency.
 */
#define UCG_OVER_UCT_MAX_CONCURRENT_OPS (8)

struct ucg_over_uct_group_ctx {
    ucg_over_ucp_group_ctx_t super;

    UCS_CACHELINE_PADDING(ucg_over_ucp_group_ctx_t);

    /*
     * The following is the key structure of a group - an array of outstanding
     * collective operations, one slot per operation. Messages for future ops
     * may be stored in a slot before the operation actually starts.
     */
    ucg_over_uct_comp_slot_t slots[UCG_OVER_UCT_MAX_CONCURRENT_OPS];


    /* From here on - only for control-path */

    ucs_list_link_t          resend_head; /**< List of actions to resend */
    ucs_recursive_spinlock_t resend_lock; /**< A Lock protecting resend lists */
    int                      timer_id;    /**< Async. progress timer ID */
#ifdef ENABLE_FAULT_TOLERANCE
    int                      ft_timer_id; /**< Fault-tolerance timer ID */
#endif

    /**
     * This endpoint holds the "master" QP for the Core-Direct API. The endpoint
     * is used when @ref ucg_plan_connect gets UCG_PLAN_CONNECT_FLAG_CORE_DIRECT
     * as one of the connection flags.
     */
    uct_ep_h                 coredirect_master;
} UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);


ucs_status_ptr_t ucg_over_uct_execute_op(ucg_over_uct_op_t *op);

ucs_status_ptr_t ucg_over_uct_plan_trigger(ucs_pmodule_target_plan_t *plan,
                                           uint16_t id, void *request);

unsigned ucg_over_uct_plan_progress(ucs_pmodule_target_action_t *action);

void ucg_over_ucp_plan_discard(ucs_pmodule_target_plan_t *plan);

void ucg_over_uct_schedule_resend(ucg_over_uct_op_t *op);

void ucg_over_uct_cancel_resend(ucg_over_uct_op_t *op);

void ucg_over_uct_async_resend(int id, ucs_event_set_types_t e, void *c);

/*
 * Callback functions exported for debugging
 */
void ucg_over_uct_print_pack_cb_name(uct_pack_callback_t pack_single_cb);

/*
 * Macros to generate the headers of all bcopy packing callback functions.
 */
typedef ssize_t (*packed_send_t)(uct_ep_h, uint8_t, uct_pack_callback_t, void*,
                                 unsigned);

static UCS_F_ALWAYS_INLINE void
ucg_over_uct_set_header(const ucg_over_uct_op_t *op,
                        const ucg_over_uct_plan_phase_t *phase,
                        int add_frag_offset, ucg_over_uct_header_t *header_p)
{
    header_p->header      = phase->tx.am_header.header;
    header_p->msg.coll_id = UCG_OVER_UCT_OP_EXPECTED(op)->coll_id;

    if (add_frag_offset) {
        header_p->remote_offset += op->iter_frag * phase->tx.frag_len;
    }
}

#endif
