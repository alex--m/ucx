/**
* Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2014. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ucs/sys/compiler.h>
#include <ucs/sys/module.h>
#include <ucs/arch/cpu.h>
#include <ucs/config/parser.h>
#include <ucs/config/ucm_opts.h>
#include <ucs/debug/debug_int.h>
#include <ucs/debug/log.h>
#include <ucs/debug/memtrack_int.h>
#include <ucs/profile/profile.h>
#include <ucs/memory/memtype_cache.h>
#include <ucs/memory/numa.h>
#include <ucs/stats/stats.h>
#include <ucs/async/async.h>
#include <ucs/sys/lib.h>
#include <ucs/sys/sys.h>
#include <ucs/sys/topo/base/topo.h>
#include <ucs/sys/math.h>

ucs_cpu_flag_t g_supported_cpu_flags;

/* run-time CPU detection */
static UCS_F_NOOPTIMIZE void ucs_check_cpu_flags(void)
{
    char str[256];
    char *p_str;
    int cpu_flags;
    struct {
        const char* flag;
        ucs_cpu_flag_t value;
        int is_optional;
    } *p_flags,
    cpu_flags_array[] = {
        { "cldemote", UCS_CPU_FLAG_CLDEMOTE, 1 },
        { "clwb",     UCS_CPU_FLAG_CLWB,     1 },
        { "clflush",  UCS_CPU_FLAG_CLFLUSH,  1 },
        { "cmov",     UCS_CPU_FLAG_CMOV,     0 },
        { "mmx",      UCS_CPU_FLAG_MMX,      0 },
        { "mmx2",     UCS_CPU_FLAG_MMX2,     0 },
        { "sse",      UCS_CPU_FLAG_SSE,      0 },
        { "sse2",     UCS_CPU_FLAG_SSE2,     0 },
        { "sse3",     UCS_CPU_FLAG_SSE3,     0 },
        { "ssse3",    UCS_CPU_FLAG_SSSE3,    0 },
        { "sse41",    UCS_CPU_FLAG_SSE41,    0 },
        { "sse42",    UCS_CPU_FLAG_SSE42,    0 },
        { "avx",      UCS_CPU_FLAG_AVX,      0 },
        { "avx2",     UCS_CPU_FLAG_AVX2,     0 },
        { "avx512f",  UCS_CPU_FLAG_AVX512F,  0 },
        { NULL,       UCS_CPU_FLAG_UNKNOWN,  0 }
    };

    cpu_flags = ucs_arch_get_cpu_flag();
    if (UCS_CPU_FLAG_UNKNOWN == cpu_flags) {
        return ;
    }
    strncpy(str, UCS_PP_MAKE_STRING(CPU_FLAGS), sizeof(str) - 1);

    p_str = strtok(str, " |\t\n\r");
    while (p_str) {
        p_flags = cpu_flags_array;
        while (p_flags && p_flags->flag) {
            if (!strcmp(p_str, p_flags->flag)) {
                if (!p_flags->is_optional && !(cpu_flags & p_flags->value)) {
                    fprintf(stderr, "[%s:%d] FATAL: UCX library was compiled with %s"
                            " but CPU does not support it.\n",
                            ucs_get_host_name(), getpid(), p_flags->flag);
                    exit(1);
                }
                break;
            }
            p_flags++;
        }
        if (NULL == p_flags->flag) {
            fprintf(stderr, "[%s:%d] FATAL: UCX library was compiled with %s"
                    " but CPU does not support it.\n",
                    ucs_get_host_name(), getpid(), p_str);
            exit(1);
        }
        p_str = strtok(NULL, " |\t\n\r");
    }

    g_supported_cpu_flags = cpu_flags;
}

static void ucs_modules_load()
{
    UCS_MODULE_FRAMEWORK_DECLARE(ucs);
    UCS_MODULE_FRAMEWORK_LOAD(ucs, UCS_MODULE_LOAD_FLAG_GLOBAL);
}

void UCS_F_CTOR ucs_init()
{
    ucs_status_t status;

    ucs_check_cpu_flags();
    ucs_log_early_init(); /* Must be called before all others */
    ucs_global_opts_init();
    ucs_init_ucm_opts();
    ucs_memtype_cache_global_init();
    ucs_cpu_init();
    ucs_log_init();
#ifdef ENABLE_STATS
    ucs_stats_init();
#endif
    ucs_memtrack_init();
    ucs_debug_init();
    status = ucs_profile_init(ucs_global_opts.profile_mode,
                              ucs_global_opts.profile_file,
                              ucs_global_opts.profile_log_size,
                              &ucs_profile_default_ctx);
    if (status != UCS_OK) {
        ucs_fatal("failed to init ucs profile - aborting");
    }

    ucs_async_global_init();
    ucs_numa_init();
    ucs_topo_init();
    ucs_rand_seed_init();
    ucs_debug("%s loaded at 0x%lx", ucs_sys_get_lib_path(),
              ucs_sys_get_lib_base_addr());
    ucs_debug("cmd line: %s", ucs_get_process_cmdline());
    ucs_modules_load();
}

static void UCS_F_DTOR ucs_cleanup(void)
{
    ucs_topo_cleanup();
    ucs_numa_cleanup();
    ucs_async_global_cleanup();
    ucs_profile_cleanup(ucs_profile_default_ctx);
    ucs_debug_cleanup(0);
    ucs_config_parser_cleanup();
    ucs_memtrack_cleanup();
#ifdef ENABLE_STATS
    ucs_stats_cleanup();
#endif
    ucs_memtype_cache_cleanup();
    ucs_cleanup_ucm_opts();
    ucs_global_opts_cleanup();
    ucs_log_cleanup();
}
