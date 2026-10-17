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
    char key[2][8];
    uint32_t id;
} record_t;

static int string_offset(void)
{
    return (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t) * 2);
}

static int id_offset(void)
{
    return (int)(offsetof(record_t, id) - sizeof(rdb_bpp_t) * 2);
}

static rdb_pool_t *make_pool(char *name, int string_index)
{
    int primary_offset = string_index == 0 ? string_offset() : id_offset();
    int primary_type = string_index == 0 ? RDB_KSTR : RDB_KUINT32;
    size_t primary_size = string_index == 0 ? sizeof(((record_t *)0)->key[0]) : 0;
    int secondary_offset = string_index == 1 ? string_offset() : id_offset();
    int secondary_type = string_index == 1 ? RDB_KSTR : RDB_KUINT32;
    size_t secondary_size = string_index == 1 ? sizeof(((record_t *)0)->key[0]) : 0;
    rdb_pool_t *pool = rdb_register_um_pool(name, 2, primary_offset,
            RDB_BTREE | primary_type, NULL, primary_size);
    CHECK(pool != NULL);
    if (pool)
        CHECK(rdb_register_um_idx(pool, 1, secondary_offset,
                    RDB_BTREE | secondary_type, NULL, secondary_size) == 1);
    return pool;
}

static record_t *new_record(const char *text, unsigned id)
{
    record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (record) {
        snprintf(record->key[0], sizeof(record->key[0]), "%s", text);
        snprintf(record->key[1], sizeof(record->key[1]), "%s", text);
        record->id = id;
    }
    return record;
}

static void test_inline_key(int string_index)
{
    static const char *keys[] = {"alpha", "delta", "omega"};
    char name[40];
    snprintf(name, sizeof(name), "inline-string-%d", string_index);
    rdb_pool_t *pool = make_pool(name, string_index);
    record_t *records[3] = {NULL};
    if (!pool)
        return;

    for (int i = 2; i >= 0; --i) {
        records[i] = new_record(keys[i], (unsigned)i + 1);
        if (records[i])
            CHECK(rdb_insert(pool, records[i]) == 2);
    }
    if (!records[0] || !records[1] || !records[2]) {
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
        return;
    }

    void *before, *after;
    before = after = records[0];
    CHECK(rdb_get(pool, string_index, "aardvark") == NULL);
    CHECK(rdb_get_neigh(pool, string_index, "aardvark", &before, &after) == NULL);
    CHECK(before == NULL && after == records[0]);
    CHECK(rdb_get(pool, string_index, "bravo") == NULL);
    CHECK(rdb_get_neigh(pool, string_index, "bravo", &before, &after) == NULL);
    CHECK(before == records[0] && after == records[1]);
    CHECK(rdb_get(pool, string_index, "delta") == records[1]);
    CHECK(rdb_get_neigh(pool, string_index, "delta", &before, &after) == records[1]);
    CHECK(before == NULL && after == NULL);
    before = after = records[0];
    CHECK(rdb_get_neigh(pool, string_index, "zulu", &before, &after) == NULL);
    CHECK(before == records[2] && after == NULL);

    for (int i = 0; i < 3; ++i) {
        CHECK(rdb_get_const(pool, 1 - string_index, records[i]->id) == records[i]);
        CHECK(rdb_delete(pool, string_index, (void *)keys[i]) == records[i]);
        CHECK(rdb_get_const(pool, 1 - string_index, records[i]->id) == NULL);
        free(records[i]);
    }
    CHECK(pool->record_count == 0);
    rdb_drop_pool(pool);
}

static void test_size_registration(void)
{
    char name[] = "inline-string-size";
    CHECK(rdb_register_um_pool(name, 1, string_offset(), RDB_BTREE | RDB_KSTR,
                NULL, 0) == NULL);
    rdb_pool_t *pool = rdb_register_um_pool(name, 2, id_offset(),
            RDB_BTREE | RDB_KUINT32, NULL, 0);
    CHECK(pool != NULL);
    if (!pool)
        return;
    CHECK(rdb_register_um_idx(pool, 1, string_offset(),
                RDB_BTREE | RDB_KSTR, NULL, 0) == -4);
    CHECK(rdb_register_um_idx(pool, 1, string_offset(),
                RDB_BTREE | RDB_KSTR, NULL,
                sizeof(((record_t *)0)->key[0])) == 1);
    rdb_drop_pool(pool);
}

int main(void)
{
    rdb_init();
    test_inline_key(0);
    test_inline_key(1);
    test_size_registration();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
