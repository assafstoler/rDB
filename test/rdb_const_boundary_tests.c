/* Value-based queries must not alias a different key by narrowing an integer. */
#include "rdb_test_support.h"
#include <limits.h>

typedef struct {
    rdb_bpp_t links[2];
    rdb_key_union key;
    uint32_t id;
} constant_item;

#define KEY_SETTER(member, type) \
static void set_##member(rdb_key_union *key, __intmax_t value) \
{ key->member = (type)value; }
KEY_SETTER(i8, int8_t)
KEY_SETTER(u8, uint8_t)
KEY_SETTER(i16, int16_t)
KEY_SETTER(u16, uint16_t)
KEY_SETTER(i32, int32_t)
KEY_SETTER(u32, uint32_t)
KEY_SETTER(i64, int64_t)
KEY_SETTER(u64, uint64_t)
KEY_SETTER(st, size_t)
KEY_SETTER(sst, ssize_t)
#ifdef USE_128_BIT_TYPES
KEY_SETTER(i128, __int128_t)
KEY_SETTER(u128, __uint128_t)
#endif

struct key_kind {
    int flag;
    size_t width;
    __intmax_t minimum, maximum;
    void (*set)(rdb_key_union *, __intmax_t);
};

static rdb_pool_t *constant_pool(char *name, int index, int flag, void *compare)
{
    int flags[2], offsets[2];
    flags[index] = flag;
    offsets[index] = KEY_OFFSET(constant_item, key, 2);
    flags[1 - index] = RDB_KUINT32;
    offsets[1 - index] = KEY_OFFSET(constant_item, id, 2);
    rdb_pool_t *pool = rdb_register_um_pool(name, 2, offsets[0],
            RDB_BTREE | flags[0], index == 0 ? compare : NULL, 0);
    CHECK(pool);
    CHECK(rdb_register_um_idx(pool, 1, offsets[1],
                RDB_BTREE | flags[1], index == 1 ? compare : NULL, 0) == 1);
    return pool;
}

enum { GET_VALUE, DELETE_VALUE, MOVE_VALUE };
static void *value_query(int operation, rdb_pool_t *source,
                         rdb_pool_t *destination, int index, __intmax_t value)
{
    if (operation == GET_VALUE) return rdb_get_const(source, index, value);
    if (operation == DELETE_VALUE) return rdb_delete_const(source, index, value);
    return rdb_move_const(destination, source, index, value);
}

static void verify_constant_pool(rdb_pool_t *pool, int index,
                                 rdb_key_union keys[4], constant_item *items[4])
{
    size_t count = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        CHECK(rdb_get(pool, index, &keys[i]) == items[i]);
        CHECK(rdb_get(pool, 1 - index, &i) == items[i]);
        if (items[i]) ++count;
    }
    CHECK(pool->record_count == count);
}

static void numeric_ranges(int operation)
{
    const struct key_kind kinds[] = {
        {RDB_KINT8, sizeof(int8_t), INT8_MIN, INT8_MAX, set_i8},
        {RDB_KUINT8, sizeof(uint8_t), 0, UINT8_MAX, set_u8},
        {RDB_KINT16, sizeof(int16_t), INT16_MIN, INT16_MAX, set_i16},
        {RDB_KUINT16, sizeof(uint16_t), 0, UINT16_MAX, set_u16},
        {RDB_KINT32, sizeof(int32_t), INT32_MIN, INT32_MAX, set_i32},
        {RDB_KUINT32, sizeof(uint32_t), 0, UINT32_MAX, set_u32},
        {RDB_KINT64, sizeof(int64_t), INT64_MIN, INT64_MAX, set_i64},
        {RDB_KUINT64, sizeof(uint64_t), 0, (__intmax_t)UINT64_MAX, set_u64},
        {RDB_KSIZE_t, sizeof(size_t), 0, (__intmax_t)SIZE_MAX, set_st},
        {RDB_KSSIZE_t, sizeof(ssize_t), -SSIZE_MAX - 1, SSIZE_MAX, set_sst},
#ifdef USE_128_BIT_TYPES
        {RDB_KINT128, sizeof(__int128_t),
            -(__int128_t)(((__uint128_t)1 << 127) - 1) - 1,
            (__int128_t)(((__uint128_t)1 << 127) - 1), set_i128},
        {RDB_KUINT128, sizeof(__uint128_t), 0, (__int128_t)~(__uint128_t)0, set_u128},
#endif
    };
    for (size_t k = 0; k < ARRAY_COUNT(kinds); ++k) {
        const struct key_kind *kind = &kinds[k];
        for (int index = 0; index < 2; ++index) {
            rdb_pool_t *source = constant_pool("constant-source", index, kind->flag, NULL);
            rdb_pool_t *destination = constant_pool("constant-destination", index, kind->flag, NULL);
            __intmax_t values[] = {
                kind->minimum, kind->minimum + 1, kind->maximum - 1, kind->maximum
            };
            rdb_key_union keys[4] = {0};
            constant_item *src[4], *dst[4] = {0};
            for (uint32_t i = 0; i < 4; ++i) {
                kind->set(&keys[i], values[i]);
                src[i] = checked_calloc(1, sizeof(*src[i]));
                src[i]->key = keys[i];
                src[i]->id = i;
                CHECK(rdb_insert(source, src[i]) == 2);
            }
            /* Only a wider API argument can represent values outside this
             * type's range. These queries used to wrap onto the stored keys. */
            if (kind->width < sizeof(__intmax_t)) {
                __intmax_t invalid[] = {
                    kind->minimum - 1, kind->maximum + 1,
                    kind->minimum - 2, kind->maximum + 2
                };
                for (size_t q = 0; q < ARRAY_COUNT(invalid); ++q) {
                    CHECK(value_query(operation, source, destination, index, invalid[q]) == NULL);
                    verify_constant_pool(source, index, keys, src);
                    verify_constant_pool(destination, index, keys, dst);
                }
            }
            /* Valid limits still work, including full-width unsigned values
             * represented by their bit pattern in the signed API argument. */
            for (uint32_t i = 0; i < 4; ++i) {
                CHECK(value_query(operation, source, destination, index, values[i]) == src[i]);
                if (operation == MOVE_VALUE) dst[i] = src[i];
                if (operation == DELETE_VALUE) free(src[i]);
                if (operation != GET_VALUE) src[i] = NULL;
                verify_constant_pool(source, index, keys, src);
                verify_constant_pool(destination, index, keys, dst);
            }
            rdb_flush(source, NULL, NULL);
            rdb_flush(destination, NULL, NULL);
            rdb_drop_pool(source);
            rdb_drop_pool(destination);
        }
    }
}
static void get_range(void) { numeric_ranges(GET_VALUE); }
static void delete_range(void) { numeric_ranges(DELETE_VALUE); }
static void move_range(void) { numeric_ranges(MOVE_VALUE); }

static int compare_wide_value(const void *old_key, const void *new_key)
{
    __intmax_t old_value, new_value;
    memcpy(&old_value, old_key, sizeof(old_value));
    memcpy(&new_value, new_key, sizeof(new_value));
    return (new_value > old_value) - (new_value < old_value);
}

/* A supplied comparator, even with a built-in type flag, owns the key format.
 * Do not narrow its query or change the address-of-full-value convention. */
static void custom(void)
{
    const int flags[] = {RDB_KCF, RDB_KUINT8};
    for (size_t flag = 0; flag < ARRAY_COUNT(flags); ++flag)
        for (int operation = GET_VALUE; operation <= MOVE_VALUE; ++operation)
            for (int index = 0; index < 2; ++index) {
                rdb_pool_t *source = constant_pool("custom-source", index, flags[flag], compare_wide_value);
                rdb_pool_t *destination = constant_pool("custom-destination", index, flags[flag], compare_wide_value);
                constant_item *item = checked_calloc(1, sizeof(*item));
                __intmax_t value = 257;
                memcpy(&item->key, &value, sizeof(value));
                CHECK(rdb_insert(source, item) == 2);
                CHECK(value_query(operation, source, destination, index, 1) == NULL);
                CHECK(source->record_count == 1 && destination->record_count == 0);
                CHECK(value_query(operation, source, destination, index, value) == item);
                CHECK(source->record_count == (operation == GET_VALUE ? 1u : 0u));
                CHECK(destination->record_count == (operation == MOVE_VALUE ? 1u : 0u));
                if (operation == DELETE_VALUE) free(item);
                rdb_flush(source, NULL, NULL);
                rdb_flush(destination, NULL, NULL);
                rdb_drop_pool(source);
                rdb_drop_pool(destination);
            }
}

static void pointer_range(void)
{
    int backing;
    for (int operation = GET_VALUE; operation <= MOVE_VALUE; ++operation)
        for (int index = 0; index < 2; ++index) {
            rdb_pool_t *source = constant_pool("pointer-source", index, RDB_KPTR, NULL);
            rdb_pool_t *destination = constant_pool("pointer-destination", index, RDB_KPTR, NULL);
            constant_item *item = checked_calloc(1, sizeof(*item));
            item->key.pStr = (char *)&backing;
            CHECK(rdb_insert(source, item) == 2);
            __intmax_t value = (__intmax_t)(uintptr_t)&backing;
            unsigned bits = sizeof(void *) * CHAR_BIT;
            if (bits < sizeof(__intmax_t) * CHAR_BIT) {
                __intmax_t invalid[] = {
                    value + ((__intmax_t)1 << bits), value - ((__intmax_t)1 << bits)
                };
                for (size_t q = 0; q < ARRAY_COUNT(invalid); ++q) {
                    CHECK(value_query(operation, source, destination, index, invalid[q]) == NULL);
                    CHECK(source->record_count == 1 && destination->record_count == 0);
                    CHECK(rdb_get(source, index, &item->key.pStr) == item);
                    CHECK(rdb_get(source, 1 - index, &item->id) == item);
                }
            }
            CHECK(value_query(operation, source, destination, index, value) == item);
            CHECK(source->record_count == (operation == GET_VALUE ? 1u : 0u));
            CHECK(destination->record_count == (operation == MOVE_VALUE ? 1u : 0u));
            if (operation == DELETE_VALUE) free(item);
            rdb_flush(source, NULL, NULL);
            rdb_flush(destination, NULL, NULL);
            rdb_drop_pool(source);
            rdb_drop_pool(destination);
        }
}

int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"get_range", get_range}, {"delete_range", delete_range},
        {"move_range", move_range}, {"custom", custom},
        {"pointer_range", pointer_range}
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
