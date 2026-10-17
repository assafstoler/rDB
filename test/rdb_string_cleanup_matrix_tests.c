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

enum { INDEXES = 5, STRING_KEYS = 3, ITEMS = 2 };
enum { ALIAS_ALL, ALIAS_01, ALIAS_02, ALIAS_12, EQUAL_STRINGS, PATTERNS };
enum { FLUSH_DEFAULT, FLUSH_CALLBACK, ITERATE_ALL, ITERATE_ONE, DELETE_ONE };

typedef struct {
    rdb_bpp_t links[INDEXES];
    char *key[STRING_KEYS];
    unsigned id;
} record_t;

typedef struct {
    unsigned seen;
    unsigned cleaned;
    unsigned deleted;
} cleanup_data_t;

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    CHECK(copy != NULL);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static void free_unique_keys(record_t *record)
{
    for (int key = 0; key < STRING_KEYS; ++key) {
        int seen = 0;
        for (int prior = 0; prior < key; ++prior)
            if (record->key[key] == record->key[prior]) seen = 1;
        if (!seen)
            free(record->key[key]);
    }
}

static record_t *new_record(unsigned id, int pattern)
{
    static const char *texts[ITEMS][STRING_KEYS] = {
        {"alpha", "charlie", "echo"},
        {"bravo", "delta", "foxtrot"}
    };
    record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!record)
        return NULL;
    record->id = id;
    for (int key = 0; key < STRING_KEYS; ++key) {
        record->key[key] = copy_text(texts[id][pattern == EQUAL_STRINGS ? 0 : key]);
        if (!record->key[key]) {
            free_unique_keys(record);
            free(record);
            return NULL;
        }
    }

    if (pattern == ALIAS_ALL) {
        free(record->key[1]); free(record->key[2]);
        record->key[1] = record->key[2] = record->key[0];
    } else if (pattern == ALIAS_01) {
        free(record->key[1]);
        record->key[1] = record->key[0];
    } else if (pattern == ALIAS_02) {
        free(record->key[2]);
        record->key[2] = record->key[0];
    } else if (pattern == ALIAS_12) {
        free(record->key[2]);
        record->key[2] = record->key[1];
    } else if (pattern == EQUAL_STRINGS) {
        /* Keep separate allocations with equal values to catch content-based
         * cleanup deduplication where pointer identity is required. */
    }
    return record;
}

static int role_type(int role)
{
    if (role < STRING_KEYS) return RDB_KPSTR;
    return role == STRING_KEYS ? RDB_KFIFO : RDB_KLIFO;
}

static int role_offset(int role)
{
    if (role >= STRING_KEYS)
        return 0;
    return (int)(offsetof(record_t, key[role]) - sizeof(rdb_bpp_t) * INDEXES);
}

static rdb_pool_t *make_pool(unsigned serial, int shift)
{
    char name[48];
    snprintf(name, sizeof(name), "cleanup-matrix-%u", serial);
    int primary_role = shift % INDEXES;
    rdb_pool_t *pool = rdb_register_um_pool(name, INDEXES,
            role_offset(primary_role), RDB_BTREE | role_type(primary_role), NULL, 0);
    CHECK(pool != NULL);
    if (!pool)
        return NULL;
    for (int index = 1; index < INDEXES; ++index) {
        int role = (index + shift) % INDEXES;
        CHECK(rdb_register_um_idx(pool, index, role_offset(role),
                    RDB_BTREE | role_type(role), NULL, 0) == index);
    }
    return pool;
}

static void check_keys_clear(record_t *record)
{
    for (int key = 0; key < STRING_KEYS; ++key)
        CHECK(record->key[key] == NULL);
}

static void cleanup_record(void *data, void *opaque)
{
    record_t *record = data;
    cleanup_data_t *cleanup = opaque;
    CHECK(record->id < ITEMS);
    if (record->id < ITEMS) {
        CHECK((cleanup->seen & (1U << record->id)) == 0);
        cleanup->seen |= 1U << record->id;
    }
    ++cleanup->cleaned;
    check_keys_clear(record);
    free(record);
}

static int delete_selected(void *data, void *opaque)
{
    record_t *record = data;
    cleanup_data_t *cleanup = opaque;
    if (record->id != 0)
        return RDB_CB_OK;
    ++cleanup->deleted;
    return RDB_CB_DELETE_NODE;
}

static int delete_all(void *data, void *opaque)
{
    (void)data;
    (void)opaque;
    return RDB_CB_DELETE_NODE;
}

static void check_live_keys(record_t *record, int pattern)
{
    static const char *texts[ITEMS][STRING_KEYS] = {
        {"alpha", "charlie", "echo"},
        {"bravo", "delta", "foxtrot"}
    };
    CHECK(record->key[0] != NULL && record->key[1] != NULL && record->key[2] != NULL);
    for (int key = 0; key < STRING_KEYS; ++key) {
        int origin = key;
        if (pattern == ALIAS_ALL || pattern == EQUAL_STRINGS)
            origin = 0;
        else if ((pattern == ALIAS_01 && key == 1) ||
                (pattern == ALIAS_02 && key == 2))
            origin = 0;
        else if (pattern == ALIAS_12 && key == 2)
            origin = 1;
        CHECK(strcmp(record->key[key], texts[record->id][origin]) == 0);
    }
    if (pattern == ALIAS_ALL)
        CHECK(record->key[0] == record->key[1] && record->key[1] == record->key[2]);
    else if (pattern == ALIAS_01)
        CHECK(record->key[0] == record->key[1] && record->key[1] != record->key[2]);
    else if (pattern == ALIAS_02)
        CHECK(record->key[0] == record->key[2] && record->key[0] != record->key[1]);
    else if (pattern == ALIAS_12)
        CHECK(record->key[1] == record->key[2] && record->key[0] != record->key[1]);
    else if (pattern == EQUAL_STRINGS)
        CHECK(record->key[0] != record->key[1] && record->key[1] != record->key[2] &&
                record->key[0] != record->key[2]);
}

static void check_empty(rdb_pool_t *pool)
{
    CHECK(pool->record_count == 0);
    for (int index = 0; index < INDEXES; ++index) {
        CHECK(pool->root[index] == NULL);
        CHECK(pool->tail[index] == NULL);
    }
}

static void run_case(unsigned serial, int shift, int pattern, int operation,
        int selected_index)
{
    rdb_pool_t *pool = make_pool(serial, shift);
    record_t *records[ITEMS] = {NULL};
    cleanup_data_t cleanup = {0};
    if (!pool)
        return;
    for (unsigned id = 0; id < ITEMS; ++id) {
        records[id] = new_record(id, pattern);
        CHECK(records[id] != NULL);
        if (records[id])
            CHECK(rdb_insert(pool, records[id]) == INDEXES);
    }

    if (!records[0] || !records[1]) {
        rdb_flush(pool, cleanup_record, &cleanup);
        rdb_drop_pool(pool);
        return;
    }

    if (operation == FLUSH_DEFAULT) {
        rdb_flush(pool, NULL, NULL);
        records[0] = records[1] = NULL;
    } else if (operation == FLUSH_CALLBACK) {
        rdb_flush(pool, cleanup_record, &cleanup);
        records[0] = records[1] = NULL;
        CHECK(cleanup.cleaned == ITEMS);
        CHECK(cleanup.seen == ((1U << ITEMS) - 1));
    } else if (operation == ITERATE_ALL) {
        rdb_iterate(pool, selected_index, delete_all, NULL, cleanup_record, &cleanup);
        records[0] = records[1] = NULL;
        CHECK(cleanup.cleaned == ITEMS);
    } else if (operation == ITERATE_ONE) {
        rdb_iterate(pool, selected_index, delete_selected, &cleanup,
                cleanup_record, &cleanup);
        CHECK(cleanup.deleted == 1 && cleanup.cleaned == 1);
        records[0] = NULL;
        check_live_keys(records[1], pattern);
        CHECK(rdb_get(pool, selected_index,
                    (role_type((selected_index + shift) % INDEXES) == RDB_KPSTR)
                    ? records[1]->key[(selected_index + shift) % INDEXES] : NULL)
                == records[1]);
        rdb_flush(pool, cleanup_record, &cleanup);
        records[1] = NULL;
        CHECK(cleanup.cleaned == ITEMS);
    } else {
        int key_role = (selected_index + shift) % INDEXES;
        CHECK(key_role < STRING_KEYS);
        char *query = records[0]->key[key_role];
        CHECK(rdb_delete(pool, selected_index, query) == records[0]);
        check_live_keys(records[0], pattern);
        free_unique_keys(records[0]);
        free(records[0]);
        records[0] = NULL;
        rdb_flush(pool, cleanup_record, &cleanup);
        records[1] = NULL;
        CHECK(cleanup.cleaned == 1);
    }

    check_empty(pool);
    rdb_drop_pool(pool);
    for (int id = 0; id < ITEMS; ++id)
        free(records[id]);
}

int main(void)
{
    unsigned serial = 0;
    rdb_init();
    for (int shift = 0; shift < INDEXES; ++shift)
        for (int pattern = 0; pattern < PATTERNS; ++pattern) {
            run_case(serial++, shift, pattern, FLUSH_DEFAULT, 0);
            run_case(serial++, shift, pattern, FLUSH_CALLBACK, 0);
            for (int index = 0; index < INDEXES; ++index) {
                run_case(serial++, shift, pattern, ITERATE_ALL, index);
                run_case(serial++, shift, pattern, ITERATE_ONE, index);
            }
            run_case(serial++, shift, pattern, DELETE_ONE,
                    (INDEXES - shift) % INDEXES);
        }
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
