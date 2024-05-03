/**
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "ucx_info.h"

#include <ucg/api/ucg_mpi.h>
#include <ucs/debug/memtrack_int.h>
#include <ucg/api/ucg_mpi.h>

/* Note: <ucg/api/...> not used because this header is not installed */
#include "../../ucg/api/ucg_plan_component.h"

/* In accordance with @ref enum ucg_predefined */
const char *ucg_predefined_collective_names[] = {
    [UCG_PRIMITIVE_BARRIER]              = "barrier",
    [UCG_PRIMITIVE_IBARRIER]             = "ibarrier",
    [UCG_PRIMITIVE_SCAN]                 = "scan",
    [UCG_PRIMITIVE_EXSCAN]               = "exscan",
    [UCG_PRIMITIVE_ALLREDUCE]            = "allreduce",
    [UCG_PRIMITIVE_REDUCE_SCATTER]       = "reduce_scatter",
    [UCG_PRIMITIVE_REDUCE_SCATTER_BLOCK] = "reduce_scatter_block",
    [UCG_PRIMITIVE_REDUCE]               = "reduce",
    [UCG_PRIMITIVE_GATHER]               = "gather",
    [UCG_PRIMITIVE_GATHERV]              = "gatherv",
    [UCG_PRIMITIVE_BCAST]                = "bcast",
    [UCG_PRIMITIVE_SCATTER]              = "scatter",
    [UCG_PRIMITIVE_SCATTERV]             = "scatterv",
    [UCG_PRIMITIVE_ALLGATHER]            = "allgather",
    [UCG_PRIMITIVE_ALLGATHERV]           = "allgatherv",
    [UCG_PRIMITIVE_ALLTOALL]             = "alltoall"
};

#define EMPTY UCG_GROUP_MEMBER_DISTANCE_UNKNOWN

ucp_address_t *worker_address = 0;
int dummy_resolve_address(void *cb_group_obj,
                          ucg_group_member_index_t index,
                          ucp_address_t **addr, size_t *addr_len)
{
    *addr = worker_address;
    *addr_len = 0; /* special debug flow: replace uct_ep_t with member indexes */
    return 0;
}

void dummy_release_address(ucp_address_t *addr) { }

ucs_status_t gen_ucg_topology(ucg_group_member_index_t me,
        ucg_group_member_index_t peer_count[4],
        enum ucg_group_member_distance **distance_array_p,
        ucg_group_member_index_t *distance_array_length_p)
{
    printf("UCG Processes per socket:  %u\n", peer_count[1]);
    printf("UCG Sockets per host:      %u\n", peer_count[2]);
    printf("UCG Hosts in the network:  %u\n", peer_count[3]);

    /* generate the array of distances in order to create a group */
    ucg_group_member_index_t member_count = 1;
    peer_count[0] = 1; /* not initialized by the user */
    unsigned distance_idx;
    for (distance_idx = 0; distance_idx < 4; distance_idx++) {
        if (peer_count[distance_idx]) {
            member_count *= peer_count[distance_idx];
            peer_count[distance_idx] = member_count;
        }
    }

    if (me >= member_count) {
        printf("<Error: index is %u, out of %u total>\n", me, member_count);
        return UCS_ERR_INVALID_PARAM;
    }

    /* create the distance array for group creation */
    printf("UCG Total member count:    %u\n", member_count);
    enum ucg_group_member_distance *distance_array =
            ucs_malloc(member_count * sizeof(*distance_array), "distance array");
    if (!distance_array) {
        printf("<Error: failed to allocate the distance array>\n");
        return UCS_ERR_NO_MEMORY;
    }

    memset(distance_array, EMPTY, member_count * sizeof(*distance_array));
    enum ucg_group_member_distance distance = UCG_GROUP_MEMBER_DISTANCE_NONE;
    for (distance_idx = 0; distance_idx < 4; distance_idx++) {
        if (peer_count[distance_idx]) {
            unsigned array_idx, array_offset = me - (me % peer_count[distance_idx]);
            for (array_idx = 0; array_idx < peer_count[distance_idx]; array_idx++) {
                if (distance_array[array_idx + array_offset] == EMPTY) {
                    distance_array[array_idx + array_offset] = distance;
                }
            }
        }

        switch (distance) {
        case UCG_GROUP_MEMBER_DISTANCE_NONE:
            distance = UCG_GROUP_MEMBER_DISTANCE_L3CACHE;
            break;

        case UCG_GROUP_MEMBER_DISTANCE_L3CACHE:
            distance = UCG_GROUP_MEMBER_DISTANCE_BOARD;
            break;

        case UCG_GROUP_MEMBER_DISTANCE_HOST:
            distance = UCG_GROUP_MEMBER_DISTANCE_CU;
            break;

        case UCG_GROUP_MEMBER_DISTANCE_CU:
            distance = UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
            break;

        default:
            break;
        }
    }

    *distance_array_length_p = member_count;
    *distance_array_p = distance_array;
    return UCS_OK;
}

void print_ucg_topology(ucp_worker_h worker, ucg_group_member_index_t root,
        ucg_group_member_index_t me, const char *collective_type_name,
        size_t dtype_count, enum ucg_group_member_distance *distance_array,
        ucg_group_member_index_t member_count, int is_verbose)
{
    unsigned idx, last;
    ucg_coll_h plan;
    ucg_group_h group;
    ucs_status_t status;
    size_t worker_address_length;
    ucg_group_params_t group_params;
    ucg_collective_params_t coll_params;

    /* print the resulting distance array*/
    printf("UCG Distance array for rank #%3u [", me);
    for (idx = 0; idx < member_count; idx++) {
        printf("%x", distance_array[idx]);
    }
    printf("] (Capital letter for root and self)\n\n\n");
    if (!is_verbose) {
        return;
    }

    /* create a group with the generated parameters */
    group_params.field_mask     = UCG_GROUP_PARAM_FIELD_UCP_WORKER   |
                                  UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |
                                  UCG_GROUP_PARAM_FIELD_MEMBER_INDEX |
                                  UCG_GROUP_PARAM_FIELD_CB_CONTEXT   |
                                  UCG_GROUP_PARAM_FIELD_DISTANCES;
    group_params.worker         = worker;
    group_params.member_index   = me;
    group_params.member_count   = member_count;
    group_params.distance_type  = UCG_GROUP_DISTANCE_TYPE_ARRAY;
    group_params.distance_array = distance_array;
    group_params.cb_context     = NULL; /* dummy */
#if ENABLE_DEBUG_DATA
    group_params.name           = "test group";
#endif

    status = ucp_worker_get_address(worker, &worker_address,
                                    &worker_address_length);
    if (status != UCS_OK) {
        goto cleanup;
    }

    status = ucg_group_create(&group_params, &group);
    if (status != UCS_OK) {
        goto address_cleanup;
    }

    /* plan a collective operation */
    coll_params.send.buffer    = "send-buffer";
    coll_params.recv.buffer    = "recv-buffer";
    coll_params.send.dtype     =
    coll_params.recv.dtype     = (void*)1; /* see @ref datatype_test_converter */
    coll_params.send.count     =
    coll_params.recv.count     = dtype_count;
    coll_params.send.type.root = root;
#if ENABLE_DEBUG_DATA
    coll_params.name           = collective_type_name;
#endif

    // TODO: support neighbor + alltoallv/w
    last = ucs_static_array_size(ucg_predefined_collective_names);
    for (idx = 0; idx < last; idx++) {
        if (!strcmp(ucg_predefined_collective_names[idx], collective_type_name)) {
            coll_params.send.type.modifiers =
                UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID |
                UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS |
                ucg_predefined_modifiers[idx];
            break;
        }
    }
    if (idx == last) {
        status = UCS_ERR_UNSUPPORTED;
        goto group_cleanup;
    }

    status = ucg_collective_create(group, &coll_params, &plan);
    if (status != UCS_OK) {
        goto group_cleanup;
    }

    /* Call the printing function of that component */
    ((ucs_pmodule_target_plan_t*)plan)->component->print(plan);
    ucg_plan_connect_mock_cleanup();

group_cleanup:
    ucg_group_destroy(group);

address_cleanup:
    ucp_worker_release_address(worker, worker_address);

cleanup:
    if (status != UCS_OK) {
        printf("<Failed to plan a UCG collective: %s>\n", ucs_status_string(status));
    }
}
