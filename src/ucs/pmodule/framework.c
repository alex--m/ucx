/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucs/config/parser.h>

#include "framework.h"

ucs_config_field_t ucs_pmodule_config_table[] = {
  {"MODULES", "all",
   "Specifies which module(s) to use. The order is not meaningful.\n"
   "\"all\" would use all available modules.",
   ucs_offsetof(ucs_pmodule_framework_config_t, modules),
   UCS_CONFIG_TYPE_STRING_ARRAY},

  {"CACHE_SIZE", "100",
   "How many operations can be stored in the cache.\n",
   ucs_offsetof(ucs_pmodule_framework_config_t, cache_size),
   UCS_CONFIG_TYPE_UINT},

  {NULL}
};

ucs_status_t
ucs_pmodule_framework_init(const ucs_pmodule_component_params_t *comp_params,
                           const ucs_pmodule_framework_params_t *fw_params,
                           const ucs_pmodule_framework_config_t *fw_config,
                           ucs_list_link_t* components_list,
                           ucs_pmodule_framework_context_t *ctx)
{
    memcpy(&ctx->config, fw_config, sizeof(*fw_config));

    /* Query all the available fss, to find the total space they require */
    size_t ctx_total_size;
    ucs_status_t status = ucs_pmodule_component_query(components_list,
                                                      &ctx->component_descs,
                                                      &ctx->num_components,
                                                      &ctx_total_size);
    if (status != UCS_OK) {
        return status;
    }

    ctx->component_ctx = ucs_malloc(ctx_total_size, "ucs pmodule context");
    if (ctx->component_ctx == NULL) {
        status = UCS_ERR_NO_MEMORY;
        goto cleanup_desc;
    }

    status = ucs_pmodule_component_init(ctx->component_descs,
                                        ctx->num_components, comp_params,
                                        ctx->component_ctx,
                                        &ctx->per_obj_ctx_size);
    if (status != UCS_OK) {
        goto cleanup_pctx;
    }

    ucs_list_head_init(&ctx->obj_head);
    return UCS_OK;

cleanup_fss:
    ucs_pmodule_component_finalize(ctx->component_descs, ctx->num_components,
                                   ctx->component_ctx);

cleanup_pctx:
    ucs_free(ctx->component_ctx);

cleanup_desc:
    ucs_free(ctx->component_descs);
    return status;
}

void ucs_pmodule_framework_cleanup(ucs_pmodule_framework_context_t *ctx)
{
    ucs_pmodule_object_t *obj, *tmp;
    if (!ucs_list_is_empty(&ctx->obj_head)) {
        ucs_list_for_each_safe(obj, tmp, &ctx->obj_head, list) {
            ucs_pmodule_object_destroy(obj);
        }
    }

    ucs_pmodule_component_finalize(ctx->component_descs, ctx->num_components,
                                   ctx->component_ctx);
    ucs_free(ctx->component_descs);
    ucs_free(ctx->component_ctx);
}

void ucs_pmodule_framework_add_object(ucs_pmodule_framework_context_t *ctx,
                                      ucs_pmodule_object_t *object)
{
    ucs_list_add_tail(&ctx->obj_head, &object->list);
}

void ucs_pmodule_framework_remove_object(ucs_pmodule_object_t *object)
{
    ucs_list_del(&object->list);
}

void ucs_pmodule_framework_print_info(const ucs_pmodule_framework_context_t *ctx,
                                      const char* framework_name, FILE *stream)
{
    fprintf(stream, "#\n");
    fprintf(stream, "# %s context\n", framework_name);
    fprintf(stream, "#\n");

    ucs_pmodule_component_print_info(ctx->component_descs, ctx->num_components,
                                     stream);

    fprintf(stream, "#\n");
}
