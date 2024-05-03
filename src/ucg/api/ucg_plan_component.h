/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_PLAN_COMPONENT_H_
#define UCG_PLAN_COMPONENT_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ucg/api/ucg.h>
#include <uct/api/v2/uct_v2.h>
#include <ucp/core/ucp_types.h>
#include <ucs/pmodule/component.h>
#include <ucs/datastruct/int_array.h>
#include <ucs/datastruct/ptr_array.h>

#define UCG_GROUP_FIRST_GROUP_ID (1)
#define UCG_GROUP_FIRST_COLL_ID  (1)
#define UCG_GROUP_FIRST_STEP_IDX (1)

BEGIN_C_DECLS

typedef uint8_t                        ucg_coll_id_t;  /* cyclic */
typedef uint8_t                        ucg_step_idx_t;
typedef uint32_t                       ucg_offset_t;
typedef ucs_pmodule_target_action_t    ucg_op_t;
typedef void                           ucg_comp_config_t;
typedef ucs_pmodule_component_ctx_h    ucg_comp_ctx_h;
typedef void*                          ucg_group_ctx_h;

extern ucg_params_t ucg_global_params;

enum ucg_collective_internal_modifiers { /* on @ref ucg_collective_modifiers */
    UCG_GROUP_COLLECTIVE_INTERNAL_MODIFIER_P2P_INSTEAD_OF_BCAST = UCS_BIT(15)
};

typedef enum ucg_topo_type {
    UCG_TOPO_TYPE_NONE,
    UCG_TOPO_TYPE_KARY_TREE,
    UCG_TOPO_TYPE_KNOMIAL_TREE,
    UCG_TOPO_TYPE_RING,
    UCG_TOPO_TYPE_BRUCK,
    UCG_TOPO_TYPE_RECURSIVE,
    UCG_TOPO_TYPE_PAIRWISE,
    UCG_TOPO_TYPE_NEIGHBORS
} ucg_topo_type_t;

enum ucg_topo_flags {
    UCG_TOPO_FLAG_TREE_FANIN    = UCS_BIT(0),
    UCG_TOPO_FLAG_TREE_FANOUT   = UCS_BIT(1),
    UCG_TOPO_FLAG_FULL_EXCHANGE = UCS_BIT(0) | UCS_BIT(1),
    UCG_TOPO_FLAG_RING_SINGLE   = UCS_BIT(2)
};

typedef struct ucg_topo_params {
    ucg_group_member_index_t me;               /* my index within the group */
    ucg_group_member_index_t total;            /* group member count */
    ucg_group_member_index_t root;             /* for ops with distinct root */
    ucg_group_member_index_t multiroot_thresh; /* max members for multiroot */
    unsigned                 flags;            /* @ref ucg_topo_flags */
    void                     *cb_ctx;          /* for neighborhood info cb */

    /*
     * Note: both 'me' and 'root' will likely be included in several ranges
     *       as listed below. For example, the process of the first core of
     *       the first CPU on a host will be included in both the levels for
     *       UCG_GROUP_MEMBER_DISTANCE_CORE, UCG_GROUP_MEMBER_DISTANCE_SOCKET,
     *       and possible others as well (if multiple hosts are employed).
     */

    struct ucg_topo_params_by_level {
        ucg_group_member_index_t first;  /* first member in this distance-level */
        ucg_group_member_index_t stride; /* for round-robin process placements */
        unsigned                 count;  /* total members on this level */
        ucg_topo_type_t          type;   /* the type of topology to be applied */

        /*
         * Message size is hardly topology-related information, but it may be
         * useful for choosing the communication pattern. For example, a gather
         * operation may have increasing sizes as the data is transferred closer
         * to the root of the collective operation.
         */
        size_t                   rx_msg_size;
        size_t                   tx_msg_size;

        /* type-dependant information */
        union {
            unsigned tree_radix;         /* for K-ary and K-nomial trees */
            unsigned recursive_factor;   /* for recursive K-ing and bruck */
        };
    } by_level[UCG_GROUP_MEMBER_DISTANCE_UNKNOWN];
} ucg_topo_params_t;

enum ucg_topo_desc_step_flags {
    UCG_TOPO_DESC_STEP_FLAG_RX_VALID            = UCS_BIT(0),
    UCG_TOPO_DESC_STEP_FLAG_TX_VALID            = UCS_BIT(1),

    /**
     * Leadership means that there is one leader member on each level, who is
     * responsible for coordination - for example the creation of collective
     * transports. If this member is that leader (for RX or TX) - the first pair
     * of flags is used. In steps with no leadership at all, such as in
     * recursive topologies, the second pair of flags is used.
     */
    UCG_TOPO_DESC_STEP_FLAG_RX_LEADER           = UCS_BIT(2),
    UCG_TOPO_DESC_STEP_FLAG_TX_LEADER           = UCS_BIT(3),
    UCG_TOPO_DESC_STEP_FLAG_RX_NO_LEADERSHIP    = UCS_BIT(4),
    UCG_TOPO_DESC_STEP_FLAG_TX_NO_LEADERSHIP    = UCS_BIT(5),

    /**
     * This flag distiguishes between two RX scenarios, with respect to a "team"
     * of <count> members performing the RX portion of this step:
     *   0: one team member sends a message to every member of the team (fanout)
     *   1: every peer in the team sends a message to me (e.g. fanin, multiroot)
     * Note that in the '1' case the count is needed to decide whether to
     * to anticipate a broadcast or a P2P transport (see also next comment).
     */
     UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER = UCS_BIT(6)
};

typedef struct ucg_topo_desc_step {
    unsigned flags; /*< @ref enum ucg_topo_desc_step_flags */

    struct {
        ucg_group_member_index_t       index;
        ucg_group_member_index_t       count;
        size_t                         msg_size;
        ucg_step_idx_t                 step_idx;
        enum ucg_group_member_distance distance;

        union {
             /* List of P2P TX destinations */
            ucs_int_array_t tx_send_dests;

            /**
             * In general, (UCT) connections are required only for TX, whereas
             * RX simply gets delivered to the handler regardless. There are two
             * good reasons to connect to a remote peer which sends to this one:
             *
             * 1. shared-memory broadcasts, where the sender posts to his own
             *    queue for every peer to read. Unless this connection is
             *    explicitly established - this process will not poll the
             *    relevant broadcast queue and never get the message.
             * 2. In order to detect the capabilities (e.g. MTU) of the
             *    transport used for RX in this step, connecting to any peer
             *    would do. There are some assumptions here, incl. that all
             *    sources send over the same transport and that A->B transport\
             *    is the same as B->A.
             */
            ucg_group_member_index_t rx_peer;
        };
    } rx, tx;

    /**
     * If either RX or TX collective transports are potentially used, and this
     * process is the root of this distance-level, the array would store the
     * all the members on this level (i.e. within the same distance). During
     * connection establishment, every member in this array would receive the
     * address of this process, so that collective transports could be employed.
     * This array would include this process, so it could determine its own
     * (relative) index within this level.
     */
    ucs_int_array_t level_members;

    /**
     * This is not necessarily the root for the entire collective operation, but
     * rather the root for this step, for example the leader for this host in a
     * multi-host collective. This root would be responsible for the wireup of
     * collective transports.
     */
    ucg_group_member_index_t root;
} ucg_topo_desc_step_t;

typedef struct ucg_topo_desc {
    ucs_ptr_array_t steps; /* @ref ucg_topo_desc_step_t */
} ucg_topo_desc_t;


/**
 * @ingroup UCG_RESOURCE
 * @brief Collectives' estimation of latency.
 *
 * This structure describes optional information which could be used
 * to select the best planner for a given collective operation..
 */
typedef struct ucg_plan_plogp_params {
    /* overhead time - per message and per byte, in seconds */
    struct {
        double sec_per_message;
        double sec_per_byte;
    } send, recv, gap;

    /* p2p latency, in seconds, by distance (assumes uniform network) */
    double latency_in_sec[UCG_GROUP_MEMBER_DISTANCE_UNKNOWN];

    /* number of peers on each level */
    ucg_group_member_index_t peer_count[UCG_GROUP_MEMBER_DISTANCE_UNKNOWN];
} ucg_plan_plogp_params_t;

typedef double (*ucg_plan_estimator_f)(ucg_plan_plogp_params_t plogp,
                                       ucg_collective_params_t *coll);

/**
 * @ingroup UCG_RESOURCE
 * @brief Collective planning resource descriptor.
 *
 * This structure describes a collective operation planning resource.
 */
enum ucg_plan_flags {
    UCG_PLAN_FLAG_PLOGP_LATENCY_ESTIMATOR = 0, /*< Supports PlogP latency estimation */
    UCG_PLAN_FLAG_FAULT_TOLERANCE_SUPPORT = 1  /*< Supported custom fault tolerance */
};

/**
 * @ingroup UCG_RESOURCE
 * @brief Collective planning resource descriptor.
 *
 * This structure describes a collective operation planning resource.
 */
typedef struct ucg_plan_desc {
    ucs_pmodule_component_desc_t super;
    unsigned                     modifiers_supported; /*< @ref enum ucg_collective_modifiers */
    unsigned                     flags;               /*< @ref enum ucg_plan_flags */

    /* Optional parameters - depending on flags */
    ucg_plan_estimator_f         latency_estimator;         /*< @ref ucg_plan_estimator_f */
    unsigned                     fault_tolerance_supported; /*< @ref enum ucg_fault_tolerance_mode */
} ucg_plan_desc_t;

typedef ucs_pmodule_target_plan_t ucg_group_plan_t;

const ucg_group_params_t* ucg_group_get_params(ucg_group_h group);
ucg_coll_id_t ucg_group_get_next_coll_id(ucg_group_h group);
ucs_status_t ucg_group_store_wireup_message(ucg_group_h group, void *data,
                                            size_t length, unsigned am_flags);
void ucg_group_am_msg_discard(ucp_recv_desc_t *rdesc, ucg_group_h group);
ucs_status_t ucg_group_am_msg_store(void *data, size_t length, unsigned am_flags,
                                    ucg_group_h group,
#if ENABLE_MT
                                    ucs_ptr_array_locked_t *msg_array);
#else
                                    ucs_ptr_array_t *msg_array);
#endif

static UCS_F_ALWAYS_INLINE int
ucg_group_params_want_timestamp(const ucg_group_params_t *params)
{
    return (params->field_mask & UCG_GROUP_PARAM_FIELD_FLAGS) &&
           (params->flags      & UCG_GROUP_CREATE_FLAG_TX_TIMESTAMP);
}

static UCS_F_ALWAYS_INLINE const ucg_collective_params_t*
ucg_plan_get_params(ucg_group_plan_t *plan)
{
    return (const ucg_collective_params_t*)plan->params.key;
}

static UCS_F_ALWAYS_INLINE ucg_group_member_index_t
ucg_plan_get_root(ucg_group_plan_t *plan)
{
    return UCG_PARAM_ROOT(ucg_plan_get_params(plan));
}

/* Helper function for connecting to other group members - by their index */
typedef ucs_status_t (*ucg_plan_reg_handler_cb)(uct_iface_h iface, void *arg);
ucs_status_t ucg_plan_connect_p2p_single(ucg_group_h group,
                                         ucg_group_member_index_t peer,
                                         uct_ep_h *uct_ep_p, ucp_ep_h *ucp_ep_p,
                                         uct_iface_h *iface_p, uct_md_h *md_p,
                                         const uct_iface_attr_t **iface_attr_p);
ucs_status_t ucg_plan_connect_p2p(ucg_group_h group,
                                  const ucs_int_array_t *destinations,
                                  uct_ep_h *uct_ep_p, ucp_ep_h *ucp_ep_p,
                                  uct_iface_h *iface_p, uct_md_h *md_p,
                                  const uct_iface_attr_t **iface_attr_p);
ucs_status_t ucg_plan_connect_coll(ucg_group_h group, uint64_t wireup_uid,
                                   const ucg_topo_desc_step_t *step,
                                   int is_leader, int is_incast,
                                   uint8_t wireup_am_id,
                                   uct_incast_operator_t operator_,
                                   uct_incast_operand_t operand,
                                   unsigned operand_count,
                                   int is_operand_cache_aligned, size_t msg_size,
                                   uct_reduction_external_cb_t ext_aux_cb,
                                   void *ext_operator, void *ext_datatype,
                                   ucg_group_member_index_t coll_threshold,
                                   ucg_group_member_index_t *coll_index_p,
                                   ucg_group_member_index_t *coll_count_p,
                                   uct_ep_h *uct_ep_p, uint16_t *ep_cnt_p,
                                   uct_iface_h *iface_p, uct_md_h *md_p,
                                   const uct_iface_attr_t **iface_attr_p,
                                   int *requires_optimization_p,
                                   unsigned *iface_id_base_p,
                                   ucp_tl_bitmap_t *coll_tl_bitmap_p);
void ucg_plan_disconnect_coll(ucg_group_h group, unsigned iface_id_base,
                              ucp_tl_bitmap_t coll_tl_bitmap);
ucs_status_t ucg_plan_connect_mock(ucg_group_h group, int is_collective,
                                   int is_incast, uct_iface_h *iface_p,
                                   const uct_iface_attr_t **iface_attr_p,
                                   uct_md_h *md_p);
void ucg_plan_connect_mock_cleanup(void);

/* Helper function for selecting other planners - to be used as fall-back */
ucs_status_t ucg_plan_choose(const ucg_collective_params_t *coll_params,
                             ucg_group_h group, ucg_plan_desc_t **desc_p,
                             ucg_group_ctx_h *gctx_p);

/* This combination of flags and structure provide additional info for planning */
enum ucg_plan_resource_flags {
    UCG_PLAN_RESOURCE_FLAG_REDUCTION_ACCELERATOR = UCS_BIT(0),
};

/* Helper function to obtain the worker which the given group is using */
ucp_worker_h ucg_plan_get_group_worker(ucg_group_h group);

/* Helper function to add delay and "manufacture" imbalance (for benchmarks) */
void ucg_context_barrier_delay(ucg_group_h group);

/* Helper function to determine if a memory domain can register memory */
int ucg_plan_md_can_register_memory(uct_md_h md);

/* Helper function to choose one of the supported incast callback functions */
ucs_status_t ucg_plan_choose_reduction_cb(void *external_op, void *external_dt,
                                          size_t dt_size, uint64_t dt_count,
                                          int does_zero_dt_count_means_unknown,
                                          int input_buffers_are_cache_aligned,
                                          uct_incast_operand_t *operand_p,
                                          uct_incast_operator_t *operator_p,
                                          uct_reduction_internal_cb_t *reduce_p);

/* Helper function for creating a topology (e.g. K-nomial tree) */
ucs_status_t ucg_topo_create(const ucg_topo_params_t *params,
                             ucg_topo_desc_t **desc_p);

/* Helper function for destroying a topology (from @ref ucg_topo_create) */
void ucg_topo_destroy(ucg_topo_desc_t *desc);

/* Helper function for printing information about a topology */
void ucg_topo_print(const ucg_topo_desc_t *desc);

/* Helper function for registering Active-Message handlers */
typedef struct ucp_am_handler ucp_am_handler_t;
ucs_status_t ucg_context_set_am_handler(uint8_t id, ucp_am_handler_t *handler);

/* Helper function for registering Active-Message handlers */
ucs_status_t ucg_context_set_async_timer(ucs_async_context_t *async,
                                         ucs_async_event_cb_t cb,
                                         void *cb_arg,
                                         ucs_time_t interval,
                                         int *timer_id_p);
ucs_status_t ucg_context_unset_async_timer(ucs_async_context_t *async,
                                           int timer_id);

/* Helper function to count the peers within a given distance from myself */
ucs_status_t
ucg_topo_get_peers_by_distance(const ucg_group_params_t *group_params,
                               enum ucg_group_member_distance distance,
                               ucg_group_member_index_t *first_p,
                               ucg_group_member_index_t *last_p,
                               unsigned *count_p);

/* Helper functions for periodic checks for faults on a remote group member */
typedef struct ucg_ft_handle* ucg_ft_h;
ucs_status_t ucg_ft_start(ucg_group_h group,
                          ucg_group_member_index_t index,
                          uct_ep_h optional_ep,
                          ucg_ft_h *handle_p);
ucs_status_t ucg_ft_end(ucg_ft_h handle,
                        ucg_group_member_index_t index);
ucs_status_t ucg_ft_propagate(ucg_group_h group,
                              const ucg_group_params_t *params,
                              uct_ep_h new_ep);

END_C_DECLS

#endif
