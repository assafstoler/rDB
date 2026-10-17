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

typedef union {
    int8_t i8;
    uint8_t u8;
    int16_t i16;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    size_t size;
    ssize_t ssize;
    void *pointer;
#ifdef USE_128_BIT_TYPES
    __int128_t i128;
    __uint128_t u128;
#endif
} query_key_t;

typedef struct {
    rdb_bpp_t links[2];
    query_key_t key;
    uint32_t id;
} record_t;

typedef void (*key_setter_t)(query_key_t *, __intmax_t);

typedef struct {
    uint32_t flag;
    size_t size;
    key_setter_t set;
    __intmax_t aliases[2];
} key_kind_t;

static int compare_wide_value(void *old_key, void *new_key);

#define KEY_SETTER(member, type) \
    static void set_##member(query_key_t *key, __intmax_t value) \
    { key->member = (type)value; }

KEY_SETTER(i8, int8_t)
KEY_SETTER(u8, uint8_t)
KEY_SETTER(i16, int16_t)
KEY_SETTER(u16, uint16_t)
KEY_SETTER(i32, int32_t)
KEY_SETTER(u32, uint32_t)
KEY_SETTER(i64, int64_t)
KEY_SETTER(u64, uint64_t)
KEY_SETTER(size, size_t)
KEY_SETTER(ssize, ssize_t)
#ifdef USE_128_BIT_TYPES
KEY_SETTER(i128, __int128_t)
KEY_SETTER(u128, __uint128_t)
#endif

static const key_kind_t key_kinds[] = {
    {RDB_KINT8, sizeof(int8_t), set_i8, {257, -255}},
    {RDB_KUINT8, sizeof(uint8_t), set_u8, {257, -255}},
    {RDB_KINT16, sizeof(int16_t), set_i16, {65537, -65535}},
    {RDB_KUINT16, sizeof(uint16_t), set_u16, {65537, -65535}},
    {RDB_KINT32, sizeof(int32_t), set_i32, {INT64_C(4294967297), -INT64_C(4294967295)}},
    {RDB_KUINT32, sizeof(uint32_t), set_u32, {INT64_C(4294967297), -INT64_C(4294967295)}},
    {RDB_KINT64, sizeof(int64_t), set_i64, {0, 0}},
    {RDB_KUINT64, sizeof(uint64_t), set_u64, {0, 0}},
    {RDB_KSIZE_t, sizeof(size_t), set_size, {0, 0}},
    {RDB_KSSIZE_t, sizeof(ssize_t), set_ssize, {0, 0}},
#ifdef USE_128_BIT_TYPES
    {RDB_KINT128, sizeof(__int128_t), set_i128, {0, 0}},
    {RDB_KUINT128, sizeof(__uint128_t), set_u128, {0, 0}},
#endif
};

enum { GET_VALUE, DELETE_VALUE, MOVE_VALUE };

static int key_offset(void)
{
    return (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t) * 2);
}

static int id_offset(void)
{
    return (int)(offsetof(record_t, id) - sizeof(rdb_bpp_t) * 2);
}

static void keep_record(void *data, void *unused)
{
    (void)data;
    (void)unused;
}

static rdb_pool_t *make_pool(const char *name, int key_index, uint32_t key_flag)
{
    int primary_offset = key_index == 0 ? key_offset() : id_offset();
    uint32_t primary_type = key_index == 0 ? key_flag : RDB_KUINT32;
    int secondary_offset = key_index == 1 ? key_offset() : id_offset();
    uint32_t secondary_type = key_index == 1 ? key_flag : RDB_KUINT32;
    void *compare = key_flag == RDB_KCF ? (void *)compare_wide_value : NULL;
    if (key_index == 0 && (key_flag & RDB_NOKEYS)) primary_offset = 0;
    if (key_index == 1 && (key_flag & RDB_NOKEYS)) secondary_offset = 0;
    rdb_pool_t *pool = rdb_register_um_pool((char *)name, 2, primary_offset,
            RDB_BTREE | primary_type, key_index == 0 ? compare : NULL, 0);
    CHECK(pool != NULL);
    if (!pool)
        return NULL;
    CHECK(rdb_register_um_idx(pool, 1, secondary_offset,
                RDB_BTREE | secondary_type,
                key_index == 1 ? compare : NULL, 0) == 1);
    return pool;
}

static void *query_value(int operation, rdb_pool_t *source,
        rdb_pool_t *destination, int index, __intmax_t value)
{
    if (operation == GET_VALUE)
        return rdb_get_const(source, index, value);
    if (operation == DELETE_VALUE)
        return rdb_delete_const(source, index, value);
    return rdb_move_const(destination, source, index, value);
}

static void check_numeric_operation(const key_kind_t *kind, int operation,
        int key_index, unsigned serial)
{
    char source_name[48], destination_name[48];
    snprintf(source_name, sizeof(source_name), "const-matrix-source-%u", serial);
    snprintf(destination_name, sizeof(destination_name), "const-matrix-destination-%u", serial);
    rdb_pool_t *source = make_pool(source_name, key_index, kind->flag);
    rdb_pool_t *destination = make_pool(destination_name, key_index, kind->flag);
    record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!source || !destination || !record)
        goto cleanup;

    kind->set(&record->key, 1);
    record->id = 42;
    CHECK(rdb_insert(source, record) == 2);

    if (kind->size < sizeof(__intmax_t)) {
        for (size_t i = 0; i < 2; ++i) {
            CHECK(query_value(operation, source, destination, key_index,
                        kind->aliases[i]) == NULL);
            CHECK(source->record_count == 1);
            CHECK(destination->record_count == 0);
            CHECK(rdb_get(source, key_index, &record->key) == record);
            CHECK(rdb_get_const(source, 1 - key_index, 42) == record);
        }
    }

    CHECK(query_value(operation, source, destination, key_index, 1) == record);
    if (operation == GET_VALUE) {
        CHECK(source->record_count == 1 && destination->record_count == 0);
        CHECK(rdb_get_const(source, 1 - key_index, 42) == record);
    } else if (operation == DELETE_VALUE) {
        CHECK(source->record_count == 0 && destination->record_count == 0);
        free(record);
        record = NULL;
    } else {
        CHECK(source->record_count == 0 && destination->record_count == 1);
        CHECK(rdb_get(destination, key_index, &record->key) == record);
        CHECK(rdb_get_const(destination, 1 - key_index, 42) == record);
    }

cleanup:
    if (source) {
        rdb_flush(source, keep_record, NULL);
        rdb_drop_pool(source);
    }
    if (destination) {
        rdb_flush(destination, keep_record, NULL);
        rdb_drop_pool(destination);
    }
    free(record);
}

static void numeric_widths(void)
{
    unsigned serial = 0;
    for (size_t kind = 0; kind < sizeof(key_kinds) / sizeof(key_kinds[0]); ++kind)
        for (int operation = GET_VALUE; operation <= MOVE_VALUE; ++operation)
            for (int key_index = 0; key_index < 2; ++key_index)
                check_numeric_operation(&key_kinds[kind], operation,
                        key_index, serial++);
}

static int compare_wide_value(void *old_key, void *new_key)
{
    int32_t old_value = *(int32_t *)old_key;
    __intmax_t new_value = *(__intmax_t *)new_key;
    return (new_value > old_value) - (new_value < old_value);
}

static void pointer_and_custom_keys(void)
{
    static int pointed_value;
    unsigned serial = 0;
    for (int kind = 0; kind < 2; ++kind)
        for (int operation = GET_VALUE; operation <= MOVE_VALUE; ++operation)
            for (int key_index = 0; key_index < 2; ++key_index) {
                uint32_t flag = kind == 0 ? RDB_KPTR : RDB_KCF;
                __intmax_t value = kind == 0
                    ? (__intmax_t)(uintptr_t)&pointed_value : 257;
                char source_name[48], destination_name[48];
                snprintf(source_name, sizeof(source_name), "const-special-source-%u", serial);
                snprintf(destination_name, sizeof(destination_name), "const-special-destination-%u", serial++);
                rdb_pool_t *source = make_pool(source_name, key_index, flag);
                rdb_pool_t *destination = make_pool(destination_name, key_index, flag);
                record_t *record = calloc(1, sizeof(*record));
                CHECK(record != NULL);
                if (!source || !destination || !record) {
                    if (source) rdb_drop_pool(source);
                    if (destination) rdb_drop_pool(destination);
                    free(record);
                    continue;
                }
                if (kind == 0)
                    record->key.pointer = &pointed_value;
                else
                    record->key.i32 = 257;
                record->id = 42;
                CHECK(rdb_insert(source, record) == 2);
                CHECK(query_value(operation, source, destination, key_index, value) == record);
                CHECK(source->record_count == (operation == GET_VALUE ? 1u : 0u));
                CHECK(destination->record_count == (operation == MOVE_VALUE ? 1u : 0u));
                if (operation != DELETE_VALUE) {
                    rdb_pool_t *owner = operation == MOVE_VALUE ? destination : source;
                    CHECK(rdb_get_const(owner, 1 - key_index, 42) == record);
                }
                rdb_flush(source, keep_record, NULL);
                rdb_flush(destination, keep_record, NULL);
                rdb_drop_pool(source);
                rdb_drop_pool(destination);
                free(record);
            }
}

static void queue_key_queries(void)
{
    unsigned serial = 0;
    for (int queue = RDB_KFIFO; queue <= RDB_KLIFO; ++queue)
        for (int operation = GET_VALUE; operation <= MOVE_VALUE; ++operation)
            for (int key_index = 0; key_index < 2; ++key_index) {
                char source_name[48], destination_name[48];
                snprintf(source_name, sizeof(source_name), "const-queue-source-%u", serial);
                snprintf(destination_name, sizeof(destination_name), "const-queue-destination-%u", serial++);
                rdb_pool_t *source = make_pool(source_name, key_index, queue);
                rdb_pool_t *destination = make_pool(destination_name, key_index, queue);
                record_t *records[3] = {NULL};
                if (!source || !destination) {
                    if (source) rdb_drop_pool(source);
                    if (destination) rdb_drop_pool(destination);
                    continue;
                }
                for (unsigned id = 0; id < 3; ++id) {
                    records[id] = calloc(1, sizeof(*records[id]));
                    CHECK(records[id] != NULL);
                    if (!records[id]) continue;
                    records[id]->id = id + 1;
                    CHECK(rdb_insert(source, records[id]) == 2);
                }
                if (!records[0] || !records[1] || !records[2]) {
                    rdb_flush(source, keep_record, NULL);
                    rdb_drop_pool(source);
                    rdb_drop_pool(destination);
                    for (int id = 0; id < 3; ++id) free(records[id]);
                    continue;
                }

                for (unsigned step = 0; step < 3; ++step) {
                    unsigned id = queue == RDB_KFIFO ? step : 2 - step;
                    CHECK(query_value(operation, source, destination, key_index,
                                (step == 1) ? INT64_MAX : -1) == records[id]);
                    CHECK(source->record_count ==
                            (operation == GET_VALUE ? 3u : 2u - step));
                    CHECK(destination->record_count ==
                            (operation == MOVE_VALUE ? step + 1 : 0u));
                    if (operation == GET_VALUE)
                        break;
                }
                rdb_flush(source, keep_record, NULL);
                rdb_flush(destination, keep_record, NULL);
                rdb_drop_pool(source);
                rdb_drop_pool(destination);
                for (int id = 0; id < 3; ++id) free(records[id]);
            }
}

static void unsigned_full_width_queries(void)
{
    const uint32_t flags[] = {
        RDB_KUINT64, RDB_KSIZE_t
#ifdef USE_128_BIT_TYPES
        , RDB_KUINT128
#endif
    };
    unsigned serial = 10000;
    for (size_t kind = 0; kind < sizeof(flags) / sizeof(flags[0]); ++kind)
        for (int operation = GET_VALUE; operation <= MOVE_VALUE; ++operation)
            for (int key_index = 0; key_index < 2; ++key_index) {
                char source_name[48], destination_name[48];
                snprintf(source_name, sizeof(source_name), "const-full-source-%u", serial);
                snprintf(destination_name, sizeof(destination_name), "const-full-destination-%u", serial++);
                rdb_pool_t *source = make_pool(source_name, key_index, flags[kind]);
                rdb_pool_t *destination = make_pool(destination_name, key_index, flags[kind]);
                record_t *record = calloc(1, sizeof(*record));
                CHECK(record != NULL);
                if (!source || !destination || !record) {
                    if (source) rdb_drop_pool(source);
                    if (destination) rdb_drop_pool(destination);
                    free(record);
                    continue;
                }
                __intmax_t query = -1;
                int negative_query_matches = 0;
                if (flags[kind] == RDB_KUINT64)
                    record->key.u64 = UINT64_MAX;
                else if (flags[kind] == RDB_KSIZE_t) {
                    record->key.size = SIZE_MAX;
                }
#ifdef USE_128_BIT_TYPES
                else
                    record->key.u128 = ~(__uint128_t)0;
#endif
                __intmax_t maximum_query;
                if (flags[kind] == RDB_KUINT64) {
                    maximum_query = (__intmax_t)UINT64_MAX;
                    negative_query_matches = sizeof(__intmax_t) == sizeof(uint64_t);
                } else if (flags[kind] == RDB_KSIZE_t) {
                    maximum_query = (__intmax_t)SIZE_MAX;
                    negative_query_matches = sizeof(__intmax_t) == sizeof(size_t);
                }
#ifdef USE_128_BIT_TYPES
                else {
                    maximum_query = -1;
                    negative_query_matches = 1;
                }
#endif
                record->id = 42;
                CHECK(rdb_insert(source, record) == 2);
                CHECK(query_value(operation, source, destination,
                            key_index, query) ==
                        (negative_query_matches ? record : NULL));
                CHECK(source->record_count ==
                        (negative_query_matches && operation != GET_VALUE ? 0u : 1u));
                CHECK(destination->record_count ==
                        (negative_query_matches && operation == MOVE_VALUE ? 1u : 0u));

                rdb_flush(source, keep_record, NULL);
                rdb_flush(destination, keep_record, NULL);
                if (flags[kind] == RDB_KUINT64)
                    record->key.u64 = UINT64_MAX;
                else if (flags[kind] == RDB_KSIZE_t)
                    record->key.size = SIZE_MAX;
#ifdef USE_128_BIT_TYPES
                else
                    record->key.u128 = ~(__uint128_t)0;
#endif
                CHECK(rdb_insert(source, record) == 2);
                CHECK(query_value(operation, source, destination,
                            key_index, maximum_query) == record);
                CHECK(source->record_count == (operation == GET_VALUE ? 1u : 0u));
                CHECK(destination->record_count == (operation == MOVE_VALUE ? 1u : 0u));
                rdb_flush(source, keep_record, NULL);
                rdb_flush(destination, keep_record, NULL);
                rdb_drop_pool(source);
                rdb_drop_pool(destination);
                free(record);
            }
}

int main(void)
{
    rdb_init();
    numeric_widths();
    pointer_and_custom_keys();
    queue_key_queries();
    unsigned_full_width_queries();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
