/* Separate, correctly locked pools must not share mutable dump state. */
#include "rdb_test_support.h"
#include <unistd.h>

enum { THREADS = 6, INDEXES = 3, RECORDS = 31, ROUNDS = 32 };
enum layout { TREES, QUEUES, MIXED };
typedef struct { rdb_bpp_t links[INDEXES]; int32_t key; } item;

struct worker {
    rdb_pool_t *pool;
    pthread_barrier_t *phase;
    int index, count;
    item *records[RECORDS];
    unsigned char before[RECORDS][sizeof(item)];
    char expected[RECORDS][64];
};

static void wait_barrier(pthread_barrier_t *phase)
{
    int rc = pthread_barrier_wait(phase);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
}

static void *dump_worker(void *opaque)
{
    struct worker *w = opaque;
    for (int round = 0; round < ROUNDS; ++round) {
        wait_barrier(w->phase);
        CHECK(rdb_lock(w->pool, __func__) == 0);
        rdb_dump(w->pool, w->index, "\n");
        rdb_unlock(w->pool, __func__);
    }
    return NULL;
}

static void prepare(struct worker *w, int id, enum layout layout,
                    pthread_barrier_t *phase)
{
    char name[64];
    snprintf(name, sizeof(name), "concurrent-dump-%d", id);
    w->index = id % INDEXES;
    w->count = 1 + 6 * id; /* Different tree depths, including a singleton. */
    w->phase = phase;
    int flag = RDB_KINT32;
    if (layout == QUEUES || (layout == MIXED && id % 3 != 0))
        flag = id % 2 ? RDB_KFIFO : RDB_KLIFO;
    int offset = KEY_OFFSET(item, key, INDEXES);
    w->pool = rdb_register_um_pool(name, INDEXES, offset,
            RDB_BTREE | (w->index == 0 ? flag : RDB_KINT32), NULL, 0);
    CHECK(w->pool);
    for (int slot = 1; slot < INDEXES; ++slot)
        CHECK(rdb_register_um_idx(w->pool, slot, offset,
                RDB_BTREE | (w->index == slot ? flag : RDB_KINT32), NULL, 0) == slot);
    for (int n = 0; n < w->count; ++n) {
        w->records[n] = checked_calloc(1, sizeof(item));
        w->records[n]->key = id * 100 + n + 1;
        CHECK(rdb_insert(w->pool, w->records[n]) == INDEXES);
    }
    for (int n = 0; n < w->count; ++n) {
        memcpy(w->before[n], w->records[n], sizeof(item));
        int position = flag == RDB_KLIFO ? w->count - 1 - n : n;
        int length;
        if (flag == RDB_KINT32)
            length = snprintf(w->expected[position], sizeof(w->expected[position]),
                    "%d\n", w->records[n]->key);
        else
            length = snprintf(w->expected[position], sizeof(w->expected[position]),
                    "%p\n", (void *)&w->records[n]->links[w->index]);
        CHECK(length > 0 && (size_t)length < sizeof(w->expected[position]));
    }
}

static void exercise(enum layout layout)
{
    pthread_barrier_t phase;
    CHECK(pthread_barrier_init(&phase, NULL, THREADS) == 0);
    struct worker workers[THREADS] = {0};
    for (int id = 0; id < THREADS; ++id) prepare(&workers[id], id, layout, &phase);

    /* Redirect once before creating workers. Do not serialize whole dumps via
     * stdout: that would hide the cross-pool race. Each item uses one fprintf. */
    FILE *output = tmpfile();
    CHECK(output);
    CHECK(fflush(stdout) == 0);
    int saved = dup(STDOUT_FILENO);
    CHECK(saved >= 0);
    CHECK(dup2(fileno(output), STDOUT_FILENO) == STDOUT_FILENO);
    pthread_t threads[THREADS];
    for (int id = 0; id < THREADS; ++id)
        CHECK(pthread_create(&threads[id], NULL, dump_worker, &workers[id]) == 0);
    for (int id = 0; id < THREADS; ++id) CHECK(pthread_join(threads[id], NULL) == 0);
    CHECK(fflush(stdout) == 0);
    CHECK(dup2(saved, STDOUT_FILENO) == STDOUT_FILENO);
    CHECK(close(saved) == 0);
    CHECK(pthread_barrier_destroy(&phase) == 0);

    /* Interleaving is allowed, but each pool's exact sequence must be retained.
     * Keys/addresses are unique across pools, identifying each line's owner. */
    rewind(output);
    int seen[THREADS] = {0};
    char line[64];
    while (fgets(line, sizeof(line), output)) {
        int owner = -1;
        for (int id = 0; id < THREADS; ++id) {
            struct worker *w = &workers[id];
            if (strcmp(line, w->expected[seen[id] % w->count]) == 0) {
                CHECK(owner == -1);
                owner = id;
            }
        }
        CHECK(owner >= 0);
        ++seen[owner];
        CHECK(seen[owner] <= ROUNDS * workers[owner].count);
    }
    CHECK(!ferror(output));
    CHECK(fclose(output) == 0);
    for (int id = 0; id < THREADS; ++id) {
        struct worker *w = &workers[id];
        CHECK(seen[id] == ROUNDS * w->count);
        CHECK(w->pool->record_count == (uint32_t)w->count);
        int key_index = (w->index + 1) % INDEXES;
        for (int n = 0; n < w->count; ++n) {
            CHECK(memcmp(w->before[n], w->records[n], sizeof(item)) == 0);
            CHECK(rdb_get(w->pool, key_index, &w->records[n]->key) == w->records[n]);
        }
        rdb_flush(w->pool, NULL, NULL);
        rdb_drop_pool(w->pool);
    }
}

static void trees(void) { exercise(TREES); }
static void queues(void) { exercise(QUEUES); }
static void mixed(void) { exercise(MIXED); }

int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"trees", trees}, {"queues", queues}, {"mixed", mixed}
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
