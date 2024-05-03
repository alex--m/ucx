/*
 * See file LICENSE for terms.
 */

#include <ucs/debug/log.h>

/* Note: <ucg/api/...> not used because this header is not installed */
#include "../api/ucg_plan_component.h"

/*
    UCG works with a 8-bit (@ref ucg_step_idx_t) step index. Each step of a
    collective operation has both a TX index (attached to every sent message)
    and an RX index (serves as criteria for matching incoming messages). As the
    collective progresses, step indexes should grow in value.

    To simplify the algorithm, for each distance there's a maximal number of
    peers within that distance - a number listed in the following array. For
    example, there can be up to 1<<8 = 64 cores. This array can be changed as
    hardware advances, but the sum must remain below 128. Otherwise, collectives
    could use index values exceeding the size of ucg_step_idx_t (128 for fan-in
    and 128 for fan-out are 256).
*/
uint8_t ucg_topo_bits_per_level[] = {
    [UCG_GROUP_MEMBER_DISTANCE_NONE]     = UCG_GROUP_FIRST_STEP_IDX,
    [UCG_GROUP_MEMBER_DISTANCE_HWTHREAD] = 8,
    [UCG_GROUP_MEMBER_DISTANCE_CORE]     = 1,
    [UCG_GROUP_MEMBER_DISTANCE_L1CACHE]  = 0,
    [UCG_GROUP_MEMBER_DISTANCE_L2CACHE]  = 6,
    [UCG_GROUP_MEMBER_DISTANCE_L3CACHE]  = 6,
    [UCG_GROUP_MEMBER_DISTANCE_SOCKET]   = 4,
    [UCG_GROUP_MEMBER_DISTANCE_NUMA]     = 3,
    [UCG_GROUP_MEMBER_DISTANCE_BOARD]    = 8,
    [UCG_GROUP_MEMBER_DISTANCE_HOST]     = 8,
    [UCG_GROUP_MEMBER_DISTANCE_CU]       = 0,
    [UCG_GROUP_MEMBER_DISTANCE_CLUSTER]  = 9,
    [UCG_GROUP_MEMBER_DISTANCE_UNKNOWN]  = 0
};

static ucs_status_t ucg_topo_append_step(ucg_topo_desc_t *desc,
                                         ucg_topo_desc_step_t **step_p)
{
    ucg_topo_desc_step_t *step = UCS_ALLOC_CHECK(sizeof(ucg_topo_desc_step_t),
                                                 "ucg_topo_desc_step");

    memset(step, 0, sizeof(*step));
    ucs_int_array_init(&step->level_members,    "ucg_topo_level_members");
    ucs_int_array_init(&step->tx.tx_send_dests, "ucg_topo_send_destinations");

    (void)ucs_ptr_array_insert(&desc->steps, step);

    *step_p = step;
    return UCS_OK;
}

static ucs_status_t
ucg_topo_append_recv_to_last_step(ucg_topo_desc_t *desc, size_t msg_size,
                                  ucg_group_member_index_t index,
                                  ucg_group_member_index_t source,
                                  ucg_step_idx_t step_idx, int no_level_leader,
                                  int is_level_leader,
                                  enum ucg_group_member_distance distance,
                                  ucg_topo_desc_step_t **step_p)
{
    ucs_status_t status;
    ucg_topo_desc_step_t *last_step = *step_p;

    ucs_assert(!(is_level_leader && no_level_leader));

    /* Create a new step when... */
    if ((!last_step) || /* no previous step (this would be the first) */

        /* If the prev. step is expecting messages with a different index */
        ((last_step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) &&
         (last_step->rx.step_idx != step_idx)) ||

        /* Don't mix inter- and intra-host communication in the same step */
        (ucg_group_member_outside_this_host(distance) !=
         ucg_group_member_outside_this_host(last_step->rx.distance)) ||

        /* Can't mix RX after TX in the same step - new step is needed */
        (last_step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID)) {
        /* Create a new step so as not to mix this recv */
        status = ucg_topo_append_step(desc, step_p);
        if (status != UCS_OK) {
            return UCS_OK;
        }

        last_step = *step_p;
    }

    if (!(last_step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID)) {
        last_step->rx.count    = 1;
        last_step->rx.index    = index;
        last_step->rx.msg_size = msg_size;
        last_step->rx.step_idx = step_idx;
        last_step->rx.distance = distance;
        last_step->rx.rx_peer  = source;
        last_step->flags      |= UCG_TOPO_DESC_STEP_FLAG_RX_VALID;
        if (no_level_leader) {
            last_step->flags  |= UCG_TOPO_DESC_STEP_FLAG_RX_NO_LEADERSHIP;
        } else if (is_level_leader) {
            last_step->flags  |= UCG_TOPO_DESC_STEP_FLAG_RX_LEADER;
        }
    }

    last_step->rx.count++;

    return UCS_OK;
}

static ucs_status_t
ucg_topo_append_send_to_last_step(ucg_topo_desc_t *desc, size_t msg_size,
                                  ucg_group_member_index_t index,
                                  ucg_group_member_index_t count,
                                  ucg_group_member_index_t destination,
                                  ucg_step_idx_t step_idx, int no_level_leader,
                                  int is_level_leader,
                                  enum ucg_group_member_distance distance,
                                  ucg_topo_desc_step_t **step_p)
{
    ucs_status_t status;
    ucg_topo_desc_step_t *last_step = *step_p;

    ucs_assert(!(is_level_leader && no_level_leader));

    /* Create a new step when... */
    if ((!last_step) || /* no previous step (this would be the first) */

        /* Don't mix inter- and intra-host communication in the same step */
        (ucg_group_member_outside_this_host(distance) !=
         ucg_group_member_outside_this_host(last_step->tx.distance)) ||

        /* Don't mix inter- and intra-socket transmission in the same step */
        ((last_step->tx.distance != distance) &&
         (last_step->tx.distance != 0) && !is_level_leader &&
         !ucg_group_member_outside_this_host(distance) &&
         !ucg_group_member_outside_this_host(last_step->tx.distance)) ||

        /* Don't mix different step index numbers */
        ((last_step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) &&
         (last_step->tx.step_idx != step_idx))) {
        /* Create a new step so as not to mix this send */
        status = ucg_topo_append_step(desc, step_p);
        if (status != UCS_OK) {
            return UCS_OK;
        }

        last_step = *step_p;
    }

    if (!(last_step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID)) {
        last_step->tx.count    = 1;
        last_step->tx.index    = index;
        last_step->tx.msg_size = msg_size;
        last_step->tx.step_idx = step_idx;
        last_step->tx.distance = distance;
        last_step->flags      |= UCG_TOPO_DESC_STEP_FLAG_TX_VALID;
        if (no_level_leader) {
            last_step->flags  |= UCG_TOPO_DESC_STEP_FLAG_TX_NO_LEADERSHIP;
        } else if (is_level_leader) {
            last_step->flags  |= UCG_TOPO_DESC_STEP_FLAG_TX_LEADER;
        }
    }

    ucs_int_array_insert(&last_step->tx.tx_send_dests, destination);

    last_step->tx.count++;

    return UCS_OK;
}

static ucs_status_t
ucg_topo_set_tree_collective(ucg_topo_desc_step_t *step, int is_send,
                             const struct ucg_topo_params_by_level *level,
                             ucg_group_member_index_t my_abs_index,
                             ucg_group_member_index_t root)
{
    int empty_level;
    int root_on_level;
    unsigned iter_idx;
    ucg_group_member_index_t iter_peer;

    root_on_level = ((((root - level->first) % level->stride) == 0) &&
                     (((root - level->first) / level->stride) < level->count));
    step->root    =  (root_on_level) ? root : level->first;

    if (step->root != my_abs_index) {
        return UCS_OK;
    }

    step->flags |= is_send ? UCG_TOPO_DESC_STEP_FLAG_TX_LEADER :
                             UCG_TOPO_DESC_STEP_FLAG_RX_LEADER;

    /* Record the list of members to send the root's address to */
    empty_level = !ucs_int_array_is_empty(&step->level_members);
    for (iter_idx = 0, iter_peer = level->first;
         iter_idx < level->count;
         iter_idx++, iter_peer += level->stride) {
        if (!empty_level || (iter_peer != my_abs_index)) {
            ucs_int_array_insert_unique(&step->level_members, iter_peer, 0);
        }
    }

    return UCS_OK;
}

static ucs_status_t
ucg_topo_append_tree_multiroot(const struct ucg_topo_params_by_level *level,
                               ucg_group_member_index_t my_abs_idx,
                               ucg_group_member_index_t root,
                               ucg_topo_desc_t *desc,
                               ucg_step_idx_t step_idx,
                               enum ucg_group_member_distance distance,
                               ucg_topo_desc_step_t **last_step)
{
    unsigned index;
    ucs_status_t status;
    ucg_group_member_index_t radix;
    ucg_group_member_index_t first;
    ucg_group_member_index_t stride;
    ucg_group_member_index_t count;
    ucg_group_member_index_t next_parent;
    ucg_group_member_index_t next_child;
    ucg_group_member_index_t actual_parent;
    ucg_group_member_index_t actual_child;
    ucg_topo_desc_step_t *next_step, *prev_step = NULL;

    first  = level->first;
    stride = level->stride;
    count  = level->count;
    radix  = ucs_min(level->tree_radix, count);
    ucs_assert_always(*last_step != NULL);
    ucs_assert_always(radix > 1);

    if (my_abs_idx == root) {
        /* Need to undo the step from waiting for all the other roots */
        ucs_assert((*last_step)->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID);
        ucs_assert((*last_step)->rx.count >= (radix - 1));
        for (next_parent = 1; next_parent < radix; next_parent++) {
            if (--(*last_step)->rx.count == 1) {
                /* Disable RX in this step */
                (*last_step)->flags &= ~UCG_TOPO_DESC_STEP_FLAG_RX_VALID;

                /* Check if TX is used in this step */
                if (!((*last_step)->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID)) {
                    /* Discard this step! (since all it did was this waiting) */
                    ucs_ptr_array_for_each(next_step, index, &desc->steps) {
                        if (*last_step == next_step) {
                            ucs_ptr_array_remove(&desc->steps, index);
                            ucs_free(next_step);
                            next_step = NULL;
                            break;
                        } else {
                            prev_step = next_step;
                        }
                    }

                    assert(next_step == NULL);
                    *last_step = prev_step;
                }
            }
        }
    }

    /* Send to every other root */
    for (next_parent = 0; next_parent < radix; next_parent++) {
        for (next_child = 0; next_child < radix; next_child++) {
            if (next_child == next_parent) continue;
            actual_parent = first + (next_parent * stride);
            actual_child  = first + (next_child  * stride);

            if ((actual_child == my_abs_idx) && (actual_parent != root)) {
                status = ucg_topo_append_send_to_last_step(desc,
                                                           level->tx_msg_size,
                                                           next_child,
                                                           radix, actual_parent,
                                                           step_idx, 0,
                                                           (my_abs_idx == root),
                                                           distance, last_step);
                if (status != UCS_OK) {
                    return status;
                }
            }
        }
    }

    status = ucg_topo_set_tree_collective(*last_step, 1, level,
                                          my_abs_idx, root);
    if (status != UCS_OK) {
        return status;
    }

    /* Expect an incoming message from every other root */
    for (next_parent = 0; next_parent < radix; next_parent++) {
        for (next_child = 0; next_child < radix; next_child++) {
            if (next_child == next_parent) continue;
            actual_parent = first + (next_parent * stride);
            actual_child  = first + (next_child  * stride);

            if (actual_child == my_abs_idx) {
                status = ucg_topo_append_recv_to_last_step(desc,
                                                           level->rx_msg_size,
                                                           next_child,
                                                           actual_parent,
                                                           step_idx, 0,
                                                           (my_abs_idx == root),
                                                           distance, last_step);
                if (status != UCS_OK) {
                    return status;
                }
            }

            (*last_step)->flags |= UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER;
        }
    }

    return UCS_OK;
}

static ucs_status_t
ucg_topo_get_tree_neighbors(const struct ucg_topo_params_by_level *level,
                            ucg_group_member_index_t my_abs_idx,
                            ucg_group_member_index_t root,
                            enum ucg_group_member_distance distance,
                            int is_knomial, int is_multiroot_step,
                            ucs_int_array_t *parents, ucs_int_array_t *children,
                            ucg_group_member_index_t *my_child_idx_p)
{
    unsigned radix;
    int is_root_in_level;
    ucg_group_member_index_t first;
    ucg_group_member_index_t stride;
    ucg_group_member_index_t count;
    ucg_group_member_index_t first_child;
    ucg_group_member_index_t child_count;
    ucg_group_member_index_t next_child;
    ucg_group_member_index_t next_parent;
    ucg_group_member_index_t actual_parent;
    ucg_group_member_index_t actual_child;
    ucg_group_member_index_t first_parent;

    next_parent      = 0;
    first_parent     = 0;
    first            = level->first;
    stride           = level->stride;
    count            = level->count;
    radix            = ucs_min(level->tree_radix, count);
    is_root_in_level = ((root >= first) &&
                        (root < first + (count * stride)) &&
                        ((root - first) % stride == 0));
    first_child      = is_multiroot_step ? radix : 1;
    next_child       = first_child;
    *my_child_idx_p  = (ucg_group_member_index_t)-1; /* must change */

    ucs_assert_always(radix > 1);

    while (next_child < count) {
        for (child_count = 0; child_count < radix - is_knomial; child_count++) {
            for (next_parent = first_parent;
                (next_parent < first_child) && (next_child < count);
                next_parent++, next_child++) {
                actual_parent = first + next_parent * stride;
                actual_child  = first + next_child  * stride;

                if (is_root_in_level && (root != first)) {
                    actual_parent += root - first;
                    if (actual_parent >= first + count * stride) {
                        actual_parent -= stride * count;
                    }

                    actual_child += root - first;
                    if (actual_child >= first + count * stride) {
                        actual_child -= stride * count;
                    }
                }

                if (actual_parent == my_abs_idx) {
                    *my_child_idx_p = 0;
                    (void) ucs_int_array_insert(children, actual_child);
                }

                if (actual_child == my_abs_idx) {
                    *my_child_idx_p = child_count + 1;
                    (void) ucs_int_array_insert(parents, actual_parent);
                }
            }
        }

        first_child += (first_child - first_parent) * (radix - is_knomial);
        if (!is_knomial) {
            first_parent = next_parent;
        }
    }

    return UCS_OK;
}

static ucs_status_t
ucg_topo_append_tree_neighbors(const struct ucg_topo_params_by_level *level,
                               ucg_group_member_index_t my_abs_idx,
                               ucg_group_member_index_t root,
                               ucg_topo_desc_t *desc,
                               ucg_step_idx_t step_idx,
                               int is_send, int is_parents,
                               int is_multiroot_step, int is_knomial,
                               ucs_int_array_t *neighbors,
                               enum ucg_group_member_distance distance,
                               ucg_group_member_index_t my_rel_idx,
                               ucg_topo_desc_step_t **last_step)
{
    ucs_status_t status;
    ucs_int_array_t parent_parents;
    ucs_int_array_t parent_children;
    ucg_group_member_index_t next_idx;
    ucg_group_member_index_t next_array_idx;
    ucg_group_member_index_t team_member_idx = 0;
    ucg_group_member_index_t team_member_cnt = 0;
    ucg_group_member_index_t count = ucs_int_array_get_elem_count(neighbors);

    ucs_int_array_for_each(next_idx, next_array_idx, neighbors) {
        if (is_parents) {
            ucs_int_array_init(&parent_parents,  "ucg_topo_tree_parent_parents");
            ucs_int_array_init(&parent_children, "ucg_topo_tree_parent_children");

            ucs_assert(is_multiroot_step || (count == 1));
            status = ucg_topo_get_tree_neighbors(level, next_idx, root, distance,
                                                 is_knomial, is_multiroot_step,
                                                 &parent_parents,
                                                 &parent_children,
                                                 &team_member_idx);

            team_member_idx = my_rel_idx;
            team_member_cnt = ucs_int_array_get_elem_count(&parent_children) + 1;

            ucs_int_array_cleanup(&parent_children, 0);
            ucs_int_array_cleanup(&parent_parents, 0);

            if (status != UCS_OK) {
                return status;
            }
        } else {
            team_member_idx = 0;
            team_member_cnt = count + 1;
        }

        if (is_send) {
            status = ucg_topo_append_send_to_last_step(desc, level->tx_msg_size,
                                                       team_member_idx,
                                                       team_member_cnt,
                                                       next_idx, step_idx, 0,
                                                       !is_parents, distance,
                                                       last_step);
            if (status != UCS_OK) {
                return status;
            }
        } else {
            status = ucg_topo_append_recv_to_last_step(desc, level->rx_msg_size,
                                                       team_member_idx, next_idx,
                                                       step_idx, 0, !is_parents,
                                                       distance, last_step);
            if (status != UCS_OK) {
                return status;
            }

            if (!is_parents) {
                (*last_step)->flags |= UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER;
            }
        }
    }

    if (!count) {
        return UCS_OK;
    }

    return ucg_topo_set_tree_collective(*last_step, is_send, level, my_abs_idx,
                                        root);
}

#define UCG_TOPO_TREE_LEVEL_FULL ((enum ucg_group_member_distance)-1)

static ucs_status_t
ucg_topo_append_tree(const ucg_topo_params_t *params,
                     enum ucg_group_member_distance distance,
                     ucg_topo_desc_t *desc, ucg_step_idx_t step_idx,
                     int is_knomial, int is_fanout, int is_multiroot_step,
                     ucg_topo_desc_step_t **last_step)
{
    ucs_status_t status;
    ucs_int_array_t parents;
    ucs_int_array_t children;
    ucg_group_member_index_t my_child_idx;
    ucg_group_member_index_t my_abs_idx          = params->me;
    ucg_group_member_index_t root                = params->root;
    struct ucg_topo_params_by_level full_level   = {
        .first      = 0,
        .stride     = 1,
        .count      = params->total,
        .tree_radix = params->total
    };
    const struct ucg_topo_params_by_level *level =
        (distance == UCG_TOPO_TREE_LEVEL_FULL) ?
        &full_level : &params->by_level[distance];

    ucs_assert((level->type > UCG_TOPO_TYPE_KNOMIAL_TREE) /* as a fallback */ ||
               (params->flags & (UCG_TOPO_FLAG_TREE_FANIN |
                                 UCG_TOPO_FLAG_TREE_FANOUT)));

    ucs_debug("tree topology built: my_index=%u step_index=%u distance=%u "
              "is_fanout=%u is_knomial=%u is_multiroot_step=%u", my_abs_idx,
              step_idx, distance, is_fanout, is_knomial, is_multiroot_step);

    /* Set a multi-root step if I was a "step-leader" in the last step */
    if (is_multiroot_step && ((*last_step)->rx.index == 0)) {
        status = ucg_topo_append_tree_multiroot(level, my_abs_idx, root,
                                                desc, step_idx, distance,
                                                last_step);
        if (status != UCS_OK) {
            return status;
        }
    }

    ucs_int_array_init(&parents,  "ucg_topo_tree_parents");
    ucs_int_array_init(&children, "ucg_topo_tree_children");

    status = ucg_topo_get_tree_neighbors(level, my_abs_idx, root, distance,
                                         is_knomial, is_multiroot_step,
                                         &parents, &children, &my_child_idx);
    if (status != UCS_OK) {
        goto append_tree_cleanup;
    }

    if (is_fanout) {
        status = ucg_topo_append_tree_neighbors(level, my_abs_idx, root, desc,
                                                step_idx, 0, 1,
                                                is_multiroot_step, is_knomial,
                                                &parents, my_child_idx,
                                                distance, last_step);
        if (status != UCS_OK) {
            return status;
        }

        status = ucg_topo_append_tree_neighbors(level, my_abs_idx, root, desc,
                                                step_idx, 1, 0,
                                                is_multiroot_step, is_knomial,
                                                &children, 0, distance,
                                                last_step);
        if (status != UCS_OK) {
            return status;
        }
    } else {
        status = ucg_topo_append_tree_neighbors(level, my_abs_idx, root, desc,
                                                step_idx, 0, 0,
                                                is_multiroot_step, is_knomial,
                                                &children, 0, distance,
                                                last_step);
        if (status != UCS_OK) {
            return status;
        }

        status = ucg_topo_append_tree_neighbors(level, my_abs_idx, root, desc,
                                                step_idx, 1, 1,
                                                is_multiroot_step, is_knomial,
                                                &parents, my_child_idx,
                                                distance, last_step);
        if (status != UCS_OK) {
            return status;
        }
    }

append_tree_cleanup:
    ucs_int_array_cleanup(&children, 0);
    ucs_int_array_cleanup(&parents, 0);
    return status;
}

static ucs_status_t
ucg_topo_append_recursive(const ucg_topo_params_t *params,
                          enum ucg_group_member_distance distance,
                          ucg_topo_desc_t *desc, ucg_step_idx_t step_idx,
                          int is_bruck, ucg_topo_desc_step_t **last_step)
{
    ucg_group_member_index_t peer;
    const struct ucg_topo_params_by_level *level = &params->by_level[distance];
    unsigned factor_idx, factor                  = level->recursive_factor;
    ucg_group_member_index_t me                  = params->me;
    ucg_group_member_index_t first               = level->first;
    ucg_group_member_index_t stride              = level->stride;
    ucg_group_member_index_t count               = level->count;
    ucg_group_member_index_t last                = first + (stride * (count - 1));
    ucg_group_member_index_t span                = 1;
    size_t msg_size                              = level->tx_msg_size;

    ucs_debug("recursive topology built: first=%u stride=%u count=%u me=%u "
              "step_idx=%u", first, stride, count, me, step_idx);

    ucs_assert(level->tx_msg_size == level->rx_msg_size);
    ucs_assert_always(factor > 1);
    ucs_assert_always(count > 1);

    if (factor > count) {
        factor = count;
    }

    /* check if total is a power of factor */
    while ((span * factor) <= count) {
        span *= factor;
    }

    if (span < count) {
        if (me >= (first + (span * stride))) {
            /* Need to send to my peer, which is: */
            peer = me % (span * stride);
            ucg_topo_append_send_to_last_step(desc, msg_size, 1, 2, peer,
                                              step_idx, 1, 0, distance,
                                              last_step);
            span  = stride * factor;
            count = stride * count;
            while (span <= count) {
                span *= factor;
                step_idx++;
            }

            ucg_topo_append_recv_to_last_step(desc, msg_size, 1, peer, step_idx,
                                              1, 0, distance, last_step);
            return UCS_OK;
        }

        for (peer = me + (span * stride); peer <= last; peer += span * stride) {
            /* Need to wait for my peer */
            ucg_topo_append_recv_to_last_step(desc, msg_size, 0, peer, step_idx,
                                              1, 0, distance, last_step);
            (*last_step)->flags |= UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER;
        }

        step_idx++;
    }

    /* Recursive K-ing, e.g. doubling when factor is 2 */
    span  = stride * factor;
    count = stride * count;
    while (span <= count) {
        /* Send to every recursive peer */
        for (factor_idx = 1; factor_idx < factor; factor_idx++) {
            peer = params->me + (factor_idx * stride);
            if (((peer - first) / span) > ((params->me - first) / span)) {
                peer -= factor * stride;
            }

            ucg_topo_append_send_to_last_step(desc, msg_size, 0, factor, peer,
                                              step_idx, 1, 0, distance,
                                              last_step);
        }

        /* Recieve from every recursive peer */
        for (factor_idx = 1; factor_idx < factor; factor_idx++) {
            peer = params->me + (factor_idx * stride);
            if (((peer - first) / span) > ((params->me - first) / span)) {
                peer -= factor * stride;
            }

            ucg_topo_append_recv_to_last_step(desc, msg_size, 0, peer, step_idx,
                                              1, 0, distance, last_step);
        }

        (*last_step)->flags |= UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER;
        stride              *= factor;
        span                *= factor;
        step_idx            += 1;
    }

    /* Last step, like the first, may be required for non-power-of-factor */
    peer   = me;
    span  /= factor;
    stride = level->stride;
    if (span < count) {
        peer = me + (span * stride);
        while (peer < count) {
            ucg_topo_append_send_to_last_step(desc, msg_size, 0, 1, peer,
                                              step_idx, 1, 0, distance,
                                              last_step);
            (*last_step)->tx.count++;
            peer += span * stride;
        }
    }

    return UCS_OK;
}

static ucs_status_t
ucg_topo_append_ring(const ucg_topo_params_t *params,
                     enum ucg_group_member_distance distance,
                     ucg_topo_desc_t *desc, ucg_step_idx_t step_idx,
                     ucg_topo_desc_step_t **last_step)
{
    ucs_status_t status;
    ucg_group_member_index_t next, last, prev;
    const struct ucg_topo_params_by_level *level = &params->by_level[distance];
    int is_single = params->flags & UCG_TOPO_FLAG_RING_SINGLE;

    next = params->me + level->stride;
    last = level->first + (level->stride * level->count);
    prev = ((params->me == level->first) ? last : params->me) - level->stride;
    if (next >= last) {
        next -= level->stride * level->count;
    }

    if (params->root != params->me) {
        status = ucg_topo_append_recv_to_last_step(desc, level->rx_msg_size, 0,
                                                   prev, step_idx, 1, 0,
                                                   distance, last_step);
        if (status != UCS_OK) {
            return status;
        }
    }

    status = ucg_topo_append_send_to_last_step(desc, level->tx_msg_size, 1, 2,
                                               next, step_idx, 1, 0, distance,
                                               last_step);
    if (status != UCS_OK) {
        return status;
    }

    if ((params->root == params->me) || !is_single) {
        if (params->root != params->me) {
            ++step_idx;
        }

        status = ucg_topo_append_recv_to_last_step(desc, level->rx_msg_size, 0,
                                                   prev, step_idx, 1, 0,
                                                   distance, last_step);
        if (status != UCS_OK) {
            return status;
        }
    }

    if (is_single) {
        return UCS_OK;
    }

    if (params->root == params->me) {
        ++step_idx;
    }

    status = ucg_topo_append_send_to_last_step(desc, level->tx_msg_size, 1, 2,
                                               next, step_idx, 1, 0, distance,
                                               last_step);
    if (status != UCS_OK) {
        return status;
    }

    if (params->root == params->me) {
        status = ucg_topo_append_recv_to_last_step(desc, level->rx_msg_size, 0,
                                                   prev, step_idx, 1, 0,
                                                   distance, last_step);
        if (status != UCS_OK) {
            return status;
        }
    }

    return UCS_OK;
}

static ucs_status_t
ucg_topo_append_pairwise(const ucg_topo_params_t *params,
                         enum ucg_group_member_distance distance,
                         ucg_topo_desc_t *desc, ucg_step_idx_t step_idx,
                         ucg_topo_desc_step_t **last_step)
{
    unsigned idx;
    ucs_status_t status;
    ucg_group_member_index_t peer;
    const struct ucg_topo_params_by_level *level = &params->by_level[distance];

    for (idx = 0; idx < level->count; idx++) {
        peer = level->first + (idx * level->stride);
        if (peer == params->me) continue;
        status = ucg_topo_append_send_to_last_step(desc, level->tx_msg_size, 1,
                                                   2, peer, step_idx, 1, 0,
                                                   distance, last_step);
        if (status != UCS_OK) {
            return status;
        }
    }

    for (idx = 0; idx < level->count; idx++) {
        peer = level->first + (idx * level->stride);
        if (peer == params->me) continue;
        status = ucg_topo_append_recv_to_last_step(desc, level->rx_msg_size, 0,
                                                   peer, step_idx, 1, 0,
                                                   distance, last_step);
        if (status != UCS_OK) {
            return status;
        }
    }

    (*last_step)->flags |= UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER;

    return UCS_OK;
}

static ucs_status_t
ucg_topo_append_neighbors(const ucg_topo_params_t *params,
                          enum ucg_group_member_distance distance,
                          ucg_topo_desc_t *desc, ucg_step_idx_t step_idx,
                          ucg_topo_desc_step_t **last_step)
{
    int ret;
    ucs_status_t status;
    ucg_group_member_index_t *peers;
    unsigned idx, send_cnt, recv_cnt;
    const struct ucg_topo_params_by_level *level = &params->by_level[distance];

    /* ask how many sends and recieves are expected */
    ret = ucg_global_params.neighbors.vertex_count_f(params->cb_ctx, &send_cnt,
                                                     &recv_cnt);
    if (ret) {
        ucs_error("Error from the vertex counter callback");
        return UCS_ERR_INVALID_PARAM;
    }

    /* list the send destinations */
    peers = alloca((send_cnt + recv_cnt) * sizeof(ucg_group_member_index_t));
    ret   = ucg_global_params.neighbors.vertex_query_f(params->cb_ctx, peers,
                                                       peers + send_cnt);
    if (ret) {
        ucs_error("Error from the vertex query callback");
        return UCS_ERR_INVALID_PARAM;
    }

    /* fill in this step's peers */
    for (idx = 0; idx < send_cnt; idx++) {
        status = ucg_topo_append_send_to_last_step(desc, level->tx_msg_size, 0,
                                                   0, peers[idx], step_idx, 1,
                                                   0, distance, last_step);
        if (status != UCS_OK) {
            return status;
        }
    }

    for (idx = 0; idx < recv_cnt; idx++) {
        status = ucg_topo_append_recv_to_last_step(desc, level->rx_msg_size, 0,
                                                   peers[send_cnt], step_idx, 1,
                                                   0, distance, last_step);
        if (status != UCS_OK) {
            return status;
        }

        (*last_step)->flags |= UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER;
    }

    return UCS_OK;
}

static inline enum ucg_group_member_distance
ucg_topo_use_distance(int is_fanout, enum ucg_group_member_distance distance_idx)
{
    return is_fanout ? UCG_GROUP_MEMBER_DISTANCE_UNKNOWN - distance_idx - 1 :
                       distance_idx;
}

ucs_status_t ucg_topo_create(const ucg_topo_params_t *params,
                             ucg_topo_desc_t **desc_p)
{
    int is_bruck;
    int is_fanout;
    int is_knomial;
    int is_tree_last;
    ucs_status_t status;
    int can_be_multiroot;
    int is_interhost_last;
    int is_recursive_last;
    int is_multiroot_step;
    ucg_step_idx_t step_idx;
    UCS_V_UNUSED unsigned count;
    ucg_topo_desc_step_t *last_step;
    enum ucg_group_member_distance distance_idx;
    const struct ucg_topo_params_by_level *level;
    enum ucg_group_member_distance distance_used;

    ucg_topo_desc_t *ret = UCS_ALLOC_CHECK(sizeof(ucg_topo_desc_t),
                                           "ucg_topo_desc");
    ucs_ptr_array_init(&ret->steps, "ucg_topo_desc");

    is_multiroot_step = 0;
    is_interhost_last = 0;
    is_recursive_last = 0;
    can_be_multiroot  = 0;
    is_tree_last      = 0;
    last_step         = NULL;
    is_fanout         = !(params->flags & UCG_TOPO_FLAG_TREE_FANIN);
    step_idx          = 0;

do_fanout:
    for (distance_idx = 0;
         distance_idx < UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
         distance_idx++) {
        distance_used = ucg_topo_use_distance(is_fanout, distance_idx);
        level         = &params->by_level[distance_used];
        is_knomial    = 1;
        is_bruck      = 1;

        /* A bunch of sanity checks */
        if (level->type != UCG_TOPO_TYPE_NONE) {
            ucs_debug("Topology at distance %u from process #%u: type=%u "
                      "first=%u stride=%u count=%u fanout=%u multiroot_step=%u",
                      distance_used, params->me, level->type, level->first,
                      level->stride, level->count, is_fanout, is_multiroot_step);
            ucs_assert(level->count  >= 1);
            ucs_assert(level->stride >= 1);
            ucs_assert((params->me >= level->first));
            ucs_assert(((params->me - level->first) % level->stride) == 0);
            ucs_assert(((params->me - level->first) / level->stride) < level->count);
        } else {
            ucs_debug("Topology at distance %u from process #%u: NONE",
                      distance_used, params->me);
        }

        if ((level->type != UCG_TOPO_TYPE_NONE) &&
            (level->count > ((unsigned)1 <<
                             ucg_topo_bits_per_level[distance_used]))) {
            count = level->count;
            count = ucs_roundup_pow2(count);
            ucs_error("1<<ucg_topo_bits_per_level[%u] should be at least %u",
                      distance_used, ucs_count_trailing_zero_bits(count));
            return UCS_ERR_UNSUPPORTED;
        }

        /* Determine the starting step index for this step (both RX and TX) */
        ucs_assert((unsigned)step_idx + ucg_topo_bits_per_level[distance_used] <
                   (unsigned)((ucg_step_idx_t)-1));
        step_idx += ucg_topo_bits_per_level[distance_used];
        ucs_assert(distance_idx || is_fanout || step_idx == UCG_GROUP_FIRST_STEP_IDX);

        switch (level->type) {
        case UCG_TOPO_TYPE_NONE:
            status = UCS_OK;
            break;

        case UCG_TOPO_TYPE_RECURSIVE:
            is_bruck = 0;
            /* no break */
        case UCG_TOPO_TYPE_BRUCK:
            if (ucs_test_all_flags(params->flags, UCG_TOPO_FLAG_FULL_EXCHANGE)) {
                if (is_multiroot_step) {
                    is_multiroot_step = 0;
                    continue;
                }

                status = ucg_topo_append_recursive(params, distance_used, ret,
                                                   step_idx, is_bruck,
                                                   &last_step);
                break;
            }
            /* no break */
        case UCG_TOPO_TYPE_KARY_TREE:
            is_knomial = 0;
            /* no break */
        case UCG_TOPO_TYPE_KNOMIAL_TREE:
            if (is_fanout && (params->total <= level->tree_radix)) {
                distance_used = UCG_TOPO_TREE_LEVEL_FULL;
            } else if (is_multiroot_step) {
                last_step->tx.step_idx = step_idx;
            }
            status = ucg_topo_append_tree(params, distance_used, ret, step_idx,
                                          is_knomial, is_fanout, is_multiroot_step,
                                          &last_step);

            if (distance_used == UCG_TOPO_TREE_LEVEL_FULL) {
                if (params->me == params->root) {
                    last_step->tx.index    = params->me;
                    last_step->tx.count    = params->total;
                    last_step->tx.step_idx = (ucg_step_idx_t)-1;
                    last_step->tx.msg_size = level->tx_msg_size;
                } else {
                    last_step->rx.index    = params->me;
                    last_step->rx.count    = params->total;
                    last_step->rx.step_idx = (ucg_step_idx_t)-1;
                    last_step->rx.rx_peer  = params->root;
                    last_step->rx.msg_size = level->rx_msg_size;
                }
                goto out;
            }
            break;

        case UCG_TOPO_TYPE_RING:
            status = ucg_topo_append_ring(params, distance_used, ret, step_idx,
                                          &last_step);
            break;

        case UCG_TOPO_TYPE_PAIRWISE:
            status = ucg_topo_append_pairwise(params, distance_used, ret,
                                              step_idx, &last_step);
            break;

        case UCG_TOPO_TYPE_NEIGHBORS:
            status = ucg_topo_append_neighbors(params, distance_used, ret,
                                               step_idx, &last_step);
            if (status == UCS_OK) {
                goto out;
            }
            break;

        default:
            ucs_error("Invalid topology type requested");
            status = UCS_ERR_INVALID_PARAM;
            break;
        }

        if ((last_step != NULL) &&
            (last_step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID)) {
            ucs_assert(last_step->rx.rx_peer != params->me);
        }

        if (status != UCS_OK) {
            goto create_failed;
        }

        if (level->type != UCG_TOPO_TYPE_NONE) {
            is_multiroot_step = 0; /* Any type except tree/none invalidates */
            can_be_multiroot  = ((params->root == params->me) ||
                                 ((params->root == level->first) &&
                                  ((level->stride * level->count) ==
                                   params->total)));
            is_interhost_last = (distance_idx > UCG_GROUP_MEMBER_DISTANCE_HOST);
            is_recursive_last = ((level->type == UCG_TOPO_TYPE_RECURSIVE) ||
                                 (level->type == UCG_TOPO_TYPE_BRUCK));
            is_tree_last      = ((level->type == UCG_TOPO_TYPE_KARY_TREE) ||
                                 (level->type == UCG_TOPO_TYPE_KNOMIAL_TREE)) &&
                                (level->count <= params->multiroot_thresh);
        }
    }

    /* Only in case of Fanin+Fanout we need to do the way back too */
    ucs_assert(UCG_TOPO_FLAG_FULL_EXCHANGE & UCG_TOPO_FLAG_TREE_FANOUT);
    if ((params->flags & UCG_TOPO_FLAG_TREE_FANOUT) && !is_fanout) {
        is_fanout         = 1;
        is_multiroot_step = can_be_multiroot && is_interhost_last &&
                            (is_tree_last || is_recursive_last);
        goto do_fanout;
    }

out:
    ucs_assert(!ucs_ptr_array_is_empty(&ret->steps));
    *desc_p = ret;
    return UCS_OK;

create_failed:
    ucg_topo_destroy(ret);
    return status;
}

void ucg_topo_destroy(ucg_topo_desc_t *desc)
{
    unsigned index;
    ucg_topo_desc_step_t *step;

    ucs_assert(!ucs_ptr_array_is_empty(&desc->steps));
    ucs_ptr_array_for_each(step, index, &desc->steps) {
        if (step->flags & (UCG_TOPO_DESC_STEP_FLAG_RX_LEADER |
                           UCG_TOPO_DESC_STEP_FLAG_TX_LEADER)) {
            ucs_int_array_cleanup(&step->level_members, 0);
        } else {
            ucs_assert(ucs_int_array_is_empty(&step->level_members));
        }

        if (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) {
            ucs_int_array_cleanup(&step->tx.tx_send_dests, 0);
        } else {
            ucs_assert(ucs_int_array_is_empty(&step->tx.tx_send_dests));
        }

        ucs_free(step);
    }

    ucs_ptr_array_cleanup(&desc->steps, 0);
    ucs_free(desc);
}

void ucg_topo_print(const ucg_topo_desc_t *desc)
{
    ucg_topo_desc_step_t *step;
    unsigned step_index, peer_index;
    ucg_group_member_index_t peer;

    printf("Planner name:   UCG topology\n");
    printf("Steps:          %i\n",  ucs_ptr_array_get_elem_count(&desc->steps));

    ucs_ptr_array_for_each(step, step_index, &desc->steps) {
        printf("Step #%i (Flags: ", step_index);

        if (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) {
            if (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_LEADER) {
                printf("rx (leader)");
            } else {
                printf("rx (led by %u)", step->root);
            }
            if(step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_FROM_EVERY_PEER) {
                printf(" from every peer");
            }
        }

        if (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) {
            if (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) {
                printf(", ");
            }
            if (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_LEADER) {
                printf("tx (leader)");
            } else {
                printf("tx (led to %u)", step->root);
            }
        }

        printf("):\n");

        if (step->flags & UCG_TOPO_DESC_STEP_FLAG_RX_VALID) {
            printf("\t(rx)   step index:   %u\n", step->rx.step_idx);
            printf("\t(rx)   my index:     %u\n", step->rx.index);
            printf("\t(rx)   step peers:   %u\n", step->rx.count);
            printf("\t(rx)   message size: %lu\n", step->rx.msg_size);
            printf("\t(rx)   peer index:   %u\n\n", step->rx.rx_peer);
        }

        if (step->flags & UCG_TOPO_DESC_STEP_FLAG_TX_VALID) {
            printf("\t(tx)   step index:   %u\n", step->tx.step_idx);
            printf("\t(tx)   my index:     %u\n", step->tx.index);
            printf("\t(tx)   step peers:   %u\n", step->tx.count);
            printf("\t(tx)   message size: %lu\n", step->tx.msg_size);
            printf("\t(tx)   peer indexes: ");
            ucs_int_array_for_each(peer, peer_index, &step->tx.tx_send_dests) {
                printf("%u, ", peer);
            }
            printf("\n\n");
        }

        if (step->flags & (UCG_TOPO_DESC_STEP_FLAG_RX_LEADER |
                           UCG_TOPO_DESC_STEP_FLAG_TX_LEADER)) {
            printf("\t(coll) master over: ");
            ucs_int_array_for_each(peer, peer_index, &step->level_members) {
                printf("%u, ", peer);
            }
            printf("\n\n");
        }
    }
}
