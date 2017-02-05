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
#include "rdbfw.h"
#include "utils.h"


void help_and_exit(void)
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
int load_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    p->handle = dlopen (p->pathname , RTLD_LAZY| RTLD_GLOBAL);

    if (p->handle == NULL) {
        printf("failed to load plugin %s\n", p->pathname);
        fputs (dlerror(), stderr);
        printf("\n");
        goto load_plugin_err;
    } 
    
    p->plugin_info = (rdbfw_plugin_api_t *) dlsym(p->handle, "rdbfw_fns");
    if ((p->error = dlerror()) != NULL)  {
        fputs(p->error, stderr);
        printf("\n");
        goto load_plugin_err;
    }
    p->state = RDBFW_STATE_LOADED;
    printf("LOADED: %s\n", p->name);

    return RDB_CB_OK;

load_plugin_err:

    // we could use abort here, but I think we should load all the moduels we can
    return RDB_CB_OK;

}

int init_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_LOADED) {
        (*p->plugin_info).init(p);
        //p->state = RDBFW_STATE_INITILIZED;
        printf("INIT: %s (%p)\n", p->name, p);
        return RDB_CB_OK;
    }

    return RDB_CB_OK;
}

int start_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_INITILIZED) {
        if ((*p->plugin_info).start)  (*p->plugin_info).start(p);
        printf("started %s\n", p->name);
        p->state = RDBFW_STATE_RUNNING;
        return RDB_CB_OK;
    }

    return RDB_CB_OK;
}

int stop_plugin_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    if (p->state == RDBFW_STATE_RUNNING) {
        if ((*p->plugin_info).stop)  (*p->plugin_info).stop(p);
        printf("stopping %s\n", p->name);
        p->state = RDBFW_STATE_STOPPING;
        pthread_mutex_lock(&p->msg_mutex);
        pthread_cond_signal(&p->msg_condition);
        pthread_mutex_unlock(&p->msg_mutex);
        return RDB_CB_OK;
    }

    return RDB_CB_OK;
}

int any_running_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    int *ar;
    ar=user_ptr;

    if ((p->state == RDBFW_STATE_RUNNING) ||
        (p->state == RDBFW_STATE_STOPPING)) {
        *ar=1;
        //printf("%s running\n", p->name);
    }
    return RDB_CB_OK;
}

int print_counters_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;
    //int *ar;
    //ar=user_ptr;

    //if ((p->state == RDBFW_STATE_RUNNING) ||
    //    (p->state == RDBFW_STATE_STOPPING)) {
    //    *ar=1;
        printf("%s totals\n", p->name);
        printf("rx_count=%" PRIu64 " - " ,p->msg_rx_count);
        printf("wake_count=%" PRIu64 "\n" ,p->wakeup_count);
    //}
    return RDB_CB_OK;
}

/*int dump_msg_req_cb_cb (void *data, void *user_ptr) {
    printf(".\n");
    return RDB_CB_OK;
}
int dump_msg_req_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    rdb_iterate(p->msg_control.from,  0, dump_msg_req_cb_cb, NULL, NULL, NULL); 

    return RDB_CB_OK;
}*/

int dump_cb (void *data, void *user_ptr) {
    rdbmsg_queue_t *p;
    p = (rdbmsg_queue_t *) data;

        if (p) printf("%d dump\n", p->msg.legacy);
        else printf("ND\n");
    return RDB_CB_OK;
}

int rdb_dump_cb (void *data, void *user_ptr) {
    plugins_t *p;
    p = (plugins_t *) data;

    printf("%p, %s, %d\n",p, p->name, p->state);
    return RDB_CB_OK;
}

// how much may we append to the given name. out longest is '.group', so 6
#define PLUGIN_NAME_SUFFIX_MAX 6 
#define PLUGIN_LIB_PREFIX "./lib"
#define PLUGIN_LIB_PREFIX_LEN (sizeof(PLUGIN_LIB_PREFIX))
#define PLUGIN_LIB_SUFFIX ".so"
#define PLUGIN_LIB_SUFFIX_LEN (sizeof(PLUGIN_LIB_SUFFIX))

int register_plugin(char *name, rdb_pool_t *plugin_pool, plugins_t *plugin_node) {
    char *buf;
    int i;
    rdbmsg_queue_t *q;

    if (name == NULL) goto register_plugin_err;
    
    buf = malloc(strlen(name) + PLUGIN_NAME_SUFFIX_MAX + 1) ;
    if (buf == NULL) goto register_plugin_err;

    plugin_node = calloc (1,sizeof (plugins_t));
    if (plugin_node == NULL) goto register_plugin_err;

    plugin_node->name = malloc(strlen(name) + 1) ;
    if (plugin_node->name == NULL) {
        free (plugin_node);
        goto register_plugin_err;
    }
    strcpy (plugin_node->name, name);

    plugin_node->pathname = malloc(PLUGIN_LIB_PREFIX_LEN + strlen(name) + PLUGIN_LIB_SUFFIX_LEN + 1) ;
    if (plugin_node->pathname == NULL) {
        free (plugin_node->name);
        free (plugin_node);
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
    for (i = 0; i < 50000; i++) {
        q = calloc (1,sizeof (rdbmsg_queue_t));
        if (q == NULL) exit(1); //goto some_err;
        
        if (0 == rdb_insert(plugin_node->empty_msg_store, q)) {
            printf("Can't allocate message buffers\n");
            exit(1);
        }
    }
    
    sprintf(buf,"%s.root",name);
    plugin_node->msg_dispatch_root  = rdb_register_um_pool ( buf,
                        1, 0, RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL ); 
    rdb_insert(plugin_pool, plugin_node);

    return 0;

register_plugin_err:

    return 1;
}

int main( int argc, char *argv[] )
{

    int rc;
    int any_running;

    rdb_pool_t *plugin_pool;
    plugins_t *plugin_node = NULL;

    struct timespec time_start,
               time_end,
               delta_time;

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
    register_plugin("hw_timers", plugin_pool, plugin_node);
    register_plugin("event_skeleton", plugin_pool, plugin_node);
    //register_plugin("skeleton", plugin_pool, plugin_node);

    //load plugins
    rdb_iterate(plugin_pool,  0, load_plugin_cb, NULL, NULL, NULL); 
    // init all plugins
    rdb_iterate(plugin_pool,  0, init_plugin_cb, NULL, NULL, NULL); 

    //Hz (delay) for periodic message delivery - or how long may a message be 
    // queued for before wakeing up the client
    rdbmsg_delay_HZ(10000000);
    // this is the maximum pending messages that may be queued before a module
    // is woken up to process them
    wake_count_limit = 1;

    //dump req tree - need to reqrite for complex dispatch tree
    //rdb_iterate(plugin_pool,  0, dump_msg_req_cb, NULL, NULL, NULL); 

    // start all plugins
    rc = clock_gettime(CLOCK_REALTIME, &time_start);
    rdb_iterate(plugin_pool,  0, start_plugin_cb, NULL, NULL, NULL); 

    // run until all plugins (tests) have stoped
    while(1) {
        any_running=0;
        rdb_iterate(plugin_pool,  0, any_running_cb, (void *) &any_running, NULL, NULL); 
        //printf("%d\n", any_running);

        if (!any_running) break;
        else { 
            rc = clock_gettime(CLOCK_REALTIME, &time_end);
            diff_time_ns(&time_start, &time_end, &delta_time);
            if (delta_time.tv_sec >= 5) {
            usleep (10000);
                rdb_iterate(plugin_pool,  0, stop_plugin_cb, NULL, NULL, NULL); 
            }
            //rdb_iterate(plugin_pool,  0, rdb_dump_cb, NULL, NULL, NULL); 
            usleep (10000);
        }

    }
    
    /*do {
        any_running=0;
        rdb_iterate(plugin_pool,  0, any_running_cb, (void *) &any_running, NULL, NULL); 
    } while (any_running);*/
    rc = clock_gettime(CLOCK_REALTIME, &time_end);

    if (rc) {
        printf("Error: Failed to get clock\n");
    } else {
        diff_time_ns(&time_start, &time_end, &delta_time);
        print_time(&delta_time);
    }
        
    rdb_iterate(plugin_pool,  0, print_counters_cb, NULL, NULL, NULL); 
    exit(0);
}

