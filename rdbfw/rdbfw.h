
#ifndef __RDBFW_H
#define __RDBFW_H
/***
 * Below are defined the functions that may be supported by loadable modules.
 *
 *   init: function is used for any pre-run work that needs to take place. such 
 * as allocating memory, zeroing of variables, etc. do not trust load-time 
 * defaults as functions may be 'un, and re-initilised during run-time.
 *
 *   make_depend: function calle by framework after all modules are loaded and
 * initilized, used by framework to ask each module to verify and report any
 * other modules it depends upon.
 *
 *   start: signal the module to start performing it's tasks.
 *
 *   stop: signal module to go idle (only respond to framework / inquery messages
 * 
 *   NOTE: when a module is stopped, and started, it will retain it's state. this menns 
 * INITILIZED state equals (STOPPED with all default values).
 * going from STOPPED to RUNNING must be supported. as well as re-init to start
 * fread.
 *
 *   de_init: opposite of init. free all memory, close handles, etc (preper module
 * for possible unloading. de_init returns a modile to RDBFW_STATE_NULL;
 *
 *   msg_pending: called when there is at-least one message in the process inbox.
 *
 *   last_gasp: called when last gasp is detected. moduels should stop non 
 * essential operations and hastly save / transmit any data requiring so.
 * system should not be rendeder in-op by last gasp. power may not really 
 * go down, in such case a last_gast_delete will follow, and operations should
 * normally resume. during last_gasp, no other functions will be called, except
 * last_gasp_delete
 *
 *   capabilities: module reports a string containing it's capabilities. this 
 * is to be used for inquery by other modules / FW, as needed
 *
 *   state: return the current running state, as is defined in the enum below
 *
 *   normal flow could look something like:
 * .init -> .make_depend -> .start -> .stop -> .start -> .last_gasp -> 
 * .last_gasp_depete -> .stop -> .de_init
 *
 *   Modules may or may not be unloaded from the FW after a de_init call. 
 ***/
#include "rDB.h"

#define MAX_THREAD_RETRY 10

typedef struct rdbfw_plugin_api_s {
    void (*init)(void *);                // Initilize the module, if requited (allocation, zero of pointers)
    void (*make_depend)(void *);
    void (*start)(void *);
    void (*stop)(void *);
    void (*de_init)(void *);
    void (*msg_pending)(void *);
    void (*last_gasp)(void *);
    void (*last_gast_delete)(void *);
    void (*capabilities)(void *);
    void (*state)(void *);
} rdbfw_plugin_api_t;

typedef enum {
    RDBFW_STATE_NULL=0,
    RDBFW_STATE_LOADED,
    RDBFW_STATE_INITILIZING, // not sure about the ING entries, may not be used / removed
    RDBFW_STATE_INITILIZED,
    RDBFW_STATE_RUNNING,
    RDBFW_STATE_GASPING,
    RDBFW_STATE_STOPPING,
    RDBFW_STATE_STOPPED,
    RDBFW_STATE_STOPALL,    // tells the framework to stop all tasks, ie, after fatal error // self won't be called to stop as it aborted
    RDBFW_STATE_SOFTSTOPALL,    // tells the framework to stop all tasks, ie, normal stop requst. even SELF need ot be stopped
} rdbfw_plugin_state_e ;

typedef struct plugins_s {
    rdb_bpp_t               pp[1]; // Required for rDB unmanaged tree with two indexes
    char                    *name;
    char                    *pathname;
    rdbfw_plugin_api_t      *plugin_info;
    rdbfw_plugin_state_e    state;
    void                    *handle;        // used by dlopen
    char                    *error;         // "
    uint32_t                fault_group;
    rdb_pool_t              *msg_q_pool;
    rdb_pool_t              *empty_msg_store;
    rdb_pool_t              *msg_dispatch_root;
    uint32_t                msg_pending_count;      
    pthread_mutex_t         msg_mutex;
    pthread_mutex_t         startup_mutex;
    pthread_cond_t          msg_condition;
#ifdef MSG_ACCOUNTING
    uint64_t                msg_rx_count;
    uint64_t                msg_tx_count;
#endif
#ifdef WAKEUP_ACCOUNTING
    uint64_t                wakeup_count;
#endif
} plugins_t;

extern uint64_t wake_count_limit;

#ifdef SHARED_ONLY
//This function exists as an entrypoint for when the used as a shared library - linked with another app.
//Pass argc and argv into it as you would normally use the command line version of this function.
int rdbfw_main(int argc, char *argv[]);
#endif

#endif
