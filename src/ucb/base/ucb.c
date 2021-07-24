/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucs/debug/debug_int.h>
#include <ucs/pmodule/framework.h>
#include <ucg/base/ucg_context.h>
#include <ucp/core/ucp_types.h>
#include <ucb/api/ucb_version.h>
#include <ucg/api/ucg_version.h>
#include <ucp/api/ucp_version.h>
#include <ucb/api/ucb.h>

#define UCB_FILE_TO_GROUP(_file) ((ucg_group_h)file->super.parent)

typedef struct ucb_config {
    /** General configuration for a persistent component framework */
    ucs_pmodule_framework_config_t super;

    /* TODO: Global and UCB-specific configuration, e.g. whether to use threads */

    ucg_config_t *ucg_config;
} ucb_config_t;

ucs_config_field_t ucb_config_table[] = {
  /* TODO: Global and UCB-specific configuration, e.g. whether to use threads */

  {NULL}

};

UCS_PMODULE_FRAMEWORK(b)

typedef struct ucb_context {
    /* TODO: variables to hold global configuration from @ref ucb_config_t */

    ucs_pmodule_framework_context_t super;

    ucg_context_t ucg_ctx;/* must be last (ABI compat.) */
} ucb_context_t;

struct ucb_file {
    ucs_pmodule_object_t super;
};

ucb_params_t ucb_global_params; /* Ugly - but efficient */

static void ucb_comp_fallback(void *req, ucs_status_t status)
{
    uint32_t *flags = UCS_PTR_BYTE_OFFSET(req,
            ucb_global_params.completion.comp_flag_offset);

    ucs_status_t *result = UCS_PTR_BYTE_OFFSET(req,
            ucb_global_params.completion.comp_status_offset);

    *result = status;
    *flags |= 1;
}

static void ucb_comp_fallback_no_offsets(void *req, ucs_status_t status)
{
    *(uint8_t*)req = (uint8_t)-1;
}

static void ucb_store_params(const ucb_params_t *params)
{
    if (params->field_mask & UCG_PARAM_FIELD_COMPLETION_CB) {
        ucb_global_params.completion = params->completion;
        if (ucb_global_params.completion.coll_comp_cb_f == NULL) {
            ucb_global_params.completion.coll_comp_cb_f =
                    ucb_comp_fallback;
        }
    } else {
        ucb_global_params.completion.coll_comp_cb_f =
                ucb_comp_fallback_no_offsets;
    }
}

static ucs_status_t ucb_context_init(const ucb_params_t *params,
                                     const ucb_config_t *config,
                                     ucb_context_t *ctx)
{
    ucs_pmodule_component_params_t comp_params = {0};
    return ucs_pmodule_framework_init(&comp_params, params->super,
                                      &config->super,
                                      &UCS_PMODULE_COMPONENT_LIST(b),
                                      &ctx->super);
}

ucs_status_t ucb_init_version(unsigned ucb_api_major_version,
                              unsigned ucb_api_minor_version,
                              unsigned ucp_api_major_version,
                              unsigned ucp_api_minor_version,
                              const ucb_params_t *params,
                              const ucb_config_t *config,
                              ucb_context_h *context_p)
{
    ucs_status_t status;
    unsigned major_version, minor_version, release_number;

    ucb_get_version(&major_version, &minor_version, &release_number);

    if ((ucb_api_major_version != major_version) ||
        ((ucb_api_major_version == major_version) &&
         (ucb_api_minor_version > minor_version))) {
        ucs_debug_address_info_t addr_info;
        status = ucs_debug_lookup_address(ucp_init_version, &addr_info);
        ucs_warn("UCB version is incompatible, required: %d.%d, actual: %d.%d (release %d %s)",
                  ucb_api_major_version, ucb_api_minor_version,
                  major_version, minor_version, release_number,
                  status == UCS_OK ? addr_info.file.path : "");
    }

    if (params->super == NULL) {
        status = UCS_ERR_INVALID_PARAM;
        goto err;
    }

    ucb_config_t *dfl_config = NULL;
    if (config == NULL) {
        status = ucb_config_read(NULL, NULL, &dfl_config);
        if (status != UCS_OK) {
            goto err;
        }
        dfl_config->ucg_config = NULL;
        config                 = dfl_config;
    }

    /* Store the UCB parameters in a global location, for easy access */
    ucb_store_params(params);

    /* Create the UCP context, which should have room for UCG in its headroom */
    status = ucp_init_version(ucp_api_major_version, ucp_api_minor_version,
                              params->ucp, (const ucp_config_t*)&config->super,
                              (ucp_context_h*)context_p);
    if (status != UCS_OK) {
        goto err_config;
    }

    *context_p = ucs_container_of(*context_p, ucb_context_t, ucg_ctx);
    status     = ucb_context_init(params, config, *context_p);

    if (status != UCS_OK) {
        goto err_context;
    }

    return UCS_OK;

err_context:
    ucg_cleanup(&(*context_p)->ucg_ctx);
err_config:
    if (dfl_config != NULL) {
        ucb_config_release(dfl_config);
    }
err:
    return status;
}

ucs_status_t ucb_init(const ucb_params_t *params,
                      const ucb_config_t *config,
                      ucb_context_h *context_p)
{
    return ucb_init_version(UCB_API_MAJOR, UCB_API_MINOR,
                            UCP_API_MAJOR, UCP_API_MINOR,
                            params, config, context_p);
}

void ucb_cleanup(ucb_context_h context)
{
    ucg_cleanup(&context->ucg_ctx);
    ucs_pmodule_framework_cleanup(&context->super);
}

ucg_context_h ucb_context_get_ucg(ucb_context_h context)
{
    return &context->ucg_ctx;
}

void ucb_context_print_info(const ucb_context_h context, FILE *stream)
{
    ucs_pmodule_framework_print_info(&context->super, "UCB", stream);
}

ucs_status_t ucb_config_read(const char *env_prefix, const char *filename,
                             ucb_config_t **config_p)
{
    ucb_config_t *config = UCS_ALLOC_CHECK(sizeof(*config), "ucb config");
    ucg_config_t **ucg   = (ucg_config_t**)&config->super.parent;
    ucs_status_t status  = ucg_config_read(env_prefix, filename, ucg);
    if (status != UCS_OK) {
        goto err_free_config;
    }

    env_prefix = (*ucg)->ucp_config->env_prefix;
    status     = ucs_config_parser_fill_opts(config, ucb_config_table,
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

void ucb_config_release(ucb_config_t *config)
{
    ucs_config_parser_release_opts(config, ucb_config_table);
    ucg_config_release(config->ucg_config);
    ucs_free(config);
}

ucs_status_t ucb_config_modify(ucb_config_t *config, const char *name,
                               const char *value)
{
    return ucs_config_parser_set_value(config, ucb_config_table, name, value);
}

void ucb_config_print(const ucb_config_t *config, FILE *stream,
                      const char *title, ucs_config_print_flags_t print_flags)
{
    ucs_config_parser_print_opts(stream, title, config, ucb_config_table,
                                 NULL, UCS_DEFAULT_ENV_PREFIX, print_flags);
}
