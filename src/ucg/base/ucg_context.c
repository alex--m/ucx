/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "math.h"

#include "ucg_plan.h"
#include "ucg_group.h"
#include "ucg_context.h"
#include "ucg_listener.h"

#include <ucs/sys/module.h>
#include <ucs/debug/debug_int.h>

extern ucs_config_field_t ucb_config_table[];

const char *ucg_context_distribution_type_names[] = {
    [UCG_DELAY_DISTRIBUTION_NONE]    = "none",
    [UCG_DELAY_DISTRIBUTION_UNIFORM] = "uniform",
    [UCG_DELAY_DISTRIBUTION_NORMAL]  = "normal",
    NULL
};

ucs_config_field_t ucg_config_table[] = {
  {"UCG_", "", NULL,
   ucs_offsetof(ucg_context_config_t, super),
   UCS_CONFIG_TYPE_TABLE(ucs_pmodule_config_table)},

  {"DELAY_DISTRIBUTION", "none",
   "Should datatypes be treated as volatile and reloaded on each invocation.\n",
   ucs_offsetof(ucg_context_config_t, delay_distribution),
   UCS_CONFIG_TYPE_ENUM(ucg_context_distribution_type_names)},

  {"DELAY_EXPECTED_VALUE", "0",
   "Should datatypes be treated as volatile and reloaded on each invocation.\n",
   ucs_offsetof(ucg_context_config_t, delay_expected_value),
   UCS_CONFIG_TYPE_DOUBLE},

  {"DELAY_NORMAL_VARIANCE", "1",
   "Should datatypes be treated as volatile and reloaded on each invocation.\n",
   ucs_offsetof(ucg_context_config_t, delay_normal_variance),
   UCS_CONFIG_TYPE_DOUBLE},

  {NULL}
};

UCS_PMODULE_FRAMEWORK(UCG, ucg);

static inline double ucg_context_sample_uniform(double expected_value)
{
    return ((double) rand() / (RAND_MAX)) * 2 * expected_value;
}

static double ucg_context_sample_normal(double expected_value, double variance)
{
    double u = ucg_context_sample_uniform(1) - 1;
    double v = ucg_context_sample_uniform(1) - 1;
    double r = u * u + v * v;
    if (r == 0 || r > 1) {
        return ucg_context_sample_normal(expected_value, variance);
    }

    return (variance * u * sqrt(-2 * log(r) / r)) + expected_value;
}

void ucg_context_barrier_delay(ucg_group_h group)
{
    useconds_t delay             = 0;
    ucg_context_t *ctx           = ucs_derived_of(group->super.context,
                                                  ucg_context_t);
    ucg_context_config_t *config = &ctx->config;

    switch (config->delay_distribution) {
        case UCG_DELAY_DISTRIBUTION_NONE:
            return;

        case UCG_DELAY_DISTRIBUTION_UNIFORM:
            delay = (useconds_t)ucg_context_sample_uniform(config->delay_expected_value);
            break;

        case UCG_DELAY_DISTRIBUTION_NORMAL:
            delay = (useconds_t)ucg_context_sample_normal(config->delay_expected_value,
                                                          config->delay_normal_variance);
            break;
    }

    usleep(delay);
}

ucs_status_t ucg_context_set_am_handler(uint8_t id, ucp_am_handler_t *handler)
{
    /*
     * Set the Active Message handler (before creating the UCT interfaces)
     *
     * @note: The reason this is handled as early as query, and not during init
     * time, is that other processes may finish init before others start. This
     * makes for a race condition, potentially causing Active Messages to arrive
     * before their handler is registered. Registering the Active Message ID now
     * guarantees once init is finished on any of the processes - the others are
     * aware of this ID and messages can be sent.
     */
    ucs_assert(id < UCT_AM_ID_MAX);
    ucs_assert((ucp_am_handlers[id] == NULL) ||
               (ucp_am_handlers[id] == handler));
    ucp_am_handlers[id] = handler;
    return UCS_OK;
}

ucs_status_t ucg_context_set_async_timer(ucs_async_context_t *async,
                                         ucs_async_event_cb_t cb,
                                         void *cb_arg,
                                         ucs_time_t interval,
                                         int *timer_id_p)
{
    ucs_async_mode_t async_mode = async->mode;

    UCS_ASYNC_BLOCK(async);

    ucs_status_t status = ucs_async_add_timer(async_mode, interval, cb,
                                              cb_arg, async, timer_id_p);
    if (status != UCS_OK) {
        ucs_error("unable to add timer handler - %s",
                  ucs_status_string(status));
    }

    UCS_ASYNC_UNBLOCK(async);

    return status;
}

ucs_status_t ucg_context_unset_async_timer(ucs_async_context_t *async,
                                           int timer_id)
{
    UCS_ASYNC_BLOCK(async);

    ucs_status_t status = ucs_async_remove_handler(timer_id, 1);
    if (status != UCS_OK) {
        ucs_error("unable to remove timer handler %d - %s",
                  timer_id, ucs_status_string(status));
    }

    UCS_ASYNC_UNBLOCK(async);

    return status;
}

static ucs_status_t ucg_context_init(const ucg_context_config_t *config,
                                     void *groups_ctx)
{
    ucs_status_t status;
    uint8_t am_id                         = UCP_AM_ID_LAST;
    ucg_context_t *ctx                    = (ucg_context_t*)groups_ctx;
    ucs_pmodule_component_params_t params = {
        .field_mask = UCS_PMODULE_COMPONENT_PARAMS_FIELD_AM_ID |
                      UCS_PMODULE_COMPONENT_PARAMS_FIELD_DUMMY_GROUP |
                      UCS_PMODULE_COMPONENT_PARAMS_FIELD_BARRIR_DELAY,
        .am_id      = &am_id
    };

    ctx->dummy_group          =
    params.dummy_group        = UCS_ALLOC_CHECK(sizeof(ucg_group_t),
                                                "ucg_dummy_group");
    params.is_barrier_delayed = (config->delay_distribution !=
                                 UCG_DELAY_DISTRIBUTION_NONE);
    ctx->dummy_group->worker  = NULL;
    ucs_ptr_array_locked_init(&ctx->dummy_group->addresses.matched,
                              "ucg_context_wireup_pool");

    memcpy(&ctx->config, config, sizeof(*config));

    status = ucs_pmodule_framework_init(&ctx->super, &params, &config->super,
                                        UCS_PMODULE_COMPONENT_QUERY(ucg)(0),
                                        "ucg context");
    if (status != UCS_OK) {
        return status;
    }

#ifdef ENABLE_FAULT_TOLERANCE
    /* Initialize the fault-tolerance context for the entire UCG layer */
    status = ucg_ft_init(&worker->async, new_group, ucg_base_am_id + idx,
                         ucg_group_fault_cb, ctx, &ctx->ft_ctx);
    if (status != UCS_OK) {
        goto cleanup_framework;
    }
#endif

    status = ucg_listener_am_init(am_id, &ctx->super.targets_head);
    if (status != UCS_OK) {
        goto cleanup_ft;
    }

    ucs_spinlock_init(&ctx->lock, 0);
    ctx->last_group_id = UCG_GROUP_FIRST_GROUP_ID - 1;
    return UCS_OK;

cleanup_ft:
#ifdef ENABLE_FAULT_TOLERANCE
    if (ucs_list_is_empty(&group->list)) {
        ucg_ft_cleanup(&gctx->ft_ctx);
    }
cleanup_framework:
#endif
    ucs_pmodule_framework_cleanup(&ctx->super);
    return status;
}

static void ucg_context_cleanup(void *groups_ctx)
{
    ucg_context_t *ctx = (ucg_context_t*)groups_ctx;

#ifdef ENABLE_FAULT_TOLERANCE
    ucg_ft_cleanup(&ctx->ft_ctx);
#endif

    ucs_pmodule_framework_cleanup(&ctx->super);
    ucs_ptr_array_locked_cleanup(&ctx->dummy_group->addresses.matched, 0);
    ucs_spinlock_destroy(&ctx->lock);
    ucs_free(ctx->dummy_group);
}

ucg_message_pool_h ucg_context_get_wireup_message_pool(ucg_context_h context)
{
    return &context->dummy_group->addresses.matched;
}

UCS_PMODULE_FRAMEWORK_CONTEXT_COMMON(UCG, ucg, UCB, ucb, params->super->super);

void ucg_context_copy_used_params(ucg_params_t *dst, const ucg_params_t *src)
{
    ucg_collective_comp_cb_t cb = (ucg_collective_comp_cb_t)ucs_empty_function;
    size_t ucg_params_size = sizeof(src->field_mask) +
                             ucs_offsetof(ucg_params_t, field_mask);

    if (src->field_mask != 0) {
        enum ucg_params_field msb_flag = UCS_BIT((sizeof(uint64_t) * 8) - 1 -
                ucs_count_leading_zero_bits(src->field_mask));
        ucs_assert((msb_flag & src->field_mask) == msb_flag);

        switch (msb_flag) {
        case UCG_PARAM_FIELD_NAME:
            ucg_params_size = ucs_offsetof(ucg_params_t, context_headroom);
            break;

        case UCG_PARAM_FIELD_CONTEXT_HEADROOM:
            ucg_params_size = ucs_offsetof(ucg_params_t, address);
            break;

        case UCG_PARAM_FIELD_ADDRESS_CB:
            ucg_params_size = ucs_offsetof(ucg_params_t, neighbors);
            break;

        case UCG_PARAM_FIELD_NEIGHBORS_CB:
            ucg_params_size = ucs_offsetof(ucg_params_t, datatype);
            break;

        case UCG_PARAM_FIELD_DATATYPE_CB:
            ucg_params_size = ucs_offsetof(ucg_params_t, reduce_op);
            break;

        case UCG_PARAM_FIELD_REDUCE_OP_CB:
            ucg_params_size = ucs_offsetof(ucg_params_t, completion);
            break;

        case UCG_PARAM_FIELD_COMPLETION_CB:
            ucg_params_size = ucs_offsetof(ucg_params_t, set_imbalance_cb_f);
            break;

        case UCG_PARAM_FIELD_SET_IMBALANCE_CB:
            ucg_params_size = ucs_offsetof(ucg_params_t, mpi_in_place);
            break;

        case UCG_PARAM_FIELD_MPI_IN_PLACE:
            ucg_params_size = ucs_offsetof(ucg_params_t, get_global_index_f);
            break;

        case UCG_PARAM_FIELD_GLOBAL_INDEX:
            ucg_params_size = ucs_offsetof(ucg_params_t, fault);
            break;

        case UCG_PARAM_FIELD_HANDLE_FAULT:
            ucg_params_size = ucs_offsetof(ucg_params_t, job_info);
            break;

        case UCG_PARAM_FIELD_JOB_INFO:
            ucg_params_size = sizeof(ucg_params_t);
            break;
        }
    }

    memcpy(dst, src, ucg_params_size);

    if (!(src->field_mask & UCG_PARAM_FIELD_COMPLETION_CB)) {
        ucg_global_params.completion.comp_cb_f[0][0][0] = cb;
        ucg_global_params.completion.comp_cb_f[0][0][1] = cb;
        ucg_global_params.completion.comp_cb_f[0][1][0] = cb;
        ucg_global_params.completion.comp_cb_f[0][1][1] = cb;
        ucg_global_params.completion.comp_cb_f[1][0][0] = cb;
        ucg_global_params.completion.comp_cb_f[1][0][1] = cb;
        ucg_global_params.completion.comp_cb_f[1][1][0] = cb;
        ucg_global_params.completion.comp_cb_f[1][1][1] = cb;
    }

    if (!(src->field_mask & UCG_PARAM_FIELD_DATATYPE_CB)) {
       ucg_global_params.datatype.convert_f           = ucs_empty_function_do_assert;
       ucg_global_params.datatype.get_span_f          = ucs_empty_function_do_assert;
       ucg_global_params.datatype.is_integer_f        = ucs_empty_function_return_zero_int;
       ucg_global_params.datatype.is_floating_point_f = ucs_empty_function_return_zero_int;
    }

    if (!(src->field_mask & UCG_PARAM_FIELD_REDUCE_OP_CB)) {
       ucg_global_params.reduce_op.reduce_cb_f       = ucs_empty_function_do_assert;
       ucg_global_params.reduce_op.get_operator_f    = ucs_empty_function_return_zero_int;
    }
}