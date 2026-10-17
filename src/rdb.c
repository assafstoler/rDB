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

typedef struct {
    rdb_kcf_key_part_t *parts;
    size_t part_count;
    size_t key_size;
} rdb_kcf_index_t;

typedef struct {
    rdb_kcf_index_t index[RDB_POOL_MAX_IDX];
} rdb_pool_private_t;

typedef struct rdb_reindex_key {
    unsigned char *bytes;
    size_t size;
} rdb_reindex_key_t;

typedef struct rdb_reindex_entry {
    void *record;
    rdb_index_mask_t indexes;
    rdb_reindex_key_t keys[RDB_POOL_MAX_IDX];
    struct rdb_reindex_entry *next;
} rdb_reindex_entry_t;

typedef struct rdb_reindex_temp {
    char *buffer;
    size_t capacity;
    struct rdb_reindex_temp *next;
} rdb_reindex_temp_t;

struct rdb_reindex_ctx {
    rdb_pool_t *pool;
    void *record;
    int error;
    rdb_reindex_entry_t *entries;
    rdb_reindex_entry_t **entries_tail;
    rdb_reindex_temp_t *temps;
};

static uint32_t rdb_init_flags;


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
    (void)rdb_init_ex(0);
}

int rdb_init_ex(uint32_t flags)
{
    if (flags & ~RDB_INIT_ALLOW_KCFUT)
        return rdb_error_value(-1, "rdb_init_ex called with unsupported flags");
#ifdef KM
    sema_init(&reg_mutex, 1);
    sema_init(&rdb_error_mutex, 1);
#endif
    pool_root = NULL;
    rdb_init_flags = flags;
    return 0;
}

int rdb_pool_get_fault(const rdb_pool_t *pool, rdb_fault_info_t *out)
{
    if (!pool)
        return -1;
    if (out)
        *out = pool->fault;
    return pool->fault.code ? 1 : 0;
}

// Keep the first consistency failure in the pool even if saving the message
// fails. A fault is sticky until the pool is dropped.
static int fault_pool(rdb_pool_t *pool, int index, void *record,
        const char *operation, const char *reason)
{
    if (!pool->fault.code) {
        char message[320];
        pool->fault.code = RDB_ERR_FAULTED;
        pool->fault.index = index;
        pool->fault.record = record;
        pool->fault.operation = operation;
        pool->fault.reason = reason;
        snprintf(message, sizeof(message), "Pool '%s' index %d: %s failed: %s; pool faulted",
                pool->name, index, operation, reason);
        rdb_error(message);
    }
    return RDB_ERR_FAULTED;
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

// Validate public index arguments before indexing pool metadata. Recursive
// tree/list helpers only receive indexes that have already passed this check.
static int valid_pool_index(const rdb_pool_t *pool, int index)
{
    return pool && index >= 0 && index < RDB_POOL_MAX_IDX &&
            index < pool->indexCount;
}

static size_t reindex_type_size(uint32_t type, size_t declared_size)
{
    switch (type) {
        case RDB_KSTR: return declared_size;
        case RDB_KPSTR: return sizeof(char *);
        case RDB_KPTR: return sizeof(void *);
        case RDB_KINT8: case RDB_KUINT8: return sizeof(uint8_t);
        case RDB_KINT16: case RDB_KUINT16: return sizeof(uint16_t);
        case RDB_KINT32: case RDB_KUINT32: return sizeof(uint32_t);
        case RDB_KINT64: case RDB_KUINT64: return sizeof(uint64_t);
        case RDB_KSIZE_t: return sizeof(size_t);
        case RDB_KSSIZE_t: return sizeof(ssize_t);
#ifdef USE_128_BIT_TYPES
        case RDB_KINT128: case RDB_KUINT128: return sizeof(__int128_t);
#endif
        default: return 0;
    }
}

static rdb_kcf_index_t *kcf_index_meta(rdb_pool_t *pool, int index)
{
    rdb_pool_private_t *private_data = pool ? pool->private_data : NULL;
    if (!private_data || !valid_pool_index(pool, index))
        return NULL;
    return &private_data->index[index];
}

static int valid_key_size(uint32_t flags, size_t key_size)
{
    uint32_t kind = flags & RDB_KEYS;
    return kind == RDB_KSTR ? key_size > 0 : key_size == 0;
}

static int set_key_size(rdb_pool_t *pool, int index, size_t key_size)
{
    rdb_pool_private_t *private_data;
    if (!key_size) return 0;
    private_data = pool->private_data;
    if (!private_data) {
        private_data = rdb_alloc(sizeof(*private_data));
        if (!private_data) return RDB_ERR_REINDEX_NOMEM;
        memset(private_data, 0, sizeof(*private_data));
        pool->private_data = private_data;
    }
    private_data->index[index].key_size = key_size;
    return 0;
}

// Caller holds reg_mutex; registration uses this helper without relocking.
static rdb_pool_t *find_pool_by_name_unlocked(char *poolName)
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

// Protect traversal from concurrent registration/drop of unrelated pools.
// The returned handle is borrowed; the caller must keep it alive while in use.
rdb_pool_t *rdb_find_pool_by_name (char *poolName)
{
    rdb_pool_t *pool;
    rdb_sem_lock(&reg_mutex);
    pool = find_pool_by_name_unlocked(poolName);
    rdb_sem_unlock(&reg_mutex);
    return pool;
}

// Print pool usage report
// Called only with a valid buffer and positive size. Preserve the prefix and
// replace the last three characters with dots, or fewer if the buffer is tiny.
static char *pool_stats_truncated(char *buf, int max_len)
{
    int end = max_len - 1;
    int dots = end < 3 ? end : 3;
    memset(buf + end - dots, '.', dots);
    buf[end] = '\0';
    return buf;
}

#ifdef RDB_POOL_COUNTERS
// Record writers remain serialized by the caller's pool lock. Relaxed loads
// and stores let reporting sample counters without taking that lock.
static inline uint32_t pool_count(const rdb_pool_t *pool)
{
    return __atomic_load_n(&pool->record_count, __ATOMIC_RELAXED);
}

static inline void pool_count_set(rdb_pool_t *pool, uint32_t count)
{
    __atomic_store_n(&pool->record_count, count, __ATOMIC_RELAXED);
}
#endif

char * rdb_print_pool_stats (char *buf, int max_len)
{
    if (!buf || max_len <= 0) {
        return buf;
    }

    int used=0;
    int rc;
    
#ifdef RDB_POOL_COUNTERS
    int truncated = 0, format_failed = 0;
    rdb_pool_t *pool;

    // Registry locking keeps pool names and links alive. Do not take pool
    // locks here: callers may already hold one while requesting statistics.
    rdb_sem_lock(&reg_mutex);
    for (pool = pool_root; pool; pool = pool->next) {
        rc = snprintf(buf + used, max_len - used, "Pool: %s : %u\n",
                pool->name, (unsigned)pool_count(pool));
        if (rc <= 0) {
            format_failed = 1;
            break;
        }
        // snprintf returns the required length, not the bytes that fit.
        if (rc >= max_len - used) {
            truncated = 1;
            break;
        }
        used += rc;
    }
    rdb_sem_unlock(&reg_mutex);
    if (truncated)
        return pool_stats_truncated(buf, max_len);
    // Error reporting may allocate; do it after releasing the registry lock.
    if (format_failed)
        rdb_error("Failure during error print\n");
    rc = snprintf(buf + used, max_len - used, ":END:");
#else
    rc = snprintf(buf + used, max_len - used,
            "rDB pool record counting disabled");
#endif
    if (rc >= max_len - used) {
        return pool_stats_truncated(buf, max_len);
    }
    return (buf);
}

static int valid_index_flags(const char *name, int index, uint32_t flags,
        void *compare_fn)
{
    const uint32_t builtins = RDB_KSTR | RDB_KPSTR | RDB_KINT8 | RDB_KUINT8 |
        RDB_KINT16 | RDB_KUINT16 | RDB_KINT32 | RDB_KUINT32 |
        RDB_KINT64 | RDB_KUINT64 | RDB_KPTR | RDB_KSIZE_t | RDB_KSSIZE_t
#ifdef USE_128_BIT_TYPES
        | RDB_KINT128 | RDB_KUINT128
#endif
        ;
    const uint32_t kinds = builtins | RDB_KCF | RDB_KCFUT | RDB_NOKEYS;
    const uint32_t allowed = kinds | RDB_BTREE | RDB_KASC | (uint32_t)RDB_NO_IDX;
    const uint32_t kind = flags & kinds;
    const char *reason = NULL;

    if (flags & RDB_LIST)
        reason = "RDB_LIST is unsupported; use RDB_BTREE";
    else if (flags & RDB_KDEC)
        reason = "RDB_KDEC (descending order) is unsupported";
    else if (flags & ~allowed)
        reason = "unknown or unsupported key/option bits";
    else if (!(flags & RDB_BTREE))
        reason = "RDB_BTREE is required, including for FIFO/LIFO";
    else if (!kind || (kind & (kind - 1)))
        reason = "select exactly one key type, FIFO or LIFO";
    else if ((flags & RDB_NO_IDX) && !(kind & RDB_NOKEYS))
        reason = "RDB_NO_IDX is only valid with FIFO/LIFO";
    else if ((kind & RDB_NOKEYS) && compare_fn)
        reason = "FIFO/LIFO cannot use a comparator";
    else if (kind == RDB_KCF && !compare_fn)
        reason = "RDB_KCF requires a comparator";
    else if (kind == RDB_KCFUT && !compare_fn)
        reason = "RDB_KCFUT requires a comparator";

    if (reason) {
        char message[256];
        snprintf(message, sizeof(message), "Pool '%s' index %d flags 0x%08x: %s",
                name, index, (unsigned)flags, reason);
        rdb_error(message);
        return 0;
    }

    return 1;
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
    rdb_pool_private_t *private_data;
    int index;

    rdb_sem_lock(&reg_mutex);
    if (!(pool && pool->name)) {
        rdb_sem_unlock(&reg_mutex);
        return;
    }

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

#ifndef KM
    // Pool users must be quiescent and both locks released before dropping it.
    pthread_mutex_destroy(&pool->read_mutex);
    pthread_mutex_destroy(&pool->write_mutex);
#endif

    if (pool->name) {
        //info ("freeing %s\n", pool->name);
        rdb_free (pool->name);
    }

    private_data = pool->private_data;
    if (private_data) {
        for (index = 0; index < RDB_POOL_MAX_IDX; ++index)
            if (private_data->index[index].parts)
                rdb_free(private_data->index[index].parts);
        rdb_free(private_data);
    }

    if (pool) {
        rdb_free(pool) ;
        pool = NULL;
    }

    rdb_sem_unlock(&reg_mutex);
    return;
}

// rDB Iternal: Add a new pool to our pool chain
rdb_pool_t *rdb_add_pool (
        char *poolName, 
        int indexCount, 
        int key_offset, 
        int FLAGS, 
        void *compare_fn,
        size_t key_size) {

    rdb_pool_t *pool;
    int name_length;

    if (indexCount < 1 || indexCount > RDB_POOL_MAX_IDX) {
        char message[96];
        snprintf(message, sizeof(message),
                "Invalid pool index count %d (expected 1..%d)",
                indexCount, RDB_POOL_MAX_IDX);
        rdb_error(message);
        return NULL;
    }

    if (!valid_index_flags(poolName, 0, (uint32_t)FLAGS, compare_fn))
        return NULL;
    if (!valid_key_size((uint32_t)FLAGS, key_size)) {
        rdb_error("RDB_KSTR indexes require a nonzero key size; other indexes require zero");
        return NULL;
    }

    pool = rdb_alloc(sizeof (rdb_pool_t));

    if (pool == NULL) {
        rdb_error ("Fatal: Pool allocation error, out of memory");
        return NULL;
    }

    memset (pool, 0, sizeof (rdb_pool_t));

    if (set_key_size(pool, 0, key_size) < 0) {
        rdb_free(pool);
        return NULL;
    }

    name_length = strlen (poolName);
    pool->name = rdb_alloc (name_length + 1);

    if (pool->name == NULL) {
        rdb_error ("rDB: Fatal: pool allocation error, out of memor for pool"
                " name");
        if (pool->private_data) rdb_free(pool->private_data);
        rdb_free (pool);
        return NULL;
    }

    strncpy (pool->name, poolName, name_length+1); //+1 to shut compiler warning on some systems, even though we overwrite it to ensure terminating null.
    pool->name[name_length] = 0;
    
    if (-1 == set_pool_fn_pointers(pool, 0, FLAGS, compare_fn)){
        rdb_error ("rDB: Fatal: pool registration without type or matching"
                " compare fn");
        rdb_free(pool->name);
        if (pool->private_data) rdb_free(pool->private_data);
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
    const char *failed_mutex = "read";
    int mutex_error = pthread_mutex_init(&pool->read_mutex, NULL);
    if (mutex_error == 0) {
        failed_mutex = "write";
        mutex_error = pthread_mutex_init(&pool->write_mutex, NULL);
        if (mutex_error != 0)
            pthread_mutex_destroy(&pool->read_mutex);
    }
    if (mutex_error != 0) {
        char message[128];
        snprintf(message, sizeof(message),
                "Pool %s mutex initialization failed: %s (%d)",
                failed_mutex, strerror(mutex_error), mutex_error);
        rdb_error(message);
        rdb_free(pool->name);
        if (pool->private_data) rdb_free(pool->private_data);
        rdb_free(pool);
        return NULL;
    }
#endif

    // Publish only after all pool resources have initialized successfully.
    pool->next = pool_root;
    if (pool_root)
        pool_root->prev = pool;
    pool_root = pool;

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
        void *fn,
        size_t key_size) {

    rdb_pool_t *pool;

    //TODO:kernel frindly locks
    rdb_sem_lock(&reg_mutex);

    if (find_pool_by_name_unlocked(poolName) != NULL) {
        rdb_error ("rDB: Fatal: Duplicte pool name in rdb_register_pool");
        pool = NULL;
    } else {
        pool = rdb_add_pool (poolName, idxCount, key_offset, FLAGS, fn,
                key_size);
    }
    rdb_sem_unlock(&reg_mutex);

    return pool;
}

// Remove rDB traces. use before exit() or when rDB no longer needed.
// Use rdb_init after, to re_start rDB

// rdb_gc: drop pools that are marked for dropping. pools must be empty first.
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

    if (!gc)
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
        int FLAGS, void *compare_fn, size_t key_size) {
    if (pool && pool->fault.code) return RDB_ERR_FAULTED;
    rdb_sem_lock(&reg_mutex);
    if (idx == 0) {
        rdb_sem_unlock(&reg_mutex);
        return (rdb_error_value (-1, 
            "Index 0 (zero) can only be set via rdb_register_pool"));
    }

    if (!pool || idx < 0 || idx >= RDB_POOL_MAX_IDX || idx >= pool->indexCount) {
        rdb_sem_unlock(&reg_mutex);
        return (rdb_error_value (-2, "Index outside pool's declared index range"));
    }

    if (pool->FLAGS[idx] != 0) {
        rdb_sem_unlock(&reg_mutex);
        return (rdb_error_value (-3, "Redefinition of used index not allowed"));
    }

    if (!valid_index_flags(pool->name, idx, (uint32_t)FLAGS, compare_fn)) {
        rdb_sem_unlock(&reg_mutex);
        return -4;
    }

    if (!valid_key_size((uint32_t)FLAGS, key_size)) {
        rdb_sem_unlock(&reg_mutex);
        return rdb_error_value(-4,
                "RDB_KSTR indexes require a nonzero key size; other indexes require zero");
    }

    if (-1 == set_pool_fn_pointers(pool, idx, FLAGS, compare_fn)){
        rdb_sem_unlock(&reg_mutex);
        return (rdb_error_value (-4,
            "Index Registration without valid type or compare fn. Ignored"));
    }

    if (set_key_size(pool, idx, key_size) < 0) {
        rdb_sem_unlock(&reg_mutex);
        return RDB_ERR_REINDEX_NOMEM;
    }

    pool->root[idx] = NULL;
    pool->key_offset[idx] = sizeof (PP_T) * pool->indexCount + key_offset;
    pool->FLAGS[idx] = FLAGS;
    debug ("registered index %d for pool %s, Keyoffset is %d\n", idx, pool->name, key_offset);
    rdb_sem_unlock(&reg_mutex);
    return (idx);
}

int rdb_register_kcf_key_parts(rdb_pool_t *pool, int index,
        const rdb_kcf_key_part_t *parts, size_t part_count)
{
    rdb_pool_private_t *private_data;
    rdb_kcf_index_t *metadata;
    rdb_kcf_key_part_t *copy;
    size_t i;

    if (!valid_pool_index(pool, index) || !parts || !part_count ||
            !(pool->FLAGS[index] & RDB_KCF))
        return rdb_error_value(RDB_ERR_REINDEX_INVALID,
                "KCF key parts require a valid RDB_KCF index and nonempty parts");
    if (pool->fault.code)
        return RDB_ERR_FAULTED;
    if (pool->root[index])
        return rdb_error_value(RDB_ERR_REINDEX_INVALID,
                "KCF key parts must be registered before inserting records");
    if (part_count > (size_t)-1 / sizeof(*copy))
        return rdb_error_value(RDB_ERR_REINDEX_INVALID,
                "Too many KCF key components");

    for (i = 0; i < part_count; ++i) {
        size_t size = reindex_type_size(parts[i].type, parts[i].size);
        if (!size || (parts[i].type == RDB_KSTR && !parts[i].size) ||
                (parts[i].type != RDB_KSTR && parts[i].size) ||
                parts[i].offset > (size_t)-1 - size)
            return rdb_error_value(RDB_ERR_REINDEX_INVALID,
                    "Invalid KCF key component description");
        for (size_t prior = 0; prior < i; ++prior) {
            size_t prior_size = reindex_type_size(parts[prior].type,
                    parts[prior].size);
            size_t part_end = parts[i].offset + size;
            size_t prior_end = parts[prior].offset + prior_size;
            if (parts[i].offset < prior_end && parts[prior].offset < part_end)
                return rdb_error_value(RDB_ERR_REINDEX_INVALID,
                        "KCF key components may not overlap");
        }
    }

    private_data = pool->private_data;
    if (!private_data) {
        private_data = rdb_alloc(sizeof(*private_data));
        if (!private_data)
            return RDB_ERR_REINDEX_NOMEM;
        memset(private_data, 0, sizeof(*private_data));
        pool->private_data = private_data;
    }
    metadata = &private_data->index[index];
    if (metadata->parts)
        return rdb_error_value(RDB_ERR_REINDEX_INVALID,
                "KCF key parts are already registered");

    copy = rdb_alloc(sizeof(*copy) * part_count);
    if (!copy)
        return RDB_ERR_REINDEX_NOMEM;
    memcpy(copy, parts, sizeof(*copy) * part_count);
    metadata->parts = copy;
    metadata->part_count = part_count;
    return 0;
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
    uintptr_t old_address = (uintptr_t)*old;
    uintptr_t new_address = (uintptr_t)*new;
    return (new_address < old_address) ? -1 :
            ((new_address > old_address) ? 1 : 0);
}
int key_cmp_const_ptr (void *old, size_t new)
{
    uintptr_t old_address = (uintptr_t)old;
    uintptr_t new_address = (uintptr_t)new;
    return (new_address < old_address) ? -1 :
            ((new_address > old_address) ? 1 : 0);
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
            case RDB_KCFUT:
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
    rdb_info("RDBL    %s by %s\n", pool->name, parent);
#endif
    return rdb_sem_lock(&pool->write_mutex);
}

void rdb_unlock(rdb_pool_t *pool, const char *parent) 
{
#ifdef RDB_LOCK_DEBUG
    rdb_info("RDb-UL %s - %s\n", pool->name, parent);
#endif
    rdb_sem_unlock(&pool->write_mutex);
    return ;
}

// Dump an entire pool to stdout. only the selected index field will be
// printed out. Also calculates tree depth...
void rdb_dump (rdb_pool_t *pool, int index, char *separator) {

    if (!valid_pool_index(pool, index)) {
        rdb_error("rdb_dump called with NULL pool or index outside pool's declared range");
        return;
    }

    if (pool->fault.code || pool->root[index] == NULL) return;

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

// Internal deletion distinguishes a miss from successful unlinking and AVL
// height changes. Public delete_one keeps its historical 0/1 result.
enum { DELETE_MISSING = 0, DELETE_REMOVED = 1, DELETE_SHRANK = 2 };
static int unlink_record(rdb_pool_t *pool, void *record, PP_T *queue_links,
        const char *operation);
static void reindex_free_record_strings(rdb_pool_t *pool, void *record);

// Return shoud be the # of updated indexes, which must match the number of 
// defined indexes, anything less shows an error and should be treated as 
// such by user.
// TODO? Should we make this atomic - meaning if one index failes fo insert, 
// delete the previously inserted ones?

int rdb_insert (rdb_pool_t *pool, void *data)
{
    int indexCount;

    if (!pool || !data) return -1;
    if (pool->fault.code) return RDB_ERR_FAULTED;
    if (!pool->indexCount || pool->indexCount > RDB_POOL_MAX_IDX) {
        char message[160];
        snprintf(message, sizeof(message),
                "Pool '%s': insert rejected: invalid index count %u (expected 1..%d)",
                pool->name, (unsigned)pool->indexCount, RDB_POOL_MAX_IDX);
        return rdb_error_value(-1, message);
    }

    for (indexCount = 0; indexCount < pool->indexCount; ++indexCount) {
        int undo;
        if (_rdb_insert(pool, data, pool->root[indexCount], indexCount,
                    NULL, 0) >= 0)
            continue;

        for (undo = 0; undo < indexCount; ++undo) {
            if (_rdb_delete(pool, undo, data, NULL, NULL, 0) <= DELETE_MISSING)
                return fault_pool(pool, undo, data, "insert rollback",
                        "could not unlink a partially inserted record");
        }
        return 0;
    }
#ifdef RDB_POOL_COUNTERS
    pool_count_set(pool, pool_count(pool) + 1);
#endif
    return pool->indexCount;
}

// Only insert one index (asuming this index was removed and updated prior).
int rdb_insert_one (rdb_pool_t *pool, int index, void *data)
{
    if (!valid_pool_index(pool, index)) return -1;
    if (pool->fault.code) return RDB_ERR_FAULTED;
    return _rdb_insert (pool, data, pool->root[index], index, NULL, 0) ;
}

void   *_rdb_get (
        rdb_pool_t  *pool, 
        int         index, 
        const void  *data, 
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
void   *rdb_get (rdb_pool_t *pool, int idx, const void *data)
{
    if (!valid_pool_index(pool, idx) || pool->fault.code) return NULL;
    debug("Get:pool=%s,idx=%d", pool->name, idx);
    return _rdb_get (pool, idx, data, NULL, 0);
}

void   *rdb_get_const (rdb_pool_t *pool, int idx, __intmax_t value)
{
    if (!valid_pool_index(pool, idx) || pool->fault.code) return NULL;
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
    *before = *after = NULL;
    if (!valid_pool_index(pool, idx) || pool->fault.code) return NULL;
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
    PP_T   *pp, *pr;
    int     rc, rc2 = 0;
    int     rc3 = 0;

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

            if (fn) rc2 = fn(dataHead, data);
            if (pool->fault.code) return RDBFE_ABORT;
            if (fn == NULL || RDB_CB_DELETE_NODE == rc2
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

                if (unlink_record(pool, dataHead, NULL, "iterate") < 0)
                    return RDBFE_ABORT;

                reindex_free_record_strings(pool, dataHead);
                if (del_fn) del_fn(dataHead, delfn_data);
                else if (dataHead) rdb_free(dataHead);

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
    PP_T   *pp;
    int     rc, rc2 = 0;

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

        if (fn) rc2 = fn(dataHead, data);
        if (pool->fault.code) return RDBFE_ABORT;
        if (fn == NULL || RDB_CB_DELETE_NODE == rc2
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

            if (unlink_record(pool, dataHead, NULL, "iterate") < 0)
                return RDBFE_ABORT;

            reindex_free_record_strings(pool, dataHead);
            if (del_fn) del_fn(dataHead, delfn_data);
            else if (dataHead) rdb_free(dataHead);

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
 * del_fn is called after node is unlinked from all indexes. rDB first frees
 * every registered KPSTR component and sets its pointer to NULL, then calls
 * del_fn. The callback should free the record and any other user allocations,
 * but must not free indexed KPSTR strings.
 *
 * del_fn receive a pointer to the node head, and a potential user  argument
 * del_fn is optional. If it is NULL, rDB frees the record after freeing its
 * registered KPSTR index strings. Any other user-owned allocations require a
 * del_fn.
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

    if (!valid_pool_index(pool, index)) {
        return rdb_error("rdb_iterate index outside pool's declared range");
    }

    if (pool->fault.code || pool->root[index] == NULL) {
        return;     // no data is not an error; a fault forbids traversal
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
    } while (!pool->fault.code && rc != 0 && ( rc & RDBFE_ABORT ) != RDBFE_ABORT &&
                                                    resumePtr != NULL);
}

static int reindex_collect_count(rdb_pool_t *pool, int index, void *record,
        size_t *count)
{
    PP_T *links;
    (void)pool;
    if (!record) return 0;
    links = (PP_T *)record + index;
    if (reindex_collect_count(pool, index, links->left, count) < 0)
        return -1;
    if (*count == (size_t)-1) return -1;
    ++*count;
    return reindex_collect_count(pool, index, links->right, count);
}

static void reindex_collect_records(rdb_pool_t *pool, int index, void *record,
        void **records, size_t *position)
{
    PP_T *links;
    if (!record) return;
    links = (PP_T *)record + index;
    reindex_collect_records(pool, index, links->left, records, position);
    records[(*position)++] = record;
    reindex_collect_records(pool, index, links->right, records, position);
}

static rdb_reindex_entry_t *reindex_find_entry(rdb_reindex_ctx_t *ctx,
        const void *record)
{
    rdb_reindex_entry_t *entry;
    for (entry = ctx->entries; entry; entry = entry->next)
        if (entry->record == record) return entry;
    return NULL;
}

static void reindex_key_clear(rdb_pool_t *pool, int index,
        rdb_reindex_key_t *key)
{
    uint32_t kind = pool->FLAGS[index] & RDB_KEYS;
    size_t part;
    rdb_kcf_index_t *metadata;
    if (!key->bytes) return;
    if (kind == RDB_KPSTR) {
        char **value = (char **)key->bytes;
        if (*value) rdb_free(*value);
    } else if (kind == RDB_KCF) {
        metadata = kcf_index_meta(pool, index);
        if (metadata) for (part = 0; part < metadata->part_count; ++part) {
            if (metadata->parts[part].type == RDB_KPSTR) {
                char **value = (char **)(key->bytes + metadata->parts[part].offset);
                if (*value) rdb_free(*value);
            }
        }
    }
    rdb_free(key->bytes);
    key->bytes = NULL;
    key->size = 0;
}

static void reindex_entry_free(rdb_pool_t *pool, rdb_reindex_entry_t *entry)
{
    int index;
    for (index = 0; index < pool->indexCount; ++index)
        reindex_key_clear(pool, index, &entry->keys[index]);
    rdb_free(entry);
}

static void reindex_entries_free(rdb_reindex_ctx_t *ctx)
{
    rdb_reindex_entry_t *entry = ctx->entries;
    while (entry) {
        rdb_reindex_entry_t *next = entry->next;
        reindex_entry_free(ctx->pool, entry);
        entry = next;
    }
    ctx->entries = NULL;
    ctx->entries_tail = &ctx->entries;
}

static rdb_reindex_temp_t *reindex_find_temp(rdb_reindex_ctx_t *ctx,
        const char *buffer)
{
    rdb_reindex_temp_t *temp;
    for (temp = ctx->temps; temp; temp = temp->next)
        if (temp->buffer == buffer) return temp;
    return NULL;
}

static int reindex_copy_string(rdb_reindex_ctx_t *ctx, const char *source,
        char **copy)
{
    rdb_reindex_temp_t *temp;
    const char *end;
    size_t length;
    if (!source) return RDB_ERR_REINDEX_INVALID;
    temp = reindex_find_temp(ctx, source);
    if (temp) {
        end = memchr(source, 0, temp->capacity);
        if (!end) return RDB_ERR_REINDEX_INVALID;
        length = (size_t)(end - source);
    } else {
        length = strlen(source);
    }
    *copy = rdb_alloc(length + 1);
    if (!*copy) return RDB_ERR_REINDEX_NOMEM;
    memcpy(*copy, source, length + 1);
    return 0;
}

static int reindex_key_size(rdb_pool_t *pool, int index, size_t *size)
{
    uint32_t kind = pool->FLAGS[index] & RDB_KEYS;
    rdb_kcf_index_t *metadata;
    size_t part, end, max_end = 0;
    if (kind == RDB_KCF) {
        metadata = kcf_index_meta(pool, index);
        if (!metadata || !metadata->part_count)
            return RDB_ERR_REINDEX_UNSUPPORTED;
        for (part = 0; part < metadata->part_count; ++part) {
            end = metadata->parts[part].offset + reindex_type_size(
                    metadata->parts[part].type, metadata->parts[part].size);
            if (end > max_end) max_end = end;
        }
        *size = max_end;
        return 0;
    }
    if (kind == RDB_KSTR) {
        metadata = kcf_index_meta(pool, index);
        if (!metadata || !metadata->key_size)
            return RDB_ERR_REINDEX_UNSUPPORTED;
        *size = metadata->key_size;
        return 0;
    }
    *size = reindex_type_size(kind, 0);
    return *size ? 0 : RDB_ERR_REINDEX_UNSUPPORTED;
}

static int reindex_key_copy(rdb_reindex_ctx_t *ctx, int index,
        const void *record, rdb_reindex_key_t *key)
{
    rdb_pool_t *pool = ctx->pool;
    uint32_t kind = pool->FLAGS[index] & RDB_KEYS;
    rdb_kcf_index_t *metadata;
    size_t size, part;
    int rc;
    const unsigned char *source = (const unsigned char *)record +
            pool->key_offset[index];

    rc = reindex_key_size(pool, index, &size);
    if (rc < 0) return rc;
    key->bytes = rdb_alloc(size);
    if (!key->bytes) return RDB_ERR_REINDEX_NOMEM;
    key->size = size;
    memcpy(key->bytes, source, size);

    if (kind == RDB_KSTR) {
        if (!memchr(source, 0, size)) {
            reindex_key_clear(pool, index, key);
            return RDB_ERR_REINDEX_INVALID;
        }
    } else if (kind == RDB_KPSTR) {
        char **value = (char **)source;
        char **copy = (char **)key->bytes;
        char *source_string = *value;
        *copy = NULL;
        rc = reindex_copy_string(ctx, source_string, copy);
        if (rc < 0) {
            reindex_key_clear(pool, index, key);
            return rc;
        }
    } else if (kind == RDB_KCF) {
        metadata = kcf_index_meta(pool, index);
        for (part = 0; part < metadata->part_count; ++part)
            if (metadata->parts[part].type == RDB_KPSTR)
                *(char **)(key->bytes + metadata->parts[part].offset) = NULL;
        for (part = 0; part < metadata->part_count; ++part) {
            const rdb_kcf_key_part_t *component = &metadata->parts[part];
            if (component->type == RDB_KPSTR) {
                char **copy = (char **)(key->bytes + component->offset);
                char *source_string = *(char **)(source + component->offset);
                rc = reindex_copy_string(ctx, source_string, copy);
                if (rc < 0) {
                    reindex_key_clear(pool, index, key);
                    return rc;
                }
            } else if (component->type == RDB_KSTR &&
                    !memchr(source + component->offset, 0, component->size)) {
                reindex_key_clear(pool, index, key);
                return RDB_ERR_REINDEX_INVALID;
            }
        }
    }
    return 0;
}

static int reindex_compare_key(rdb_pool_t *pool, int index,
        const void *left, const void *right)
{
    return pool->fn[index]((void *)left, (void *)right);
}

static int reindex_check_conflict(rdb_reindex_ctx_t *ctx, int index,
        void *record, const rdb_reindex_key_t *key)
{
    rdb_pool_t *pool = ctx->pool;
    void *node = pool->root[index];
    rdb_reindex_entry_t *owner, *pending;
    while (node) {
        int compare = reindex_compare_key(pool, index,
                (unsigned char *)node + pool->key_offset[index], key->bytes);
        if (!compare) break;
        if (compare < 0)
            node = ((PP_T *)node + index)->left;
        else
            node = ((PP_T *)node + index)->right;
    }
    if (node && node != record) {
        owner = reindex_find_entry(ctx, node);
        if (!owner || !(owner->indexes & RDB_INDEX_MASK(index)) ||
                reindex_compare_key(pool, index,
                    owner->keys[index].bytes, key->bytes) == 0)
            return RDB_ERR_REINDEX_CONFLICT;
    }
    for (pending = ctx->entries; pending; pending = pending->next) {
        if (pending->record != record &&
                (pending->indexes & RDB_INDEX_MASK(index)) &&
                reindex_compare_key(pool, index,
                    pending->keys[index].bytes, key->bytes) == 0)
            return RDB_ERR_REINDEX_CONFLICT;
    }
    return 0;
}

static int reindex_set_error(rdb_reindex_ctx_t *ctx, int error,
        const char *message)
{
    if (ctx && !ctx->error) {
        ctx->error = error;
        rdb_error_value(error, (char *)message);
    }
    return error;
}

int rdb_reindex_check_apply(rdb_reindex_ctx_t *ctx, void *record,
        const void *candidate_record, rdb_index_mask_t changed_indexes)
{
    rdb_reindex_key_t keys[RDB_POOL_MAX_IDX] = {{0}};
    rdb_reindex_entry_t *entry;
    rdb_index_mask_t valid_mask;
    int index, rc;

    if (!ctx || !record || !candidate_record || record != ctx->record)
        return reindex_set_error(ctx, RDB_ERR_REINDEX_INVALID,
                "Reindex validation requires the active record and candidate");
    if (ctx->error) return ctx->error;
    valid_mask = (1U << ctx->pool->indexCount) - 1U;
    if (changed_indexes & ~valid_mask)
        return reindex_set_error(ctx, RDB_ERR_REINDEX_INVALID,
                "Reindex mask contains an index outside the pool");

    for (index = 0; index < ctx->pool->indexCount; ++index) {
        uint32_t kind = ctx->pool->FLAGS[index] & RDB_KEYS;
        if (kind == RDB_KCFUT) {
            if (changed_indexes & RDB_INDEX_MASK(index)) {
                for (int cleanup = 0; cleanup < ctx->pool->indexCount; ++cleanup)
                    reindex_key_clear(ctx->pool, cleanup, &keys[cleanup]);
                return reindex_set_error(ctx, RDB_ERR_REINDEX_UNSUPPORTED,
                        "RDB_KCFUT indexes cannot be reindexed");
            }
            continue;
        }
        if (ctx->pool->FLAGS[index] & RDB_NOKEYS) {
            if (changed_indexes & RDB_INDEX_MASK(index)) {
                for (int cleanup = 0; cleanup < ctx->pool->indexCount; ++cleanup)
                    reindex_key_clear(ctx->pool, cleanup, &keys[cleanup]);
                return reindex_set_error(ctx, RDB_ERR_REINDEX_UNSUPPORTED,
                        "FIFO/LIFO indexes cannot be reindexed");
            }
            continue;
        }
        rc = reindex_key_copy(ctx, index, candidate_record, &keys[index]);
        if (rc < 0) {
            for (int cleanup = 0; cleanup < ctx->pool->indexCount; ++cleanup)
                reindex_key_clear(ctx->pool, cleanup, &keys[cleanup]);
            return reindex_set_error(ctx, rc,
                    "Candidate uses an unsupported or invalid index key");
        }
        if (!(changed_indexes & RDB_INDEX_MASK(index))) {
            const void *old_key = (const unsigned char *)record +
                    ctx->pool->key_offset[index];
            int changed = reindex_compare_key(ctx->pool, index, old_key,
                    keys[index].bytes) != 0;
            reindex_key_clear(ctx->pool, index, &keys[index]);
            if (changed) {
                for (int cleanup = 0; cleanup < ctx->pool->indexCount; ++cleanup)
                    reindex_key_clear(ctx->pool, cleanup, &keys[cleanup]);
                return reindex_set_error(ctx, RDB_ERR_REINDEX_UNMARKED,
                        "Candidate changed an index omitted from the reindex mask");
            }
            continue;
        }
    }

    for (index = 0; index < ctx->pool->indexCount; ++index) {
        if (!(changed_indexes & RDB_INDEX_MASK(index))) continue;
        rc = reindex_check_conflict(ctx, index, record, &keys[index]);
        if (rc < 0) {
            for (int cleanup = 0; cleanup < ctx->pool->indexCount; ++cleanup)
                reindex_key_clear(ctx->pool, cleanup, &keys[cleanup]);
            return reindex_set_error(ctx, rc,
                    "Reindex candidate conflicts with an occupied or reserved key");
        }
    }

    entry = reindex_find_entry(ctx, record);
    if (!entry) {
        entry = rdb_alloc(sizeof(*entry));
        if (!entry) {
            for (index = 0; index < ctx->pool->indexCount; ++index)
                reindex_key_clear(ctx->pool, index, &keys[index]);
            return reindex_set_error(ctx, RDB_ERR_REINDEX_NOMEM,
                    "Unable to retain reindex candidate");
        }
        memset(entry, 0, sizeof(*entry));
        entry->record = record;
        *ctx->entries_tail = entry;
        ctx->entries_tail = &entry->next;
    }
    for (index = 0; index < ctx->pool->indexCount; ++index) {
        if (!(changed_indexes & RDB_INDEX_MASK(index))) continue;
        reindex_key_clear(ctx->pool, index, &entry->keys[index]);
        entry->keys[index] = keys[index];
        keys[index].bytes = NULL;
        entry->indexes |= RDB_INDEX_MASK(index);
    }
    return 0;
}

int rdb_reindex_kpstr_edit(rdb_reindex_ctx_t *ctx, void *record,
        void *candidate_record, int index, size_t part_offset,
        size_t capacity, char **edit_buffer)
{
    uint32_t kind;
    char **field;
    char *buffer, *source;
    rdb_reindex_temp_t *temp;
    rdb_kcf_index_t *metadata;
    size_t part;
    int allowed = 0;

    if (!ctx || !record || !candidate_record || !edit_buffer ||
            record != ctx->record || !valid_pool_index(ctx->pool, index) ||
            !capacity)
        return reindex_set_error(ctx, RDB_ERR_REINDEX_INVALID,
                "Invalid KPSTR edit request");
    if (ctx->error) return ctx->error;
    kind = ctx->pool->FLAGS[index] & RDB_KEYS;
    if (kind == RDB_KPSTR && part_offset == 0)
        allowed = 1;
    else if (kind == RDB_KCF) {
        metadata = kcf_index_meta(ctx->pool, index);
        if (metadata) for (part = 0; part < metadata->part_count; ++part)
            if (metadata->parts[part].type == RDB_KPSTR &&
                    metadata->parts[part].offset == part_offset)
                allowed = 1;
    }
    if (!allowed)
        return reindex_set_error(ctx, RDB_ERR_REINDEX_UNSUPPORTED,
                "KPSTR edit must name a registered KPSTR index component");

    field = (char **)((unsigned char *)candidate_record +
            ctx->pool->key_offset[index] + part_offset);
    source = *field;
    if (source) {
        temp = reindex_find_temp(ctx, source);
        if (temp) {
            const char *end = memchr(source, 0, temp->capacity);
            if (!end)
                return reindex_set_error(ctx, RDB_ERR_REINDEX_INVALID,
                        "KPSTR edit buffer is not terminated within its capacity");
            if ((size_t)(end - source) + 1 > capacity)
                return reindex_set_error(ctx, RDB_ERR_REINDEX_INVALID,
                        "KPSTR edit capacity does not fit the existing string");
        } else if (strlen(source) + 1 > capacity) {
            return reindex_set_error(ctx, RDB_ERR_REINDEX_INVALID,
                    "KPSTR edit capacity does not fit the existing string");
        }
    }
    if (!source)
        return reindex_set_error(ctx, RDB_ERR_REINDEX_INVALID,
                "KPSTR edit requires a non-NULL source string");
    buffer = rdb_alloc(capacity);
    temp = rdb_alloc(sizeof(*temp));
    if (!buffer || !temp) {
        if (buffer) rdb_free(buffer);
        if (temp) rdb_free(temp);
        return reindex_set_error(ctx, RDB_ERR_REINDEX_NOMEM,
                "Unable to allocate KPSTR edit buffer");
    }
    memcpy(buffer, source, strlen(source) + 1);
    temp->buffer = buffer;
    temp->capacity = capacity;
    temp->next = ctx->temps;
    ctx->temps = temp;
    *field = buffer;
    *edit_buffer = buffer;
    return 0;
}

static void reindex_temps_free(rdb_reindex_ctx_t *ctx)
{
    rdb_reindex_temp_t *temp = ctx->temps;
    while (temp) {
        rdb_reindex_temp_t *next = temp->next;
        rdb_free(temp->buffer);
        rdb_free(temp);
        temp = next;
    }
    ctx->temps = NULL;
}

static int reindex_apply_key(rdb_pool_t *pool, int index, void *record,
        rdb_reindex_key_t *key, char **old_strings, size_t *old_count)
{
    uint32_t kind = pool->FLAGS[index] & RDB_KEYS;
    unsigned char *destination = (unsigned char *)record +
            pool->key_offset[index];
    rdb_kcf_index_t *metadata;
    size_t part;
    if (kind == RDB_KPSTR) {
        char **old_value = (char **)destination;
        char **new_value = (char **)key->bytes;
        old_strings[(*old_count)++] = *old_value;
        *old_value = *new_value;
        *new_value = NULL;
        return 0;
    }
    if (kind != RDB_KCF) {
        memcpy(destination, key->bytes, key->size);
        return 0;
    }
    metadata = kcf_index_meta(pool, index);
    for (part = 0; part < metadata->part_count; ++part) {
        const rdb_kcf_key_part_t *component = &metadata->parts[part];
        size_t size = reindex_type_size(component->type, component->size);
        unsigned char *to = destination + component->offset;
        unsigned char *from = key->bytes + component->offset;
        if (component->type == RDB_KPSTR) {
            char **old_value = (char **)to;
            char **new_value = (char **)from;
            old_strings[(*old_count)++] = *old_value;
            *old_value = *new_value;
            *new_value = NULL;
        } else {
            memcpy(to, from, size);
        }
    }
    return 0;
}

static void reindex_old_strings_free(char **strings, size_t count)
{
    size_t i, prior;
    for (i = 0; i < count; ++i) {
        if (!strings[i]) continue;
        for (prior = 0; prior < i; ++prior)
            if (strings[prior] == strings[i]) break;
        if (prior == i) rdb_free(strings[i]);
    }
}

static int reindex_flush(rdb_reindex_ctx_t *ctx)
{
    rdb_pool_t *pool = ctx->pool;
    rdb_reindex_entry_t *entry;
    size_t old_capacity = 0, old_count = 0, i;
    char **old_strings;
    int index, failed_index = -1;

    for (entry = ctx->entries; entry; entry = entry->next) {
        for (index = 0; index < pool->indexCount; ++index) {
            rdb_kcf_index_t *metadata;
            if (!(entry->indexes & RDB_INDEX_MASK(index))) continue;
            if ((pool->FLAGS[index] & RDB_KEYS) == RDB_KPSTR)
                ++old_capacity;
            else if ((pool->FLAGS[index] & RDB_KEYS) == RDB_KCF) {
                metadata = kcf_index_meta(pool, index);
                if (metadata) for (i = 0; i < metadata->part_count; ++i)
                    if (metadata->parts[i].type == RDB_KPSTR)
                        ++old_capacity;
            }
        }
    }
    old_strings = NULL;
    if (old_capacity)
        old_strings = rdb_alloc(sizeof(*old_strings) * old_capacity);
    if (old_capacity && !old_strings) {
        reindex_entries_free(ctx);
        return RDB_ERR_REINDEX_NOMEM;
    }

    // Remove every affected old key before changing any live record.
    for (entry = ctx->entries; entry; entry = entry->next) {
        for (index = 0; index < pool->indexCount; ++index) {
            if (!(entry->indexes & RDB_INDEX_MASK(index))) continue;
            if (_rdb_delete(pool, index, entry->record, NULL, NULL, 0) <= DELETE_MISSING) {
                failed_index = index;
                goto fault;
            }
        }
    }

    // Apply staged values while all affected index links are detached.
    for (entry = ctx->entries; entry; entry = entry->next)
        for (index = 0; index < pool->indexCount; ++index)
            if (entry->indexes & RDB_INDEX_MASK(index))
                reindex_apply_key(pool, index, entry->record,
                        &entry->keys[index], old_strings, &old_count);

    // Reinsert only after every staged value has been installed.
    for (entry = ctx->entries; entry; entry = entry->next) {
        for (index = 0; index < pool->indexCount; ++index) {
            if (!(entry->indexes & RDB_INDEX_MASK(index))) continue;
            if (_rdb_insert(pool, entry->record, pool->root[index], index,
                        NULL, 0) < 0) {
                failed_index = index;
                goto fault;
            }
        }
    }
    reindex_old_strings_free(old_strings, old_count);
    if (old_strings) rdb_free(old_strings);
    reindex_entries_free(ctx);
    return 0;

fault:
    {
        void *fault_record = entry ? entry->record : NULL;
        reindex_old_strings_free(old_strings, old_count);
        if (old_strings) rdb_free(old_strings);
        reindex_entries_free(ctx);
        return fault_pool(pool, failed_index, fault_record,
                "reindex apply", "staged index update could not be applied");
    }
}

static int reindex_string_seen_before(rdb_pool_t *pool, void *record,
        int current_index, size_t current_part, char *value)
{
    int index;
    size_t part;
    for (index = 0; index <= current_index; ++index) {
        uint32_t kind = pool->FLAGS[index] & RDB_KEYS;
        if (kind == RDB_KPSTR) {
            char *prior = *(char **)((unsigned char *)record +
                    pool->key_offset[index]);
            if (index < current_index && prior == value) return 1;
        } else if (kind == RDB_KCF) {
            rdb_kcf_index_t *metadata = kcf_index_meta(pool, index);
            if (!metadata) continue;
            for (part = 0; part < metadata->part_count; ++part) {
                char *prior;
                if (index == current_index && part >= current_part) break;
                if (metadata->parts[part].type != RDB_KPSTR) continue;
                prior = *(char **)((unsigned char *)record +
                        pool->key_offset[index] + metadata->parts[part].offset);
                if (prior == value) return 1;
            }
        }
    }
    return 0;
}

static void reindex_free_record_strings(rdb_pool_t *pool, void *record)
{
    int index;
    size_t part;
    for (index = 0; index < pool->indexCount; ++index) {
        uint32_t kind = pool->FLAGS[index] & RDB_KEYS;
        if (kind == RDB_KPSTR) {
            char **field = (char **)((unsigned char *)record +
                    pool->key_offset[index]);
            if (*field) {
                if (!reindex_string_seen_before(pool, record, index, 0, *field))
                    rdb_free(*field);
            }
        } else if (kind == RDB_KCF) {
            rdb_kcf_index_t *metadata = kcf_index_meta(pool, index);
            if (metadata) for (part = 0; part < metadata->part_count; ++part) {
                if (metadata->parts[part].type == RDB_KPSTR) {
                    char **field = (char **)((unsigned char *)record +
                            pool->key_offset[index] + metadata->parts[part].offset);
                    if (*field) {
                        if (!reindex_string_seen_before(pool, record, index,
                                    part, *field))
                            rdb_free(*field);
                    }
                }
            }
        }
    }
    for (index = 0; index < pool->indexCount; ++index) {
        uint32_t kind = pool->FLAGS[index] & RDB_KEYS;
        if (kind == RDB_KPSTR) {
            char **field = (char **)((unsigned char *)record +
                    pool->key_offset[index]);
            *field = NULL;
        } else if (kind == RDB_KCF) {
            rdb_kcf_index_t *metadata = kcf_index_meta(pool, index);
            if (metadata) for (part = 0; part < metadata->part_count; ++part)
                if (metadata->parts[part].type == RDB_KPSTR) {
                    char **field = (char **)((unsigned char *)record +
                            pool->key_offset[index] + metadata->parts[part].offset);
                    *field = NULL;
                }
        }
    }
}

static int reindex_delete_record(rdb_reindex_ctx_t *ctx, void *record,
        void (*del_fn)(void *, void *), void *del_data)
{
    if (unlink_record(ctx->pool, record, NULL, "iterate reindex") < 0)
        return RDB_ERR_FAULTED;
    reindex_free_record_strings(ctx->pool, record);
    if (del_fn) del_fn(record, del_data);
    else rdb_free(record);
    return 0;
}

int rdb_iterate_reindex(rdb_pool_t *pool, int index,
        rdb_iterate_reindex_fn fn, void *fn_data,
        void del_fn(void *, void *), void *del_data)
{
    rdb_reindex_ctx_t ctx;
    void **records;
    size_t count = 0, position = 0, i;
    int rc = 0;

    if (!pool || !valid_pool_index(pool, index) || !fn)
        return rdb_error_value(RDB_ERR_REINDEX_INVALID,
                "rdb_iterate_reindex requires a pool, valid index and callback");
    if (!pool->indexCount || pool->indexCount > RDB_POOL_MAX_IDX)
        return rdb_error_value(RDB_ERR_REINDEX_INVALID,
                "rdb_iterate_reindex requires a valid index count");
    if (pool->fault.code) return RDB_ERR_FAULTED;
    if (!(pool->FLAGS[index] & RDB_BTREE))
        return rdb_error_value(RDB_ERR_REINDEX_UNSUPPORTED,
                "rdb_iterate_reindex requires an RDB_BTREE index");
    if (pool->FLAGS[index] & RDB_NOKEYS)
        return rdb_error_value(RDB_ERR_REINDEX_UNSUPPORTED,
                "rdb_iterate_reindex requires a keyed traversal index");
    for (i = 0; i < pool->indexCount; ++i) {
        if (!pool->FLAGS[i])
            return rdb_error_value(RDB_ERR_REINDEX_INVALID,
                    "rdb_iterate_reindex requires all pool indexes to be registered");
        if ((pool->FLAGS[i] & RDB_KEYS) == RDB_KCFUT &&
                !(rdb_init_flags & RDB_INIT_ALLOW_KCFUT))
            return rdb_error_value(RDB_ERR_REINDEX_UNSUPPORTED,
                    "rdb_iterate_reindex rejects pools with RDB_KCFUT by default");
        if ((pool->FLAGS[i] & RDB_KEYS) == RDB_KCF) {
            rdb_kcf_index_t *metadata = kcf_index_meta(pool, i);
            if (!metadata || !metadata->part_count)
                return rdb_error_value(RDB_ERR_REINDEX_UNSUPPORTED,
                        "rdb_iterate_reindex requires registered RDB_KCF key parts");
        }
    }
    if (reindex_collect_count(pool, index, pool->root[index], &count) < 0 ||
            count > (size_t)-1 / sizeof(*records))
        return RDB_ERR_REINDEX_NOMEM;
    if (!count) return 0;
    records = rdb_alloc(count * sizeof(*records));
    if (!records) return RDB_ERR_REINDEX_NOMEM;
    reindex_collect_records(pool, index, pool->root[index], records, &position);

    memset(&ctx, 0, sizeof(ctx));
    ctx.pool = pool;
    ctx.entries_tail = &ctx.entries;
    for (i = 0; i < count; ++i) {
        int callback_result;
        if (pool->fault.code) {
            rc = RDB_ERR_FAULTED;
            break;
        }
        ctx.record = records[i];
        ctx.error = 0;
        callback_result = fn(records[i], fn_data, &ctx);
        reindex_temps_free(&ctx);
        if (ctx.error) {
            rc = ctx.error;
            break;
        }
        if (callback_result == RDB_CB_DELETE_NODE ||
                callback_result == RDB_CB_DELETE_NODE_AND_ABORT) {
            rdb_reindex_entry_t *entry = reindex_find_entry(&ctx, records[i]);
            if (entry) {
                rdb_reindex_entry_t **link = &ctx.entries;
                while (*link != entry) link = &(*link)->next;
                *link = entry->next;
                if (!*link) ctx.entries_tail = link;
                reindex_entry_free(pool, entry);
            }
            if (reindex_delete_record(&ctx, records[i], del_fn, del_data) < 0) {
                rc = RDB_ERR_FAULTED;
                break;
            }
            if (callback_result == RDB_CB_DELETE_NODE_AND_ABORT) {
                rc = RDB_ITERATE_ABORTED;
                break;
            }
        } else if (callback_result != RDB_CB_OK) {
            rc = RDB_ITERATE_ABORTED;
            break;
        }
    }
    if (!pool->fault.code) {
        int apply_rc = reindex_flush(&ctx);
        if (apply_rc < 0) rc = apply_rc;
    } else {
        reindex_entries_free(&ctx);
        rc = RDB_ERR_FAULTED;
    }
    reindex_temps_free(&ctx);
    rdb_free(records);
    return rc;
}

void _rdb_flush( 
        rdb_pool_t  *pool, 
        void        *start, 
        void        fn( void *, void *), 
        void        *fn_data) {

    void   *dataHead;
    PP_T   *pp;

    if (pool->FLAGS[0] & RDB_BTREE) {
        set_pointers( pool, 0, start, &pp, &dataHead);

        if (pp->left != NULL)
            _rdb_flush( pool, pp->left, fn, fn_data );

        if (pp->right != NULL)
            _rdb_flush( pool, pp->right, fn, fn_data );

        reindex_free_record_strings(pool, dataHead);
        if (NULL != fn) fn(dataHead, fn_data);
        else if (dataHead) rdb_free(dataHead);
    }
}

void _rdb_flush_list(   
        rdb_pool_t  *pool, 
        void        *start, 
        void        fn( void *, void *), 
        void *fn_data) {

    void   *dataHead;
    PP_T   *pp;

    if (pool->FLAGS[0] & RDB_BTREE)
        do {
            set_pointers( pool, 0, start, &pp, &dataHead);

            start = pp->right;

            reindex_free_record_strings(pool, dataHead);
            if (NULL != fn) fn(dataHead, fn_data);
            else if (dataHead) rdb_free(dataHead);
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
 * fn() is optional. rDB always frees registered KPSTR index strings before
 * calling fn. If fn is NULL, rDB also frees each record; any other user-owned
 * allocations require a callback.
 */
void rdb_flush( rdb_pool_t *pool, void fn( void *, void *), void *fn_data)
{

    int cnt;
    
    if (!pool || pool->fault.code || pool->root[0] == NULL)
        return;

    if (pool->FLAGS[0] & (RDB_NOKEYS))
        _rdb_flush_list (pool, NULL, fn, fn_data);
    else
        _rdb_flush (pool, NULL, fn, fn_data);

    for (cnt = 0; cnt < pool->indexCount; cnt++)
        if (pool->root[cnt])
            pool->root[cnt] = NULL;

#ifdef RDB_POOL_COUNTERS
    pool_count_set(pool, 0);
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
        return DELETE_SHRANK;
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
        return DELETE_SHRANK;
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
        return DELETE_SHRANK;
    } else {
        // Two children

        debug("rdbDeleteByPointer: a: I should never get here! index = %d, %p %p\n",
                                        index, ppkDead->left, ppkDead->right);
        return fault_pool(pool, index,
                (char *)ppkDead - sizeof(PP_T) * index, "delete",
                "unexpected two-child unlink");
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
        PP_T *ppk, *ppkRight, *ppkLeft;
        if (!pool->root[lookupIndex] || !data) return DELETE_MISSING;

        ppk = data + (sizeof (PP_T) * lookupIndex);
        ppkLeft = ppk->left;
        ppkRight = ppk->right;

        // These local checks detect broken queue membership without walking it.
        if ((!ppkLeft && pool->root[lookupIndex] != data) ||
            (!ppkRight && pool->tail[lookupIndex] != data))
            return DELETE_MISSING;
        if ((ppkLeft && ppkLeft->right != ppk) ||
            (ppkRight && ppkRight->left != ppk) ||
            (pool->root[lookupIndex] == data && ppkLeft) ||
            (pool->tail[lookupIndex] == data && ppkRight))
            return fault_pool(pool, lookupIndex, data, "delete",
                    "inconsistent queue links");

        if (pool->root[lookupIndex] == pool->tail[lookupIndex])
            pool->root[lookupIndex] = pool->tail[lookupIndex] = NULL;
        else {
            if (ppkLeft) ppkLeft->right = ppkRight;
            if (ppkRight) ppkRight->left = ppkLeft;
            if (pool->root[lookupIndex] == data)
                pool->root[lookupIndex] = ppkRight - lookupIndex;
            if (pool->tail[lookupIndex] == data)
                pool->tail[lookupIndex] = ppkLeft - lookupIndex;
        }
        // Detached entries must not retain pointers to former neighbours.
        ppk->left = ppk->right = NULL;
        return DELETE_REMOVED;
    } else if ((pool->FLAGS[lookupIndex] & RDB_BTREE) == RDB_BTREE) {
        debug("Delete:start: \n");

        if (pool->root[lookupIndex] == NULL) {
            return DELETE_MISSING;
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
                        return DELETE_MISSING;
                    }

                    rc2 = _rdb_delete (pool, lookupIndex, data, ppkDead->left,
                                                    dataHead, RDB_TREE_LEFT);
                    debug("(L)My Bal After  %d %d\n", ppkDead->balance, rc2);
                }
                else if (rc > 0) {
                    // Right child
                    debug("D:right Child!\n");

                    if (ppkDead->right == NULL) {
                        return DELETE_MISSING;
                    }

                    rc2 = _rdb_delete (pool, lookupIndex, data, ppkDead->right,
                                                    dataHead, RDB_TREE_RIGHT);
                    debug("(R)My Bal After  %d %d\n", ppkDead->balance, rc2);
                }

                if (rc2 <= DELETE_MISSING) return rc2;
                if (DELETE_SHRANK == rc2) { // And I am the parent...
                    ppkParent = (PP_T *) parent + lookupIndex;
                    ppkParent = parent + lookupIndex;
                    debug("- bal  to be changed = %d\n", ppkDead->balance);
                    // If on right side then add -1 - subtract one form balance
                    ppkDead->balance += (rc > 0) ? -1 : 1;
                    debug("- bal changed = %d\n", ppkDead->balance);

                    if (ppkDead->balance == -1 || ppkDead->balance == 1) {
                        // We moved from balance to -1 or +1 after delete, 
                        // nothing more we need to do
                        return DELETE_REMOVED;
                    } else if (ppkDead->balance == 0) {
                        // We transitioned to zero by delete, need to keep 
                        // balancing up
                        return DELETE_SHRANK;
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
                                    ppkRotate->balance == 1) return DELETE_REMOVED;
                            else return DELETE_SHRANK;

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

                            return DELETE_SHRANK;
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
                                            ppkRotate->balance == 1) return DELETE_REMOVED;
                            else return DELETE_SHRANK;

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

                            return DELETE_SHRANK;
                        }
                    }

                }
                return DELETE_REMOVED;
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

    return DELETE_MISSING;                  // Should never get here
}   

void   *rdb_delete_const (rdb_pool_t *pool, int idx, __intmax_t value)
{
    if (!valid_pool_index(pool, idx) || pool->fault.code) return NULL;
    debug("Get:pool=%s,idx=%d", pool->name, idx);
    return rdb_delete (pool, idx, &value); //, NULL, 0);
}

// Every index must confirm removal before count or caller ownership changes.
static int unlink_record(rdb_pool_t *pool, void *record, PP_T *queue_links,
        const char *operation)
{
    int index;
    for (index = 0; index < pool->indexCount; ++index) {
        if (queue_links && (pool->FLAGS[index] & RDB_NOKEYS))
            queue_links[index] = ((PP_T *)record)[index];
        if (_rdb_delete(pool, index, record, NULL, NULL, 0) <= DELETE_MISSING)
            return fault_pool(pool, index, record, operation,
                    "record could not be removed from every index");
    }
#ifdef RDB_POOL_COUNTERS
    pool_count_set(pool, pool_count(pool) - 1);
#endif
    return 0;
}

static void *delete_record(rdb_pool_t *pool, int index, void *key,
        PP_T *queue_links)
{
    void *record;
    if (pool->FLAGS[index] & RDB_NOKEYS)
        record = pool->root[index];
    else
        record = key ? _rdb_get(pool, index, key, NULL, 0) : NULL;

    if (!record || unlink_record(pool, record, queue_links, "delete") < 0)
        return NULL;
    return record;
}

void *rdb_delete(rdb_pool_t *pool, int lookupIndex, void *data)
{
    if (!valid_pool_index(pool, lookupIndex) || pool->fault.code) return NULL;
    return delete_record(pool, lookupIndex, data, NULL);
}

int rdb_delete_one(rdb_pool_t *pool, int index, void *data)
{
    int rc;
    if (!valid_pool_index(pool, index) || !data) return -1;
    if (pool->fault.code) return RDB_ERR_FAULTED;
    rc = _rdb_delete(pool, index, data, NULL, NULL, 0);
    return rc < 0 ? rc : rc == DELETE_SHRANK;
}

// Callers synchronize both pools and keep keys/comparators stable for the call.
static int move_record(rdb_pool_t *dst, rdb_pool_t *src, int index, void *key,
        void **moved)
{
    PP_T queue_links[RDB_POOL_MAX_IDX];
    void *record;
    int i;
    *moved = NULL;
    if (!valid_pool_index(src, index) || src->indexCount > RDB_POOL_MAX_IDX)
        return -1;
    if (src->fault.code || (dst && dst->fault.code)) return RDB_ERR_FAULTED;

    record = delete_record(src, index, key, queue_links);
    if (!record) return src->fault.code ? RDB_ERR_FAULTED : -1;

    if (dst && dst->indexCount && dst->indexCount <= RDB_POOL_MAX_IDX) {
        if (rdb_insert(dst, record) == dst->indexCount) {
            *moved = record;
            return 0;
        }
        if (dst->fault.code)
            return fault_pool(src, index, record, "move",
                    "destination rollback failed; record may remain linked there");
    } else {
        rdb_error("Move destination has no valid index layout");
    }

    // A rejected destination insert is recoverable if source membership can
    // be restored. Queue indexes also need their exact former positions back.
    if (rdb_insert(src, record) != src->indexCount)
        return fault_pool(src, index, record, "move rollback",
                "source restoration failed; check locking and comparators");
    for (i = 0; i < src->indexCount; ++i) {
        PP_T *link;
        if (!(src->FLAGS[i] & RDB_NOKEYS)) continue;
        if (_rdb_delete(src, i, record, NULL, NULL, 0) <= DELETE_MISSING)
            return fault_pool(src, i, record, "move rollback",
                    "queue position restoration failed");
        link = (PP_T *)record + i;
        *link = queue_links[i];
        if (link->left) ((PP_T *)link->left)->right = link;
        else src->root[i] = record;
        if (link->right) ((PP_T *)link->right)->left = link;
        else src->tail[i] = record;
    }
    return -2;
}

// Move record data without copying or reallocating it.
void *rdb_move_const (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx,
        __intmax_t value)
{
    return rdb_move(dst_pool, src_pool, idx, &value);
}

void *rdb_move (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, void *data)
{
    void *moved;
    move_record(dst_pool, src_pool, idx, data, &moved);
    return moved;
}

int rdb_move2 (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, void *data)
{
    void *moved;
    return move_record(dst_pool, src_pool, idx, data, &moved);
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
EXPORT_SYMBOL (rdb_init_ex);
EXPORT_SYMBOL (rdb_iterate_reindex);
EXPORT_SYMBOL (rdb_reindex_check_apply);
EXPORT_SYMBOL (rdb_reindex_kpstr_edit);
EXPORT_SYMBOL (rdb_register_kcf_key_parts);
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
