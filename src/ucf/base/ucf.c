/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucs/debug/debug_int.h>
#include <ucs/pmodule/framework.h>
#include <ucg/base/ucg_listener.h>
#include <ucg/base/ucg_context.h>
#include <ucp/core/ucp_types.h>
#include <ucf/api/ucf_version.h>
#include <ucg/api/ucg_version.h>
#include <ucp/api/ucp_version.h>
#include <ucf/api/ucf.h>

#define UCF_FILE_TO_GROUP(_file) ((ucg_group_h)file->super.parent)

typedef struct ucf_config {
    /** General configuration for a persistent component framework */
    ucs_pmodule_framework_config_t super;

    /* TODO: Global and UCF-specific configuration, e.g. whether to use threads */

    ucg_config_t *ucg_config;
} ucf_config_t;

ucs_config_field_t ucf_config_table[] = {
  /* TODO: Global and UCF-specific configuration, e.g. whether to use threads */

  {NULL}

};

UCS_PMODULE_FRAMEWORK(f)

typedef struct ucf_context {
    /* TODO: variables to hold global configuration from @ref ucf_config_t */

    ucs_pmodule_framework_context_t super;

    ucg_context_t ucg_ctx;/* must be last (ABI compat.) */
} ucf_context_t;

struct ucf_file {
    ucs_pmodule_object_t super;
};

struct ucf_listener {
    ucg_listener_h super;
};

ucf_params_t ucf_global_params; /* Ugly - but efficient */

static void ucf_comp_fallback(void *req, ucs_status_t status)
{
    uint32_t *flags = UCS_PTR_BYTE_OFFSET(req,
            ucf_global_params.completion.comp_flag_offset);

    ucs_status_t *result = UCS_PTR_BYTE_OFFSET(req,
            ucf_global_params.completion.comp_status_offset);

    *result = status;
    *flags |= 1;
}

static void ucf_comp_fallback_no_offsets(void *req, ucs_status_t status)
{
    *(uint8_t*)req = (uint8_t)-1;
}

static void ucf_store_params(const ucf_params_t *params)
{
    if (params->field_mask & UCG_PARAM_FIELD_COMPLETION_CB) {
        ucf_global_params.completion = params->completion;
        if (ucf_global_params.completion.coll_comp_cb_f == NULL) {
            ucf_global_params.completion.coll_comp_cb_f =
                    ucf_comp_fallback;
        }
    } else {
        ucf_global_params.completion.coll_comp_cb_f =
                ucf_comp_fallback_no_offsets;
    }

    if (params->field_mask & UCF_PARAM_FIELD_EXTERNAL_FS) {
        ucf_global_params.ext_fs = params->ext_fs;
    }
}

static uint8_t ucf_listener_am_id = 0;

ucs_status_t ucf_file_listener_create(ucf_file_h file,
                                      ucs_sock_addr_t *bind_address,
                                      ucg_listener_h *listener_p)
{
    return ucg_group_listener_create(UCF_FILE_TO_GROUP(file), bind_address,
                                     listener_p);
}

ucs_status_t ucf_file_listener_connect(ucf_file_h file,
                                       ucs_sock_addr_t *listener_addr)
{
    return ucg_group_listener_connect(UCF_FILE_TO_GROUP(file), listener_addr);
}

void ucf_file_listener_destroy(ucf_listener_h listener)
{
    ucg_group_listener_destroy(listener->super);
}

ucs_status_t ucf_file_listener_set_info_cb(void *arg, void *data,
                                           size_t length, unsigned flags)
{
    return ucg_group_listener_set_info_cb(arg, data, length, flags);
}

static void ucf_file_listener_trace_info_cb(void *arg,
                                             uct_am_trace_type_t type,
                                             uint8_t id, const void *data,
                                             size_t length, char *buffer,
                                             size_t max)
{
}

ucs_status_t ucf_listener_am_init(uint8_t am_id, ucs_list_link_t *files_head)
{
    ucf_listener_am_id = am_id;
    return ucg_context_set_am_handler(files_head, am_id,
                                      ucf_file_listener_set_info_cb,
                                      ucf_file_listener_trace_info_cb);
}

static ucs_status_t ucf_context_init(const ucf_params_t *params,
                                     const ucf_config_t *config,
                                     ucf_context_t *ctx)
{
    ucs_status_t status;
    uint8_t am_id                              = UCP_AM_ID_LAST;
    ucs_pmodule_component_params_t comp_params = {
            .field_mask = UCS_PMODULE_COMPONENT_PARAMS_FIELD_AM_ID,
            .am_id      = &am_id
    };

    status = ucs_pmodule_framework_init(&comp_params, params->super,
                                        &config->super,
                                        &UCS_PMODULE_COMPONENT_LIST(f),
                                        &ctx->super);
    if (status != UCS_OK) {
        return status;
    }

    status = ucg_listener_am_init(am_id, &ctx->super.obj_head);
    if (status != UCS_OK) {
        goto listener_failed;
    }

    return UCS_OK;

listener_failed:
    ucs_pmodule_framework_cleanup(&ctx->super);
    return status;
}

ucs_status_t ucf_init_version(unsigned ucf_api_major_version,
                              unsigned ucf_api_minor_version,
                              unsigned ucg_api_major_version,
                              unsigned ucg_api_minor_version,
                              unsigned ucp_api_major_version,
                              unsigned ucp_api_minor_version,
                              const ucf_params_t *params,
                              const ucf_config_t *config,
                              ucf_context_h *context_p)
{
    ucs_status_t status;
    unsigned major_version, minor_version, release_number;

    ucf_get_version(&major_version, &minor_version, &release_number);

    if ((ucf_api_major_version != major_version) ||
        ((ucf_api_major_version == major_version) &&
         (ucf_api_minor_version > minor_version))) {
        ucs_debug_address_info_t addr_info;
        status = ucs_debug_lookup_address(ucp_init_version, &addr_info);
        ucs_warn("UCF version is incompatible, required: %d.%d, actual: %d.%d (release %d %s)",
                  ucf_api_major_version, ucf_api_minor_version,
                  major_version, minor_version, release_number,
                  status == UCS_OK ? addr_info.file.path : "");
    }

    if (params->super == NULL) {
        status = UCS_ERR_INVALID_PARAM;
        goto err;
    }

    ucf_config_t *dfl_config = NULL;
    if (config == NULL) {
        status = ucf_config_read(NULL, NULL, &dfl_config);
        if (status != UCS_OK) {
            goto err;
        }
        dfl_config->ucg_config = NULL;
        config                 = dfl_config;
    }

    /* Store the UCF parameters in a global location, for easy access */
    ucf_store_params(params);

    /* Create the UCP context, which should have room for UCG in its headroom */
    status = ucg_init_version(ucg_api_major_version, ucg_api_minor_version,
                              ucp_api_major_version, ucp_api_minor_version,
                              params->ucg, (const ucg_config_t*)&config->super,
                              (ucg_context_h*)context_p);
    if (status != UCS_OK) {
        goto err_config;
    }

    *context_p = ucs_container_of(*context_p, ucf_context_t, ucg_ctx);
    status     = ucf_context_init(params, config, *context_p);

    if (status != UCS_OK) {
        goto err_context;
    }

    return UCS_OK;

err_context:
    ucg_cleanup(&(*context_p)->ucg_ctx);
err_config:
    if (dfl_config != NULL) {
        ucf_config_release(dfl_config);
    }
err:
    return status;
}

ucs_status_t ucf_init(const ucf_params_t *params,
                      const ucf_config_t *config,
                      ucf_context_h *context_p)
{
    return ucf_init_version(UCF_API_MAJOR, UCF_API_MINOR,
                            UCG_API_MAJOR, UCG_API_MINOR,
                            UCP_API_MAJOR, UCP_API_MINOR,
                            params, config, context_p);
}

void ucf_cleanup(ucf_context_h context)
{
    ucg_cleanup(&context->ucg_ctx);
    ucs_pmodule_framework_cleanup(&context->super);
}

ucg_context_h ucf_context_get_ucg(ucf_context_h context)
{
    return &context->ucg_ctx;
}

void ucf_context_print_info(const ucf_context_h context, FILE *stream)
{
    ucs_pmodule_framework_print_info(&context->super, "UCF", stream);
}

ucs_status_t ucf_config_read(const char *env_prefix, const char *filename,
                             ucf_config_t **config_p)
{
    ucf_config_t *config = UCS_ALLOC_CHECK(sizeof(*config), "ucf config");
    ucg_config_t **ucg   = (ucg_config_t**)&config->super.parent;
    ucs_status_t status  = ucg_config_read(env_prefix, filename, ucg);
    if (status != UCS_OK) {
        goto err_free_config;
    }

    env_prefix = (*ucg)->ucp_config->env_prefix;
    status     = ucs_config_parser_fill_opts(config, ucf_config_table,
                                             env_prefix, NULL, 0);
    if (status != UCS_OK) {
        goto err_free_ucp_config;
    }

    *config_p = config;
    return UCS_OK;

err_free_ucp_config:
    ucg_config_release(*ucg);
err_free_config:
    ucs_free(config);
err:
    return status;
}

void ucf_config_release(ucf_config_t *config)
{
    ucs_config_parser_release_opts(config, ucf_config_table);
    ucg_config_release(config->ucg_config);
    ucs_free(config);
}

ucs_status_t ucf_config_modify(ucf_config_t *config, const char *name,
                               const char *value)
{
    return ucs_config_parser_set_value(config, ucf_config_table, name, value);
}

void ucf_config_print(const ucf_config_t *config, FILE *stream,
                      const char *title, ucs_config_print_flags_t print_flags)
{
    ucs_config_parser_print_opts(stream, title, config, ucf_config_table,
                                 NULL, UCS_DEFAULT_ENV_PREFIX, print_flags);
}
