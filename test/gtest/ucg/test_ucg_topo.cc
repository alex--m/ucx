/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_test.h"

extern "C" {
#include <ucg/api/ucg_plan_component.h>
#include <ucg/base/ucg_group.h>
extern const char *ucg_topo_type_names[];
}

class ucg_topo_test : public ucg_test {
public:
    static void get_test_variants(std::vector<ucp_test_variant>& variants)
    {
        add_variant(variants, UCP_FEATURE_GROUPS, SINGLE_THREAD);
    }

    void test_by_params(const ucg_topo_params_t *params);
    void test_recursive(ucg_topo_params_t *params,
                        const ucg_topo_type_t *topo_types,
                        ucg_group_member_index_t stride,
                        enum ucg_group_member_distance dist);

private:
    void calc_group_size(const ucg_topo_params_t *params,
                         ucg_group_member_index_t *size_p);
    void align_to_rank(const ucg_topo_params_t *source_params,
                       ucg_topo_params_t *aligned_params,
                       ucg_group_member_index_t me);
};

class Dataflow {
public:
    Dataflow(ucg_group_member_index_t group_size, ucg_topo_desc_t** descs)
    {
        m_descs = descs;
        m_count = group_size;
        m_flow  = new ucg_step_idx_t [group_size * group_size];
        m_step = new ucg_step_idx_t [group_size];
    }

    ~Dataflow()
    {
        delete m_flow;
    }

    void transfer(ucg_group_member_index_t source,
                  ucg_group_member_index_t destination)
    {
        ucg_group_member_index_t i;

        for (i = 0; i < m_count; i++) {
            set(destination, i, ucs_max(get(source, i), get(destination, i)));
        }
    }

    void start(ucg_group_member_index_t member)
    {
        uint32_t index;
        ucg_topo_desc_step_t *step, *dest;
        ucg_group_member_index_t send_destination;

        while ((step = get_step(member)) != NULL) {
            //printf("=== Member #%u is at step %u ===\n", member, m_step[member]);
            if ((step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) &&
                (step->rx.count > 0)) {
                //printf("RET step->rx.count=%u step->rx.step_idx=%u\n", step->rx.count, step->rx.step_idx);
                return;
            }

            m_step[member]++;

            ucs_int_array_for_each(send_destination, index, &step->tx.tx_send_dests) {
                //printf("\tMember #%u sends to %u (idx=%u)\n", member, send_destination, step->tx.step_idx);

                transfer(member, send_destination);

                dest = get_recv_step(send_destination, step->tx.step_idx);
                ASSERT_NE((ucg_topo_desc_step_t*)NULL, dest);
                ASSERT_NE(0, dest->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID);
                ASSERT_LT(0, dest->rx.count);
                if (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER) {
                    dest->rx.count--;
                } else {
                    dest->rx.count = 1;
                }

                if (dest == get_step(send_destination)) {
                    start(send_destination);
                }
            }
        }
    }

    void test_fanin(ucg_group_member_index_t root)
    {

        //printf("\n\ntest_fanin(root=%u):\n", root);
        int i;

        clear();

        for (i = 0; i < m_count; i++) {
            set(i, i, 1);
        }

        for (i = 0; i < m_count; i++) {
            start(i);
        }

        for (i = 0; i < m_count; i++) {
            //printf("testing is_set(%u, %u)\n", root, i);
            ASSERT_EQ(1, is_set(root, i));
        }
    }

    void test_fanout(ucg_group_member_index_t root)
    {
        //printf("\n\ntest_fanout(root=%u):\n", root);
        int i;

        clear();

        set(root, root, 1);

        start(root);

        for (i = 0; i < m_count; i++) {
            //printf("testing is_set(%u, %u)\n", i, root);
            ASSERT_EQ(1, is_set(i, root));
        }
    }

    void test_alltoall()
    {
        //printf("\n\ntest_alltoall(size=%u):\n", m_count);
        int i, j;

        clear();

        for (i = 0; i < m_count; i++) {
            set(i, i, 1);
        }

        for (i = 0; i < m_count; i++) {
            start(i);
        }

        for (i = 0; i < m_count; i++) {
            for (j = 0; i < m_count; i++) {
                //printf("testing is_set(%u, %u)\n", i, j);
                ASSERT_EQ(1, is_set(i, j));
            }
        }
    }

protected:
    void clear()
    {
        memset(m_flow, 0, sizeof(m_count * m_count * sizeof(*m_flow )));
        memset(m_step, 0, sizeof(m_count *           sizeof(*m_step)));
    }

    inline ucg_step_idx_t get(ucg_group_member_index_t member,
                              ucg_group_member_index_t source)
    {
        return m_flow[(member * m_count) + source];
    }

    inline void set(ucg_group_member_index_t member,
                    ucg_group_member_index_t source,
                    ucg_step_idx_t when)
    {
        m_flow[(member * m_count) + source] = when;
    }

    inline int is_set(ucg_group_member_index_t member,
                      ucg_group_member_index_t source)
    {
        return m_flow[(member * m_count) + source] != 0;
    }

    inline ucg_topo_desc_step_t* get_step(ucg_group_member_index_t member)
    {
        void *step;

        if (ucs_ptr_array_lookup(&m_descs[member]->steps, m_step[member], step)) {
            return (ucg_topo_desc_step_t*)step;
        }

        return NULL;
    }

    inline ucg_topo_desc_step_t* get_recv_step(ucg_group_member_index_t member,
                                               ucg_step_idx_t dest_step_idx)
    {
        void *s;
        ucg_topo_desc_step_t *step;

        unsigned step_idx = m_step[member];

        while ((ucs_ptr_array_lookup(&m_descs[member]->steps, step_idx, s)) &&
               ((step = (ucg_topo_desc_step_t*)s) != NULL)) {

            //printf("get_recv_step[%u:%u] step=%p step->rx.count=%u step->rx.step_idx=%u step->tx.step_idx=%u step->rx_from_all=%u\n",
            //member, step_idx, step, step->rx.count, step->rx.step_idx, step->tx.step_idx, step->rx_from_every_peer);

            step_idx++;

            if (!(step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) ||
                 (step->rx.count < 2)) {
                continue;
            }

            if (step->rx.step_idx == dest_step_idx) {
                return step;
            }
        }

        return NULL;
    }

    ucg_group_member_index_t m_count;
    ucg_step_idx_t          *m_flow;
    ucg_step_idx_t          *m_step;
    ucg_topo_desc_t        **m_descs;
};

inline enum ucg_group_member_distance operator++(enum ucg_group_member_distance &d)
{
    d = static_cast<enum ucg_group_member_distance>(d + 1);
    return d;
}

inline ucg_topo_type_t operator--(ucg_topo_type_t &t)
{
   t = static_cast<ucg_topo_type_t>(t - 1);
   return t;
}

void ucg_topo_test::calc_group_size(const ucg_topo_params_t *params,
                                    ucg_group_member_index_t *size_p)
{
    enum ucg_group_member_distance level;
    ucg_group_member_index_t res = 1;

    char arr[UCG_GROUP_MEMBER_DISTANCE_UNKNOWN] = {0};

    for (level = UCG_GROUP_MEMBER_DISTANCE_CORE;
         level < UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
         ++level) {
        if (params->by_level[level].type != UCG_TOPO_TYPE_NONE) {
            res *= params->by_level[level].count;
            arr[level] = 'a' + (1 + params->by_level[level].count % 25);
        } else {
            arr[level] = '0';
        }
    }


    UCS_TEST_MESSAGE << "Distance array code is " << arr;

    ASSERT_LT(1, res);
    *size_p = res;
}

void ucg_topo_test::align_to_rank(const ucg_topo_params_t *source_params,
                                  ucg_topo_params_t *aligned_params,
                                  ucg_group_member_index_t me)
{
    enum ucg_group_member_distance level;

    memcpy(aligned_params, source_params, sizeof(*aligned_params));
    aligned_params->me = me;

    for (level = UCG_GROUP_MEMBER_DISTANCE_NONE;
         level < UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
         ++level) {
        if (aligned_params->by_level[level].type != UCG_TOPO_TYPE_NONE) {
            ASSERT_LT(0, aligned_params->by_level[level].stride);
            aligned_params->by_level[level].first = me -
                (me % aligned_params->by_level[level].stride);
        }
    }
}

void ucg_topo_test::test_by_params(const ucg_topo_params_t *params)
{
    ucg_topo_desc_t **desc;
    ucg_topo_params_t my_params;
    ucg_group_member_index_t rank;
    ucg_group_member_index_t group_size = 0;

    /* Allocate the data-flow state */
    calc_group_size(params, &group_size);
    desc = new ucg_topo_desc_t* [group_size];
    Dataflow flow(group_size, desc);

    /* Initialize the topology to test on */
    for (rank = 0; rank < group_size; rank++) {
        align_to_rank(params, &my_params, rank);
        ASSERT_EQ(UCS_OK, ucg_topo_create(params, &desc[rank]));
        ASSERT_LT(0, ucs_ptr_array_get_elem_count(&desc[rank]->steps));
    }

    /* Start the test itself */
    if (params->flags == UCG_TOPO_FLAG_TREE_FANIN) {
        flow.test_fanin(params->root);
    } else if ((params->flags == UCG_TOPO_FLAG_TREE_FANOUT) ||
               (params->flags == UCG_TOPO_FLAG_RING_SINGLE)) {
        flow.test_fanout(params->root);
    } else {
        flow.test_alltoall();
    }

    /* De-allocation */
    for (rank = 0; rank < group_size; rank++) {
        ucg_topo_destroy(desc[rank]);
    }

    delete[] desc;
}

UCS_TEST_P(ucg_topo_test, test_flat_topo) {
    enum ucg_group_member_distance distance;
    ucg_topo_params_t params = {0};
    unsigned size, extra = 0;
    ucg_group_t dummy = {0};
    ucg_topo_type_t type;

    params.cb_ctx = &dummy;
    for (distance = UCG_GROUP_MEMBER_DISTANCE_NONE;
         distance < UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
         ++distance) {
        params.by_level[distance].type   = UCG_TOPO_TYPE_NONE;
        params.by_level[distance].stride = 1;
    }
    params.multiroot_thresh = 4;

root_redo:
    for (size = 2; size < 5; size++) {
        for (distance = UCG_GROUP_MEMBER_DISTANCE_HWTHREAD;
             distance < UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
             ++distance) {
            params.by_level[distance].count = size;
        }

        UCS_TEST_MESSAGE << "Testing for a group of size " << size;

        for (type = UCG_TOPO_TYPE_NEIGHBORS;
             type > UCG_TOPO_TYPE_NONE;
             --type) {
            params.by_level[1].type = type;

            UCS_TEST_MESSAGE << "\ton a " << ucg_topo_type_names[type] << " topology";

            if (type == UCG_TOPO_TYPE_NEIGHBORS) {
                for (extra = 2; extra * extra <= size; extra++);
                if (extra * extra != size) {
                    continue;
                }

                ((ucg_group_params_t*)&dummy.params)->member_count = size;
            }

            for (extra = 2; extra < 10; extra++) {
                if ((type == UCG_TOPO_TYPE_KARY_TREE) ||
                    (type == UCG_TOPO_TYPE_KNOMIAL_TREE)) {
                    UCS_TEST_MESSAGE << "\t\twith a tree radix of " << extra;
                    UCS_TEST_MESSAGE << "\t\tand " << params.root << " as root";
                    params.by_level[1].tree_radix = extra;
                    params.flags = UCG_TOPO_FLAG_TREE_FANIN;
                } else if ((type == UCG_TOPO_TYPE_RECURSIVE) ||
                           (type == UCG_TOPO_TYPE_BRUCK)) {
                    UCS_TEST_MESSAGE << "\t\twith a recursive factor of " << extra;
                    params.by_level[1].recursive_factor = extra;
                    params.flags = UCG_TOPO_FLAG_FULL_EXCHANGE;
                } else if (extra != 2) {
                    break; /* don't iterate over extra */
                }

flags_redo:
                test_by_params(&params);

                if (type == UCG_TOPO_TYPE_RING) {
                     if (params.flags == 0) {
                         params.flags = UCG_TOPO_FLAG_RING_SINGLE;
                        UCS_TEST_MESSAGE << "\t\tand single-ring (one round)";
                        goto flags_redo;
                     } else {
                         params.flags = 0;
                     }
                }

                if ((type == UCG_TOPO_TYPE_KARY_TREE) ||
                    (type == UCG_TOPO_TYPE_KNOMIAL_TREE)) {
                    if (params.flags < (UCG_TOPO_FLAG_TREE_FANIN |
                                        UCG_TOPO_FLAG_TREE_FANOUT)) {
                        params.flags++;
                        goto flags_redo;
                    } else {
                        params.flags = 0;
                    }
                }
            }
        }
    }

    if (params.root == 0) {
        params.root = 1;
        goto root_redo;
    }
}

extern uint8_t ucg_topo_bits_per_level[];

static unsigned int ucg_topo_test_sizes[] = {0,2,3,4,5,7,8,9,17,63,64,256,257};

void ucg_topo_test::test_recursive(ucg_topo_params_t *params,
                                   const ucg_topo_type_t *topo_types,
                                   ucg_group_member_index_t stride,
                                   enum ucg_group_member_distance distance)
{
    unsigned *size_iter, size_max;
    const ucg_topo_type_t *type_iter;

    if (distance == UCG_GROUP_MEMBER_DISTANCE_CLUSTER) {
        if (stride > 1) {
            /* Run the test if there are processes on any level/distance */
            test_by_params(params);
        }
        return;
    }

    size_max = 1 << ucg_topo_bits_per_level[++distance];
    params->by_level[distance].stride = stride;

    for (size_iter = ucg_topo_test_sizes; *size_iter <= size_max; size_iter++) {
        if (*size_iter == 0) {
            test_recursive(params, topo_types, stride, distance);
            continue;
        }

        params->by_level[distance].count      = *size_iter;
        params->by_level[distance].tree_radix = 2; /* acts also as recursive factor */

        for (type_iter = topo_types; *type_iter != UCG_TOPO_TYPE_NONE; type_iter++) {
            params->by_level[distance].type = *type_iter;
            test_recursive(params, topo_types, *size_iter * stride, distance);
        }

        // if (distance == UCG_GROUP_MEMBER_DISTANCE_CORE) {
        //     UCS_TEST_MESSAGE << "Completed 50% of the topology " << ucg_topo_type_names[*type_iter];
        // }
    }

    params->by_level[distance].type = UCG_TOPO_TYPE_NONE;
}

UCS_TEST_P(ucg_topo_test, test_multilevel_allreduce) {
    ucg_topo_params_t params = {
        .multiroot_thresh = 4,
        .flags = UCG_TOPO_FLAG_FULL_EXCHANGE
    };
    ucg_topo_type_t topo_types[] = {
        UCG_TOPO_TYPE_KARY_TREE,
        UCG_TOPO_TYPE_KNOMIAL_TREE,
        UCG_TOPO_TYPE_RECURSIVE,
        UCG_TOPO_TYPE_NONE
    };

    test_recursive(&params, topo_types, 1, UCG_GROUP_MEMBER_DISTANCE_NONE);
}

UCS_TEST_P(ucg_topo_test, test_multilevel_bcast) {
    ucg_topo_params_t params;
    ucg_topo_type_t topo_types[] = {
        UCG_TOPO_TYPE_KARY_TREE,
        UCG_TOPO_TYPE_KNOMIAL_TREE,
        UCG_TOPO_TYPE_NONE
    };

    memset(&params, 0, sizeof(params));
    params.flags            = UCG_TOPO_FLAG_TREE_FANOUT;
    params.multiroot_thresh = 4;
    test_recursive(&params, topo_types, 1, UCG_GROUP_MEMBER_DISTANCE_NONE);

    memset(&params, 0, sizeof(params));
    params.root             = 1;
    params.flags            = UCG_TOPO_FLAG_TREE_FANOUT;
    params.multiroot_thresh = 4;
    test_recursive(&params, topo_types, 1, UCG_GROUP_MEMBER_DISTANCE_NONE);
}

UCG_INSTANTIATE_TEST_CASE(ucg_topo_test)
