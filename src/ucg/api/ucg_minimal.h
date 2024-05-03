/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_MINIMAL_H_
#define UCG_MINIMAL_H_

#include <ucg/api/ucg.h>

BEGIN_C_DECLS

/*
 * Below is a minimal API for broadcasting messages using UCG.
 */

typedef struct ucg_minimal_ctx {
    ucg_context_h       context;
    ucp_worker_h        worker;
    ucg_group_h         group;
    ucg_listener_h      listener;
} ucg_minimal_ctx_t;

enum ucg_minimal_init_flags {
    UCG_MINIMAL_FLAG_SERVER = UCS_BIT(0) /* otherwise act as a client */
};

static inline ucs_status_t
ucg_minimal_init(ucg_minimal_ctx_t *ctx,
                 ucs_sock_addr_t *server_address,
                 unsigned num_connections_to_wait,
                 ucg_collective_comp_cb_t comp_cb,
                 uint64_t flags)
{
    ucs_status_t status;
    ucg_config_t *context_config;
    ucp_context_h ucp_context;
    ucp_worker_params_t worker_params = {0};
    ucp_params_t ucp_context_params   = {0};
    ucb_params_t ucb_context_params   = {0};
    ucg_params_t ucg_context_params   = {0};
    int is_server                     = (flags & UCG_MINIMAL_FLAG_SERVER);
    ucp_context_params.field_mask     = UCP_PARAM_FIELD_FEATURES;
    ucp_context_params.features       = UCP_FEATURE_GROUPS;
    ucg_context_params.super          = &ucb_context_params;
    ucb_context_params.field_mask     = 0;
    ucb_context_params.super          = &ucp_context_params;
    ucg_context_params.field_mask     = UCG_PARAM_FIELD_ADDRESS_CB;
    ucg_group_attr_t group_attr       = {
            .field_mask               = UCG_GROUP_ATTR_FIELD_MEMBER_COUNT
    };
    ucg_group_params_t group_params   = {
            .field_mask               = UCG_GROUP_PARAM_FIELD_UCP_WORKER |
                                        UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |
                                        UCG_GROUP_PARAM_FIELD_MEMBER_INDEX,
            .member_index             = !is_server,
            .member_count             = 1 + !is_server
    };

    if (comp_cb) {
        ucg_context_params.field_mask |= UCG_PARAM_FIELD_COMPLETION_CB;
        ucg_context_params.completion.comp_cb_f[0][0][0] =
        ucg_context_params.completion.comp_cb_f[0][0][1] =
        ucg_context_params.completion.comp_cb_f[0][1][0] =
        ucg_context_params.completion.comp_cb_f[0][1][1] =
        ucg_context_params.completion.comp_cb_f[1][0][0] =
        ucg_context_params.completion.comp_cb_f[1][0][1] =
        ucg_context_params.completion.comp_cb_f[1][1][0] =
        ucg_context_params.completion.comp_cb_f[1][1][1] = comp_cb;
    }

    status = ucg_config_read(NULL, NULL, &context_config);
    if (status != UCS_OK) {
        return status;
    }

    status = ucg_init(&ucg_context_params, context_config, &ctx->context);
    ucg_config_release(context_config);
    if (status != UCS_OK) {
        return status;
    }

    ucp_context = ucb_context_get_ucp(ucg_context_get_ucb(ctx->context));
    status      = ucp_worker_create(ucp_context, &worker_params, &ctx->worker);
    if (status != UCS_OK) {
        goto cleanup_context;
    }

    group_params.worker = ctx->worker;
    status              = ucg_group_create(&group_params, &ctx->group);
    if (status != UCS_OK) {
        goto cleanup_worker;
    }

    if (!is_server) {
        status = ucg_group_listener_connect(ctx->group, server_address);
        if (status != UCS_OK) {
            goto cleanup_group;
        }

        return UCS_OK;
    }

    status = ucg_group_listener_create(ctx->group, server_address, &ctx->listener);
    if (status != UCS_OK) {
        goto cleanup_group;
    }

    do {
        (void) ucp_worker_progress(ctx->worker);

        status = ucg_group_query(ctx->group, &group_attr);
        if (status != UCS_OK) {
            goto cleanup_listener;
        }
    } while (group_attr.member_count < (num_connections_to_wait + 1));

    ucg_group_listener_destroy(ctx->listener);

    return UCS_OK;

cleanup_listener:
    ucg_group_listener_destroy(ctx->listener);

cleanup_group:
    ucg_group_destroy(ctx->group);

cleanup_worker:
    ucp_worker_destroy(ctx->worker);

cleanup_context:
    ucg_cleanup(ctx->context);
    return status;
}

static inline void
ucg_minimal_finalize(ucg_minimal_ctx_t *ctx)
{
    ucg_group_destroy(ctx->group);
    ucp_worker_destroy(ctx->worker);
    ucg_cleanup(ctx->context);
}

static inline ucs_status_t
ucg_minimal_broadcast(ucg_minimal_ctx_t *ctx, void *buffer, size_t length)
{
    ucg_coll_h collh;
    ucs_status_ptr_t op;
    ucs_status_t status;
    volatile ucs_status_t *status_ptr;
    ucg_collective_progress_t progress_f;
    ucg_collective_params_t bcast_params = {
        .send = {
            .type = {
                .modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                             UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE |
                             UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID,
                .root      = 0
            },
            .buffer        = buffer,
            .count         = length,
            .dtype         = NULL
        },
        .recv = {
            .buffer        = buffer,
            .count         = length,
            .dtype         = NULL
        }
    };

    status = ucg_collective_create(ctx->group, &bcast_params, &collh);
    if (status != UCS_OK) {
        return status;
    }

    op = ucg_collective_start(collh, NULL, &progress_f);
    if (UCS_PTR_IS_ERR(op)) {
        ucg_collective_destroy(collh);
        return UCS_PTR_STATUS(op);
    }

    if (UCS_PTR_IS_PTR(op)) {
        status_ptr = ucg_collective_get_status_ptr(op);
        while (*status_ptr == UCS_INPROGRESS) {
            (void) progress_f(op);
        }
        return *status_ptr;
    }

    return UCS_PTR_STATUS(op);
}

END_C_DECLS

#endif
