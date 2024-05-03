/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_test.h"

#define TEST_MAX_GROUPS (1000)

class ucg_group_test : public test_ucg_group_base {
public:
    /* Test variants */
    enum {
        UCP_WORKER = 0,
        UCB_PIPES
    };

    virtual ucg_group_params_t get_group_params(ucg_group_member_index_t index)
    {
        ucg_group_params_t group_params = ucg_test::get_group_params(index);

        if (get_variant_value(0) == UCB_PIPES) {
            group_params.field_mask |= UCG_GROUP_PARAM_FIELD_UCB_PIPES;
            group_params.pipes       = get_rank(index).pipes();
        } else {
            group_params.field_mask |= UCG_GROUP_PARAM_FIELD_UCP_WORKER;
            group_params.worker      = get_rank(index).worker();
        }

        return group_params;
    }

    static void get_test_variants(std::vector<ucp_test_variant>& variants)
    {
        add_variant_with_value(variants, UCP_FEATURE_GROUPS, UCP_WORKER, "worker");
        add_variant_with_value(variants, UCP_FEATURE_GROUPS, UCB_PIPES,  "pipes");
    }
};

UCS_TEST_P(ucg_group_test, test_group_sanity) {
    ucg_group_h group;
    ucg_group_attr_t attr;
    ucg_group_params_t params = get_group_params(0);
    params.field_mask |= UCG_GROUP_PARAM_FIELD_NAME         |
                         UCG_GROUP_PARAM_FIELD_ID           |
                         UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |
                         UCG_GROUP_PARAM_FIELD_MEMBER_INDEX;
    params.name = "test_name";
    params.id = 1234;
    params.member_count = 123;
    params.member_index = 4;

    ASSERT_EQ(UCS_OK, ucg_group_create(&params, &group));

    attr.field_mask = UCG_GROUP_ATTR_FIELD_NAME         |
                      UCG_GROUP_ATTR_FIELD_ID           |
                      UCG_GROUP_ATTR_FIELD_MEMBER_COUNT |
                      UCG_GROUP_ATTR_FIELD_MEMBER_INDEX;
    ASSERT_EQ(UCS_OK, ucg_group_query(group, &attr));

    ASSERT_EQ(0, strcmp(attr.name, params.name));
    ASSERT_EQ(attr.id, params.id);
    ASSERT_EQ(attr.member_count, params.member_count);
    ASSERT_EQ(attr.member_index, params.member_index);

    ucg_group_destroy(group);
}

UCS_TEST_P(ucg_group_test, test_many_groups) {
    unsigned i;
    ucg_group_h groups[TEST_MAX_GROUPS];
    ucg_group_params_t params = get_group_params(0);

    for (i = 0; i < TEST_MAX_GROUPS; i++) {
        ASSERT_EQ(UCS_OK, ucg_group_create(&params, &groups[i]));
    }

    for (i = 0; i < TEST_MAX_GROUPS; i++) {
        ucg_group_destroy(groups[i]);
    }
}

UCG_INSTANTIATE_TEST_CASE(ucg_group_test)
