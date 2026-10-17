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
    int32_t key;
} record_t;

typedef struct {
    unsigned delete_mask;
    unsigned visits;
    unsigned deleted;
} visit_data_t;

static int visit_record(void *data, void *arg)
{
    record_t *record = data;
    visit_data_t *state = arg;
    ++state->visits;
    CHECK(record->key == (int32_t)state->visits);
    if (state->delete_mask & (1U << (record->key - 1))) {
        ++state->deleted;
        return RDB_CB_DELETE_NODE;
    }
    return RDB_CB_OK;
}

static void free_record(void *data, void *unused)
{
    (void)unused;
    free(data);
}

static void selective_deletions(void)
{
    static const int insertion_order[] = {4, 2, 6, 1, 3, 5, 7};
    const unsigned all_masks = (1U << 7) - 1;

    for (unsigned mask = 1; mask <= all_masks; ++mask) {
        char name[32];
        snprintf(name, sizeof(name), "iterate-delete-%u", mask);
        int offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
        rdb_pool_t *pool = rdb_register_um_pool(name, 1, offset,
                RDB_BTREE | RDB_KINT32, NULL, 0);
        record_t *records[7] = {NULL};
        CHECK(pool != NULL);
        if (!pool) continue;

        for (size_t i = 0; i < sizeof(insertion_order) / sizeof(insertion_order[0]); ++i) {
            int key = insertion_order[i];
            records[key - 1] = calloc(1, sizeof(*records[0]));
            CHECK(records[key - 1] != NULL);
            if (!records[key - 1]) continue;
            records[key - 1]->key = key;
            CHECK(rdb_insert(pool, records[key - 1]) == 1);
        }

        visit_data_t state = {mask, 0, 0};
        rdb_iterate(pool, 0, visit_record, &state, free_record, NULL);
        CHECK(state.visits == 7);
        unsigned expected_deleted = 0;
        for (int key = 1; key <= 7; ++key) {
            int deleted = (mask & (1U << (key - 1))) != 0;
            expected_deleted += deleted;
            CHECK(rdb_get_const(pool, 0, key) == (deleted ? NULL : records[key - 1]));
            if (!deleted) records[key - 1] = NULL;
        }
        CHECK(state.deleted == expected_deleted);
        CHECK(pool->record_count == 7 - expected_deleted);
        rdb_flush(pool, free_record, NULL);
        rdb_drop_pool(pool);
    }
}

int main(void)
{
    rdb_init();
    selective_deletions();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
