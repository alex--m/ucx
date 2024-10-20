/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 *
 * See worker LICENSE for terms.
 */

#ifndef UCB_H_
#define UCB_H_

#include <ucb/api/ucb_def.h>
#include <ucb/api/ucb_version.h>
#include <uct/api/uct_def.h>
#include <ucp/api/ucp.h>

BEGIN_C_DECLS

/**
 * @defgroup UCB_API Unified Communication Batches (UCB) API
 * @{
 * This section describes UCB API.
 * @}
 */


/**
 * @defgroup UCB_CONTEXT UCB Application Context
 * @ingroup UCB_API
 * @{
 * Application context is a primary concept of UCB design which
 * provides an isolation mechanism, allowing resources associated
 * with the context to separate or share network communication context
 * across multiple instances of applications.
 *
 * This section provides a detailed description of this concept and
 * routines associated with it.
 *
 * @}
 */


 /**
 * @defgroup UCB_WORKER UCB File
 * @ingroup UCB_API
 * @{
 * UCB Group routines
 * @}
 */


/**
* @defgroup UCB_BATCH UCB File operation
* @ingroup UCB_API
* @{
* UCB Collective operations
* @}
*/


/**
 * @ingroup UCB_CONTEXT
 * @brief UCB worker parameters field mask.
 *
 * The enumeration allows specifying which fields in @ref ucb_pipes_params_t are
 * present. It is used to enable backward compatibility support.
 */
enum ucb_params_field {
    UCB_PARAM_FIELD_NAME              = UCS_BIT(0), /**< Name for this context */
    UCB_PARAM_FIELD_CONTEXT_HEADROOM  = UCS_BIT(1), /**< Extra space in context */
    UCB_PARAM_FIELD_COMPLETION_CB     = UCS_BIT(2)  /**< Actions upon completion */
};

/**
 * @ingroup UCB_CONTEXT
 * @brief Creation parameters for the UCB context.
 *
 * The structure defines the parameters that are used during the UCB
 * initialization by @ref ucb_init .
 */
typedef struct ucb_params {
    const ucp_params_t *super; /** Pointer is better suited for ABI compatibility */

    /**
     * Mask of valid fields in this structure, using bits from
     * @ref ucb_params_field. Fields not specified in this mask will be ignored.
     * Provides ABI compatibility with respect to adding new fields.
     */
    uint64_t     field_mask;

    /**
     * Tracing and analysis tools can identify the context using this name.
     * To retrieve the context's name, use @ref ucg_context_query, as the name
     * you supply may be changed by UCX under some circumstances, e.g. a name
     * conflict. This field is only assigned if you set
     * @ref UCG_PARAM_FIELD_NAME in the field mask. If not, then a default
     * unique name will be created for you.
     */
    const char *name;

    /** Head-room before the allocated context pointer, for extensions */
    size_t context_headroom;

    /* Requested action upon completion (for non-blocking calls) */
    struct {
        /* Callback function to call upon immediate completion (within call) */
        void (*blocking_comp_cb_f)(void *req, ucs_status_t status);

        /* Callback function to call upon async. completion (during progress) */
        void (*nonblocking_comp_cb_f)(void *req, ucs_status_t status);

        /* Callback function to call upon async. completion of persistent ops */
        void (*persistent_nb_comp_cb_f)(void *req, ucs_status_t status);
    } completion;
} ucb_params_t;

/**
 * @ingroup UCB_WORKER
 * @brief UCB worker parameters field mask.
 *
 * The enumeration allows specifying which fields in @ref ucb_pipes_params_t are
 * present. It is used to enable backward compatibility support.
 */
enum ucb_pipes_params_field {
    UCB_PIPES_PARAM_FIELD_NAME       = UCS_BIT(0), /**< Pipes name */
    UCB_PIPES_PARAM_FIELD_UCP_WORKER = UCS_BIT(1), /**< UCP Worker object */
    UCB_PIPES_PARAM_FIELD_CD_MASTER  = UCS_BIT(2)
};

/**
 * @ingroup UCB_WORKER
 * @brief Creation parameters for the UCB worker.
 *
 * The structure defines the parameters that are used during the UCB worker
 * @ref ucb_pipes_create "creation".
 */
typedef struct ucb_pipes_params {
    /**
     * Mask of valid fields in this structure, using bits from
     * @ref ucb_pipes_params_field. Fields not specified in this mask will be
     * ignored. Provides ABI compatibility with respect to adding new fields.
     */
    uint64_t field_mask;

    /**
     * Tracing and analysis tools can identify the pipes using this name. To
     * retrieve the group's name, use @ref ucg_pipes_query, as the name you
     * supply may be changed by UCX under some circumstances, e.g. a name
     * conflict. This field is only assigned if you set
     * @ref UCB_PIPES_PARAM_FIELD_NAME in the field mask. If not, then a
     * default unique name will be created for you.
     */
    const char *name;

    ucp_worker_h worker;

    /**
     * TODO: document, and hopefully make more generic
     */
    uct_ep_h *cd_master_ep_p;
} ucb_pipes_params_t;

/**
 * @ingroup UCB_WORKER
 * @brief Creation parameters for the UCB batch operation.
 *
 * The structure defines the parameters that are used during the UCB batch
 * @ref ucb_batch_create "creation". The size of this structure is critical
 * to performance, as well as it being contiguous, because its entire contents
 * are accessed during run-time.
 */
typedef struct ucb_batch_params {
    ucb_batch_id_t       id;
    ucb_batch_callback_t completion_cb;
    ucb_batch_recorder_t recorder_cb;
    void                *recorder_arg;
} UCS_S_PACKED UCS_V_ALIGNED(32) ucb_batch_params_t;


/**
 * @ingroup UCB_WORKER
 * @brief Create a worker object.
 *
 * This routine allocates and initializes a @ref ucb_group_h "group" object.
 * This routine is a "batch operation", meaning it has to be called for
 * each worker participating in the worker - before the first call on the worker
 * is invoked on any of those workers. The call does not contain a barrier,
 * meaning a call on one worker can complete regardless of call on others.
 *
 * @note The worker object is allocated within context of the calling thread
 *
 * @param [in]  params      User defined @ref ucb_pipes_params_t configurations
 *                          for the @ref ucb_pipes_h "UCB pipes".
 * @param [out] pipes_p     A pointer to the worker object allocated by the
 *                          UCB library
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucb_pipes_create(const ucb_pipes_params_t *params,
                              ucb_pipes_h *pipes_p);


/**
 * @ingroup UCB_WORKER
 * @brief Destroy a worker object.
 *
 * This routine releases the resources associated with a @ref ucb_group_h
 * "UCB worker". This routine is also a "batch operation", similarly to
 * @ref ucb_group_create, meaning it must be called on each worker participating
 * in the worker.
 *
 * @warning Once the UCB worker handle is destroyed, it cannot be used with any
 * UCB routine.
 *
 * The destroy process releases and shuts down all resources associated with
 * the @ref ucb_group_h "group".
 *
 * @param [in]  pipes       Pipes object to destroy.
 */
void ucb_pipes_destroy(ucb_pipes_h pipes);


/**
 * @ingroup UCB_BATCH
 * @brief Creates a batch operation on the given pipes.
 * The parameters are intentionally non-constant, to allow UCB to write-back some
 * information and avoid redundant actions on the next call. For example, memory
 * registration handles are written back to the parameters pointer passed to the
 * function, and are re-used in subsequent calls.
 *
 * @param [in]  pipes        Pipes to apply this batch operation to.
 * @param [in]  params       File operation parameters.
 * @param [in]  batch        File operation handle.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucb_batch_create(ucb_pipes_h pipes,
                              const ucb_batch_params_t *params,
                              ucb_batch_h *batch_p);


/**
 * @ingroup UCB_BATCH
 * @brief Starts a batch operation.
 *
 * @param [in]  batch        Batched operation handle.
 * @param [in]  req          Request handle, allocated by the user.
 * @param [out] progress_f_p Function to use for progress/completion polling.
 *
 * @return UCS_OK           - The batch operation was completed immediately.
 * @return UCS_PTR_IS_ERR(_ptr) - The batch operation failed and an error
 *                                code indicates the status.
 * @return otherwise        - The batch operation has started, and can be
 *                            completed at any point in time. A request handle
 *                            is returned to the application in order to track
 *                            progress of the operation: either through the
 *                            worker-wide @ref ucp_worker_progress or through
 *                            the progress function returned (the latter is only
 *                            recommended for blocking calls). No need to close
 *                            the returned handle - it'll become useless once
 *                            the batch is complete (further use of it in
 *                            progress calls will be useless, yet harmless).
 */
ucs_status_ptr_t ucb_batch_start(ucb_batch_h batch, void *req,
                                 ucb_batch_progress_t *progress_f_p);


/**
 * @ingroup UCB_BATCH
 * @brief Obtain the progress function to be applied to a worker operation.
 *
 * This routine returns a pointer to another one. The latter would explicitly
 * progresses a single worker operation request.
 *
 * @param [in]  batch          File operation handle.
 *
 * @return A valid function pointer.
 */
ucb_batch_progress_t ucb_request_get_progress(ucb_batch_h batch);


/**
 * @ingroup UCB_BATCH
 * @brief Cancel an outstanding worker operation request.
 *
 * @param [in]  batch          File operation handle.
 *
 * This routine tries to cancels an outstanding batch request.  After
 * calling this routine, the @a request will be in completed or canceled (but
 * not both) state regardless of the status of the target endpoint associated
 * with the batch request. If the request is completed successfully,
 * the @ref ucb_batch_callback_t completion callback will be
 * called with the @a status argument of the callback set to UCS_OK, and in a
 * case it is canceled the @a status argument is set to UCS_ERR_CANCELED.
 */
void ucb_request_cancel(ucb_batch_h batch, void *req);


/**
 * @ingroup UCB_BATCH
 * @brief Destroys a worker operation handle.
 *
 * @param [in]  batch          File operation handle.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
void ucb_batch_destroy(ucb_batch_h batch);


/**
 * @ingroup UCB_CONTEXT
 * @brief Read UCB configuration descriptor
 *
 * The routine fetches the information about UCB library configuration from
 * the run-time environment. Then, the fetched descriptor is used for
 * UCB library @ref ucb_init "initialization". The Application can print out the
 * descriptor using @ref ucb_config_print "print" routine. In addition
 * the application is responsible for @ref ucb_config_release "releasing" the
 * descriptor back to the UCB library.
 *
 * @param [in]  env_prefix    If non-NULL, the routine searches for the
 *                            environment variables that start with
 *                            @e \<env_prefix\>_UCX_ prefix.
 *                            Otherwise, the routine searches for the
 *                            environment variables that start with
 *                            @e UCX_ prefix.
 * @param [in]  worker_name   If non-NULL, read configuration from the worker
 *                            defined by @e workername. If the worker does not
 *                            exist, it will be ignored and no error reported
 *                            to the application.
 * @param [out] config_p      Pointer to configuration descriptor as defined by
 *                            @ref ucb_config_t "ucb_config_t".
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucb_config_read(const char *env_prefix, const char *worker_name,
                             ucb_config_t **config_p);


/**
 * @ingroup UCB_CONTEXT
 * @brief Release configuration descriptor
 *
 * The routine releases the configuration descriptor that was allocated through
 * @ref ucb_config_read "ucb_config_read()" routine.
 *
 * @param [out] config        Configuration descriptor as defined by
 *                            @ref ucb_config_t "ucb_config_t".
 */
void ucb_config_release(ucb_config_t *config);


/**
 * @ingroup UCB_CONTEXT
 * @brief Modify context configuration.
 *
 * The routine changes one configuration setting stored in @ref ucb_config_t
 * "configuration" descriptor.
 *
 * @param [in]  config        Configuration to modify.
 * @param [in]  name          Configuration variable name.
 * @param [in]  value         Value to set.
 *
 * @return Error code.
 */
ucs_status_t ucb_config_modify(ucb_config_t *config, const char *name,
                               const char *value);


/**
 * @ingroup UCB_CONTEXT
 * @brief Print configuration information
 *
 * The routine prints the configuration information that is stored in
 * @ref ucb_config_t "configuration" descriptor.
 *
 * @todo Expose ucs_config_print_flags_t
 *
 * @param [in]  config        @ref ucb_config_t "Configuration descriptor"
 *                            to print.
 * @param [in]  stream        Output stream to print the configuration to.
 * @param [in]  title         Configuration title to print.
 * @param [in]  print_flags   Flags that control various printing options.
 */
void ucb_config_print(const ucb_config_t *config, FILE *stream,
                      const char *title, ucs_config_print_flags_t print_flags);


/** @cond PRIVATE_INTERFACE */
/**
 * @ingroup UCB_CONTEXT
 * @brief UCB context initialization with particular API version.
 *
 * This is an internal routine used to check compatibility with a particular
 * API version. @ref ucb_init should be used to create UCB context.
 */
ucs_status_t ucb_init_version(unsigned ucb_api_major_version,
                              unsigned ucb_api_minor_version,
                              const ucb_params_t *params,
                              const ucb_config_t *config,
                              ucb_context_h *context_p);
/** @endcond */


/**
 * @ingroup UCB_CONTEXT
 * @brief UCB context initialization.
 *
 * This routine creates and initializes a @ref ucb_context_h
 * "UCB application context".
 *
 * @warning This routine must be called before any other UCB function
 * call in the application.
 *
 * This routine checks API version compatibility, then discovers the available
 * network interfaces, and initializes the network resources required for
 * discovering of the network and memory related devices.
 *  This routine is responsible for initialization all information required for
 * a particular application scope, for example, MPI application, OpenSHMEM
 * application, etc.
 *
 * @param [in]  config        UCB configuration descriptor allocated through
 *                            @ref ucb_config_read "ucb_config_read()" routine.
 * @param [in]  params        User defined @ref ucb_params_t configurations for the
 *                            @ref ucb_context_h "UCB application context".
 * @param [out] context_p     Initialized @ref ucb_context_h
 *                            "UCB application context".
 *
 * @return Error code as defined by @ref ucs_status_t
 */
static inline ucs_status_t ucb_init(const ucb_params_t *params,
                                    const ucb_config_t *config,
                                    ucb_context_h *context_p)
{
    return ucb_init_version(UCB_API_MAJOR, UCB_API_MINOR, params, config,
                            context_p);
}


/**
 * @ingroup UCB_CONTEXT
 * @brief Release UCB application context.
 *
 * This routine finalizes and releases the resources associated with a
 * @ref ucb_context_h "UCB application context".
 *
 * @warning An application cannot call any UCB routine
 * once the UCB application context released.
 *
 * The cleanup process releases and shuts down all resources associated with
 * the application context. After calling this routine, calling any UCB
 * routine without calling @ref ucb_init "UCB initialization routine" is invalid.
 *
 * @param [in] context   Handle to @ref ucb_context_h "UCB application context".
 */
void ucb_cleanup(ucb_context_h context);


/**
 * @ingroup UCB_CONTEXT
 * @brief Print context information.
 *
 * This routine prints information about the context configuration: including
 * memory domains, transport resources, and other useful information associated
 * with the context.
 *
 * @param [in] context      Print this context object's configuration.
 * @param [in] stream       Output stream on which to print the information.
 */
void ucb_context_print_info(const ucb_context_h context, FILE *stream);


/**
 * @ingroup UCB_CONTEXT
 * @brief Gain access to the internal UCP context within a UCB context.
 *
 * This routine allows applications using UCB to call UCP APIs as well.
 *
 * @param [in] context Handle to @ref ucb_context_h "UCB application context".
 *
 * @return Handle to an internal @ref ucp_context_h "UCP application context".
 */
ucp_context_h ucb_context_get_ucp(ucb_context_h context);


/**
 * @ingroup UCB_CONTEXT
 * @brief Get UCB library version.
 *
 * This routine returns the UCB library version.
 *
 * @param [out] major_version       Filled with library major version.
 * @param [out] minor_version       Filled with library minor version.
 * @param [out] release_number      Filled with library release number.
 */
void ucb_get_version(unsigned *major_version, unsigned *minor_version,
                     unsigned *release_number);


/**
 * @ingroup UCB_CONTEXT
 * @brief Get UCB library version as a string.
 *
 * This routine returns the UCB library version as a string which consists of:
 * "major.minor.release".
 */
const char *ucb_get_version_string(void);


END_C_DECLS

#endif
