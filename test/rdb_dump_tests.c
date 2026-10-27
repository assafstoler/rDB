/* Exact diagnostic output, with literal decimal oracles independent of rDB. */
#include "rdb_test_support.h"
#include <unistd.h>

enum { INDEXES = 3 };
typedef union {
    int64_t i64;
    uint64_t u64;
#ifdef USE_128_BIT_TYPES
    __int128_t i128;
    __uint128_t u128;
#endif
} numeric_key;
typedef struct {
    rdb_bpp_t links[INDEXES];
    numeric_key key;
    uint32_t id;
} item;
struct sample { numeric_key key; const char *text; };
#define VALUE(field, number, text) {{.field = (number)}, (text)}

/* Capture stdout only within this single-threaded test process. Compare every
 * byte, including separators and lack of an implicit trailing newline. */
static void check_dump(rdb_pool_t *pool, int index, const struct sample *values,
                        size_t count, char *separator)
{
    size_t length = 0, separator_length = strlen(separator);
    for (size_t n = 0; n < count; ++n)
        length += strlen(values[n].text) + separator_length;
    char *expected = checked_calloc(length + 1, 1);
    char *cursor = expected;
    for (size_t n = 0; n < count; ++n) {
        size_t digits = strlen(values[n].text);
        memcpy(cursor, values[n].text, digits);
        cursor += digits;
        memcpy(cursor, separator, separator_length);
        cursor += separator_length;
    }
    unsigned char before[sizeof(*pool)];
    memcpy(before, pool, sizeof(before));
    FILE *output = tmpfile();
    CHECK(output);
    CHECK(fflush(stdout) == 0);
    int saved = dup(STDOUT_FILENO);
    CHECK(saved >= 0);
    CHECK(dup2(fileno(output), STDOUT_FILENO) == STDOUT_FILENO);
    rdb_dump(pool, index, separator);
    CHECK(fflush(stdout) == 0);
    CHECK(dup2(saved, STDOUT_FILENO) == STDOUT_FILENO);
    CHECK(close(saved) == 0);
    rewind(output);
    char *actual = checked_calloc(length + 2, 1);
    size_t received = fread(actual, 1, length + 1, output);
    CHECK(!ferror(output));
    CHECK(fclose(output) == 0);
    if (received != length || strcmp(actual, expected) != 0)
        fprintf(stderr, "dump mismatch (index %d): expected [%s], got [%s]\n",
                index, expected, actual);
    CHECK(received == length && strcmp(actual, expected) == 0);
    CHECK(memcmp(before, pool, sizeof(before)) == 0);
    free(actual);
    free(expected);
}

static item *insert(rdb_pool_t *pool, const struct sample *sample, uint32_t id)
{
    item *record = checked_calloc(1, sizeof(*record));
    record->key = sample->key;
    record->id = id;
    CHECK(rdb_insert(pool, record) == INDEXES);
    return record;
}

static void exercise(int flag, const struct sample *values, size_t count)
{
    char *separators[] = {"", ",", "\n", " | "};
    for (int index = 0; index < INDEXES; ++index) {
        rdb_pool_t *pool = rdb_register_um_pool("dump", INDEXES,
                index == 0 ? KEY_OFFSET(item, key, INDEXES) : KEY_OFFSET(item, id, INDEXES),
                RDB_BTREE | (index == 0 ? flag : RDB_KUINT32), NULL, 0);
        CHECK(pool);
        for (int slot = 1; slot < INDEXES; ++slot)
            CHECK(rdb_register_um_idx(pool, slot,
                    slot == index ? KEY_OFFSET(item, key, INDEXES) : KEY_OFFSET(item, id, INDEXES),
                    RDB_BTREE | (slot == index ? flag : RDB_KUINT32), NULL, 0) == slot);
        check_dump(pool, index, values, 0, ",");
        /* Each boundary must also work alone, including zero and signed min. */
        for (size_t n = 0; n < count; ++n) {
            insert(pool, &values[n], (uint32_t)n);
            check_dump(pool, index, &values[n], 1, ",");
            rdb_flush(pool, NULL, NULL);
        }
        /* Reverse insertion forces traversal rather than insertion-order output. */
        item **records = checked_calloc(count, sizeof(*records));
        for (size_t n = count; n > 0; --n)
            records[n - 1] = insert(pool, &values[n - 1], (uint32_t)n - 1);
        for (size_t s = 0; s < ARRAY_COUNT(separators); ++s)
            check_dump(pool, index, values, count, separators[s]);
        CHECK(pool->record_count == count);
        for (size_t n = 0; n < count; ++n) {
            for (int slot = 0; slot < INDEXES; ++slot) {
                const void *key = slot == index ? (const void *)&values[n].key : &records[n]->id;
                CHECK(rdb_get(pool, slot, key) == records[n]);
            }
        }
        free(records);
        rdb_flush(pool, NULL, NULL);
        check_dump(pool, index, values, 0, "\n");
        rdb_drop_pool(pool);
    }
}

/* These records use the key type's natural alignment, not numeric_key's wider
 * alignment. The second array element also exercises narrower offsets. */
static void free_natural_record(void *record, void *unused)
{
    (void)unused;
    free(record); /* rDB has already freed and cleared owned pointer strings. */
}

static void check_natural_key(int flag, size_t record_size, size_t offset,
        const void *key, size_t key_size, const void *query, const char *text)
{
    rdb_pool_t *pool = rdb_register_um_pool("natural-dump", INDEXES,
            (int)(offset - sizeof(rdb_bpp_t) * INDEXES), RDB_BTREE | flag,
            NULL, flag == RDB_KSTR ? key_size : 0);
    CHECK(pool);
    for (int index = 1; index < INDEXES; ++index)
        CHECK(rdb_register_um_idx(pool, index,
                (int)(offset - sizeof(rdb_bpp_t) * INDEXES), RDB_BTREE | flag,
                NULL, flag == RDB_KSTR ? key_size : 0) == index);
    unsigned char *record = checked_calloc(1, record_size);
    memcpy(record + offset, key, key_size);
    if (flag == RDB_KPSTR) {
        char *owned = strdup(*(char *const *)key);
        CHECK(owned);
        memcpy(record + offset, &owned, sizeof(owned));
    }
    CHECK(rdb_insert(pool, record) == INDEXES);
    unsigned char *before = checked_calloc(1, record_size);
    memcpy(before, record, record_size);
    const struct sample expected = {{0}, text};
    for (int index = 0; index < INDEXES; ++index) {
        check_dump(pool, index, &expected, 1, ",");
        CHECK(rdb_get(pool, index, query) == record);
        CHECK(memcmp(record, before, record_size) == 0);
    }
    free(before);
    rdb_flush(pool, free_natural_record, NULL);
    rdb_drop_pool(pool);
}

#define CHECK_NATURAL_NUMBER(type, flag, number, text) do { \
    typedef struct { rdb_bpp_t links[INDEXES]; type keys[2]; } record; \
    type key = (number); \
    for (size_t n = 0; n < 2; ++n) \
        check_natural_key(flag, sizeof(record), offsetof(record, keys) + n * sizeof(type), \
                &key, sizeof(key), &key, text); \
} while (0)

static void natural_numeric(void)
{
    CHECK_NATURAL_NUMBER(int64_t, RDB_KINT64, INT64_MIN, "-9223372036854775808");
    CHECK_NATURAL_NUMBER(uint64_t, RDB_KUINT64, UINT64_MAX, "18446744073709551615");
    CHECK_NATURAL_NUMBER(int32_t, RDB_KINT32, INT32_MIN, "-2147483648");
    CHECK_NATURAL_NUMBER(uint32_t, RDB_KUINT32, UINT32_MAX, "4294967295");
    CHECK_NATURAL_NUMBER(int16_t, RDB_KINT16, INT16_MIN, "-32768");
    CHECK_NATURAL_NUMBER(uint16_t, RDB_KUINT16, UINT16_MAX, "65535");
    CHECK_NATURAL_NUMBER(int8_t, RDB_KINT8, INT8_MIN, "-128");
    CHECK_NATURAL_NUMBER(uint8_t, RDB_KUINT8, UINT8_MAX, "255");
    CHECK_NATURAL_NUMBER(ssize_t, RDB_KSSIZE_t, -123, "-123");
    CHECK_NATURAL_NUMBER(size_t, RDB_KSIZE_t, 123, "123");
}

static void natural_strings(void)
{
    typedef struct { rdb_bpp_t links[INDEXES]; char keys[2][7]; } inline_record;
    typedef struct { rdb_bpp_t links[INDEXES]; char *keys[2]; } pointer_record;
    char *texts[] = {"", "hello"};
    for (size_t n = 0; n < ARRAY_COUNT(texts); ++n) {
        char inline_key[7] = {0};
        strcpy(inline_key, texts[n]);
        for (size_t field = 0; field < 2; ++field) {
            check_natural_key(RDB_KSTR, sizeof(inline_record),
                    offsetof(inline_record, keys) + field * sizeof(inline_key),
                    inline_key, sizeof(inline_key), inline_key, inline_key);
            check_natural_key(RDB_KPSTR, sizeof(pointer_record),
                    offsetof(pointer_record, keys) + field * sizeof(char *),
                    &texts[n], sizeof(texts[n]), texts[n], texts[n]);
        }
    }
}

static void natural_pointer(void)
{
    typedef struct { rdb_bpp_t links[INDEXES]; void *keys[2]; } pointer_record;
    int target;
    void *keys[] = {NULL, &target};
    for (size_t n = 0; n < ARRAY_COUNT(keys); ++n) {
        char text[64];
        int length = snprintf(text, sizeof(text), "%p", keys[n]);
        CHECK(length > 0 && (size_t)length < sizeof(text));
        for (size_t field = 0; field < 2; ++field)
            check_natural_key(RDB_KPTR, sizeof(pointer_record),
                    offsetof(pointer_record, keys) + field * sizeof(void *),
                    &keys[n], sizeof(keys[n]), &keys[n], text);
    }
}


typedef struct {
    rdb_bpp_t links[INDEXES];
    int32_t key[2];
    uint32_t id;
} custom_item;

static int compare_custom(void *old_key, void *new_key)
{
    const int32_t *a = old_key, *b = new_key;
    if (b[0] != a[0]) return (b[0] > a[0]) ? 1 : -1;
    return (b[1] > a[1]) - (b[1] < a[1]);
}

static void custom(void)
{
    const int32_t keys[][2] = {{2, 1}, {1, 3}, {3, 0}, {1, -2}, {2, 0}};
    const unsigned order[] = {3, 1, 4, 0, 2}; /* Independent lexicographic oracle. */
    char *separators[] = {"", ",", "\n", " | "};
    for (int index = 0; index < INDEXES; ++index) {
        rdb_pool_t *pool = rdb_register_um_pool("custom-dump", INDEXES,
                index == 0 ? KEY_OFFSET(custom_item, key, INDEXES)
                           : KEY_OFFSET(custom_item, id, INDEXES),
                RDB_BTREE | (index == 0 ? RDB_KCF : RDB_KUINT32),
                index == 0 ? compare_custom : NULL, 0);
        CHECK(pool);
        for (int slot = 1; slot < INDEXES; ++slot)
            CHECK(rdb_register_um_idx(pool, slot,
                    slot == index ? KEY_OFFSET(custom_item, key, INDEXES)
                                  : KEY_OFFSET(custom_item, id, INDEXES),
                    RDB_BTREE | (slot == index ? RDB_KCF : RDB_KUINT32),
                    slot == index ? compare_custom : NULL, 0) == slot);
        check_dump(pool, index, NULL, 0, ",");
        custom_item *records[ARRAY_COUNT(keys)], before[ARRAY_COUNT(keys)];
        char addresses[ARRAY_COUNT(keys)][64];
        struct sample expected[ARRAY_COUNT(keys)] = {0};
        for (size_t n = 0; n < ARRAY_COUNT(keys); ++n) {
            records[n] = checked_calloc(1, sizeof(custom_item));
            memcpy(records[n]->key, keys[n], sizeof(keys[n]));
            records[n]->id = (uint32_t)n;
            CHECK(rdb_insert(pool, records[n]) == INDEXES);
            int size = snprintf(addresses[n], sizeof(addresses[n]), "%p",
                    (void *)records[n]->key);
            CHECK(size > 0 && (size_t)size < sizeof(addresses[n]));
            if (n == 0) {
                const struct sample one = {{0}, addresses[n]};
                check_dump(pool, index, &one, 1, ",");
            }
        }
        for (size_t n = 0; n < ARRAY_COUNT(keys); ++n) {
            expected[n].text = addresses[order[n]];
            memcpy(&before[n], records[n], sizeof(custom_item));
        }
        for (size_t s = 0; s < ARRAY_COUNT(separators); ++s)
            check_dump(pool, index, expected, ARRAY_COUNT(expected), separators[s]);
        for (size_t n = 0; n < ARRAY_COUNT(keys); ++n) {
            CHECK(memcmp(&before[n], records[n], sizeof(custom_item)) == 0);
            for (int slot = 0; slot < INDEXES; ++slot)
                CHECK(rdb_get(pool, slot, slot == index ? (const void *)keys[n]
                                                      : &records[n]->id) == records[n]);
        }
        rdb_flush(pool, NULL, NULL);
        check_dump(pool, index, NULL, 0, "\n");
        rdb_drop_pool(pool);
    }
}

static void signed64(void)
{
    const struct sample values[] = {
        VALUE(i64, INT64_MIN, "-9223372036854775808"),
        VALUE(i64, INT64_MIN + 1, "-9223372036854775807"),
        VALUE(i64, -100, "-100"), VALUE(i64, -1, "-1"), VALUE(i64, 0, "0"),
        VALUE(i64, 1, "1"), VALUE(i64, 100, "100"),
        VALUE(i64, INT64_MAX - 1, "9223372036854775806"),
        VALUE(i64, INT64_MAX, "9223372036854775807")
    };
    exercise(RDB_KINT64, values, ARRAY_COUNT(values));
}

static void unsigned64(void)
{
    const struct sample values[] = {
        VALUE(u64, 0, "0"), VALUE(u64, 1, "1"), VALUE(u64, 10, "10"),
        VALUE(u64, 100, "100"), VALUE(u64, INT64_MAX, "9223372036854775807"),
        VALUE(u64, (uint64_t)INT64_MAX + 1, "9223372036854775808"),
        VALUE(u64, UINT64_MAX - 1, "18446744073709551614"),
        VALUE(u64, UINT64_MAX, "18446744073709551615")
    };
    exercise(RDB_KUINT64, values, ARRAY_COUNT(values));
}

#ifdef USE_128_BIT_TYPES
#define SIGNED_128_MAX ((__int128_t)(((__uint128_t)1 << 127) - 1))
static void signed128(void)
{
    const struct sample values[] = {
        VALUE(i128, -SIGNED_128_MAX - 1, "-170141183460469231731687303715884105728"),
        VALUE(i128, -SIGNED_128_MAX, "-170141183460469231731687303715884105727"),
        VALUE(i128, -((__int128_t)1 << 100) - 17, "-1267650600228229401496703205393"),
        VALUE(i128, -((__int128_t)1 << 64) - 1, "-18446744073709551617"),
        VALUE(i128, -((__int128_t)1 << 64), "-18446744073709551616"),
        VALUE(i128, (__int128_t)INT64_MIN - 1, "-9223372036854775809"),
        VALUE(i128, INT64_MIN, "-9223372036854775808"),
        VALUE(i128, -100, "-100"), VALUE(i128, -1, "-1"), VALUE(i128, 0, "0"),
        VALUE(i128, 1, "1"), VALUE(i128, 100, "100"),
        VALUE(i128, INT64_MAX, "9223372036854775807"),
        VALUE(i128, (__int128_t)INT64_MAX + 1, "9223372036854775808"),
        VALUE(i128, (__int128_t)1 << 64, "18446744073709551616"),
        VALUE(i128, ((__int128_t)1 << 100) + 17, "1267650600228229401496703205393"),
        VALUE(i128, SIGNED_128_MAX - 1, "170141183460469231731687303715884105726"),
        VALUE(i128, SIGNED_128_MAX, "170141183460469231731687303715884105727")
    };
    exercise(RDB_KINT128, values, ARRAY_COUNT(values));
}

static void unsigned128(void)
{
    const struct sample values[] = {
        VALUE(u128, 0, "0"), VALUE(u128, 1, "1"), VALUE(u128, 9, "9"),
        VALUE(u128, 10, "10"), VALUE(u128, 99, "99"), VALUE(u128, 100, "100"),
        VALUE(u128, INT64_MAX, "9223372036854775807"),
        VALUE(u128, (__uint128_t)INT64_MAX + 1, "9223372036854775808"),
        VALUE(u128, UINT64_MAX, "18446744073709551615"),
        VALUE(u128, (__uint128_t)1 << 64, "18446744073709551616"),
        VALUE(u128, ((__uint128_t)1 << 64) + 17, "18446744073709551633"),
        VALUE(u128, ((__uint128_t)1 << 100) + 17, "1267650600228229401496703205393"),
        VALUE(u128, ((__uint128_t)1 << 127) - 1, "170141183460469231731687303715884105727"),
        VALUE(u128, (__uint128_t)1 << 127, "170141183460469231731687303715884105728"),
        VALUE(u128, ~(__uint128_t)0 - 1, "340282366920938463463374607431768211454"),
        VALUE(u128, ~(__uint128_t)0, "340282366920938463463374607431768211455")
    };
    exercise(RDB_KUINT128, values, ARRAY_COUNT(values));
}
#endif

int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"signed64", signed64}, {"unsigned64", unsigned64},
        {"natural_numeric", natural_numeric}, {"natural_strings", natural_strings},
        {"natural_pointer", natural_pointer}, {"custom", custom},
#ifdef USE_128_BIT_TYPES
        {"signed128", signed128}, {"unsigned128", unsigned128},
#endif
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
