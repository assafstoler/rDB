/* Cross-layout move oracle and contended two-pool moves; no timing assertions. */
#include "rdb_test_support.h"
#include <stdbool.h>
#include <limits.h>

enum { RECORDS = 12, INDEXES = RDB_POOL_MAX_IDX };
typedef struct { rdb_bpp_t links[INDEXES]; int32_t key[INDEXES]; unsigned id; } item;
typedef struct {
    rdb_pool_t *pool;
    unsigned indexes, mode;
    int kind[INDEXES];
    unsigned queue[INDEXES][RECORDS], length[INDEXES];
} state;
static item records[RECORDS];
static unsigned owner[RECORDS], random_state = 0xa93b612f;
static unsigned moved, rejected, same_pool, matrix_cases;

static unsigned random_next(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static void open_pool(state *s, char *name)
{
    const int kinds[] = {RDB_KFIFO, RDB_KLIFO, RDB_KINT32};
    for (unsigned i = 0; i < s->indexes; ++i) {
        s->kind[i] = s->mode == 0 ? RDB_KINT32 : s->mode == 1 ? RDB_KFIFO :
                     s->mode == 2 ? RDB_KLIFO : kinds[i % 3];
        int offset = KEY_OFFSET(item, key[i], s->indexes);
        if (i == 0) {
            s->pool = rdb_register_um_pool(name, s->indexes, offset,
                                         RDB_BTREE | s->kind[i], NULL, 0);
            CHECK(s->pool);
        } else CHECK(rdb_register_um_idx(s->pool, i, offset,
                                        RDB_BTREE | s->kind[i], NULL, 0) == (int)i);
    }
}
static void add(state *s, unsigned id)
{
    for (unsigned i = 0; i < s->indexes; ++i) {
        if (s->kind[i] == RDB_KINT32) continue;
        unsigned n = s->length[i]++;
        CHECK(n < RECORDS);
        if (s->kind[i] == RDB_KFIFO) s->queue[i][n] = id;
        else {
            memmove(s->queue[i] + 1, s->queue[i], n * sizeof(unsigned));
            s->queue[i][0] = id;
        }
    }
}
static void remove_id(state *s, unsigned id)
{
    for (unsigned i = 0; i < s->indexes; ++i) {
        if (s->kind[i] == RDB_KINT32) continue;
        unsigned n = s->length[i], pos = 0;
        while (pos < n && s->queue[i][pos] != id) ++pos;
        CHECK(pos < n);
        memmove(s->queue[i] + pos, s->queue[i] + pos + 1, (n - pos - 1) * sizeof(unsigned));
        --s->length[i];
    }
}
struct traversal { unsigned expected[RECORDS], count, at; };
static int visit(void *p, void *opaque)
{
    struct traversal *t = opaque;
    CHECK(t->at < t->count && p == &records[t->expected[t->at++]]);
    return RDB_CB_OK;
}
static int tree_height(void *p, unsigned index, unsigned pool, bool seen[RECORDS],
                       int64_t low, int64_t high)
{
    if (!p) return 0;
    unsigned id = 0;
    while (id < RECORDS && p != &records[id]) ++id;
    CHECK(id < RECORDS && owner[id] == pool && !seen[id]);
    seen[id] = true;
    item *r = p;
    CHECK(r->key[index] > low && r->key[index] < high);
    int l = tree_height(r->links[index].left, index, pool, seen, low, r->key[index]);
    int h = tree_height(r->links[index].right, index, pool, seen, r->key[index], high);
    CHECK(abs(h - l) <= 1 && r->links[index].balance == h - l);
    return (h > l ? h : l) + 1;
}
static void verify(state *s, unsigned pool)
{
    unsigned count = 0;
    for (unsigned id = 0; id < RECORDS; ++id) count += owner[id] == pool;
    CHECK(s->pool->record_count == count && rdb_pool_get_fault(s->pool, NULL) == 0);
    for (unsigned i = 0; i < s->indexes; ++i) {
        struct traversal t = {0};
        if (s->kind[i] == RDB_KINT32) {
            bool seen[RECORDS] = {0};
            tree_height(s->pool->root[i], i, pool, seen, INT64_MIN, INT64_MAX);
            for (unsigned id = 0; id < RECORDS; ++id) {
                CHECK(seen[id] == (owner[id] == pool));
                if (owner[id] != pool) continue;
                CHECK(rdb_get(s->pool, i, &records[id].key[i]) == &records[id]);
                unsigned n = t.count++;
                while (n && records[t.expected[n - 1]].key[i] > records[id].key[i]) {
                    t.expected[n] = t.expected[n - 1];
                    --n;
                }
                t.expected[n] = id;
            }
        } else {
            t.count = s->length[i];
            CHECK(t.count == count);
            memcpy(t.expected, s->queue[i], t.count * sizeof(unsigned));
            CHECK((void *)s->pool->root[i] == (count ? &records[t.expected[0]] : NULL));
            CHECK((void *)s->pool->tail[i] == (count ? &records[t.expected[count - 1]] : NULL));
            for (unsigned n = 0; n < count; ++n) {
                item *r = &records[t.expected[n]];
                CHECK(owner[r->id] == pool);
                CHECK(r->links[i].left == (n ? &records[t.expected[n - 1]].links[i] : NULL));
                CHECK(r->links[i].right == (n + 1 < count ? &records[t.expected[n + 1]].links[i] : NULL));
            }
        }
        CHECK(t.count == count);
        rdb_iterate(s->pool, i, visit, &t, NULL, NULL);
        CHECK(t.at == count);
    }
}
static bool accepts(state *dst, unsigned destination, unsigned id)
{
    for (unsigned i = 0; i < dst->indexes; ++i) {
        if (dst->kind[i] != RDB_KINT32) continue;
        for (unsigned other = 0; other < RECORDS; ++other)
            if (owner[other] == destination && other != id && records[id].key[i] == records[other].key[i])
                return false;
    }
    return true;
}
static void initialize_pair(state pools[2], bool collisions)
{
    open_pool(&pools[0], "matrix-a"); open_pool(&pools[1], "matrix-b");
    memset(records, 0, sizeof(records));
    for (unsigned id = 0; id < RECORDS; ++id) {
        records[id].id = id;
        owner[id] = id < RECORDS / 2 ? 0 : 1;
        for (unsigned i = 0; i < INDEXES; ++i) records[id].key[i] = id * 100 + i;
    }
    for (int i = collisions ? (int)pools[1].indexes - 1 : -1; i >= 0; --i)
        if (pools[1].kind[i] == RDB_KINT32) {
            records[0].key[i] = records[RECORDS / 2].key[i];
            break; // Force collisions late in a destination's multi-index insert.
        }
    for (unsigned id = 0; id < RECORDS; ++id) {
        CHECK(rdb_insert(pools[owner[id]].pool, &records[id]) == (int)pools[owner[id]].indexes);
        add(&pools[owner[id]], id);
    }
}
static void finish_pair(state pools[2])
{
    for (unsigned n = 0; n < 2; ++n) {
        while (pools[n].pool->record_count) {
            item *record = (item *)pools[n].pool->root[0];
            CHECK(rdb_delete(pools[n].pool, 0, &record->key[0]) == record);
            remove_id(&pools[n], record->id);
            owner[record->id] = 2;
        }
        verify(&pools[n], n);
        rdb_drop_pool(pools[n].pool);
    }
}
static void exercise(unsigned a, unsigned b, unsigned ma, unsigned mb)
{
    printf("layout source=%u/%u destination=%u/%u seed=0x%x\n", a, ma, b, mb, random_state);
    state pools[2] = {{.indexes=a, .mode=ma}, {.indexes=b, .mode=mb}};
    initialize_pair(pools, true);
    for (unsigned step = 0; step < 128; ++step) {
        verify(&pools[0], 0); verify(&pools[1], 1);
        unsigned from = random_next() % 2, to = random_next() % 8 == 0 ? from : 1 - from;
        state *src = &pools[from], *dst = &pools[to];
        if (!src->pool->record_count) continue;
        unsigned index = random_next() % src->indexes, id = random_next() % RECORDS;
        while (owner[id] != from) id = (id + 1) % RECORDS;
        if (src->kind[index] != RDB_KINT32) id = src->queue[index][0];
        bool null_destination = random_next() % 7 == 0;
        bool accepted = !null_destination && accepts(dst, to, id);
        rdb_pool_t *target = null_destination ? NULL : dst->pool;
        void *expected = accepted ? &records[id] : NULL;
        switch (random_next() % 3) {
        case 0: CHECK(rdb_move(target, src->pool, index, &records[id].key[index]) == expected); break;
        case 1: CHECK(rdb_move_const(target, src->pool, index, records[id].key[index]) == expected); break;
        case 2: CHECK(rdb_move2(target, src->pool, index, &records[id].key[index]) == (accepted ? 0 : -2)); break;
        }
        if (accepted) {
            remove_id(src, id); owner[id] = to; add(dst, id);
            ++moved; same_pool += from == to;
        } else ++rejected;
    }
    verify(&pools[0], 0); verify(&pools[1], 1);
    finish_pair(pools);
    ++matrix_cases;
}
static void matrix(void)
{
    const unsigned counts[] = {1, 2, 3, RDB_POOL_MAX_IDX};
    for (unsigned a = 0; a < ARRAY_COUNT(counts); ++a)
    for (unsigned b = 0; b < ARRAY_COUNT(counts); ++b)
    for (unsigned ma = 0; ma < 4; ++ma)
    for (unsigned mb = 0; mb < 4; ++mb) exercise(counts[a], counts[b], ma, mb);
    CHECK(matrix_cases == 256 && moved > 0 && rejected > 0 && same_pool > 0);
    printf("PASS: %u layout pairs; %u successful moves (%u same-pool), %u rejections\n",
           matrix_cases, moved, same_pool, rejected);
}

enum { THREADS = 8, LOOPS = 4000 };
static state threaded_pools[2];
static pthread_barrier_t ready;
static void *move_worker(void *opaque)
{
    unsigned id = *(unsigned *)opaque;
    int rc = pthread_barrier_wait(&ready);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned step = 0; step < LOOPS; ++step) {
        CHECK(rdb_lock(threaded_pools[0].pool, __func__) == 0);
        CHECK(rdb_lock(threaded_pools[1].pool, __func__) == 0);
        unsigned from = owner[id], to = 1 - from;
        state *src = &threaded_pools[from], *dst = &threaded_pools[to];
        // Index 2 is a tree in both pools, so each thread selects its own record.
        switch (step % 3) {
        case 0: CHECK(rdb_move(dst->pool, src->pool, 2, &records[id].key[2]) == &records[id]); break;
        case 1: CHECK(rdb_move_const(dst->pool, src->pool, 2, records[id].key[2]) == &records[id]); break;
        case 2: CHECK(rdb_move2(dst->pool, src->pool, 2, &records[id].key[2]) == 0); break;
        }
        remove_id(src, id); owner[id] = to; add(dst, id);
        CHECK(rdb_get(dst->pool, 2, &records[id].key[2]) == &records[id]);
        CHECK(rdb_get(src->pool, 2, &records[id].key[2]) == NULL);
        CHECK(src->pool->record_count + dst->pool->record_count == RECORDS);
        // Sample the full independent oracle while holding the same lock pair.
        if (step % 32 == 0) {
            verify(&threaded_pools[0], 0);
            verify(&threaded_pools[1], 1);
        }
        rdb_unlock(threaded_pools[1].pool, __func__);
        rdb_unlock(threaded_pools[0].pool, __func__);
    }
    return NULL;
}
static void threaded(void)
{
    for (unsigned n = 0; n < 2; ++n)
        threaded_pools[n] = (state){.indexes=3, .mode=3};
    initialize_pair(threaded_pools, false);
    CHECK(pthread_barrier_init(&ready, NULL, THREADS) == 0);
    pthread_t threads[THREADS];
    unsigned ids[THREADS];
    for (unsigned i = 0; i < THREADS; ++i) {
        ids[i] = i;
        CHECK(pthread_create(&threads[i], NULL, move_worker, &ids[i]) == 0);
    }
    for (unsigned i = 0; i < THREADS; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(pthread_barrier_destroy(&ready) == 0);
    verify(&threaded_pools[0], 0); verify(&threaded_pools[1], 1);
    finish_pair(threaded_pools);
    printf("Verified %u contended moves\n", THREADS * LOOPS);
}
int main(int argc, char **argv)
{
    const struct test_case cases[] = {{"matrix", matrix}, {"threaded", threaded}};
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
