/* Registration rejects invalid layouts before changing pool state. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdb.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

typedef struct {
    rdb_bpp_t links[2];
    int32_t key;
} test_record_t;

static int compare_keys(void *left, void *right)
{
    int32_t a = *(int32_t *)left;
    int32_t b = *(int32_t *)right;
    return (b > a) - (b < a);
}

typedef rdb_pool_t *(*create_pool_fn)(char *, int, int, int, void *, size_t);

static const create_pool_fn create_pool[] = {
    rdb_register_um_pool,
    rdb_add_pool
};

static void test_index_counts(void)
{
    const int counts[] = {0, -1, RDB_POOL_MAX_IDX + 1};

    for (size_t api = 0; api < sizeof(create_pool) / sizeof(create_pool[0]); ++api) {
        for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
            char name[32];
            snprintf(name, sizeof(name), "bad-count-%zu-%zu", api, i);

            CHECK(create_pool[api](name, counts[i], 0,
                        RDB_BTREE | RDB_KINT32, NULL, 0) == NULL);
            CHECK(rdb_find_pool_by_name(name) == NULL);

            rdb_pool_t *retry = rdb_register_um_pool(name, 1, 0,
                    RDB_BTREE | RDB_KFIFO, NULL, 0);
            CHECK(retry != NULL);
            if (retry)
                rdb_drop_pool(retry);
        }
    }
}

static void test_index_ranges(void)
{
    rdb_pool_t *pool = rdb_register_um_pool("index-ranges", 2, 0,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL);
    if (!pool)
        return;

    rdb_fault_info_t fault;
    CHECK(rdb_pool_get_fault(pool, &fault) == 0);
    CHECK(fault.code == 0);

    CHECK(rdb_register_um_idx(pool, 0, 0, RDB_BTREE | RDB_KINT32, NULL, 0) == -1);
    CHECK(rdb_register_um_idx(pool, -1, 0, RDB_BTREE | RDB_KINT32, NULL, 0) == -2);
    CHECK(rdb_register_um_idx(pool, 2, 0, RDB_BTREE | RDB_KINT32, NULL, 0) == -2);
    CHECK(rdb_register_um_idx(pool, RDB_POOL_MAX_IDX, 0,
                RDB_BTREE | RDB_KINT32, NULL, 0) == -2);
    CHECK(pool->FLAGS[1] == 0);

    test_record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (record) {
        record->key = 42;
        CHECK(rdb_insert_one(pool, 0, record) >= 0);

        rdb_bpp_t links[2];
        memcpy(links, record->links, sizeof(links));
        CHECK(rdb_register_um_idx(pool, 1, 0,
                    RDB_BTREE | RDB_KINT32, NULL, 0) == 1);
        CHECK(pool->FLAGS[1] == (RDB_BTREE | RDB_KINT32));
        CHECK(memcmp(links, record->links, sizeof(links)) == 0);
        CHECK(rdb_get_const(pool, 0, 42) == record);

        rdb_flush(pool, NULL, NULL);
    }
    rdb_drop_pool(pool);
}

static void test_flag_validation(void)
{
    struct rejected_flags {
        uint32_t flags;
        void *compare_fn;
    } rejected[] = {
        {RDB_BTREE, NULL},
        {RDB_KINT32, NULL},
        {RDB_BTREE | RDB_KINT32 | RDB_KUINT32, NULL},
        {RDB_BTREE | RDB_KCF, NULL},
        {RDB_BTREE | RDB_KCFUT, NULL},
        {RDB_BTREE | RDB_KINT32 | RDB_NO_IDX, NULL},
        {RDB_BTREE | RDB_KFIFO, (void *)compare_keys},
        {RDB_BTREE | RDB_KLIFO, (void *)compare_keys},
        {RDB_BTREE | RDB_LIST | RDB_KINT32, NULL},
        {RDB_BTREE | RDB_KDEC | RDB_KINT32, NULL},
        {RDB_BTREE | RDB_KINT32 | (1U << 20), NULL},
        {RDB_BTREE | RDB_KINT256, NULL},
        {RDB_BTREE | RDB_KTME, NULL}
    };

#ifndef USE_128_BIT_TYPES
    const uint32_t unavailable[] = {
        RDB_BTREE | RDB_KINT128,
        RDB_BTREE | RDB_KUINT128
    };
#endif

    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        char name[32];
        snprintf(name, sizeof(name), "bad-flags-%zu", i);
        for (size_t api = 0; api < sizeof(create_pool) / sizeof(create_pool[0]); ++api) {
            CHECK(create_pool[api](name, 2, 0, rejected[i].flags,
                        rejected[i].compare_fn, 0) == NULL);
            CHECK(rdb_find_pool_by_name(name) == NULL);
        }
    }

#ifndef USE_128_BIT_TYPES
    for (size_t i = 0; i < sizeof(unavailable) / sizeof(unavailable[0]); ++i) {
        CHECK(rdb_register_um_pool("unavailable-key", 1, 0,
                    unavailable[i], NULL, 0) == NULL);
        CHECK(rdb_find_pool_by_name("unavailable-key") == NULL);
    }
#endif

    rdb_pool_t *pool = rdb_register_um_pool("retry-flags", 2, 0,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL);
    if (!pool)
        return;

    test_record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (record) {
        record->key = 42;
        CHECK(rdb_insert_one(pool, 0, record) >= 0);
        rdb_bpp_t links[2];
        memcpy(links, record->links, sizeof(links));

        CHECK(rdb_register_um_idx(pool, 1, 0,
                    RDB_BTREE | RDB_KCF, NULL, 0) == -4);
        CHECK(pool->FLAGS[1] == 0);
        CHECK(memcmp(links, record->links, sizeof(links)) == 0);
        CHECK(rdb_get_const(pool, 0, 42) == record);

        CHECK(rdb_register_um_idx(pool, 1, 0,
                    RDB_BTREE | RDB_KINT32, NULL, 0) == 1);
        CHECK(rdb_insert_one(pool, 1, record) >= 0);
        CHECK(rdb_get_const(pool, 1, 42) == record);
        rdb_flush(pool, NULL, NULL);
    }
    rdb_drop_pool(pool);
}

int main(void)
{
    rdb_init();
    test_index_counts();
    test_index_ranges();
    test_flag_validation();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
