/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_ucp_comp.inl"

#include <ucp/dt/dt.inl>
#include <ucs/datastruct/mpool.inl>

#define UCG_OVER_UCP_PLAN_GET_TYPE(_name, _config, _is_net, _is_large) \
    ((_is_net) ? ((_is_large) ? (_config)->net_large_##_name  : \
                                (_config)->net_small_##_name) : \
                 ((_is_large) ? (_config)->host_large_##_name : \
                                (_config)->host_small_##_name))

ucs_status_t ucg_over_ucp_plan_estimate(ucg_group_ctx_h ctx,
                                       ucs_pmodule_target_action_params_h params,
                                       double *estimate_p)
{
    //const ucg_collective_params_t *coll_params = params;
    *estimate_p = 2.0; // TODO: a real estimate
    return UCS_OK;
}

/******************************************************************************
 *                                                                            *
 *                             Operation Planning                             *
 *                                                                            *
 ******************************************************************************/

static ucs_status_t
ucg_over_ucp_plan_generate_dt_info(void *dt, uint64_t cnt,
                                   ucg_over_ucp_plan_dt_info_t *info)
{
    ucp_datatype_t ucp_dt;
    ucp_dt_generic_t *gen;
    void *dt_state;

    if (ucs_unlikely(dt == NULL)) {
        ucp_dt = ucp_dt_make_contig(1);
    } else {
        if (ucg_global_params.datatype.convert_f(dt, &ucp_dt)) {
            ucs_error("Datatype conversion callback failed");
            return UCS_ERR_INVALID_PARAM;
        }
    }

    info->orig_dt   = dt;
    info->ucp_dt    = ucp_dt;
    info->is_contig = UCP_DT_IS_CONTIG(ucp_dt);

    if (ucs_likely(!info->is_contig)) {
        if (ucs_unlikely(!UCP_DT_IS_GENERIC(ucp_dt))) {
            ucs_error("Datatype is neither contiguous nor generic");
            return UCS_ERR_INVALID_PARAM;
        }

        gen             = ucp_dt_to_generic(ucp_dt);
        dt_state        = gen->ops.start_pack(gen->context, NULL, cnt);
        info->msg_size  = ucp_dt_length(ucp_dt, cnt, NULL, dt_state);
        info->dt_size   = ucp_dt_length(ucp_dt, 1, NULL, dt_state);
        gen->ops.finish(dt_state);
    } else {
        info->msg_size  = ucp_dt_length(ucp_dt, cnt, NULL, NULL);
        info->dt_size   = ucp_dt_length(ucp_dt, 1, NULL, NULL);
    }

    return UCS_OK;
}

ucs_status_t ucg_over_ucp_plan_get_dt_info(const ucg_collective_params_t *param,
                                           ucg_over_ucp_plan_dt_info_t *rx_info,
                                           ucg_over_ucp_plan_dt_info_t *tx_info)
{
    ucs_status_t status;

    int is_variadic = (UCG_PARAM_MODIFIERS(param) &
                       UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC);
    if (is_variadic) {
        // TODO: handle variadic!
        ucs_error("Variadic datatypes are not supported yet");
        return UCS_ERR_NOT_IMPLEMENTED;
    }

    status = ucg_over_ucp_plan_generate_dt_info(param->recv.dtype,
                                                param->recv.count, rx_info);
    if (status != UCS_OK) {
        return status;
    }

    return ucg_over_ucp_plan_generate_dt_info(param->send.dtype,
                                              param->send.count, tx_info);
}

static void
ucg_over_ucp_plan_mark_prev_levels(enum ucg_group_member_distance *array,
                                   ucg_group_member_index_t count,
                                   const ucg_topo_params_t *topo_params,
                                   enum ucg_group_member_distance distance,
                                   ucg_group_member_index_t base_index)
{
    ucg_group_member_index_t i;
    const struct ucg_topo_params_by_level *level;
    ucs_assert(base_index < topo_params->total);

    if (distance == UCG_GROUP_MEMBER_DISTANCE_NONE) {
        array[base_index] = UCG_GROUP_MEMBER_DISTANCE_NONE;
        ucs_assert(base_index < count);
        return;
    }

    level = &topo_params->by_level[distance--];
    if (level->type == UCG_TOPO_TYPE_NONE) {
        ucg_over_ucp_plan_mark_prev_levels(array, count, topo_params, distance,
                                           base_index);
        return;
    }

    for (i = 0; i < level->count; i++) {
        ucg_over_ucp_plan_mark_prev_levels(array, count, topo_params, distance,
                                           base_index + (i * level->stride));
    }
}

static ucs_status_t
ucg_over_ucp_plan_find_leaders(const enum ucg_group_member_distance *array,
                               const ucg_topo_params_t *topo_params,
                               enum ucg_group_member_distance distance,
                               ucg_group_member_index_t my_index,
                               ucg_group_member_index_t count,
                               ucg_group_member_index_t *first_p,
                               ucg_group_member_index_t *stride_p,
                               unsigned *count_p)
{
    /* clone the distance array, to be used as a scratchpad */
    ucg_group_member_index_t i = 0;
    size_t array_size = count * sizeof(*array);
    enum ucg_group_member_distance *temp_array =
        UCS_ALLOC_CHECK(array_size, "ucg_over_ucp_topo_array");
    memcpy(temp_array, array, array_size);
    ucs_assert(my_index < count);
    /* Note: alloca() not used here because count might be large */

    /* Find the closest node, mark myself to be at that distance */
    temp_array[my_index] = UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
    for (i = 0; i < count; i++) {
        if (temp_array[my_index] > temp_array[i]) {
            temp_array[my_index] = temp_array[i];
        }
    }

    /* "Promote" myself, if warranted */
    while ((temp_array[my_index] < distance) &&
           ((my_index == 0) || (temp_array[my_index - 1] >
                                temp_array[my_index]))) {
        temp_array[my_index]++;
    }

    /* find the first node within (exacly) the given distance */
    for (i = 0; (i < count) && (temp_array[i] != distance); i++);
    if (i == count) {
        *count_p = 0;
        ucs_free(temp_array);
        return UCS_OK;
    }

    /* sweep the temp. array and apply the "template" from the prev. level */
    *first_p  = i;
    *stride_p = 0;
    *count_p  = 0;
    do {
        /* hope that it matches our striding pattern... */
        if ((*first_p != i) && (*stride_p == 0)) {
            *stride_p = i - *first_p;
            *count_p  = 2;
        } else if ((*stride_p != 0) && ((i - *first_p) % *stride_p != 0)) {
            ucs_assert((*stride_p == 0) || ((i - *first_p) % *stride_p == 0));
            ucs_error("Group distance array seems to have unsupported strides");
            return UCS_ERR_UNSUPPORTED;
        } else {
            ++*count_p;
        }

        /* apply the "template" by zero-ing the i's peers from its prev. level*/
        ucg_over_ucp_plan_mark_prev_levels(temp_array, count, topo_params,
                                           distance - 1, i++);

        /* find the next node within the same (exact) distance */
        for (; (i < count) && (temp_array[i] != distance); i++);
    } while (i < count);

    ucs_free(temp_array);
    return UCS_OK;
}

static ucs_status_t
ucg_over_ucp_plan_peers_by_distance(const ucg_topo_params_t *topo_params,
                                    const ucg_group_params_t *group_params,
                                    enum ucg_group_member_distance distance,
                                    ucg_group_member_index_t *first_p,
                                    ucg_group_member_index_t *stride_p,
                                    unsigned *count_p)
{
    ucs_status_t status;
    const enum ucg_group_member_distance *array = group_params->distance_array;

    switch (group_params->distance_type) {
    case UCG_GROUP_DISTANCE_TYPE_FIXED:
        ucs_assert(distance <= UCG_GROUP_MEMBER_DISTANCE_UNKNOWN);
        if (group_params->distance_value == distance) {
            *first_p  = 0;
            *stride_p = 1;
            *count_p  = group_params->member_count;
        } else {
            *first_p  = group_params->member_index;
            *stride_p = 1;
            *count_p  = 1;
        }

        status = UCS_OK;
        break;

    case UCG_GROUP_DISTANCE_TYPE_TABLE:
        array = group_params->distance_table[group_params->member_index];
        /* no break */
    case UCG_GROUP_DISTANCE_TYPE_ARRAY:
        status = ucg_over_ucp_plan_find_leaders(array, topo_params, distance,
                                                group_params->member_index,
                                                group_params->member_count,
                                                first_p, stride_p, count_p);
        break;

    case UCG_GROUP_DISTANCE_TYPE_PLACEMENT:
        ucs_error("Placement indication is not yet supported");
        status = UCS_ERR_NOT_IMPLEMENTED;
        break;

    default:
        ucs_error("Invalid group distance type: %u",
                  group_params->distance_type);
        status = UCS_ERR_INVALID_PARAM;
        break;
    }

    return status;
}

static void
ucg_over_ucp_plan_set_level_topo_arg(ucg_topo_params_t *topo_params,
                                     const ucg_group_params_t *group_params,
                                     const ucg_over_ucp_config_t *config,
                                     int is_net,
                                     struct ucg_topo_params_by_level *level)
{
    unsigned threshold;
    ucg_group_member_index_t total = level->count;

    switch (level->type) {
    case UCG_TOPO_TYPE_KARY_TREE:
    case UCG_TOPO_TYPE_KNOMIAL_TREE:
        threshold = is_net ? config->net_tree_thresh :
                             config->host_tree_thresh;
        if (total > threshold) {
            level->tree_radix = is_net ? config->net_tree_radix :
                                         config->host_tree_radix;
        } else {
            level->tree_radix = (unsigned)-1;
        }

        break;

    case UCG_TOPO_TYPE_RECURSIVE:
        level->recursive_factor = is_net ? config->net_recursive_factor :
                                           config->host_recursive_factor;
        break;

    case UCG_TOPO_TYPE_NEIGHBORS:
        topo_params->cb_ctx = group_params->cb_context;
        break;

    default:
        break;
    }
}

static inline enum ucg_group_member_distance
ucg_over_ucp_plan_get_max_distance(const ucg_group_params_t *group_params)
{
    ucg_group_member_index_t i;
    enum ucg_group_member_distance max = UCG_GROUP_MEMBER_DISTANCE_NONE;
    const enum ucg_group_member_distance *array = group_params->distance_array;

    switch (group_params->distance_type) {
    case UCG_GROUP_DISTANCE_TYPE_FIXED:
        return group_params->distance_value;

    case UCG_GROUP_DISTANCE_TYPE_TABLE:
        array = group_params->distance_table[group_params->member_index];
        /* no break */
    case UCG_GROUP_DISTANCE_TYPE_ARRAY:
        for (i = 0; i < group_params->member_count; i++) {
            if (max < array[i]) {
                max = array[i];
            }
        }
        return max;

    case UCG_GROUP_DISTANCE_TYPE_PLACEMENT:
        ucs_error("Placement indication is not yet supported");
        return UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;

    default:
        ucs_error("Invalid group distance type: %u",
                  group_params->distance_type);
        return UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
    }
}

static ucs_status_t
ucg_over_ucp_plan_generate_params(ucg_topo_params_t *topo_params,
                                  const ucg_collective_params_t *coll_params,
                                  const ucg_group_params_t *group_params,
                                  const ucg_over_ucp_config_t *config,
                                  const ucg_over_ucp_plan_dt_info_t *rx_dt,
                                  const ucg_over_ucp_plan_dt_info_t *tx_dt)
{
    uint16_t modifiers;
    ucs_status_t status;
    int is_net, is_large;
    size_t max_msg_size, size_factor;
    struct ucg_topo_params_by_level *level;
    enum ucg_group_member_distance distance, max_distance;

    modifiers                     = UCG_PARAM_MODIFIERS(coll_params);
    topo_params->me               = group_params->member_index;
    topo_params->total            = group_params->member_count;
    topo_params->root             = UCG_PARAM_ROOT(coll_params);
    topo_params->multiroot_thresh = config->multiroot_thresh;
    topo_params->flags            = 0;
    level                         = NULL;
    is_large                      = 0;
    size_factor                   = 1;
    max_distance                  = ucg_over_ucp_plan_get_max_distance(group_params);

    /* plan each level (from the core to the network level) */
    for (distance = (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR) ?
                    (UCG_GROUP_MEMBER_DISTANCE_UNKNOWN - 1) : 0;
        ((distance < UCG_GROUP_MEMBER_DISTANCE_UNKNOWN) &&
         (!level || (level->first == topo_params->me)));
        distance++) {
        /* find all peer within this distance (incl. myself) */
        level  = &topo_params->by_level[distance];
        status = ucg_over_ucp_plan_peers_by_distance(topo_params, group_params,
                                                     distance, &level->first,
                                                     &level->stride,
                                                     &level->count);
        if (status != UCS_OK) {
            return status;
        }

        /* skip irrelevant distances */
        if (level->count < 2) {
            level->type = UCG_TOPO_TYPE_NONE;
            level       = NULL;
            continue;
        }

        /* determine message size */
        level->rx_msg_size = rx_dt->msg_size;
        level->tx_msg_size = tx_dt->msg_size;
        max_msg_size       = ucs_max(rx_dt->msg_size, tx_dt->msg_size);
        if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE) {
            /* for concatinating collectives (gather) message size grows */
            level->tx_msg_size *= size_factor;
            size_factor        *= level->count;
        }

        /* choose and set the (virtual, not physical) topology */
        is_net   = ucg_group_member_outside_this_host(distance);
        is_large = max_msg_size > (is_net ? config->net_size_thresh :
                                            config->host_size_thresh);
        if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR) {
            level->type = UCG_TOPO_TYPE_NEIGHBORS;
        } else if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION) {
            level->type = UCG_OVER_UCP_PLAN_GET_TYPE(reduce, config, is_net,
                                                     is_large);
            topo_params->flags |= UCG_TOPO_FLAG_TREE_FANIN;
        } else if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE) {
            level->type = UCG_TOPO_TYPE_KARY_TREE; /* bcast or scatter */
            topo_params->flags |= UCG_TOPO_FLAG_TREE_FANOUT;
        } else if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_PARTIAL) {
            level->type = UCG_TOPO_TYPE_RING;
            topo_params->flags |= UCG_TOPO_FLAG_RING_SINGLE;
        } else if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE) {
            level->type = UCG_OVER_UCP_PLAN_GET_TYPE(allreduce, config, is_net,
                                                     is_large);
            if ((level->type == UCG_TOPO_TYPE_RECURSIVE) &&
                (distance < max_distance)) {
                level->type = UCG_OVER_UCP_PLAN_GET_TYPE(reduce, config, is_net,
                                                         is_large);
            }

            topo_params->flags |= UCG_TOPO_FLAG_FULL_EXCHANGE;
            ucg_over_ucp_plan_set_level_topo_arg(topo_params, group_params,
                                                 config, is_net, level);
        } else if ((modifiers == 0) || /* alltoall */
                   (modifiers & (UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE |
                /* allgather */  UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST))) {
            level->type = UCG_OVER_UCP_PLAN_GET_TYPE(exchange, config, is_net,
                                                     is_large);
            topo_params->flags |= UCG_TOPO_FLAG_FULL_EXCHANGE;
        } else {
            level->type = UCG_TOPO_TYPE_KARY_TREE; /* the fall-back */
        }

        ucg_over_ucp_plan_set_level_topo_arg(topo_params, group_params, config,
                                             is_net, level);

        ucs_debug("topology level [%u]: type=%u first=%u stride=%u count=%u",
                  distance, level->type, level->first, level->stride, level->count);
    }

    /* Fill in the rest of the levels as empty */
    for (; distance < UCG_GROUP_MEMBER_DISTANCE_UNKNOWN; distance++) {
        level       = &topo_params->by_level[distance];
        level->type = UCG_TOPO_TYPE_NONE;
    }

    return UCS_OK;
}

static ucs_status_t
ucg_over_ucp_plan_allocate(const ucg_topo_desc_t *topo,
                           ucg_over_ucp_plan_t **plan_p,
                           ucp_ep_h **ep_base_p,
                           ucg_group_member_index_t **idx_base_p)
{
    size_t alloc_size;
    ucg_topo_desc_step_t *step;
    unsigned phs_cnt, phs_idx, step_ep_cnt, plan_ep_cnt = 0;

    ucs_ptr_array_for_each(step, phs_idx, &topo->steps) {
        step_ep_cnt = (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) ?
                      ucs_int_array_get_elem_count(&step->tx.tx_send_dests) : 0;
        if (step_ep_cnt > 1) {
            plan_ep_cnt += step_ep_cnt;
        }
    }

    phs_cnt    = ucs_ptr_array_get_elem_count(&topo->steps);
    alloc_size = sizeof(ucg_over_ucp_plan_t) +
                 (phs_cnt     * sizeof(ucg_over_ucp_plan_phase_t)) +
                 (plan_ep_cnt * sizeof(uct_ep_h));

#if ENABLE_DEBUG_DATA
    alloc_size += plan_ep_cnt * sizeof(ucg_group_member_index_t);
#endif

    if (ucs_posix_memalign((void**)plan_p, UCS_SYS_CACHE_LINE_SIZE, alloc_size,
                            "ucg_over_ucp_plan")) {
        return UCS_ERR_NO_MEMORY;
    }

    (*plan_p)->alloced = alloc_size;
    *ep_base_p         = (ucp_ep_h*)&(*plan_p)->phss[phs_cnt];
#if ENABLE_DEBUG_DATA
    *idx_base_p        = (ucg_group_member_index_t*)&(*ep_base_p)[plan_ep_cnt];
#else
    *idx_base_p        = NULL;
#endif
    return UCS_OK;
}

static ucs_status_t
ucg_over_ucp_plan_apply_topo(const ucg_topo_desc_t *topo,
                             ucg_over_ucp_group_ctx_t *gctx,
                             const ucg_group_params_t *group_params,
                             const ucg_collective_params_t *coll_params,
                             ucg_over_ucp_plan_t **plan_p)
{
    ucg_group_member_index_t *idx_base;
    const ucg_topo_desc_step_t *step;
    ucg_over_ucp_plan_t *plan;
    ucp_ep_h *ep_base;
    unsigned idx;

    ucs_status_t status = ucg_over_ucp_plan_allocate(topo, &plan, &ep_base,
                                                     &idx_base);
    if (status != UCS_OK) {
        return status;
    }

    plan->gctx    = gctx;
    plan->phs_cnt = 0;
    plan->req_cnt = 0;
    plan->tmp_buf = NULL;
    ucs_ptr_array_for_each(step, idx, &topo->steps) {
        status = ucg_over_ucp_phase_create(plan, &plan->tmp_buf, group_params,
                                           coll_params, step, plan->phs_cnt,
                                           &plan->req_cnt, &ep_base, &idx_base);
        if (status != UCS_OK) {
            ucs_free(plan);
            return status;
        }

        plan->phs_cnt++;
    }

    if ((plan->tmp_buf == coll_params->recv.buffer) ||
        (plan->tmp_buf == coll_params->send.buffer)) {
        plan->tmp_buf = NULL;
    }

    /* Set the last phase to run finalization */
    UCG_OVER_UCP_PLAN_FINI_ACTION_LASTIFY(plan->phss[plan->phs_cnt-1].fini_act);

    *plan_p = plan;
    return UCS_OK;
}

ucs_status_t ucg_over_ucp_plan_create(ucg_over_ucp_group_ctx_t *ctx,
                                      const ucg_collective_params_t *coll_params,
                                      ucg_topo_desc_t **topo_desc_p,
                                      ucg_over_ucp_plan_t **plan_p)
{
    ucs_status_t status;
    ucg_over_ucp_plan_t *plan;
    uct_incast_operand_t operand;
    ucg_topo_params_t topo_params;
    uct_incast_operator_t operator;
    ucg_over_ucp_plan_dt_info_t rx_dt;
    ucg_over_ucp_plan_dt_info_t tx_dt;

    const ucg_group_params_t *group_params = ucg_group_get_params(ctx->group);
    ucg_over_ucp_ctx_t *global_ctx         = ctx->ctx;
    ucg_over_ucp_config_t *config          = &global_ctx->config;

    /* obtain the datatype information */
    status = ucg_over_ucp_plan_get_dt_info(coll_params, &rx_dt, &tx_dt);
    if (status != UCS_OK) {
        return status;
    }

    /* choose the method for each level - based on distances and config */
    status = ucg_over_ucp_plan_generate_params(&topo_params, coll_params,
                                               group_params, config,
                                               &rx_dt, &tx_dt);
    if (status != UCS_OK) {
        return status;
    }

    /* create the send lists for each level */
    status = ucg_topo_create(&topo_params, topo_desc_p);
    if (status != UCS_OK) {
        return status;
    }

    /* generate the plan based on the topology description */
    status = ucg_over_ucp_plan_apply_topo(*topo_desc_p, ctx, group_params,
                                          coll_params, &plan);
    if (status != UCS_OK) {
        ucg_topo_destroy(*topo_desc_p);
        return status;
    }

#if ENABLE_DEBUG_DATA
    ucs_snprintf_safe(plan->name, sizeof(plan->name), "%s (ucg_over_ucp)",
                      coll_params->name ? coll_params->name : "unspecified");
#endif

    /* select a reduction callback if applicable */
    if (UCG_PARAM_MODIFIERS(coll_params) &
        UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE) {
        status = ucg_over_ucp_reduce_select_cb(UCG_PARAM_OP(coll_params),
                                               &rx_dt, &operand, &operator,
                                               &plan->reduce_f);
        if (status != UCS_OK) {
            ucg_over_ucp_plan_destroy(plan, 0);
            ucg_topo_destroy(*topo_desc_p);
            return status;
        }
    }

    // TODO: also add group_id and group for faster triggering!!!

    ucs_list_add_head(&ctx->plan_head, &plan->list);
    *plan_p = plan;
    return UCS_OK;
}

ucs_status_t ucg_over_ucp_plan_wrapper(ucg_group_ctx_h ctx,
                                       ucs_pmodule_target_action_params_h params,
                                       ucs_pmodule_target_plan_t **plan_p)
{
    ucs_status_t status;
    ucg_topo_desc_t *topo;
    ucg_over_ucp_plan_t *plan;
    ucg_over_ucp_group_ctx_t *gctx             = ctx;
    const ucg_collective_params_t *coll_params = params;

    status = ucg_over_ucp_plan_create(gctx, coll_params, &topo, &plan);
    if (status != UCS_OK) {
        return status;
    }

    ucg_topo_destroy(topo);
    *plan_p = &plan->super;
    return UCS_OK;
}

void ucg_over_ucp_plan_destroy(ucg_over_ucp_plan_t *plan, int keep_tmp_buf)
{
    int i;
    for (i = 0; i < plan->phs_cnt; i++) {
        ucg_over_ucp_phase_discard(&plan->phss[i]);
    }

    if (plan->tmp_buf && !keep_tmp_buf) {
        ucs_free(plan->tmp_buf);
    }

    ucs_list_del(&plan->list);
    ucs_free(plan);
}

void ucg_over_ucp_plan_discard(ucs_pmodule_target_plan_t *plan)
{
    ucg_over_ucp_plan_destroy(ucs_derived_of(plan, ucg_over_ucp_plan_t), 0);
}


/******************************************************************************
 *                                                                            *
 *                         Using Planned Operations                           *
 *                                                                            *
 ******************************************************************************/

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_over_ucp_plan_op_init(ucg_over_ucp_plan_t *plan, ucg_over_ucp_op_t *op,
                          unsigned id, void *request, ucp_worker_h worker)
{
    ucg_over_ucp_plan_phase_t *first = &plan->phss[0];
    size_t req_alloc                 = plan->req_cnt * sizeof(*op->reqs);
    op->super.plan                   = &plan->super;
    op->super.req                    = request;
    op->super.id                     = id;
    op->super.flags                  = 0;
    op->pending                      = first->dest.ep_cnt;
    op->phase                        = first;
    op->worker                       = worker;
    op->reqs                         = UCS_ALLOC_CHECK(req_alloc,
                                                       "over_ucp_reqs");

    ucg_over_ucp_comp_phase_init(first);

    return UCS_OK;
}

ucs_status_ptr_t ucg_over_ucp_plan_barrier(ucs_pmodule_target_plan_t *plan,
                                           uint16_t  id, void *request)
{
    ucs_assert(0);
    ucs_error("Async. barriers (e.g. MPI_Ibarrier) are not supported yet");
    return UCS_STATUS_PTR(UCS_ERR_NOT_IMPLEMENTED);

    // TODO: generate an action and append it like this:
    //   action = (ucs_pmodule_target_action_t*)ret;
    //   UCS_PMODULE_TARGET_THREAD_CS_ENTER(target);
    //   ucs_queue_push(&plan->target->pending, &action->queue);
    //   UCS_PMODULE_TARGET_THREAD_CS_EXIT(target);
}

ucs_status_ptr_t ucg_over_ucp_plan_trigger(ucs_pmodule_target_plan_t *tplan,
                                           uint16_t id, void *request)
{
    ucs_status_t status;
    ucs_status_ptr_t ret;
    ucg_over_ucp_plan_t *plan      = ucs_derived_of(tplan, ucg_over_ucp_plan_t);
    ucg_over_ucp_group_ctx_t *gctx = plan->gctx;
    ucg_over_ucp_op_t *op          = ucs_mpool_get_inline(&gctx->op_mp);

    status = ucg_over_ucp_plan_op_init(plan, op, id, request, gctx->worker);
    if (ucs_unlikely(status != UCS_OK)) {
        return UCS_STATUS_PTR(status);
    }

    /* Start the first step, which may actually complete the entire operation */
    ret = ucg_over_ucp_execute(op, 1);

    /* avoid testing, set the callback suitable for progress functions */
    op->super.flags = UCS_PMODULE_TARGET_ACTION_FLAG_ASYNC_COMPLETION;

    return ret;
}

unsigned ucg_over_ucp_plan_progress(ucs_pmodule_target_action_t *action)
{
    ucg_over_ucp_plan_t *plan      = ucs_derived_of(action->plan,
                                                    ucg_over_ucp_plan_t);
    ucg_over_ucp_group_ctx_t *gctx = plan->gctx;
    return ucp_worker_progress(gctx->worker);
}
