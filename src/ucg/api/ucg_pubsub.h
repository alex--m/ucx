/**
 * See file LICENSE for terms.
 */

#ifndef UCG_PUBSUB_H_
#define UCG_PUBSUB_H_

#include <ucs/datastruct/list.h>
#include <ucg/api/ucg_minimal.h>

BEGIN_C_DECLS

typedef struct ucg_pubsub_ctx {
    ucg_minimal_ctx_t super;
    ucs_list_link_t   subscriptions;
} ucg_pubsub_ctx_t;

typedef void (*ucg_pubsub_callback_f)(void);

typedef struct ucg_pubsub_subscription {
    uint8_t               *buffer;    /* Buffer for the incoming message */
    size_t                length;     /* Max. expected message length */
    ucg_pubsub_callback_f user_cb;    /* Callback function called upon arrival */
    uint32_t              id;         /* Identifies what to subscribe to */
    uint32_t              flags;      /* Reserved, e.g. reliability bit */

    ucg_group_h           group;      /* Filled by UCG */
    ucg_coll_h            collh;      /* Filled by UCG */
    ucs_list_link_t       list;       /* Filled by UCG */
} ucg_pubsub_subscription_t;

static void ucg_pubsub_subscribe_callback(void *req, ucs_status_t status)
{
    ucg_collective_progress_t progress_f;
    ucg_pubsub_subscription_t *sub = (ucg_pubsub_subscription_t*)req;

    /* Notify the user of an incoming message in his buffer */
    sub->user_cb();

    /* Proceed to waiting for the next message under this subscription */
    (void)ucg_collective_start(sub->collh, sub, &progress_f);
}

static inline ucs_status_t
ucg_pubsub_init(ucg_pubsub_ctx_t *ctx, ucs_sock_addr_t *server_address)
{
    ucs_list_head_init(&ctx->subscriptions);
    return ucg_minimal_init(&ctx->super, server_address, 0,
                            ucg_pubsub_subscribe_callback,
                            UCG_MINIMAL_FLAG_SERVER);
}

static inline void
ucg_pubsub_finalize(ucg_pubsub_ctx_t *ctx)
{
    ucg_minimal_finalize(&ctx->super);
}

enum ucg_pubsub_subscribe_flags {
    UCG_PUBSUB_SUBSCRIBE_FLAG_UNRELIABLE = UCS_BIT(0) /* otherwise reliable */
};

static inline ucs_status_t
ucg_pubsub_subscribe(ucg_pubsub_ctx_t *ctx, ucs_sock_addr_t *server_address,
                     ucg_pubsub_subscription_t *sub, uint64_t flags)
{
    ucs_status_ptr_t op;
    ucs_status_t status;
    ucg_pubsub_subscription_t *iter;
    ucg_collective_progress_t progress_f;
    ucg_group_params_t group_params   = {
            .field_mask               = UCG_GROUP_PARAM_FIELD_UCP_WORKER |
                                        UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |
                                        UCG_GROUP_PARAM_FIELD_MEMBER_INDEX,
            .member_index             = 1,
            .member_count             = 2
    };
    ucg_collective_params_t bcast_params = {
        .send = {
            .type = {
                .modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                             UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE |
                             UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID,
                .root      = 0
            },
            .buffer        = sub->buffer,
            .count         = sub->length,
            .dtype         = NULL
        },
        .recv = {
            .buffer        = sub->buffer,
            .count         = sub->length,
            .dtype         = NULL
        }
    };

    if (sub->flags) {
        return UCS_ERR_NOT_IMPLEMENTED;
    }

    ucs_list_for_each(iter, &ctx->subscriptions, list) {
        if (sub->id == iter->id) {
            /* Only one subscription on per-process per-id is supported */
            return UCS_ERR_UNSUPPORTED;
        }
    }

    /* Create a (sub-)group for this subscription */
    status = ucg_group_create(&group_params, &sub->group);
    if (status != UCS_OK) {
        return status;
    }

    /* Connect the new (sub-)group to the pub/sub server */
    status = ucg_group_listener_connect(sub->group, server_address);
    if (status != UCS_OK) {
        goto subscribe_group_destroy;
    }

    /* Create a persistent collective broadcast operation */
    status = ucg_collective_create(sub->group, &bcast_params, &sub->collh);
    if (status != UCS_OK) {
        goto subscribe_group_destroy;
    }

    /* Start expecting an incoming message */
    op = ucg_collective_start(sub->collh, sub, &progress_f);
    if (UCS_PTR_IS_ERR(op)) {
        status = UCS_PTR_RAW_STATUS(op);
        goto subscribe_collective_destroy;
    }

    return UCS_OK;

subscribe_collective_destroy:
    ucg_collective_destroy(sub->collh);

subscribe_group_destroy:
    ucg_group_destroy(sub->group);

    return status;
}

static inline ucs_status_t
ucg_pubsub_publish(ucg_pubsub_ctx_t *ctx, ucg_pubsub_subscription_t *sub)
{
    ucg_collective_progress_t progress_f;
    ucs_status_ptr_t op = ucg_collective_start(sub->collh, sub, &progress_f);

    return (UCS_PTR_IS_ERR(op)) ? UCS_PTR_RAW_STATUS(op) : UCS_OK;
}

static inline unsigned
ucg_pubsub_progress(ucg_pubsub_ctx_t *ctx) {
    return ucp_worker_progress(ctx->super.worker);
}

END_C_DECLS

#endif
