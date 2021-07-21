/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "component.h"

#include <ucs/arch/cpu.h>
#include <ucs/sys/string.h>
#include <ucs/debug/log_def.h>
#include <ucf/api/ucf_version.h>
#include <ucs/debug/debug_int.h>

#define ucs_pmodule_component_foreach(_descs, _desc_cnt, _comp_ctx, _obj_ctx) \
    typeof(_desc_cnt) idx = 0; \
    ucs_pmodule_component_t* comp; \
    ucs_assert((_desc_cnt) > 0); \
    for (comp = (_descs)->component; \
         idx < (_desc_cnt); \
         idx++, (_descs)++, \
         (_comp_ctx) = UCS_PTR_BYTE_OFFSET((_comp_ctx), comp->global_ctx_size), \
         (_obj_ctx)  = UCS_PTR_BYTE_OFFSET((_obj_ctx), \
                           ucs_align_up(comp->per_obj_ctx_size, \
                                        UCS_SYS_CACHE_LINE_SIZE)), \
         comp = (idx < (_desc_cnt)) ? (_descs)->component : NULL)

#define ucs_pmodule_object_foreach(_obj) \
    ucs_pmodule_desc_t *descs           = (_obj)->context->component_descs; \
    unsigned desc_cnt                   = (_obj)->context->num_components; \
    ucs_pmodule_component_ctx_h cctx    = (_obj)->context->component_ctx; \
    ucs_pmodule_object_ctx_h octx       = (void*)ucs_align_up((uintptr_t)(_obj + 1), \
                                               UCS_SYS_CACHE_LINE_SIZE); \
    ucs_pmodule_component_foreach(descs, desc_cnt, cctx, octx)

static ucs_status_t
ucs_pmodule_component_config_read(ucs_pmodule_component_t *component,
                                  const char *env_prefix,
                                  const char *filename,
                                  ucs_pmodule_component_config_h *config_p)
{
    ucs_status_t status;
    ucs_pmodule_component_config_h config;

    char full_prefix[128] = UCS_DEFAULT_ENV_PREFIX;

    config = ucs_calloc(1, component->config.size, component->config.name);
    if (config == NULL) {
        return UCS_ERR_NO_MEMORY;
    }

    /* TODO use env_prefix */
    if ((env_prefix != NULL) && (strlen(env_prefix) > 0)) {
        ucs_snprintf_zero(full_prefix, sizeof(full_prefix), "%s_%s",
                          env_prefix, UCS_DEFAULT_ENV_PREFIX);
    }

    status = ucs_config_parser_fill_opts(config, component->config.table,
                                         full_prefix, component->config.prefix,
                                         0);
    if (status != UCS_OK) {
        ucs_free(config);
    } else {
        *config_p = config;
    }

    return status;
}

ucs_status_t ucs_pmodule_component_query(ucs_list_link_t* components_list,
                                         ucs_pmodule_desc_t **desc_p,
                                         unsigned *num_desc_p,
                                         size_t *total_fs_ctx_size)
{
    size_t size;
    ucs_status_t status;
    ucs_pmodule_desc_t *desc_iter;
    ucs_pmodule_component_t *component;
    unsigned i, desc_cnt, desc_total = 0;

    /* Calculate how many descriptors to allocate */
    ucs_list_for_each(component, components_list, list) {
        status = component->query(NULL, &desc_cnt);
        if (status != UCS_OK) {
            ucs_warn("Failed to query file-system %s (for size): %m",
                     component->name);
            continue;
        }

        desc_total += desc_cnt;
    }

    /* Allocate the descriptors */
    size      = desc_total * sizeof(ucs_pmodule_desc_t);
    desc_iter = *desc_p = UCS_ALLOC_CHECK(size, "ucf descs");

    size = 0;
    ucs_list_for_each(component, components_list, list) {
        status = component->query(desc_iter, &desc_cnt);
        if (status != UCS_OK) {
            ucs_warn("Failed to query file-system %s (for content): %m",
                     component->name);
            continue;
        }

        for (i = 0; i < desc_cnt; ++i) {
            size += desc_iter[i].component->global_ctx_size;

            ucs_assertv_always(!strncmp(component->name, desc_iter[i].name,
                                        strlen(component->name)),
                               "File-system name must begin with the component name."
                               "File-system name: %s\tComponent name: %s ",
                               desc_iter[i].name, component->name);
        }

        desc_iter += desc_cnt;
    }

    *num_desc_p          = desc_iter - *desc_p;
    *total_fs_ctx_size = size;

    return UCS_OK;
}

ucs_status_t
ucs_pmodule_component_init(ucs_pmodule_desc_t *descs, unsigned desc_cnt,
                           const ucs_pmodule_component_params_t *params,
                           ucs_pmodule_component_ctx_h comp_ctx,
                           size_t *per_obj_ctx_size)
{

    ucs_pmodule_component_config_h config;

    void *dummy_obj  = (void*)(sizeof(ucs_pmodule_object_t) + UCS_SYS_CACHE_LINE_SIZE);

    ucs_pmodule_component_foreach(descs, desc_cnt, comp_ctx, dummy_obj) {
        ucs_status_t status = ucs_pmodule_component_config_read(comp, NULL, NULL, &config);
        if (status != UCS_OK) {
            continue;
        }

        status = comp->init(comp_ctx, params, config);

        ucs_config_parser_release_opts(config, comp->config.table);

        if (status != UCS_OK) {
            ucs_warn("failed to initialize fsner %s: %m", descs->name);
            continue;
        }
    }

    *per_obj_ctx_size = (size_t)dummy_obj;

    return UCS_OK;
}

void ucs_pmodule_component_finalize(ucs_pmodule_desc_t *descs,
                                    unsigned desc_cnt,
                                    ucs_pmodule_component_ctx_h comp_ctx)
{
    void *dummy_obj = NULL;

    ucs_pmodule_component_foreach(descs, desc_cnt, comp_ctx, dummy_obj) {
        comp->finalize(comp_ctx);
    }
}

ucs_status_t
ucs_pmodule_component_create(ucs_pmodule_object_t *object,
                             const ucs_pmodule_object_params_t *obj_params)
{
    ucs_pmodule_object_foreach(object) {
        ucs_status_t status = comp->create(cctx, octx, object, obj_params);
        if (status != UCS_OK) {
            return status;
        }
    }

    return UCS_OK;
}

void ucs_pmodule_component_destroy(ucs_pmodule_object_t *object)
{
    ucs_pmodule_object_foreach(object) {
        comp->destroy(octx);
    }
}

void ucs_pmodule_component_print_info(ucs_pmodule_desc_t *descs,
                                      unsigned desc_cnt, FILE *stream)
{
    void *dummy_comp = NULL;
    void *dummy_obj  = NULL;

    ucs_pmodule_component_foreach(descs, desc_cnt, dummy_comp, dummy_obj) {
        fprintf(stream, "#     component %-2d :  %s\n", idx, comp->name);
    }
}

ucs_status_t ucs_pmodule_component_single(ucs_pmodule_component_t *component,
                                          ucs_pmodule_desc_t *descs,
                                          unsigned *desc_cnt_p)
{
    if (descs) {
        descs->component = component;
        ucs_snprintf_zero(&descs->name[0], UCS_PMODULE_NAME_MAX, "%s",
                          component->name);
    }

    *desc_cnt_p = 1;

    return UCS_OK;
}

ucs_status_t
ucs_pmodule_component_choose(const ucs_pmodule_action_params_t *params,
                             ucs_pmodule_object_t *obj,
                             ucs_pmodule_desc_t **desc_p,
                             ucs_pmodule_object_ctx_h *octx_p)
{
    ucs_assert(obj->context->num_components == 1); // TODO: support more...

    *desc_p = obj->context->component_descs;
    *octx_p = (void*)ucs_align_up((uintptr_t)(obj + 1), UCS_SYS_CACHE_LINE_SIZE);

    return UCS_OK;
}
