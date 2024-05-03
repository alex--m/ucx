/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_ucp.h"

#include <ucs/debug/log_def.h>
#include <ucp/core/ucp_request.inl> /* For @ref ucp_recv_desc_release */
// TODO: encapsulate ucp_recv_desc_release in ucg_group.c and get rid of this

#ifdef DEBUG
#define UCS_F_DEBUG_OR_INLINE inline
#else
#define UCS_F_DEBUG_OR_INLINE UCS_F_ALWAYS_INLINE
#endif

/******************************************************************************
 *                                                                            *
 *                         Inter-operation Barriers                           *
 *                                                                            *
 ******************************************************************************/

static UCS_F_DEBUG_OR_INLINE void
ucg_over_ucp_comp_set_trigger_f(ucs_pmodule_target_t *target, ucg_op_t *op,
                                 ucs_pmodule_target_action_trigger_f trigger_f)
{
    ucg_over_ucp_plan_t *plan      = ucs_derived_of(op->plan,
                                                    ucg_over_ucp_plan_t);
    ucg_over_ucp_group_ctx_t *gctx = plan->gctx;

    ucs_list_for_each(plan, &gctx->plan_head, list) {
        plan->super.trigger_f = trigger_f;
    }
}

static UCS_F_DEBUG_OR_INLINE void
ucg_over_ucp_comp_barrier_lock(ucs_pmodule_target_t *target, ucg_op_t *op)
{
    ucg_over_ucp_comp_set_trigger_f(target, op, ucg_over_ucp_plan_barrier);
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
ucg_over_ucp_comp_barrier_unlock(ucs_pmodule_target_t *target, ucg_op_t *op)
{
    ucs_queue_head_t barriered;
    ucs_pmodule_target_action_t *action;
    ucs_pmodule_target_action_progress_f progress_f;

    ucs_status_ptr_t launch   = NULL;
    ucg_over_ucp_plan_t *plan = ucs_derived_of(op->plan, ucg_over_ucp_plan_t);

    /* Reset the trigger function (in a way supporting derived components) */
    ucg_over_ucp_comp_set_trigger_f(target, op, plan->super.component->trigger);

    /* Take every operation in the pending queue and re-launch it */
    ucs_queue_head_init(&barriered);
    ucs_queue_splice(&barriered, &target->pending);
    ucs_queue_for_each_extract(action, &barriered, queue,
                               !UCS_STATUS_IS_ERR(launch)) {
        launch = ucs_pmodule_target_launch(action->plan, action->id,
                                           action->req, &progress_f);
    }

    return UCS_PTR_IS_PTR(launch) ? UCS_OK : UCS_PTR_RAW_STATUS(launch);
}

/******************************************************************************
 *                                                                            *
 *                         Operation Step Completion                          *
 *                                                                            *
 ******************************************************************************/

static void UCS_F_DEBUG_OR_INLINE
ucg_over_ucp_comp_phase_init(ucg_over_ucp_plan_phase_t *phase)
{
    ucg_group_member_index_t index, count;
    size_t single;

    switch (phase->init_act) {
    case UCG_OVER_UCP_PLAN_INIT_ACTION_NONE:
        break;

    case UCG_OVER_UCP_PLAN_INIT_ACTION_COPY_SEND_TO_RECV:
        single = ucp_dt_length(phase->tx.dt, phase->tx.count, NULL, NULL);
        (void)memcpy(phase->rx.buffer, phase->tx.buffer, single);
        break;

    case UCG_OVER_UCP_PLAN_INIT_ACTION_BRUCK_INIT:
        index  = phase->me;
        count  = phase->dest.ep_cnt + 1;
        single = ucp_dt_length(phase->tx.dt, phase->tx.count, NULL, NULL);
        ucs_assert(count > 1);

        memcpy(phase->rx.buffer,
               UCS_PTR_BYTE_OFFSET(phase->tx.buffer, index * single),
               (count - index) * single);

        if (index != 0) {
            memcpy(UCS_PTR_BYTE_OFFSET(phase->rx.buffer,
                                       (count - index) * single),
                   phase->tx.buffer, index * single);
        }

        break;
    }
}

static int UCS_F_DEBUG_OR_INLINE
ucg_over_ucp_comp_phase_finish(ucg_over_ucp_plan_phase_t *phase,
                               ucg_over_ucp_op_t *op)
{
    ucg_group_member_index_t i, index, count;
    const ucg_collective_params_t *params;
    size_t single, dest;
    int8_t *temp;
    int ret = 0;

    switch (phase->fini_act) {
    case UCG_OVER_UCP_PLAN_FINI_ACTION_NONE:
        break;

    case UCG_OVER_UCP_PLAN_FINI_ACTION_LAST:
        ret = 1;
        break;

    case UCG_OVER_UCP_PLAN_FINI_ACTION_BRUCK_LAST:
        ret = 1;
        /* no break */
    case UCG_OVER_UCP_PLAN_FINI_ACTION_BRUCK:
        index  = phase->me;
        count  = phase->dest.ep_cnt + 1;
        single = ucp_dt_length(phase->tx.dt, phase->tx.count, NULL, NULL);
        temp   = (int8_t*)ucs_malloc(single * count, "ucg_over_ucp_bruck_temp");
        ucs_assert_always(temp != NULL);

        for (i = 0; i < count; i++) {
            dest = (index - i + count) % count;
            memcpy(UCS_PTR_BYTE_OFFSET(temp, dest * single),
                   UCS_PTR_BYTE_OFFSET(phase->rx.buffer, i * single), single);
        }
        memcpy(phase->tx.buffer, temp, single * count);
        ucs_free(temp);
        break;

    case UCG_OVER_UCP_PLAN_FINI_ACTION_COPY_TEMP_TO_RECV_LAST:
        ret = 1;
        /* no break */
    case UCG_OVER_UCP_PLAN_FINI_ACTION_COPY_TEMP_TO_RECV:
        count  = phase->dest.ep_cnt + 1;
        single = ucp_dt_length(phase->rx.dt, phase->rx.count, NULL, NULL);
        params = (const ucg_collective_params_t*)&op->super.plan->params;
        ucs_assert(params->recv.buffer != phase->rx.buffer);
        memcpy(params->recv.buffer, phase->rx.buffer, single * count);
        break;
    }

    return ret;
}

static int UCS_F_DEBUG_OR_INLINE
ucg_over_ucp_comp_op_async_is_blocked(ucg_op_t *op)
{
    ucg_over_ucp_plan_t *plan      = ucs_derived_of(op->plan,
                                                    ucg_over_ucp_plan_t);
    ucg_over_ucp_group_ctx_t *gctx = (ucg_over_ucp_group_ctx_t*)plan->gctx;

    return ucs_async_is_blocked(&gctx->worker->async);
}

/* WARNING: some callbacks use longjmp() - so this function may not return */
static void UCS_F_DEBUG_OR_INLINE
ucg_over_ucp_comp_op_cb(ucg_op_t *op, int is_op_mp, ucs_status_t status)
{
    ucg_collective_comp_cb_t cb;
    int is_async = op->flags & UCS_PMODULE_TARGET_ACTION_FLAG_ASYNC_COMPLETION;
    void *req    = op->req;

#if ENABLE_MT
    if (ucg_over_ucp_comp_op_async_is_blocked(op)) {
        is_async = 1;
    }
#endif

    if (is_op_mp) {
        ucs_mpool_put_inline(op);
    }

    cb = ucg_global_params.completion.comp_cb_f[is_async][status != UCS_OK][0];
    cb(req, status);
}

static void UCS_F_DEBUG_OR_INLINE
ucg_over_ucp_comp_last_step_cb(ucg_op_t *op, int is_op_mp, ucs_status_t status)
{
    UCS_PROFILE_REQUEST_EVENT(op->req, "complete_coll", 0);
    ucs_trace_req("collective returning completed request=%p (status: %s)",
                  op->req, ucs_status_string(status));

    ucg_over_ucp_comp_op_cb(op, is_op_mp, status);

    /* Mark this as complete */
    ucs_assert(status     != UCS_INPROGRESS);
    ucs_assert(op->status == UCS_INPROGRESS);
    op->status = status;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_ptr_t
ucg_over_ucp_comp_step_cb(ucg_over_ucp_op_t *op)
{
    ucg_over_ucp_plan_phase_t *phase = ++(op->phase);
    op->pending                      = phase->dest.ep_cnt;

    ucg_over_ucp_comp_phase_init(phase);

    return ucg_over_ucp_execute(op, 1);
}

/* The prototype should match ucp_send_nbx_callback_t() */
static void UCS_F_DEBUG_OR_INLINE
ucg_over_ucp_comp_send_cb(void *request, ucs_status_t status, void *user)
{
    ucg_over_ucp_plan_phase_t *phase;
    ucg_over_ucp_op_t *op = request;

    if (ucs_unlikely(status != UCS_OK)) {
        ucg_over_ucp_comp_last_step_cb(&op->super, 1, status);
        return;
    }

    ucs_assert(op->pending);
    if (ucs_likely((--(op->pending)) == 0)) {
        phase = op->phase;
        if (ucg_over_ucp_comp_phase_finish(phase, op)) {
            ucg_over_ucp_comp_last_step_cb(&op->super, 1, status);
        } else {
            ucg_over_ucp_comp_step_cb(op);
        }
    }
}

static void UCS_F_DEBUG_OR_INLINE
ucg_over_ucp_comp_recv_cb(void *request, ucs_status_t status,
                          const ucp_tag_recv_info_t *tag_info,
                          void *user_data)
{
    ucs_status_ptr_t status_ptr;
    ucg_over_ucp_plan_phase_t *phase;
    ucg_over_ucp_op_t *op = request;

    if (ucs_unlikely(status != UCS_OK)) {
        ucg_over_ucp_comp_last_step_cb(&op->super, 1, status);
        return;
    }

    ucs_assert(op->pending);
    if (ucs_likely((--(op->pending)) == 0)) {
        phase       = op->phase;
        op->pending = phase->rx_cnt;
        status_ptr  = ucg_over_ucp_execute(op, 0);
        if (ucs_unlikely(UCS_PTR_IS_ERR(status_ptr))) {
            ucg_over_ucp_comp_last_step_cb(&op->super, 1,
                                           UCS_PTR_RAW_STATUS(status_ptr));
        }
    }
}

static void UCS_F_DEBUG_OR_INLINE
ucg_over_ucp_comp_reduce_cb(void *request, ucs_status_t status,
                            const ucp_tag_recv_info_t *tag_info,
                            void *user_data)
{
    if (ucs_likely(status == UCS_OK)) {
        ucg_over_ucp_op_t *op     = request;
        ucg_over_ucp_plan_t *plan = ucs_derived_of(op->super.plan,
                                                   ucg_over_ucp_plan_t);

        ucs_assert(0); /* TODO: fix the following call: src==dst ! */
        plan->reduce_f(op->phase->rx.buffer, op->phase->rx.buffer,
                       (uintptr_t)&op->super);
    }

    ucg_over_ucp_comp_recv_cb(request, status, tag_info, user_data);
}
