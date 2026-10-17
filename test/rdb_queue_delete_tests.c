#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

static void check_repeated_queue_delete(const char *name, uint32_t type)
{
    int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *pool = rdb_register_um_pool((char *)name, 1, key_offset,
            RDB_BTREE | type, NULL, 0);
    record_t records[3] = {{0}};
    record_t *candidate = &records[1];
    record_t *neighbor, *remaining;

    CHECK(pool != NULL);
    if (!pool)
        return;

    for (int i = 0; i < 3; ++i) {
        records[i].key = i;
        CHECK(rdb_insert(pool, &records[i]) == 1);
    }

    neighbor = (record_t *)candidate->link.left;
    CHECK(neighbor != NULL);
    CHECK(candidate->link.right != NULL);
    CHECK(rdb_delete_one(pool, 0, candidate) == 0);
    CHECK(candidate->link.left == NULL && candidate->link.right == NULL);
    CHECK(rdb_delete_one(pool, 0, neighbor) == 0);
    CHECK(rdb_pool_get_fault(pool, NULL) == 0);

    for (int attempt = 0; attempt < 3; ++attempt) {
        CHECK(rdb_delete_one(pool, 0, candidate) == 0);
        CHECK(rdb_pool_get_fault(pool, NULL) == 0);
    }

    remaining = &records[0];
    if (remaining == candidate || remaining == neighbor)
        remaining = &records[2];
    CHECK(remaining != candidate && remaining != neighbor);
    CHECK(rdb_delete_one(pool, 0, remaining) == 0);
    rdb_drop_pool(pool);
}

int main(void)
{
    rdb_init();
    check_repeated_queue_delete("queue-delete-fifo", RDB_KFIFO);
    check_repeated_queue_delete("queue-delete-lifo", RDB_KLIFO);
    rdb_clean(0);

    if (failures) {
        fprintf(stderr, "%d queue-delete test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
