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
    char code[8];
    uint16_t rank;
} custom_key_t;

typedef struct {
    rdb_bpp_t link;
    custom_key_t key;
} custom_record_t;

static int compare_custom(void *old_key, void *new_key)
{
    custom_key_t *old = old_key;
    custom_key_t *new_value = new_key;
    int compare = strcmp(new_value->code, old->code);
    if (compare) return compare;
    return (new_value->rank > old->rank) - (new_value->rank < old->rank);
}

static rdb_pool_t *make_kcf_pool(const char *name)
{
    int offset = (int)(offsetof(custom_record_t, key) - sizeof(rdb_bpp_t));
    return rdb_register_um_pool((char *)name, 1, offset,
            RDB_BTREE | RDB_KCF, (void *)compare_custom, 0);
}

static void check_pool_output(const char *expected)
{
    FILE *file = tmpfile();
    char output[128];
    size_t expected_size = strlen(expected);
    CHECK(file != NULL);
    if (!file) return;

    rdb_print_pools(file);
    CHECK(fflush(file) == 0);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    size_t size = fread(output, 1, sizeof(output), file);
    CHECK(!ferror(file));
    CHECK(size == expected_size);
    if (size == expected_size)
        CHECK(memcmp(output, expected, size) == 0);
    CHECK(fclose(file) == 0);
}

static void test_init_flags_and_pool_listing(void)
{
    int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *first, *second;

    rdb_init();
    check_pool_output("");

    first = rdb_register_um_pool("listed-first", 1, key_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    second = rdb_register_um_pool("listed-second", 1, key_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(first != NULL && second != NULL);
    if (!first || !second) {
        rdb_clean(0);
        return;
    }
    check_pool_output("listed-second\nlisted-first\n");

    CHECK(rdb_init_ex(UINT32_C(0x80000000)) == -1);
    CHECK(rdb_find_pool_by_name("listed-first") == first);
    CHECK(rdb_find_pool_by_name("listed-second") == second);
    rdb_clean(0);
    CHECK(rdb_init_ex(RDB_INIT_ALLOW_KCFUT) == 0);
    CHECK(rdb_find_pool_by_name("listed-first") == NULL);
    rdb_clean(0);
}

static void test_kcf_key_part_validation(void)
{
    rdb_kcf_key_part_t valid[] = {
        {offsetof(custom_key_t, code), RDB_KSTR,
            sizeof(((custom_record_t *)0)->key.code)},
        {offsetof(custom_key_t, rank), RDB_KUINT16, 0}
    };
    rdb_kcf_key_part_t part = {0, RDB_KINT32, 0};
    rdb_kcf_key_part_t malformed[2];
    rdb_pool_t *pool, *non_kcf;

    rdb_init();
    pool = make_kcf_pool("metadata-kcf");
    non_kcf = rdb_register_um_pool("metadata-non-kcf", 1,
            (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t)),
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL && non_kcf != NULL);
    if (!pool || !non_kcf) {
        rdb_clean(0);
        return;
    }

    CHECK(rdb_register_kcf_key_parts(NULL, 0, &part, 1) ==
            RDB_ERR_REINDEX_INVALID);
    CHECK(rdb_register_kcf_key_parts(pool, -1, &part, 1) ==
            RDB_ERR_REINDEX_INVALID);
    CHECK(rdb_register_kcf_key_parts(non_kcf, 0, &part, 1) ==
            RDB_ERR_REINDEX_INVALID);
    CHECK(rdb_register_kcf_key_parts(pool, 0, NULL, 1) ==
            RDB_ERR_REINDEX_INVALID);
    CHECK(rdb_register_kcf_key_parts(pool, 0, &part, 0) ==
            RDB_ERR_REINDEX_INVALID);
    CHECK(rdb_register_kcf_key_parts(pool, 0, &part, (size_t)-1) ==
            RDB_ERR_REINDEX_INVALID);

    malformed[0] = (rdb_kcf_key_part_t){0, 0, 0};
    CHECK(rdb_register_kcf_key_parts(pool, 0, malformed, 1) ==
            RDB_ERR_REINDEX_INVALID);
    malformed[0] = (rdb_kcf_key_part_t){0, RDB_KSTR, 0};
    CHECK(rdb_register_kcf_key_parts(pool, 0, malformed, 1) ==
            RDB_ERR_REINDEX_INVALID);
    malformed[0] = (rdb_kcf_key_part_t){0, RDB_KINT32, 1};
    CHECK(rdb_register_kcf_key_parts(pool, 0, malformed, 1) ==
            RDB_ERR_REINDEX_INVALID);
    malformed[0] = (rdb_kcf_key_part_t){(size_t)-2, RDB_KINT32, 0};
    CHECK(rdb_register_kcf_key_parts(pool, 0, malformed, 1) ==
            RDB_ERR_REINDEX_INVALID);
    malformed[0] = (rdb_kcf_key_part_t){0, RDB_KINT32, 0};
    malformed[1] = (rdb_kcf_key_part_t){2, RDB_KINT16, 0};
    CHECK(rdb_register_kcf_key_parts(pool, 0, malformed, 2) ==
            RDB_ERR_REINDEX_INVALID);

    CHECK(rdb_register_kcf_key_parts(pool, 0, valid, 2) == 0);
    CHECK(rdb_register_kcf_key_parts(pool, 0, valid, 2) ==
            RDB_ERR_REINDEX_INVALID);

    custom_record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (record) {
        strcpy(record->key.code, "alpha");
        record->key.rank = 1;
        CHECK(rdb_insert(pool, record) == 1);
        rdb_flush(pool, NULL, NULL);
    }

    rdb_pool_t *late = make_kcf_pool("metadata-late");
    custom_record_t *late_record = calloc(1, sizeof(*late_record));
    CHECK(late != NULL && late_record != NULL);
    if (late && late_record) {
        strcpy(late_record->key.code, "late");
        late_record->key.rank = 2;
        CHECK(rdb_insert(late, late_record) == 1);
        CHECK(rdb_register_kcf_key_parts(late, 0, valid, 2) ==
                RDB_ERR_REINDEX_INVALID);
        rdb_flush(late, NULL, NULL);
    } else {
        free(late_record);
    }
    rdb_clean(0);
}

int main(void)
{
    test_init_flags_and_pool_listing();
    test_kcf_key_part_validation();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
