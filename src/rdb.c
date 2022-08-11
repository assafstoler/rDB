//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

/*
 * @file rDB.c
 *
 * @brief Kernel module / userspace library
 * for rDB Ram DB - tree, list, and id services.
 *
 * @par Module:
 *
 *  This module instantiates a Ram based DB like structure indexing
 *  and handling for usage in the kernel
 *
 *
 * @author Assaf Stoler <assaf.stoler@gmail.com>
 * All Rights Reserved
 *
 * @date 30 Sep. 2008
 *
 * @par Function:
 *   This file implements the specifics of the rdb functions to be
 *   used by other kernel modules.
 *
 */
//#define DEBUG

#ifdef KM
// Build a Kernel Module

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <net/sock.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/genetlink.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include "rdb.h"

#define rdb_free(a) kfree(a)
#define rdb_alloc(a) kmalloc (a, GFP_KERNEL);

#else
// Build a User-Space Library

#include <stdio.h>                              //printf,
#include <stdlib.h>                             //exit,
#include <string.h>                             //strcmp,
#include <pthread.h>
#include "rdb.h"

#define rdb_free(a) free(a)
#define rdb_alloc(a) malloc (a);

#endif

#define PP_T  rdb_bpp_t

#ifdef USE_128_BIT_TYPES
#define __intmax_t __int128_t
#define __uintmax_t __uint128_t
#else 
#define __intmax_t int64_t
#define __uintmax_t uint64_t
#endif

/*typedef struct RDB_INDEX_DATA {
    int     segments;
    int     **key_offset;                        ///< we'll need a key offset for every index segment, so it must be dynamically allocated
    int     **flags;                            ///< flags for this pool (index head) or flags for this index continuation (field type)
} rdb_index_data_t;
*/


rdb_pool_t  *pool_root;


//struct  RDB_POOLS **poolIds;

#ifdef KM
//struct  RDB_POOLS **poolIdsTmp;
#endif

char      *rdb_error_string = NULL;

// used to calculate tree depth by dump Fn()
int     levels,
        maxLevels;



#ifdef KM
struct semaphore reg_mutex;
struct semaphore rdb_error_mutex;
#define rdb_sem_lock(A) down_interruptible(A)
#define rdb_sem_unlock(A) up(A)
#else
pthread_mutex_t reg_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rdb_error_mutex = PTHREAD_MUTEX_INITIALIZER;
#define rdb_sem_lock(A) pthread_mutex_lock(A)
#define rdb_sem_unlock(A) pthread_mutex_unlock(A)
#endif
// Initilize the rDB subsystem, must be called once before any other rDB 
// function
void rdb_init (void)
{
#ifdef KM
    sema_init(&reg_mutex, 1);
    sema_init(&rdb_error_mutex, 1);
#endif
    pool_root = NULL;
}

// Store internal error string to allow user to retrieve it, and return with
// user set value. typical use will look like
// Example: return rdv_error_value(-1, "failed to dance this dance");
// library user can later print that string to errno/errstr or similar.
int rdb_error_value (int rv, char *err)
{
    rdb_sem_lock(&rdb_error_mutex);
#ifdef KM

    if (rdb_error_string != NULL) {
        kfree (rdb_error_string);
        rdb_error_string = NULL;
    }

    rdb_error_string = kmalloc (strlen (err) + 1, GFP_KERNEL);
#else
    rdb_error_string = realloc (rdb_error_string, strlen (err) + 1);
#endif

    if (rdb_error_string == NULL)
        rdb_info ("rdb: failed to Alloc RAM for error message, original error was"
                " \"%s\"\n", err);
    else
        strcpy (rdb_error_string, err);

    rdb_sem_unlock(&rdb_error_mutex);

    return rv;
}

// Same as above, without returning a value
void rdb_error (char *err)
{
    rdb_sem_lock(&rdb_error_mutex);
#ifdef KM

    if (rdb_error_string != NULL)
        kfree (rdb_error_string);

    rdb_error_string = kmalloc (strlen (err) + 1, GFP_KERNEL);
#else
    rdb_error_string = realloc (rdb_error_string, strlen (err) + 1);
#endif

    if (rdb_error_string == NULL)
        rdb_info ("rdb: failed to Alloc RAM for error message, original error was"
                " \"%s\"\n", err);
    else
        strcpy (rdb_error_string, err);

    rdb_sem_unlock(&rdb_error_mutex);
}

// If you lost your pool handle, or more likely, you are working in a multi-
// threaded application, and you need to attach to a data pool you did not
// create, you can use this function to retrieve the matching rDB handle.
// * Remember to use locks on shared data pools.
rdb_pool_t *rdb_find_pool_by_name (char *poolName)
{
    rdb_pool_t *pool;

    if (pool_root != NULL) {
        pool = pool_root;

        while (pool != NULL) {
            if (strcmp (pool->name, poolName) == 0)
                return (pool);

            pool = pool->next;
        }
    }

    return (NULL);
}
// Print pool usage report
char * rdb_print_pool_stats (char *buf, int max_len)
{
    int used=0;
    
#ifdef RDB_POOL_COUNTERS
    rdb_pool_t *pool;
    int rc;

    if (pool_root != NULL) {
        pool = pool_root;

        while (pool != NULL) {
            rc = snprintf(buf + used, max_len - used, "Pool: %s : %d\n",
                    pool->name, pool->record_count );
            if ( rc > 0 ) {
                used += rc;
            } 
            else {
                rdb_error("Failure during error print\n");
                break;
            }

            pool = pool->next;
        }
    }
    snprintf(buf + used, max_len - used, ":END:");
#else
    snprintf(buf + used, max_len - used, "rDB pool record counting disabled");
#endif
    return (buf);
}
// rDB Internal. this will set the various compate functions for the rDB
// data maganment routines.
int set_pool_fn_pointers(rdb_pool_t *pool, int i, uint32_t flags, void *cmp_fn){

    if (cmp_fn) {
        pool->fn[i] = cmp_fn;
        pool->get_fn[i] = cmp_fn;
        pool->get_const_fn[i] = cmp_fn;
    } else if (flags & RDB_KINT32) {   
        pool->fn[i] = key_cmp_int32;
        pool->get_fn[i] = key_cmp_int32;
        pool->get_const_fn[i] = key_cmp_const_int32;
    } else if (flags & RDB_KUINT32){  
        pool->fn[i] = key_cmp_uint32;
        pool->get_fn[i] = key_cmp_uint32;
        pool->get_const_fn[i] = key_cmp_const_uint32;
    } else if (flags & RDB_KINT64) {
        pool->fn[i] = key_cmp_int64;
        pool->get_fn[i] = key_cmp_int64;
        pool->get_const_fn[i] = key_cmp_const_int64;
    } else if (flags & RDB_KUINT64) {
        pool->fn[i] = key_cmp_uint64;
        pool->get_fn[i] = key_cmp_uint64;
        pool->get_const_fn[i] = key_cmp_const_uint64;
    } else if (flags & RDB_KINT16) {
        pool->fn[i] = key_cmp_int16;
        pool->get_fn[i] = key_cmp_int16;
        pool->get_const_fn[i] = key_cmp_const_int16;
    } else if (flags & RDB_KUINT16) {
        pool->fn[i] = key_cmp_uint16;
        pool->get_fn[i] = key_cmp_uint16;
        pool->get_const_fn[i] = key_cmp_const_uint16;
    } else if (flags & RDB_KINT8) {
        pool->fn[i] = key_cmp_int8;
        pool->get_fn[i] = key_cmp_int8;
        pool->get_const_fn[i] = key_cmp_const_int8;
    } else if (flags & RDB_KUINT8) {
        pool->fn[i] = key_cmp_uint8;
        pool->get_fn[i] = key_cmp_uint8;
        pool->get_const_fn[i] = key_cmp_const_uint8;
    } else if (flags & RDB_KSIZE_t) {
        pool->fn[i] = key_cmp_size_t;
        pool->get_fn[i] = key_cmp_size_t;
        pool->get_const_fn[i] = key_cmp_const_size_t;
    } else if (flags & RDB_KSSIZE_t) {
        pool->fn[i] = key_cmp_ssize_t;
        pool->get_fn[i] = key_cmp_ssize_t;
        pool->get_const_fn[i] = key_cmp_const_ssize_t;
    } else if (flags & RDB_KPTR) {
        pool->fn[i] = key_cmp_ptr;
        pool->get_fn[i] = key_cmp_const_ptr;
        pool->get_const_fn[i] = key_cmp_const_ptr;
    } 
#ifdef USE_128_BIT_TYPES
      else if (flags & RDB_KINT128) {
        pool->fn[i] = key_cmp_int128;
        pool->get_fn[i] = key_cmp_int128;
        pool->get_const_fn[i] = key_cmp_const_int128;
    } else if (flags & RDB_KUINT128) {
        pool->fn[i] = key_cmp_uint128;
        pool->get_fn[i] = key_cmp_uint128;
        pool->get_const_fn[i] = key_cmp_const_uint128;
    } 
#endif
      else if (flags & RDB_KSTR) {
        pool->fn[i] = key_cmp_str;
        pool->get_fn[i] = key_cmp_str; 
        pool->get_const_fn[i] = key_cmp_str; 
    } else if (flags & RDB_KPSTR) {
        pool->fn[i] = key_cmp_str_p;
        pool->get_fn[i] = key_cmp_const_str_p;
        pool->get_const_fn[i] = key_cmp_const_str_p;
    //else if (FLAGS & RDB_KTME)    pool->fn[0] = keyCompareTME;
    //else if (FLAGS & RDB_KTMA)    pool->fn[0] = keyCompareTMA;
    } else if (flags & RDB_NOKEYS) {
        pool->fn[i] = NULL;
        pool->get_fn[i] = NULL;
        pool->get_const_fn[i] = NULL;
    } else {
        return -1;
    }
    return 0;
}

// rDB Iternal: drop a new pool from our pool chain
void rdb_drop_pool (rdb_pool_t *pool) {
    rdb_pool_t *prev, *next;

    if (pool && pool->name); // info("dropping %s\n", pool->name);
    else return;

    next=pool->next;
    prev=pool->prev;

    if (prev) {
        prev->next = pool->next;
    }
    if (next) {
        next->prev = pool->prev;
    }
    if (pool_root == pool) {
        if (prev) {
            pool_root = prev;
        } else if (next) {
            pool_root = next;
        } else {
            pool_root = NULL;
        }
    }

    if (pool->name) {
        //info ("freeing %s\n", pool->name);
        rdb_free (pool->name);
    }

    if (pool) {
        rdb_free(pool) ;
        pool = NULL;
    }

    return;
}

// rDB Iternal: Add a new pool to our pool chain
rdb_pool_t *rdb_add_pool (
        char *poolName, 
        int indexCount, 
        int key_offset, 
        int FLAGS, 
        void *compare_fn) {

    rdb_pool_t *pool;
    int name_length;

    pool = rdb_alloc(sizeof (rdb_pool_t));

    if (pool == NULL) {
        rdb_error ("Fatal: Pool allocation error, out of memory");
        return NULL;
    }

    memset (pool, 0, sizeof (rdb_pool_t));

    // inserting myself to the top of the pool, why? 
    // because it is easy, don't need to look for the end of the pool chain
    pool->next = pool_root;

    if (pool_root)
        pool_root->prev = pool;

    pool_root = pool;
    name_length = strlen (poolName);
    pool->name = rdb_alloc (name_length + 1);

    if (pool->name == NULL) {
        rdb_error ("rDB: Fatal: pool allocation error, out of memor for pool"
                " name");
        pool_root = pool->next;
        rdb_free (pool);
        return NULL;
    }

    strncpy (pool->name, poolName, name_length);
    pool->name[name_length] = 0;
    
    if (-1 == set_pool_fn_pointers(pool, 0, FLAGS, compare_fn)){
        rdb_error ("rDB: Fatal: pool registration without type or matching"
                " compare fn");
        pool_root = pool->next;
        rdb_free (pool);
        return NULL;
    }

    pool->root[0] = NULL;
    pool->key_offset[0] = sizeof (PP_T) * indexCount + key_offset;

    debug ("pool->key_offset=%d\n", pool->key_offset[0]);

    pool->indexCount = indexCount;
    pool->FLAGS[0] = FLAGS;

    debug ("pool %s, FLAGS=%xn", pool->name, pool->FLAGS[0]);
#ifdef KM
    sema_init(&pool->read_mutex, 1);
    sema_init(&pool->write_mutex, 1);
#else
    pthread_mutex_init(&pool->read_mutex, NULL); 
    pthread_mutex_init(&pool->write_mutex, NULL); 
#endif
    return pool;
}

// Register data pool with rDB, returns a pool handler to be used with future
// function calls.
// key_offset is the offset form the start of the user data, ignoring the pp_t[]
// at the start of the structure - however it is calculated and stored as offset
// from the top of the structure.

rdb_pool_t *rdb_register_um_pool (
        char *poolName, 
        int idxCount, 
        int key_offset, 
        int FLAGS, 
        void *fn) {

    rdb_pool_t *pool;

    //TODO:kernel frindly locks
    rdb_sem_lock(&reg_mutex);

    if (rdb_find_pool_by_name (poolName) != NULL) {
        rdb_error ("rDB: Fatal: Duplicte pool name in rdb_register_pool");
        pool = NULL;
    } else {
        pool = rdb_add_pool (poolName, idxCount, key_offset, FLAGS, fn);
    }
    rdb_sem_unlock(&reg_mutex);

    return pool;
}

// Remove rDB traces. use before exit() or when rDB no longer needed.
// Use rdb_init after, to re_start rDB

// rdb_gc: drop pools that ae marged for dropping. pools must be empty first.
void rdb_gc() {
    rdb_clean(1);
}

void rdb_clean(int gc) {
    rdb_pool_t  *pool, 
                *pool_next;

    if (pool_root != NULL) {
        pool = pool_root;

        while (pool != NULL) {
            pool_next = pool->next;

            if (gc == 0 || (gc == 1 && pool->drop)) { 
                rdb_drop_pool (pool);
            }

            pool = pool_next;
        }
    }

    if (!gc && rdb_error_string) {
        rdb_free (rdb_error_string);
        rdb_error_string = NULL;
    }

    pool_root = NULL;

    return ;
}

void rdb_print_pools(void *out) {
    rdb_pool_t  *pool, 
                *pool_next;

    if (pool_root != NULL) {
        pool = pool_root;

        while (pool != NULL) {
            pool_next = pool->next;
            //rdb_flush(pool,NULL,NULL);
#ifdef KM
            printk("%s\n", pool->name);
#else
            FILE *fp = out;
            fprintf(fp,"%s\n", pool->name);
#endif
            pool = pool_next;
        }
    }
    return ;
}

// Register additional Indexes to an existing data pool
int rdb_register_um_idx (rdb_pool_t *pool, int idx, int key_offset,
        int FLAGS, void *compare_fn) {
    rdb_sem_lock(&reg_mutex);
    if (idx == 0) {
        rdb_sem_unlock(&reg_mutex);
        return (rdb_error_value (-1, 
            "Index 0 (zero) can only be set via rdb_register_pool"));
    }

    if (idx >= RDB_POOL_MAX_IDX) {
        rdb_sem_unlock(&reg_mutex);
        return (rdb_error_value (-2, "Index >= RDB_POOL_MAX_IDX"));
    }

    if (pool->FLAGS[idx] != 0) {
        rdb_sem_unlock(&reg_mutex);
        return (rdb_error_value (-3, "Redefinition of used index not allowed"));
    }

    if (-1 == set_pool_fn_pointers(pool, idx, FLAGS, compare_fn)){
        rdb_sem_unlock(&reg_mutex);
        return (rdb_error_value (-4,
            "Index Registration without valid type or compare fn. Ignored"));
    }

    pool->root[idx] = NULL;
    pool->key_offset[idx] = sizeof (PP_T) * pool->indexCount + key_offset;
    pool->FLAGS[idx] = FLAGS;
    debug ("registered index %d for pool %s, Keyoffset is %d\n", idx, pool->name, key_offset);
    rdb_sem_unlock(&reg_mutex);
    return (idx);
}

// Below is a set of self explanatory compare functions...
int key_cmp_int32 (int32_t *old, int32_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_int32 (int32_t *old, __intmax_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_uint32 (uint32_t *old, uint32_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_uint32 (uint32_t *old, __uintmax_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_int16 (int16_t *old, int16_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_int16 (int16_t *old, __intmax_t new)
{   
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_uint16 (uint16_t *old, uint16_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_uint16 (uint16_t *old, __uintmax_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_int8 (int8_t *old, int8_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_int8 (int8_t *old, __intmax_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_uint8 (uint8_t *old, uint8_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_uint8 (uint8_t *old, __uintmax_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_int64 (int64_t *old, int64_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_int64 (int64_t *old, __intmax_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_uint64 (uint64_t *old, uint64_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_uint64 (uint64_t *old, __uintmax_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_ptr (void **old, void **new)
{
    if ( new == NULL ) {
       return -1;
    } 
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_ptr (void *old, size_t new)
{
    return ((void *) new <  old) ? -1 : (( (void *) new > old) ? 1 : 0); 
}

int key_cmp_size_t (size_t *old, size_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_size_t (size_t *old, size_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_ssize_t (ssize_t *old, ssize_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_ssize_t (ssize_t *old, ssize_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}
/* 128 bit fn's are tricky when we don't have 128 native type to supply a 
 * comperator.
 * 256 bit's are alwways that way .... 
 * as such, 'const' fn's will take biggest native size as comperator while
 * non-const will use full size of type
 * */

#ifdef USE_128_BIT_TYPES
int key_cmp_int128 (__int128_t *old, __int128_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_int128 (__int128_t *old, __int128_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}

int key_cmp_uint128 (__uint128_t *old, __uint128_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_const_uint128 (__uint128_t *old, __uint128_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}
#else
//TODO: use GMP if found, to support big numbers safely.
/* - this junk is here until i decide which way to go - GMP, native, or NONE, when we have no native 128 bit support
 * currently it's out - no 128 bit without 128 bit native support.
 *
int key_cmp_int128 (type_128 *old, type_128 *new)
{
    return (*new->msb <  *old->msb) ? -1 : (( *new->msb > *old->msb) ? 1 : ((*new->lsb < *old->lsb) ? -1 : ((*new->lsb > *old->lsb) ? 1 : 0 ))); 
}
int key_cmp_const_int128 (type_128 *old, __intmax_t new)
{
    //type_128 new_new;
    //new_new.lsb=new;
    //if (new < 0) new_new.msb=0xffffffffffffffff; // extending the sign
    if (0 < *old->msb ) return -1;    // must be bigger then what we can spcify with 64 bits 'new'
    if (0 > *old->msb && *old->msb != -1) return 1;
    return (new <  (int64_t) *old->lsb) ? -1 : (( new > (int64_t) *old->lsb) ? 1 : 0); 
    //return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
    //return (new <  *old->msb) ? -1 : (( new > *old->msb) ? 1 : ((new < *old->lsb) ? -1 : ((new > *old->lsb) ? 1 : 0 ))); 
}

int key_cmp_uint128 (__uint128_t *old, type_128 *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
    return (*new->msb <  *old->msb) ? -1 : (( *new->msb > *old->msb) ? 1 : ((*new->lsb < *old->lsb) ? -1 : ((*new->lsb > *old->lsb) ? 1 : 0 ))); 
}
int key_cmp_const_uint128 (__uint128_t *old, __uint128_t new)
{
    return (new <  *old) ? -1 : (( new > *old) ? 1 : 0); 
}*/
#endif
//TODO add 256 bit types, and fix rDB.h line 38 to reflect

int key_cmp_str (char *old, char *new)
{
    return strcmp ( new, old);
}

int key_cmp_str_p (char **old, char **new)
{
    return strcmp ( *new, *old);
}
int key_cmp_const_str_p (char **old, char *new)
{
    return strcmp ( new, *old);
}
/*int keyCompareTME (struct timeval *key, struct timeval *keyNew)
{
    if (keyNew->tv_sec != key->tv_sec) return ((keyNew->tv_sec < key->tv_sec) ? -1 : 1);

    if (keyNew->tv_usec != key->tv_sec) return ((keyNew->tv_usec < key->tv_usec) ? -1 : 1);
    return 0;
}*/

/*int keyCompareTMA (TVA *key, TVA *keyNew)
{
    if (keyNew->tv.tv_sec != key->tv.tv_sec) return ((keyNew->tv.tv_sec <
                        key->tv.tv_sec) ? -1 : 1);
    if (keyNew->tv.tv_usec != key->tv.tv_usec) return ((keyNew->tv.tv_usec <
                        key->tv.tv_usec) ? -1 : 1);
    if (keyNew->acc != key->acc) return ((keyNew->acc < key->acc) ? -1 : 1);
    return 0;
}*/

//TODO: some compilers dont like this inlining. need more research
/*inline*/ void set_pointers (
        rdb_pool_t *pool, 
        int index, 
        void *start, 
        PP_T ** ppk, 
        void **dataHead){

    if (start == NULL || (start == pool->root[index]))
        *ppk = (void *) pool->root[index] + (sizeof (PP_T) * index);
    else
        *ppk = (void *) start + (sizeof (PP_T) * index);

    *dataHead = (void *) *ppk - (sizeof (PP_T) * index);
    return;
}

// TODO: Bring this up-to-date
// Note: type casting used to 
// 1) hash compiler about identcal type warnings, like 
// uint32 and unisnged long ...
// 2) handle int32 as long, since long is 32 bit on 16, 32 and 64 bit machines
//
void _rdb_dump (rdb_pool_t *pool, int index, char *separator, void *start)
{
    void  **searchNext;
    PP_T   *pp;
    rdb_key_union *key;

    if (start == NULL && pool->root[index] == NULL) return;

    if (start == NULL)
        maxLevels = 1;
    else
        levels++;

    if (levels > maxLevels)
        maxLevels = levels;

    if (pool->FLAGS[index] & RDB_BTREE && (pool->FLAGS[index] & RDB_NOKEYS))  {
        pp = ((void *) (pool->root[index])) + (sizeof (PP_T) * index) ;    // print data-head
        while (pp) {
            //searchNext = (void **) pp;
            //key = (void *) searchNext + pool->key_offset[0];
            //info ("%s%s", &key->str, separator);
            rdb_c_info ("%p%s", pp, separator);
            pp = pp->right;
        }
    } else if (pool->FLAGS[index] & RDB_BTREE && (pool->FLAGS[index] & RDB_KEYS)) {
        if (start == NULL) {
            pp = (void *) pool->root[index] + (sizeof (PP_T) * index);
            searchNext = (void **) pool->root[index];
        }
        else {
            searchNext = start;
            pp = start + (sizeof (PP_T) * index);
        }

        key = (void *) searchNext + pool->key_offset[index];

        if (pp->left != NULL) {
            _rdb_dump (pool, index, separator, pp->left);
            levels--;
        }

        switch (pool->FLAGS[index] & RDB_KEYS) {
            case RDB_KPTR:
                rdb_c_info ("%p%s", (void *) key->pStr, separator);
                break;

            case RDB_KPSTR:
                rdb_c_info ("%s%s", key->pStr, separator);
                break;

            case RDB_KSTR:
                rdb_c_info ("%s%s", &key->str, separator);
                break;

            case RDB_KINT8:
                rdb_c_info ("%hhd%s", key->i8, separator);
                break;

            case RDB_KINT16:
                rdb_c_info ("%hd%s", key->i16, separator);
                break;

            case RDB_KINT32:
                rdb_c_info ("%ld%s", (long) key->i32, separator);
                break;

            case RDB_KINT64:
                rdb_c_info ("%lld%s", (long long int) key->i64, separator);
                break;

            case RDB_KUINT8:
                rdb_c_info ("%hhu%s", key->u8, separator);
                break;

            case RDB_KUINT16:
                rdb_c_info ("%hu%s", key->u16, separator);
                break;

            case RDB_KUINT32:
                rdb_c_info ("%lu%s", (unsigned long) key->u32, separator);
                break;

            case RDB_KUINT64:
                rdb_c_info ("%llu%s", (unsigned long long) key->u64, separator);
                break;

            //TODO: this is a bug, data below may be truncated.
            //need to craft 128bit decimal print functions.
            //
#ifdef USE_128_BIT_TYPES
            case RDB_KUINT128:
                rdb_c_info ("%llu%s", (unsigned long long) key->u128, separator);
                break;

            case RDB_KINT128:
                rdb_c_info ("%lld%s", (long long) key->u128, separator);
                break;
#endif
            case RDB_KSIZE_t:
                rdb_c_info ("%zu%s", (size_t) key->st, separator);
                break;

            case RDB_KSSIZE_t:
                rdb_c_info ("%zd%s", (ssize_t) key->sst, separator);
                break;
            // we can't print custom functions data so we print the address
            // TODO: Consider adding a print-to-str fn() hook to pool, so we can dump custom-index data
            case RDB_KCF:
                rdb_c_info ("%p%s", key, separator);
                break;
 /*           case RDB_KTME:
                printoutalways ("Dump_TME: %ld:%ld\n", key->tv.tv_sec, key->tv.tv_usec);
                break;

            case RDB_KTMA:
                printoutalways ("Dump_TMA: %lu:%lu.%lu\n", (unsigned long) key->tva.tv.tv_sec,
                                (unsigned long) key->tva.tv.tv_usec, (unsigned long) key->tva.acc);
                break;
*/
            /*case RDB_K4U32A:
                printoutalways ("Dump_4U23A: %lu:%lu:%lu:%lu\n", (unsigned long) key->u32a.l1,
                                (unsigned long) key->u32a.l2, (unsigned long) key->u32a.l3, (unsigned long) key->u32a.acc);
                break;*/
        }

#ifdef DEBUG
        //        if (levels == maxLevels)
        //            printf ("LEVELS %d\n", maxLevels);
#endif

        if (pp->right != NULL) {
            _rdb_dump (pool, index, separator, pp->right);
            levels--;
        }
    }

    if (start == NULL)
        debug ("Final Level=%d\n", maxLevels);

}

int rdb_lock(rdb_pool_t *pool, const char *parent) 
{
#ifdef RDB_LOCK_DEBUG
    info("RDBL    %s by %s\n", pool->name, parent);
#endif
    return rdb_sem_lock(&pool->write_mutex);
}

void rdb_unlock(rdb_pool_t *pool, const char *parent) 
{
#ifdef RDB_LOCK_DEBUG
    info("RDb-UL %s - %s\n", pool->name, parent);
#endif
    rdb_sem_unlock(&pool->write_mutex);
    return ;
}

// Dump an entire pool to stdout. only the selected index field will be
// printed out. Also calculates tree depth...
void rdb_dump (rdb_pool_t *pool, int index, char *separator) {

    if (pool->root[index] == NULL) return;

    if ((pool->FLAGS[index] & (RDB_KEYS | RDB_NOKEYS)) != 0)
        _rdb_dump (pool, index, separator, NULL);
}

int _rdb_insert (
        rdb_pool_t  *pool, 
        void        *data, 
        void        *start, 
        int         index, 
        void        *parent, 
        int         side) { 

    int     rc, rc2;
    void   *dataHead;

    if (data == NULL)
        return (-1);

    debug ("Insert:AVL: pool=%s, idx=%d, start = %p\n",
            pool->name , 
            (int) index, 
            start);

    if ((pool->FLAGS[index] & RDB_BTREE) == RDB_BTREE) {
        PP_T   *ppk,
               *ppkNew;
        PP_T   *ppkParent = NULL;       // NULL is here to make compiler happy
        PP_T   *ppkRotate;
        PP_T   *ppkBottom;

        if (pool->FLAGS[index] & (RDB_NOKEYS)) {
            if (pool->root[index] == NULL) {
                pool->root[index] = pool->tail[index] = data;
                ppkNew = (void *) data + (sizeof (PP_T) * index);
                ppkNew->left = ppkNew->right = NULL;
                ppkNew->balance = 0;
            } else {   
                // Added elemant
                if (pool->FLAGS[index] & RDB_KFIFO) { 
                    // FIFO, add to tail, as we always read/remove from head 
                    // forward
                    ppkParent = (void *) pool->tail[index] + (sizeof (PP_T) * index);
                    ppkNew = data + (sizeof (PP_T) * index);
                    ppkNew->left = ppkParent;// - index;
                    ppkNew->right = NULL;
                    ppkParent->right = ppkNew;// - index;
                    ppkNew->balance = 0;
                    pool->tail[index] = data;
                }
                else if (pool->FLAGS[index] & RDB_KLIFO) {   
                    // LIFO, add to head, as we always read/remove from head 
                    // forward
                    ppkParent = (void *) pool->root[index] + (sizeof (PP_T) * index);
                    ppkNew = data + (sizeof (PP_T) * index);
                    ppkNew->right = ppkParent;// - index;
                    ppkNew->left = NULL;
                    ppkParent->left = ppkNew;// - index;
                    ppkNew->balance = 0;
                    pool->root[index] = data;
                }
            }
        }

        else if (pool->root[index] == NULL) {
            debug ("Virgin Insert, pool=%s\n",pool->name);
            pool->root[index] = data;
            debug ("ppkRoot  = %p,%p \n",  pool->root[index],
                       pool->root[index] + (sizeof (PP_T) * index));
            ppkNew = data + (sizeof (PP_T) * index);
            ppkNew->left = ppkNew->right = NULL;
            ppkNew->balance = 0;

#ifdef DEBUG
            if (start == NULL)
                ppk = (PP_T *) pool->root[index] + (sizeof (PP_T) * index);
            else
                ppk = start + (sizeof (PP_T) * index);

            dataHead = ppk - index;
            ppkNew = data;
            debug ("Datahead: %p, data: %p, ppkNew: %p, start%snull %p ? %p\n",
                    dataHead, data, ppkNew, (start == NULL) ? "=" : "!=",
                    dataHead + pool->key_offset[index], 
                    (void *) (ppkNew) + pool->key_offset[index]);
#endif
        }
        else {
            set_pointers (pool, index, start, &ppk, &dataHead);
            debug ("Pointers SET, start = %x\n", (unsigned long) (start));
            ppkNew = (void *) data + (sizeof (PP_T) * index);

            if ((rc = pool->fn[index] (dataHead + pool->key_offset[index],
                    (void *) data + pool->key_offset[index])) < 0) {
                // left side
                if (ppk->left == NULL) {
                    debug ("Left Insert\n");
                    ppk->left = data;           //ppkNew;
                    ppk->balance -= 1;
                    ppkNew->left = ppkNew->right = NULL;
                    ppkNew->balance = 0;

                    if (ppk->balance != 0)
                        return 1;           // added a level
                    else
                        return 0;           // added a leaf in an existing level
                }
                else {
                    // Here we possibly added a level to the left tree
                    debug ("Diving Left\n");

                    if ((rc2 = _rdb_insert (pool, data, ppk->left, index, start,
                                           RDB_TREE_LEFT)) == 1) {
                        // level added on left side
                        ppk->balance--;

                        if (ppk->balance == -2) {
                            // we need to rotate
                            ppkRotate = (void *) ppk->left + 
                                    (sizeof (PP_T) * index);

                            if (ppkRotate->balance < 0) {
                                //left left case
                                debug ("Left Left Rotate\n");

                                if (parent) {
                                    ppkParent = (void *) parent + 
                                            (sizeof (PP_T) * index);

                                    if (!side)  //RDB_TREE_LEFT
                                        ppkParent->left = ppk->left;
                                    else
                                        ppkParent->right = ppk->left;
                                }
                                else
                                    pool->root[index] = ppk->left;  //ppkRotate;

                                ppk->left = ppkRotate->right;
                                ppkRotate->right = dataHead;    //ppk;
                                ppk->balance = -1 * (ppkRotate->balance + 1);
                                ppkRotate->balance = (ppkRotate->balance + 1);
                                ppk = ppkRotate;
                            }
                            else {
                                // left right case
                                debug ("Left Right Rotate\n");
                                ppkBottom = (void *) ppkRotate->right + 
                                                (sizeof (PP_T) * index);

                                if (ppkBottom->balance > 0) {
                                    ppk->balance = 0;
                                    ppkRotate->balance = -1;
                                }
                                else if (ppkBottom->balance == 0) {
                                    ppk->balance = 0;
                                    ppkRotate->balance = 0;
                                }
                                else {
                                    ppk->balance = 1;
                                    ppkRotate->balance = 0;
                                }

                                ppkBottom->balance = 0;

                                ppkRotate->right = ppkBottom->left;
                                ppkBottom->left = ppkRotate - index;
                                ppk->left = ppkBottom->right;
                                ppkBottom->right = dataHead;    //ppk;
                                ppk = ppkBottom;

                                if (parent) {
                                    ppkParent = (void *) parent + 
                                            (sizeof (PP_T) * index);

                                    if (!side)  //RDB_TREE_LEFT
                                        ppkParent->left = ppkBottom - index;
                                    else
                                        ppkParent->right = ppkBottom - index;
                                }
                                else pool->root[index] = ppkBottom - index;
                            }
                        }

                        if (ppk->balance >= 0)
                            return 0;   // no further balanceing action needed

                        if (ppk->balance == -1)
                            return 1;   // need to continue balancing up
                    }
                    else if (rc2 == -1) return -1;
                }
            }
            else if (rc > 0) {                  // addin on Right side
                //right side
                if (ppk->right == NULL) {
                    debug ("Right Insert\n");
                    ppk->right = data;          //ppkNew;
                    ppk->balance += 1;
                    ppkNew->left = ppkNew->right = NULL;
                    ppkNew->balance = 0;

                    if (ppk->balance != 0)
                        return 1;       // added a level
                    else
                        return 0;       // added a leaf... in an existing level
                }
                else {
                    debug ("Diving Right, start = %p, balance=%d\n", 
                                                        start, ppk->balance);

                    if (( rc2 = _rdb_insert (pool, data, ppk->right, index, 
                                            start, RDB_TREE_RIGHT)) == 1) {
                        // level added on right side
                        ppk->balance++;

                        if (ppk->balance == 2) {
                            // we need to rotate
                            ppkRotate = (void *) ppk->right + 
                                    (sizeof (PP_T) * index);

                            if (ppkRotate->balance > 0) {
                                //right right case ( left rotation )
                                debug ("Right Right Rotate, parent = %x, "
                                        "index=%d\n", (unsigned) parent, index);

                                if (parent != NULL) {
                                    debug ("parent exist\n");
                                    ppkParent = (void *) parent + 
                                            (sizeof (PP_T) * index);

                                    if (!side)  //RDB_TREE_LEFT
                                        ppkParent->left = ppk->right;
                                    else
                                        ppkParent->right = ppk->right;
                                }
                                else
                                    pool->root[index] = ppk->right; //ppkRotate;

                                ppk->right = ppkRotate->left;
                                ppkRotate->left = dataHead;     //ppk;
                                ppk->balance = -1 * (ppkRotate->balance - 1);
                                ppkRotate->balance = (ppkRotate->balance - 1);
                                ppk = ppkRotate;
                                debug ("pool->root[%d] = %x\n", index, 
                                        pool->root[index]);
                            }
                            else {
                                // right left case 
                                // (right rotation followed by left rotation )
                                debug ("Right Left Rotate\n");
                                ppkBottom = (void *) ppkRotate->left + 
                                        (sizeof (PP_T) * index);

                                if (ppkBottom->balance < 0) {
                                    ppk->balance = 0;
                                    ppkRotate->balance = 1;
                                }
                                else if (ppkBottom->balance == 0) {
                                    ppk->balance = 0;
                                    ppkRotate->balance = 0;
                                }
                                else {
                                    ppk->balance = -1;
                                    ppkRotate->balance = 0;
                                }

                                ppkBottom->balance = 0;

                                ppkRotate->left = ppkBottom->right;
                                ppkBottom->right = ppkRotate - index;
                                ppk->right = ppkBottom->left;
                                ppkBottom->left = dataHead;     //ppk;
                                ppk = ppkBottom;

                                if (parent) {
                                    ppkParent = (void *) parent + 
                                            (sizeof (PP_T) * index);

                                    if (!side)  //RDB_TREE_LEFT
                                        ppkParent->left = ppkBottom - index;
                                    else
                                        ppkParent->right = ppkBottom - index;
                                }
                                else pool->root[index] = ppkBottom - index;
                            }
                        }

                        if (ppk->balance <= 0)
                            return 0;   // no further balanceing action needed

                        if (ppk->balance == 1)
                            return 1;   // need to continue balancing up
                    }
                    else if (rc2 == -1) return -1;
                }
            }
            else {
                debug ("Skipped due to multiple key on pool %s index %d\n",
                        pool->name, index);
                //TODO: give actal data
                return (rdb_error_value(-1, "Insert index failed due to "
                        "duplicate key in pool")); 
            }   // multiple keys not yet supported!
        }

        return 0;
    }

    return -1;                          // we found no mechanizm to add node
}

inline int _rdb_delete_by_pointer (
        rdb_pool_t  *pool, 
        void        *parent, 
        int         index,
        PP_T        *ppkDead,
        int         side);

int _rdb_delete (
        rdb_pool_t  *pool, 
        int         lookupIndex, 
        void        *data, 
        void        *start, 
        PP_T        *parent,
        int         side);

// Return shoud be the # of updated indexes, which must match the number of 
// defined indexes, anything less shows an error and should be treated as 
// such by user.
// TODO? Should we make this atomic - meaning if one index failes fo insert, 
// delete the previously inserted ones?

int rdb_insert (rdb_pool_t *pool, void *data)
{
    int     indexCount, ic2, last_success = -1;
    int     rc = 0;
  
    if (data != NULL) {
        for (indexCount = 0; indexCount < pool->indexCount; indexCount++) {
            (_rdb_insert (pool, data, pool->root[indexCount], 
                indexCount, NULL, 0) < 0) ? rc : rc++;

            if ( rc <= indexCount ) {
                //TODO: NOW!: finish partial delete
                if (last_success >= 0) { // we failed to insert, we have what to undo
                    //printf("RECOVERY\n");
                    rc = 0;
                    for (ic2 = 0; ic2 <= last_success; ic2++ ) {
                        //printf("RECOVERY %d\n", ic2);
                        (_rdb_delete (pool, ic2, data, NULL, NULL, 0) < 0) ? rc : rc++;
                    }
                    if (rc != ic2 + 1) {
                        //not able to delete what we just inserted? Lock missed?
                        rdb_error_value(-1, "rdb_insert failed. Insert UNDO failed. LOCK ERROR?");
                    } 
                    rc = 0; // to ensure counter will not go up
                }
                break;
            }
            else {
                last_success = indexCount;
            }
        }
#ifdef RDB_POOL_COUNTERS
        if (rc > 0) pool->record_count++;
#endif
    } else
        rc = -1;

    return rc;
}

// Only insert one index (asuming this index was removed and updated prior).
int rdb_insert_one (rdb_pool_t *pool, int index, void *data)
{
    return _rdb_insert (pool, data, pool->root[index], index, NULL, 0) ;
}

void   *_rdb_get (
        rdb_pool_t  *pool, 
        int         index, 
        void        *data, 
        void        *start, 
        int         partial) {

    PP_T   *ppk;
    int     rc;
    void   *dataHead;

    if ((pool->FLAGS[index] & RDB_BTREE) == RDB_BTREE) {

        if (pool->root[index] == NULL) {
            debug("GetFail - pool=%s, Null rool node\n",pool->name);
            return (NULL);
        } else {
            if (pool->FLAGS[index] & (RDB_NOKEYS)) {
                return pool->root[index];

            }
            set_pointers (pool, index, start, &ppk, &dataHead);

            if (data == NULL ) {
                debug("GetFail???\n");
                return (dataHead);          // special case, return root node
            }

            if ((rc = pool->get_fn[index] (dataHead + pool->key_offset[index],
                    (void *) data)) < 0) {
                // left side

                debug("Left Get\n");

                if (ppk->left == NULL) {
                    return (NULL);
                }
                else
                    return (_rdb_get (pool, index, data, ppk->left, partial));
            } else if (rc > 0) {
                //i right side

                debug("Right Get\n");

                if (ppk->right == NULL) {
                    return (NULL);
                }
                else
                    return (_rdb_get (pool, index, data, ppk->right, partial));
            }
            else {
                debug("Get:Done\n");
                return (dataHead);              
            }
        }
    }
    return NULL;                                //should never get here
}

/*
void   *_rdb_get_const (
        rdb_pool_t  *pool, 
        int         index, 
        __int128_t  value, 
        void        *start, 
        int         partial) {

    PP_T   *ppk;
    int     rc;
    void   *dataHead;

    if ((pool->FLAGS[index] & RDB_BTREE) == RDB_BTREE) {

        if (pool->root[index] == NULL) {
            debug("GetFail - pool=%s, Null rool node\n",pool->name);
            return (NULL);
        }
        else {
            set_pointers (pool, index, start, &ppk, &dataHead);

            if ((rc = pool->get_const_fn[index] (dataHead + 
                    pool->key_offset[index], value)) < 0) {
                // left side

                debug("Left Get\n");

                if (ppk->left == NULL) {
                    return (NULL);
                }
                else
                    return (_rdb_get_const (pool, index, value, ppk->left, partial));
            } else if (rc > 0) {
                //i right side

                debug("Right Get\n");

                if (ppk->right == NULL) {
                    return (NULL);
                }
                else
                    return (_rdb_get_const (pool, index, value, ppk->right, partial));
            }
            else {
                debug("Get:Done\n");
                return (dataHead); 
            }
        }
    }
    return NULL;                                //should never eet here
}*/

// Find requested data set and return a pointer to it
// As a special case, if data = null, root node will be returned.
void   *rdb_get (rdb_pool_t *pool, int idx, void *data)
{
    debug("Get:pool=%s,idx=%d", pool->name, idx);
    return _rdb_get (pool, idx, data, NULL, 0);
}

void   *rdb_get_const (rdb_pool_t *pool, int idx, __intmax_t value)
{
    debug("Get:pool=%s,idx=%d", pool->name, idx);
    return _rdb_get/*_const*/ (pool, idx, &value, NULL, 0);
}

void   *_rdb_get_neigh (
        rdb_pool_t  *pool, 
        int         index, 
        void        *data, 
        void        *start, 
        int         partial,
        void        **before, 
        void        **after) {

    PP_T   *ppk;
    int     rc;
    void   *dataHead;

    if ((pool->FLAGS[index] & RDB_BTREE) == RDB_BTREE) {

        if (pool->root[index] == NULL) {
            debug("GetFail\n");
            before = NULL;
            after = NULL;
            return (NULL);
        }
        else {
            set_pointers (pool, index, start, &ppk, &dataHead);

            if (data == NULL ) {
                debug("GetFail???\n");
                before = NULL;
                after = NULL;
                return (dataHead);              // special case, return root node
            }

            if ((rc = pool->fn[index] (dataHead + pool->key_offset[index],
                                  (void *) data)) < 0) {
                // left side
                debug("Left Get\n");

                *after = (NULL == start) ? (void *) pool->root[index] : start ;

                //*after=start;
                if (ppk->left == NULL) {
                    return (NULL);
                }
                else
                    return (_rdb_get_neigh (pool, index, data, ppk->left, 
                                                    partial, before, after));
            }
            else if (rc > 0) {
                //right side
                debug("Right Get\n");
                *before = (NULL == start) ? (void *) pool->root[index] : start ;

                //*before=start;
                if (ppk->right == NULL) {
                    return (NULL);
                }
                else
                    return (_rdb_get_neigh (pool, index, data, ppk->right, 
                                                    partial, before, after));
            }
            else {
                debug("Get:Done\n");
                *after = *before = NULL;
                return (dataHead);
            }
        }

    }

    return NULL;                                //should never eet here
}

// Same as pair above, plus, if key not found, set up pointers to
// records before and after the lookup record.
void   *rdb_get_neigh (rdb_pool_t *pool, int idx, void *data, void **before, void **after)
{
    return _rdb_get_neigh (pool, idx, data, NULL, 0, before, after);
}


// Internal usage
#define RDBFE_NODE_DELETED 1
#define RDBFE_NODE_RESUME_ON 2
#define RDBFE_NODE_DONE 4
#define RDBFE_NODE_FIND_NEXT 8
#define RDBFE_ABORT 32

int _rdb_iterate (
        rdb_pool_t  *pool, 
        int         index, 
        int         fn (void *, void *), 
        void        *data,
        void        del_fn(void *, void*),
        void        *delfn_data, 
        void        *start, 
        void        *parent, 
        int         side, 
        void        **resumePtr) {

    void   *dataHead;
    char   **dataField;
    PP_T   *pp, *pr;
    int     rc, rc2 = 0;
    int     rc3 = 0;
    int     indexCount;

    if (pool->FLAGS[index] & RDB_BTREE) {
        set_pointers (pool, index, start, &pp, &dataHead);

        if (*resumePtr != NULL) {
            debug ("0->Resumeing\n");
            if ((rc3 = pool->fn[index] (dataHead + pool->key_offset[index],
                            (void *) *resumePtr /*+ (sizeof (PP_T) * pool->indexCount)*/ + pool->key_offset[index])) < 0) {
                debug("1->left(i=%d)\n",index);
                if ( 1 == ( 1 & (rc =  _rdb_iterate (pool, index, fn, data,
                                    del_fn, delfn_data, pp->left, start, RDB_TREE_LEFT,
                                    resumePtr)))) { 
                    // tree may have been modified
                    // moving bit left - getting RDBFE_NODE_RESUME_ON and
                    // not changing abort status;
                    return (rc + 1 ); 
                }
                else if ( rc & RDBFE_NODE_FIND_NEXT ) {
                    *resumePtr = dataHead;
                    return rc - 8 ; //(leave abort status in if exist)
                }
                else if (rc > 1) return rc;
            }
        }
        else {
            if (pp->left != NULL) {
                debug("2->left\n");
                if ( 1 == ( 1 & (rc =  _rdb_iterate (pool, index, fn, data,
                                    del_fn, delfn_data, pp->left, start, RDB_TREE_LEFT,
                                    resumePtr)))) { 
                    // tree may have been modified
                    return (rc + 1 ) ; //RDBFE_NODE_RESUME_ON);
                }
                else if ( rc & RDBFE_NODE_FIND_NEXT ) {
                    *resumePtr = dataHead;
                    return rc - 8 ; //
                }
                else if (rc > 1) return rc;
            }
        }

        if (*resumePtr != NULL &&  (dataHead == *resumePtr)) {
            *resumePtr = NULL; 
            debug ("redume work\n");
        }
                       // time to start working again

        if (*resumePtr == NULL) {
            rc = 0;

            if (fn == NULL || RDB_CB_DELETE_NODE == (rc2 = fn (dataHead, data))
                    || RDB_CB_DELETE_NODE_AND_ABORT == rc2 || 
                                                        RDB_CB_ABORT == rc2 ) {

                if (rc2 == RDB_CB_DELETE_NODE_AND_ABORT) 
                    rc =  RDBFE_NODE_DELETED | RDBFE_ABORT ;
                else if (rc2 == RDB_CB_ABORT) 
                    return RDBFE_ABORT;
                else rc = RDBFE_NODE_DELETED;

                // Setting resume pointer to next record
                if (pp->right) {
                    pr = (void *) pp->right + (sizeof (PP_T) * index);

                    while(pr->left != NULL) {
                        pr = pr->left + (sizeof (PP_T) * index);
                    }
                    *resumePtr = (void *) pr - (sizeof (PP_T) * index);
                }
                else if (parent == NULL) {
                    pr = (void *) pool->root[index] + (sizeof (PP_T) * index);

                    if (pr->right) {
                        pr = pr->right + (sizeof (PP_T) * index);

                        while(pr->left != NULL) {
                            pr = pr->left + (sizeof (PP_T) * index);
                        }
                        *resumePtr = (void *) pr - (sizeof (PP_T) * index);
                    }
                    else {
                        *resumePtr = NULL; 
                        // we are removing root and there is nothing to it's
                        // right, we are done
                        // Signal we are done
                        rc = RDBFE_NODE_DONE | (rc & RDBFE_ABORT);  
                    }

                }
                else {   // Parent is not null
                    if (side == 0) {
                        *resumePtr = parent;
                        //*resumePtr = (void *) parent - (sizeof (PP_T) * index);
                    }
                    else {
                        *resumePtr = NULL;
                        // Signal we still need to find next pointer
                        rc = RDBFE_NODE_FIND_NEXT | (rc & RDBFE_ABORT);
                    }
                }

                for (indexCount = 0; indexCount < pool->indexCount; 
                                                            indexCount++) {
                    debug ("rdb_iterate: Delete # %d\n", indexCount);
                    _rdb_delete (pool, indexCount, dataHead, NULL, NULL, 0);
                }
#ifdef RDB_POOL_COUNTERS
                pool->record_count--;
#endif

                if (del_fn) del_fn(dataHead, delfn_data);
                else { 
                    // Courtesy delete of data block and dynamic indexes
                    // We have an index which is a pointer, need to free it too.
                    for (indexCount = 0; indexCount < pool->indexCount;
                            indexCount++) if (pool->FLAGS[indexCount] &
                                                            RDB_KPSTR) { 
                        dataField = dataHead + pool->key_offset[indexCount];
                        //debug("off = %d add %x, str %s\n",
                        // pool->key_offset[indexCount],
                        // (unsigned) *dataField, *dataField);
                            if (*dataField) rdb_free(*dataField);
                    }
                    if (dataHead) rdb_free (dataHead);
                }

                debug ("after delete rc=%d\n", rc);
                return rc;
            }
        }

        if (pp->right != NULL) {
            debug("(3)->right start %p resumePtr %p\n",start, *resumePtr);
            if (1 == (1 & (rc = _rdb_iterate(pool, index, fn, data, del_fn, 
                    delfn_data, pp->right, start, RDB_TREE_RIGHT, 
                                                            resumePtr)))) {
                // Moving bit left- getting RDBFE_NODE_RESUME_ON and not 
                // changing abort status;
                return ( rc + 1 ); 
            }
            else if ( rc > 1 ) return rc;
        }

    }

    return 0;
}

int _rdb_iterate_list (
        rdb_pool_t  *pool, 
        int         index, 
        int         fn (void *, void *), 
        void        *data,
        void        del_fn(void *, void*),
        void        *delfn_data, 
        void        *start, 
        void        *parent, 
        int         side, 
        void        **resumePtr) {

    void   *dataHead;
    char   **dataField;
    PP_T   *pp;
    int     rc, rc2 = 0;
    int     indexCount;

rfeStart:

    if (*resumePtr) {
        set_pointers (pool, index, *resumePtr, &pp, &dataHead); 
        *resumePtr = NULL;
    } else
        set_pointers (pool, index, start, &pp, &dataHead); 
			

    if (*resumePtr != NULL &&  (dataHead == *resumePtr)) *resumePtr = NULL; 
                       // time to start working again

    if (*resumePtr == NULL) {
        rc = 0;

        if (fn == NULL || RDB_CB_DELETE_NODE == (rc2 = fn (dataHead, data))
                || RDB_CB_DELETE_NODE_AND_ABORT == rc2 || 
                RDB_CB_ABORT == rc2 ) {

            if (rc2 == RDB_CB_DELETE_NODE_AND_ABORT) 
                rc =  RDBFE_NODE_DELETED | RDBFE_ABORT ;
            else if (rc2 == RDB_CB_ABORT) 
                return RDBFE_ABORT;
            else rc = RDBFE_NODE_DELETED;

            // Setting resume pointer to next record
            if (pool->FLAGS[index] & (RDB_NOKEYS)) { 
                // FIFO/LIFO, no need to recurse -
                if (pp->right) {
                    *resumePtr = (void *) pp->right - (sizeof (PP_T) * index);
                }
                else *resumePtr = NULL ;   // we are the last one;
            }

            for (indexCount = 0; indexCount < pool->indexCount; 
                    indexCount++) {
                debug ("rdb_iterate: Delete # %d\n", indexCount);
                _rdb_delete (pool, indexCount, dataHead, NULL, NULL, 0);
            }
#ifdef RDB_POOL_COUNTERS
            pool->record_count--;
#endif

            if (del_fn) del_fn(dataHead, delfn_data);
            else { 
                // Courtesy delete of data block and dynamic indexes
                // We have an index which is a pointer, need to free it too.
                for (indexCount = 0; indexCount < pool->indexCount;
                        indexCount++) if (pool->FLAGS[indexCount] &
                            RDB_KPSTR) { 
                            dataField = dataHead + pool->key_offset[indexCount];
                            //debug("off = %d add %x, str %s\n",
                            // pool->key_offset[indexCount],
                            // (unsigned) *dataField, *dataField);
                            if (*dataField) rdb_free(*dataField);
                        }
                        if (dataHead) rdb_free (dataHead);
            }

            return rc;
        }
    }

    if (pp->right) {
        start = (void *) pp->right - (sizeof (*pp) * index);
        goto rfeStart;
    }
    return 0;
}

/* rdb_iterate scans the data pool, in order, by index, calling fn() on each node
 * that is not null.
 * If fn returns RDB_DB_DELETE_NODE, or if fn is null, the tree node will be 
 * deleted from rDB and del_fn will be called to do it's thing (if not NULL).
 *
 * del_fn is called after node is unlinked from all data pools and it's purpose
 * is to free any allocated memory and any other cleanup needed.
 *
 * del_fn receive a pointer to the node head, and a potential user  argument
 * del_fn is optional but is highly recommanded. if it is missing (NULL), 
 * rRB will free ptr for you, but it will not know how to free
 * any dynamic allocations tied to it (except PSTR index fields, which is will
 * free), So If there are any, and del_fn is null, memoty leak will occur.
 */

void rdb_iterate(
        rdb_pool_t  *pool, 
        int         index, 
        int         fn(void *, void *),
        void        *fn_data,
        void        del_fn(void *, void *),
        void        *del_data) {

    void        *resumePtr;
    int         rc = 0;

    if (pool == NULL) {
        return rdb_error("rdb_iterate called with NULL pool");
    }

    if (pool->root[index] == NULL) {
        return;	    // no data is not an error
    }

    if ((pool->FLAGS[index] & RDB_BTREE) == 0) {
        return rdb_error("iterate called without RDB_BTREE flag.");
    }

    resumePtr = NULL;

    do {
        if ((pool->FLAGS[index] & (RDB_NOKEYS)) == 0) { 
            // tree iteration
            debug("Tree iterate - %s",pool->name);
            rc = _rdb_iterate (pool, index, fn, fn_data, del_fn, del_data,
                                pool->root[index], NULL, 0, &resumePtr);
        } else { 
            // list iteration
            debug("List iterate - %s",pool->name);
            rc = _rdb_iterate_list (pool, index, fn, fn_data, del_fn, del_data,
                                pool->root[index], NULL, 0, &resumePtr);
        }
	    debug("rc=%d %p\n",rc, resumePtr);
    } while (rc != 0 && ( rc & RDBFE_ABORT ) != RDBFE_ABORT && 
                                                    resumePtr != NULL);
}

void _rdb_flush( 
        rdb_pool_t  *pool, 
        void        *start, 
        void        fn( void *, void *), 
        void        *fn_data) {

    void   *dataHead;
    PP_T   *pp;
    void   **dataField;
    int     indexCount;

    if (pool->FLAGS[0] & RDB_BTREE) {
        set_pointers( pool, 0, start, &pp, &dataHead);

        if (pp->left != NULL)
            _rdb_flush( pool, pp->left, fn, fn_data );

        if (pp->right != NULL)
            _rdb_flush( pool, pp->right, fn, fn_data );

        if (NULL != fn) fn(dataHead, fn_data);
        else { 
            // Courtesy delete of data block

            for (indexCount = 0; indexCount < pool->indexCount;
                    indexCount++) if (pool->FLAGS[indexCount] &
                                                        RDB_KPSTR) { 
                // We have an index which is a pointer, need to free it too.
                dataField = dataHead + pool->key_offset[indexCount];
                //debug("off = %d add %x, str %s\n",
                //pool->key_offset[indexCount],(unsigned) *dataField,
                //*dataField);
                if (*dataField) rdb_free(*dataField);
                }
            if (dataHead) rdb_free (dataHead);
        }
    }
}

void _rdb_flush_list(   
        rdb_pool_t  *pool, 
        void        *start, 
        void        fn( void *, void *), 
        void *fn_data) {

    void   *dataHead;
    PP_T   *pp;
    void   **dataField;
    int     indexCount;

    if (pool->FLAGS[0] & RDB_BTREE)
        do {
            set_pointers( pool, 0, start, &pp, &dataHead);

            start = pp->right;

            if (NULL != fn) fn(dataHead, fn_data);
            else { 
                // Courtesy delete of data block

                for (indexCount = 0; indexCount < pool->indexCount;
                        indexCount++) if (pool->FLAGS[indexCount] &
                                                        RDB_KPSTR) { 
                    // We have an index which is a pointer, need to free it too.
                    dataField = dataHead + pool->key_offset[indexCount];
                    // debug("off = %d add %x, str %s\n",
                    // pool->key_offset[indexCount],
                    // (unsigned) *dataField, *dataField);
                        if (*dataField) rdb_free(*dataField);
                    }
                if (dataHead) rdb_free (dataHead);
            }
        }
        while (start != NULL);
}

/* This will delete all the nodes in the tree,
 * Since we are destroying the tree, we do not care about fixing parent
 * pointers, re-balancing etc, this means there is no before and after delete
 * functions, but only one fn which serve as both.
 *
 * This fn should free the allocated memory (ptr), and any additionali
 * dynamic allocations tied to it.
 *
 * fn() is optional but is highly recommanded. if it is missing (NULL), 
 * rdb will free ptr for you, but it will not know how to free any dynamic 
 * allocations tied to it, so If there is any, and fn is null, 
 * memoty leak will occur.
 */
void rdb_flush( rdb_pool_t *pool, void fn( void *, void *), void *fn_data)
{

    int cnt;
    
    if (pool->root[0] == NULL)
        return;

    if (pool->FLAGS[0] & (RDB_NOKEYS))
        _rdb_flush_list (pool, NULL, fn, fn_data);
    else
        _rdb_flush (pool, NULL, fn, fn_data);

    for (cnt = 0; cnt < pool->indexCount; cnt++)
        if (pool->root[cnt])
            pool->root[cnt] = NULL;

#ifdef RDB_POOL_COUNTERS
    pool->record_count = 0;
#endif

}




/* In order to delete and keep an AVL tree balanced, when we may have multiple
 * indexes, we need to do the following:
 *
 * 1) Do an rdb_get to aquire a 'data' pointer so we can read all indexes and 
 *    delete the node out of all index trees. this is done by rdb_delete().
 *
 * 2) Next, recursive function _rdb_delete() is called, Once for each index.
 *    It will find the pointer to the data to be deleted (ppkDead), as well as
 *    a 'data' pointer to the parent.
 *
 * 3) Next we call _rdb_delete_by_pointer() to actually un-link the data blocks
 *    from the index trees. We have the following cases...
 *    3.1) Unlink block is a leaf (no childrens), we just update parent pointers
 *         point to NULL and return indicator to parent to re-balance tree if 
 *         needed
 */


/* In orfer to delete a node we need to do the following:
 *
 * 1) Find the parent node (if we are not deleting the root node) so new 
 *    subtree can be linked to it (*parent)
 *
 * 2) Find the node to be deleted , same is in normal lookup 
 *    (this is how we get the parent - *ppkDead)
 *
 * 3) If deleted node only has one child, link ppkLeft Or ppkRight 
 *    to parent and we are done
 *
 * 4) If we have 2 children set *ppkLeft and *ppkRight and...
 *    Find most right point of left child (*ppkHook)
 *
 * 5) ppkHook->right = ppkRight
 *
 * 6) parent->side = ppkLeft...
 */

//TODO inline this
int _rdb_delete_by_pointer (
        rdb_pool_t  *pool, 
        void        *parent, 
        int         index, 
        PP_T        *ppkDead, 
        int side) {

    PP_T   *ppkParent = NULL;           // NULL is here to make compiler happy

    if (parent)
        ppkParent = (void *) parent + (sizeof (PP_T) * index);

    debug("deleteByPtr:  pool=%s, parent %p\n", pool->name, parent);

    if (ppkDead->right == NULL && ppkDead->left == NULL) {
        // No children, only need to fix parent if exist
        debug("rdb_delete_by_ptr: no Children : inside:  parent %p\n", parent);

        if (parent) {
            if (side)                           // We hung of the parents right
                ppkParent->right = NULL;
            else                                // Left
                ppkParent->left = NULL;
        }
        else pool->root[index] = NULL ;

        // We just deleted a leaf, parent need to update balance.
        return PARENT_BAL_CNG; 
    } else if (ppkDead->right && (ppkDead->left == NULL)) {
        // One child on the right
        if (parent) {
            if (side)
                // we hook up parent to his new child
                ppkParent->right = ppkDead->right;     
            else
                ppkParent->left = ppkDead->right;
        } else
            // ppkDead child now become root node
            pool->root[index] = ppkDead->right; 

        // parent need to update balance.
        return PARENT_BAL_CNG;                  
    }
    else if ((ppkDead->right == NULL) && ppkDead->left) {
        // One chile on left
        if (parent) {
            if (side)
                // we hooked up parent to his new child
                ppkParent->right = ppkDead->left; 
            else
                ppkParent->left = ppkDead->left;
        } else
            //ppkDead child now become root node
            pool->root[index] = ppkDead->left; 

        // parent need to update balance.
        return PARENT_BAL_CNG;
    } else {
        // Two children

        debug("rdbDeleteByPointer: a: I should never get here! index = %d, %p %p\n",
                                        index, ppkDead->left, ppkDead->right);
#ifdef KM
        return 0;
#else
        exit(1);
#endif
    }
}

int _rdb_delete (
        rdb_pool_t  *pool, 
        int         lookupIndex, 
        void        *data, 
        void        *start, 
        PP_T        *parent, 
        int side) {

    PP_T   *ppkDead;
    PP_T   *ppkParent;
    PP_T   *ppkRotate;
    PP_T   *ppkBottom;
    int     rc,
            rc2;
    void   *dataHead;

    if (pool->FLAGS[lookupIndex] & (RDB_NOKEYS)) {
        void   *ptr = NULL;         // NULL to sashhh the compiler
        int         indexCount;
        PP_T   *ppk,
               *ppkRight,
               *ppkLeft;
        ptr = data; 
        indexCount = lookupIndex;
        if (pool->FLAGS[indexCount] & (RDB_NOKEYS)) {
            ppk = ptr + (sizeof (PP_T) * indexCount);

            if (ppk->left) ppkLeft = ppk->left ;
            else ppkLeft = NULL;

            if (ppk->right) ppkRight = ppk->right ;
            else ppkRight = NULL;

            if (pool->root[indexCount] == pool->tail[indexCount]) 
                //we were the last item
                pool->root[indexCount] = pool->tail[indexCount] = NULL; 
            else { 
                if (ppkLeft) ppkLeft->right = ppk->right;

                if (ppkRight) ppkRight->left = ppk->left;

                if (pool->root[indexCount] == ptr) 
                    pool->root[indexCount] = ppkRight - indexCount;

                if (pool->tail[indexCount] == ptr) 
                    pool->tail[indexCount] = ppkLeft - indexCount;
            }
        }
    } else if ((pool->FLAGS[lookupIndex] & RDB_BTREE) == RDB_BTREE) {
        debug("Delete:start: \n");

        if (pool->root[lookupIndex] == NULL) {
            return (0);
        } else {
            set_pointers (pool, lookupIndex, start, &ppkDead, &dataHead);
            debug("Delete:before compare: \n");

            //TODO should be pool->get_fn? make a test with p_str
            if ((rc = pool->fn[lookupIndex] (dataHead + 
                    pool->key_offset[lookupIndex], (void *) data + 
                            (pool->key_offset[lookupIndex]))) != 0) {
retest_delete_cond:
                debug("Delete:compare: (%d) idx (%d)\n", rc, lookupIndex);
                rc2 = 0;

                if (rc < 0) {
                    // Left child
                    debug("D:left Child!\n");

                    if (ppkDead->left == NULL) {
                        return 0;
                    }

                    rc2 = _rdb_delete (pool, lookupIndex, data, ppkDead->left,
                                                    dataHead, RDB_TREE_LEFT);
                    debug("(L)My Bal After  %d %d\n", ppkDead->balance, rc2);
                }
                else if (rc > 0) {
                    // Right child
                    debug("D:right Child!\n");

                    if (ppkDead->right == NULL) {
                        return 0;
                    }

                    rc2 = _rdb_delete (pool, lookupIndex, data, ppkDead->right,
                                                    dataHead, RDB_TREE_RIGHT);
                    debug("(R)My Bal After  %d %d\n", ppkDead->balance, rc2);
                }

                if (PARENT_BAL_CNG == rc2) { // And I am the parent...
                    ppkParent = (PP_T *) parent + lookupIndex;
                    ppkParent = parent + lookupIndex;
                    debug("- bal  to be changed = %d\n", ppkDead->balance);
                    // If on right side then add -1 - subtract one form balance
                    ppkDead->balance += (rc > 0) ? -1 : 1;
                    debug("- bal changed = %d\n", ppkDead->balance);

                    if (ppkDead->balance == -1 || ppkDead->balance == 1) {
                        // We moved from balance to -1 or +1 after delete, 
                        // nothing more we need to do
                        return 0;
                    } else if (ppkDead->balance == 0) {
                        // We transitioned to zero by delete, need to keep 
                        // balancing up
                        return PARENT_BAL_CNG;  
                    } else if (ppkDead->balance < -1) {
                        // Left case rotare
                        debug("Some left rotation ppkDead->left=%p\n", 
                                                            ppkDead->left);
                        ppkRotate = (PP_T *) ppkDead->left + lookupIndex;

                        if (ppkRotate->balance < 1) {
                            // Left left rotate
                            debug ("Left Left Rotate\n");

                            if (parent) {
                                ppkParent = (PP_T *) parent + lookupIndex;

                                if (!side)      //RDB_TREE_LEFT
                                    ppkParent->left = ppkDead->left;
                                else ppkParent->right = ppkDead->left;
                            } else pool->root[lookupIndex] = ppkDead->left;

                            ppkDead->left = ppkRotate->right;
                            ppkRotate->right = dataHead;
                            ppkDead->balance = -1 * (ppkRotate->balance + 1);
                            ppkRotate->balance = (ppkRotate->balance + 1);
                            ppkDead = ppkRotate;

                            if (ppkRotate->balance == -1 || 
                                    ppkRotate->balance == 1) return 0;
                            else return PARENT_BAL_CNG;

                        }
                        else {
                            // Left right

                            debug ("Left Right Rotate\n");
                            ppkBottom = (void *) ppkRotate->right + 
                                    (sizeof (PP_T) * lookupIndex);

                            if (ppkBottom->balance > 0) {
                                ppkDead->balance = 0;
                                ppkRotate->balance = -1;
                            }
                            else if (ppkBottom->balance == 0) {
                                ppkDead->balance = 0;
                                ppkRotate->balance = 0;
                            }
                            else {
                                ppkDead->balance = 1;
                                ppkRotate->balance = 0;
                            }

                            ppkBottom->balance = 0;

                            ppkRotate->right = ppkBottom->left;
                            ppkBottom->left = ppkRotate - lookupIndex;
                            ppkDead->left = ppkBottom->right;
                            ppkBottom->right = dataHead;
                            ppkDead = ppkBottom;

                            if (parent) {
                                ppkParent = (void *) parent + (sizeof (PP_T) *
                                                                lookupIndex);

                                if (!side)      //RDB_TREE_LEFT
                                    ppkParent->left = ppkBottom - lookupIndex;
                                else
                                    ppkParent->right = ppkBottom - lookupIndex;
                            }
                            else {
                                pool->root[lookupIndex] =  ppkBottom - 
                                                                lookupIndex;
                            }

                            return PARENT_BAL_CNG;
                        }
                    }
                    else if (ppkDead->balance > 1) {
                        // Rotate
                        ppkRotate = (PP_T *) ppkDead->right + lookupIndex;

                        if (ppkRotate->balance > -1) {
                            // Right right rotate
                            debug ("Right Right Rotate, parent = %p,index=%d\n",
                                                (unsigned) parent, lookupIndex);

                            if (parent != NULL) {
                                debug ("parent exist\n");
                                ppkParent = (void *) parent + (sizeof (PP_T) * 
                                                                lookupIndex);

                                if (!side)      //RDB_TREE_LEFT
                                    ppkParent->left = ppkDead->right;
                                else
                                    ppkParent->right = ppkDead->right;
                            }
                            else
                                pool->root[lookupIndex] = ppkDead->right;

                            ppkDead->right = ppkRotate->left;
                            ppkRotate->left = dataHead; 
                            ppkDead->balance = -1 * (ppkRotate->balance - 1);
                            ppkRotate->balance = (ppkRotate->balance - 1);
                            ppkDead = ppkRotate;

                            if (ppkRotate->balance == -1 || 
                                            ppkRotate->balance == 1) return 0;
                            else return PARENT_BAL_CNG;

                        }
                        else {
                            // Right left case
                            debug ("Right Left Rotate\n");
                            ppkBottom = (void *) ppkRotate->left + 
                                                (sizeof (PP_T) * lookupIndex);

                            if (ppkBottom->balance < 0) {
                                ppkDead->balance = 0;
                                ppkRotate->balance = 1;
                            }
                            else if (ppkBottom->balance == 0) {
                                ppkDead->balance = 0;
                                ppkRotate->balance = 0;
                            }
                            else {
                                ppkDead->balance = -1;
                                ppkRotate->balance = 0;
                            }

                            ppkBottom->balance = 0;

                            ppkRotate->left = ppkBottom->right;
                            ppkBottom->right = ppkRotate - lookupIndex;
                            ppkDead->right = ppkBottom->left;
                            ppkBottom->left = dataHead; //ppk;
                            ppkDead = ppkBottom;

                            if (parent) {
                                ppkParent = (void *) parent + (sizeof (PP_T) * 
                                                                lookupIndex);

                                if (!side)      //RDB_TREE_LEFT
                                    ppkParent->left = ppkBottom - lookupIndex;
                                else
                                    ppkParent->right = ppkBottom - lookupIndex;
                            }
                            else {
                                pool->root[lookupIndex] = ppkBottom - 
                                                                lookupIndex;
                            }

                            return PARENT_BAL_CNG;
                        }
                    }

                }
            }
            else {
                debug("Delete:compare:- _%d_ idx _%d_ ppkDead = %x\n", rc, 
                                    lookupIndex, (unsigned) ppkDead);

                if (ppkDead->right != NULL && ppkDead->left != NULL) { 
                    // we need to perform a pre-delete swap!
                    PP_T *ppkTemp;

                    debug("Rotate 1 ppkDead %x\n", (unsigned) ppkDead);
                    ppkRotate = (PP_T *) ppkDead->right + lookupIndex;
                    ppkTemp = NULL;

                    if (ppkRotate->left) {
                        while (ppkRotate->left) {
                            ppkTemp   = ppkRotate;
                            ppkRotate = (PP_T *) ppkRotate->left + lookupIndex;
                        }

                        // Here we have the smallest element of the right tree, 
                        // now we need to perform the swap
                        if (parent) {
                            ppkParent = (PP_T *) parent + lookupIndex;
                            debug("D:We have a parent\n");

                            if (side) ppkParent->right = (PP_T *) ppkRotate - 
                                                                    lookupIndex;
                            else ppkParent->left = (PP_T *) ppkRotate - 
                                                                    lookupIndex;
                        }
                        else pool->root[lookupIndex] = ppkRotate - lookupIndex;

                        ppkBottom = ppkDead->right;
                        ppkDead->right = ppkRotate->right;
                        ppkRotate->right = ppkBottom;

                        ppkBottom = ppkDead->left;
                        ppkDead->left = ppkRotate->left;
                        ppkRotate->left = ppkBottom;

                        rc = ppkRotate->balance;
                        ppkRotate->balance = ppkDead->balance;
                        ppkDead->balance = rc;

                        if (ppkTemp) ppkTemp->left = ppkDead - lookupIndex ; 
                        // if ppkRotate is now root, it has no father
                    }
                    else {   
                        // Special case for root deletion with no left node on 
                        // right side, we'll just swap ppkDead with ppkRotate
                        debug("DS: Parent = %x\n", (unsigned) parent);

                        if (parent) {
                            ppkParent = (PP_T *) parent + lookupIndex;
                            debug("DS: We have a parent\n");

                            if (side) ppkParent->right = (PP_T *) ppkRotate - 
                                                                    lookupIndex;
                            else ppkParent->left = (PP_T *) ppkRotate - 
                                                                    lookupIndex;
                        }
                        else pool->root[lookupIndex] = ppkRotate - lookupIndex;

                        ppkDead->right = ppkRotate->right;
                        ppkRotate->right = ppkDead - lookupIndex;
                        ppkRotate->left = ppkDead->left;
                        ppkDead->left = NULL;

                        rc = ppkDead->balance;
                        ppkDead->balance = ppkRotate->balance;
                        ppkRotate->balance = rc;


                    }

                    start = ppkRotate - lookupIndex;
                    set_pointers(pool, lookupIndex, start, &ppkDead, &dataHead);

                    rc = 1;

                    goto retest_delete_cond;

                }

                debug("My Bal Before ... and now I'm dead %d\n", 
                                                        ppkDead->balance);
                rc = _rdb_delete_by_pointer (pool, (void *) parent, lookupIndex,
                                                                 ppkDead, side);
                debug("Delete by Ptr returned %d\n", rc);
                return (rc);
            }
        }
    }

    return 0;                                // Should never get here
}   

void   *rdb_delete_const (rdb_pool_t *pool, int idx, __intmax_t value)
{
    debug("Get:pool=%s,idx=%d", pool->name, idx);
    return rdb_delete (pool, idx, &value); //, NULL, 0);
}

// TODO, make sure rdb_delete works well on trees with both indexed and non-indexed 
// indexes. ( is that a valid configuration? )
//
void   *
rdb_delete (rdb_pool_t *pool, int lookupIndex, void *data)
{

    int     indexCount;
    void   *ptr = NULL;                     // NULL to hash the compiler

    if (pool->FLAGS[lookupIndex] & (RDB_NOKEYS)) {
        PP_T   *ppk = NULL,
               *ppkRight = NULL,
               *ppkLeft = NULL;
        ptr = pool->root[lookupIndex]; 
        if (ptr == NULL)
            return NULL;

        for (indexCount = 0; indexCount < pool->indexCount; 
                                                        indexCount++) {
            if (pool->FLAGS[indexCount] & (RDB_NOKEYS)) {
                ppk = ptr + (sizeof (PP_T) * indexCount);

                if (ppk->left) 
                    ppkLeft = ppk->left ;//+ indexCount;
                else ppkLeft = NULL;

                if (ppk->right) 
                    ppkRight = ppk->right ;//+ indexCount;
                else ppkRight = NULL;

                if (pool->root[indexCount] == pool->tail[indexCount]) 
                        pool->root[indexCount] = pool->tail[indexCount] = NULL;
                        // We were the last item
                else { 
                    if (ppkLeft) ppkLeft->right = ppk->right;

                    if (ppkRight) ppkRight->left = ppk->left;

                    if (pool->root[indexCount] == ptr) pool->root[indexCount] =
                                                        ppkRight - indexCount;

                    if (pool->tail[indexCount] == ptr) pool->tail[indexCount] =
                                                        ppkLeft - indexCount;
                }
            }
            else {
                _rdb_delete (pool, indexCount, ptr, NULL, NULL, 0);
            }
        }
    }
    else if (data) {
        if ((ptr = _rdb_get (pool, lookupIndex, data, NULL, 0)) != NULL)
            for (indexCount = 0; indexCount < pool->indexCount; indexCount++) {
                _rdb_delete (pool, indexCount, ptr, NULL, NULL, 0);
            }

        else
            debug ("Can't locate delete item\n");

    }

#ifdef RDB_POOL_COUNTERS
    if (ptr != NULL) pool->record_count--;
#endif

    return ptr;
}
int rdb_delete_one (rdb_pool_t *pool, int index, void *data)
{
    return _rdb_delete (pool, index, data, NULL, NULL, 0);
}

// move data (identified by value const, from source tree to destination tree.
// data is not copied or reallocated, only trees pointers are updated.
//
void *rdb_move_const (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, __intmax_t value) {
    void *ptr;
    ptr = rdb_delete (src_pool, idx, &value);
    if (ptr && rdb_insert (dst_pool, ptr)) return ptr;
    return NULL;
}

void *rdb_move (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, void *data) {
    void *ptr;
    ptr = rdb_delete (src_pool, idx, data);
    if (ptr && rdb_insert (dst_pool, ptr)) return ptr;
    return 0;
}

// same but slower, with mode debug (not returning a pointer to data)
int rdb_move2 (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, void *data) {
    void *ptr;
    if ((ptr = rdb_delete (src_pool, idx, data))) {
        if ( dst_pool->indexCount == rdb_insert (dst_pool, ptr)) return 0;
        return -2;
    }
    return -1;
}

#ifdef KM
static int __init init (void)
{
    rdb_init ();
    return 0;
}

static void __exit fini (void)
{
    return;
}

EXPORT_SYMBOL (rdb_register_um_pool);
EXPORT_SYMBOL (rdb_register_um_idx);
EXPORT_SYMBOL (rdb_dump);
EXPORT_SYMBOL (rdb_insert);
EXPORT_SYMBOL (rdb_get);
EXPORT_SYMBOL (rdb_get_neigh);
EXPORT_SYMBOL (rdb_iterate);
EXPORT_SYMBOL (rdb_flush);
EXPORT_SYMBOL (rdb_delete);
EXPORT_SYMBOL (rdb_clean);


/*
 * Inform the world about the module
 */

MODULE_AUTHOR ("Assaf Stoler <assaf.stoler@gmail.com>");
MODULE_LICENSE ("LGPL");
MODULE_DESCRIPTION ("RDB Ram-DB module");
MODULE_VERSION("1.0-rc1");


/*
 * This handles module initialization
 */

module_init (init);
module_exit (fini);
#endif
