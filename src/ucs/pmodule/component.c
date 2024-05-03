/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "component.h"

#include <ucs/arch/cpu.h>
#include <ucs/sys/string.h>
#include <ucs/debug/log_def.h>
#include <ucs/debug/debug_int.h>
#include <ucs/debug/memtrack_int.h>

#define ucs_pmodule_component_foreach(_descs_head, _comp_ctx, _tgt_ctx, _code) \
    UCS_V_UNUSED const char *comp_name; \
    ucs_pmodule_component_desc_t *desc; \
    ucs_pmodule_component_t* comp; \
    ucs_list_for_each(desc, _descs_head, list) { \
        comp        = desc->component; \
        comp_name   = comp->name; \
        _code; \
        (_comp_ctx) = UCS_PTR_BYTE_OFFSET((_comp_ctx), comp->global_ctx_size); \
        (_tgt_ctx)  = UCS_PTR_BYTE_OFFSET((_tgt_ctx), \
                                          ucs_align_up(comp->per_tgt_ctx_size, \
                                                       UCS_SYS_CACHE_LINE_SIZE)); \
        }

static ucs_status_t
ucs_pmodule_component_config_read(ucs_pmodule_component_t *component,
                                  const char *env_prefix,
                                  const char *filename,
                                  ucs_pmodule_component_config_h *config_p)
{
    ucs_status_t status;
    ucs_pmodule_component_config_h config;

    char full_prefix[128] = UCS_DEFAULT_ENV_PREFIX;

    if (component->config.size == 0) {
        *config_p = NULL;
        return UCS_OK;
    }

    config = ucs_calloc(1, component->config.size, component->config.name);
    if (config == NULL) {
        ucs_error("failed to allocate component config structure");
        return UCS_ERR_NO_MEMORY;
    }

    if ((env_prefix != NULL) && (strlen(env_prefix) > 0)) {
        ucs_snprintf_zero(full_prefix, sizeof(full_prefix), "%s_%s",
                          env_prefix, UCS_DEFAULT_ENV_PREFIX);
    }

    status = ucs_config_parser_fill_opts(config, &component->config,
                                         full_prefix, 0);
    if (status != UCS_OK) {
        ucs_free(config);
    } else {
        *config_p = config;
    }

    return status;
}

static int ucs_pmodule_component_is_allowed(const char *component_name,
                                            const ucs_config_allow_list_t *list)
{
    size_t len, my_len = strlen(component_name);
    const char *next;
    unsigned idx;

    if (list->mode == UCS_CONFIG_ALLOW_LIST_ALLOW_ALL) {
        return 1;
    }

    for (idx = 0; idx < list->array.count; idx++) {
        next = list->array.names[idx];
        if (!strcmp(component_name, next)) {
            return list->mode == UCS_CONFIG_ALLOW_LIST_ALLOW;
        }

        len = ucs_min(my_len, strlen(next));
        if ((next[len - 1] == '*') &&
            (!strncmp(component_name, list->array.names[idx], len - 1))) {
            return list->mode == UCS_CONFIG_ALLOW_LIST_ALLOW;
        }
    }

    return list->mode != UCS_CONFIG_ALLOW_LIST_ALLOW;
}

ucs_status_t
ucs_pmodule_component_query(ucs_list_link_t *components_list,
                            const ucs_config_allow_list_t *allow_list,
                            ucs_list_link_t *desc_head_p,
                            size_t *total_plan_ctx_size)
{
    ucs_status_t status;
    ucs_pmodule_component_t *component;
    ucs_pmodule_component_desc_t *desc;
    size_t ctx_size;

    ucs_list_head_init(desc_head_p);
    ucs_list_for_each(component, components_list, list) {
        if (ucs_pmodule_component_is_allowed(component->name, allow_list)) {
            status = component->query(desc_head_p);
            if (status != UCS_OK) {
                ucs_warn("Failed to query component %s: %m", component->name);
                continue;
            }
        }
    }

    /* Sum the context sizes */
    ctx_size = 0;
    ucs_list_for_each(desc, desc_head_p, list) {
        ctx_size += desc->component->global_ctx_size;
    }
    *total_plan_ctx_size = ctx_size;

    return UCS_OK;
}

static ucs_status_t
ucs_pmodule_component_init_per_desc(ucs_pmodule_component_t* comp,
                                    const char *comp_name,
                                    ucs_pmodule_component_ctx_h comp_ctx,
                                    const ucs_pmodule_component_params_t *params)
{
    ucs_pmodule_component_config_h config;

    ucs_status_t status = ucs_pmodule_component_config_read(comp, NULL, NULL,
                                                            &config);
    if (status != UCS_OK) {
        return status;
    }

    status = comp->init(comp_ctx, params, config);
    if (status != UCS_OK) {
        ucs_warn("failed to initialize planner %s: %m", comp_name);
    }

    if (config) {
        ucs_config_parser_release_opts(config, comp->config.table);
        ucs_free(config);
    }

    return status;
}

ucs_status_t
ucs_pmodule_component_init(ucs_list_link_t *desc_head,
                           const ucs_pmodule_component_params_t *params,
                           ucs_pmodule_component_ctx_h comp_ctx,
                           size_t *per_tgt_ctx_size)
{
    void *dummy_tgt  = (void*)(sizeof(ucs_pmodule_target_t) + UCS_SYS_CACHE_LINE_SIZE);

    ucs_pmodule_component_foreach(desc_head, comp_ctx, dummy_tgt,
                                  ucs_pmodule_component_init_per_desc(comp,
                                                                      comp_name,
                                                                      comp_ctx,
                                                                      params));

    *per_tgt_ctx_size = (size_t)dummy_tgt;

    return UCS_OK;
}

void ucs_pmodule_component_finalize(ucs_list_link_t *desc_head,
                                    ucs_pmodule_component_ctx_h comp_ctx)
{
    void *dummy_tgt = NULL;

    ucs_pmodule_component_foreach(desc_head, comp_ctx, dummy_tgt,
                                  comp->finalize(comp_ctx));

    while (!ucs_list_is_empty(desc_head)) {
        ucs_free(ucs_list_extract_head(desc_head, ucs_pmodule_component_desc_t,
                                       list));
    }
}

ucs_status_t
ucs_pmodule_component_create(ucs_list_link_t *desc_head,
                             ucs_pmodule_component_ctx_h comp_ctx,
                             ucs_pmodule_target_ctx_h target_ctx,
                             ucs_pmodule_target_t *target,
                             const void *target_params)
{
    ucs_status_t status;

    ucs_pmodule_component_foreach(desc_head, comp_ctx, target_ctx,
        status = comp->create(comp_ctx, target_ctx, target, target_params);
        if (status != UCS_OK) {
            return status;
        });

    return UCS_OK;
}

void ucs_pmodule_component_destroy(ucs_list_link_t *desc_head,
                                   ucs_pmodule_component_ctx_h comp_ctx,
                                   ucs_pmodule_target_ctx_h target_ctx)
{
    ucs_pmodule_component_foreach(desc_head, comp_ctx, target_ctx,
                                  comp->destroy(target_ctx));
}

ucs_status_t
ucs_pmodule_component_plan(ucs_list_link_t *desc_head,
                           ucs_pmodule_component_ctx_h comp_ctx,
                           ucs_pmodule_target_ctx_h target_ctx,
                           ucs_pmodule_target_action_params_h params,
                           ucs_pmodule_target_plan_t **plan_p)
{
    ucs_status_t status;

    double estimation;
    double lowest_estimation = 0;
    ucs_pmodule_component_desc_t *best_comp = NULL;
    ucs_pmodule_target_ctx_h best_target_ctx = NULL;

    ucs_pmodule_component_foreach(desc_head, comp_ctx, target_ctx,
        if ((((status = comp->estimate(target_ctx, params, &estimation)) == UCS_OK) &&
             (estimation < lowest_estimation)) || (!best_comp)) {
            lowest_estimation = estimation;
            best_target_ctx   = target_ctx;
            best_comp         = desc;
        });
    if (ucs_unlikely((best_comp == NULL) || (best_target_ctx == NULL))) {
        return UCS_ERR_NOT_IMPLEMENTED;
    }

    status = best_comp->component->plan(best_target_ctx, params, plan_p);

    if (status == UCS_OK) {
        (*plan_p)->trigger_f  = best_comp->component->trigger;
        (*plan_p)->progress_f = best_comp->component->progress;
        (*plan_p)->component  = best_comp->component;
    }

    return status;
}

void ucs_pmodule_component_plan_discard(ucs_pmodule_target_plan_t *plan)
{
    plan->component->discard(plan);
}

void ucs_pmodule_component_print_info(ucs_list_link_t *desc_head, FILE *stream)
{
    unsigned idx     = 0;
    void *dummy_comp = NULL;
    void *dummy_tgt  = NULL;

    ucs_pmodule_component_foreach(desc_head, dummy_comp, dummy_tgt,
        fprintf(stream, "#     component %-2d :  %s\n", idx++, comp_name));
}