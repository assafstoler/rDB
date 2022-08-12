//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

#ifndef __rDB_h_
#define __rDB_h_

#ifndef KM
#include <sys/time.h>
#include <stdint.h>
#include <stddef.h>
#else
#include <linux/types.h>
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

#ifdef KM
#define error(b,arg...) printk(b,##arg)
#else
#define error(b,arg...) do { fprintf(stderr, "%20s:%d: ",__FUNCTION__,__LINE__); fprintf(stderr,b,##arg); fflush(stderr); } while (0)
#endif

//#define RDB_LOCK_DEBUG

#define RDB_KFIFO   (1 << 0 )   // No key - make a FIFO in essence. uses one BPP_t 
#define RDB_KLIFO   (1 << 1 )   // No key - make a LIFO / stack in essence. uses one BPP_t 
#define RDB_KCF     (1 << 2 )   // use custom function to check index order. key type N/A
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

// special case keys
#define RDB_KTME	(1 << 25)	// Key is a time_t structure
#define RDB_KTMA	(1 << 26)	// Key is time_t + 32 bit accomulator for non-unique key simulation
// Sorting order
#define RDB_KASC	(1 << 27)	// Key is sorted n ascending sequance
#define RDB_KDEC	(1 << 28)	// Key is worted in descending sequance
// Data pool type (tree, list or fifo for now ... skip-lists planned)
#define RDB_BTREE	(1 << 29) //16384	// use btree for key/index - we always use AVL now.
#define RDB_LIST	(1 << 30) //32768	// use linked list for key storage
#define RDB_NO_IDX  (1 << 31) // FIFO or LIFO - no Index


#define RDB_KEYS (RDB_KPSTR | RDB_KSTR | RDB_KINT8 | RDB_KUINT8 | RDB_KINT16 | \
    RDB_KUINT16 | RDB_KINT32 | RDB_KUINT32 | RDB_KINT64 | RDB_KUINT64 | \
    RDB_KINT128 | RDB_KUINT128 | RDB_KPTR | RDB_KSIZE_t | RDB_KSSIZE_t | RDB_KTME | \
    RDB_KTMA | RDB_KCF)
#define RDB_NOKEYS (RDB_KFIFO | RDB_KLIFO)

#define RDB_TREE_LEFT   0
#define RDB_TREE_RIGHT  1
#define PARENT_BAL_CNG 	1

// Iterate(Delete) return codes.
// values from 0 on up means a re-index of index n is needed (meaning that data that it part of the index has changes
#define RDB_CB_OK			-1  		// continue;
#define RDB_CB_DELETE_NODE 	-2  		// delete this node out of the tree / list
#define RDB_CB_REINDEX_ALL	-3  		// more then one index data has changed re-index all
#define RDB_CB_DELETE_NODE_AND_ABORT -4 // delete this node and stop iterating the tree
#define RDB_CB_ABORT        -5  		// stop iterating the tree

#define RDB_POOL_MAX_IDX 24      		// how many indexes we allow on each pool (tree)

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
    struct    		timeval tv;
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
#ifdef RDB_POOL_COUNTERS
    uint32_t        record_count;
#endif
}  rdb_pool_t;


extern char   	*rdb_error_string;
void        rdb_init(void);
int         rdb_error_value (int rv, char *err);
void        rdb_error (char *err);
rdb_pool_t *rdb_find_pool_by_name (char *poolName);
rdb_pool_t *rdb_add_pool (char *poolName, int indexCount, int key_offset,
                int FLAGS, void *compare_fn);
rdb_pool_t *rdb_register_um_pool (char *poolName, 
	            int idxCount, int key_offset, int FLAGS, void *fn);
void        rdb_clean(int);
void        rdb_gc(void);
int         rdb_register_um_idx (rdb_pool_t *pool, int idx, int key_offset,
                int FLAGS, void *compare_fn);
int         rdb_lock(rdb_pool_t *pool, const char *parent); 
void         rdb_unlock(rdb_pool_t *pool, const char *parent);
int         rdb_insert (rdb_pool_t *pool, void *data);
int         rdb_insert_one (rdb_pool_t *pool, int index, void *data);
void       *rdb_get (rdb_pool_t *pool, int idx, const void *data);
void       *rdb_get_const (rdb_pool_t *pool, int idx, __intmax_t value);
void       *rdb_get_neigh (rdb_pool_t *pool, int idx, void *data, void **before, void **after);
void        rdb_iterate(rdb_pool_t *pool, int index, int fn(void *, void *),
                void *fn_data, void del_fn(void *, void *), void *del_data);
void        rdb_flush( rdb_pool_t *pool, void fn( void *, void *), void *fn_data);
void       *rdb_delete (rdb_pool_t *pool, int lookupIndex, void *data);
int         rdb_delete_one (rdb_pool_t *pool, int index, void *data);
void       *rdb_delete_const (rdb_pool_t *pool, int idx, __intmax_t value);
void       *rdb_move_const (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, __intmax_t value);
void       *rdb_move (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, void *data);
int         rdb_move2 (rdb_pool_t *dst_pool, rdb_pool_t *src_pool, int idx, void *data);
void        rdb_drop_pool (rdb_pool_t *pool);
void        rdb_print_pools(void *fp);
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
