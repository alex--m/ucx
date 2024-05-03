/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_OVER_UCP_H_
#define UCG_OVER_UCP_H_

#include <ucp/dt/dt.h>
#include <ucp/core/ucp_request.h>
#include <ucs/datastruct/ptr_array.h>
#include <ucg/api/ucg_plan_component.h>


typedef struct ucg_over_ucp_config {
    unsigned        multiroot_thresh;

    ucg_topo_type_t host_small_reduce;
    ucg_topo_type_t host_small_allreduce;
    ucg_topo_type_t host_small_exchange;
    ucg_topo_type_t host_large_reduce;
    ucg_topo_type_t host_large_allreduce;
    ucg_topo_type_t host_large_exchange;
    size_t          host_size_thresh;
    unsigned        host_tree_radix;
    unsigned        host_tree_thresh;
    unsigned        host_recursive_factor;

    ucg_topo_type_t net_small_reduce;
    ucg_topo_type_t net_small_allreduce;
    ucg_topo_type_t net_small_exchange;
    ucg_topo_type_t net_large_reduce;
    ucg_topo_type_t net_large_allreduce;
    ucg_topo_type_t net_large_exchange;
    size_t          net_size_thresh;
    unsigned        net_tree_radix;
    unsigned        net_tree_thresh;
    unsigned        net_recursive_factor;
} ucg_over_ucp_config_t;

typedef struct ucg_over_ucp_ctx {
    ucs_ptr_array_locked_t group_by_id; /* @ref ucg_over_ucp_group_ctx_t */
    ucg_over_ucp_config_t  config;      /* user-configured settings */
} ucg_over_ucp_ctx_t;

typedef struct ucg_over_ucp_group_ctx ucg_over_ucp_group_ctx_t;

ucs_status_t ucg_over_ucp_init(ucg_comp_ctx_h cctx,
                               const ucs_pmodule_component_params_t *params,
                               ucg_comp_config_t *config);

void ucg_over_ucp_finalize(ucg_comp_ctx_h cctx);

ucs_status_t ucg_over_ucp_create_common(ucg_over_ucp_ctx_t* pctx,
                                        ucg_over_ucp_group_ctx_t *gctx,
                                        ucs_pmodule_target_t *target,
                                        const ucg_group_params_t *params);

/******************************************************************************
 *                                                                            *
 *                               Sending Messages                             *
 *                                                                            *
 ******************************************************************************/

/* Definitions of several callback functions, used during an operation */
typedef struct ucg_over_ucp_plan ucg_over_ucp_plan_t;
typedef void (*ucg_over_ucp_plan_init_f)(ucg_over_ucp_plan_t *plan,
                                         ucg_coll_id_t coll_id);
typedef void (*ucg_over_ucp_plan_fini_f)(ucg_over_ucp_plan_t *plan);
typedef void (*ucg_over_ucp_plan_compreq_f)(void *req, ucs_status_t status);

typedef struct ucg_over_ucp_plan_dt_info {
    int            is_contig;
    size_t         msg_size;
    size_t         dt_size;
    ucp_datatype_t ucp_dt;
    void           *orig_dt;
} ucg_over_ucp_plan_dt_info_t;

ucs_status_t ucg_over_ucp_plan_get_dt_info(const ucg_collective_params_t *params,
                                           ucg_over_ucp_plan_dt_info_t *rx_info,
                                           ucg_over_ucp_plan_dt_info_t *tx_info);

ucs_status_t ucg_over_ucp_reduce_select_cb(void *reduce_op,
                                           const ucg_over_ucp_plan_dt_info_t *d,
                                           uct_incast_operand_t *operand_p,
                                           uct_incast_operator_t *operator_p,
                                           uct_reduction_internal_cb_t *reduce_p);

typedef enum ucg_over_ucp_plan_init_action {
    UCG_OVER_UCP_PLAN_INIT_ACTION_NONE = 0,
    UCG_OVER_UCP_PLAN_INIT_ACTION_COPY_SEND_TO_RECV,
    UCG_OVER_UCP_PLAN_INIT_ACTION_BRUCK_INIT
} ucg_over_ucp_plan_init_action_t;

typedef enum ucg_over_ucp_plan_fini_action {
    UCG_OVER_UCP_PLAN_FINI_ACTION_NONE = 0,
    UCG_OVER_UCP_PLAN_FINI_ACTION_LAST,
    UCG_OVER_UCP_PLAN_FINI_ACTION_BRUCK,
    UCG_OVER_UCP_PLAN_FINI_ACTION_BRUCK_LAST,
    UCG_OVER_UCP_PLAN_FINI_ACTION_COPY_TEMP_TO_RECV,
    UCG_OVER_UCP_PLAN_FINI_ACTION_COPY_TEMP_TO_RECV_LAST
} ucg_over_ucp_plan_fini_action_t;

#define UCG_OVER_UCP_PLAN_FINI_ACTION_LASTIFY(_action) ((_action)++)

typedef struct ucg_over_ucp_plan_phase_dest {
    union {
        ucp_ep_h                 *multi_eps; /**< endpoint pointer array */
        ucp_ep_h                  single_ep; /**< single endpoint handle */
    };
    uint16_t                      ep_cnt;    /**< Number of (multi-)endpoints */

#if ENABLE_DEBUG_DATA
    union {
        ucg_group_member_index_t  index;     /**< corresponds to single_ep */
        ucg_group_member_index_t *indexes;   /**< array corresponding to EPs */
    };
#endif
} ucg_over_ucp_plan_phase_dest_t;

typedef struct ucg_over_ucp_plan_phase_buffer {
    uint8_t            *buffer;
    union {
        ucp_datatype_t dt;
        ucp_datatype_t *var_dts;
    };
    union {
        int            count;
        const int      *var_counts;
    };
    union {
        size_t         stride;
#define UCG_OVER_UCP_PLAN_PHASE_STRIDE_VARIADIC ((size_t)-1)
        const int      *var_displs;
    };
    ucg_step_idx_t     step_idx;
} ucg_over_ucp_plan_phase_buffer_t;

static UCS_F_ALWAYS_INLINE uint8_t*
ucg_over_ucp_plan_phase_calc_buffer(ucg_over_ucp_plan_phase_buffer_t *desc,
                                    unsigned index, ucp_datatype_t *dt_p,
                                    int *count_p)
{
    int is_variadic = (desc->stride == UCG_OVER_UCP_PLAN_PHASE_STRIDE_VARIADIC);
    *dt_p           = is_variadic ? desc->var_dts[index] : desc->dt;
    *count_p        = is_variadic ? desc->var_counts[index] : desc->count;

    return UCS_PTR_BYTE_OFFSET(desc->buffer, is_variadic ?
                                             (index * desc->stride) :
                                             desc->var_displs[index]);
}

typedef struct ucg_over_ucp_plan_phase {
    ucg_over_ucp_plan_phase_dest_t   dest;     /**< send destiantion list */
    ucg_over_ucp_plan_phase_buffer_t tx;       /**< send-side memory layout */
    ucg_over_ucp_plan_phase_buffer_t rx;       /**< recv-side memory layout */
    ucg_group_member_index_t         rx_cnt;   /**< expected recieve count */
    ucg_over_ucp_plan_init_action_t  init_act; /**< Initialize the collective */
    ucg_over_ucp_plan_fini_action_t  fini_act; /**< Finalize the collective */
    ucg_group_member_index_t         me;       /**< my index within the level */
} ucg_over_ucp_plan_phase_t;

struct ucg_over_ucp_plan {
    ucg_group_plan_t            super;
    uct_reduction_internal_cb_t reduce_f; /**< callback for reduction collectives */
    void                       *gctx;     /**< group context, type depends on subclass */
    /* --- 128 bytes --- */
    ucs_list_link_t             list;     /**< member of a per-group list of plans */
    size_t                      alloced;  /**< size allocated towards the plan */
    int8_t                     *tmp_buf;  /**< to be freed, for some operations */
    ucg_group_member_index_t    req_cnt;  /**< number of requests per operation */
    ucg_step_idx_t              phs_cnt;  /**< number of phases (below) */
#if ENABLE_DEBUG_DATA
#define UCG_OVER_UCP_PLANNER_NAME_MAX_LENGTH (64)
    char                        name[UCG_OVER_UCP_PLANNER_NAME_MAX_LENGTH];
#endif
    ucg_over_ucp_plan_phase_t   phss[0];  /**< phases of the collective plan */
} UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);

ucs_status_t ucg_over_ucp_plan_estimate(ucg_group_ctx_h ctx,
                                        ucs_pmodule_target_action_params_h params,
                                        double *estimate_p);

ucs_status_t ucg_over_ucp_plan_wrapper(ucg_group_ctx_h ctx,
                                       ucs_pmodule_target_action_params_h params,
                                       ucs_pmodule_target_plan_t **plan_p);

void ucg_over_ucp_plan_destroy(ucg_over_ucp_plan_t *plan, int keep_tmp_buf);

void ucg_over_ucp_plan_discard(ucs_pmodule_target_plan_t *plan);

void ucg_over_ucp_plan_print(ucs_pmodule_target_plan_t *plan);

ucs_status_t ucg_over_ucp_phase_create(ucg_over_ucp_plan_t *plan, int8_t **buf,
                                       const ucg_group_params_t *group_params,
                                       const ucg_collective_params_t *coll_params,
                                       const ucg_topo_desc_step_t *step,
                                       enum ucg_group_member_distance topo_level,
                                       unsigned *req_cnt_p, ucp_ep_h **ep_base_p,
                                       ucg_group_member_index_t **idx_base_p);

void ucg_over_ucp_phase_discard(ucg_over_ucp_plan_phase_t *phase);


/******************************************************************************
 *                                                                            *
 *                             Operation Execution                            *
 *                                                                            *
 ******************************************************************************/

typedef union ucg_over_ucp_tag {
    ucp_tag_t tag;
    struct {
        union {
            struct {
                ucg_coll_id_t    coll_id;
                uint8_t          step_idx;
            };
            uint16_t             local_id;
        };
        ucg_group_id_t           group_id;
        ucg_group_member_index_t src_idx;
    };
} UCS_S_PACKED ucg_over_ucp_tag_t;

typedef struct ucg_over_ucp_op {
    ucg_op_t                   super;
    volatile uint32_t          pending;     /**< number of step's pending messages */
    ucg_over_ucp_plan_phase_t *phase;
    ucg_over_ucp_tag_t         base_tag;
    ucp_request_t             *reqs;
    ucp_worker_h               worker;
} UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE) ucg_over_ucp_op_t;

struct ucg_over_ucp_group_ctx {
    ucg_comp_ctx_h            ctx;          /**< global context */
    ucg_group_h               group;        /**< group handle */
    ucp_worker_h              worker;       /**< group's worker */
    ucg_group_id_t            group_id;     /**< Group identifier */
    ucs_list_link_t           plan_head;    /**< list of plans (for cleanup) */
    ucs_ptr_array_t           faults;       /**< flexible array of faulty members */
    ucs_mpool_t               op_mp;
} UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);


ucs_status_t ucg_over_ucp_plan_create(ucg_over_ucp_group_ctx_t *ctx,
                                      const ucg_collective_params_t *coll_params,
                                      ucg_topo_desc_t **topo_desc_p,
                                      ucg_over_ucp_plan_t **plan_p);

ucs_status_ptr_t ucg_over_ucp_plan_barrier(ucs_pmodule_target_plan_t *plan,
                                           uint16_t id, void *request);

ucs_status_ptr_t ucg_over_ucp_plan_trigger(ucs_pmodule_target_plan_t *plan,
                                           uint16_t id, void *request);

ucs_status_ptr_t ucg_over_ucp_execute(ucg_over_ucp_op_t *op,
                                      int first_invocation);

unsigned ucg_over_ucp_plan_progress(ucs_pmodule_target_action_t *action);

#endif
