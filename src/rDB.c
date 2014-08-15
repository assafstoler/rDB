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

#ifdef KM
// Build a Kernel Module

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <net/sock.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/genetlink.h>

#include <linux/vmalloc.h>
#include "rDB.h"

#define rdb_free(a) kfree(a)
#define rdb_alloc(a) kmalloc (a, GFP_KERNEL);

#else
// Build a UserSpace Library

#include <stdio.h>                              //printf,
#include <stdlib.h>                             //exit,
#include <string.h>                             //strcmp,
#include "rDB.h"
#include <pthread.h>

#define rdb_free(a) free(a)
#define rdb_alloc(a) malloc (a);

#endif

#define PP_T  rdb_bpp_t


/*typedef struct RDB_INDEX_DATA {
    int     segments;
    int     **key_offset;                        ///< we'll need a key offset for every index segment, so it must be dynamically allocated
    int     **flags;                            ///< flags for this pool (index head) or flags for this index continuation (field type)
} rdb_index_data_t;
*/


rdb_pool_t  *pool_root;


//struct  RDB_POOLS **poolIds;

#ifdef KM
//struct 	RDB_POOLS **poolIdsTmp;
#endif

char   	*rdb_error_string = NULL;

//#define DEBUG
#ifdef DEBUG
#ifdef KM
#define debug(b,arg...) printk(b,##arg)
#else
#define debug(b,arg...) do { fprintf(stdout,b,##arg); fflush(stdout); } while (0)
#endif
#else
#define debug(b,arg...)
#endif

#ifdef KM
#define info(b,arg...) printk(b,##arg)
#else
#define info(b,arg...) do { fprintf(stdout,b,##arg); fflush(stdout); } while (0)
#endif



pthread_mutex_t reg_mutex = PTHREAD_MUTEX_INITIALIZER;

/// Initilize the rDB subsystem, need to be called once before any other rDB function
void rdb_init (void)
{
    pool_root = NULL;
}

int rdb_error_value (int rv, char *err)
{
#ifdef KM

    if (rdb_error_string != NULL)
        kfree (rdb_error_string);

    rdb_error_string = kmalloc (strlen (err) + 1, GFP_KERNEL);
#else
    rdb_error_string = realloc (rdb_error_string, strlen (err) + 1);
#endif

    if (rdb_error_string == NULL)
        info ("rdb: failed to Alloc RAM for error message, original error was \"%s\"\n", err);
    else
        strcpy (rdb_error_string, err);

    return rv;
}

void rdb_error (char *err)
{
#ifdef KM

    if (rdb_error_string != NULL)
        kfree (rdb_error_string);

    rdb_error_string = kmalloc (strlen (err) + 1, GFP_KERNEL);
#else
    rdb_error_string = realloc (rdb_error_string, strlen (err) + 1);
#endif

    if (rdb_error_string == NULL)
        info ("rdb: failed to Alloc RAM for error message, original error was \"%s\"\n", err);
    else
        strcpy (rdb_error_string, err);
}

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

/// Add a new pool to our pool chain
rdb_pool_t *rdb_add_pool (char *poolName, int indexCount, int key_offset, int FLAGS, void *compare_fn)
{
    rdb_pool_t *pool;

    pool = rdb_alloc (sizeof (rdb_pool_t));

    if (pool == NULL) {
        rdb_error ("FATAL: Pool allocation error, out of memory");
        return NULL;
    }

    memset (pool, 0, sizeof (rdb_pool_t));

    // inserting myself to the top of the pool, why? 
    // because it is easy, don't need to look for the end of the pool chain
    pool->next = pool_root;

    if (pool_root)
        pool_root->prev = pool;

    pool_root = pool;
    pool->name = rdb_alloc (strlen (poolName) + 1);

    if (pool->name == NULL) {
        rdb_error ("FATAL: pool allocation error, out of memory (pool name)");
        pool_root = pool->next;
        rdb_free (pool);
        return NULL;
    }

    strcpy (pool->name, poolName);
    
    if (compare_fn)                 pool->fn[0] = compare_fn;
    else if (FLAGS & RDB_KINT32)    pool->fn[0] = key_cmp_int32;
    else if (FLAGS & RDB_KUINT32)   pool->fn[0] = key_cmp_uint32;
    else if (FLAGS & RDB_KINT64)    pool->fn[0] = key_cmp_int64;
    else if (FLAGS & RDB_KUINT64)   pool->fn[0] = key_cmp_uint64;
    else if (FLAGS & RDB_KINT16)    pool->fn[0] = key_cmp_int16;
    else if (FLAGS & RDB_KUINT16)   pool->fn[0] = key_cmp_uint16;
    else if (FLAGS & RDB_KINT8)     pool->fn[0] = key_cmp_int8;
    else if (FLAGS & RDB_KUINT8)    pool->fn[0] = key_cmp_uint8;
    else if (FLAGS & RDB_KINT128)   pool->fn[0] = key_cmp_int128;
    else if (FLAGS & RDB_KUINT128)  pool->fn[0] = key_cmp_uint128;
    else if (FLAGS & RDB_KSTR)      pool->fn[0] = key_cmp_str;
    else if (FLAGS & RDB_KPSTR)     pool->fn[0] = key_cmp_str; //TODO is this right ? we shall see
    //else if (FLAGS & RDB_KTME)    pool->fn[0] = keyCompareTME;
    //else if (FLAGS & RDB_KTMA)    pool->fn[0] = keyCompareTMA;
    else if (FLAGS & RDB_NOKEYS)    pool->fn[0] = NULL;
    else {
        rdb_error ("RDB Fatal: pool registration without type or matching compare fn");
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
    pthread_mutex_init(&pool->read_mutex, NULL); 
    pthread_mutex_init(&pool->write_mutex, NULL); 

    return pool;
}

// Register data pool with rDB, returns a pool handler to be used with future
// function calls.
// key_offset is the offset form the start of the user data, ignoring the pp_t[]
// at the start of the structure - however it is calculated and stored as offset
// from the top of the structure.

rdb_pool_t *rdb_register_um_pool (char *poolName, 
	int idxCount, int key_offset, int FLAGS, void *fn)
{
    rdb_pool_t *pool;

    pthread_mutex_lock(&reg_mutex);

    if (rdb_find_pool_by_name (poolName) != NULL) {
        rdb_error ("FATAL: Attempt to register an existing rdb pool (by name)");
        pool = NULL;
    } else {
        pool = rdb_add_pool (poolName, idxCount, key_offset, FLAGS, fn);
    }
    pthread_mutex_unlock(&reg_mutex);

    return pool;
}

void rdb_clean(void) {
    rdb_pool_t *pool, *pool_next;

    if (pool_root != NULL) {
        pool = pool_root;

        while (pool != NULL) {
            pool_next = pool->next;
            rdb_free (pool->name);
            rdb_free (pool);
            pool = pool_next;
        }
    }

    pool_root = NULL;
    return ;
}

int rdb_register_um_idx (rdb_pool_t *pool, int idx, int key_offset,
        int FLAGS, void *compare_fn) {
    pthread_mutex_lock(&reg_mutex);
    if (idx == 0)
        return (rdb_error_value (-1, 
            "Index 0 (zero) can only be set via rdb_register_pool"));

    if (idx >= RDB_POOL_MAX_IDX)
        return (rdb_error_value (-2, "Index >= RDB_INDEX_MAX_IDX"));

    if (pool->FLAGS[idx] != 0)
        return (rdb_error_value (-3, "Redefinition of used index not allowed"));

    if (compare_fn)                 pool->fn[idx] = compare_fn;
    else if (FLAGS & RDB_KINT32)    pool->fn[idx] = key_cmp_int32;
    else if (FLAGS & RDB_KUINT32)   pool->fn[idx] = key_cmp_uint32;
    else if (FLAGS & RDB_KINT64)    pool->fn[idx] = key_cmp_int64;
    else if (FLAGS & RDB_KUINT64)   pool->fn[idx] = key_cmp_uint64;
    else if (FLAGS & RDB_KINT16)    pool->fn[idx] = key_cmp_int16;
    else if (FLAGS & RDB_KUINT16)   pool->fn[idx] = key_cmp_uint16;
    else if (FLAGS & RDB_KINT8)     pool->fn[idx] = key_cmp_int8;
    else if (FLAGS & RDB_KUINT8)    pool->fn[idx] = key_cmp_uint8;
    else if (FLAGS & RDB_KINT128)   pool->fn[idx] = key_cmp_int128;
    else if (FLAGS & RDB_KUINT128)  pool->fn[idx] = key_cmp_uint128;
    else if (FLAGS & RDB_KSTR)      pool->fn[idx] = key_cmp_str;
    else if (FLAGS & RDB_KPSTR)     pool->fn[idx] = key_cmp_str; //TODO is thsi right ?
//    else if (FLAGS & RDB_KTME)    pool->fn[idx] = keyCompareTME;
//    else if (FLAGS & RDB_KTMA)    pool->fn[idx] = keyCompareTMA;
    else if (FLAGS & (RDB_NOKEYS))  pool->fn[idx] = NULL;
    //else if (FLAGS & RDB_K4U32A)  pool->fn[idx] = keyCompare4U32A;
    else {
        return (rdb_error_value (-4,
            "Index Registration without valid type or compatr fn"));
    }

    pool->root[idx] = NULL;
    pool->key_offset[idx] = sizeof (PP_T) * pool->indexCount + key_offset;
    pool->FLAGS[idx] = FLAGS;
    debug ("registered index %d for pool %d, Keyoffset is %d\n", idx, hdl, key_offset);
    pthread_mutex_unlock(&reg_mutex);
    return (idx);
}

int key_cmp_int32 (int32_t *old, int32_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); // 8% lookup boost :)
}
int key_cmp_uint32 (uint32_t *old, uint32_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmpi_nt16 (int16_t *old, int16_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_uint16 (uint16_t *old, uint16_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_int8 (int8_t *old, int8_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_uint8 (uint8_t *old, uint8_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_int64 (int64_t *old, int64_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_uint64 (uint64_t *old, uint64_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_int128 (__int128_t *old, __int128_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
int key_cmp_uint128 (__uint128_t *old, __uint128_t *new)
{
    return (*new <  *old) ? -1 : (( *new > *old) ? 1 : 0); 
}
//TODO add 256 bit types

int key_cmp_str (char *old, char *new)
{
    return strcmp ( new, old);
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

//TODO have function accept union -save assignment

#ifdef DEBUG
int key_cmp_dbg (rdb_pool_t *pool, int index, void *old, void *new)
{
    rdb_key_union  *key,
                 *keyNew;
    key = old;
    keyNew = new;
    info ("pool %s, Add_a: %x : %x\n", pool->name, (unsigned long) old, (unsigned long) new);
    info ("pool %s, Add_b: %x : %x\n", pool->name, (unsigned long) keyNew, (unsigned long) key);
    info ("pool %s, FLAGS=%xn", pool->name, pool->FLAGS[index]);

    switch (pool->FLAGS[index] & (RDB_KEYS)) {
        case RDB_KPSTR:
            debug ("pool=%s, pstr %s : %s\n", pool->name, keyNew->pStr, key->pStr);
            break;

        case RDB_KSTR:
            debug ("str %s : %s\n", &keyNew->str, &key->str);
            break;

        case RDB_KINT8:
            debug ("Int: new: %d, old: %d = %d\n", keyNew->i8, key->i8,
                            (keyNew->i8 == key->i8) ? 0 : ((keyNew->i8 < key->i8) ? -1 : 1));
            break;

        case RDB_KINT16:
            debug ("Int: new: %d, old: %d = %d\n", keyNew->i16, key->i16,
                            (keyNew->i16 == key->i16) ? 0 : ((keyNew->i16 < key->i16) ? -1 : 1));
            break;

        case RDB_KINT32:
            debug ("Int: new: %ld, old: %ld = %l\n", keyNew->i32, key->i32,
                            (keyNew->i32 == key->i32) ? 0 : ((keyNew->i32 < key->i32) ? -1 : 1));
            break;

        case RDB_KINT64:
            debug ("Int: new: %lld, old: %lld = %d\n", keyNew->i64, key->i64,
                            (keyNew->i64 == key->i64) ? 0 : ((keyNew->i64 < key->i64) ? -1 : 1));
            break;

        case RDB_KUINT8:
            debug ("Int: new: %d, old: %d = %d\n", keyNew->u8, key->u8,
                            (keyNew->u8 == key->u8) ? 0 : ((keyNew->u8 < key->u8) ? -1 : 1));
            break;

        case RDB_KUINT16:
            debug ("Int: new: %d, old: %d = %d\n", keyNew->u16, key->u16,
                            (keyNew->u16 == key->u16) ? 0 : ((keyNew->u16 < key->u16) ? -1 : 1));
            break;

        case RDB_KUINT32:
            debug ("Int: new: %lu, old: %lu = %d\n", keyNew->u32, key->u32,
                            (keyNew->u32 == key->u32) ? 0 : ((keyNew->u32 < key->u32) ? -1 : 1));
            break;

        case RDB_KUINT64:
            debug ("Int: new: %llx, old: %llx = %d\n", keyNew->u64, key->u64,
                            (keyNew->u64 == key->u64) ? 0 : ((keyNew->u64 < key->u64) ? -1 : 1));
            break;

/*        case RDB_KTME:
            if (keyNew->tv.tv_sec != key->tv.tv_sec) return ((keyNew->tv.tv_sec < key->tv.tv_sec) ? -1 : 1);

            if (keyNew->tv.tv_usec != key->tv.tv_sec) return ((keyNew->tv.tv_usec < key->tv.tv_usec) ? -1 : 1);

            return 0;
        case RDB_KTMA:
            printoutalways ("tma: new: %ld:%ld.%ld\n", keyNew->tva.tv.tv_sec, keyNew->tva.tv.tv_usec,
                            keyNew->tva.acc);
            printoutalways ("tma: old: %ld:%ld.%ld\n", key->tva.tv.tv_sec, key->tva.tv.tv_usec, key->tva.acc);

            if (keyNew->tva.tv.tv_sec != key->tva.tv.tv_sec) return ((keyNew->tva.tv.tv_sec <
                        key->tva.tv.tv_sec) ? -1 : 1);

            if (keyNew->tva.tv.tv_usec != key->tva.tv.tv_usec) return ((keyNew->tva.tv.tv_usec <
                        key->tva.tv.tv_usec) ? -1 : 1);

            if (keyNew->tva.acc != key->tva.acc) return ((keyNew->tva.acc < key->tva.acc) ? -1 : 1);

            return 0;
*/
        default:                                   // can not be
            return 0;
    }

    return 0;
}
#endif

int     levels,
        maxLevels;

#ifdef DEBUG
inline void set_pointers (rdb_pool_t *pool, int index, void *start, PP_T ** ppk, void **dataHead)
#else
void set_pointers (rdb_pool_t *pool, int index, void *start, PP_T ** ppk, void **dataHead)
#endif
{
    if (start == NULL || (start == pool->root[index]))
        *ppk = (void *) pool->root[index] + (sizeof (PP_T) * index);
    else
        *ppk = (void *) start + (sizeof (PP_T) * index);

    *dataHead = (void *) *ppk - (sizeof (PP_T) * index);
    return;
}

void _rdb_dump (rdb_pool_t *pool, int index, void *start)
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

    if (pool->FLAGS[index] & RDB_BTREE) {
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
            _rdb_dump (pool, index, pp->left);
            levels--;
        }

        switch (pool->FLAGS[index] & (RDB_KEYS)) {
            case RDB_KPSTR:
#ifdef __i386__
                info ("Dump_ps: %s (%x)\n", key->pStr, (unsigned) &key->pStr);  //searchPrev);
#endif
#ifdef __x86_64__
                info ("Dump_ps: %s (%llx)\n", key->pStr, (unsigned long long) &key->pStr);  //searchPrev);
#endif
                break;

            case RDB_KSTR:
                info ("Dump_s : %s\n", &key->str);
                break;

            case RDB_KINT8:
                info ("Dump_i8 : %d\n", key->i8);
                break;

            case RDB_KINT16:
                info ("Dump_i16 : %d\n", key->i16);
                break;

            case RDB_KINT32:
                info ("Dump_i32 : %ld\n", (long) key->i32);
                break;

            case RDB_KINT64:
                info ("Dump_i64 : %lld\n", (long long) key->i64);
                break;

            case RDB_KUINT8:
                info ("Dump_u8 : %u\n", key->u8);
                break;

            case RDB_KUINT16:
                info ("Dump_u16 : %u\n", key->u16);
                break;

            case RDB_KUINT32:
                info ("Dump_u32 : %lu\n", (unsigned long) key->u32);
                break;

            case RDB_KUINT64:
                info ("Dump_u64 : %llu\n", (unsigned long long) key->u64);
                break;

            case RDB_KUINT128:
                info ("Dump_u128 : (%llx)\n", (unsigned long long) key->u128);
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
            _rdb_dump (pool, index, pp->right);
            levels--;
        }
    }

    if (start == NULL)
        debug ("Final Level=%d\n", maxLevels);

}

int rdb_lock(rdb_pool_t *pool) 
{
    return pthread_mutex_lock(&pool->write_mutex);
}

int rdb_unlock(rdb_pool_t *pool) 
{
    return pthread_mutex_unlock(&pool->write_mutex);
}

void rdb_dump (rdb_pool_t *pool, int index)
{
    if (pool->root[index] == NULL) return;

    if ((pool->FLAGS[index] & (RDB_KEYS)) != 0)
        _rdb_dump (pool, index, NULL);
}
//#define DEBUG
int _rdb_insert (rdb_pool_t *pool, void *data, void *start, int index, void *parent, int side)
{
    int     rc, rc2;
    void   *dataHead;

    if (data == NULL)
        return (-1);

#ifdef DEBUG
    printout ("Insert:AVL: pool=%s, idx=%d, start = %x\n",pool->name , (int) index, (unsigned long) start);
#endif

    if ((pool->FLAGS[index] & RDB_BTREE) == RDB_BTREE) {
        PP_T   *ppk,
               *ppkNew;
        PP_T   *ppkParent = NULL;               // NULL is here to make compiler happy
        PP_T   *ppkRotate;
        PP_T   *ppkBottom;

        if (pool->FLAGS[index] & (RDB_NOKEYS)) {
            //printf("A\n");
            if (pool->root[index] == NULL) {
                pool->root[index] = pool->tail[index] = data;
                ppkNew = data + (sizeof (PP_T) * index);
                ppkNew->left = ppkNew->right = NULL;
                ppkNew->balance = 0;
            }
            else {   // added elemant
                if (pool->FLAGS[index] &
                        RDB_KFIFO) { //FIFO, add to tail, as we always read/remove from head forward
                    ppkParent = pool->tail[index] + (sizeof (PP_T) * index);
                    ppkNew = data + (sizeof (PP_T) * index);
                    ppkNew->left = ppkParent - index;
                    ppkNew->right = NULL;
                    ppkParent->right = ppkNew - index;
                    ppkNew->balance = 0;
                    pool->tail[index] = data;
                }
                else if (pool->FLAGS[index] &
                         RDB_KLIFO) {   //FIFO, add to head, as we always read/remove from head forward
                    ppkParent = pool->root[index] + (sizeof (PP_T) * index);
                    ppkNew = data + (sizeof (PP_T) * index);
                    ppkNew->right = ppkParent - index;
                    ppkNew->left = NULL;
                    ppkParent->left = ppkNew - index;
                    ppkNew->balance = 0;
                    pool->root[index] = data;
                }
            }
            //printf("B\n");

        }

        else if (pool->root[index] == NULL) {
#ifdef DEBUG
            printout ("Virgin Insert, pool=%s\n",pool->name);
#endif
            pool->root[index] = data;
#ifdef DEBUG
            printout ("ppkRoot  = %p,%p \n",  pool->root[index],
                       pool->root[index] + (sizeof (PP_T) * index));
#endif
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
            printout ("Datahead: %p, data: %p, ppkNew: %p, start%snull %p ? %p\n", dataHead,
                      data, ppkNew,
                      (start == NULL) ? "=" : "!=", dataHead + pool->key_offset[index],
                      (void *) (ppkNew) + pool->key_offset[index]);
            //keyCompDebug (pool, index, dataHead + pool->key_offset[index],
            //              (void *) ppkNew + pool->key_offset[index]);
#endif
        }
        else {
            set_pointers (pool, index, start, &ppk, &dataHead);
#ifdef DEBUG
            printout ("Pointers SET, start = %x\n", (unsigned long) (start));
#endif
            ppkNew = (void *) data + (sizeof (PP_T) * index);
#ifdef DEBUG
            //keyCompDebug (pool, index, dataHead + pool->key_offset[index],
            //              (void *) data + pool->key_offset[index]);
#endif

            //if ((rc = keyCompare (pool, index, dataHead + pool->key_offset[index],
            //                      (void *) data + pool->key_offset[index], 0, 0)) < 0) {
            if ((rc = pool->fn[index] (/*pool, index, */dataHead + pool->key_offset[index],
                                  (void *) data + pool->key_offset[index])) < 0) {
                // left side
                if (ppk->left == NULL) {
#ifdef DEBUG
                    printout ("Left Insert\n");
#endif
                    ppk->left = data;           //ppkNew;
                    ppk->balance -= 1;
                    ppkNew->left = ppkNew->right = NULL;
                    ppkNew->balance = 0;

                    if (ppk->balance != 0)
                        return 1;               // added a level
                    else
                        return 0;               // added a leaf... in an existing level
                }
                else {
                    // here we possibly added a level to the left tree
#ifdef DEBUG
                    printout ("Diving Left\n");
#endif

                    if ((rc2 = _rdb_insert (pool, data, ppk->left, index, start,
                                           RDB_TREE_LEFT)) == 1) {
                        // level added on left side
                        ppk->balance--;

                        if (ppk->balance == -2) {
                            // we need to rotate
                            ppkRotate = (void *) ppk->left + (sizeof (PP_T) * index);

                            if (ppkRotate->balance < 0) {
                                //left left case
#ifdef DEBUG
                                printout ("Left Left Rotate\n");
#endif

                                if (parent) {
                                    ppkParent = (void *) parent + (sizeof (PP_T) * index);

                                    if (!side)  //RDB_TREE_LEFT
                                        ppkParent->left = ppk->left;
                                    else
                                        ppkParent->right = ppk->left;
                                }
                                else
                                    pool->root[index] = ppk->left;      //ppkRotate;

                                ppk->left = ppkRotate->right;
                                ppkRotate->right = dataHead;    //ppk;
                                ppk->balance = -1 * (ppkRotate->balance + 1);
                                ppkRotate->balance = (ppkRotate->balance + 1);
                                ppk = ppkRotate;
                            }
                            else {
                                // left right case
#ifdef DEBUG
                                printout ("Left Right Rotate\n");
#endif
                                ppkBottom = (void *) ppkRotate->right + (sizeof (PP_T) * index);

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
                                    ppkParent = (void *) parent + (sizeof (PP_T) * index);

                                    if (!side)  //RDB_TREE_LEFT
                                        ppkParent->left = ppkBottom - index;
                                    else
                                        ppkParent->right = ppkBottom - index;
                                }
                                else pool->root[index] = ppkBottom - index;
                            }
                        }

                        if (ppk->balance >= 0)
                            return 0;           // no further balanceing action needed

                        if (ppk->balance == -1)
                            return 1;           // need to continue balancing up
                    }
                    else if (rc2 == -1) return -1;
                }
            }
            else if (rc > 0) {                  // addin on Right side
                //right side
                if (ppk->right == NULL) {
#ifdef DEBUG
                    printout ("Right Insert\n");
#endif
                    ppk->right = data;          //ppkNew;
                    ppk->balance += 1;
                    ppkNew->left = ppkNew->right = NULL;
                    ppkNew->balance = 0;

                    if (ppk->balance != 0)
                        return 1;               // added a level
                    else
                        return 0;               // added a leaf... in an existing level
                }
                else {
#ifdef DEBUG
                    printout ("Diving Right, start = %p, balance=%d\n", start, ppk->balance);
#endif

                    if (( rc2 = _rdb_insert (pool, data, ppk->right, index, start,
                                            RDB_TREE_RIGHT)) == 1) {
                        // level added on right side
                        ppk->balance++;

                        if (ppk->balance == 2) {
                            // we need to rotate
                            ppkRotate = (void *) ppk->right + (sizeof (PP_T) * index);

                            if (ppkRotate->balance > 0) {
                                //right right case ( left rotation )
#ifdef DEBUG
                                printout ("Right Right Rotate, parent = %x,index=%d\n", (unsigned) parent, index);
#endif

                                if (parent != NULL) {
#ifdef DEBUG
                                    printout ("parent exist\n");
#endif
                                    ppkParent = (void *) parent + (sizeof (PP_T) * index);

                                    if (!side)  //RDB_TREE_LEFT
                                        ppkParent->left = ppk->right;
                                    else
                                        ppkParent->right = ppk->right;
                                }
                                else
                                    pool->root[index] = ppk->right;     //ppkRotate;

                                ppk->right = ppkRotate->left;
                                ppkRotate->left = dataHead;     //ppk;
                                ppk->balance = -1 * (ppkRotate->balance - 1);
                                ppkRotate->balance = (ppkRotate->balance - 1);
                                ppk = ppkRotate;
#ifdef DEBUG
                                printout ("pool->root[%d] = %x\n", index, pool->root[index]);
#endif
                            }
                            else {
                                // right left case ( right rotation followed by left rotation )
#ifdef DEBUG
                                printout ("Right Left Rotate\n");
#endif
                                ppkBottom = (void *) ppkRotate->left + (sizeof (PP_T) * index);

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
                                    ppkParent = (void *) parent + (sizeof (PP_T) * index);

                                    if (!side)  //RDB_TREE_LEFT
                                        ppkParent->left = ppkBottom - index;
                                    else
                                        ppkParent->right = ppkBottom - index;
                                }
                                else pool->root[index] = ppkBottom - index;
                            }
                        }

                        if (ppk->balance <= 0)
                            return 0;           // no further balanceing action needed

                        if (ppk->balance == 1)
                            return 1;           // need to continue balancing up
                    }
                    else if (rc2 == -1) return -1;
                }
            }
            else {
#ifdef DEBUG
                printout ("Skipped due to multiple key on pool %s index %d\n", pool->name, index);
                //keyCompDebug (pool, index, dataHead + pool->key_offset[index], /*ppkNew */ data +
                //              pool->key_offset[index]);
#endif
                return (-1);
            }                                   // multiple keys not yet supported!
        }

        return 0;
    }

    return -1;                                  // we found no mechanizm to add node


}

// return shoud be the # of updated indexes, which must maych the number of defined indexes, anything less shows an error
// TODO? should we make this atomic - meaning if one index failes fo insert, delete the previously inserted ones?

int rdb_insert (rdb_pool_t *pool, void *data)
{
    int     indexCount;
    int     rc = 0;
  
    if (data != NULL)
        for (indexCount = 0; indexCount < pool->indexCount; indexCount++) 
            (_rdb_insert (pool, data, pool->root[indexCount], 
                indexCount, NULL, 0) < 0) ? rc : rc++;
    else
        rc = -1;

    return rc;
}

// only insert one index (asuming this index was removed and updated prior).
int rdb_insert_one (rdb_pool_t *pool, int index, void *data)
{
    return _rdb_insert (pool, data, pool->root[index], index, NULL, 0) ;
}

void   *_rdb_get (rdb_pool_t *pool, int index, void *data, void *start, int partial)
{
    PP_T   *ppk;
    int     rc;
    void   *dataHead;

    if ((pool->FLAGS[index] & RDB_BTREE) == RDB_BTREE) {

        if (pool->root[index] == NULL) {
#ifdef DEBUG
            printout("GetFail - pool=%s, Null rool node\n",pool->name);
#endif
            return (NULL);
        }
        else {
            set_pointers (pool, index, start, &ppk, &dataHead);

            if (data == NULL ) {
                debug("GetFail???\n");
                return (dataHead);              // special case, return root node
            }

#ifdef DEBUG
            key_cmp_dbg (pool, index, dataHead + pool->key_offset[index], data);
#endif
            if ((rc = pool->fn[index] (/*pool, index,*/ dataHead + pool->key_offset[index],
                                  (void *) data)) < 0) {
            //if ((rc = keyCompare (pool, index, dataHead + pool->key_offset[index], data, 1,
            //                      partial)) < 0) {
                // left side
                debug("Left Get\n");

                if (ppk->left == NULL) {
                    return (NULL);
                }
                else
                    return (_rdb_get (pool, index, data, ppk->left, partial));
            }
            else if (rc > 0) {
                //right side
                debug("Right Get\n");

                if (ppk->right == NULL) {
                    return (NULL);
                }
                else
                    return (_rdb_get (pool, index, data, ppk->right, partial));
            }
            else {
                debug("Get:Done\n");
                return (dataHead);              // multiple keys n tree form not supported! if we here, we found out needle
            }
        }

    }

    return NULL;                                //should never eet here
}

//as a special case, if data = null, root node will be returned.
void   *rdb_get (rdb_pool_t *pool, int idx, void *data)
{
    debug("Get:pool=%s,idx=%d", pool->name, idx);
    return _rdb_get (pool, idx, data, NULL, 0);
}

void   *_rdb_get_neigh (rdb_pool_t *pool, int index, void *data, void *start, int partial,
                      void **before, void **after)
{
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

#ifdef DEBUG
            key_cmp_dbg (pool, index, dataHead + pool->key_offset[index], data);
#endif

            if ((rc = pool->fn[index] (/*pool, index,*/ dataHead + pool->key_offset[index],
                                  (void *) data)) < 0) {
            //if ((rc = keyCompare (pool, index, dataHead + pool->key_offset[index], data, 1,
            //                      partial)) < 0) {
                // left side
                debug("Left Get\n");

                *after = (NULL == start) ? (void *) pool->root[index] : start ;

                //*after=start;
                if (ppk->left == NULL) {
                    return (NULL);
                }
                else
                    return (_rdb_get_neigh (pool, index, data, ppk->left, partial, before, after));
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
                    return (_rdb_get_neigh (pool, index, data, ppk->right, partial, before, after));
            }
            else {
                debug("Get:Done\n");
                *after = *before = NULL;
                return (dataHead);              // multiple keys n tree form not supported! if we here, we found out needle
            }
        }

    }

    return NULL;                                //should never eet here
}
void   *rdb_get_neigh (rdb_pool_t *pool, int idx, void *data, void **before, void **after)
{
    return _rdb_get_neigh (pool, idx, data, NULL, 0, before, after);
}

inline int _rdb_delete_by_pointer (rdb_pool_t *pool, void *parent, int index, PP_T * ppkDead,
                                int side);
int _rdb_delete (rdb_pool_t *pool, int lookupIndex, void *data, void *start, PP_T * parent,
                int side);
#define RDBFE_NODE_DELETED 1
#define RDBFE_NODE_RESUME_ON 2
#define RDBFE_NODE_DONE 4
#define RDBFE_NODE_FIND_NEXT 8

#define RDBFE_ABORT 32

int _rdb_for_each (rdb_pool_t *pool, int index, int fn (void *, void *), void *data,
                 void del_fn(void *, void*), void *delfn_data, void *start, void *parent, int side, void **resumePtr)
{
    void   *dataHead;
    char   **dataField;
    PP_T   *pp, *pr;
    int     rc, rc2 = 0;
    int     indexCount;

rfeStart:

    if (pool->FLAGS[index] & RDB_BTREE) {
        set_pointers (pool, index, start, &pp, &dataHead);

        if (pool->FLAGS[index] & (RDB_NOKEYS)) { // FIFO/LIFO, no need to recurse -
            if (*resumePtr) {
                set_pointers (pool, index, *resumePtr, &pp, &dataHead); // jump to resumePtr....
                *resumePtr = NULL;
            }
            else
                set_pointers (pool, index, start, &pp, &dataHead); // jump to resumePtr....
        }
        else {
            if (*resumePtr != NULL) {
                if ((pool->fn[index] (/*pool, index,*/ dataHead + pool->key_offset[index],
                                  (void *) resumePtr + pool->key_offset[index])) < 0) {
                //if ( keyCompare (pool, index, dataHead + pool->key_offset[index],
                //                 *resumePtr + pool->key_offset[index], 0, 0) < 0) {
                    //printoutalways("->left\n");
                    if ( 1 == ( 1 & (rc =  _rdb_for_each (pool, index, fn, data, del_fn, delfn_data, pp->left, start,
                                                        RDB_TREE_LEFT, resumePtr)))) { // tree may have been modified
                        return (rc + 1 ); // moving bit left- getting RDBFE_NODE_RESUME_ON and not changing abort status;
                    }
                    else if ( rc & RDBFE_NODE_FIND_NEXT ) {
                        *resumePtr = dataHead;
                        return rc - 6 ; //2; (leave abort status in if exist)
                    }
                    else if (rc > 1) return rc;
                }
            }
            else {
                if (pp->left != NULL) {
                    //printoutalways("->left\n");
                    if ( 1 == ( 1 & (rc =  _rdb_for_each (pool, index, fn, data, del_fn, delfn_data, pp->left, start,
                                                        RDB_TREE_LEFT, resumePtr)))) { // tree may have been modified
                        return (rc + 1 ) ; //RDBFE_NODE_RESUME_ON);
                    }
                    else if ( rc & RDBFE_NODE_FIND_NEXT ) {
                        *resumePtr = dataHead;
                        return rc - 6 ; //
                    }
                    else if (rc > 1) return rc;
                }
            }
        }

        if (*resumePtr != NULL
                &&  (dataHead == *resumePtr)) *resumePtr = NULL; // time to start working again

        if (*resumePtr == NULL) {
            rc = 0;

            if (fn == NULL || RDB_CB_DELETE_NODE == (rc2 = fn (dataHead, data))
                    || RDB_CB_DELETE_NODE_AND_ABORT == rc2 || RDB_CB_ABORT == rc2 ) {

                if (rc2 == RDB_CB_DELETE_NODE_AND_ABORT) rc =  RDBFE_NODE_DELETED | RDBFE_ABORT ;
                else if (rc2 == RDB_CB_ABORT) return RDBFE_ABORT;
                else rc = RDBFE_NODE_DELETED;

                // setting resume pointer to next record
                if (pool->FLAGS[index] & (RDB_NOKEYS)) { // FIFO/LIFO, no need to recurse -
                    if (pp->right) {
                        *resumePtr = (void *) pp->right - (sizeof (PP_T) * index);
                    }
                    else *resumePtr = NULL ;   // we are the last one;
                }
                else {   //TREE
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
                            *resumePtr = NULL; // we are removing root and there is nothing to it's right, we are done
                            rc = RDBFE_NODE_DONE | (rc & RDBFE_ABORT);               // 4; //signal we are done
                        }

                    }
                    else {   //parent is not null
                        if (side == 0) {

                            *resumePtr = parent;

                        }
                        else {
                            *resumePtr = NULL;
                            rc = RDBFE_NODE_FIND_NEXT | (rc & RDBFE_ABORT) ; //5; // signal we still need t fine next pointer
                        }
                    }
                }

                for (indexCount = 0; indexCount < pool->indexCount; indexCount++) {
                    debug ("rdbForEach: Delete #%d\n", indexCount);
                    _rdb_delete (pool, indexCount, dataHead, NULL, NULL, 0);
                }

                if (del_fn) del_fn(dataHead, delfn_data);
                else { // courtesy delete of data block and dynamic indexes

                    for (indexCount = 0; indexCount < pool->indexCount;
                            indexCount++) if (pool->FLAGS[indexCount] &
                                                  RDB_KPSTR) { //we have an index which is a pointer, need to free it too.
                            dataField = dataHead + pool->key_offset[indexCount];
                            //                    printoutalways("off = %d add %x, str %s\n",pool->key_offset[indexCount],(unsigned) *dataField, *dataField);
#ifdef KM

                            if (*dataField) kfree(*dataField);

#else

                            if (*dataField) free(*dataField);

#endif

                        }

#ifdef KM

                    if (dataHead) kfree (dataHead);

#else

                    if (dataHead) free (dataHead);

#endif
                }

                return rc;
            }
        }

        if (pool->FLAGS[index] & (RDB_NOKEYS)) { // FIFO/LIFO, no need to recurse -
            start = pp->right;

            if (start) goto rfeStart;
        }
        else if (pp->right != NULL) {
            //printoutalways("->right\n");
            if ( 1 == ( 1 & ( rc = _rdb_for_each (pool, index, fn, data, del_fn, delfn_data, pp->right, start,
                                                RDB_TREE_RIGHT, resumePtr)))) {
                return ( rc + 1 ); // moving bit left- getting RDBFE_NODE_RESUME_ON and not changing abort status;
            }
            else if ( rc > 1 ) return rc;
        }

    }

    return 0;
}

/* rdbIterateDelete will scan the tree, in order, by index, calling fn on each node (if not null).
 * if fn returns RDB_DB_DELETE_NODE, or if fn is null, the tree node will be deleted from rdb and del_fn will be called.
 * del_fn is called after node is removed from all trees and it's purpose is to free any allocated memory and any other needed clean up.
 * def_fn gets a pointer to the node head, and a potential arg
 * del_fn is optional but is highly recommanded. if it is missing (NULL), rdb will free ptr for you, but it will not know how to free
 * any dynamic allocations tied to it (except PSTR index fields, which is will free), so If there are any, and del_fn is null, memoty leak will occur.
 */

//int _rdbForEach (rdb_pool_t *pool, int index, int fn (void *, void *), void *data, void del_fn(void *, void*),void *delfn_data, void *start, void *parent, int side, void **resumePtr)
void rdbi_iterate(rdb_pool_t *pool, int index, int fn(void *, void *),
         void *fn_data, void del_fn(void *, void *), void *del_data)
{
    void        *resumePtr;
    int         rc = 0;

    resumePtr = NULL;

    if (pool->root[index] == NULL)
        return;

    do {
        rc = _rdb_for_each (pool, index, fn, fn_data, del_fn, del_data, pool->root[index], NULL, 0,
                          &resumePtr);
    }
    while (rc != 0 && ( rc & RDBFE_ABORT ) != RDBFE_ABORT && resumePtr != NULL);


}

/* This will delete all the nodes in the tree,
 * since we are destroying the tree, we do not care about fixing parent pointers, re-balancing etc,
 * this meand there is no before and after delete function, but only one fn which serve as both.
 * this fn should free the allocated memory (ptr), and any additional dynamic allocations tied to it.
 * fn is optional but is highly recommanded. if it is missing (NULL), rdb will free ptr for you, but it will not know how to free
 * any dynamic allocations tied to it, so If there is any, and fn is null, memoty leak will occur.
 */
void _rdb_flush( rdb_pool_t *pool, void *start, void fn( void *, void *), void *fn_data)
{
    void   *dataHead;
    PP_T   *pp;
    void   **dataField;
    int     indexCount;

    if (pool->FLAGS[0] & RDB_BTREE) {
        //inline void setPointers (rdb_pool_t *pool, int index, void *start, PP_T ** ppk, void **dataHead)
        set_pointers( pool, 0, start, &pp, &dataHead);

        if (pp->left != NULL)
            _rdb_flush( pool, pp->left, fn, fn_data );

        if (pp->right != NULL)
            _rdb_flush( pool, pp->right, fn, fn_data );

        if (NULL != fn) fn(dataHead, fn_data);
        else { // courtesy delete of data block

            for (indexCount = 0; indexCount < pool->indexCount;
                    indexCount++) if (pool->FLAGS[indexCount] &
                                          RDB_KPSTR) { //we have an index which is a pointer, need to free it too.
                    dataField = dataHead + pool->key_offset[indexCount];
                    //                printoutalways("off = %d add %x, str %s\n",pool->key_offset[indexCount],(unsigned) *dataField, *dataField);
#ifdef KM

                    if (*dataField) kfree(*dataField);

#else

                    if (*dataField) free(*dataField);

#endif
                }

#ifdef KM

            if (dataHead) kfree (dataHead);

#else

            if (dataHead) free (dataHead);

#endif
        }
    }
}

void _rdb_flush_list( rdb_pool_t *pool, void *start, void fn( void *, void *), void *fn_data)
{
    void   *dataHead;
    PP_T   *pp;
    void   **dataField;
    int     indexCount;

    if (pool->FLAGS[0] & RDB_BTREE)
        do {
            //inline void setPointers (rdb_pool_t *pool, int index, void *start, PP_T ** ppk, void **dataHead)
            set_pointers( pool, 0, start, &pp, &dataHead);

            start = pp->right;

            if (NULL != fn) fn(dataHead, fn_data);
            else { // courtesy delete of data block

                for (indexCount = 0; indexCount < pool->indexCount;
                        indexCount++) if (pool->FLAGS[indexCount] &
                                              RDB_KPSTR) { //we have an index which is a pointer, need to free it too.
                        dataField = dataHead + pool->key_offset[indexCount];
                        //                printoutalways("off = %d add %x, str %s\n",pool->key_offset[indexCount],(unsigned) *dataField, *dataField);
#ifdef KM

                        if (*dataField) kfree(*dataField);

#else

                        if (*dataField) free(*dataField);

#endif
                    }

#ifdef KM

                if (dataHead) kfree (dataHead);

#else

                if (dataHead) free (dataHead);

#endif
            }
        }
        while (start != NULL);
}

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

}




/* In order to delete and keep an AVL tree balanced, when we may have multiple indexes, we need to do the following:
 * 1) do an rdbGet on aquire a 'data' pointer so we can read all indexes and delete the item out of all trees. this is done by rdbDelete.
 * 2) next recursive function '_rdbDelete' function is called, (once for each index). if will find the pointer to the data to be deleted (ppkDead), as well as a 'data' pointer to the parent.
 * 3) now we call _rdbDeleteByPointer to actually un-line the data block from the tree... we have the following cases...
 * 3.1) unlink block is a leaf (no childrens), we just update parent pointers to NULL and return indicator to parent to re-balance tree if needed
 */


/* In orfer to delete a node we need to do the following:
 * 1) find the parent node (if we are not deleting the root node) so new subtree can be linked to it (*parent)
 * 2) find the node to be deleted , same is in normal lookup (this is how we get the parent). (*ppkDead)
 * 3) if deleted node only has one child, link ppkLeft Or ppkRight to parent and we are done
 * 4) if we have 2 children set *ppkLeft and *ppkRight
 * 4a) find most right point of left child (*ppkHook)
 * 5) ppkHook->right = ppkRight
 * 6) parent->side = ppkLeft...
 *
 * now lets see me code this - no bugs
 */
//TODO inline this
int _rdb_delete_by_pointer (rdb_pool_t *pool, void *parent, int index, PP_T * ppkDead, int side)
{

    PP_T   *ppkParent = NULL;                   // NULL is here to make compiler happy
    PP_T   *ppkLeft;
    PP_T   *ppkRight;
    PP_T   *ppkHook;

    if (parent)
        ppkParent = (void *) parent + (sizeof (PP_T) * index);

    debug("deleteByPtr:  pool=%s, parent %x\n", pool->name,  (unsigned) parent);

    if (ppkDead->right == NULL && ppkDead->left == NULL) {
        // no children, only need to fix parent if exist
        debug("deleteByPtr: no Children : inside:  parent %x\n", (unsigned) parent);

        if (parent) {
            if (side)                           // we ware on the right
                ppkParent->right = NULL;
            else
                ppkParent->left = NULL;
        }
        else pool->root[index] = NULL ;

        return PARENT_BAL_CNG;                  // we just deleted leaf, parent need to update balance.
    }
    else if (ppkDead->right && (ppkDead->left == NULL)) {
        // one child on the right
        if (parent) {
            if (side)
                ppkParent->right = ppkDead->right;      // we hooked up parent to his new child
            else
                ppkParent->left = ppkDead->right;
        }
        else
            pool->root[index] = ppkDead->right; //ppkDead child now become root node

        return PARENT_BAL_CNG;                  // parent need to update balance.
    }
    else if ((ppkDead->right == NULL) && ppkDead->left) {
        // one chile on left
        if (parent) {
            if (side)
                ppkParent->right = ppkDead->left;       // we hooked up parent to his new child
            else
                ppkParent->left = ppkDead->left;
        }
        else
            pool->root[index] = ppkDead->left;  //ppkDead child now become root node

        return PARENT_BAL_CNG;                  // parent need to update balance.
    }
    else {
        // two children
#ifdef KM
#ifdef __i386__
        info ("rdbDeleteByPointer:a: I should never get here ~~~~~!!!!!index = %d, %x %x\nData tree may be corrupt!\n",
                       index, (unsigned) ppkDead->left, (unsigned) ppkDead->right);
#endif
#ifdef __x86_64__
        info ("rdbDeleteByPointer:a: I should never get here ~~~~~!!!!!index = %d, %llx %llx\nData tree may be corrupt!\n",
                       index, (unsigned long long) ppkDead->left, (unsigned long long) ppkDead->right);
#endif
        return 0;
#else
#ifdef __i386__
        info ("rdbDeleteByPointer:a: I should never get here ~~~~~!!!!!index = %d, %x %x\n", index,
                       (unsigned) ppkDead->left, (unsigned) ppkDead->right);
#endif
#ifdef __x86_64__
        info ("rdbDeleteByPointer:a: I should never get here ~~~~~!!!!!index = %d, %llx %llx\n",
                       index, (unsigned long long) ppkDead->left, (unsigned long long) ppkDead->right);
#endif
        exit(1);
#endif
        ppkRight = (void *) ppkDead->right + (sizeof (PP_T) * index);

        if (ppkRight->right && (ppkRight->left == NULL)) { //special case suck up one level
            if (parent) {
                if (side)
                    ppkParent->right = (void *) ppkRight - (sizeof (PP_T) * index);
                else
                    ppkParent->left = (void *) ppkRight - (sizeof (PP_T) * index);
            }
            else
                pool->root[index] = (void *) ppkRight - (sizeof (PP_T) * index);

            ppkRight->left = ppkDead->left;
            return PARENT_BAL_CNG;

        }

#ifdef KM
#ifdef __x86_64__
        printoutalways("rdbDeleteByPointer:a: I should never get here ~~~~~!!!!!index = %d, %llx %llx\nData tree may be corrupt!\n",
                       index, (unsigned long long) ppkDead->left, (unsigned long long) ppkDead->right);
        printoutalways("rdbDeleteByPointer:b: I should never get here ~~~~~!!!!!index = %d, %llx %llx\n",
                       index, (unsigned long long) ppkRight->left, (unsigned long long) ppkRight->right);
#endif
#ifdef __i386__
        printoutalways("rdbDeleteByPointer:a: I should never get here ~~~~~!!!!!index = %d, %x %x\nData tree may be corrupt!\n",
                       index, (unsigned) ppkDead->left, (unsigned) ppkDead->right);
        printoutalways("rdbDeleteByPointer:b: I should never get here ~~~~~!!!!!index = %d, %x %x\n", index,
                       (unsigned) ppkRight->left, (unsigned) ppkRight->right);
#endif
        return 0;
#else
#ifdef __x86_64__
        info ("rdbDeleteByPointer:a: I should never get here ~~~~~!!!!!index = %d, %llx %llx\n",
                       index, (unsigned long long) ppkDead->left, (unsigned long long) ppkDead->right);
        info ("rdbDeleteByPointer:b: I should never get here ~~~~~!!!!!index = %d, %llx %llx\n",
                       index, (unsigned long long) ppkRight->left, (unsigned long long) ppkRight->right);
        exit(1);
#endif
#ifdef __i386__
        info ("rdbDeleteByPointer:a: I should never get here ~~~~~!!!!!index = %d, %x %x\n", index,
                       (unsigned) ppkDead->left, (unsigned) ppkDead->right);
       	info ("rdbDeleteByPointer:b: I should never get here ~~~~~!!!!!index = %d, %x %x\n", index,
                       (unsigned) ppkRight->left, (unsigned) ppkRight->right);
        exit(1);
#endif
#endif
        ppkHook = ppkLeft = (void *) ppkDead->left + (sizeof (PP_T) * index);

        while (ppkHook->right) {
            ppkHook = (void *) ppkHook->right + (sizeof (PP_T) * index);
        }

        ppkHook->right = (void *) ppkRight - (sizeof (PP_T) * index);

        if (parent) {
            if (side)
                ppkParent->right = (void *) ppkLeft - (sizeof (PP_T) * index);
            else
                ppkParent->left = (void *) ppkLeft - (sizeof (PP_T) * index);
        }
        else
            pool->root[index] = (void *) ppkLeft - (sizeof (PP_T) * index);
    }
}

int _rdb_delete (rdb_pool_t *pool, int lookupIndex, void *data, void *start, PP_T * parent, int side)
{
    PP_T   *ppkDead;
    PP_T   *ppkParent;
    PP_T   *ppkRotate;
    PP_T   *ppkBottom;
    int     rc,
            rc2;
    void   *dataHead;

    if (pool->FLAGS[lookupIndex] & (RDB_NOKEYS)) {
        void   *ptr = NULL;                         // null to sashhh the compiler
        int		indexCount;
        PP_T   *ppk,
               *ppkRight,
               *ppkLeft;
        ptr = data; //pool->root[lookupIndex];// + (sizeof (PP_T) * lookupIndex);
        indexCount = lookupIndex;
        {
            if (pool->FLAGS[indexCount] & (RDB_NOKEYS)) {
                ppk = ptr + (sizeof (PP_T) * indexCount);

                if (ppk->left) ppkLeft = ppk->left + indexCount;
                else ppkLeft = NULL;

                if (ppk->right) ppkRight = ppk->right + indexCount;
                else ppkRight = NULL;

                if (pool->root[indexCount] == pool->tail[indexCount]) pool->root[indexCount] =
                        pool->tail[indexCount] = NULL; //we were the last item
                else { //if (pool->root[indexCount] == ptr) { // not only one - first
                    if (ppkLeft) ppkLeft->right = ppk->right;

                    if (ppkRight) ppkRight->left = ppk->left;

                    if (pool->root[indexCount] == ptr) pool->root[indexCount] = ppkRight - indexCount;

                    if (pool->tail[indexCount] == ptr) pool->tail[indexCount] = ppkLeft - indexCount;
                }
            }
        }
    }
    else if ((pool->FLAGS[lookupIndex] & RDB_BTREE) == RDB_BTREE) {
        debug("Delete:start: \n");

        if (pool->root[lookupIndex] == NULL) {
            return (0);
        }
        else {
            set_pointers (pool, lookupIndex, start, &ppkDead, &dataHead);
            debug("Delete:before compare: \n");

            if ((rc = pool->fn[lookupIndex] (/*pool, lookupIndex,*/ dataHead + pool->key_offset[lookupIndex],
                                  (void *) data + (pool->key_offset[lookupIndex]))) != 0) {
            //if ((rc = keyCompare (pool, lookupIndex, dataHead + pool->key_offset[lookupIndex],
            //                      data + (pool->key_offset[lookupIndex]), 0, 0)) != 0) {
retest_delete_cond:
                debug("Delete:compare: %d idx %d\n", rc, lookupIndex);
                rc2 = 0;

                if (rc < 0) {
                    // left child
                    debug("D:left Child!\n");

                    if (ppkDead->left == NULL) {
                        return 0;
                    }

                    //		    printout("My Bal Before %d\n",ppkDead->balance);
                    rc2 = _rdb_delete (pool, lookupIndex, data, ppkDead->left, dataHead /*start*/, RDB_TREE_LEFT);
                    debug("My Bal After  %d %d\n", ppkDead->balance, rc2);
                }
                else if (rc > 0) {
                    //right child
                    debug("D:right Child!\n");

                    if (ppkDead->right == NULL) {
                        return 0;
                    }

                    //		    printout("My Bal Before %d\n",ppkDead->balance);
                    rc2 = _rdb_delete (pool, lookupIndex, data, ppkDead->right, dataHead /*start*/, RDB_TREE_RIGHT);
                    debug("My Bal After  %d %d\n", ppkDead->balance, rc2);
                }

                if (PARENT_BAL_CNG == rc2) { // and i am the parent...
                    // _rdbDelete (id, lookupIndex, data, ppkDead->right, start, RDB_TREE_RIGHT))a
                    ppkParent = (PP_T *) parent + lookupIndex;
                    ppkParent = parent + lookupIndex;
                    //ppkParent->balance += 1 * (-1 * side);      //if on right side then add -1 - aka subtract one form balance
                    debug("- bal  to be changed = %d\n", ppkDead->balance);
                    //ppkDead->balance += (side) ? -1 : 1;      //if on right side then add -1 - aka subtract one form balance
                    ppkDead->balance += (rc > 0) ? -1 :
                                        1;      //if on right side then add -1 - aka subtract one form balance
                    debug("- bal changed = %d\n", ppkDead->balance);

                    //	                    if (ppkParent->balance == 0) return PARENT_BAL_CNG;      // we just deleted leaf, parent need to update balance.
                    //			    else return 0;
                    if (ppkDead->balance == -1 || ppkDead->balance == 1) {
                        // we moved from balance to -1 or +1 after delete, nothing more we need to do
                        //			printout("Returning 0\n");
                        return 0;
                    }
                    else if (ppkDead->balance == 0) {
                        return PARENT_BAL_CNG;  // we moved to zero by delete, need to keep balancing up
                    }
                    else if (ppkDead->balance < -1) {
                        // left case rotare
                        debug("Some left rotation ppkDead->left=%x\n", (unsigned) ppkDead->left);
                        ppkRotate = (PP_T *) ppkDead->left + lookupIndex;

                        if (ppkRotate->balance < 1) {
                            // left left rotate

                            debug ("Left Left Rotate\n");

                            if (parent) {
                                ppkParent = (PP_T *) parent + lookupIndex;

                                if (!side)      //RDB_TREE_LEFT
                                    ppkParent->left = ppkDead->left;
                                else
                                    ppkParent->right = ppkDead->left;
                            }
                            else
                                pool->root[lookupIndex] = ppkDead->left;      //ppkRotate;

                            ppkDead->left = ppkRotate->right;
                            ppkRotate->right = dataHead;        //ppk;
                            //ppkRotate->balance += 1;
                            //ppkDead->balance += 2;
                            ppkDead->balance = -1 * (ppkRotate->balance + 1);
                            ppkRotate->balance = (ppkRotate->balance + 1);
                            ppkDead = ppkRotate;

                            if (ppkRotate->balance == -1 || ppkRotate->balance == 1) return 0;
                            else return PARENT_BAL_CNG;

                        }
                        else {
                            // left right

                            debug ("Left Right Rotate\n");
                            ppkBottom = (void *) ppkRotate->right + (sizeof (PP_T) * lookupIndex);

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
                            ppkBottom->right = dataHead;        //ppk;
                            ppkDead = ppkBottom;

                            if (parent) {
                                ppkParent = (void *) parent + (sizeof (PP_T) * lookupIndex);

                                if (!side)      //RDB_TREE_LEFT
                                    ppkParent->left = ppkBottom - lookupIndex;
                                else
                                    ppkParent->right = ppkBottom - lookupIndex;
                            }
                            else {
                                pool->root[lookupIndex] =  ppkBottom - lookupIndex;
                            }

                            return PARENT_BAL_CNG;
                        }
                    }
                    else if (ppkDead->balance > 1) {
                        //rotate
                        ppkRotate = (PP_T *) ppkDead->right + lookupIndex;

                        if (ppkRotate->balance > -1) {
                            // right right rotate
                            debug ("Right Right Rotate, parent = %x,index=%d\n", (unsigned) parent, lookupIndex);

                            if (parent != NULL) {
                                debug ("parent exist\n");
                                ppkParent = (void *) parent + (sizeof (PP_T) * lookupIndex);

                                if (!side)      //RDB_TREE_LEFT
                                    ppkParent->left = ppkDead->right;
                                else
                                    ppkParent->right = ppkDead->right;
                            }
                            else
                                pool->root[lookupIndex] = ppkDead->right;     //ppkRotate;

                            ppkDead->right = ppkRotate->left;
                            ppkRotate->left = dataHead; //ppk;
                            //                            ppkRotate->balance -= 1;
                            //                            ppkDead->balance -= 2;
                            ppkDead->balance = -1 * (ppkRotate->balance - 1);
                            ppkRotate->balance = (ppkRotate->balance - 1);
                            ppkDead = ppkRotate;

                            if (ppkRotate->balance == -1 || ppkRotate->balance == 1) return 0;
                            else return PARENT_BAL_CNG;

                        }
                        else {
                            // right left case
                            debug ("Right Left Rotate\n");
                            ppkBottom = (void *) ppkRotate->left + (sizeof (PP_T) * lookupIndex);

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
                                ppkParent = (void *) parent + (sizeof (PP_T) * lookupIndex);

                                if (!side)      //RDB_TREE_LEFT
                                    ppkParent->left = ppkBottom - lookupIndex;
                                else
                                    ppkParent->right = ppkBottom - lookupIndex;
                            }
                            else {
                                pool->root[lookupIndex] = ppkBottom - lookupIndex;
                            }

                            return PARENT_BAL_CNG;
                        }
                    }

                }
            }
            else {
                debug("Delete:compare:- %d idx %d ppkDead = %x\n", rc, lookupIndex, (unsigned) ppkDead);

                //if (rc < 0)
                if (ppkDead->right != NULL && ppkDead->left != NULL) { // we need to perform a pre-delete swap!
                    PP_T *ppkTemp;

                    debug("Rotate 1 ppkDead %x\n", (unsigned) ppkDead);
                    ppkRotate = (PP_T *) ppkDead->right + lookupIndex;
                    ppkTemp = NULL; //ppkDead;

                    if (ppkRotate->left) {
                        while (ppkRotate->left) {
                            //			    printout("Rotate x\n");
                            ppkTemp   = ppkRotate;
                            ppkRotate = (PP_T *) ppkRotate->left + lookupIndex;
                        }

                        // here we have the smallest element of the right tree, now we need to perform the swap
                        if (parent) {
                            ppkParent = (PP_T *) parent + lookupIndex;
                            debug("D:We have a parent\n");

                            if (side) ppkParent->right = (PP_T *) ppkRotate - lookupIndex;
                            else ppkParent->left = (PP_T *) ppkRotate - lookupIndex;
                        }
                        else pool->root[lookupIndex] = ppkRotate - lookupIndex;       //ppkRotate;

                        ppkBottom = ppkDead->right;
                        ppkDead->right = ppkRotate->right;
                        ppkRotate->right = ppkBottom;

                        ppkBottom = ppkDead->left;
                        ppkDead->left = ppkRotate->left;
                        ppkRotate->left = ppkBottom;

                        rc = ppkRotate->balance;
                        ppkRotate->balance = ppkDead->balance;
                        ppkDead->balance = rc;

                        if (ppkTemp) ppkTemp->left = ppkDead - lookupIndex ; // if ppkRotate is now root, it has no father
                    }
                    else {   // special case for root deletion with no left node on right side, we'll just swap ppkDead with ppkRotate
                        debug("DS: Parent = %x\n", (unsigned) parent);

                        if (parent) {
                            ppkParent = (PP_T *) parent + lookupIndex;
                            debug("DS: We have a parent\n");

                            if (side) ppkParent->right = (PP_T *) ppkRotate - lookupIndex;
                            else ppkParent->left = (PP_T *) ppkRotate - lookupIndex;
                        }
                        else pool->root[lookupIndex] = ppkRotate - lookupIndex;       //ppkRotate;

                        ppkDead->right = ppkRotate->right;
                        ppkRotate->right = ppkDead - lookupIndex;
                        ppkRotate->left = ppkDead->left;
                        ppkDead->left = NULL;

                        rc = ppkDead->balance;
                        ppkDead->balance = ppkRotate->balance;
                        ppkRotate->balance = rc;


                    }

                    start = ppkRotate - lookupIndex;
                    set_pointers (pool, lookupIndex, start, &ppkDead, &dataHead);

                    rc = 1;

                    goto retest_delete_cond;

                }

                debug("My Bal Before ... and now I'm dead %d\n", ppkDead->balance);
                rc = _rdb_delete_by_pointer (pool, (void *) parent, lookupIndex, ppkDead, side);
                debug("Delete by Ptr returned %d\n", rc);
                return (rc);
            }
        }
    }

    return 0;                                //should never get here
}

void   *
rdb_delete (rdb_pool_t *pool, int lookupIndex, void *data)
{

    int     indexCount;
    void   *ptr = NULL;                         // null to sashhh the compiler

    if (pool->FLAGS[lookupIndex] & (RDB_NOKEYS)) {
        PP_T   *ppk = NULL,
               *ppkRight = NULL,
               *ppkLeft = NULL;
        ptr = pool->root[lookupIndex]; // + (sizeof (PP_T) * lookupIndex);
        if (ptr == NULL)
            return NULL;

        for (indexCount = 0; indexCount < pool->indexCount; indexCount++) {
            if (pool->FLAGS[indexCount] & (RDB_NOKEYS)) {
                ppk = ptr + (sizeof (PP_T) * indexCount);

                if (ppk->left) 
                    ppkLeft = ppk->left + indexCount;
                else ppkLeft = NULL;

                if (ppk->right) 
                    ppkRight = ppk->right + indexCount;
                else ppkRight = NULL;

                if (pool->root[indexCount] == pool->tail[indexCount]) pool->root[indexCount] =
                        pool->tail[indexCount] = NULL; //we were the last item
                else { //if (pool->root[indexCount] == ptr) { // not only one - first
                    if (ppkLeft) ppkLeft->right = ppk->right;

                    if (ppkRight) ppkRight->left = ppk->left;

                    if (pool->root[indexCount] == ptr) pool->root[indexCount] = ppkRight - indexCount;

                    if (pool->tail[indexCount] == ptr) pool->tail[indexCount] = ppkLeft - indexCount;
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

    return ptr;
}
int rdb_delete_one (rdb_pool_t *pool, int index, void *data)
{
    return _rdb_delete (pool, index, data, NULL, NULL, 0);
}

#ifdef KM
static int __init init (void)
{
    rdbInit ();
    return 0;
}

static void __exit fini (void)
{
    return;
}

EXPORT_SYMBOL (rdb_register_pool);
EXPORT_SYMBOL (rdb_register_idx);
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
MODULE_LICENSE ("GPL");
MODULE_DESCRIPTION ("RDB Ram-DB module");
MODULE_VERSION("1.0-rc1");


/*
 * This handles module initialization
 */

module_init (init);
module_exit (fini);
#endif
