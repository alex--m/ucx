/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "mm_coll_ep.h"

#include <ucs/arch/atomic.h>
#include <ucs/async/async.h>
#include <ucs/type/init_once.h>

#ifdef _DEBUG
#define UCS_F_DEBUG_OR_INLINE inline
#else
#define UCS_F_DEBUG_OR_INLINE UCS_F_ALWAYS_INLINE
#endif

/*
 * Prefetch is done after each send (with non-zero length). The assumption is
 * that it would take a while to get a response, and this time is better spent
 * prefetching instead of a plain progress busy-loop. There's probably no optimal
 * number to put here, but as long as there's plenty of progress time this number
 * can be increased for better latency.
 */
#define MM_COLL_EP_MAX_PREFETCH (512)

enum uct_mm_coll_ep_reduce_cb_type {
    UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL = 0,
    UCT_MM_COLL_EP_REDUCE_CB_TYPE_EXTERNAL,
    UCT_MM_COLL_EP_REDUCE_CB_TYPE_EXTERNAL_AUX
};

static UCS_F_DEBUG_OR_INLINE size_t
uct_mm_coll_ep_get_offset_by_size_and_index(int is_short, int is_incast,
                                            unsigned short_size,
                                            unsigned bcopy_size,
                                            unsigned short_slot_size,
                                            unsigned bcopy_slot_size,
                                            uint8_t proc_cnt,
                                            uint32_t offset_id,
                                            size_t *slot_size_p)
{
    size_t slot_size, total_size;

    if (is_short) {
        total_size = short_size;
        slot_size  = *slot_size_p = short_slot_size;
    } else {
        total_size = bcopy_size - ((is_incast && !offset_id) * sizeof(uint64_t));
        slot_size  = *slot_size_p = bcopy_slot_size;
    }

    ucs_assert(proc_cnt > offset_id);
    ucs_assert(total_size >= (slot_size * (proc_cnt - offset_id)));
    return total_size - (slot_size * (proc_cnt - offset_id));
}

static UCS_F_DEBUG_OR_INLINE size_t
uct_mm_coll_ep_get_slot_offset_by_id(const uct_mm_coll_ep_t *ep, int is_short,
                                     int is_incast, int is_collaborative,
                                     uint16_t elem_slot, uint32_t seg_slot,
                                     uint8_t offset_id, size_t *slot_size_p)
{
    return uct_mm_coll_ep_get_offset_by_size_and_index(is_short, is_incast,
                                                       ep->elem_size -
                                                       sizeof(uct_mm_coll_fifo_element_t),
                                                       ep->seg_size, elem_slot,
                                                       seg_slot, ep->sm_proc_cnt
                                                       + is_collaborative,
                                                       offset_id, slot_size_p);
}

static UCS_F_DEBUG_OR_INLINE size_t
uct_mm_coll_ep_get_slot_offset(const uct_mm_coll_ep_t *ep, int is_short,
                               int is_incast, int is_collaborative,
                               int use_custom_offset_id, uint8_t offset_id,
                               uct_coll_length_info_t len_info,
                               size_t payload_length, size_t *slot_size_p)
{
    if (use_custom_offset_id) {
        return uct_mm_coll_ep_get_slot_offset_by_id(ep, is_short, is_incast,
                                                    is_collaborative,
                                                    ep->elem_slot, ep->seg_slot,
                                                    offset_id, slot_size_p);
    }

    if (ucs_likely(len_info == UCT_COLL_LENGTH_INFO_DEFAULT)) {
        if (ucs_likely(is_short)) {
            *slot_size_p = ep->elem_slot;
            return ep->elem_offset;
        }

        *slot_size_p = ep->seg_slot;
        return ep->seg_offset;
    }

    return uct_mm_coll_ep_get_slot_offset_by_id(ep, is_short, is_incast,
                                                is_collaborative, payload_length,
                                                payload_length, ep->offset_id,
                                                slot_size_p);
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
uct_mm_coll_ep_get_data_ptr(int is_short, uct_mm_coll_fifo_element_t *elem,
                            uct_mm_coll_ep_t *ep, int is_loopback, int is_incast,
                            int is_collaborative, int use_custom_offset_id,
                            uint8_t offset_id, uct_coll_length_info_t len_info,
                            size_t payload_length, uint8_t **base_ptr_p,
                            size_t *slot_offset_p, size_t *slot_size_p)
{
    void *seg_address;
    ucs_status_t status;

    if (is_short) {
        *base_ptr_p    = (uint8_t*)(elem + 1);
        *slot_offset_p = uct_mm_coll_ep_get_slot_offset(ep, 1, is_incast,
                                                        is_collaborative,
                                                        use_custom_offset_id,
                                                        offset_id, len_info,
                                                        payload_length,
                                                        slot_size_p);
        return UCS_OK;
    }

    if (is_loopback) {
        *slot_size_p   = ep->seg_slot;
        *base_ptr_p    = elem->super.desc_data;
        *slot_offset_p = !use_custom_offset_id ? 0 :
                         uct_mm_coll_ep_get_slot_offset(ep, 0, is_incast,
                                                        is_collaborative, 1,
                                                        offset_id, len_info,
                                                        payload_length,
                                                        slot_size_p);
        return UCS_OK;
    }

    ucs_assert(ep != NULL);
    status = uct_mm_ep_get_remote_seg(ucs_derived_of(ep, uct_mm_ep_t),
                                      elem->super.desc.seg_id,
                                      elem->super.desc.seg_size, &seg_address);
    ucs_assert(status == UCS_OK);
    if (ucs_unlikely(status != UCS_OK)) {
        /* Make the compiler happy: */
        *slot_offset_p = 0;
        *slot_size_p   = 0;
        *base_ptr_p    = NULL;
        return status;
    }

    ucs_assert(seg_address != NULL);
    VALGRIND_MAKE_MEM_DEFINED(seg_address, elem->super.desc.seg_size -
                                           elem->super.desc.offset);

    *base_ptr_p    = UCS_PTR_BYTE_OFFSET(seg_address, elem->super.desc.offset);
    *slot_offset_p = uct_mm_coll_ep_get_slot_offset(ep, 0, is_incast,
                                                    is_collaborative,
                                                    use_custom_offset_id,
                                                    offset_id, len_info,
                                                    payload_length, slot_size_p);
    return UCS_OK;
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_ep_get_base_ptr(int is_short, int is_incast, int is_collaborative,
                            uct_mm_coll_fifo_element_t *elem,
                            uct_mm_coll_ep_t *ep, uint8_t **base_ptr_p)
{
    size_t ignored;
    ucs_assert_always(UCS_OK ==
                      uct_mm_coll_ep_get_data_ptr(is_short, elem, ep, 1,
                                                  is_incast, is_collaborative,
                                                  0, 0,
                                                  UCT_COLL_LENGTH_INFO_PACKED,
                                                  0, base_ptr_p, &ignored,
                                                  &ignored));
}

static UCS_F_DEBUG_OR_INLINE uint8_t*
uct_mm_coll_iface_slotted_get_slot_counter(uint8_t *slot_ptr,
                                           unsigned slot_size)
{
    return UCS_PTR_BYTE_OFFSET(slot_ptr, slot_size - 1);
}

#define UCT_MM_COLL_EP_PTR_ROUND_UP_TO_CACHE_LINE(_ptr) \
    (UCS_PTR_BYTE_OFFSET(_ptr, (UCS_SYS_CACHE_LINE_SIZE - \
                                ((uintptr_t)(_ptr) % UCS_SYS_CACHE_LINE_SIZE))))

/* In order to avoid the compiler's out-of-bounds error, we allocate more */
#define ARRAY_SIZE (UCS_SYS_CACHE_LINE_SIZE + \
                    (sizeof(uint64_t) * UCT_MM_COLL_MAX_COUNT_SUPPORTED))
#define DEFINE_ARRAY(_ntb) \
    UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE) uint8_t _ntb [ARRAY_SIZE]; \
    VALGRIND_MAKE_MEM_DEFINED(_ntb, ARRAY_SIZE);
#define GET_FLAG_ARRAY(_ntb) \
    ((uint8_t*)_ntb)[UCS_SYS_CACHE_LINE_SIZE - 1]
#define SET_FLAG_ARRAY(_ntb, _flag) \
    GET_FLAG_ARRAY(_ntb) = _flag
#ifdef __AVX512F__
#define DEFINE_CACHELINE(_ntb) UCS_V_UNUSED __m512i _ntb
#define SET_FLAG_CACHELINE(_ntb, _flag) _ntb = _mm512_set1_epi8(_flag)
#define uct_mm_coll_ep_memcpy_cl_nt_opt(_use_nt_cl, _nt512b, _dest_ptr) \
    if (_use_nt_cl) _mm512_store_si512(_dest_ptr, _nt512b);
#else
#define DEFINE_CACHELINE(_ntb) DEFINE_ARRAY(_ntb)
#define SET_FLAG_CACHELINE(_ntb, _flag) SET_FLAG_ARRAY(_ntb, _flag)
#define uct_mm_coll_ep_memcpy_cl_nt_opt(_use_nt_cl, _nt_ptr, _dest_ptr) \
    uct_mm_coll_ep_memcpy_cl_nt_no_opt(_use_nt_cl, _nt_ptr, _dest_ptr, \
                                       UCS_SYS_CACHE_LINE_SIZE - 1)
#endif

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_ep_memcpy_cl_nt_no_opt(int use_nt_cl,
                                   void* restrict nontemporal_ptr,
                                   void* restrict dest_ptr,
                                   size_t length)
{
    uint8_t tmp;
    if (!use_nt_cl) {
        return;
    }

    ucs_assert(length < UCS_SYS_CACHE_LINE_SIZE);

#ifdef __AVX512F__
    DEFINE_CACHELINE(src);
    tmp = GET_FLAG_ARRAY(nontemporal_ptr);
    SET_FLAG_CACHELINE(src, GET_FLAG_ARRAY(dest_ptr));
    uct_mm_coll_ep_memcpy_cl_nt_opt(1, src, dest_ptr);
#else
    uint8_t *src = __builtin_assume_aligned(nontemporal_ptr,
                                            UCS_SYS_CACHE_LINE_SIZE);
    tmp = GET_FLAG_ARRAY(src);
    memcpy(dest_ptr, src, length);
#endif

    ucs_memory_cpu_store_fence();
    SET_FLAG_ARRAY(dest_ptr, tmp);
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_iface_slotted_set_slot_counter_nontemporal(uint8_t *cache_line_ptr,
                                                       uint8_t new_counter_value)
{
/* Some CPU architectures namely ARMv9 (e.g. Graviton and Cobalt) prefer this */
#if HAVE_BENEFIT_FOR_FULL_CACHE_LINES
    DEFINE_CACHELINE(ntb);

    SET_FLAG_CACHELINE(ntb, new_counter_value);
    /* WARNING: for AVX512, this sets all 64 bytes! */

    uct_mm_coll_ep_memcpy_cl_nt_opt(1, ntb,
                                    __builtin_assume_aligned(cache_line_ptr,
                                                             UCS_SYS_CACHE_LINE_SIZE));
#else
    SET_FLAG_ARRAY(cache_line_ptr, new_counter_value);
#endif
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_iface_slotted_set_slot_counter_unchecked(uint8_t *slot_ptr,
                                                     unsigned slot_size,
                                                     uint8_t new_counter_value,
                                                     int use_store_fence_before,
                                                     int use_store_fence_after)
{
    volatile uint8_t *slot_counter =
        uct_mm_coll_iface_slotted_get_slot_counter(slot_ptr, slot_size);

    if (use_store_fence_before) {
        ucs_memory_cpu_store_fence();
    }

    *slot_counter = new_counter_value;

    if (use_store_fence_after) {
        ucs_share_cache((void*)slot_counter);
        ucs_memory_cpu_store_fence();
    }
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_iface_slotted_set_slot_counter(uint8_t *slot_ptr,
                                           unsigned slot_size,
                                           uint8_t new_counter_value,
                                           int use_store_fence_before,
                                           int use_store_fence_after)
{
    ucs_assert(new_counter_value !=
               *uct_mm_coll_iface_slotted_get_slot_counter(slot_ptr, slot_size));
    uct_mm_coll_iface_slotted_set_slot_counter_unchecked(slot_ptr, slot_size,
                                                         new_counter_value,
                                                         use_store_fence_before,
                                                         use_store_fence_after);
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_iface_cb_reduce_timestamp(uint64_t *dst, const uint64_t *src)
{
    uint64_t dst_ts = *dst;
    uint64_t src_ts = *src;
    if (dst_ts > src_ts) {
        *dst = src_ts;
    }
}

static UCS_F_DEBUG_OR_INLINE size_t
uct_mm_coll_ep_am_common_reduce_ext(void *dst_ptr, const void *src_ptr,
                                    const uct_mm_coll_iface_cb_ctx_t *reduce_ctx,
                                    int use_aux_cb, int use_timestamp)
{
    uct_mm_base_incast_ext_t *rctx        = reduce_ctx->rctx;
    uct_mm_base_incast_ext_cb_t *mode     = use_aux_cb ? &rctx->aux : &rctx->def;
    uct_reduction_external_cb_t reduce_cb = mode->cb;

    /* Perform the reduction */
    reduce_cb(rctx->operator, src_ptr, dst_ptr, mode->operand_count,
              rctx->datatype, rctx->md);

    if (use_timestamp) {
        dst_ptr = UCS_PTR_BYTE_OFFSET(dst_ptr, mode->total_size);
        src_ptr = UCS_PTR_BYTE_OFFSET(src_ptr, mode->total_size);
        uct_mm_coll_iface_cb_reduce_timestamp((uint64_t*)dst_ptr,
                                              (const uint64_t*)src_ptr);
    }

    return mode->total_size;
}

static size_t
uct_mm_coll_ep_am_common_reduce_ext_ts(void *dst, const void *src,
                                       size_t reduce_ctx)
{
    return uct_mm_coll_ep_am_common_reduce_ext(dst, src,
        (const uct_mm_coll_iface_cb_ctx_t*)reduce_ctx, 0, 1);
}

static size_t
uct_mm_coll_ep_am_common_reduce_ext_no_ts(void *dst, const void *src,
                                          size_t reduce_ctx)
{
    return uct_mm_coll_ep_am_common_reduce_ext(dst, src,
        (const uct_mm_coll_iface_cb_ctx_t*)reduce_ctx, 0, 0);
}

static size_t
uct_mm_coll_ep_am_common_reduce_ext_aux_ts(void *dst, const void *src,
                                           size_t reduce_ctx)
{
    return uct_mm_coll_ep_am_common_reduce_ext(dst, src,
        (const uct_mm_coll_iface_cb_ctx_t*)reduce_ctx, 1, 1);
}

static size_t
uct_mm_coll_ep_am_common_reduce_ext_aux_no_ts(void *dst, const void *src,
                                              size_t reduce_ctx)
{
    return uct_mm_coll_ep_am_common_reduce_ext(dst, src,
        (const uct_mm_coll_iface_cb_ctx_t*)reduce_ctx, 1, 0);
}

static UCS_F_DEBUG_OR_INLINE size_t
uct_mm_coll_ep_common_packer(void *dst, const void *src, int is_first)
{
    const uct_mm_coll_iface_cb_ctx_t *ctx = (typeof(ctx))src;
    uintptr_t pack_cb_arg                 = (uintptr_t)ctx->pack_cb_arg;

    if (!is_first) {
        ucs_assert((pack_cb_arg & UCT_PACK_CALLBACK_REDUCE) == 0);
        pack_cb_arg |= UCT_PACK_CALLBACK_REDUCE;
    }

    return ctx->pack_cb(dst, (void*)pack_cb_arg);
}

static size_t
uct_mm_coll_ep_wrap_packer(void *dst, const void *src, size_t zero_ep_offset_id)
{
    return uct_mm_coll_ep_common_packer(dst, src, (int)zero_ep_offset_id);
}

static size_t
uct_mm_coll_ep_memcpy_packer(void *dst, const void *src, size_t length)
{
    return uct_mm_coll_ep_common_packer(dst, src, 1);
}

static size_t
uct_mm_coll_ep_reduce_packer(void *dst, const void *src, size_t length)
{
    return uct_mm_coll_ep_common_packer(dst, src, 0);
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_ep_am_common_reduce(enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                                const uct_mm_coll_iface_cb_t reduce_cb,
                                const void *reduce_ctx, void *dst_ptr,
                                const void *src_ptr, size_t length,
                                int use_timestamp, size_t *posted_length_p)
{
    int is_reduce_internal = (reduce_cb_type ==
                              UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL);
    if (!is_reduce_internal) {
        ucs_assert((reduce_cb == uct_mm_coll_ep_am_common_reduce_ext_ts)     ||
                   (reduce_cb == uct_mm_coll_ep_am_common_reduce_ext_no_ts)  ||
                   (reduce_cb == uct_mm_coll_ep_am_common_reduce_ext_aux_ts) ||
                   (reduce_cb == uct_mm_coll_ep_am_common_reduce_ext_aux_no_ts));
        ucs_assert(length <= (sizeof(uint64_t) +
                   ((reduce_cb_type == UCT_MM_COLL_EP_REDUCE_CB_TYPE_EXTERNAL) ?
                    ((uct_mm_coll_iface_cb_ctx_t*)reduce_ctx)->rctx->def.total_size :
                    ((uct_mm_coll_iface_cb_ctx_t*)reduce_ctx)->rctx->aux.total_size)));
    }

    /* Make the counter protect loads from that slot */
    ucs_memory_cpu_load_fence();

    /* Do the reduction (via callback function) */
    *posted_length_p = reduce_cb(dst_ptr, src_ptr,
                                 is_reduce_internal ? length :
                                                      (size_t)reduce_ctx);
}

static UCS_F_DEBUG_OR_INLINE size_t
uct_mm_coll_ep_common_memcpy(int is_short, int is_bcast, uint8_t *slot_ptr,
                             const void *payload_or_pack_cb_arg,
                             int nonzero_ep_offset_id,
                             enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                             int is_collaborative, size_t length,
                             const uct_mm_coll_iface_cb_t memcpy_cb)
{
    int use_offset_id = (reduce_cb_type || is_collaborative) && !is_short;
    if (use_offset_id && is_short) {
        ucs_assert((memcpy_cb == (const uct_mm_coll_iface_cb_t)uct_mm_coll_ep_wrap_packer) ||
                   (memcpy_cb == (const uct_mm_coll_iface_cb_t)uct_mm_coll_ep_memcpy_packer));
    }

    return memcpy_cb(slot_ptr, payload_or_pack_cb_arg,
                     (use_offset_id ? !nonzero_ep_offset_id : length));
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_ep_am_common_reduce_or_memcpy(int condition, int use_timestamp,
                                          int is_short, int is_bcast,
                                          int nonzero_ep_offset_id,
                                          int nonzero_length,
                                          enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                                          const uct_mm_coll_iface_cb_t memcpy_cb,
                                          const uct_mm_coll_iface_cb_t reduce_cb,
                                          void *base_ptr, size_t *length_p,
                                          const void *payload_or_pack_cb_arg)
{
    const uct_mm_coll_iface_cb_ctx_t *reduce_ctx;

    if (!nonzero_length) {
        return;
    }

    if (condition) {
        if (reduce_cb_type == UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL) {
            reduce_ctx = NULL;
        } else {
            reduce_ctx             = (typeof(reduce_ctx))payload_or_pack_cb_arg;
            payload_or_pack_cb_arg = reduce_ctx->pack_cb_arg;
        }

        uct_mm_coll_ep_am_common_reduce(reduce_cb_type, reduce_cb, reduce_ctx,
                                        base_ptr, payload_or_pack_cb_arg,
                                        *length_p, use_timestamp, length_p);
    } else {
        *length_p = uct_mm_coll_ep_common_memcpy(is_short, is_bcast, base_ptr,
                                                 payload_or_pack_cb_arg,
                                                 nonzero_ep_offset_id,
                                                 reduce_cb_type,
                                                 0, *length_p, memcpy_cb);
    }
}

static UCS_F_DEBUG_OR_INLINE size_t
uct_mm_coll_ep_collaborative_memcpy(int is_short, int is_bcast, uint8_t *slot_ptr,
                                    const void *payload_or_pack_cb_arg,
                                    int nonzero_ep_offset_id, int nonzero_length,
                                    enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                                    size_t length, const uct_mm_coll_iface_cb_t memcpy_cb)
{
    if (!nonzero_length) {
        return 0;
    }

    return uct_mm_coll_ep_common_memcpy(is_short, is_bcast, slot_ptr,
                                        payload_or_pack_cb_arg,
                                        nonzero_ep_offset_id,
                                        reduce_cb_type, 1,
                                        length, memcpy_cb);
}

static UCS_F_DEBUG_OR_INLINE uint8_t
uct_mm_coll_ep_collaborative_reduction(uint8_t *base_ptr, uint8_t *slot_ptr,
                                       size_t slot_size, uint8_t pending,
                                       uint8_t procs, int use_timestamp,
                                       int nonzero_length, int use_nt_cl,
                                       enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                                       const uct_mm_coll_iface_cb_t reduce_cb,
                                       const void *reduce_ctx,
                                       size_t length)
{
    uint8_t cnt;
    size_t ignored;
    uint8_t *flag_ptr = uct_mm_coll_iface_slotted_get_slot_counter(slot_ptr,
                                                                   slot_size);

    while ((cnt = *flag_ptr) != 0) {

        /* Reduce that new slot into the first (base) one */
        if (nonzero_length) {
            /* Reduce the contents from the next slot into the base-address */
            uct_mm_coll_ep_am_common_reduce(reduce_cb_type, reduce_cb,
                                            reduce_ctx, base_ptr,
                                            slot_ptr, length, use_timestamp,
                                            &ignored);
        }

        /* Mark that new slot as empty (for recycling purposes) */
        uct_mm_coll_iface_slotted_set_slot_counter(slot_ptr, slot_size, 0, 0, 0);

        pending += cnt;
        ucs_assert(pending <= procs);
        if (pending == procs) {
            return pending;
        }

        slot_ptr = UCS_PTR_BYTE_OFFSET(slot_ptr, cnt * slot_size);
        flag_ptr = uct_mm_coll_iface_slotted_get_slot_counter(slot_ptr,
                                                              slot_size);
    }

    return pending;
}

static UCS_F_DEBUG_OR_INLINE int
uct_mm_coll_ep_collaborative_is_elem_ready(uint8_t *base_ptr, size_t slot_offset,
                                           size_t slot_size, uct_mm_coll_ep_t *ep,
                                           int is_short, int is_bcast,
                                           int use_timestamp,
                                           int nonzero_ep_offset_id,
                                           int nonzero_length,
                                           enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                                           const uct_mm_coll_iface_cb_t reduce_cb,
                                           const void *reduce_ctx,
                                           size_t length, uint8_t old_pending,
                                           int use_nt_cl, uint8_t *ntb_ptr,
                                           int update_elem_pending,
                                           volatile uint32_t *elem_pending_p)
{
    uint8_t pending;
    uint8_t *slot_ptr;
    uint8_t proc_cnt = ep->sm_proc_cnt;

    ucs_assert(reduce_cb != NULL);
    ucs_assert(old_pending <= proc_cnt);
    pending  = old_pending ? old_pending : 1;
    slot_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, slot_offset);

    /* In buffered-copy, the first slot also contains the 8B header */
    if (!is_short && !is_bcast) {
        if (!nonzero_ep_offset_id) {
            base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, sizeof(uint64_t));
        }
        length -= sizeof(uint64_t);
    }

    /* First, aggregate all the completed slots adjacent to the current one */
    pending = uct_mm_coll_ep_collaborative_reduction(use_nt_cl ?
                                                     ntb_ptr : base_ptr,
                                                     slot_ptr, slot_size,
                                                     pending, proc_cnt,
                                                     use_timestamp,
                                                     nonzero_length, use_nt_cl,
                                                     reduce_cb_type, reduce_cb,
                                                     reduce_ctx,
                                                     length);

    if (ucs_likely((pending == proc_cnt) && !nonzero_ep_offset_id)) {
        if (nonzero_length) {
            uct_mm_coll_ep_memcpy_cl_nt_no_opt(use_nt_cl, ntb_ptr, base_ptr, length);
        }
        ucs_assert(!nonzero_ep_offset_id);
        ucs_assert(update_elem_pending);
        return 1;
    }

    /* If not all slots were aggregated - update pending field */
    ucs_assert(pending < proc_cnt);
    if (pending != old_pending) {
        if (update_elem_pending) {
            ucs_assert(!nonzero_ep_offset_id);
            *elem_pending_p = pending;
        } else if (use_nt_cl && !nonzero_length) {
            uct_mm_coll_iface_slotted_set_slot_counter_nontemporal(base_ptr, pending);
            return 0;
        } else {
            /* Set the slot's counter value */
            uct_mm_coll_iface_slotted_set_slot_counter_unchecked(use_nt_cl ?
                                                                 ntb_ptr :
                                                                 base_ptr,
                                                                 slot_size,
                                                                 pending,
                                                                 !use_nt_cl &&
                                                                 nonzero_length, 0);
        }

        /* For non-temporal usage - overwrite the slot with the 64B register */
        uct_mm_coll_ep_memcpy_cl_nt_no_opt(use_nt_cl, ntb_ptr, base_ptr, length);
    }

    return 0;
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_ep_collaborative_send(uct_mm_coll_fifo_element_t *elem,
                                  uct_mm_coll_ep_t *ep, uint8_t *slot_ptr,
                                  size_t slot_size, int is_short,
                                  int use_timestamp, int is_bcast,
                                  int set_slot_counter, int nonzero_ep_offset_id,
                                  int nonzero_length,
                                  enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                                  const uct_mm_coll_iface_cb_t memcpy_cb,
                                  const uct_mm_coll_iface_cb_t reduce_cb,
                                  const void *payload_or_pack_cb_arg,
                                  int use_nt_cl, int wait_all_procs,
                                  int update_length, size_t *length_p)
{
    uint32_t pending;
    size_t slot_offset;

    DEFINE_ARRAY(ntb);

#ifdef _DEBUG
    memset(ntb, 0, sizeof(ntb));
#endif

    /* First value written regardless of neighbors (NOP for bcast completion) */
    size_t length = (nonzero_length && update_length) ? *length_p : 0;
    length        = uct_mm_coll_ep_collaborative_memcpy(is_short, is_bcast,
                                                        use_nt_cl ?
                                                            ntb : slot_ptr,
                                                        payload_or_pack_cb_arg,
                                                        nonzero_ep_offset_id,
                                                        nonzero_length,
                                                        reduce_cb_type,
                                                        length, memcpy_cb);

    ucs_assert(!is_bcast || (slot_size == UCS_SYS_CACHE_LINE_SIZE));
    ucs_assert(!is_bcast || !((uintptr_t)slot_ptr % UCS_SYS_CACHE_LINE_SIZE));

    if (!set_slot_counter) {
        /* When broadcasting - this will be the only (data) slot anyway */
        ucs_assert(is_bcast);
        if (nonzero_length) {
            /* This is not a barrier - actually write the data! */
            uct_mm_coll_ep_memcpy_cl_nt_no_opt(use_nt_cl, ntb, slot_ptr, length);
        }
        if (update_length) {
            *length_p = length;
        }
        return;
    }

    if (!is_short) {
        ucs_assert(!use_nt_cl);
        if (update_length) {
            ucs_assert(length > sizeof(uint64_t));
            *length_p = length;
        }
    }

    pending     = 0;
    slot_offset = slot_size;
    do {
        if (uct_mm_coll_ep_collaborative_is_elem_ready(slot_ptr, slot_offset,
                                                       slot_size, ep, is_short,
                                                       is_bcast, use_timestamp,
                                                       nonzero_ep_offset_id,
                                                       nonzero_length,
                                                       reduce_cb_type, reduce_cb,
                                                       payload_or_pack_cb_arg,
                                                       length, pending,
                                                       use_nt_cl, ntb,
                                                       !nonzero_ep_offset_id,
                                                       &pending)) {
            ucs_assert(!nonzero_ep_offset_id);
            pending = ep->sm_proc_cnt;
            break;
        }

        /* Proceed to the next slot (if the loop is employed) */
        slot_offset = pending * slot_size;

    /*
     * Because of UCX API, progress function calls handling incoming packets
     * cannot pass a callback function to use for reduction. This means the
     * root cannot reduce unless (a) the operator and operand are given, or
     * (b) a global, external reduce function is provided for this occasion.
     * For bcopy with explicit packer functions, such as those used for the
     * zero-copy reduction optimization, we promote the sender with the
     * lowest ID to perform the necessary reductions (and the waiting).
     * In most cases, however, this loop body is only executed once.
     *
     * PROBLEM: this defies MPI because it doesn't call progress while waiting.
     */
    } while (wait_all_procs);

    if (!nonzero_ep_offset_id) {
        ucs_assert(set_slot_counter);
        ucs_memory_cpu_store_fence();
        elem->pending = pending;
    }
}

static UCS_F_DEBUG_OR_INLINE int
uct_mm_coll_ep_flagged_slots_is_elem_ready(uint8_t *slot_ptr, size_t slot_size,
                                           uct_mm_coll_ep_t *ep, int is_short,
                                           int use_timestamp, uint8_t owner_flag,
                                           uint8_t old_pending,
                                           volatile uint32_t *elem_pending)
{
    uint8_t *flag_ptr = uct_mm_coll_iface_slotted_get_slot_counter(slot_ptr,
                                                                   slot_size);
    uint8_t slot_flag = *flag_ptr;
    ucs_assert((slot_flag & ~UCT_MM_FIFO_ELEM_FLAG_OWNER) == 0);

    /* Count adjacent completed slots - so it can be skipped in the next test */
    while (slot_flag != owner_flag) {
        ucs_assert(slot_flag == !owner_flag);
        ucs_assert(old_pending < ep->sm_proc_cnt);
        if (ucs_likely(++old_pending == ep->sm_proc_cnt)) {
            /* Make the counters protect all the slots */
            ucs_memory_cpu_load_fence();
            return 1;
        }

        flag_ptr  = UCS_PTR_BYTE_OFFSET(flag_ptr, slot_size);
        slot_flag = *flag_ptr;
    }

    /* Update the pending index - next time the test will start from this slot */
    *elem_pending = old_pending;

    return 0;
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_incast_ep_flagged_slots_reset_desc_flags(uct_mm_coll_ep_t *ep,
                                                uint8_t *base_ptr,
                                                uct_mm_coll_fifo_element_t *elem)
{
    /* Set all the slots to match the current ownership flag */
    uint8_t i;
    size_t slot_size = ep->seg_slot;
    uint8_t is_owner = !(UCT_MM_FIFO_ELEM_FLAG_OWNER & elem->super.flags);
    base_ptr         = UCS_PTR_BYTE_OFFSET(base_ptr, sizeof(uint64_t));

    for (i = 0; i < ep->sm_proc_cnt; i++) {
        /* Avoid the assertion in uct_mm_coll_iface_slotted_get_slot_counter */
        uct_mm_coll_iface_slotted_set_slot_counter_unchecked(base_ptr, slot_size,
                                                             is_owner, 0, 0);
        base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, slot_size);
    }
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_ep_reset_element_data(uct_mm_coll_fifo_element_t* elem,
                                  enum uct_mm_coll_type type,
                                  uct_mm_coll_ep_t *ep, uint8_t *base_ptr,
                                  int is_short, int is_incast)
{
    switch (type) {
#if HAVE_SM_COLL_EXTRA
    case UCT_MM_COLL_TYPE_ATOMIC:
        /* Atomic assumes the data part of an element starts as zeros */
        memset(base_ptr, 0, elem->super.length);
        /* no break */
    case UCT_MM_COLL_TYPE_LOCKED:
    case UCT_MM_COLL_TYPE_COUNTED_SLOTS:
        ucs_assert(elem->pending == ep->sm_proc_cnt);
        break;

    case UCT_MM_COLL_TYPE_HYPOTHETIC:
        /* We don't care, since this is only hypothetic */
        break;
#endif

    case UCT_MM_COLL_TYPE_FLAGGED_SLOTS:
        /* This is ok because the ownership flag value alternates */
        break;

    case UCT_MM_COLL_TYPE_COLLABORATIVE:
        elem->pending = 0;
        break;

    default:
        ucs_assert(0);
        break;
    }
}

ucs_status_t mm_coll_iface_elems_prepare(uct_mm_coll_iface_t *iface,
                                         enum uct_mm_coll_type type,
                                         int for_termination)
{
    unsigned i;
    uint8_t *seg;
    uct_mm_coll_ep_t ep = {0};
    UCS_V_UNUSED ucs_status_t status;
    size_t elem_size                 = iface->super.config.fifo_elem_size;
    uct_mm_coll_fifo_element_t *elem = iface->super.recv_fifo_elems;
    int is_collaborative             = (type == UCT_MM_COLL_TYPE_COLLABORATIVE);

    if (!for_termination) {
        ucs_assert_always(((uintptr_t)elem % UCS_SYS_CACHE_LINE_SIZE) == 0);
        ucs_assert_always((sizeof(*elem)   % UCS_SYS_CACHE_LINE_SIZE) == 0);
    }

    for (i = 0; i < iface->super.config.fifo_size;
         i++, elem = UCS_PTR_BYTE_OFFSET(elem, elem_size)) {
#if HAVE_SM_COLL_EXTRA
        if (type == UCT_MM_COLL_TYPE_LOCKED) {
            if (for_termination) {
                ucs_spinlock_destroy(&elem->lock);
            } else {
                status = ucs_spinlock_init(&elem->lock,
                                           UCS_SPINLOCK_FLAG_SHARED);
                if (status != UCS_OK) {
                    return status;
                }
            }
        }
#endif

        if (!for_termination) {
            /* Initialize essential fields */
            elem->pending     = 0;
            elem->super.flags = UCT_MM_FIFO_ELEM_FLAG_OWNER;

            /* Set the element payload to zero */
            memset(elem + 1, 0, elem_size - sizeof(*elem));

            /* Set the attached (local) segment to zero */
            uct_mm_coll_ep_get_base_ptr(0, 0, is_collaborative, elem, &ep, &seg);
            memset(seg, 0, iface->super.config.seg_size);
        }
    }

    return UCS_OK;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
uct_mm_coll_bcast_complete_elem(uct_mm_coll_fifo_element_t *elem,
                                enum uct_mm_coll_type type, int only_check,
                                int is_short, int use_timestamp,
                                int nonzero_ep_offset_id, uint8_t *slot_ptr,
                                uct_mm_coll_ep_t *ep, uint8_t owner_flag)
{
    size_t ignored;
    uint32_t pending;
    size_t slot_offset;
    ucs_status_t status;
    const size_t slot_size = UCS_SYS_CACHE_LINE_SIZE;

    switch (type) {
#if HAVE_SM_COLL_EXTRA
    case UCT_MM_COLL_TYPE_LOCKED:
        if (only_check) {
            pending = elem->pending;
        } else {
            ucs_spin_lock(&elem->lock);
            pending = ++(elem->pending);
            ucs_spin_unlock(&elem->lock);
        }

        ucs_assert(pending <= ep->sm_proc_cnt);
        if (pending == ep->sm_proc_cnt) {
            return UCS_OK;
        }
        break;

    case UCT_MM_COLL_TYPE_ATOMIC:
    case UCT_MM_COLL_TYPE_COUNTED_SLOTS:
        pending = only_check ? elem->pending :
                               ucs_atomic_fadd32(&elem->pending, 1) + 1;
        if (pending == ep->sm_proc_cnt) {
            return UCS_OK;
        }
        break;

    case UCT_MM_COLL_TYPE_HYPOTHETIC:
        return UCS_OK;
#endif

    case UCT_MM_COLL_TYPE_FLAGGED_SLOTS:
        ucs_assert(ep->seg_slot  == UCS_SYS_CACHE_LINE_SIZE);
        ucs_assert(ep->elem_slot == UCS_SYS_CACHE_LINE_SIZE);
        if (!only_check) {
            uct_mm_coll_iface_slotted_set_slot_counter_nontemporal(slot_ptr,
                                                                   !owner_flag);
            return UCS_INPROGRESS;
        }

        /* Calls reaching here do not provide a slot pointer: need to calculate */
        ucs_assert(slot_ptr == NULL);
        pending = elem->pending;
        status  = uct_mm_coll_ep_get_data_ptr(is_short, elem, ep, 1, 0, 0, 1,
                                              pending, UCT_COLL_LENGTH_INFO_DEFAULT,
                                              0, &slot_ptr, &slot_offset, &ignored);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }

        ucs_assert(slot_offset >= elem->super.length);
        if (uct_mm_coll_ep_flagged_slots_is_elem_ready(slot_ptr + slot_offset,
                                                       slot_size, ep, is_short,
                                                       use_timestamp, owner_flag,
                                                       pending, &elem->pending)) {
            return UCS_OK;
        }
        break;

    case UCT_MM_COLL_TYPE_COLLABORATIVE:
        ucs_assert(ep->seg_slot  == UCS_SYS_CACHE_LINE_SIZE);
        ucs_assert(ep->elem_slot == UCS_SYS_CACHE_LINE_SIZE);
        if (!only_check) {
            ucs_assert(!*uct_mm_coll_iface_slotted_get_slot_counter(slot_ptr,
                                                                    slot_size));

            uct_mm_coll_ep_collaborative_send(elem, ep, slot_ptr, slot_size, is_short,
                                              use_timestamp, 1, 1, nonzero_ep_offset_id,
                                              0, 0, ucs_empty_function_return_zero_size_t,
                                              ucs_empty_function_return_zero_size_t, NULL,
                                              0, 0, 0, NULL);

            if (!nonzero_ep_offset_id) {
                ucs_assert(!*uct_mm_coll_iface_slotted_get_slot_counter(slot_ptr,
                                                                        slot_size) ||
                           (elem->pending > 0));
            }
            return UCS_INPROGRESS;
        }

        /* Calls reaching here do not provide a slot pointer: need to calculate */
        ucs_assert(slot_ptr == NULL);
        ucs_assert(!nonzero_ep_offset_id);
        pending = elem->pending;
        status  = uct_mm_coll_ep_get_data_ptr(is_short, elem, ep, 1, 0, 1, 1,
                                              pending, UCT_COLL_LENGTH_INFO_DEFAULT,
                                              0, &slot_ptr, &slot_offset, &ignored);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }

        /* Set the base to be the first bcast ack. slot, then consider pending */
        ucs_assert(pending <= ep->sm_proc_cnt);
        if (pending == 0) {
            pending = 1; /* Otherwise uct_mm_coll_ep_collaborative_is_elem_ready
                            will set elem->pending=1, since update_elem_pending
                            is set in the following invocation. */
        }
        if ((ucs_likely(pending == ep->sm_proc_cnt)) ||
            (uct_mm_coll_ep_collaborative_is_elem_ready(slot_ptr, slot_offset,
                                                        slot_size, ep, is_short,
                                                        1, use_timestamp, 0, 0, 0,
                                                        ucs_empty_function_return_zero_size_t,
                                                        NULL, 0, pending, 0, NULL,
                                                        1, &elem->pending))) {
            elem->pending = 0;
            return UCS_OK;
        }

        break;

    case UCT_MM_COLL_TYPE_LAST:
        ucs_assert(0);
        return 0;
    }

    return UCS_INPROGRESS;
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_coll_iface_complete_elem(uct_mm_coll_iface_t *mm_iface,
                                uct_mm_coll_fifo_element_t *elem,
                                uct_mm_coll_fifo_element_t *elems_fifo,
                                uct_mm_fifo_check_t *recv_check,
                                size_t elem_size, int complete_for_all)
{
    if (complete_for_all) {
        elem->pending = 0;
    }

    ucs_assert(elem == UCS_PTR_BYTE_OFFSET(elems_fifo, elem_size *
                                           (recv_check->read_index &
                                            mm_iface->super.fifo_mask)));

    elem = UCT_MM_COLL_NEXT_FIFO_ELEM(&mm_iface->super, elem,
                                      recv_check->read_index, elems_fifo,
                                      elem_size);

    recv_check->read_elem      = &elem->super;
    recv_check->is_flag_cached = 0;

    if (complete_for_all) {
        uct_mm_progress_fifo_tail(recv_check);
    }
}

static UCS_F_DEBUG_OR_INLINE void
uct_mm_bcast_ep_poll_tail(uct_mm_base_bcast_iface_t *iface,
                          enum uct_mm_coll_type type,
                          int use_timestamp, int nonzero_ep_offset_id)
{
    ucs_status_t status;
    uint8_t flags, owner_flag;
    uct_mm_coll_fifo_element_t *elem;
    int is_short, needs_reset        = 0;
    uct_mm_coll_ep_t *ep             = iface->super.loopback_ep;
    uct_mm_base_iface_t *mm_iface    = &iface->super.super;
    uct_mm_fifo_check_t *recv_check  = &mm_iface->recv_check;
    uct_mm_coll_fifo_element_t *fifo = mm_iface->recv_fifo_elems;
    size_t elem_size                 = mm_iface->config.fifo_elem_size;
    uint64_t read_limit              = recv_check->fifo_ctl->head;

    while ((ucs_unlikely(recv_check->read_index != read_limit)) &&
           (uct_mm_iface_fifo_has_new_data(recv_check, 1))) {
        /* Check if we've reached the release factor */
        if (uct_mm_progress_fifo_test(recv_check, 1)) {
            elem       = UCS_PTR_BYTE_OFFSET(fifo, elem_size *
                                             (recv_check->read_index &
                                              mm_iface->fifo_mask));
            flags      = elem->super.flags;
            is_short   = flags & UCT_MM_FIFO_ELEM_FLAG_INLINE;
            owner_flag = flags & UCT_MM_FIFO_ELEM_FLAG_OWNER;
            status     = uct_mm_coll_bcast_complete_elem(elem, type, 1, is_short,
                                                         use_timestamp,
                                                         nonzero_ep_offset_id,
                                                         NULL, ep, owner_flag);
            if (ucs_unlikely(status == UCS_INPROGRESS)) {
                return;
            }

            ucs_assert(status == UCS_OK);

            uct_mm_coll_iface_complete_elem(&iface->super, elem, fifo,
                                            recv_check, elem_size, 1);
        } else {
            recv_check->read_index++;
            recv_check->is_flag_cached = 0;
            needs_reset                = 1;
        }
    }

    if (needs_reset) {
        recv_check->read_index--;
        elem = UCS_PTR_BYTE_OFFSET(fifo, elem_size *
                                   (recv_check->read_index &
                                    mm_iface->fifo_mask));
        uct_mm_coll_iface_complete_elem(&iface->super, elem, fifo, recv_check,
                                        elem_size, 1);
    }
}

#define UCT_MM_COLL_EP_OWNER_FLAG(_head_index, _fifo_size) \
    (((_head_index) & (_fifo_size)) != 0)

static UCS_F_DEBUG_OR_INLINE ssize_t
uct_mm_coll_ep_am_common_send(uct_coll_length_info_t len_info, int is_bcast,
                              int is_short, uct_ep_h tl_ep, uint8_t am_id,
                              size_t length, uint64_t header, unsigned flags,
                              enum uct_mm_coll_type type, int nonzero_ep_offset_id,
                              int nonzero_length, int use_timestamp,
                              int use_cacheline_per_peer, int do_prefetch,
                              enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                              const uct_mm_coll_iface_cb_t memcpy_cb,
                              const uct_mm_coll_iface_cb_t reduce_cb,
                              const void *payload_or_pack_cb_arg)
{
    uint8_t *base_ptr;
    size_t slot_offset;
    uint8_t elem_flags;
    ucs_status_t status;
    int use_nt_cl;
    size_t posted_length;
    uct_mm_fifo_element_t dummy;
    UCS_V_UNUSED uint32_t previous_pending;
    UCS_V_UNUSED uct_mm_base_bcast_iface_t *bcast_iface;

    DEFINE_ARRAY(ntb);

    /* Grab the next cell I haven't yet written to */
    uct_mm_coll_ep_t *ep             = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);
    uct_mm_ep_t *mm_ep               = ucs_derived_of(ep, uct_mm_ep_t);
    uint64_t head                    = ep->tx_index;
    uct_mm_coll_fifo_element_t *elem = ep->tx_elem;
    unsigned fifo_size               = ep->fifo_size;
    int is_collaborative             = (type == UCT_MM_COLL_TYPE_COLLABORATIVE);

    /* Sanity checks */
    UCT_CHECK_AM_ID(am_id);

    /* Check if there is room in the remote process's receive FIFO to write */
    if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, mm_ep->cached_tail, fifo_size)) {
        if (!ucs_arbiter_group_is_empty(&mm_ep->arb_group)) {
            /* pending isn't empty. don't send now to prevent out-of-order sending */
            UCS_STATS_UPDATE_COUNTER(mm_ep->super.stats, UCT_EP_STAT_NO_RES, 1);
            return UCS_ERR_NO_RESOURCE;
        } else {
            /* pending is empty */
            /* update the local copy of the tail to its actual value on the remote peer */
            uct_mm_ep_update_cached_tail(mm_ep);
            if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, mm_ep->cached_tail, fifo_size)) {
                UCS_STATS_UPDATE_COUNTER(mm_ep->super.stats, UCT_EP_STAT_NO_RES, 1);
                return UCS_ERR_NO_RESOURCE;
            }
        }
    }

    /* Get ready to write to the next element */
    status = uct_mm_coll_ep_get_data_ptr(is_short, elem, ep, is_bcast, !is_bcast,
                                         is_collaborative, 0, 0, len_info,
                                         length, &base_ptr, &slot_offset,
                                         &posted_length);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    /* Non-temporal optimization - to write cache-lines without reading first */
    use_nt_cl = is_short && !is_bcast && (use_cacheline_per_peer || is_collaborative);
    /*
     * Note: Collaborative method uses the last byte of each cache line as a
     *       flag, so it should always use the nontemporal buffer instead of
     *       writing directly to the destination slot. Writing to that
     *       destination slot, combined with the full cache-line copy
     *       optimization, would result in grabbing the flag from the source
     *       slot instead of setting the flag at the end of the collaboration.
     */

    /* Take action - based on the transport type */
    switch (type) {
#if HAVE_SM_COLL_EXTRA
    case UCT_MM_COLL_TYPE_LOCKED:
        if (is_bcast) {
            previous_pending = 0;
        } else {
            ucs_spin_lock(&elem->lock);
            previous_pending = elem->pending++;
            ucs_assert(previous_pending <= (ep->sm_proc_cnt - 1));
            if (previous_pending && !is_short) {
                base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, sizeof(uint64_t));
            }
        }

        /* Perfrom the send/reduction, while the element is locked */
        uct_mm_coll_ep_am_common_reduce_or_memcpy(previous_pending, use_timestamp,
                                                  is_short, is_bcast,
                                                  nonzero_ep_offset_id,
                                                  nonzero_length,
                                                  reduce_cb_type,
                                                  memcpy_cb, reduce_cb, base_ptr,
                                                  &length, payload_or_pack_cb_arg);

        if (is_bcast) {
            posted_length = length;
            break;
        }

        ucs_spin_unlock(&elem->lock);

        if (!is_short) {
            length += sizeof(uint64_t);
        }
        if (previous_pending != (ep->sm_proc_cnt - 1)) {
            /* Return without posting the (collective) send */
            goto not_last;
        }

        posted_length = length;
        break;

    case UCT_MM_COLL_TYPE_ATOMIC:
        ucs_assert(!reduce_cb_type);
        if (nonzero_ep_offset_id && !is_short) {
            base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, sizeof(uint64_t));
        }

        /* Perfrom the send/reduction, assuming the callback uses atomics */
        uct_mm_coll_ep_am_common_reduce_or_memcpy((is_short && !is_bcast) ||
                                                  nonzero_ep_offset_id,
                                                  use_timestamp, is_short,
                                                  is_bcast, nonzero_ep_offset_id,
                                                  nonzero_length, 0, memcpy_cb,
                                                  reduce_cb, base_ptr, &length,
                                                  payload_or_pack_cb_arg);

        if ((!nonzero_ep_offset_id) && (!is_bcast)) {
            length += sizeof(uint64_t);
        }

        /* Increment the number of participants who have contributed */
        if (!is_bcast &&
            (ucs_atomic_fadd32(&elem->pending, 1) != (ep->sm_proc_cnt - 1))) {
            /* Return without posting the (collective) send */
            goto not_last;
        }

        posted_length = length;
        break;

    case UCT_MM_COLL_TYPE_HYPOTHETIC:
        if (nonzero_ep_offset_id && !is_short) {
            base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, sizeof(uint64_t));
        }

        /* Always over-write memory, which obviously break correctness */
        length = uct_mm_coll_ep_collaborative_memcpy(is_short, is_bcast,
                                                     use_nt_cl ? ntb : base_ptr,
                                                     payload_or_pack_cb_arg,
                                                     nonzero_ep_offset_id,
                                                     nonzero_length,
                                                     reduce_cb_type,
                                                     length, memcpy_cb);

        uct_mm_coll_ep_memcpy_cl_nt_no_opt(use_nt_cl, ntb, base_ptr, length);

        if (nonzero_ep_offset_id) {
            length += sizeof(uint64_t);
            goto not_last;
        }

        posted_length = length;
        break;

    case UCT_MM_COLL_TYPE_COUNTED_SLOTS:
#endif
    case UCT_MM_COLL_TYPE_FLAGGED_SLOTS:
        if (is_bcast) {
        /* Find the right offset to apply memory copy/reduction to */
            ucs_assert(slot_offset == 0);
        } else {
            base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, slot_offset);
        }

        /* Perfrom the send/reduction, assuming exclusive access to that slot */
        uct_mm_coll_ep_am_common_reduce_or_memcpy(!is_short &&
                                                  nonzero_ep_offset_id,
                                                  use_timestamp, is_short,
                                                  is_bcast, nonzero_ep_offset_id,
                                                  nonzero_length, 0, memcpy_cb,
                                                  reduce_cb, use_nt_cl ?
                                                  ntb : base_ptr, &length,
                                                  payload_or_pack_cb_arg);

        if ((type == UCT_MM_COLL_TYPE_FLAGGED_SLOTS) && !is_bcast) {
            /* Asserts (for dubugging) require the flag changes across iterations */
#if UCS_ENABLE_ASSERT
            if (use_nt_cl) {
                ucs_assert(posted_length == UCS_SYS_CACHE_LINE_SIZE);
                ntb[UCS_SYS_CACHE_LINE_SIZE-1] =
                    UCT_MM_COLL_EP_OWNER_FLAG(head, fifo_size);
            }
#endif

            /* Mark this slot as ready */
            if (nonzero_ep_offset_id) {
                uct_mm_coll_iface_slotted_set_slot_counter(use_nt_cl ?
                                                           ntb : base_ptr,
                                                           posted_length,
                    !UCT_MM_COLL_EP_OWNER_FLAG(head, fifo_size), !use_nt_cl, 1);
            }
        }

        /* If used, flush the non-temporal ntbfer */
        uct_mm_coll_ep_memcpy_cl_nt_no_opt(use_nt_cl, ntb, base_ptr, length);

        if (is_bcast) {
            ucs_assert(!nonzero_ep_offset_id);
            posted_length = length;
            break;
        }

#if HAVE_SM_COLL_EXTRA
        if (type == UCT_MM_COLL_TYPE_COUNTED_SLOTS) {
            /* Increment the number of participants who have contributed */
            if ((ucs_atomic_fadd32(&elem->pending, 1) != (ep->sm_proc_cnt - 1))) {
                /* Return without posting the (collective) send */
                goto not_last;
            }
        } else
#endif
        {
            /* Always the lowest rank sets the element as ready */
            if (nonzero_ep_offset_id) {
                goto not_last;
            }

            /* Lowest (non-root) ID triggers element "completion" */
            elem->pending = 1;
        }
        break;

    case UCT_MM_COLL_TYPE_COLLABORATIVE:
        /* Find the right offset to apply memory copy/reduction to */
        if (is_bcast) {
            elem->pending = 0;
            ucs_assert(slot_offset == 0);
        } else {
            base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, slot_offset);
        }

        /* Write/reduce my payload into one of the slots according to arrival */
        uct_mm_coll_ep_collaborative_send(elem, ep, base_ptr, posted_length,
                                          is_short, use_timestamp, is_bcast,
                                          !is_bcast, nonzero_ep_offset_id,
                                          nonzero_length, reduce_cb_type,
                                          memcpy_cb, reduce_cb,
                                          payload_or_pack_cb_arg, use_nt_cl,
                                          !is_bcast && !is_short && !nonzero_ep_offset_id &&
                                          (reduce_cb_type != UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL),
                                          1, &length);

        /* Lowest (non-root) ID triggers element "completion", the rest can go */
        if (nonzero_ep_offset_id) {
            ucs_assert(!is_bcast);
            goto not_last;
        }

#if ENABLE_ASSERT
        if (is_bcast) {
            /* Test that all the slot counters are initialized to zero */
            status = uct_mm_coll_ep_get_data_ptr(is_short, elem, ep, 0, 0, 1, 1,
                                                 0, len_info, length, &base_ptr,
                                                 &slot_offset, &posted_length);
            ucs_assert(status == UCS_OK);
            ucs_assert(posted_length == UCS_SYS_CACHE_LINE_SIZE);
            base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, slot_offset);
            for (elem_flags = 1; elem_flags < ep->sm_proc_cnt; elem_flags++) {
                ucs_assert(!*uct_mm_coll_iface_slotted_get_slot_counter(base_ptr,
                    (elem_flags + 1) * posted_length));
            }
        }
#endif

        /* Lowest (non-root) ID triggers element "completion" - moment of truth */
        ucs_assert(elem->pending >= !is_bcast);
        posted_length = length;
        break;

    case UCT_MM_COLL_TYPE_LAST:
        ucs_assert(0);
        return UCS_ERR_INVALID_PARAM;
    }

    /* Change the owner bit to indicate that the writing is complete.
     * The owner bit flips after every FIFO wrap-around */
    elem_flags = UCT_MM_COLL_EP_OWNER_FLAG(head, fifo_size) |
                 (len_info << UCT_MM_FIFO_ELEM_FLAG_SHIFT);
    if (is_short) {
        elem_flags  |= UCT_MM_FIFO_ELEM_FLAG_INLINE;
        elem->header = header;
    } else {
        ucs_assert(posted_length != 0);
    }

    if (is_bcast) {
        /* Ensure previous memory stores have been completed before proceeding */
        ucs_memory_cpu_store_fence();
        /* non-bcast have this fence inside their function */
    }

    /* Set this element as "written" - pass ownership to the receiver */
    dummy.am_id        = am_id;
    dummy.length       = posted_length;
    dummy.flags        = elem_flags;
    elem->super.atomic = dummy.atomic; /* high-contention memory */

    /* Progress the (remote) head */
    mm_ep->fifo_ctl->head = head;

    /* Signal remote, if so requested */
    if (ucs_unlikely(flags & UCT_SEND_FLAG_SIGNALED)) {
        uct_mm_ep_signal_remote(mm_ep);
    }

not_last:
    uct_mm_iface_trace_am(ucs_derived_of(ep->super.super.super.iface, uct_mm_base_iface_t),
                          UCT_AM_TRACE_TYPE_SEND, elem->super.flags, am_id,
                          base_ptr, length, head & ~UCT_MM_IFACE_FIFO_HEAD_EVENT_ARMED);
    if (is_short) {
        UCT_TL_EP_STAT_OP(&mm_ep->super, AM, SHORT, length);
    } else {
        UCT_TL_EP_STAT_OP(&mm_ep->super, AM, BCOPY, length);
    }

    /* Update both the index and the pointer to the next element */
    ep->tx_elem = elem = UCT_MM_COLL_NEXT_FIFO_ELEM(ep, elem, ep->tx_index,
                                                    ep->fifo_elems, ep->elem_size);

    /* Prefetch optimization: prepare to access the next element */
    if (ucs_likely(do_prefetch)) {
        /* Attempt to write-back/demote caching level of sent (best effort) */
        ucs_share_cache(base_ptr);

        /* Prepare to write the next element */
        if (ucs_unlikely((elem == ep->fifo_elems) || !is_short)) {
            /* Wrap-around took place! */
            uct_mm_coll_ep_get_data_ptr(is_short, elem, ep, is_bcast, !is_bcast,
                                        is_collaborative, 0, 0, len_info, 0,
                                        &base_ptr, &slot_offset, &posted_length);
        } else {
            slot_offset = ep->elem_size;
        }
        base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, slot_offset);

        if (is_bcast) {
            ucs_prefetch_write(base_ptr); /* Location to be broadcasted from */
        } else if ((type == UCT_MM_COLL_TYPE_FLAGGED_SLOTS) ||
#if HAVE_SM_COLL_EXTRA
                   (type == UCT_MM_COLL_TYPE_COUNTED_SLOTS) ||
#endif
                   (type == UCT_MM_COLL_TYPE_COLLABORATIVE)) {
            if (is_short) {
                ucs_prefetch_write(base_ptr);
            } else {
                ucs_prefetch_write(uct_mm_coll_iface_slotted_get_slot_counter(base_ptr,
                                                                              posted_length));
                for (posted_length = 0;
                     posted_length < ucs_min(length, MM_COLL_EP_MAX_PREFETCH);
                     posted_length += UCS_SYS_CACHE_LINE_SIZE) {
                    ucs_prefetch_write(base_ptr);
                    base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr,
                                                   UCS_SYS_CACHE_LINE_SIZE);
                }
            }
        }
    }

    /* Broadcast optimization during Barrier: poll for completion */
    if (is_bcast && !nonzero_ep_offset_id && nonzero_length) {
        bcast_iface = ucs_derived_of(ep->super.super.super.iface,
                                     uct_mm_base_bcast_iface_t);
        uct_mm_bcast_ep_poll_tail(bcast_iface, type, use_timestamp,
                                  nonzero_ep_offset_id);
    }

    VALGRIND_MAKE_MEM_UNDEFINED(ntb, UCS_SYS_CACHE_LINE_SIZE);

    return (ssize_t)length;
}

static UCS_F_DEBUG_OR_INLINE ucs_status_t
uct_mm_coll_ep_am_short(uct_ep_h ep, uint8_t id, uint64_t header,
                        const void *payload, unsigned length, int is_bcast,
                        enum uct_mm_coll_type type, int use_cacheline_per_peer,
                        int use_timestamp, int nonzero_ep_offset_id,
                        int nonzero_length, enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                        const uct_mm_coll_iface_cb_t memcpy_cb,
                        const uct_mm_coll_iface_cb_t reduce_cb)
{
    uct_mm_coll_iface_cb_ctx_t ctx;
    uct_mm_base_incast_iface_t *iface;
    unsigned orig_length            = nonzero_length ?
                                      UCT_COLL_LENGTH_INFO_UNPACK_VALUE(length) : 0;
    int is_slotted                  = (type == UCT_MM_COLL_TYPE_FLAGGED_SLOTS) ||
#if HAVE_SM_COLL_EXTRA
                                      (type == UCT_MM_COLL_TYPE_COUNTED_SLOTS) ||
#endif
                                      (type == UCT_MM_COLL_TYPE_COLLABORATIVE);
    uct_coll_length_info_t len_info = is_slotted ? UCT_COLL_LENGTH_INFO_DEFAULT :
                                      UCT_COLL_LENGTH_INFO_UNPACK_MODE(length);

    if (reduce_cb_type != UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL) {
        iface           = ucs_derived_of(ep->iface, uct_mm_base_incast_iface_t);
        ctx.rctx        = &iface->ext_reduce;
        ctx.pack_cb_arg = (void*)payload;
        payload         = &ctx;
    }

    ucs_assert(nonzero_length == (orig_length != 0));
    ucs_assert(!((uct_mm_coll_ep_t*)ep)->offset_id == !nonzero_ep_offset_id);
    ucs_assert((len_info == UCT_COLL_LENGTH_INFO_DEFAULT) || !is_slotted);
    ucs_assert((len_info == UCT_COLL_LENGTH_INFO_DEFAULT) ||
               (len_info == UCT_COLL_LENGTH_INFO_PACKED));
    ssize_t ret = uct_mm_coll_ep_am_common_send(len_info, is_bcast, 1, ep, id,
                                                orig_length, header, 0,
                                                type, nonzero_ep_offset_id,
                                                nonzero_length, use_timestamp,
                                                use_cacheline_per_peer,
                                                nonzero_length, reduce_cb_type,
                                                memcpy_cb, reduce_cb, payload);

    return (ret > 0) ? UCS_OK : (ucs_status_t)ret;
}

static UCS_F_DEBUG_OR_INLINE ssize_t
uct_mm_coll_ep_am_bcopy(uct_ep_h ep, uint8_t id, uct_pack_callback_t pack_cb,
                        void *arg, unsigned flags, int is_bcast,
                        enum uct_mm_coll_type type, int use_cacheline_per_peer,
                        int use_timestamp, int nonzero_ep_offset_id,
                        int nonzero_length, int use_reduce_cb,
                        const uct_mm_coll_iface_cb_t reduce_cb_orig,
                        enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type)
{
    uct_mm_coll_iface_cb_ctx_t ctx;
    uct_mm_coll_iface_cb_t memcpy_cb;
    uct_mm_base_incast_iface_t *iface;
    uct_mm_coll_iface_cb_t reduce_cb = reduce_cb_orig;
    unsigned orig_flags              = UCT_COLL_LENGTH_INFO_UNPACK_VALUE(flags);
    uct_coll_length_info_t len_info  = UCT_COLL_LENGTH_INFO_UNPACK_MODE(flags);

    /*
     * There are three types of bcopy functions (ordered from best to worst):
     *
     * 1. bcopy with internal reduction (in uct_mm_incast_ep_am_bcopy_func_arr)
     *    typical pack_cb:   external packer for 8B header followed by payload
     *    typical reduce_cb: uct_mm_coll_ep_reduce_extra_callback_func_arr
     *
     * 2. bcopy without explicit reduction (in uct_mm_incast_ep_am_bcopy_table)
     *    typical pack_cb:   external packer for 8B header followed by payload
     *    typical reduce_cb: same external packer w/ UCT_PACK_CALLBACK_REDUCE support
     *
     * 3. bcopy with external reduction (in uct_mm_incast_ep_am_bcopy_ext_cb_table)
     *    typical pack_cb:   external packer for 8B header followed by payload
     *    typical reduce_cb: uct_mm_coll_ep_am_common_reduce_ext()
     *
     * The first two use a reduce callback function to aggregate (e.g. sum) the
     * source buffer onto the destination buffer, which may limit some external
     * optimizations (e.g. using remote keys for large intra-node reductions).
     * Therefore, they only work if the user passes NULL as the pack_cb, thus
     * relinquishing control of the reduction operation. If a user does pass a
     * non-NULL packer callback - that callback would be invoked instead of the
     * reduction (thus disabling the optimization).
     */

    if (is_bcast) {
        /* Packing will never involve reduction in this case */
        memcpy_cb = (const uct_mm_coll_iface_cb_t)pack_cb; /* TODO: fix nasty cast? */
        reduce_cb = (const uct_mm_coll_iface_cb_t)ucs_empty_function_return_zero_size_t;
    } else {
        memcpy_cb       = use_reduce_cb ?
                          uct_mm_coll_ep_wrap_packer :
                          uct_mm_coll_ep_memcpy_packer;
        reduce_cb       = (reduce_cb_type == UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL) ?
                          reduce_cb :
                          uct_mm_coll_ep_reduce_packer;
        iface           = ucs_derived_of(ep->iface, uct_mm_base_incast_iface_t);
        ctx.rctx        = &iface->ext_reduce;
        ctx.pack_cb     = pack_cb;
        ctx.pack_cb_arg = arg;
        arg             = &ctx;
        ucs_assert(reduce_cb != NULL);
    }

    ucs_assert(nonzero_length);
    ucs_assert(!((uct_mm_coll_ep_t*)ep)->offset_id == !nonzero_ep_offset_id);
    return uct_mm_coll_ep_am_common_send(len_info, is_bcast, 0, ep, id, 0, 0,
                                         orig_flags, type,
                                         nonzero_ep_offset_id, nonzero_length,
                                         use_timestamp, use_cacheline_per_peer,
                                         nonzero_length, reduce_cb_type,
                                         memcpy_cb, reduce_cb, arg);
}

static inline ucs_status_t uct_mm_coll_ep_add(uct_mm_coll_ep_t* ep)
{
    /* Block the asynchronous progress while adding this new endpoint
     * (ucs_ptr_array_locked_t not used - to avoid lock during progress). */
    uct_base_iface_t *base_iface = ucs_derived_of(ep->super.super.super.iface,
                                                  uct_base_iface_t);
    uct_mm_coll_iface_t *iface   = ucs_derived_of(base_iface,
                                                  uct_mm_coll_iface_t);
    ucs_async_context_t *async   = base_iface->worker->async;

    UCS_ASYNC_BLOCK(async);

    ucs_ptr_array_set(&iface->ep_ptrs, ep->remote_id, ep);

    UCS_ASYNC_UNBLOCK(async);

    return UCS_OK;
}

#define UCS_CLASS_CALL_SUPER_INIT_PACKED(_superclass, ...) \
    { \
        { \
            ucs_status_t _status = _UCS_CLASS_INIT_NAME(_superclass)\
                    ((uct_mm_ep_t*)ucs_derived_of(self, uct_mm_coll_ep_t), \
                     _myclass->superclass, _init_count, ## __VA_ARGS__); \
            if (_status != UCS_OK) { \
                return _status; \
            } \
            if (_myclass->superclass != &_UCS_CLASS_DECL_NAME(void)) { \
                ++(*_init_count); \
            } \
        } \
    }

UCS_CLASS_INIT_FUNC(uct_mm_coll_ep_t, const uct_ep_params_t *params)
{
    size_t ignored;
    uct_ep_params_t super_params;
    const uct_mm_coll_iface_addr_t *addr = (const void *)params->iface_addr;
    uct_mm_coll_iface_t *iface           = ucs_derived_of(params->iface,
                                                          uct_mm_coll_iface_t);
    int is_collaborative                 = (iface->type ==
                                            UCT_MM_COLL_TYPE_COLLABORATIVE);

    /* Sanity checks */
    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE);
    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE_ADDR);
    ucs_assert(iface->super.fifo_mask + 1 == iface->super.config.fifo_size);
    ucs_assert(addr->coll_id < iface->sm_proc_cnt);

    /* Parameter copy-aside */
    memcpy(&super_params, params, sizeof(*params)); // TODO: fix ABI compatibility
    super_params.iface_addr =
            (void*)&((uct_mm_coll_iface_addr_t*)params->iface_addr)->super;

    UCS_CLASS_CALL_SUPER_INIT_PACKED(uct_mm_ep_t, &super_params);

    if (addr->coll_id == (uint32_t)-1) {
        return UCS_OK;
    }

    self->ref_count   = 1;
    self->remote_id   = addr->coll_id;
    self->sm_proc_cnt = iface->sm_proc_cnt - 1;
    self->offset_id   = iface->my_coll_id - (iface->my_coll_id > addr->coll_id);
    self->elem_slot   = iface->elem_slot_size;

    self->elem_size   = iface->super.config.fifo_elem_size;
    self->elem_offset = (iface->my_coll_id == 0) ? 0 :
                        uct_mm_coll_ep_get_slot_offset_by_id(self, 1,
                                                             iface->is_incast,
                                                             is_collaborative,
                                                             self->elem_slot, 0,
                                                             self->offset_id,
                                                             &ignored);
    self->seg_slot    = iface->seg_slot_size;
    self->seg_size    = iface->super.config.seg_size;
    self->seg_offset  = (iface->my_coll_id == 0) ? 0 :
                        uct_mm_coll_ep_get_slot_offset_by_id(self, 0,
                                                             iface->is_incast,
                                                             is_collaborative, 0,
                                                             self->seg_slot,
                                                             self->offset_id,
                                                             &ignored);

    self->fifo_size   = iface->super.config.fifo_size;
    self->fifo_mask   = iface->super.fifo_mask;
    self->tx_index    = 0;
    self->tx_elem     = self->super.fifo_elems;
    self->fifo_elems  = self->super.fifo_elems;

    if (iface->my_coll_id == self->remote_id) {
        iface->loopback_ep = self;
    } else {
        uct_mm_coll_ep_add(self);
    }

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_bcast_ep_t, const uct_ep_params_t *params)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(params->iface, uct_mm_coll_iface_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_ep_t, params);

    self->recv_check.is_flag_cached           = 0;
    self->recv_check.read_index               = 0;
    self->recv_check.fifo_size                = iface->super.recv_check.fifo_size;
    self->recv_check.read_elem                = self->super.super.fifo_elems;
    self->recv_check.fifo_ctl                 = self->super.super.fifo_ctl;
    self->recv_check.fifo_release_factor_mask =
        iface->super.recv_check.fifo_release_factor_mask;

    ucs_debug("mm_bcast: ep connected: %p, src_coll_id: %u, dst_coll_id: %u",
              self, iface->my_coll_id, self->super.remote_id);

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_incast_ep_t, const uct_ep_params_t *params)
{
    uct_mm_coll_iface_t *coll_iface          = ucs_derived_of(params->iface,
                                                              uct_mm_coll_iface_t);
    uct_mm_base_incast_iface_t *incast_iface = ucs_derived_of(coll_iface,
                                                              uct_mm_base_incast_iface_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_ep_t, params);

    self->ext_reduce = incast_iface->ext_reduce;

    ucs_debug("mm_incast: ep connected: %p, src_coll_id: %u, dst_coll_id: %u",
              self, coll_iface->my_coll_id, self->super.remote_id);

    return UCS_OK;
}

static UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_ep_t) {}
static UCS_CLASS_CLEANUP_FUNC(uct_mm_bcast_ep_t) {}
static UCS_CLASS_CLEANUP_FUNC(uct_mm_incast_ep_t) {}

UCS_CLASS_DEFINE(uct_mm_coll_ep_t, uct_mm_ep_t)
UCS_CLASS_DEFINE(uct_mm_bcast_ep_t, uct_mm_coll_ep_t)
UCS_CLASS_DEFINE(uct_mm_incast_ep_t, uct_mm_coll_ep_t)

int uct_mm_coll_ep_is_connected(const uct_ep_h tl_ep,
                                const uct_ep_is_connected_params_t *params)
{
    return uct_mm_ep_is_connected(tl_ep, params);
}

ucs_status_t uct_mm_coll_ep_flush(uct_ep_h tl_ep, unsigned flags,
                                  uct_completion_t *comp)
{
    uct_mm_coll_ep_t *ep = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);

    if (ucs_unlikely(ep->super.fifo_ctl == NULL)) {
        return UCS_OK;
    }

    return uct_mm_ep_flush(tl_ep, flags, comp);
}

/**
 * This function processes incoming messages (in elements). Specifically, this
 * function is used in "loopback mode" to check for incast, in which case the
 * passed endpoint is my own. After invoking the Active Message handler, the
 * return value may indicate that this message still needs to be kept
 * (UCS_INPROGRESS), and the appropriate callbacks are set for releasing it in
 * the future (by an upper layer calling @ref uct_iface_release_desc ).
 */
static UCS_F_DEBUG_OR_INLINE int
uct_mm_coll_ep_process_recv(uct_mm_coll_ep_t *ep, uct_mm_coll_iface_t *iface,
                            uct_mm_fifo_check_t *recv_check, int is_incast,
                            enum uct_mm_coll_type type, int use_timestamp,
                            int nonzero_ep_offset_id, int nonzero_length,
                            int is_loopback,
                            enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type,
                            const uct_mm_coll_iface_cb_t reduce_cb,
                            const void *reduce_ctx)
{
    void *desc;
    size_t length;
    size_t slot_size;
    int am_cb_flags;
    uint8_t *base_ptr;
    size_t slot_offset;
    ucs_status_t status;
    uint8_t pending_value;
    ssize_t headroom_offset;
    int is_counted_slots_incast;
    int is_slotted_incast_result;

    ucs_memory_cpu_load_fence();

    /* Detect incoming message parameters */
    uct_mm_coll_fifo_element_t *elem = ucs_derived_of(recv_check->read_elem,
                                                      uct_mm_coll_fifo_element_t);
    uint8_t flags                    = elem->super.flags;
    uct_coll_length_info_t len_info  = flags >> UCT_MM_FIFO_ELEM_FLAG_SHIFT;
    int is_short                     = flags & UCT_MM_FIFO_ELEM_FLAG_INLINE;
    int is_collaborative             = (type == UCT_MM_COLL_TYPE_COLLABORATIVE);
    int is_collaborative_incast      = is_incast && is_collaborative;
    int is_flagged_slots_incast      = is_incast &&
                                       (type == UCT_MM_COLL_TYPE_FLAGGED_SLOTS);
    int use_pending                  = is_flagged_slots_incast ||
                                       is_collaborative_incast;
    if (use_pending) {
        ucs_assert(len_info == UCT_COLL_LENGTH_INFO_DEFAULT);
        len_info      = UCT_COLL_LENGTH_INFO_DEFAULT;
        pending_value = elem->pending;
        ucs_assert(pending_value > 0);
        ucs_assert(pending_value <= ep->sm_proc_cnt);
    } else {
        pending_value = 0;
    }

    /* Get a pointer to the start of the incoming data */
    length = nonzero_length ? elem->super.length :
                              (is_short ? 0 : sizeof(elem->header));
    status = uct_mm_coll_ep_get_data_ptr(is_short, elem, ep, is_loopback,
                                         is_incast, is_collaborative,
                                         use_pending, pending_value,
                                         len_info, length, &base_ptr,
                                         &slot_offset, &slot_size);
    if (ucs_unlikely(status != UCS_OK)) {
        return 0;
    }

    /* Strided modes only - check if this is the last writer */
    if ((is_flagged_slots_incast &&
         !uct_mm_coll_ep_flagged_slots_is_elem_ready(base_ptr + slot_offset,
                                                     slot_size, ep, is_short,
                                                     use_timestamp, flags &
                                                     UCT_MM_FIFO_ELEM_FLAG_OWNER,
                                                     pending_value,
                                                     &elem->pending)) ||
        (is_collaborative_incast && (pending_value < ep->sm_proc_cnt) &&
         !uct_mm_coll_ep_collaborative_is_elem_ready(base_ptr, slot_offset,
                                                     slot_size, ep, is_short,
                                                     !is_incast, use_timestamp,
                                                     nonzero_ep_offset_id,
                                                     nonzero_length,
                                                     reduce_cb_type,
                                                     reduce_cb, reduce_ctx,
                                                     length, pending_value, 0,
                                                     NULL, 1, &elem->pending))) {
        ucs_assert(recv_check == &iface->super.recv_check);
        ucs_assert(len_info == UCT_COLL_LENGTH_INFO_DEFAULT);
        recv_check->is_flag_cached = 0; /* To force a re-check */
        return 0; /* incast started, but not all peers have written yet */
    }

    /* Adjust the base pointer, length and flags passed in the FIFO element */
#if HAVE_SM_COLL_EXTRA
    is_counted_slots_incast = is_incast && (type == UCT_MM_COLL_TYPE_COUNTED_SLOTS);
#else
    is_counted_slots_incast = 0;
#endif
    is_slotted_incast_result = is_counted_slots_incast || is_flagged_slots_incast;
    if (is_slotted_incast_result) {
        length = length * ep->sm_proc_cnt;
    }
    if (is_short) {
        /* This needs to change in order to support packed scatter operations */
        base_ptr   -= sizeof(elem->header);
        length     += sizeof(elem->header);
        am_cb_flags = 0;
    } else {
        if (is_slotted_incast_result) {
            length += sizeof(elem->header);
        }
        am_cb_flags = is_incast ? UCT_CB_PARAM_FLAG_DESC : 0;
    }
    if (use_timestamp) {
        am_cb_flags |= UCT_CB_PARAM_FLAG_TIMED;
    }

    uct_mm_iface_trace_am(&iface->super, UCT_AM_TRACE_TYPE_RECV, flags,
                          elem->super.am_id, base_ptr, length,
                          recv_check->read_index);

    /* Process the incoming message using the active-message callback */
    status = uct_iface_invoke_am(&iface->super.super.super, elem->super.am_id,
                                 base_ptr, length, am_cb_flags);

    ucs_assert((status != UCS_INPROGRESS) || (is_incast && !is_short));
    if (ucs_likely(is_short)) {
        /* Set the correct base pointer for completions (excl. headers) */
        base_ptr = UCS_PTR_BYTE_OFFSET(base_ptr, sizeof(elem->header));
        ucs_assert(status == UCS_OK);
    } else if (is_incast && ucs_unlikely(status == UCS_INPROGRESS)) {
        /* I'm the owner of this memory and can replace the element's segment */
        ucs_assert(recv_check == &iface->super.recv_check);
        ucs_assert(!is_short);

        /* Set the release callback for this descriptor still in use */
        headroom_offset     = -iface->super.rx_headroom;
        desc                = UCS_PTR_BYTE_OFFSET(base_ptr, headroom_offset);
        uct_recv_desc(desc) = (uct_recv_desc_t*)&iface->super.release_desc;

        /* Assign a new receive descriptor to this FIFO element.*/
        uct_mm_assign_desc_to_fifo_elem(&iface->super, &elem->super, 1);

        /* Set base_ptr to point to the new location, for the logic below */
        uct_mm_coll_ep_get_base_ptr(0, is_incast, is_collaborative, elem, ep,
                                    &base_ptr);

        /* Flagged slots method requires extra care in the new descriptor */
        if (is_flagged_slots_incast) {
            uct_mm_incast_ep_flagged_slots_reset_desc_flags(ep, base_ptr, elem);
        }
    } else {
        ucs_assert(status == UCS_OK);
    }

    if (is_incast) {
        /* Reset the element so it could be reused (required for some types) */
        uct_mm_coll_ep_reset_element_data(elem, type, ep, base_ptr, is_short, 1);
    } else if (uct_mm_progress_fifo_test(recv_check, 1)) {
        /* Mark as completed, and decide if the fifo tail should move */
        (void)uct_mm_coll_bcast_complete_elem(elem, type, 0, is_short,
                                              use_timestamp, nonzero_ep_offset_id,
                                              base_ptr + slot_offset, ep,
                                              flags & UCT_MM_FIFO_ELEM_FLAG_OWNER);
    }

    uct_mm_coll_iface_complete_elem(iface, elem, ep->fifo_elems, recv_check,
                                    ep->elem_size, is_incast);
    return 1;
}

/*
 * the "use_cb" field passed to uct_mm_incast_iface_poll_fifo() is an
 * optimization: the use of a callback is known at compile-time, whereas the
 * alternative of checking for non-zero callback would be in run-time.
 */
static UCS_F_DEBUG_OR_INLINE unsigned
uct_mm_incast_iface_poll_fifo(uct_mm_base_incast_iface_t *iface,
                              enum uct_mm_coll_type type,
                              int use_cacheline_per_peer,
                              int use_timestamp, int nonzero_ep_offset_id,
                              int nonzero_length, const uct_mm_coll_iface_cb_t reduce_cb,
                              enum uct_mm_coll_ep_reduce_cb_type reduce_cb_type)
{
    uct_mm_coll_iface_cb_ctx_t ctx;
    unsigned poll_count, poll_total;
    uct_mm_base_iface_t *mm_iface   = &iface->super.super;
    uct_mm_fifo_check_t *recv_check = &mm_iface->recv_check;

    if (!uct_mm_iface_fifo_has_new_data(recv_check, 0)) {
        return 0;
    }

    if (reduce_cb_type != UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL) {
        ctx.rctx = &iface->ext_reduce;
        ucs_assert(reduce_cb != NULL);
    }

    UCT_BASE_IFACE_LOCK(iface);

    ucs_assert(iface->super.type == type);
    poll_count = poll_total = mm_iface->fifo_poll_count;
    while (uct_mm_iface_fifo_has_new_data(recv_check, 1) &&
           uct_mm_coll_ep_process_recv(&iface->dummy->super, &iface->super,
                                       recv_check, 1, type, use_timestamp,
                                       nonzero_ep_offset_id, nonzero_length, 1,
                                       reduce_cb_type, reduce_cb, &ctx)) {
        if (ucs_unlikely(--poll_count == 0)) {
            goto poll_fifo_skip;
        }
    }

    poll_total -= poll_count;

poll_fifo_skip:
    uct_mm_iface_fifo_window_adjust(mm_iface, poll_total);

    if (ucs_unlikely(poll_total == 0)) {
        /* progress the pending sends (if there are any) */
        ucs_arbiter_dispatch(&mm_iface->arbiter, 1, uct_mm_ep_process_pending,
                             &poll_total);
    }

    UCT_BASE_IFACE_UNLOCK(iface);

    return poll_total;
}

static inline uct_mm_coll_ep_t* uct_mm_coll_ep_find(uct_mm_coll_iface_t *iface,
                                                    uint8_t coll_id)
{
    unsigned index;
    uct_mm_coll_ep_t *ep_iter;
    ucs_ptr_array_for_each(ep_iter, index, &iface->ep_ptrs) {
        if (coll_id == ep_iter->remote_id) {
            ep_iter->ref_count++;
            return ep_iter;
        }
    }

    return NULL;
}

static inline uct_ep_h uct_mm_coll_ep_check_existing(const uct_ep_params_t *params)
{
    uct_mm_coll_ep_t *ret;
    uint8_t coll_id = ((uct_mm_coll_iface_addr_t*)params->iface_addr)->coll_id;
    uct_mm_coll_iface_t *iface = ucs_derived_of(params->iface, uct_mm_coll_iface_t);

    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE);
    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE_ADDR);

    ret = (coll_id == iface->my_coll_id) ? iface->loopback_ep :
                                           uct_mm_coll_ep_find(iface, coll_id);

    return (uct_ep_h)ucs_derived_of(ret, uct_mm_ep_t);
}

ucs_status_t uct_mm_bcast_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p)
{
    uct_ep_h ep = uct_mm_coll_ep_check_existing(params);

    if (ucs_likely(ep != NULL)) {
        *ep_p = ep;
        return UCS_OK;
    }

    return UCS_CLASS_NEW(uct_mm_bcast_ep_t, ep_p, params);
}

ucs_status_t uct_mm_incast_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p)
{
    uct_ep_h ep = uct_mm_coll_ep_check_existing(params);

    if (ucs_likely(ep != NULL)) {
        *ep_p = ep;
        return UCS_OK;
    }

    return UCS_CLASS_NEW(uct_mm_incast_ep_t, ep_p, params);
}

static inline void uct_mm_coll_ep_del(uct_mm_coll_iface_t *iface,
                                      uct_mm_coll_ep_t* ep)
{
    unsigned index;
    uct_mm_coll_ep_t *ep_iter;
    uint8_t coll_id = ep->remote_id;

    ucs_ptr_array_for_each(ep_iter, index, &iface->ep_ptrs) {
        if (coll_id == ep_iter->remote_id) {
            /* Block the asynchronous progress while adding this new endpoint
             * (ucs_ptr_array_locked_t not used - to avoid lock during progress). */
            uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t);
            ucs_async_context_t *async   = base_iface->worker->async;
            UCS_ASYNC_BLOCK(async);
            ucs_ptr_array_remove(&iface->ep_ptrs, index);
            UCS_ASYNC_UNBLOCK(async);

            return;
        }
    }

    ucs_error("failed to find the endpoint in its array (id=%u)", coll_id);
}

void uct_mm_coll_ep_destroy(uct_ep_h tl_ep)
{
    uct_mm_coll_ep_t *ep = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);
    if (--ep->ref_count) {
        return;
    }

    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    if (ep->remote_id != iface->my_coll_id) {
        uct_mm_coll_ep_del(ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t), ep);
    } else {
        iface->loopback_ep = NULL;
    }

    UCS_CLASS_DELETE(uct_mm_coll_ep_t, ep);
}

void uct_mm_bcast_ep_destroy(uct_ep_h tl_ep)
{
    uct_mm_base_bcast_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_base_bcast_iface_t);
    if (iface->last_nonzero_ep == ucs_derived_of(tl_ep, uct_mm_bcast_ep_t)) {
        iface->last_nonzero_ep = iface->dummy_ep;
    }

    uct_mm_coll_ep_destroy(tl_ep);
}

static UCS_F_DEBUG_OR_INLINE unsigned
uct_mm_bcast_ep_poll_fifo(uct_mm_base_bcast_iface_t *iface,
                          enum uct_mm_coll_type type, uct_mm_bcast_ep_t *ep,
                          int use_timestamp, int nonzero_ep_offset_id,
                          int nonzero_length)
{
    uct_mm_fifo_check_t *recv_check = ucs_unaligned_ptr(&ep->recv_check);
    ucs_assert((ep == iface->dummy_ep) ||
               (!ep->super.offset_id == !nonzero_ep_offset_id));

    if (ucs_unlikely(!uct_mm_iface_fifo_has_new_data(recv_check, 1))) {
        return 0;
    }

    return uct_mm_coll_ep_process_recv(&ep->super, &iface->super, recv_check, 0,
                                       type, use_timestamp, nonzero_ep_offset_id,
                                       nonzero_length, 0, 0,
                                       (const uct_mm_coll_iface_cb_t)
                                       ucs_empty_function_do_assert, NULL);
}

static UCS_F_DEBUG_OR_INLINE unsigned
uct_mm_bcast_iface_progress_ep(uct_mm_base_bcast_iface_t *iface,
                               enum uct_mm_coll_type type, uct_mm_bcast_ep_t *ep,
                               unsigned count_limit, int use_timestamp,
                               int nonzero_ep_offset_id, int nonzero_length)
{
    unsigned count, total_count = 0;

    do {
        count = uct_mm_bcast_ep_poll_fifo(iface, type, ep, use_timestamp,
                                          nonzero_ep_offset_id, nonzero_length);
    } while (ucs_unlikely(count != 0) &&
             ((total_count = total_count + count) < count_limit));

    return total_count;
}

static UCS_F_DEBUG_OR_INLINE unsigned
uct_mm_bcast_iface_progress_common(uct_iface_h tl_iface, enum uct_mm_coll_type type,
                                   int use_cacheline_per_peer, int use_timestamp,
                                   int nonzero_ep_offset_id, int nonzero_length)
{
    unsigned count;
    size_t ep_ptrs_size;
    uct_mm_base_bcast_iface_t *iface = ucs_derived_of(tl_iface,
                                                      uct_mm_base_bcast_iface_t);
    ucs_ptr_array_t *ep_ptrs         = &iface->super.ep_ptrs;
    uct_mm_base_iface_t *mm_iface    = ucs_derived_of(tl_iface,
                                                      uct_mm_base_iface_t);
    uct_mm_bcast_ep_t *checked_ep    = iface->last_nonzero_ep;
    uct_mm_fifo_check_t *recv_check  = ucs_unaligned_ptr(&checked_ep->recv_check);

    ucs_assert(checked_ep != NULL);
    ucs_assert(iface->super.type == type);

    /* Check the (most likely) only endpoint serverd by this interface */
    if ((ucs_ptr_array_get_elem_count(ep_ptrs) == 1) &&
        !uct_mm_iface_fifo_has_new_data(recv_check, 0) &&
        (iface->poll_iface_idx++ &
         checked_ep->recv_check.fifo_release_factor_mask)) {
        return 0;
    }

    UCT_BASE_IFACE_LOCK(iface);

    /* Start by checking the last used endpoint */
    count = uct_mm_bcast_iface_progress_ep(iface, type, checked_ep, mm_iface->fifo_poll_count,
                                           use_timestamp, nonzero_ep_offset_id,
                                           nonzero_length);
    if ((ucs_likely(count > 0)) ||
        (iface->poll_iface_idx++ &
         checked_ep->recv_check.fifo_release_factor_mask)) {
        goto poll_done;
    }

    if (ucs_ptr_array_lookup(ep_ptrs, iface->poll_ep_idx, checked_ep)) {
        count = uct_mm_bcast_iface_progress_ep(iface, type, checked_ep,
                                               mm_iface->fifo_poll_count, use_timestamp,
                                               nonzero_ep_offset_id,
                                               nonzero_length);
    } else {
        count = 0;
    }

    ep_ptrs_size = ep_ptrs->size;
    if ((ucs_likely(ep_ptrs_size > 0)) &&
        (++(iface->poll_ep_idx) == ep_ptrs_size)) {
        iface->poll_ep_idx = 0;
    }

    if (ucs_likely(count == 0)) {
        /* use the time to check if the tail element has been released */
        uct_mm_bcast_ep_poll_tail(iface, type, use_timestamp, nonzero_ep_offset_id);

        /* progress the pending sends (if there are any) */
        ucs_arbiter_dispatch(&mm_iface->arbiter, 1, uct_mm_ep_process_pending,
                             &count);
    } else {
        iface->last_nonzero_ep = checked_ep;
    }

    uct_mm_iface_fifo_window_adjust(mm_iface, count);

poll_done:
    UCT_BASE_IFACE_UNLOCK(iface);

    return count;
}

static UCS_F_DEBUG_OR_INLINE size_t
memcpy_len_zero_no_ts(void *dst, const void *src, size_t length)
{
    ucs_assert(!length);
    return 0;
}

static UCS_F_DEBUG_OR_INLINE void uct_mm_coll_iface_cb_set_ts1(void *dst)
{
    *(uint64_t*)dst = ucs_arch_read_hres_clock();
}

static UCS_F_DEBUG_OR_INLINE size_t
memcpy_len_zero_ts(void *dst, const void *src, size_t length)
{
    ucs_assert(!length);
    uct_mm_coll_iface_cb_set_ts1(UCS_PTR_BYTE_OFFSET(dst, 0));
    return 0;
}

static UCS_F_DEBUG_OR_INLINE size_t
memcpy_len_nonzero_no_ts(void *dst, const void *src, size_t length)
{
#ifdef USE_NONTEMPORAL_MEMCPY
    ucs_memcpy_nontemporal(dst, src, length);
#else
    memcpy(dst, src, length);
#endif
    return length;
}

static UCS_F_DEBUG_OR_INLINE size_t
memcpy_len_nonzero_ts(void *dst, const void *src, size_t length)
{
#ifdef USE_NONTEMPORAL_MEMCPY
    ucs_memcpy_nontemporal(dst, src, length);
#else
    memcpy(dst, src, length);
#endif
    uct_mm_coll_iface_cb_set_ts1(UCS_PTR_BYTE_OFFSET(dst, length));
    return length;
}

#define MM_COLL_EP_USE_CL_cl          (1)
#define MM_COLL_EP_USE_CL_no_cl       (0)
#define MM_COLL_EP_USE_TS_ts          (1)
#define MM_COLL_EP_USE_TS_no_ts       (0)
#define MM_COLL_EP_OFFSET_id_nonzero  (1)
#define MM_COLL_EP_OFFSET_id_zero     (0)
#define MM_COLL_EP_LENGTH_len_nonzero (1)
#define MM_COLL_EP_LENGTH_len_zero    (0)

#define UCT_MM_COLL_EP_SEND_FUNCS_DIRECTION(_lower_name, _upper_name, _len_suffix, \
                                            _id_suffix, _ts_suffix, _cl_suffix, \
                                            _dir, _var) \
ucs_status_t \
uct_mm_##_dir##_ep_am_short##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_lower_name \
    (uct_ep_h ep, uint8_t id, uint64_t header, const void *payload, unsigned length) \
{ \
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, (_var), \
                                   UCT_MM_COLL_TYPE_ ## _upper_name, \
                                   MM_COLL_EP_USE_CL##_cl_suffix, \
                                   MM_COLL_EP_USE_TS##_ts_suffix, \
                                   MM_COLL_EP_OFFSET##_id_suffix, \
                                   MM_COLL_EP_LENGTH##_len_suffix, \
                                   0, memcpy##_len_suffix##_ts_suffix, NULL); \
} \
\
ssize_t \
uct_mm_##_dir##_ep_am_bcopy##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_lower_name \
    (uct_ep_h ep, uint8_t id, uct_pack_callback_t pack_cb, void *arg, unsigned flags) \
{ \
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, (_var), \
                                   UCT_MM_COLL_TYPE_ ## _upper_name, \
                                   MM_COLL_EP_USE_CL##_cl_suffix, \
                                   MM_COLL_EP_USE_TS##_ts_suffix, \
                                   MM_COLL_EP_OFFSET##_id_suffix, \
                                   MM_COLL_EP_LENGTH##_len_suffix, \
                                   0, NULL, UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL); \
}

#define       UCT_MM_COLL_EP_SEND_FUNCS(_lower_name, _upper_name, _len_suffix, _id_suffix, _ts_suffix, _cl_suffix) \
    UCT_MM_COLL_EP_SEND_FUNCS_DIRECTION(_lower_name, _upper_name, _len_suffix, _id_suffix, _ts_suffix, _cl_suffix, bcast,  1) \
    UCT_MM_COLL_EP_SEND_FUNCS_DIRECTION(_lower_name, _upper_name, _len_suffix, _id_suffix, _ts_suffix, _cl_suffix, incast, 0) \
\
unsigned \
uct_mm_bcast_iface_progress##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_lower_name \
    (uct_iface_h tl_iface) \
{ \
    return uct_mm_bcast_iface_progress_common(tl_iface, \
                                              UCT_MM_COLL_TYPE_ ## _upper_name, \
                                              MM_COLL_EP_USE_CL##_cl_suffix, \
                                              MM_COLL_EP_USE_TS##_ts_suffix, \
                                              MM_COLL_EP_OFFSET##_id_suffix, \
                                              MM_COLL_EP_LENGTH##_len_suffix); \
} \
\
ucs_status_t \
uct_mm_incast_ep_am_short_ext_cb##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_lower_name \
    (uct_ep_h ep, uint8_t id, uint64_t header, const void *payload, unsigned length) \
{ \
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 0, \
                                   UCT_MM_COLL_TYPE_ ## _upper_name, \
                                   MM_COLL_EP_USE_CL##_cl_suffix, \
                                   MM_COLL_EP_USE_TS##_ts_suffix, \
                                   MM_COLL_EP_OFFSET##_id_suffix, \
                                   MM_COLL_EP_LENGTH##_len_suffix, 1, \
                                   (const uct_mm_coll_iface_cb_t)memcpy##_len_suffix##_ts_suffix, \
                                   uct_mm_coll_ep_am_common_reduce_ext##_ts_suffix); \
} \
\
ssize_t \
uct_mm_incast_ep_am_bcopy_ext_cb##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_lower_name \
    (uct_ep_h ep, uint8_t id, uct_pack_callback_t pack_cb, void *arg, unsigned flags) \
{ \
    int is_aux = UCT_COLL_LENGTH_INFO_UNPACK_VALUE(flags) & UCT_SEND_FLAG_CB_AUX_REDUCE; \
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 0, \
                                   UCT_MM_COLL_TYPE_ ## _upper_name, \
                                   MM_COLL_EP_USE_CL##_cl_suffix, \
                                   MM_COLL_EP_USE_TS##_ts_suffix, \
                                   MM_COLL_EP_OFFSET##_id_suffix, \
                                   MM_COLL_EP_LENGTH##_len_suffix, 1, \
                                   ucs_likely(is_aux) ? \
                                   uct_mm_coll_ep_am_common_reduce_ext_aux##_ts_suffix : \
                                   uct_mm_coll_ep_am_common_reduce_ext##_ts_suffix, \
                                   ucs_likely(is_aux) ? \
                                   UCT_MM_COLL_EP_REDUCE_CB_TYPE_EXTERNAL_AUX : \
                                   UCT_MM_COLL_EP_REDUCE_CB_TYPE_EXTERNAL); \
} \
\
unsigned \
uct_mm_incast_iface_progress##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_lower_name \
    (uct_iface_h tl_iface) \
{ \
    uct_mm_base_incast_iface_t *iface = ucs_derived_of(tl_iface, \
                                                       uct_mm_base_incast_iface_t); \
    \
    return uct_mm_incast_iface_poll_fifo(iface, UCT_MM_COLL_TYPE_##_upper_name, \
                                         MM_COLL_EP_USE_CL##_cl_suffix, \
                                         MM_COLL_EP_USE_TS##_ts_suffix, \
                                         MM_COLL_EP_OFFSET##_id_suffix, \
                                         MM_COLL_EP_LENGTH##_len_suffix, \
                                         NULL, UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL); \
} \
\
unsigned \
uct_mm_incast_iface_progress_ext_cb##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_lower_name \
    (uct_iface_h tl_iface) \
{ \
    uintptr_t is_aux                  = (uintptr_t)tl_iface & \
                                        UCT_PACK_CALLBACK_REDUCE; \
    uct_mm_base_incast_iface_t *iface = ucs_derived_of((uintptr_t)tl_iface & \
                                                       ~UCT_PACK_CALLBACK_REDUCE, \
                                                       uct_mm_base_incast_iface_t); \
    \
    return uct_mm_incast_iface_poll_fifo(iface, UCT_MM_COLL_TYPE_##_upper_name, \
                                         MM_COLL_EP_USE_CL##_cl_suffix, \
                                         MM_COLL_EP_USE_TS##_ts_suffix, \
                                         MM_COLL_EP_OFFSET##_id_suffix, \
                                         MM_COLL_EP_LENGTH##_len_suffix, \
                                         ucs_likely(is_aux) ? \
                                         uct_mm_coll_ep_am_common_reduce_ext_aux##_ts_suffix : \
                                         uct_mm_coll_ep_am_common_reduce_ext##_ts_suffix, \
                                         ucs_likely(is_aux) ? \
                                         UCT_MM_COLL_EP_REDUCE_CB_TYPE_EXTERNAL_AUX : \
                                         UCT_MM_COLL_EP_REDUCE_CB_TYPE_EXTERNAL); \
} \
\
static UCS_F_DEBUG_OR_INLINE ucs_status_t \
uct_mm_incast_ep_am_short##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_lower_name##_cb \
    (uct_ep_h ep, uint8_t id, uint64_t header, const void *payload, unsigned length, \
     const uct_mm_coll_iface_cb_t memcpy_cb, const uct_mm_coll_iface_cb_t reduce_cb) \
{ \
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 0, \
                                   UCT_MM_COLL_TYPE_ ## _upper_name, \
                                   MM_COLL_EP_USE_CL##_cl_suffix, \
                                   MM_COLL_EP_USE_TS##_ts_suffix, \
                                   MM_COLL_EP_OFFSET##_id_suffix, \
                                   MM_COLL_EP_LENGTH##_len_suffix, 0, \
                                   memcpy_cb, reduce_cb); \
} \
\
static UCS_F_DEBUG_OR_INLINE ssize_t \
uct_mm_incast_ep_am_bcopy##_len_suffix##_id_suffix##_ts_suffix##_cl_suffix##_##_lower_name##_cb \
    (uct_ep_h ep, uint8_t id, uct_pack_callback_t pack_cb, void *arg, unsigned flags, \
     const uct_mm_coll_iface_cb_t reduce_cb) \
{ \
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 0, \
                                   UCT_MM_COLL_TYPE_ ## _upper_name, \
                                   MM_COLL_EP_USE_CL##_cl_suffix, \
                                   MM_COLL_EP_USE_TS##_ts_suffix, \
                                   MM_COLL_EP_OFFSET##_id_suffix, \
                                   MM_COLL_EP_LENGTH##_len_suffix, 1, \
                                   reduce_cb, UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL); \
}

#define ucs_sum(_x, _y) ((_x)+(_y))
#define ucs_atomic_add0(_x, _y)
#define ucs_atomic_add(_x, _y) (0)

/* Below are fixes for "ucs_##_operator##_bits" - should never be called */
#define ucs_sum0(_x, _y)  ucs_assert(0);
#define ucs_sum8(_x, _y)  ucs_assert(0);
#define ucs_sum16(_x, _y) ucs_assert(0);
#define ucs_sum32(_x, _y) ucs_assert(0);
#define ucs_sum64(_x, _y) ucs_assert(0);

#define ucs_min0(_x, _y)  ucs_assert(0);
#define ucs_min8(_x, _y)  ucs_assert(0);
#define ucs_min16(_x, _y) ucs_assert(0);
#define ucs_min32(_x, _y) ucs_assert(0);
#define ucs_min64(_x, _y) ucs_assert(0);

#define ucs_max0(_x, _y)  ucs_assert(0);
#define ucs_max8(_x, _y)  ucs_assert(0);
#define ucs_max16(_x, _y) ucs_assert(0);
#define ucs_max32(_x, _y) ucs_assert(0);
#define ucs_max64(_x, _y) ucs_assert(0);

#define UCT_MM_COLL_CB_NAME(_method, _operator, _operand, _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _suffix) \
    uct_mm_coll_##_method##_##_operator##_##_operand##_##cnt##_cnt##_len##_len_nonzero##_id##_id_nonzero##_ts##_use_ts##_cl##_use_cl##_##_suffix

static UCS_F_DEBUG_OR_INLINE void uct_mm_coll_iface_cb_set_ts0(void *dst) { }

static UCS_F_DEBUG_OR_INLINE void uct_mm_coll_iface_cb_reduce_ts1(void *dst,
                                                                const void *src)
{
    uct_mm_coll_iface_cb_reduce_timestamp((uint64_t*)dst, (const uint64_t*)src);
}

static UCS_F_DEBUG_OR_INLINE void uct_mm_coll_iface_cb_reduce_ts0(void *dst,
                                                                  const void *src) { }

static UCS_F_DEBUG_OR_INLINE void ucs_memcpy_wrapper(void* restrict dst, int ignore,
                                                     const void* restrict src)
{
    memcpy(dst, src, UCS_SYS_CACHE_LINE_SIZE);
}

#ifdef USE_NONTEMPORAL_MEMCPY
#define UCS_MEMCPY_CACHE_LINE ucs_memcpy_nontemporal_cache_line
#define UCS_MEMCPY            ucs_memcpy_nontemporal
#else
#define UCS_MEMCPY_CACHE_LINE ucs_memcpy_wrapper
#define UCS_MEMCPY            memcpy
#endif

#define UCT_MM_COLL_IFACE_DECLARE_CB_INNER(_name, _operator, _caps1, _operand, \
                                           _caps2, _cnt, _len_nonzero, \
                                           _id_nonzero, _use_ts, _use_cl, \
                                           _is_atomic, _bits, _constant_cnt) \
    static UCS_F_DEBUG_OR_INLINE size_t \
    UCT_MM_COLL_CB_NAME(_name##_memcpy, _operator, _operand, _cnt, \
                        _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb) \
        (void* restrict dst, const void* restrict src, size_t length) \
    { \
        ucs_assert((_len_nonzero) == !!length); \
        if ((_cnt) == 0) { \
            ucs_assert((length % sizeof(_operand)) == 0); \
        } else if ((_constant_cnt) != (_cnt)) { \
            length = (_constant_cnt) * sizeof(_operand); \
        } else { \
            ucs_assert(((_cnt) * sizeof(_operand)) == length); \
        } \
        \
        if (_len_nonzero) { \
            ucs_assert(src != dst); \
            if (_use_cl && ((_cnt) * sizeof(_operand) <= UCS_SYS_CACHE_LINE_SIZE)) { \
                ucs_assert(((uintptr_t)src % UCS_SYS_CACHE_LINE_SIZE) == 0); \
                ucs_assert(((uintptr_t)dst % UCS_SYS_CACHE_LINE_SIZE) == 0); \
                UCS_MEMCPY_CACHE_LINE(dst, 0 /* to temp ntb */, src); \
            } else { \
                UCS_MEMCPY(dst, src, length); \
            } \
        } else { \
            ucs_assert(length == 0); \
        } \
        \
        uct_mm_coll_iface_cb_set_ts ## _use_ts (UCS_PTR_BYTE_OFFSET(dst, length)); \
        \
        return length; \
    } \
    \
    static UCS_F_DEBUG_OR_INLINE size_t \
    UCT_MM_COLL_CB_NAME(_name##_reduce, _operator, _operand, _cnt, \
                        _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb) \
        (void* restrict dst, const void* restrict src, size_t length) \
    { \
        UCS_V_UNUSED unsigned idx, max; \
        UCS_V_UNUSED _operand* restrict dst_op = (_operand* restrict)dst; \
        UCS_V_UNUSED _operand* restrict src_op = (_operand* restrict)src; \
        \
        if ((_cnt) == 0) { \
            max = length / sizeof(_operand); \
            ucs_assert((length % sizeof(_operand)) == 0); \
        } else { \
            max = (_constant_cnt); \
            if ((_constant_cnt) != (_cnt)) { \
                /* bcopy callback, where we need to return the size */ \
                length = (_constant_cnt) * sizeof(_operand); \
            } else if ((!_is_atomic) && (_len_nonzero) && (_use_cl) && \
                       (ucs_popcount(_constant_cnt) > 1) && \
                       ((_constant_cnt * sizeof(_operand)) < \
                        UCS_SYS_CACHE_LINE_SIZE)) { \
                max = ucs_align_up_pow2(_constant_cnt, 1); \
            } \
        } \
        \
        if (_len_nonzero) { \
            ucs_assert(src != dst); \
            if (_use_cl) { \
                ucs_assert(((uintptr_t)src % UCS_SYS_CACHE_LINE_SIZE) == 0); \
                ucs_assert(((uintptr_t)dst % UCS_SYS_CACHE_LINE_SIZE) == 0); \
                \
                dst_op = (_operand* restrict) \
                         __builtin_assume_aligned(dst, UCS_SYS_CACHE_LINE_SIZE); \
                src_op = (_operand* restrict) \
                         __builtin_assume_aligned(src, UCS_SYS_CACHE_LINE_SIZE); \
            } else { \
                dst_op = (_operand* restrict)dst; \
                src_op = (_operand* restrict)src; \
            } \
            \
            for (idx = 0; idx < max; idx++) { \
                if (_is_atomic) { \
                    ucs_##_operator##_bits (&dst_op[idx], src_op[idx]); \
                } else { \
                    dst_op[idx] = ucs_##_operator(dst_op[idx], src_op[idx]); \
                } \
            } \
        } else { \
            ucs_assert(length == 0); \
        } \
        \
        uct_mm_coll_iface_cb_reduce_ts ## _use_ts (dst, src); \
        \
        return length; \
    }

#define UCT_MM_COLL_IFACE_DECLARE_CB(_operator, _caps1, _operand, _caps2, _cnt, \
                                     _len_nonzero, _id_nonzero, _use_ts, _use_cl, \
                                     _is_atomic, _lower_name, _upper_name, _bits) \
    \
    UCT_MM_COLL_IFACE_DECLARE_CB_INNER(inl, _operator, _caps1, _operand, _caps2, \
                                       _cnt, _len_nonzero, _id_nonzero, _use_ts, \
                                       _use_cl, _is_atomic, _bits, _cnt) \
    \
    UCT_MM_COLL_IFACE_DECLARE_CB_INNER(inl_extra, _operator, _caps1, _operand, \
                                       _caps2, _cnt, _len_nonzero, _id_nonzero, \
                                       _use_ts, _use_cl, _is_atomic, _bits, \
                                       ((UCT_MM_COLL_MAX_COUNT_SUPPORTED - 1) \
                                       << (_cnt + 1)))

#define UCT_MM_COLL_IFACE_DECLARE_API(_operator, _caps1, _operand, _caps2, _cnt, \
                                      _len_nonzero, _id_nonzero, _use_ts, _use_cl, \
                                      _is_atomic, _lower_name, _upper_name, _bits) \
    \
    ucs_status_t \
    UCT_MM_COLL_CB_NAME(incast_send_short, _operator, _operand, _cnt, _len_nonzero, \
                        _id_nonzero, _use_ts, _use_cl, _lower_name) \
        (uct_ep_h ep, uint8_t id, uint64_t h, const void *p, unsigned l) \
    { \
        const uct_mm_coll_iface_cb_t mcb = UCT_MM_COLL_CB_NAME(inl_memcpy, \
            _operator, _operand, _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb); \
        const uct_mm_coll_iface_cb_t rcb = UCT_MM_COLL_CB_NAME(inl_reduce, \
            _operator, _operand, _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb); \
        \
        return _len_nonzero ? \
            (_id_nonzero ? \
              (_use_ts ? \
                (_use_cl ? uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_ts_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb) : \
                           uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_ts_no_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb)) : \
                (_use_cl ? uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_no_ts_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb) : \
                           uct_mm_incast_ep_am_short_len_nonzero_id_nonzero_no_ts_no_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb))) : \
              (_use_ts ? \
                (_use_cl ? uct_mm_incast_ep_am_short_len_nonzero_id_zero_ts_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb) : \
                           uct_mm_incast_ep_am_short_len_nonzero_id_zero_ts_no_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb)) : \
                (_use_cl ? uct_mm_incast_ep_am_short_len_nonzero_id_zero_no_ts_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb) : \
                           uct_mm_incast_ep_am_short_len_nonzero_id_zero_no_ts_no_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb)))) : \
            (_id_nonzero ? \
              (_use_ts ? \
                (_use_cl ? uct_mm_incast_ep_am_short_len_zero_id_nonzero_ts_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb) : \
                           uct_mm_incast_ep_am_short_len_zero_id_nonzero_ts_no_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb)) : \
                (_use_cl ? uct_mm_incast_ep_am_short_len_zero_id_nonzero_no_ts_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb) : \
                           uct_mm_incast_ep_am_short_len_zero_id_nonzero_no_ts_no_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb))) : \
              (_use_ts ? \
                (_use_cl ? uct_mm_incast_ep_am_short_len_zero_id_zero_ts_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb) : \
                           uct_mm_incast_ep_am_short_len_zero_id_zero_ts_no_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb)) : \
                (_use_cl ? uct_mm_incast_ep_am_short_len_zero_id_zero_no_ts_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb) : \
                           uct_mm_incast_ep_am_short_len_zero_id_zero_no_ts_no_cl_##_lower_name##_cb(ep, id, h, p, l, mcb, rcb)))); \
    } \
    \
    ssize_t \
    UCT_MM_COLL_CB_NAME(incast_send_bcopy, _operator, _operand, _cnt, _len_nonzero, \
                        _id_nonzero, _use_ts, _use_cl, _lower_name) \
        (uct_ep_h ep, uint8_t id, uct_pack_callback_t p, void *a, unsigned f) \
    { \
        const uct_mm_coll_iface_cb_t rcb = UCT_MM_COLL_CB_NAME(inl_extra_reduce, \
            _operator, _operand, _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb); \
        \
        return _len_nonzero ? \
            (_id_nonzero ? \
              (_use_ts ? \
                (_use_cl ? uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_ts_cl_##_lower_name##_cb(ep, id, p, a, f, rcb) : \
                           uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_ts_no_cl_##_lower_name##_cb(ep, id, p, a, f, rcb)) : \
                (_use_cl ? uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_cl_##_lower_name##_cb(ep, id, p, a, f, rcb) : \
                           uct_mm_incast_ep_am_bcopy_len_nonzero_id_nonzero_no_ts_no_cl_##_lower_name##_cb(ep, id, p, a, f, rcb))) : \
              (_use_ts ? \
                (_use_cl ? uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_ts_cl_##_lower_name##_cb(ep, id, p, a, f, rcb) : \
                           uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_ts_no_cl_##_lower_name##_cb(ep, id, p, a, f, rcb)) : \
                (_use_cl ? uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_no_ts_cl_##_lower_name##_cb(ep, id, p, a, f, rcb) : \
                           uct_mm_incast_ep_am_bcopy_len_nonzero_id_zero_no_ts_no_cl_##_lower_name##_cb(ep, id, p, a, f, rcb)))) : \
            (_id_nonzero ? \
              (_use_ts ? \
                (_use_cl ? uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_ts_cl_##_lower_name##_cb(ep, id, p, a, f, rcb) : \
                           uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_ts_no_cl_##_lower_name##_cb(ep, id, p, a, f, rcb)) : \
                (_use_cl ? uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_no_ts_cl_##_lower_name##_cb(ep, id, p, a, f, rcb) : \
                           uct_mm_incast_ep_am_bcopy_len_zero_id_nonzero_no_ts_no_cl_##_lower_name##_cb(ep, id, p, a, f, rcb))) : \
              (_use_ts ? \
                (_use_cl ? uct_mm_incast_ep_am_bcopy_len_zero_id_zero_ts_cl_##_lower_name##_cb(ep, id, p, a, f, rcb) : \
                           uct_mm_incast_ep_am_bcopy_len_zero_id_zero_ts_no_cl_##_lower_name##_cb(ep, id, p, a, f, rcb)) : \
                (_use_cl ? uct_mm_incast_ep_am_bcopy_len_zero_id_zero_no_ts_cl_##_lower_name##_cb(ep, id, p, a, f, rcb) : \
                           uct_mm_incast_ep_am_bcopy_len_zero_id_zero_no_ts_no_cl_##_lower_name##_cb(ep, id, p, a, f, rcb)))); \
    } \
    \
    unsigned \
    UCT_MM_COLL_CB_NAME(incast_progress_short, _operator, _operand, _cnt, _len_nonzero, \
                        _id_nonzero, _use_ts, _use_cl, _lower_name) \
        (uct_iface_h tl_iface) \
    { \
        uct_mm_base_incast_iface_t *iface = \
            ucs_derived_of(tl_iface, uct_mm_base_incast_iface_t); \
        const uct_mm_coll_iface_cb_t reduce_cb = \
            UCT_MM_COLL_CB_NAME(inl_reduce, _operator, _operand, _cnt, \
                                _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb); \
    \
        return uct_mm_incast_iface_poll_fifo(iface, UCT_MM_COLL_TYPE_##_upper_name, \
                                             _use_ts, _use_cl, _id_nonzero, _len_nonzero, \
                                             reduce_cb, UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL); \
    } \
    \
    unsigned \
    UCT_MM_COLL_CB_NAME(incast_progress_bcopy, _operator, _operand, _cnt, _len_nonzero, \
                        _id_nonzero, _use_ts, _use_cl, _lower_name) \
        (uct_iface_h tl_iface) \
    { \
        uct_mm_base_incast_iface_t *iface = \
            ucs_derived_of(tl_iface, uct_mm_base_incast_iface_t); \
        const uct_mm_coll_iface_cb_t reduce_cb = \
            UCT_MM_COLL_CB_NAME(inl_extra_reduce, _operator, _operand, _cnt, \
                                _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb); \
    \
        return uct_mm_incast_iface_poll_fifo(iface, UCT_MM_COLL_TYPE_##_upper_name, \
                                             _use_ts, _use_cl, _id_nonzero, _len_nonzero, \
                                             reduce_cb, UCT_MM_COLL_EP_REDUCE_CB_TYPE_INTERNAL); \
    }

#define UCT_MM_COLL_IFACE_DECLARE_INIT(_operator, _caps1, _operand, _caps2, _cnt, \
                                       _len_nonzero, _id_nonzero, _use_ts, _use_cl, \
                                       _is_atomic, _lower_name, _upper_name, _bits) \
    uct_mm_coll_ep_memcpy_basic_callback_func_arr[_len_nonzero][_use_ts][_use_cl] \
                                                 [UCT_COLL_OPERAND_##_caps2][_cnt] = \
        UCT_MM_COLL_CB_NAME(inl_memcpy, _operator, _operand, _cnt, \
                            _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb); \
    \
    uct_mm_coll_ep_reduce_basic_callback_func_arr[_len_nonzero][_use_ts][_use_cl] \
                                                 [UCT_COLL_OPERATOR_##_caps1] \
                                                 [UCT_COLL_OPERAND_##_caps2][_cnt] = \
        UCT_MM_COLL_CB_NAME(inl_reduce, _operator, _operand, _cnt, \
                            _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb); \
    \
    uct_mm_coll_ep_memcpy_extra_callback_func_arr[_len_nonzero][_use_ts][_use_cl] \
                                                 [UCT_COLL_OPERAND_##_caps2][_cnt] = \
        UCT_MM_COLL_CB_NAME(inl_extra_memcpy, _operator, _operand, _cnt, \
                            _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb); \
    \
    uct_mm_coll_ep_reduce_extra_callback_func_arr[_len_nonzero][_use_ts][_use_cl] \
                                                 [UCT_COLL_OPERATOR_##_caps1] \
                                                 [UCT_COLL_OPERAND_##_caps2][_cnt] = \
        UCT_MM_COLL_CB_NAME(inl_extra_reduce, _operator, _operand, _cnt, \
                            _len_nonzero, _id_nonzero, _use_ts, _use_cl, cb); \
    \
    uct_mm_incast_ep_am_short_func_arr[_len_nonzero][_id_nonzero][_use_ts] \
                                      [_use_cl][UCT_COLL_OPERATOR_##_caps1] \
                                      [UCT_COLL_OPERAND_##_caps2][_cnt] \
                                      [UCT_MM_COLL_TYPE_##_upper_name] = \
        UCT_MM_COLL_CB_NAME(incast_send_short, _operator, _operand, _cnt, \
                            _len_nonzero, _id_nonzero, _use_ts, _use_cl, \
                            _lower_name); \
    \
    uct_mm_incast_ep_am_bcopy_func_arr[_len_nonzero][_id_nonzero][_use_ts] \
                                      [_use_cl][UCT_COLL_OPERATOR_##_caps1] \
                                      [UCT_COLL_OPERAND_##_caps2][_cnt] \
                                      [UCT_MM_COLL_TYPE_##_upper_name] = \
        UCT_MM_COLL_CB_NAME(incast_send_bcopy, _operator, _operand, _cnt, \
                            _len_nonzero, _id_nonzero, _use_ts, _use_cl, \
                            _lower_name); \
    \
    uct_mm_incast_iface_progress_short_func_arr[_len_nonzero][_id_nonzero][_use_ts] \
                                               [_use_cl][UCT_COLL_OPERATOR_##_caps1] \
                                               [UCT_COLL_OPERAND_##_caps2][_cnt] \
                                               [UCT_MM_COLL_TYPE_##_upper_name] = \
        UCT_MM_COLL_CB_NAME(incast_progress_short, _operator, _operand, _cnt, \
                            _len_nonzero, _id_nonzero, _use_ts, _use_cl, \
                            _lower_name); \
    \
    uct_mm_incast_iface_progress_bcopy_func_arr[_len_nonzero][_id_nonzero][_use_ts] \
                                               [_use_cl][UCT_COLL_OPERATOR_##_caps1] \
                                               [UCT_COLL_OPERAND_##_caps2][_cnt] \
                                               [UCT_MM_COLL_TYPE_##_upper_name] = \
        UCT_MM_COLL_CB_NAME(incast_progress_bcopy, _operator, _operand, _cnt, \
                            _len_nonzero, _id_nonzero, _use_ts, _use_cl, \
                            _lower_name);

#define UCT_MM_COLL_IFACE_DECLARE_BY_OPERATOR(_do, _operator, _caps,      _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, float,    FLOAT,    _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name,  0) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, double,   DOUBLE,   _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name,  0) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, int8_t,   INT8_T,   _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name,  8) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, int16_t,  INT16_T,  _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name, 16) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, int32_t,  INT32_T,  _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name, 32) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, int64_t,  INT64_T,  _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name, 64) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, uint8_t,  UINT8_T,  _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name,  8) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, uint16_t, UINT16_T, _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name, 16) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, uint32_t, UINT32_T, _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name, 32) \
    UCT_MM_COLL_IFACE_DECLARE_##_do(_operator, _caps, uint64_t, UINT64_T, _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, _is_atomic, _lower_name, _upper_name, 64)

#if ENABLE_DEBUG_DATA /* Reduce built-time for debug builds... */
#define UCT_MM_COLL_IFACE_DECLARE_BY_CNT(_do, _lower_name, _upper_name, _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl) \
    UCT_MM_COLL_IFACE_DECLARE_BY_OPERATOR(_do, sum,        SUM,         _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, 0, _lower_name, _upper_name) \
    UCT_MM_COLL_IFACE_DECLARE_BY_OPERATOR(_do, atomic_add, SUM_ATOMIC,  _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, 1, _lower_name, _upper_name)
#else
#define UCT_MM_COLL_IFACE_DECLARE_BY_CNT(_do, _lower_name, _upper_name, _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl) \
    UCT_MM_COLL_IFACE_DECLARE_BY_OPERATOR(_do, min,        MIN,         _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, 0, _lower_name, _upper_name) \
    UCT_MM_COLL_IFACE_DECLARE_BY_OPERATOR(_do, max,        MAX,         _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, 0, _lower_name, _upper_name) \
    UCT_MM_COLL_IFACE_DECLARE_BY_OPERATOR(_do, sum,        SUM,         _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, 0, _lower_name, _upper_name) \
    UCT_MM_COLL_IFACE_DECLARE_BY_OPERATOR(_do, atomic_add, SUM_ATOMIC,  _cnt, _len_nonzero, _id_nonzero, _use_ts, _use_cl, 1, _lower_name, _upper_name)
#endif

#define UCT_MM_COLL_IFACE_DECLARE_BY_CL(_do, _lower_name, _upper_name, _cnt, _len_nonzero, _id_nonzero, _use_ts) \
       UCT_MM_COLL_IFACE_DECLARE_BY_CNT(_do, _lower_name, _upper_name, _cnt, _len_nonzero, _id_nonzero, _use_ts, 1) \
       UCT_MM_COLL_IFACE_DECLARE_BY_CNT(_do, _lower_name, _upper_name, _cnt, _len_nonzero, _id_nonzero, _use_ts, 0)

#define UCT_MM_COLL_IFACE_DECLARE_BY_TS(_do, _lower_name, _upper_name, _cnt, _len_nonzero, _id_nonzero) \
        UCT_MM_COLL_IFACE_DECLARE_BY_CL(_do, _lower_name, _upper_name, _cnt, _len_nonzero, _id_nonzero, 1) \
        UCT_MM_COLL_IFACE_DECLARE_BY_CL(_do, _lower_name, _upper_name, _cnt, _len_nonzero, _id_nonzero, 0)

#define UCT_MM_COLL_IFACE_DECLARE_BY_ID(_do, _lower_name, _upper_name, _cnt, _len_nonzero) \
        UCT_MM_COLL_IFACE_DECLARE_BY_TS(_do, _lower_name, _upper_name, _cnt, _len_nonzero, 1) \
        UCT_MM_COLL_IFACE_DECLARE_BY_TS(_do, _lower_name, _upper_name, _cnt, _len_nonzero, 0)

#define UCT_MM_COLL_IFACE_DECLARE_BY_LEN(_do, _lower_name, _upper_name, _cnt) \
         UCT_MM_COLL_IFACE_DECLARE_BY_ID(_do, _lower_name, _upper_name, _cnt, 1) \
         UCT_MM_COLL_IFACE_DECLARE_BY_ID(_do, _lower_name, _upper_name, _cnt, 0)

#define UCT_MM_COLL_IFACE_DECLARE_BY_TYPE(_do, _lower_name, _upper_name) \
    UCS_PP_FOREACH_NUM(UCT_MM_COLL_IFACE_DECLARE_BY_LEN, \
                       UCT_MM_COLL_MAX_COUNT_SUPPORTED, \
                       _do, _lower_name, _upper_name)

#define UCT_MM_COLL_EP_SEND_FUNCS_BY_TS(_lower_name, _upper_name, _len_nonzero, _id_nonzero, _ts_suffix) \
              UCT_MM_COLL_EP_SEND_FUNCS(_lower_name, _upper_name, _len_nonzero, _id_nonzero, _ts_suffix, _cl) \
              UCT_MM_COLL_EP_SEND_FUNCS(_lower_name, _upper_name, _len_nonzero, _id_nonzero, _ts_suffix, _no_cl)

#define UCT_MM_COLL_EP_SEND_FUNCS_BY_ID(_lower_name, _upper_name, _len_nonzero, _id_nonzero) \
        UCT_MM_COLL_EP_SEND_FUNCS_BY_TS(_lower_name, _upper_name, _len_nonzero, _id_nonzero, _ts) \
        UCT_MM_COLL_EP_SEND_FUNCS_BY_TS(_lower_name, _upper_name, _len_nonzero, _id_nonzero, _no_ts)

#define UCT_MM_COLL_EP_SEND_FUNCS_BY_LEN(_lower_name, _upper_name, _len_nonzero) \
         UCT_MM_COLL_EP_SEND_FUNCS_BY_ID(_lower_name, _upper_name, _len_nonzero, _id_zero) \
         UCT_MM_COLL_EP_SEND_FUNCS_BY_ID(_lower_name, _upper_name, _len_nonzero, _id_nonzero)

#define UCT_MM_COLL_IFACE_DEFINE_TYPE(_lower_name, _upper_name) \
    UCT_MM_COLL_EP_SEND_FUNCS_BY_LEN(_lower_name, _upper_name, _len_zero) \
    UCT_MM_COLL_EP_SEND_FUNCS_BY_LEN(_lower_name, _upper_name, _len_nonzero) \
    UCT_MM_COLL_IFACE_DECLARE_BY_TYPE(API, _lower_name, _upper_name)

UCT_MM_COLL_IFACE_DECLARE_BY_TYPE(CB, ignored, IGNORED)
#if HAVE_SM_COLL_EXTRA
UCT_MM_COLL_IFACE_DEFINE_TYPE(locked,        LOCKED)
UCT_MM_COLL_IFACE_DEFINE_TYPE(atomic,        ATOMIC)
UCT_MM_COLL_IFACE_DEFINE_TYPE(hypothetic,    HYPOTHETIC)
UCT_MM_COLL_IFACE_DEFINE_TYPE(counted_slots, COUNTED_SLOTS)
#endif
UCT_MM_COLL_IFACE_DEFINE_TYPE(flagged_slots, FLAGGED_SLOTS)
UCT_MM_COLL_IFACE_DEFINE_TYPE(collaborative, COLLABORATIVE)

/**
 * To accelerate some instances of the transport, where the send/reduction size
 * is known in advance, these per-operator per-operand functions contain pre-defined
 */
uct_mm_coll_iface_cb_t uct_mm_coll_ep_memcpy_basic_callback_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED];

uct_mm_coll_iface_cb_t uct_mm_coll_ep_reduce_basic_callback_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED];

uct_mm_coll_iface_cb_t uct_mm_coll_ep_memcpy_extra_callback_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]; /* Note: in this case it's process count */

uct_mm_coll_iface_cb_t uct_mm_coll_ep_reduce_extra_callback_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]; /* Note: in this case it's process count */

typeof(uct_ep_am_short_func_t) uct_mm_incast_ep_am_short_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_OFFSET_ID_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]
    [UCT_MM_COLL_TYPE_LAST];

typeof(uct_ep_am_bcopy_func_t) uct_mm_incast_ep_am_bcopy_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_OFFSET_ID_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]
    [UCT_MM_COLL_TYPE_LAST];

typeof(uct_iface_progress_func_t) uct_mm_incast_iface_progress_short_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_OFFSET_ID_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]
    [UCT_MM_COLL_TYPE_LAST];

typeof(uct_iface_progress_func_t) uct_mm_incast_iface_progress_bcopy_func_arr
    [UCT_MM_COLL_TX_LENGTH_USAGE]
    [UCT_MM_COLL_OFFSET_ID_USAGE]
    [UCT_MM_COLL_TIMESTAMP_USAGE]
    [UCT_MM_COLL_CACHELINE_USAGE]
    [UCT_COLL_OPERATOR_LAST]
    [UCT_COLL_OPERAND_LAST]
    [UCT_MM_COLL_MAX_COUNT_SUPPORTED]
    [UCT_MM_COLL_TYPE_LAST];

static ucs_init_once_t uct_mm_coll_init_incast_once = UCS_INIT_ONCE_INITIALIZER;

#define UCT_MM_COLL_INIT_ZEROS(_x) memset(_x, 0, sizeof(_x))

void uct_mm_coll_ep_init_incast_cb_arrays() {
    UCS_INIT_ONCE(&uct_mm_coll_init_incast_once) {
        UCT_MM_COLL_INIT_ZEROS(uct_mm_coll_ep_memcpy_basic_callback_func_arr);
        UCT_MM_COLL_INIT_ZEROS(uct_mm_coll_ep_reduce_basic_callback_func_arr);
        UCT_MM_COLL_INIT_ZEROS(uct_mm_coll_ep_memcpy_extra_callback_func_arr);
        UCT_MM_COLL_INIT_ZEROS(uct_mm_coll_ep_reduce_extra_callback_func_arr);

#if HAVE_SM_COLL_EXTRA
        UCT_MM_COLL_IFACE_DECLARE_BY_TYPE(INIT, locked,        LOCKED)
        UCT_MM_COLL_IFACE_DECLARE_BY_TYPE(INIT, atomic,        ATOMIC)
        UCT_MM_COLL_IFACE_DECLARE_BY_TYPE(INIT, hypothetic,    HYPOTHETIC)
        UCT_MM_COLL_IFACE_DECLARE_BY_TYPE(INIT, counted_slots, COUNTED_SLOTS)
#endif
        UCT_MM_COLL_IFACE_DECLARE_BY_TYPE(INIT, flagged_slots, FLAGGED_SLOTS)
        UCT_MM_COLL_IFACE_DECLARE_BY_TYPE(INIT, collaborative, COLLABORATIVE)
    }
}

unsigned uct_mm_coll_ep_get_extra_index(unsigned op_count)
{
    if ((UCT_MM_COLL_MAX_COUNT_SUPPORTED < 2) ||
        (ucs_popcount(UCT_MM_COLL_MAX_COUNT_SUPPORTED) != 1) ||
        (ucs_popcount(op_count) != 1)) {
        return 0;
    }

    return ucs_count_trailing_zero_bits(op_count) -
           ucs_count_trailing_zero_bits(UCT_MM_COLL_MAX_COUNT_SUPPORTED - 1);
}

ucs_status_t uct_reduction_get_callbacks(uct_incast_operator_t operator,
                                         uct_incast_operand_t operand,
                                         unsigned op_count, int set_timestamp,
                                         int input_buffers_are_cache_aligned,
                                         int does_zero_op_count_means_unknown,
                                         uct_reduction_internal_cb_t *memcpy_p,
                                         uct_reduction_internal_cb_t *reduce_p)
{
    int is_aligned       = input_buffers_are_cache_aligned;
    int is_len_nonzero   = (op_count != 0) || does_zero_op_count_means_unknown;
    unsigned extra_index = uct_mm_coll_ep_get_extra_index(op_count);

    uct_mm_coll_ep_init_incast_cb_arrays();

    if (operator == UCT_COLL_OPERATOR_EXTERNAL) {
        return UCS_ERR_UNSUPPORTED;
    }

    if ((set_timestamp > 1) ||
        (set_timestamp < 0) ||
        (operator >= UCT_COLL_OPERATOR_LAST) ||
        (operand  >= UCT_COLL_OPERAND_LAST)) {
        return UCS_ERR_INVALID_PARAM;
    }

    if (op_count < UCT_MM_COLL_MAX_COUNT_SUPPORTED) {
        *memcpy_p = uct_mm_coll_ep_memcpy_basic_callback_func_arr[is_len_nonzero]
                                                                 [set_timestamp]
                                                                 [is_aligned]
                                                                 [operand]
                                                                 [op_count];
        *reduce_p = uct_mm_coll_ep_reduce_basic_callback_func_arr[is_len_nonzero]
                                                                 [set_timestamp]
                                                                 [is_aligned]
                                                                 [operator]
                                                                 [operand]
                                                                 [op_count];
    } else if ((ucs_popcount(op_count) == 1) &&
               (extra_index < UCT_MM_COLL_MAX_COUNT_SUPPORTED)) {
        // TODO: as an optimization, for x=67 use 64+loop so that most sizes get accelerated
        *memcpy_p = uct_mm_coll_ep_memcpy_extra_callback_func_arr[1]
                                                                 [set_timestamp]
                                                                 [is_aligned]
                                                                 [operand]
                                                                 [extra_index];
        *reduce_p = uct_mm_coll_ep_reduce_extra_callback_func_arr[1]
                                                                 [set_timestamp]
                                                                 [is_aligned]
                                                                 [operator]
                                                                 [operand]
                                                                 [extra_index];
    } else {
        *memcpy_p = uct_mm_coll_ep_memcpy_basic_callback_func_arr[1]
                                                                 [set_timestamp]
                                                                 [is_aligned]
                                                                 [operand][0];
        *reduce_p = uct_mm_coll_ep_reduce_basic_callback_func_arr[1]
                                                                 [set_timestamp]
                                                                 [is_aligned]
                                                                 [operator]
                                                                 [operand][0];
    }

    return (!*memcpy_p || !*reduce_p) ? UCS_ERR_UNSUPPORTED : UCS_OK;
}
