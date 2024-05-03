/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
 * Copyright (c) UT-Battelle, LLC. 2015. ALL RIGHTS RESERVED.
 * Copyright (C) Los Alamos National Security, LLC. 2018. ALL RIGHTS RESERVED.
 * Copyright (C) Huawei Technologies Co., Ltd. 2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_test.h"

#define UNCHANGED ((int)-1)

class test_ucg_coll : public test_ucg_group_base {
public:
    /* Test variants */
    enum {
        SHORT,
        BCOPY,
        ZCOPY
    };

    static void get_test_variants(std::vector<ucp_test_variant>& variants,
                                  int variant, const std::string& name)
    {
        add_variant_with_value(variants, UCP_FEATURE_GROUPS, variant, name);
    }

    static void get_test_variants(std::vector<ucp_test_variant>& variants)
    {
        get_test_variants(variants, SHORT, "short");
        get_test_variants(variants, BCOPY, "bcopy");
        get_test_variants(variants, ZCOPY, "zcopy");
    }

    int *m_send_buffer;
    int *m_recv_buffer;

    test_ucg_coll() : test_ucg_group_base () {} //(10, 1, 0) {}

    virtual void create_comms(ucg_group_member_index_t group_size) {
        m_send_buffer = new int[group_size];
        m_recv_buffer = new int[group_size];

        ucg_group_member_index_t idx;
        for (idx = 0; idx < group_size; idx++) {
            m_send_buffer[idx] = idx + 1;
            m_recv_buffer[idx] = UNCHANGED;
        }
    }

    virtual int is_request_completed(ucg_request_t *req) {
        return UCS_INPROGRESS != ucg_collective_check_status(req);
    }

protected:
    ucs_status_t do_blocking_collective(ucg_collective_type_t *type);
    bool is_done(std::vector<ucg_request_t> reqs,
                 std::vector<ucs_status_ptr_t> ops);
    void wait(std::vector<ucg_request_t> reqs,
              std::vector<ucs_status_ptr_t> ops);

};

ucs_status_t test_ucg_coll::do_blocking_collective(ucg_collective_type_t *type)
{
    ucg_collective_progress_t progress_f;
    ucg_group_member_index_t group_size = ::commmunicator_storage<rank>::m_entities.size();
    std::vector<ucg_coll_h> colls(group_size);
    std::vector<ucg_request_t> reqs(group_size);
    std::vector<ucs_status_ptr_t> ops(group_size);

    ucg_collective_params_t params = {0};
    params.send.type.modifiers     = type->modifiers;
    params.send.type.root          = type->root;
    params.send.buffer             = m_send_buffer;
    params.send.count              = 1;
    params.send.dtype              = (void*)ucp_dt_make_contig(sizeof(int));
    params.recv.buffer             = m_recv_buffer;
    params.recv.count              = 1;
    params.recv.dtype              = (void*)ucp_dt_make_contig(sizeof(int));

    ucs_status_t status;
    ucs_status_ptr_t status_ptr;
    ucg_group_member_index_t idx;
    for (idx = 0; idx < group_size; idx++) {
        status = ucg_collective_create(get_rank(idx).group(0), &params, &colls[idx]);
        if (UCS_STATUS_IS_ERR(status)) {
            return status;
        }
    }

    for (idx = 0; idx < group_size; idx++) {
        status_ptr = ucg_collective_start(colls[idx], &reqs[idx], &progress_f);
        if (UCS_PTR_IS_ERR(status_ptr)) {
            return UCS_PTR_RAW_STATUS(status_ptr);
        }

        ops[idx] = status_ptr;
    }

    wait(reqs, ops);

    for (idx = 0; idx < group_size; idx++) {
        ucg_collective_destroy(colls[idx]);
    }

    return UCS_OK;
}

bool test_ucg_coll::is_done(std::vector<ucg_request_t> reqs,
                            std::vector<ucs_status_ptr_t> ops) {
    ucg_group_member_index_t idx;
    for (idx = 0; idx < reqs.size(); idx++) {
        if (is_request_completed(&reqs[idx])) {
            return false;
        }
    }

    return true;
}

void test_ucg_coll::wait(std::vector<ucg_request_t> reqs,
                         std::vector<ucs_status_ptr_t> ops) {
    while (!is_done(reqs, ops)) {
        progress();
    }
}

UCS_TEST_P(test_ucg_coll, reduce)
{
    ucg_collective_type_t type = {0};
    type.modifiers             = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                 UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION;

    do_blocking_collective(&type);
}

UCS_TEST_P(test_ucg_coll, reduce_nonzero_root)
{
    ucg_collective_type_t type;

    type.modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                     UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION;
    type.root      = 1;

    do_blocking_collective(&type);
}

UCG_INSTANTIATE_TEST_CASE(test_ucg_coll)
