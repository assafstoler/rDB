#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>

#include "rDB.h"
#include "messaging.h"
#include "rdbfw.h"
#include "log.h"

extern pthread_mutex_t timed_mutex;
extern pthread_cond_t timed_condition;

extern rdb_pool_t *empty_msg_store;

static pthread_t skeleton_main_thread;
static pthread_attr_t attr;
    
static plugins_t *ctx;
static int break_requested = 0;

static void *skeleton_main(void *p) {

    log (LOG_INFO, "Starting %s\n", ctx->name);

    pthread_mutex_unlock(&ctx->startup_mutex);

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

static void skeleton_destroy(void *p) {
    ctx = p;
    
    log (LOG_INFO, "Destroy %s\n", ctx->name);
    ctx->state = RDBFW_STATE_LOADED;

}

static void skeleton_init(void *p) {
    ctx = p;
    
    log (LOG_INFO, "Initilizing %s\n", ctx->name);

    pthread_mutex_init(&ctx->msg_mutex, NULL);
    pthread_cond_init(&ctx->msg_condition, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ctx->state = RDBFW_STATE_INITILIZED;

}

static void skeleton_start(void *p) {
    int rc;
    int cnt = 0;

    pthread_mutex_lock(&ctx->startup_mutex);
    while (1) {
        rc = pthread_create( &skeleton_main_thread, &attr, skeleton_main, NULL);
        if (rc == 0) {
            break;
        }
        if (rc == EAGAIN) {
            if (cnt > MAX_THREAD_RETRY) {
                log (LOG_ERROR, "Thread creation failed, MAX_THREAD_RETRY exusted\n");
                ctx->state = RDBFW_STATE_STOPALL;
                return;
            } 
            else {
                cnt++;
                log (LOG_ERROR, "Thread creation failed, will retry\n");
                usleep (100000);
                continue;
            }
        }
        else if (rc == EPERM) {
            log (LOG_ERROR, "Thread creation failed - missing permissions - aborting\n");
            ctx->state = RDBFW_STATE_STOPALL;
            return;
        }
        else if (rc == EINVAL) {
            log (LOG_ERROR, "Thread creation failed - Invalid attribute - aborting\n");
            ctx->state = RDBFW_STATE_STOPALL;
            return;
        }
    }
    
    pthread_mutex_lock(&ctx->startup_mutex);
    ctx->state = RDBFW_STATE_RUNNING;
    pthread_mutex_unlock(&ctx->startup_mutex);
}

static void skeleton_stop(void *p) {
    break_requested = 1;
    
    // even though we set break_requested to one we also need to
    // make sure it's awake after that moment, to it can be processed.
    // the join will ensure we dont quit until out internal threads did.
    pthread_mutex_lock(&ctx->msg_mutex);
    pthread_cond_signal(&ctx->msg_condition);
    pthread_mutex_unlock(&ctx->msg_mutex);
    pthread_join(skeleton_main_thread, NULL);

    ctx->state = RDBFW_STATE_STOPPED;
}

const rdbfw_plugin_api_t skeleton_rdbfw_fns = {
    .init = skeleton_init,
    .start = skeleton_start,
    .stop = skeleton_stop,
    .de_init = skeleton_destroy,
};
