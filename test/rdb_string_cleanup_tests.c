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
    rdb_bpp_t links[3];
    char *key[2];
} record_t;

static rdb_pool_t *make_pool(char *name)
{
    int first = (int)(offsetof(record_t, key[0]) - sizeof(rdb_bpp_t) * 3);
    int second = (int)(offsetof(record_t, key[1]) - sizeof(rdb_bpp_t) * 3);
    rdb_pool_t *pool = rdb_register_um_pool(name, 3, first,
            RDB_BTREE | RDB_KPSTR, NULL, 0);
    CHECK(pool != NULL);
    if (!pool) return NULL;
    CHECK(rdb_register_um_idx(pool, 1, first,
                RDB_BTREE | RDB_KPSTR, NULL, 0) == 1);
    CHECK(rdb_register_um_idx(pool, 2, second,
                RDB_BTREE | RDB_KPSTR, NULL, 0) == 2);
    return pool;
}

static record_t *new_record(char *first, char *second)
{
    record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (record) {
        record->key[0] = first;
        record->key[1] = second;
    }
    return record;
}

static void check_cleared_record(void *data, void *unused)
{
    record_t *record = data;
    (void)unused;
    CHECK(record->key[0] == NULL);
    CHECK(record->key[1] == NULL);
    free(record);
}

static void flush_alias_with_callback(void)
{
    rdb_pool_t *pool = make_pool("string-alias-callback");
    char *shared = malloc(8);
    CHECK(shared != NULL);
    if (!pool || !shared) {
        if (pool) rdb_drop_pool(pool);
        free(shared);
        return;
    }
    strcpy(shared, "shared");
    record_t *record = new_record(shared, shared);
    CHECK(record && rdb_insert(pool, record) == 3);
    rdb_flush(pool, check_cleared_record, NULL);
    CHECK(pool->record_count == 0);
    rdb_drop_pool(pool);
}

static void flush_distinct_equal_keys(void)
{
    rdb_pool_t *pool = make_pool("string-equal-content");
    char *first = malloc(8), *second = malloc(8);
    CHECK(first && second);
    if (!pool || !first || !second) {
        if (pool) rdb_drop_pool(pool);
        free(first);
        free(second);
        return;
    }
    strcpy(first, "same");
    strcpy(second, "same");
    record_t *record = new_record(first, second);
    CHECK(record && rdb_insert(pool, record) == 3);
    rdb_flush(pool, NULL, NULL);
    CHECK(pool->record_count == 0);
    rdb_drop_pool(pool);
}

static int delete_record(void *data, void *unused)
{
    (void)data;
    (void)unused;
    return RDB_CB_DELETE_NODE;
}

static void iterate_delete_alias(void)
{
    rdb_pool_t *pool = make_pool("string-alias-iterate");
    char *shared = malloc(8);
    CHECK(shared != NULL);
    if (!pool || !shared) {
        if (pool) rdb_drop_pool(pool);
        free(shared);
        return;
    }
    strcpy(shared, "shared");
    record_t *record = new_record(shared, shared);
    CHECK(record && rdb_insert(pool, record) == 3);
    rdb_iterate(pool, 0, delete_record, NULL, NULL, NULL);
    CHECK(pool->record_count == 0);
    rdb_drop_pool(pool);
}

static void delete_keeps_record_owned(void)
{
    rdb_pool_t *pool = make_pool("string-delete-owned");
    char *shared = malloc(8);
    CHECK(shared != NULL);
    if (!pool || !shared) {
        if (pool) rdb_drop_pool(pool);
        free(shared);
        return;
    }
    strcpy(shared, "shared");
    record_t *record = new_record(shared, shared);
    CHECK(record && rdb_insert(pool, record) == 3);
    CHECK(rdb_delete(pool, 0, shared) == record);
    CHECK(record->key[0] == shared && record->key[1] == shared);
    CHECK(strcmp(record->key[0], "shared") == 0);
    free(shared);
    free(record);
    rdb_drop_pool(pool);
}

int main(void)
{
    rdb_init();
    flush_alias_with_callback();
    flush_distinct_equal_keys();
    iterate_delete_alias();
    delete_keeps_record_owned();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
