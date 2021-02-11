/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifndef UCF_H_
#define UCF_H_

#include <ucf/api/ucf_def.h>
#include <ucg/api/ucg.h>
#include <ucs/pmodule/framework.h>

BEGIN_C_DECLS

/**
 * @defgroup UCF_API Unified Communication File-system (UCF) API
 * @{
 * This section describes UCF API.
 * @}
 */


/**
 * @defgroup UCF_CONTEXT UCF Application Context
 * @ingroup UCF_API
 * @{
 * Application context is a primary concept of UCF design which
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
 * @defgroup UCF_FILE UCF File
 * @ingroup UCF_API
 * @{
 * UCF Group routines
 * @}
 */


/**
* @defgroup UCF_COLLECTIVE UCF File operation
* @ingroup UCF_API
* @{
* UCF Collective operations
* @}
*/


/**
 * @ingroup UCF_CONTEXT
 * @brief UCF file parameters field mask.
 *
 * The enumeration allows specifying which fields in @ref ucf_file_params_t are
 * present. It is used to enable backward compatibility support.
 */
enum ucf_params_field {
    UCF_PARAM_FIELD_COMPLETION_CB = UCS_BIT(1), /**< Actions upon completion */
    UCF_PARAM_FIELD_EXTERNAL_FS   = UCS_BIT(2)  /**< External filesystem to use */
};

enum ucf_external_fs_field {
    UCF_EXTERNAL_FS_FIELD_OPEN  = UCS_BIT(0),
    UCF_EXTERNAL_FS_FIELD_READ  = UCS_BIT(1),
    UCF_EXTERNAL_FS_FIELD_WRITE = UCS_BIT(2),
    UCF_EXTERNAL_FS_FIELD_CLOSE = UCS_BIT(3)
};

typedef struct ucf_external_fs {
    uint64_t field_mask; /* @ref enum ucf_external_fs_field */

/*
    typedef ssize_t (*mca_fbtl_base_module_ipreadv_fn_t)
        (struct ompio_file_t *file,
         ucf_request_t *request);
    typedef ssize_t (*mca_fbtl_base_module_ipwritev_fn_t)
        (struct ompio_file_t *file,
         ucf_request_t *request);
*/
    /*
    open
    close
    mca_fbtl_base_module_preadv_fn_t        fbtl_preadv;
    mca_fbtl_base_module_ipreadv_fn_t       fbtl_ipreadv;
    mca_fbtl_base_module_pwritev_fn_t       fbtl_pwritev;
    mca_fbtl_base_module_ipwritev_fn_t      fbtl_ipwritev;
    mca_fbtl_base_module_progress_fn_t      fbtl_progress;
    mca_fbtl_base_module_request_free_fn_t  fbtl_request_free;
     */
} ucf_external_fs_t;

/**
 * @ingroup UCF_CONTEXT
 * @brief Creation parameters for the UCF context.
 *
 * The structure defines the parameters that are used during the UCF
 * initialization by @ref ucf_init .
 */
typedef struct ucf_params {
    const ucs_pmodule_framework_params_t *super;
    const ucg_params_t                   *ucg;

    /**
     * Mask of valid fields in this structure, using bits from
     * @ref ucf_params_field. Fields not specified in this mask will be ignored.
     * Provides ABI compatibility with respect to adding new fields.
     */
    uint64_t field_mask;

    /* Requested action upon completion (for non-blocking calls) */
    struct {
        /* Callback function to invoke upon completion of a collective call */
        void (*coll_comp_cb_f)(void *req, ucs_status_t status);

        /* offset where to set completion (ignored unless coll_comp_cb is NULL) */
        size_t comp_flag_offset;

        /* offset where to write status (ignored unless coll_comp_cb is NULL) */
        size_t comp_status_offset;
    } completion;

    ucf_external_fs_t ext_fs;

} ucf_params_t;

/**
 * @ingroup UCF_FILE
 * @brief UCF file collective operation characteristics.
 *
 * The enumeration allows specifying modifiers to describe the requested
 * collective operation, as part of @ref ucf_collective_params_t
 * passed to @ref ucf_collective_start . For example, for MPI_Reduce:
 *
 * modifiers = UCF_FILE_ACCESS_READ |
 *             UCF_FILE_ACCESS_WRITE;
 *
 * The premise is that (a) any collective type can be described as a combination
 * of the flags below, and (b) the implementation can benefit from applying
 * logic based on these flags. For example, we can check if a collective has
 * a single rank as the source, which will be true for both MPI_Bcast and
 * MPI_Scatterv today, and potentially other types in the future.
 *
 * @note
 * For simplicity, some rarely used collectives were intentionally omitted. For
 * instance, MPI_Scan and MPI_Exscan could be supported using additional flags,
 * which are not part of the API at this time.
 */
enum ucf_file_access_type {
    UCF_FILE_ACCESS_READ   = UCS_BIT(0),
    UCF_FILE_ACCESS_WRITE  = UCS_BIT(1),
    UCF_FILE_ACCESS_APPEND = UCS_BIT(2)
};

/**
 * @ingroup UCF_FILE
 * @brief UCF file parameters field mask.
 *
 * The enumeration allows specifying which fields in @ref ucf_file_params_t are
 * present. It is used to enable backward compatibility support.
 */
enum ucf_file_params_field {
    UCF_FILE_PARAM_FIELD_ACCESS_TYPE = UCS_BIT(0), /**< File access type */
    UCF_FILE_PARAM_FIELD_GROUP       = UCS_BIT(1), /**< Unique identifier */
    UCF_FILE_PARAM_FIELD_GRANULARITY = UCS_BIT(2)  /**< I/O chunk size */
};

/**
 * @ingroup UCF_FILE
 * @brief Creation parameters for the UCF file.
 *
 * The structure defines the parameters that are used during the UCF file
 * @ref ucf_file_create "creation".
 */
typedef struct ucf_file_params {
    /**
     * Mask of valid fields in this structure, using bits from
     * @ref ucf_file_params_field. Fields not specified in this mask will be
     * ignored. Provides ABI compatibility with respect to adding new fields.
     */
    uint64_t field_mask;

    /*
     * Type of access. // TODO: elaborate
     */
    enum ucf_file_access_type access_type;

    /*
     * Group of peers accessing this file. // TODO: elaborate
     */
    ucg_group_h group;

    /*
     * Used if access is always a multiple of this size. // TODO: elaborate
     */
    size_t granularity;

} ucf_file_params_t;

/**
 * @ingroup UCF_FILE
 * @brief UCF file collective operation characteristics.
 *
 * The enumeration ...
 */
typedef enum ucf_io_type {
    UCF_IO_READ,
    UCF_IO_WRITE,
    UCF_IO_APPEND,
    UCF_IO_PREFETCH
} ucf_io_type_t;

/**
 * @ingroup UCF_FILE
 * @brief Creation parameters for the UCF I/O operation.
 *
 * The structure defines the parameters that are used during the UCF collective
 * @ref ucf_collective_create "creation". The size of this structure is critical
 * to performance, as well as it being contiguous, because its entire contents
 * are accessed during run-time.
 */
typedef struct ucf_io_params {
    enum ucf_io_type type;
    void *buffer;
    size_t count;
} UCS_S_PACKED UCS_V_ALIGNED(32) ucf_io_params_t;


/**
 * @ingroup UCF_FILE
 * @brief Create a group object.
 *
 * This routine allocates and initializes a @ref ucf_group_h "group" object.
 * This routine is a "collective operation", meaning it has to be called for
 * each worker participating in the group - before the first call on the group
 * is invoked on any of those workers. The call does not contain a barrier,
 * meaning a call on one worker can complete regardless of call on others.
 *
 * @note The group object is allocated within context of the calling thread
 *
 * @param [in] worker      Worker to create a group on top of.
 * @param [in] params      User defined @ref ucf_file_params_t configurations for the
 *                         @ref ucf_group_h "UCF file".
 * @param [out] group_p    A pointer to the group object allocated by the
 *                         UCF library
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucf_file_create(ucp_worker_h worker,
                             const ucf_file_params_t *params,
                             ucf_file_h *file_p);


/**
 * @ingroup UCF_FILE
 * @brief Destroy a group object.
 *
 * This routine releases the resources associated with a @ref ucf_group_h
 * "UCF file". This routine is also a "collective operation", similarly to
 * @ref ucf_group_create, meaning it must be called on each worker participating
 * in the group.
 *
 * @warning Once the UCF file handle is destroyed, it cannot be used with any
 * UCF routine.
 *
 * The destroy process releases and shuts down all resources associated with
 * the @ref ucf_group_h "group".
 *
 * @param [in]  group       Group object to destroy.
 */
void ucf_file_destroy(ucf_file_h file);





struct params {
    size_t chunk_size; // e.g. 32-MB writes only
};


ucs_status_t ucf_fkey_pack(); // pack a file handle for sharing

ucs_status_t ucf_fkey_unpack(); // unpack a shared file handle


/**
 * @ingroup UCF_COLLECTIVE
 * @brief Creates a collective operation on a group object.
 * The parameters are intentionally non-constant, to allow UCF to write-back some
 * information and avoid redundant actions on the next call. For example, memory
 * registration handles are written back to the parameters pointer passed to the
 * function, and are re-used in subsequent calls.
 *
 * @param [in]  group        Group of participants in this collective operation.
 * @param [in]  params       File operation parameters.
 * @param [in]  iop          File operation handle.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucf_io_create(ucf_file_h file,
                           const ucf_io_params_t *params,
                           ucf_io_h *iop_p);


/**
 * @ingroup UCF_COLLECTIVE
 * @brief Starts a collective operation.
 *
 * @param [in]  iop          File operation handle.
 * @param [in]  req          Request handle, allocated by the user.
 *
 * @return UCS_OK           - The collective operation was completed immediately.
 * @return UCS_INPROGRESS   - The collective was not completed and is in progress.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucf_io_start(ucf_io_h iop, void *req);


/**
 * @ingroup UCF_COLLECTIVE
 * @brief Obtain the progress function to be applied to a file operation.
 *
 * This routine returns a pointer to another one. The latter would explicitly
 * progresses a single file operation request.
 *
 * @param [in]  iop          File operation handle.
 *
 * @return A valid function pointer.
 */
ucf_io_progress_t ucf_request_get_progress(ucf_io_h iop);


/**
 * @ingroup UCF_COLLECTIVE
 * @brief Cancel an outstanding file operation request.
 *
 * @param [in]  iop          File operation handle.
 *
 * This routine tries to cancels an outstanding collective request.  After
 * calling this routine, the @a request will be in completed or canceled (but
 * not both) state regardless of the status of the target endpoint associated
 * with the collective request. If the request is completed successfully,
 * the @ref ucf_collective_callback_t completion callback will be
 * called with the @a status argument of the callback set to UCS_OK, and in a
 * case it is canceled the @a status argument is set to UCS_ERR_CANCELED.
 */
void ucf_request_cancel(ucf_io_h iop);


/**
 * @ingroup UCF_COLLECTIVE
 * @brief Destroys a file operation handle.
 *
 * @param [in]  iop          File operation handle.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
void ucf_io_destroy(ucf_io_h iop);


/**
 * @ingroup UCF_CONTEXT
 * @brief Read UCF configuration descriptor
 *
 * The routine fetches the information about UCF library configuration from
 * the run-time environment. Then, the fetched descriptor is used for
 * UCF library @ref ucf_init "initialization". The Application can print out the
 * descriptor using @ref ucf_config_print "print" routine. In addition
 * the application is responsible for @ref ucf_config_release "releasing" the
 * descriptor back to the UCF library.
 *
 * @param [in]  env_prefix    If non-NULL, the routine searches for the
 *                            environment variables that start with
 *                            @e \<env_prefix\>_UCX_ prefix.
 *                            Otherwise, the routine searches for the
 *                            environment variables that start with
 *                            @e UCX_ prefix.
 * @param [in]  filename      If non-NULL, read configuration from the file
 *                            defined by @e filename. If the file does not
 *                            exist, it will be ignored and no error reported
 *                            to the application.
 * @param [out] config_p      Pointer to configuration descriptor as defined by
 *                            @ref ucf_config_t "ucf_config_t".
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucf_config_read(const char *env_prefix, const char *filename,
                             ucf_config_t **config_p);


/**
 * @ingroup UCF_CONTEXT
 * @brief Release configuration descriptor
 *
 * The routine releases the configuration descriptor that was allocated through
 * @ref ucf_config_read "ucf_config_read()" routine.
 *
 * @param [out] config        Configuration descriptor as defined by
 *                            @ref ucf_config_t "ucf_config_t".
 */
void ucf_config_release(ucf_config_t *config);


/**
 * @ingroup UCF_CONTEXT
 * @brief Modify context configuration.
 *
 * The routine changes one configuration setting stored in @ref ucf_config_t
 * "configuration" descriptor.
 *
 * @param [in]  config        Configuration to modify.
 * @param [in]  name          Configuration variable name.
 * @param [in]  value         Value to set.
 *
 * @return Error code.
 */
ucs_status_t ucf_config_modify(ucf_config_t *config, const char *name,
                               const char *value);


/**
 * @ingroup UCF_CONTEXT
 * @brief Print configuration information
 *
 * The routine prints the configuration information that is stored in
 * @ref ucf_config_t "configuration" descriptor.
 *
 * @todo Expose ucs_config_print_flags_t
 *
 * @param [in]  config        @ref ucf_config_t "Configuration descriptor"
 *                            to print.
 * @param [in]  stream        Output stream to print the configuration to.
 * @param [in]  title         Configuration title to print.
 * @param [in]  print_flags   Flags that control various printing options.
 */
void ucf_config_print(const ucf_config_t *config, FILE *stream,
                      const char *title, ucs_config_print_flags_t print_flags);


/** @cond PRIVATE_INTERFACE */
/**
 * @ingroup UCF_CONTEXT
 * @brief UCF context initialization with particular API version.
 *
 * This is an internal routine used to check compatibility with a particular
 * API version. @ref ucf_init should be used to create UCF context.
 */
ucs_status_t ucf_init_version(unsigned ucf_api_major_version,
                              unsigned ucf_api_minor_version,
                              unsigned ucg_api_major_version,
                              unsigned ucg_api_minor_version,
                              unsigned ucp_api_major_version,
                              unsigned ucp_api_minor_version,
                              const ucf_params_t *params,
                              const ucf_config_t *config,
                              ucf_context_h *context_p);
/** @endcond */


/**
 * @ingroup UCF_CONTEXT
 * @brief UCF context initialization.
 *
 * This routine creates and initializes a @ref ucf_context_h
 * "UCF application context".
 *
 * @warning This routine must be called before any other UCF function
 * call in the application.
 *
 * This routine checks API version compatibility, then discovers the available
 * network interfaces, and initializes the network resources required for
 * discovering of the network and memory related devices.
 *  This routine is responsible for initialization all information required for
 * a particular application scope, for example, MPI application, OpenSHMEM
 * application, etc.
 *
 * @param [in]  config        UCF configuration descriptor allocated through
 *                            @ref ucf_config_read "ucf_config_read()" routine.
 * @param [in]  params        User defined @ref ucf_params_t configurations for the
 *                            @ref ucf_context_h "UCF application context".
 * @param [out] context_p     Initialized @ref ucf_context_h
 *                            "UCF application context".
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucf_init(const ucf_params_t *params,
                      const ucf_config_t *config,
                      ucf_context_h *context_p);


/**
 * @ingroup UCF_CONTEXT
 * @brief Release UCF application context.
 *
 * This routine finalizes and releases the resources associated with a
 * @ref ucf_context_h "UCF application context".
 *
 * @warning An application cannot call any UCF routine
 * once the UCF application context released.
 *
 * The cleanup process releases and shuts down all resources associated with
 * the application context. After calling this routine, calling any UCF
 * routine without calling @ref ucf_init "UCF initialization routine" is invalid.
 *
 * @param [in] context_p   Handle to @ref ucf_context_h
 *                         "UCF application context".
 */
void ucf_cleanup(ucf_context_h context_p);


/**
 * @ingroup UCF_CONTEXT
 * @brief Print context information.
 *
 * This routine prints information about the context configuration: including
 * memory domains, transport resources, and other useful information associated
 * with the context.
 *
 * @param [in] context      Print this context object's configuration.
 * @param [in] stream       Output stream on which to print the information.
 */
void ucf_context_print_info(const ucf_context_h context, FILE *stream);


/**
 * @ingroup UCF_CONTEXT
 * @brief Gain access to the internal UCF context within a UCF context.
 *
 * This routine allows applications using UCF to call UCF APIs as well.
 *
 * @param [in] context Handle to @ref ucf_context_h "UCF application context".
 *
 * @return Handle to an internal @ref ucp_context_h "UCF application context".
 */
ucg_context_h ucf_context_get_ucg(ucf_context_h context);


/**
 * @ingroup UCF_CONTEXT
 * @brief Get UCF library version.
 *
 * This routine returns the UCF library version.
 *
 * @param [out] major_version       Filled with library major version.
 * @param [out] minor_version       Filled with library minor version.
 * @param [out] release_number      Filled with library release number.
 */
void ucf_get_version(unsigned *major_version, unsigned *minor_version,
                     unsigned *release_number);


/**
 * @ingroup UCF_CONTEXT
 * @brief Get UCF library version as a string.
 *
 * This routine returns the UCF library version as a string which consists of:
 * "major.minor.release".
 */
const char *ucf_get_version_string(void);


END_C_DECLS

#endif
