/* Installed header first: no source-tree headers or private build defines. */
#include <rdb/rdb.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
    return EXIT_FAILURE; \
} } while (0)

typedef struct {
    rdb_bpp_t links[2];
    uint32_t key;
} consumer_record_t;

int main(void)
{
    CHECK(sizeof(__intmax_t) == RDB_TEST_VALUE_BYTES);
    char pool_name[] = "installed-consumer";
    int key_offset = (int)(offsetof(consumer_record_t, key) - 2 * sizeof(rdb_bpp_t));
    rdb_init();
    rdb_pool_t *pool = rdb_register_um_pool(pool_name, 2, key_offset,
            RDB_BTREE | RDB_KUINT32, NULL, 0);
    CHECK(pool);
    CHECK(rdb_register_um_idx(pool, 1, 0, RDB_BTREE | RDB_KFIFO, NULL, 0) == 1);
    consumer_record_t *record = (consumer_record_t *)calloc(1, sizeof(*record));
    CHECK(record);
    record->key = 17;
    CHECK(rdb_insert(pool, record) == 2);
    CHECK(rdb_get_const(pool, 0, 17) == record);
    __intmax_t missing = ((__intmax_t)1 << 32) + 17;
    CHECK(rdb_get_const(pool, 0, missing) == NULL);
    CHECK(rdb_delete_const(pool, 0, missing) == NULL);
    CHECK(rdb_delete_const(pool, 0, 17) == record);
    CHECK(rdb_insert(pool, record) == 2);

    /* Confirm which library is loaded, independently of the consumer's
     * RDB_POOL_COUNTERS define. Its public layout must be identical. */
    char stats[256];
    CHECK(rdb_print_pool_stats(stats, sizeof(stats)) == stats);
#if RDB_TEST_LIBRARY_COUNTERS
    CHECK(pool->record_count == 1);
    CHECK(strcmp(stats, "Pool: installed-consumer : 1\n:END:") == 0);
#else
    CHECK(pool->record_count == 0);
    CHECK(strcmp(stats, "rDB pool record counting disabled") == 0);
#endif
    rdb_fault_info_t fault;
    CHECK(rdb_pool_get_fault(pool, &fault) == 0);
    CHECK(pool->fault.code == fault.code && pool->fault.record == fault.record);

    /* A genuine partial unlink checks fault-field offsets across the DSO
     * boundary, including the build where record counters are disabled. */
    CHECK(rdb_delete_one(pool, 1, record) == 0);
    CHECK(rdb_delete(pool, 0, &record->key) == NULL);
    CHECK(rdb_pool_get_fault(pool, &fault) == 1);
    CHECK(fault.code == RDB_ERR_FAULTED && fault.record == record && fault.index == 1);
    CHECK(pool->fault.code == fault.code && pool->fault.index == fault.index);
    CHECK(pool->fault.record == fault.record);
    CHECK(fault.operation && fault.reason);
    CHECK(pool->fault.operation == fault.operation && pool->fault.reason == fault.reason);
    rdb_drop_pool(pool);
    free(record); // The fixture owns this allocation; do not traverse damaged indexes.
    rdb_clean(0);
    return EXIT_SUCCESS;
}
