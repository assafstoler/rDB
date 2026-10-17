#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
    int id;
    int failures;
    rdb_pool_t *keeper;
} worker_data_t;

typedef struct {
    rdb_bpp_t link;
    int32_t key;
} record_t;

static void *register_and_drop_pools(void *arg)
{
    worker_data_t *data = arg;
    int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));

    for (int i = 0; i < 500; ++i) {
        char name[64];
        rdb_pool_t *pool;
        snprintf(name, sizeof(name), "registry-%d-%d", data->id, i);
        pool = rdb_register_um_pool(name, 1, key_offset,
                RDB_BTREE | RDB_KINT32, NULL, 0);
        if (!pool) {
            ++data->failures;
            continue;
        }
        if (rdb_find_pool_by_name("registry-keeper") != data->keeper)
            ++data->failures;
        rdb_drop_pool(pool);
    }
    return NULL;
}

int main(void)
{
    int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    worker_data_t workers[2];
    pthread_t threads[2];
    int started = 0;
    rdb_pool_t *keeper;

    rdb_init();
    keeper = rdb_register_um_pool("registry-keeper", 1, key_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(keeper != NULL);
    if (!keeper) {
        rdb_clean(0);
        return 1;
    }
    workers[0] = (worker_data_t){0, 0, keeper};
    workers[1] = (worker_data_t){1, 0, keeper};

    for (int i = 0; i < 2; ++i) {
        int rc = pthread_create(&threads[i], NULL,
                register_and_drop_pools, &workers[i]);
        CHECK(rc == 0);
        if (rc != 0)
            break;
        ++started;
    }
    for (int i = 0; i < started; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);

    for (int i = 0; i < 2; ++i)
        CHECK(workers[i].failures == 0);
    CHECK(rdb_find_pool_by_name("registry-keeper") == keeper);
    rdb_drop_pool(keeper);
    CHECK(rdb_find_pool_by_name("registry-keeper") == NULL);

    if (failures) {
        fprintf(stderr, "%d registry-lock test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
