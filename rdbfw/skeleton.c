#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>

#include "rDB.h"
#include "messaging.h"
#include "rdbfw.h"

static int rc;

extern pthread_mutex_t timed_mutex;
extern pthread_cond_t timed_condition;

extern rdb_pool_t *empty_msg_store;

static pthread_t skeleton_main_thread;
static pthread_attr_t attr;
    
static plugins_t *ctx;
static int break_requested = 0;
static int xxx=0;
void *skeleton_main(void *p) {
    rdbmsg_queue_t *q;

    printf("Starting %s\n", ctx->name);
    ctx->state = RDBFW_STATE_RUNNING;

    //pthread_mutex_lock(&ctx->msg_mutex);

    while (break_requested == 0) {
	// Here we do some work ... this will be an infinite loop
	// taking 100% of a CPU core ... or trying to
	// by emitting message after message

        sleep(1);

        rdbmsg_emit_simple(RDBMSG_ROUTE_NA, 
                RDBMSG_ROUTE_NA,
                RDBMSG_GROUP_TIMERS,
                RDBMSG_ID_TIMER_TICK_1,
                0);

    }

    ctx->state = RDBFW_STATE_STOPPED;
    pthread_exit(NULL);

}

void skeleton_init(void *p) {
    ctx = p;
    
    printf("Initilizing %s\n", ctx->name);

    pthread_mutex_init(&ctx->msg_mutex, NULL);
    pthread_cond_init(&ctx->msg_condition, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ctx->state = RDBFW_STATE_INITILIZED;

}

void skeleton_start(void *p) {
    rc = pthread_create( &skeleton_main_thread, &attr, skeleton_main, NULL);
}

void skeleton_stop(void *p) {
    break_requested = 1;
}

const rdbfw_plugin_api_t rdbfw_fns = {
    .init = skeleton_init,
    .start = skeleton_start,
    .stop = skeleton_stop,
};
