/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucf_file.h"
#include "ucf_context.h"
#include <ucp/core/ucp_worker.h>

/******************************************************************************
 *                                                                            *
 *                                Pipes Creation                              *
 *                                                                            *
 ******************************************************************************/

static void ucf_file_copy_params(ucf_file_params_t *dst,
                                  const ucf_file_params_t *src)
{
    size_t file_params_size = sizeof(src->field_mask) +
                               ucs_offsetof(ucf_params_t, field_mask);

    if (src->field_mask != 0) {
        enum ucf_file_params_field msb_flag = UCS_BIT((sizeof(uint64_t) * 8)
                - 1 - ucs_count_leading_zero_bits(src->field_mask));
        ucs_assert((msb_flag & src->field_mask) == msb_flag);

        switch (msb_flag) {
        case UCF_FILE_PARAM_FIELD_NAME:
        case UCF_FILE_PARAM_FIELD_UCP_WORKER:
            file_params_size = ucs_offsetof(ucf_file_params_t, group);
            break;

        case UCF_FILE_PARAM_FIELD_UCG_GROUP:
            file_params_size = ucs_offsetof(ucf_file_params_t, access_type);
            break;

        case UCF_FILE_PARAM_FIELD_ACCESS_TYPE:
            file_params_size = ucs_offsetof(ucf_file_params_t, granularity);
            break;

        case UCF_FILE_PARAM_FIELD_GRANULARITY:
            file_params_size = sizeof(ucf_file_params_t);
            break;
        }
    }

    memcpy(dst, src, file_params_size);
}

ucs_status_t ucf_file_create(const ucf_file_params_t *params,
                             ucf_file_h *file_p)
{
    ucs_status_t status;
    ucp_worker_h worker;
    ucb_context_t *ucb_ctx;
    ucg_context_t *ucg_ctx;
    ucf_context_t *ucf_ctx;
    ucf_file_t *file;

    ucs_pmodule_target_params_t target_params = {
        .field_mask     = UCS_PMODULE_TARGET_PARAM_FIELD_LEGROOM |
                          UCS_PMODULE_TARGET_PARAM_FIELD_PER_FRAMEWORK,
        .target_legroom = sizeof(ucf_file_t) - sizeof(file->super),
        .per_framework  = params
    };

    uint64_t field_mask = params->field_mask;
    if (field_mask & UCF_FILE_PARAM_FIELD_UCP_WORKER) {
        worker = params->worker;
    } else if (field_mask & UCG_GROUP_PARAM_FIELD_UCB_PIPES) {
        field_mask |= UCF_FILE_PARAM_FIELD_UCG_GROUP;
        worker = params->group->worker;
    } else {
        ucs_error("A UCP worker or UCG group object is required for a file");
        return UCS_ERR_INVALID_PARAM;
    }

    if (params->field_mask & UCF_FILE_PARAM_FIELD_NAME) {
        target_params.name        = params->name;
        target_params.field_mask |= UCS_PMODULE_TARGET_PARAM_FIELD_NAME;
    }

    /* Allocate the file as a superset of a target */
    ucb_ctx = ucs_container_of(worker->context, ucb_context_t, ucp_ctx);
    ucg_ctx = ucs_container_of(ucb_ctx,         ucg_context_t, ucb_ctx);
    ucf_ctx = ucs_container_of(ucg_ctx,         ucf_context_t, ucg_ctx);
    status = ucs_pmodule_framework_target_create(&ucf_ctx->super, &target_params,
                                                 (ucs_pmodule_target_t**)&file);
    if (status != UCS_OK) {
        return status;
    }

    /* Fill in the file fields */
    ucf_file_copy_params(&file->params, params);

    *file_p = file;
    return UCS_OK;
}

void ucf_file_destroy(ucf_file_h file)
{
    ucs_pmodule_framework_target_destroy(file->super.context, &file->super);
}


/******************************************************************************
 *                                                                            *
 *                                 Group Usage                                *
 *                                                                            *
 ******************************************************************************/

UCS_PROFILE_FUNC(ucs_status_t, ucf_iop_create,
        (file, params, iop), ucf_file_h file,
        const ucf_iop_params_t *params, ucf_iop_h *iop)
{
    unsigned hash = 7; // TODO

    return ucs_pmodule_target_get_plan(&file->super, hash, params, 0 /* TBD */,
                                       (ucs_pmodule_target_plan_t**)iop);
}

UCS_PROFILE_FUNC(ucs_status_ptr_t, ucf_iop_start,
                 (iop, req, progress_f_p), ucf_iop_h iop, void *req,
                 ucf_iop_progress_t *progress_f_p)
{
    return ucg_collective_start(iop, req, progress_f_p);
}

ucf_iop_progress_t ucf_iop_get_progress(ucf_iop_h iop)
{
    return (ucf_iop_progress_t)((ucs_pmodule_target_plan_t*)iop)->progress_f;
}

ucs_status_t ucf_iop_cancel(ucf_iop_h iop, void *req)
{
    return UCS_ERR_NOT_IMPLEMENTED; // TODO: implement...
}

void ucf_iop_destroy(ucf_iop_h iop)
{
    // TODO
}
