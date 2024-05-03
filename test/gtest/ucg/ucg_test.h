/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2014.  ALL RIGHTS RESERVED.
 * Copyright (C) Huawei Technologies Co., Ltd. 2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_TEST_H_
#define UCG_TEST_H_

#include <ucp/ucp_test.h>

/* UCG version compile time test */
#if (UCG_API_VERSION != UCG_VERSION(UCG_API_MAJOR,UCG_API_MINOR))
#error possible bug in UCG version
#endif

typedef struct ucg_request {
    uintptr_t    is_complete;
    ucs_status_t status;
} ucg_request_t;

template <typename T>
class commmunicator_storage : public ucs::entities_storage<T> {
public:
    const ucs::ptr_vector<T>& comm() const {
        return m_comm;
    }

    T& get_rank(size_t idx) {
        return m_comm.at(idx);
    }

    ucs::ptr_vector<T> m_comm;
};

class rank {
    typedef std::vector<ucs::handle<ucg_group_h, rank*>> group_vec_t;

public:
    rank(const ucp_test_param& test_param, ucg_config_t* ucg_config,
         const ucp_worker_params_t& worker_params,
         const ucp_test_base* test_owner);

    ~rank();

    void groupify(const ucs::ptr_vector<rank>& ranks,
                  const ucg_group_params_t& group_params,
                  int group_idx = 0, int do_set_group = 1);

    ucg_group_h group(int group_index = 0) const;

    ucp_worker_h worker() const;

    ucb_pipes_h pipes() const;

    ucg_context_h ucgh() const;

    int get_num_groups() const;

    unsigned worker_progress();

    void warn_existing_groups() const;

    void cleanup();

protected:
    ucs::handle<ucg_context_h>      m_ucgh;
    ucs::handle<ucp_worker_h>       m_worker;
    ucs::handle<ucb_pipes_h>        m_pipes;
    group_vec_t                     m_groups;
    const ucp_test_base             *m_test;

private:
    void set_group(ucg_group_h group, int group_index);
};

/**
 * UCG test
 */
class ucg_test : public ucp_test,
                 public ::commmunicator_storage<rank> {

    friend class rank;

public:
    ucg_test();
    virtual ~ucg_test();

    ucg_config_t* m_ucg_config;

    static std::vector<ucp_test_param>
    enum_test_params(const std::vector<ucp_test_variant>& variants,
                     const std::string& tls, const std::string& planners)
    {
        std::vector<ucp_test_param> result;

        if (!check_planners(planners)) {
            return result;
        }

        return ucp_test::enum_test_params(variants, tls, planners);
    }

    virtual ucg_group_params_t get_group_params(ucg_group_member_index_t index);

private:
    static void set_ucg_config(ucg_config_t *config, const std::string& planners);
    static bool check_planners(const std::string& planners);

protected:
    typedef void (*get_variants_func_t)(std::vector<ucp_test_variant>&);

    virtual void init();
    virtual void cleanup();
    virtual bool has_planner(const std::string& planner_name) const;
    bool has_any_planner(const std::vector<std::string>& planner_names) const;
    bool has_any_planner(const std::string *planners, size_t planner_size) const;
    rank* create_comm(bool add_in_front = false);
    rank* create_comm(bool add_in_front, const ucp_test_param& test_param);
    void set_ucg_config(ucg_config_t *config);
    static void fill_ctx_params(ucx_params_t *params);
};

class test_ucg_group_base : public ucg_test {
public:
    virtual void create_comms(ucg_group_member_index_t group_size);
};

class test_ucg_coll_base : public test_ucg_group_base {
public:
    test_ucg_coll_base(unsigned nodes, unsigned ppn,
                       ucg_group_member_index_t my_rank,
                       const ucg_collective_params_t *coll_params);
    virtual ~test_ucg_coll_base();

    virtual ucs_status_t create();
    virtual ucs_status_t start(ucg_request_t *req);
    virtual ucg_collective_progress_t get_progress();
    virtual ucs_status_t cancel();
    virtual void destroy();

    virtual void once() {
        ucg_request_t req;

        ASSERT_UCS_OK(create());
        ASSERT_UCS_OK_OR_INPROGRESS(start(&req));
        ASSERT_UCS_OK(request_wait(&req));

        destroy();
    }

protected:
    ucg_coll_h        *m_op;
    ucg_group_params_t m_coll_params;
};


static inline ucs_status_t ucg_worker_create(ucg_context_h context,
                                             const ucp_worker_params_t *params,
                                             ucp_worker_h *worker_p)
{
    return ucp_worker_create(ucb_context_get_ucp(ucg_context_get_ucb(context)),
                             params, worker_p);
}


class test_ucg_context : public ucg_test {
public:
    static void get_test_variants(std::vector<ucp_test_variant> &variants)
    {
        add_variant(variants, UCP_FEATURE_GROUPS);
    }
};


/**
 * Instantiate the parameterized test case a combination of transports.
 *
 * @param _test_case   Test case class, derived from ucg_test.
 * @param _planners    Instantiation name and also the name of the planner.
 */
#define UCG_INSTANTIATE_TEST_CASE_PLANNERS(_test_case, _planners) \
    INSTANTIATE_TEST_SUITE_P(_planners, _test_case, \
                             testing::ValuesIn(enum_test_params<_test_case>("all", #_planners)));


/**
 * Instantiate the parameterized test case for all transport combinations.
 *
 * @param _test_case  Test case class, derived from ucg_test.
 */
#define UCG_INSTANTIATE_TEST_CASE(_test_case) \
    UCG_INSTANTIATE_TEST_CASE_PLANNERS(_test_case, over_ucp) \
    UCG_INSTANTIATE_TEST_CASE_PLANNERS(_test_case, over_uct) \
    UCG_INSTANTIATE_TEST_CASE_PLANNERS(_test_case, over_ucb) \
    UCG_INSTANTIATE_TEST_CASE_PLANNERS(_test_case, all)

#endif
