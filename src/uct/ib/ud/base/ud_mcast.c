/**
* Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2021. ALL RIGHTS RESERVED.
* Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "ud_mcast.h"
#include "ud_iface.h"

#ifdef ENABLE_STATS
enum {
    UCT_UD_MCAST_IFACE_STAT_RX_DROP,
    UCT_UD_MCAST_IFACE_STAT_LAST
};

static ucs_stats_class_t uct_ud_mcast_iface_stats_class = {
    .name          = "ud_mcast",
    .num_counters  = UCT_UD_MCAST_IFACE_STAT_LAST,
    .class_id      = UCS_STATS_CLASS_ID_INVALID,
    .counter_names = {
        [UCT_UD_MCAST_IFACE_STAT_RX_DROP] = "rx_drop"
    }
};
#endif

static ucs_status_t uct_ud_mcast_iface_query_empty(uct_iface_h tl_iface,
                                                   uct_iface_attr_t *iface_attr)
{
    /* In cases where a collective transport is not possible - avoid usage */
    uct_ud_iface_t *iface = ucs_derived_of(tl_iface, uct_ud_iface_t);

    uct_ud_iface_query(iface, iface_attr, 0, 0);

    iface_attr->cap.flags |= UCT_IFACE_FLAG_BCAST;

    return UCS_OK;
}

ucs_status_t
uct_ud_mcast_init_collective_check(uct_worker_h *worker_p,
                                   const uct_iface_params_t *params,
                                   uct_iface_ops_t *uct_mm_bcast_iface_ops)
{
    int is_collective     = (params->field_mask & UCT_IFACE_PARAM_FIELD_GROUP_INFO);
    //uint32_t procs        = is_collective ? params->group_info.proc_cnt : 1;

    if (!is_collective) {
        *worker_p                                      = NULL;
        uct_mm_bcast_iface_ops->iface_query            = uct_ud_mcast_iface_query_empty;
        uct_mm_bcast_iface_ops->iface_progress         = (uct_iface_progress_func_t)
                                                         ucs_empty_function_do_assert;
        uct_mm_bcast_iface_ops->iface_progress_enable  = (uct_iface_progress_enable_func_t)
                                                         ucs_empty_function;
        uct_mm_bcast_iface_ops->iface_progress_disable = (uct_iface_progress_disable_func_t)
                                                         ucs_empty_function;
    }

    return UCS_OK;
}

static ucs_status_t
uct_ud_mcast_create_ah(uct_ud_verbs_ep_peer_address_t* address_p)
{
    struct ibv_ah_attr ah_attr;
    enum ibv_mtu path_mtu;
    ucs_status_t status;

    memset(address_p, 0, sizeof(*address_p));



}

static ucs_status_t
uct_ud_mcast_join(uct_ud_mcast_ctx_t *mcast, union ibv_gid *mcast_dgid)
{
    uct_ep_h rdma_cm_ep;
    struct sockaddr_in6 priv_data = {
        .sin6_family = AF_INET6
    };
    ucs_sock_addr_t sock_addr = {
        .addr = &priv_data,
        .addrlen = sizeof(priv_data)
    };
    uct_ep_params_t create_params = {
        .field_mask = UCT_EP_PARAM_FIELD_SOCKADDR
        .sockaddr = &sock_addr
    };
    uct_ep_connect_params_t connect_params = {
        .field_mask          = UCT_EP_CONNECT_PARAM_FIELD_PRIVATE_DATA |
                               UCT_EP_CONNECT_PARAM_FIELD_PRIVATE_DATA_LENGTH |
                               UCT_RDMACM_CM_EP_MULTICAST,
        .private_data        = &priv_data,
        .private_data_length = sizeof(priv_data)
    };

    /* Fill in the connection (destination) socket address */
    create_params.sockaddr->addr->sa_family = AF_INET6
    if (mcast_dgid) {
        /* Connect to an already existing multicast group */
        memcpy(&priv_data.sin6_addr, &(data->dgid), sizeof(struct in6_addr));
    } else {
        /* Create a new multicast group */
        priv_data.sin6_flowinfo = comm->comm_id;
    }

    status = UCS_CLASS_NEW_FUNC_NAME(uct_rdmacm_cm_ep_t)(&create_params,
                                                         &rdma_cm_ep);
    if (status != UCS_OK) {
        return status;
    }

    return uct_rdmacm_cm_ep_connect(rdma_cm_ep, &connect_params);
}

ucs_status_t
uct_ud_mcast_group_init(uct_ud_mcast_ctx_t *mcast, ucs_stats_node_t *parent)
{
    ucs_status_t status;






    status = UCS_STATS_NODE_ALLOC(&mcast->stats, &uct_ud_mcast_iface_stats_class,
                                  parent, "-%p", mcast);
    if (status != UCS_OK) {
        return status;
    }

    return UCS_OK;
}

uct_ud_mcast_ep_connect(uct_ud_mcast_ctx_t *mcast, union ibv_gid *mcast_dgid)

void uct_ud_mcast_group_cleanup(uct_ud_mcast_ctx_t *mcast)
{
    UCS_STATS_NODE_FREE(mcast->stats);
}















// {


//     ctx->channel = rdma_create_event_channel();
//     if (!ctx->channel) {
//         tl_debug(lib, "rdma_create_event_channel failed, errno %d", errno);
//         status = UCC_ERR_NO_RESOURCE;
//         goto error;
//     }

//     memset(&dst_addr, 0, sizeof(struct sockaddr_storage));
//     dst_addr.ss_family = is_ipv4 ? AF_INET : AF_INET6;
//     if (rdma_create_id(ctx->channel, &ctx->id, NULL, RDMA_PS_UDP)) {
//         tl_debug(lib, "failed to create rdma id, errno %d", errno);
//         status = UCC_ERR_NOT_SUPPORTED;
//         goto error;
//     }

//     if (ibv_modify_qp(comm->mcast.qp, &attr,
//                       IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY)) {
//         tl_warn(ctx->lib, "failed to move mcast qp to INIT, errno %d", errno);
//         return UCC_ERR_NO_RESOURCE;
//     }

//     if (ibv_attach_mcast(comm->mcast.qp, &comm->mgid, comm->mcast_lid)) {
//         tl_warn(ctx->lib, "failed to attach QP to the mcast group, errno %d", errno);
//         return UCC_ERR_NO_RESOURCE;
//     }

//     /* Ok, now cycle to RTR on everyone */
//     attr.qp_state = IBV_QPS_RTR;
//     if (ibv_modify_qp(comm->mcast.qp, &attr, IBV_QP_STATE)) {
//         tl_warn(ctx->lib, "failed to modify QP to RTR, errno %d", errno);
//         return UCC_ERR_NO_RESOURCE;
//     }

//     attr.qp_state = IBV_QPS_RTS;
//     attr.sq_psn   = DEF_PSN;
//     if (ibv_modify_qp(comm->mcast.qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
//         tl_warn(ctx->lib, "failed to modify QP to RTS, errno %d", errno);
//         return UCC_ERR_NO_RESOURCE;
//     }

//     /* Create the address handle */
//     if (UCC_OK != ucc_tl_mlx5_mcast_create_ah(comm)) {
//         tl_warn(ctx->lib, "failed to create adress handle");
//         return UCC_ERR_NO_RESOURCE;
//     }
// }


// ucs_status_t ucc_tl_mlx5_mcast_join_mcast_post(ucc_tl_mlx5_mcast_coll_context_t *ctx,
//                                                struct sockaddr_in6              *net_addr,
//                                                int                               is_root)
// {
//     char        buf[40];
//     const char *dst;

//     dst = inet_ntop(AF_INET6, net_addr, buf, 40);
//     if (NULL == dst) {
//         tl_warn(ctx->lib, "inet_ntop failed");
//         return UCC_ERR_NO_RESOURCE;
//     }

//     tl_debug(ctx->lib, "joining addr: %s is_root %d", buf, is_root);

//     if (rdma_join_multicast(ctx->id, (struct sockaddr*)net_addr, NULL)) {
//         tl_warn(ctx->lib, "rdma_join_multicast failed errno %d", errno);
//         return UCC_ERR_NO_RESOURCE;
//     }

//     return UCC_OK;
// }

// ucs_status_t ucc_tl_mlx5_mcast_join_mcast_test(ucc_tl_mlx5_mcast_coll_context_t *ctx,
//                                                struct rdma_cm_event            **event,
//                                                int                               is_root)
// {
//     char        buf[40];
//     const char *dst;

//     if (rdma_get_cm_event(ctx->channel, event) < 0) {
//         if (EINTR != errno) {
//             tl_warn(ctx->lib, "rdma_get_cm_event failed, errno %d %s",
//                     errno, strerror(errno));
//             return UCC_ERR_NO_RESOURCE;
//         } else {
//             return UCC_INPROGRESS;
//         }
//     }

//     if (RDMA_CM_EVENT_MULTICAST_JOIN != (*event)->event) {
//         tl_warn(ctx->lib, "failed to join multicast, is_root %d. unexpected event was"
//                 " received: event=%d, str=%s, status=%d",
//                  is_root, (*event)->event, rdma_event_str((*event)->event),
//                  (*event)->status);
//         if (rdma_ack_cm_event(*event) < 0) {
//             tl_warn(ctx->lib, "rdma_ack_cm_event failed");
//         }
//         return UCC_ERR_NO_RESOURCE;
//     }

//     dst = inet_ntop(AF_INET6, (*event)->param.ud.ah_attr.grh.dgid.raw, buf, 40);
//     if (NULL == dst) {
//         tl_warn(ctx->lib, "inet_ntop failed");
//         return UCC_ERR_NO_RESOURCE;
//     }

//     tl_debug(ctx->lib, "is_root %d: joined dgid: %s, mlid 0x%x, sl %d", is_root, buf,
//              (*event)->param.ud.ah_attr.dlid, (*event)->param.ud.ah_attr.sl);

//     return UCC_OK;

// }

// ucs_status_t ucc_tl_mlx5_setup_mcast_group_join_post(ucc_tl_mlx5_mcast_coll_comm_t *comm)
// {
//     ucs_status_t          status;
//     struct sockaddr_in6   net_addr = {0,};

//     if (comm->rank == 0) {
//         net_addr.sin6_family   = AF_INET6;
//         net_addr.sin6_flowinfo = comm->comm_id;

//         status = ucc_tl_mlx5_mcast_join_mcast_post(comm->ctx, &net_addr, 1);
//         if (status < 0) {
//             tl_warn(comm->lib, "rank 0 is unable to join mcast group");
//             return status;
//         }
//     }

//     return UCC_OK;
// }

// ucs_status_t ucc_tl_mlx5_fini_mcast_group(ucc_tl_mlx5_mcast_coll_context_t *ctx,
//                                           ucc_tl_mlx5_mcast_coll_comm_t    *comm)
// {
//     char        buf[40];
//     const char *dst;

//     dst = inet_ntop(AF_INET6, &comm->mcast_addr, buf, 40);
//     if (NULL == dst) {
//         tl_error(comm->lib, "inet_ntop failed");
//         return UCC_ERR_NO_RESOURCE;
//     }

//     tl_debug(ctx->lib, "mcast leave: ctx %p, comm %p, dgid: %s", ctx, comm, buf);

//     if (rdma_leave_multicast(ctx->id, (struct sockaddr*)&comm->mcast_addr)) {
//         tl_error(comm->lib, "mcast rmda_leave_multicast failed");
//         return UCC_ERR_NO_RESOURCE;
//     }

//     return UCC_OK;
// }

// ucs_status_t ucc_tl_mlx5_clean_mcast_comm(ucc_tl_mlx5_mcast_coll_comm_t *comm)
// {
//     ucc_tl_mlx5_mcast_context_t *mcast_ctx = ucc_container_of(comm->ctx, ucc_tl_mlx5_mcast_context_t, mcast_context);
//     ucc_tl_mlx5_context_t       *mlx5_ctx  = ucc_container_of(mcast_ctx, ucc_tl_mlx5_context_t, mcast);
//     ucc_context_h                context   = mlx5_ctx->super.super.ucc_context;
//     int                          ret;
//     ucs_status_t                 status;

//     tl_debug(comm->lib, "cleaning  mcast comm: %p, id %d, mlid %x",
//              comm, comm->comm_id, comm->mcast_lid);

//     while (UCC_INPROGRESS == (status = ucc_tl_mlx5_mcast_reliable(comm))) {
//         ucc_context_progress(context);
//     }

//     if (UCC_OK != status) {
//         tl_error(comm->lib, "failed to clean mcast team: relibality progress status %d",
//                  status);
//         return status;
//     }

//     if (comm->mcast.qp) {
//         ret = ibv_detach_mcast(comm->mcast.qp, &comm->mgid, comm->mcast_lid);
//         if (ret) {
//             tl_error(comm->lib, "couldn't detach QP, ret %d, errno %d", ret, errno);
//             return UCC_ERR_NO_RESOURCE;
//         }
//     }

//     if (comm->mcast.qp) {
//         ret = ibv_destroy_qp(comm->mcast.qp);
//         if (ret) {
//             tl_error(comm->lib, "failed to destroy QP %d", ret);
//             return UCC_ERR_NO_RESOURCE;
//         }
//     }

//     if (comm->mcast.ah) {
//         ret = ibv_destroy_ah(comm->mcast.ah);
//         if (ret) {
//             tl_error(comm->lib, "couldn't destroy ah");
//             return UCC_ERR_NO_RESOURCE;
//         }
//     }

//     if (comm->mcast_lid) {
//         status = ucc_tl_mlx5_fini_mcast_group(comm->ctx, comm);
//         if (status) {
//             tl_error(comm->lib, "couldn't leave mcast group");
//             return status;
//         }
//     }

//     if (ctx->id && rdma_destroy_id(ctx->id)) {
//         tl_error(ctx->lib, "rdma_destroy_id failed errno %d", errno);
//         return UCC_ERR_NO_RESOURCE;
//     }

//     ctx->id = NULL;

//     if (ctx->channel) {
//         rdma_destroy_event_channel(ctx->channel);
//         ctx->channel = NULL;
//     }
// }
