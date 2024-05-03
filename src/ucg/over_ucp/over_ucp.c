/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>
#include <ucs/arch/atomic.h>
#include <ucs/profile/profile.h>
#include <ucp/core/ucp_request.inl>

/* Note: <ucg/api/...> not used because this header is not installed */
#include "../api/ucg_plan_component.h"

#include "over_ucp.h"
#include "over_ucp_comp.inl" /* ucg_over_ucp_plan_recv_cb() in handler */

const char *ucg_topo_type_names[] = {
    [UCG_TOPO_TYPE_NONE]         = "none",
    [UCG_TOPO_TYPE_KARY_TREE]    = "k-ary tree",
    [UCG_TOPO_TYPE_KNOMIAL_TREE] = "k-nomial tree",
    [UCG_TOPO_TYPE_RING]         = "ring",
    [UCG_TOPO_TYPE_BRUCK]        = "bruck",
    [UCG_TOPO_TYPE_RECURSIVE]    = "recursive",
    [UCG_TOPO_TYPE_PAIRWISE]     = "pairwise",
    [UCG_TOPO_TYPE_NEIGHBORS]    = "2D torus",
    NULL
};

ucs_config_field_t ucg_over_ucp_config_table[] = {
    {"MULTIROOT_THRESH", "4", "Upper-limit for using mutiroot at the top-most tree level",
     ucs_offsetof(ucg_over_ucp_config_t, multiroot_thresh), UCS_CONFIG_TYPE_UINT},

    {"INTRANODE_SMALL_REDUCE", "k-ary tree", "Method to aggregate small buffers to a process",
     ucs_offsetof(ucg_over_ucp_config_t, host_small_reduce),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTRANODE_SMALL_ALLREDUCE", "k-ary tree", "Method to aggregate small buffers within hosts",
     ucs_offsetof(ucg_over_ucp_config_t, host_small_allreduce),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTRANODE_SMALL_EXCHANGE", "bruck", "Method to exchange small buffers within hosts",
     ucs_offsetof(ucg_over_ucp_config_t, host_small_exchange),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTRANODE_LARGE_REDUCE", "k-ary tree", "Method to aggregate large buffers to a process",
     ucs_offsetof(ucg_over_ucp_config_t, host_large_reduce),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTRANODE_LARGE_ALLREDUCE", "k-ary tree", "Method to aggregate large buffers within hosts",
     ucs_offsetof(ucg_over_ucp_config_t, host_large_allreduce),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTRANODE_LARGE_EXCHANGE", "pairwise", "Method to exchange large buffers within hosts",
     ucs_offsetof(ucg_over_ucp_config_t, host_large_exchange),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTRANODE_SIZE_THRESH", "1M", "Size threshold for aggregation within hosts",
     ucs_offsetof(ucg_over_ucp_config_t, host_size_thresh), UCS_CONFIG_TYPE_MEMUNITS},

    {"INTRANODE_TREE_RADIX", "3", "K-ary/K-nomial tree radix within hosts",
     ucs_offsetof(ucg_over_ucp_config_t, host_tree_radix), UCS_CONFIG_TYPE_UINT},

    {"INTRANODE_TREE_THRESH", "18", "Per-level CPU/core-count threshold justifying a tree",
     ucs_offsetof(ucg_over_ucp_config_t, host_tree_thresh), UCS_CONFIG_TYPE_UINT},

    {"INTRANODE_RD_FACTOR", "2", "Recursive K-ing factor within hosts (2 for recursive doubling)",
     ucs_offsetof(ucg_over_ucp_config_t, host_recursive_factor), UCS_CONFIG_TYPE_UINT},


    {"INTERNODE_SMALL_REDUCE", "k-nomial tree", "Method to aggregate small buffers to a host",
     ucs_offsetof(ucg_over_ucp_config_t, net_small_reduce),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTERNODE_SMALL_ALLREDUCE", "recursive", "Method to aggregate small buffers among hosts",
     ucs_offsetof(ucg_over_ucp_config_t, net_small_allreduce),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTERNODE_SMALL_EXCHANGE", "bruck", "Method to exchange small buffers among hosts",
     ucs_offsetof(ucg_over_ucp_config_t, net_small_exchange),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTERNODE_LARGE_REDUCE", "k-nomial tree", "Method to aggregate large buffers to a host",
     ucs_offsetof(ucg_over_ucp_config_t, net_large_reduce),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTERNODE_LARGE_ALLREDUCE", "recursive", "Method to aggregate large buffers among hosts",
     ucs_offsetof(ucg_over_ucp_config_t, net_large_allreduce),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTERNODE_LARGE_EXCHANGE", "pairwise", "Method to exchange large buffers among hosts",
     ucs_offsetof(ucg_over_ucp_config_t, net_large_exchange),
     UCS_CONFIG_TYPE_ENUM(ucg_topo_type_names)},

    {"INTERNODE_SIZE_THRESH", "1M", "Size threshold for aggregation among hosts",
     ucs_offsetof(ucg_over_ucp_config_t, net_size_thresh), UCS_CONFIG_TYPE_MEMUNITS},

    {"INTERNODE_TREE_RADIX", "7", "K-ary/K-nomial tree radix among hosts",
     ucs_offsetof(ucg_over_ucp_config_t, net_tree_radix), UCS_CONFIG_TYPE_UINT},

    {"INTERNODE_TREE_THRESH", "16", "Per-level host-count threshold justifying a tree",
     ucs_offsetof(ucg_over_ucp_config_t, net_tree_thresh), UCS_CONFIG_TYPE_UINT},

    {"INTERNODE_RD_FACTOR", "2", "Recursive K-ing factor among hosts (2 for recursive doubling)",
     ucs_offsetof(ucg_over_ucp_config_t, net_recursive_factor), UCS_CONFIG_TYPE_UINT},


    {NULL}
};

extern ucs_pmodule_component_t ucg_over_ucp_component;
static ucs_status_t ucg_over_ucp_query(ucs_list_link_t *desc_head)
{
    ucg_plan_desc_t *desc     = UCS_ALLOC_TYPE_CHECK(ucg_plan_desc_t);
    desc->super.component     = &ucg_over_ucp_component;
    desc->modifiers_supported = (unsigned)-1; /* supports ANY collective */
    desc->flags               = 0;

    ucs_list_add_head(desc_head, &desc->super.list);
    return UCS_OK;
}

ucs_status_t ucg_over_ucp_init(ucg_comp_ctx_h cctx,
                               const ucs_pmodule_component_params_t *params,
                               ucg_comp_config_t *config)
{
    unsigned group_id;
    ucg_over_ucp_ctx_t *bctx = cctx;
    ucs_status_t status      = ucs_config_parser_clone_opts(config, &bctx->config,
                                                            ucg_over_ucp_config_table);
    if (status != UCS_OK) {
        return status;
    }

    ucs_ptr_array_locked_init(&bctx->group_by_id, "over_ucp_group_table");

    for (group_id = 0; group_id < UCG_GROUP_FIRST_GROUP_ID; group_id++) {
        ucs_ptr_array_locked_insert(&bctx->group_by_id, NULL);
    }

    return UCS_OK;
}

void ucg_over_ucp_finalize(ucg_comp_ctx_h cctx)
{
    unsigned group_id;
    for (group_id = 0; group_id < UCG_GROUP_FIRST_GROUP_ID; group_id++) {
        ucs_ptr_array_locked_remove(&((ucg_over_ucp_ctx_t*)cctx)->group_by_id,
                                    group_id);
    }

    ucs_ptr_array_locked_cleanup(&((ucg_over_ucp_ctx_t*)cctx)->group_by_id, 1);
}

ucs_status_t ucg_over_ucp_create_common(ucg_over_ucp_ctx_t* pctx,
                                        ucg_over_ucp_group_ctx_t *gctx,
                                        ucs_pmodule_target_t *target,
                                        const ucg_group_params_t *params)
{
    /* Fill in the information in the per-group context */
    gctx->group        = (ucg_group_h)target;
    gctx->worker       = params->worker;
    gctx->ctx          = pctx;

    if (params->field_mask & UCG_GROUP_PARAM_FIELD_ID) {
        gctx->group_id = params->id;
        ucs_ptr_array_locked_set(&pctx->group_by_id, params->id, gctx);
    } else {
        gctx->group_id = ucs_ptr_array_locked_insert(&pctx->group_by_id, gctx);
    }

    ucs_assert(gctx->group_id >= UCG_GROUP_FIRST_GROUP_ID);
    ucs_list_head_init(&gctx->plan_head);

    return UCS_OK;
}

static ucs_mpool_ops_t ucg_over_ucp_op_mpool_ops = {
    .chunk_alloc   = ucs_mpool_chunk_malloc,
    .chunk_release = ucs_mpool_chunk_free,
    .obj_init      = NULL,
    .obj_cleanup   = NULL,
    .obj_str       = NULL
};

static ucs_status_t ucg_over_ucp_create(ucg_comp_ctx_h cctx,
                                        ucg_group_ctx_h ctx,
                                        ucs_pmodule_target_t *target,
                                        const ucs_pmodule_target_params_t *params)
{
    ucs_status_t status;
    ucs_mpool_params_t mp_params;
    ucg_over_ucp_ctx_t* pctx               = cctx;
    ucg_over_ucp_group_ctx_t *gctx         = ctx;
    const ucg_group_params_t *group_params = params->per_framework;

    status = ucg_over_ucp_create_common(pctx, gctx, target, group_params);
    if (status == UCS_OK) {
        ucs_mpool_params_reset(&mp_params);
        mp_params.elem_size       = sizeof(ucg_over_ucp_op_t);
        mp_params.elems_per_chunk = 16;
        mp_params.ops             = &ucg_over_ucp_op_mpool_ops;
        mp_params.name            = "ucg_over_ucp_ops";
        status = ucs_mpool_init(&mp_params, &gctx->op_mp);
    }

    return status;
}

static void ucg_over_ucp_destroy(ucg_group_ctx_h ctx)
{
    ucg_over_ucp_plan_t      *plan;
    ucg_over_ucp_group_ctx_t *gctx = ctx;
    ucg_over_ucp_ctx_t       *bctx = gctx->ctx;

    while (!ucs_list_is_empty(&gctx->plan_head)) {
        plan = ucs_list_head(&gctx->plan_head, ucg_over_ucp_plan_t, list);
        ucs_pmodule_target_plan_uncache(plan->super.target, &plan->super);
        ucg_over_ucp_plan_destroy(plan, 0);
    }

    /* Remove the group from the global storage array */
    ucs_ptr_array_locked_remove(&bctx->group_by_id, gctx->group_id);
    ucs_mpool_cleanup(&gctx->op_mp, 1);

    /* Note: gctx is freed as part of the group object itself */
}

static ucs_status_t ucg_over_ucp_handle_fault(ucg_group_ctx_h gctx, uint64_t id)
{
    // ucg_group_member_index_t index = id;
    ucs_error("Handling faults is not implemented yet");
    return UCS_ERR_NOT_IMPLEMENTED;
}

UCS_PMODULE_COMPONENT_DEFINE(ucg_over_ucp_component, "over_ucp",
                             sizeof(ucg_over_ucp_ctx_t),
                             sizeof(ucg_over_ucp_group_ctx_t),
                             ucg_over_ucp_query, ucg_over_ucp_init,
                             ucg_over_ucp_finalize, ucg_over_ucp_create,
                             ucg_over_ucp_destroy, ucg_over_ucp_plan_estimate,
                             ucg_over_ucp_plan_wrapper, ucg_over_ucp_plan_trigger,
                             ucg_over_ucp_plan_progress, ucg_over_ucp_plan_discard,
                             ucg_over_ucp_plan_print, ucg_over_ucp_handle_fault,
                             ucg_over_ucp_config_table, ucg_over_ucp_config_t,
                             "UCG_OVER_UCP_", ucg_components_list);
