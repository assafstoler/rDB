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

enum { INDEXES = 3, MAX_RECORDS = 4 };

typedef struct {
    rdb_bpp_t links[INDEXES];
    int32_t key[INDEXES];
} record_t;

typedef struct {
    record_t *records[MAX_RECORDS];
    size_t count;
} visit_data_t;

typedef struct {
    visit_data_t index[INDEXES];
    rdb_bpp_t *root[INDEXES];
    rdb_bpp_t *tail[INDEXES];
    unsigned record_count;
} snapshot_t;

static int collect_record(void *data, void *opaque)
{
    visit_data_t *visit = opaque;
    CHECK(visit->count < MAX_RECORDS);
    if (visit->count < MAX_RECORDS)
        visit->records[visit->count++] = data;
    return RDB_CB_OK;
}

static record_t *new_record(int value)
{
    record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (record)
        for (int index = 0; index < INDEXES; ++index)
            record->key[index] = value + 100 * index;
    return record;
}

static void keep_record(void *data, void *unused)
{
    (void)data;
    (void)unused;
}

static int key_offset(int index)
{
    return (int)(offsetof(record_t, key[index]) - sizeof(rdb_bpp_t) * INDEXES);
}

static rdb_pool_t *make_pool(const char *prefix, unsigned serial, int queue_flag)
{
    char name[64];
    snprintf(name, sizeof(name), "%s-%u", prefix, serial);
    rdb_pool_t *pool = rdb_register_um_pool(name, INDEXES, key_offset(0),
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL);
    if (!pool)
        return NULL;
    CHECK(rdb_register_um_idx(pool, 1, 0, RDB_BTREE | queue_flag, NULL, 0) == 1);
    CHECK(rdb_register_um_idx(pool, 2, key_offset(2),
                RDB_BTREE | RDB_KINT32, NULL, 0) == 2);
    return pool;
}

static snapshot_t take_snapshot(rdb_pool_t *pool)
{
    snapshot_t result = {0};
    result.record_count = pool->record_count;
    for (int index = 0; index < INDEXES; ++index) {
        result.root[index] = pool->root[index];
        result.tail[index] = pool->tail[index];
        rdb_iterate(pool, index, collect_record, &result.index[index], NULL, NULL);
    }
    return result;
}

static void check_snapshot(rdb_pool_t *pool, const snapshot_t *expected)
{
    snapshot_t actual = take_snapshot(pool);
    CHECK(actual.record_count == expected->record_count);
    for (int index = 0; index < INDEXES; ++index) {
        CHECK(actual.root[index] == expected->root[index]);
        CHECK(actual.tail[index] == expected->tail[index]);
        CHECK(actual.index[index].count == expected->index[index].count);
        for (size_t n = 0; n < actual.index[index].count; ++n)
            CHECK(actual.index[index].records[n] == expected->index[index].records[n]);
    }
}

static void call_move(int variant, rdb_pool_t *destination, rdb_pool_t *source,
        record_t *record)
{
    if (variant == 0)
        CHECK(rdb_move(destination, source, 0, &record->key[0]) == NULL);
    else if (variant == 1)
        CHECK(rdb_move_const(destination, source, 0, record->key[0]) == NULL);
    else
        CHECK(rdb_move2(destination, source, 0, &record->key[0]) == -2);
}

static void call_successful_move(int variant, rdb_pool_t *destination,
        rdb_pool_t *source, record_t *record)
{
    if (variant == 0)
        CHECK(rdb_move(destination, source, 0, &record->key[0]) == record);
    else if (variant == 1)
        CHECK(rdb_move_const(destination, source, 0, record->key[0]) == record);
    else
        CHECK(rdb_move2(destination, source, 0, &record->key[0]) == 0);
}

/* A collision at the last tree index happens after the destination has
 * accepted the record into its first tree and queue. The source queue must
 * return to its exact former order on every public move API. */
static void destination_collision_rollbacks(void)
{
    unsigned serial = 0;
    for (int variant = 0; variant < 3; ++variant)
        for (int source_queue = RDB_KFIFO; source_queue <= RDB_KLIFO; ++source_queue)
            for (int destination_queue = RDB_KFIFO;
                    destination_queue <= RDB_KLIFO; ++destination_queue)
                for (int position = 0; position < 3; ++position) {
                    rdb_pool_t *source = make_pool("move-matrix-source", serial,
                            source_queue);
                    rdb_pool_t *destination = make_pool("move-matrix-destination",
                            serial++, destination_queue);
                    record_t *records[3] = {NULL};
                    record_t *blocker = new_record(900);
                    if (!source || !destination || !blocker)
                        goto cleanup;

                    for (int i = 0; i < 3; ++i) {
                        records[i] = new_record((i + 1) * 10);
                        CHECK(records[i] != NULL);
                        if (records[i])
                            CHECK(rdb_insert(source, records[i]) == INDEXES);
                    }
                    if (!records[0] || !records[1] || !records[2])
                        goto cleanup;
                    blocker->key[2] = records[position]->key[2];
                    CHECK(rdb_insert(destination, blocker) == INDEXES);

                    snapshot_t source_before = take_snapshot(source);
                    snapshot_t destination_before = take_snapshot(destination);
                    for (int attempt = 0; attempt < 2; ++attempt) {
                        call_move(variant, destination, source, records[position]);
                        CHECK(rdb_pool_get_fault(source, NULL) == 0);
                        CHECK(rdb_pool_get_fault(destination, NULL) == 0);
                        check_snapshot(source, &source_before);
                        check_snapshot(destination, &destination_before);
                    }

                    CHECK(rdb_delete(destination, 2, &blocker->key[2]) == blocker);
                    call_successful_move(variant, destination, source, records[position]);
                    CHECK(source->record_count == 2);
                    CHECK(destination->record_count == 1);
                    CHECK(rdb_get(source, 0, &records[position]->key[0]) == NULL);
                    CHECK(rdb_get(destination, 0, &records[position]->key[0]) == records[position]);

cleanup:
                    if (source) {
                        rdb_flush(source, keep_record, NULL);
                        rdb_drop_pool(source);
                    }
                    if (destination) {
                        rdb_flush(destination, keep_record, NULL);
                        rdb_drop_pool(destination);
                    }
                    for (int i = 0; i < 3; ++i)
                        free(records[i]);
                    free(blocker);
                }
}

/* A missing destination is a rejected move too. In particular, moving a
 * middle queue record must not append it at the end while restoring it. */
static void missing_destination_restores_queue(void)
{
    unsigned serial = 1000;
    for (int variant = 0; variant < 3; ++variant)
        for (int queue = RDB_KFIFO; queue <= RDB_KLIFO; ++queue)
            for (int position = 0; position < 3; ++position) {
                rdb_pool_t *source = make_pool("move-null-source", serial++, queue);
                record_t *records[3] = {NULL};
                if (!source)
                    continue;
                for (int i = 0; i < 3; ++i) {
                    records[i] = new_record((i + 1) * 10);
                    if (records[i]) CHECK(rdb_insert(source, records[i]) == INDEXES);
                }
                if (!records[0] || !records[1] || !records[2]) {
                    rdb_flush(source, keep_record, NULL);
                    rdb_drop_pool(source);
                    for (int i = 0; i < 3; ++i) free(records[i]);
                    continue;
                }
                snapshot_t before = take_snapshot(source);
                record_t *record = records[position];
                if (variant == 0)
                    CHECK(rdb_move(NULL, source, 0, &record->key[0]) == NULL);
                else if (variant == 1)
                    CHECK(rdb_move_const(NULL, source, 0, record->key[0]) == NULL);
                else
                    CHECK(rdb_move2(NULL, source, 0, &record->key[0]) == -2);
                CHECK(rdb_pool_get_fault(source, NULL) == 0);
                check_snapshot(source, &before);
                rdb_flush(source, keep_record, NULL);
                rdb_drop_pool(source);
                for (int i = 0; i < 3; ++i) free(records[i]);
            }
}

int main(void)
{
    rdb_init();
    destination_collision_rollbacks();
    missing_destination_restores_queue();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
