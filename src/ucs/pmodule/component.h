/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCS_PMODULE_COMPONENT_H_
#define UCS_PMODULE_COMPONENT_H_

#include <string.h>

#include <ucs/arch/cpu.h>
#include <ucs/sys/compiler.h>
#include <ucs/config/parser.h>
#include <ucs/datastruct/queue_types.h>
#include <ucs/datastruct/list.h>
#include <ucs/type/spinlock.h>

typedef struct ucs_pmodule_component        ucs_pmodule_component_t;
typedef struct ucs_pmodule_component_params ucs_pmodule_component_params_t;
typedef void*                               ucs_pmodule_component_config_h;
typedef void*                               ucs_pmodule_component_ctx_h;
typedef void*                               ucs_pmodule_object_ctx_h;

#include "framework.h" /* uses some of the above definitions */
#include "object.h" /* uses definitions from framework */

enum ucs_pmodule_component_params_field {
    UCS_PMODULE_COMPONENT_PARAMS_FIELD_AM_ID = UCS_BIT(0)
};

struct ucs_pmodule_component_params {
    uint64_t field_mask;

    /*
     * Active-Message ID allocator
     */
    uint8_t *am_id;
};

/**
 * @ingroup UCF_RESOURCE
 * @brief Collective planning resource descriptor.
 *
 * This structure describes a collective operation planning resource.
 */
typedef struct ucs_pmodule_desc {
    char                     name[UCS_PMODULE_NAME_MAX]; /**< Name */
    ucs_pmodule_component_t *component;                  /**< Component object */
} ucs_pmodule_desc_t;


typedef struct ucs_pmodule_plan {
    /* Plan lookup - caching mechanism */
    ucs_pmodule_op_type_t    type;
    ucs_recursive_spinlock_t lock;
    ucs_pmodule_cache_t      cache;
    ucs_pmodule_desc_t      *desc;
    ucs_pmodule_object_t    *object;
    char                     priv[0];
} ucs_pmodule_plan_t;

struct ucs_pmodule_framework_context {
    ucs_pmodule_desc_t             *component_descs;
    void                           *component_ctx;
    unsigned                        num_components;
    size_t                          per_obj_ctx_size;
    ucs_list_link_t                 obj_head;

    ucs_pmodule_framework_config_t *config;
};

struct ucs_pmodule_action_params {
    ucs_pmodule_op_type_t type;

    UCS_CACHELINE_PADDING(ucs_pmodule_op_type_t);
} UCS_S_PACKED UCS_V_ALIGNED(64);

typedef ucs_status_t (*ucs_pmodule_action_trigger_f)(ucs_pmodule_action_t *action,
                                                     ucs_pmodule_op_id_t op_id,
                                                     void *request);
typedef void         (*ucs_pmodule_action_discard_f)(ucs_pmodule_action_t *action, uint32_t id);
typedef void         (*ucs_pmodule_action_compreq_f)(void *req, ucs_status_t status);

struct ucs_pmodule_action {
    /* Collective-specific request content */
    ucs_pmodule_action_trigger_f trigger_f;     /**< shortcut for the trigger call */
    ucs_pmodule_action_discard_f discard_f;     /**< shortcut for the discard call */
    ucs_pmodule_action_compreq_f compreq_f;     /**< request completion callback */

    union {
        ucs_list_link_t          list;          /**< cache list member */
        struct {
            ucs_queue_elem_t     queue;         /**< pending queue member */
            void                *pending_req;   /**< original invocation request */
        };
    };

    ucs_pmodule_plan_t          *plan;          /**< File access this belongs to */

    ucs_pmodule_action_params_t  params;        /**< original parameters for it */
    /* Note: the params field must be aligned, for SIMD ISA */

    /* Component-specific request content */
    char                         priv[0];
};

struct ucs_pmodule_component {
    const char                     name[UCS_PMODULE_NAME_MAX]; /**< Component name */
    ucs_config_global_list_entry_t config;           /**< Configuration information */
    size_t                         global_ctx_size;  /**< Size to be allocated once, for context */
    size_t                         per_obj_ctx_size; /**< Size to be allocated within each group */
    ucs_list_link_t                list;             /**< Entry in global list of components */

    /* test for support and other attribures of this component */
    ucs_status_t           (*query)(ucs_pmodule_desc_t *descs,
                                    unsigned *desc_cnt_p);

    ucs_status_t           (*init)(ucs_pmodule_component_ctx_h ctx,
                                   const ucs_pmodule_component_params_t *params,
                                   ucs_pmodule_component_config_h config);

    void                   (*finalize)(ucs_pmodule_component_ctx_h ctx);

    /* create a new planner context for a group */
    ucs_status_t           (*create)(ucs_pmodule_component_ctx_h ctx,
                                     ucs_pmodule_object_ctx_h octx,
                                     ucs_pmodule_object_t *object,
                                     const ucs_pmodule_object_params_t *params);

    /* destroy a group context, along with all its operations and requests */
    void                   (*destroy)(ucs_pmodule_object_ctx_h octx);

    /* establish initial file access with this component */
    ucs_status_t           (*plan)(ucs_pmodule_object_ctx_h octx,
                                   const ucs_pmodule_action_params_t *params,
                                   ucs_pmodule_plan_t **plan_p);

    /* Prepare an operation to follow the given plan */
    ucs_status_t           (*prepare)(ucs_pmodule_plan_t *plan,
                                      const ucs_pmodule_action_params_t *params,
                                      ucs_pmodule_action_t **action_p);

    /* Trigger an operation to start, generate a request handle for updates */
    ucs_pmodule_action_trigger_f  trigger;

    /* Progress an outstanding operation */
    ucs_pmodule_action_progress_f progress;

    /* Discard an operation previously prepared */
    ucs_pmodule_action_discard_f  discard;

    /* print a plan object, for debugging purposes */
    void                   (*print)(ucs_pmodule_plan_t *plan,
                                    const ucs_pmodule_action_params_t *params);

    /* Handle a fault in a given index of the  */
    ucs_status_t           (*fault)(ucs_pmodule_object_ctx_h octx, uint64_t id);
};

/**
 * Define a planning component.
 *
 * @param _comp_name  Component structure to initialize.
 * @param _name       Component name.
 * @param _glob_size  Size of the global context allocated for this framework.
 * @param _obj_size   Size of a per-object context allocated in this framework.
 * @param _query      Function to query planning resources.
 * @param _init       Function to initialize the component.
 * @param _finalize   Function to finalize the component (release resources).
 * @param _create     Function to create the component context.
 * @param _destroy    Function to destroy the component context.
 * @param _progress   Function to progress operations by this component.
 * @param _plan       Function to plan future operations.
 * @param _prepare    Function to prepare an operation according to a plan.
 * @param _trigger    Function to start a prepared collective operation.
 * @param _discard    Function to release an operation and related objects.
 * @param _print      Function to output information useful for developers.
 * @param _fault      Function to handle faults detected during the run.
 * @param _cfg_prefix Prefix for configuration environment variables.
 * @param _cfg_table  Defines the planning component's configuration values.
 * @param _cfg_struct Planning component configuration structure.
 */
#define UCS_PMODULE_COMPONENT_DEFINE(_comp_name, _name, _glob_size, _obj_size, \
                                     _query, _init, _finalize, _create, \
                                     _destroy, _plan, _prepare, _trigger, \
                                     _progress, _discard, _print, _fault, \
                                     _cfg_table, _cfg_struct, _cfg_prefix, \
                                     _components_list) \
    ucs_pmodule_component_t _comp_name = { \
        .name             = _name, \
        .config.name      = _name" module",\
        .config.prefix    = _cfg_prefix, \
        .config.table     = _cfg_table, \
        .config.size      = sizeof(_cfg_struct), \
        .global_ctx_size  = _glob_size, \
        .per_obj_ctx_size = _obj_size, \
        .query            = _query, \
        .init             = _init, \
        .finalize         = _finalize, \
        .create           = _create, \
        .destroy          = _destroy, \
        .plan             = _plan, \
        .prepare          = _prepare, \
        .trigger          = _trigger, \
        .progress         = _progress, \
        .discard          = _discard, \
        .print            = _print, \
        .fault            = _fault \
    }; \
    extern ucs_list_link_t* _components_list; \
    UCS_STATIC_INIT { \
        ucs_list_add_tail(_components_list, &(_comp_name).list); \
    } \
    UCS_CONFIG_REGISTER_TABLE_ENTRY(&(_comp_name).config, &ucs_config_global_list)

ucs_status_t ucs_pmodule_component_query(ucs_list_link_t* components_list,
                                         ucs_pmodule_desc_t **desc_p,
                                         unsigned *num_desc_p,
                                         size_t *total_fs_ctx_size);

ucs_status_t
ucs_pmodule_component_init(ucs_pmodule_desc_t *descs, unsigned desc_cnt,
                           const ucs_pmodule_component_params_t *params,
                           ucs_pmodule_component_ctx_h comp_ctx,
                           size_t *per_obj_ctx_size);

void ucs_pmodule_component_finalize(ucs_pmodule_desc_t *descs,
                                    unsigned desc_cnt,
                                    ucs_pmodule_component_ctx_h comp_ctx);

ucs_status_t
ucs_pmodule_component_create(ucs_pmodule_object_t *object,
                             const ucs_pmodule_object_params_t *obj_params);

void ucs_pmodule_component_destroy(ucs_pmodule_object_t *object);

void ucs_pmodule_component_print_info(ucs_pmodule_desc_t *descs,
                                      unsigned desc_cnt, FILE *stream);

/* Helper function to generate a simple file-system description */
ucs_status_t ucs_pmodule_component_single(ucs_pmodule_component_t *component,
                                          ucs_pmodule_desc_t *descs,
                                          unsigned *desc_cnt_p);

/* Helper function for selecting other modules - to be used as fall-back */
ucs_status_t
ucs_pmodule_component_choose(const ucs_pmodule_action_params_t *params,
                             ucs_pmodule_object_t *obj,
                             ucs_pmodule_desc_t **desc_p,
                             ucs_pmodule_object_ctx_h *octx_p);

#endif /* UCS_PMODULE_COMPONENT_H_ */
