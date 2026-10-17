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
    union {
        int8_t i8;
        uint8_t u8;
        int32_t i32;
        uint64_t u64;
        void *pointer;
#ifdef USE_128_BIT_TYPES
        __uint128_t u128;
#endif
    } key;
} record_t;

static int compare_custom(void *old_key, void *new_key)
{
    int32_t old_value = *(int32_t *)old_key;
    __intmax_t new_value = *(__intmax_t *)new_key;
    return (new_value > old_value) - (new_value < old_value);
}

static rdb_pool_t *make_pool(char *name, int type, void *compare)
{
    int offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    return rdb_register_um_pool(name, 1, offset, RDB_BTREE | type, compare, 0);
}

static void check_rejected(rdb_pool_t *source, rdb_pool_t *destination,
        record_t *record, __intmax_t value)
{
    CHECK(rdb_get_const(source, 0, value) == NULL);
    CHECK(rdb_delete_const(source, 0, value) == NULL);
    CHECK(rdb_move_const(destination, source, 0, value) == NULL);
    CHECK(source->record_count == 1 && destination->record_count == 0);
    CHECK(rdb_get(source, 0, &record->key) == record);
}

static void unsigned8_range(void)
{
    rdb_pool_t *source = make_pool("const-u8-source", RDB_KUINT8, NULL);
    rdb_pool_t *destination = make_pool("const-u8-destination", RDB_KUINT8, NULL);
    record_t *record = calloc(1, sizeof(*record));
    CHECK(source && destination && record);
    if (!source || !destination || !record) goto done;

    record->key.u8 = 1;
    CHECK(rdb_insert(source, record) == 1);
    check_rejected(source, destination, record, 257);
    check_rejected(source, destination, record, -255);
    CHECK(rdb_move_const(destination, source, 0, 1) == record);
    CHECK(rdb_delete_const(destination, 0, 1) == record);
    CHECK(destination->record_count == 0);

done:
    if (source) rdb_drop_pool(source);
    if (destination) rdb_drop_pool(destination);
    free(record);
}

static void signed8_range(void)
{
    rdb_pool_t *source = make_pool("const-i8-source", RDB_KINT8, NULL);
    rdb_pool_t *destination = make_pool("const-i8-destination", RDB_KINT8, NULL);
    record_t *record = calloc(1, sizeof(*record));
    CHECK(source && destination && record);
    if (!source || !destination || !record) goto done;

    record->key.i8 = -1;
    CHECK(rdb_insert(source, record) == 1);
    check_rejected(source, destination, record, 255);
    CHECK(rdb_get_const(source, 0, -1) == record);
    CHECK(rdb_delete_const(source, 0, -1) == record);

done:
    if (source) rdb_drop_pool(source);
    if (destination) rdb_drop_pool(destination);
    free(record);
}

static void pointer_query(void)
{
    static int pointed_value;
    rdb_pool_t *pool = make_pool("const-pointer", RDB_KPTR, NULL);
    record_t *record = calloc(1, sizeof(*record));
    CHECK(pool && record);
    if (!pool || !record) goto done;

    record->key.pointer = &pointed_value;
    CHECK(rdb_insert(pool, record) == 1);
    CHECK(rdb_get_const(pool, 0, (__intmax_t)(uintptr_t)&pointed_value) == record);
    CHECK(rdb_delete_const(pool, 0, (__intmax_t)(uintptr_t)&pointed_value) == record);

done:
    if (pool) rdb_drop_pool(pool);
    free(record);
}

static void custom_value(void)
{
    rdb_pool_t *pool = make_pool("const-custom", RDB_KCF, compare_custom);
    record_t *record = calloc(1, sizeof(*record));
    CHECK(pool && record);
    if (!pool || !record) goto done;

    record->key.i32 = 257;
    CHECK(rdb_insert(pool, record) == 1);
    CHECK(rdb_get_const(pool, 0, 257) == record);
    CHECK(rdb_delete_const(pool, 0, 257) == record);

done:
    if (pool) rdb_drop_pool(pool);
    free(record);
}

#ifdef USE_128_BIT_TYPES
static void uint128_bit_pattern(void)
{
    rdb_pool_t *pool = make_pool("const-u128", RDB_KUINT128, NULL);
    record_t *record = calloc(1, sizeof(*record));
    CHECK(pool && record);
    if (!pool || !record) goto done;

    record->key.u128 = ~(__uint128_t)0;
    CHECK(rdb_insert(pool, record) == 1);
    CHECK(rdb_get_const(pool, 0, -1) == record);
    CHECK(rdb_delete_const(pool, 0, -1) == record);

done:
    if (pool) rdb_drop_pool(pool);
    free(record);
}
#endif

static void test_constant_comparators(void)
{
    int8_t i8 = 10;
    uint8_t u8 = 10;
    int16_t i16 = 10;
    uint16_t u16 = 10;
    int32_t i32 = 10;
    uint32_t u32 = 10;
    int64_t i64 = 10;
    uint64_t u64 = 10;
    size_t size = 10;
    ssize_t ssize = 10;
    int marker;
    uintptr_t address = (uintptr_t)&marker;

#define CHECK_ORDER(compare, key, low, same, high) do { \
    CHECK((compare)(&(key), (low)) < 0); \
    CHECK((compare)(&(key), (same)) == 0); \
    CHECK((compare)(&(key), (high)) > 0); \
} while (0)

    CHECK_ORDER(key_cmp_const_int8, i8, 9, 10, 11);
    CHECK_ORDER(key_cmp_const_uint8, u8, 9, 10, 11);
    CHECK_ORDER(key_cmp_const_int16, i16, 9, 10, 11);
    CHECK_ORDER(key_cmp_const_uint16, u16, 9, 10, 11);
    CHECK_ORDER(key_cmp_const_int32, i32, 9, 10, 11);
    CHECK_ORDER(key_cmp_const_uint32, u32, 9, 10, 11);
    CHECK_ORDER(key_cmp_const_int64, i64, 9, 10, 11);
    CHECK_ORDER(key_cmp_const_uint64, u64, 9, 10, 11);
    CHECK_ORDER(key_cmp_const_size_t, size, 9, 10, 11);
    CHECK_ORDER(key_cmp_const_ssize_t, ssize, 9, 10, 11);
#undef CHECK_ORDER

    CHECK(key_cmp_const_ptr(&marker, address - 1) < 0);
    CHECK(key_cmp_const_ptr(&marker, address) == 0);
    CHECK(key_cmp_const_ptr(&marker, address + 1) > 0);
#ifdef USE_128_BIT_TYPES
    __int128_t i128 = 10;
    __uint128_t u128 = 10;
    CHECK(key_cmp_const_int128(&i128, 9) < 0);
    CHECK(key_cmp_const_int128(&i128, 10) == 0);
    CHECK(key_cmp_const_int128(&i128, 11) > 0);
    CHECK(key_cmp_const_uint128(&u128, 9) < 0);
    CHECK(key_cmp_const_uint128(&u128, 10) == 0);
    CHECK(key_cmp_const_uint128(&u128, 11) > 0);
#endif
}

int main(void)
{
    rdb_init();
    test_constant_comparators();
    unsigned8_range();
    signed8_range();
    pointer_query();
    custom_value();
#ifdef USE_128_BIT_TYPES
    uint128_bit_pattern();
#endif
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
