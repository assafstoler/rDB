/* Readers use the reporting API; writers keep the normal pool/lifetime locks. */
#include "rdb_test_support.h"

enum { WRITERS = 3, READERS = 2, ROUNDS = 300, ITEMS = 8 };
typedef struct { rdb_bpp_t links; int32_t key; } item;
static pthread_barrier_t start;
struct worker { int id, registry; rdb_pool_t *a, *b; };

static void begin(void)
{
    int rc = pthread_barrier_wait(&start);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
}
static rdb_pool_t *make_pool(char *name)
{
    rdb_pool_t *pool = rdb_register_um_pool(name, 1, KEY_OFFSET(item, key, 1),
                                         RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool);
    return pool;
}
static void fill(rdb_pool_t *pool)
{
    for (int key = 0; key < ITEMS; ++key) {
        item *record = checked_calloc(1, sizeof(*record));
        record->key = key;
        CHECK(rdb_insert(pool, record) == 1);
    }
}
static int remove_even(void *record, void *unused)
{
    (void)unused;
    return ((item *)record)->key % 2 ? RDB_CB_OK : RDB_CB_DELETE_NODE;
}
static void *write_stats(void *opaque)
{
    struct worker *w = opaque;
    begin();
    for (int round = 0; round < ROUNDS; ++round) {
        char name[64];
        snprintf(name, sizeof(name), "stats-temporary-%d-%d", w->id, round);
        /* This is an allowed pool -> registry order. Reporting must never
         * acquire a pool lock while holding the registry lock. */
        CHECK(rdb_lock(w->a, __func__) == 0);
        if (w->registry) {
            rdb_pool_t *temporary = make_pool(name);
            CHECK(rdb_lock(temporary, __func__) == 0);
            fill(temporary);
            rdb_flush(temporary, NULL, NULL);
            rdb_unlock(temporary, __func__);
            rdb_drop_pool(temporary); // No other user borrows this handle.
        } else {
            CHECK(rdb_lock(w->b, __func__) == 0);
            fill(w->a);
            item *removed = rdb_delete_const(w->a, 0, 0);
            CHECK(removed); free(removed);
            CHECK(rdb_move_const(w->b, w->a, 0, 1));
            CHECK(rdb_move_const(w->a, w->b, 0, 1));
            rdb_iterate(w->a, 0, remove_even, NULL, NULL, NULL);
            rdb_flush(w->a, NULL, NULL);
            CHECK(w->a->record_count == 0 && w->b->record_count == 0);
            rdb_unlock(w->b, __func__);
        }
        rdb_unlock(w->a, __func__);
    }
    return NULL;
}
static void check_report(void)
{
    char report[4096];
    CHECK(rdb_print_pool_stats(report, sizeof(report)) == report);
    const char *row = report;
    while (strncmp(row, ":END:", 5) != 0) {
        char name[64];
        unsigned count;
        int used = 0;
        CHECK(sscanf(row, "Pool: %63s : %u\n%n", name, &count, &used) == 2);
        CHECK(used > 0 && count <= ITEMS && strncmp(name, "stats-", 6) == 0);
        row += used;
    }
    CHECK(strcmp(row, ":END:") == 0);
    /* Force every short-buffer exit while registry writers are active. */
    for (int size = 1; size <= 5; ++size) {
        char tiny[8];
        memset(tiny, 'X', sizeof(tiny));
        CHECK(rdb_print_pool_stats(tiny + 1, size) == tiny + 1);
        CHECK(tiny[0] == 'X' && tiny[size + 1] == 'X');
        CHECK(tiny[size] == '\0');
    }
}
static void *read_stats(void *unused)
{
    (void)unused;
    begin();
    for (int round = 0; round < ROUNDS; ++round) check_report();
    return NULL;
}
static void concurrent(int registry)
{
    struct worker workers[WRITERS];
    pthread_t threads[WRITERS + READERS];
    CHECK(pthread_barrier_init(&start, NULL, WRITERS + READERS) == 0);
    for (int i = 0; i < WRITERS; ++i) {
        char name[64];
        workers[i].id = i; workers[i].registry = registry;
        snprintf(name, sizeof(name), "stats-%d-a", i);
        workers[i].a = make_pool(name);
        snprintf(name, sizeof(name), "stats-%d-b", i);
        workers[i].b = make_pool(name);
        CHECK(pthread_create(&threads[i], NULL, write_stats, &workers[i]) == 0);
    }
    for (int i = WRITERS; i < WRITERS + READERS; ++i)
        CHECK(pthread_create(&threads[i], NULL, read_stats, NULL) == 0);
    for (int i = 0; i < WRITERS + READERS; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(pthread_barrier_destroy(&start) == 0);
    check_report();
    for (int i = 0; i < WRITERS; ++i) {
        CHECK(workers[i].a->record_count == 0 && workers[i].b->record_count == 0);
        rdb_drop_pool(workers[i].b);
        rdb_drop_pool(workers[i].a);
    }
    char empty[16];
    CHECK(strcmp(rdb_print_pool_stats(empty, sizeof(empty)), ":END:") == 0);
}
static void mutations(void) { concurrent(0); }
static void registry(void) { concurrent(1); }
static void locked_pool(void)
{
    rdb_pool_t *pool = make_pool("stats-locked");
    CHECK(rdb_lock(pool, __func__) == 0);
    check_report(); // Would deadlock if reporting acquired this pool's lock.
    rdb_unlock(pool, __func__);
    rdb_drop_pool(pool);
}
int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"mutations", mutations}, {"registry", registry}, {"locked_pool", locked_pool}
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
