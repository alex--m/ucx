/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_ucp.h"

void ucg_over_ucp_plan_print(ucs_pmodule_target_plan_t *tgt_plan)
{
    ucg_over_ucp_plan_phase_t *phase;
    ucg_over_ucp_plan_t *plan = ucs_derived_of(tgt_plan, ucg_over_ucp_plan_t);

    printf("Planner name:   UCG over UCP\n");
    printf("Phases:         %i\n", plan->phs_cnt);

    unsigned phase_idx;
    for (phase_idx = 0; phase_idx < plan->phs_cnt; phase_idx++) {
        printf("Phase #%i ", phase_idx);
        phase = &plan->phss[phase_idx];
        if (phase->rx_cnt)
            printf("(rx_idx: %u) ", phase->rx.step_idx);
        if (phase->dest.ep_cnt)
            printf("(tx_idx: %u) ", phase->tx.step_idx);
        if (phase->dest.ep_cnt > 0) {
            if (phase->rx_cnt) {
                printf(": %u incoming messages, then sends to", phase->rx_cnt);
            } else {
                printf(": sends to");
            }
        } else {
            printf(": %u incoming messages (no peer information)", phase->rx_cnt);
        }

#if ENABLE_DEBUG_DATA || ENABLE_FAULT_TOLERANCE
        unsigned peer_idx;
        ucg_group_member_index_t *idx_iter = (phase->dest.ep_cnt == 1) ?
                                             &phase->dest.index :
                                              phase->dest.indexes;
        for (peer_idx = 0;
             peer_idx < phase->dest.ep_cnt;
             peer_idx++, idx_iter++) {
            printf(" %u,", *idx_iter);
        }
#else
        printf(" unspecified destinations (configured without \"--enable-debug-data\")");
#endif
        printf("\n\n");
    }
}
