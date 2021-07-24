/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See worker LICENSE for terms.
 */

#ifndef UCB_DEF_H_
#define UCB_DEF_H_

#include <ucs/type/status.h>
#include <ucs/config/types.h>
#include <stddef.h>
#include <stdint.h>


/**
 * @ingroup UCB_CONTEXT
 * @brief UCB Application Context
 *
 * UCB application context (or just a context) is an opaque handle that holds a
 * UCB communication instance's global information.  It represents a single UCB
 * communication instance.  The communication instance could be an OS process
 * (an application) that uses UCP library.  This global information includes
 * communication resources, endpoints, memory, temporary worker storage, and
 * other communication information directly associated with a specific UCB
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
typedef struct ucb_context               *ucb_context_h;


/**
 * @ingroup UCB_CONTEXT
 * @brief UCB configuration descriptor
 *
 * This descriptor defines the configuration for @ref ucb_context_h
 * "UCB application context". The configuration is loaded from the run-time
 * environment (using configuration workers of environment variables)
 * using @ref ucb_config_read "ucb_config_read" routine and can be printed
 * using @ref ucb_config_print "ucb_config_print" routine. In addition,
 * application is responsible to release the descriptor using
 * @ref ucb_config_release "ucb_config_release" routine.
 *
 * @todo This structure will be modified through a dedicated function.
 */
typedef struct ucb_config                ucb_config_t;


/**
 * @ingroup UCB_WORKER
 * @brief UCB File
 *
 * UCB worker is an opaque object representing a set of connected remote workers.
 * This object is used for batch operations - like the ones defined by the
 * Message Passing Interface (MPI). Groups are created with respect to a local
 * worker, and share its endpoints for communication with the remote workers.
 */
typedef struct ucb_worker                  *ucb_worker_h;


/**
 * @ingroup UCB_WORKER
 * @brief UCB batch operation handle
 *
 * UCB batch is an opaque object representing a description of a worker read/write
 * operation. Much like in object-oriented paradigms, a collective is like a
 * "class" which can be instantiated - an instance would be a UCB request to
 * perform this batch operation once. The description holds all the
 * necessary information to perform this read/write, so re-starting an
 * operation requires no additional parameters.
 */
typedef void                            *ucb_batch_h;


/**
 * @ingroup UCB_WORKER
 * @brief UCB batch operation identifier
 */
typedef uint64_t                         ucb_batch_id_t;

/**
 * @ingroup UCB_WORKER
 * @brief Completion callback for non-blocking batch operations.
 *
 * This callback routine is invoked whenever the @ref ucb_batch
 * "batch operation" is completed. It is important to note that the call-back is
 * only invoked in a case when the operation cannot be completed in place.
 *
 * @param [in]  request   The completed collective operation request.
 * @param [in]  status    Completion status. If the send operation was completed
 *                        successfully UCX_OK is returned. If send operation was
 *                        canceled UCS_ERR_CANCELED is returned.
 *                        Otherwise, an @ref ucs_status_t "error status" is
 *                        returned.
 */
typedef void (*ucb_batch_recorder_t)(ucb_batch_id_t id, void *arg);


/**
 * @ingroup UCB_WORKER
 * @brief Completion callback for non-blocking batch operations.
 *
 * This callback routine is invoked whenever the @ref ucb_batch
 * "batch operation" is completed. It is important to note that the call-back is
 * only invoked in a case when the operation cannot be completed in place.
 *
 * @param [in]  request   The completed collective operation request.
 * @param [in]  status    Completion status. If the send operation was completed
 *                        successfully UCX_OK is returned. If send operation was
 *                        canceled UCS_ERR_CANCELED is returned.
 *                        Otherwise, an @ref ucs_status_t "error status" is
 *                        returned.
 */
typedef void (*ucb_batch_callback_t)(void *request, ucs_status_t status);


/**
 * @ingroup UCB_WORKER
 * @brief Progress a specific batch operation request.
 *
 * This routine would explicitly progress an batch operation request.
 *
 * @param [in]  batch      The batch operation to be progressed.
 *
 * @return Non-zero if any communication was progressed, zero otherwise.
 *
 */
typedef unsigned (*ucb_batch_progress_t)(ucb_batch_h batch);


#endif
