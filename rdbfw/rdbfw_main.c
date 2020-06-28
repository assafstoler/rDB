//All rights reserved.
//see LICENSE for more info

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include <netdb.h>
#include <netinet/in.h>

#include <unistd.h>

#include <string.h>
#include <time.h>
#include <pthread.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>

#include "rDB.h"
#include "messaging.h"
#include "utils.h"
#include "rdbfw.h"
#include "fwalloc.h"
#include <errno.h>
#include <locale.h>
#include "signal.h"
#include "log.h"

#ifdef USE_PRCTL
#include <sys/prctl.h>
#endif


#define PLUGINS_SUFFIX "_rdbfw_fns"

FILE *logger = NULL;
char out_path[255] = {0};
int unittest_en = 0;
int local_libs = 0;
char log_log_buf[256];
char sig_log_buf[256];
const char *rdbfw_app_name = NULL;

uint32_t log_level = LOG_WARN;
pthread_mutex_t  log_mutex;

static int automated_test  = 0;
static int break_requested = 0;
static int signal_trapped = 0;
static int rdbfw_active = 0;
        
static pthread_mutex_t   main_mutex;
static pthread_cond_t    main_condition;
static pthread_t         main_thread;
    
static struct timespec time_start,
               time_end,
               delta_time;

static char *eptr = NULL;
    
//rdb_pool_t *plugin_pool;
//plugins_t *plugin_node = NULL;
    
extern int optind;

// used to alloate a unique auto-id for auto-context dynamic plugins
static uint32_t ctx_auto_id = 0;

//Protorypes
//
static void fw_term(int rc, rdb_pool_t *plugin_pool);

//Code...
//

static int help_cb(void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    if ((*p->plugin_info).opt_help) {
        (*p->plugin_info).opt_help(p);
    }
    return RDB_CB_OK;
}
static void help_and_exit(rdb_pool_t *plugin_pool)
{
    printf( "%s (rDB+rdbfw framework library)\n"
            "\n"
            "Usage: ...\n\n"
            "%s [global options]\n\n"
            "-v int\t: Set log level 0-7 (Off/Fatal/Error/Warning/Info/Dbg/Dbg+/Trace)\n"
            "-u int\t: Run unittest #\n"
            "-t int\t: test mode for # seconds (auto exit after timeout - for valgrind and alike)\n\n"
            "ENV Variables:\n"
            "USE_LOCAL_LIB=1 \t: Use local (./) for module library path.  Default: Use ldconfig PATH\n"
            "RDBFW_UT=nnn \t: Run realy stage unit test #nnn - this is an alternate way to -u, as -u can not operate at early\n\tstages, prior to args being processed, which only happen after module load\n"
            ,
            (char *) rdbfw_app_name,
            (char *) rdbfw_app_name );

    rdbfw_app_help();
    
    printf( "[--module [options]]\n" );
    rdb_iterate(plugin_pool,  1, help_cb, NULL, NULL, NULL); 
}

/* This callback is called on for every module listed in the
 * loadable list
 */

static int load_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    char *buf = NULL;
    int *rc = user_ptr;
    
    if (!p || !p->name || !p->pathname) {
        if (p && p->name) {
            fwl (LOG_ERROR, p, "failed to load plugin %s\n", p->name);
            goto load_plugin_cb_err;
        } else if (p && p->pathname) {
            fwl (LOG_ERROR, p, "failed to load plugin %s\n", p->pathname);
            goto load_plugin_cb_err;
        } else {
            fwl (LOG_ERROR, p, "failed to load plugin\n");
            goto load_plugin_cb_err;
        }
    }
    
    if (unittest_en == UT_LOAD_PLUGIN_1 || p->state != RDBFW_STATE_NULL) {
        fwl (LOG_ERROR, p, "called while plugin %s is loaded", p->name);
        //goto load_plugin_cb_err;
        return RDB_CB_OK;
    }

        
#ifdef STATIC_BUILD
    p->handle = dlopen (NULL , RTLD_LAZY| RTLD_GLOBAL);
#else
    p->handle = dlopen (p->pathname , RTLD_NOW| RTLD_GLOBAL);
#endif
    if (p->handle == NULL) {
        eptr = dlerror();
        fwl (LOG_ERROR, p, "failed to load plugin %s - %s\n", p->pathname, eptr);
        //eptr=dlerror();
        goto load_plugin_cb_err;
    }
    
    if (unittest_en != UT_LOAD_PLUGIN_2) {
        buf = malloc (strlen(p->name) + strlen(PLUGINS_SUFFIX) + 1);
    }
    if (buf == NULL) {
        fwl (LOG_ERROR, p, "malloc error\n");
        goto load_plugin_cb_err;
    }

    strcpy(buf,p->name);
    strcat(buf,"_rdbfw_fns");

    if (unittest_en != UT_LOAD_PLUGIN_3) {
        p->plugin_info = (rdbfw_plugin_api_t *) dlsym(p->handle, buf); 
    }
    if ((p->plugin_info == NULL) ||
            ((eptr = dlerror()) != NULL))  {
        fwl (LOG_ERROR, p, "failed dlsym() : %s - %s\n", eptr, buf);
        //eptr=dlerror();
        goto load_plugin_cb_err;
    }

    free(buf);

    p->state = RDBFW_STATE_LOADED;
    fwl (LOG_INFO, p, "Loaded %p, %s\n", p, p->name);
   
    return RDB_CB_OK;

load_plugin_cb_err:

    if (p->handle) {
        dlclose(p->handle);
        p->handle = NULL;
    }
    if (buf) {
        free (buf);
        buf = NULL;
    }
    *rc = 1;
    return RDB_CB_OK;

}

static int drop_plugin_cb (void *data, void *user_ptr){
    plugins_t *p;
    p = (plugins_t *) data;
    int rc=0;
    fwl (LOG_DEBUG, p, "dlclose(%s)\n", p->uname);

    if (p->handle) {
        rc = dlclose(p->handle);
        fwl (LOG_DEBUG, p, "dlclose(%s) %d\n", p->uname, rc);
    }

    p->state = RDBFW_STATE_NULL;

    rdbmsg_destroy_tree (p->msg_dispatch_root, NULL, 0);
    //rdb_flush (p->msg_dispatch_root, NULL, NULL);
    rdb_drop_pool (p->msg_dispatch_root);
    p->msg_dispatch_root = NULL;

    rdb_flush(p->msg_q_pool, NULL, NULL);
    rdb_drop_pool(p->msg_q_pool);    
    rdb_flush(p->empty_msg_store,NULL,NULL);
    rdb_drop_pool(p->empty_msg_store);    
    
    // other direct p->* free'd by unlink_plugin_cb() which is automatically called
    return RDB_CB_DELETE_NODE;
}


static int de_init_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_STOPPED || 
            p->state == RDBFW_STATE_INITIALIZED ||
             p->state == RDBFW_STATE_INITIALIZING) {
        (*p->plugin_info).de_init(p);
        while (p->state != RDBFW_STATE_LOADED) {
            usleep(0);
        }
        pthread_mutex_destroy(&p->msg_mutex);
        pthread_mutex_destroy(&p->startup_mutex);
        pthread_cond_destroy(&p->msg_condition);
        return RDB_CB_OK;
    } 
    else {
        fwl (LOG_ERROR, p, "%s NOT stopped during plugin de_init. state = %d\n", p->name, p->state);
    }

    return RDB_CB_OK;
}

static int pre_init_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    int *failure = (int *) user_ptr;

    if (p->state == RDBFW_STATE_LOADED) {
        if ((*p->plugin_info).pre_init) {
            (*p->plugin_info).pre_init(p);
            fwl (LOG_INFO, p, "%s\n", p->uname);
        }
        return RDB_CB_OK;
    }
    else {
        fwl (LOG_ERROR, p, "Pre_Init called while not in RDBFW_STATE_LOADED state\n");
        *failure = 1;
    }

    return RDB_CB_ABORT;
}

static int init_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    int *failure = (int *) user_ptr;

    if (unittest_en != UT_INIT_1 && p->state == RDBFW_STATE_LOADED) {
        fwl (LOG_INFO, p, "Module %s init: Start\n", p->name);
        pthread_mutex_init(&p->msg_mutex, NULL);
        pthread_mutex_init(&p->startup_mutex, NULL);
        pthread_cond_init(&p->msg_condition, NULL);
        (*p->plugin_info).init(p);
        //TODO: high priority - handle stopall request or we can deadock here. true elsewhere too
        while (p->state != RDBFW_STATE_INITIALIZED &&
                p->state != RDBFW_STATE_STOPALL) {
            usleep(0);
        }
        fwl (LOG_INFO, p, "Module %s init: Done\n", p->name);
        return RDB_CB_OK;
    }
    else {
        fwl (LOG_ERROR, p, "Init called while not in RDBFW_STATE_LOADED state\n");
        *failure = 1;
    }

    return RDB_CB_ABORT;
}

static int start_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    int *failure = (int *) user_ptr;

    if (p->state == RDBFW_STATE_INITIALIZED) {
        fwl (LOG_INFO, p, "START: %s\n", p->name);
        if ((*p->plugin_info).start)  (*p->plugin_info).start(p);
        while (p->state != RDBFW_STATE_RUNNING &&
                p->state != RDBFW_STATE_STOPALL) {
            usleep(0);
        }
        fwl (LOG_INFO, p, "RUNNING: %s\n", p->name);
        return RDB_CB_OK;
    }
    else {
        fwl (LOG_ERROR, p, "Start called while not in RDBFW_STATE_INTIALIZED state\n");
        *failure = 1;
    }


    return RDB_CB_ABORT;
}

//TODO: private signal condition?!
static int relay_signal_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_RUNNING || p->state == RDBFW_STATE_SOFTSTOPALL) {

        if ((*p->plugin_info).signal) {
            p->sig_id = signal_trapped;
            (*p->plugin_info).stop(p);
        }
        pthread_mutex_lock(&p->msg_mutex);
        pthread_cond_signal(&p->msg_condition);
        pthread_mutex_unlock(&p->msg_mutex);
        fwl (LOG_INFO, p, "%s\n", p->uname);
        //exit(0);
        return RDB_CB_OK;
    } 
    return RDB_CB_OK;
}
//TODO: cycle all pvt message Q's ?!
static int stop_unittest_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (strstr(p->name, "unittest") == NULL) {
        return RDB_CB_OK;
    } 
    if (p->state == RDBFW_STATE_RUNNING || p->state == RDBFW_STATE_SOFTSTOPALL) {
        fwl (LOG_INFO, p, "stopping %s\n", p->name);
        if ((*p->plugin_info).stop)  (*p->plugin_info).stop(p);
        pthread_mutex_lock(&p->msg_mutex);
        pthread_cond_signal(&p->msg_condition);
        pthread_mutex_unlock(&p->msg_mutex);
        while (p->state != RDBFW_STATE_STOPPED) {
            usleep(0);
        }
        fwl (LOG_INFO, p, "%s\n", p->name);
        return RDB_CB_OK;
    } 
    else {
        fwl (LOG_DEBUG, p, "NOT stopping %s as it's not in running state\n", p->name);
    }
    return RDB_CB_OK;
}
static int stop_timers_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (strstr(p->name, "timers") == NULL) {
        return RDB_CB_OK;
    } 
    if (p->state == RDBFW_STATE_RUNNING || p->state == RDBFW_STATE_SOFTSTOPALL) {
        // this one only does timers.
        // NOTE: it may be timers.c or hw_timers.c or ios_timers or alike. watch not to
        //       name other modules with the string "timers", or change this code.
        fwl (LOG_INFO, p, "stopping %s\n", p->name);
        //p->state = RDBFW_STATE_STOPPING;
        if ((*p->plugin_info).stop)  (*p->plugin_info).stop(p);
        pthread_mutex_lock(&p->msg_mutex);
        pthread_cond_signal(&p->msg_condition);
        pthread_mutex_unlock(&p->msg_mutex);
        while (p->state != RDBFW_STATE_STOPPED) {
            usleep(0);
        }
        fwl (LOG_INFO, p, "%s\n", p->name);
        return RDB_CB_OK;
    } 
    else {
        fwl (LOG_DEBUG, p, "NOT stopping %s as it's not in running state\n", p->name);
    }
    return RDB_CB_OK;
}

static int stop_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (strstr(p->name, "timers") != NULL) {
        return RDB_CB_OK;
    } 
    if (strstr(p->name, "unittest") != NULL) {
        return RDB_CB_OK;
    } 
    if (p->state == RDBFW_STATE_RUNNING || p->state == RDBFW_STATE_SOFTSTOPALL) {
        // all others
        fwl (LOG_INFO, p, "stopping %s\n", p->name);
        //p->state = RDBFW_STATE_STOPPING;
        if ((*p->plugin_info).stop)  (*p->plugin_info).stop(p);
        pthread_mutex_lock(&p->msg_mutex);
        pthread_cond_signal(&p->msg_condition);
        pthread_mutex_unlock(&p->msg_mutex);
        while (p->state != RDBFW_STATE_STOPPED) {
            usleep(0);
        }
        fwl (LOG_INFO, p, "%s\n", p->name);
        return RDB_CB_OK;
    }
    else {
        fwl (LOG_DEBUG, p, "NOT stopping %s as it's not in running state\n", p->name);
    }
    return RDB_CB_OK;
}

#define MARK_TASK_RUNNING 1 
#define MARK_TASK_STOPALL 2
static int display_run_state_cb(void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    fwl (LOG_INFO, p, "%s %d\n",p->name,p->state);
    return RDB_CB_OK;
}
static int any_running_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    int *ar;
    ar=user_ptr;

    if ((p->state == RDBFW_STATE_RUNNING) ||
        (p->state == RDBFW_STATE_STOPPING) ||
        (p->state == RDBFW_STATE_SOFTSTOPALL)) {
        *ar= *ar | MARK_TASK_RUNNING;
    } 
    if (p->state == RDBFW_STATE_STOPALL || p->state == RDBFW_STATE_SOFTSTOPALL) {
        *ar= *ar | MARK_TASK_STOPALL;
    }
    return RDB_CB_OK;
}

static int stopall_requested(rdb_pool_t *plugin_pool) {
        int any_running=0;
        rdb_iterate(plugin_pool,  1, any_running_cb, (void *) &any_running, NULL, NULL); 
        if (any_running & MARK_TASK_STOPALL) return 1;
        return 0;
}
static int running(rdb_pool_t *plugin_pool) {
        int any_running=0;
        rdb_iterate(plugin_pool,  1, any_running_cb, (void *) &any_running, NULL, NULL); 
        if (any_running & MARK_TASK_RUNNING) return 1;
        return 0;
}

#ifdef FALSE
static int print_counters_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    
    fwl (LOG_INFO, p, "%s totals\n", p->name);
    fwl (LOG_INFO, p, "rx_count=%" PRIu64 " - " ,p->msg_rx_count);
    fwl (LOG_INFO, p, "wake_count=%" PRIu64 "\n" ,p->wakeup_count);
    return RDB_CB_OK;
}

static int dump_cb (void *data, void *user_ptr) {
    rdbmsg_queue_t *p;
    p = (rdbmsg_queue_t *) data;

    if (p) {
        printf("%d dump\n", p->msg.legacy);
    }
    else {
        printf("ND\n");
    }
    return RDB_CB_OK;
}

static int rdb_dump_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    printf("%p, %s, %d\n",p, p->name, p->state);
    return RDB_CB_OK;
}
#endif

// how much may we append to the given name. out longest is '.group', so 6
#define PLUGIN_NAME_SUFFIX_MAX 9 
#define PLUGIN_LIB_PREFIX "lib"
#define PLUGIN_LCL_LIB_PREFIX "./lib"
#define PLUGIN_LIB_PREFIX_LEN (sizeof(PLUGIN_LCL_LIB_PREFIX))
#define PLUGIN_LIB_SUFFIX ".so"
#define PLUGIN_LIB_SUFFIX_LEN (sizeof(PLUGIN_LIB_SUFFIX))

int register_plugin(
        char *name, 
        rdb_pool_t *plugin_pool,
        int msg_slots,
        uint32_t req_ctx_id
        /*int argc,
        char **argv*/) {
    plugins_t *plugin_node;
    uint32_t ctx_id;
    char *buf;
    int i;
    rdbmsg_queue_t *q;

    if (name == NULL) {
        goto register_plugin_err;
    }
    
    buf = malloc(strlen(name) + PLUGIN_NAME_SUFFIX_MAX + 1) ;
    if (buf == NULL) {
        goto register_plugin_err;
    }

    plugin_node = calloc (1,sizeof (plugins_t));
    
    if (plugin_node == NULL) {
        free (buf);
        goto register_plugin_err;
    }

    plugin_node->name = malloc(strlen(name) + 1) ;
    if (plugin_node->name == NULL) {
        free (plugin_node);
        free (buf);
        goto register_plugin_err;
    }
    plugin_node->uname = malloc(strlen(name) + NAME_EXTRA_LEN) ;
    if (plugin_node->uname == NULL) {
        free (plugin_node->name);
        free (plugin_node);
        free (buf);
        goto register_plugin_err;
    }
    if ( req_ctx_id == CTX_DYNAMIC ) {
        ctx_id = ctx_auto_id++;
    }
    else {
        ctx_id = req_ctx_id;
    }
    plugin_node->ctx_id = ctx_id;

    sprintf(plugin_node->name, "%s", name);
    sprintf(plugin_node->uname, "%s.%"PRIu32"", name, ctx_id);

#ifdef __MACH__
    plugin_node->pathname = malloc(PLUGIN_LIB_PREFIX_LEN + strlen(name) + strlen(bundle_path) + PLUGIN_LIB_SUFFIX_LEN + 1) ;
#else
    plugin_node->pathname = malloc(PLUGIN_LIB_PREFIX_LEN + strlen(name) + PLUGIN_LIB_SUFFIX_LEN + 1) ;
#endif
    if (plugin_node->pathname == NULL) {
        free (plugin_node->name);
        free (plugin_node->uname);
        free (plugin_node);
        free (buf);
        goto register_plugin_err;
    }
#ifdef __MACH__
    //TODO: Add auto prefix assignment if bundle_path == NULL
    strcpy (plugin_node->pathname, bundle_path);
    if (local_libs) {
        strcat (plugin_node->pathname, PLUGIN_LCL_LIB_PREFIX);
    }
    else {
        strcat (plugin_node->pathname, PLUGIN_LIB_PREFIX);
    }
#else
    if (local_libs) {
        strcpy (plugin_node->pathname, PLUGIN_LCL_LIB_PREFIX);
    }
    else {
        strcpy (plugin_node->pathname, PLUGIN_LIB_PREFIX);
    }
#endif
    strcat (plugin_node->pathname, name);
    strcat (plugin_node->pathname, PLUGIN_LIB_SUFFIX);

    plugin_node->plugin_info = NULL;
    plugin_node->state = RDBFW_STATE_NULL;
    plugin_node->fault_group = 0;
    sprintf(buf,"%s.q", plugin_node->uname);
    plugin_node->msg_q_pool = rdb_register_um_pool ( buf,
                                     1, 0, RDB_KFIFO | RDB_NO_IDX | RDB_BTREE, NULL ); 
    
    sprintf(buf,"%s.ems",plugin_node->uname);
    plugin_node->empty_msg_store = rdb_register_um_pool ( buf,
                                     1, 0, RDB_KFIFO | RDB_NO_IDX | RDB_BTREE, NULL ); 
    for (i = 0; i < msg_slots; i++) {
        q = calloc (1,sizeof (rdbmsg_queue_t));
        if (q == NULL) {
            fwlog (LOG_ERROR,
                    "Unable to allocate all requested messaages for %s. (req: %d, available %d)\n",
                    plugin_node->uname, msg_slots, i);
            break;
        }
        
        if (0 == rdb_insert(plugin_node->empty_msg_store, q)) {
            fwlog (LOG_ERROR,
                    "Failed to insert msg_buffer into data pool for plugin %s. Discarding\n",
                    plugin_node->uname);
            free (q);
        }
    }
    
    sprintf(buf,"%s.root",plugin_node->uname);
    plugin_node->msg_dispatch_root  = rdb_register_um_pool ( buf,
                        1, 0, RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL ); 
    if (0 == rdb_insert(plugin_pool, plugin_node)) {
        fwlog (LOG_ERROR, 
                "Failed to insert %s to plugin_pool. this plugin will NOT function\n",
                buf);
        if (plugin_node->msg_dispatch_root != NULL) {
            rdb_drop_pool(plugin_node->msg_dispatch_root);    
        }
        free (plugin_node->uname);
        free (plugin_node->name);
        free (plugin_node);
        free (buf);
        goto register_plugin_err;
    }

    free (buf);
    return 0;

register_plugin_err:

    return -1;
}

static void sig_func(int sig) {
    if (sig == SIGPIPE) {
        sigfwlog (LOG_WARN, "Caught signal %d\n", sig);
        signal_trapped = sig;
        pthread_mutex_lock(&main_mutex);
        pthread_cond_signal(&main_condition);
        pthread_mutex_unlock(&main_mutex);
    }
    else if (sig == SIGINT) {
        sigfwlog (LOG_WARN, "Caught signal %d\n", sig);
        signal_trapped = sig;
        break_requested = 1;
        pthread_mutex_lock(&main_mutex);
        pthread_cond_signal(&main_condition);
        pthread_mutex_unlock(&main_mutex);
    }
    else if (sig == SIGUSR1) {
        sigfwlog (LOG_WARN, "Caught signal %d\n", sig);
        signal_trapped = sig;
        pthread_mutex_lock(&main_mutex);
        pthread_cond_signal(&main_condition);
        pthread_mutex_unlock(&main_mutex);
    }
    else if (sig == SIGUSR2) {
        sigfwlog (LOG_WARN, "Caught signal %d\n", sig);
        signal_trapped = sig;
        pthread_mutex_lock(&main_mutex);
        pthread_cond_signal(&main_condition);
        pthread_mutex_unlock(&main_mutex);
    }
    else if (sig == SIGPIPE) {
        fwlog (LOG_WARN, "Caught sigpipe %d\n", sig);
        signal_trapped = sig;
        pthread_mutex_lock(&main_mutex);
        pthread_cond_signal(&main_condition);
        pthread_mutex_unlock(&main_mutex);
    }
    else if (sig == SIGQUIT) {
        fwlog (LOG_WARN, "Caught SigQUIT %d\n", sig);
        signal_trapped = sig;
        break_requested = 1;
        pthread_mutex_lock(&main_mutex);
        pthread_cond_signal(&main_condition);
        pthread_mutex_unlock(&main_mutex);
    }
    else if (sig == SIGTERM) {
        fwlog (LOG_WARN, "Caught SigTERM %d\n", sig);
        signal_trapped = sig;
        break_requested = 1;
        pthread_mutex_lock(&main_mutex);
        pthread_cond_signal(&main_condition);
        pthread_mutex_unlock(&main_mutex);
    }
    else {
        sigfwlog (LOG_WARN, "Caught signal %d\n", sig);
        signal_trapped = sig;
        pthread_mutex_lock(&main_mutex);
        pthread_cond_signal(&main_condition);
        pthread_mutex_unlock(&main_mutex);
    }
}

static void unlink_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p && p->name) {
        free(p->name);
    }
    if (p && p->uname) {
        free(p->uname);
    }
    if (p && p->pathname) {
        free(p->pathname); 
    }
    if (p) {
        free(p);
        p = NULL;
    }
    return;
}

typedef struct args_copy_s {
    int argc;
    char **argv;
    plugins_t *p;
    int err;
} args_copy_t;

static int args_copy_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    args_copy_t *ac = ( args_copy_t * ) user_ptr;
    int used = 0;
    int me = 0;

    if (unittest_en != UT_ARGS_1) {
        p->argv = calloc (1, (ac->argc+1) * sizeof *p->argv);
    }
    if (NULL == p->argv) {
        goto args_cp_err;
    }

    for(int i = 0; i < ac->argc; ++i)
    {
        if ( i != 0 && ! me ) {
            fwl (LOG_TRACE, ac->p, "arg %s %s %s\n", ac->argv[i], ac->argv[i+1], p->name);
            if ( 0 == strcmp ( ac->argv[i], "-m" ) && ac->argv[i+1] != NULL &&
                    strcmp (ac->argv[i+1], 
                        (p->friendly_name != NULL) ? p->friendly_name : p->name ) == 0 ) {
                // us starting
                fwl (LOG_TRACE, ac->p, "module \"%s\"\n", ac->argv[i+1]);
                i+= 1;
                me = 1;
            }
        }
        else {
            if ( 0 == strcmp ( ac->argv[i], "-m" ) ) {
                me = 0;
                continue;
            }
            if (me || i == 0) {
                fwl (LOG_TRACE, ac->p, "module opt \"%d, %s\"\n", i, ac->argv[i]);
                size_t length = strlen(ac->argv[i])+1;
                if (unittest_en != UT_ARGS_2) {
                    p->argv[used] = malloc(length);
                }
                if (NULL == p->argv[used]){
                    goto args_cp_err;
                }
                memcpy ( p->argv[used], ac->argv[i], length);
                used++;
            }
        }
    }
    used++;
    p->argv[used] = NULL;
    p->argc = used - 1;

    return RDB_CB_OK;

args_cp_err:
    ac->err = 1; 
    fwl (LOG_ERROR, NULL, "Alloc failure during argc clone\n");
    return RDB_CB_ABORT;

}

static int args_free_cb(void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->argv) {
        for(int i = 0; i < p->argc; ++i) {
            if (p->argv[i]) {
                free (p->argv[i]);
                p->argv[i] = NULL;
            }
        }
        free (p->argv);
    }
    return RDB_CB_OK;
}


void rdbfw_update_state (plugins_t *ctx, rdbfw_plugin_state_e state) {
    ctx->state = state;
    pthread_mutex_lock(&main_mutex);
    pthread_cond_signal(&main_condition);
    pthread_mutex_unlock(&main_mutex);

}
int rdbfw_is_running (void) {
    pthread_mutex_lock(&main_mutex);
    pthread_cond_signal(&main_condition);
    pthread_mutex_unlock(&main_mutex);
    return (rdbfw_active);
}

int rdbfw_stop (void) {
    pthread_mutex_lock(&main_mutex);
    break_requested = 1;
    pthread_cond_signal(&main_condition);
    pthread_mutex_unlock(&main_mutex);
    pthread_join (main_thread, NULL);
    rdbfw_active = 0;
    return (rdbfw_active);
}
// wait for natural stoppage (signaled by one of the active modules)
int rdbfw_wait (void) {
    pthread_mutex_unlock(&main_mutex);
    pthread_join (main_thread, NULL);
    rdbfw_active = 0;
    return (rdbfw_active);
}

void *rdbfw_main_loop (void *plugin_pool) {
    char sbuf[8096];
    int rc;
    struct timespec main_timeout;

    //pthread_mutex_lock(&main_mutex);
#ifdef USE_PRCTL
    prctl(PR_SET_NAME,"rdbfw_main_loop\0",NULL,NULL,NULL);
#endif
    clock_gettime(CLOCK_REALTIME, &main_timeout);
    main_timeout.tv_sec += automated_test;

    while (running (plugin_pool)) {
        rc = 0;
        while (rc == 0) {
            if (automated_test) rc = pthread_cond_timedwait (&main_condition, &main_mutex, &main_timeout);
            else rc = pthread_cond_wait(&main_condition, &main_mutex);

            if (signal_trapped || break_requested || stopall_requested(plugin_pool)) rc = 1;
        }

        if (signal_trapped) {
            rdb_iterate(plugin_pool,  1, relay_signal_cb, NULL, NULL, NULL); 
            signal_trapped = 0;
            if (! break_requested) {
                continue;
            }
        }

        if (log_level >= LOG_DEBUG) {
            fwl (LOG_DEBUG, NULL, "Pool status dump - pre-shutdown\n");
            rdb_print_pool_stats(sbuf,8096);
            fprintf(stderr,"%s\n",sbuf);
        }
        sbuf[0]=0;

        fwl (LOG_INFO, NULL, "SHUTDOWN started\n");
        rdb_iterate(plugin_pool,  2, stop_timers_plugin_cb, NULL, NULL, NULL); 
        fwl (LOG_INFO, NULL, "Timers halted\n");
        rdb_iterate(plugin_pool,  2, stop_plugin_cb, NULL, NULL, NULL); 
        fwl (LOG_INFO, NULL, "Plugins halted.\n");
        rdb_iterate(plugin_pool,  2, stop_unittest_plugin_cb, NULL, NULL, NULL); 
        fwl (LOG_INFO, NULL, "unittest Plugins halted.\n");
        rdb_iterate(plugin_pool,  1, display_run_state_cb, NULL, NULL, NULL); 
        break;
    }

    if ( rdb_error_string != NULL ) {
        fwl (LOG_ERROR, NULL, "SHOTDOWN RDB Error recorded: %s\n", rdb_error_string);
    }

    rdb_iterate(plugin_pool,  2, de_init_plugin_cb, NULL, NULL, NULL); 
    fw_term(-99, plugin_pool);
    pthread_mutex_unlock(&main_mutex);
    
    if (log_level >= LOG_DEBUG) {
        fwl (LOG_DEBUG, NULL, "Pool status dump - POST-Shutdown\n");
        rdb_print_pool_stats(sbuf,8096);
        fprintf(stderr,"%s\n",sbuf);
    }
    sbuf[0]=0;

    pthread_exit (NULL);
    exit(0);
    rdb_iterate(plugin_pool,  0, drop_plugin_cb, NULL, unlink_plugin_cb, NULL); 
    rdbmsg_clean();

    rdb_free_prealloc();
    if ( rdb_error_string != NULL ) {
        fwl (LOG_WARN, NULL, "SHOTDOWN err: %s\n", rdb_error_string);
    }

    //fw_term(-99, plugin_pool);
    //rdb_print_pools(logger); 

    fwl (LOG_INFO, NULL, "\n\nleft over data pools:\n")   ;
    rdb_print_pool_stats(sbuf,8096);
    fprintf(stderr,"%s\n",sbuf);
    //exit(0);

   // rdb_free_prealloc();

    rc = clock_gettime(CLOCK_REALTIME, &time_end);

    if (rc) {
        fwl (LOG_ERROR, NULL, "Error: Failed to get clock\n");
    } else {
        char time_buf[TIME_BUF_MAXLEN];
        s_ts_diff_time_ns(&time_start, &time_end, &delta_time);
        fwl (LOG_ERROR, NULL, "uptime: %s", snprint_ts_time (&delta_time, time_buf, TIME_BUF_MAXLEN) ); 
    }
        
    //rdb_iterate(plugin_pool,  0, print_counters_cb, NULL, NULL, NULL);
    // skipping teardown for demo
    //plugin_pool->drop = 1;
    rdb_gc();
    //info("###left:\n");
    //rdb_print_pools(stdout); 
    rdb_flush(plugin_pool, unlink_plugin_cb, NULL);    
    rdb_drop_pool(plugin_pool);    
    //plugin_pool->drop = 1;
    //rdb_gc();

    fwl (LOG_INFO, NULL, "left over data pools:\n")   ;
    rdb_print_pools(logger); 

    rdb_print_pool_stats(sbuf,4096);
    fwlog(LOG_WARN,"%s\n",sbuf);

    rdbfw_active = 0;
    
    pthread_mutex_unlock(&main_mutex);
    pthread_exit (NULL);
}

#ifdef RDB_USE_STD_MAIN
int main(int argc, char *argv[])
#else
#ifdef SHARED_ONLY
int rdbfw_main(int argc, char *argv[], char *app_name)
#else
int main(int argc, char *argv[])
#endif
#endif
{
    int rc;
    int show_help = 0;
    int c;
    args_copy_t ac;
    pthread_attr_t attr;
    rdb_pool_t *plugin_pool;

    rdbfw_app_name = app_name;
    
    log_level = LOG_WARN;

    pthread_mutex_init (&main_mutex, NULL);
    pthread_mutex_lock(&main_mutex);
    pthread_cond_init (&main_condition, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    rdbfw_active = 1;
    opterr = 0;
    
    setlocale (LC_NUMERIC, "");
    pthread_mutex_init(&log_mutex, NULL);
    
    // default handler
    struct sigaction act ;

    act.sa_handler = sig_func;
    sigemptyset (&act.sa_mask);
    act.sa_flags = 0;

    sigaction (SIGPIPE, &act, NULL);
    sigaction (SIGUSR1, &act, NULL);
    sigaction (SIGUSR2, &act, NULL);
    sigaction (SIGQUIT, &act, NULL);
    sigaction (SIGTERM, &act, NULL);
    sigaction (SIGINT, &act, NULL);

    // Just until we opened up our logger
    log_level = LOG_INFO;
    logger = stdout;
    fwl (LOG_INFO, NULL, "--Mark--\n");
    const char* ut_str = getenv("RDBFW_UT");
    if (NULL != ut_str) {
        unittest_en = atoi(ut_str);
        log_level = LOG_DEBUG_MORE;
        fwl (LOG_INFO, NULL, "Unittest via ENV: %d\n", unittest_en);
    }
    const char* lib_str = getenv("RDBFW_LOCAL_LIB");
    if (NULL != lib_str) {
        local_libs = 1;
        fwl (LOG_INFO, NULL, "using ./ Library PATH\n");
    }
    else fwl (LOG_INFO, NULL, "NOT using ./ Library PATH\n");


    // initilize the rDB library
    rdb_init();

    // register plugins pool
    plugin_pool = rdb_register_um_pool(
        "plugins",
        3,
        0,
        RDB_KPSTR | RDB_KASC | RDB_BTREE,
        NULL);

    rdb_register_um_idx (plugin_pool, 1, 0, RDB_KFIFO | RDB_NO_IDX | RDB_BTREE, NULL ); 
    rdb_register_um_idx (plugin_pool, 2, 0, RDB_KLIFO | RDB_NO_IDX | RDB_BTREE, NULL ); 

    if (NULL != plugin_pool) {
    
        // Initilize the messaging system. this must follow an rdb_init as it's uses rdb.
        if (-1 != rdbmsg_init (plugin_pool)) {
            // and framework allocation
            if (-1 != rdbfw_alloc_init ()) {
                // allow app to prealloc RAM
                if (-1 != rdbfw_app_prealloc()) {
                    // allow app to register it's plug-in modules
                    if (-1 != rdbfw_app_register_plugins (plugin_pool)) {
                        //load plugins
                        rc = 0;
                        rdb_iterate(plugin_pool,  1, load_plugin_cb, &rc, NULL, NULL); 
                        if (0 == rc) {
    
                            ac.argc = argc;
                            ac.argv = argv;
                            ac.p = NULL;
                            ac.err = 0;
                            rdb_iterate(plugin_pool,  1, args_copy_cb, &ac, NULL, NULL); 
                            if (!ac.err) {
                                int done_with_opts = 0;

                                optind = 0;
                                while ((c = getopt (argc, argv, "-o:v:u:t:hm::")) != -1) {
                                    switch (c) {
                                        case 'm':
                                            done_with_opts = 1;
                                            break;
                                        case 'o':
                                            strcpy (out_path, optarg);
                                            if (strlen(out_path)) {
                                                logger = fopen(out_path, "a");
                                                if (logger == NULL) {
                                                    logger = stdout;
                                                    fwl (LOG_INFO, NULL, "--Mark--\n");
                                                    fwl (LOG_ERROR, NULL, "FATAL: Failed to open output file %s. using stdout\n", out_path);
                                                } 
                                                else {
                                                    fwl (LOG_INFO, NULL, "--Mark--\n");
                                                }
                                            }
                                            break;
                                        case 'v':
                                            log_level = atoi (optarg);
                                            fwl (LOG_INFO, NULL, "Setting log_level %d\n", log_level);
                                            break;
                                        case 'u':
                                            unittest_en = atoi (optarg);
                                            break;
                                        case 't':
                                            automated_test = atoi (optarg);
                                            break;
                                        case 'h':
                                            show_help = 1;
                                            break;
                                        default:
                                            // module opts or break
                                            break;
                                    }
                                    if ( done_with_opts ) break;
                                }
                                if (!show_help) {
                                    optind = 0;
                                    //TODO: add process opts unit tests
                                    if (-1 != rdbfw_app_process_opts (argc, argv)) {
                                        // init all plugins
                                        // TODO:
                                        // add error reporting/abort
                                        int failure=0;
                                        rdb_iterate(plugin_pool,  1, pre_init_plugin_cb, &failure, NULL, NULL); 
                                        if (!failure) {
                                            failure = 0;
                                            rdb_iterate(plugin_pool,  1, init_plugin_cb, &failure, NULL, NULL); 
                                            if (!failure) {

                                                rdbfw_app_config_timers();

                                                // start all plugins
                                                rc = clock_gettime(CLOCK_REALTIME, &time_start);

                                                rdb_iterate(plugin_pool,  1, start_plugin_cb, NULL, NULL, NULL); 

                                                // run until all plugins (tests) have stoped

                                                    fwl (LOG_INFO, NULL, "Invoking timers thread loop\n");
                                                rc = pthread_create( &main_thread, &attr, rdbfw_main_loop, plugin_pool);

                                                if ( 0 == rc ) {
                                                    fwl (LOG_INFO, NULL, "RUNNING FULLY\n");
                                                }
                                                else {
                                                    rc = -12;
                                                }
                                            }
                                            else {
                                                rc = -11;
                                            }
                                        } 
                                        else {
                                            rc = -10;
                                        }
                                    } 
                                    else {
                                        // app_opts reported fail (abort)
                                        rc = -9;
                                    }
                                }
                                else {
                                    // main opts failure
                                    rc = -8;
                                    help_and_exit(plugin_pool);
                                }
                            }
                            else {
                                rc = -7;
                                // args
                            }
                        }
                        else {
                            // load_plugin_cb failure
                            rc = -6;
                        }
                    }
                    else {
                        // registre_plugin_failure
                        rc = -5;
                    }
                }
                else {
                    //app_prealloc_failure
                    rc = -4;
                }
            }
            else {
                // alloc_init_failure
                rc = -3;
            }
        }
        else {
            //rdbmsg_init_failure
            rc = -2;
        }
    }
    else {
        // plugin_pool registration error
        fwl (LOG_FATAL, NULL, "Plugin_pool Registration failure\n");
        rc = -1;
    }
    fwl_no_emit (LOG_ERROR, NULL, "startup rc = %d\n", rc);

    fw_term (rc, plugin_pool);

    return rc;

}

static void fw_term(int rc, rdb_pool_t *plugin_pool) {
    struct sigaction noact;

    noact.sa_handler = SIG_DFL;
    sigemptyset (&noact.sa_mask);
    noact.sa_flags = 0;

    if (rc <= -7) {
        rdb_lock(plugin_pool, __FUNCTION__);
        rdb_iterate(plugin_pool,  2, args_free_cb, NULL, NULL, NULL); 
        rdb_unlock(plugin_pool, __FUNCTION__);
    }

    if (rc <= -5) {
        rdb_lock(plugin_pool, __FUNCTION__);
        rdb_iterate(plugin_pool,  2, drop_plugin_cb, NULL, unlink_plugin_cb, NULL); 
        rdb_unlock(plugin_pool, __FUNCTION__);
    }
    if (rc <= -4) {
        rdb_free_prealloc();
    }
    if (rc <= -3) {
        rdbmsg_clean();
        rdbmsg_destroy();
    }
    if (rc <= -2) {
        //rdbmsg_clean();
    }
    if (rc <= -1) {
        sigaction (SIGPIPE, &noact, NULL);
        sigaction (SIGUSR1, &noact, NULL);
        sigaction (SIGUSR2, &noact, NULL);
        sigaction (SIGQUIT, &noact, NULL);
        sigaction (SIGTERM, &noact, NULL);
        sigaction (SIGINT, &noact, NULL);
       
        // for any error, we also need to clean up the basics
        rdb_flush (plugin_pool, NULL, NULL);
        rdb_drop_pool (plugin_pool);
    }
}
