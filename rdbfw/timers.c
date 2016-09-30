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

#include "rDB.h"
#include "messaging.h"
#include "rdbfw.h"

#define MAX_TIMERS 16

pthread_mutex_t bench_mutex;
pthread_cond_t bench_condition;
    

typedef struct t_info_s {
    uint32_t hz;
    int id;
} t_info_t;

static t_info_t t_info[MAX_TIMERS];

static pthread_t timers_main_thread;
static pthread_t timers[MAX_TIMERS];
static pthread_attr_t attr;
    
static plugins_t *ctx;
static int break_requested = 0;

void *timer_thread(void *p);


static int find_free_timer() {
    int i;

    for (i = 0; i < MAX_TIMERS; i++) {
        if (t_info[i].hz == 0 /*timers[i] == NULL*/) return i;
    }
return -1;
}


void *timers_main(void *p) {
    //rdbmsg_msg_t *msg;
    rdbmsg_queue_t *q;

    int timer_id;
    int rc;

    ctx->state = RDBFW_STATE_RUNNING;

    pthread_mutex_lock(&ctx->msg_mutex);
    while (break_requested == 0) {
        pthread_cond_wait(&ctx->msg_condition, &ctx->msg_mutex);
        printf("timers_main woke up\n");
        do {
            //printf("Timed Work\n");                  // do some work...
            rdb_lock(ctx->msg_q_pool);
            q = rdb_delete(ctx->msg_q_pool, 0, NULL);
            if (q != NULL) {
                ctx->msg_pending_count--;
#ifdef USE_MSG_BUFFERS
                rdb_lock(ctx->empty_msg_store);
                if (0 == rdb_insert(ctx->empty_msg_store, q)) {
                    printf("failed to release buffer\n");
                    exit(1);
                }
                rdb_unlock(ctx->empty_msg_store);
#else 
                free(q);
#endif
            }
            rdb_unlock(ctx->msg_q_pool);
            if (q) printf("Received message grp.id = %d.%d\n",q->msg.group, q->msg.id);
            if (q) {    // process message
                if (q->msg.id == RDBMSG_ID_TIMER_START) {
                    timer_id = find_free_timer();
                    // we reply to the request before starting the timer, so receiver has a change to set up listener before ticks accomulate
                    rdbmsg_emit_simple(RDBMSG_ROUTE_MDL_TIMERS, 
                            q->msg.from,    // replying directly to the requester
                            RDBMSG_GROUP_TIMERS,
                            RDBMSG_ID_TIMER_ACK,
                            timer_id);
                    if (timer_id != -1) {   // we got a timer
                        t_info[timer_id].hz = q->msg.len;
                        t_info[timer_id].id = timer_id + RDBMSG_ID_TIMER_TICK_0,
                        rc = pthread_create( &timers[timer_id], &attr, timer_thread, (void *)&t_info[timer_id]);
                        if (rc != 0) { 
                            //TODO: handle error
                            //note, we already ack'ed the timer before creating
                            //it, this is to give the listener a chance to 
                            //start listening before the first tick. is that 
                            //needed? what todo with read error?
                        }

                    }
                    
                }

            }
        } while (q);

    }
    pthread_mutex_unlock(&ctx->msg_mutex);

    ctx->state = RDBFW_STATE_STOPPED;
    pthread_exit(NULL);
}

void timers_init(void *p) {
    ctx = p;

    pthread_mutex_init(&bench_mutex, NULL);
    pthread_cond_init(&bench_condition, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ctx->state = RDBFW_STATE_INITILIZED;

    rdbmsg_request(p, RDBMSG_ROUTE_NA, RDBMSG_ROUTE_MDL_TIMERS, RDBMSG_GROUP_NA, RDBMSG_ID_TIMER_START);
    rdbmsg_request(p, RDBMSG_ROUTE_NA, RDBMSG_ROUTE_MDL_TIMERS, RDBMSG_GROUP_NA, RDBMSG_ID_TIMER_STOP);

}

void timers_stop(void *P) {
    break_requested = 1;
}

void timers_start(void *P) {

    memset(t_info,0,sizeof(t_info));
    /*rc =*/ pthread_create( &timers_main_thread, &attr, timers_main, NULL);
}

void *timer_thread(void *p)
{
    t_info_t *t_info;
    t_info = p;
    printf("Hz = %d\n",t_info->hz);
    
    struct timeval tv;

    while (!break_requested) {
        if (t_info->hz == 1) {
            tv.tv_sec=1;
            tv.tv_usec= 0;
        } else {
            tv.tv_sec=0;
            tv.tv_usec = 1000000/t_info->hz;
        }
        // portable, simple way to wait for timer. 
        select(0, NULL, NULL, NULL, &tv);
        rdbmsg_emit_simple(RDBMSG_ROUTE_NA, 
                RDBMSG_ROUTE_NA,
                RDBMSG_GROUP_TIMERS,
                t_info->id,
                0);
    }

   pthread_exit(NULL); 
}


const rdbfw_plugin_api_t rdbfw_fns = {
    .init = timers_init,
    .start = timers_start,
    .stop = timers_stop,
};
