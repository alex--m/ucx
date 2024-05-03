/**
* Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2021. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCS_DEBUG_H_
#define UCS_DEBUG_H_

#include <ucs/sys/compiler_def.h>

BEGIN_C_DECLS

/**
 * Disable signal handling in UCS for signal.
 * Previous signal handler is set.
 * @param signum   Signal number to disable handling.
 */
void ucs_debug_disable_signal(int signum);

/**
 * Retrieve a custom context corresponding to the stack-trace of this call.
 * For example, every time this function is called from withing foo() it will
 * return one context, and every time called from bar() it will return another.
 * When first called with a new stack trace - it will store the context passed
 * as an argument as the permanent context for that trace for future invocations
 * and will also return it.
 *
 * @param new_ctx        Signal number to check.
 *
 * @return The context passed during the first call with the same stack trace.
 */
void* ucs_debug_get_ctx_by_trace(void *new_ctx);


/**
 * Cleanup function, in case @ref ucs_debug_get_ctx_by_trace was ever called.
 */
void ucs_debug_clear_traces(void);

END_C_DECLS

#endif
