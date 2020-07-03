//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info


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
 * INITIALIZED state equals (STOPPED with all default values).
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
#include <stdio.h>
#include <rdb/rDB.h>

// Used by moduels which can only load once
static const uint64_t CTX_SINGULAR = 0;

// Used by moduels which can dynamically multiply
static const uint64_t CTX_DYNAMIC = 0xFFFFFFFFFFFFFFFF;

// User Contexes - where more then one invication of a plugin is supported
static const uint64_t CTX_SERVER = ( 1 << 0 );
static const uint64_t CTX_CLIENT = ( 1 << 1 );

#define MAX_THREAD_RETRY 10

// suffix used in plugins for dlsym() benefit
// expecting module-name_suffix
#define PLUGINS_SUFFIX "_rdbfw_fns"

#define TIME_BUF_MAXLEN 32

extern char out_path[255];
extern FILE *logger;
extern int unittest_en;

static const int NAME_EXTRA_LEN = 11; //10 byte context ID + null

typedef struct rdbfw_plugin_api_s {
    void (*init)(void *);                // Initilize the module, if requited (allocation, zero of pointers)
    void (*make_depend)(void *);
    void (*start)(void *);
    void (*stop)(void *);
    void (*de_init)(void *);
    void (*msg_pending)(void *);
    void (*last_gasp)(void *);
    void (*last_gasp_delete)(void *);
    void (*capabilities)(void *);
    void (*state)(void *);
    void (*signal)(void *);
    void (*opt_help)(void *);
    void (*pre_init)(void *);           // set uo stuff like set friendly name, etc
} rdbfw_plugin_api_t;

typedef enum {
    RDBFW_STATE_NULL=0,
    RDBFW_STATE_LOADED,
    RDBFW_STATE_INITIALIZING, // not sure about the ING entries, may not be used / removed
    RDBFW_STATE_INITIALIZED,
    RDBFW_STATE_RUNNING,
    RDBFW_STATE_GASPING,
    RDBFW_STATE_STOPPING,
    RDBFW_STATE_STOPPED,
    RDBFW_STATE_STOPALL,    // tells the framework to stop all tasks, ie, after fatal error // self won't be called to stop as it aborted
    RDBFW_STATE_SOFTSTOPALL,    // tells the framework to stop all tasks, ie, normal stop requst. even SELF need ot be stopped
} rdbfw_plugin_state_e ;

typedef struct plugins_s {
    rdb_bpp_t               pp[3]; // Required for rDB unmanaged tree with three indexes
    char                    *uname; // unique name - 
    char                    *name; // non-unique - used for plugin loading / file finding
    char                    *friendly_name;
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
//#ifdef MSG_ACCOUNTING
    uint64_t                msg_rx_count;
    uint64_t                msg_tx_count;
//#endif
//#ifdef WAKEUP_ACCOUNTING
    uint64_t                wakeup_count;
//#endif
    void                    *ctx;           // user context pointer
    uint64_t                ctx_id;         // unique # associated with 'this' context's job
    int                     sig_id;         // signal (only set for signal() fn)
    int                     argc;
    char                    **argv;

} plugins_t;

extern uint64_t wake_count_limit;

int rdbfw_main(int argc, char *argv[], char *app_name);

int register_plugin(
        char *name, 
        rdb_pool_t *plugin_pool,
        int msg_slots,
        uint32_t req_ctx_id
        );

void rdbfw_app_help(void);
int rdbfw_app_process_opts (int argc, char **argcv);
int rdbfw_app_prealloc ( void );
int rdbfw_app_register_plugins ( rdb_pool_t *plugin_pool );
void rdbfw_app_config_timers ( void );
void rdbfw_update_state (plugins_t *ctx, rdbfw_plugin_state_e state);
int rdbfw_is_running (void);
int rdbfw_stop (void);
int rdbfw_wait (void);
//int fwl (int level, void *p, ...);

extern rdb_pool_t *plugin_pool;
extern const char *rdbfw_app_name;

extern uint32_t log_level;
extern pthread_mutex_t  log_mutex;

// UNITTEST Test definitiond

#define UT_MSG_INIT_1 1
#define UT_MSG_INIT_2 2
#define UT_MSG_INIT_3 3

#define UT_ALLOC_INIT_1 10
#define UT_ALLOC_INIT_2 11

#define UT_ALLOC_PRE_1 20
#define UT_ALLOC_PRE_2 21
#define UT_ALLOC_PRE_3 22
#define UT_ALLOC_PRE_4 23
#define UT_ALLOC_PRE_5 24
#define UT_ALLOC_PRE_6 25
#define UT_ALLOC_PRE_7 26
#define UT_ALLOC_PRE_8 27

#define UT_REG_PLUGIN_1 30
#define UT_REG_PLUGIN_2 31
#define UT_LOAD_PLUGIN_1 32
#define UT_LOAD_PLUGIN_2 33
#define UT_LOAD_PLUGIN_3 34

#define UT_ARGS_1 40
#define UT_ARGS_2 41

#define UT_INIT_1 50

// framework error codes

#define RDBFW_SUCCESS 0
#define RDBFW_ERR_THREAD_CREATE     -12 // failure to spawn rdbfw_main_loop
#define RDBFW_ERR_INIT_MODULE       -11 // Failure to initilize a module
#define RDBFW_ERR_PRE_INIT_MODULE   -10 // Failure in pre-init stage
#define RDBFW_ERR_MODULE_OPTS       -9  // Failure processing opts
#define RDBFW_ERR_HELP_REQUESTED    -8  // Print help screen and abort indicated in command line opts
#define RDBFW_ERR_OPTS              -7  // Failure processing main args
#define RDBFW_ERR_MODULE_LOAD       -6  // Module load error (load_plugin_cb())
#define RDBFW_ERR_MODULE_REGISTER   -5  // Module Registration error (register_plugin_cb())
#define RDBFW_ERR_PREALLOC          -4  // memory (pre)allocation error
#define RDBFW_ERR_PREALLOC_INIT     -3  // Error initilizing internal memory allocator
#define RDBFW_ERR_MESSAGING_INIT    -2  // Messaging subsystem failed ot load
#define RDBFW_ERR_MAIN_POOL         -1  // Unable to register Main pool
// test 
#endif
