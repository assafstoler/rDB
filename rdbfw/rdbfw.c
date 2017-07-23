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

#include "rDB.h"
#include "messaging.h"
#include "utils.h"
#include "rdbfw.h"

#include "signal.h"

#include "log.h"

#define PLUGINS_SUFFIX "_rdbfw_fns"

uint32_t log_level = LOG_INFO;
static int automated_test  = 0;
static int break_requested = 0;

//Protorypes
//
static void handle_fatal_error(void);

//Code...
//
static void help_and_exit(void)
{
    printf( "rDB Framework skeleton & demo\n"
            "\n"
            "Usage: ...\n"
                );
    exit (0);
}

/* This callback is called on for every module listed in the
 * loadable list
 */

static int load_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    char *buf = NULL;
    
    if (!p || !p->name || !p->pathname) {
        if (p && p->name) {
            log (LOG_ERROR, "failed to load plugin %s\n", p->name);
            goto load_plugin_cb_err;
        } else if (p && p->pathname) {
            log (LOG_ERROR, "failed to load plugin %s\n", p->pathname);
            goto load_plugin_cb_err;
        } else {
            log (LOG_ERROR, "failed to load plugin\n");
            goto load_plugin_cb_err;
        }
    }
    
    if (p->state != RDBFW_STATE_NULL) {
        log (LOG_ERROR, "called while plugin %s is loaded", p->name);
        return RDB_CB_OK;
    }

        
#ifdef STATIC_BUILD
    p->handle = dlopen (NULL , RTLD_LAZY| RTLD_GLOBAL);
#else
    p->handle = dlopen (p->pathname , RTLD_NOW| RTLD_LOCAL);
#endif
    if (p->handle == NULL) {
        log (LOG_ERROR, "failed to load plugin %s - %s\n", p->pathname, dlerror());
        goto load_plugin_cb_err;
    }
    
    buf = malloc (strlen(p->name) + strlen(PLUGINS_SUFFIX) + 1);
    if (buf == NULL) {
        log (LOG_ERROR, "malloc error\n");
        goto load_plugin_cb_err;
    }

    strcpy(buf,p->name);
    strcat(buf,"_rdbfw_fns");

    p->plugin_info = (rdbfw_plugin_api_t *) dlsym(p->handle, buf); 
    if ((p->error = dlerror()) != NULL)  {
        log (LOG_ERROR, "failed dlsym() : %s\n", p->error);
        free(buf);
        goto load_plugin_cb_err;
    }

    free(buf);

    p->state = RDBFW_STATE_LOADED;
    log (LOG_INFO, "Loaded %s\n", p->name);
   
    return RDB_CB_OK;

load_plugin_cb_err:

    handle_fatal_error();
    return RDB_CB_OK;

}

static int drop_plugin_cb (void *data, void *user_ptr){
    plugins_t *p;
    p = (plugins_t *) data;
    int rc=0;

    if (p->handle) {
        rc = dlclose(p->handle);
        log (LOG_DEBUG, "dlclose(%s) %d\n", p->name, rc);
    }

    p->state = RDBFW_STATE_NULL;

    rdb_flush(p->msg_q_pool, NULL, NULL);
    rdb_drop_pool(p->msg_q_pool);    
    rdb_flush(p->empty_msg_store,NULL,NULL);
    rdb_drop_pool(p->empty_msg_store);    
    rdbmsg_destroy_tree (p->msg_dispatch_root, NULL, 0);


    return RDB_CB_OK;
}


static int de_init_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_STOPPED || 
            p->state == RDBFW_STATE_INITILIZED ||
             p->state == RDBFW_STATE_INITILIZING) {
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
        log (LOG_ERROR, "%s NOT stopped during plugin de_init. state = %d\n", p->name, p->state);
    }

    return RDB_CB_OK;
}

static int init_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_LOADED) {
        pthread_mutex_init(&p->msg_mutex, NULL);
        pthread_mutex_init(&p->startup_mutex, NULL);
        pthread_cond_init(&p->msg_condition, NULL);
        (*p->plugin_info).init(p);
        //TODO: high priority - handle stopall request or we can deadock here. true elsewhere too
        while (p->state != RDBFW_STATE_INITILIZED) {
            usleep(0);
        }
        log (LOG_INFO, "%s\n", p->name);
        return RDB_CB_OK;
    }

    return RDB_CB_OK;
}

static int start_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_INITILIZED) {
        if ((*p->plugin_info).start)  (*p->plugin_info).start(p);
        while (p->state != RDBFW_STATE_RUNNING &&
                p->state != RDBFW_STATE_STOPALL) {
            usleep(0);
        }
        log (LOG_INFO, "%s\n", p->name);
        return RDB_CB_OK;
    }

    return RDB_CB_OK;
}

static int stop_timers_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_RUNNING || p->state == RDBFW_STATE_SOFTSTOPALL) {
        // this one only does timers.
        // NOTE: it may be timers.c or hw_timers.c or ios_timers or alike. watch not to
        //       name other modules with the string "timers", or change this code.
        if (strstr(p->name, "timers") == NULL) {
           return RDB_CB_OK;
        } 
        log (LOG_INFO, "stopping %s\n", p->name);
        //p->state = RDBFW_STATE_STOPPING;
        if ((*p->plugin_info).stop)  (*p->plugin_info).stop(p);
        pthread_mutex_lock(&p->msg_mutex);
        pthread_cond_signal(&p->msg_condition);
        pthread_mutex_unlock(&p->msg_mutex);
        while (p->state != RDBFW_STATE_STOPPED) {
            usleep(0);
        }
        log (LOG_INFO, "%s\n", p->name);
        return RDB_CB_OK;
    } 
    else {
        log (LOG_DEBUG, "NOT stopping %s as it's not in running state\n", p->name);
    }
    return RDB_CB_OK;
}

static int stop_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_RUNNING || p->state == RDBFW_STATE_SOFTSTOPALL) {
        // all others
        if (strstr(p->name, "timers") != NULL) {
           return RDB_CB_OK;
        } 
        log (LOG_INFO, "stopping %s\n", p->name);
        //p->state = RDBFW_STATE_STOPPING;
        if ((*p->plugin_info).stop)  (*p->plugin_info).stop(p);
        pthread_mutex_lock(&p->msg_mutex);
        pthread_cond_signal(&p->msg_condition);
        pthread_mutex_unlock(&p->msg_mutex);
        while (p->state != RDBFW_STATE_STOPPED) {
            usleep(0);
        }
        log (LOG_INFO, "%s\n", p->name);
        return RDB_CB_OK;
    }
    else {
        log (LOG_DEBUG, "NOT stopping %s\n", p->name);
    }
    return RDB_CB_OK;
}

#define MARK_TASK_RUNNING 1 
#define MARK_TASK_STOPALL 2
static int display_run_state_cb(void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    log (LOG_INFO, "%s %d\n",p->name,p->state);
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
        rdb_iterate(plugin_pool,  0, any_running_cb, (void *) &any_running, NULL, NULL); 
        if (any_running & MARK_TASK_STOPALL) return 1;
        return 0;
}
static int running(rdb_pool_t *plugin_pool) {
        int any_running=0;
        rdb_iterate(plugin_pool,  0, any_running_cb, (void *) &any_running, NULL, NULL); 
        if (any_running & MARK_TASK_RUNNING) return 1;
        return 0;
}

#ifdef FALSE
static int print_counters_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    
    log (LOG_INFO, "%s totals\n", p->name);
    log (LOG_INFO, "rx_count=%" PRIu64 " - " ,p->msg_rx_count);
    log (LOG_INFO, "wake_count=%" PRIu64 "\n" ,p->wakeup_count);
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
#define PLUGIN_NAME_SUFFIX_MAX 6 
#define PLUGIN_LIB_PREFIX "./lib"
#define PLUGIN_LIB_PREFIX_LEN (sizeof(PLUGIN_LIB_PREFIX))
#define PLUGIN_LIB_SUFFIX ".so"
#define PLUGIN_LIB_SUFFIX_LEN (sizeof(PLUGIN_LIB_SUFFIX))

static int register_plugin(char *name, rdb_pool_t *plugin_pool, plugins_t *plugin_node, int msg_slots) {
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
    strcpy (plugin_node->name, name);

    plugin_node->pathname = malloc(PLUGIN_LIB_PREFIX_LEN + strlen(name) + PLUGIN_LIB_SUFFIX_LEN + 1) ;
    if (plugin_node->pathname == NULL) {
        free (plugin_node->name);
        free (plugin_node);
        free (buf);
        goto register_plugin_err;
    }
    strcpy (plugin_node->pathname, PLUGIN_LIB_PREFIX);
    strcat (plugin_node->pathname, name);
    strcat (plugin_node->pathname, PLUGIN_LIB_SUFFIX);

    plugin_node->plugin_info = NULL;
    plugin_node->state = RDBFW_STATE_NULL;
    plugin_node->fault_group = 0;
    sprintf(buf,"%s.q",name);
    plugin_node->msg_q_pool = rdb_register_um_pool ( buf,
                                     1, 0, RDB_KFIFO | RDB_NO_IDX | RDB_BTREE, NULL ); 
    
    sprintf(buf,"%s.ems",name);
    plugin_node->empty_msg_store = rdb_register_um_pool ( buf,
                                     1, 0, RDB_KFIFO | RDB_NO_IDX | RDB_BTREE, NULL ); 
    for (i = 0; i < msg_slots; i++) {
        q = calloc (1,sizeof (rdbmsg_queue_t));
        if (q == NULL) {
            log (LOG_ERROR,
                    "Unable to allocate all requested messaages for %s. (req: %d, available %d)\n",
                    plugin_node->name, msg_slots, i);
            break;
        }
        
        if (0 == rdb_insert(plugin_node->empty_msg_store, q)) {
            log (LOG_ERROR,
                    "Failed to insert msg_buffer into data pool for plugin %s. Discarding\n",
                    plugin_node->name);
            free (q);
        }
    }
    
    sprintf(buf,"%s.root",name);
    plugin_node->msg_dispatch_root  = rdb_register_um_pool ( buf,
                        1, 0, RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL ); 
    if (0 == rdb_insert(plugin_pool, plugin_node)) {
        log (LOG_ERROR, 
                "Failed to insert %s to plugin_pool. this plugin will NOT function\n",
                buf);
        if (plugin_node->msg_dispatch_root != NULL) {
            rdb_drop_pool(plugin_node->msg_dispatch_root);    
        }
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
    log (LOG_WARN, "Caught signal %d\n", sig);
    break_requested = 1;
}

static void unlink_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p && p->name) {
        free(p->name);
    }
    if (p && p->pathname) {
        free(p->pathname); 
    }
    if (p) {
        free(p);
    }
    return;
}

static void handle_fatal_error(void) {
    //if we are run as a service / library, this would be a place
    //to report failure back to calling process .. and hang - nothing we
    //can do. 
    //

#ifdef SHARED_ONLY
    // HERE is a good place to call-back and report fatal failure
    while (1) {
        sleep (1);
    }
#else
    //If we are stand alone process. exit (1) is probably appropriate
    exit(1);
#endif
}

#ifdef SHARED_ONLY
int rdbfw_main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    int rc;

    rdb_pool_t *plugin_pool;
    plugins_t *plugin_node = NULL;

    struct timespec time_start,
               time_end,
               delta_time;

    // default handler
    signal(SIGUSR1,sig_func);
    signal(SIGQUIT,sig_func);
    signal(SIGTERM,sig_func);
    signal(SIGINT,sig_func);

    // initilize the rDB library
    rdb_init();

    // register plugins pool
    plugin_pool = rdb_register_um_pool(
        "plugins",
        1,
        0,
        RDB_KPSTR | RDB_KASC | RDB_BTREE,
        NULL);
    
    // Initilize the messaging system. this must follow an rdb_init as it's uses rdb.
    rdbmsg_init(plugin_pool);
    
    // Register plugins
    if (-1 == register_plugin("hw_timers", plugin_pool, plugin_node, 100)) {
        log (LOG_ERROR, "Failed to regusted plugin. Aborting\n");
        handle_fatal_error();
    }

    //register_plugin("timers", plugin_pool, plugin_node);
    if (-1 == register_plugin("event_skeleton", plugin_pool, plugin_node, 500)) {
        log (LOG_ERROR, "Failed to regusted plugin. Aborting\n");
        handle_fatal_error();
    }
    //register_plugin("skeleton", plugin_pool, plugin_node);

    //load plugins
    rdb_iterate(plugin_pool,  0, load_plugin_cb, NULL, NULL, NULL); 
    // init all plugins
    rdb_iterate(plugin_pool,  0, init_plugin_cb, NULL, NULL, NULL); 

    //Hz (delay) for periodic message delivery - or how long may a message be 
    // queued for before wakeing up the client
    //rdbmsg_delay_HZ(10);
    // this is the maximum pending messages that may be queued before a module
    // is woken up to process them - 0 = no wait, nu need for delay HZ above.
    wake_count_limit = 0;

    // start all plugins
    rc = clock_gettime(CLOCK_REALTIME, &time_start);
    rdb_iterate(plugin_pool,  0, start_plugin_cb, NULL, NULL, NULL); 

    // run until all plugins (tests) have stoped
    while(running(plugin_pool)) {

        if (break_requested || automated_test ||
                stopall_requested(plugin_pool)) {
            if (automated_test) {
                 usleep(30000);
            }

            log (LOG_INFO, "SHOTDOWN started\n");
            rdb_iterate(plugin_pool,  0, stop_timers_plugin_cb, NULL, NULL, NULL); 
            rdb_iterate(plugin_pool,  0, stop_plugin_cb, NULL, NULL, NULL); 
            log (LOG_INFO, "Stopall complete.\n");
            rdb_iterate(plugin_pool,  0, display_run_state_cb, NULL, NULL, NULL); 
            break;
        } else {
            //rdb_iterate(plugin_pool,  0, rdb_dump_cb, NULL, NULL, NULL); 
            usleep (100000);
        }
    }

                
    rc = clock_gettime(CLOCK_REALTIME, &time_end);

    if (rc) {
        log (LOG_ERROR, "Error: Failed to get clock\n");
    } else {
        diff_time_ns(&time_start, &time_end, &delta_time);
        log (LOG_ERROR, "uptime: "); 
        print_time(&delta_time);
    }
        
    //rdb_iterate(plugin_pool,  0, print_counters_cb, NULL, NULL, NULL);
    // skipping teardown for demo
    rdb_iterate(plugin_pool,  0, de_init_plugin_cb, NULL, NULL, NULL); 
    rdb_iterate(plugin_pool,  0, drop_plugin_cb, NULL, unlink_plugin_cb, NULL); 
    //plugin_pool->drop = 1;
    rdb_gc();
    //info("###left:\n");
    //rdb_print_pools(stdout); 
    rdb_flush(plugin_pool, unlink_plugin_cb, NULL);    
    rdb_drop_pool(plugin_pool);    
    //plugin_pool->drop = 1;
    //rdb_gc();

    info("left over data pools:\n")   ;
    rdb_print_pools(stdout); 
    //rdb_clean(0);
    exit(0);

}

