/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_MPI_H_
#define UCG_MPI_H_

#include <ucg/api/ucg.h>

BEGIN_C_DECLS

#if ENABLE_DEBUG_DATA
#define UCG_COLL_DEBUG_NAME(_name) , .name = # _name
#else
#define UCG_COLL_DEBUG_NAME(_name)
#endif

/*
 * Below are the definitions targeted specifically for the MPI standard.
 * This includes a list of predefined collective operations, and the modifiers
 * that describe each. The macros generate functions with prototypes matching
 * the MPI requirement, including the same arguments the user would pass.
 */

enum ucg_predefined {
    UCG_PRIMITIVE_BARRIER,
    UCG_PRIMITIVE_IBARRIER,
    UCG_PRIMITIVE_SCAN,
    UCG_PRIMITIVE_EXSCAN,
    UCG_PRIMITIVE_ALLREDUCE,
    UCG_PRIMITIVE_REDUCE_SCATTER,
    UCG_PRIMITIVE_REDUCE_SCATTER_BLOCK,
    UCG_PRIMITIVE_REDUCE,
    UCG_PRIMITIVE_GATHER,
    UCG_PRIMITIVE_GATHERV,
    UCG_PRIMITIVE_BCAST,
    UCG_PRIMITIVE_SCATTER,
    UCG_PRIMITIVE_SCATTERV,
    UCG_PRIMITIVE_ALLGATHER,
    UCG_PRIMITIVE_ALLGATHERV,
    UCG_PRIMITIVE_ALLTOALL,
    UCG_PRIMITIVE_NEIGHBOR_ALLTOALL,
    UCG_PRIMITIVE_NEIGHBOR_ALLGATHER,
    UCG_PRIMITIVE_NEIGHBOR_ALLGATHERV,

    /* API for Alltoallv and Alltoallw is different - see TYPE_VALID flag */
    UCG_PRIMITIVE_ALLTOALLV,
    UCG_PRIMITIVE_ALLTOALLW,
    UCG_PRIMITIVE_NEIGHBOR_ALLTOALLV,
    UCG_PRIMITIVE_NEIGHBOR_ALLTOALLW,
};

static const uint16_t ucg_predefined_modifiers[] = {
    [UCG_PRIMITIVE_BARRIER]              = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST,
    [UCG_PRIMITIVE_IBARRIER]             = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER,
    [UCG_PRIMITIVE_SCAN]                 = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_PARTIAL,
    [UCG_PRIMITIVE_EXSCAN]               = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_PARTIAL |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC /* abuse */,
    [UCG_PRIMITIVE_ALLREDUCE]            = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST,
    [UCG_PRIMITIVE_REDUCE_SCATTER]       = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE,
    [UCG_PRIMITIVE_REDUCE_SCATTER_BLOCK] = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC /* abuse */,
    [UCG_PRIMITIVE_REDUCE]               = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION,
    [UCG_PRIMITIVE_GATHER]               = UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION,
    [UCG_PRIMITIVE_GATHERV]              = UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC,
    [UCG_PRIMITIVE_BCAST]                = UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE,
    [UCG_PRIMITIVE_SCATTER]              = UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE,
    [UCG_PRIMITIVE_SCATTERV]             = UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC,
    [UCG_PRIMITIVE_ALLGATHER]            = UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST,
    [UCG_PRIMITIVE_ALLGATHERV]           = UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC,
    [UCG_PRIMITIVE_ALLTOALL]             = 0,
    [UCG_PRIMITIVE_ALLTOALLV]            = UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC,
    [UCG_PRIMITIVE_ALLTOALLW]            = 0,
    [UCG_PRIMITIVE_NEIGHBOR_ALLGATHER]   = UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR,
    [UCG_PRIMITIVE_NEIGHBOR_ALLGATHERV]  = UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC,
    [UCG_PRIMITIVE_NEIGHBOR_ALLTOALL]    = UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR,
    [UCG_PRIMITIVE_NEIGHBOR_ALLTOALLV]   = UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR |
                                           UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC,
    [UCG_PRIMITIVE_NEIGHBOR_ALLTOALLW]   = UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR
};

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_coll_barrier_init(uint16_t additional_modifiers, ucg_group_h group,
                      ucg_coll_h *coll_p)
{
    ucs_status_t status;
    ucg_collective_params_t barrier_params = {
        .send = {
            .type = {
                .modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID |
                             UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE  |
                             UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST,
            }
        }
        UCG_COLL_DEBUG_NAME(barrier)
    };
    uint16_t old = barrier_params.send.type.modifiers;
    barrier_params.send.type.modifiers = old | additional_modifiers;

    status = ucg_collective_create(group, &barrier_params, coll_p);

    barrier_params.send.type.modifiers = old;
    return status;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_coll_ibarrier_init(uint16_t additional_modifiers, ucg_group_h group,
                       ucg_coll_h *coll_p)
{
    ucs_status_t status;
    ucg_collective_params_t ibarrier_params = {
        .send = {
            .type = {
                .modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID |
                             UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE  |
                             UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST  |
                             UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER,
            }
        }
        UCG_COLL_DEBUG_NAME(ibarrier)
    };
    uint16_t old = ibarrier_params.send.type.modifiers;
    ibarrier_params.send.type.modifiers = old | additional_modifiers;

    status = ucg_collective_create(group, &ibarrier_params, coll_p);

    ibarrier_params.send.type.modifiers = old;
    return status;
}

#define UCG_COLL_PARAMS_BUF_CR(_buffer, _count, _dtype) \
    .cbuffer = _buffer, \
    .count   = _count, \
    .dtype   = _dtype

#define UCG_COLL_PARAMS_BUF_R(_buffer, _count, _dtype) \
    .buffer = _buffer, \
    .count  = _count, \
    .dtype  = _dtype

#define UCG_COLL_PARAMS_BUF_O(_buffer, _count, _dtype) \
    UCG_COLL_PARAMS_BUF_R(_buffer, _count, _dtype), \
    .op     = op

#define UCG_COLL_PARAMS_BUF_N(_buffer, _counts, _dtype) \
    .buffer = _buffer, \
    .counts = _counts, \
    .dtype  = _dtype, \
    .op     = op

#define UCG_COLL_PARAMS_BUF_CV(_buffer, _counts, _dtype, _displs) \
    .cbuffer = _buffer, \
    .counts  = _counts, \
    .dtype   = (void*) _dtype, \
    .displs  = _displs

#define UCG_COLL_PARAMS_BUF_CS(_buffer, _counts, _dtype) \
    .cbuffer = _buffer, \
    .counts  = _counts, \
    .dtype   = (void*) _dtype

#define UCG_COLL_PARAMS_BUF_V(_buffer, _count, _dtype, _displs) \
    .buffer = _buffer, \
    .count  = _count, \
    .dtype  = (void*) _dtype,  \
    .displs = _displs

#define UCG_COLL_PARAMS_BUF_W(_buffer, _counts, _dtype, _displs) \
    .buffer = _buffer, \
    .counts = _counts, \
    .dtype  = (void*) _dtype,  \
    .displs = _displs

#define UCG_COLL_PARAMS_BUF_CW(_buffer, _counts, _dtypes, _displs) \
    .cbuffer  = _buffer, \
    .counts   = _counts, \
    .dtypes   = (void**) _dtypes, \
    .wdispls  = _displs

#define UCG_COLL_PARAMS_BUF_A(_buffer, _counts, _dtypes, _displs) \
    .buffer = _buffer, \
    .counts = _counts, \
    .dtypes = (void**) _dtypes, \
    .displs = _displs

#define UCG_COLL_PARAMS_BUF_CA(_buffer, _counts, _dtypes, _displs) \
    .cbuffer = _buffer, \
    .counts  = _counts, \
    .dtypes  = (void**) _dtypes, \
    .displs  = _displs

#define UCG_COLL_PARAMS_BUF_AW(_buffer, _counts, _dtypes, _displs) \
    .buffer  = _buffer, \
    .counts  = _counts, \
    .dtypes  = (void**) _dtypes, \
    .wdispls = _displs

#define UCG_COLL_INIT_BASE(_prefix, _lname, _uname, _stype, _sargs, _rtype, \
                           _rargs, _extra_modifiers, ...) \
static UCS_F_ALWAYS_INLINE ucs_status_t \
ucg_coll##_prefix##_lname##_init(__VA_ARGS__,  void *op, \
                         ucg_group_member_index_t root, \
                         unsigned modifiers, ucg_group_h group, \
                         ucg_coll_h *coll_p) \
{ \
    uint16_t md = modifiers | UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID | \
                  ucg_predefined_modifiers[UCG_PRIMITIVE_##_uname]; \
    const ucg_collective_params_t params = { \
        .send = { \
            .type = { \
                .modifiers = md, \
                .root      = root \
            }, \
            UCG_COLL_PARAMS_BUF##_stype _sargs \
        }, \
        .recv = { \
            UCG_COLL_PARAMS_BUF##_rtype _rargs \
        } \
        UCG_COLL_DEBUG_NAME(_lname) \
    }; \
    \
    return ucg_collective_create(group, &params, coll_p); \
}

#define UCG_COLL_INIT(_lname, _uname, _stype, _sargs, _rtype, _rargs, ...) \
    UCG_COLL_INIT_BASE(_, _lname, _uname, _stype, _sargs, _rtype, _rargs, 0, \
                       __VA_ARGS__) \
    UCG_COLL_INIT_BASE(_neighbor_, _lname, _uname, _stype, _sargs, _rtype, \
                       _rargs, UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR, \
                       __VA_ARGS__)

#define UCG_COLL_INIT_FUNC_SR0_RR1(_lname, _uname) \
UCG_COLL_INIT(_lname, _uname, \
              _CR, (sbuf, count, mpi_dtype), \
              _O,  (rbuf, count, mpi_dtype), \
              const void *sbuf, \
                    void *rbuf, int count, void *mpi_dtype)

#define UCG_COLL_INIT_FUNC_SR0_RRN(_lname, _uname) \
UCG_COLL_INIT(_lname, _uname, \
              _CR, (sbuf, 0,      mpi_dtype), \
              _N,  (rbuf, counts, mpi_dtype), \
              const void *sbuf, \
                    void *rbuf, const void *counts, void *mpi_dtype)

#define UCG_COLL_INIT_FUNC_SR1_RR1(_lname, _uname) \
UCG_COLL_INIT(_lname, _uname, \
              _CR, (sbuf, scount, mpi_sdtype), \
              _R,  (rbuf, rcount, mpi_rdtype), \
              const void *sbuf, int scount, void *mpi_sdtype, \
                    void *rbuf, int rcount, void *mpi_rdtype)

#define UCG_COLL_INIT_FUNC_SR1_RV1(_lname, _uname) \
UCG_COLL_INIT(_lname, _uname, \
              _CR, (sbuf, scount, mpi_sdtype), \
              _V,  (rbuf, rcount, mpi_rdtype, rdispls), \
              const void *sbuf, int scount,                     void *mpi_sdtype, \
                    void *rbuf, int rcount, const void *rdispls, void *mpi_rdtype)

#define UCG_COLL_INIT_FUNC_SR1_RVN(_lname, _uname) \
UCG_COLL_INIT(_lname, _uname, \
              _CR, (sbuf, scount,  mpi_sdtype), \
              _W,  (rbuf, rcounts, mpi_rdtype, rdispls), \
              const void *sbuf,       int  scount,                      void *mpi_sdtype, \
                    void *rbuf, const void *rcounts, const void *rdispls, void *mpi_rdtype)

#define UCG_COLL_INIT_FUNC_SVN_RR1(_lname, _uname) \
UCG_COLL_INIT(_lname, _uname, \
              _CS, (sbuf, scounts, mpi_sdtype), \
              _V,  (rbuf, rcount,  mpi_rdtype, sdispls), \
              const void *sbuf, const void *scounts, const void *sdispls, void *mpi_sdtype, \
                    void *rbuf,       int  rcount,                      void *mpi_rdtype)

/**
 *                 +--------- Sender-side
 *                 |+-------- Regular/Variadic item size
 *                 ||+------- 0 (none), 1 or N item counts
 *                 |||
 *                 ||| +----- Receiver-side
 *                 ||| |+---- Regular/Variadic item size
 *                 ||| ||+--- 1 or N item counts
 *                 ||| |||
 *                 VVV VVV
 */
UCG_COLL_INIT_FUNC_SR0_RR1(scan,                 SCAN)
UCG_COLL_INIT_FUNC_SR0_RR1(exscan,               EXSCAN)
UCG_COLL_INIT_FUNC_SR0_RR1(allreduce,            ALLREDUCE)
UCG_COLL_INIT_FUNC_SR0_RRN(reduce_scatter,       REDUCE_SCATTER)
UCG_COLL_INIT_FUNC_SR0_RR1(reduce_scatter_block, REDUCE_SCATTER_BLOCK)
UCG_COLL_INIT_FUNC_SR0_RR1(reduce,               REDUCE)
UCG_COLL_INIT_FUNC_SR1_RR1(gather,               GATHER)
UCG_COLL_INIT_FUNC_SR1_RVN(gatherv,              GATHERV)
UCG_COLL_INIT_FUNC_SR0_RR1(bcast,                BCAST)
UCG_COLL_INIT_FUNC_SR1_RR1(scatter,              SCATTER)
UCG_COLL_INIT_FUNC_SVN_RR1(scatterv,             SCATTERV)
UCG_COLL_INIT_FUNC_SR1_RR1(allgather,            ALLGATHER)
UCG_COLL_INIT_FUNC_SR1_RVN(allgatherv,           ALLGATHERV)
UCG_COLL_INIT_FUNC_SR1_RR1(alltoall,             ALLTOALL)

/**
 * Below are special functions for Alltoallv/w (and their neighborhood variant).
 * For more info see @ref enum ucg_collective_modifiers documentation.
 */

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_coll_alltoallv_init(const void *sbuf, const void *scounts, const void *sdisps,
                        void* sdtype, void *rbuf, const void *rcounts,
                        const void *rdisps, void* rdtype, void *op,
                        ucg_group_member_index_t root, unsigned modifiers,
                        ucg_group_h group, ucg_coll_h *coll_p)
{
    ucg_collective_params_t params = {
        .send = {
            UCG_COLL_PARAMS_BUF_CV(sbuf, scounts, sdtype, sdisps)
        },
        .recv = {
            UCG_COLL_PARAMS_BUF_A(rbuf, rcounts, rdtype, rdisps)
        }
        UCG_COLL_DEBUG_NAME(alltoallv)
    };

    params.send.type.modifiers |= UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC;

    return ucg_collective_create(group, &params, coll_p);
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_coll_alltoallw_init(const void *sbuf, const void *scounts,
                        const void *sdisps, void* const *sdtypes,
                        void *rbuf, const void *rcounts, const void *rdisps,
                        void* const *rdtypes, void *op,
                        ucg_group_member_index_t root, unsigned modifiers,
                        ucg_group_h group, ucg_coll_h *coll_p)
{
    const ucg_collective_params_t params = {
        .send = {
            UCG_COLL_PARAMS_BUF_CA(sbuf, scounts, sdtypes, sdisps)
        },
        .recv = {
            UCG_COLL_PARAMS_BUF_A(rbuf, rcounts, rdtypes, rdisps)
        }
        UCG_COLL_DEBUG_NAME(alltoallw)
    };

    return ucg_collective_create(group, &params, coll_p);
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_coll_neighbor_alltoallv_init(const void *sbuf, const void *scounts,
                                 const void *sdisps, void* sdtype, void *rbuf,
                                 const void *rcounts, const void *rdisps,
                                 void* rdtype, void *op,
                                 ucg_group_member_index_t root, unsigned modifiers,
                                 ucg_group_h group, ucg_coll_h *coll_p)
{
    ucg_collective_params_t params = {
        .send = {
            UCG_COLL_PARAMS_BUF_CV(sbuf, scounts, sdtype, sdisps)
        },
        .recv = {
            UCG_COLL_PARAMS_BUF_A(rbuf, rcounts, rdtype, rdisps)
        }
        UCG_COLL_DEBUG_NAME(alltoallv)
    };

    params.send.type.modifiers |= UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC |
                                  UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR;

    return ucg_collective_create(group, &params, coll_p);
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_coll_neighbor_alltoallw_init(const void *sbuf, const void *scounts,
                        const void **sdisps, void* const *sdtypes,
                        void *rbuf, const void *rcounts, const void **rdisps,
                        void* const *rdtypes, void *op,
                        ucg_group_member_index_t root, unsigned modifiers,
                        ucg_group_h group, ucg_coll_h *coll_p)
{
    ucg_collective_params_t params = {
        .send = {
            UCG_COLL_PARAMS_BUF_CW(sbuf, scounts, sdtypes, sdisps)
        },
        .recv = {
            UCG_COLL_PARAMS_BUF_AW(rbuf, rcounts, rdtypes, rdisps)
        }
        UCG_COLL_DEBUG_NAME(alltoallw)
    };

    params.send.type.modifiers |= UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR;

    return ucg_collective_create(group, &params, coll_p);
}

END_C_DECLS

#endif
