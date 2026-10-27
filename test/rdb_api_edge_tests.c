/* Public API contracts and bounded structural checks; no production overrides. */
#include "rdb_test_support.h"
#include <limits.h>
#include <stdbool.h>

enum { INDEXES = 3, CAPACITY = 96 };
typedef struct record {
    rdb_bpp_t links[INDEXES];
    int32_t key[INDEXES];
    unsigned id;
} record;

static rdb_pool_t *make_pool(char *name, const int flags[INDEXES])
{
    rdb_pool_t *pool = rdb_register_um_pool(name, INDEXES,
            KEY_OFFSET(record, key[0], INDEXES), flags[0] | RDB_BTREE, NULL, 0);
    CHECK(pool);
    for (int i = 1; i < INDEXES; ++i)
        CHECK(rdb_register_um_idx(pool, i, KEY_OFFSET(record, key[i], INDEXES),
                    flags[i] | RDB_BTREE, NULL, 0) == i);
    return pool;
}

static rdb_pool_t *tree_pool(char *name)
{
    const int flags[] = { RDB_KINT32, RDB_KINT32, RDB_KINT32 };
    return make_pool(name, flags);
}

static record *make_record(unsigned id)
{
    record *item = checked_calloc(1, sizeof(*item));
    item->id = id;
    item->key[0] = (int32_t)id * 2;
    item->key[1] = 1000 - (int32_t)id * 3;
    item->key[2] = (int32_t)id * 5;
    return item;
}

struct visit {
    record *items[CAPACITY];
    size_t count;
};

static int collect(void *data, void *opaque)
{
    struct visit *visit = opaque;
    CHECK(visit->count < CAPACITY);
    visit->items[visit->count++] = data;
    return RDB_CB_OK;
}

/* Child pointers in tree indexes point to records, not to their pointer packs.
 * Check membership before dereferencing, and bound traversal to detect cycles. */
static int check_tree(record *node, int index, record *const records[CAPACITY],
                      bool seen[CAPACITY], int64_t low, int64_t high)
{
    if (!node) return 0;
    size_t id;
    for (id = 0; id < CAPACITY && records[id] != node; ++id) {}
    CHECK(id < CAPACITY);
    CHECK(!seen[id]);
    seen[id] = true;
    CHECK(node->key[index] > low && node->key[index] < high);
    int left = check_tree(node->links[index].left, index, records, seen,
                          low, node->key[index]);
    int right = check_tree(node->links[index].right, index, records, seen,
                           node->key[index], high);
    CHECK(abs(right - left) <= 1);
    CHECK(node->links[index].balance == right - left);
    return 1 + (left > right ? left : right);
}

static void verify_trees(rdb_pool_t *pool, record *const records[CAPACITY])
{
    size_t expected = 0;
    for (size_t id = 0; id < CAPACITY; ++id) expected += records[id] != NULL;
    CHECK(pool->record_count == expected);
    for (int index = 0; index < INDEXES; ++index) {
        bool seen[CAPACITY] = {false};
        check_tree((record *)pool->root[index], index, records, seen,
                   INT64_MIN, INT64_MAX);
        struct visit visit = {0};
        rdb_iterate(pool, index, collect, &visit, NULL, NULL);
        CHECK(visit.count == expected);
        for (size_t i = 1; i < visit.count; ++i)
            CHECK(visit.items[i - 1]->key[index] < visit.items[i]->key[index]);
        for (size_t id = 0; id < CAPACITY; ++id) {
            CHECK(seen[id] == (records[id] != NULL));
            if (records[id])
                CHECK(rdb_get(pool, index, &records[id]->key[index]) == records[id]);
        }
    }
}

static void pools(void)
{
    /* Tail, middle, head and only-element removal, followed by name reuse. */
    const int orders[][4] = {{0, 2, 3, 1}, {3, 2, 1, 0}, {1, 0, 2, 3}};
    char names[][16] = {"pool-a", "pool-b", "pool-c", "pool-d"};
    for (size_t round = 0; round < ARRAY_COUNT(orders); ++round) {
        rdb_pool_t *handles[4];
        bool alive[4] = {true, true, true, true};
        for (int i = 0; i < 4; ++i) handles[i] = tree_pool(names[i]);
        CHECK(rdb_find_pool_by_name("missing") == NULL);
        for (int n = 0; n < 4; ++n) {
            int index = orders[round][n];
            rdb_drop_pool(handles[index]);
            alive[index] = false;
            for (int i = 0; i < 4; ++i)
                CHECK(rdb_find_pool_by_name(names[i]) == (alive[i] ? handles[i] : NULL));
        }
        rdb_drop_pool(NULL);
        rdb_clean(0);
        rdb_init();
    }
}

static void empty(void)
{
    rdb_pool_t *pool = tree_pool("empty");
    int32_t key = 2;
    struct visit visit = {0};
    CHECK(rdb_get(pool, 0, &key) == NULL);
    CHECK(rdb_get(pool, 0, NULL) == NULL);
    CHECK(rdb_get_const(pool, 0, key) == NULL);
    CHECK(rdb_delete(pool, 0, &key) == NULL);
    CHECK(rdb_delete_const(pool, 0, key) == NULL);
    CHECK(rdb_insert(pool, NULL) == -1);
    CHECK(rdb_insert_one(pool, 1, NULL) == -1);
    rdb_iterate(pool, 0, collect, &visit, NULL, NULL);
    rdb_flush(pool, NULL, NULL);
    CHECK(visit.count == 0 && pool->record_count == 0);
    rdb_iterate(NULL, 0, collect, &visit, NULL, NULL);
    CHECK(rdb_error_string && strstr(rdb_error_string, "NULL pool"));
}


/* Unlike whole-record deletion, this entry point directly searches an index.
 * Exercise misses without passing an invalid record pointer or changing keys
 * while they are indexed. Other indexes must remain byte-for-byte intact. */
static void delete_one_misses(void)
{
    const unsigned counts[] = {0, 1, 7};
    for (int index = 0; index < INDEXES; ++index)
    for (size_t c = 0; c < ARRAY_COUNT(counts); ++c) {
        rdb_pool_t *pool = tree_pool("delete-one-misses");
        record *records[CAPACITY] = {0};
        record before[7];
        for (unsigned n = 0; n < counts[c]; ++n) {
            unsigned id = n * 2;
            records[id] = make_record(id);
            CHECK(rdb_insert(pool, records[id]) == INDEXES);
        }
        unsigned char pool_before[sizeof(*pool)];
        memcpy(pool_before, pool, sizeof(pool_before));
        for (unsigned n = 0; n < counts[c]; ++n)
            memcpy(&before[n], records[n * 2], sizeof(record));
        const int32_t missing[] = {INT32_MIN, INT32_MAX, -1, 1, 3, 7, 997, 991, 985};
        for (size_t k = 0; k < ARRAY_COUNT(missing); ++k) {
            CHECK(rdb_get(pool, index, &missing[k]) == NULL);
            record candidate = {0}, candidate_before;
            candidate.key[index] = missing[k];
            memcpy(&candidate_before, &candidate, sizeof(candidate));
            for (int attempt = 0; attempt < 3; ++attempt) {
                CHECK(rdb_delete_one(pool, index, &candidate) == 0);
                CHECK(memcmp(pool_before, pool, sizeof(pool_before)) == 0);
                CHECK(memcmp(&candidate_before, &candidate, sizeof(candidate)) == 0);
                for (unsigned n = 0; n < counts[c]; ++n)
                    CHECK(memcmp(&before[n], records[n * 2], sizeof(record)) == 0);
                verify_trees(pool, records);
            }
        }
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}

/* Outputs belong to the lookup, not to the caller's previous search. Seed
 * distinct non-NULL values so missing assignments cannot pass accidentally. */
static void check_neighbors(rdb_pool_t *pool, int index, void *key,
                            void *match, void *lower, void *upper)
{
    char sentinels[2];
    void *before = &sentinels[0], *after = &sentinels[1];
    CHECK(rdb_get_neigh(pool, index, key, &before, &after) == match);
    CHECK(before == lower);
    CHECK(after == upper);
}

static void neighbors_empty(void)
{
    rdb_pool_t *pool = tree_pool("empty-neighbors");
    for (int round = 0; round < 2; ++round) {
        int32_t key = 5;
        for (int index = 0; index < INDEXES; ++index) {
            check_neighbors(pool, index, &key, NULL, NULL, NULL);
            check_neighbors(pool, index, NULL, NULL, NULL, NULL);
        }
        if (round == 0) {
            CHECK(rdb_insert(pool, make_record(1)) == INDEXES);
            rdb_flush(pool, NULL, NULL);
        }
    }
}

static void neighbors_root(void)
{
    rdb_pool_t *pool = tree_pool("root-neighbors");
    for (unsigned id = 0; id < 8; ++id)
        CHECK(rdb_insert(pool, make_record(id)) == INDEXES);
    while (pool->record_count) {
        for (int index = 0; index < INDEXES; ++index) {
            void *root = rdb_get(pool, index, NULL);
            CHECK(root != NULL && root == pool->root[index]);
            check_neighbors(pool, index, NULL, root, NULL, NULL);
        }
        record *root = rdb_get(pool, 0, NULL);
        CHECK(rdb_delete(pool, 0, &root->key[0]) == root);
        free(root);
    }
}

int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"pools", pools}, {"empty", empty}, {"delete_one_misses", delete_one_misses},
        {"neighbors_empty", neighbors_empty}, {"neighbors_root", neighbors_root}
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
