/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCS_PMODULE_COMPONENT_H_
#define UCS_PMODULE_COMPONENT_H_

#include <string.h>

#include <ucs/arch/cpu.h>
#include <ucs/sys/math.h>
#include <ucs/sys/compiler.h>
#include <ucs/config/parser.h>
#include <ucs/datastruct/list.h>

#include "target.h"

typedef struct ucs_pmodule_component_params ucs_pmodule_component_params_t;
typedef void*                               ucs_pmodule_component_config_h;
typedef void*                               ucs_pmodule_component_ctx_h;
typedef void*                               ucs_pmodule_target_ctx_h;

enum ucs_pmodule_component_params_field {
    UCS_PMODULE_COMPONENT_PARAMS_FIELD_AM_ID        = UCS_BIT(0),
    UCS_PMODULE_COMPONENT_PARAMS_FIELD_DUMMY_GROUP  = UCS_BIT(1),
    UCS_PMODULE_COMPONENT_PARAMS_FIELD_BARRIR_DELAY = UCS_BIT(2)
};

struct ucs_pmodule_component_params {
    uint64_t field_mask;

    /*
     * Active-Message ID allocator
     */
    uint8_t *am_id;

    /*
     * Dummy group for unexpected incoming messages (e.g. Active Messages).
     */
     void *dummy_group;

    /*
     * Delay barriers for benchmarking purposes.
     */
     int is_barrier_delayed;
};

/**
 * @ingroup UCF_RESOURCE
 * @brief Collective planning resource descriptor.
 *
 * This structure describes a collective operation planning resource.
 */
typedef struct ucs_pmodule_desc {
    ucs_pmodule_component_t *component;
    ucs_list_link_t          list;
} ucs_pmodule_component_desc_t;

struct ucs_pmodule_component {
    const char                     name[UCS_PMODULE_NAME_MAX]; /**< Component name */
    ucs_config_global_list_entry_t config;           /**< Configuration information */
    size_t                         global_ctx_size;  /**< Size to be allocated once, for context */
    size_t                         per_tgt_ctx_size; /**< Size to be allocated within each group */
    ucs_list_link_t                list;             /**< Entry in global list of components */

    /* Test for support and other attribures of this component */
    ucs_status_t           (*query)(ucs_list_link_t *desc_head);

    ucs_status_t           (*init)(ucs_pmodule_component_ctx_h ctx,
                                   const ucs_pmodule_component_params_t *params,
                                   ucs_pmodule_component_config_h config);

    void                   (*finalize)(ucs_pmodule_component_ctx_h ctx);

    /* Create a new planner context for a group */
    ucs_status_t           (*create)(ucs_pmodule_component_ctx_h ctx,
                                     ucs_pmodule_target_ctx_h tctx,
                                     ucs_pmodule_target_t *target,
                                     const ucs_pmodule_target_params_t *params);

    /* Destroy a group context, along with all its operations and requests */
    void                   (*destroy)(ucs_pmodule_target_ctx_h tctx);

    /* Estimate action time using this component */
    ucs_status_t           (*estimate)(ucs_pmodule_target_ctx_h tctx,
                                       ucs_pmodule_target_action_params_h params,
                                       double *estimate_p);

    /* Plan a future action with this component */
    ucs_status_t           (*plan)(ucs_pmodule_target_ctx_h tctx,
                                   ucs_pmodule_target_action_params_h params,
                                   ucs_pmodule_target_plan_t **plan_p);

    /* Trigger an operation to start, generate a request handle for updates */
    ucs_pmodule_target_action_trigger_f  trigger;

    /* Progress an outstanding operation */
    ucs_pmodule_target_action_progress_f progress;

    /* Discard a previously prepared plan */
    void                   (*discard)(ucs_pmodule_target_plan_t *plan);

    /* print a plan target, for debugging purposes */
    void                   (*print)(ucs_pmodule_target_plan_t *plan);

    /* Handle a fault in a given index of the  */
    ucs_status_t           (*fault)(ucs_pmodule_target_ctx_h tctx, uint64_t id);
};

/**
 * Define a planning component.
 *
 * @param _comp_name  Component structure to initialize.
 * @param _name       Component name.
 * @param _glob_size  Size of the global context allocated for this framework.
 * @param _tgt_size   Size of a per-target context allocated in this framework.
 * @param _query      Function to query planning resources.
 * @param _init       Function to initialize the component.
 * @param _finalize   Function to finalize the component (release resources).
 * @param _create     Function to create the component context.
 * @param _destroy    Function to destroy the component context.
 * @param _progress   Function to progress operations by this component.
 * @param _plan       Function to plan future operations.
 * @param _prepare    Function to prepare an operation according to a plan.
 * @param _trigger    Function to start a prepared collective operation.
 * @param _discard    Function to release an operation and related targets.
 * @param _print      Function to output information useful for developers.
 * @param _fault      Function to handle faults detected during the run.
 * @param _cfg_prefix Prefix for configuration environment variables.
 * @param _cfg_table  Defines the planning component's configuration values.
 * @param _cfg_struct Planning component configuration structure.
 */
#define UCS_PMODULE_COMPONENT_DEFINE(_comp_name, _name, _glob_size, _tgt_size, \
                                     _query, _init, _finalize, _create, \
                                     _destroy, _estimate, _plan, _trigger, \
                                     _progress, _discard, _print, _fault, \
                                     _cfg_table, _cfg_struct, _cfg_prefix, \
                                     _components_list) \
    ucs_pmodule_component_t _comp_name = { \
        .name             = _name, \
        .config.name      = _name" component",\
        .config.prefix    = _cfg_prefix, \
        .config.table     = _cfg_table, \
        .config.size      = sizeof(_cfg_struct), \
        .global_ctx_size  = ucs_align_up(_glob_size, UCS_SYS_CACHE_LINE_SIZE), \
        .per_tgt_ctx_size = ucs_align_up(_tgt_size,  UCS_SYS_CACHE_LINE_SIZE), \
        .query            = _query, \
        .init             = _init, \
        .finalize         = _finalize, \
        .create           = _create, \
        .destroy          = _destroy, \
        .estimate         = _estimate, \
        .plan             = _plan, \
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

ucs_status_t
ucs_pmodule_component_query(ucs_list_link_t *components_list,
                            const ucs_config_allow_list_t *allow_list,
                            ucs_list_link_t *desc_head_p,
                            size_t *total_plan_ctx_size);

ucs_status_t
ucs_pmodule_component_init(ucs_list_link_t *desc_head,
                           const ucs_pmodule_component_params_t *params,
                           ucs_pmodule_component_ctx_h comp_ctx,
                           size_t *per_tgt_ctx_size);

void ucs_pmodule_component_print_info(ucs_list_link_t *desc_head, FILE *stream);

void ucs_pmodule_component_finalize(ucs_list_link_t *desc_head,
                                    ucs_pmodule_component_ctx_h comp_ctx);

ucs_status_t
ucs_pmodule_component_create(ucs_list_link_t *desc_head,
                             ucs_pmodule_component_ctx_h comp_ctx,
                             ucs_pmodule_target_ctx_h target_ctx,
                             ucs_pmodule_target_t *target,
                             const void* target_params);

void ucs_pmodule_component_destroy(ucs_list_link_t *desc_head,
                                   ucs_pmodule_component_ctx_h comp_ctx,
                                   ucs_pmodule_target_ctx_h target_ctx);

ucs_status_t
ucs_pmodule_component_plan(ucs_list_link_t *desc_head,
                           ucs_pmodule_component_ctx_h comp_ctx,
                           ucs_pmodule_target_ctx_h target_ctx,
                           ucs_pmodule_target_action_params_h params,
                           ucs_pmodule_target_plan_t **plan_p);

void ucs_pmodule_component_plan_discard(ucs_pmodule_target_plan_t *plan);

#endif /* UCS_PMODULE_COMPONENT_H_ */
