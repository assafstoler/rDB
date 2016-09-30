#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/select.h>
#include <errno.h>
#include <assert.h>

#include "rDB.h"
#include "messaging.h"
#include "rdbfw.h"
    

rdb_pool_t *ppc; // copy of pool pointer, as messages need to be able to iterate all modules we need to find them
plugins_t *plugin_node;

extern uint64_t wake_count_limit;
static pthread_t awake_sleepers_thread;



int rdbmsg_init(rdb_pool_t *plugin_pool) {

    ppc = plugin_pool;

    return 0;
}


/* request to listen (receive) messages matching the specified creiteria 
 * to, form, group and Id, any of which can be set to ALL or NA.
 * any message matching all criteria (AND) will be delivered.
 * */
 
int rdbmsg_request(void  *ctx, int from, int to, int group, int id){
    int rc = 1;
    rdb_pool_t *p;
    rdbmsg_dispatch_t    *d;
    // TODO set and protect max buf correctly!
    char buf[80];
   
    //TODO: once working,make this into a fn() 
    //TODO: add 'rdb_remove' as needed to back out of a failed insert
    p = ((plugins_t *)(ctx))->msg_dispatch_root;
    
    d = rdb_get_const(p, 0, from);
    if (d == NULL) { 
        d = calloc(1,sizeof(rdbmsg_dispatch_t)) ;
        if (d == NULL) goto rdbmsg_request_out;
        d->value = from;
        rc = rdb_insert(p, d);
        if (rc != 1) {
            rc = 1;
            goto rdbmsg_request_out;
        } else rc=0;
    }

    if (d->next == NULL) {
        sprintf(buf,"%s.%d", ((plugins_t *)(ctx))->name, from);
        d->next  = rdb_register_um_pool ( buf,
                            1, 0, RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL ); 
        if (d->next == NULL) {
            printf("unable to register tree %s.%d\n", ((plugins_t *)(ctx))->name, from);
            //TODO exit() or other more orderly shutdown?
            exit(1);
        }
    }

    p = d->next;
    d = rdb_get_const(p, 0, to);
    if (d == NULL) { 
        d = calloc(1,sizeof(rdbmsg_dispatch_t)) ;
        if (d == NULL) goto rdbmsg_request_out;
        d->value = to;
        rc = rdb_insert(p, d);
        if (rc != 1) {
            rc = 1;
            goto rdbmsg_request_out;
        } else rc=0;
    }
    
    if (d->next == NULL) {
        sprintf(buf,"%s.%d.%d", ((plugins_t *)(ctx))->name, from, to);
        d->next  = rdb_register_um_pool ( buf,
                            1, 0, RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL ); 
        if (d->next == NULL) {
            printf("unable to register tree %s.%d.%d\n", ((plugins_t *)(ctx))->name, from, to);
            //TODO exit() or other more orderly shutdown?
            exit(1);
        }
    }

    p = d->next;
    d = rdb_get_const(p, 0, group);
    if (d == NULL) { 
        d = calloc(1,sizeof(rdbmsg_dispatch_t)) ;
        if (d == NULL) goto rdbmsg_request_out;
        d->value = group;
        rc = rdb_insert(p, d);
        if (rc != 1) {
            rc = 1;
            goto rdbmsg_request_out;
        } else rc=0;
    }
    
    if (d->next == NULL) {
        sprintf(buf,"%s.%d.%d.%d", ((plugins_t *)(ctx))->name, from, to, id);
        d->next  = rdb_register_um_pool ( buf,
                            1, 0, RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL ); 
        if (d->next == NULL) {
            printf("unable to register tree %s.%d.%d.%d\n", ((plugins_t *)(ctx))->name, from, to, id);
            //TODO exit() or other more orderly shutdown?
            exit(1);
        }
    }

    p = d->next;
    d = rdb_get_const(p, 0, id);
    if (d == NULL) { 
        d = calloc(1,sizeof(rdbmsg_dispatch_t)) ;
        if (d == NULL) goto rdbmsg_request_out;
        d->value = id;
        rc = rdb_insert(p, d);
        if (rc != 1) {
            rc = 1;
            goto rdbmsg_request_out;
        } else rc=0;
    }


    printf("request registered %d %d\n", id, rc);
    //rdb_dump(p,0,",");
    //printf("\nrequests dumped\n");


rdbmsg_request_out:
    return rc;

}

// In order to use the rdb_iterate function, and be able to transfer to it both
// the stage and the user pointer, which holds the message, a new structure is 
// created = sfu. for the life of me can't recall why I put an 'f' there ;)
typedef struct sfu_s {
        int stage;
        void *user_ptr;
} sfu_t;

void check_if_subscribed (void *data, void *user_ptr, int stage);

int check_cb (void *data, void *user_ptr) {
    rdbmsg_dispatch_t        *d;
    sfu_t                   *sfu;
    rdbmsg_internal_msg_t    *msg;

    d = (rdbmsg_dispatch_t *) data;
    sfu = (sfu_t *) user_ptr;
    msg = (rdbmsg_internal_msg_t *) sfu->user_ptr;

    check_if_subscribed(d->next, msg, sfu->stage);
    
    if (msg->rc == RDBMSG_RC_IS_SUBSCRIBER) return RDB_CB_ABORT;
    else return RDB_CB_OK;
   
} 
// data - pointer to tree
// user_ptr - the message being sent

void check_if_subscribed (void *data, void *user_ptr, int stage) {
    rdb_pool_t *pool;
    rdbmsg_internal_msg_t *msg;
    rdbmsg_dispatch_t *d;
    sfu_t sfu;

    pool = (rdb_pool_t *) data;
    msg = (rdbmsg_internal_msg_t *) user_ptr;
  
    // this will be skipped if NDEBUG is defined, as this test is only needed
    // in debugging context.
    assert (stage >= 0 && (stage < 4));
     
    //printf("check: s %d\n", stage);

    if (stage == 0) {
        if (msg->from == RDBMSG_ROUTE_NA) { // all from possibilitied need to be checked)
            sfu.stage = stage + 1;
            sfu.user_ptr = user_ptr;

            //TODO break for rc!=0. each module get each message only once. add ctest to verify
            rdb_iterate(pool,  0, check_cb, &sfu, NULL, NULL); 
        } else {
            d = rdb_get_const (pool, 0, RDBMSG_ROUTE_NA); 
            if (d) {
                check_if_subscribed (d->next, user_ptr, stage + 1);
            } 
            d = rdb_get_const ( pool, 0, msg->from);
            if (d) {
                check_if_subscribed (d->next, user_ptr, stage + 1);
            } 
        }

    }
    if (stage == 1) {
        if (msg-> to == RDBMSG_ROUTE_NA) { // all from possibilitied need to be checked)
            sfu.stage = stage + 1;
            sfu.user_ptr = user_ptr;

            //TODO break for rc!=0. each module get each message only once. add ctest to verify
            rdb_iterate(pool,  0, check_cb, &sfu, NULL, NULL); 
        } else {
            d = rdb_get_const (pool, 0, RDBMSG_ROUTE_NA);
            if (d) {
                check_if_subscribed (d->next, user_ptr, stage + 1);
            } 
            d = rdb_get_const ( pool, 0, msg->to);
            if (d) {
                check_if_subscribed (d->next, user_ptr, stage + 1);
            } 
        }

    }
    if (stage == 2) {
        if (msg->group == RDBMSG_GROUP_NA) { // all from possibilitied need to be checked)
            sfu.stage = stage + 1;
            sfu.user_ptr = user_ptr;

            //TODO break for rc!=0. each module get each message only once. add ctest to verify
            rdb_iterate(pool,  0, check_cb, &sfu, NULL, NULL); 
        } else {
            d = rdb_get_const (pool, 0, RDBMSG_GROUP_NA);
            if (d) {
                check_if_subscribed (d->next, user_ptr, stage + 1);
            } 
            d = rdb_get_const ( pool, 0, msg->group);
            if (d) {
                check_if_subscribed (d->next, user_ptr, stage + 1);
            } 
        }

    }
    if (stage == 3) {
        if (msg->id == RDBMSG_ID_NA) { // all from possibilitied need to be checked)
            sfu.stage = stage + 1;
            sfu.user_ptr = user_ptr;

            //TODO break for rc!=0. each module get each message only once. add ctest to verify
            rdb_iterate(pool,  0, check_cb, &sfu, NULL, NULL); 
        } else {
            d = rdb_get_const (pool, 0, RDBMSG_ID_NA);
            if (d) {
                msg->rc = RDBMSG_RC_IS_SUBSCRIBER;
            } else { 
                d = rdb_get_const ( pool, 0, msg->id);
                if (d) {
                    msg->rc = RDBMSG_RC_IS_SUBSCRIBER;
                }
            } 
        }
    }
    return;

}

void * awake_sleepers(void *Hz_p);
    
int rdbmsg_delay_HZ(int new_Hz){
    static int Hz=0;
    int rc = -1;

    if (new_Hz && (!Hz)) { // start delay delivery
        Hz = new_Hz;
        rc = pthread_create( &awake_sleepers_thread, NULL, awake_sleepers, &Hz);

    } else if (new_Hz && Hz) { // new Hz
        Hz = new_Hz;
        //adjust speed
    } else if ((!new_Hz) && Hz) { //stop
        Hz=0;
    }
    
    return (rc);
}

// If messaging system if configured for allow delaied awakening
// of receivers, this fn() will trigger wake them up every so often -
// based on the HZ value supplied to rdbmsg_delay_HZ. value of 0 means 
// immedieet delivery of all messages.

int awake_sleepers_cb (void *data, void *user_ptr) {
    plugins_t *ctx;
    ctx = (plugins_t *) data;

    if (ctx->msg_pending_count) {
        pthread_mutex_lock(&ctx->msg_mutex);
        pthread_cond_signal(&ctx->msg_condition);
        pthread_mutex_unlock(&ctx->msg_mutex);
    }

    return RDB_CB_OK;
}

void * awake_sleepers(void *Hz_p){
    struct timeval tv;
    int rc;
    int *Hz;
    int local_Hz;           // copy Hz value to avoid devide by zero if HZ was changed...
    
    Hz = (void *) Hz_p;

    while ((local_Hz = *Hz)) {
        if (local_Hz == 1) {
            tv.tv_sec=1;
            tv.tv_usec= 0;
        } else {
            tv.tv_sec=0;
            tv.tv_usec = 1000000 / local_Hz;
        }
        // TODO: this is portable ... but consider using timers directly it RT accuracy required.
        rc = select(0, NULL, NULL, NULL, &tv);
        if ((rc == -1) && (errno != EINTR)) { // error, and not a signal
            // Report?;
        } else rdb_iterate(ppc,  0, awake_sleepers_cb, NULL, NULL, NULL); 
    }
    return 0;
}


// iterate the modules.
int emit_simple_cb (void *data, void *user_ptr) {
    plugins_t *ctx;
    rdbmsg_internal_msg_t *msg;
    rdbmsg_queue_t *q;

    ctx = (plugins_t *) data;
    msg = (rdbmsg_internal_msg_t *) user_ptr;

    msg->rc = 0; // reset our flag in case it's set
    check_if_subscribed (ctx->msg_dispatch_root, user_ptr, 0);

    if (msg->rc == RDBMSG_RC_IS_SUBSCRIBER) {
#ifdef USE_MSG_BUFFERS
        rdb_lock(ctx->empty_msg_store);
        q = rdb_delete(ctx->empty_msg_store, 0, NULL);
        rdb_unlock(ctx->empty_msg_store);
#else
        q = calloc(1,sizeof(rdbmsg_queue_t)) ;
#endif
        if (q == NULL) {
            rdb_unlock(ctx->msg_q_pool);
            goto emit_simple_err;
        }
        memset(q,0,sizeof(rdbmsg_queue_t));
        
        memcpy(&(q->msg), msg, sizeof (rdbmsg_msg_t));

        rdb_lock(ctx->msg_q_pool);
        // Insersion to a rdb FIFO/LIFO can not fail is 1 is not null, 
        // which we test for above. so in the name of speed, test is 
        // omitted.
        rdb_insert(ctx->msg_q_pool, q);
        ctx->msg_pending_count++;
#ifdef MSG_ACCOUNTING
        ctx->msg_rx_count++;
#endif
        rdb_unlock(ctx->msg_q_pool);

        msg->emitted++;

        if (ctx->msg_pending_count > wake_count_limit) {
        pthread_mutex_lock(&ctx->msg_mutex);
        pthread_cond_signal(&ctx->msg_condition);
        pthread_mutex_unlock(&ctx->msg_mutex);
        }
        if (ctx->msg_pending_count > 40000) {
            printf("* %d - ",ctx->msg_pending_count);
            usleep(0);
            printf("%d\n",ctx->msg_pending_count);
        }
        //printf("msg inserted to %s (%d, id=%d)\n",ctx->name,rc, q->msg.id);
        return RDB_CB_OK; // no need to continue testing ths plug in
    }

//emit_simple_ok:
    return RDB_CB_OK;

emit_simple_err:
    //TODO: do we want to abort? report error somehow?
    printf("out of message buffers\n");
    return RDB_CB_OK;
}

int rdbmsg_emit_simple(int from, int to, int group, int id, int data){
    
    rdbmsg_internal_msg_t msg;

    //TODO: make sure all are within their respected range
    msg.from = from;
    msg.to = to;
    msg.group = group;
    msg.id = id;
    msg.len = data;
    msg.data = NULL;
    msg.legacy = 0;
    msg.rc = 0;
    msg.emitted=0;
    rdb_iterate(ppc,  0, emit_simple_cb, (void *) &msg, NULL, NULL); 

    return (msg.emitted);
}

