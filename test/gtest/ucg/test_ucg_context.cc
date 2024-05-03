/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#include "ucg_test.h"
extern "C" {
#include <ucs/sys/sys.h>
}

UCS_TEST_P(test_ucg_context, minimal_field_mask) {
    ucs::handle<ucg_config_t*> config;
    UCS_TEST_CREATE_HANDLE(ucg_config_t*, config, ucg_config_release,
                           ucg_config_read, NULL, NULL);

    ucs::handle<ucg_context_h> ucgh;
    ucs::handle<ucp_worker_h> worker;
    ucs::handle<ucg_group_h> group;

    {
        /* UCG Context */
        ucp_params_t ucp_params;
        VALGRIND_MAKE_MEM_UNDEFINED(&ucp_params, sizeof(ucp_params));
        ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES;
        ucp_params.features   = get_variant_ctx_params()->ucp.features;

        ucb_params_t ucb_params;
        VALGRIND_MAKE_MEM_UNDEFINED(&ucb_params, sizeof(ucb_params));
        ucb_params.field_mask = 0;
        ucb_params.super      = &ucp_params;

        ucg_params_t ucg_params;
        VALGRIND_MAKE_MEM_UNDEFINED(&ucg_params, sizeof(ucg_params));
        ucg_params.field_mask = 0;
        ucg_params.super      = &ucb_params;

        UCS_TEST_CREATE_HANDLE(ucg_context_h, ucgh, ucg_cleanup,
                               ucg_init, &ucg_params, config.get());
    }

    {
        /* UCP Worker */
        ucp_worker_params_t params;
        VALGRIND_MAKE_MEM_UNDEFINED(&params, sizeof(params));
        params.field_mask = 0;

        UCS_TEST_CREATE_HANDLE(ucp_worker_h, worker, ucp_worker_destroy,
                               ucg_worker_create, ucgh.get(), &params);
    }

    {
        /* UCG Group */
        ucg_group_params_t params;
        VALGRIND_MAKE_MEM_UNDEFINED(&params, sizeof(params));
        params.field_mask = UCG_GROUP_PARAM_FIELD_UCP_WORKER;
        params.worker     = worker;

        UCS_TEST_CREATE_HANDLE(ucg_group_h, group, ucg_group_destroy,
                               ucg_group_create, &params);
    }
}

UCG_INSTANTIATE_TEST_CASE_PLANNERS(test_ucg_context, all)


class test_ucg_version : public test_ucg_context {
};

UCS_TEST_P(test_ucg_version, wrong_api_version) {

    ucs::handle<ucg_config_t*> config;
    UCS_TEST_CREATE_HANDLE(ucg_config_t*, config, ucg_config_release,
                           ucg_config_read, NULL, NULL);

    ucx_params_t params = *get_variant_ctx_params();
    fill_ctx_params(&params);

    ucg_context_h ucgh;
    ucs_status_t status;
    size_t warn_count;
    {
        scoped_log_handler slh(hide_warns_logger);
        warn_count = m_warnings.size();
        status = ucg_init_version(99, 99, &params.ucg, config.get(), &ucgh);
    }
    if (status != UCS_OK) {
        ADD_FAILURE() << "Failed to create UCG with wrong version";
    } else {
        if (m_warnings.size() == warn_count) {
            ADD_FAILURE() << "Missing wrong version warning";
        }
        ucg_cleanup(ucgh);
    }
}

UCS_TEST_P(test_ucg_version, version_string) {

    unsigned major_version, minor_version, release_number;

    ucg_get_version(&major_version, &minor_version, &release_number);

    std::string string_version     = std::to_string(major_version) + '.' +
                                     std::to_string(minor_version) + '.' +
                                     std::to_string(release_number);
    std::string ucg_string_version = ucg_get_version_string();

    EXPECT_EQ(string_version,
              ucg_string_version.substr(0, string_version.length()));
}

UCG_INSTANTIATE_TEST_CASE_PLANNERS(test_ucg_version, all)
