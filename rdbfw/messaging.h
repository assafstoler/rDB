//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

#ifndef MESSAGING_H
#define MESSAGING_H

#include "rdbfw.h"

#define RDBMSG_RC_NO_MATCH       0
#define RDBMSG_RC_IS_SUBSCRIBER  1

#define RDBMSG_USE_MALLOC 0
#define RDBMSG_USE_UNIQUE_FWALLOC 1
#define RDBMSG_USE_SHARED_FWALLOC 2

// Note: The following 4 enum's does not overlap.
// This was done to allow delivery speed optimizations.
// It is no longer needed so overlap is OK, but i left it as is as I see no reason to change.

#ifdef __GNUC__
#define NO_UNUSED_WARNING __attribute__ ((unused))
#else
#define NO_UNUSED_WARNING
#endif


#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,


//The following two structures must stay in sync for the overlapping portion.
typedef struct rdbmsg_msg_s {
    int             from;
    int             to;
    int             group;
    int             id;
    int             len;
    int64_t         emit_ns;
    void            *data;
    int             legacy;     // Legacy message will be (also) transleted to legacy netlink messag system 
    int             use_fwalloc; // use framework allocator for emit_message()
    void            (*data_cleanup)(void *); // function used to free the data section, if any NULL is ok (flat)
} rdbmsg_msg_t;

typedef struct rdbmsg_internal_msg_s {
    int             from;
    int             to;
    int             group;
    int             id;
    int             len;
    int64_t         emit_ns;
    void            *data;
    int             legacy;     // Legacy message will be (also) transleted to legacy netlink messag system 
    int             use_fwalloc; // use framework allocator for emit_message()
    void            (*data_cleanup)(void *); // function used to free the data section, if any NULL is ok (flat)
    int             rc;         // this is used as a return code for message dispatch lookup.
    int             emitted;    // count for how many clients this message has been emitted
    // related to WIP - custom message queues
    rdb_pool_t              *msg_q_pool; // !null == this message uses custom dispatch pool
    pthread_mutex_t         *msg_mutex;  // and mutex
    pthread_cond_t          *msg_condition;  // and condition
    void            *data_ref;  // pointer to data to _up_ref_ if we use emit_by_ref
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
    int                     use_fwalloc; // use framework allocator for emit_message()
    rdb_pool_t              *pvt_msg_q_pool; // !null == this message uses custom dispatch pool
    pthread_mutex_t         *pvt_msg_mutex;  // and mutex
    pthread_cond_t          *pvt_msg_condition;  // and condition
    void                    (*data_cleanup)(void *); // function used to free the data section, if any NULL is ok (flat)
} rdbmsg_dispatch_t;

typedef struct rdbmsg_msg_type_s {
    rdb_bpp_t               pp[2];      // rdb pool index data
    uint32_t                value;
    char                    string[64];
} rdbmsg_msg_type_t;


// Used as a FIFO queue, one for each module (two if I already added out-of-bound
// messaging)

typedef struct rdbmsg_queue_s {
    rdb_bpp_t               pp[1];      // Required for rDB unmanaged tree.
    rdbmsg_msg_t             msg;
} rdbmsg_queue_t;


void rdbmsg_register_hooks(void);

int rdbmsg_register_msg_type (char *type, char *msg);
int rdbmsg_lookup_id (char *str);
char * rdbmsg_lookup_string (uint32_t value);

int rdbmsg_init(rdb_pool_t *plugin_pool);
int rdbmsg_request(void  *ctx, int from, int to, int group, int id);
int rdbmsg_request_custom(void  *ctx, int from, int to, int group, int id,
        int use_fwalloc,
        rdb_pool_t *msg_q_pool,
        pthread_mutex_t *msg_mutex,
        pthread_cond_t *msg_condition );
int rdbmsg_emit_simple(int from, int to, int group, int id, int data);
int rdbmsg_emit (int from, int to, int group, int id, int length, void *data, void (*data_cleanup)(void *));
int rdbmsg_emit_log (int from, int to, int group, int id, int length, void *data, int unlock);
int rdbmsg_delay_HZ(int new_Hz);
int rdbmsg_free (plugins_t *ctx, rdbmsg_queue_t *q);
int rdbmsg_implode (plugins_t *ctx, rdbmsg_queue_t *q);
void rdbmsg_destroy_tree (void *data, void *user_ptr, int stage);
int rdbmsg_destroy (void);
void rdbmsg_clean(void);

#endif
