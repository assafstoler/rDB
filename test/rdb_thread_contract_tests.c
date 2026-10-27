/* Concurrency only within rDB's documented locking/ownership contract. */
#include "rdb_test_support.h"

enum { THREADS = 6, RECORDS = 128, ROUNDS = 100 };
typedef struct { rdb_bpp_t links[1]; int32_t key; } item;

static void wait_barrier(pthread_barrier_t *barrier)
{
    int result = pthread_barrier_wait(barrier);
    CHECK(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
}

static rdb_pool_t *register_pool(char *name)
{
    return rdb_register_um_pool(name, 1, KEY_OFFSET(item, key, 1),
                                RDB_BTREE | RDB_KINT32, NULL, 0);
}

struct worker {
    int id;
    rdb_pool_t *pool;
    pthread_barrier_t *start, *filled, *release;
};

static void *registry_worker(void *opaque)
{
    struct worker *worker = opaque;
    wait_barrier(worker->start);
    for (int round = 0; round < ROUNDS; ++round) {
        char name[64];
        snprintf(name, sizeof(name), "thread-%d-round-%d", worker->id, round);
        rdb_pool_t *pool = register_pool(name);
        CHECK(pool);
        CHECK(strcmp(pool->name, name) == 0);
        rdb_drop_pool(pool);
    }
    return NULL;
}

static void registry(void)
{
    pthread_barrier_t start;
    CHECK(pthread_barrier_init(&start, NULL, THREADS + 1) == 0);
    struct worker workers[THREADS] = {{0}};
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; ++i) {
        workers[i].id = i;
        workers[i].start = &start;
        CHECK(pthread_create(&threads[i], NULL, registry_worker, &workers[i]) == 0);
    }
    wait_barrier(&start);
    for (int i = 0; i < THREADS; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(pthread_barrier_destroy(&start) == 0);
    /* No concurrent registry reader or borrowed-handle destruction is assumed. */
    for (int i = 0; i < THREADS; ++i) {
        for (int round = 0; round < ROUNDS; ++round) {
            char name[64];
            snprintf(name, sizeof(name), "thread-%d-round-%d", i, round);
            CHECK(rdb_find_pool_by_name(name) == NULL);
        }
    }
    rdb_pool_t *again = register_pool("after-concurrent-drops");
    CHECK(again && rdb_find_pool_by_name("after-concurrent-drops") == again);
    rdb_drop_pool(again);
}

/* Readers only borrow a permanent pool (or look up absent names). The temporary
 * pools belong to the writers, so this never races destruction of a handle the
 * reader is using. Three barriers per round exercise both publication and drop. */
static void check_lookup(rdb_pool_t *permanent)
{
    rdb_pool_t *found = rdb_find_pool_by_name("lookup-permanent");
    CHECK(found == permanent);
    if (found) CHECK(strcmp(found->name, "lookup-permanent") == 0);
    CHECK(rdb_find_pool_by_name("lookup-missing") == NULL);
}

static void *lookup_worker(void *opaque)
{
    struct worker *worker = opaque;
    const int writer = worker->id < THREADS / 2;
    for (int round = 0; round < ROUNDS; ++round) {
        rdb_pool_t *temporary = NULL;
        char name[64];
        snprintf(name, sizeof(name), "lookup-writer-%d-%d", worker->id, round);
        wait_barrier(worker->start);
        if (writer) {
            temporary = register_pool(name);
            CHECK(temporary);
        } else {
            for (int n = 0; n < 8; ++n) check_lookup(worker->pool);
        }
        wait_barrier(worker->start);
        if (writer) {
            rdb_drop_pool(temporary);
        } else {
            for (int n = 0; n < 8; ++n) check_lookup(worker->pool);
        }
        wait_barrier(worker->start);
    }
    return NULL;
}

static void concurrent_lookup(int with_permanent)
{
    rdb_pool_t *permanent = with_permanent ? register_pool("lookup-permanent") : NULL;
    CHECK(!with_permanent || permanent);
    pthread_barrier_t phase;
    CHECK(pthread_barrier_init(&phase, NULL, THREADS + 1) == 0);
    struct worker workers[THREADS] = {{0}};
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; ++i) {
        workers[i].id = i;
        workers[i].pool = permanent;
        workers[i].start = &phase;
        CHECK(pthread_create(&threads[i], NULL, lookup_worker, &workers[i]) == 0);
    }
    for (int round = 0; round < ROUNDS; ++round) {
        wait_barrier(&phase);
        wait_barrier(&phase);
        wait_barrier(&phase);
        check_lookup(permanent);
    }
    for (int i = 0; i < THREADS; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(pthread_barrier_destroy(&phase) == 0);
    if (permanent) rdb_drop_pool(permanent);
    CHECK(rdb_find_pool_by_name("lookup-permanent") == NULL);
}

static void registry_lookup(void) { concurrent_lookup(1); }
static void registry_lookup_empty(void) { concurrent_lookup(0); }

static void *duplicate_worker(void *opaque)
{
    struct worker *worker = opaque;
    wait_barrier(worker->start);
    worker->pool = register_pool("contended-name");
    return NULL;
}

static void duplicate_name(void)
{
    pthread_barrier_t start;
    CHECK(pthread_barrier_init(&start, NULL, THREADS + 1) == 0);
    for (int round = 0; round < 20; ++round) {
        pthread_t threads[THREADS];
        struct worker workers[THREADS] = {{0}};
        for (int i = 0; i < THREADS; ++i) {
            workers[i].start = &start;
            CHECK(pthread_create(&threads[i], NULL, duplicate_worker, &workers[i]) == 0);
        }
        wait_barrier(&start);
        int winners = 0;
        rdb_pool_t *winner = NULL;
        for (int i = 0; i < THREADS; ++i) {
            CHECK(pthread_join(threads[i], NULL) == 0);
            if (workers[i].pool) { ++winners; winner = workers[i].pool; }
        }
        CHECK(winners == 1);
        CHECK(rdb_find_pool_by_name("contended-name") == winner);
        rdb_drop_pool(winner);
    }
    CHECK(pthread_barrier_destroy(&start) == 0);
}

static void *shared_worker(void *opaque)
{
    struct worker *worker = opaque;
    wait_barrier(worker->start);
    for (int n = 0; n < RECORDS; ++n) {
        item *node = checked_calloc(1, sizeof(*node));
        node->key = worker->id * RECORDS + n;
        CHECK(rdb_lock(worker->pool, __func__) == 0);
        CHECK(rdb_insert(worker->pool, node) == 1);
        CHECK(rdb_get(worker->pool, 0, &node->key) == node);
        rdb_unlock(worker->pool, __func__);
    }
    wait_barrier(worker->filled);
    wait_barrier(worker->release);
    for (int n = RECORDS - 1; n >= 0; --n) {
        int32_t key = worker->id * RECORDS + n;
        CHECK(rdb_lock(worker->pool, __func__) == 0);
        item *node = rdb_delete(worker->pool, 0, &key);
        CHECK(node && node->key == key);
        CHECK(rdb_get(worker->pool, 0, &key) == NULL);
        rdb_unlock(worker->pool, __func__);
        free(node);
    }
    return NULL;
}

static int check_sequence(void *opaque, void *next)
{
    item *node = opaque;
    int32_t *expected = next;
    CHECK(node->key == (*expected)++);
    return RDB_CB_OK;
}

static void shared_pool(void)
{
    rdb_pool_t *pool = register_pool("shared");
    CHECK(pool);
    pthread_barrier_t start, filled, release;
    CHECK(pthread_barrier_init(&start, NULL, THREADS + 1) == 0);
    CHECK(pthread_barrier_init(&filled, NULL, THREADS + 1) == 0);
    CHECK(pthread_barrier_init(&release, NULL, THREADS + 1) == 0);
    struct worker workers[THREADS];
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; ++i) {
        workers[i] = (struct worker){i, pool, &start, &filled, &release};
        CHECK(pthread_create(&threads[i], NULL, shared_worker, &workers[i]) == 0);
    }
    wait_barrier(&start);
    wait_barrier(&filled);
    CHECK(rdb_lock(pool, __func__) == 0);
    CHECK(pool->record_count == THREADS * RECORDS);
    int32_t expected = 0;
    rdb_iterate(pool, 0, check_sequence, &expected, NULL, NULL);
    CHECK(expected == THREADS * RECORDS);
    rdb_unlock(pool, __func__);
    wait_barrier(&release);
    for (int i = 0; i < THREADS; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(pool->record_count == 0 && pool->root[0] == NULL);
    CHECK(pthread_barrier_destroy(&start) == 0);
    CHECK(pthread_barrier_destroy(&filled) == 0);
    CHECK(pthread_barrier_destroy(&release) == 0);
    rdb_drop_pool(pool);
}

int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"registry", registry}, {"registry_lookup", registry_lookup},
        {"registry_lookup_empty", registry_lookup_empty},
        {"duplicate_name", duplicate_name}, {"shared_pool", shared_pool}
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
