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

static char *capture_dump(rdb_pool_t *pool, int index)
{
    FILE *capture = tmpfile();
    CHECK(capture != NULL);
    if (!capture) return NULL;
    CHECK(fflush(stdout) == 0);
    int saved_stdout = dup(STDOUT_FILENO);
    CHECK(saved_stdout >= 0);
    if (saved_stdout < 0) {
        fclose(capture);
        return NULL;
    }
    CHECK(dup2(fileno(capture), STDOUT_FILENO) == STDOUT_FILENO);
    rdb_dump(pool, index, ",");
    CHECK(fflush(stdout) == 0);
    CHECK(dup2(saved_stdout, STDOUT_FILENO) == STDOUT_FILENO);
    CHECK(close(saved_stdout) == 0);
    rewind(capture);
    char output[64];
    size_t size = fread(output, 1, sizeof(output) - 1, capture);
    CHECK(!ferror(capture));
    output[size] = '\0';
    CHECK(fclose(capture) == 0);
    char *copy = malloc(size + 1);
    CHECK(copy != NULL);
    if (copy) memcpy(copy, output, size + 1);
    return copy;
}

enum { NATURAL_INDEXES = 3 };
typedef struct {
    rdb_bpp_t links[NATURAL_INDEXES];
    unsigned char key_data[64];
} natural_record_t;

static void free_record(void *record, void *unused)
{
    (void)unused;
    free(record);
}

static void check_natural_value(char *name, int type, size_t capacity,
        size_t key_size, size_t relative_offset, const void *value,
        const void *query, const char *expected)
{
    static unsigned pool_number;
    char pool_name[40];
    snprintf(pool_name, sizeof(pool_name), "%s-%u", name, pool_number++);
    size_t offset = offsetof(natural_record_t, key_data) + relative_offset;
    int key_offset = (int)(offset - sizeof(rdb_bpp_t) * NATURAL_INDEXES);
    rdb_pool_t *pool = rdb_register_um_pool(pool_name, NATURAL_INDEXES,
            key_offset, RDB_BTREE | type, NULL, capacity);
    natural_record_t *record = calloc(1, sizeof(*record));
    CHECK(pool && record);
    if (!pool || !record) {
        if (pool) rdb_drop_pool(pool);
        free(record);
        return;
    }
    for (int index = 1; index < NATURAL_INDEXES; ++index)
        CHECK(rdb_register_um_idx(pool, index, key_offset,
                    RDB_BTREE | type, NULL, capacity) == index);
    memcpy((unsigned char *)record + offset, value, key_size);
    CHECK(rdb_insert(pool, record) == NATURAL_INDEXES);
    natural_record_t before;
    memcpy(&before, record, sizeof(before));
    for (int index = 0; index < NATURAL_INDEXES; ++index) {
        char *actual = capture_dump(pool, index);
        CHECK(actual && strcmp(actual, expected) == 0);
        free(actual);
        CHECK(rdb_get(pool, index, query) == record);
        CHECK(memcmp(&before, record, sizeof(before)) == 0);
    }
    rdb_flush(pool, free_record, NULL);
    rdb_drop_pool(pool);
}

static void natural_numeric(void)
{
    int8_t i8 = -8; uint8_t u8 = 250;
    int16_t i16 = -1234; uint16_t u16 = 54321;
    int32_t i32 = -1234567; uint32_t u32 = 3456789012U;
    int64_t i64 = INT64_MIN + 1; uint64_t u64 = UINT64_MAX - 1;
    size_t size = 12345; ssize_t ssize = -54321;

#define CHECK_NATURAL_NUM(type, flags, key, text) do { \
    for (size_t element = 0; element < 2; ++element) \
        check_natural_value("natural-num", flags, 0, sizeof(type), \
                element * sizeof(type), &key, &key, text); \
} while (0)
    CHECK_NATURAL_NUM(int8_t, RDB_KINT8, i8, "-8,");
    CHECK_NATURAL_NUM(uint8_t, RDB_KUINT8, u8, "250,");
    CHECK_NATURAL_NUM(int16_t, RDB_KINT16, i16, "-1234,");
    CHECK_NATURAL_NUM(uint16_t, RDB_KUINT16, u16, "54321,");
    CHECK_NATURAL_NUM(int32_t, RDB_KINT32, i32, "-1234567,");
    CHECK_NATURAL_NUM(uint32_t, RDB_KUINT32, u32, "3456789012,");
    CHECK_NATURAL_NUM(int64_t, RDB_KINT64, i64, "-9223372036854775807,");
    CHECK_NATURAL_NUM(uint64_t, RDB_KUINT64, u64, "18446744073709551614,");
    CHECK_NATURAL_NUM(size_t, RDB_KSIZE_t, size, "12345,");
    CHECK_NATURAL_NUM(ssize_t, RDB_KSSIZE_t, ssize, "-54321,");
#undef CHECK_NATURAL_NUM
}

static void natural_strings_and_pointers(void)
{
    char inline_key[16] = "inline";
    for (size_t element = 0; element < 2; ++element)
        check_natural_value("natural-kstr", RDB_KSTR, sizeof(inline_key),
                sizeof(inline_key), element * sizeof(inline_key),
                inline_key, inline_key, "inline,");

    for (size_t element = 0; element < 2; ++element) {
        char *stored = malloc(sizeof("pointer string"));
        CHECK(stored != NULL);
        if (!stored) continue;
        strcpy(stored, "pointer string");
        char *query = "pointer string";
        check_natural_value("natural-kpstr", RDB_KPSTR, 0,
                sizeof(stored), element * sizeof(stored),
                &stored, query, "pointer string,");
    }

    static int target;
    void *pointer = &target;
    char expected[64];
    int length = snprintf(expected, sizeof(expected), "%p,", pointer);
    CHECK(length > 0 && (size_t)length < sizeof(expected));
    for (size_t element = 0; element < 2; ++element)
        check_natural_value("natural-kptr", RDB_KPTR, 0,
                sizeof(pointer), element * sizeof(pointer),
                &pointer, &pointer, expected);
}

#ifdef USE_128_BIT_TYPES
typedef union {
    __int128_t signed_value;
    __uint128_t unsigned_value;
} wide_key_t;

typedef struct {
    rdb_bpp_t link;
    wide_key_t key;
} record_t;

static void check_dump_value(char *name, int type, wide_key_t key,
        size_t key_size, const char *expected)
{
    int offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *pool = rdb_register_um_pool(name, 1, offset,
            RDB_BTREE | type, NULL, 0);
    record_t *record = calloc(1, sizeof(*record));
    CHECK(pool && record);
    if (!pool || !record) {
        if (pool) rdb_drop_pool(pool);
        free(record);
        return;
    }
    memcpy(&record->key, &key, key_size);
    CHECK(rdb_insert(pool, record) == 1);
    char *actual = capture_dump(pool, 0);
    CHECK(actual && strcmp(actual, expected) == 0);
    free(actual);
    rdb_flush(pool, NULL, NULL);
    rdb_drop_pool(pool);
}

static void wide_values(void)
{
    __int128_t signed_max = (__int128_t)(((__uint128_t)1 << 127) - 1);
    __int128_t signed_min = -signed_max - 1;
    wide_key_t key;

    key.signed_value = signed_min;
    check_dump_value("dump-signed128-min", RDB_KINT128, key,
            sizeof(key.signed_value), "-170141183460469231731687303715884105728,");
    key.signed_value = signed_max;
    check_dump_value("dump-signed128-max", RDB_KINT128, key,
            sizeof(key.signed_value), "170141183460469231731687303715884105727,");
    key.unsigned_value = ((__uint128_t)1 << 64);
    check_dump_value("dump-unsigned128-over64", RDB_KUINT128, key,
            sizeof(key.unsigned_value), "18446744073709551616,");
    key.unsigned_value = ~(__uint128_t)0;
    check_dump_value("dump-unsigned128-max", RDB_KUINT128, key,
            sizeof(key.unsigned_value), "340282366920938463463374607431768211455,");
}
#endif

int main(void)
{
    rdb_init();
    natural_numeric();
    natural_strings_and_pointers();
#ifdef USE_128_BIT_TYPES
    wide_values();
#endif
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
