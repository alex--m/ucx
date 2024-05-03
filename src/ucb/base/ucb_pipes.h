/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCB_PIPES_H_
#define UCB_PIPES_H_

#include <ucs/pmodule/target.h>
#include <ucb/api/ucb.h>

typedef struct ucb_pipes {
    ucs_pmodule_target_t  super;
    ucb_batch_id_t        next_batch_id;
    ucp_worker_h          worker;       /**< for conn. est. and progress calls */
    ucs_list_link_t       list;         /**< worker's group list */
    ucb_pipes_params_t    params;       /**< parameters, for future connections */

    /* Below this point - the private per-planner data is allocated/stored */
} ucb_pipes_t;

const ucb_pipes_params_t* ucb_pipes_get_params(ucb_pipes_h pipes); /* for tests */

#endif /* UCB_PIPES_H_ */
