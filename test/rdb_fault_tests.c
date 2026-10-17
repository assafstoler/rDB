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
    rdb_bpp_t links[RDB_POOL_MAX_IDX];
    int32_t key[RDB_POOL_MAX_IDX];
} record_t;

static int compare_int32(void *old_key, void *new_key)
{
    int32_t old_value = *(int32_t *)old_key;
    int32_t new_value = *(int32_t *)new_key;
    return (new_value > old_value) - (new_value < old_value);
}

static int rollback_triggered;
static record_t *rollback_victim;

static int compare_rollback_tree(void *old_key, void *new_key)
{
    if (rollback_triggered && new_key == &rollback_victim->key[0])
        return -1;
    return compare_int32(old_key, new_key);
}

static int compare_duplicate(void *old_key, void *new_key)
{
    int rc = compare_int32(old_key, new_key);
    if (new_key == &rollback_victim->key[1] && rc == 0)
        rollback_triggered = 1;
    return rc;
}

static record_t *new_record(int key0, int key1)
{
    record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (record) {
        record->key[0] = key0;
        record->key[1] = key1;
    }
    return record;
}

static rdb_pool_t *make_tree_pool(char *name, int indexes,
        int (*first_compare)(void *, void *), int (*second_compare)(void *, void *))
{
    int offset = (int)offsetof(record_t, key[0]) -
            (int)(sizeof(rdb_bpp_t) * indexes);
    rdb_pool_t *pool = rdb_register_um_pool(name, indexes, offset,
            RDB_BTREE | RDB_KCF, (void *)(first_compare ? first_compare : compare_int32));
    CHECK(pool != NULL);
    if (!pool)
        return NULL;

    for (int index = 1; index < indexes; ++index) {
        int key_offset = (int)offsetof(record_t, key[index]) -
                (int)(sizeof(rdb_bpp_t) * indexes);
        void *compare = (void *)(index == 1 && second_compare
                ? second_compare : compare_int32);
        CHECK(rdb_register_um_idx(pool, index, key_offset,
                    RDB_BTREE | RDB_KCF, compare) == index);
    }
    return pool;
}

static rdb_pool_t *make_queue_pool(char *name)
{
    int offset = (int)offsetof(record_t, key[0]) -
            (int)(sizeof(rdb_bpp_t) * 2);
    rdb_pool_t *pool = rdb_register_um_pool(name, 2, offset,
            RDB_BTREE | RDB_KINT32, NULL);
    CHECK(pool != NULL);
    if (pool)
        CHECK(rdb_register_um_idx(pool, 1, 0,
                    RDB_BTREE | RDB_KFIFO, NULL) == 1);
    return pool;
}

static int visits, cleanups;

static int visit_record(void *record, void *unused)
{
    (void)record;
    (void)unused;
    ++visits;
    return RDB_CB_OK;
}

static void cleanup_record(void *record, void *unused)
{
    (void)record;
    (void)unused;
    ++cleanups;
}

static void leave_record_owned(void *record, void *unused)
{
    (void)record;
    (void)unused;
}

extern int _rdb_delete_by_pointer(rdb_pool_t *, void *, int, rdb_bpp_t *, int);

static void test_unexpected_two_child_unlink(void)
{
    rdb_pool_t *pool = make_tree_pool("fault-two-child", 1, NULL, NULL);
    record_t *records[] = {
        new_record(20, 0), new_record(10, 0), new_record(30, 0)
    };
    if (!pool || !records[0] || !records[1] || !records[2])
        goto done;

    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i)
        CHECK(rdb_insert(pool, records[i]) == 1);
    CHECK(pool->root[0] == (rdb_bpp_t *)records[0]);
    CHECK(_rdb_delete_by_pointer(pool, NULL, 0, pool->root[0], 0) == RDB_ERR_FAULTED);

    rdb_fault_info_t fault;
    CHECK(rdb_pool_get_fault(pool, &fault) == 1);
    CHECK(fault.code == RDB_ERR_FAULTED && fault.index == 0);
    CHECK(fault.record == records[0]);
    CHECK(fault.operation && strcmp(fault.operation, "delete") == 0);
    CHECK(fault.reason && strstr(fault.reason, "two-child"));
    CHECK(_rdb_delete_by_pointer(pool, NULL, 0, pool->root[0], 0) == RDB_ERR_FAULTED);
    rdb_fault_info_t saved_fault;
    CHECK(rdb_pool_get_fault(pool, &saved_fault) == 1);
    CHECK(memcmp(&saved_fault, &fault, sizeof(fault)) == 0);

    rdb_bpp_t roots[RDB_POOL_MAX_IDX];
    memcpy(roots, pool->root, sizeof(roots));
    record_t *candidate = new_record(40, 0);
    CHECK(rdb_insert(pool, candidate) == RDB_ERR_FAULTED);
    CHECK(rdb_insert_one(pool, 0, candidate) == RDB_ERR_FAULTED);
    CHECK(rdb_delete_one(pool, 0, records[1]) == RDB_ERR_FAULTED);
    CHECK(rdb_register_um_idx(pool, 0, 0, RDB_BTREE | RDB_KINT32, NULL) == RDB_ERR_FAULTED);
    CHECK(rdb_get(pool, 0, &records[0]->key[0]) == NULL);
    CHECK(rdb_get_const(pool, 0, 20) == NULL);
    void *before = records[0], *after = records[1];
    CHECK(rdb_get_neigh(pool, 0, &records[0]->key[0], &before, &after) == NULL);
    CHECK(before == NULL && after == NULL);
    CHECK(rdb_delete(pool, 0, &records[0]->key[0]) == NULL);
    CHECK(rdb_delete_const(pool, 0, 20) == NULL);
    visits = cleanups = 0;
    rdb_iterate(pool, 0, visit_record, NULL, cleanup_record, NULL);
    rdb_flush(pool, cleanup_record, NULL);
    CHECK(visits == 0 && cleanups == 0);
    CHECK(memcmp(roots, pool->root, sizeof(roots)) == 0);
    CHECK(rdb_pool_get_fault(pool, NULL) == 1);
    free(candidate);

    rdb_pool_t *healthy = make_tree_pool("fault-other-pool", 1, NULL, NULL);
    record_t *kept = new_record(77, 0);
    if (healthy && kept) {
        CHECK(rdb_insert(healthy, kept) == 1);
        CHECK(rdb_move2(healthy, pool, 0, &records[0]->key[0]) == RDB_ERR_FAULTED);
        CHECK(rdb_move2(pool, healthy, 0, &kept->key[0]) == RDB_ERR_FAULTED);
        CHECK(rdb_get_const(healthy, 0, 77) == kept);
        rdb_flush(healthy, NULL, NULL);
    } else {
        free(kept);
    }
    if (healthy) rdb_drop_pool(healthy);

done:
    if (pool) rdb_drop_pool(pool);
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i)
        free(records[i]);
}

static void test_partial_multi_index_delete(void)
{
    rdb_pool_t *pool = make_tree_pool("fault-partial-delete", 2, NULL, NULL);
    record_t *record = new_record(20, 70);
    if (!pool || !record)
        goto done;
    CHECK(rdb_insert(pool, record) == 2);
    CHECK(rdb_delete_one(pool, 1, record) >= 0);
    CHECK(rdb_delete(pool, 0, &record->key[0]) == NULL);
    CHECK(rdb_pool_get_fault(pool, NULL) == 1);
    CHECK(pool->fault.index == 1);
    CHECK(strcmp(pool->fault.operation, "delete") == 0);
    CHECK(strstr(pool->fault.reason, "every index"));
    CHECK(!pool->root[0] && !pool->root[1]);
    CHECK(pool->record_count == 1);

done:
    if (pool) rdb_drop_pool(pool);
    free(record);
}

static void test_inconsistent_queue_links(void)
{
    int count = 2;
    int offset = (int)offsetof(record_t, key[0]) -
            (int)(sizeof(rdb_bpp_t) * count);
    rdb_pool_t *pool = rdb_register_um_pool("fault-queue-links", count, offset,
            RDB_BTREE | RDB_KINT32, NULL);
    CHECK(pool != NULL);
    if (!pool)
        return;
    CHECK(rdb_register_um_idx(pool, 1, 0, RDB_BTREE | RDB_KFIFO, NULL) == 1);
    record_t *records[] = {
        new_record(20, 0), new_record(10, 0), new_record(30, 0)
    };
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i)
        CHECK(rdb_insert(pool, records[i]) == count);

    /* Break the predecessor's reciprocal link to the middle queue record. */
    rdb_bpp_t *link = (rdb_bpp_t *)records[1] + 1;
    CHECK(link->left == (rdb_bpp_t *)records[0] + 1);
    ((rdb_bpp_t *)link->left)->right = NULL;
    CHECK(rdb_delete(pool, 0, &records[1]->key[0]) == NULL);
    CHECK(rdb_pool_get_fault(pool, NULL) == 1);
    CHECK(pool->fault.index == 1);
    CHECK(strstr(pool->fault.reason, "queue links"));
    CHECK(pool->fault.record == records[1]);

    rdb_drop_pool(pool);
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i)
        free(records[i]);
}

static void test_failed_insert_rollback(void)
{
    rollback_triggered = 0;
    rdb_pool_t *pool = make_tree_pool("fault-insert-rollback", 2,
            compare_rollback_tree, compare_duplicate);
    record_t *existing = new_record(10, 7);
    rollback_victim = new_record(20, 7);
    if (!pool || !existing || !rollback_victim)
        goto done;

    CHECK(rdb_insert(pool, existing) == 2);
    CHECK(rdb_insert(pool, rollback_victim) == RDB_ERR_FAULTED);
    CHECK(rollback_triggered);
    CHECK(pool->fault.index == 0);
    CHECK(strcmp(pool->fault.operation, "insert rollback") == 0);
    CHECK(strstr(pool->fault.reason, "partially inserted"));
    CHECK(((rdb_bpp_t *)existing)->right == rollback_victim);
    CHECK(pool->record_count == 1);

done:
    if (pool) rdb_drop_pool(pool);
    free(rollback_victim);
    free(existing);
    rollback_victim = NULL;
}

static void test_move_rejection_restores_source(void)
{
    rdb_pool_t *src = make_queue_pool("move-rollback-source");
    rdb_pool_t *dst = make_queue_pool("move-rollback-destination");
    record_t *before = new_record(10, 0);
    record_t *victim = new_record(20, 0);
    record_t *after = new_record(30, 0);
    record_t *conflict = new_record(20, 0);
    if (!src || !dst || !before || !victim || !after || !conflict)
        goto done;

    CHECK(rdb_insert(src, before) == 2);
    CHECK(rdb_insert(src, victim) == 2);
    CHECK(rdb_insert(src, after) == 2);
    CHECK(rdb_insert(dst, conflict) == 2);
    CHECK(rdb_move2(dst, src, 0, &victim->key[0]) == -2);
    CHECK(rdb_pool_get_fault(src, NULL) == 0);
    CHECK(rdb_pool_get_fault(dst, NULL) == 0);
    CHECK(rdb_get_const(src, 0, 20) == victim);
    CHECK(rdb_get_const(dst, 0, 20) == conflict);
    CHECK(src->root[1] == (rdb_bpp_t *)before);
    CHECK(src->tail[1] == (rdb_bpp_t *)after);
    CHECK(src->record_count == 3 && dst->record_count == 1);
    CHECK(((rdb_bpp_t *)before + 1)->right == (rdb_bpp_t *)victim + 1);
    CHECK(((rdb_bpp_t *)victim + 1)->left == (rdb_bpp_t *)before + 1);
    CHECK(((rdb_bpp_t *)victim + 1)->right == (rdb_bpp_t *)after + 1);
    CHECK(((rdb_bpp_t *)after + 1)->left == (rdb_bpp_t *)victim + 1);

done:
    if (src) rdb_flush(src, leave_record_owned, NULL);
    if (dst) rdb_flush(dst, leave_record_owned, NULL);
    if (src) rdb_drop_pool(src);
    if (dst) rdb_drop_pool(dst);
    free(before);
    free(victim);
    free(after);
    free(conflict);
}

int main(void)
{
    rdb_init();
    test_unexpected_two_child_unlink();
    test_partial_multi_index_delete();
    test_inconsistent_queue_links();
    test_failed_insert_rollback();
    test_move_rejection_restores_source();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
