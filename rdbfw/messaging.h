#ifndef MESSAGING_H
#define MESSAGING_H

#include "rdbfw.h"

#define RDBMSG_RC_NO_MATCH       0
#define RDBMSG_RC_IS_SUBSCRIBER  1

// Note: The following 4 enum's does not overlap.
// This was done to allow delivery speed optimizations.
// It is no longer needed so overlap is OK, but i left it as is as I see no reason to change.
typedef enum {
    RDBMSG_ROUTE_NA=0,
    RDBMSG_ROUTE_FW,
    RDBMSG_ROUTE_MDL_PINGPONG,
    RDBMSG_ROUTE_MDL_SOCKEER_COMM,
    RDBMSG_ROUTE_MDL_TIMERS,
    RDBMSG_ROUTE_MDL_TIMED,
    RDBMSG_ROUTE_MDL_EVENT_SKEL,
    RDBMSG_ROUTE_MAX=1023,
} rdbmsg_routing_e ;

typedef enum {
    RDBMSG_GROUP_NA=2048,
    RDBMSG_GROUP_TIMERS,
    RDBMSG_GROUP_LOGGING,
    RDBMSG_GROUP_MAX=3095,
} rdbmsg_group_e;

typedef enum {
    RDBMSG_ID_NA=3096,
    RDBMSG_ID_TIMER_START,
    RDBMSG_ID_TIMER_STOP,
    RDBMSG_ID_TIMER_ACK,
    RDBMSG_ID_TIMER_TICK_0,
    RDBMSG_ID_TIMER_TICK_1,
    RDBMSG_ID_TIMER_TICK_2,
    RDBMSG_ID_TIMER_TICK_3,
    RDBMSG_ID_TIMER_TICK_4,
    RDBMSG_ID_TIMER_TICK_5,
    RDBMSG_ID_TIMER_TICK_6,
    RDBMSG_ID_TIMER_TICK_7,
    RDBMSG_ID_TIMER_TICK_8,
    RDBMSG_ID_TIMER_TICK_9,
    RDBMSG_ID_TIMER_TICK_10,
    RDBMSG_ID_TIMER_TICK_11,
    RDBMSG_ID_TIMER_TICK_12,
    RDBMSG_ID_TIMER_TICK_13,
    RDBMSG_ID_TIMER_TICK_14,
    RDBMSG_ID_TIMER_TICK_15,
    RDBMSG_ID_LAST_GASP,
    RDBMSG_ID_MAX=65535,
} rdbmsg_id_e;


//The following two structures must stay in sync for the overlapping portion.
typedef struct rdbmsg_msg_s {
    rdbmsg_routing_e    from;
    rdbmsg_routing_e      to;
    rdbmsg_group_e   group;
    rdbmsg_id_e      id;
    int             len;
    void            *data;
    int             legacy;     // Legacy message will be (also) transleted to legacy netlink messag system 
} rdbmsg_msg_t;

typedef struct rdbmsg_internal_msg_s {
    rdbmsg_routing_e    from;
    rdbmsg_routing_e      to;
    rdbmsg_group_e   group;
    rdbmsg_id_e      id;
    int             len;
    void            *data;
    int             legacy;     // Legacy message will be (also) transleted to legacy netlink messag system 
    int             rc;         // this is used as a return code for message dispatch lookup.
    int             emitted;    // count for how many clients this message has been emitted
} rdbmsg_internal_msg_t;


// rdbmsg users list is fifo - no need to sort as we alwys need to iterate all possible users for each message emitted
// for each user we keep a list for each grouping type

/*typedef struct rdbmsg_user_entry_s {
    rdb_bpp_t       pp[1]; // Required for rDB unmanaged tree.
    int             value;
#ifdef RDBMSG_ACCOUNTING
    uint64_t        count;
#endif
} rdbmsg_user_t;
*/

// Below is the main data node used in the tree-of-trees (or our 4 dimentional
// tree structure) which we use to quickly know which module is expecting which
// messages. see documentation for more info

typedef struct rdbmsg_dispatch_s {
    rdb_bpp_t               pp[1];      // rdb pool index data
    uint32_t                value;      // value of [from | to | group | id]
    rdb_pool_t              *next;       // pointer to next level tree (form->to->group->id)
} rdbmsg_dispatch_t;


// Used as a FIFO queue, one for each module (two if I already added out-of-bound
// messaging)

typedef struct rdbmsg_queue_s {
    rdb_bpp_t               pp[1];      // Required for rDB unmanaged tree.
    rdbmsg_msg_t             msg;
} rdbmsg_queue_t;


void rdbmsg_register_hooks(void);

int rdbmsg_init(rdb_pool_t *plugin_pool);
int rdbmsg_request(void  *ctx, int from, int to, int group, int id);
int rdbmsg_emit_simple(int from, int to, int group, int id, int data);
int rdbmsg_delay_HZ(int new_Hz);
int rdbmsg_free (plugins_t *ctx, rdbmsg_queue_t *q);
void rdbmsg_destroy_tree (void *data, void *user_ptr, int stage);

#endif
