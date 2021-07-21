/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCS_PMODULE_FRAMEWORK_H_
#define UCS_PMODULE_FRAMEWORK_H_

#include <ucs/sys/module.h>

typedef struct ucs_pmodule_framework_params  ucs_pmodule_framework_params_t;
typedef struct ucs_pmodule_framework_context ucs_pmodule_framework_context_t;
typedef struct ucs_pmodule_framework_config  ucs_pmodule_framework_config_t;

#include "component.h" /* uses some of the above definitions */
#include "object.h" /* uses some of the above definitions */

#define UCS_PMODULE_COMPONENT_LIST(_letter) uc ## _letter ## _components_list
#define UCS_PMODULE_COMPONENT_QUERY(_letter) \
    query_ ## uc ## _letter ## _components_list
#define UCS_PMODULE_FRAMEWORK(_letter) \
    UCS_LIST_HEAD(UCS_PMODULE_COMPONENT_LIST(_letter)); \
    UCS_CONFIG_REGISTER_TABLE(uc ## _letter ## _config_table, \
                              "uc" # _letter, NULL, uc ## _letter ## _config_t,\
                              &ucs_config_global_list) \
    static UCS_F_ALWAYS_INLINE ucs_list_link_t* \
    UCS_PMODULE_COMPONENT_QUERY(_letter) (unsigned flags) { \
        UCS_MODULE_FRAMEWORK_DECLARE(uc ## _letter); \
        UCS_MODULE_FRAMEWORK_LOAD(uc ## _letter, flags); \
        return &UCS_PMODULE_COMPONENT_LIST(_letter); \
    }

struct ucs_pmodule_framework_params {
    uint64_t field_mask;
};

struct ucs_pmodule_framework_config {
    /** Array of module names to use */
    ucs_config_names_array_t modules;

    /** Used for UCX layers depending on another layer */
    ucs_pmodule_framework_config_t *parent;

    /** Up to how many operations should be cached in each object */
    unsigned cache_size;
};

ucs_status_t
ucs_pmodule_framework_init(const ucs_pmodule_component_params_t *comp_params,
                           const ucs_pmodule_framework_params_t *fw_params,
                           const ucs_pmodule_framework_config_t *fw_config,
                           ucs_list_link_t* components_list,
                           ucs_pmodule_framework_context_t *ctx);

void ucs_pmodule_framework_cleanup(ucs_pmodule_framework_context_t *ctx);

void ucs_pmodule_framework_add_object(ucs_pmodule_framework_context_t *ctx,
                                      ucs_pmodule_object_t *object);

void ucs_pmodule_framework_remove_object(ucs_pmodule_object_t *object);

void ucs_pmodule_framework_print_info(const ucs_pmodule_framework_context_t *ctx,
                                      const char* framework_name, FILE *stream);

#endif /* UCS_PMODULE_FRAMEWORK_H_ */
