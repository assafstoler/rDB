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
    rdb_bpp_t links[2];
    uint32_t id;
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
    record_t records[3] = {{0}};
    record_t *expected[3];
    void *values[3] = {NULL, NULL, NULL};

    rdb_init();
    values[1] = malloc(1);
    values[2] = malloc(1);
    CHECK(values[1] != NULL && values[2] != NULL);
    if (!values[1] || !values[2]) {
        free(values[1]);
        free(values[2]);
        return EXIT_FAILURE;
    }

    records[0].key = values[2];
    records[1].key = values[0];
    records[2].key = values[1];
    for (int i = 0; i < 3; ++i)
        expected[i] = &records[i];
    for (int i = 0; i < 3; ++i)
        records[i].id = (uint32_t)(i + 1);
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if ((uintptr_t)expected[j]->key < (uintptr_t)expected[i]->key) {
                record_t *tmp = expected[i];
                expected[i] = expected[j];
                expected[j] = tmp;
            }

    for (int pointer_index = 0; pointer_index < 2; ++pointer_index) {
        int scalar_index = 1 - pointer_index;
        int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t) * 2);
        int id_offset = (int)(offsetof(record_t, id) - sizeof(rdb_bpp_t) * 2);
        int primary_offset = pointer_index ? id_offset : key_offset;
        int primary_type = pointer_index ? RDB_KUINT32 : RDB_KPTR;
        int secondary_offset = pointer_index ? key_offset : id_offset;
        int secondary_type = pointer_index ? RDB_KPTR : RDB_KUINT32;
        rdb_pool_t *pool;
        visit_data_t visited = {{NULL, NULL, NULL}, 0};
        char pool_name[] = "pointer-keys-0";
        pool_name[13] = (char)('0' + pointer_index);

        pool = rdb_register_um_pool(pool_name, 2, primary_offset,
                RDB_BTREE | primary_type, NULL, 0);
        CHECK(pool != NULL);
        if (!pool) {
            free(values[1]);
            free(values[2]);
            rdb_clean(0);
            return EXIT_FAILURE;
        }
        CHECK(rdb_register_um_idx(pool, 1, secondary_offset,
                    RDB_BTREE | secondary_type, NULL, 0) == 1);
        for (int i = 0; i < 3; ++i)
            CHECK(rdb_insert(pool, &records[i]) == 2);

        rdb_iterate(pool, pointer_index, collect_record, &visited, NULL, NULL);
        CHECK(visited.count == 3);
        for (size_t i = 0; i < visited.count; ++i)
            CHECK(visited.records[i] == expected[i]);

        for (int i = 0; i < 3; ++i) {
            void *key = records[i].key;
            CHECK(rdb_get(pool, pointer_index, &key) == &records[i]);
            CHECK(rdb_get(pool, pointer_index, &records[i].key) == &records[i]);
            CHECK(rdb_get_const(pool, scalar_index, records[i].id) == &records[i]);
        }
        static int missing_value;
        void *missing = &missing_value;
        CHECK(rdb_get(pool, pointer_index, &missing) == NULL);

        for (int i = 0; i < 3; ++i) {
            void *key = records[i].key;
            CHECK(rdb_delete(pool, pointer_index, &key) == &records[i]);
            CHECK(rdb_get_const(pool, scalar_index, records[i].id) == NULL);
        }
        CHECK(pool->record_count == 0);
        rdb_drop_pool(pool);
    }
    free(values[1]);
    free(values[2]);

    if (failures) {
        fprintf(stderr, "%d pointer-key test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
