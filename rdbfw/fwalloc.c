//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>

#include "rDB.h"
//#include "vr.h"
#include "log.h"
#include "fwalloc.h"

//#define FWALLOC_BYPASS

#ifdef FWALLOC_TESTS
uint32_t log_level = LOG_INFO;
pthread_mutex_t  log_mutex;


int main (int argc, char *argv[]) {
    int rc;
    uint8_t *ptr[3];

    logger = stdout;
    fwlog(LOG_INFO, "Starting fwalloc tests\n");
    rdbfw_alloc_init();
    fwlog(LOG_INFO, "fwalloc init OK\n");
    rc = rdbfw_alloc_prealloc (MTU, 1000, 1000, 100);
    if ( rc == -1 ) {
        fwlog (LOG_ERROR, "Failed prealloc (MTU)\n");
        exit(1);
    }
    rc = rdbfw_alloc_prealloc (100, 1000, 1000, 100);
    if ( rc == -1 ) {
        fwlog (LOG_ERROR, "Failed prealloc (100)\n");
        exit(1);
    }
    ptr[0] = rdbfw_alloc(MTU);
    *ptr[0] = 0;
    ptr[1] = rdbfw_alloc(100);
    *ptr[1] = 0;
    ptr[2] = rdbfw_alloc(500);
    *ptr[2] = 0;
    rdbfw_free (ptr[0]);
    rdbfw_free (ptr[1]);
    rdbfw_free (ptr[2]);
    rdb_free_prealloc();
    fwlog(LOG_INFO, "fwalloc free OK\n");

    exit(0);
}
#endif //FWALLOC_TESTS

pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
#ifndef FWALLOC_BYPASS 
static rdb_pool_t *alloc_master_pool = NULL;

static int superblocks_allocated = 0;
#endif

int rdbfw_alloc_init(void) {
#ifdef FWALLOC_BYPASS 
    return 0;
#else
    if (unittest_en == UT_ALLOC_INIT_1 || alloc_master_pool != NULL) {
        fwlog ( LOG_WARN, "rdbfw_alloc_init called while initilized. call ignord\n" );
        return -1;
    }

    pthread_mutex_lock(&alloc_mutex);
    if (unittest_en != UT_ALLOC_INIT_2) {
        alloc_master_pool = rdb_register_um_pool ("master alloc pool", 1, 0, 
                RDB_KUINT32 | RDB_KASC | RDB_BTREE, NULL );
    }
    pthread_mutex_unlock(&alloc_mutex);

    if (alloc_master_pool == NULL) {
        fwlog ( LOG_ERROR, "Failed to register master alloc pool\n");
        return -1;
    }
        
    return 0;
#endif
}

#define ALLOC_NAME_LN 24

int compare_block(void *old, void *new) {
    /*block_key_t *kta, *ktb;
    ktb = old;
    kta = new;

    fwlog(LOG_DEBUG, "%p == %p %d! %p == %p %d!\n", kta->sb, ktb->sb,
            (kta->sb < ktb->sb) ? -1 : (kta->sb > ktb->sb) ? 1 : 0,
            kta->data, ktb->data,
            (kta->data < ktb->data) ? -1 : (kta->data > ktb->data) ? 1 : 0);
      */      //(ia < ib) ? -1 : (ia > ib) ? 1 : (off_a < off_b) ? -1 : (off_a > off_b) ? 1 : 0);
    
    return ((((block_key_t *) new)->sb < ((block_key_t *) old)->sb) ? -1 : (((block_key_t *) new)->sb > ((block_key_t *) old)->sb) ? 1 : (((block_key_t *) new)->data_addr < ((block_key_t *) old)->data_addr) ? -1 : (((block_key_t *) new)->data_addr > ((block_key_t *) old)->data_addr) ? 1 : 0); 
    //return ((kta->sb < ktb->sb) ? -1 : (kta->sb > ktb->sb) ? 1 : (kta->data_addr < ktb->data_addr) ? -1 : (kta->data_addr > ktb->data_addr) ? 1 : 0); 

} 

/* How out 'allocator' works:
 * gross_sz == the grodss allocated block size. it includes our header, alligbnment gap if needed, and the used requested data length
 *
 * am (alloc_master) pool holds index on sizes for which we allocasted superblocks
 * am->sb_sz holds cound size elements (including headers)
 * am->gsz include the superblock headers as well. (one SB header per superblock, count block headers + data)
 *
 * each superblock is allocated as one *alloc(), it's content divided into separate 
 *
 * am points to all individual blocks (free or used pool). eaxchg block points to it's super block, anf thus to him main AM record as well.
 *
 * */
int rdbfw_alloc_prealloc (uint32_t sz, uint32_t count, uint32_t max_count, int hint_sb_sz) {
#ifdef FWALLOC_BYPASS 
    return 0;
#else
    char buf[ALLOC_NAME_LN];
    int i,j,k;
    alloc_master_t *am;
    //int sb_sz;
    int gross_sz;
    int alignment = sizeof (void *);
    int add_on = 0;

    gross_sz = sz + offsetof (block_t, data);
    gross_sz = gross_sz / alignment * alignment + ((gross_sz % alignment != 0) * alignment);  // ensure mem allighment

    // UT
    if (unittest_en == UT_ALLOC_PRE_1) {
        fwl (LOG_DEBUG, NULL, "prealloc_1 unittets abort\n");
        goto rdbfw_alloc_prealloc_err;
    }
    //Sanity
    if ( sz > MAX_RDBFW_ALLOC_SZ || sz == 0 ) {
        fwl (LOG_ERROR, NULL, "Sanity: Failed to prealloc memory - called with sz:%" PRIu32 ", ct: %" PRIu32
                ", max_ct %" PRIu32 ", pool = %p\n", sz, count, max_count, alloc_master_pool); 
        goto rdbfw_alloc_prealloc_err;
    }

    pthread_mutex_lock(&alloc_mutex);
    am = rdb_get (alloc_master_pool, 0, &sz);

    if (am == NULL) {
        //new reaquest
        if ( sz > MAX_RDBFW_ALLOC_SZ ||
                count > max_count ||
                ! max_count ||
                NULL == alloc_master_pool) {
            fwl (LOG_ERROR, NULL, "Failed to prealloc memory - called with sz:%" PRIu32 ", ct: %" PRIu32 
                    ", max_ct %"PRIu32", pool = %p\n", sz, count, max_count, alloc_master_pool); 
            goto rdbfw_alloc_prealloc_err;
        }
        if (unittest_en != UT_ALLOC_PRE_2) {
            am = calloc (1,sizeof (alloc_master_t));
        }
        if (am == NULL) {
            fwl (LOG_ERROR, NULL, "calloc-error\n");
            goto rdbfw_alloc_prealloc_err;
        }
        am->sb_sz = count * gross_sz; // everything that live in it
        am->sb_gsz = am->sb_sz +  offsetof(superblock_t, data);
        am->sb_cnt = 1;
        am->max_blocks_allowed = max_count;
        /*if (hint_sb_sz) {
            am->hint_sb_sz = hint_sb_sz;
        }*/
    
        // calculate superblock size and quantity

        if ( hint_sb_sz < ( gross_sz + offsetof(superblock_t, data) ) ||
                ( hint_sb_sz > MAX_RDBFW_SUPERALLOC_SZ ) ) {
            hint_sb_sz = MAX_RDBFW_SUPERALLOC_SZ;
        }
        if ( am->sb_gsz > hint_sb_sz ) {
            // Too big - we ned to split into multiple blocks
            // calculate size by number
            int fit_in_sb;
            fwl(LOG_DEBUG, NULL, "%d-%d\n", (hint_sb_sz - (int) offsetof(superblock_t, data)) , gross_sz);
            fit_in_sb = (hint_sb_sz - offsetof(superblock_t, data)) / gross_sz;
            am->sb_cnt = count / fit_in_sb + ( count % fit_in_sb != 0) ;

            //am->sb_cnt = am->sb_sz / MAX_RDBFW_SUPERALLOC_SZ + 
            //    ( ( am->sb_sz % MAX_RDBFW_SUPERALLOC_SZ ) != 0);
            //am->sb_sz = count * gross_sz / am->sb_cnt + ( ( ( count * gross_sz ) % am->sb_cnt ) != 0);
            am->sb_sz = fit_in_sb * gross_sz;
            am->sb_gsz = am->sb_sz +  offsetof(superblock_t, data);
        }
        am->sb_item_cnt = am->sb_sz / gross_sz;

        am->block_sz = sz;
        am->blocks_free = count;

        fwl (LOG_DEBUG, NULL, "sb_cnt = %d\tsb_sz = %d(g=%d)\tsb_item_cnt = %d\t block_sz = %d\t, gross_sz = %d\n",
                am->sb_cnt, am->sb_sz, am->sb_gsz, am->sb_item_cnt, am->block_sz, gross_sz);

        // Register pools 
        snprintf( buf, ALLOC_NAME_LN, "alloc.%" PRIu32 ".super", sz );

        if (unittest_en != UT_ALLOC_PRE_3) {
            am->superblock_pool = rdb_register_um_pool (buf, 1, 
                offsetof(superblock_t, data_addr) - offsetof ( superblock_t, block_sz),
                RDB_KPTR | RDB_KASC | RDB_BTREE, NULL );
        }
        if ( am->superblock_pool == NULL) {
            fwl ( LOG_ERROR, NULL, "Failed to register superblock pool\n");
            free (am);
            goto rdbfw_alloc_prealloc_err;
        }

        snprintf( buf, ALLOC_NAME_LN, "alloc.%" PRIu32 ".free", sz );

        // free pool must be of type LIFO so we can 'undo' allocation in case of failure
        if (unittest_en != UT_ALLOC_PRE_4) {
            am->free_pool = rdb_register_um_pool (buf, 1, 0, 
                RDB_KLIFO | RDB_NO_IDX | RDB_BTREE, NULL );
        }
        if ( am->free_pool == NULL) {
            fwl ( LOG_ERROR, NULL, "Failed to register free pool\n");
            rdb_drop_pool (am->superblock_pool);
            free (am);
            goto rdbfw_alloc_prealloc_err;
        }

        snprintf( buf, ALLOC_NAME_LN, "alloc.%" PRIu32 ".used", sz );
    }
    else {
        add_on = 1;
        am->sb_cnt++;
    }
    
    if ( !add_on ) {
        if (unittest_en == UT_ALLOC_PRE_5 || 0 >= rdb_insert (alloc_master_pool, am )) {
            fwl (LOG_ERROR, NULL, "Unable to insert alloc-master record\n");
            rdb_drop_pool (am->free_pool);
            rdb_drop_pool (am->superblock_pool);
            free (am);
            goto rdbfw_alloc_prealloc_err;
        }
    }

    // in case of add-on we only allocate one superblock
    for ( i = 0 ; i < ((add_on==1) ? 1 : am->sb_cnt) ; i++ ) {
        superblock_t *sb = NULL;
        //superblock_all_t *sb_all;
        
        if (unittest_en != UT_ALLOC_PRE_6) {
            sb = calloc (1, am->sb_gsz);
        }
        if (sb == NULL) {
            fwl ( LOG_ERROR, NULL, "Failure to allocate superblock of %d bytes\n", am->sb_gsz );
            if (add_on) {
                am->sb_cnt--;
            }
            // TODO: unwind ?
            if (!add_on) {
                rdb_delete (alloc_master_pool, 0, &sz);
                rdb_drop_pool (am->free_pool);
                rdb_drop_pool (am->superblock_pool);
                free (am);
            }
            goto rdbfw_alloc_prealloc_err;
        }
        fwl (LOG_DEBUG, NULL, "sb == %p\n",sb);
        /*sb_all = calloc (1, sizeof (superblock_all_t));
        if (sb_all == NULL) {
            fwlog ( LOG_ERROR, "Failure to allocate superblock(all) of %d bytes\n", am->sb_sz );
            if (add_on) {
                am->sb_cnt--;
            }
            goto rdbfw_alloc_prealloc_err;
        }*/
        fwl (LOG_DEBUG, NULL, "SB.%d @%p\n", i, sb);
        superblocks_allocated++;
        sb->block_sz = sz;
        sb->am = am;
        sb->data_addr = (void *) sb + offsetof (superblock_t, data);
        if ( unittest_en == UT_ALLOC_PRE_7 || 0 == rdb_insert (am->superblock_pool, sb ) ) {
            fwl (LOG_ERROR, NULL, "Unable to insert superblock record\n");
            free ( sb );
            if (!add_on) {
                rdb_delete (alloc_master_pool, 0, &sz);
                rdb_drop_pool (am->free_pool);
                rdb_drop_pool (am->superblock_pool);
                free (am);
            }
            goto rdbfw_alloc_prealloc_err;
        }

        block_t *blk;
        blk = sb->data_addr;
        if (add_on) {
            am->blocks_free += am->sb_item_cnt;
        }
        for ( j = 0 ; j < am->sb_item_cnt ; j++ ) {
            blk = (void *) &sb->data_addr + ( j * gross_sz );
            blk->sb = sb;
            blk->data_addr = (void *) blk + offsetof (block_t, data);
            //fwlog (LOG_DEBUG, "sizeof offset %p %p %d\n",blk, blk->data, offsetof (block_t, data));
            if ( unittest_en == UT_ALLOC_PRE_8 || 0 == rdb_insert (am->free_pool, blk ) ) {
                fwl (LOG_ERROR, NULL, "Unable to insert block record\n");
                if (j) for ( k = 0 ; k < j; k++) {
                    // undo
                    rdb_delete (am->free_pool, 0, NULL);
                }
                rdb_delete (am->superblock_pool, 0, &sb->data_addr);
                free (sb);
                sb = NULL;
                if (add_on) {
                    am->sb_cnt--;
                }
                if (!add_on) {
                    rdb_delete (alloc_master_pool, 0, &sz);
                    rdb_drop_pool (am->free_pool);
                    rdb_drop_pool (am->superblock_pool);
                    free (am);
                }
                goto rdbfw_alloc_prealloc_err;
            }
            fwl (LOG_DEBUG_MORE, NULL, "SBd.%d.%d @%p (data @%p, SB=%p) \n", i, j, blk, blk->data_addr,blk->sb);
        }
    }
    /*if ( ( !add_on ) && 0 == rdb_insert (alloc_master_pool, am ) ) {
        fwlog (LOG_ERROR, "Unable to insert alloc-master record\n");
        goto rdbfw_alloc_prealloc_err;
    }*/

    pthread_mutex_unlock(&alloc_mutex);
    return 0;
            
rdbfw_alloc_prealloc_err:
    pthread_mutex_unlock(&alloc_mutex);
    return -1;
#endif
}

void *rdbfw_alloc_no_emit(uint32_t size){
#ifdef FWALLOC_BYPASS 
    return malloc (size);
#else
    alloc_master_t *am, *am_prev, *am_next;
    block_t  *blk_next=NULL;

    pthread_mutex_lock(&alloc_mutex);

    am = rdb_get_neigh ( alloc_master_pool , 0, &size, (void **) &am_prev, (void **) &am_next );

    if ( am != NULL || am_next != NULL) {
        if ( ! am ) {
            // no exacgt match = fake it using higher up;
            am = am_next;
        }
        // we have exact match
        // TODO: will rdb_iterate be faster (abort on 1st entry)
        
        blk_next = rdb_delete ( am->free_pool, 0 , NULL );
        fwlog_no_emit (LOG_DEBUG,"Got %p\n", blk_next);
        if ( blk_next == NULL && ( am->max_blocks_allowed > am->blocks_in_use ) ) {
            // we ran out - go alloc first
            fwlog_no_emit (LOG_DEBUG_MORE,"More %d of %d!\n",am->blocks_in_use, am->max_blocks_allowed);
            pthread_mutex_unlock(&alloc_mutex);
            rdbfw_alloc_prealloc (am->block_sz, 0, 0, 0);
            pthread_mutex_lock(&alloc_mutex);
            blk_next = rdb_delete ( am->free_pool, 0 , NULL );
        }
        if ( blk_next != NULL ) {
            // we have a free unit
            fwlog_no_emit (LOG_DEBUG_MORE,"Free Unit at %p!\n", blk_next);
            
            fwlog_no_emit (LOG_DEBUG_MORE,"deliver %p SB=%p\n", &blk_next->data_addr,blk_next->sb);
            am->blocks_free--;
            am->blocks_in_use ++;
            pthread_mutex_unlock(&alloc_mutex);
            blk_next->ref_ct = 1;
            //fwlog_no_emit ( LOG_INFO, "RefSET %d %p\n", blk_next->ref_ct, &blk_next->ref_ct) ;
            return blk_next->data_addr;
            
        }
        else {
            fwlog_no_emit ( LOG_ERROR, "failed to allocate memory for new SB\n");
        }
    } 
    pthread_mutex_unlock(&alloc_mutex);
    fwlog_no_emit (LOG_WARN,"rdbfw_alloc failure\n");
    return NULL;
#endif
}

void *rdbfw_alloc(uint32_t size){
#ifdef FWALLOC_BYPASS 
    return malloc (size);
#else
    alloc_master_t *am, *am_prev, *am_next;
    block_t  *blk_next=NULL;

    pthread_mutex_lock(&alloc_mutex);

    am = rdb_get_neigh ( alloc_master_pool , 0, &size, (void **) &am_prev, (void **) &am_next );

    if ( am != NULL || am_next != NULL) {
        if ( ! am ) {
            // no exacgt match = fake it using higher up;
            am = am_next;
        }
        // we have exact match
        // TODO: will rdb_iterate be faster (abort on 1st entry)
        
        blk_next = rdb_delete ( am->free_pool, 0 , NULL );
        fwlog(LOG_DEBUG,"Got %p\n", blk_next);
        if ( blk_next == NULL && ( am->max_blocks_allowed > am->blocks_in_use ) ) {
            // we ran out - go alloc first
            fwlog(LOG_DEBUG_MORE,"More %d of %d!\n",am->blocks_in_use, am->max_blocks_allowed);
            pthread_mutex_unlock(&alloc_mutex);
            rdbfw_alloc_prealloc (am->block_sz, 0, 0, 0);
            pthread_mutex_lock(&alloc_mutex);
            blk_next = rdb_delete ( am->free_pool, 0 , NULL );
        }
        if ( blk_next != NULL ) {
            // we have a free unit
            fwlog(LOG_DEBUG_MORE,"Free Unit at %p!\n", blk_next);
            
            fwlog(LOG_DEBUG_MORE,"deliver %p SB=%p\n", &blk_next->data_addr,blk_next->sb);
            am->blocks_free--;
            am->blocks_in_use ++;
            pthread_mutex_unlock(&alloc_mutex);
            blk_next->ref_ct = 1;
            //fwl ( LOG_INFO, NULL, "RefSET %d %p\n", blk_next->ref_ct, &blk_next->ref_ct) ;
            return blk_next->data_addr;
            
        }
        else {
            fwlog ( LOG_ERROR, "failed to allocate memory for new SB\n");
        }
    } 
    pthread_mutex_unlock(&alloc_mutex);
    while (1) fwlog (LOG_WARN,"rdbfw_alloc failure\n");
    return NULL;
#endif
}

int rdbfw_up_ref ( void *ptr, int count ) {
#ifdef FWALLOC_BYPASS 
    return 0;
#else
    int tmp_ref_ct;
    block_t *blk;
    if (ptr) {
        blk =  ptr - offsetof (block_t, data);
    } else {
        fwl ( LOG_ERROR, NULL, "attempt to up-ref a null pointer\n");
        return -1;
    }
    if (count < 1) {
        fwl ( LOG_ERROR, NULL, "attempt to up-ref by an invalid count (%d)\n", count);
        return -1;
    }
    //fwl ( LOG_INFO, NULL, "Ref was %d %p\n", blk->ref_ct, &blk->ref_ct);
    tmp_ref_ct = __sync_add_and_fetch ( &blk->ref_ct, count );
    //fwl ( LOG_INFO, NULL, "Ref is now %d\n", tmp_ref_ct);
    return tmp_ref_ct;
#endif
}

void rdbfw_free(void *ptr){
#ifdef FWALLOC_BYPASS 
    free (ptr);
#else
    int tmp_ref_ct;
    block_t *blk;
    alloc_master_t *am;

    if (ptr) {
        blk =  ptr - offsetof (block_t, data);
    } else {
        fwlog (LOG_ERROR, "attempt to free a null pointer\n");
        return ;
    }
    pthread_mutex_lock(&alloc_mutex);
    tmp_ref_ct = __sync_sub_and_fetch ( &blk->ref_ct, 1 );

    if ( 0 == tmp_ref_ct ) {
        // we are zero - last ref - lets clean up for real
        fwl ( LOG_DEBUG_MORE, NULL, "(internal) free block of size %u at %p-%p\n", blk->sb->block_sz, blk, ptr);

    //    pthread_mutex_lock(&alloc_mutex);

        //fwlog ( LOG_DEBUG_MORE, "internal free %p\nsb=%p\n", ptr, blk->sb );

        am = blk->sb->am;
        if ( am == NULL ) {
            fwlog (LOG_ERROR, "Can't free block of size %u\n", blk->sb->block_sz);
            pthread_mutex_unlock(&alloc_mutex);
            return ;
        }

        rdb_insert (am->free_pool, blk);
        am->blocks_free ++;
        am->blocks_in_use --;
        pthread_mutex_unlock(&alloc_mutex);
        return ;
    }
    if ( tmp_ref_ct > 0 ) {
        fwlog ( LOG_DEBUG_MORE, "internal down_ref %d (free) %p\nsb=%p\n", tmp_ref_ct, ptr, blk->sb );
        pthread_mutex_unlock(&alloc_mutex);
        return;
        // we already reduced the ref-ct
    }
    else if ( tmp_ref_ct < 0 ) {
        fwl ( LOG_FATAL, NULL, "double free at fwalloc\n" );
        exit (1);
    }
    
#endif
}

static int superblock_delete_cb(void *data_ptr, void *user_ptr) {
    superblock_t *sb = ( superblock_t * ) data_ptr;
        fwlog (LOG_INFO, "sb != %p\n",sb);
    return RDB_CB_DELETE_NODE;
}

__attribute__((unused)) static int free_prealloc_cb(void *data_ptr, void *user_ptr) {
    alloc_master_t *am = (alloc_master_t *) data_ptr;

    if ( am->blocks_in_use ) {
        fwlog (LOG_ERROR, "free_prealloc called while blocks are still in usage\n");
        return RDB_CB_OK;
    }
    if ( ! am->blocks_free ) {
        fwlog (LOG_ERROR, "free_prealloc called while none are allocated ( size = %d )\n", am->block_sz);
        return RDB_CB_OK;
    }
    rdb_iterate ( am->superblock_pool, 0, superblock_delete_cb, NULL, NULL, NULL);
    rdb_drop_pool ( am->free_pool );
    rdb_drop_pool ( am->superblock_pool );

    fwlog (LOG_INFO, "freed superblock(s): %d\n", am->block_sz);
    return RDB_CB_DELETE_NODE;
    //return RDB_CB_OK;
}
void rdb_free_prealloc(void) {
#ifdef FWALLOC_BYPASS 
    return;
#else
    pthread_mutex_lock(&alloc_mutex);
    rdb_iterate ( alloc_master_pool, 0, free_prealloc_cb, NULL, NULL, NULL);
    rdb_drop_pool ( alloc_master_pool );
    alloc_master_pool = NULL;
    pthread_mutex_unlock(&alloc_mutex);
    return;
#endif
}
