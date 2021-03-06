/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2020.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifndef GTEST_MEM_BUFFER_H_
#define GTEST_MEM_BUFFER_H_

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ucs/memory/memory_type.h>
#include <stdint.h>
#include <string>
#include <vector>

extern "C" {
#include <uct/base/uct_md.h> /* for uct_md_h */
}

/**
 * Wrapper and utility functions for memory type buffers, e.g buffers which are
 * not necessarily allocated on host memory, such as cuda, rocm, etc.
 */
class mem_buffer {
public:
    static const std::vector<ucs_memory_type_t>& supported_mem_types();

    /* allocate buffer of a given memory type */
    static void *allocate(size_t size, ucs_memory_type_t mem_type,
                          uct_md_h md = NULL);

    /* release buffer of a given memory type */
    static void release(void *ptr, ucs_memory_type_t mem_type,
                        uct_md_h md = NULL);

    /* fill pattern in a host-accessible buffer */
    static void pattern_fill(void *buffer, size_t length, uint64_t seed);

    /* check pattern in a host-accessible buffer */
    static void pattern_check(const void *buffer, size_t length, uint64_t seed);

    /* check pattern in a host-accessible buffer, take seed from 1st word */
    static void pattern_check(const void *buffer, size_t length);

    /* fill pattern in a memtype buffer */
    static void pattern_fill(void *buffer, size_t length, uint64_t seed,
                             ucs_memory_type_t mem_type);

    /* check pattern in a memtype buffer */
    static void pattern_check(const void *buffer, size_t length, uint64_t seed,
                              ucs_memory_type_t mem_type);

    /* copy from host memory to memtype buffer */
    static void copy_to(void *dst, const void *src, size_t length,
                        ucs_memory_type_t dst_mem_type);

    /* copy from memtype buffer to host memory */
    static void copy_from(void *dst, const void *src, size_t length,
                          ucs_memory_type_t src_mem_type);

    /* compare memtype buffer with host memory, return true if equal */
    static bool compare(const void *expected, const void *buffer,
                        size_t length, ucs_memory_type_t mem_type);

    /* compare when both expected data and buffer can be different mem types */
    static bool compare(const void *expected, const void *buffer,
                        size_t length, ucs_memory_type_t mem_type_expected,
                        ucs_memory_type_t mem_type_buffer);

    /* return the string name of a memory type */
    static std::string mem_type_name(ucs_memory_type_t mem_type);

    /* returns whether any other type of memory besides the CPU is supported */
    static bool is_only_host_mem_supported();

    /* set device context if compiled with GPU support */
    static void set_device_context();

    mem_buffer(size_t size, ucs_memory_type_t mem_type, uct_md_h md = NULL);
    virtual ~mem_buffer();

    ucs_memory_type_t mem_type() const;

    void *ptr() const;

    size_t size() const;

    uct_md_h md() const;

private:
    static void abort_wrong_mem_type(ucs_memory_type_t mem_type);

    static bool is_cuda_supported();

    static bool is_rocm_supported();

    static bool is_dm_supported();

    static uint64_t pat(uint64_t prev);

    const ucs_memory_type_t m_mem_type;
    void * const            m_ptr;
    const size_t            m_size;
#if HAVE_DM
    uct_md_h                m_md;
    struct ibv_dm          *m_dm_obj;
#endif
};


#endif
