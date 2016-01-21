/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_IB_LOG_H
#define UCT_IB_LOG_H

#include "ib_verbs.h"
#include "ib_iface.h"

#include <uct/base/uct_log.h>
#include <ucs/debug/log.h>
#include <ucs/sys/math.h>


enum {
    UCT_IB_OPCODE_FLAG_HAS_RADDR       = UCS_BIT(0),
    UCT_IB_OPCODE_FLAG_HAS_ATOMIC      = UCS_BIT(1),
    UCT_IB_OPCODE_FLAG_HAS_EXT_ATOMIC  = UCS_BIT(2)
};


typedef struct uct_ib_opcode {
    const char *name;
    uint32_t   flags;
} uct_ib_opcode_t;


void uct_ib_log_dump_opcode(uint32_t qp_num, uct_ib_opcode_t *op, int signal,
                            int fence, int se, char *buf, size_t max);

void uct_ib_log_dump_sg_list(uct_ib_iface_t *iface, uct_am_trace_type_t type,
                             struct ibv_sge *sg_list, int num_sge,
                             uint64_t inline_bitmap,
                             uct_log_data_dump_func_t data_dump,
                             char *buf, size_t max);

void uct_ib_log_dump_remote_addr(uint64_t remote_addr, uint32_t rkey,
                                 char *buf, size_t max);

void uct_ib_log_dump_atomic_fadd(uint64_t add, char *buf, size_t max);

void uct_ib_log_dump_atomic_cswap(uint64_t compare, uint64_t swap, char *buf, size_t max);

void uct_ib_log_dump_atomic_masked_fadd(int argsize, uint64_t add, uint64_t boundary,
                                        char *buf, size_t max);

void uct_ib_log_dump_atomic_masked_cswap(int argsize, uint64_t compare, uint64_t compare_mask,
                                         uint64_t swap, uint64_t swap_mask,
                                         char *buf, size_t max);

void uct_ib_log_dump_recv_completion(uct_ib_iface_t *iface, enum ibv_qp_type qp_type,
                                     uint32_t local_qp, uint32_t sender_qp,
                                     uint16_t sender_lid, void *data, size_t length,
                                     uct_log_data_dump_func_t data_dump,
                                     char *buf, size_t max);

void __uct_ib_log_post_send(const char *file, int line, const char *function,
                            uct_ib_iface_t *iface, struct ibv_qp *qp,
                            struct ibv_send_wr *wr,
                            uct_log_data_dump_func_t packet_dump_cb);

void __uct_ib_log_recv_completion(const char *file, int line, const char *function,
                                  uct_ib_iface_t *iface, enum ibv_qp_type qp_type,
                                  struct ibv_wc *wc, void *data,
                                  uct_log_data_dump_func_t packet_dump_cb);

#if HAVE_DECL_IBV_EXP_POST_SEND
void __uct_ib_log_exp_post_send(const char *file, int line, const char *function,
                                uct_ib_iface_t *iface, struct ibv_qp *qp,
                                struct ibv_exp_send_wr *wr,
                                uct_log_data_dump_func_t packet_dump_cb);
#endif


#define uct_ib_log_post_send(_iface, _qp, _wr, _dump_cb) \
    if (ucs_log_enabled(UCS_LOG_LEVEL_TRACE_DATA)) { \
        __uct_ib_log_post_send(__FILE__, __LINE__, __FUNCTION__, _iface, _qp, _wr, _dump_cb); \
    }

#define uct_ib_log_recv_completion(_iface, _qp_type, _wc, _data, _dump_cb, ...) \
    if (ucs_log_enabled(UCS_LOG_LEVEL_TRACE_DATA)) { \
        __uct_ib_log_recv_completion(__FILE__, __LINE__, __FUNCTION__, \
                                     _iface, _qp_type, _wc, _data, _dump_cb, ## __VA_ARGS__); \
    }

#define uct_ib_log_exp_post_send(_iface, _qp, _wr, _dump_cb) \
    if (ucs_log_enabled(UCS_LOG_LEVEL_TRACE_DATA)) { \
        __uct_ib_log_exp_post_send(__FILE__, __LINE__, __FUNCTION__, _iface, _qp, _wr, _dump_cb); \
    }


#if HAVE_INSTRUMENTATION

#define CHECK_RECV_SIGNALED(_wr) (1)

#define CHECK_SEND_SIGNALED(_wr) \
    ((_wr->send_flags & IBV_SEND_SIGNALED) && (_wr->opcode == IBV_WR_SEND))

#define CHECK_SEND_EXP_SIGNALED(_wr) \
    ((_wr->exp_send_flags & IBV_EXP_SEND_SIGNALED) && \
     (_wr->exp_opcode == IBV_EXP_WR_SEND))

#define UCT_IB_INSTRUMENT_RECORD_WR_LEN(_type, _name, _wr, _check) { \
    typeof(_wr) iterator = (_wr); \
    while (iterator) { \
        if _check(iterator) { \
            size_t length = 0; \
            int sge_index; \
            for (sge_index = 0; sge_index < iterator->num_sge; sge_index++) { \
                length += iterator->sg_list[sge_index].length; \
            } \
            UCS_INSTRUMENT_RECORD(_type, _name, iterator->wr_id, length); \
        } \
        iterator = iterator->next; \
    } \
}

#define UCT_IB_INSTRUMENT_RECORD_RECV_WR_LEN(_name, _wr) \
        UCT_IB_INSTRUMENT_RECORD_WR_LEN(UCS_INSTRUMENT_TYPE_IB_RX, _name, _wr, \
                                        CHECK_RECV_SIGNALED)

#define UCT_IB_INSTRUMENT_RECORD_SEND_WR_LEN(_name, _wr) \
        UCT_IB_INSTRUMENT_RECORD_WR_LEN(UCS_INSTRUMENT_TYPE_IB_TX, _name, _wr, \
                                        CHECK_SEND_SIGNALED)

#define UCT_IB_INSTRUMENT_RECORD_SEND_EXP_WR_LEN(_name, _wr) \
        UCT_IB_INSTRUMENT_RECORD_WR_LEN(UCS_INSTRUMENT_TYPE_IB_TX, _name, _wr, \
                                        CHECK_SEND_EXP_SIGNALED)

#else

#define UCT_IB_INSTRUMENT_RECORD_WR_LEN(...)
#define UCT_IB_INSTRUMENT_RECORD_RECV_WR_LEN(...)
#define UCT_IB_INSTRUMENT_RECORD_SEND_WR_LEN(...)
#define UCT_IB_INSTRUMENT_RECORD_SEND_EXP_WR_LEN(...)

#endif

#endif
