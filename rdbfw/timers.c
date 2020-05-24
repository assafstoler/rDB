//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

/*
 * The Timers module:
 *
 * This module implement a serious of timers using the select's call timeout 
 * option for efficient time-keeping.
 *
 * each requested timers causes in invocation of a thread that loops around a 
 * single timer. 
 *
 * Timers are requested via the messaging system rdbmsg. available are
 * RDBMSG_ID_TIMER_START (data is frequency (1/HZ)) 
 * RDBMSG_ID_TIMER_STOP (data is which timer - index)
 * RDBMSG_ID_TIMER_ACK (data is which timer index will be used with 'last' request.
 * - since messages are always delivered in order, it is a valid usa case for a module to
 *   ask for several timers, and only then proccess the replies. which will corolate in order
 *   to the requests.
 * - ack messages are sent with the 'to' field filled with the 'from value used during the 
 *   TIMER_START request. modues requesting timers should listen to TIMER_ACK message with the "to"
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
#include <errno.h>
#ifdef USE_PRCTL
#include <sys/prctl.h>
#endif

#include "rDB.h"
#include "messaging.h"
#include "rdbfw.h"
#include "utils.h"
#include "log.h"

#define MAX_TIMERS 16

typedef struct t_info_s {
    uint32_t hz;
    int id;
    int counter;
    int stop;
} t_info_t;

static t_info_t t_info[MAX_TIMERS];

static pthread_t timers_main_thread;
static pthread_t timers[MAX_TIMERS];
static pthread_attr_t attr;
    
static plugins_t *ctx;
static int break_requested = 0;

static void *timer_thread(void *p);

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
            t_info[i].stop = 0;
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
            //pthread_cancel(timers[i]);
            pthread_join(timers[i], NULL);
            memset(&t_info[i],0, sizeof (t_info[i]));
            memset(&timers[i],0, sizeof (timers[i]));
        }
    }
    // unlocking here after (hopefully) all pending timers are gone
    pthread_mutex_unlock(&ctx->msg_mutex);

    fwlog (LOG_DEBUG_MORE, "Done\n");
}

static void *timers_main(void *p) {
    rdbmsg_queue_t *q;

    int timer_id;
    static int first_entry = 1;

#ifdef USE_PRCTL
    prctl(PR_SET_NAME,"timers_main\0",NULL,NULL,NULL);
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
            fwl (LOG_DEBUG_MORE, p, "%s: Timed Work\n", ctx->name);                  // do some work...

            rdb_lock(ctx->msg_q_pool,__FUNCTION__);
            q = rdb_delete(ctx->msg_q_pool, 0, NULL);
            rdb_unlock(ctx->msg_q_pool, __FUNCTION__);

            if (q) {    // process message
                fwlog (LOG_DEBUG_MORE, "%s: Received message grp.id = %d.%d\n",
                        ctx->name, q->msg.group, q->msg.id);
                if (q->msg.id == timer_stop_id) {
                    timer_id = q->msg.len - timer_tick[0];
                    t_info[timer_id].stop = 1;//pthread_cancel(timers[timer_id]);
                    pthread_join(timers[timer_id], NULL);
                    memset(&t_info[timer_id],0, sizeof (t_info[timer_id]));
                    memset(&timers[timer_id],0, sizeof (timers[timer_id]));
                }
                if (q->msg.id == timer_start_id) {
                    timer_id = find_free_timer();
                    // we reply to the request before starting the timer, so receiver has a change to set up listener before ticks accomulate
                    rdbmsg_emit_simple(timer_route, 
                            q->msg.from,    // replying directly to the requester
                            timer_group,
                            timer_ack_id,
                            timer_id);
                    if (timer_id != -1) {   // we got a timer
                        int rc;
                        int cnt = 0;
                        t_info[timer_id].hz = q->msg.len;
                        t_info[timer_id].id = timer_id + timer_tick[0];
                        while (1) {
                            rc = pthread_create( &timers[timer_id], &attr, timer_thread, (void *)&t_info[timer_id]);
                            if (rc == 0) {
                                break;
                            }
                            if (rc == EAGAIN) {
                                if (cnt > MAX_THREAD_RETRY) {
                                    fwlog (LOG_ERROR, "Timer thread creation failed, MAX_THREAD_RETRY exusted\n");
                                    ctx->state = RDBFW_STATE_STOPALL;
                                } 
                                else {
                                    cnt++;
                                    fwlog (LOG_ERROR, "Timer tread creation failed, will retry\n");
                                    usleep (10000);
                                    continue;
                                }
                            }
                            else if (rc == EPERM) {
                                fwlog (LOG_ERROR, "Timer thread creation failed - missing permissions - aborting\n");
                                ctx->state = RDBFW_STATE_STOPALL;
                            }
                            else if (rc == EINVAL) {
                                fwlog (LOG_ERROR, "Timer thread creation failed - Invalid attribute - aborting\n");
                                ctx->state = RDBFW_STATE_STOPALL;
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

    timer_route = rdbmsg_register_msg_type ( "route", "ROUTE_MDL_TIMERS" );
    timer_group = rdbmsg_register_msg_type ( "group", "GROUP_TIMERS" );
    timer_start_id = rdbmsg_register_msg_type ( "id", "ID_TIMER_START" );
    timer_stop_id = rdbmsg_register_msg_type ( "id", "ID_TIMER_STOP" );
    timer_ack_id = rdbmsg_register_msg_type ( "id", "ID_TIMER_ACK" );
    for (i = 0; i < MAX_TIMERS; i++) {
        sprintf ( str, "ID_TIMER_TICK_%d",i);
        timer_tick[i] = rdbmsg_register_msg_type ( "id", str );
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

//    pthread_mutex_init(&ctx->msg_mutex, NULL);
//    pthread_cond_init(&ctx->msg_condition, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ctx->state = RDBFW_STATE_INITIALIZED;

    if (0 != rdbmsg_request(p, rdbmsg_lookup_id ("ROUTE_NA"), timer_route,
                rdbmsg_lookup_id ("GROUP_NA"), timer_start_id)) {
        fwlog (LOG_ERROR, "rdbmsg_request failed. events may not fire. Aborting (%d.%d.%d.%d)",
                rdbmsg_lookup_id ("ROUTE_NA"), timer_route, 
                rdbmsg_lookup_id ("GROUP_NA"), timer_start_id);
        ctx->state = RDBFW_STATE_STOPALL;
        return;
    }
    if (0 != rdbmsg_request(p, rdbmsg_lookup_id ("ROUTE_NA"), timer_route,
                rdbmsg_lookup_id ("GROUP_NA"), timer_stop_id)) {
        fwlog (LOG_ERROR, "rdbmsg_request failed. events may not fire. Aborting (%d.%d.%d.%d)",
                rdbmsg_lookup_id ("ROUTE_NA"), timer_route, 
                rdbmsg_lookup_id ("GROUP_NA"), timer_stop_id);
        ctx->state = RDBFW_STATE_STOPALL;
        return;
    }

}

static void timers_de_init(void *p) {
    ctx = p;
    
    fwlog (LOG_INFO, "Destroy %s\n", ctx->uname);
    ctx->state = RDBFW_STATE_LOADED;
    


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
    ctx->state = RDBFW_STATE_STOPPED;
}

static void timers_start(void *P) {
    int rc;
    int cnt = 0;

    pthread_mutex_lock(&ctx->startup_mutex);

    memset(t_info,0,sizeof(t_info));

    while (1) {
        rc = pthread_create( &timers_main_thread, &attr, timers_main, NULL);
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

static void *timer_thread(void *p)
{
    t_info_t *t_info;
    t_info = p;
    struct timespec tv, tnext, tnow;
    int rc;
    int64_t sleep_time = 0;
    int route_na = rdbmsg_lookup_id ("ROUTE_NA");
    
#ifdef USE_PRCTL
    prctl(PR_SET_NAME,"timers_slave\0",NULL,NULL,NULL);
#endif
    fwlog (LOG_INFO, "Starting timer thread @ %dHz\n",t_info->hz);

    clock_gettime(CLOCK_REALTIME, &tnext);

    while (!break_requested && !t_info->stop) {
        clock_gettime(CLOCK_REALTIME, &tnow);
        tnext.tv_nsec += 1000000000 / t_info->hz;
        if (tnext.tv_nsec >= 1000000000) {
            tnext.tv_nsec -= 1000000000;
            tnext.tv_nsec += 1000000000 % t_info->hz; // avoid drift
            fwlog (LOG_DEBUG, "drift avoidance %ld\n", 1000000000L % t_info->hz);
            tnext.tv_sec++;
        }

        sleep_time = s_ts_diff_time_ns(&tnow, &tnext, NULL);
        if (sleep_time <= -10000000000) { //over 10 seconds late - assume clock-drift.
            clock_gettime(CLOCK_REALTIME, &tnext); // 'resetting' timer
            fwlog (LOG_INFO, "TIMERS Reset after long sleep\n");
        }

        tv.tv_sec = 0;
        tv.tv_nsec = s_ts_diff_time_ns(&tnow, &tnext, NULL);
        if (tv.tv_nsec < 0) { 
            // we aer late, no delay needed
            ////fwlog (LOG_WARN, "Timer late - skipping sleep\n");
        }
        else {
            ////fwlog (LOG_TRACE, "Timer sleep %ld\n", tv.tv_nsec);
            while (tv.tv_nsec >= 1000000000) {
                tv.tv_nsec -= 1000000000;
                tv.tv_sec ++;
            }
            // portable, simple way to wait for timer. 
            rc=pselect(0, NULL, NULL, NULL, &tv,NULL);
            if (rc != 0) {
                fwlog (LOG_DEBUG, "rc = %d during select. Sperious timer may occured\n", rc);
            }
        }
        // we do NOT want to emit if we are canceled 
        //if (break_requested) break;
        //pthread_testcancel();
        //fwlog_no_emit(LOG_WARN, "CORE_EMIT\n");
        rdbmsg_emit_simple(route_na, 
                route_na,
                timer_group,
                t_info->id,
                t_info->counter++);

        if (t_info->counter>=1000) {
            t_info->counter=0;
        }
    //pthread_exit(NULL); 
    }

    pthread_exit(NULL); 
}

const rdbfw_plugin_api_t timers_rdbfw_fns = {
    .pre_init = timers_pre_init,
    .init = timers_init,
    .de_init = timers_de_init,
    .start = timers_start,
    .stop = timers_stop,
};
