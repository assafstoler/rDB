#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rdb.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

typedef struct {
    rdb_bpp_t links[RDB_POOL_MAX_IDX];
    int32_t key;
} record_t;

static rdb_pool_t *make_pool(const char *name, int indexes)
{
    const int key_offset = (int)(offsetof(record_t, key) -
            sizeof(rdb_bpp_t) * indexes);
    rdb_pool_t *pool = rdb_register_um_pool((char *)name, indexes,
            key_offset, RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL);
    if (!pool)
        return NULL;

    for (int index = 1; index < indexes; ++index)
        CHECK(rdb_register_um_idx(pool, index, key_offset,
                    RDB_BTREE | RDB_KINT32, NULL, 0) == index);
    return pool;
}

static int delete_visit(void *record, void *count)
{
    (void)record;
    ++*(int *)count;
    return RDB_CB_DELETE_NODE;
}

static void delete_cleanup(void *record, void *count)
{
    (void)record;
    ++*(int *)count;
}

static size_t dump_output(rdb_pool_t *pool, int index)
{
    FILE *capture = tmpfile();
    CHECK(capture != NULL);
    if (!capture)
        return 0;
    CHECK(fflush(stdout) == 0);
    int saved = dup(STDOUT_FILENO);
    CHECK(saved >= 0);
    if (saved < 0) {
        fclose(capture);
        return 0;
    }
    CHECK(dup2(fileno(capture), STDOUT_FILENO) == STDOUT_FILENO);
    rdb_dump(pool, index, ",");
    CHECK(fflush(stdout) == 0);
    CHECK(dup2(saved, STDOUT_FILENO) == STDOUT_FILENO);
    CHECK(close(saved) == 0);
    rewind(capture);
    char output[64];
    size_t size = fread(output, 1, sizeof(output), capture);
    CHECK(!ferror(capture));
    CHECK(fclose(capture) == 0);
    return size;
}

static void check_invalid_index(rdb_pool_t *source, rdb_pool_t *destination,
        record_t *record, record_t *other, record_t *candidate, int index)
{
    rdb_bpp_t source_roots[RDB_POOL_MAX_IDX];
    rdb_bpp_t destination_roots[RDB_POOL_MAX_IDX];
    record_t record_before, other_before, candidate_before;
    const uint32_t source_count = source->record_count;
    const uint32_t destination_count = destination->record_count;
    int32_t key = record->key;
    int visits = 0, cleanups = 0;
    void *before = record, *after = other;

    memcpy(source_roots, source->root, sizeof(source_roots));
    memcpy(destination_roots, destination->root, sizeof(destination_roots));
    memcpy(&record_before, record, sizeof(record_before));
    memcpy(&other_before, other, sizeof(other_before));
    memcpy(&candidate_before, candidate, sizeof(candidate_before));

    CHECK(rdb_get(source, index, &key) == NULL);
    CHECK(rdb_get_const(source, index, key) == NULL);
    CHECK(rdb_get_neigh(source, index, &key, &before, &after) == NULL);
    CHECK(before == NULL && after == NULL);
    CHECK(rdb_insert_one(source, index, candidate) == -1);
    CHECK(rdb_delete(source, index, &key) == NULL);
    CHECK(rdb_delete_one(source, index, record) == -1);
    CHECK(rdb_delete_const(source, index, key) == NULL);
    rdb_iterate(source, index, delete_visit, &visits, delete_cleanup, &cleanups);
    CHECK(rdb_error_string && strstr(rdb_error_string, "rdb_iterate"));
    CHECK(dump_output(source, index) == 0);
    CHECK(rdb_error_string && strstr(rdb_error_string, "rdb_dump"));
    CHECK(rdb_move(destination, source, index, &key) == NULL);
    CHECK(rdb_move_const(destination, source, index, key) == NULL);
    CHECK(rdb_move2(destination, source, index, &key) == -1);

    CHECK(visits == 0 && cleanups == 0);
    CHECK(memcmp(source_roots, source->root, sizeof(source_roots)) == 0);
    CHECK(memcmp(destination_roots, destination->root, sizeof(destination_roots)) == 0);
    CHECK(source->record_count == source_count);
    CHECK(destination->record_count == destination_count);
    CHECK(memcmp(&record_before, record, sizeof(record_before)) == 0);
    CHECK(memcmp(&other_before, other, sizeof(other_before)) == 0);
    CHECK(memcmp(&candidate_before, candidate, sizeof(candidate_before)) == 0);
    for (int slot = 0; slot < source->indexCount; ++slot) {
        CHECK(rdb_get(source, slot, &record->key) == record);
        CHECK(rdb_get(destination, slot, &other->key) == other);
    }
}

static void test_invalid_indexes(void)
{
    const int counts[] = {1, 3, RDB_POOL_MAX_IDX};
    for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c) {
        const int indexes = counts[c];
        char source_name[32], destination_name[32];
        snprintf(source_name, sizeof(source_name), "bounds-source-%d", indexes);
        snprintf(destination_name, sizeof(destination_name), "bounds-dest-%d", indexes);
        rdb_pool_t *source = make_pool(source_name, indexes);
        rdb_pool_t *destination = make_pool(destination_name, indexes);
        CHECK(source && destination);
        if (!source || !destination) {
            if (source) rdb_drop_pool(source);
            if (destination) rdb_drop_pool(destination);
            continue;
        }

        record_t *record = calloc(1, sizeof(*record));
        record_t *other = calloc(1, sizeof(*other));
        record_t candidate = {0};
        CHECK(record && other);
        if (!record || !other) {
            free(record);
            free(other);
            rdb_drop_pool(source);
            rdb_drop_pool(destination);
            continue;
        }

        record->key = 31;
        other->key = 97;
        candidate.key = 53;
        CHECK(rdb_insert(source, record) == indexes);
        CHECK(rdb_insert(destination, other) == indexes);

        const int invalid[] = {
            -1, -RDB_POOL_MAX_IDX, INT_MIN, indexes, indexes + 1,
            RDB_POOL_MAX_IDX - 1, RDB_POOL_MAX_IDX,
            RDB_POOL_MAX_IDX + 1, INT_MAX
        };
        for (size_t n = 0; n < sizeof(invalid) / sizeof(invalid[0]); ++n) {
            if (invalid[n] >= 0 && invalid[n] < indexes)
                continue;
            check_invalid_index(source, destination, record, other, &candidate, invalid[n]);
        }

        CHECK(rdb_get(NULL, 0, NULL) == NULL);
        CHECK(rdb_get_const(NULL, 0, 1) == NULL);
        void *before = record, *after = other;
        CHECK(rdb_get_neigh(NULL, 0, NULL, &before, &after) == NULL);
        CHECK(before == NULL && after == NULL);
        CHECK(rdb_insert_one(NULL, 0, &candidate) == -1);
        CHECK(rdb_delete(NULL, 0, NULL) == NULL);
        CHECK(rdb_delete_one(NULL, 0, &candidate) == -1);
        CHECK(rdb_delete_const(NULL, 0, 1) == NULL);
        CHECK(rdb_move(destination, NULL, 0, NULL) == NULL);
        CHECK(rdb_move_const(destination, NULL, 0, 1) == NULL);
        CHECK(rdb_move2(destination, NULL, 0, NULL) == -1);
        int visits = 0, cleanups = 0;
        rdb_iterate(NULL, 0, delete_visit, &visits, delete_cleanup, &cleanups);
        CHECK(rdb_error_string && strstr(rdb_error_string, "rdb_iterate"));
        CHECK(dump_output(NULL, 0) == 0);
        CHECK(rdb_error_string && strstr(rdb_error_string, "rdb_dump"));

        rdb_flush(source, NULL, NULL);
        rdb_flush(destination, NULL, NULL);
        rdb_drop_pool(source);
        rdb_drop_pool(destination);
    }
}

static void test_valid_boundaries(void)
{
    const int counts[] = {1, 3, RDB_POOL_MAX_IDX};
    for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c) {
        const int indexes = counts[c];
        char name[32];
        snprintf(name, sizeof(name), "bounds-valid-%d", indexes);
        rdb_pool_t *pool = make_pool(name, indexes);
        CHECK(pool != NULL);
        if (!pool)
            continue;

        record_t *record = calloc(1, sizeof(*record));
        CHECK(record != NULL);
        if (!record) {
            rdb_drop_pool(pool);
            continue;
        }
        record->key = 42;
        CHECK(rdb_insert(pool, record) == indexes);

        const int valid[] = {0, indexes - 1};
        for (size_t n = 0; n < sizeof(valid) / sizeof(valid[0]); ++n) {
            const int index = valid[n];
            void *before = record, *after = record;
            int32_t lower_key = record->key - 1;
            int32_t upper_key = record->key + 1;
            CHECK(rdb_get(pool, index, &record->key) == record);
            CHECK(rdb_get_const(pool, index, record->key) == record);
            CHECK(rdb_get_neigh(pool, index, &record->key, &before, &after) == record);
            CHECK(before == NULL && after == NULL);
            before = after = record;
            CHECK(rdb_get_neigh(pool, index, &lower_key, &before, &after) == NULL);
            CHECK(before == NULL && after == record);
            before = after = record;
            CHECK(rdb_get_neigh(pool, index, &upper_key, &before, &after) == NULL);
            CHECK(before == record && after == NULL);
            before = after = record;
            CHECK(rdb_get_neigh(pool, index, NULL, &before, &after) == record);
            CHECK(before == NULL && after == NULL);
            CHECK(rdb_delete_one(pool, index, record) >= 0);
            before = after = record;
            CHECK(rdb_get_neigh(pool, index, &record->key, &before, &after) == NULL);
            CHECK(before == NULL && after == NULL);
            CHECK(rdb_get(pool, index, &record->key) == NULL);
            CHECK(rdb_insert_one(pool, index, record) >= 0);
            CHECK(rdb_get(pool, index, &record->key) == record);
        }

        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}

int main(void)
{
    rdb_init();
    test_invalid_indexes();
    test_valid_boundaries();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
