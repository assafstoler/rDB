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
    rdb_bpp_t link;
    int32_t key;
} record_t;

typedef struct {
    rdb_bpp_t link;
    char inline_key[16];
    char *pointer_key;
} string_record_t;

typedef struct {
    const char *const *expected;
    size_t count;
    size_t next;
    int pointer_key;
} string_state_t;

enum { TREE_INDEX, FIFO_INDEX, LIFO_INDEX };

typedef struct {
    int32_t keys[4];
    size_t count;
    size_t stop_after;
    int32_t deleted_key;
    size_t deletes;
} iterate_state_t;

static rdb_pool_t *make_pool(const char *name, int kind)
{
    uint32_t type = kind == TREE_INDEX ? RDB_KINT32 :
            (kind == FIFO_INDEX ? RDB_KFIFO : RDB_KLIFO);
    int offset = kind == TREE_INDEX ?
            (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t)) : 0;
    rdb_pool_t *pool = rdb_register_um_pool((char *)name, 1, offset,
            RDB_BTREE | type, NULL, 0);
    CHECK(pool != NULL);
    return pool;
}

static record_t *insert_record(rdb_pool_t *pool, int32_t key)
{
    record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!record)
        return NULL;
    record->key = key;
    CHECK(rdb_insert(pool, record) == 1);
    return record;
}

static int collect_key(void *data, void *opaque)
{
    record_t *record = data;
    iterate_state_t *state = opaque;
    if (state->count < sizeof(state->keys) / sizeof(state->keys[0]))
        state->keys[state->count] = record->key;
    ++state->count;
    if (state->stop_after && state->count == state->stop_after)
        return RDB_CB_ABORT;
    return RDB_CB_OK;
}

static int delete_and_abort(void *data, void *opaque)
{
    record_t *record = data;
    iterate_state_t *state = opaque;
    ++state->count;
    if (state->count == state->stop_after) {
        state->deleted_key = record->key;
        return RDB_CB_DELETE_NODE_AND_ABORT;
    }
    return RDB_CB_OK;
}

static void free_record(void *data, void *unused)
{
    (void)unused;
    free(data);
}

static void count_deleted(void *data, void *opaque)
{
    iterate_state_t *state = opaque;
    ++state->deletes;
    free(data);
}

static size_t expected_order(int kind, int32_t keys[4])
{
    if (kind == TREE_INDEX) {
        keys[0] = 10;
        keys[1] = 20;
        keys[2] = 30;
        keys[3] = 40;
    } else if (kind == FIFO_INDEX) {
        keys[0] = 30;
        keys[1] = 10;
        keys[2] = 40;
        keys[3] = 20;
    } else {
        keys[0] = 20;
        keys[1] = 40;
        keys[2] = 10;
        keys[3] = 30;
    }
    return 4;
}

static void check_iteration_order(rdb_pool_t *pool,
        const int32_t *expected, size_t expected_count)
{
    iterate_state_t state = {{0}, 0, 0, 0, 0};
    rdb_iterate(pool, 0, collect_key, &state, NULL, NULL);
    CHECK(state.count == expected_count);
    for (size_t i = 0; i < expected_count && i < state.count; ++i)
        CHECK(state.keys[i] == expected[i]);
}

static void test_order_and_abort(int kind)
{
    char name[40];
    int32_t expected[4];
    size_t expected_count = expected_order(kind, expected);
    snprintf(name, sizeof(name), "iterate-order-%d", kind);
    rdb_pool_t *pool = make_pool(name, kind);
    CHECK(pool != NULL);
    if (!pool)
        return;

    const int32_t insertion[] = {30, 10, 40, 20};
    for (size_t i = 0; i < sizeof(insertion) / sizeof(insertion[0]); ++i)
        CHECK(insert_record(pool, insertion[i]) != NULL);
    check_iteration_order(pool, expected, expected_count);

    iterate_state_t state = {{0}, 0, 2, 0, 0};
    rdb_iterate(pool, 0, collect_key, &state, NULL, NULL);
    CHECK(state.count == 2);
    CHECK(state.keys[0] == expected[0] && state.keys[1] == expected[1]);
    CHECK(pool->record_count == 4);
    check_iteration_order(pool, expected, expected_count);

    rdb_flush(pool, free_record, NULL);
    rdb_drop_pool(pool);
}

static void test_delete_and_abort(int kind)
{
    char name[40];
    int32_t expected[4], remaining[3];
    size_t expected_count = expected_order(kind, expected);
    snprintf(name, sizeof(name), "iterate-delete-abort-%d", kind);
    rdb_pool_t *pool = make_pool(name, kind);
    CHECK(pool != NULL);
    if (!pool)
        return;

    const int32_t insertion[] = {30, 10, 40, 20};
    for (size_t i = 0; i < sizeof(insertion) / sizeof(insertion[0]); ++i)
        CHECK(insert_record(pool, insertion[i]) != NULL);

    iterate_state_t state = {{0}, 0, 2, 0, 0};
    rdb_iterate(pool, 0, delete_and_abort, &state, count_deleted, &state);
    CHECK(state.count == 2);
    CHECK(state.deletes == 1);
    CHECK(pool->record_count == 3);
    size_t remaining_count = 0;
    for (size_t i = 0; i < expected_count; ++i)
        if (expected[i] != state.deleted_key)
            remaining[remaining_count++] = expected[i];
    CHECK(remaining_count == 3);
    check_iteration_order(pool, remaining, remaining_count);

    rdb_flush(pool, free_record, NULL);
    rdb_drop_pool(pool);
}

static int expect_string(void *data, void *opaque)
{
    string_record_t *record = data;
    string_state_t *state = opaque;
    const char *key = state->pointer_key ? record->pointer_key : record->inline_key;
    CHECK(state->next < state->count);
    if (state->next < state->count)
        CHECK(strcmp(key, state->expected[state->next]) == 0);
    ++state->next;
    return RDB_CB_OK;
}

static void test_string_order(uint32_t key_type)
{
    static const char *const inserted[] = {"delta", "alpha", "charlie", "bravo"};
    static const char *const expected[] = {"alpha", "bravo", "charlie", "delta"};
    static unsigned serial;
    char name[40];
    int pointer_key = key_type == RDB_KPSTR;
    int offset = (int)((pointer_key ? offsetof(string_record_t, pointer_key) :
            offsetof(string_record_t, inline_key)) - sizeof(rdb_bpp_t));
    snprintf(name, sizeof(name), "iterate-string-%u", serial++);
    rdb_pool_t *pool = rdb_register_um_pool(name, 1, offset,
            RDB_BTREE | key_type, NULL,
            pointer_key ? 0 : sizeof(((string_record_t *)0)->inline_key));
    CHECK(pool != NULL);
    if (!pool)
        return;

    for (size_t i = 0; i < sizeof(inserted) / sizeof(inserted[0]); ++i) {
        string_record_t *record = calloc(1, sizeof(*record));
        CHECK(record != NULL);
        if (!record)
            continue;
        if (pointer_key)
            record->pointer_key = strdup(inserted[i]);
        else
            strcpy(record->inline_key, inserted[i]);
        CHECK(!pointer_key || record->pointer_key != NULL);
        if (pointer_key && !record->pointer_key) {
            free(record);
            continue;
        }
        if (rdb_insert(pool, record) != 1) {
            CHECK(0);
            free(record->pointer_key);
            free(record);
        }
    }
    string_state_t state = {
        expected, sizeof(expected) / sizeof(expected[0]), 0, pointer_key
    };
    rdb_iterate(pool, 0, expect_string, &state, NULL, NULL);
    CHECK(state.next == state.count);
    CHECK(pool->record_count == state.count);
    rdb_flush(pool, NULL, NULL);
    rdb_drop_pool(pool);
}

int main(void)
{
    rdb_init();
    for (int kind = TREE_INDEX; kind <= LIFO_INDEX; ++kind) {
        test_order_and_abort(kind);
        test_delete_and_abort(kind);
    }
    test_string_order(RDB_KSTR);
    test_string_order(RDB_KPSTR);
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
