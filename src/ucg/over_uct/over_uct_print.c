/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "over_uct.h"

void ucg_over_ucp_plan_print(ucs_pmodule_target_plan_t *tgt_plan);

void ucg_over_uct_plan_print(ucs_pmodule_target_plan_t *tgt_plan)
{
    int is_tl_bcast;
    int is_incast_tl;
    uint64_t cap_flags;
    unsigned phase_idx;
    ucg_over_uct_plan_phase_t *phase;
    ucg_over_uct_plan_t *plan = ucs_derived_of(tgt_plan, ucg_over_uct_plan_t);
    UCS_V_UNUSED ucg_step_idx_t prev_index = UCG_GROUP_FIRST_STEP_IDX;

#if ENABLE_DEBUG_DATA
    ucg_topo_print(plan->topo_desc);

    ucg_over_ucp_plan_print((ucs_pmodule_target_plan_t*)plan->ucp_plan);
#endif

    printf("Planner name:   UCG over UCT\n");
    printf("Phases:         %i\n",  plan->super.phs_cnt);
    printf("My index:       %u\n",  plan->my_index);

    printf("Additional flags:\n");
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_BARRIER) {
        printf("  - async. barrier\n");
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_PIPELINED) {
        printf("  - some phases are pipelined\n");
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_OPTIMIZE_CB) {
        printf("  - optimization attempt after %u invocations\n", plan->opt_cnt);
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_COPY_SRC_BUFFER) {
        printf("  - initialize \"%s\" from \"%s\"\n", plan->copy_dst,
               (char*)ucg_plan_get_params(&plan->super.super)->send.buffer);
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_COPY_SRC_OFFSET) {
        printf("  - only a portion of the input is used to initialize\n");
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_COPY_DST_OFFSET) {
        printf("  - only a portion of the output is initialized\n");
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_SEND_PACK) {
        printf("  - a generic datatype is packed and then sent\n");
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_RECV_UNPACK) {
        printf("  - a generic datatype is recieved and then unpacked\n");
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_VOLATILE_DT) {
        printf("  - rerun datatype detection on every invocation\n");
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_BARRIER_DELAY) {
        printf("  - add delay after the barrier (for benchmarking)\n");
    }
    if (plan->op_flags & UCG_OVER_UCT_PLAN_FLAG_IMBALANCE_INFO) {
        printf("  - collect imbalance time information\n");
    }

    for (phase_idx = 0; phase_idx < plan->super.phs_cnt; phase_idx++) {
        phase = &plan->phss[phase_idx];
        printf("Phase #%i:\n", phase_idx);

        if (phase->flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_EXPECTS_RX) {
            ucs_assert(prev_index <= phase->rx.step_idx);
            prev_index = phase->rx.step_idx;


            printf("\t(RX) Destination buffer:     %s\n", phase->rx.buffer);
            printf("\t(RX) Fragments total:        %u\n", phase->rx.frags_cnt);
            if (phase->rx.frags_cnt) {
                printf("\t(RX) Step index:             %u\n", phase->rx.step_idx);
                printf("\t(RX) Batch count:            %u\n", phase->rx.batch_cnt);
                printf("\t(RX) Batch length:           %u\n", phase->rx.batch_len);
            }

            printf("\t(RX) Completion aggregation: ");
            switch (phase->rx.comp_agg &
                    ~UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY) {
                case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_NOP:
                    printf("none");
                    break;

                case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_NO_OFFSET:
                    printf("write (no offset)");
                    break;

                case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_WRITE_WITH_OFFSET:
                    printf("write (with offset)");
                    break;

                case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_PIPELINE:
                    printf("pipeline");
                    break;

                case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_GATHER:
                    printf("gather");
                    break;

                case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_INTERNAL:
                    printf("reduce (internal)");
                    break;

                case UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REDUCE_EXTERNAL:
                    printf("reduce (external)");
                    break;
            }

            if (phase->rx.comp_agg &
                UCG_OVER_UCT_PLAN_PHASE_COMP_AGGREGATE_REMOTE_KEY) {
                printf(" - using a remote key (for zero-copy)\n");
            } else {
                printf("\n");
            }

            printf("\t(RX) Completion criteria:    ");
            switch (phase->rx.comp_criteria) {
                case UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_SINGLE_MESSAGE:
                    printf("one RX message\n");
                    break;

                case UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES:
                    printf("RX messages\n");
                    break;

                case UCG_OVER_UCT_PLAN_PHASE_COMP_CRITERIA_MULTIPLE_MESSAGES_ZCOPY:
                    printf("RX zero-copy\n");
                    break;
            }

            printf("\t(RX) Completion applies to:  ");
            switch (phase->rx.comp_action & 0x1) {
                case UCG_OVER_UCT_PLAN_PHASE_COMP_OP:
                    printf("operation");
                    break;

                case UCG_OVER_UCT_PLAN_PHASE_COMP_STEP:
                    printf("step");
                    break;
            }
            if  (phase->rx.comp_action & UCG_OVER_UCT_PLAN_PHASE_COMP_SEND) {
                printf(" - once TX has also been completed\n");
            } else {
                printf("\n");
            }

            if (phase->rx.comp_flags) {
                printf("\t(RX) Additional flags:\n");
            }
            if (phase->rx.comp_flags & UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_SLOT_LENGTH) {
                printf("\t\t   - data is slotted\n");
            }
            if (phase->rx.comp_flags & UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_FRAGMENTED_DATA) {
                printf("\t\t   - data is fragmented\n");
            }
            if (phase->rx.comp_flags & UCG_OVER_UCT_PLAN_PHASE_COMP_FLAG_GENERIC_DATATYPE) {
                printf("\t\t   - generic datatype\n");
            }
        }

        if (ucg_over_uct_plan_phase_flags_get_method(phase->flags)) {
            printf("\t(TX) Transmission buffer:    %s\n", phase->tx.buffer);
            printf("\t(TX) Destination count:      %u\n", phase->tx.ep_cnt);
            if (phase->tx.ep_cnt) {
                ucs_assert(prev_index <= phase->tx.am_header.msg.step_idx);
                prev_index = phase->tx.am_header.msg.step_idx;
                printf("\t(TX) Step index:             %u\n",
                       phase->tx.am_header.msg.step_idx);

                ucs_assert(phase->tx.mock.iface_attr != NULL);
                cap_flags    = phase->tx.mock.iface_attr->cap.flags;
                is_incast_tl = (cap_flags & UCT_IFACE_FLAG_INCAST) != 0;
                is_tl_bcast  = (cap_flags & UCT_IFACE_FLAG_BCAST)  != 0;
                if (is_incast_tl || is_tl_bcast) {
                    printf("\t(TX) Sent length:            %lu\n",
                           UCT_COLL_LENGTH_INFO_UNPACK_VALUE(phase->tx.length));
                    switch (UCT_COLL_LENGTH_INFO_UNPACK_MODE(phase->tx.length)) {
                    case UCT_COLL_LENGTH_INFO_DEFAULT:
                        printf("\t(TX) Sent mode:              padded\n");
                        break;

                    case UCT_COLL_LENGTH_INFO_PACKED:
                        printf("\t(TX) Sent mode:              packed\n");
                        break;

                    case UCT_COLL_LENGTH_INFO_VAR_COUNT:
                        printf("\t(TX) Sent mode:              variable count\n");
                        break;

                    case UCT_COLL_LENGTH_INFO_VAR_DTYPE:
                        printf("\t(TX) Sent mode:              variable datatype\n");
                        break;
                    }
                } else {
                    printf("\t(TX) Sent length:            %lu\n", phase->tx.length);
                }

                if (phase->flags & UCG_OVER_UCT_PLAN_PHASE_FLAG_FRAGMENTED) {
                    printf("\t(TX) fragment length:        %u\n",  phase->tx.frag_len);
                } else {
                    printf("\t(TX) Bcopy packing callback: ");
                    ucg_over_uct_print_pack_cb_name(phase->tx.bcopy_single.pack_single_cb);
                    printf("\n");
                }
            }
        }

        printf("\n");
    }
}
