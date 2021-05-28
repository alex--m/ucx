/**
* Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2015. ALL RIGHTS RESERVED.
* Copyright (c) UT-Battelle, LLC. 2015. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#include "test_ucp_memheap.h"

#include <common/mem_buffer.h>
#include <common/test_helpers.h>
#include <ucs/sys/sys.h>


void test_ucp_memheap::init()
{
    ucp_test::init();
    sender().connect(&receiver(), get_ep_params());
}

void test_ucp_memheap::test_xfer(send_func_t send_func, size_t size,
                                 unsigned num_iters, size_t alignment,
                                 ucs_memory_type_t send_mem_type,
                                 ucs_memory_type_t target_mem_type,
                                 unsigned mem_map_flags,
                                 bool is_ep_flush, bool user_memh,
                                 bool is_append, void *arg)
{
    void *atomic_val = NULL; // *send_buf
    ucp_rkey_h rkey, atomic_rkey;
    ucp_mem_map_params_t params;
    ucs_status_t status;
    ptrdiff_t padding;
    ucp_mem_h memheap_memh, atomic_memh, send_memh = NULL;

    ucs_assert(!(mem_map_flags & (UCP_MEM_MAP_ALLOCATE | UCP_MEM_MAP_FIXED)));

    mem_buffer memheap(num_iters * size + alignment, target_mem_type);
    padding = UCS_PTR_BYTE_DIFF(memheap.ptr(),
                                ucs_align_up_pow2_ptr(memheap.ptr(), alignment));
    ucs_assert(padding >= 0);
    ucs_assert(padding < alignment);

    /* Allocate heap */
    params.field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
                        UCP_MEM_MAP_PARAM_FIELD_LENGTH |
                        UCP_MEM_MAP_PARAM_FIELD_FLAGS;
    params.address    = memheap.ptr();
    params.length     = memheap.size();
    params.flags      = mem_map_flags;

    status = ucp_mem_map(receiver().ucph(), &params, &memheap_memh);
    ASSERT_UCS_OK(status);

    mem_buffer::pattern_fill(memheap.ptr(), memheap.size(), ucs::rand(),
                             memheap.mem_type());

    /* Unpack remote key */
    void *rkey_buffer;
    size_t rkey_buffer_size;
    status = ucp_rkey_pack(receiver().ucph(), memheap_memh, &rkey_buffer,
                           &rkey_buffer_size);
    ASSERT_UCS_OK(status);

    status = ucp_ep_rkey_unpack(sender().ep(), rkey_buffer, &rkey);
    ASSERT_UCS_OK(status);

    ucp_rkey_buffer_release(rkey_buffer);

    mem_buffer expected_data(memheap.size(), send_mem_type);

    if (user_memh) {
        params.address = expected_data.ptr();
        params.length  = expected_data.size();
        status         = ucp_mem_map(sender().ucph(), &params, &send_memh);
        ASSERT_UCS_OK(status);
    }

    if (is_append) {
        mem_buffer atomic(2 * sizeof(atomic_val), target_mem_type);
        params.address = atomic.ptr();
        params.length  = atomic.size();
        status         = ucp_mem_map(receiver().ucph(), &params, &atomic_memh);
        ASSERT_UCS_OK(status);

        status = ucp_rkey_pack(receiver().ucph(), atomic_memh, &rkey_buffer,
                               &rkey_buffer_size);
        ASSERT_UCS_OK(status);

        status = ucp_ep_rkey_unpack(sender().ep(), rkey_buffer, &atomic_rkey);
        ASSERT_UCS_OK(status);

        /*
         * The first atomic variable is used (on the receive-side) as the index
         * during append, initialized to the base address of the receive buffer.
         */
        mem_buffer::copy_to(atomic.ptr(), &atomic_val, sizeof(atomic_val),
                            atomic.mem_type());

        /*
         * The second atomic variable is used to store the fetch result on the
         * sender-side during append operations, and is initialized to contain
         * the rkey handle to simplify passing this argument to @ref do_append.
         */
        mem_buffer::copy_to(UCS_PTR_BYTE_OFFSET(atomic.ptr(), sizeof(void*)),
                            &atomic_rkey, sizeof(ucp_rkey_h),
                            atomic.mem_type());

        /* when append is called - this points to the atomic, not the buffer */
        //send_buf = atomic.ptr();
    }

    /* Perform data sends */
    ptrdiff_t offset = padding;
    for (unsigned i = 0; i < num_iters; ++i, offset += size) {
        ucs_assert(offset + size <= memheap.size());

        // if (!is_append) {
        //     send_buf = UCS_PTR_BYTE_OFFSET(memheap.ptr(), offset);
        // }

        (this->*send_func)(size, //send_buf, rkey,
                           UCS_PTR_BYTE_OFFSET(expected_data.ptr(), offset),
                           send_memh,
                           UCS_PTR_BYTE_OFFSET(memheap.ptr(), offset),
                           rkey, arg);
        if (num_errors() > 0) {
            break;
        }
    }

    /* Flush to make sure memheap is synchronized */
    if (is_ep_flush) {
        flush_ep(sender());
    } else {
        flush_worker(sender());
    }

    //mem_buffer::copy_from(&atomic_val, atomic.ptr(), sizeof(atomic_val),
    //                      atomic.mem_type());

    /* Validate data */
    if (!mem_buffer::compare(UCS_PTR_BYTE_OFFSET(expected_data.ptr(), padding),
                             UCS_PTR_BYTE_OFFSET(memheap.ptr(), padding),
                             size * num_iters, send_mem_type, target_mem_type)) {
        ADD_FAILURE() << "data validation failed";
    }

    if (is_append) {
        ucp_rkey_destroy(atomic_rkey);

        status = ucp_mem_unmap(receiver().ucph(), atomic_memh);
        ASSERT_UCS_OK(status);
    }

    ucp_rkey_destroy(rkey);

    status = ucp_mem_unmap(receiver().ucph(), memheap_memh);
    ASSERT_UCS_OK(status);

    if (user_memh) {
        status = ucp_mem_unmap(sender().ucph(), send_memh);
        ASSERT_UCS_OK(status);
    }
}
