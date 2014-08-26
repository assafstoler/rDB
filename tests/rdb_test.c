#include <stdio.h>  //printf,
#include <stdlib.h> //exit,
#include <string.h>
#include <math.h>   // pow
#include <unistd.h> // getopt
#include "rDB.h"

#define fatal(b,arg...) do {     \
        fprintf(stderr,b,##arg); \
        fprintf(stderr,"\n");    \
        fflush(stdout);          \
        exit(-1);                \
        } while (0);

#define error(b,arg...) do {     \
        fprintf(stderr,b,##arg); \
        fprintf(stderr,"\n");    \
        fflush(stdout);          \
        } while (0);

#define info(b,arg...) do {     \
        fprintf(stdout,b,##arg); \
        fprintf(stdout,"\n");    \
        fflush(stdout);          \
        } while (0);

// On how many indexes we would refferance our demo data.
// the use of define here is not required and done for conveniance only

#define TEST_INDEXES 13
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
#define IDX_CUSTOM  12
//#defnie IDX_UI256
//#defnie IDX_I256
//

typedef struct test_data_s {

    // This is the rDB pointer pack (times # of indexes for this structure) 
    // It is required at to be at the top of a structure to be used with rDB.
	rdb_bpp_t	pp[TEST_INDEXES]; 
    // Below is user data
    // Strait indexes.
	char 	    string[5];
	char 	    *string_ptr;
    uint8_t     ui8;
    int8_t      i8;
    uint16_t    ui16;
    int16_t     i16;
    uint32_t    ui32;
    int32_t     i32;
    uint64_t    ui64;
    int64_t     i64;
    __uint128_t ui128;
    __int128_t  i128;
    // Soon, people, Soon...
    //type_u256   ui256;
    //type_256    i256;
    // Used defined index with custon compare fn.
    // for test we will use 2 fields
    int32_t     ud1;
    char        ud2[5];
    char        *ud3;
} test_data_t;

// We define a data set, and a data set pointer that we shall use later on
test_data_t td,
            *ptd;

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

#ifdef __out
// Insert one record, and dump it to stdout, then flush tree
int simple_demo(rdb_pool_t *pool) {

    pdd=malloc(sizeof(demo_data_t));
    if (!pdd) fatal("fatal: failed allocating data node");
	info("main addr %p", pdd);	

    // We allocate the address pointer so it can point to a string
	pdd->address_ptr = malloc(64);
    if (!pdd->address_ptr) fatal("fatal: failed allocating address_pointer");
    info ("address ptr = %p", pdd->address_ptr);

	strcpy(pdd->name,"Assaf Stoler");
	strcpy(pdd->address_ptr,"rdb@assafstoler.com");
	pdd->age=43;
	pdd->long_value=100000;

    // Data node is added to the tree, and Indexed.
 	if (rdb_insert(pool,pdd) != INDEXES) 
        error("one or more index failed insertion");

 	info("We will now dump the tree, containing one record");
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
					if (pdd==NULL) fatal("Allocation error in %s",__FUNCTION__);
					pdd->address_ptr = calloc(1,16);
					if (pdd->address_ptr == NULL) fatal("Allocation error in %s",__FUNCTION__);
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
    info ("we Just completed %.0f insertions (%.0f records  * 4 indexes)", pow(LOOPS,4)*4, pow(LOOPS,4));

    return 0;
}
#endif 

// Index numbers can change when / if we add or remove indexes.
// It's a good programing disciplan to define a name to the indexes to
// avoid unexpected behaviour when that happens

int register_pools() {

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
                            IDX_CUSTOM, 
                            (void *) &td.ud1 - (void *) &td.string,
                            RDB_KCF | RDB_KASC | RDB_BTREE,
                            compare_custom_index) == -1) return -1;

    return 0;
}

int add_test_data (rdb_pool_t *pool, int loops) {
	int rc,a,b,c,d;
	
    for (a=0; a<loops; a++) {
		for (b=0; b<loops; b++) {
			for (c=0; c<loops; c++) {
				for (d=0; d<loops; d++) {
					ptd=malloc(sizeof(test_data_t));
					if (ptd==NULL) fatal("Allocation error in %s",__FUNCTION__);
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
                    // We could have calculated the various LOOPS powers out
                    // of this loop, to increase perfoemance, but not an issue
                    // for this demo
					ptd->ui128 = d + (c * loops) + (b * pow(loops, 2)) + 
                                                        (a * pow(loops, 3)) ;
                    ptd->i128 = ptd->ui128 * -1;
                    
                    ptd->ui64 = ptd->ui128;
                    ptd->i64 = ptd->i128;
                    
                    ptd->ui32 = ptd->ui128;
                    ptd->i32 = ptd->i128;
                    
                    ptd->ui16 = ptd->ui128;
                    ptd->i16 = ptd->i128;

                    ptd->ui8 = ptd->ui128;
                    ptd->i8 = ptd->i128;
                
                    // those two will make a unique index together...
                    // udq will repeat itself, but combined with ud2 no issue
                    ptd->ud1 = ptd->ui8;
                    memcpy(ptd->ud2, ptd->string,5);

					rc=rdb_insert(pool,ptd);
                    // We check that rDB was able to link all indexes. rDB will
                    // simply skip indexes it can not link-in (due to 
                    // duplicates, for example)
					if (rc!=TEST_INDEXES) return -1; 
                        //fatal ("%s: INSERT rc=%d %s",__FUNCTION__ , rc, rdb_error_string);
				}
			}
		}
	}
    return 0;
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
        rdb_clean();

        // repeat twice to make sure we can re_init after close    	
        rdb_init();
        rdb_clean();

        info("Ok");

    } else if (test == 2) {
        
        rdb_init();
        if (-1 == (rc = register_pools())) exit(-1);
        info ("%s",NULL == rdb_find_pool_by_name("test_pool") ? "Fail" : "Ok");
        rdb_clean();
        info ("%s",NULL == rdb_find_pool_by_name("test_pool") ? "Ok" : "Fail");

    } else if (test == 3) {
        
        // insert 16 records, print via index

        rdb_init();
        register_pools();
        add_test_data(pool1,loops);
        rdb_dump(pool1,dump,",");
    }

    exit(0);






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
    rdb_clean();
*/
    exit(0);
}

