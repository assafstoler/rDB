#ifndef MESSAGING_H
#define MESSAGING_H

#include "rdbfw.h"

#define RDBMSG_RC_NO_MATCH       0
#define RDBMSG_RC_IS_SUBSCRIBER  1

// Note: The following 4 enum's does not overlap.
// This was done to allow delivery speed optimizations.
// It is no longer needed so overlap is OK, but i left it as is as I see no reason to change.

#ifdef __GNUC__
#define NO_UNUSED_WARNING __attribute__ ((unused))
#else
#define NO_UNUSED_WARNING
#endif

#define FOREACH_ROUTE(ROUTE) \
    ROUTE(RDBMSG_ROUTE_NA=0) \
    ROUTE(RDBMSG_ROUTE_FW) \
    ROUTE(RDBMSG_ROUTE_MDL_PINGPONG) \
    ROUTE(RDBMSG_ROUTE_MDL_SOCKEER_COMM) \
    ROUTE(RDBMSG_ROUTE_MDL_TIMERS) \
    ROUTE(RDBMSG_ROUTE_MDL_TIMED) \
    ROUTE(RDBMSG_ROUTE_MDL_EVENT_SKEL) \
    ROUTE(RDBMSG_ROUTE_MAX=1023) \

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

typedef enum ROUTE_ENUM {
    FOREACH_ROUTE(GENERATE_ENUM)
} rdbmsg_routing_e;

static const char NO_UNUSED_WARNING *ROUTE_STRING[] = {
    FOREACH_ROUTE(GENERATE_STRING)
};


#define FOREACH_GROUP(GROUP) \
    GROUP(RDBMSG_GROUP_NA=0) \
    GROUP(RDBMSG_GROUP_TIMERS) \
    GROUP(RDBMSG_GROUP_LOGGING) \
    GROUP(RDBMSG_GROUP_MAX=3095) \

typedef enum GROUP_ENUM {
    FOREACH_GROUP(GENERATE_ENUM)
} rdbmsg_group_e;

static const char NO_UNUSED_WARNING *GROUP_STRING[] = {
    FOREACH_GROUP(GENERATE_STRING)
};

#define FOREACH_ID(ID) \
    ID(RDBMSG_ID_NA=3096) \
    ID(RDBMSG_ID_TIMER_START) \
    ID(RDBMSG_ID_TIMER_STOP) \
    ID(RDBMSG_ID_TIMER_ACK) \
    ID(RDBMSG_ID_TIMER_TICK_0) \
    ID(RDBMSG_ID_TIMER_TICK_1) \
    ID(RDBMSG_ID_TIMER_TICK_2) \
    ID(RDBMSG_ID_TIMER_TICK_3) \
    ID(RDBMSG_ID_TIMER_TICK_4) \
    ID(RDBMSG_ID_TIMER_TICK_5) \
    ID(RDBMSG_ID_TIMER_TICK_6) \
    ID(RDBMSG_ID_TIMER_TICK_7) \
    ID(RDBMSG_ID_TIMER_TICK_8) \
    ID(RDBMSG_ID_TIMER_TICK_9) \
    ID(RDBMSG_ID_TIMER_TICK_10) \
    ID(RDBMSG_ID_TIMER_TICK_11) \
    ID(RDBMSG_ID_TIMER_TICK_12) \
    ID(RDBMSG_ID_TIMER_TICK_13) \
    ID(RDBMSG_ID_TIMER_TICK_14) \
    ID(RDBMSG_ID_TIMER_TICK_15) \
    ID(RDBMSG_ID_LAST_GASP) \
    ID(RDBMSG_ID_MUX_STATE) \
    ID(RDBMSG_ID_MAX=65535) \

    // SAPI will replace individual status messages. Id's above remain to maintain correct sequance ID's


typedef enum ID_ENUM {
    FOREACH_ID(GENERATE_ENUM)
} rdbmsg_id_e;

static const char NO_UNUSED_WARNING *ID_STRING[] = {
    FOREACH_ID(GENERATE_STRING)
};

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
int rdbmsg_emit (int from, int to, int group, int id, int length, void *data);
int rdbmsg_delay_HZ(int new_Hz);
int rdbmsg_free (plugins_t *ctx, rdbmsg_queue_t *q);
void rdbmsg_destroy_tree (void *data, void *user_ptr, int stage);
void rdbmsg_clean(void);

#endif
