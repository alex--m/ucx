/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include <stdio.h>
#include <dlfcn.h>
#include "ucdconfig.h"
#include "ucd.h"
#include "ucd_internal.h"

#ifdef HAVE_YAKSA
#include "yaksa.h"
ucdi_yaksa_fns_s ucdi_yaksa_fns = { 0 };
#endif

int ucdi_yaksa_is_available = -1;
static void *yaksa = NULL;

int ucdi_init(void)
{
    int rc = UCD_SUCCESS;

    /* if UCDI is already initialized, return */
    if (ucdi_yaksa_is_available != -1) {
        goto fn_exit;
    }
#ifdef HAVE_YAKSA
    yaksa = dlopen(LIBYAKSA, RTLD_LAZY);
    if (yaksa) {
        ucdi_yaksa_is_available = 1;

        ucdi_yaksa_fns.info_create = dlsym(yaksa, "yaksa_info_create");
        if (ucdi_yaksa_fns.info_create == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.info_free = dlsym(yaksa, "yaksa_info_free");
        if (ucdi_yaksa_fns.info_free == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.info_keyval_append = dlsym(yaksa, "yaksa_info_keyval_append");
        if (ucdi_yaksa_fns.info_keyval_append == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.init = dlsym(yaksa, "yaksa_init");
        if (ucdi_yaksa_fns.init == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.finalize = dlsym(yaksa, "yaksa_finalize");
        if (ucdi_yaksa_fns.finalize == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_vector = dlsym(yaksa, "yaksa_type_create_vector");
        if (ucdi_yaksa_fns.type_create_vector == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_hvector = dlsym(yaksa, "yaksa_type_create_hvector");
        if (ucdi_yaksa_fns.type_create_hvector == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_contig = dlsym(yaksa, "yaksa_type_create_contig");
        if (ucdi_yaksa_fns.type_create_contig == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_dup = dlsym(yaksa, "yaksa_type_create_dup");
        if (ucdi_yaksa_fns.type_create_dup == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_indexed_block = dlsym(yaksa, "yaksa_type_create_indexed_block");
        if (ucdi_yaksa_fns.type_create_indexed_block == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_hindexed_block =
            dlsym(yaksa, "yaksa_type_create_hindexed_block");
        if (ucdi_yaksa_fns.type_create_hindexed_block == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_indexed = dlsym(yaksa, "yaksa_type_create_indexed");
        if (ucdi_yaksa_fns.type_create_indexed == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_hindexed = dlsym(yaksa, "yaksa_type_create_hindexed");
        if (ucdi_yaksa_fns.type_create_hindexed == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_resized = dlsym(yaksa, "yaksa_type_create_resized");
        if (ucdi_yaksa_fns.type_create_resized == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_struct = dlsym(yaksa, "yaksa_type_create_struct");
        if (ucdi_yaksa_fns.type_create_struct == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_create_subarray = dlsym(yaksa, "yaksa_type_create_subarray");
        if (ucdi_yaksa_fns.type_create_subarray == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_get_size = dlsym(yaksa, "yaksa_type_get_size");
        if (ucdi_yaksa_fns.type_get_size == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_get_true_extent = dlsym(yaksa, "yaksa_type_get_true_extent");
        if (ucdi_yaksa_fns.type_get_true_extent == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_get_extent = dlsym(yaksa, "yaksa_type_get_extent");
        if (ucdi_yaksa_fns.type_get_extent == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.type_free = dlsym(yaksa, "yaksa_type_free");
        if (ucdi_yaksa_fns.type_free == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.request_test = dlsym(yaksa, "yaksa_request_test");
        if (ucdi_yaksa_fns.request_test == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.request_wait = dlsym(yaksa, "yaksa_request_wait");
        if (ucdi_yaksa_fns.request_wait == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.ipack = dlsym(yaksa, "yaksa_ipack");
        if (ucdi_yaksa_fns.ipack == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.iunpack = dlsym(yaksa, "yaksa_iunpack");
        if (ucdi_yaksa_fns.iunpack == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.iov_len = dlsym(yaksa, "yaksa_iov_len");
        if (ucdi_yaksa_fns.iov_len == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.iov = dlsym(yaksa, "yaksa_iov");
        if (ucdi_yaksa_fns.iov == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.flatten_size = dlsym(yaksa, "yaksa_flatten_size");
        if (ucdi_yaksa_fns.flatten_size == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.flatten = dlsym(yaksa, "yaksa_flatten");
        if (ucdi_yaksa_fns.flatten == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }

        ucdi_yaksa_fns.unflatten = dlsym(yaksa, "yaksa_unflatten");
        if (ucdi_yaksa_fns.unflatten == NULL) {
            ucdi_yaksa_is_available = 0;
            dlclose(yaksa);
        }
    }
#else
    ucdi_yaksa_is_available = 0;
#endif

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}

int ucdi_finalize(void)
{
    int rc = UCD_SUCCESS;

    if (ucdi_yaksa_is_available && yaksa) {
        dlclose(yaksa);
    }

  fn_exit:
    return rc;
  fn_fail:
    goto fn_exit;
}
