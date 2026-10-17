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
    uint32_t id;
    char *key;
} record_t;

static rdb_pool_t *make_pool(int string_index)
{
    int id_offset = (int)(offsetof(record_t, id) - sizeof(rdb_bpp_t) * 2);
    int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t) * 2);
    int primary_offset = string_index == 0 ? key_offset : id_offset;
    int secondary_offset = string_index == 0 ? id_offset : key_offset;
    int primary_type = string_index == 0 ? RDB_KPSTR : RDB_KUINT32;
    int secondary_type = string_index == 0 ? RDB_KUINT32 : RDB_KPSTR;
    rdb_pool_t *pool = rdb_register_um_pool("string-neighbor", 2,
            primary_offset, RDB_BTREE | primary_type, NULL, 0);
    CHECK(pool != NULL);
    if (pool)
        CHECK(rdb_register_um_idx(pool, 1, secondary_offset,
                    RDB_BTREE | secondary_type, NULL, 0) == 1);
    return pool;
}

static void check_query(rdb_pool_t *pool, int index, const char *query,
        record_t *expected,
        record_t *lower, record_t *higher)
{
    int before_sentinel, after_sentinel;
    void *before = &before_sentinel, *after = &after_sentinel;
    CHECK(rdb_get(pool, index, query) == expected);
    CHECK(rdb_get_neigh(pool, index, (void *)query, &before, &after) == expected);
    CHECK(before == lower && after == higher);
}

static void test_string_index(int string_index)
{
    static const char *keys[] = {"alpha", "delta", "omega"};
    rdb_pool_t *pool = make_pool(string_index);
    record_t *records[3] = {NULL, NULL, NULL};
    if (!pool)
        return;

    /* Reverse insertion order so lookup and neighbor traversal walk both sides. */
    for (int i = 2; i >= 0; --i) {
        records[i] = calloc(1, sizeof(*records[i]));
        CHECK(records[i] != NULL);
        if (!records[i]) {
            rdb_drop_pool(pool);
            return;
        }
        records[i]->id = (uint32_t)(i + 1);
        records[i]->key = malloc(strlen(keys[i]) + 1);
        CHECK(records[i]->key != NULL);
        if (!records[i]->key) {
            free(records[i]);
            records[i] = NULL;
            rdb_drop_pool(pool);
            return;
        }
        strcpy(records[i]->key, keys[i]);
        CHECK(rdb_insert(pool, records[i]) == 2);
    }

    check_query(pool, string_index, "aardvark", NULL, NULL, records[0]);
    check_query(pool, string_index, "alpha", records[0], NULL, NULL);
    check_query(pool, string_index, "bravo", NULL, records[0], records[1]);
    check_query(pool, string_index, "delta", records[1], NULL, NULL);
    check_query(pool, string_index, "nu", NULL, records[1], records[2]);
    check_query(pool, string_index, "zulu", NULL, records[2], NULL);

    for (int i = 0; i < 3; ++i) {
        CHECK(rdb_get_const(pool, 1 - string_index, records[i]->id) == records[i]);
        CHECK(rdb_delete(pool, string_index, (void *)keys[i]) == records[i]);
        CHECK(rdb_get_const(pool, 1 - string_index, records[i]->id) == NULL);
        free(records[i]->key);
        free(records[i]);
        records[i] = NULL;
    }
    int before_sentinel, after_sentinel;
    void *before = &before_sentinel, *after = &after_sentinel;
    CHECK(rdb_get_neigh(pool, string_index, "delta", &before, &after) == NULL);
    CHECK(before == NULL && after == NULL);
    rdb_drop_pool(pool);
}

int main(void)
{
    rdb_init();
    test_string_index(0);
    test_string_index(1);
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
