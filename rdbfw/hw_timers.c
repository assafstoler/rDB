//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

/*
 * The Timers module:
 *
 * This module implement a serious of timers using the system's HW timer 
 * option for efficient time-keeping.
 *
 * Timers are requested via the messaging system rdbmsg. available are
 * RDBMSG_ID_TIMER_START (data is frequency (1/HZ)) 
 * RDBMSG_ID_TIMER_STOP (data is which timer - index)
 * RDBMSG_ID_TIMER_ACK (data is which timer index will be used with 'last' request.
 * - since messages are always delivered in order, it is a valid usa case for a module to
 *   ask for several timers, and only then proccess the replies. which will corolate in order
 *   to the requests.
 * - ack messages are sent with the 'to' field filled with the 'from value used during the 
 *   TIMER_START request. modules requesting timers should listen to TIMER_ACK message with the "to"
 *   field populated with their own identefication to avoid picking up ack's intended for other modules.
 *
 * RDBMSG_ID_TIMER_TICK_0
 * RDBMSG_ID_TIMER_TICK_2
 * ...
 * RDBMSG_ID_TIMER_TICK_15
 *
 * timers may be private or public.
 * */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#ifdef USE_PRCTL
#include <sys/prctl.h>
#endif

#include "rDB.h"
#include "messaging.h"
#include "rdbfw.h"
#include "log.h"


#define MAX_TIMERS 16

static pthread_mutex_t *mutex_ptr;

typedef struct t_info_s {
    uint32_t hz;
    int id;
    int counter;
    timer_t hw_id;
} t_info_t;

static t_info_t t_info[MAX_TIMERS];

static pthread_t timers_main_thread;
static pthread_attr_t attr, *attrp;
    
static plugins_t *ctx;
static int break_requested = 0;

static void *timer_thread(union sigval *p);

static int route_na;
static int timer_route;
static int timer_group;
static int timer_start_id;
static int timer_stop_id;
static int timer_ack_id;
static int timer_tick[MAX_TIMERS];


static int find_free_timer() {
    int i;

    pthread_mutex_lock(&ctx->msg_mutex);
    for (i = 0; i < MAX_TIMERS; i++) {
        if (t_info[i].hz == 0) {
            pthread_mutex_unlock(&ctx->msg_mutex);
            return i;
        }
    }

    pthread_mutex_unlock(&ctx->msg_mutex);
    return -1;
}

static void timers_stop_all(void){
    int i;

    pthread_mutex_lock(&ctx->msg_mutex);
    for (i = 0; i < MAX_TIMERS; i++) {
        if (t_info[i].hz != 0) {
            fwlog (LOG_INFO, "Stoping timer thread %d\n", i);
            timer_delete(t_info[i].hw_id);
            t_info[i].hz=0;
            // Unlike timers.c here we do not memset as timer threads may still be called.
            // which is Ok since we memset at (each) startup anyhow
        }
    }
    // unlocking here after (hopefully) all pending timers are gone
    pthread_mutex_unlock(&ctx->msg_mutex);
            
    fwlog (LOG_DEBUG, "Done\n");
}

static void *timers_main(void *p) {
    rdbmsg_queue_t *q;

    int timer_id;
    int rc;
    static int first_entry = 1;

#ifdef USE_PRCTL
    prctl(PR_SET_NAME,"hw_timers_main\0",NULL,NULL,NULL);
#endif

    pthread_mutex_unlock(&ctx->startup_mutex);

    //NOTE: if logging is required between this point and there msg_mutex is unlocked (a few lines below).
    //It is required to use fwl_no_emit(), as regular logging will also emit a message, which will try to obtain
    //same message mutex resulting in a deadlock!
    //
    pthread_mutex_lock(&ctx->msg_mutex);
    while (break_requested == 0) {
        // first time we want to skip the condition as there may already be something in the Q, 
        // for which we ay have already missed the wakeup signal
        if (first_entry) {
            first_entry = 0;
        } else {
            pthread_cond_wait(&ctx->msg_condition, &ctx->msg_mutex);
#ifdef WAKEUP_ACCOUNTING
            ctx->wakeup_count++;
#endif
        }

        // in case we woke up to a stop single, we want to emit no more. break out now.
        if (break_requested) break;

        pthread_mutex_unlock(&ctx->msg_mutex);
        do {
            fwlog (LOG_DEBUG_MORE, "%s: Timed Work\n", ctx->name);                  // do some work...
            rdb_lock(ctx->msg_q_pool,__FUNCTION__);
            q = rdb_delete(ctx->msg_q_pool, 0, NULL);
            rdb_unlock(ctx->msg_q_pool,__FUNCTION__);

            if (q) {    // process message
                fwlog (LOG_DEBUG_MORE, "%s: Received message grp.id = %d.%d\n",
                        ctx->name, q->msg.group, q->msg.id);
                if (q->msg.id == timer_stop_id) {
                    timer_id = q->msg.len - timer_tick[0];
                    timer_delete(t_info[timer_id].hw_id);
                    t_info[timer_id].hz=0;
                }
                if (q->msg.id == timer_start_id) {
                    timer_id = find_free_timer();
                    // we reply to the request before starting the timer, 
                    // so receiver has a change to set up listener before ticks accomulate
                    rdbmsg_emit_simple(timer_route, 
                            q->msg.from,    // replying directly to the requester
                            timer_group,
                            timer_ack_id,
                            timer_id);
                    if (timer_id != -1) {   // we got a timer
                        struct sigevent se; 
                        struct itimerspec ts;

                        t_info[timer_id].hz = q->msg.len;
                        t_info[timer_id].id = timer_id + timer_tick[0];

                        se.sigev_notify = SIGEV_THREAD;
                        se.sigev_value.sival_ptr = &t_info[timer_id].id;
                        se.sigev_notify_function = (void *) timer_thread;
                        se.sigev_notify_attributes = NULL;

                        if (t_info[timer_id].hz == 1) {
                            ts.it_value.tv_sec = 1;
                            ts.it_value.tv_nsec = 0;
                            ts.it_interval.tv_sec = 1;
                            ts.it_interval.tv_nsec = 0;
                        }
                        else {
                            ts.it_value.tv_sec = 0;
                            ts.it_value.tv_nsec = 1000000000 / t_info[timer_id].hz;
                            ts.it_interval.tv_sec = 0;
                            ts.it_interval.tv_nsec = 1000000000 / t_info[timer_id].hz;
                        }

                        rc = timer_create(CLOCK_REALTIME, &se,(timer_t *)  &(t_info[timer_id].hw_id));
                        if (rc != 0) { 
                            fwlog (LOG_ERROR, "FATAL: timer_create failed (%d: %s) - Aborting\n", rc, strerror(errno));
                            ctx->state = RDBFW_STATE_SOFTSTOPALL;
                        } 
                        else {
                            rc = timer_settime( t_info[timer_id].hw_id, 0, &ts, NULL);
                            if (rc != -0) { 
                                fwlog (LOG_ERROR, "FATAL: timer_settime failed (%d: %s) - Aborting\n", rc, strerror(errno));
                                ctx->state = RDBFW_STATE_SOFTSTOPALL;
                            }
                        }
                    }
                }
                rdbmsg_implode(ctx, q);
            } else {
                break;
            }
        } while (1);
        pthread_mutex_lock(&ctx->msg_mutex);

    }
    pthread_mutex_unlock(&ctx->msg_mutex);

    pthread_exit(NULL);
}

static void timers_pre_init(void *p) {
    char *str;
    int i;
    str = malloc(64);

    timer_route = rdbmsg_lookup_id("ROUTE_MDL_TIMERS"); 
    if (timer_route <= 0) {
        timer_route = rdbmsg_register_msg_type ( "route", "ROUTE_MDL_TIMERS" );
    }
    timer_group = rdbmsg_lookup_id("GROUP_TIMERS"); 
    if (timer_group <= 0) {
        timer_group = rdbmsg_register_msg_type ( "group", "GROUP_TIMERS" );
    }
    timer_start_id = rdbmsg_lookup_id("ID_TIMER_START"); 
    if (timer_start_id <= 0) {
        timer_start_id = rdbmsg_register_msg_type ( "id", "ID_TIMER_START" );
    }
    timer_stop_id = rdbmsg_lookup_id("ID_TIMER_STOP"); 
    if (timer_stop_id <= 0) {
        timer_stop_id = rdbmsg_register_msg_type ( "id", "ID_TIMER_STOP" );
    }
    timer_ack_id = rdbmsg_lookup_id("ID_TIMER_ACK"); 
    if (timer_ack_id <= 0) {
        timer_ack_id = rdbmsg_register_msg_type ( "id", "ID_TIMER_ACK" );
    }

    for (i = 0; i < MAX_TIMERS; i++) {
        sprintf ( str, "ID_TIMER_TICK_%d",i);
	int existing = rdbmsg_lookup_id (str);
	fwl (LOG_DEBUG, p, "Existing timer %i, %s.\n", existing, str );
	if (existing > 0) {
            timer_tick[i] = existing;
	} else {
            timer_tick[i] = rdbmsg_register_msg_type ( "id", str );
	}
        if ( timer_tick[i] < 0 ) {
            fwl (LOG_ERROR, p, "Unable to register message %s\n", str);
            exit (1);
        }
    }
    free (str);

    if ( timer_route < 0 || timer_group < 0 || timer_start_id < 0 ||
            timer_stop_id < 0 || timer_ack_id < 0 ) {
        fwl (LOG_ERROR, p, "Unable to register message(s)\n");
        exit (1);
    }
    else {
        fwl (LOG_INFO, p, "register message(s) successfully\n");
    }

}


static void timers_init(void *p) {
    ctx = p;
    mutex_ptr = &ctx->msg_mutex;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ctx->state = RDBFW_STATE_INITIALIZED;
    attrp=&attr;

    route_na = rdbmsg_lookup_id ("ROUTE_NA");

    if (0 != rdbmsg_request(p, route_na, timer_route,
                rdbmsg_lookup_id ("GROUP_NA"), timer_start_id)) {
        fwlog (LOG_ERROR, "rdbmsg_request failed. events may not fire. Aborting (%d.%d.%d.%d)",
                route_na, timer_route, 
                rdbmsg_lookup_id ("GROUP_NA"), timer_start_id);
        ctx->state = RDBFW_STATE_STOPALL;
        return;
    }
    if (0 != rdbmsg_request(p, route_na, timer_route,
                rdbmsg_lookup_id ("GROUP_NA"), timer_stop_id)) {
        fwlog (LOG_ERROR, "rdbmsg_request failed. events may not fire. Aborting (%d.%d.%d.%d)",
                route_na, timer_route, 
                rdbmsg_lookup_id ("GROUP_NA"), timer_stop_id);
        ctx->state = RDBFW_STATE_STOPALL;
        return;
    }
}

static void timers_de_init(void *p) {
    ctx = p;
    int rc;
    
    fwlog (LOG_INFO, "Destroy %s\n", ctx->uname);
    ctx->state = RDBFW_STATE_LOADED;
    
    rc = pthread_attr_destroy(attrp);

    fwlog (LOG_DEBUG, "rc = %d\n", rc);


}

static void timers_stop(void *P) {
    break_requested = 1;

    // even though we set break_requested to one we also need to
    // make sure it's awake after that moment, to it can be processed.
    // the join will ensure we dont quit until out internal threads did.

    pthread_mutex_lock(&ctx->msg_mutex);
    pthread_cond_signal(&ctx->msg_condition);
    pthread_mutex_unlock(&ctx->msg_mutex);
    pthread_join(timers_main_thread, NULL);
    
    // now that we surely stopped the timer main thread, safe to kill
    // all children with no worry of a re-spawn
    timers_stop_all();
    usleep(10000);

    ctx->state = RDBFW_STATE_STOPPED;

}

static void timers_start(void *P) {
    int rc;
    int cnt = 0;

    pthread_mutex_lock(&ctx->startup_mutex);

    memset(t_info,0,sizeof(t_info));

    while (1) {
        rc = pthread_create( &timers_main_thread, attrp, timers_main, NULL);
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
   
    pthread_mutex_lock(&ctx->startup_mutex);
    ctx->state = RDBFW_STATE_RUNNING;
    pthread_mutex_unlock(&ctx->startup_mutex);
}

static void *timer_thread(union sigval *p)
{
    int id =  p->sival_int - timer_tick[0]; 

    pthread_mutex_lock(mutex_ptr);
    if (p->sival_int >= timer_tick[0] && t_info[id].hz) {
        //TODO: emit only to requester
        rdbmsg_emit_simple(route_na, 
                route_na,
                timer_group,
                t_info[id].id,
                t_info[id].counter++);

        if (t_info[id].counter >= t_info[id].hz) {
            t_info[id].counter -= t_info[id].hz ;
        }
    }

    pthread_mutex_unlock(mutex_ptr);
    return NULL;
}


const rdbfw_plugin_api_t hw_timers_rdbfw_fns = {
    .pre_init = timers_pre_init,
    .init = timers_init,
    .de_init = timers_de_init,
    .start = timers_start,
    .stop = timers_stop,
};
