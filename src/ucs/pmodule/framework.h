/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCS_PMODULE_FRAMEWORK_H_
#define UCS_PMODULE_FRAMEWORK_H_

#include <dlfcn.h>
#include <ucs/sys/module.h>
#include <ucs/datastruct/list.h>

#include "component.h"

#define UCS_PMODULE_FRAMEWORK_INIT_VERSION_PROLOG(_upper, _lower,\
                                                  _status, _config, _in_params,\
                                                  _api_major_version, \
                                                  _api_minor_version, \
                                                  _out_params) \
    int ret; \
    Dl_info dl_info; \
    ucs_status_t status; \
    ucs_log_level_t log_level; \
    unsigned major_version, minor_version, release_number; \
    UCS_V_UNUSED uint64_t headroom_flag, parent_headroom_flag; \
    UCS_STRING_BUFFER_ONSTACK(strb, 256); \
    \
    _lower ## _config_t *dfl_config = NULL; \
    _lower ## _get_version(&major_version, &minor_version, &release_number); \
    \
    if ((major_version == _api_major_version) && \
        (minor_version >= _api_minor_version)) { \
        /* API version is compatible: same major, same or higher minor */ \
        ucs_string_buffer_appendf(&strb, "Version %s", \
                                  _lower ## _get_version_string()); \
        log_level = UCS_LOG_LEVEL_INFO; \
    } else { \
        ucs_string_buffer_appendf( \
            &strb, \
            #_upper " API version is incompatible: required >= %d.%d, actual %s", \
            _api_major_version, _api_minor_version, \
            _lower ## _get_version_string()); \
        log_level = UCS_LOG_LEVEL_WARN; \
    } \
    \
    if (ucs_log_is_enabled(log_level)) { \
        ret = dladdr(_lower ## _init_version, &dl_info); \
        if (ret != 0) { \
            ucs_string_buffer_appendf(&strb, " (loaded from %s)", \
                                      dl_info.dli_fname); \
        } \
        ucs_log(log_level, "%s", ucs_string_buffer_cstr(&strb)); \
    } \
    \
    if (config == NULL) { \
        status = _lower ## _config_read(NULL, NULL, &dfl_config);\
        if (status != UCS_OK) { \
            goto err; \
        } \
        _config = dfl_config; \
    } else { \
        status = UCS_OK; \
    } \
    \
    if (status == UCS_OK) { \
        _lower ## _context_copy_used_params(_out_params, _in_params); \
    }

#define UCS_PMODULE_FRAMEWORK_INIT_VERSION_EPILOG(_upper, _lower, \
                                                  _upper_parent, \
                                                  _lower_parent, \
                                                  _config, _in_params,\
                                                  _out_params) \
    _lower_parent ## _params_t* parent_params = \
        (_lower_parent ## _params_t*)((_in_params)->super); \
    if (parent_params == NULL) { \
        status = UCS_ERR_INVALID_PARAM; \
        goto err_config; \
    } \
    headroom_flag        = (_in_params)->field_mask & \
                           _upper ## _PARAM_FIELD_CONTEXT_HEADROOM; \
    parent_headroom_flag = parent_params->field_mask & \
                           _upper_parent ## _PARAM_FIELD_CONTEXT_HEADROOM; \
    if (!parent_headroom_flag) { \
        parent_params->field_mask |= \
            _upper_parent ## _PARAM_FIELD_CONTEXT_HEADROOM; \
        parent_params->context_headroom = 0; \
    } \
    if (headroom_flag) { \
        parent_params->context_headroom += (_in_params)->context_headroom; \
    } \
    parent_params->context_headroom += ucs_offsetof(_lower ## _context_t, \
                                                    _lower_parent ## _ctx); \
    \
    /* Create parent context, which should have room for us in its headroom */ \
    status = _lower_parent ## _init \
        ((_lower_parent ## _params_t*)((_in_params)->super), \
         (_lower_parent ## _config_t*)(_config)->super.parent, \
         (_lower_parent ## _context_h*)context_p); \
    \
    /* Undo the crime (even if parent init fails) */ \
    if (parent_headroom_flag) { \
        if (headroom_flag) { \
            parent_params->context_headroom -= (_in_params)->context_headroom; \
        } \
        parent_params->context_headroom -= \
            ucs_offsetof(_lower ## _context_t, \
                         _lower_parent ## _ctx); \
        *context_p = UCS_PTR_BYTE_OFFSET(*context_p, \
                                         -parent_params->context_headroom); \
    } else { \
        parent_params->field_mask &= ~UCP_PARAM_FIELD_CONTEXT_HEADROOM; \
    } \
    \
    if (status != UCS_OK) { \
        goto err_config; \
    } \
    \
    *context_p = ucs_container_of(*context_p, _lower ## _context_t, \
                                  _lower_parent ## _ctx); \
    status     = _lower ## _context_init(config, *context_p); \
    if (status != UCS_OK) { \
        goto err_context; \
    } \
    \
    return UCS_OK; \
    \
err_context: \
    _lower_parent ## _cleanup \
        (&(*context_p)->_lower_parent ## _ctx); \
err_config: \
    if (dfl_config != NULL) { \
        _lower ## _config_release(dfl_config); \
    } \
err: \
    return status;


#define UCS_PMODULE_FRAMEWORK_CONTEXT_COMMON(_upper, _lower, _upper_parent, \
                                             _lower_parent, _ucp_params) \
\
_lower ## _params_t _lower ## _global_params; \
void \
_lower ## _context_copy_used_params(_lower ## _params_t *dst, \
                                    const _lower ## _params_t *src); \
\
void _lower ## _cleanup(_lower ## _context_h context) \
{ \
    _lower ## _context_cleanup(context); \
    _lower_parent ## _cleanup(&context->_lower_parent ## _ctx); \
} \
\
void _lower ## _context_print_info \
    (const _lower ## _context_h context, FILE *stream) \
{ \
    ucs_pmodule_framework_print_info(&context->super, #_upper_parent , stream);\
    _lower_parent ## _context_print_info(&context->_lower_parent ## _ctx, \
                                         stream); \
} \
\
ucs_status_t _lower ## _config_read(const char *env_prefix, \
                                    const char *filename, \
                                    _lower ## _config_t **config_p) \
{ \
    ucs_status_t status; \
    _lower_parent ## _config_t **parent; \
    \
    _lower ## _config_t *config = \
        UCS_ALLOC_CHECK(sizeof(*config), # _lower " config"); \
    \
    parent = (_lower_parent ## _config_t**)&config->super.parent; \
    status = _lower_parent ## _config_read(env_prefix, filename, parent); \
    if (status != UCS_OK) { \
        goto err_free_config; \
    } \
    \
    config->super.env_prefix = (*parent)->super.env_prefix; \
    \
    status = ucs_config_parser_fill_opts(config, \
                                         UCS_CONFIG_GET_TABLE(_lower ## _config_table), \
                                         config->super.env_prefix, 0); \
    if (status != UCS_OK) { \
        goto err_free__lower_parent ## _config; \
    } \
    \
    *config_p = config; \
    return UCS_OK; \
    \
err_free__lower_parent ## _config: \
    _lower_parent ## _config_release(*parent); \
err_free_config: \
    ucs_free(config); \
    return status; \
} \
\
void _lower ## _config_release(_lower ## _config_t *config) \
{ \
    _lower_parent ## _config_t *parent = \
        (_lower_parent ## _config_t*)config->super.parent; \
    \
    ucs_config_parser_release_opts(config, _lower ## _config_table); \
    _lower_parent ## _config_release(parent); \
    ucs_free(config); \
} \
\
ucs_status_t _lower ## _config_modify(_lower ## _config_t *config,\
                                      const char *name, const char *value) \
{ \
    ucs_status_t status; \
    _lower_parent ## _config_t *parent; \
    \
    parent = (_lower_parent ## _config_t*)config->super.parent; \
    status = _lower_parent ## _config_modify(parent, name, value); \
    if (status != UCS_ERR_NO_ELEM) { \
        return status; \
    } \
    \
    return ucs_config_parser_set_value(config, _lower ## _config_table, NULL, name, value); \
} \
\
void _lower ## _config_print(const _lower ## _config_t *config, \
                             FILE *stream, const char *title, \
                             ucs_config_print_flags_t print_flags) \
{ \
    _lower_parent ## _config_t *parent; \
    parent = (_lower_parent ## _config_t*)config->super.parent; \
    _lower_parent ## _config_print(parent, stream, title, print_flags); \
    \
    ucs_config_parser_print_opts(stream, title, config, \
                                 _lower ## _config_table, \
                                 NULL, UCS_DEFAULT_ENV_PREFIX, print_flags); \
} \
\
ucs_status_t _lower ## _init_version(unsigned api_major_version, \
                                     unsigned api_minor_version, \
                                     const _lower ## _params_t *params, \
                                     const _lower ## _config_t *config, \
                                     _lower ## _context_h *context_p) \
{ \
    UCS_PMODULE_FRAMEWORK_INIT_VERSION_PROLOG(_upper, _lower, \
                                              status, config, params, \
                                              api_major_version, \
                                              api_minor_version, \
                                              &_lower ## _global_params); \
    UCS_PMODULE_FRAMEWORK_INIT_VERSION_EPILOG(_upper, _lower, _upper_parent, \
                                              _lower_parent, config, params, \
                                              &_lower ## _global_params); \
} \
_lower_parent ## _context_h \
_lower ## _context_get_ ## _lower_parent (_lower ## _context_h context) \
{ \
    return &context->_lower_parent ## _ctx; \
}


#define UCS_PMODULE_COMPONENT_LIST(_prefix) _prefix ## _components_list
#define UCS_PMODULE_COMPONENT_QUERY(_prefix) \
    query_ ## _prefix ## _components_list
#define UCS_PMODULE_FRAMEWORK(_upper, _lower) \
    UCS_LIST_HEAD(UCS_PMODULE_COMPONENT_LIST(_lower)); \
    UCS_CONFIG_REGISTER_TABLE(_lower ## _config_table, # _lower, # _upper "_", \
                              _lower ## _config_t, &ucs_config_global_list) \
    static UCS_F_ALWAYS_INLINE ucs_list_link_t* \
    UCS_PMODULE_COMPONENT_QUERY(_lower) (unsigned flags) { \
        UCS_MODULE_FRAMEWORK_DECLARE(_lower); \
        UCS_MODULE_FRAMEWORK_LOAD(_lower, flags); \
        return &UCS_PMODULE_COMPONENT_LIST(_lower); \
    }

extern ucs_config_field_t ucs_pmodule_config_table[];
typedef struct ucs_pmodule_framework_config  ucs_pmodule_framework_config_t;
struct ucs_pmodule_framework_config {
    /** Array of module names to use */
    ucs_config_allow_list_t components;

    /** Used for UCX layers depending on another layer */
    ucs_pmodule_framework_config_t *parent;

    /** This config environment prefix */
    char *env_prefix;

    /** Up to how many operations should be cached in each target */
    unsigned cache_size;
};

struct ucs_pmodule_framework_context {
    ucs_list_link_t                component_descs;  /**< list of planners */
    ucs_pmodule_component_ctx_h    component_ctx;    /**< concatenated contexts */
    ucs_list_link_t                targets_head;     /**< targets ever created */
    size_t                         per_tgt_ctx_size; /**< total context size */
    ucs_pmodule_framework_config_t config;           /**< common configuration */

    UCS_STATS_NODE_DECLARE(stats);
};

ucs_status_t
ucs_pmodule_framework_init(ucs_pmodule_framework_context_t *ctx,
                           const ucs_pmodule_component_params_t *comp_params,
                           const ucs_pmodule_framework_config_t *config,
                           ucs_list_link_t* components_list,
                           const char *framework_name);

void ucs_pmodule_framework_cleanup(ucs_pmodule_framework_context_t *ctx);

ucs_status_t
ucs_pmodule_framework_target_create(ucs_pmodule_framework_context_t *ctx,
                                    const ucs_pmodule_target_params_t *params,
                                    ucs_pmodule_target_t **target_p);

void ucs_pmodule_framework_target_destroy(ucs_pmodule_framework_context_t *ctx,
                                          ucs_pmodule_target_t *target);

ucs_status_t
ucs_pmodule_framework_target_plan(ucs_pmodule_framework_context_t *ctx,
                                  ucs_pmodule_target_t *target,
                                  ucs_pmodule_target_action_params_h params,
                                  ucs_pmodule_target_plan_t **plan_p);

void ucs_pmodule_framework_target_plan_discard(ucs_pmodule_target_plan_t *plan);

void ucs_pmodule_framework_print_info(ucs_pmodule_framework_context_t *ctx,
                                      const char* framework_name, FILE *stream);

#endif /* UCS_PMODULE_FRAMEWORK_H_ */
