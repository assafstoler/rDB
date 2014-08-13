#ifndef __rDB_h_
#define __rDB_h_

#include <sys/time.h>
#include <stdint.h>

//#define RDB_KEYS RDB_KPSTR | RDB_KSTR | RDB_KINT | RDB_KUINT8 | RDB_KINT16 | RDB_KUINT16 | RDB_KINT32 | RDB_KUINT32 | RDB_KINT64 | RDB_KUINT64 | RDB_KINT128 | RDB_KUINT128 | RDB_KTMA | RDB_KTME | RDB_K4U32A
// for fifos and stuff
//#define RDB_NOKEYS (RDB_KFIFO | RDB_KLIFO

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
//#define RDB_KINT256  (1 << 15)  // Key is an signed 256 bit integer.
//#define RDB_KUINT256 (1 << 16)  // Key is an unsigned 256 bit integer.

// special case keys
#define RDB_KTME	(1 << 25)	// Key is a time_t structure
#define RDB_KTMA	(1 << 26)	// Key is time_t + 32 bit accomulator for non-unique key simulation
// Sorting order
#define RDB_KASC	(1 << 27)	// Key is sorted n ascending sequance
#define RDB_KDEC	(1 << 28)	// Key is worted in descending sequance
// Dara pool type (tree, list or fifo for now ... skip-lists planned)
#define RDB_BTREE	(1 << 29) //16384	// use btree for key/index - we always use AVL now.
#define RDB_LIST	(1 << 30) //32768	// use linked list for key storage
#define RDB_FIFO    (1 << 31) // FIFO - no Index


#define RDB_KEYS (RDB_KPSTR | RDB_KSTR | RDB_KINT8 | RDB_KUINT8 | RDB_KINT16 | \
    RDB_KUINT16 | RDB_KINT32 | RDB_KUINT32 | RDB_KINT64 | RDB_KUINT64 | \
    RDB_KINT128 | RDB_KUINT128 | RDB_KTME | RDB_KTMA | RDB_KCF)
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

#define RDB_POOL_MAX_IDX 16      		// how many indexes we allow on each pool (tree)

//TODO kill thouse special types
/*typedef struct _U32A {
    uint32_t	l1,
                l2,
                l3,
                acc;
} U32a;

typedef struct timeval_accomulator {
    struct timeval tv;
    uint32_t acc;
} TVA;
*/

typedef struct _type_u256 {
    __uint128_t lsb,
                msb;
} type_u256;

typedef struct _type_i256 {
    __uint128_t lsb;
    __int128_t 	msb;
} type_256;

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
    __int128_t  	i128;
    __uint128_t 	u128;
    type_256		i256;
    type_u256 		u256;
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

    // ID number of pool - think of it as a pool handle
    unsigned int 	id;

    // Number of indexs this data poll have
    unsigned char 	indexCount;

    // Number of bytes into the data structure to find the sort key
    unsigned int 	key_offset[RDB_POOL_MAX_IDX];

    // Flags for this pool. bitwise - one per index (globals use index zero)
    unsigned int 	FLAGS[RDB_POOL_MAX_IDX];       	

    // Fn() pointer for compare operation
    int32_t 	 	(*fn[RDB_POOL_MAX_IDX])();

    pthread_mutex_t write_mutex;
    pthread_mutex_t read_mutex;

    //   rdb_index_data_t**  index_data;             ///< index data master containder - dynamic
}  rdb_pool_t;

//TODO remove this backwards compatability hack
#define rdbRegisterPool(a,b,c,d)  rdb_register_um_pool(a, b, c, d, NULL) 
#define rdbRegisterIdx(a,b,c,d)  rdb_register_um_idx(a, b, c, d, NULL) 

rdb_pool_t *rdb_register_um_pool(char *poolName, int idxCount, int keyOffset, int FLAGS, void *);
int rdbRegisterPoolNew(char *poolName, int idxCount, int FLAGS);
int rdbRegisterIdxNew(int hdl,  int idx, int segment, int keyOffset, int FLAGS) ;
int rdb_register_um_idx(rdb_pool_t *,  int idx, int keyOffset, int FLAGS, void *) ;
int findPoolIdByName (char *poolName);
rdb_pool_t *findPoolAddressIdByName (char *poolName);
int rdb_insert(int id, void *data);
int rdbInsertOne (int id, int index, void *data);
int rdbDeleteOne (int id, int index, void *data);
void rdbDump(int id, int index);
void *rdbDelete(int id, int idx, void *data);
void *rdbGet(int id, int index , void *data) ;
void *rdbGetNeigh (int id, int idx, void *data, void **before, void **after) ;
void rdbInit(void);
void rdbIterateDelete(int id, int idx, int fn(void *, void *), void *fn_data, void del_fn(void *,
                      void *), void *del_data) ;
void rdbFlush( int id, void fn( void *, void *), void *fn_data);
void rdb_clean(void);
int rdbLock(int id);
int rdbUnlock(int id);

void rdb_init(void);
void _rdbDump (int id, int index, void *start);

int keyCompareNull (void *old, void *);
int keyCompareString (char *old, char *);
int keyCompareInt8 (int8_t *old, int8_t *);
int keyCompareUInt8 (uint8_t *old, uint8_t *);
int keyCompareInt16 (int16_t *old, int16_t *);
int keyCompareUInt16 (uint16_t *old, uint16_t *);
int keyCompareInt32 (int32_t *old, int32_t *);
int keyCompareUInt32 (uint32_t *old, uint32_t *);
int keyCompareInt64 (int64_t *old, int64_t *);
int keyCompareUInt64 (uint64_t *old, uint64_t *);
int keyCompareInt128 (__int128_t *old, __int128_t *);
int keyCompareUInt128 (__uint128_t *old, __uint128_t *);
//int keyCompareTME (struct timeval *old, struct timeval *);
//int keyCompareTMA (TVA *old, TVA *);
//int keyCompare4U32A (U32a *old, U32a *);


#endif
