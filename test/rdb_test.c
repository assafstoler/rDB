#include <stdio.h>  //printf,
#include <stdlib.h> //exit,
#include <string.h>
#include <math.h>   // pow
#include <unistd.h> // getopt
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include "rDB.h"

#ifdef fatal
#undef fatal
#define fatal(b,arg...) do {     \
        fprintf(stdout,b,##arg); \
        fprintf(stdout,"\n");    \
        fflush(stderr);          \
        exit(-1);                \
        } while (0);
#endif

#ifdef info
#undef info
#define info(b,arg...) do {     \
        printf(b,##arg); \
        fflush(stdout);          \
        } while (0);
#endif





// On how many indexes we would refferance our demo data.
// the use of define here is not required and done for conveniance only

#define TEST_INDEXES 17
// Index numbers can change when / if we add or remove indexes.
// It's a good programing disciplan to define a name to the indexes to
// avoid unexpected behaviour when that happens
//
#define IDX_STR     0
#define IDX_PSTR    1
#define IDX_UI8     2
#define IDX_I8      3
#define IDX_UI16    4
#define IDX_I16     5
#define IDX_UI32    6
#define IDX_I32     7
#define IDX_UI64    8
#define IDX_I64     9
#define IDX_UI128   10
#define IDX_I128    11
#define IDX_CUSTOM_1  12
#define IDX_CUSTOM_2  13
#define IDX_FIFO    14
#define IDX_LIFO    15
#define IDX_KPTR    16
//#defnie IDX_UI256
//#defnie IDX_I256
//

#define RDB_TEST_STRING "RDB_TEST_STRING"

typedef struct test_data_s {

    // This is the rDB pointer pack (times # of indexes for this structure) 
    // It is required at to be at the top of a structure to be used with rDB.
     rdb_bpp_t pp[TEST_INDEXES]; 
    // Below is user data
    // Strait indexes.
     char          string[5];
     char          *string_ptr;
    uint8_t     ui8;
    int8_t      i8;
    uint16_t    ui16;
    int16_t     i16;
    uint32_t    ui32;
    int32_t     i32;
    uint64_t    ui64;
    int64_t     i64;
#ifdef USE_128_BIT_TYPES
    __uint128_t ui128;
    __int128_t  i128;
#endif
    // Soon, people, Soon...
    //type_u256   ui256;
    //type_256    i256;
    // User defined index with custon compare fn.
    // for test we will use 2 fields
    int32_t     ud1;
    char        ud2[5];
    char        *ud3;
} test_data_t;
typedef struct test_data_one_s {

    // This is the rDB pointer pack (times # of indexes for this structure) 
    // It is required at to be at the top of a structure to be used with rDB.
     rdb_bpp_t pp[1]; 
    // Below is user data
    // Strait indexes.
     char          string[5];
     char          *string_ptr;
    uint8_t     ui8;
    int8_t      i8;
    uint16_t    ui16;
    int16_t     i16;
    uint32_t    ui32;
    int32_t     i32;
    uint64_t    ui64;
    int64_t     i64;
#ifdef USE_128_BIT_TYPES
    __uint128_t ui128;
    __int128_t  i128;
#endif
    // Soon, people, Soon...
    //type_u256   ui256;
    //type_256    i256;
    // User defined index with custon compare fn.
    // for test we will use 2 fields
    int32_t     ud1;
    char        ud2[5];
    char        *ud3;
} test_data_one_t;

// We define a data set, and a data set pointer that we shall use later on
test_data_t td,
            *ptd;
// We define a data set, and a data set pointer that we shall use later on
test_data_one_t one_td,
            *one_ptd;

// Those are the handle which is used to access and identify a data pools
// Each data pool will have one. We prepare 16 handles... probably won't use all
rdb_pool_t  *pool1,
            *pool2,
            *pool3,
            *pool4,
            *pool5,
            *pool6,
            *pool7,
            *pool8,
            *pool9,
            *pool10,
            *pool11,
            *pool12,
            *pool13,
            *pool14,
            *pool15,
            *pool16;

// now we define a compare function for out cusotm index.
int compare_custom_index(void *a, void *b){
    int32_t *ia, *ib;
    char    *sa, *sb;

    ia = a;
    ib = b;
    sa = (void *) ia + sizeof (int32_t);
    sb = (void *) ib + sizeof (int32_t);
    
    return ((*ia < *ib) ? -1 : (*ia > *ib) ? 1 : strncmp(sa,sb,5));
}

int compare_custom_2_index(void *a, void *b){
    uint8_t *ia, *ib;

    ib = a;
    ia = b;
    
    return ((*ia < *ib) ? -1 : (*ia > *ib) ? 1 : 0);
}


#ifdef __out
// Insert one record, and dump it to stdout, then flush tree
int simple_demo(rdb_pool_t *pool) {

    pdd=malloc(sizeof(demo_data_t));
    if (!pdd) fatal("fatal: failed allocating data node");
     info("main addr %p\n", pdd);    

    // We allocate the address pointer so it can point to a string
     pdd->address_ptr = malloc(64);
    if (!pdd->address_ptr) fatal("fatal: failed allocating address_pointer");
    info ("address ptr = %p\n", pdd->address_ptr);

     strcpy(pdd->name,"Assaf Stoler");
     strcpy(pdd->address_ptr,"rdb@assafstoler.com");
     pdd->age=43;
     pdd->long_value=100000;

    // Data node is added to the tree, and Indexed.
     if (rdb_insert(pool,pdd) != INDEXES) 
        error("one or more index failed insertioni\n");

     info("We will now dump the tree, containing one record\n");
    rdb_dump(pool,1);

    // rDB is smart enought to know it needs to free address_ptr before it free
    // the actual data node. (since it's an indexed field) thus helping avoid
    // memory leaks. if we had non-indexed allocated data in out structure, we
    // would have had to use an at-delete-time callback fn which would have 
    // freed that data. since we dont, we just use NULL's
    rdb_flush(pool, NULL, NULL);

    // The pool is now empty again... ready for our next demo
    // As such, the dump below will produce no data
    rdb_dump(pool,0);

    return 0;
}

// How much data we want ... we get LOOPS to the power of 4.
// Default (18) will produce appx 100K records.
#define LOOPS 18

// Here we will insert 104976 records to the tree, with 4 indexes each
int um_multi_record_insert_demo(rdb_pool_t *pool) {
     int rc,a,b,c,d;
     
    for (a=0; a<LOOPS; a++) {
          for (b=0; b<LOOPS; b++) {
               for (c=0; c<LOOPS; c++) {
                    for (d=0; d<LOOPS; d++) {
                         pdd=malloc(sizeof(demo_data_t));
                         if (pdd==NULL) fatal("Allocation error in %s\n",__FUNCTION__);
                         pdd->address_ptr = calloc(1,16);
                         if (pdd->address_ptr == NULL) fatal("Allocation error in %s\n",__FUNCTION__);
                         pdd->name[0]='A' + d;
                         pdd->name[1]='A' + c;
                         pdd->name[2]='A' + b;
                         pdd->name[3]='A' + a;
                         pdd->name[4]=0;
                         *(pdd->address_ptr)='a' + d;
                    *(pdd->address_ptr+1)='a' + c;
                    *(pdd->address_ptr+2)='a' + b;
                         *(pdd->address_ptr+3)='a' + a;
                    // We could have calculated the various LOOPS powers out
                    // of this loop, to increase perfoemance, but not an issue
                    // for this demo
                         pdd->age= d + (c * LOOPS) + (b * pow(LOOPS, 2)) + (a * pow(LOOPS, 3)) ;
                         pdd->long_value=pow(LOOPS, 4) - pdd->age;
                         rc=rdb_insert(pool,pdd);
                    // We check that rDB was able to link all indexes. rDB will
                    // simply skip indexes it can not link-in (due to 
                    // duplicates, for example)
                         if (rc!=INDEXES) fatal ("%s: INSERT rc=%d %s",__FUNCTION__ , rc, rdb_error_string);
                    }
               }
          }
     }
    info ("we Just completed %.0f insertions (%.0f records  * 4 indexes)\n", pow(LOOPS,4)*4, pow(LOOPS,4));

    return 0;
}
#endif 

// Index numbers can change when / if we add or remove indexes.
// It's a good programing disciplan to define a name to the indexes to
// avoid unexpected behaviour when that happens


int register_pool_1() {

    pool1 = rdb_register_um_pool("test_pool", 
                            TEST_INDEXES, 
                            0, // offset if first index. usually it's zero
                            RDB_KSTR | RDB_KASC | RDB_BTREE,
                            NULL);
    if (pool1 == NULL) return -1;

    // Registering the other indexes for our data structure.
     if (rdb_register_um_idx(pool1,
                            IDX_PSTR, 
                            (void *) &td.string_ptr- (void *) &td.string,
                            RDB_KPSTR | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;
     
    if (rdb_register_um_idx(pool1,
                            IDX_UI8, 
                            (void *) &td.ui8 - (void *) &td.string,
                            RDB_KUINT8 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_I8, 
                            (void *) &td.i8 - (void *) &td.string,
                            RDB_KINT8 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_UI16, 
                            (void *) &td.ui16 - (void *) &td.string,
                            RDB_KUINT16 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_I16, 
                            (void *) &td.i16 - (void *) &td.string,
                            RDB_KINT16 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_UI32, 
                            (void *) &td.ui32 - (void *) &td.string,
                            RDB_KUINT32 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_I32, 
                            (void *) &td.i32 - (void *) &td.string,
                            RDB_KINT32 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_UI64, 
                            (void *) &td.ui64 - (void *) &td.string,
                            RDB_KUINT64 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_I64, 
                            (void *) &td.i64 - (void *) &td.string,
                            RDB_KINT64 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

#ifdef USE_128_BIT_TYPES
    if (rdb_register_um_idx(pool1,
                            IDX_UI128, 
                            (void *) &td.ui128 - (void *) &td.string,
                            RDB_KUINT128 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_I128, 
                            (void *) &td.i128 - (void *) &td.string,
                            RDB_KINT128 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;
#else 
    // use 64 for tests to be able to pass regardless
    if (rdb_register_um_idx(pool1,
                            IDX_UI128, 
                            (void *) &td.ui64 - (void *) &td.string,
                            RDB_KUINT64 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_I128, 
                            (void *) &td.i64 - (void *) &td.string,
                            RDB_KINT64 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;
#endif
/*    if (rdb_register_um_idx(pool1,
                            IDX_UI256, 
                            (void *) &td.ui256 - (void *) &td.string,
                            RDB_KUINT256 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_I256, 
                            (void *) &td.i256 - (void *) &td.string,
                            RDB_KINT256 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;
*/
    if (rdb_register_um_idx(pool1,
                            IDX_CUSTOM_1, 
                            (void *) &td.ud1 - (void *) &td.string,
                            RDB_KCF | RDB_KASC | RDB_BTREE,
                            compare_custom_index) == -1) return -1;

    if (rdb_register_um_idx(pool1,
                            IDX_CUSTOM_2, 
                            (void *) &td.ui8 - (void *) &td.string,
                            RDB_KCF | RDB_KASC | RDB_BTREE,
                            compare_custom_2_index) == -1) return -1;
    
    if (rdb_register_um_idx(pool1,
                            IDX_FIFO, 
                            0,
                            RDB_KFIFO | RDB_NO_IDX | RDB_BTREE,
                            NULL) == -1) return -1;
    
    if (rdb_register_um_idx(pool1,
                            IDX_LIFO, 
                            0,
                            RDB_KLIFO | RDB_NO_IDX | RDB_BTREE,
                            NULL) == -1) return -1;
    
    if (rdb_register_um_idx(pool1,
                            IDX_KPTR, 
                            (void *) &td.string_ptr - (void *) &td.string,
                            RDB_KPTR | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    return 0;
}

int register_pool_2() {

    pool2 = rdb_register_um_pool("copy_pool", 
                            TEST_INDEXES, 
                            0, // offset if first index. usually it's zero
                            RDB_KSTR | RDB_KASC | RDB_BTREE,
                            NULL);
    if (pool2 == NULL) return -1;

    // Registering the other indexes for our data structure.
     if (rdb_register_um_idx(pool2,
                            IDX_PSTR, 
                            (void *) &td.string_ptr- (void *) &td.string,
                            RDB_KPSTR | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;
     
    if (rdb_register_um_idx(pool2,
                            IDX_UI8, 
                            (void *) &td.ui8 - (void *) &td.string,
                            RDB_KUINT8 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_I8, 
                            (void *) &td.i8 - (void *) &td.string,
                            RDB_KINT8 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_UI16, 
                            (void *) &td.ui16 - (void *) &td.string,
                            RDB_KUINT16 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_I16, 
                            (void *) &td.i16 - (void *) &td.string,
                            RDB_KINT16 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_UI32, 
                            (void *) &td.ui32 - (void *) &td.string,
                            RDB_KUINT32 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_I32, 
                            (void *) &td.i32 - (void *) &td.string,
                            RDB_KINT32 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_UI64, 
                            (void *) &td.ui64 - (void *) &td.string,
                            RDB_KUINT64 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_I64, 
                            (void *) &td.i64 - (void *) &td.string,
                            RDB_KINT64 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

#ifdef USE_128_BIT_TYPES
    if (rdb_register_um_idx(pool2,
                            IDX_UI128, 
                            (void *) &td.ui128 - (void *) &td.string,
                            RDB_KUINT128 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_I128, 
                            (void *) &td.i128 - (void *) &td.string,
                            RDB_KINT128 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;
#endif

/*    if (rdb_register_um_idx(pool2,
                            IDX_UI256, 
                            (void *) &td.ui256 - (void *) &td.string,
                            RDB_KUINT256 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_I256, 
                            (void *) &td.i256 - (void *) &td.string,
                            RDB_KINT256 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;
*/
    if (rdb_register_um_idx(pool2,
                            IDX_CUSTOM_1, 
                            (void *) &td.ud1 - (void *) &td.string,
                            RDB_KCF | RDB_KASC | RDB_BTREE,
                            compare_custom_index) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_CUSTOM_2, 
                            (void *) &td.ui8 - (void *) &td.string,
                            RDB_KCF | RDB_KASC | RDB_BTREE,
                            compare_custom_2_index) == -1) return -1;

    if (rdb_register_um_idx(pool2,
                            IDX_FIFO, 
                            0,
                            RDB_KFIFO | RDB_NO_IDX | RDB_BTREE,
                            NULL) == -1) return -1;
    
    if (rdb_register_um_idx(pool2,
                            IDX_LIFO, 
                            0,
                            RDB_KLIFO | RDB_NO_IDX | RDB_BTREE,
                            NULL) == -1) return -1;
    
    if (rdb_register_um_idx(pool2,
                            IDX_KPTR, 
                            (void *) &td.string_ptr - (void *) &td.string,
                            RDB_KPTR | RDB_KASC | RDB_BTREE,
                            NULL) == -1) return -1;
    
    return 0;
}

int register_pool_3() {

    pool3 = rdb_register_um_pool("no_keys_pool", 
                            2, 
                            0, // offset if first index. usually it's zero
                            RDB_KFIFO | RDB_NO_IDX | RDB_BTREE,
                            NULL);
    if (pool3 == NULL) return -1;

    if (rdb_register_um_idx(pool3,
                            1, 
                            0,
                            RDB_KLIFO | RDB_NO_IDX | RDB_BTREE,
                            NULL) == -1) return -1;
    
    return 0;
}

int register_pools() {

    if (-1 == register_pool_1()) fatal("Failed registring pool 1");
    if (-1 == register_pool_2()) fatal("Failed registring pool 2");
    if (-1 == register_pool_3()) fatal("Failed registring pool 3");
    return 0;
}

int add_test_data (rdb_pool_t *pool, int loops) {
     int rc,a,b,c,d;
     
    for (a=0; a<loops; a++) {
          for (b=0; b<loops; b++) {
               for (c=0; c<loops; c++) {
                    for (d=0; d<loops; d++) {
                         ptd=malloc(sizeof(test_data_t));
                         if (ptd==NULL) fatal("Allocation error in %s\n",__FUNCTION__);
                         //printf("%p - ",ptd);
                         ptd->string_ptr = calloc(1,16);
                         if (ptd->string_ptr == NULL) return -1;
                         ptd->string[0]='A' + d;
                         ptd->string[1]='A' + c;
                         ptd->string[2]='A' + b;
                         ptd->string[3]='A' + a;
                         ptd->string[4]=0;
                         *(ptd->string_ptr)='a' + d;
                    *(ptd->string_ptr+1)='a' + c;
                    *(ptd->string_ptr+2)='a' + b;
                         *(ptd->string_ptr+3)='a' + a;
                         //printf("%p %s\n",ptd->string_ptr,ptd->string_ptr);
                    // We could have pre-calculated the various LOOPS powers out
                    // of this loop, to increase perfoemance, but not an issue
                    // for this test
#ifdef USE_128_BIT_TYPES
                    ptd->ui128 = d + (c * loops) + (b * pow(loops, 2)) + 
                                                    (a * pow(loops, 3)) ;
                    ptd->i128 = ptd->ui128 * -1;
                    
                    ptd->ui64 = ptd->ui128;
                    ptd->i64 = ptd->i128;
#else
                    ptd->ui64 = d + (c * loops) + (b * pow(loops, 2)) + 
                                                    (a * pow(loops, 3)) ;
                    ptd->i64 = ptd->ui64 * -1;
#endif                    
                    ptd->ui32 = ptd->ui64;
                    ptd->i32 = ptd->i64;
                    
                    ptd->ui16 = ptd->ui64;
                    ptd->i16 = ptd->i64;

                    ptd->ui8 = ptd->ui64;
                    ptd->i8 = ptd->i64;
                
                    // those two will make a unique index together...
                    // udq will repeat itself, but combined with ud2 no issue
                    ptd->ud1 = ptd->ui8;
                    memcpy(ptd->ud2, ptd->string,5);

                    //printf("%s %d %u\n", ptd->string, ptd->i32, ptd->ui32);
                         rc=rdb_insert(pool,ptd);
                         //printf("rc=%d\n", rc);
                    // We check that rDB was able to link all indexes. rDB will
                    // simply skip indexes it can not link-in (due to 
                    // duplicates, for example)
                         if (rc!=TEST_INDEXES) {
                             debug("Reduced index coverage test\n");
                             //return -1; 
                         }
                        //fatal ("%s: INSERT rc=%d %s",__FUNCTION__ , rc, rdb_error_string);
                    }
               }
          }
     }
    return 0;
}

int add_one_test_data (rdb_pool_t *pool, int loops) {
    int rc,a,b,c,d;

    for (a=0; a<loops; a++) {
        for (b=0; b<loops; b++) {
            for (c=0; c<loops; c++) {
                for (d=0; d<loops; d++) {
                    one_ptd=calloc(1,sizeof(test_data_one_t));
                    if (one_ptd==NULL) fatal("Allocation error in %s\n",__FUNCTION__);
                    one_ptd->string_ptr = calloc(1,16);
                    if (one_ptd->string_ptr == NULL) return -1;
                    one_ptd->string[0]='A' + d;
                    one_ptd->string[1]='A' + c;
                    one_ptd->string[2]='A' + b;
                    one_ptd->string[3]='A' + a;
                    one_ptd->string[4]=0;
                    *(one_ptd->string_ptr)='a' + d;
                    *(one_ptd->string_ptr+1)='a' + c;
                    *(one_ptd->string_ptr+2)='a' + b;
                    *(one_ptd->string_ptr+3)='a' + a;
                    // We could have pre-calculated the various LOOPS powers out
                    // of this loop, to increase perfoemance, but not an issue
                    // for this test
#ifdef USE_128_BIT_TYPES
                    one_ptd->ui128 = d + (c * loops) + (b * pow(loops, 2)) + 
                        (a * pow(loops, 3)) ;
                    one_ptd->i128 = one_ptd->ui128 * -1;

                    one_ptd->ui64 = one_ptd->ui128;
                    one_ptd->i64 = one_ptd->i128;
                    //printf("%ld %s\n", one_ptd->ui64, one_ptd->string);
#else
                    one_ptd->ui64 = d + (c * loops) + (b * pow(loops, 2)) + 
                        (a * pow(loops, 3)) ;
                    one_ptd->i64 = one_ptd->ui64 * -1;
#endif                    
                    one_ptd->ui32 = one_ptd->ui64;
                    one_ptd->i32 = one_ptd->i64;

                    one_ptd->ui16 = one_ptd->ui64;
                    one_ptd->i16 = one_ptd->i64;

                    one_ptd->ui8 = one_ptd->ui64;
                    one_ptd->i8 = one_ptd->i64;

                    // those two will make a unique index together...
                    // udq will repeat itself, but combined with ud2 no issue
                    one_ptd->ud1 = one_ptd->ui8;
                    memcpy(one_ptd->ud2, one_ptd->string,5);

                    //printf("%s %d %u\n", one_ptd->string, one_ptd->i32, one_ptd->ui32);
                    rc=rdb_insert(pool,one_ptd);
                    //printf("rc=%d\n", rc);
                    // We check that rDB was able to link all indexes. rDB will
                    // simply skip indexes it can not link-in (due to 
                    // duplicates, for example)
                    if (rc!=TEST_INDEXES) {
                        debug("Reduced index coverage test\n");
                        //return -1; 
                    }
                    //fatal ("%s: INSERT rc=%d %s",__FUNCTION__ , rc, rdb_error_string);
                }
            }
        }
    }
    return 0;
}

static int my_dump_clean(void *ptr){
	ptd=ptr;
    printf("%d,",ptd->ui32);

	return RDB_CB_OK;
}

static int my_dump_drop_2(void *ptr){
	ptd=ptr;

	if (ptd->ui32 == 2 ) {
        return RDB_CB_DELETE_NODE;
    }
    printf("%d,",ptd->ui32);
	return RDB_CB_OK;
}

/*static int my_dump_stop_at_5(void *ptr){
	ptd=ptr;
    printf("%d,",ptd->ui32);

	if (ptd->ui32 == 5) return RDB_CB_ABORT;
	return RDB_CB_OK;
}*/
static int my_dump_one_clean(void *ptr){
	one_ptd=ptr;
    //printf("*\n");
    printf("%d,",one_ptd->ui32); //, one_ptd->string);

	return RDB_CB_OK;
}

static int my_dump_one_drop_2(void *ptr){
	one_ptd=ptr;

	if (one_ptd->ui32 == 2 ) {
        //printf("-:%d,",one_ptd->ui32);
        return RDB_CB_DELETE_NODE;
    }
    printf("%d,",one_ptd->ui32);
	return RDB_CB_OK;
}

static int my_dump_one_stop_at_5(void *ptr){
	one_ptd=ptr;
    printf("%d,",one_ptd->ui32);

	if (one_ptd->ui32 == 5) return RDB_CB_ABORT;
	return RDB_CB_OK;
}

int main(int argc, char *argv[]) {

    int rc;
    int opt;
    int test=-1;
    int loops = 2;
    int dump = 0;

    while ((opt = getopt(argc, argv, "t:l:i:")) != -1) {
        switch (opt) {
        case 't':
            test = atoi(optarg);
            break;
        case 'l':
            loops = atoi(optarg);
            break;
        case 'i':
            dump = atoi(optarg);
            break;
        default:
            info("unknown argument: Usage rdb_test -t<n>\n"
                "Possibe tests:\n"
                "1) test init/close\n"
                "2) test init/define pools/close\n");
            break;
        }
    }

    // Getting things started...
    
    if (test == 1) {
        
     rdb_init();
        rdb_clean(0);

        // repeat twice to make sure we can re_init after close       
        rdb_init();

        rdb_clean(0);

        info("Ok");

    } else if (test == 2) {
        
        rdb_init();
        if (-1 == (rc = register_pools())) exit(-1);
        info ("%s\n",NULL == rdb_find_pool_by_name("test_pool") ? "Fail" : "Ok");
        rdb_clean(0);
        info ("%s\n",NULL == rdb_find_pool_by_name("test_pool") ? "Ok" : "Fail");

    } else if (test == 3) {
        
        // insert 16 records, print via index


        rdb_init();
        register_pools();
        if (-1 == add_test_data(pool1,loops))
            fatal ("FAIL"); //%s: INSERT rc=%d %s",__FUNCTION__ , rc, rdb_error_string);

        if (dump >= IDX_FIFO) {
            // FIFO/LIFO - dump will show address but that's dynamic so not much
            // value for uit tests... so we print index 0 data by order of dump
            while ((ptd = rdb_delete(pool1,dump,NULL)) != NULL) {
                printf("%s,",ptd->string);
            
            }
        } else rdb_dump(pool1,dump,",");

    } else if (test == 4) {
        
        // insert 256 records, print via index
        // Get record by multiple size constants

        rdb_init();
        register_pools();
        if (-1 == add_test_data(pool1,4))
            fatal("%s", rdb_error_string);
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,2, 3)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,4, 3)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,6, 3)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,8, 3)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,10, 3)) ? "FAIL" : ptd->string );

        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,2, 3L)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,4, 3L)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,6, 3L)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,8, 3L)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,10, 3L)) ? "FAIL" : ptd->string );
        
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,2, 3LL)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,4, 3LL)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,6, 3LL)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,8, 3LL)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,10, 3LL)) ? "FAIL" : ptd->string );
        
        
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,3,-3)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,5,-3)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,7,-3)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,9,-3)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,11,-3)) ? "FAIL" : ptd->string );

        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,3,-3L)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,5,-3L)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,7,-3L)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,9,-3L)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,11,-3L)) ? "FAIL" : ptd->string );
        
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,3,-3LL)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,5,-3LL)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,7,-3LL)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,9,-3LL)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,11,-3LL)) ? "FAIL" : ptd->string );
        

        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,2, 3U)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,4, 3U)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,6, 3U)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,8, 3U)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,10, 3U)) ? "FAIL" : ptd->string );

        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,2, 3LU)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,4, 3LU)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,6, 3LU)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,8, 3LU)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,10, 3LU)) ? "FAIL" : ptd->string );
        
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,2, 3LLU)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,4, 3LLU)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,6, 3LLU)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,8, 3LLU)) ? "FAIL" : ptd->string );
        info ("%s\n", NULL == (ptd=rdb_get_const(pool1,10, 3LLU)) ? "FAIL" : ptd->string );
        
        rdb_clean(0);

    } else if (test == 5) {
        int rc;

        dump = 0;

        int del_idx;
        void *before, *after, *ptr;
        test_data_t *tbefore, *tafter, *tptr;
    

        // This will test tree rotation on data insertion and deletion.
        // To avoid coming up with data, we use add_test_data() to populate pool1 and
        // selectively move records to pool2 (and back) in designed order to trigger the 
        // conditions we aim to test. NOTE: inserting and deleting a data record from a 
        // tree does not destroy the data record (when using rdb_delete() and rdb_insert(). 
        //
        // be ware, deleting records using an iterative function will also attempt to free data.
        // one can cancel this by supplying a dummy delete_fn()). 

        rdb_init();
        register_pools();
        pool4 = rdb_register_um_pool("test_pool_4", 
                            1, 
                            0, // offset of first index. usually it's zero
                            RDB_KSTR | RDB_KASC | RDB_BTREE,
                            NULL);
        if (pool4 == NULL) return -1;
        if (-1 == add_one_test_data(pool4,2))
            fatal ("FAIL"); //%s: INSERT rc=%d %s",__FUNCTION__ , rc, rdb_error_string);
        if (-1 == add_test_data(pool1,2))
            fatal ("FAIL"); //%s: INSERT rc=%d %s",__FUNCTION__ , rc, rdb_error_string);
//        if (-1 == add_test_data(pool3,2))
//            fatal ("FAIL"); //%s: INSERT rc=%d %s",__FUNCTION__ , rc, rdb_error_string);
        
        // here we have pool1 filled with 16 records (index 2 = [0-15]).
        
//        info ("%s\n", NULL == (ptd=rdb_delete_const(pool1,2, 4)) ? "FAIL" : ptd->string );
//        info ("%s\n", NULL == (ptd=rdb_delete_const(pool1,2, 4)) ? "FAIL" : ptd->string );
//        info ("%s\n", NULL == (ptd=rdb_delete_const(pool1,2, 9)) ? "FAIL" : ptd->string );
        rdb_iterate(pool4,dump,(void *) &my_dump_one_clean, NULL, NULL, NULL);
        info("%s\n",""); 
        rdb_iterate(pool4,dump,(void *) &my_dump_one_drop_2, NULL, NULL, NULL);
        info("%s\n","");
        rdb_iterate(pool4,dump,(void *) &my_dump_one_stop_at_5, NULL, NULL, NULL);
        info("%s\n","");

        // FIFO tests
        rdb_iterate(pool1,IDX_FIFO,(void *) &my_dump_clean, NULL, NULL, NULL);
        info("%s\n","");
        rdb_delete (pool1, IDX_FIFO, NULL);
        rdb_delete (pool1, IDX_LIFO, NULL);
        rdb_iterate(pool1,IDX_LIFO,(void *) &my_dump_clean, NULL, NULL, NULL);
        info("%s\n","");
        rdb_iterate(pool1,IDX_LIFO,(void *) &my_dump_drop_2, NULL, NULL, NULL);
        info("%s\n","");
        rdb_iterate(pool1,IDX_FIFO,(void *) &my_dump_clean, NULL, NULL, NULL);
        info("%s\n","");

        rdb_dump(pool1, 2, ",");
        info("%s\n","");

        // To test failed insert due to mixed index....
        // add '8' record to pool2
        rc = rdb_insert (pool2, rdb_get_const (pool1, 2, 8));
        printf("insert return = %d\n", rc);
        info ("dump pool2 :");
        rdb_iterate(pool2,1,(void *) &my_dump_clean, NULL, NULL, NULL);
        info ("\n");

        // delete first two idx's
        rdb_delete_one (pool2, 0, rdb_get_const (pool2, 2, 8));

        // try Adding same... 
        rc = rdb_insert (pool2, rdb_delete_const (pool1, 2, 8));
        printf("re-insert return = %d\n", rc);
        info ("dump pool2 :");
        rdb_iterate(pool2,0,(void *) &my_dump_clean, NULL, NULL, NULL);
        info ("\n");
       
        // here we re-insert the deleted index to 'fix' the damaged pool 
        rdb_insert_one (pool2, 0, rdb_get_const (pool2, 2, 8));
        info ("dump pool2 :");
        rdb_iterate(pool2,0,(void *) &my_dump_clean, NULL, NULL, NULL);
        info ("\n");
        // end of partial insert test case

        rdb_move_const (pool2, pool1, 2, 10);
        del_idx = 4;
        rdb_move (pool2, pool1, 2, &del_idx);
        del_idx = 12;
        rdb_delete(pool1,2,&del_idx);
        del_idx = 0;
        rdb_delete(pool1,2,&del_idx);

        del_idx = 1;
        if (NULL != rdb_get(pool1,2,&del_idx)) {
            info("Get OK\n");
        } else {
            info("Get Fail\n");
        }
        
        // this should fail
        del_idx = 12;
        if (NULL == rdb_get(pool1,2,&del_idx)) {
            info("NULL Get OK\n");
        } else {
            info("NULL Get Fail\n");
        }
    
        del_idx = 1;
        before = after = NULL;
        if (NULL != (ptr = rdb_get_neigh(pool1, 2, &del_idx, &before, &after)) && (NULL == before) && (NULL  == after)) {
            info("Neigh Get - hit OK\n");
        } else {
            info("Neigh Get - hit Fail %p %p %p\n",ptr ,before, after);
        }
        
        del_idx = 10;
        tbefore = tafter = NULL;
        if (NULL == (tptr = rdb_get_neigh(pool1, 2, &del_idx, (void **) &tbefore, (void **) &tafter)) && ((NULL != tbefore) || (NULL  != tafter))) {
            info("Neigh Get - miss OK\n");
            if (tbefore && tafter) {
                info ("search %d b %hhu a %hhu\n", del_idx, tbefore->ui8, tafter->ui8);
            }
            else if (tafter) {
                info ("search %d b --- a %hhu\n", del_idx, tafter->ui8);
            }
            else {
                info ("search %d b %hhu a ---\n", del_idx, tbefore->ui8);
            }
        } else {
            info("Neigh Get - miss Fail %p %p %p (hhu)\n",tptr ,tbefore, tafter);
            if (tafter) info ("search %d b hhu a %hhu\n", del_idx, /*tbefore->ui8*/ tafter->ui8);
        }
        
        if (NULL == (tptr = rdb_get_neigh(pool1, IDX_CUSTOM_2, &del_idx, (void **)  &tbefore, (void **) &tafter)) && ((NULL != tbefore) || (NULL  != tafter))) {
            info("Neigh Get - miss OK\n");
            if (tbefore && tafter) {
                info ("search %d b %hhu a %hhu\n", del_idx, tbefore->ui8, tafter->ui8);
            }
            else if (tafter) {
                info ("search %d b --- a %hhu\n", del_idx, tafter->ui8);
            }
            else {
                info ("search %d b %hhu a ---\n", del_idx, tbefore->ui8);
            }
        } else {
            info("Neigh Get - miss Fail %p %p %p (hhu)\n",tptr ,tbefore, tafter);
            if (tafter) info ("search %d b hhu a %hhu\n", del_idx, /*tbefore->ui8*/ tafter->ui8);
        }
        

        rdb_dump(pool1, 2, ",");
        info("%s\n","");
        rdb_dump(pool2, 2, ".");
        info("%s\n","");
        //rdb_dump(pool2, IDX_KPTR, ".");
        //info("%s\n","");
       
        // testing get_neight with Pointers 
        ptd = rdb_get_const (pool1, 2, 5);
        //info ("Address of %u if %p\n",ptd->ui32, ptd->string_ptr);
        void *get_ptr, *p2; // , *p1;
        test_data_t *b, *a, *n, *ptd2;
        b = a = n = NULL;
        get_ptr = ptd->string_ptr;
        //ptd = rdb_get_const (pool2, IDX_KPTR, get_ptr);
        ptd2 = rdb_get_const (pool1, 2, 11);
        p2 = ptd->string_ptr;
        //*p2 = p1;
        //ptd2->string_ptr ++;
        ptd = rdb_get (pool1, IDX_KPTR, &ptd2->string_ptr);
        //info ("Address of %u if %p\n",ptd->ui32, ptd->string_ptr);
        //p2 = NULL;
        //info ("P2=%p\n", p2);

        b = a = NULL;
        ptd = rdb_get_neigh (pool1, IDX_KPTR,&p2, (void **) &b, (void **) &a);
        if (a && b) {
            info ("Before/After %u/%u\n",b->ui32, a->ui32);
        }
        else if (b) {
            info ("Before/After %u/-\n",b->ui32);
        }
        else if (a) {
            info ("Before/After -/%u\n", a->ui32);
        }
            //info ("Address of %u if %p\n",a->ui32, a->string_ptr);
       // }
        else if (ptd) {
            info ("Match %u\n", ptd->ui32);
            //info ("ptdAddress of %u if %p\n",ptd->ui32, ptd->string_ptr);
        }
      
        info ("Between test "); 
        b = a = NULL;
        p2++;
        ptd = rdb_get_neigh (pool1, IDX_KPTR,&p2, (void **) &b, (void **) &a);
        if (a && b) {
            info ("Before/After %u/%u\n",b->ui32, a->ui32);
        }
        else if (b) {
            info ("Before/After %u/-\n",b->ui32);
        }
        else if (a) {
            info ("Before/After -/%u\n", a->ui32);
        }
        else if (ptd) {
            info ("Match %u\n", ptd->ui32);
        }
        
        info ("Null test "); 
        b = a = NULL;
        p2 = NULL;
        ptd = rdb_get_neigh (pool1, IDX_KPTR,&p2, (void **) &b, (void **) &a);
        if (a && b) {
            info ("Before/After %u/%u\n",b->ui32, a->ui32);
        }
        else if (b) {
            info ("Before/After %u/-\n",b->ui32);
        }
        else if (a) {
            info ("Before/After -/%u\n", a->ui32);
        }
        else if (ptd) {
            info ("Match %u\n", ptd->ui32);
        }




        //info("%s\n","Empty Dump Start");
        rdb_flush(pool1,NULL,NULL);
        rdb_dump(pool1, 2, ",");
        //info("%s\n","Empty Dump End - there should be nothing between START and END");
        info("%s\n","");

        info("lock %d\n", rdb_lock(pool1,__FUNCTION__));
        rdb_unlock(pool1,__FUNCTION__);
        info("unlock\n");

    } else if (test == 6) {

        // test various utility functione

        int rc;

        rdb_init();
        register_pools();
        
        rc = rdb_error_value(-1,RDB_TEST_STRING);
        if (rc != -1 || (strcmp (rdb_error_string, RDB_TEST_STRING) != 0)) {
            fatal ("FAIL");
        } 

        rdb_error(RDB_TEST_STRING);
        if (strcmp (rdb_error_string, RDB_TEST_STRING) != 0) {
            fatal ("FAIL");

        }

    
        pool2 = rdb_register_um_pool("copy_pool", 
                            TEST_INDEXES, 
                            0, // offset if first index. usually it's zero
                            RDB_KCF | RDB_KASC | RDB_BTREE,
                            NULL);
        if (pool2 == NULL) info("%s\n", rdb_error_string);
        
        pool2 = rdb_register_um_pool("null_custom_pool", 
                            TEST_INDEXES, 
                            0, // offset if first index. usually it's zero
                            RDB_KCF | RDB_KASC | RDB_BTREE,
                            NULL);
        if (pool2 == NULL) info("%s\n", rdb_error_string);
     
        if (rdb_register_um_idx(pool1,
                            0, 
                            (void *) &td.string_ptr- (void *) &td.string,
                            RDB_KPSTR | RDB_KASC | RDB_BTREE,
                            NULL) == -1) {
            info("%s\n", rdb_error_string);
        }
        
        if (rdb_register_um_idx(pool1,
                            RDB_POOL_MAX_IDX, 
                            (void *) &td.string_ptr- (void *) &td.string,
                            RDB_KPSTR | RDB_KASC | RDB_BTREE,
                            NULL) < 0) {
            info("%s\n", rdb_error_string);
        }
        
        if (rdb_register_um_idx(pool1,
                            1, 
                            (void *) &td.string_ptr- (void *) &td.string,
                            RDB_KPSTR | RDB_KASC | RDB_BTREE,
                            NULL) < 0) {
            info("%s\n", rdb_error_string);
        }

        if (rdb_register_um_idx(pool1,
                            RDB_POOL_MAX_IDX -1, 
                            0,
                            /*RDB_KPSTR | */  RDB_KASC | RDB_BTREE,
                            NULL) < 0) {
            info("%s\n", rdb_error_string);
        }
        
        // ASC/DEC not yet implemented
        /*if (rdb_register_um_idx(pool1,
                            RDB_POOL_MAX_IDX -1, 
                            0,
                            RDB_KPSTR | RDB_BTREE,
                            NULL) < 0) {
            info("%s\n", rdb_error_string);
        }*/

        info("Ok\n");

    }







/*
    simple_demo(pool);
    
    um_multi_record_insert_demo(pool);
    

    // Uncommect Below to see the whole (long) output
    //
    //  Dump ordered by name
    // rdb_dump(pool,0);
    //
    //  Dump ordered by address, age and value...
    // rdb_dump(pool,1);
    // rdb_dump(pool,2);
    // rdb_dump(pool,3);

    // Get and Print some records  
    pdd=rdb_get(pool,0,"AAAA");
     if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    pdd=rdb_get(pool,0,"AAAB");
     if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    pdd=rdb_get(pool,0,"BAAA");
     if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    pdd=rdb_get(pool,0,"BAAB");
     if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
     pdd=rdb_get(pool,1,"dead");
     if (pdd!=NULL) {
        info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    }
     else info ("filed to find record with value 'dead' in index 1");
    
    // This is one way to delete a record. Please note rdb_delete only unlinka
    // data from the tree and return a pointer to it. it DOES NOT free any
    // allocated momory. it's user responsibility to free the pointer(s) or
    // re-link them to a tree.
    
    info ("Now we delete a record, and repeat the get attempt");
    
    pdd=rdb_delete(pool,0,"BAAB");
     if (pdd!=NULL) info("deleted: %s %s %d %ld",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    pdd=rdb_get(pool,0,"BAAB");
     if (pdd!=NULL) {
        info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    } else info ("get BAAB failed");
    
    
    // This shows the common way to search for data ... 
    // one use rdb_get with a pointer to a variable containing what is 
    // search for.
    int find_me = 500;   
     pdd=rdb_get(pool,3,&find_me);
     if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
     
    // But, at times we need a constant value. This call will allow that.
    // (But for real numbers only, obviously).
    pdd=rdb_get_const(pool,3,501);
     if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
   
    // Below is a violation (string is refferance by ponter), compiler will
    // complain, rightfully, but it will work. One shuld use rdb_get() 
    // for strings, as done above 
    //
    // pdd=rdb_get_const(pool,1,"abcd");
     // if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,pdd->age,pdd->long_value);
    
    // This will iterate the entire data pool, calling myDump on each record
    // BUT, return code from the call can intervene. in this case, it will
    // abort the cycle after printing the record where at age == 3    
    // Try changing the index we use to iterate on . what happened?
    rdb_iterate(pool,2,(void *) &myDump, NULL, NULL, NULL);
    
    // free all data in the tree
    rdb_flush(pool, NULL, NULL);

    //Clean out rdb leftovers (like pools data), free all resources.
    rdb_clean(0);
*/
    exit(0);
}


