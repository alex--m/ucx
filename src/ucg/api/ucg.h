/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_H_
#define UCG_H_

#include <ucg/api/ucg_def.h>
#include <ucg/api/ucg_version.h>
#include <ucb/api/ucb.h>

BEGIN_C_DECLS

/**
 * @defgroup UCG_API Unified Communication Protocol (UCG) API
 * @{
 * This section describes UCG API.
 * @}
 */


/**
 * @defgroup UCG_CONTEXT UCG Application Context
 * @ingroup UCG_API
 * @{
 * Application context is a primary concept of UCG design which
 * provides an isolation mechanism, allowing resources associated
 * with the context to separate or share network communication context
 * across multiple instances of applications.
 *
 * This section provides a detailed description of this concept and
 * routines associated with it.
 *
 * @}
 */


 /**
 * @defgroup UCG_GROUP UCG Group
 * @ingroup UCG_API
 * @{
 * UCG Group routines
 * @}
 */


/**
* @defgroup UCG_COLLECTIVE UCG Collective operation
* @ingroup UCG_API
* @{
* UCG Collective operations
* @}
*/


/**
 * @ingroup UCG_CONTEXT
 * @brief UCG group parameters field mask.
 *
 * The enumeration allows specifying which fields in @ref ucg_group_params_t are
 * present. It is used to enable backward compatibility support.
 */
enum ucg_params_field {
    UCG_PARAM_FIELD_NAME             = UCS_BIT(0), /**< Name for this context */
    UCG_PARAM_FIELD_CONTEXT_HEADROOM = UCS_BIT(1), /**< Extra space in context */
    UCG_PARAM_FIELD_ADDRESS_CB       = UCS_BIT(2), /**< Peer address lookup */
    UCG_PARAM_FIELD_NEIGHBORS_CB     = UCS_BIT(3), /**< Neighborhood info */
    UCG_PARAM_FIELD_DATATYPE_CB      = UCS_BIT(4), /**< Callback for datatypes */
    UCG_PARAM_FIELD_REDUCE_OP_CB     = UCS_BIT(5), /**< Callback for reduce ops */
    UCG_PARAM_FIELD_COMPLETION_CB    = UCS_BIT(6), /**< Actions upon completion */
    UCG_PARAM_FIELD_SET_IMBALANCE_CB = UCS_BIT(7), /**< Imbalance notification */
    UCG_PARAM_FIELD_MPI_IN_PLACE     = UCS_BIT(8), /**< MPI_IN_PLACE value */
    UCG_PARAM_FIELD_GLOBAL_INDEX     = UCS_BIT(9), /**< Callback for global index */
    UCG_PARAM_FIELD_HANDLE_FAULT     = UCS_BIT(10),/**< Fault-tolerance support */
    UCG_PARAM_FIELD_JOB_INFO         = UCS_BIT(11) /**< Info about the MPI job */
};

enum ucg_fault_tolerance_mode {
    UCG_FAULT_IS_FATAL = 0,      /**< Fault will cause the run to terminate */
    UCG_FAULT_IS_RETURNED,       /**< Fault will be returned to the user */
    UCG_FAULT_IS_TRANSPARENT,    /**< Fault will be circumvented by UCG */
    UCG_FAULT_IS_HANDLED_BY_USER /**< Fault will be handled by the user - the
                                      return value of the handler will indicate
                                      success or failure to handle the fault.*/
};

enum ucg_operator {
    UCG_OPERATOR_MAX,
    UCG_OPERATOR_MIN,
    UCG_OPERATOR_SUM,
    UCG_OPERATOR_PROD,
    UCG_OPERATOR_LAND,
    UCG_OPERATOR_BAND,
    UCG_OPERATOR_LOR,
    UCG_OPERATOR_BOR,
    UCG_OPERATOR_LXOR,
    UCG_OPERATOR_BXOR,
    UCG_OPERATOR_NOP
};

enum ucg_group_distance_type {
    UCG_GROUP_DISTANCE_TYPE_FIXED,    /**< Info form is a constant value */
    UCG_GROUP_DISTANCE_TYPE_ARRAY,    /**< Info form is a 1-D distance array */
    UCG_GROUP_DISTANCE_TYPE_TABLE,    /**< Info form is a 2-D distance table */
    UCG_GROUP_DISTANCE_TYPE_PLACEMENT /**< Info form is placement per level */
};

/**
 * @ingroup UCG_GROUP
 * @brief UCG group member distance.
 *
 * During group creation, the caller can pass information about the distance of
 * each other member of the group. This information may be used to select the
 * best logical topology for collective operations inside UCG.
 *
 * Note: this list of options matches that of hwloc.
 */
enum ucg_group_member_distance {
    UCG_GROUP_MEMBER_DISTANCE_NONE = 0,

    /* Within the same host */
    UCG_GROUP_MEMBER_DISTANCE_HWTHREAD,
    UCG_GROUP_MEMBER_DISTANCE_CORE,
    UCG_GROUP_MEMBER_DISTANCE_L1CACHE,
    UCG_GROUP_MEMBER_DISTANCE_L2CACHE,
    UCG_GROUP_MEMBER_DISTANCE_L3CACHE,
    UCG_GROUP_MEMBER_DISTANCE_SOCKET,
    UCG_GROUP_MEMBER_DISTANCE_NUMA,
    UCG_GROUP_MEMBER_DISTANCE_BOARD,
    UCG_GROUP_MEMBER_DISTANCE_HOST,

    /* Over the network */
    UCG_GROUP_MEMBER_DISTANCE_CU,
    UCG_GROUP_MEMBER_DISTANCE_CLUSTER,

    UCG_GROUP_MEMBER_DISTANCE_UNKNOWN
} UCS_S_PACKED;

static inline int
ucg_group_member_outside_this_host(enum ucg_group_member_distance distance)
{
    return distance > UCG_GROUP_MEMBER_DISTANCE_HOST;
}

/**
 * @ingroup UCG_CONTEXT
 * @brief Creation parameters for the UCG context.
 *
 * The structure defines the parameters that are used during the UCG
 * initialization by @ref ucg_init .
 */
typedef struct ucg_params {
    const ucb_params_t *super;

    /**
     * Mask of valid fields in this structure, using bits from
     * @ref ucg_params_field. Fields not specified in this mask will be ignored.
     * Provides ABI compatibility with respect to adding new fields.
     */
    uint64_t field_mask;

    /**
     * Tracing and analysis tools can identify the context using this name.
     * To retrieve the context's name, use @ref ucg_context_query, as the name
     * you supply may be changed by UCX under some circumstances, e.g. a name
     * conflict. This field is only assigned if you set
     * @ref UCG_PARAM_FIELD_NAME in the field mask. If not, then a default
     * unique name will be created for you.
     */
    const char *name;

    /** Head-room before the allocated context pointer, for extensions */
    size_t context_headroom;

    /* Callback functions for address lookup, used at connection establishment */
    struct {
        int (*lookup_f)(void *cb_group_context,
                        ucg_group_member_index_t index,
                        ucp_address_t **addr,
                        size_t *addr_len);
        void (*release_f)(ucp_address_t *addr);
    } address;

    /*
     * The two callback functions below are used for Neighborhood collectives:
     * the first checks the local in-degree and out-degree of the communicator
     * graph, and the second fills in the indexes of these peers (assuming the
     * array is large enough to store all these indexes).
     */
    struct {
        int (*vertex_count_f)(void *cb_group_context,
                              unsigned *in_degree,
                              unsigned *out_degree);
        int (*vertex_query_f)(void *cb_group_context,
                              ucg_group_member_index_t *in,
                              ucg_group_member_index_t *out);
    } neighbors;

    /* Information about datatypes */
    struct {
        /* Convert the opaque data-type into UCX structure (should return 0) */
        int (*convert_f)(void *datatype, ucp_datatype_t *ucp_datatype);

        /* Get the buffer size needed to unpack a (non-contiguous) data-type */
        int (*get_span_f)(void *datatype, int count, ptrdiff_t *span,
                          ptrdiff_t *gap);

        /* Check if the data-type is an integer (of any length) */
        int (*is_integer_f)(void *datatype, int *is_signed);

        /* Check if the data-type is a floating-point (of any length) */
        int (*is_floating_point_f)(void *datatype);
    } datatype;

    /* Information about reduction operations */
    struct {
        /**
         * To support any type of reduction for an MPI implementation, this
         * callback function can be called (when a new message arrives) to
         * reduce the data into a buffer (which already contains a partial
         * result). Below are some additional functions to detect the type of
         * reduction, so that simple reductions (e.g. sum on integers) doesn't
         * require using this callback.
         */
        uct_reduction_external_cb_t reduce_cb_f;

        /* Check the reduction operator type (MPI_SUM -> UCG_OPERATOR_SUM) */
        int (*get_operator_f)(void *reduce_op, enum ucg_operator *operator,
                              int *want_location, int *is_commutative);
    } reduce_op;

    /* Requested action upon completion (for non-blocking calls) */
    struct {
        /**
         * Callback function to call upon completion. The three dimentions here
         * represent three choices when calling the callback, listed below.
         *
         * A. Synchronous vs. asynchronous completion:
         * [0] - Immediate completion (within a ucg_collective_start() call)
         * [1] - Asynchronous completion, during a progress function call
         *
         * B. Success vs. failure
         * [0] - Collective operation was successful (status is UCS_OK).
         * [1] - Collective operation has failed.
         *
         * C. Persistent vs. non-persistent request
         * [0] - Non-persistent request
         * [1] - Persistent request
         */
        ucg_collective_comp_cb_t comp_cb_f[2][2][2];
    } completion;

    /* Callback function to notify of measured imbalance during collectives */
    ucg_collective_imbalance_set_cb_t set_imbalance_cb_f;

    /* The value of MPI_IN_PLACE, which can replace send or receive buffers */
    void* mpi_in_place;

    /*
     * Callback function to convert from group-specific index to the global
     * index, for example the one used by the same process in MPI_COMM_WORLD.
     *
     * Note: if this function is passed as a parameter, it is assumed that the
     *       the first group to be created is the "global" group, and so the
     *       context of that group would be used for address resolution (see
     *       lookup_f above, which also accepts cb_group_context).
     */
    int (*get_global_index_f)(void *cb_group_context,
                              ucg_group_member_index_t group_index,
                              ucg_group_member_index_t *global_index_p);

    /* Fault-tolerance can be enabled by passing */
    struct {
        enum ucg_fault_tolerance_mode mode;
        void *context;
        int (*handler_f)(void *context, int *error_code, ...);
        void (*err_str_f)(int error_code, char **error_description);
    } fault;

    struct {
        uint32_t job_uid;  /** Unique ID of the job which this process is a member of */
        uint32_t step_idx; /** Step index within this job (e.g. in SLURM) */

        ucg_group_member_index_t job_size;      /* Number of members (processes) */
        enum ucg_group_member_distance map_by;  /* mpirun --map-by=?  */
        enum ucg_group_member_distance rank_by; /* mpirun --rank-by=? */
        enum ucg_group_member_distance bind_to; /* mpirun --bind-to=? */
        unsigned procs_per_host;                /* PPN */
    } job_info;
} ucg_params_t;

/**
 * @ingroup UCG_GROUP
 * @brief UCG group collective operation characteristics.
 *
 * The enumeration allows specifying modifiers to describe the requested
 * collective operation, as part of @ref ucg_collective_params_t
 * passed to @ref ucg_collective_start . For example, for MPI_Reduce:
 *
 * modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
 *             UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION;
 *
 * The premise is that (a) any collective type can be described as a combination
 * of the flags below, and (b) the implementation can benefit from applying
 * logic based on these flags. For example, we can check if a collective has
 * a single rank as the source, which will be true for both MPI_Bcast and
 * MPI_Scatterv today, and potentially other types in the future.
 */
enum ucg_collective_modifiers {
    UCG_GROUP_COLLECTIVE_MODIFIER_32_BIT_INTEGERS    = UCS_BIT( 0), /* 32-bit counts/displs */
    UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC           = UCS_BIT( 1), /* MPI_*v */
    UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR           = UCS_BIT( 2), /* Neighbor collectives */
    /* Special validity indicator which means the rest of the modifiers can also be used */
    UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID         = UCS_BIT( 3),

    /* Network Pattern Considerations */
    UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE      = UCS_BIT( 4), /* otherwise from all */
    UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION = UCS_BIT( 5), /* otherwise to all */
    UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE          = UCS_BIT( 6), /* buffer reduction */
    UCG_GROUP_COLLECTIVE_MODIFIER_CONCATENATE        = UCS_BIT( 7), /* buffer concatenation */
    UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST          = UCS_BIT( 8), /* otherwise scatter */
    UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_PARTIAL  = UCS_BIT( 9), /* MPI_Scan (+VAR -> Exscan) */

    /* Buffer/Data Management Considerations */
    UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE   = UCS_BIT(10), /* stable reduction */
    UCG_GROUP_COLLECTIVE_MODIFIER_NONCONTIG_DATATYPE = UCS_BIT(11), /* some may be non-contiguous */
    UCG_GROUP_COLLECTIVE_MODIFIER_PERSISTENT         = UCS_BIT(12), /* otherwise destroy coll_h */
    UCG_GROUP_COLLECTIVE_MODIFIER_SYMMETRIC          = UCS_BIT(13), /* persistent on all ranks */
    UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER            = UCS_BIT(14), /* prevent others from starting */
    UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS           = UCS_BIT(15), /* information gathering only */

    UCG_GROUP_COLLECTIVE_MODIFIER_MASK               = UCS_MASK(16)
};

/**
 * @ingroup UCG_GROUP
 * @brief UCG group collective operation description.
 *
 * Some collective operations have one special rank. For example MPI_Bcast has
 * the root of the broadcast, and MPI_Reduce has the root where the final result
 * must be written. The "root" field is used in cases where "modifiers" includes:
 *   (a) UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE
 *   (b) UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION
 * In other cases, the "root" field is ignored.
 */
typedef struct ucg_collective_type {
    uint16_t                 modifiers; /* Collective description, using
                                           @ref ucg_collective_modifiers */
    uint16_t                 reserved;
    ucg_group_member_index_t root;      /* Root rank, if applicable */
} UCS_S_PACKED ucg_collective_type_t;

/**
 * @ingroup UCG_GROUP
 * @brief UCG group parameters field mask.
 *
 * The enumeration allows specifying which fields in @ref ucg_group_params_t are
 * present. It is used to enable backward compatibility support.
 */
enum ucg_group_params_field {
    UCG_GROUP_PARAM_FIELD_NAME         = UCS_BIT(0), /**< Group "nickname" */
    UCG_GROUP_PARAM_FIELD_UCP_WORKER   = UCS_BIT(1), /**< UCP Worker object */
    UCG_GROUP_PARAM_FIELD_UCB_PIPES    = UCS_BIT(2), /**< UCB Pipes (worker) */
    UCG_GROUP_PARAM_FIELD_ID           = UCS_BIT(3), /**< Unique identifier */
    UCG_GROUP_PARAM_FIELD_MEMBER_COUNT = UCS_BIT(4), /**< Number of members */
    UCG_GROUP_PARAM_FIELD_MEMBER_INDEX = UCS_BIT(5), /**< My member index */
    UCG_GROUP_PARAM_FIELD_CB_CONTEXT   = UCS_BIT(6), /**< Context for callbacks */
    UCG_GROUP_PARAM_FIELD_DISTANCES    = UCS_BIT(7), /**< Rank distance info */
    UCG_GROUP_PARAM_FIELD_FLAGS        = UCS_BIT(8), /**< Various modifiers */
    UCG_GROUP_PARAM_FIELD_WIREUP_POOL  = UCS_BIT(9), /**< Wire-up messages */
    UCG_GROUP_PARAM_FIELD_CACHE_SIZE   = UCS_BIT(10) /**< Op. cache size */
};

/**
 * @ingroup UCG_GROUP
 * @brief UCG group creation flags.
 */
enum ucg_group_create_flags {
    UCG_GROUP_CREATE_FLAG_NEIGHBORHOOD = UCS_BIT(0), /**< custom network graph */
    UCG_GROUP_CREATE_FLAG_TX_TIMESTAMP = UCS_BIT(1)  /**< transfer timestamps */
};

/**
 * @ingroup UCG_GROUP
 * @brief Creation parameters for the UCG group.
 *
 * The structure defines the parameters that are used during the UCG group
 * @ref ucg_group_create "creation".
 */
typedef struct ucg_group_params {
    /**
     * Mask of valid fields in this structure, using bits from
     * @ref ucg_group_params_field. Fields not specified in this mask will be
     * ignored. Provides ABI compatibility with respect to adding new fields.
     */
    uint64_t field_mask;

    /**
     * Tracing and analysis tools can identify the group using this name. To
     * retrieve the worker's name, use @ref ucg_group_query, as the name you
     * supply may be changed by UCX under some circumstances, e.g. a name
     * conflict. This field is only assigned if you set
     * @ref UCG_GROUP_PARAM_FIELD_NAME in the field mask. If not, then a
     * default unique name will be created for you.
     */
    const char *name;

    ucp_worker_h worker;

    ucb_pipes_h pipes;

    ucg_group_id_t id; /* Unique group identifier */

    ucg_group_member_index_t member_count; /* Number of group members */
    ucg_group_member_index_t member_index; /* My member index within the group */

    void *cb_context; /* Opaque context object for address/neighbor callbacks.
                         In MPI implementations this would likely be MPI_Comm */

    enum ucg_group_distance_type distance_type; /* indicates the contents of
                                                   the union below */
    union {
        /* Used in case the distance is fixed among all nodes */
        enum ucg_group_member_distance distance_value;

        /*
         * This array contains information about the process placement of different
         * group members, which is used to select the best topology for collectives.
         *
         * For example, for 2 nodes, 3 sockets each, 4 cores per socket, each member
         * should be passed the distance array contents as follows:
         *   1st group member distance array:  0111222222223333333333333333
         *   2nd group member distance array:  1011222222223333333333333333
         *   3rd group member distance array:  1101222222223333333333333333
         *   4th group member distance array:  1110222222223333333333333333
         *   5th group member distance array:  2222011122223333333333333333
         *   6th group member distance array:  2222101122223333333333333333
         *   7th group member distance array:  2222110122223333333333333333
         *   8th group member distance array:  2222111022223333333333333333
         *    ...
         *   12th group member distance array: 3333333333333333011122222222
         *   13th group member distance array: 3333333333333333101122222222
         *    ...
         */
        enum ucg_group_member_distance *distance_array;

        /* A 2-D matrix where [i][j] is the distance between i and j */
        /* Used if info_type is set to @ref UCG_TOPO_INFO_DISTANCE_TABLE */
        enum ucg_group_member_distance **distance_table;

        /* Placement table, so [1][2]=4 means the rank 1 is running on core 4 */
        /* Used if info_type is set to @ref UCG_TOPO_INFO_PLACEMENT_TABLE */
        uint16_t *placement[UCG_GROUP_MEMBER_DISTANCE_UNKNOWN];
    };

    uint64_t flags; /* Various flags from @ref ucg_group_create_flags */

    /*
     * Messages used for collective transport wireup (containing addresses) may
     * arrive before a single group has been created. Such early messages would
     * be stored in a global array, and each group could search it if expects
     * this kind of messages to arrive and it hasn't.
     */
    ucg_message_pool_h wireup_pool;

    uint32_t cache_size; /* Sets the number of cached collective operations */
} ucg_group_params_t;


/**
 * @ingroup UCG_GROUP
 * @brief UCG group attributes field mask.
 *
 * The enumeration allows specifying which fields in @ref ucg_group_attr_t are
 * present. It is used to enable backward compatibility support.
 */
enum ucg_group_attr_field {
    UCG_GROUP_ATTR_FIELD_NAME         = UCS_BIT(0), /**< UCG group name */
    UCG_GROUP_ATTR_FIELD_ID           = UCS_BIT(1), /**< UCG group ID */
    UCG_GROUP_ATTR_FIELD_MEMBER_COUNT = UCS_BIT(2), /**< Group members total */
    UCG_GROUP_ATTR_FIELD_MEMBER_INDEX = UCS_BIT(3)  /**< Index within the group */
};


/**
 * @ingroup UCG_GROUP
 * @brief UCG group attributes.
 *
 * The structure defines the attributes which characterize
 * the particular group.
 */
typedef struct ucg_group_attr {
    /**
     * Mask of valid fields in this structure, using bits from
     * @ref ucg_group_attr_field.
     * Fields not specified in this mask will be ignored.
     * Provides ABI compatibility with respect to adding new fields.
     */
    uint64_t              field_mask;

    /**
     * Tracing and analysis tools can identify the worker using this name.
     */
    char                  name[UCG_GROUP_NAME_MAX];

    /*
     * Unique group identifier - can either be specified or auto-generated.
     */
    ucg_group_id_t        id;

    /*
     * Number of group members
     */
    ucg_group_member_index_t member_count;

    /*
     * My member index within the group
     */
    ucg_group_member_index_t member_index;
} ucg_group_attr_t;


/**
 * @ingroup UCG_GROUP
 * @brief Parameters for a UCG group listener object.
 *
 * This structure defines parameters for @ref ucg_group_listener_create,
 * which is used to listen for incoming client/server connections on a group.
 */
typedef struct ucg_listener_params {
    /**
     * Mask of valid fields in this structure, using bits from
     * @ref ucg_listener_params_field.
     * Fields not specified in this mask will be ignored.
     * Provides ABI compatibility with respect to adding new fields.
     */
    uint64_t                            field_mask;

    /**
     * An address in the form of a sockaddr.
     * This field is mandatory for filling (along with its corresponding bit
     * in the field_mask - @ref UCP_LISTENER_PARAM_FIELD_SOCK_ADDR).
     * The @ref ucp_listener_create routine will return with an error if sockaddr
     * is not specified.
     */
    ucs_sock_addr_t                     sockaddr;

    /**
     * Handler to endpoint creation in a client-server connection flow.
     * In order for the callback inside this handler to be invoked, the
     * UCP_LISTENER_PARAM_FIELD_ACCEPT_HANDLER needs to be set in the
     * field_mask.
     */
    ucp_listener_accept_handler_t       accept_handler;

    /**
     * Handler of an incoming connection request in a client-server connection
     * flow. In order for the callback inside this handler to be invoked, the
     * @ref UCP_LISTENER_PARAM_FIELD_CONN_HANDLER needs to be set in the
     * field_mask.
     * @note User is expected to call ucp_ep_create with set
     *       @ref UCP_EP_PARAM_FIELD_CONN_REQUEST flag to
     *       @ref ucp_ep_params_t::field_mask and
     *       @ref ucp_ep_params_t::conn_request in order to be able to receive
     *       communications.
     */
    ucp_listener_conn_handler_t         conn_handler;
} ucg_listener_params_t;


/**
 * @ingroup UCG_GROUP
 * @brief UCG group listener attributes.
 *
 * The structure defines the attributes which characterize
 * the particular listener for a group.
 */
typedef struct ucg_listener_attr {
    /**
     * Mask of valid fields in this structure, using bits from
     * @ref ucp_listener_attr_field.
     * Fields not specified in this mask will be ignored.
     * Provides ABI compatibility with respect to adding new fields.
     */
    uint64_t                field_mask;

    /**
     * Sockaddr on which this listener is listening for incoming connection
     * requests.
     */
    struct sockaddr_storage sockaddr;
} ucg_listener_attr_t;


/**
 * @ingroup UCG_GROUP
 * @brief Creation parameters for the UCG collective operation.
 *
 * The structure defines the parameters that are used during the UCG collective
 * @ref ucg_collective_create "creation". The size of this structure is critical
 * to performance, as well as it being contiguous, because its entire contents
 * are accessed during run-time.
 *
 * This structure aims to minimize the footprint of a wide range of collective
 * call types. Specifically, MPI has collectives with up to 8 pointers (excl.
 * communicator and request) passed as parameters, but only a handful of types
 * require this: (i)alltoall[vw](_init)
 */
typedef struct ucg_collective_params {
    struct {
        union {
            /**
             * If the least-significant bit is 0 - it's displacements for both
             * the send and receive portions of the collective parameters, which
             * also indicates the collective is MPI's Alltoallv/w with variable
             * data sizes. Otherwise, the type field indicates which collectives
             * are in Use. See @ref UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID .
             */
            const int            *displs; /**< item displacement array */
            const void          **wdispls; /**< wide item displacement array */

            /* only in "send" (see @ref UCG_PARAM_MODIFIERS ) */
            ucg_collective_type_t type;   /**< type and root of the collective */

            /* only in "recv" (see @ref UCG_PARAM_OP ) */
            void                 *op;     /**< external reduce operation handle */

        };
        union {
            void                 *buffer;  /**< buffer location to use */
            const void           *cbuffer; /**< same, minus compiler warnings */
        };
        union {
            size_t                count;  /**< item count */
            const void           *counts; /**< item count array */
        };
        union {
            void                 *dtype;  /**< external data-type context */
            void                **dtypes; /**< external data-type context array */
            /*
             * Note: if UCG_PARAM_FIELD_DATATYPE_CB is not passed during UCG
             *       initialization, UCG will assume that dtype is already a
             *       UCP datatype (will perform static cast to ucp_datatpe_t)
             *       and dtypes points to an array of such UCP datatypes.
             *       Also, setting dtype to NULL will be translated to a single
             *       byte (regardless of UCG_PARAM_FIELD_DATATYPE_CB).
             */
        };
    } send, recv;
    union {
        const char *name;   /* For debug, used if ENABLE_DEBUG_DATA is defined */
        uintptr_t reserved; /* For performance, used internally */
    };
} UCS_S_PACKED UCS_V_ALIGNED(64) ucg_collective_params_t;

#define UCG_PARAM_OP(_params)               ((_params)->recv.op)
#define UCG_PARAM_TYPE_IS_VALID(_params)    ((_params)->send.type.modifiers & \
                                             UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID)
#define UCG_PARAM_ROOT(_params)             (UCG_PARAM_TYPE_IS_VALID(_params) ? \
                                             ((_params)->send.type.root) : 0)
#define UCG_PARAM_MODIFIERS(_params)        (UCG_PARAM_TYPE_IS_VALID(_params) ? \
                                             ((_params)->send.type.modifiers) : \
                                             ((_params)->send.type.modifiers & \
                                              (UCG_GROUP_COLLECTIVE_MODIFIER_VARIADIC | \
                                               UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR)))


/**
 * @ingroup UCG_COLLECTIVE
 * @brief Query type for the UCG support of a collective operation.
 */
enum ucg_collective_support_query_field {
    UCG_COLLECTIVE_SUPPORT_QUERY_BY_TYPE,
    UCG_COLLECTIVE_SUPPORT_QUERY_BY_PARAMS
};

/**
 * @ingroup UCG_COLLECTIVE
 * @brief Query parameters for the UCG support of a collective operation.
 */
typedef struct ucg_collective_support_params {
    enum ucg_collective_support_query_field query;
    union {
        ucg_collective_type_t type;
        const ucg_collective_params_t *params;
    };
} ucg_collective_support_params_t;


/**
 * @ingroup UCG_GROUP
 * @brief Create a group object.
 *
 * This routine allocates and initializes a @ref ucg_group_h "group" object.
 * This routine is a "collective operation", meaning it has to be called for
 * each worker participating in the group - before the first call on the group
 * is invoked on any of those workers. The call does not contain a barrier,
 * meaning a call on one worker can complete regardless of call on others.
 *
 * @note The group object is allocated within context of the calling thread
 *
 * @param [in] worker        Worker to create a group on top of.
 * @param [in] params        User defined @ref ucg_group_params_t configurations
 *                           for the @ref ucg_group_h "UCG group".
 * @param [out] group_p      A pointer to the group object allocated by UCG
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucg_group_create(const ucg_group_params_t *params,
                              ucg_group_h *group_p);


/**
 * @ingroup UCG_GROUP
 * @brief Destroy a group object.
 *
 * This routine releases the resources associated with a @ref ucg_group_h
 * "UCG group". This routine is also a "collective operation", similarly to
 * @ref ucg_group_create, meaning it must be called on each worker participating
 * in the group.
 *
 * @warning Once the UCG group handle is destroyed, it cannot be used with any
 * UCG routine.
 *
 * The destroy process releases and shuts down all resources associated with
 * the @ref ucg_group_h "group".
 *
 * @param [in]  group        Group object to destroy.
 */
void ucg_group_destroy(ucg_group_h group);


/**
 * @ingroup UCG_GROUP
 * @brief Get attributes specific to a particular group.
 *
 * This routine fetches information about the group.
 *
 * @param [in]  group        Group object to query.
 * @param [out] attr         Filled with attributes of a group.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucg_group_query(ucg_group_h group,
                             ucg_group_attr_t *attr);


/**
 * @ingroup UCP_WORKER
 * @brief Create a listener to accept connections on. Connection requests on
 * the listener will arrive at a local address specified by the user.
 *
 * This routine creates a new listener object that is bound to a specific
 * local address.
 * The listener will listen to incoming connection requests.
 * After receiving a request from the remote peer, an endpoint to this peer
 * will be created - either right away or by calling @ref ucp_ep_create,
 * as specified by the callback type in @ref ucp_listener_params_t.
 * The user's callback will be invoked once the endpoint is created.
 *
 * @param [in]  group        Group object to create the listener on.
 * @param [in]  bind_address Local address to bind and listen on.
 * @param [out] listener_p   A handle to the created listener, can be released
 *                           by calling @ref ucg_group_listener_destroy
 *
 * @return Error code as defined by @ref ucs_status_t
 *
 * @note @ref ucp_listener_params_t::conn_handler or
 *       @ref ucp_listener_params_t::accept_handler must be provided to be
 *       able to handle incoming connections.
 */
ucs_status_t ucg_group_listener_create(ucg_group_h group,
                                       ucs_sock_addr_t *bind_address,
                                       ucg_listener_h *listener_p);

ucs_status_t ucg_group_listener_connect(ucg_group_h group,
                                        ucs_sock_addr_t *listener_addr);

/**
 * @ingroup UCP_WORKER
 * @brief Stop accepting connections on a local address of the group object.
 *
 * This routine unbinds the worker from the given handle and stops
 * listening for incoming connection requests on it.
 *
 * @param [in] listener      A handle to the listener to stop listening on.
 */
void ucg_group_listener_destroy(ucg_listener_h listener);

/**
 * @ingroup UCG_COLLECTIVE
 * @brief Checks if a collective operation is supported by UCG.
 * The parameters describe a collective operation, and this function indicates
 * whether a subsequent call to @ref ucg_collective_create would be supported.
 *
 * @param [in]  params       Collective operation description to query about.
 *
 * @return UCS_OK if the collective operation is supported by UCG, and error
 *         code as defined by @ref ucs_status_t otherwise.
 */
ucs_status_t ucg_collective_is_supported(const ucg_collective_support_params_t *params);


/**
 * @ingroup UCG_COLLECTIVE
 * @brief Creates a collective operation on a group object.
 * The parameters are intentionally non-constant, to allow UCG to write-back some
 * information and avoid redundant actions on the next call. For example, memory
 * registration handles are written back to the parameters pointer passed to the
 * function, and are re-used in subsequent calls.
 *
 * @param [in]  group        Group of participants in this collective operation.
 * @param [in]  params       Collective operation parameters.
 * @param [out] coll         Collective operation handle.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucg_collective_create(ucg_group_h group,
                                   const ucg_collective_params_t *params,
                                   ucg_coll_h *coll);


/**
 * @ingroup UCG_COLLECTIVE
 * @brief Starts a collective operation.
 *
 * @param [in]  coll         Collective operation handle.
 * @param [in]  user_request Request context for by the user (for callbacks).
 * @param [out] progress_f_p Function to use for progress/completion polling.
 *
 * @return UCS_OK           - The collective operation was completed immediately.
 * @return UCS_PTR_IS_ERR(_ptr) - The collective operation failed and an error
 *                                code indicates the status.
 * @return otherwise        - The collective operation has started, and can be
 *                            completed at any point in time. A request handle
 *                            is returned to the application in order to track
 *                            progress of the operation: either through the
 *                            worker-wide @ref ucp_worker_progress or through
 *                            the progress function returned (the latter is only
 *                            recommended for blocking calls). No need to close
 *                            the returned handle - it'll become useless once
 *                            the collective is complete (further use of it in
 *                            progress calls will be useless, yet harmless).
 */
ucs_status_ptr_t ucg_collective_start(ucg_coll_h coll, void *user_request,
                                      ucg_collective_progress_t *progress_f_p);


/**
 * @ingroup UCG_COLLECTIVE
 * @brief Obtain the request pointer passed to a collective operation.
 *
 * @param [in]  op           Operation returned by @ref ucg_collective_start .
 *
 * @return request pointer originally passed to @ref ucg_collective_start .
 */
void* ucg_collective_get_request(void *op);


/**
 * @ingroup UCG_COLLECTIVE
 * @brief Check the status of an active collective operation.
 *
 * This routine checks the state of an operation and returns its current status.
 * Any value different from UCS_INPROGRESS means that request is in a completed
 * state.
 *
 * @param [in]  request     Non-blocking collective operation to check.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucg_collective_check_status(void *request);


/**
 * @ingroup UCG_COLLECTIVE
 * @brief Get a pointer to the status of an active collective operation.
 *
 * This routine helps the caller check the state of an operation and its current
 * status more frequently than @ref ucg_collective_check_status . Any value
 * different from UCS_INPROGRESS means that request is in a completed state.
 *
 * @param [in]  request     Non-blocking collective operation to check.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
volatile ucs_status_t* ucg_collective_get_status_ptr(void *request);


/**
 * @ingroup UCG_COLLECTIVE
 * @brief Obtain the progress function to be applied to a collective operation.
 *
 * This routine returns a pointer to another one. The latter would explicitly
 * progresses a single collective operation request.
 *
 * @param [in]  coll         Collective operation handle.
 *
 * @return A valid function pointer.
 */
ucg_collective_progress_t ucg_collective_get_progress(ucg_coll_h coll);


/**
 * @ingroup UCG_COLLECTIVE
 * @brief Cancel an outstanding collective request.
 *
 * @param [in]  op           Operation returned by @ref ucg_collective_start .
 *
 * This routine tries to cancels an outstanding collective request.  After
 * calling this routine, the @a request will be in completed or canceled (but
 * not both) state regardless of the status of the target endpoint associated
 * with the collective request. If the request is completed successfully,
 * the completion callback will be called with the @a status argument of the
 * callback set to UCS_OK, and in a case it is canceled the @a status argument
 * is set to UCS_ERR_CANCELED.
 */
ucs_status_t ucg_collective_cancel(void *op, void *req);


/**
 * @ingroup UCG_COLLECTIVE
 * @brief Destroys a collective operation handle.
 *
 * This is only required for persistent collectives, where the flag
 * UCG_GROUP_COLLECTIVE_MODIFIER_PERSISTENT is passed when calling
 * @ref ucg_collective_create. Otherwise, the handle is
* destroyed when the collective operation is completed.
 *
 * @param [in]  coll         Collective operation handle.
 *
 * @return Error code as defined by @ref ucs_status_t
 */
void ucg_collective_destroy(ucg_coll_h coll);


/**
 * @ingroup UCG_CONTEXT
 * @brief Read UCG configuration descriptor
 *
 * The routine fetches the information about UCG library configuration from
 * the run-time environment. Then, the fetched descriptor is used for
 * UCG library @ref ucg_init "initialization". The Application can print out the
 * descriptor using @ref ucg_config_print "print" routine. In addition
 * the application is responsible for @ref ucg_config_release "releasing" the
 * descriptor back to the UCG library.
 *
 * @param [in]  env_prefix   If non-NULL, the routine searches for the
 *                           environment variables that start with
 *                           @e \<env_prefix\>_UCX_ prefix.
 *                           Otherwise, the routine searches for the
 *                           environment variables that start with
 *                           @e UCX_ prefix.
 * @param [in]  filename     If non-NULL, read configuration from the file
 *                           defined by @e filename. If the file does not
 *                           exist, it will be ignored and no error reported
 *                           to the application.
 * @param [out] config_p     Pointer to configuration descriptor as defined by
 *                           @ref ucg_config_t "ucg_config_t".
 *
 * @return Error code as defined by @ref ucs_status_t
 */
ucs_status_t ucg_config_read(const char *env_prefix, const char *filename,
                             ucg_config_t **config_p);


/**
 * @ingroup UCG_CONTEXT
 * @brief Release configuration descriptor
 *
 * The routine releases the configuration descriptor that was allocated through
 * @ref ucg_config_read "ucg_config_read()" routine.
 *
 * @param [out] config       Configuration descriptor as defined by
 *                           @ref ucg_config_t "ucg_config_t".
 */
void ucg_config_release(ucg_config_t *config);


/**
 * @ingroup UCG_CONTEXT
 * @brief Modify context configuration.
 *
 * The routine changes one configuration setting stored in @ref ucg_config_t
 * "configuration" descriptor.
 *
 * @param [in]  config       Configuration to modify.
 * @param [in]  name         Configuration variable name.
 * @param [in]  value        Value to set.
 *
 * @return Error code.
 */
ucs_status_t ucg_config_modify(ucg_config_t *config, const char *name,
                               const char *value);


/**
 * @ingroup UCG_CONTEXT
 * @brief Print configuration information
 *
 * The routine prints the configuration information that is stored in
 * @ref ucg_config_t "configuration" descriptor.
 *
 * @todo Expose ucs_config_print_flags_t
 *
 * @param [in]  config       @ref ucg_config_t "Configuration descriptor"
 *                           to print.
 * @param [in]  stream       Output stream to print the configuration to.
 * @param [in]  title        Configuration title to print.
 * @param [in]  print_flags  Flags that control various printing options.
 */
void ucg_config_print(const ucg_config_t *config, FILE *stream,
                      const char *title, ucs_config_print_flags_t print_flags);


/** @cond PRIVATE_INTERFACE */
/**
 * @ingroup UCG_CONTEXT
 * @brief UCG context initialization with particular API version.
 *
 * This is an internal routine used to check compatibility with a particular
 * API version. @ref ucg_init should be used to create UCG context.
 */
ucs_status_t ucg_init_version(unsigned ucg_api_major_version,
                              unsigned ucg_api_minor_version,
                              const ucg_params_t *params,
                              const ucg_config_t *config,
                              ucg_context_h *context_p);
/** @endcond */


/**
 * @ingroup UCG_CONTEXT
 * @brief UCG context initialization.
 *
 * This routine creates and initializes a @ref ucg_context_h
 * "UCG application context".
 *
 * @warning This routine must be called before any other UCG function
 * call in the application.
 *
 * This routine checks API version compatibility, then discovers the available
 * network interfaces, and initializes the network resources required for
 * discovering of the network and memory related devices.
 *  This routine is responsible for initialization all information required for
 * a particular application scope, for example, MPI application, OpenSHMEM
 * application, etc.
 *
 * @param [in]  config       UCG configuration descriptor allocated through
 *                           @ref ucg_config_read "ucg_config_read()" routine.
 * @param [in]  params       User defined @ref ucg_params_t configurations for the
 *                           @ref ucg_context_h "UCG application context".
 * @param [out] context_p    Initialized @ref ucg_context_h
 *                           "UCG application context".
 *
 * @return Error code as defined by @ref ucs_status_t
 */
static inline ucs_status_t ucg_init(const ucg_params_t *params,
                                    const ucg_config_t *config,
                                    ucg_context_h *context_p)
{
    return ucg_init_version(UCG_API_MAJOR, UCG_API_MINOR, params, config,
                            context_p);
}


/**
 * @ingroup UCG_CONTEXT
 * @brief Release UCG application context.
 *
 * This routine finalizes and releases the resources associated with a
 * @ref ucg_context_h "UCG application context".
 *
 * @warning An application cannot call any UCG routine
 * once the UCG application context released.
 *
 * The cleanup process releases and shuts down all resources associated with
 * the application context. After calling this routine, calling any UCG
 * routine without calling @ref ucg_init "UCG initialization routine" is invalid.
 *
 * @param [in] context       Handle to @ref ucg_context_h "UCG application context".
 */
void ucg_cleanup(ucg_context_h context);


/**
 * @ingroup UCG_CONTEXT
 * @brief Print context information.
 *
 * This routine prints information about the context configuration: including
 * memory domains, transport resources, and other useful information associated
 * with the context.
 *
 * @param [in] context       Print this context object's configuration.
 * @param [in] stream        Output stream on which to print the information.
 */
void ucg_context_print_info(const ucg_context_h context, FILE *stream);


/**
 * @ingroup UCG_CONTEXT
 * @brief Gain access to the internal UCB context within a UCG context.
 *
 * This routine allows applications using UCG to call UCB APIs as well.
 *
 * @param [in] context       Handle to @ref ucg_context_h "UCG application context".
 *
 * @return Handle to an internal @ref ucb_context_h "UCB application context".
 */
ucb_context_h ucg_context_get_ucb(ucg_context_h context);


/**
 * @ingroup UCG_CONTEXT
 * @brief Gain access to the internal array of (unexpected) wireup messages.
 *
 * This routine allows future UCG groups to poll for unexpected wireup messages.
 *
 * @param [in] context       Handle to @ref ucg_context_h "UCG application context".
 *
 * @return Handle to an internal @ref ucg_message_pool_h "UCG wireup pool".
 */
ucg_message_pool_h ucg_context_get_wireup_message_pool(ucg_context_h context);


/**
 * @ingroup UCG_CONTEXT
 * @brief Get UCG library version.
 *
 * This routine returns the UCG library version.
 *
 * @param [out] major_version  Filled with library major version.
 * @param [out] minor_version  Filled with library minor version.
 * @param [out] release_number Filled with library release number.
 */
void ucg_get_version(unsigned *major_version, unsigned *minor_version,
                     unsigned *release_number);


/**
 * @ingroup UCG_CONTEXT
 * @brief Get UCG library version as a string.
 *
 * This routine returns the UCG library version as a string which consists of:
 * "major.minor.release".
 */
const char *ucg_get_version_string(void);


END_C_DECLS

#endif
