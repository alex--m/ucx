/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCF_FILE_H_
#define UCF_FILE_H_

#include <ucf/api/ucf.h>
#include <ucg/base/ucg_group.h>

typedef struct ucf_file {
    ucs_pmodule_target_t super;
    ucp_worker_h         worker;       /**< for conn. est. and progress calls */
    ucf_file_params_t    params;      /**< parameters, for future connections */

    /* Below this point - the private per-planner data is allocated/stored */
} ucf_file_t;

const ucf_file_params_t* ucf_file_get_params(ucf_file_h file); /* for tests */

#endif /* UCF_FILE_H_ */
