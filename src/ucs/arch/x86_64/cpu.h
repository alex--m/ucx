/**
* Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2013. ALL RIGHTS RESERVED.
* Copyright (C) ARM Ltd. 2016-2017.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCS_ASM_X86_64_H_
#define UCS_ASM_X86_64_H_

#include <ucm/util/log.h>
#include <ucs/arch/atomic.h>
#include <ucs/sys/compiler.h>
#include <ucs/arch/generic/cpu.h>
#include <ucs/sys/compiler_def.h>
#include <ucs/config/types.h>
#include <ucs/config/global_opts.h>
#include <stdint.h>
#include <string.h>

#ifdef __SSE4_1__
#  include <smmintrin.h>
#endif
#ifdef __AVX__
#  include <immintrin.h>
#endif
#if defined(__CLWB__) || defined(__CLDEMOTE__)
#  include <x86intrin.h>
#endif

BEGIN_C_DECLS

/** @file cpu.h */

#define UCS_ARCH_CACHE_LINE_SIZE 64

/**
 * In x86_64, there is strong ordering of each processor with respect to another
 * processor, but weak ordering with respect to the bus.
 */
#define ucs_memory_bus_store_fence()  asm volatile ("sfence" ::: "memory")
#define ucs_memory_bus_load_fence()   asm volatile ("lfence" ::: "memory")
#define ucs_memory_bus_cacheline_wc_flush()
#define ucs_memory_cpu_fence()        ucs_compiler_fence()
#define ucs_memory_cpu_store_fence()  ucs_compiler_fence()
#define ucs_memory_cpu_load_fence()   ucs_compiler_fence()
#define ucs_memory_cpu_wc_fence()     asm volatile ("sfence" ::: "memory")

extern ucs_ternary_auto_value_t ucs_arch_x86_enable_rdtsc;

double ucs_arch_get_clocks_per_sec();
void ucs_x86_init_tsc_freq();

ucs_cpu_model_t ucs_arch_get_cpu_model() UCS_F_NOOPTIMIZE;
ucs_cpu_flag_t ucs_arch_get_cpu_flag() UCS_F_NOOPTIMIZE;
ucs_cpu_vendor_t ucs_arch_get_cpu_vendor();
void ucs_cpu_init();
ucs_status_t ucs_arch_get_cache_size(size_t *cache_sizes);

static UCS_F_ALWAYS_INLINE int ucs_arch_x86_rdtsc_enabled()
{
    if (ucs_unlikely(ucs_arch_x86_enable_rdtsc == UCS_TRY)) {
        ucs_x86_init_tsc_freq();
        ucm_assert(ucs_arch_x86_enable_rdtsc != UCS_TRY);
    }

    return ucs_arch_x86_enable_rdtsc;
}

static UCS_F_ALWAYS_INLINE uint64_t ucs_arch_x86_read_tsc()
{
    uint32_t low, high;

    asm volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | (uint64_t)low;
}

static UCS_F_ALWAYS_INLINE uint64_t ucs_arch_read_hres_clock()
{
    if (ucs_unlikely(ucs_arch_x86_rdtsc_enabled() == UCS_NO)) {
        return ucs_arch_generic_read_hres_clock();
    }

    return ucs_arch_x86_read_tsc();
}

#define ucs_arch_wait_mem ucs_arch_generic_wait_mem

#if !HAVE___CLEAR_CACHE
static inline void ucs_arch_clear_cache(void *start, void *end)
{
    char *ptr;

    for (ptr = (char*)start; ptr < (char*)end; ptr++) {
        ucs_memory_bus_fence();
        _mm_clflush(*ptr);
        ucs_memory_bus_fence();
    }
}
#endif

static inline void ucs_arch_share_cache(void *addr)
{
    /**
     * This forces the x86 store-buffers to be flushed, so that the data becomes
     * visible from every core. Note: CLFLUSH and co. are no good here, since it
     * incurs a cache miss on all the peers (and all cache levels).
     */
#if HAVE_CLDEMOTE
    _mm_cldemote(addr);
#endif
}

static inline void *ucs_memcpy_relaxed(void *dst, const void *src, size_t len)
{
#if ENABLE_BUILTIN_MEMCPY
    if (ucs_unlikely((len > ucs_global_opts.arch.builtin_memcpy_min) &&
                     (len < ucs_global_opts.arch.builtin_memcpy_max))) {
        asm volatile ("rep movsb"
                      : "=D" (dst),
                      "=S" (src),
                      "=c" (len)
                      : "0" (dst),
                      "1" (src),
                      "2" (len)
                      : "memory");
        return dst;
    }
#endif
    return memcpy(dst, src, len);
}

#ifdef __SSE4_1__
#define _mm_load_aligned(a)       _mm_stream_load_si128((__m128i *) (a))
#define _mm_load_unaligned(a)     _mm_lddqu_si128((__m128i *) (a))
#define _mm_store(a,v)            _mm_storeu_si128((__m128i *) (a), (v))
#define _mm_store_aligned(a,v)    _mm_store_si128((__m128i *) (a),  (v))
#define _mm_store_aligned_nt(a,v) _mm_store_si128((__m128i *) (a),  (v))

static UCS_F_ALWAYS_INLINE void
ucs_memcpy_512b(void* restrict dst, int is_dst_aligned_64, int is_dst_nt,
                const void* restrict src, uintptr_t src_offset, uintptr_t len)
{
    ucs_assert(len == 64); /* Partial copy is not implemented */

#ifdef __AVX512F__
    if (src_offset == 0) {
        __m512i tmp512 = _mm512_stream_load_si512((__m512i*)src);

        if (is_dst_aligned_64) {
            if (is_dst_nt) {
                _mm512_stream_si512((__m512i*)dst, tmp512);
            } else {
                _mm512_store_si512(dst, tmp512);
            }
        } else {
            _mm512_storeu_si512(dst, tmp512);
        }

        return;
    }
#endif

    __m128i *s = (__m128i *)src;
    __m128i *d = (__m128i *)dst;
    __m128i tmp[4];

    ucm_assert(((uintptr_t)src % sizeof(__m128i)) == 0);

    tmp[0] = _mm_load_aligned(s + 0);
    tmp[1] = _mm_load_aligned(s + 1);
    tmp[2] = _mm_load_aligned(s + 2);
    tmp[3] = _mm_load_aligned(s + 3);

    if (is_dst_aligned_64) {
        ucm_assert(((uintptr_t)dst % sizeof(UCS_ARCH_CACHE_LINE_SIZE)) == 0);

        if (is_dst_nt) {
            _mm_store_aligned_nt(d + 0, tmp[0]);
            _mm_store_aligned_nt(d + 1, tmp[1]);
            _mm_store_aligned_nt(d + 2, tmp[2]);
            _mm_store_aligned_nt(d + 3, tmp[3]);
        } else {
            _mm_store_aligned(d + 0, tmp[0]);
            _mm_store_aligned(d + 1, tmp[1]);
            _mm_store_aligned(d + 2, tmp[2]);
            _mm_store_aligned(d + 3, tmp[3]);
        }
    } else {
        _mm_store(d + 0, tmp[0]);
        _mm_store(d + 1, tmp[1]);
        _mm_store(d + 2, tmp[2]);
        _mm_store(d + 3, tmp[3]);
    }
}

static UCS_F_ALWAYS_INLINE void
ucs_memcpy_128b(void* restrict dst, int is_dst_aligned_64, int is_dst_nt,
                const void* restrict src, uintptr_t src_offset, uintptr_t len)
{
    __m128i tmp = _mm_load_unaligned(src);
    memcpy(dst, UCS_PTR_BYTE_OFFSET(&tmp, src_offset), len);
    ucs_assert(src_offset + len <= 16);
}
#else
static UCS_F_ALWAYS_INLINE void
ucs_memcpy_512b(void* restrict dst, int is_dst_aligned_64, int is_dst_nt,
                const void* restrict src, uintptr_t src_offset, uintptr_t len)
{
    memcpy(dst, UCS_PTR_BYTE_OFFSET(src, src_offset), len);
}

static UCS_F_ALWAYS_INLINE void
ucs_memcpy_128b(void* restrict dst, int is_dst_aligned_64, int is_dst_nt,
                const void* restrict src, uintptr_t src_offset, uintptr_t len)
{
    memcpy(dst, UCS_PTR_BYTE_OFFSET(src, src_offset), len);
    ucs_assert(src_offset + len <= 16);
}
#endif

static UCS_F_ALWAYS_INLINE void
ucs_memcpy_nontemporal(void* restrict dst, const void* restrict src, size_t len)
{
#ifdef __SSE4_1__
    if (ucs_likely(len > 16)) {
        ucs_arch_generic_memcpy(dst, src, len);
        return;
    }
#endif
    memcpy(dst, src, len);
}

static UCS_F_ALWAYS_INLINE void
ucs_memcpy_nontemporal_cache_line(void* restrict dst, int is_dst_nt,
                                  const void* restrict src)
{
    ucm_assert(((uintptr_t)dst % UCS_ARCH_CACHE_LINE_SIZE) == 0);
    ucm_assert(((uintptr_t)src % UCS_ARCH_CACHE_LINE_SIZE) == 0);

#ifdef __SSE4_1__
    ucs_memcpy_512b(dst, 1, is_dst_nt, src, 0, 64);
#else
    memcpy(dst, src, UCS_ARCH_CACHE_LINE_SIZE);
    if (is_dst_nt) {
        ucs_arch_share_cache(dst);
    }
#endif
}

static inline int ucs_arch_cache_line_is_equal(const void* restrict a,
                                               const void* restrict b)
{
#ifdef __AVX512F__
    ucm_assert(((uintptr_t)a % UCS_ARCH_CACHE_LINE_SIZE) == 0);
    ucm_assert(((uintptr_t)b % UCS_ARCH_CACHE_LINE_SIZE) == 0);

    return (0 == _mm512_cmpneq_epu32_mask(_mm512_load_epi32(a),
                                          _mm512_load_epi32(b)));
#else
    return (0 == memcmp(a, b, UCS_ARCH_CACHE_LINE_SIZE));
#endif
}

END_C_DECLS

#endif

