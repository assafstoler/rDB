//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

#include <stdio.h>  //printf,
#include <stdlib.h> //exit,
#include <unistd.h> // getopt
#include <pthread.h>
#include <getopt.h>
#include "log.h"
#include "fwalloc.c"
#ifdef USE_PRCTL
#include <sys/prctl.h>
#endif


// Make sure not to collide with framework opts (m,o,v,u,t and h)
// sure 
void rdbfw_app_help(void) {
    printf( "-p <text>\t: Print text to stdout \n"
            "-i <text>\t: Print text as INFO level log entry (use -l 4 or above to see)\n"
            "-e <text>\t: Print text as ERROR level log entry (use -l 2 or above to see\n"
                );
    return;
}

int rdbfw_app_process_opts (int argc, char **argv) {
    int c;

    while ((c = getopt (argc, argv, "-p:i:e:")) != -1)
        switch (c) {
            case 'p':
                printf("%s\n", optarg);
                break;
            case 'i':
                fwl (LOG_INFO, NULL, "%s\n", optarg);
                break;
            case 'e':
                fwl (LOG_ERROR, NULL, "%s\n", optarg);
                break;
            default:
                break;

        }
    return 0;
}

// Allow for global (rdbfw) memory pre-allocation
int rdbfw_app_prealloc(void) {
    return rdbfw_alloc_prealloc ( 10000, 10, 100, 0);
}


int rdbfw_app_register_plugins( rdb_pool_t *plugin_pool/*, int argc, char **argv */) {

    // Register plugins

    if ( unittest_en == UT_REG_PLUGIN_1 ||
            -1 == register_plugin("timers", plugin_pool, 1000, CTX_SINGULAR) ) {
        fwl (LOG_ERROR, NULL, "Failed to register plugin. Aborting\n");
        return -1;
    }
    
    if ( unittest_en == UT_REG_PLUGIN_1 ||
            -1 == register_plugin("event_skeleton", plugin_pool, 1000, CTX_SINGULAR) ) {
        fwl (LOG_ERROR, NULL, "Failed to register plugin. Aborting\n");
        return -1;
    }
    
    if ( unittest_en == UT_REG_PLUGIN_2 &&
            -1 == register_plugin("mdl_tester", plugin_pool, 10, CTX_SINGULAR) ) {
        fwl (LOG_ERROR, NULL, "Failed to register plugin. Aborting\n");
        return -1;
    }
    fwl (LOG_DEBUG, NULL, "All Plugins registered\n");

    return 0;

}

void rdbfw_app_config_timers( void ) {
    //Hz (delay) for periodic message delivery - or how long may a message be 
    // queued for before wakeing up the client.
    // Default: no wait
    //rdbmsg_delay_HZ(10);
    //  
    // This is the maximum pending messages that may be queued before a module
    // is woken up to process them - 0 = no wait, (So, delay HZ above ignored).
    wake_count_limit = 0; //messaging.h
}
    


// This is the a minimal framework invocation example.
// will run and wait forever, unless ru with -t # to set test timeout.
// Alternatively, replace rdbfw_wait() with rdbfw_stop() ...
// rdbfw_is_running can alwaye be inspected.
//
// Most rdbfw apps will not need anything more in main, however, things that with to
// interact with the framework externally (like unit-test) may find this a convenient
// to use main to run external code
int main(int argc, char *argv[]) {

#ifdef USE_PRCTL
    prctl(PR_SET_NAME,"fw_test-main\0",NULL,NULL,NULL);
#endif
    if (0 != rdbfw_main (argc, argv)) {
        printf("Fatal: Error loading framework - Abort\n");
        exit(1);
    }
    printf("is running %d\n", rdbfw_is_running());
    rdbfw_wait();
 
    fwl_no_emit(LOG_INFO, NULL,"RErunning\n");

    if (0 != rdbfw_main (argc, argv)) {
        printf("Fatal: Error loading framework - Abort\n");
        exit(1);
    }
    printf("is running %d\n", rdbfw_is_running());
    rdbfw_wait();

    printf("is running %d\n", rdbfw_is_running());

    exit(0);
    
}


