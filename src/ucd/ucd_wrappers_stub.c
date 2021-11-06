/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include <stdio.h>
#include "ucd.h"

int ucd_info_create(ucd_info_t * info)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_info_free(ucd_info_t info)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_info_keyval_append(ucd_info_t info, const char *key, const void *val, unsigned int vallen)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_init(ucd_info_t info)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_finalize(void)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_vector(int count, int blocklength, int stride, ucd_type_t oldtype,
                           ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_hvector(int count, int blocklength, intptr_t stride, ucd_type_t oldtype,
                            ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_contig(int count, ucd_type_t oldtype, ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_dup(ucd_type_t oldtype, ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_indexed_block(int count, int blocklength, const int *array_of_displacements,
                                  ucd_type_t oldtype, ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_hindexed_block(int count, int blocklength,
                                   const intptr_t * array_of_displacements, ucd_type_t oldtype,
                                   ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_indexed(int count, const int *array_of_blocklengths,
                            const int *array_of_displacements, ucd_type_t oldtype,
                            ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_hindexed(int count, const int *array_of_blocklengths,
                             const intptr_t * array_of_displacements, ucd_type_t oldtype,
                             ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_resized(ucd_type_t oldtype, intptr_t lb, intptr_t extent,
                            ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_struct(int count, const int *array_of_blocklengths,
                           const intptr_t * array_of_displacements,
                           const ucd_type_t * array_of_types, ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_create_subarray(int ndims, const int *array_of_sizes, const int *array_of_subsizes,
                             const int *array_of_starts, ucd_subarray_order_e order,
                             ucd_type_t oldtype, ucd_info_t info, ucd_type_t * newtype)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_get_size(ucd_type_t type, uintptr_t * size)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_get_true_extent(ucd_type_t type, intptr_t * lb, intptr_t * extent)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_get_extent(ucd_type_t type, intptr_t * lb, intptr_t * extent)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_type_free(ucd_type_t type)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_request_test(ucd_request_t request, int *completed)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_request_wait(ucd_request_t request)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_ipack(const void *inbuf, uintptr_t incount, ucd_type_t type, uintptr_t inoffset,
              void *outbuf, uintptr_t max_pack_bytes, uintptr_t * actual_pack_bytes,
              ucd_info_t info, ucd_request_t * request)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_iunpack(const void *inbuf, uintptr_t insize, void *outbuf, uintptr_t outcount,
                ucd_type_t type, uintptr_t outoffset, uintptr_t * actual_unpack_bytes,
                ucd_info_t info, ucd_request_t * request)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_iov_len(uintptr_t count, ucd_type_t type, uintptr_t * iov_len)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_iov(const char *buf, uintptr_t count, ucd_type_t type, uintptr_t iov_offset,
            struct iovec *iov, uintptr_t max_iov_len, uintptr_t * actual_iov_len)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_flatten_size(ucd_type_t type, uintptr_t * flattened_type_size)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_flatten(ucd_type_t type, void *flattened_type)
{
    return UCD_ERR__NOT_SUPPORTED;
}

int ucd_unflatten(ucd_type_t * type, const void *flattened_type)
{
    return UCD_ERR__NOT_SUPPORTED;
}
