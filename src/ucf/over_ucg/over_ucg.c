/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucf/api/ucf.h>

static ucs_config_field_t ucf_over_ucg_config_table[] = {
    {NULL}
};

typedef struct ucf_over_ucg_config {
} ucf_over_ucg_config_t;

typedef struct ucf_over_ucg_ctx {

} ucf_over_ucg_ctx_t;

typedef struct ucf_over_ucg_file_ctx {
} ucf_over_ucg_file_ctx_t;

ucs_status_t ucf_over_ucg_query(ucs_pmodule_desc_t *descs, unsigned *desc_cnt_p);

static ucs_status_t ucf_over_ucg_init(ucs_pmodule_component_ctx_h ctx,
                                     const ucs_pmodule_component_params_t *params,
                                     ucs_pmodule_component_config_h config)
{
    //ucf_over_ucg_ctx_t *over_ucg_ctx       = (ucf_over_ucg_ctx_t*)ctx;
    //ucf_over_ucg_config_t *over_ucg_config = (ucf_over_ucg_config_t*)config;

    /* TODO: initialize per-process data-structures */

    return UCS_ERR_NOT_IMPLEMENTED;
}

static void ucf_over_ucg_finalize(ucs_pmodule_component_ctx_h ctx)
{
    //ucf_over_ucg_ctx_t *over_ucg_ctx = (ucf_over_ucg_ctx_t*)ctx;
    /* TODO: destroy per-process data-structures */
}

/* create a new planner context for a group */
static ucs_status_t ucf_over_ucg_create(ucs_pmodule_component_ctx_h ctx,
                                       ucs_pmodule_object_ctx_h octx,
                                       ucs_pmodule_object_t *object,
                                       const ucs_pmodule_object_params_t *params)
{
    //ucf_over_ucg_ctx_t *over_ucg_ctx        = (ucf_over_ucg_ctx_t*)ctx;
    //ucf_over_ucg_ctx_t *over_ucg_fctx       = (ucf_over_ucg_file_ctx_t*)octx;
    //ucf_file_h file                       = ucs_derived_of(object);
    // const ucf_file_params_t *file_params = ucs_derived_of(params);
    /* TODO: create a "parallel file" where I/O could be performed */
    return UCS_ERR_NOT_IMPLEMENTED;
}

/* destroy a group context, along with all its operations and requests */
static void ucf_over_ucg_destroy(ucs_pmodule_object_ctx_h octx)
{
    //ucf_over_ucg_ctx_t *over_ucg_fctx = (ucf_over_ucg_file_ctx_t*)octx;
    /* TODO: destroy this instance of a parallel file */
}

/* plan a collective operation with this component */
static ucs_status_t ucf_over_ucg_plan(ucs_pmodule_object_ctx_h octx,
                                     const ucs_pmodule_action_params_t *params,
                                     ucs_pmodule_plan_t **plan_p)
{
    //ucf_over_ucg_ctx_t *over_ucg_fctx  = (ucf_over_ucg_file_ctx_t*)octx;
    //const ucf_io_params_t *io_params = ucs_derived_of(params);
    /* TODO: prepare an I/O operation for later triggering */
    return UCS_ERR_NOT_IMPLEMENTED;
}

static ucs_status_t ucf_over_ucg_io_create(ucs_pmodule_plan_t *plan,
                                          const ucs_pmodule_action_params_t *params,
                                          ucs_pmodule_action_t **io_p)
{
    //const ucf_io_params_t *io_params = ucs_derived_of(params);
    /* TODO: prepare an I/O operation for later triggering */
    return UCS_ERR_NOT_IMPLEMENTED;
}

static ucs_status_t ucf_over_ucg_io_trigger(ucs_pmodule_action_t *io,
                                           ucs_pmodule_op_id_t io_id,
                                           void *request)
{
    /* TODO: start the given I/O operation */
    return UCS_ERR_NOT_IMPLEMENTED;
}

static unsigned ucf_over_ucg_io_progress(ucs_pmodule_action_t *io)
{
    /* TODO: progress this request, possibly by calling lower-level progress */
    return 0;
}

static void ucf_over_ucg_io_discard(ucs_pmodule_action_t *io, uint32_t id)
{
    /* TODO: cancel an outstanding I/O operation */
}

static void ucf_over_ucg_print(ucs_pmodule_plan_t *plan,
                              const ucs_pmodule_action_params_t *params)
{
    /* TODO: print the file layout */
}

static ucs_status_t ucf_over_ucg_handle_fault(ucs_pmodule_object_ctx_h octx,
                                             uint64_t id)
{
    //ucf_over_ucg_ctx_t *over_ucg_fctx  = (ucf_over_ucg_file_ctx_t*)octx;
    return UCS_ERR_NOT_IMPLEMENTED;
}

UCS_PMODULE_COMPONENT_DEFINE(ucf_over_ucg_component, "over_ucg",
                             sizeof(ucf_over_ucg_ctx_t),
                             sizeof(ucf_over_ucg_file_ctx_t),
                             ucf_over_ucg_query, ucf_over_ucg_init,
                             ucf_over_ucg_finalize, ucf_over_ucg_create,
                             ucf_over_ucg_destroy, ucf_over_ucg_plan,
                             ucf_over_ucg_io_create, ucf_over_ucg_io_trigger,
                             ucf_over_ucg_io_progress, ucf_over_ucg_io_discard,
                             ucf_over_ucg_print, ucf_over_ucg_handle_fault,
                             ucf_over_ucg_config_table, ucf_over_ucg_config_t,
                             "BUILTIN_", UCS_PMODULE_COMPONENT_LIST(f));

ucs_status_t ucf_over_ucg_query(ucs_pmodule_desc_t *descs, unsigned *desc_cnt_p)
{
    /* Return a simple description of the "Builtin" module */
    return ucs_pmodule_component_single(&ucf_over_ucg_component, descs,
                                        desc_cnt_p);
}
