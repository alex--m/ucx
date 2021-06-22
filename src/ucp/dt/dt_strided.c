/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "dt_strided.h"

ucs_status_t ucp_dt_make_strided(unsigned count, size_t length, size_t stride,
                                 ucp_datatype_t *datatype_p)
{
    ucp_datatype_t datatype;

    if ((length > UCS_MASK(UCP_DATATYPE_LENGTH_BITS)) ||
        (stride > UCS_MASK(UCP_DATATYPE_STRIDE_BITS)) ||
        (count  > UCS_MASK(64 - UCP_DATATYPE_SHIFT
                              - UCP_DATATYPE_LENGTH_BITS
                              - UCP_DATATYPE_STRIDE_BITS))) {
        return UCS_ERR_EXCEEDS_LIMIT;
    }

    datatype    = count;
    datatype    = stride               | (datatype << UCP_DATATYPE_STRIDE_BITS);
    datatype    = length               | (datatype << UCP_DATATYPE_LENGTH_BITS);
    *datatype_p = UCP_DATATYPE_STRIDED | (datatype << UCP_DATATYPE_SHIFT);

    return UCS_OK;
}

void ucp_dt_strided_seek(void *base, size_t stride, size_t item_length,
                         ptrdiff_t distance, size_t *item_off,
                         unsigned *item_index)
{
    *item_index += (*item_off + distance) / item_length;
    *item_off   += (distance >= 0) || ((distance % item_length) == 0) ?
                   (distance % item_length) :
                   (distance % item_length) - item_length;
}