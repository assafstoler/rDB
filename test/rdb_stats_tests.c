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

static rdb_pool_t *make_pool(const char *name)
{
    int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *pool = rdb_register_um_pool((char *)name, 1, key_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL);
    return pool;
}

static void check_report_sizes(const char *expected)
{
    size_t full_size = strlen(expected);
    unsigned char buf[256];
    CHECK(full_size + 1 < sizeof(buf));
    if (full_size + 1 >= sizeof(buf))
        return;

    for (int max_len = 1; max_len <= (int)full_size; ++max_len) {
        int dots = max_len - 1 < 3 ? max_len - 1 : 3;
        int prefix = max_len - 1 - dots;
        memset(buf, 0xa5, sizeof(buf));
        CHECK(rdb_print_pool_stats((char *)buf + 1, max_len) ==
                (char *)buf + 1);
        CHECK(buf[0] == 0xa5);
        CHECK(buf[max_len] == '\0');
        CHECK(buf[max_len + 1] == 0xa5);
        CHECK(memcmp(buf + 1, expected, (size_t)prefix) == 0);
        for (int i = 0; i < dots; ++i)
            CHECK(buf[1 + prefix + i] == '.');
    }

    memset(buf, 0xa5, sizeof(buf));
    CHECK(rdb_print_pool_stats((char *)buf + 1, (int)full_size + 1) ==
            (char *)buf + 1);
    CHECK(memcmp(buf + 1, expected, full_size + 1) == 0);
    CHECK(buf[0] == 0xa5);
    CHECK(buf[full_size + 2] == 0xa5);
}

int main(void)
{
    static const char long_name[] =
            "stats-a-long-pool-name-that-exercises-row-truncation-boundaries";
#ifdef RDB_POOL_COUNTERS
    const char *empty_report = ":END:";
#else
    const char *empty_report = "rDB pool record counting disabled";
#endif
    char invalid[8] = "guard";
    char saved[sizeof(invalid)];
    rdb_pool_t *tail, *short_pool, *long_pool;
    record_t *short_record, *long_record;
    char expected[192];

    rdb_init();
    CHECK(rdb_print_pool_stats(NULL, 8) == NULL);
    memcpy(saved, invalid, sizeof(saved));
    CHECK(rdb_print_pool_stats(invalid, 0) == invalid);
    CHECK(memcmp(invalid, saved, sizeof(invalid)) == 0);
    CHECK(rdb_print_pool_stats(invalid, -1) == invalid);
    CHECK(memcmp(invalid, saved, sizeof(invalid)) == 0);
    check_report_sizes(empty_report);

    tail = make_pool("stats-short");
    short_pool = make_pool("stats-middle");
    long_pool = make_pool(long_name);
    short_record = calloc(1, sizeof(*short_record));
    long_record = calloc(1, sizeof(*long_record));
    CHECK(short_record != NULL && long_record != NULL);
    if (!tail || !short_pool || !long_pool || !short_record || !long_record) {
        free(short_record);
        free(long_record);
        rdb_clean(0);
        return EXIT_FAILURE;
    }

    short_record->key = 11;
    long_record->key = 29;
    CHECK(rdb_insert(short_pool, short_record) == 1);
    CHECK(rdb_insert(long_pool, long_record) == 1);
#ifdef RDB_POOL_COUNTERS
    CHECK(snprintf(expected, sizeof(expected),
                "Pool: %s : 1\nPool: stats-middle : 1\n"
                "Pool: stats-short : 0\n:END:", long_name) <
            (int)sizeof(expected));
#else
    CHECK(snprintf(expected, sizeof(expected), "%s", empty_report) <
            (int)sizeof(expected));
#endif
    check_report_sizes(expected);

#ifdef RDB_POOL_COUNTERS
    {
        const uint32_t counts[] = {
            0, INT32_MAX, UINT32_C(2147483648), UINT32_MAX
        };
        const char *count_text[] = {
            "0", "2147483647", "2147483648", "4294967295"
        };
        for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
            long_pool->record_count = counts[i];
            CHECK(snprintf(expected, sizeof(expected),
                        "Pool: %s : %s\nPool: stats-middle : 1\n"
                        "Pool: stats-short : 0\n:END:",
                        long_name, count_text[i]) < (int)sizeof(expected));
            check_report_sizes(expected);
            CHECK(long_pool->record_count == counts[i]);
        }
        long_pool->record_count = 1;
    }
#endif

    rdb_clean(0);
    free(short_record);
    free(long_record);

    if (failures) {
        fprintf(stderr, "%d rdb_stats test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
