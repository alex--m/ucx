/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucb/api/ucb.h>

static ucs_config_field_t ucb_builtin_config_table[] = {
    {NULL}
};

typedef struct ucb_builtin_config {
} ucb_builtin_config_t;

typedef struct ucb_builtin_ctx {

} ucb_builtin_ctx_t;

typedef struct ucb_builtin_file_ctx {
} ucb_builtin_file_ctx_t;

ucs_status_t ucb_builtin_query(ucs_pmodule_desc_t *descs, unsigned *desc_cnt_p);

static ucs_status_t ucb_builtin_init(ucs_pmodule_component_ctx_h ctx,
                                     const ucs_pmodule_component_params_t *params,
                                     ucs_pmodule_component_config_h config)
{
    //ucb_builtin_ctx_t *builtin_ctx       = (ucb_builtin_ctx_t*)ctx;
    //ucb_builtin_config_t *builtin_config = (ucb_builtin_config_t*)config;

    /* TODO: initialize per-process data-structures */

    return UCS_ERR_NOT_IMPLEMENTED;
}

static void ucb_builtin_finalize(ucs_pmodule_component_ctx_h ctx)
{
    //ucb_builtin_ctx_t *builtin_ctx = (ucb_builtin_ctx_t*)ctx;
    /* TODO: destroy per-process data-structures */
}

/* create a new planner context for a group */
static ucs_status_t ucb_builtin_create(ucs_pmodule_component_ctx_h ctx,
                                       ucs_pmodule_object_ctx_h octx,
                                       ucs_pmodule_object_t *object,
                                       const ucs_pmodule_object_params_t *params)
{
    //ucb_builtin_ctx_t *builtin_ctx        = (ucb_builtin_ctx_t*)ctx;
    //ucb_builtin_ctx_t *builtin_fctx       = (ucb_builtin_file_ctx_t*)octx;
    //ucb_file_h file                       = ucs_derived_of(object);
    // const ucb_file_params_t *file_params = ucs_derived_of(params);
    /* TODO: create a "parallel file" where I/O could be performed */
    return UCS_ERR_NOT_IMPLEMENTED;
}

/* destroy a group context, along with all its operations and requests */
static void ucb_builtin_destroy(ucs_pmodule_object_ctx_h octx)
{
    //ucb_builtin_ctx_t *builtin_fctx = (ucb_builtin_file_ctx_t*)octx;
    /* TODO: destroy this instance of a parallel file */
}

/* plan a collective operation with this component */
static ucs_status_t ucb_builtin_plan(ucs_pmodule_object_ctx_h octx,
                                     const ucs_pmodule_action_params_t *params,
                                     ucs_pmodule_plan_t **plan_p)
{
    //ucb_builtin_ctx_t *builtin_fctx  = (ucb_builtin_file_ctx_t*)octx;
    //const ucb_batch_params_t *batch_params = ucs_derived_of(params);
    /* TODO: prepare an I/O operation for later triggering */
    return UCS_ERR_NOT_IMPLEMENTED;
}

static ucs_status_t ucb_builtin_batch_create(ucs_pmodule_plan_t *plan,
                                             const ucs_pmodule_action_params_t *params,
                                             ucs_pmodule_action_t **batch_p)
{
    //const ucb_batch_params_t *batch_params = ucs_derived_of(params);
    /* TODO: prepare an I/O operation for later triggering */
    return UCS_ERR_NOT_IMPLEMENTED;
}

static ucs_status_t ucb_builtin_batch_trigger(ucs_pmodule_action_t *batch,
                                              ucs_pmodule_op_id_t batch_id,
                                              void *request)
{
    /* TODO: start the given I/O operation */
    return UCS_ERR_NOT_IMPLEMENTED;
}

static unsigned ucb_builtin_batch_progress(ucs_pmodule_action_t *batch)
{
    /* TODO: progress this request, possibly by calling lower-level progress */
    return 0;
}

static void ucb_builtin_batch_discard(ucs_pmodule_action_t *batch, uint32_t id)
{
    /* TODO: cancel an outstanding I/O operation */
}

static void ucb_builtin_print(ucs_pmodule_plan_t *plan,
                              const ucs_pmodule_action_params_t *params)
{
    /* TODO: print the file layout */
}

static ucs_status_t ucb_builtin_handle_fault(ucs_pmodule_object_ctx_h octx,
                                             uint64_t id)
{
    //ucb_builtin_ctx_t *builtin_fctx  = (ucb_builtin_file_ctx_t*)octx;
    return UCS_ERR_NOT_IMPLEMENTED;
}

UCS_PMODULE_COMPONENT_DEFINE(ucb_builtin_component, "builtin",
                             sizeof(ucb_builtin_ctx_t),
                             sizeof(ucb_builtin_file_ctx_t),
                             ucb_builtin_query, ucb_builtin_init,
                             ucb_builtin_finalize, ucb_builtin_create,
                             ucb_builtin_destroy, ucb_builtin_plan,
                             ucb_builtin_batch_create, ucb_builtin_batch_trigger,
                             ucb_builtin_batch_progress, ucb_builtin_batch_discard,
                             ucb_builtin_print, ucb_builtin_handle_fault,
                             ucb_builtin_config_table, ucb_builtin_config_t,
                             "BUILTIN_", UCS_PMODULE_COMPONENT_LIST(b));

ucs_status_t ucb_builtin_query(ucs_pmodule_desc_t *descs, unsigned *desc_cnt_p)
{
    /* Return a simple description of the "Builtin" module */
    return ucs_pmodule_component_single(&ucb_builtin_component, descs,
                                        desc_cnt_p);
}
