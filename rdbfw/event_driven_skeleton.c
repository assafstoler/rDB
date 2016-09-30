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

static pthread_t skel_event_thread;
static pthread_attr_t attr;
    
static plugins_t *ctx;
static int break_requested = 0;


void *skel_event(void *p) {
    rdbmsg_msg_t *msg;
    rdbmsg_queue_t *q;
    printf("Starting %s\n", ctx->name);
    ctx->state = RDBFW_STATE_RUNNING;

    pthread_mutex_lock(&ctx->msg_mutex);
    while (break_requested == 0) {
        pthread_cond_wait(&ctx->msg_condition, &ctx->msg_mutex);
#ifdef WAKEUP_ACCOUNTING
        ctx->wakeup_count++;
#endif
        printf("skel_event_woken_up (%d)\n",ctx->msg_pending_count);                  // do some work...
        do {
            rdb_lock(ctx->msg_q_pool);
            q = rdb_delete(ctx->msg_q_pool, 0, NULL);
            if (q) {
		msg=&(q->msg);
		printf("from %d to %d grp %d ID %d val %d\n",msg->from, msg->to, msg->group, msg->id, msg->len);
                if (msg->id ==  RDBMSG_ID_TIMER_ACK) {
                    // now ask to receive (in addition) messages for assigned timer    
                    rdbmsg_request(ctx, RDBMSG_ROUTE_NA, RDBMSG_ROUTE_NA, RDBMSG_GROUP_TIMERS, RDBMSG_ID_TIMER_TICK_0 + msg->len);
                    printf("got timer id %d\n",msg->len);
                }
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
            //if (q == NULL) printf("got empty message - sperious\n");
            rdb_unlock(ctx->msg_q_pool);
            //if (q) printf("Received message grp.id = %d.%d\n",q->msg.group, q->msg.id);
        } while (q);

    }
    pthread_mutex_unlock(&ctx->msg_mutex);

    ctx->state = RDBFW_STATE_STOPPED;
    pthread_exit(NULL);

}

void skel_event_init(void *p) {
    ctx = p;
    
    printf("Initilizing %s\n", ctx->name);

    pthread_mutex_init(&ctx->msg_mutex, NULL);
    pthread_cond_init(&ctx->msg_condition, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ctx->state = RDBFW_STATE_INITILIZED;
 
    // ask to receive messages of type... Timers Ack.    
    rdbmsg_request(p, RDBMSG_ROUTE_NA, RDBMSG_ROUTE_NA, RDBMSG_GROUP_TIMERS, RDBMSG_ID_TIMER_ACK);

}

void skel_event_start(void *p) {
    rc = pthread_create( &skel_event_thread, &attr, skel_event, NULL);
    rdbmsg_emit_simple(RDBMSG_ROUTE_MDL_EVENT_SKEL,
                RDBMSG_ROUTE_MDL_TIMERS,
                RDBMSG_GROUP_TIMERS,
                RDBMSG_ID_TIMER_START,
                2 /*Hz*/);
    rdbmsg_emit_simple(RDBMSG_ROUTE_MDL_EVENT_SKEL,
                RDBMSG_ROUTE_MDL_TIMERS,
                RDBMSG_GROUP_TIMERS,
                RDBMSG_ID_TIMER_START,
                10 );
}

void skel_event_stop(void *p) {
    break_requested = 1;
}

const rdbfw_plugin_api_t rdbfw_fns = {
    .init = skel_event_init,
    .start = skel_event_start,
    .stop = skel_event_stop,
};
