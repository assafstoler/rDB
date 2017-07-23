#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <dlfcn.h>

#include "rDB.h"
#include "messaging.h"
#include "rdbfw.h"
#include "utils.h"
#include "log.h"
#ifdef USE_PRCTL
#include <sys/prctl.h>
#endif
    
static pthread_t skel_event_thread;
static pthread_attr_t attr;
    
static plugins_t *ctx;
static int break_requested = 0;

struct timespec ts[3];

static void *skel_event(void *p) {
    rdbmsg_msg_t *msg;
    rdbmsg_queue_t *q;
    log (LOG_INFO, "Starting %s\n", ctx->name);
    static int first_entry = 1;

#ifdef USE_PRCTL
    prctl(PR_SET_NAME,"event_skeleton\0",NULL,NULL,NULL);
#endif

    pthread_mutex_unlock(&ctx->startup_mutex);

    pthread_mutex_lock(&ctx->msg_mutex);
    while (break_requested == 0) {
        if (first_entry) {
            // first time we want to skip the condition as there may already be something in the Q, 
            // for which we ay have already missed the wakeup signal
            // It's also the right place to register out timer messages since there was no point
            // to do until we are in the event loop
            first_entry = 0;
        } else {
            pthread_cond_wait(&ctx->msg_condition, &ctx->msg_mutex);
#ifdef WAKEUP_ACCOUNTING
            ctx->wakeup_count++;
#endif
        }

        if (ctx->msg_pending_count > 1) {
            printf("skel_event_woken_up (%d)\n",ctx->msg_pending_count);                  // do some work...
        }

        pthread_mutex_unlock(&ctx->msg_mutex);
        do {
            rdb_lock(ctx->msg_q_pool,__FUNCTION__);
            q = rdb_delete(ctx->msg_q_pool, 0, NULL);
            rdb_unlock(ctx->msg_q_pool,__FUNCTION__);
            if (q) {

                msg=&(q->msg);
                //info("SKEL: from %d to %d grp %d ID %d val %d\n",msg->from, msg->to, msg->group, msg->id, msg->len);
                if (msg->id ==  RDBMSG_ID_TIMER_ACK) {
                    // now ask to receive (in addition) messages for assigned timer    
                    if (0 != rdbmsg_request(ctx, RDBMSG_ROUTE_NA, RDBMSG_ROUTE_NA, RDBMSG_GROUP_TIMERS, RDBMSG_ID_TIMER_TICK_0 + msg->len)) {
                        log (LOG_ERROR, "rdbmsg_request failed. events may not fire. Aborting");
                        ctx->state = RDBFW_STATE_SOFTSTOPALL;
                    }
                    else {
                        log (LOG_INFO, "got timer id %d\n",msg->len);
                    }
                }
                
                rdbmsg_free(ctx, q);                
                //info("free\n");
            }
            //if (q == NULL) printf("got empty message - sperious\n");
            //if (q) printf("Received message grp.id = %d.%d\n",q->msg.group, q->msg.id);
        } while (q);
        pthread_mutex_lock(&ctx->msg_mutex);

    }
    pthread_mutex_unlock(&ctx->msg_mutex);

    pthread_exit(NULL);

}

static void skel_event_destroy(void *p) {
    ctx = p;
    
    log (LOG_INFO, "Destroy %s\n", ctx->name);
    ctx->state = RDBFW_STATE_LOADED;

}

static void skel_event_init(void *p) {
    ctx = p;
    
    log (LOG_INFO, "Initilizing %s\n", ctx->name);

    pthread_mutex_init(&ctx->msg_mutex, NULL);
    pthread_cond_init(&ctx->msg_condition, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
 
    // ask to receive messages of type... Timers Ack.    
    if (0 != rdbmsg_request(p, RDBMSG_ROUTE_NA, RDBMSG_ROUTE_MDL_EVENT_SKEL, RDBMSG_GROUP_TIMERS, RDBMSG_ID_TIMER_ACK)) {
        log (LOG_ERROR, "rdbmsg_request failed. events may not fire. Aborting");
        ctx->state = RDBFW_STATE_STOPALL;
        return;
    }
    
    ctx->state = RDBFW_STATE_INITILIZED;

}

static void skel_event_start(void *p) {
    int rc;
    int cnt = 0;

    pthread_mutex_lock(&ctx->startup_mutex);
    while (1) {
        rc = pthread_create( &skel_event_thread, &attr, skel_event, NULL);
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

    rdbmsg_emit_simple(RDBMSG_ROUTE_MDL_EVENT_SKEL,
                RDBMSG_ROUTE_MDL_TIMERS,
                RDBMSG_GROUP_TIMERS,
                RDBMSG_ID_TIMER_START,
                100); //Hz
    /*rdbmsg_emit_simple(RDBMSG_ROUTE_MDL_EVENT_SKEL,
                RDBMSG_ROUTE_MDL_TIMERS,
                RDBMSG_GROUP_TIMERS,
                RDBMSG_ID_TIMER_START,
                10 ); */
}

static void skel_event_stop(void *p) {
    break_requested = 1;

    // even though we set break_requested to one we also need to
    // make sure it's awake after that moment, to it can be processed.
    // the join will ensure we dont quit until out internal threads did.
    pthread_mutex_lock(&ctx->msg_mutex);
    pthread_cond_signal(&ctx->msg_condition);
    pthread_mutex_unlock(&ctx->msg_mutex);
    pthread_join(skel_event_thread, NULL);

    ctx->state = RDBFW_STATE_STOPPED;
}

const rdbfw_plugin_api_t event_skeleton_rdbfw_fns = {
    .init = skel_event_init,
    .start = skel_event_start,
    .stop = skel_event_stop,
    .de_init = skel_event_destroy,
};
