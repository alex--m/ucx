/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_uct.h"
#include "over_uct_comp.inl"

#include <ucp/dt/dt.inl>
#include <ucs/datastruct/mpool.inl>


static int ucg_over_uct_plan_is_supported(const ucg_collective_params_t *params)
{
    void *op;
    int want_location;
    int is_commutative;
    enum ucg_operator operator;
    uint16_t modifiers = UCG_PARAM_MODIFIERS(params);
    int is_mock        = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS;
    int is_variadic    = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC;
    int is_reduction   = modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE;
    int is_barrier     = is_reduction && !params->send.dtype;

    if (is_variadic) {
        return 0;
    }

    /* sanity checks for reduction operations */
    if (is_reduction && !is_mock && !is_barrier) {
        if (!(ucg_global_params.field_mask & UCG_PARAM_FIELD_REDUCE_OP_CB)) {
            ucs_error("Cannot perform reductions: Missing ucg_init() parameters");
            return 0;
        }

        if ((op = UCG_PARAM_OP(params)) == NULL) {
            ucs_error("Cannot perform reductions: reduction operation is NULL");
            return 0;
        }

        if (ucg_global_params.reduce_op.get_operator_f(op, &operator,
                                                       &want_location,
                                                       &is_commutative)) {
            ucs_error("Cannot perform reductions: unknown reduction operation");
            return 0;
        }

        if (!is_commutative) {
            ucs_error("Cannot perform reduction: non-commutative operations unsupported");
            return 0;
            // TODO: set UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE instead
        }

        if (want_location) {
            ucs_error("Cannot perform reductions: MPI's MINLOC/MAXLOC unsupported");
            return 0;
        }
    }

    // TODO: also check that on every level of the topology - each node has the
    //       exact same number of peers (no imbalanced trees)

    return 1;
}

ucs_status_t ucg_over_uct_plan_estimate(ucg_group_ctx_h ctx,
                                        ucs_pmodule_target_action_params_h params,
                                        double *estimate_p)
{
    const ucg_collective_params_t *coll_params = params;
    *estimate_p = ucg_over_uct_plan_is_supported(coll_params) ? 1.0 : 1000.0;
    // TODO: a real performance estimate...
    return UCS_OK;
}

/******************************************************************************
 *                                                                            *
 *                             Operation Planning                             *
 *                                                                            *
 ******************************************************************************/

static ucs_status_t
ucg_over_ucp_plan_upgrade(ucg_over_ucp_plan_t *ucp_plan,
                          ucg_over_uct_plan_t **plan_p,
                          ucg_over_uct_phase_extra_info_t **info_base_p,
                          uct_ep_h **ep_base_p)
{
    ucg_over_uct_plan_t *uct_plan = NULL;
    ucg_step_idx_t phs_cnt        = ucp_plan->phs_cnt;
    size_t ep_alloc_size          = ucp_plan->alloced -
                                    ((uintptr_t)&(ucp_plan->phss[phs_cnt]) -
                                     (uintptr_t)ucp_plan);
    size_t info_alloc_size        = phs_cnt * 2 * sizeof(**info_base_p);
    size_t alloc_size             = (uintptr_t)(&uct_plan->phss[phs_cnt]) +
                                    ep_alloc_size + info_alloc_size;

    if (ucs_posix_memalign((void**)&uct_plan, UCS_SYS_CACHE_LINE_SIZE,
                           alloc_size, "ucg_over_uct_plan")) {
        return UCS_ERR_NO_MEMORY;
    }

    memset(uct_plan, 0, alloc_size);
    memcpy(&uct_plan->super, ucp_plan, sizeof(*ucp_plan));

    uct_plan->super.alloced = alloc_size;
    *ep_base_p              = (uct_ep_h*)&(uct_plan->phss[phs_cnt]);
    *info_base_p            = UCS_PTR_BYTE_OFFSET(*ep_base_p, ep_alloc_size);
    *plan_p                 = uct_plan;
    return UCS_OK;
}

static inline ucs_status_t
ucg_over_uct_plan_apply_dt_info(ucg_over_uct_plan_t *plan,
                                const ucg_over_uct_config_t *config,
                                const ucg_collective_params_t *params,
                                ucg_over_ucp_plan_dt_info_t *rx_dt,
                                ucg_over_ucp_plan_dt_info_t *tx_dt)
{
    plan->send_dt = tx_dt->ucp_dt;
    if (!tx_dt->is_contig) {
        plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_RECV_UNPACK;
    }

    plan->recv_dt = rx_dt->ucp_dt;
    if (!rx_dt->is_contig) {
        plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_RECV_UNPACK;
    }

    if (config->is_dt_volatile) {
        plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_VOLATILE_DT;
    }

    return UCS_OK;
}

void ucg_over_uct_async_resend(int id, ucs_event_set_types_t e, void *c)
{
    ucs_status_ptr_t res;
    ucg_over_uct_op_t *op, *next_op;
    ucg_over_uct_group_ctx_t *gctx = c;

    ucs_recursive_spin_lock(&gctx->resend_lock);

    ucs_list_for_each_safe(op, next_op, &gctx->resend_head, resend_list) {
        /* Only consider collectives which are not currently being triggered */
        if (op->super.flags & UCS_PMODULE_TARGET_ACTION_FLAG_ASYNC_COMPLETION) {
            /* Remove from resend list */
            ucg_over_uct_cancel_resend(op);

            /* Do the actual resend */
            res = ucg_over_uct_execute_op(op);
            if (!UCS_PTR_IS_PTR(res)) {
                /* operation is complete! */
                break;
            }
        }
    }

    ucs_recursive_spin_unlock(&gctx->resend_lock);
}

ucs_status_t ucg_over_uct_plan_create(ucg_over_uct_group_ctx_t *gctx,
                                      const ucg_collective_params_t *coll_params,
                                      ucg_over_uct_plan_t **plan_p)
{
    unsigned idx;
    uct_ep_h *ep_base;
    ucs_status_t status;
    ucg_over_uct_plan_t *plan;
    ucg_topo_desc_t *topo_desc;
    ucg_topo_desc_step_t *step;
    ucg_over_ucp_plan_t *ucp_plan;
    ucg_over_uct_plan_phase_t *phase;
    ucg_over_ucp_plan_dt_info_t rx_dt;
    ucg_over_ucp_plan_dt_info_t tx_dt;
    ucg_over_ucp_plan_phase_t *ucp_phase;
    ucg_over_uct_phase_extra_info_t *info_base;

    int requires_optimization              = 0;
    int8_t *temp_buffer                    = NULL;
    ucg_over_uct_ctx_t *octx               = gctx->super.ctx;
    ucg_over_uct_config_t *config          = &octx->config;
    uint16_t modifiers                     = UCG_PARAM_MODIFIERS(coll_params);
    ucg_group_h group                      = gctx->super.group;
    const ucg_group_params_t *group_params = ucg_group_get_params(group);

    /* start from the UCP-based plan */
    status = ucg_over_ucp_plan_create(&gctx->super, coll_params, &topo_desc,
                                      &ucp_plan);
    if (status != UCS_OK) {
        return status;
    }

    /* allocate a UCT-based plan */
    status = ucg_over_ucp_plan_upgrade(ucp_plan, &plan, &info_base, &ep_base);
    if (status != UCS_OK) {
        goto upgrade_failed;
    }

    /* Fill in general plan parameters */
    plan->my_index = group_params->member_index;
    plan->op_flags = 0;
    if (modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER) {
        plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_BARRIER;
    }
    /* Note MPI_Barrier() does not set UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER */
    if ((octx->is_barrier_delayed) &&
        (!coll_params->send.count) &&
        (!coll_params->recv.count)) {
        plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_BARRIER_DELAY;
    }

    if (ucg_group_params_want_timestamp(group_params)) {
        plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_IMBALANCE_INFO;
    }

    /* obtain the datatype information */
    status = ucg_over_ucp_plan_get_dt_info(coll_params, &rx_dt, &tx_dt);
    if (status != UCS_OK) {
        goto upgrade_failed;
    }

    status = ucg_over_uct_plan_apply_dt_info(plan, config, coll_params, &rx_dt,
                                             &tx_dt);
    if (status != UCS_OK) {
        goto upgrade_failed;
    }

    plan->max_frag = 0;
    plan->opt_cnt  = (typeof(plan->opt_cnt))-1;
    phase          = &plan->phss[0];
    ucp_phase      = &ucp_plan->phss[0];
    ucs_assert(((uintptr_t)phase % UCS_SYS_CACHE_LINE_SIZE) == 0);

    ucs_ptr_array_for_each(step, idx, &topo_desc->steps) {
        /* Assign additional phase information pointer */
        phase->info = info_base;
        info_base  += 2;

        /* Fill in phase details */
        status = ucg_over_uct_phase_create(plan, phase++, step, ucp_phase++,
                                           config, &rx_dt, &tx_dt, coll_params,
                                           &temp_buffer, &requires_optimization,
                                           &ep_base, octx->coll_am_id);
        if (status != UCS_OK) {
            goto phase_create_failed;
        }
    }

    /* As an optimization, avoid sync. completion (using longjmp in MPI) */
    if ((idx == 1) &&
        ((step->flags & (UCG_TOPO_DESC_STEP_FLAG_RX_VALID |
                         UCG_TOPO_DESC_STEP_FLAG_TX_VALID)) ==
                         UCG_TOPO_DESC_STEP_FLAG_TX_VALID)) {
        plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_ASYNC_COMPLETION;
    }

    (--phase)->flags |= UCG_OVER_UCT_PLAN_PHASE_FLAG_LAST_STEP;
    ucs_list_add_head(&gctx->super.plan_head, &plan->super.list);

    /*
     * In some cases - an optimization is mandatory for successful execution.
     * For reference, see @ref ucg_plan_connect_coll .
     */
    if (requires_optimization) {
        status = ucg_over_uct_optimize_now(plan, coll_params);
        if (status != UCS_OK) {
            return status;
        }
    } else if (plan->opt_cnt != ((typeof(plan->opt_cnt))-1)) {
        plan->op_flags |= UCG_OVER_UCT_PLAN_FLAG_OPTIMIZE_CB;
    }

#if ENABLE_DEBUG_DATA
    plan->ucp_plan  = ucp_plan;
    plan->topo_desc = topo_desc;
#else
    ucg_over_ucp_plan_destroy(ucp_plan, 1);
    ucg_topo_destroy(topo_desc);
#endif

    plan->super.gctx = gctx;
    *plan_p          = plan;
    return UCS_OK;

phase_create_failed:
    ucs_free(plan);

upgrade_failed:
    ucg_over_ucp_plan_destroy(ucp_plan, 0);
    return status;
}

ucs_status_t ucg_over_uct_plan_wrapper(ucg_group_ctx_h ctx,
                                       ucs_pmodule_target_action_params_h params,
                                       ucs_pmodule_target_plan_t **plan_p)
{
    ucg_over_uct_group_ctx_t *gctx             = ctx;
    const ucg_collective_params_t *coll_params = params;

    return ucg_over_uct_plan_create(gctx, coll_params,
                                    (ucg_over_uct_plan_t**)plan_p);
}

void ucg_over_uct_plan_destroy(ucg_over_uct_plan_t *plan)
{
    int i;
    ucg_over_uct_group_ctx_t *gctx        = plan->super.gctx;
    ucg_group_h group                     = gctx->super.group;
    const ucg_collective_params_t *params = ucg_plan_get_params(&plan->super.super);
    int is_mock                           = UCG_PARAM_MODIFIERS(params) &
                                            UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS;

    for (i = 0; i < plan->super.phs_cnt; i++) {
        ucg_over_uct_phase_destroy(&plan->phss[i], params, group, is_mock);
    }

#if ENABLE_DEBUG_DATA
    ucg_over_ucp_plan_destroy(plan->ucp_plan, is_mock);
    ucg_topo_destroy(plan->topo_desc);
#else
    if (plan->super.tmp_buf && !is_mock) {
        ucs_free(plan->super.tmp_buf);
    }
#endif

    ucs_list_del(&plan->super.list);
    ucs_free(plan);
}

void ucg_over_uct_plan_discard(ucs_pmodule_target_plan_t *plan)
{
    ucg_over_uct_plan_destroy(ucs_derived_of(plan, ucg_over_uct_plan_t));
}

/******************************************************************************
 *                                                                            *
 *                             Operation Creation                             *
 *                                                                            *
 ******************************************************************************/

static ucs_status_t UCS_F_ALWAYS_INLINE
ucg_over_uct_plan_op_init(ucg_over_uct_plan_t *plan, ucg_over_uct_op_t *op,
                          unsigned id, void *request,
                          ucg_over_uct_header_step_t *local_id_p)
{
    ucg_over_uct_plan_phase_t *first_phase;

    first_phase           = &plan->phss[0];
    op->super.plan        = &plan->super.super;
    op->super.req         = request;
    op->super.id          = id;
    op->super.flags       = 0;
    op->super.status      = UCS_INPROGRESS;
    op->comp.count        = first_phase->rx.frags_cnt;
    op->phase             = first_phase;
    op->iter_ep           = 0;
    op->iter_frag         = 0;
    local_id_p->step_idx  = first_phase->rx.step_idx;
    local_id_p->coll_id   = (ucg_coll_id_t)id;

    ucs_assert(ucs_list_is_empty(&op->resend_list));

    return UCS_OK;
}

ucs_status_ptr_t ucg_over_uct_plan_trigger(ucs_pmodule_target_plan_t *plan,
                                           uint16_t id, void *request)
{
    ucs_status_t status;
    ucs_status_ptr_t ret;
    ucg_over_uct_header_step_t expected;

    /* Allocate a "slot" for this operation, from a per-group array of slots */
    unsigned slot_idx              = id % UCG_OVER_UCT_MAX_CONCURRENT_OPS;
    ucg_over_uct_plan_t *uct_plan  = ucs_derived_of(plan, ucg_over_uct_plan_t);
    ucg_over_uct_group_ctx_t *gctx = (ucg_over_uct_group_ctx_t*)uct_plan->super.gctx;
    ucg_over_uct_comp_slot_t *slot = &gctx->slots[slot_idx];
    ucg_over_uct_op_t *op          = &slot->op;

    if (ucs_unlikely(UCG_OVER_UCT_OP_EXPECTED(op)->local_id != 0)) {
        ucs_error("concurrent collective operations limit exceeded");
        return UCS_STATUS_PTR(UCS_ERR_NO_RESOURCE);
    }

    status = ucg_over_uct_comp_by_flags(uct_plan, op, 1);
    if (ucs_unlikely(status != UCS_OK)) {
        return UCS_STATUS_PTR(status);
    }

    /* Initialize the request structure, located inside the selected slot */
    status = ucg_over_uct_plan_op_init(uct_plan, op, id, request, &expected);
    if (ucs_unlikely(status != UCS_OK)) {
        return UCS_STATUS_PTR(status);
    }

    /* Only once the "expected" value is set - incoming messages are treated */
#if ENABLE_MT
    ucs_memory_cpu_store_fence();
#endif
    UCG_OVER_UCT_OP_EXPECTED(op)->local_id = expected.local_id;

    /* Start the first step, which may actually complete the entire operation */
    ret = ucg_over_uct_execute_op(op);

    op->super.flags = UCS_PMODULE_TARGET_ACTION_FLAG_ASYNC_COMPLETION;

    return ret;
}

unsigned ucg_over_uct_plan_progress(ucs_pmodule_target_action_t *action)
{
    int resend;
    unsigned ret;
    ucg_over_uct_group_ctx_t *gctx;
    ucg_over_uct_op_t *op = ucs_derived_of(action, ucg_over_uct_op_t);
    ucs_status_t status   = op->super.status;

    if (ucs_unlikely(status != UCS_INPROGRESS)) {
        return 1;
    }

    /* In case of resends we lock to avoid collision with the async. thread */
    gctx   = ucs_derived_of(op->super.plan, ucg_over_uct_plan_t)->super.gctx;
    resend = !ucs_list_is_empty(&gctx->resend_head);
    if (ucs_unlikely(resend && !ucs_recursive_spin_trylock(&gctx->resend_lock))) {
        return 0;
    }

    if (ucs_unlikely(((ret = op->progress_f(op->iface)) == 0) && resend)) {
        ucg_over_uct_async_resend(0, 0, gctx);
        ret = 1;
    }

end_progress:
    if (ucs_unlikely(resend)) {
        ucs_recursive_spin_unlock(&gctx->resend_lock);
    }
    return ret;
}
