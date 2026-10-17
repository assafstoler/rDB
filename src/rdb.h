//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

#ifndef __rDB_h_
#define __rDB_h_

#ifndef KM
#include "rdb_build_config.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
typedef struct timeval rdb_timeval_t;
#else
#include <linux/types.h>
typedef struct {
    long tv_sec;
    long tv_usec;
} rdb_timeval_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG
#ifdef KM
#define debug(b,arg...) printk(b,##arg)
#else

#define debug(b,arg...) do { fprintf(stdout, "%20s:%d: ",__FUNCTION__,__LINE__); fprintf(stdout,b,##arg); fflush(stdout); } while (0)
#endif
#else
#define debug(b,arg...)
#endif

#ifdef KM
// Kernel output    
#define rdb_info(b,arg...)  do {     \
    printk("%s: ",__FUNCTION__); \
    printk(b,##arg);            \
    } while (0)
#define rdb_c_info(b,arg...) printk(b,##arg)
#define rdb_fatal(b,arg...) do {    \
    info(b,##arg);              \
    } while (0)

#else
// Userspace 
#define rdb_fatal(b,arg...) do {    \
    fprintf(stderr,b,##arg);    \
    fflush(stderr);             \
    exit(-1);                   \
} while (0)

#define rdb_c_info(b,arg...)  do {    \
    fprintf(stdout,b,##arg);    \
    fflush(stdout);             \
    } while (0)
#define rdb_info(b,arg...)  do {    \
        fprintf(stdout, "%20s: ",__FUNCTION__); \
        rdb_c_info(b,##arg); \
    } while (0)
#define rdb_l_info(l,b,arg...)  do {    \
    if (l==0 || l & DEBUG_FLAGS) { \
        fprintf(stdout, "%20s: ",__FUNCTION__); \
        rdb_c_info(b,##arg); \
    } \
    } while (0)
#endif

//#ifdef KM
//#define rdb_error(b,arg...) printk(b,##arg)
//#else
//#define rdb_error(b,arg...) do { fprintf(stderr, "%20s:%d: ",__FUNCTION__,__LINE__); fprintf(stderr,b,##arg); fflush(stderr); } while (0)
//#endif

//#define RDB_LOCK_DEBUG

#define RDB_KFIFO   (1 << 0 )   // No key - make a FIFO in essence. uses one BPP_t 
#define RDB_KLIFO   (1 << 1 )   // No key - make a LIFO / stack in essence. uses one BPP_t 
#define RDB_KCF     (1 << 2 )   // comparator uses a registered supported key layout
#define RDB_KSTR	(1 << 3 )	// Key is a string
#define RDB_KPSTR	(1 << 4 )   // Key is a pointer to a string
#define RDB_KINT8	(1 << 5 )	// Key is an 8 bit integer
#define RDB_KUINT8	(1 << 6 )	// Key is an unsigned 8 bit integer
#define RDB_KINT16	(1 << 7 ) 	// Key is an 16 bit integer
#define RDB_KUINT16	(1 << 8 )	// Key is an unsigned 16 bit integer
#define RDB_KINT32	(1 << 9 )	// Key is an 32 bit integer
#define RDB_KUINT32	(1 << 10) 	// Key is an unsigned 32 bit integer
#define RDB_KINT64	(1 << 11)	// Key is an 64 bit integer
#define RDB_KUINT64	(1 << 12)	// Key is an unsigned 64 bit integer
#define RDB_KINT128	(1 << 13) 
#define RDB_KUINT128 (1 << 14)  // Key is an unsigned 128 bit integer - usefull for IPv6 !
#define RDB_KINT256  (1 << 15)  // Key is an signed 256 bit integer.
#define RDB_KUINT256 (1 << 16)  // Key is an unsigned 256 bit integer.
#define RDB_KPTR     (1 << 17)  // Key is a native pointer.
#define RDB_KSIZE_t  (1 << 18)  // Key is an unsigned native (size_t)
#define RDB_KSSIZE_t (1 << 19)  // Key is a signed natve (ssize_t)
#define RDB_KCFUT    (1U << 20) // custom function - user types; not reindexable

// special case keys
#define RDB_KTME	(1 << 25)	// Key is a time_t structure
#define RDB_KTMA	(1 << 26)	// Key is time_t + 32 bit accomulator for non-unique key simulation
// Sorting order
#define RDB_KASC	(1 << 27)	// Key is sorted n ascending sequance
#define RDB_KDEC	(1 << 28)	// Key is worted in descending sequance
// Data pool type (tree, list or fifo for now ... skip-lists planned)
#define RDB_BTREE	(1 << 29) //16384	// use btree for key/index - we always use AVL now.
#define RDB_LIST	(1 << 30) //32768	// use linked list for key storage
#define RDB_NO_IDX  (1U << 31) // FIFO or LIFO - no Index


#define RDB_KEYS (RDB_KPSTR | RDB_KSTR | RDB_KINT8 | RDB_KUINT8 | RDB_KINT16 | \
    RDB_KUINT16 | RDB_KINT32 | RDB_KUINT32 | RDB_KINT64 | RDB_KUINT64 | \
    RDB_KINT128 | RDB_KUINT128 | RDB_KPTR | RDB_KSIZE_t | RDB_KSSIZE_t | RDB_KTME | \
    RDB_KTMA | RDB_KCF | RDB_KCFUT)
#define RDB_NOKEYS (RDB_KFIFO | RDB_KLIFO)

#define RDB_TREE_LEFT   0
#define RDB_TREE_RIGHT  1
#define PARENT_BAL_CNG 	1

// Iteration callback results. Nonnegative index numbers and REINDEX_ALL are
// reserved; they do not perform any reindexing.
#define RDB_CB_OK			-1  		// continue;
#define RDB_CB_DELETE_NODE 	-2  		// delete this node out of the tree / list
#define RDB_CB_REINDEX_ALL	-3  		// reserved; not implemented
#define RDB_CB_DELETE_NODE_AND_ABORT -4 // delete this node and stop iterating the tree
#define RDB_CB_ABORT        -5  		// stop iterating the tree

#define RDB_POOL_MAX_IDX 24      		// how many indexes we allow on each pool (tree)
#define RDB_INDEX_MASK(index) (1U << (unsigned)(index))

// rdb_iterate_reindex return values and initialization options.
#define RDB_ITERATE_ABORTED 1
#define RDB_ERR_REINDEX_CONFLICT (-6)
#define RDB_ERR_REINDEX_UNSUPPORTED (-7)
#define RDB_ERR_REINDEX_UNMARKED (-8)
#define RDB_ERR_REINDEX_INVALID (-9)
#define RDB_ERR_REINDEX_NOMEM (-10)
#define RDB_INIT_ALLOW_KCFUT (1U << 0)

typedef uint32_t rdb_index_mask_t;
typedef struct rdb_reindex_ctx rdb_reindex_ctx_t;

typedef struct {
    size_t offset;  // from the comparator key base
    uint32_t type;  // scalar, RDB_KPTR, RDB_KPSTR or RDB_KSTR component
    size_t size;    // capacity for RDB_KSTR; zero for other component types
} rdb_kcf_key_part_t;

#ifdef USE_128_BIT_TYPES
#define __intmax_t __int128_t
#define __uintmax_t __uint128_t
#else 
#ifdef KM
#define __intmax_t int64_t
#define __uintmax_t uint64_t
#else
#define __intmax_t int64_t
#define __uintmax_t uint64_t
#endif
#endif

#ifdef USE_128_BIT_TYPES
typedef struct _type_u256 {
    __uint128_t lsb,
                msb;
} type_u256;

typedef struct _type_i256 {
    __uint128_t lsb;
    __int128_t  msb;
} type_256;
#else
typedef struct _type_u128 {
    uint64_t    lsb,
                msb;
} type_u128;

typedef struct _type_i128 {
    uint64_t    lsb;
    int64_t     msb;
} type_128;

#endif

typedef union {
    char      		*pStr;
    char       		str;
    int8_t     		i8;
    int16_t    		i16;
    int32_t    		i32;
    int64_t    		i64;
    uint8_t    		u8;
    uint16_t   		u16;
    uint32_t   		u32;
    uint64_t   		u64;
    size_t          st;
    ssize_t         sst;
#ifdef USE_128_BIT_TYPES
    __int128_t  	i128;
    __uint128_t 	u128;
    type_256		i256;
    type_u256 		u256;
#endif
    rdb_timeval_t tv;
 //   TVA	   tva;
 //   U32a	   u32a;
} rdb_key_union;

//typedef struct pointer_pack_s {
//    void 	*left;		// optional for rDB usage , user treats this as RO value
//    void 	*right;		// optional for rDB usage , user treats this as RO value
//} pp_t;
typedef struct balanced_pointer_pack_s {
    void 	*left;		// optional for rDB usage , user treats this as RO value
    void 	*right;		// optional for rDB usage , user treats this as RO value
    int	balance;	// rDB uses to keep track of AVL tree balance
} rdb_bpp_t;

// A pool retains the first detected consistency failure until it is dropped.
// Record operations then stop; rdb_drop_pool still releases the pool metadata.
#define RDB_ERR_FAULTED (-5)
typedef struct {
    int code;
    int index;
    void *record;
    const char *operation;
    const char *reason;
} rdb_fault_info_t;

typedef struct RDB_POOLS {
    // pointer to 1st (root) node - new
    rdb_bpp_t  		*root[RDB_POOL_MAX_IDX];

    // pointer to last-newest element inserted (currebtly used for FIFO only)
    rdb_bpp_t  		*tail[RDB_POOL_MAX_IDX];     
 
    // name of data pool (structure?)
    char   			*name;

    // pool chain pointers
    void   			*next; 
    void   			*prev;

    // Number of indexs this data poll have
    unsigned char 	indexCount;
    unsigned char   drop;   // if true, pool is to be GC'ed

    // Number of bytes into the data structure to find the sort key
    unsigned int 	key_offset[RDB_POOL_MAX_IDX];

    // Flags for this pool. bitwise - one per index (globals use index zero)
    uint32_t 	FLAGS[RDB_POOL_MAX_IDX];       	

    // Fn() pointer for compare operation
    int32_t 	 	(*fn[RDB_POOL_MAX_IDX])();
    int32_t 	 	(*get_fn[RDB_POOL_MAX_IDX])();
    int32_t 	 	(*get_const_fn[RDB_POOL_MAX_IDX])();

#ifdef KM
    struct semaphore write_mutex;
    struct semaphore read_mutex;
#else
    pthread_mutex_t write_mutex;
    pthread_mutex_t read_mutex;
#endif
    //   rdb_index_data_t**  index_data;             ///< index data master containder - dynamic
    // Library-maintained. Direct reads require normal pool synchronization;
    // rdb_print_pool_stats can sample it safely while record writers run.
    uint32_t        record_count;
    // Zero until a consistency failure is recorded.
    rdb_fault_info_t fault;
    // Private index metadata, managed by rDB.
    void *private_data;
}  rdb_pool_t;


extern char   	*rdb_error_string;
void        rdb_init(void);
// Set process-wide options before registering pools. By default, reindex
// iteration rejects pools containing RDB_KCFUT. Allowing them permits
// traversal, but RDB_KCFUT indexes themselves still cannot be reindexed.
int         rdb_init_ex(uint32_t flags);
// Return -1 for NULL, 0 for a healthy pool, or 1 for a faulted pool.
// If out is non-NULL, copy the saved fault information into it.
int         rdb_pool_get_fault(const rdb_pool_t *pool, rdb_fault_info_t *out);
// Save a copy of err, which may be the saved message or a suffix of it.
// On allocation failure, print err and retain the previous message (or NULL
// if none was saved). rdb_error_value always returns rv. Callers must serialize
// access to rdb_error_string against other error writers and cleanup.
int         rdb_error_value (int rv, char *err);
void        rdb_error (char *err);
// Registry traversal is synchronized; the returned handle is borrowed and
// must remain alive while the caller uses it.
rdb_pool_t *rdb_find_pool_by_name (char *poolName);
// Both pool creation APIs accept 1..RDB_POOL_MAX_IDX indexes. In user space,
// mutex initialization failure returns NULL, reports the lock and error code,
// and leaves the registry unchanged. key_size is the inline buffer capacity
// for RDB_KSTR and zero for every other primary index.
rdb_pool_t *rdb_add_pool (char *poolName, int indexCount, int key_offset,
                int FLAGS, void *compare_fn, size_t key_size);
rdb_pool_t *rdb_register_um_pool (char *poolName, 
	            int idxCount, int key_offset, int FLAGS, void *fn,
                size_t key_size);
void        rdb_clean(int);
void        rdb_gc(void);
// For a direct RDB_KSTR index, key_size is the inline buffer capacity;
// pass zero for all other index types (including RDB_KCF layouts).
int         rdb_register_um_idx (rdb_pool_t *pool, int idx, int key_offset,
                int FLAGS, void *compare_fn, size_t key_size);
int         rdb_lock(rdb_pool_t *pool, const char *parent); 
void         rdb_unlock(rdb_pool_t *pool, const char *parent);
int         rdb_insert (rdb_pool_t *pool, void *data);
int         rdb_insert_one (rdb_pool_t *pool, int index, void *data);
// For RDB_KPTR lookup/delete/move, pass &key (the address of a pointer value).
// A key whose value is NULL is queried with &key; data == NULL selects the root.
// For RDB_KSTR and RDB_KPSTR lookup/delete/move, pass the query string itself.
void       *rdb_get (rdb_pool_t *pool, int idx, const void *data);
// The _const APIs reject out-of-range values for built-in numeric and pointer
// keys. Custom comparators receive &value; FIFO/LIFO indexes ignore the value.
void       *rdb_get_const (rdb_pool_t *pool, int idx, __intmax_t value);
// Tree lookup with the same key convention as rdb_get (including string keys).
// On a miss, return NULL and the closest lower/higher records.
// before/after must point to writable pointer variables; both are always assigned.
// Missing neighbors, exact matches and data == NULL (select root) use NULL outputs.
void       *rdb_get_neigh (rdb_pool_t *pool, int idx, void *data, void **before, void **after);
// Returning RDB_CB_DELETE_NODE removes the current record and continues in
// order; RDB_CB_DELETE_NODE_AND_ABORT removes it and then stops iteration.
void        rdb_iterate(rdb_pool_t *pool, int index, int fn(void *, void *),
                void *fn_data, void del_fn(void *, void *), void *del_data);
typedef int (*rdb_iterate_reindex_fn)(void *record, void *user_data,
        rdb_reindex_ctx_t *ctx);
// The callback must make a shallow copy of the record and change index fields
// only on that copy. Submit every changed supported index with
// rdb_reindex_check_apply(). The record passed by rDB must remain unchanged.
// Accepted keys are retained by rDB and applied after callbacks finish;
// validation errors stop iteration, while earlier accepted keys are applied.
// RDB_KCF layouts must be registered before records are inserted. Pools with
// RDB_KCFUT indexes are rejected unless RDB_INIT_ALLOW_KCFUT was selected;
// those indexes must remain unchanged during reindex iteration.
int         rdb_iterate_reindex(rdb_pool_t *pool, int index,
                rdb_iterate_reindex_fn fn, void *fn_data,
                void del_fn(void *, void *), void *del_data);
// Validate and retain changed keys from candidate_record. A zero result means
// the proposal is unique against current and already-staged keys. It does not
// edit the live record or tree. The context and record argument are valid only
// for the active callback. Any index changed on the candidate must be marked;
// unsupported or unmarked edits fail and abort iteration.
int         rdb_reindex_check_apply(rdb_reindex_ctx_t *ctx, void *record,
                const void *candidate_record, rdb_index_mask_t changed_indexes);
// Make a private, bounded edit copy of a KPSTR key component and point the
// candidate field at it. Edit the returned buffer, then submit the candidate
// with rdb_reindex_check_apply(); rDB retains the accepted value and releases
// the edit buffer when the callback returns.
int         rdb_reindex_kpstr_edit(rdb_reindex_ctx_t *ctx, void *record,
                void *candidate_record, int index, size_t part_offset,
                size_t capacity, char **edit_buffer);
// Describe the fields read by an RDB_KCF comparator. Offsets are relative to
// that index's key base; RDB_KSTR parts require their buffer capacity in size.
// Register once, before inserting records into the index.
int         rdb_register_kcf_key_parts(rdb_pool_t *pool, int index,
                const rdb_kcf_key_part_t *parts, size_t part_count);
// rDB owns KPSTR values in registered indexes: it frees and clears them when
// records are deleted by iteration or rdb_flush, before calling del_fn.
void        rdb_flush( rdb_pool_t *pool, void fn( void *, void *), void *fn_data);
// Indexed record operations require 0 <= index < pool->indexCount and
// index < RDB_POOL_MAX_IDX (the source index for moves). Invalid indexes leave
// records and pool metadata unchanged. Pointer APIs return NULL; insert_one,
// delete_one and move2 return -1. Iterate and dump report an error and do no work.
void       *rdb_delete (rdb_pool_t *pool, int lookupIndex, void *data);
// Repeating this call on a still-live FIFO/LIFO record already unlinked by it
// returns 0 without changing or faulting the pool; never pass freed storage.
int         rdb_delete_one (rdb_pool_t *pool, int index, void *data);
void       *rdb_delete_const (rdb_pool_t *pool, int idx, __intmax_t value);
void       *rdb_move_const (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, __intmax_t value);
void       *rdb_move (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, void *data);
int         rdb_move2 (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, void *data);
// Release pool metadata and user-space mutexes, not unmanaged records. Stop
// users/waiters and release both locks before dropping a pool.
void        rdb_drop_pool (rdb_pool_t *pool);
void        rdb_print_pools(void *fp);
// Return buf; max_len includes the terminating NUL. Truncated reports end with
// "..." (fewer dots if the buffer cannot hold three dots and a NUL).
// A NULL buffer or nonpositive max_len causes no writes.
// Safe alongside normally locked record updates and pool registration/drop.
// Uses only the registry lock; counts are sampled individually, not as a
// simultaneous snapshot. Coordinate initialization and global cleanup as usual.
char       *rdb_print_pool_stats (char *buf, int max_len);

//void        _rdb_dump (rdb_pool_t *, int index, void *start);
void        rdb_dump (rdb_pool_t *pool, int index, char *separator);

int         key_cmp_str (char *old, char *);
int         key_cmp_str_p (char **old, char **);
int         key_cmp_int8 (int8_t *old, int8_t *);
int         key_cmp_uint8 (uint8_t *old, uint8_t *);
int         key_cmp_int16 (int16_t *old, int16_t *);
int         key_cmp_uint16 (uint16_t *old, uint16_t *);
int         key_cmp_int32 (int32_t *old, int32_t *);
int         key_cmp_uint32 (uint32_t *old, uint32_t *);
int         key_cmp_int64 (int64_t *old, int64_t *);
int         key_cmp_uint64 (uint64_t *old, uint64_t *);
int         key_cmp_size_t (size_t *old, size_t *);
int         key_cmp_ssize_t (ssize_t *old, ssize_t *);
int         key_cmp_ptr (void **old, void **);
#ifdef USE_128_BIT_TYPES
int         key_cmp_int128 (__int128_t *old, __int128_t *);
int         key_cmp_uint128 (__uint128_t *old, __uint128_t *);
#endif

//This is the only one where get = insert...
//int         key_cmp_str (char *old, char *);
int         key_cmp_const_str_p (char **old, char *);
int         key_cmp_const_int8 (int8_t *old, __intmax_t);
int         key_cmp_const_uint8 (uint8_t *old, __uintmax_t);
int         key_cmp_const_int16 (int16_t *old, __intmax_t);
int         key_cmp_const_uint16 (uint16_t *old, __uintmax_t);
int         key_cmp_const_int32 (int32_t *old, __intmax_t);
int         key_cmp_const_uint32 (uint32_t *old, __uintmax_t);
int         key_cmp_const_int64 (int64_t *old, __intmax_t);
int         key_cmp_const_uint64 (uint64_t *old, __uintmax_t);
int         key_cmp_const_size_t (size_t *old, size_t);
int         key_cmp_const_ssize_t (ssize_t *old, ssize_t);
int         key_cmp_const_ptr (void *old, size_t);
#ifdef USE_128_BIT_TYPES
int         key_cmp_const_int128 (__int128_t *old, __intmax_t);
int         key_cmp_const_uint128 (__uint128_t *old, __uintmax_t);
#endif
//int keyCompareTME (struct timeval *old, struct timeval *);
//int keyCompareTMA (TVA *old, TVA *);
//int keyCompare4U32A (U32a *old, U32a *);


#ifdef __cplusplus
}
#endif
#endif
