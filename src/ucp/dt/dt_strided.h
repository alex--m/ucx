/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCP_DT_STRIDED_H_
#define UCP_DT_STRIDED_H_

#include "dt_contig.h"

/*
 * The strided datatype consists of the following four parts (LSB->MSB):
 * 1. type (3 bits, has to be equal to @ref UCP_DATATYPE_STRIDED )
 * 2. length (how long is each element, in bytes)
 * 3. stride (distance between the start of two consecutive elements, in bytes)
 * 4. amount of elements (the rest of the bits in a 64-bit type)
 */
#define UCP_DATATYPE_LENGTH_BITS (16)
#define UCP_DATATYPE_STRIDE_BITS (16)

#define UCP_DT_IS_STRIDED(_datatype) \
          (((_datatype) & UCP_DATATYPE_CLASS_MASK) == UCP_DATATYPE_STRIDED)

static inline size_t ucp_strided_dt_elem_size(ucp_datatype_t datatype)
{
    return (datatype >> UCP_DATATYPE_SHIFT) &
           UCS_MASK(UCP_DATATYPE_LENGTH_BITS);
}

static inline size_t ucp_strided_dt_elem_stride(ucp_datatype_t datatype)
{
    return (datatype >> (UCP_DATATYPE_SHIFT + UCP_DATATYPE_LENGTH_BITS)) &
           UCS_MASK(UCP_DATATYPE_STRIDE_BITS);
}

static inline size_t ucp_strided_dt_elem_amount(ucp_datatype_t datatype)
{
    return (datatype >> (UCP_DATATYPE_SHIFT +
                         UCP_DATATYPE_LENGTH_BITS +
                         UCP_DATATYPE_STRIDE_BITS));
}

static inline void ucp_strided_dt_elem_info(ucp_datatype_t datatype,
                                            size_t *size, size_t *stride,
                                            unsigned *amount)
{
    ucs_assert(UCP_DT_IS_STRIDED(datatype));

    datatype >>= UCP_DATATYPE_SHIFT;
    *size      = datatype & UCS_MASK(UCP_DATATYPE_LENGTH_BITS);

    datatype >>= UCP_DATATYPE_LENGTH_BITS;
    *stride    = datatype & UCS_MASK(UCP_DATATYPE_STRIDE_BITS);

    datatype >>= UCP_DATATYPE_STRIDE_BITS;
    *amount    = (unsigned)datatype;
}

static inline size_t ucp_strided_dt_length(ucp_datatype_t datatype, size_t count)
{
    unsigned elem_cnt;
    size_t size, stride;

    ucp_strided_dt_elem_info(datatype, &size, &stride, &elem_cnt);

    return count * size * elem_cnt;
}

static inline void
ucp_dt_strided_pack(ucp_datatype_t datatype, ucp_worker_h worker, void *dest,
                    const void *src, unsigned dtcnt, ucs_memory_type_t mem_type)
{
    unsigned idx, cnt;
    size_t size, stride;

    ucp_strided_dt_elem_info(datatype, &size, &stride, &cnt);

    for (idx = 0 ; idx < (cnt * dtcnt); idx++) {
        ucp_dt_contig_pack(worker, dest, src, size, mem_type);
        dest = UCS_PTR_BYTE_OFFSET(dest, stride);
        src = UCS_PTR_BYTE_OFFSET(src, size);
    }
}

/**
 * Seek to a logical offset in the strided buffer
 *
 * @param [in]     buffer         Buffer to seek in
 * @param [in]     stride         Number of entries in the @a buffer
 * @param [in]     item_length    Number of entries in the @a buffer
 * @param [in]     distance       Distance to move, relative to the current
 *                                location
 * @param [inout]  item_offset    The offset in bytes from the beginning of the
 *                                current strided item
 * @param [inout]  item_index     Current strided item index
 */
void ucp_dt_strided_seek(void *base, size_t stride, size_t item_length,
                         ptrdiff_t distance, size_t *item_off,
                         unsigned *item_index);

#endif
