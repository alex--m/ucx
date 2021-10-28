#include <gperftools/tcmalloc.h>
#include <ucs/sys/preprocessor.h>

#ifdef UCM_MALLOC_PREFIX
#define dlcalloc                     UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, calloc)
#define dlfree                       UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, free)
#define dlmalloc                     UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc)
#define dlmemalign                   UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, memalign)
#define dlposix_memalign             UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, posix_memalign)
#define dlrealloc                    UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, realloc)
#define dlvalloc                     UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, valloc)
#define dlpvalloc                    UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, pvalloc)
#define dlmallinfo                   UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, mallinfo)
#define dlmallopt                    UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, mallopt)
#define dlmalloc_trim                UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_trim)
#define dlmalloc_stats               UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_stats)
#define dlmalloc_usable_size         UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_usable_size)
#define dlmalloc_footprint           UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_footprint)
#define dlmalloc_max_footprint       UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_max_footprint)
#define dlmalloc_footprint_limit     UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_footprint_limit)
#define dlmalloc_set_footprint_limit UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_set_footprint_limit)
#define dlmalloc_inspect_all         UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_inspect_all)
#define dlindependent_calloc         UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, independent_calloc)
#define dlindependent_comalloc       UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, independent_comalloc)
#define dlbulk_free                  UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, bulk_free)

#define M_TRIM_THRESHOLD (0)
#define M_MMAP_THRESHOLD (1)
static int ucm_dlmallopt_get(int param) { return -1; }

typeof(tc_calloc)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, calloc)                                           = tc_calloc;
typeof(tc_free)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, free)                                               = tc_free;
typeof(tc_malloc)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc)                                           = tc_malloc;
typeof(tc_memalign)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, memalign)                                       = tc_memalign;
typeof(tc_posix_memalign)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, posix_memalign)                           = tc_posix_memalign;
typeof(tc_realloc)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, realloc)                                         = tc_realloc;
typeof(tc_valloc)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, valloc)                                           = tc_valloc;
typeof(tc_pvalloc)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, pvalloc)                                         = tc_pvalloc;
typeof(tc_mallinfo)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, mallinfo)                                       = tc_mallinfo;
typeof(tc_mallopt)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, mallopt)                                         = tc_mallopt;
typeof(tc_malloc_stats)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_stats)                               = tc_malloc_stats;
typeof(tc_malloc_size)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_usable_size)                          = tc_malloc_size;
//typeof(tc_malloc_trim)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_trim)                               = tc_malloc_trim;
//typeof(tc_malloc_footprint)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_footprint)                     = tc_malloc_footprint;
//typeof(tc_malloc_max_footprint)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_max_footprint)             = tc_malloc_max_footprint;
//typeof(tc_malloc_footprint_limit)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_footprint_limit)         = tc_malloc_footprint_limit;
//typeof(tc_malloc_set_footprint_limit)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_set_footprint_limit) = tc_malloc_set_footprint_limit;
//typeof(tc_malloc_inspect_all)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, malloc_inspect_all)                 = tc_malloc_inspect_all;
//typeof(tc_independent_calloc)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, independent_calloc)                 = tc_independent_calloc;
//typeof(tc_independent_comalloc)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, independent_comalloc)             = tc_independent_comalloc;
//typeof(tc_bulk_free)* UCS_PP_TOKENPASTE(UCM_MALLOC_PREFIX, bulk_free)                                   = tc_bulk_free;
#endif /* UCM_MALLOC_PREFIX */

