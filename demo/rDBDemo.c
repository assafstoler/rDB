#include <stdio.h>  //printf,
#include <stdlib.h> //exit,
#include <string.h>
#include <math.h>   // pow
#include <inttypes.h>
#include <pthread.h>
#include <time.h>
#include "rDB.h"

// On how many indexes we would refferance our demo data.
// the use of define here is not required and done for conveniance only
#define INDEXES 4 

typedef struct DEMO_DATA {
    // This is the rDB pointer pack (times # of indexes for this structure) 
    // It is required at to be at the top of a structure to be used with rDB.
	rdb_bpp_t	pp[INDEXES]; 
    // Below is user data
	char 	    name[64];
	char 	    *address_ptr;
	uint32_t    age;
	int64_t	    long_value;
    char        *some_other_user_data;
} demo_data_t;

// We define a data set, and a data set pointer that we shall use later on
demo_data_t dd,
            *pdd;

// This os the handle which is used to access and identify a data pool
// Each data pool will have one.
rdb_pool_t *pool;

// This is a callback function. rDB will call a callback function when 
// iterating over a data pool, once for each record.
// This is one one perform custom operations on a big data set
// In this case we just use it to dump the data to console.
// ... and abord after a few records ... 
static int myDump(void *ptr){
	pdd=ptr;
	printf("MyDump: %s %s %d %" PRId64 "\n",
        pdd->name,
        pdd->address_ptr,
        pdd->age,
        pdd->long_value);
	if (pdd->age==3) return RDB_CB_ABORT;
	//if (pdd->value>=20000 &&  pdd->value < 30000) return RDB_CB_DELETE_NODE;
	return RDB_CB_OK;
}

// Insert one record, and dump it to stdout, then flush tree
int simple_demo(rdb_pool_t *pool) {

    pdd=malloc(sizeof(demo_data_t));
    if (!pdd) fatal("fatal: failed allocating data node\n");
	info("main addr %p\n", pdd);	

    // We allocate the address pointer so it can point to a string
	pdd->address_ptr = malloc(64);
    if (!pdd->address_ptr) fatal("fatal: failed allocating address_pointer\n");
    info ("address ptr = %p\n", pdd->address_ptr);

	strcpy(pdd->name,"Assaf Stoler");
	strcpy(pdd->address_ptr,"rdb@assafstoler.com");
	pdd->age=43;
	pdd->long_value=100000;

    // Data node is added to the tree, and Indexed.
 	if (rdb_insert(pool,pdd) != INDEXES) 
        error("one or more index failed insertion\n");

 	info("We will now dump the tree, containing one record\n");
    rdb_dump(pool,1,"\n");

    // rDB is smart enought to know it needs to free address_ptr before it free
    // the actual data node. (since it's an indexed field) thus helping avoid
    // memory leaks. if we had non-indexed allocated data in out structure, we
    // would have had to use an at-delete-time callback fn which would have 
    // freed that data. since we dont, we just use NULL's
    rdb_flush(pool, NULL, NULL);

    // The pool is now empty again... ready for our next demo
    // As such, the dump below will produce no data
    rdb_dump(pool,0,"\n");

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
					if (rc!=INDEXES) fatal ("%s: INSERT rc=%d %s\n",__FUNCTION__ , rc, rdb_error_string);
				}
			}
		}
	}
    info ("we Just completed %.0f insertions (%.0f records  * 4 indexes)\n", pow(LOOPS,4)*4, pow(LOOPS,4));

    return 0;
}

int main(int argc, char *argv[]) {

	printf("rDB DEMO Client\n");
    
    // Getting things started...
	rdb_init();

    // Register the data pool. (_um for un-managed pool, which is it for now)
    // Note that the first index is also registered in this call.
	pool = rdb_register_um_pool("demoData", 
                            INDEXES, 
                            0, //(void *) &dd.age - (void *) dd.name,
                            RDB_KSTR | RDB_KASC | RDB_BTREE,
                            NULL);
    if (pool == NULL) fatal("pool registration failed\n");

    // Registering the 3 other indexes for out data structure.
	if (rdb_register_um_idx(pool,
                            1, 
                            (void *) &dd.address_ptr- (void *) &dd.name,
                            RDB_KPSTR | RDB_KASC | RDB_BTREE,
                            NULL) == -1) fatal("Failed to register index 1\n");
	
    if (rdb_register_um_idx(pool,
                            2, 
                            (void *) &dd.age - (void *) &dd.name,
                            RDB_KUINT32 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) fatal("Failed to register index 2\n");

    if (rdb_register_um_idx(pool,
                            3, 
                            (void *) &dd.long_value - (void *) &dd.name,
                            RDB_KINT64 | RDB_KASC | RDB_BTREE,
                            NULL) == -1) fatal("Failed to register index 3\n");


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
	if (pdd!=NULL) info("get: %s %s %d %" PRId64 "\n",pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    pdd=rdb_get(pool,0,"AAAB");
	if (pdd!=NULL) info("get: %s %s %d %" PRId64 "\n", pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    pdd=rdb_get(pool,0,"BAAA");
	if (pdd!=NULL) info("get: %s %s %d %" PRId64 "\n", pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    pdd=rdb_get(pool,0,"BAAB");
	if (pdd!=NULL) info("get: %s %s %d %" PRId64 "\n", pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
	pdd=rdb_get(pool,1,"dead");
	if (pdd!=NULL) {
        info("get: %s %s %d %" PRId64 "\n", pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    }
     else info ("filed to find record with value 'dead' in index 1\n");
    
    // This is one way to delete a record. Please note rdb_delete only unlinka
    // data from the tree and return a pointer to it. it DOES NOT free any
    // allocated momory. it's user responsibility to free the pointer(s) or
    // re-link them to a tree.
    
    info ("Now we delete a record, and repeat the get attempt\n");
    
    pdd=rdb_delete(pool,0,"BAAB");
	if (pdd!=NULL) info("deleted: %s %s %d %" PRId64 "\n", pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    pdd=rdb_get(pool,0,"BAAB");
	if (pdd!=NULL) {
        info("get: %s %s %d %" PRId64 "\n", pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
    } else info ("get BAAB failed\n");
    
    
    // This shows the common way to search for data ... 
    // one use rdb_get with a pointer to a variable containing what is 
    // search for.
    int find_me = 500;	
	pdd=rdb_get(pool,3,&find_me);
	if (pdd!=NULL) info("get: %s %s %d %" PRId64 "\n", pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
	
    // But, at times we need a constant value. This call will allow that.
    // (But for real numbers only, obviously).
    pdd=rdb_get_const(pool,3,501);
	if (pdd!=NULL) info("get: %s %s %d %" PRId64 "\n", pdd->name, pdd->address_ptr,
            pdd->age, pdd->long_value);
   
    // Below is a violation (string is refferance by ponter), compiler will
    // complain, rightfully, but it will work. One shuld use rdb_get() 
    // for strings, as done above 
    //
    // pdd=rdb_get_const(pool,1,"abcd");
	// if (pdd!=NULL) info("get: %s %s %d %ld\n",pdd->name, pdd->address_ptr,pdd->age,pdd->long_value);
    
    // This will iterate the entire data pool, calling myDump on each record
    // BUT, return code from the call can intervene. in this case, it will
    // abort the cycle after printing the record where at age == 3	
    // Try changing the index we use to iterate on . what happened?
    rdb_iterate(pool,2,(void *) &myDump, NULL, NULL, NULL);
    
    // free all data in the tree
    rdb_flush(pool, NULL, NULL);

    //Clean out rdb leftovers (like pools data), free all resources.
    rdb_clean(0);

    exit(0);
}


