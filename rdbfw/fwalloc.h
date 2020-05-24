//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

#ifndef __FWALLOC__H__
#define __FWALLOC__H__

#define MAX_RDBFW_ALLOC_SZ ( 1048576 * 16 )
#define MAX_RDBFW_SUPERALLOC_SZ ( 1048576 * 64 ) // (max) 64M per allocation
//#define MAX_RDBFW_SUPERALLOC_SZ 120 // (max) 1M per allocation

/* rdbfw-alloc data explained:
 *
 * An alloc_sz record will exist for any requested / preallocated rdbfw-alloc pool/size.
 * each allocaed 'superblock' is stored in the superblocks pool
 * 
 *
 * */

typedef struct {
    rdb_bpp_t               pp[1];
    uint32_t block_sz;
    uint32_t blocks_in_use;
    uint32_t max_blocks_allowed;
    uint32_t blocks_free;
    uint32_t sb_sz; // superblock size
    uint32_t sb_gsz; // superblock gross size (header included)
    uint32_t sb_cnt; // how many allocated superclocks we have
    uint32_t sb_item_cnt; // how many blocks per superblock
    rdb_pool_t *free_pool;
    rdb_pool_t *superblock_pool;
} alloc_master_t;

typedef struct {
    rdb_bpp_t               pp[1];
    uint32_t block_sz;
    alloc_master_t *am; //
    void *data_addr;
    void *data; // have to be last. only add stuff above
} superblock_t;

typedef struct {
    rdb_bpp_t               pp[1];
    uint32_t block_sz;
    alloc_master_t *am; //
    size_t data_start;
    size_t data_end;
} superblock_all_t;

typedef struct {
    rdb_bpp_t               pp[1];
    superblock_t *sb;
    int ref_ct;
    // MUST
    // be
    // last!!!
    void *data_addr;
    void *data;
} block_t;

typedef struct {
    superblock_t **sb;
    void **data_addr;
} block_key_t;

typedef struct {
    rdb_bpp_t               pp[1];
    uint32_t block_sz;
    uint32_t alloc_ct;
} legacy_alloc_t;

int rdbfw_alloc_init(void);

int rdbfw_alloc_prealloc (uint32_t sz, uint32_t count, uint32_t max_count, int hint_sb_sz) ;
void *rdbfw_alloc_no_emit(uint32_t size);
void *rdbfw_alloc(uint32_t size);
void rdbfw_free(void *);
void rdb_free_prealloc(void);
int rdbfw_up_ref ( void *ptr, int count );

/*
 * count = 3, sz = 4
 * max = 10
 * sb_sz=12
 * sb_cnt = 1 + 1 = 2
 * sb_sz = 12 / 2 ==6
 *
 *
 * count = 3, sz = 5
 * max = 10
 * sb_sz=15
 * sb_cnt = 1 + 1 = 2
 * sb_sz = 15 / 2 + 1 == 8 // place for 16 items*/


#endif
