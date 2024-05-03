/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "framework.h"

#include <ucs/debug/log_def.h>
#include <ucs/config/parser.h>
#include <ucs/debug/memtrack_int.h>

ucs_config_field_t ucs_pmodule_config_table[] = {
  {"COMPONENTS", "all",
   "Comma-separated list of glob patterns specifying which component to load.\n"
   "The order is not meaningful. For example:\n"
   " *     - load all modules\n"
   " ^cu*  - do not load modules that begin with 'cu'",
   ucs_offsetof(ucs_pmodule_framework_config_t, components),
   UCS_CONFIG_TYPE_ALLOW_LIST},

  {"CACHE_SIZE", "128",
   "How many operations can be stored in the cache.\n",
   ucs_offsetof(ucs_pmodule_framework_config_t, cache_size),
   UCS_CONFIG_TYPE_UINT},

  {NULL}
};

#ifdef ENABLE_STATS
enum {
    UCS_PMODULE_FRAMEWORK_STAT_TARGETS_CREATED,

    UCS_PMODULE_FRAMEWORK_STAT_LAST
};

static ucs_stats_class_t ucs_pmodule_framework_stats_class = {
    .name           = "pmodule_framework",
    .num_counters   = UCS_PMODULE_FRAMEWORK_STAT_LAST,
    .counter_names  = {
        [UCS_PMODULE_FRAMEWORK_STAT_TARGETS_CREATED] = "targets_created",
    }
};
#endif /* ENABLE_STATS */

ucs_status_t
ucs_pmodule_framework_init(ucs_pmodule_framework_context_t *ctx,
                           const ucs_pmodule_component_params_t *comp_params,
                           const ucs_pmodule_framework_config_t *config,
                           ucs_list_link_t* components_list,
                           const char *framework_name)
{
    /* Query all the available planners, to find the total space they require */
    int ret;
    size_t ctx_total_size;
    ucs_status_t status = ucs_pmodule_component_query(components_list,
                                                      &config->components,
                                                      &ctx->component_descs,
                                                      &ctx_total_size);
    if (status != UCS_OK) {
        return status;
    }

    status = ucs_config_parser_clone_opts(config, &ctx->config,
                                          ucs_pmodule_config_table);
    if (status != UCS_OK) {
        goto cleanup_desc;
    }

    ret = ucs_posix_memalign(&ctx->component_ctx, UCS_SYS_CACHE_LINE_SIZE,
                             ctx_total_size, framework_name);
    if (ret || (ctx->component_ctx == NULL)) {
        status = UCS_ERR_NO_MEMORY;
        goto cleanup_config;
    }

    status = UCS_STATS_NODE_ALLOC(&ctx->stats, &ucs_pmodule_framework_stats_class,
                                  ucs_stats_get_root(), "-%s", framework_name);
    if (status != UCS_OK) {
        goto cleanup_ctx;
    }

    status = ucs_pmodule_component_init(&ctx->component_descs,
                                        comp_params,
                                        ctx->component_ctx,
                                        &ctx->per_tgt_ctx_size);
    if (status != UCS_OK) {
        goto cleanup_stats;
    }

    ucs_list_head_init(&ctx->targets_head);
    return UCS_OK;

cleanup_stats:
    UCS_STATS_NODE_FREE(ctx->stats);

cleanup_ctx:
    ucs_free(ctx->component_ctx);

cleanup_config:
    ucs_config_parser_release_opts(&ctx->config, ucs_pmodule_config_table);

cleanup_desc:
    while (!ucs_list_is_empty(&ctx->component_descs)) {
        ucs_free(ucs_list_extract_head(&ctx->component_descs,
                                       ucs_pmodule_component_desc_t, list));
    }
    return status;
}

void ucs_pmodule_framework_cleanup(ucs_pmodule_framework_context_t *ctx)
{
    ucs_pmodule_target_t *target;
    ucs_list_link_t *head = &ctx->targets_head;
    while (!ucs_list_is_empty(head)) {
        target = ucs_list_extract_head(head, ucs_pmodule_target_t, list);
#if ENABLE_DEBUG_DATA
        ucs_trace("target %s is not free during cleanup", target->name);
#endif
        ucs_pmodule_target_destroy(target);
    }

    ucs_pmodule_component_finalize(&ctx->component_descs, ctx->component_ctx);

    head = &ctx->component_descs;
    while (!ucs_list_is_empty(head)) {
        ucs_free(ucs_list_extract_head(head, ucs_pmodule_component_desc_t, list));
    }

    ucs_free(ctx->component_ctx);
}

static inline void*
ucs_pmodule_framework_target_get_ctx(ucs_pmodule_target_t *target)
{
    return UCS_PTR_BYTE_OFFSET(target, -target->headroom);
}

ucs_status_t
ucs_pmodule_framework_target_plan(ucs_pmodule_framework_context_t *ctx,
                                  ucs_pmodule_target_t *target,
                                  ucs_pmodule_target_action_params_h params,
                                  ucs_pmodule_target_plan_t **plan_p)
{
    void *target_ctx = ucs_pmodule_framework_target_get_ctx(target);
    ucs_status_t res = ucs_pmodule_component_plan(&ctx->component_descs,
                                                  ctx->component_ctx, target_ctx,
                                                  params, plan_p);
    if (res == UCS_OK) {
        (*plan_p)->target = target; /* Used during cache eviction */
    }

    return res;
}

void ucs_pmodule_framework_target_plan_discard(ucs_pmodule_target_plan_t *plan)
{
    ucs_pmodule_target_plan_uncache(plan->target, plan);
    ucs_pmodule_component_plan_discard(plan);
}

static void
ucs_pmodule_framework_target_plan_evict_cb(ucs_pmodule_cached_t *evicted)
{
    ucs_pmodule_target_plan_t *plan = ucs_container_of(evicted,
                                                       ucs_pmodule_target_plan_t,
                                                       params);

    ucs_pmodule_framework_target_plan_discard(plan);
}

ucs_status_t
ucs_pmodule_framework_target_create(ucs_pmodule_framework_context_t *ctx,
                                    const ucs_pmodule_target_params_t *params,
                                    ucs_pmodule_target_t **target_p)
{
    void *target_ctx;
    ucs_status_t status;
    ucs_pmodule_target_t *target;
    ucs_pmodule_target_params_t new_params = *params;

    /* Leave room for the per-target */
    if (params->field_mask & UCS_PMODULE_TARGET_PARAM_FIELD_HEADROOM) {
        new_params.target_headroom += ctx->per_tgt_ctx_size;
    } else {
        new_params.target_headroom  = ctx->per_tgt_ctx_size;
        new_params.field_mask      |= UCS_PMODULE_TARGET_PARAM_FIELD_HEADROOM;
    }

    if (!(params->field_mask & UCS_PMODULE_TARGET_PARAM_FIELD_CACHE_MAX_ELEMS)) {
        new_params.cache_max_elems.limit    = ctx->config.cache_size;
        new_params.cache_max_elems.evict_cb = ucs_pmodule_framework_target_plan_evict_cb;
        new_params.field_mask              |=
            UCS_PMODULE_TARGET_PARAM_FIELD_CACHE_MAX_ELEMS;
    }

    status = ucs_pmodule_target_create(&new_params UCS_STATS_ARG(ctx->stats),
                                       &target);
    if (status != UCS_OK) {
        return UCS_OK;
    }

    target_ctx = ucs_pmodule_framework_target_get_ctx(target);
    status     = ucs_pmodule_component_create(&ctx->component_descs,
                                              ctx->component_ctx, target_ctx,
                                              target, &new_params);
    if (status != UCS_OK) {
        ucs_pmodule_target_destroy(target);
    } else {
        UCS_STATS_UPDATE_COUNTER(ctx->stats,
                                 UCS_PMODULE_FRAMEWORK_STAT_TARGETS_CREATED, 1);

        ucs_list_add_tail(&ctx->targets_head, &target->list);
        target->context = ctx;
        target->plan_f  = ucs_pmodule_framework_target_plan;
        *target_p       = target;
    }

    return status;
}

void ucs_pmodule_framework_target_destroy(ucs_pmodule_framework_context_t *ctx,
                                          ucs_pmodule_target_t *target)
{
    ucs_pmodule_component_destroy(&ctx->component_descs, ctx->component_ctx,
                                  ucs_pmodule_framework_target_get_ctx(target));

    ucs_list_del(&target->list);

    ucs_pmodule_target_destroy(target);
}

void ucs_pmodule_framework_print_info(ucs_pmodule_framework_context_t *ctx,
                                      const char* framework_name, FILE *stream)
{
    fprintf(stream, "#\n");
    fprintf(stream, "# %s context\n", framework_name);
    fprintf(stream, "#\n");

    ucs_pmodule_component_print_info(&ctx->component_descs, stream);

    fprintf(stream, "#\n");
}