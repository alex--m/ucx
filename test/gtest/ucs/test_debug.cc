/**
* Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2012. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#include <common/test.h>
extern "C" {
#include <ucs/debug/debug_int.h>
#include <ucs/sys/compiler.h>
#include <ucs/sys/sys.h>
}

#include <dlfcn.h>

extern "C" {

void UCS_F_NOINLINE my_cool_function(unsigned *lineno) { *lineno = __LINE__; };

}

class test_debug : public ucs::test {
};

std::string __basename(const std::string& path) {
    char *p = strdup(path.c_str());
    std::string bn(::basename(p));
    free(p);
    return bn;
}

UCS_TEST_F(test_debug, lookup_ucs_func) {
    const char sym[] = "ucs_log_flush";

    ucs_debug_address_info info;
    ucs_status_t status = ucs_debug_lookup_address(dlsym(RTLD_DEFAULT, sym), &info);
    ASSERT_UCS_OK(status);

    EXPECT_NE(std::string::npos, std::string(info.file.path).find("libucs.so"));
#ifdef HAVE_DETAILED_BACKTRACE
    EXPECT_EQ(sym, std::string(info.function));
#endif
}

UCS_TEST_F(test_debug, lookup_invalid) {
    ucs_debug_address_info info;
    ucs_status_t status = ucs_debug_lookup_address((void*)0xffffffffffff, &info);
    EXPECT_EQ(UCS_ERR_NO_ELEM, status);
}

UCS_TEST_SKIP_COND_F(test_debug, lookup_address, BULLSEYE_ON) {
    unsigned lineno;

    my_cool_function(&lineno);

    ucs_debug_address_info info;
    ucs_status_t status = ucs_debug_lookup_address((void*)&my_cool_function,
                                                   &info);
    ASSERT_UCS_OK(status);

    UCS_TEST_MESSAGE << info.source_file << ":" << info.line_number <<
                        " " << info.function << "()";

    EXPECT_NE(std::string::npos, std::string(info.file.path).find("gtest"));

#ifdef HAVE_DETAILED_BACKTRACE
    EXPECT_EQ("my_cool_function", std::string(info.function));
    EXPECT_EQ(lineno, info.line_number);
    EXPECT_EQ(__basename(__FILE__), __basename(info.source_file));
#else
    EXPECT_EQ(0u, info.line_number);
    EXPECT_EQ("???", std::string(info.source_file));
#endif
}

UCS_TEST_F(test_debug, print_backtrace) {
    char *data;
    size_t size;

    FILE *f = open_memstream(&data, &size);
    ucs_debug_print_backtrace(f, 0);
    fclose(f);

    /* Some functions that should appear */
    EXPECT_TRUE(strstr(data, "print_backtrace") != NULL);
#ifdef HAVE_DETAILED_BACKTRACE
    EXPECT_TRUE(strstr(data, "main") != NULL);
#endif

    free(data);
}

static void* test_func_shared(void *trace_ctx)
{
    return ucs_debug_get_ctx_by_trace(trace_ctx);
}

static void* test_func_a(void)
{
    return test_func_shared((void*)&test_func_a);
}

static void* test_func_b(void)
{
    return test_func_shared((void*)&test_func_b);
}

static void* test_func_c(void)
{
    return test_func_shared((void*)&test_func_c);
}

UCS_TEST_F(test_debug, context_per_trace) {
    EXPECT_EQ(NULL, ucs_debug_get_ctx_by_trace(NULL));
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(&test_func_a, test_func_a());
        EXPECT_EQ(&test_func_b, test_func_b());
        EXPECT_EQ(&test_func_c, test_func_c());
    }
    ucs_debug_clear_traces();
}
