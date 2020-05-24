//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

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
static int skeleton_main_thread_started = 0;

//cache needed messaging id's
static int timer_ack_id;
static int timer_id;
static int route_timers;
static int route_skel;
static int group_timers;
//test
static int group_skel;


static void *skel_event(void *p) {
    rdbmsg_msg_t *msg;
    rdbmsg_queue_t *q;
    fwlog (LOG_INFO, "Starting %s\n", ctx->name);
    static int first_entry = 1;

#ifdef USE_PRCTL
    prctl(PR_SET_NAME,"event_skeleton\0",NULL,NULL,NULL);
#endif

    pthread_mutex_unlock(&ctx->startup_mutex);

    //NOTE: if logging is required between this point and there msg_mutex is unlocked (a few lines below).
    //It is required to use fwl_no_emit(), as regular logging will also emit a message, which will try to obtain
    //same message mutex resulting in a deadlock!
    //
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

        pthread_mutex_unlock(&ctx->msg_mutex);

        if (ctx->msg_pending_count > 1) {
            fwl(LOG_WARN, p, "skel_event_woken_up (%d)\n",ctx->msg_pending_count);                  // do some work...
        }

        do {
            rdb_lock(ctx->msg_q_pool,__FUNCTION__);
            q = rdb_delete(ctx->msg_q_pool, 0, NULL);
            rdb_unlock(ctx->msg_q_pool,__FUNCTION__);
            if (q) {

                msg=&(q->msg);
                fwl(LOG_DEBUG, p, "SKEL: from %s to %s grp %s ID %s val %d\n",
                        rdbmsg_lookup_string ( msg->from ),
                        rdbmsg_lookup_string ( msg->to ),
                        rdbmsg_lookup_string ( msg->group ),
                        rdbmsg_lookup_string ( msg->id ),
                        msg->len);
                if (msg->id == timer_ack_id) {
                    // now ask to receive (in addition) messages for assigned timer    
                    timer_id = rdbmsg_lookup_id ("ID_TIMER_TICK_0") + msg->len;
                    if (0 != rdbmsg_request(ctx,
					    rdbmsg_lookup_id("ROUTE_MDL_TIMERS"),
					    route_skel,
					    rdbmsg_lookup_id ("GROUP_TIMERS"),
                        timer_id)) {
                        fwlog (LOG_ERROR, "rdbmsg_request failed. events may not fire. Aborting");
                        ctx->state = RDBFW_STATE_SOFTSTOPALL;
                    }
                }
                else if (msg->id == timer_id) {
                    fwl (LOG_INFO, p, "got timer id %d\n",msg->len);
                }
                else {
                    fwl (LOG_INFO, p, "UNEXPECTED MESSAGE: %s\n",rdbmsg_lookup_string ( msg->id ) );
                }
                
                rdbmsg_implode(ctx, q);
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
    
    fwlog (LOG_INFO, "Destroy %s\n", ctx->name);
    ctx->state = RDBFW_STATE_LOADED;

}

static void skel_event_pre_init(void *p) {
    route_skel = rdbmsg_register_msg_type ( "route", "ROUTE_MDL_EVENT_SKEL" );
    group_skel = rdbmsg_register_msg_type ( "group", "GROUP_SKEL" );
    if ( route_skel < 0 || group_skel < 0 ) {
        fwl ( LOG_FATAL, p, "Unable to register route, or group message: %d:%d",
           route_skel, group_skel );
        ((plugins_t*) p)->state = RDBFW_STATE_STOPALL;
    }
    fwl (LOG_INFO, p, "route_skel = %d\n", route_skel); 
}

static void skel_event_init(void *p) {
    ctx = p;
    
    fwlog (LOG_INFO, "Initilizing %s\n", ctx->name);

    timer_ack_id = rdbmsg_lookup_id ("ID_TIMER_ACK");
    route_timers = rdbmsg_lookup_id ("ROUTE_MDL_TIMERS");
    group_timers = rdbmsg_lookup_id ("GROUP_TIMERS");

    pthread_mutex_init(&ctx->msg_mutex, NULL);
    pthread_cond_init(&ctx->msg_condition, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
 
    // ask to receive messages of type... Timers Ack.    
    if (0 != rdbmsg_request(p, route_timers, route_skel, rdbmsg_lookup_id ("GROUP_TIMERS"), 
                rdbmsg_lookup_id ("ID_TIMER_ACK") ) ) {
        fwlog (LOG_ERROR, "rdbmsg_request failed. events may not fire. Aborting");
        ctx->state = RDBFW_STATE_STOPALL;
        return;
    }
    
    ctx->state = RDBFW_STATE_INITIALIZED;

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
                fwlog (LOG_ERROR, "Thread creation failed, MAX_THREAD_RETRY exusted\n");
                ctx->state = RDBFW_STATE_STOPALL;
                return;
            } 
            else {
                cnt++;
                fwlog (LOG_ERROR, "Thread creation failed, will retry\n");
                usleep (100000);
                continue;
            }
        }
        else if (rc == EPERM) {
            fwlog (LOG_ERROR, "Thread creation failed - missing permissions - aborting\n");
            ctx->state = RDBFW_STATE_STOPALL;
            return;
        }
        else if (rc == EINVAL) {
            fwlog (LOG_ERROR, "Thread creation failed - Invalid attribute - aborting\n");
            ctx->state = RDBFW_STATE_STOPALL;
            return;
        }
    }
    skeleton_main_thread_started = 1;
   
    pthread_mutex_lock(&ctx->startup_mutex);
    ctx->state = RDBFW_STATE_RUNNING;
    pthread_mutex_unlock(&ctx->startup_mutex);

    rdbmsg_emit_simple(route_skel,
                rdbmsg_lookup_id ("ROUTE_MDL_TIMERS"),
                rdbmsg_lookup_id ("GROUP_TIMERS"),
                rdbmsg_lookup_id ("ID_TIMER_START"),
                2000 );
}

static void skel_event_stop(void *pp) {
    plugins_t *p = pp;
    break_requested = 1;

    fwl (LOG_WARN, p, "Stopping %s\n",p->uname);
    
    // even though we set break_requested to one we also need to
    // make sure it's awake after that moment, to it can be processed.
    // the join will ensure we dont quit until out internal threads did.
    if ( skeleton_main_thread_started ) {
        pthread_mutex_lock(&ctx->msg_mutex);
        pthread_cond_signal(&ctx->msg_condition);
        pthread_mutex_unlock(&ctx->msg_mutex);
        pthread_join(skel_event_thread, NULL);
    }

    ctx->state = RDBFW_STATE_STOPPED;
}

const rdbfw_plugin_api_t event_skeleton_rdbfw_fns = {
    .pre_init = skel_event_pre_init,
    .init = skel_event_init,
    .start = skel_event_start,
    .stop = skel_event_stop,
    .de_init = skel_event_destroy,
};
