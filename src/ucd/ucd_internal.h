/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef UCD_INTERNAL_H_INCLUDED
#define UCD_INTERNAL_H_INCLUDED

#include "ucd.h"
#include "ucdconfig.h"

#define UCDI_ERR_POP(rc, fn_fail)               \
    do {                                        \
        if (rc)                                 \
            goto fn_fail;                       \
    } while (0)

extern int ucdi_yaksa_is_available;

#ifdef HAVE_YAKSA
#include "yaksa.h"

typedef struct {
    int (*info_create) (yaksa_info_t * info);
    int (*info_free) (yaksa_info_t info);
    int (*info_keyval_append) (yaksa_info_t info, const char *key, const void *val,
                               unsigned int vallen);
    int (*init) (yaksa_info_t info);
    int (*finalize) (void);
    int (*type_create_vector) (int count, int blocklength, int stride, yaksa_type_t oldtype,
                               yaksa_info_t info, yaksa_type_t * newtype);
    int (*type_create_hvector) (int count, int blocklength, intptr_t stride,
                                yaksa_type_t oldtype, yaksa_info_t info, yaksa_type_t * newtype);
    int (*type_create_contig) (int count, yaksa_type_t oldtype, yaksa_info_t info,
                               yaksa_type_t * newtype);
    int (*type_create_dup) (yaksa_type_t oldtype, yaksa_info_t info, yaksa_type_t * newtype);
    int (*type_create_indexed_block) (int count, int blocklength,
                                      const int *array_of_displacements, yaksa_type_t oldtype,
                                      yaksa_info_t info, yaksa_type_t * newtype);
    int (*type_create_hindexed_block) (int count, int blocklength,
                                       const intptr_t * array_of_displacements,
                                       yaksa_type_t oldtype, yaksa_info_t info,
                                       yaksa_type_t * newtype);
    int (*type_create_indexed) (int count, const int *array_of_blocklengths,
                                const int *array_of_displacements, yaksa_type_t oldtype,
                                yaksa_info_t info, yaksa_type_t * newtype);
    int (*type_create_hindexed) (int count, const int *array_of_blocklengths,
                                 const intptr_t * array_of_displacements,
                                 yaksa_type_t oldtype, yaksa_info_t info, yaksa_type_t * newtype);
    int (*type_create_resized) (yaksa_type_t oldtype, intptr_t lb, intptr_t extent,
                                yaksa_info_t info, yaksa_type_t * newtype);
    int (*type_create_struct) (int count, const int *array_of_blocklengths,
                               const intptr_t * array_of_displacements,
                               const yaksa_type_t * array_of_types, yaksa_info_t info,
                               yaksa_type_t * newtype);
    int (*type_create_subarray) (int ndims, const int *array_of_sizes,
                                 const int *array_of_subsizes, const int *array_of_starts,
                                 yaksa_subarray_order_e order, yaksa_type_t oldtype,
                                 yaksa_info_t info, yaksa_type_t * newtype);
    int (*type_get_size) (yaksa_type_t type, uintptr_t * size);
    int (*type_get_true_extent) (yaksa_type_t type, intptr_t * lb, intptr_t * extent);
    int (*type_get_extent) (yaksa_type_t type, intptr_t * lb, intptr_t * extent);
    int (*type_free) (yaksa_type_t type);
    int (*request_test) (yaksa_request_t request, int *completed);
    int (*request_wait) (yaksa_request_t request);
    int (*ipack) (const void *inbuf, uintptr_t incount, yaksa_type_t type, uintptr_t inoffset,
                  void *outbuf, uintptr_t max_pack_bytes, uintptr_t * actual_pack_bytes,
                  yaksa_info_t info, yaksa_request_t * request);
    int (*iunpack) (const void *inbuf, uintptr_t insize, void *outbuf, uintptr_t outcount,
                    yaksa_type_t type, uintptr_t outoffset, uintptr_t * actual_unpack_bytes,
                    yaksa_info_t info, yaksa_request_t * request);
    int (*iov_len) (uintptr_t count, yaksa_type_t type, uintptr_t * iov_len);
    int (*iov) (const char *buf, uintptr_t count, yaksa_type_t type, uintptr_t iov_offset,
                struct iovec * iov, uintptr_t max_iov_len, uintptr_t * actual_iov_len);
    int (*flatten_size) (yaksa_type_t type, uintptr_t * flattened_type_size);
    int (*flatten) (yaksa_type_t type, void *flattened_type);
    int (*unflatten) (yaksa_type_t * type, const void *flattened_type);
} ucdi_yaksa_fns_s;

extern ucdi_yaksa_fns_s ucdi_yaksa_fns;
#endif /* HAVE_YAKSA */

int ucdi_init(void);
int ucdi_finalize(void);

#endif /* UCD_INTERNAL_H_INCLUDED */
