//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

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
#include "utils.h"
#include "rdbfw.h"
#include "log.h"
#include "fwalloc.h"

rdb_pool_t *ppc; // copy of pool pointer, as messages need to be able to iterate all modules we need to find them
plugins_t *plugin_node;

uint64_t wake_count_limit;
static pthread_t awake_sleepers_thread;

static rdb_pool_t *msg_type_pool=NULL;
static uint32_t route_na = 0;
static uint32_t route_idx_next = 0;
static uint32_t route_idx_max = 1023;

static uint32_t group_na = 2048;
static uint32_t group_idx_next = 2048;
static uint32_t group_idx_max = 3095;

static uint32_t id_na = 3096;
static uint32_t id_idx_next = 3096;
static uint32_t id_idx_max = 65535;

static pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

int rdbmsg_init (rdb_pool_t *plugin_pool) {
    route_idx_next = 0;
    group_idx_next = 2048;
    id_idx_next = 3096;

    ppc = plugin_pool;
    
    if (unittest_en != UT_MSG_INIT_1) {
        msg_type_pool = rdb_register_um_pool(
                "msg_types",
                2,
                0,
                RDB_KUINT32 | RDB_KASC | RDB_BTREE,
                NULL);
    }

    if ( NULL == msg_type_pool ) {
        fwl_no_emit (LOG_FATAL, NULL, "failed to register message-type pool\n");
        return -1;
    }
    if ( unittest_en == UT_MSG_INIT_2 || -1 == rdb_register_um_idx ( msg_type_pool,
            1,
            offsetof ( rdbmsg_msg_type_t, string ) - offsetof ( rdbmsg_msg_type_t , value),
            RDB_KSTR | RDB_KASC | RDB_BTREE,
            NULL ) ) {
        fwl_no_emit (LOG_ERROR, NULL, "failed to register message-type index (string)\n");
        rdb_drop_pool (msg_type_pool);
        msg_type_pool = NULL;
        return -1;
    }

    if ( unittest_en == UT_MSG_INIT_3 || 
            -1 == rdbmsg_register_msg_type ( "route", "ROUTE_NA" ) ||
            -1 == rdbmsg_register_msg_type ( "route", "ROUTE_FW" ) ||
            -1 == rdbmsg_register_msg_type ( "group", "GROUP_NA" ) ||
            -1 == rdbmsg_register_msg_type ( "id", "ID_NA" ) ||
            -1 == rdbmsg_register_msg_type ( "route", "ROUTE_LOGGER" ) ||
            -1 == rdbmsg_register_msg_type ( "group", "GROUP_LOGGER" ) ||
            -1 == rdbmsg_register_msg_type ( "id", "ID_LOG_ENTRY" )) {
        fwl_no_emit (LOG_FATAL, NULL, "Failed to register key messaging system type(s)\n");
        rdb_flush (msg_type_pool, NULL, NULL);
        rdb_drop_pool (msg_type_pool);
        msg_type_pool = NULL;
        return -1;
    }
    fwl_no_emit (LOG_DEBUG_MORE, NULL, "rdbmsg_init(): Success\n");
    
   return 0;
}
int rdbmsg_destroy (void) {
        rdb_flush (msg_type_pool, NULL, NULL);
        rdb_drop_pool (msg_type_pool);
        msg_type_pool = NULL;
        ppc = NULL;
        return 0;
}

int rdbmsg_lookup_id (char *str){
    rdbmsg_msg_type_t *msg;
    
    pthread_rwlock_rdlock ( &rwlock );
    
    msg = rdb_get (msg_type_pool, 1, str);

    pthread_rwlock_unlock ( &rwlock );

    if ( msg ) {
        return msg->value;
    }
    else {
        fwl_no_emit (LOG_DEBUG_MORE, NULL, "LOOKUP FAILED for string: \"%s\"\n", str);
        return -1;
    }
}

char * rdbmsg_lookup_string (uint32_t value){
    rdbmsg_msg_type_t *msg;
    
    pthread_rwlock_rdlock ( &rwlock );

    msg = rdb_get (msg_type_pool, 0, &value);
    
    pthread_rwlock_unlock ( &rwlock );
    
    if ( msg ) {
        return msg->string;
    }
    else {
        return NULL;
    }
}

int rdbmsg_register_msg_type (char *type, char *msg_str){
    rdbmsg_msg_type_t *msg;
    int rc;

    pthread_rwlock_wrlock ( &rwlock );

    msg = rdb_get (msg_type_pool, 1, msg_str);
    if ( NULL != msg ) {
        fwl_no_emit (LOG_WARN, NULL, "Attempting to register existing message - request ignored (%s:%s)\n",
			type, msg_str );
        rc = -1;
    }
    else {
        msg = malloc ( sizeof (rdbmsg_msg_type_t ) );
        if ( NULL == msg ) {
            fwl_no_emit (LOG_ERROR, NULL, "Out of memory while attempting to register message\n");
            rc = -1;
            goto msg_register_out;
        }
        strncpy (msg->string, msg_str, 64);
        msg->string[63] = 0;
        if ( strcmp ( "route", type ) == 0 ) {
            if ( route_idx_next <= route_idx_max ) {
                msg->value = route_idx_next++;
            }
            else {
                fwl_no_emit (LOG_ERROR, NULL, "Out of Route's while attempting to register message\n");
                rc = -1;
                goto msg_register_out;
            }
        }
        else if ( strcmp ( "group", type ) == 0 ) {
            if ( group_idx_next <= group_idx_max ) {
                msg->value = group_idx_next++;
            }
            else {
                fwl_no_emit (LOG_ERROR, NULL, "Out of Groups while attempting to register message\n");
                rc = -1;
                goto msg_register_out;
            }
        }
        else if ( strcmp ( "id", type ) == 0 ) {
            if ( id_idx_next <= id_idx_max ) {
                msg->value = id_idx_next++;
            }
            else {
                fwl_no_emit (LOG_ERROR, NULL, "Out of ID's while attempting to register message\n");
                rc = -1;
                goto msg_register_out;
            }
        }
        else { //wrongtype
            fwl_no_emit (LOG_ERROR, NULL, "Illigal type while attempting to register message\n");
            rc = -1;
            goto msg_register_out;
        }
        if ( 0 == rdb_insert ( msg_type_pool, msg ) ) {
            fwl_no_emit (LOG_ERROR, NULL, "Failed to insert record while attempting to register message\n");
            rc = -1;
            goto msg_register_out;
        }
        rc = msg->value;
    }

msg_register_out:

    pthread_rwlock_unlock ( &rwlock );
    return rc;


}

/* How it works
 * Both the Emitter and the Requester are allowed to have wildcards in all the fields
 * (To, from, group, and id). 
 * when checking if a ,essage should be emitter, the checker first see if the requested is a wild-card, 
 * in case the outcome is obvious (true). if not,it will check for two conditions: a) the sender issued 
 * a wildcard, b) the sender is an exact matcg to the reciever. if any are true, the message will be emitted.
 * */

/* request to listen (receive) messages matching the specified creiteria 
 * to, form, group and Id, any of which can be set to ALL or NA.
 * any message matching all criteria (AND) will be delivered.
 * */
 
int rdbmsg_request ( void  *ctx, int from, int to, int group, int id) {
    return rdbmsg_request_custom (ctx, from, to, group, id, 0, NULL, NULL, NULL);
}

int rdbmsg_request_custom ( void  *ctx, int from, int to, int group, int id,
        int use_fwalloc,
        rdb_pool_t *msg_q_pool,
        pthread_mutex_t *msg_mutex,
        pthread_cond_t *msg_condition ){
    int rc = 1;
    rdb_pool_t *p;
    rdbmsg_dispatch_t    *d;
    // TODO set and protect max buf correctly!
    char buf[80];
    /*fwlog (LOG_DEBUG, "%s ... in progress %d %d %d %d\n",
            ((plugins_t *)(ctx))->name, from, to, group, id);
   */
    //TODO: once working,make this into a fn() 
    //TODO: add 'rdb_remove' as needed to back out of a failed insert

    if ( -1 == from || -1 == to || -1 == group || -1 == id) {
        fwl_no_emit ( LOG_ERROR, ctx, "BAD ID? %d:%d:%d:%d\n", from, to, group, id);
        return -1;
    }

    rdb_lock(ppc,__FUNCTION__);
    p = ((plugins_t *)(ctx))->msg_dispatch_root;

    d = rdb_get_const(p, 0, from);
    if (d == NULL) { 
        d = calloc(1,sizeof(rdbmsg_dispatch_t)) ;
        if (d == NULL) goto rdbmsg_request_out;
        d->value = from;
        rc = rdb_insert(p, d);
        if (rc != 1) {
            rc = 1;
            fwl_no_emit ( LOG_ERROR, ctx, "failed to insert rdb_request (1)\n");
            goto rdbmsg_request_out;
        } else rc=0;
    }

    if (d->next == NULL) {
        sprintf(buf,"%s.%d", ((plugins_t *)(ctx))->uname, from);
        d->next  = rdb_register_um_pool ( buf,
                            1, 0, RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL ); 
        if (d->next == NULL) {
            fwl_no_emit ( LOG_ERROR, ctx, "unable to register tree %s.%d\n", ((plugins_t *)(ctx))->uname, from);
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
            fwl_no_emit ( LOG_ERROR, ctx, "failed to insert rdb_request (2)\n");
            goto rdbmsg_request_out;
        } else rc=0;
    }
    
    if (d->next == NULL) {
        sprintf(buf,"%s.%d.%d", ((plugins_t *)(ctx))->uname, from, to);
        d->next  = rdb_register_um_pool ( buf,
                            1, 0, RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL ); 
        if (d->next == NULL) {
            fwl_no_emit (LOG_ERROR, ctx, "unable to register tree %s.%d.%d\n",
                    ((plugins_t *)(ctx))->uname, from, to);
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
            fwl_no_emit ( LOG_ERROR, ctx, "failed to insert rdb_request (3)\n");
            goto rdbmsg_request_out;
        } else rc=0;
    }
    
    if (d->next == NULL) {
        sprintf(buf,"%s.%d.%d.%d", ((plugins_t *)(ctx))->uname, from, to, group);
        d->next  = rdb_register_um_pool ( buf,
                            1, 0, RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL ); 
        if (d->next == NULL) {
            fwl_no_emit ( LOG_ERROR, ctx, "unable to register tree %s.%d.%d.%d\n", ((plugins_t *)(ctx))->uname, from, to, group);
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
        d->use_fwalloc = use_fwalloc;
        rc = rdb_insert(p, d);
        if (rc != 1) {
            rc = 1;
            fwl_no_emit ( LOG_ERROR, ctx, "failed to insert rdb_request (4)\n");
            goto rdbmsg_request_out;
        } else rc=0;
    }
    if ( NULL != msg_q_pool ) { // custom queue enable
        if ( d->pvt_msg_q_pool != NULL && d->pvt_msg_q_pool != msg_q_pool ) {
            fwl_no_emit ( LOG_ERROR, ctx, "Reclassification of message queue in rdbmsg_request()\n");
            exit (1);
        }
        d->pvt_msg_q_pool = msg_q_pool;
        d->pvt_msg_mutex = msg_mutex;
        d->pvt_msg_condition = msg_condition;
        d->use_fwalloc = use_fwalloc;
    }

    fwl_no_emit ( LOG_DEBUG, ctx, "%s:! request registered %d %d\n", p->name, id, rc);


rdbmsg_request_out:
    p = ((plugins_t *)(ctx))->msg_dispatch_root;
    rdb_unlock(ppc,__FUNCTION__);
    fwl_no_emit ( LOG_DEBUG, ctx, "%s:? request registered %d %d\n", p->name, id, rc);
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
     
    //fwlog (LOG_TRACE, "check: s %d\n", stage);

    if (stage == 0) {
        if (msg->from == route_na) { // all from possibilitied need to be checked)
            sfu.stage = stage + 1;
            sfu.user_ptr = user_ptr;

            //TODO break for rc!=0. each module get each message only once. add ctest to verify
            rdb_iterate(pool,  0, check_cb, &sfu, NULL, NULL); 
        } else {
            d = rdb_get_const (pool, 0, route_na); 
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
        if (msg-> to == route_na) { // all from possibilitied need to be checked)
            sfu.stage = stage + 1;
            sfu.user_ptr = user_ptr;

            //TODO break for rc!=0. each module get each message only once. add ctest to verify
            rdb_iterate(pool,  0, check_cb, &sfu, NULL, NULL); 
        } else {
            d = rdb_get_const (pool, 0, route_na);
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
        if (msg->group == group_na) { // all from possibilitied need to be checked)
            sfu.stage = stage + 1;
            sfu.user_ptr = user_ptr;

            //TODO break for rc!=0. each module get each message only once. add ctest to verify
            rdb_iterate(pool,  0, check_cb, &sfu, NULL, NULL); 
        } else {
            d = rdb_get_const (pool, 0, group_na);
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
        if (msg->id == id_na) { // all from possibilitied need to be checked)
            sfu.stage = stage + 1;
            sfu.user_ptr = user_ptr;

            //TODO break for rc!=0. each module get each message only once. add ctest to verify
            rdb_iterate(pool,  0, check_cb, &sfu, NULL, NULL); 
        } else {
            d = rdb_get_const (pool, 0, id_na);
            if (d) {
                msg->rc = RDBMSG_RC_IS_SUBSCRIBER;
                msg->use_fwalloc = d->use_fwalloc;
                msg->msg_q_pool = d->pvt_msg_q_pool;
                msg->msg_mutex = d->pvt_msg_mutex;
                msg->msg_condition = d->pvt_msg_condition;
            } else { 
                d = rdb_get_const ( pool, 0, msg->id);
                if (d) {
                    msg->rc = RDBMSG_RC_IS_SUBSCRIBER;
                    msg->use_fwalloc = d->use_fwalloc;
                    msg->msg_q_pool = d->pvt_msg_q_pool;
                    msg->msg_mutex = d->pvt_msg_mutex;
                    msg->msg_condition = d->pvt_msg_condition;
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


int rdbmsg_implode (plugins_t *ctx, rdbmsg_queue_t *q) {
    rdbmsg_msg_t *msg;
    msg=&(q->msg);

    if (msg->data) {
        if ( msg->data_cleanup ) {
            msg->data_cleanup(msg->data);
        }
        else if ( msg->use_fwalloc ) {
            rdbfw_free ( msg->data );
        }
        else {
            free ( msg->data );
        }
        msg->data = NULL;
    }

    rdbmsg_free (ctx, q);
    return 0;
}

int rdbmsg_free (plugins_t *ctx, rdbmsg_queue_t *q) {
    __sync_fetch_and_sub(&ctx->msg_pending_count,1);
#ifdef USE_MSG_BUFFERS
    //rdbmsg_msg_t *msg;
    //            msg=&(q->msg);
    rdb_lock(ctx->empty_msg_store,__FUNCTION__);
    if (0 == rdb_insert(ctx->empty_msg_store, q)) {
        fwlog_no_emit (LOG_ERROR, "failed to release buffer\n");
        rdb_unlock(ctx->empty_msg_store,__FUNCTION__);
        return -1;
    }
    rdb_unlock(ctx->empty_msg_store,__FUNCTION__);
#else 
    free(q);
#endif
    return 0;
}

// iterate the modules.
int emit_simple_cb (void *data, void *user_ptr) {
    plugins_t *ctx;
    rdbmsg_internal_msg_t *msg;
    rdbmsg_queue_t *q;
    int rc;
    int lock_rc;
    int lock_rc_try_cnt=0;
    int lock_errno;

    ctx = (plugins_t *) data;
    msg = (rdbmsg_internal_msg_t *) user_ptr;

    msg->rc = 0; // reset our flag in case it's set
    // Call below also populate pvt message queue pointer, if available
    check_if_subscribed (ctx->msg_dispatch_root, user_ptr, 0);

    if (msg->rc == RDBMSG_RC_IS_SUBSCRIBER) {
#ifdef USE_MSG_BUFFERS
        rdb_lock(ctx->empty_msg_store,__FUNCTION__);
        q = rdb_delete(ctx->empty_msg_store, 0, NULL);
        rdb_unlock(ctx->empty_msg_store,__FUNCTION__);
#else
        q = calloc(1,sizeof(rdbmsg_queue_t)) ;
#endif
        if (q == NULL) {
            goto emit_simple_err;
        }
        memset(q,0,sizeof(rdbmsg_queue_t));
        
        memcpy(&(q->msg), msg, sizeof (rdbmsg_msg_t));

        if ( !msg->msg_mutex ) { //legacy
            msg->msg_q_pool = ctx->msg_q_pool;
            msg->msg_mutex = &ctx->msg_mutex;
            msg->msg_condition = &ctx->msg_condition;
        }
        do {
            lock_rc = pthread_mutex_trylock(msg->msg_mutex);
            if (lock_rc != 0) {
                lock_errno = errno;
                usleep(0);
                if (lock_rc_try_cnt++) {
                    fwlog_no_emit(LOG_WARN, "Can't lock message queue for %s for %d cycles\n",
                            ctx->uname, lock_rc_try_cnt);
                }
            }
        } while (lock_rc != 0 && lock_errno == EBUSY);
        rdb_lock(msg->msg_q_pool,__FUNCTION__);
        // Insersion to a rdb FIFO/LIFO can not fail is 1 is not null, 
        // which we test for above. so in the name of speed, test is 
        // omitted.
        rc = rdb_insert(msg->msg_q_pool, q);
        if (rc == 0) {
            fwl_no_emit (LOG_ERROR, NULL, "rdb_insertion fail while sending %d to %s. message discarded\n",msg->id, ctx->uname);
            return RDB_CB_OK;
            //goto emit_err;
        }
        __sync_fetch_and_add(&ctx->msg_pending_count, 1);
#ifdef MSG_ACCOUNTING
        ctx->msg_rx_count++;
#endif
        rdb_unlock(msg->msg_q_pool,__FUNCTION__);
        pthread_mutex_unlock(msg->msg_mutex);

        msg->emitted++;

        if (ctx->msg_pending_count > wake_count_limit) {
        pthread_mutex_lock(msg->msg_mutex);
        pthread_cond_signal(msg->msg_condition);
        pthread_mutex_unlock(msg->msg_mutex);
        }
        if (ctx->msg_pending_count > 40000) {
            printf("* %d - ",ctx->msg_pending_count);
            usleep(0);
            printf("%d\n",ctx->msg_pending_count);
        }
        fwl_no_emit (LOG_DEBUG_MORE, NULL, "msg emitted to %s (id=%d %s)\n",
                ctx->uname, q->msg.id, rdbmsg_lookup_string ( q->msg.id ) );
        return RDB_CB_OK; // no need to continue testing ths plug in
    } //else info("%s not subscribed (id=%d)\n",ctx->name, msg->id);


//emit_simple_ok:
    return RDB_CB_OK;

emit_simple_err:
    //TODO: do we want to abort? report error somehow?
    fwlog_no_emit (LOG_ERROR, "out of message buffers sending %d to %s\n",msg->id, ctx->uname);
    return RDB_CB_OK;
}

int rdbmsg_emit_simple(int from, int to, int group, int id, int data){
    
    rdbmsg_internal_msg_t msg;

    //TODO: make sure all are within their respected range
    rdb_lock(ppc,__FUNCTION__);
    debug("-- %d.%d.%d.%d\n", from, to, group, id);
    msg.from = from;
    msg.to = to;
    msg.group = group;
    msg.id = id;
    msg.len = data;
    msg.data = NULL;
    msg.legacy = 0;
    msg.rc = 0;
    msg.emitted=0;
    msg.msg_q_pool = NULL;
    msg.msg_mutex = NULL;
    msg.msg_condition = NULL;
    msg.emit_ns = clock_gettime_uns();
    rdb_iterate(ppc,  0, emit_simple_cb, (void *) &msg, NULL, NULL); 
    rdb_unlock(ppc,__FUNCTION__);

    return (msg.emitted);
}

int emit_cb (void *data, void *user_ptr) {
    plugins_t *ctx;
    rdbmsg_internal_msg_t *msg;
    rdbmsg_queue_t *q;
    int rc;
    void *ptr = NULL;
    int lock_rc;
    int lock_rc_try_cnt=0;
    int lock_errno;

    ctx = (plugins_t *) data;
    msg = (rdbmsg_internal_msg_t *) user_ptr;

    if (!ctx) return RDB_CB_OK;
    if (!ctx->msg_dispatch_root) return RDB_CB_OK;

    msg->rc = 0; // reset our flag in case it's set
    check_if_subscribed (ctx->msg_dispatch_root, user_ptr, 0);

    if (msg->rc == RDBMSG_RC_IS_SUBSCRIBER) {
        if ( RDBMSG_USE_UNIQUE_FWALLOC == msg->use_fwalloc ) {
            ptr = rdbfw_alloc_no_emit (msg->len);
        } else if ( RDBMSG_USE_SHARED_FWALLOC == msg->use_fwalloc ) {
            if ( msg->data_ref ) {
                if ( -1  == rdbfw_up_ref ( msg->data_ref, 1 ) ){
                    fwlog_no_emit ( LOG_ERROR, "Fail to up_ref, message not sent!\n" );
                    return RDB_CB_OK;
                }
                ptr = msg->data_ref;
            }
            else {
                ptr = msg->data_ref = rdbfw_alloc_no_emit ( msg->len );
            }
        } else {
            ptr = malloc (msg->len);
        }
        if (ptr != NULL) {
            memcpy (ptr, msg->data, msg->len);
            msg->data = ptr;
        } 
        else {
            //TODO: Alloc!!!
            fwlog_no_emit (LOG_ERROR, "out of memory sending %d to %s. message discarded\n",msg->id, ctx->uname);
            q = NULL; // ensure we don't free a random pointer
            goto emit_err;
        }

#ifdef USE_MSG_BUFFERS
        rdb_lock(ctx->empty_msg_store,__FUNCTION__);
        q = rdb_delete(ctx->empty_msg_store, 0, NULL);
        rdb_unlock(ctx->empty_msg_store,__FUNCTION__);
#else
        q = calloc(1,sizeof(rdbmsg_queue_t)) ;
#endif
        if (q == NULL) {
            fwlog_no_emit (LOG_ERROR, "out of message buffers sending %d to %s\n",msg->id, ctx->uname);
            goto emit_err;
        }
        memset(q,0,sizeof(rdbmsg_queue_t));
        
        memcpy(&(q->msg), msg, sizeof (rdbmsg_msg_t));

        if ( !msg->msg_mutex ) { //legacy
            msg->msg_q_pool = ctx->msg_q_pool;
            msg->msg_mutex = &ctx->msg_mutex;
            msg->msg_condition = &ctx->msg_condition;
        }
        do {
            lock_rc = pthread_mutex_trylock(msg->msg_mutex);
            if (lock_rc != 0) {
                lock_errno = errno;
                usleep(0);
                if (lock_rc_try_cnt++) {
                    fwlog_no_emit(LOG_WARN, "Can't lock message queue for %s for %d cycles\n",
                            ctx->uname, lock_rc_try_cnt);
                }
            }
        } while (lock_rc != 0 && lock_errno == EBUSY);
        rdb_lock(msg->msg_q_pool,__FUNCTION__);
        // Insersion to a rdb FIFO/LIFO can not fail is 1 is not null, 
        // which we test for above. so in the name of speed, test is 
        // omitted.
        rc = rdb_insert(msg->msg_q_pool, q);
        if (rc == 0) {
            fwl_no_emit (LOG_ERROR, NULL, "rdb_insertion fail while sending %d to %s. message discarded\n",msg->id, ctx->name);
            goto emit_err;
        }
        __sync_fetch_and_add(&ctx->msg_pending_count, 1);
#ifdef MSG_ACCOUNTING
        ctx->msg_rx_count++;
#endif
        rdb_unlock(msg->msg_q_pool,__FUNCTION__);
        pthread_mutex_unlock(msg->msg_mutex);

        msg->emitted++;

        if (ctx->msg_pending_count > wake_count_limit) {
        pthread_mutex_lock(msg->msg_mutex);
        pthread_cond_signal(msg->msg_condition);
        pthread_mutex_unlock(msg->msg_mutex);
        }
        if (ctx->msg_pending_count > 40000) {
            printf("* %d - ",ctx->msg_pending_count);
            usleep(0);
            printf("%d\n",ctx->msg_pending_count);
        }
        fwl_no_emit (LOG_DEBUG_MORE, NULL, "msg emitted to %s (id=%d %s)\n",
                ctx->uname, q->msg.id, "**");
        return RDB_CB_OK; // no need to continue testing ths plug in
    } //else info("%s not subscribed (id=%d)\n",ctx->name, msg->id);


//emit_simple_ok:
    return RDB_CB_OK;

emit_err:
    //TODO: do we want to abort? report error somehow?
    if (ptr) {
        if ( msg->use_fwalloc ) {
            rdbfw_free ( ptr );
        } 
        else {
            free (ptr);
        }
    } 

    if (q)  {
#ifdef USE_MSG_BUFFERS
        rdb_lock(ctx->empty_msg_store,__FUNCTION__);
        rdb_insert(ctx->empty_msg_store, q);
        rdb_unlock(ctx->empty_msg_store,__FUNCTION__);
#else
        free (q);
#endif
    }
    return RDB_CB_OK;
}

// emit: when emitting message, the 'data' section is allocated and copied by the send fn();
// sender need to dispose of it's copy, if appropriate.
// receiver is responsible to free after usage
//
int rdbmsg_emit_log (int from, int to, int group, int id, int length, void *data, int unlock){
    uint8_t *data_cp;
    data_cp = malloc (length);
    memcpy (data_cp, data, length);
    if ( unlock ) {
        pthread_mutex_unlock(&log_mutex); 
    }
    int rc;

    rc = rdbmsg_emit( from, to, group, id, length, data_cp, NULL);
    
    free (data_cp);

    return (rc);
}

int rdbmsg_emit (int from, int to, int group, int id, int length, void *data, void (*data_cleanup)(void *) ) {
    
    rdbmsg_internal_msg_t msg;

    //TODO: make sure all are within their respected range
    if ( NULL != ppc ) {
        rdb_lock(ppc,__FUNCTION__);
        debug("-- %d.%d.%d.%d\n", from, to, group, id);
        msg.from = from;
        msg.to = to;
        msg.group = group;
        msg.id = id;
        msg.len = length;
        msg.data = data;
        msg.legacy = 0;
        msg.rc = 0;
        msg.emitted=0;
        msg.msg_q_pool = NULL;
        msg.msg_mutex = NULL;
        msg.msg_condition = NULL;
        msg.data_cleanup = data_cleanup;
        msg.emit_ns = clock_gettime_uns();
        msg.data_ref = NULL;
        rdb_iterate(ppc,  0, emit_cb, (void *) &msg, NULL, NULL); 
        rdb_unlock(ppc,__FUNCTION__);
    }

    return (msg.emitted);
}

void unlink_rdbmsg_cb (void *data, void *user_ptr) {
    rdbmsg_dispatch_t *d;
    d = (rdbmsg_dispatch_t *) data;

    if (d) {
        free(d);
        d = NULL;
    }
    return;
}


int destroy_cb (void *data, void *user_ptr) {
    rdbmsg_dispatch_t        *d;
    sfu_t                   *sfu;
    rdbmsg_internal_msg_t    *msg;

    d = (rdbmsg_dispatch_t *) data;
    sfu = (sfu_t *) user_ptr;
    msg = (rdbmsg_internal_msg_t *) sfu->user_ptr;

    rdbmsg_destroy_tree(d->next, msg, sfu->stage);
    rdb_drop_pool (d->next);
    
    //if (msg->rc == RDBMSG_RC_IS_SUBSCRIBER) return RDB_CB_ABORT;
    /*else*/ return RDB_CB_OK;
   
} 
void rdbmsg_destroy_tree (void *data, void *user_ptr, int stage) {
    rdb_pool_t *pool;
    sfu_t sfu;

    pool = (rdb_pool_t *) data;
  
    // this will be skipped if no DEBUG is defined, as this test is only needed
    // in debugging context.
    assert (stage >= 0 && (stage < 4));
    
    if ( stage >= 0 && stage < 3 ) {
        sfu.stage = stage + 1;
        sfu.user_ptr = user_ptr;

        if (pool && pool->root[0]) {
            rdb_iterate(pool,  0, destroy_cb, &sfu, NULL, NULL); 
            pool->drop=1;
        }
        else return;
    }


    // can no longer log here as it'll emit UT message which from the log which will cause
    // circular deadlock
    //
    //fwlog (LOG_DEBUG, "flushing %s %d\n", pool->name, stage);
    //fwlog (LOG_INFO, "--Dropping pool %s\n", pool->name);
    if (pool && pool->root[0]) rdb_flush(pool, unlink_rdbmsg_cb, NULL);
    pool->drop=1;
    return;

}

// iterate the modules.
int rdbmsg_clean_cb (void *data, void *user_ptr) {
    plugins_t *ctx;

    ctx = (plugins_t *) data;

    fwl_no_emit (LOG_DEBUG, NULL, "Cleaning %s\n", ctx->uname);
    rdbmsg_destroy_tree (ctx->msg_dispatch_root, NULL, 0);

    return RDB_CB_OK;

}
void rdbmsg_clean(void){
    rdb_lock(ppc,__FUNCTION__);
    rdb_iterate(ppc,  0, rdbmsg_clean_cb, NULL, NULL, NULL); 
    rdb_unlock(ppc,__FUNCTION__);
}

