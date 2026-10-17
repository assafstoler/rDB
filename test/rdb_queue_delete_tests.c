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
    rdb_bpp_t links[2];
    int32_t key[2];
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
        records[i].key[0] = i;
        CHECK(rdb_insert(pool, &records[i]) == 1);
    }

    neighbor = (record_t *)candidate->links[0].left;
    CHECK(neighbor != NULL);
    CHECK(candidate->links[0].right != NULL);
    CHECK(rdb_delete_one(pool, 0, candidate) == 0);
    CHECK(candidate->links[0].left == NULL && candidate->links[0].right == NULL);
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

static void leave_record(void *record, void *unused)
{
    (void)record;
    (void)unused;
}

static void check_empty_endpoints(rdb_pool_t *pool)
{
    CHECK(pool->record_count == 0);
    for (int index = 0; index < pool->indexCount; ++index) {
        CHECK(pool->root[index] == NULL);
        CHECK(pool->tail[index] == NULL);
    }
}

static void check_flush_endpoints(int queue_index, uint32_t queue_type)
{
    static int pool_number;
    char name[32];
    const int tree_index = 1 - queue_index;
    const int tree_offset = (int)(offsetof(record_t, key[tree_index]) -
            sizeof(rdb_bpp_t) * 2);
    const int primary_offset = queue_index == 0 ? 0 : tree_offset;
    const int primary_type = queue_index == 0 ? (int)queue_type : RDB_KINT32;
    const int secondary_offset = queue_index == 1 ? 0 : tree_offset;
    const int secondary_type = queue_index == 1 ? (int)queue_type : RDB_KINT32;
    snprintf(name, sizeof(name), "flush-endpoints-%d", pool_number++);
    rdb_pool_t *pool = rdb_register_um_pool(name, 2, primary_offset,
            RDB_BTREE | primary_type, NULL, 0);
    record_t records[3] = {{0}};
    CHECK(pool != NULL);
    if (!pool) return;
    CHECK(rdb_register_um_idx(pool, 1, secondary_offset,
                RDB_BTREE | secondary_type, NULL, 0) == 1);

    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 3; ++i) {
            records[i].key[0] = i + 1;
            records[i].key[1] = i + 1;
            CHECK(rdb_insert(pool, &records[i]) == 2);
        }
        record_t *head = queue_type == RDB_KFIFO ? &records[0] : &records[2];
        record_t *tail = queue_type == RDB_KFIFO ? &records[2] : &records[0];
        CHECK(pool->root[queue_index] == (rdb_bpp_t *)head);
        CHECK(pool->tail[queue_index] == (rdb_bpp_t *)tail);

        rdb_flush(pool, leave_record, NULL);
        check_empty_endpoints(pool);
    }
    rdb_drop_pool(pool);
}

int main(void)
{
    rdb_init();
    check_repeated_queue_delete("queue-delete-fifo", RDB_KFIFO);
    check_repeated_queue_delete("queue-delete-lifo", RDB_KLIFO);
    check_flush_endpoints(0, RDB_KFIFO);
    check_flush_endpoints(0, RDB_KLIFO);
    check_flush_endpoints(1, RDB_KFIFO);
    check_flush_endpoints(1, RDB_KLIFO);
    rdb_clean(0);

    if (failures) {
        fprintf(stderr, "%d queue-delete test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
