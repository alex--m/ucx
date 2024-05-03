/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucb_pipes.h"
#include "ucb_context.h"
#include <ucg/api/ucg_mpi.h>
#include <ucp/core/ucp_worker.h>

/******************************************************************************
 *                                                                            *
 *                                Pipes Creation                              *
 *                                                                            *
 ******************************************************************************/

static void ucb_pipes_copy_params(ucb_pipes_params_t *dst,
                                  const ucb_pipes_params_t *src)
{
    size_t pipes_params_size = sizeof(src->field_mask) +
                               ucs_offsetof(ucg_params_t, field_mask);

    if (src->field_mask != 0) {
        enum ucb_pipes_params_field msb_flag = UCS_BIT((sizeof(uint64_t) * 8)
                - 1 - ucs_count_leading_zero_bits(src->field_mask));
        ucs_assert((msb_flag & src->field_mask) == msb_flag);

        switch (msb_flag) {
        case UCB_PIPES_PARAM_FIELD_NAME:
            pipes_params_size = ucs_offsetof(ucb_pipes_params_t, worker);
            break;

        case UCB_PIPES_PARAM_FIELD_UCP_WORKER:
            pipes_params_size = ucs_offsetof(ucb_pipes_params_t, cd_master_ep_p);
            break;

        case UCB_PIPES_PARAM_FIELD_CD_MASTER:
            pipes_params_size = sizeof(ucb_pipes_params_t);
            break;
        }
    }

    memcpy(dst, src, pipes_params_size);
}

ucs_status_t ucb_pipes_create(const ucb_pipes_params_t *params,
                              ucb_pipes_h *pipes_p)
{
    ucb_context_t *ctx;
    ucb_pipes_t *pipes;
    ucs_status_t status;

    ucs_pmodule_target_params_t target_params = {
        .field_mask     = UCS_PMODULE_TARGET_PARAM_FIELD_LEGROOM |
                          UCS_PMODULE_TARGET_PARAM_FIELD_PER_FRAMEWORK,
        .target_legroom = sizeof(ucb_pipes_t) - sizeof(pipes->super),
        .per_framework  = params
    };

    uint64_t field_mask = params->field_mask;
    if (!(field_mask & UCB_PIPES_PARAM_FIELD_UCP_WORKER)) {
        ucs_error("A UCP worker object is required for the pipes");
        return UCS_ERR_INVALID_PARAM;
    }

    if (params->field_mask & UCB_PIPES_PARAM_FIELD_NAME) {
        target_params.name        = params->name;
        target_params.field_mask |= UCS_PMODULE_TARGET_PARAM_FIELD_NAME;
    }

    /* Allocate the group as a superset of a target */
    ctx    = ucs_container_of(params->worker->context, ucb_context_t, ucp_ctx);
    status = ucs_pmodule_framework_target_create(&ctx->super, &target_params,
                                                 (ucs_pmodule_target_t**)&pipes);
    if (status != UCS_OK) {
        return status;
    }

    /* Fill in the group fields */
    pipes->worker        = params->worker;
    pipes->next_batch_id = 1;

    ucb_pipes_copy_params(&pipes->params, params);

    *pipes_p = pipes;
    return UCS_OK;
}

void ucb_pipes_destroy(ucb_pipes_h pipes)
{
    ucs_pmodule_framework_target_destroy(pipes->super.context, &pipes->super);
}


/******************************************************************************
 *                                                                            *
 *                                 Group Usage                                *
 *                                                                            *
 ******************************************************************************/

UCS_PROFILE_FUNC(ucs_status_t, ucb_batch_create,
        (pipes, params, coll), ucb_pipes_h pipes,
        const ucb_batch_params_t *params, ucg_coll_h *coll)
{
    unsigned hash = 7; // TODO

    return ucs_pmodule_target_get_plan(&pipes->super, hash, params, 0 /* TBD */,
                                       (ucs_pmodule_target_plan_t**)coll);
}

UCS_PROFILE_FUNC(ucs_status_ptr_t, ucb_batch_start,
                 (coll, req, progress_f_p), ucg_coll_h coll, void *req,
                 ucb_batch_progress_t *progress_f_p)
{
    ucs_pmodule_target_plan_t *plan = (ucs_pmodule_target_plan_t*)coll;
    ucb_pipes_t *pipes              = ucs_derived_of(plan->target, ucb_pipes_t);
    ucb_batch_id_t batch_id         = ucs_atomic_fadd64(&pipes->next_batch_id, 1);

    ucs_trace_req("ucb_batch_start: op=%p req=%p", coll, req);

    return ucs_pmodule_target_launch(plan, batch_id, req,
        (ucs_pmodule_target_action_progress_f*)progress_f_p);
}

ucb_batch_progress_t ucb_batch_get_progress(ucg_coll_h coll)
{
    return (ucb_batch_progress_t)
        ((ucs_pmodule_target_plan_t*)coll)->progress_f;
}

ucs_status_t ucb_batch_cancel(ucg_coll_h coll, void *req)
{
    return UCS_ERR_NOT_IMPLEMENTED; // TODO: implement...
}

void ucb_batch_destroy(ucg_coll_h coll)
{
    // TODO
}
