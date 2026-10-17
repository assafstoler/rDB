#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "rdb.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

typedef struct {
    rdb_bpp_t link;
    void *key;
} record_t;

typedef struct {
    record_t *records[3];
    size_t count;
} visit_data_t;

static int collect_record(void *record, void *arg)
{
    visit_data_t *data = arg;
    if (data->count < sizeof(data->records) / sizeof(data->records[0]))
        data->records[data->count++] = record;
    return RDB_CB_OK;
}

int main(void)
{
    int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *pool;
    record_t records[3] = {{0}};
    record_t *expected[3];
    void *values[3] = {NULL, NULL, NULL};
    visit_data_t visited = {{NULL, NULL, NULL}, 0};

    rdb_init();
    pool = rdb_register_um_pool("pointer-keys", 1, key_offset,
            RDB_BTREE | RDB_KPTR, NULL, 0);
    CHECK(pool != NULL);
    if (!pool) {
        rdb_clean(0);
        return EXIT_FAILURE;
    }

    values[1] = malloc(1);
    values[2] = malloc(1);
    CHECK(values[1] != NULL && values[2] != NULL);
    if (!values[1] || !values[2]) {
        free(values[1]);
        free(values[2]);
        rdb_drop_pool(pool);
        return EXIT_FAILURE;
    }

    records[0].key = values[2];
    records[1].key = values[0];
    records[2].key = values[1];
    for (int i = 0; i < 3; ++i) {
        CHECK(rdb_insert(pool, &records[i]) == 1);
        expected[i] = &records[i];
    }

    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if ((uintptr_t)expected[j]->key < (uintptr_t)expected[i]->key) {
                record_t *tmp = expected[i];
                expected[i] = expected[j];
                expected[j] = tmp;
            }

    rdb_iterate(pool, 0, collect_record, &visited, NULL, NULL);
    CHECK(visited.count == 3);
    for (size_t i = 0; i < visited.count; ++i)
        CHECK(visited.records[i] == expected[i]);

    for (int i = 0; i < 3; ++i)
        CHECK(rdb_delete_one(pool, 0, &records[i]) >= 0);
    rdb_drop_pool(pool);
    free(values[1]);
    free(values[2]);

    if (failures) {
        fprintf(stderr, "%d pointer-key test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
