/**
* Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2015. ALL RIGHTS RESERVED.
* Copyright (C) ARM Ltd. 2016-2017.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCS_GENERIC_CPU_H_
#define UCS_GENERIC_CPU_H_

#include <ucs/sys/math.h>
#include <sys/time.h>
#include <stdint.h>


static inline uint64_t ucs_arch_generic_read_hres_clock(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
	return 0;
    }
    return ((((uint64_t)(tv.tv_sec)) * 1000000ULL) + ((uint64_t)(tv.tv_usec)));
}

static inline double ucs_arch_generic_get_clocks_per_sec()
{
    return 1.0E6;
}

static inline void ucs_arch_generic_wait_mem(void *address)
{
    /* NOP */
}

static UCS_F_ALWAYS_INLINE void
ucs_memcpy_128b(void* restrict dst, int is_dst_aligned_64, int is_dst_nt,
                const void* restrict src, uintptr_t src_offset, uintptr_t len);

static UCS_F_ALWAYS_INLINE void
ucs_memcpy_512b(void* restrict dst, int is_dst_aligned_64, int is_dst_nt,
                const void* restrict src, uintptr_t src_offset, uintptr_t len);

static UCS_F_ALWAYS_INLINE void
ucs_arch_generic_memcpy(void* restrict dst, const void* restrict src, size_t len)
{
    int is_dst_aligned_64;
    uintptr_t copy, src_misalign = (uintptr_t)src & 15;

    /* Copy unaligned portion of src */
    if (src_misalign != 0) {
        copy = ucs_min(len, 16 - src_misalign);

        ucs_memcpy_128b(dst, 0, 0, UCS_PTR_BYTE_OFFSET(src, -src_misalign),
                        src_misalign, copy);

        src = UCS_PTR_BYTE_OFFSET(src, copy);
        dst = UCS_PTR_BYTE_OFFSET(dst, copy);
        len -= copy;
    } else {
        src_misalign = (uintptr_t)src & 63;
    }

    /* Copy 64 bytes at a time */
    if (len >= 64) {
        is_dst_aligned_64 = ((uintptr_t)dst & 63) == 0;
        do {
            ucs_memcpy_512b(dst, is_dst_aligned_64, 0, src, src_misalign, 64);

            src = UCS_PTR_BYTE_OFFSET(src, 64);
            dst = UCS_PTR_BYTE_OFFSET(dst, 64);
            len -= 64;
        } while (len >= 64);
    }

    /* Copy 16 bytes at a time */
    while (len >= 16) {
        ucs_memcpy_128b(dst, 0, 0, src, 0, 16);

        src = UCS_PTR_BYTE_OFFSET(src, 16);
        dst = UCS_PTR_BYTE_OFFSET(dst, 16);
        len -= 16;
    }

    /* Copy any remaining bytes */
    if (len) {
        ucs_memcpy_128b(dst, 0, 0, src, 0, len);
    }
}

#endif
