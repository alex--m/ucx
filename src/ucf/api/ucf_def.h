/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCF_DEF_H_
#define UCF_DEF_H_

#include <ucs/type/status.h>
#include <ucs/config/types.h>
#include <stddef.h>
#include <stdint.h>


/**
 * @ingroup UCF_CONTEXT
 * @brief UCF Application Context
 *
 * UCF application context (or just a context) is an opaque handle that holds a
 * UCF communication instance's global information.  It represents a single UCF
 * communication instance.  The communication instance could be an OS process
 * (an application) that uses UCP library.  This global information includes
 * communication resources, endpoints, memory, temporary file storage, and
 * other communication information directly associated with a specific UCF
 * instance.  The context also acts as an isolation mechanism, allowing
 * resources associated with the context to manage multiple concurrent
 * communication instances. For example, users using both MPI and OpenSHMEM
 * sessions simultaneously can isolate their communication by allocating and
 * using separate contexts for each of them. Alternatively, users can share the
 * communication resources (memory, network resource context, etc.) between
 * them by using the same application context. A message sent or a RMA
 * operation performed in one application context cannot be received in any
 * other application context.
 */
typedef struct ucf_context               *ucf_context_h;


/**
 * @ingroup UCF_CONTEXT
 * @brief UCF configuration descriptor
 *
 * This descriptor defines the configuration for @ref ucf_context_h
 * "UCF application context". The configuration is loaded from the run-time
 * environment (using configuration files of environment variables)
 * using @ref ucf_config_read "ucf_config_read" routine and can be printed
 * using @ref ucf_config_print "ucf_config_print" routine. In addition,
 * application is responsible to release the descriptor using
 * @ref ucf_config_release "ucf_config_release" routine.
 *
 * @todo This structure will be modified through a dedicated function.
 */
typedef struct ucf_config                ucf_config_t;


 /**
  * @ingroup UCF_FILE
  * @brief UCF File
  *
  * UCF file is an opaque object representing a set of connected remote workers.
  * This object is used for I/O operations - like the ones defined by the
  * Message Passing Interface (MPI). Groups are created with respect to a local
  * worker, and share its endpoints for communication with the remote workers.
  */
typedef struct ucf_file                  *ucf_file_h;


/**
 * @ingroup UCF_FILE
 * @brief UCF listen handle.
 *
 * The listener handle is an opaque object that is used for listening on a
 * specific address and accepting connections from clients to join a group.
 */
typedef struct ucf_listener              *ucf_listener_h;


 /**
  * @ingroup UCF_FILE
  * @brief UCF I/O operation handle
  *
  * UCF I/O is an opaque object representing a description of a file read/write
  * operation. Much like in object-oriented paradigms, a collective is like a
  * "class" which can be instantiated - an instance would be a UCF request to
  * perform this I/O operation once. The description holds all the
  * necessary information to perform this read/write, so re-starting an
  * operation requires no additional parameters.
  */
typedef void                            *ucf_io_h;

/**
 * @ingroup UCF_FILE
 * @brief Completion callback for non-blocking I/O operations.
 *
 * This callback routine is invoked whenever the @ref ucf_io
 * "I/O operation" is completed. It is important to note that the call-back is
 * only invoked in a case when the operation cannot be completed in place.
 *
 * @param [in]  request   The completed collective operation request.
 * @param [in]  status    Completion status. If the send operation was completed
 *                        successfully UCX_OK is returned. If send operation was
 *                        canceled UCS_ERR_CANCELED is returned.
 *                        Otherwise, an @ref ucs_status_t "error status" is
 *                        returned.
 */
typedef void (*ucf_io_callback_t)(void *request, ucs_status_t status);


/**
 * @ingroup UCF_FILE
 * @brief Progress a specific I/O operation request.
 *
 * This routine would explicitly progress an I/O operation request.
 *
 * @param [in]  io      The I/O operation to be progressed.
 *
 * @return Non-zero if any communication was progressed, zero otherwise.
 *
 */
typedef unsigned (*ucf_io_progress_t)(ucf_io_h io);


#endif
