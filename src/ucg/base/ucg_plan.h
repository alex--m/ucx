/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_PLAN_H_
#define UCG_PLAN_H_

#include <ucp/core/ucp_types.h>

#include "../api/ucg_plan_component.h"

size_t ucg_plan_get_context_size(ucg_plan_desc_t *descs, unsigned desc_cnt);

ucs_status_t ucg_plan_init(ucg_plan_desc_t *descs, unsigned desc_cnt,
                           ucg_comp_ctx_h plan, size_t *per_group_ctx_size,
                           uint8_t *am_id_p);


ucs_status_t ucg_plan_group_create(ucg_group_h group);

void ucg_plan_group_destroy(ucg_group_h group);

void ucg_plan_print_info(ucg_plan_desc_t *descs, unsigned desc_cnt, FILE *stream);

ucp_ep_h ucp_plan_get_p2p_ep_by_index(ucg_group_h group,
                                      ucg_group_member_index_t group_idx);

ucs_status_t
ucg_plan_await_lane_connection(ucp_worker_h worker, ucp_ep_h ucp_ep,
                               ucp_lane_index_t lane, uct_ep_h uct_ep);

#endif /* UCG_PLAN_H_ */
