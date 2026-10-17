#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include "rdb.h"

enum { INDEXES = 3, WORKERS = 4, ITEMS_PER_WORKER = 64 };

typedef struct {
    rdb_bpp_t links[INDEXES];
    int32_t key[INDEXES];
} record_t;

typedef struct {
    rdb_pool_t *pool;
    record_t records[ITEMS_PER_WORKER];
    int id;
    int failures;
    int done;
} worker_t;

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void *insert_and_remove(void *opaque)
{
    worker_t *worker = opaque;
    for (int i = 0; i < ITEMS_PER_WORKER; ++i) {
        record_t *record = &worker->records[i];
        record->key[0] = worker->id * 10000 + i * 3 + 1;
        record->key[2] = worker->id * 10000 + i * 3 + 2;
        if (rdb_lock(worker->pool, "shared-pool-writer") != 0) {
            ++worker->failures;
            continue;
        }
        if (rdb_insert(worker->pool, record) != INDEXES)
            ++worker->failures;
        if (rdb_get(worker->pool, 0, &record->key[0]) != record)
            ++worker->failures;
        if (rdb_get(worker->pool, 2, &record->key[2]) != record)
            ++worker->failures;
        rdb_unlock(worker->pool, "shared-pool-writer");
        sched_yield();
    }

    for (int n = 0; n < ITEMS_PER_WORKER; ++n) {
        int i = (n * 17) % ITEMS_PER_WORKER;
        record_t *record = &worker->records[i];
        if (rdb_lock(worker->pool, "shared-pool-writer") != 0) {
            ++worker->failures;
            continue;
        }
        if (rdb_get(worker->pool, 0, &record->key[0]) != record)
            ++worker->failures;
        if (rdb_delete(worker->pool, 2, &record->key[2]) != record)
            ++worker->failures;
        if (rdb_get(worker->pool, 0, &record->key[0]) != NULL)
            ++worker->failures;
        rdb_unlock(worker->pool, "shared-pool-writer");
        sched_yield();
    }
    __atomic_store_n(&worker->done, 1, __ATOMIC_RELAXED);
    return NULL;
}

int main(void)
{
    int first_offset = (int)(offsetof(record_t, key[0]) - sizeof(rdb_bpp_t) * INDEXES);
    int last_offset = (int)(offsetof(record_t, key[2]) - sizeof(rdb_bpp_t) * INDEXES);
    rdb_pool_t *pool;
    worker_t workers[WORKERS];
    pthread_t threads[WORKERS];
    int started = 0;

    rdb_init();
    pool = rdb_register_um_pool("shared-pool-thread", INDEXES, first_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL);
    if (!pool) {
        rdb_clean(0);
        return EXIT_FAILURE;
    }
    CHECK(rdb_register_um_idx(pool, 1, 0, RDB_BTREE | RDB_KFIFO, NULL, 0) == 1);
    CHECK(rdb_register_um_idx(pool, 2, last_offset,
                RDB_BTREE | RDB_KINT32, NULL, 0) == 2);

    for (int i = 0; i < WORKERS; ++i) {
        memset(&workers[i], 0, sizeof(workers[i]));
        workers[i].pool = pool;
        workers[i].id = i;
        int rc = pthread_create(&threads[i], NULL, insert_and_remove, &workers[i]);
        CHECK(rc == 0);
        if (rc != 0)
            break;
        ++started;
    }

    if (started == WORKERS) {
        char stats[128];
        while (1) {
            CHECK(rdb_print_pool_stats(stats, sizeof(stats)) == stats);
            CHECK(strstr(stats, "Pool: shared-pool-thread : ") != NULL);
            CHECK(strstr(stats, ":END:") != NULL);
            int done = 1;
            for (int i = 0; i < WORKERS; ++i)
                if (!__atomic_load_n(&workers[i].done, __ATOMIC_RELAXED)) done = 0;
            if (done) break;
            sched_yield();
        }
    }
    for (int i = 0; i < started; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    for (int i = 0; i < started; ++i)
        CHECK(workers[i].failures == 0);

    CHECK(pool->record_count == 0);
    for (int index = 0; index < INDEXES; ++index) {
        CHECK(pool->root[index] == NULL);
        CHECK(pool->tail[index] == NULL);
    }
    rdb_drop_pool(pool);
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
