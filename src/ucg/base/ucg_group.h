/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_GROUP_H_
#define UCG_GROUP_H_

#include <ucs/pmodule/target.h>
#include <ucs/datastruct/khash.h>
#include <ucs/datastruct/ptr_array.h>
#include <ucp/core/ucp_types.h>

/* Note: <ucg/api/...> not used because this header is not installed */
#include "../api/ucg_plan_component.h"

#define UCG_GROUP_CACHE_MODIFIER_MASK UCS_MASK(8)

KHASH_MAP_INIT_INT(ucg_group_ep, ucp_ep_h);

typedef struct ucg_group {
    ucs_pmodule_target_t     super;
    ucg_coll_id_t            next_id;   /**< ID for the upcoming operation */
    ucp_worker_h             worker;    /**< for conn. est. & progress calls */
    const ucg_group_params_t params;    /**< parameters, for future connections */
    khash_t(ucg_group_ep)    p2p_eps;   /**< P2P endpoints by member index */

    struct {
        ucs_ptr_array_locked_t  matched;   /**< wireup addresses matching this group */
        ucs_ptr_array_locked_t *unmatched; /**< unmatched Wireup addresses */
    } addresses;

    /* The size of the distance array stored here depends on the parameters */
    ucg_group_member_index_t distances[0];

    /* Below this point - the private per-planner data is allocated/stored */
} ucg_group_t;

const ucg_group_params_t* ucg_group_get_params(ucg_group_h group); /* for tests */

ucs_status_t ucg_group_wireup_coll_ifaces(ucg_group_h group, uint64_t wireup_uid,
                                          uint8_t am_id, int is_leader,
                                          int is_incast,
                                          const ucs_int_array_t *level_members,
                                          ucg_group_member_index_t coll_threshold,
                                          uct_incast_operator_t operator_,
                                          uct_incast_operand_t operand,
                                          unsigned operand_count,
                                          int is_operand_cache_aligned,
                                          size_t msg_size,
                                          uct_reduction_external_cb_t ext_aux_cb,
                                          void *ext_operator, void *ext_datatype,
                                          unsigned *iface_id_base_p,
                                          ucp_tl_bitmap_t *coll_tl_bitmap_p,
                                          ucg_group_member_index_t *coll_index,
                                          ucg_group_member_index_t *coll_count,
                                          ucp_ep_h *ep_p);

void ucg_group_cleanup_coll_ifaces(ucg_group_h group, unsigned iface_id_base,
                                   ucp_tl_bitmap_t coll_tl_bitmap);

void ucg_group_store_ep(khash_t(ucg_group_ep) *khash,
                        ucg_group_member_index_t index,
                        ucp_ep_h ep);

#endif /* UCG_GROUP_H_ */
