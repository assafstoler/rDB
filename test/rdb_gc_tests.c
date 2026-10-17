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

int main(void)
{
    rdb_pool_t *tail, *kept, *head;
    record_t *record;

    rdb_init();
    tail = make_pool("gc-tail");
    kept = make_pool("gc-kept");
    head = make_pool("gc-head");
    record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!tail || !kept || !head || !record) {
        free(record);
        rdb_clean(0);
        return EXIT_FAILURE;
    }

    record->key = 23;
    CHECK(rdb_insert(kept, record) == 1);
    tail->drop = 1;
    head->drop = 1;

    rdb_gc();

    CHECK(rdb_find_pool_by_name("gc-head") == NULL);
    CHECK(rdb_find_pool_by_name("gc-tail") == NULL);
    CHECK(rdb_find_pool_by_name("gc-kept") == kept);
    CHECK(kept->prev == NULL);
    CHECK(kept->next == NULL);
    CHECK(rdb_get_const(kept, 0, record->key) == record);

    rdb_clean(0);
    free(record);

    if (failures) {
        fprintf(stderr, "%d rdb_gc test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
