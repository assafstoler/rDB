#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
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
    rdb_pool_t *pool;
    record_t *record;
    int failures;
    int done;
} writer_data_t;

static void *update_records(void *arg)
{
    writer_data_t *data = arg;

    for (int i = 0; i < 2000; ++i) {
        if (rdb_lock(data->pool, "stats-writer") != 0) {
            ++data->failures;
            continue;
        }
        data->record->key = i;
        if (rdb_insert(data->pool, data->record) != 1)
            ++data->failures;
        if (rdb_delete(data->pool, 0, &data->record->key) != data->record)
            ++data->failures;
        rdb_unlock(data->pool, "stats-writer");
    }
    __atomic_store_n(&data->done, 1, __ATOMIC_RELAXED);
    return NULL;
}

int main(void)
{
    int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *pool;
    record_t *record;
    writer_data_t writer;
    pthread_t thread;
    int rc;

    rdb_init();
    pool = rdb_register_um_pool("stats-concurrent", 1, key_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    record = calloc(1, sizeof(*record));
    CHECK(pool != NULL && record != NULL);
    if (!pool || !record) {
        free(record);
        rdb_clean(0);
        return EXIT_FAILURE;
    }

    writer = (writer_data_t){pool, record, 0, 0};
    rc = pthread_create(&thread, NULL, update_records, &writer);
    CHECK(rc == 0);
    if (rc == 0) {
        char stats[128];
        do {
            CHECK(rdb_print_pool_stats(stats, sizeof(stats)) == stats);
            CHECK(strstr(stats, "Pool: stats-concurrent : ") != NULL);
            CHECK(strstr(stats, ":END:") != NULL);
        } while (__atomic_load_n(&writer.done, __ATOMIC_RELAXED) == 0);
        CHECK(pthread_join(thread, NULL) == 0);
        CHECK(writer.failures == 0);
    }

    CHECK(pool->record_count == 0);
    rdb_clean(0);
    free(record);

    if (failures) {
        fprintf(stderr, "%d rdb_stats concurrency test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
