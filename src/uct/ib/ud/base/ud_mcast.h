/**
* Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2020. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/


#ifndef UCT_UD_MCAST_H
#define UCT_UD_MCAST_H

#include <uct/base/uct_worker.h>
#include <uct/ib/base/ib_device.h>
#include <uct/ib/base/ib_iface.h>
#include <ucs/datastruct/sglib_wrapper.h>
#include <ucs/datastruct/ptr_array.h>
#include <ucs/datastruct/sglib.h>
#include <ucs/datastruct/list.h>
#include <ucs/datastruct/arbiter.h>
#include <ucs/async/async.h>
#include <ucs/time/timer_wheel.h>
#include <ucs/sys/compiler_def.h>
#include <ucs/sys/sock.h>

#include "ud_def.h"
#include "ud_ep.h"
#include "ud_iface_common.h"

BEGIN_C_DECLS

/** @file ud_mcast.h */

typedef struct uct_ud_mcast_ctx {
        int a;

        UCS_STATS_NODE_DECLARE(stats)
} uct_ud_mcast_ctx_t;

ucs_status_t
uct_ud_mcast_init_collective_check(uct_worker_h *worker_p,
                                   const uct_iface_params_t *params,
                                   uct_iface_ops_t *uct_mm_bcast_iface_ops);

ucs_status_t
uct_ud_mcast_group_init(uct_ud_mcast_ctx_t *mcast, ucs_stats_node_t *parent);

void uct_ud_mcast_group_cleanup(uct_ud_mcast_ctx_t *mcast);

END_C_DECLS

#endif
