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

#ifdef USE_128_BIT_TYPES
typedef union {
    __int128_t signed_value;
    __uint128_t unsigned_value;
} wide_key_t;

typedef struct {
    rdb_bpp_t link;
    wide_key_t key;
} record_t;

static char *capture_dump(rdb_pool_t *pool)
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
    rdb_dump(pool, 0, ",");
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
    char *actual = capture_dump(pool);
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
#ifdef USE_128_BIT_TYPES
    wide_values();
#endif
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
