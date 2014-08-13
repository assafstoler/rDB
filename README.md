rDB
===

rDB or RAM DataBase is a library to easily manage and manipulate program data with strong focus on performance and ease of use, both in kernel and user space

I'm not set on the chosen licensing and will try to accomodate users needs they arise.

while new on GitHub, this code, in one form or another, have been use in high speed, big-data commercial applications for several years and proven to be mature and stable (Both in-kernel and userspace).

rDB is coded in 'C' and intended to be used under Linux. however, I see no issues porting it to other OS's

What will rDB give you? got a data structure ? want to treat it like a database with easy ability to insert, get, delete, and iterate records. read on.

In rDB, data is internally stored in one of the following:

* Balanced binary trees (multiple index supported)
* Linked Lists          (multiple index supported)
* FIFO                  (non-indexed)
* LIFO                  (non-indexed)
* skip-lists			(not yet implemented)

rdDB support natively all standard data types to be used as indexes, Including numericals, strings, pointers to strings, and custom-user indexed that can collect multiple data types and fields into one index. ie, the fields holding first name, middiel initial, and last name, can be defined together as one index.

Data pools:

A collection of data records in the same format (struture) is called a data pool. the data-managment architecture is hidden form the user and all data types use the same interace.

There is no limit on the number of data pools (trees, list, etc) one chose to impiment. each pool is limited to 16 indexes by default (configurable).

 
rDB offers two approches to managng your structures and memory (data pools):

1) Unmanaged data pools: 
   You allocate, (and at times, free, see below) your memory. rDB only index and manage it foryou. perfect for variable size data set or with complex / multi-dimentional pointers. this is the default.
   Unmanaged data pools require you include one or more rDB 'pointer packs' at the top of your structure. one for each index to be used.

2) Managed data pools:
   rDB takes care of allocating new records, and frees you from the need to introduce the rDB pointer pack into your data structure. it also speeds up data processing by avoiding most of the calls to 'malloc' and 'free' and instead running it's own internal memory managment, garbage collections, and so forth.
   (at the moment managed trees are not yet public, as not all of the supporting functions are complete. I do expect it to be pushed out shortly)

Note on freeing memory:

Some (most) rdb functions that can delete records will do the freeing for you, if you supply it with the correct data.
Others will only unlink the data record fromt he internal tree's ot lists, and return you a pointer to the data. it's your responsibility to free the data at that time. see each fn() documentation to know which apply.


Quick Start - un-managed (um) data pools (Please see demo / doc folders for a more in-depth knowledge)

1. define your data.
```
typedef struct my_data_s {
    rdb_pp_t[2]     pp; // Required for rDB unmanaged tree with two indexes
    char            name[64];
    char            ssn[12];
    int             age;
    char            passwd[32];
} user_data_t;

user_data_t ud_tmp; // only used to calculate index offset

2. init the rDB library
rdb_init();

3. Registrer a data pool(s)
rdb_pool_t *my_pool;
my_pool = rdb_register_um_pool(
	"clients", 		// name of data pool. 
	2,  			// index count. we want 2
	0, 				// offset of the first key. since we use name
					// as our first key, offset is zero. 
					// offset is form end of rdb_pp_t.
	RDB_KSTR |		// Key is a string
	RDB_KASC | 		// Acending sort order
	RDB_BTREE,		// store this index as a balanced binary tree
	NULL); 			// we need no custom compare fn as RDB knows 
					// how to compare strings

rdb_register_um_index(
	my_pool, 		// returned from the register pool call - the pool handle
	1,				// the index we are defining (zero based. index zero was 
					// defined in the register pool call.
	&ud_tmp.ssn - &ud_tmp.name, // the byte offset of the index
	RDB_KSTR,		// Key is a string
	NULL);


4. allocate and store some data
user_data_t *ud;
while (we have data to add) { 	// one really need to chage the data ...
	ud = malloc(sizeof(user_data_t);
	sprintf (ud.name, "john doe");
	sprintf (ud.phone, "123-456-5555");
	ud.age=99;
	sprintf (ud.name, "123456"); 

	rdb_insert(my_pool, ud);
}

5. lookup data by index

ud = rdb_get (
	my_pool, 		// pool to look in
	1,				// which index to use
	"121-12-121");	// data to look for

6 print all records

rdb_iterate(
	my_pool, 		//pool to iterate
	0,				// which index to use while walking the data pool
	my_print_fn,	// pointer to out callback function. will be called for every 
					// node (data record) in the data pool.
	NULL,			// optional pointer to deliver to the custom callback fn()
	NULL,			// optional delete fn. use in case additionla work needs to 
					// be accomplishe before a record is freed
	NULL);			// optional pointer to deliver to delete fn()

you will need to create the callback fn().

int my_print_fn(void *data, void *user_ptr){
	printf("name: %s",(user_data_t *)data->name);
	return RDB_CB_OK;
}
```
