/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include <stdio.h>
#include <stdlib.h>
#include "ucd.h"
#include "ucd_internal.h"
#include "yaksa.h"

static yaksa_type_t ucdi_ucd_to_yaksa_type(ucd_type_t ucdtype)
{
    switch (ucdtype) {
        case UCD_TYPE__NULL:
            return YAKSA_TYPE__NULL;
        case UCD_TYPE___BOOL:
            return YAKSA_TYPE___BOOL;
        case UCD_TYPE__CHAR:
            return YAKSA_TYPE__CHAR;
        case UCD_TYPE__SIGNED_CHAR:
            return YAKSA_TYPE__SIGNED_CHAR;
        case UCD_TYPE__UNSIGNED_CHAR:
            return YAKSA_TYPE__UNSIGNED_CHAR;
        case UCD_TYPE__WCHAR_T:
            return YAKSA_TYPE__WCHAR_T;
        case UCD_TYPE__INT8_T:
            return YAKSA_TYPE__INT8_T;
        case UCD_TYPE__INT16_T:
            return YAKSA_TYPE__INT16_T;
        case UCD_TYPE__INT32_T:
            return YAKSA_TYPE__INT32_T;
        case UCD_TYPE__INT64_T:
            return YAKSA_TYPE__INT64_T;
        case UCD_TYPE__UINT8_T:
            return YAKSA_TYPE__UINT8_T;
        case UCD_TYPE__UINT16_T:
            return YAKSA_TYPE__UINT16_T;
        case UCD_TYPE__UINT32_T:
            return YAKSA_TYPE__UINT32_T;
        case UCD_TYPE__UINT64_T:
            return YAKSA_TYPE__UINT64_T;
        case UCD_TYPE__INT:
            return YAKSA_TYPE__INT;
        case UCD_TYPE__UNSIGNED:
            return YAKSA_TYPE__UNSIGNED;
        case UCD_TYPE__SHORT:
            return YAKSA_TYPE__SHORT;
        case UCD_TYPE__UNSIGNED_SHORT:
            return YAKSA_TYPE__UNSIGNED_SHORT;
        case UCD_TYPE__LONG:
            return YAKSA_TYPE__LONG;
        case UCD_TYPE__UNSIGNED_LONG:
            return YAKSA_TYPE__UNSIGNED_LONG;
        case UCD_TYPE__LONG_LONG:
            return YAKSA_TYPE__LONG_LONG;
        case UCD_TYPE__UNSIGNED_LONG_LONG:
            return YAKSA_TYPE__UNSIGNED_LONG_LONG;
        case UCD_TYPE__INT_FAST8_T:
            return YAKSA_TYPE__INT_FAST8_T;
        case UCD_TYPE__INT_FAST16_T:
            return YAKSA_TYPE__INT_FAST16_T;
        case UCD_TYPE__INT_FAST32_T:
            return YAKSA_TYPE__INT_FAST32_T;
        case UCD_TYPE__INT_FAST64_T:
            return YAKSA_TYPE__INT_FAST64_T;
        case UCD_TYPE__UINT_FAST8_T:
            return YAKSA_TYPE__UINT_FAST8_T;
        case UCD_TYPE__UINT_FAST16_T:
            return YAKSA_TYPE__UINT_FAST16_T;
        case UCD_TYPE__UINT_FAST32_T:
            return YAKSA_TYPE__UINT_FAST32_T;
        case UCD_TYPE__UINT_FAST64_T:
            return YAKSA_TYPE__UINT_FAST64_T;
        case UCD_TYPE__INT_LEAST8_T:
            return YAKSA_TYPE__INT_LEAST8_T;
        case UCD_TYPE__INT_LEAST16_T:
            return YAKSA_TYPE__INT_LEAST16_T;
        case UCD_TYPE__INT_LEAST32_T:
            return YAKSA_TYPE__INT_LEAST32_T;
        case UCD_TYPE__INT_LEAST64_T:
            return YAKSA_TYPE__INT_LEAST64_T;
        case UCD_TYPE__UINT_LEAST8_T:
            return YAKSA_TYPE__UINT_LEAST8_T;
        case UCD_TYPE__UINT_LEAST16_T:
            return YAKSA_TYPE__UINT_LEAST16_T;
        case UCD_TYPE__UINT_LEAST32_T:
            return YAKSA_TYPE__UINT_LEAST32_T;
        case UCD_TYPE__UINT_LEAST64_T:
            return YAKSA_TYPE__UINT_LEAST64_T;
        case UCD_TYPE__BYTE:
            return YAKSA_TYPE__BYTE;
        case UCD_TYPE__INTMAX_T:
            return YAKSA_TYPE__INTMAX_T;
        case UCD_TYPE__UINTMAX_T:
            return YAKSA_TYPE__UINTMAX_T;
        case UCD_TYPE__SIZE_T:
            return YAKSA_TYPE__SIZE_T;
        case UCD_TYPE__INTPTR_T:
            return YAKSA_TYPE__INTPTR_T;
        case UCD_TYPE__UINTPTR_T:
            return YAKSA_TYPE__UINTPTR_T;
        case UCD_TYPE__PTRDIFF_T:
            return YAKSA_TYPE__PTRDIFF_T;
        case UCD_TYPE__FLOAT:
            return YAKSA_TYPE__FLOAT;
        case UCD_TYPE__DOUBLE:
            return YAKSA_TYPE__DOUBLE;
        case UCD_TYPE__LONG_DOUBLE:
            return YAKSA_TYPE__LONG_DOUBLE;
        case UCD_TYPE__C_COMPLEX:
            return YAKSA_TYPE__C_COMPLEX;
        case UCD_TYPE__C_DOUBLE_COMPLEX:
            return YAKSA_TYPE__C_DOUBLE_COMPLEX;
        case UCD_TYPE__C_LONG_DOUBLE_COMPLEX:
            return YAKSA_TYPE__C_LONG_DOUBLE_COMPLEX;
        case UCD_TYPE__FLOAT_INT:
            return YAKSA_TYPE__FLOAT_INT;
        case UCD_TYPE__DOUBLE_INT:
            return YAKSA_TYPE__DOUBLE_INT;
        case UCD_TYPE__LONG_INT:
            return YAKSA_TYPE__LONG_INT;
        case UCD_TYPE__2INT:
            return YAKSA_TYPE__2INT;
        case UCD_TYPE__SHORT_INT:
            return YAKSA_TYPE__SHORT_INT;
        case UCD_TYPE__LONG_DOUBLE_INT:
            return YAKSA_TYPE__LONG_DOUBLE_INT;
        default:
            return ucdtype;
    }
}

static yaksa_subarray_order_e ucdi_ucd_to_yaksa_subarray_order(ucd_subarray_order_e order)
{
    if (order == UCD_SUBARRAY_ORDER__C)
        return YAKSA_SUBARRAY_ORDER__C;
    else
        return YAKSA_SUBARRAY_ORDER__FORTRAN;
}

int ucd_info_create(ucd_info_t * info)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.info_create(info);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_info_free(ucd_info_t info)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.info_free(info);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_info_keyval_append(ucd_info_t info, const char *key, const void *val, unsigned int vallen)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.info_keyval_append(info, key, val, vallen);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_init(ucd_info_t info)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.init(info);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_finalize(void)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.finalize();
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

    rc = ucdi_finalize();
    UCDI_ERR_POP(rc, fn_fail);

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_vector(int count, int blocklength, int stride, ucd_type_t oldtype,
                           ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_vector(count, blocklength, stride,
                                               ucdi_ucd_to_yaksa_type(oldtype), info, newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_hvector(int count, int blocklength, intptr_t stride, ucd_type_t oldtype,
                            ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_hvector(count, blocklength, stride,
                                                ucdi_ucd_to_yaksa_type(oldtype), info, newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_contig(int count, ucd_type_t oldtype, ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_contig(count, ucdi_ucd_to_yaksa_type(oldtype), info,
                                               newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_dup(ucd_type_t oldtype, ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_dup(ucdi_ucd_to_yaksa_type(oldtype), info, newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_indexed_block(int count, int blocklength, const int *array_of_displacements,
                                  ucd_type_t oldtype, ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_indexed_block(count, blocklength, array_of_displacements,
                                                      ucdi_ucd_to_yaksa_type(oldtype), info,
                                                      newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_hindexed_block(int count, int blocklength,
                                   const intptr_t * array_of_displacements, ucd_type_t oldtype,
                                   ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_hindexed_block(count, blocklength, array_of_displacements,
                                                       ucdi_ucd_to_yaksa_type(oldtype), info,
                                                       newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_indexed(int count, const int *array_of_blocklengths,
                            const int *array_of_displacements, ucd_type_t oldtype,
                            ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_indexed(count, array_of_blocklengths,
                                                array_of_displacements,
                                                ucdi_ucd_to_yaksa_type(oldtype), info, newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_hindexed(int count, const int *array_of_blocklengths,
                             const intptr_t * array_of_displacements, ucd_type_t oldtype,
                             ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_hindexed(count, array_of_blocklengths,
                                                 array_of_displacements,
                                                 ucdi_ucd_to_yaksa_type(oldtype), info, newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_resized(ucd_type_t oldtype, intptr_t lb, intptr_t extent,
                            ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_resized(ucdi_ucd_to_yaksa_type(oldtype), lb, extent, info,
                                                newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_struct(int count, const int *array_of_blocklengths,
                           const intptr_t * array_of_displacements,
                           const ucd_type_t * array_of_types, ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;
    yaksa_type_t *array_of_yaksa_types;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        array_of_yaksa_types = (yaksa_type_t *) malloc(count * sizeof(yaksa_type_t));
        for (int i = 0; i < count; i++) {
            array_of_yaksa_types[i] = ucdi_ucd_to_yaksa_type(array_of_types[i]);
        }

        rc = ucdi_yaksa_fns.type_create_struct(count, array_of_blocklengths, array_of_displacements,
                                               array_of_yaksa_types, info, newtype);
        UCDI_ERR_POP(rc, fn_fail);

        free(array_of_yaksa_types);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_create_subarray(int ndims, const int *array_of_sizes, const int *array_of_subsizes,
                             const int *array_of_starts, ucd_subarray_order_e order,
                             ucd_type_t oldtype, ucd_info_t info, ucd_type_t * newtype)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_create_subarray(ndims, array_of_sizes, array_of_subsizes,
                                                 array_of_starts,
                                                 ucdi_ucd_to_yaksa_subarray_order(order),
                                                 ucdi_ucd_to_yaksa_type(oldtype), info, newtype);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_get_size(ucd_type_t type, uintptr_t * size)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_get_size(ucdi_ucd_to_yaksa_type(type), size);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_get_true_extent(ucd_type_t type, intptr_t * lb, intptr_t * extent)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_get_true_extent(ucdi_ucd_to_yaksa_type(type), lb, extent);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_get_extent(ucd_type_t type, intptr_t * lb, intptr_t * extent)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_get_extent(ucdi_ucd_to_yaksa_type(type), lb, extent);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_type_free(ucd_type_t type)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.type_free(ucdi_ucd_to_yaksa_type(type));
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_request_test(ucd_request_t request, int *completed)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.request_test(request, completed);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_request_wait(ucd_request_t request)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.request_wait(request);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_ipack(const void *inbuf, uintptr_t incount, ucd_type_t type, uintptr_t inoffset,
              void *outbuf, uintptr_t max_pack_bytes, uintptr_t * actual_pack_bytes,
              ucd_info_t info, ucd_request_t * request)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.ipack(inbuf, incount, ucdi_ucd_to_yaksa_type(type), inoffset, outbuf,
                                  max_pack_bytes, actual_pack_bytes, info, request);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_iunpack(const void *inbuf, uintptr_t insize, void *outbuf, uintptr_t outcount,
                ucd_type_t type, uintptr_t outoffset, uintptr_t * actual_unpack_bytes,
                ucd_info_t info, ucd_request_t * request)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.iunpack(inbuf, insize, outbuf, outcount, ucdi_ucd_to_yaksa_type(type),
                                    outoffset, actual_unpack_bytes, info, request);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_iov_len(uintptr_t count, ucd_type_t type, uintptr_t * iov_len)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.iov_len(count, ucdi_ucd_to_yaksa_type(type), iov_len);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_iov(const char *buf, uintptr_t count, ucd_type_t type, uintptr_t iov_offset,
            struct iovec *iov, uintptr_t max_iov_len, uintptr_t * actual_iov_len)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.iov(buf, count, ucdi_ucd_to_yaksa_type(type), iov_offset, iov,
                                max_iov_len, actual_iov_len);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_flatten_size(ucd_type_t type, uintptr_t * flattened_type_size)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.flatten_size(ucdi_ucd_to_yaksa_type(type), flattened_type_size);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_flatten(ucd_type_t type, void *flattened_type)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.flatten(ucdi_ucd_to_yaksa_type(type), flattened_type);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucd_unflatten(ucd_type_t * type, const void *flattened_type)
{
    int rc = YAKSA_SUCCESS;

    rc = ucdi_init();
    UCDI_ERR_POP(rc, fn_fail);

    if (ucdi_yaksa_is_available) {
        rc = ucdi_yaksa_fns.unflatten(type, flattened_type);
        UCDI_ERR_POP(rc, fn_fail);
    } else {
        rc = UCD_ERR__NOT_SUPPORTED;
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}
