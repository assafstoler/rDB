#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "rdb.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

enum { ROUNDS = 32 };

typedef struct {
    rdb_bpp_t link;
    int32_t key;
} record_t;

typedef struct {
    rdb_pool_t *pool;
    int failed;
} worker_t;

static void *dump_pool(void *arg)
{
    worker_t *worker = arg;
    for (int round = 0; round < ROUNDS; ++round) {
        if (rdb_lock(worker->pool, __func__) != 0) {
            worker->failed = 1;
            return NULL;
        }
        rdb_dump(worker->pool, 0, "\n");
        rdb_unlock(worker->pool, __func__);
    }
    return NULL;
}

static rdb_pool_t *make_pool(const char *name, int base, int count)
{
    int offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *pool = rdb_register_um_pool((char *)name, 1, offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL);
    if (!pool) return NULL;
    for (int i = 0; i < count; ++i) {
        record_t *record = calloc(1, sizeof(*record));
        CHECK(record != NULL);
        if (!record) continue;
        record->key = base + i;
        CHECK(rdb_insert(pool, record) == 1);
    }
    return pool;
}

static void free_record(void *record, void *unused)
{
    (void)unused;
    free(record);
}

int main(void)
{
    enum { FIRST_COUNT = 7, SECOND_COUNT = 31 };
    rdb_pool_t *pools[2];
    rdb_init();
    pools[0] = make_pool("dump-thread-short", 100, FIRST_COUNT);
    pools[1] = make_pool("dump-thread-long", 1000, SECOND_COUNT);
    if (!pools[0] || !pools[1]) {
        for (int i = 0; i < 2; ++i) if (pools[i]) rdb_drop_pool(pools[i]);
        return EXIT_FAILURE;
    }

    FILE *output = tmpfile();
    CHECK(output != NULL);
    if (!output) return EXIT_FAILURE;
    CHECK(fflush(stdout) == 0);
    int saved_stdout = dup(STDOUT_FILENO);
    CHECK(saved_stdout >= 0);
    if (saved_stdout < 0) {
        fclose(output);
        return EXIT_FAILURE;
    }
    CHECK(dup2(fileno(output), STDOUT_FILENO) == STDOUT_FILENO);

    worker_t workers[2] = {{pools[0], 0}, {pools[1], 0}};
    pthread_t threads[2];
    CHECK(pthread_create(&threads[0], NULL, dump_pool, &workers[0]) == 0);
    CHECK(pthread_create(&threads[1], NULL, dump_pool, &workers[1]) == 0);
    CHECK(pthread_join(threads[0], NULL) == 0);
    CHECK(pthread_join(threads[1], NULL) == 0);
    CHECK(fflush(stdout) == 0);
    CHECK(dup2(saved_stdout, STDOUT_FILENO) == STDOUT_FILENO);
    CHECK(close(saved_stdout) == 0);
    CHECK(!workers[0].failed && !workers[1].failed);

    rewind(output);
    unsigned lines = 0;
    char buffer[64];
    while (fgets(buffer, sizeof(buffer), output)) ++lines;
    CHECK(!ferror(output));
    CHECK(fclose(output) == 0);
    CHECK(lines == ROUNDS * (FIRST_COUNT + SECOND_COUNT));

    for (int i = 0; i < 2; ++i) {
        int base = i == 0 ? 100 : 1000;
        int count = i == 0 ? FIRST_COUNT : SECOND_COUNT;
        CHECK(pools[i]->record_count == (uint32_t)count);
        for (int n = 0; n < count; ++n)
            CHECK(rdb_get_const(pools[i], 0, base + n) != NULL);
        rdb_flush(pools[i], free_record, NULL);
        rdb_drop_pool(pools[i]);
    }
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
