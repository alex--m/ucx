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

#include "over_uct.h"
#include "over_uct_comp.inl"

extern ucs_config_field_t ucg_over_ucp_config_table[];
ucs_config_field_t ucg_over_uct_config_table[] = {
    {"", "", NULL,
     ucs_offsetof(ucg_over_uct_config_t, super),
     UCS_CONFIG_TYPE_TABLE(ucg_over_ucp_config_table)},

    {"VOLATILE_DATATYPES", "n",
     "Should datatypes be treated as volatile and reloaded on each invocation.\n",
     ucs_offsetof(ucg_over_uct_config_t, is_dt_volatile), UCS_CONFIG_TYPE_BOOL},

    {"INCAST_MEMBER_THRESH", "5", "How many members merit an incast transport",
     ucs_offsetof(ucg_over_uct_config_t, incast_member_thresh), UCS_CONFIG_TYPE_UINT},
    // TODO: transport should provide information to choose this automatically

    {"BCAST_MEMBER_THRESH", "5", "How many members merit a broadcast transport",
     ucs_offsetof(ucg_over_uct_config_t, bcast_member_thresh), UCS_CONFIG_TYPE_UINT},
    // TODO: transport should provide information to choose this automatically

    {"ZCOPY_RKEY_THRESH", "INF", "Threshseold for using remote keys for zero-copy",
     ucs_offsetof(ucg_over_uct_config_t, zcopy_rkey_thresh), UCS_CONFIG_TYPE_MEMUNITS},

    {"TOTAL_ZCOPY_THRESH", "8K", "Threshseold of TX size times destinations for zero-copy",
     ucs_offsetof(ucg_over_uct_config_t, zcopy_total_thresh), UCS_CONFIG_TYPE_MEMUNITS},
    // TODO: transport should provide information to choose this automatically

    {"BCOPY_TO_ZCOPY_OPT", "1", "Switch for optimization from bcopy to zcopy",
     ucs_offsetof(ucg_over_uct_config_t, bcopy_to_zcopy_opt), UCS_CONFIG_TYPE_UINT},

    {"MEM_REG_OPT_CNT", "10", "Operation counter before registering the memory",
     ucs_offsetof(ucg_over_uct_config_t, mem_reg_opt_cnt), UCS_CONFIG_TYPE_UINT},

    {"RESEND_TIMER_TICK", "100ms", "Resolution for (async) resend timer",
     ucs_offsetof(ucg_over_uct_config_t, resend_timer_tick), UCS_CONFIG_TYPE_TIME},

#ifdef ENABLE_FAULT_TOLERANCE
    {"FT_TIMER_TICK", "100ms", "Resolution for (async) fault-tolerance timer",
     ucs_offsetof(ucg_over_uct_config_t, ft_timer_tick), UCS_CONFIG_TYPE_TIME},
#endif

    {NULL}
};

static UCS_F_DEBUG_OR_INLINE int
ucg_over_uct_async_resend_lock(ucg_over_uct_group_ctx_t *gctx)
{
    if (ucs_unlikely(!ucs_list_is_empty(&gctx->resend_head))) {
        ucs_recursive_spin_lock(&gctx->resend_lock);
        return 1;
    }

    return 0;
}

UCS_PROFILE_FUNC(ucs_status_t, ucg_over_uct_payload_handler,
                 (cctx, data, length, am_flags),
                 void *cctx, void *data, size_t length, unsigned am_flags)
{
    ucg_group_h group;
    ucs_status_t status;
    ucg_coll_id_t coll_id;
    ucg_group_id_t group_id;
    ucg_over_uct_group_ctx_t *gctx;
    ucg_over_uct_comp_slot_t *slot;
    const ucg_over_uct_header_t *header;
    ucg_over_uct_header_step_t *expected;
    int async_resend_locked, is_async;
    ucg_over_uct_op_t *op;

#if ENABLE_MT
    ucs_ptr_array_locked_t *msg_array;
    ucs_ptr_array_locked_t *tmp_array;
#else
    ucs_ptr_array_t *msg_array;
    ucs_ptr_array_t *tmp_array;
#endif

    ucg_over_uct_ctx_t *ctx = cctx;

    /* Find the Group context, based on the ID received in the header */
    ucs_assert(length >= sizeof(header));
    header = (ucg_over_uct_header_t*)data;
    ucs_assert(header->header != 0); /* group_id >= UCG_GROUP_FIRST_GROUP_ID */
    group_id = header->group_id;
    ucs_assert(group_id >= UCG_GROUP_FIRST_GROUP_ID);

    /* Intentionally disregard lock, in order to reduce the latency */
    if (ucs_likely(ucs_ptr_array_lookup(&ctx->super.group_by_id.super, group_id, gctx))
#if ENABLE_MT
        || ucs_unlikely(ucs_ptr_array_locked_lookup(&ctx->super.group_by_id, group_id,
                        UCS_PTR_ARRAY_LOCKED_FLAG_KEEP_LOCK_IF_NOT_FOUND, (void**)&gctx)))
#else
        )
#endif
    {
        /* Find the slot to be used, based on the ID received in the header */
        coll_id  = header->msg.coll_id;
        slot     = &gctx->slots[coll_id % UCG_OVER_UCT_MAX_CONCURRENT_OPS];
        op       = &slot->op;
        expected = UCG_OVER_UCT_OP_EXPECTED(op);
        ucs_assert((expected->coll_id != coll_id) ||
                   (expected->step_idx <= header->msg.step_idx));

        /*
         * Test if this message was received from within the call-stack of
         * the user's original collective invocation. If so - instead of
         * locking (no need for this operation because it wouldn't be on
         * the resend list) or processing the incoming message (which would
         * possibly cause a nested completion we want to avoid here), simply
         * return and this message would be handled in an outer stack frame.
         */
        is_async = op->super.flags &
                   UCS_PMODULE_TARGET_ACTION_FLAG_ASYNC_COMPLETION;

        /* Consume the message if it fits the current collective and step index */
        if (ucs_likely(is_async && (header->msg.local_id == expected->local_id))) {
            /* Make sure the packet indeed belongs to the collective currently on */
            data    = ((ucg_over_uct_header_t*)data) + 1;
            length -= sizeof(ucg_over_uct_header_t);

            ucs_trace_req("ucg_over_uct_payload_handler APPLY: coll_id %u "
                          "step_idx %u pending %i data %p", header->msg.coll_id,
                          header->msg.step_idx, op->comp.count, data);

            /* Possibly lock to avoid a collision with the async. resend thread */
            async_resend_locked = ucg_over_uct_async_resend_lock(gctx);

            /* Handle the incoming message */
            ucg_over_uct_comp_recv_cb(op, *header, (uint8_t*)data, length,
                                      am_flags, NULL, NULL);

            status = UCS_OK;
            goto handled;
        }

        async_resend_locked = 0;
        msg_array           = &slot->messages;
        group               = gctx->super.group;
        ucs_trace_req("ucg_over_uct_payload_handler STORE: group_id %u "
                      "coll_id %u expected_id %u step_idx %u expected_idx %u",
                      header->group_id, header->msg.coll_id, expected->coll_id,
                      header->msg.step_idx, expected->step_idx);
    } else {
        /*
         * This message is destined for a group that has not been created
         * (in this process) yet. Next, a temporary array will be created for
         * this and all further messages to this future group (by group ID). It
         * is possible such an array has already been created, in which case
         * just add to that array and dispose of the redundant one (created only
         * to ensure atomicity).
         */
        ucs_warn("Unexpected message arrived for an uninitialized (yet) group");
        async_resend_locked = 0;
        group               = ctx->dummy_group;
        tmp_array           = UCS_ALLOC_CHECK(sizeof(*msg_array),
                                              "unexpected group");

#if ENABLE_MT
        expected = NULL;
        ucs_ptr_array_locked_init(tmp_array, "unexpected group messages");
#else
        ucs_ptr_array_init(tmp_array, "unexpected group messages");
#endif
        ucs_ptr_array_locked_set_first(&ctx->unexpected, group_id, tmp_array,
                                       (void**)&msg_array);
        if (tmp_array != msg_array) {
            ucs_free(tmp_array);
        }
    }

    /* store the message for a future step or operation */
    status = ucg_group_am_msg_store(data, length, am_flags, group, msg_array);

handled:
#if ENABLE_MT
    if (!expected) {
        ucs_ptr_array_locked_release_lock(&ctx->super.group_by_id);
    }
#endif
    if (ucs_unlikely(async_resend_locked)) {
        ucs_recursive_spin_unlock(&gctx->resend_lock);
    }
    return status;
}

static void ucg_over_uct_payload_msg_dump(ucp_worker_h worker,
                                          uct_am_trace_type_t type, uint8_t id,
                                          const void *data, size_t length,
                                          char *buffer, size_t max)
{
    const ucg_over_uct_header_t *header = (const ucg_over_uct_header_t*)data;
    snprintf(buffer, max, "UCG_OVER_UCT [group_id %u coll_id %u step_idx %u "
             "offset %lu length %lu]", (unsigned)header->group_id,
             (unsigned)header->msg.coll_id, (unsigned)header->msg.step_idx,
             (uint64_t)header->remote_offset, length - sizeof(*header));
}

UCS_PROFILE_FUNC(ucs_status_t, ucg_over_uct_wireup_handler,
                 (cctx, data, length, am_flags),
                 void *cctx, void *data, size_t length, unsigned am_flags)
{
    ucg_over_uct_group_ctx_t *gctx;
    ucg_over_uct_header_t *header = (ucg_over_uct_header_t*)data;
    ucg_over_uct_ctx_t *ctx       = cctx;

    ucs_assert(length >= sizeof(header));
    ucs_assert(header->group_id >= UCG_GROUP_FIRST_GROUP_ID);

    /* Find the Group context, based on the ID received in the header */
    if (ucs_ptr_array_locked_lookup(&ctx->super.group_by_id, header->group_id,
                                    0, (void**)&gctx)) {
        return ucg_group_store_wireup_message(gctx->super.group, data, length,
                                              am_flags);
    }

    /* An unexpected wireup message, prior to group creation! */
    ucs_assert(am_flags & UCT_CB_PARAM_FLAG_DESC); /* Dummies do not allocate */
    return ucg_group_store_wireup_message(ctx->dummy_group, data, length,
                                          am_flags);
}

static void ucg_over_uct_wireup_msg_dump(ucp_worker_h worker,
                                         uct_am_trace_type_t type,
                                         uint8_t id, const void *data,
                                         size_t length, char *buffer, size_t max)
{
    const ucg_over_uct_header_t *header = (const ucg_over_uct_header_t*)data;
    snprintf(buffer, max, "UCG_OVER_UCT_WIREUP [group_id %u step_idx %u length %lu]",
             (unsigned)header->group_id, (unsigned)header->msg.step_idx,
             length - sizeof(*header));
}

/******************************************************************************
 *                                                                            *
 *                            Component Management                            *
 *                                                                            *
 ******************************************************************************/

extern ucs_pmodule_component_t ucg_over_uct_component;
static ucs_status_t ucg_over_uct_query(ucs_list_link_t *desc_head)
{
    ucg_plan_desc_t *desc     = UCS_ALLOC_TYPE_CHECK(ucg_plan_desc_t);
    desc->super.component     = &ucg_over_uct_component;
    desc->modifiers_supported = (unsigned)-1; /* supports ANY collective */
    desc->flags               = 0;

    ucs_list_add_head(desc_head, &desc->super.list);
    return UCS_OK;
}

static void
ucg_over_uct_disable_timers(ucg_over_uct_group_ctx_t *gctx)
{
    ucs_assert(gctx->timer_id != -1);
    ucg_context_unset_async_timer(&gctx->super.worker->async, gctx->timer_id);
    gctx->timer_id = -1;
}

static ucs_status_t
ucg_over_uct_enable_timers(ucg_over_uct_group_ctx_t *gctx)
{
    ucs_status_t status;

    ucp_worker_h worker     = gctx->super.worker;
    ucg_over_uct_ctx_t *ctx = (ucg_over_uct_ctx_t*)gctx->super.ctx;
    ucs_time_t interval     = ucs_time_from_sec(ctx->config.resend_timer_tick);

    ucs_assert(gctx->timer_id == -1);

    status = ucg_context_set_async_timer(&worker->async,
                                         ucg_over_uct_async_resend,
                                         gctx, interval, &gctx->timer_id);
    if (status != UCS_OK) {
        return status;
    }

#ifdef ENABLE_FAULT_TOLERANCE
    if (ucg_params.fault.mode > UCG_FAULT_IS_FATAL) {
        interval = ucs_time_from_sec(tctx->config.ft_timer_tick);
        status = ucg_context_set_async_timer(&worker->async,
                                             ucg_over_uct_async_ft, gctx,
                                             interval, &gctx->ft_timer_id);
        if (status != UCS_OK) {
            ucg_over_uct_disable_timers(gctx);
            return status;
        }
    }
#endif

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE void
ucg_over_uct_req_set_resend(ucg_over_uct_op_t *op, int enable)
{
    ucg_over_ucp_plan_t *plan      = ucs_derived_of(op->super.plan,
                                                    ucg_over_ucp_plan_t);
    ucg_over_uct_group_ctx_t *gctx = (ucg_over_uct_group_ctx_t*)plan->gctx;

    ucs_recursive_spin_lock(&gctx->resend_lock);

    if (enable) {
        if (ucs_unlikely(gctx->timer_id == -1)) {
            ucg_over_uct_enable_timers(gctx);
        }

        if (ucs_list_is_empty(&op->resend_list)) {
            ucs_list_add_tail(&gctx->resend_head, &op->resend_list);
        }
    } else {
        ucs_list_del(&op->resend_list);
        ucs_list_head_init(&op->resend_list);
        ucs_assert(ucs_list_is_empty(&op->resend_list));
    }

    ucs_recursive_spin_unlock(&gctx->resend_lock);
}

void ucg_over_uct_schedule_resend(ucg_over_uct_op_t *op)
{
    ucg_over_uct_req_set_resend(op, 1);
}

void ucg_over_uct_cancel_resend(ucg_over_uct_op_t *op)
{
    ucg_over_uct_req_set_resend(op, 0);
}

#ifdef ENABLE_FAULT_TOLERANCE
static void ucg_over_uct_async_ft(int id, ucs_event_set_types_t events, void *arg)
{
    if ((status == UCS_INPROGRESS) &&
            !(req->op->flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_FT_ONGOING)) {
        ucg_over_uct_plan_phase_t *phase = req->op->phase;
        if (phase->tx.ep_cnt == 1) {
            ucg_ft_start(group, phase->indexes[0], phase->single_ep, &phase->handles[0]);
        } else {
            unsigned peer_idx = 0;
            while (peer_idx < phase->tx.ep_cnt) {
                ucg_ft_start(group, phase->indexes[peer_idx],
                        phase->multi_eps[peer_idx], &phase->handles[peer_idx]);
                peer_idx++;
            }
        }

        req->op->flags |= UCG_OVER_UCT_PLAN_PHASE_FLAG_FT_ONGOING;
    }
}
#endif

UCP_DEFINE_AM(UCP_FEATURE_GROUPS, ucg_over_uct_payload, ucg_over_uct_payload_handler,
              ucg_over_uct_payload_msg_dump, UCT_CB_FLAG_ALT_ARG);
UCP_DEFINE_AM(UCP_FEATURE_GROUPS, ucg_over_uct_wireup, ucg_over_uct_wireup_handler,
              ucg_over_uct_wireup_msg_dump, UCT_CB_FLAG_ALT_ARG);
// Note: wireup should move inside ucg_group.c, but UCG doesn't have a group_id
//       to group_ptr mapping, so no good way to deliver AMs to their group.

ucs_status_t ucg_over_uct_init(ucg_comp_ctx_h cctx,
                               const ucs_pmodule_component_params_t *params,
                               ucg_comp_config_t *config)
{
    ucs_status_t status;
    ucg_over_uct_ctx_t *ctx = cctx;

    ucs_assert(ucs_test_all_flags(params->field_mask,
                                  UCS_PMODULE_COMPONENT_PARAMS_FIELD_AM_ID |
                                  UCS_PMODULE_COMPONENT_PARAMS_FIELD_DUMMY_GROUP |
                                  UCS_PMODULE_COMPONENT_PARAMS_FIELD_BARRIR_DELAY));

    ctx->coll_am_id         = (*params->am_id)++;
    ctx->wireup_am_id       = (*params->am_id)++;
    ctx->dummy_group        = params->dummy_group;
    ctx->is_barrier_delayed = params->is_barrier_delayed;

#ifdef ENABLE_FAULT_TOLERANCE
    if (ucg_params.fault.mode > UCG_FAULT_IS_FATAL) {
        return UCS_ERR_UNSUPPORTED;
    }
#endif

    status = ucs_config_parser_clone_opts(config, &ctx->config,
                                          ucg_over_uct_config_table);
    if (status != UCS_OK) {
        return status;
    }

    status = ucg_over_ucp_init(&ctx->super, params, config);
    if (status != UCS_OK) {
        return status;
    }

    ucp_am_handler_ucg_over_uct_payload.alt_arg = ctx;
    status = ucg_context_set_am_handler(ctx->coll_am_id,
                                        &ucp_am_handler_ucg_over_uct_payload);
    if (status != UCS_OK) {
        return status;
    }

    ucp_am_handler_ucg_over_uct_wireup.alt_arg = ctx;
    status = ucg_context_set_am_handler(ctx->wireup_am_id,
                                        &ucp_am_handler_ucg_over_uct_wireup);
    if (status != UCS_OK) {
        return status;
    }

    ucs_ptr_array_locked_init(&ctx->unexpected, "over_uct_unexpected_messages");
    return UCS_OK;
}

void ucg_over_uct_finalize(ucg_comp_ctx_h cctx)
{
    ucg_over_uct_ctx_t *ctx = cctx;
    ucs_ptr_array_locked_cleanup(&ctx->unexpected, 1);
    ucg_over_ucp_finalize(&ctx->super);
}

static UCS_F_ALWAYS_INLINE void
ucg_over_uct_check_prior_messages(ucg_over_uct_group_ctx_t *gctx,
                                  ucg_over_uct_ctx_t *tctx)
{
    unsigned i;
    ucp_recv_desc_t *rdesc;
    ucs_ptr_array_t *unexpected;
    ucg_over_uct_header_t *header;

    ucs_assert(!ucs_recursive_spinlock_is_held(&tctx->unexpected.lock));

    /* Packets may have arrives for this group and need to be stored here */
    if (ucs_ptr_array_locked_lookup(&tctx->unexpected, gctx->super.group_id,
                                    UCS_PTR_ARRAY_LOCKED_FLAG_KEEP_LOCK_IF_FOUND,
                                    (void**)&unexpected)) {

        ucs_assert(!ucs_ptr_array_is_empty(unexpected));

        ucs_ptr_array_for_each(rdesc, i, unexpected) {
            ucs_ptr_array_remove(unexpected, i);

            header = (ucg_over_uct_header_t*)(rdesc + 1);
            ucs_assert(header->group_id >= UCG_GROUP_FIRST_GROUP_ID);
#if ENABLE_MT
            ucs_ptr_array_locked_insert(&gctx->slots[header->msg.coll_id].messages,
#else
            ucs_ptr_array_insert(&gctx->slots[header->msg.coll_id].messages,
#endif
                                 rdesc);
        }

        ucs_ptr_array_cleanup(unexpected, 1);
        ucs_ptr_array_remove(&tctx->unexpected.super, gctx->super.group_id);
        ucs_ptr_array_locked_release_lock(&tctx->unexpected);
        ucs_free(unexpected);
    }

    ucs_assert(!ucs_recursive_spinlock_is_held(&tctx->unexpected.lock));
}

ucs_status_t ucg_over_uct_create(ucg_comp_ctx_h cctx, ucg_group_ctx_h ctx,
                                 ucs_pmodule_target_t *target,
                                 const ucs_pmodule_target_params_t *params)
{
    unsigned i;
    ucs_status_t status;
    ucg_over_uct_group_ctx_t *gctx         = ctx;
    ucg_over_uct_ctx_t *tctx               = cctx;
    const ucg_group_params_t *group_params = params->per_framework;

    /* Initialize collective operation slots, incl. already pending messages */
    for (i = 0; i < UCG_OVER_UCT_MAX_CONCURRENT_OPS; i++) {
        gctx->slots[i].op.fragment_pending = NULL;
        gctx->slots[i].op.frags_allocd     = 0;
        gctx->slots[i].op.comp.func        = NULL;
        gctx->slots[i].op.comp.status      = UCS_OK;
        gctx->slots[i].op.comp.reserved    = 0;

#if ENABLE_MT
        ucs_ptr_array_locked_init(&gctx->slots[i].messages, "ucg_over_uct messages");
#else
        ucs_ptr_array_init(&gctx->slots[i].messages, "ucg_over_uct messages");
#endif
        UCG_OVER_UCT_OP_EXPECTED(&gctx->slots[i].op)->local_id = 0;
        ucs_list_head_init(&gctx->slots[i].op.resend_list);
    }

    gctx->timer_id = -1;
    ucs_list_head_init(&gctx->resend_head);
    ucs_recursive_spinlock_init(&gctx->resend_lock, 0);
    status = ucg_over_ucp_create_common(cctx, &gctx->super, target, group_params);
    if (status != UCS_OK) {
        return status;
    }

    ucg_over_uct_check_prior_messages(gctx, tctx);
    return UCS_OK;
}

void ucg_over_uct_destroy(ucg_group_ctx_h ctx)
{
    unsigned i;
    ucg_over_ucp_plan_t *plan;
    ucg_over_uct_comp_slot_t *slot;
    ucg_over_uct_group_ctx_t *gctx = ctx;
    ucg_over_uct_ctx_t *tctx       = gctx->super.ctx;

    ucs_assert(ucs_list_is_empty(&gctx->resend_head));
    ucs_assert(!ucs_recursive_spinlock_is_held(&gctx->resend_lock));
    if (gctx->timer_id != -1) {
        ucg_over_uct_disable_timers(gctx);
    }
    ucs_recursive_spinlock_destroy(&gctx->resend_lock);

    /* Cleanup left-over messages and outstanding operations */
    for (i = 0; i < UCG_OVER_UCT_MAX_CONCURRENT_OPS; i++) {
        slot = &gctx->slots[i];
        if (UCG_OVER_UCT_OP_EXPECTED(&slot->op)->local_id != 0) {
            ucs_warn("Collective operation #%u was left incomplete (Group #%u)",
                     UCG_OVER_UCT_OP_EXPECTED(&slot->op)->coll_id,
                     gctx->super.group_id);
        }

        if (slot->op.fragment_pending) {
            ucs_free((void*)slot->op.fragment_pending);
        }

        ucg_over_uct_comp_check_pending(&slot->op, 0);
#if ENABLE_MT
        ucs_ptr_array_locked_cleanup(&slot->messages, 0);
#else
        ucs_ptr_array_cleanup(&slot->messages, 0);
#endif
    }

    /* Cleanup plans created for this group */
    while (!ucs_list_is_empty(&gctx->super.plan_head)) {
        plan = ucs_list_head(&gctx->super.plan_head, ucg_over_ucp_plan_t, list);
        ucs_pmodule_target_plan_uncache(plan->super.target, &plan->super);
        ucg_over_uct_plan_destroy(ucs_derived_of(plan, ucg_over_uct_plan_t));
    }

    /* Remove the group from the global storage array */
    ucs_ptr_array_locked_remove(&tctx->super.group_by_id, gctx->super.group_id);

    /* Note: gctx is freed as part of the group object itself */
}

static ucs_status_t ucg_over_uct_handle_fault(ucg_group_ctx_h gctx, uint64_t id)
{
    // ucg_group_member_index_t index = id;
    return UCS_ERR_NOT_IMPLEMENTED;
}

UCS_PMODULE_COMPONENT_DEFINE(ucg_over_uct_component, "over_uct",
                             sizeof(ucg_over_uct_ctx_t),
                             sizeof(ucg_over_uct_group_ctx_t),
                             ucg_over_uct_query, ucg_over_uct_init,
                             ucg_over_uct_finalize, ucg_over_uct_create,
                             ucg_over_uct_destroy, ucg_over_uct_plan_estimate,
                             ucg_over_uct_plan_wrapper, ucg_over_uct_plan_trigger,
                             ucg_over_uct_plan_progress, ucg_over_uct_plan_discard,
                             ucg_over_uct_plan_print, ucg_over_uct_handle_fault,
                             ucg_over_uct_config_table, ucg_over_uct_config_t,
                             "UCG_OVER_UCT_", ucg_components_list);
