/* Moves must not orphan records or reorder queues on destination rejection. */
#include "rdb_test_support.h"

enum { INDEXES = 3, MAX_ITEMS = 16 };
typedef struct {
    rdb_bpp_t links[INDEXES];
    int32_t key[INDEXES];
} record;

struct visit { void *items[MAX_ITEMS]; size_t count; };
struct snapshot {
    struct visit indexes[INDEXES];
    rdb_bpp_t *roots[INDEXES], *tails[INDEXES];
    unsigned count;
};

static int collect(void *item, void *opaque)
{
    struct visit *visit = opaque;
    CHECK(visit->count < MAX_ITEMS);
    visit->items[visit->count++] = item;
    return RDB_CB_OK;
}

static struct snapshot snapshot(rdb_pool_t *pool)
{
    struct snapshot result = {0};
    result.count = pool->record_count;
    for (int i = 0; i < INDEXES; ++i) {
        result.roots[i] = pool->root[i];
        result.tails[i] = pool->tail[i];
        rdb_iterate(pool, i, collect, &result.indexes[i], NULL, NULL);
    }
    return result;
}

static void check_snapshot(rdb_pool_t *pool, const struct snapshot *before)
{
    struct snapshot after = snapshot(pool);
    CHECK(after.count == before->count);
    for (int i = 0; i < INDEXES; ++i) {
        if (pool->FLAGS[i] & RDB_NOKEYS) {
            CHECK(after.roots[i] == before->roots[i]);
            CHECK(after.tails[i] == before->tails[i]);
        }
        CHECK(after.indexes[i].count == before->indexes[i].count);
        for (size_t n = 0; n < after.indexes[i].count; ++n) {
            CHECK(after.indexes[i].items[n] == before->indexes[i].items[n]);
            if (pool->FLAGS[i] & RDB_NOKEYS) {
                /* Forward iteration alone cannot detect broken back-links. */
                rdb_bpp_t *link = (rdb_bpp_t *)after.indexes[i].items[n] + i;
                rdb_bpp_t *previous = n
                    ? (rdb_bpp_t *)before->indexes[i].items[n - 1] + i : NULL;
                rdb_bpp_t *next = n + 1 < before->indexes[i].count
                    ? (rdb_bpp_t *)before->indexes[i].items[n + 1] + i : NULL;
                CHECK(link->left == previous && link->right == next);
            }
        }
    }
}

static void unchanged(rdb_pool_t *pool, const struct snapshot *before)
{
    check_snapshot(pool, before);
}

static rdb_pool_t *make_pool(char *name, int queues)
{
    int flags[] = {RDB_KINT32, RDB_KINT32, RDB_KINT32};
    if (queues) { flags[0] = RDB_KFIFO; flags[1] = RDB_KLIFO; }
    rdb_pool_t *pool = rdb_register_um_pool(name, INDEXES,
            KEY_OFFSET(record, key[0], INDEXES), RDB_BTREE | flags[0], NULL, 0);
    CHECK(pool);
    for (int i = 1; i < INDEXES; ++i)
        CHECK(rdb_register_um_idx(pool, i, KEY_OFFSET(record, key[i], INDEXES),
                    RDB_BTREE | flags[i], NULL, 0) == i);
    return pool;
}

static record *make_record(int id)
{
    record *item = checked_calloc(1, sizeof(*item));
    for (int i = 0; i < INDEXES; ++i) item->key[i] = id + 100 * i;
    return item;
}

static void expect_move(int variant, rdb_pool_t *dst, rdb_pool_t *src,
                        int index, void *key, void *expected, int status)
{
    /* The API must work with the caller already holding both pool locks. */
    CHECK(rdb_lock(src, __func__) == 0);
    if (dst && dst != src) CHECK(rdb_lock(dst, __func__) == 0);
    if (variant == 0) CHECK(rdb_move(dst, src, index, key) == expected);
    if (variant == 1) {
        /* Only integer-key fixtures pass a non-NULL key to the scalar API. */
        CHECK(!key || (src->FLAGS[index] & RDB_KINT32));
        CHECK(rdb_move_const(dst, src, index, key ? *(int32_t *)key : 0) == expected);
    }
    if (variant == 2) CHECK(rdb_move2(dst, src, index, key) == status);
    if (dst && dst != src) rdb_unlock(dst, __func__);
    rdb_unlock(src, __func__);
}

static void collisions(int variant)
{
    for (int src_queues = 0; src_queues < 2; ++src_queues)
    for (int dst_queues = 0; dst_queues < 2; ++dst_queues)
    for (int collision = dst_queues ? 2 : 0; collision < INDEXES; ++collision)
    for (int position = 0; position < 3; ++position) {
        rdb_pool_t *src = make_pool("source", src_queues);
        rdb_pool_t *dst = make_pool("destination", dst_queues);
        record *items[3];
        for (int i = 0; i < 3; ++i) {
            items[i] = make_record(i + 1);
            CHECK(rdb_insert(src, items[i]) == INDEXES);
            CHECK(rdb_insert(dst, make_record(i + 11)) == INDEXES);
        }
        record *moving = items[position];
        record *blocker = make_record(20);
        blocker->key[collision] = moving->key[collision];
        CHECK(rdb_insert(dst, blocker) == INDEXES);
        struct snapshot source_before = snapshot(src), destination_before = snapshot(dst);
        for (int attempt = 0; attempt < 3; ++attempt) {
            expect_move(variant, dst, src, 2, &moving->key[2], NULL, -2);
            unchanged(src, &source_before);
            unchanged(dst, &destination_before);
            CHECK(rdb_error_string && strstr(rdb_error_string, "duplicate"));
        }
        /* Retrying after removing the collision must transfer the same object. */
        CHECK(rdb_delete(dst, collision, &blocker->key[collision]) == blocker);
        free(blocker);
        expect_move(variant, dst, src, 2, &moving->key[2], moving, 0);
        CHECK(src->record_count == 2 && dst->record_count == 4);
        CHECK(rdb_get(src, 2, &moving->key[2]) == NULL);
        CHECK(rdb_get(dst, 2, &moving->key[2]) == moving);
        if (dst_queues) {
            CHECK((record *)dst->tail[0] == moving);
            CHECK((record *)dst->root[1] == moving);
        }
        rdb_flush(src, NULL, NULL);
        rdb_flush(dst, NULL, NULL);
        rdb_drop_pool(src);
        rdb_drop_pool(dst);
    }
}
static void collision_pointer(void) { collisions(0); }
static void collision_constant(void) { collisions(1); }
static void collision_status(void) { collisions(2); }

/* Distinguish logical membership/order from an AVL tree's internal shape. */
static int reject_after_comparisons;
static int injected_compare(void *old_key, void *new_key)
{
    if (reject_after_comparisons && --reject_after_comparisons == 0) return 0;
    return key_cmp_int32(old_key, new_key);
}

static void late_rejection(void)
{
    for (int variant = 0; variant < 3; ++variant)
    for (int queues = 0; queues < 2; ++queues)
    for (int position = 0; position < 3; ++position) {
        rdb_pool_t *src = make_pool("source", queues);
        rdb_pool_t *dst = make_pool("destination", 1);
        record *items[3];
        for (int i = 0; i < 3; ++i) {
            items[i] = make_record(i + 1);
            CHECK(rdb_insert(src, items[i]) == INDEXES);
        }
        CHECK(rdb_insert(dst, make_record(10)) == INDEXES);
        /* Fault-inject rejection by the last index, after both destination
         * queue insertions. Recovery must restore exact source positions. */
        dst->fn[2] = injected_compare;
        struct snapshot source_before = snapshot(src), destination_before = snapshot(dst);
        reject_after_comparisons = 1;
        expect_move(variant, dst, src, 2, &items[position]->key[2], NULL, -2);
        CHECK(reject_after_comparisons == 0);
        unchanged(src, &source_before);
        unchanged(dst, &destination_before);
        CHECK(rdb_error_string && strstr(rdb_error_string, "duplicate"));
        expect_move(variant, dst, src, 2, &items[position]->key[2], items[position], 0);
        CHECK(src->record_count == 2 && dst->record_count == 2);
        rdb_flush(src, NULL, NULL);
        rdb_flush(dst, NULL, NULL);
        rdb_drop_pool(src);
        rdb_drop_pool(dst);
    }
}

static void queue_heads(void)
{
    for (int variant = 0; variant < 3; ++variant)
    for (int index = 0; index < 2; ++index) {
        rdb_pool_t *src = make_pool("source", 1);
        rdb_pool_t *dst = make_pool("destination", 0);
        for (int i = 0; i < 3; ++i) CHECK(rdb_insert(src, make_record(i + 1)) == INDEXES);
        record *moving = rdb_get(src, index, NULL);
        record *blocker = make_record(10);
        blocker->key[2] = moving->key[2];
        CHECK(rdb_insert(dst, blocker) == INDEXES);
        struct snapshot source_before = snapshot(src), destination_before = snapshot(dst);
        expect_move(variant, dst, src, index, NULL, NULL, -2);
        unchanged(src, &source_before);
        unchanged(dst, &destination_before);
        CHECK(rdb_delete(dst, 2, &blocker->key[2]) == blocker);
        free(blocker);
        expect_move(variant, dst, src, index, NULL, moving, 0);
        CHECK(src->record_count == 2 && dst->record_count == 1);
        CHECK(rdb_get(dst, 2, &moving->key[2]) == moving);
        rdb_flush(src, NULL, NULL);
        rdb_flush(dst, NULL, NULL);
        rdb_drop_pool(src);
        rdb_drop_pool(dst);
    }
}

static void same_pool(void)
{
    for (int variant = 0; variant < 3; ++variant)
    for (int queues = 0; queues < 2; ++queues) {
        rdb_pool_t *pool = make_pool("same", queues);
        record *items[3];
        int32_t missing = -1;
        expect_move(variant, pool, pool, 2, &missing, NULL, -1);
        for (int i = 0; i < 3; ++i) {
            items[i] = make_record(i);
            CHECK(rdb_insert(pool, items[i]) == INDEXES);
        }
        struct snapshot before = snapshot(pool);
        expect_move(variant, pool, pool, 2, &missing, NULL, -1);
        unchanged(pool, &before);
        if (variant != 1) {
            /* NULL is a queue-head request only, not a tree-root deletion. */
            expect_move(variant, pool, pool, 2, NULL, NULL, -1);
            unchanged(pool, &before);
        }
        expect_move(variant, pool, pool, 2, &items[1]->key[2], items[1], 0);
        CHECK(pool->record_count == 3);
        for (int i = 0; i < 3; ++i) CHECK(rdb_get(pool, 2, &items[i]->key[2]) == items[i]);
        if (queues) {
            CHECK((record *)pool->root[0] == items[0] && (record *)pool->tail[0] == items[1]);
            CHECK((record *)pool->root[1] == items[1] && (record *)pool->tail[1] == items[0]);
        }
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}


static void null_destination(void)
{
    const int counts[] = {0, 1, 7};
    for (int variant = 0; variant < 3; ++variant)
    for (int queues = 0; queues < 2; ++queues)
    for (size_t c = 0; c < ARRAY_COUNT(counts); ++c)
    for (int index = 0; index < INDEXES; ++index) {
        rdb_pool_t *src = make_pool("source", queues);
        record *items[7];
        for (int n = 0; n < counts[c]; ++n) {
            items[n] = make_record(n);
            CHECK(rdb_insert(src, items[n]) == INDEXES);
        }
        struct snapshot before = snapshot(src);
        int positions[] = {0, counts[c] / 2, counts[c] - 1};
        int32_t missing = -1;
        void *key = &missing;
        record *moving = NULL;
        for (size_t n = 0; n < ARRAY_COUNT(positions); ++n) {
            if (queues && index < 2) {
                key = NULL;
                moving = rdb_get(src, index, NULL);
            } else if (counts[c]) {
                moving = items[positions[n]];
                key = &moving->key[index];
            }
            for (int attempt = 0; attempt < 3; ++attempt) {
                expect_move(variant, NULL, src, index, key, NULL, moving ? -2 : -1);
                unchanged(src, &before);
                if (moving)
                    CHECK(rdb_error_string && strstr(rdb_error_string, "destination"));
            }
        }
        /* A rejected move must not stop a later move into a real pool. */
        if (moving) {
            rdb_pool_t *dst = make_pool("destination", queues);
            expect_move(variant, dst, src, index, key, moving, 0);
            CHECK(src->record_count == (unsigned)counts[c] - 1);
            CHECK(dst->record_count == 1);
            CHECK(rdb_get(dst, 2, &moving->key[2]) == moving);
            rdb_flush(dst, NULL, NULL);
            rdb_drop_pool(dst);
        }
        rdb_flush(src, NULL, NULL);
        rdb_drop_pool(src);
    }
}

static void unconfigured_destination(void)
{
    for (int variant = 0; variant < 3; ++variant)
    for (int mode = 0; mode < 3; ++mode) {
        rdb_pool_t *src = make_pool("source", 1);
        rdb_pool_t *dst = rdb_register_um_pool("destination", INDEXES,
                KEY_OFFSET(record, key[0], INDEXES), RDB_BTREE | RDB_KINT32, NULL, 0);
        CHECK(dst);
        /* Registration rejects zero now. Fault-inject corrupt metadata to
         * retain coverage of the mover's defensive invalid-layout rollback. */
        if (mode == 0) dst->indexCount = 0;
        if (mode == 2) {
            CHECK(rdb_register_um_idx(dst, 1, KEY_OFFSET(record, key[1], INDEXES),
                        RDB_LIST | RDB_KINT32, NULL, 0) == -4);
            /* Registration rejects this now; explicitly inject the invalid
             * destination metadata to test the defensive move path. */
            dst->FLAGS[1] = RDB_LIST | RDB_KINT32;
        }
        record *moving = make_record(1);
        CHECK(rdb_insert(src, moving) == INDEXES);
        struct snapshot before = snapshot(src);
        expect_move(variant, dst, src, 2, &moving->key[2], NULL, -2);
        unchanged(src, &before);
        CHECK(dst->record_count == 0);
        for (int i = 0; i < INDEXES; ++i)
            CHECK(dst->root[i] == NULL && dst->tail[i] == NULL);
        if (mode == 0) dst->indexCount = INDEXES;
        rdb_flush(src, NULL, NULL);
        rdb_drop_pool(src);
        rdb_drop_pool(dst);
    }
}

typedef struct {
    rdb_bpp_t links[INDEXES];
    int32_t id;
    char inline_key[16];
    char *string_key;
    void *pointer_key;
    int32_t composite[2];
} typed_record;

static int composite_compare(void *old_key, void *new_key)
{
    int32_t *a = old_key, *b = new_key;
    if (b[0] != a[0]) return (b[0] > a[0]) ? 1 : -1;
    return (b[1] > a[1]) - (b[1] < a[1]);
}

static rdb_pool_t *typed_pool(char *name, int flag, size_t offset)
{
    rdb_pool_t *pool = rdb_register_um_pool(name, INDEXES,
            KEY_OFFSET(typed_record, id, INDEXES), RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool);
    size_t key_size = flag == RDB_KSTR ? sizeof(((typed_record *)0)->inline_key) : 0;
    CHECK(rdb_register_um_idx(pool, 1, offset, RDB_BTREE | flag,
                flag == RDB_KCF ? composite_compare : NULL, key_size) == 1);
    CHECK(rdb_register_um_idx(pool, 2, 0, RDB_BTREE | RDB_KFIFO, NULL, 0) == 2);
    return pool;
}

static void typed_keys(void)
{
    const int flags[] = {RDB_KSTR, RDB_KPSTR, RDB_KPTR, RDB_KCF};
    const size_t offsets[] = {
        KEY_OFFSET(typed_record, inline_key, INDEXES),
        KEY_OFFSET(typed_record, string_key, INDEXES),
        KEY_OFFSET(typed_record, pointer_key, INDEXES),
        KEY_OFFSET(typed_record, composite, INDEXES)
    };
    int backing;
    for (int variant = 0; variant < 3; ++variant)
    for (size_t type = 0; type < ARRAY_COUNT(flags); ++type) {
        rdb_pool_t *src = typed_pool("source", flags[type], offsets[type]);
        rdb_pool_t *dst = typed_pool("destination", flags[type], offsets[type]);
        typed_record *moving = checked_calloc(1, sizeof(*moving));
        typed_record *blocker = checked_calloc(1, sizeof(*blocker));
        typed_record *items[] = {moving, blocker};
        for (int i = 0; i < 2; ++i) {
            items[i]->id = i + 1;
            strcpy(items[i]->inline_key, "identical");
            items[i]->pointer_key = &backing;
            items[i]->composite[0] = 3;
            items[i]->composite[1] = 7;
            if (flags[type] == RDB_KPSTR) {
                /* Equal strings in distinct allocations must collide. */
                items[i]->string_key = strdup("identical");
                CHECK(items[i]->string_key);
            }
        }
        CHECK(rdb_insert(src, moving) == INDEXES);
        CHECK(rdb_insert(dst, blocker) == INDEXES);
        struct snapshot source_before = snapshot(src), destination_before = snapshot(dst);
        expect_move(variant, dst, src, 0, &moving->id, NULL, -2);
        unchanged(src, &source_before);
        unchanged(dst, &destination_before);
        CHECK(rdb_delete(dst, 0, &blocker->id) == blocker);
        free(blocker->string_key);
        free(blocker);
        expect_move(variant, dst, src, 0, &moving->id, moving, 0);
        CHECK(src->record_count == 0 && dst->record_count == 1);
        CHECK(rdb_get(dst, 0, &moving->id) == moving);
        rdb_flush(dst, NULL, NULL);
        rdb_drop_pool(src);
        rdb_drop_pool(dst);
    }
}


/* Both pools put FIFO and LIFO before the string index, so a duplicate string
 * rejects the move only after the destination has inserted both queue links. */
static rdb_pool_t *string_queue_pool(char *name, int flag)
{
    rdb_pool_t *pool = rdb_register_um_pool(name, INDEXES, 0,
            RDB_BTREE | RDB_KFIFO, NULL, 0);
    CHECK(pool);
    CHECK(rdb_register_um_idx(pool, 1, 0, RDB_BTREE | RDB_KLIFO, NULL, 0) == 1);
    size_t offset = flag == RDB_KSTR
        ? KEY_OFFSET(typed_record, inline_key, INDEXES)
        : KEY_OFFSET(typed_record, string_key, INDEXES);
    size_t key_size = flag == RDB_KSTR ? sizeof(((typed_record *)0)->inline_key) : 0;
    CHECK(rdb_register_um_idx(pool, 2, offset, RDB_BTREE | flag, NULL, key_size) == 2);
    return pool;
}

static typed_record *string_queue_record(int flag, const char *key)
{
    typed_record *item = checked_calloc(1, sizeof(*item));
    CHECK(strlen(key) < sizeof(item->inline_key));
    strcpy(item->inline_key, key);
    if (flag == RDB_KPSTR) {
        item->string_key = strdup(key);
        CHECK(item->string_key);
    }
    return item;
}

static char *string_queue_key(typed_record *item, int flag)
{
    return flag == RDB_KSTR ? item->inline_key : item->string_key;
}

static void fifo_lifo_strings(int flag)
{
    /* Queue order differs from string-sort order. Test singleton pools too. */
    const char *names[] = {"charlie", "alpha", "bravo"};
    const char *other_names[] = {"delta", "echo"};
    for (int count = 1; count <= 3; count += 2)
    for (int variant = 0; variant < 3; ++variant)
    for (int selection = 0; selection < count + 2; ++selection) {
        int index = selection < 2 ? selection : 2;
        /* The scalar-key API can select a queue head, not a string key.
         * The pointer-key APIs also select head/middle/tail by string. */
        if (variant == 1 && index == 2) continue;
        int position = index == 0 ? 0 : index == 1 ? count - 1 : selection - 2;
        rdb_pool_t *src = string_queue_pool("source", flag);
        rdb_pool_t *dst = string_queue_pool("destination", flag);
        typed_record *items[ARRAY_COUNT(names)];
        for (int i = 0; i < count; ++i) {
            items[i] = string_queue_record(flag, names[i]);
            CHECK(rdb_insert(src, items[i]) == INDEXES);
        }
        for (int i = 0; i < count - 1; ++i)
            CHECK(rdb_insert(dst, string_queue_record(flag, other_names[i])) == INDEXES);
        typed_record *moving = items[position];
        char *key = string_queue_key(moving, flag);
        typed_record *blocker = string_queue_record(flag, key);
        char *blocker_key = string_queue_key(blocker, flag);
        /* Equal text in independent storage, including owned pointer strings. */
        CHECK(key != blocker_key && strcmp(key, blocker_key) == 0);
        CHECK(rdb_insert(dst, blocker) == INDEXES);
        void *lookup = index == 2 ? key : NULL;
        struct snapshot source_before = snapshot(src), destination_before = snapshot(dst);
        for (int attempt = 0; attempt < 3; ++attempt) {
            expect_move(variant, dst, src, index, lookup, NULL, -2);
            unchanged(src, &source_before);
            unchanged(dst, &destination_before);
            CHECK(rdb_error_string && strstr(rdb_error_string, "duplicate"));
            CHECK(string_queue_key(moving, flag) == key);
            CHECK(string_queue_key(blocker, flag) == blocker_key);
            CHECK(strcmp(key, names[position]) == 0);
            CHECK(strcmp(blocker_key, names[position]) == 0);
            for (int i = 0; i < count; ++i)
                CHECK(rdb_get(src, 2, string_queue_key(items[i], flag)) == items[i]);
            CHECK(rdb_get(dst, 2, key) == blocker);
        }

        CHECK(rdb_delete(dst, 2, blocker_key) == blocker);
        free(blocker->string_key);
        free(blocker);
        expect_move(variant, dst, src, index, lookup, moving, 0);
        CHECK(src->record_count == (unsigned)(count - 1));
        CHECK(dst->record_count == (unsigned)count);
        CHECK(rdb_get(src, 2, key) == NULL);
        CHECK(rdb_get(dst, 2, key) == moving);
        CHECK((typed_record *)dst->tail[0] == moving);
        CHECK((typed_record *)dst->root[1] == moving);
        CHECK(strcmp(string_queue_key(moving, flag), names[position]) == 0);
        /* Default flush also owns and frees RDB_KPSTR keys exactly once. */
        rdb_flush(src, NULL, NULL);
        rdb_flush(dst, NULL, NULL);
        rdb_drop_pool(src);
        rdb_drop_pool(dst);
    }
}
static void fifo_lifo_inline_string(void) { fifo_lifo_strings(RDB_KSTR); }
static void fifo_lifo_pointer_string(void) { fifo_lifo_strings(RDB_KPSTR); }

static unsigned source_comparisons, destination_comparisons;
static int count_source(void *a, void *b)
{
    ++source_comparisons;
    return key_cmp_int32(a, b);
}
static int count_destination(void *a, void *b)
{
    ++destination_comparisons;
    return key_cmp_int32(a, b);
}

/* A deterministic fast-path check, not a timing threshold: successful moves
 * must do exactly the key comparisons of a direct delete followed by insert. */
static void comparison_budget(void)
{
    for (int queues = 0; queues < 2; ++queues) {
        unsigned baseline_source = 0, baseline_destination = 0;
        for (int variant = -1; variant < 3; ++variant) {
            rdb_pool_t *src = make_pool("source", queues);
            rdb_pool_t *dst = make_pool("destination", queues);
            for (int i = 0; i < INDEXES; ++i) {
                if (src->FLAGS[i] & RDB_NOKEYS) continue;
                src->fn[i] = src->get_fn[i] = count_source;
                dst->fn[i] = dst->get_fn[i] = count_destination;
            }
            record *moving = NULL;
            for (int i = 1; i <= 7; ++i) {
                record *item = make_record(i);
                if (i == 4) moving = item;
                CHECK(rdb_insert(src, item) == INDEXES);
                CHECK(rdb_insert(dst, make_record(i + 10)) == INDEXES);
            }
            CHECK(moving);
            source_comparisons = destination_comparisons = 0;
            if (variant == -1) {
                CHECK(rdb_delete(src, 2, &moving->key[2]) == moving);
                CHECK(rdb_insert(dst, moving) == INDEXES);
                baseline_source = source_comparisons;
                baseline_destination = destination_comparisons;
                CHECK(baseline_source && baseline_destination);
            } else {
                expect_move(variant, dst, src, 2, &moving->key[2], moving, 0);
                CHECK(source_comparisons == baseline_source);
                CHECK(destination_comparisons == baseline_destination);
            }
            CHECK(src->record_count == 6 && dst->record_count == 8);
            rdb_flush(src, NULL, NULL);
            rdb_flush(dst, NULL, NULL);
            rdb_drop_pool(src);
            rdb_drop_pool(dst);
        }
    }
}

int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"collision_pointer", collision_pointer},
        {"collision_constant", collision_constant},
        {"collision_status", collision_status},
        {"late_rejection", late_rejection}, {"queue_heads", queue_heads},
        {"same_pool", same_pool}, {"unconfigured_destination", unconfigured_destination},
        {"null_destination", null_destination},
        {"typed_keys", typed_keys}, {"comparison_budget", comparison_budget},
        {"fifo_lifo_inline_string", fifo_lifo_inline_string},
        {"fifo_lifo_pointer_string", fifo_lifo_pointer_string}
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
