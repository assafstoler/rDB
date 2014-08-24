#include <stdio.h>  //printf,
#include <stdlib.h> //exit,
#include <string.h>
#include <math.h>   // pow
#include "rDB.h"

#define PP_T  rdb_bpp_t

// ow many Indexes we would like to keep on out demo data.
// the use of define here is not required and done for conveniance only
#define INDEXES 4 

typedef struct DEMO_DATA {
    // this is the rDB pointer pack (times # of indexes for this structure) 
	PP_T	pp[INDEXES]; 
    // below is user data
	char 	    name[64];
	char 	    *address_ptr;
	uint32_t    age;
	int64_t	    long_value;
} demo_data_t;

demo_data_t dd,
            *pdd;
// This os the handle which is used to access and identify a data pool
rdb_pool_t *pool;


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


static int myDump(void *ptr){
	pdd=ptr;
	printf("MyDump: %s %s %d %ld\n",
        pdd->name,
        pdd->address_ptr,
        pdd->age,
        pdd->long_value);
	if (pdd->age==3) return RDB_CB_ABORT;
	//if (pdd->value>=20000 &&  pdd->value < 30000) return RDB_CB_DELETE_NODE;
	return RDB_CB_OK;
}

// Insert one record, and 'dump' it to stdout, then flush tree
int simple_demo(rdb_pool_t *pool) {

    pdd=malloc(sizeof(demo_data_t));
    if (!pdd) fatal("fatal: failed allocating data node");
	info("main addr %p", pdd);	

    // we allocate the address pointer so it can point to a string
	pdd->address_ptr = malloc(64);
    if (!pdd->address_ptr) fatal("fatal: failed allocating address_pointer");
    info ("address ptr = %p", pdd->address_ptr);

	strcpy(pdd->name,"Assaf Stoler");
	strcpy(pdd->address_ptr,"rdb@assafstoler.com");
	pdd->age=43;
	pdd->long_value=100000;

    // data node is added to the tree, and Indexed.
 	if (rdb_insert(pool,pdd) != INDEXES) 
        error("one or more index failed insertion");

 	info("We will now dump the tree, containing one record");
    rdb_dump(pool,1);

    //rDB is smart enought to know it needs to free address_ptr before it free
    //the actual data node. (since it's an indexed field) thus helping avoid
    //memory leaks
    rdb_flush(pool, NULL, NULL);

    // the pool is now empty again... ready for out next demo
    // as such, the dump below will produce no data
    rdb_dump(pool,0);

    return 0;
}

#define LOOPS 18

// here we will insert 104976 records to the tree, with 4 indexes each
int um_multi_record_insert_demo(rdb_pool_t *pool) {
	int rc,a,b,c,d;
	int round=0;
	
    for (a=0; a<LOOPS; a++) {
		for (b=0; b<LOOPS; b++) {
			for (c=0; c<LOOPS; c++) {
				for (d=0; d<LOOPS; d++) {
					pdd=malloc(sizeof(demo_data_t));
					if (pdd==NULL) fatal("Allocation error in %s",__FUNCTION__);
					pdd->address_ptr = calloc(1,16);
					if (pdd->address_ptr == NULL) fatal("Allocation error in %s",__FUNCTION__);
//					strcpy(pdd->name,"nnnn");
//					strcpy(pdd->string,"+nnnn+");
//					strcpy(pdd->pstring,"-nnnn-");
					pdd->name[0]='A' + d;
					pdd->name[1]='A' + c;
					pdd->name[2]='A' + b;
					pdd->name[3]='A' + a;
					pdd->name[4]=0;
					*(pdd->address_ptr)='a' + d;
                    *(pdd->address_ptr+1)='a' + c;
                    *(pdd->address_ptr+2)='a' + b;
					*(pdd->address_ptr+3)='a' + a;
					pdd->age= d + (c * LOOPS) + (b * pow(LOOPS, 2)) + (a * pow(LOOPS, 3)) ;
					pdd->long_value=pow(LOOPS, 4) - pdd->age;
					rc=rdb_insert(pool,pdd);
					if (rc!=4) fatal ("%s: INSERT rc=%d (round %d) %s",__FUNCTION__ ,rc ,round, rdb_error_string);
				}
			}
		}
	}
    info ("we Just completed 400K insertions (100K records  * 4 indexes)");

    return 0;
}

int main(int argc, char *argv[]) {

//	int rc,cnt,a,b,c,d;
//	int round=0;

	printf("rDB DEMO Client\n");
	rdb_init();
//	poolId1=rdbRegisterPool("demoData", INDEXES, 4, RDB_KSTR | RDB_KASC | RDB_BTREE);
	pool = rdb_register_um_pool("demoData", 
                            INDEXES, 
                            0, //(void *) &dd.age - (void *) dd.name,
                            RDB_KSTR | RDB_KASC | RDB_BTREE,
                            NULL);
    if (pool == NULL) fatal("pool registration failed");

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


//	poolId2=rdbRegisterPool("demoData2",  1, 12, RDB_KSTR | RDB_KASC | RDB_BTREE);
//	poolId3=rdbRegisterPool("demoData3",  1, 24, RDB_KINT | RDB_KASC | RDB_BTREE);
//	poolId4=rdbRegisterPool("demoData4",  1, 28, RDB_KLONG | RDB_KASC | RDB_BTREE);


    simple_demo(pool);
    
    um_multi_record_insert_demo(pool);

    // dump ordered by name
    //rdb_dump(pool,0);
    // dump ordered by address, age and value
    //rdb_dump(pool,1);
    //rdb_dump(pool,2);
    //rdb_dump(pool,3);
	
    pdd=rdb_get(pool,0,"AAAA");
	if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,pdd->age,pdd->long_value);
    pdd=rdb_get(pool,0,"AAAB");
	if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,pdd->age,pdd->long_value);
    pdd=rdb_get(pool,0,"BAAA");
	if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,pdd->age,pdd->long_value);
    pdd=rdb_get(pool,0,"BAAB");
	if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,pdd->age,pdd->long_value);
	pdd=rdb_get(pool,1,"dead");
	if (pdd!=NULL) {
        info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,pdd->age,pdd->long_value);
    }
     else info ("filed to find record with value 'dead' in index 1");
    
    int xxx=500;	
	pdd=rdb_get(pool,3,&xxx);
	if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,pdd->age,pdd->long_value);
	
    //pdd=rdb_get(pool,3,501);
	//if (pdd!=NULL) info("get: %s %s %d %ld",pdd->name, pdd->address_ptr,pdd->age,pdd->long_value);

    // this will iterate the entire data pool, calling myDump on each record
    // BUT, return code from the call can intervene. in this case, it will
    // about the cycle after print record where age==3	
    // try changing the index we use to iterate on . what happened?
    rdb_iterate(pool,2,(void *) &myDump, NULL, NULL, NULL);

    exit(0);
}


