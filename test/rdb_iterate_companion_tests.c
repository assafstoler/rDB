/* Ordinary rdb_iterate callbacks explicitly unlink/edit/reinsert other indexes.
 * The active index/key stays fixed; callback results request deletion or abort.
 * Staged edits through rdb_iterate_reindex have separate tests.
 */
#include "rdb_test_support.h"

enum { INDEXES = 4, ITEMS = 7 };
enum { KEY_NUMBER, KEY_STRING, KEY_FIFO, KEY_LIFO };
typedef struct {
    rdb_bpp_t links[INDEXES];
    unsigned id;
    int32_t number;
    char *text;
} item;

struct traversal {
    rdb_pool_t *pool;
    item *records[ITEMS];
    int32_t numbers[ITEMS];
    char texts[ITEMS][32];
    unsigned queues[2][ITEMS], queue_count[2];
    unsigned expected[ITEMS], count, visited, seen, removed, cleaned, reindexed;
    unsigned delete_mask, reindex_mask, stop_after, rejected;
    int active, shift, direction, stop_code, collisions;
};

static const unsigned orders[][ITEMS] = {
    {0, 3, 6, 2, 5, 1, 4},
    {3, 1, 5, 0, 2, 4, 6},
    {0, 1, 2, 3, 4, 5, 6},
    {6, 5, 4, 3, 2, 1, 0},
    {6, 0, 5, 1, 4, 2, 3}
};

static int index_for(const struct traversal *state, int kind)
{
    return (kind + INDEXES - state->shift) % INDEXES;
}

static int key_offset(int kind)
{
    return kind == KEY_STRING ? KEY_OFFSET(item, text, INDEXES)
                              : KEY_OFFSET(item, number, INDEXES);
}

static void queue_add(struct traversal *state, int kind, unsigned id)
{
    unsigned *queue = state->queues[kind - KEY_FIFO];
    unsigned *count = &state->queue_count[kind - KEY_FIFO];
    CHECK(*count < ITEMS);
    if (kind == KEY_FIFO) queue[*count] = id;
    else {
        memmove(queue + 1, queue, *count * sizeof(*queue));
        queue[0] = id;
    }
    ++*count;
}

static void queue_remove(struct traversal *state, int kind, unsigned id)
{
    unsigned *queue = state->queues[kind - KEY_FIFO];
    unsigned *count = &state->queue_count[kind - KEY_FIFO];
    unsigned position = 0;
    while (position < *count && queue[position] != id) ++position;
    CHECK(position < *count);
    memmove(queue + position, queue + position + 1,
            (*count - position - 1) * sizeof(*queue));
    --*count;
}

/* This ordering oracle uses copied keys and queue IDs, never library links
 * or the production comparator. The original traversal order is saved once.
 */
static unsigned ordered_ids(const struct traversal *state, int kind, unsigned *ids)
{
    if (kind >= KEY_FIFO) {
        unsigned count = state->queue_count[kind - KEY_FIFO];
        memcpy(ids, state->queues[kind - KEY_FIFO], count * sizeof(*ids));
        return count;
    }
    unsigned count = 0;
    for (unsigned id = 0; id < state->count; ++id) {
        if (!state->records[id]) continue;
        unsigned position = count++;
        while (position && (kind == KEY_NUMBER
                ? state->numbers[ids[position - 1]] > state->numbers[id]
                : strcmp(state->texts[ids[position - 1]], state->texts[id]) > 0)) {
            ids[position] = ids[position - 1];
            --position;
        }
        ids[position] = id;
    }
    return count;
}

static int verify_tree(const struct traversal *state, int index, const item *record,
                       const unsigned *rank, int low, int high, unsigned *seen)
{
    if (!record) return 0;
    unsigned id = 0;
    while (id < state->count && state->records[id] != record) ++id;
    CHECK(id < state->count); /* Recognize the pointer before dereferencing it. */
    CHECK(!(*seen & (1U << id)));
    *seen |= 1U << id;
    CHECK((int)rank[id] > low && (int)rank[id] < high);
    const rdb_bpp_t *link = &record->links[index];
    int left = verify_tree(state, index, link->left, rank, low, (int)rank[id], seen);
    int right = verify_tree(state, index, link->right, rank, (int)rank[id], high, seen);
    CHECK(right - left >= -1 && right - left <= 1);
    CHECK(link->balance == right - left);
    return 1 + (left > right ? left : right);
}

static void verify(const struct traversal *state)
{
    unsigned live = 0, live_mask = 0;
    for (unsigned id = 0; id < state->count; ++id) {
        const item *record = state->records[id];
        if (record) {
            ++live;
            live_mask |= 1U << id;
            CHECK(record->id == id && record->number == state->numbers[id]);
            CHECK(strcmp(record->text, state->texts[id]) == 0);
        }
        CHECK(rdb_get(state->pool, index_for(state, KEY_NUMBER),
                      &state->numbers[id]) == record);
        CHECK(rdb_get(state->pool, index_for(state, KEY_STRING),
                      state->texts[id]) == record);
    }
    CHECK(state->pool->record_count == live);
    for (int kind = KEY_NUMBER; kind <= KEY_LIFO; ++kind) {
        int index = index_for(state, kind);
        unsigned ids[ITEMS], rank[ITEMS] = {0}, seen = 0;
        unsigned count = ordered_ids(state, kind, ids);
        CHECK(count == live);
        if (kind < KEY_FIFO) {
            for (unsigned n = 0; n < count; ++n) rank[ids[n]] = n;
            verify_tree(state, index, (item *)state->pool->root[index],
                        rank, -1, (int)count, &seen);
            CHECK(seen == live_mask);
        } else {
            CHECK((void *)state->pool->root[index] ==
                    (count ? state->records[ids[0]] : NULL));
            CHECK((void *)state->pool->tail[index] ==
                    (count ? state->records[ids[count - 1]] : NULL));
            for (unsigned n = 0; n < count; ++n) {
                const rdb_bpp_t *link = &state->records[ids[n]]->links[index];
                CHECK(link->left == (n ? &state->records[ids[n - 1]]->links[index] : NULL));
                CHECK(link->right == (n + 1 < count ? &state->records[ids[n + 1]]->links[index] : NULL));
            }
        }
    }
}

/* insert_one failure leaves this record unlinked from that index. Its caller
 * restores the original key and reinserts it before continuing the scan.
 */
static void rejected_reindex(struct traversal *state, item *record, int kind)
{
    if (!state->collisions || kind >= KEY_FIFO) return;
    unsigned other = 0;
    while (other < state->count && (!state->records[other] || other == record->id))
        ++other;
    if (other == state->count) return;
    int index = index_for(state, kind);
    int32_t old_number = record->number;
    char *old_text = record->text;
    CHECK(rdb_delete_one(state->pool, index, record) >= 0);
    if (kind == KEY_NUMBER) record->number = state->records[other]->number;
    else record->text = state->records[other]->text;
    CHECK(rdb_insert_one(state->pool, index, record) < 0);
    CHECK(rdb_get(state->pool, index, kind == KEY_NUMBER
                ? (const void *)&record->number : (const void *)record->text) == state->records[other]);
    record->number = old_number;
    record->text = old_text;
    CHECK(rdb_insert_one(state->pool, index, record) >= 0);
    ++state->rejected;
    verify(state);
}

static void reindex_companions(struct traversal *state, item *record)
{
    int active_index = index_for(state, state->active);
    unsigned char active_links[ITEMS][sizeof(rdb_bpp_t)];
    void *root = state->pool->root[active_index], *tail = state->pool->tail[active_index];
    for (unsigned id = 0; id < state->count; ++id)
        if (state->records[id])
            memcpy(active_links[id], &state->records[id]->links[active_index], sizeof(rdb_bpp_t));

    for (int kind = KEY_NUMBER; kind <= KEY_LIFO; ++kind) {
        if (kind == state->active) continue;
        int index = index_for(state, kind);
        rejected_reindex(state, record, kind);
        CHECK(rdb_delete_one(state->pool, index, record) >= 0);
        if (kind == KEY_NUMBER) {
            CHECK(rdb_get(state->pool, index, &state->numbers[record->id]) == NULL);
            record->number = (state->direction ? -100 : 100) - 3 * (int32_t)record->id;
            state->numbers[record->id] = record->number;
        } else if (kind == KEY_STRING) {
            CHECK(rdb_get(state->pool, index, state->texts[record->id]) == NULL);
            CHECK(snprintf(state->texts[record->id], sizeof(state->texts[record->id]),
                        "%c-%02u", state->direction ? 'a' : 'z', record->id) > 0);
            free(record->text);
            record->text = strdup(state->texts[record->id]);
            CHECK(record->text);
        } else {
            queue_remove(state, kind, record->id);
            queue_add(state, kind, record->id);
        }
        CHECK(rdb_insert_one(state->pool, index, record) >= 0);
        verify(state);
        CHECK(state->pool->root[active_index] == root && state->pool->tail[active_index] == tail);
        for (unsigned id = 0; id < state->count; ++id)
            if (state->records[id])
                CHECK(memcmp(active_links[id], &state->records[id]->links[active_index], sizeof(rdb_bpp_t)) == 0);
    }
    state->reindexed |= 1U << record->id;
}

static int visit(void *data, void *opaque)
{
    item *record = data;
    struct traversal *state = opaque;
    verify(state); /* Also catches a broken unlink from the previous callback. */
    CHECK(state->visited < state->count);
    CHECK(record == state->records[state->expected[state->visited]]);
    CHECK(!(state->seen & (1U << record->id)));
    state->seen |= 1U << record->id;
    ++state->visited;
    if (state->reindex_mask & (1U << record->id)) reindex_companions(state, record);
    int result = state->delete_mask & (1U << record->id) ? RDB_CB_DELETE_NODE : RDB_CB_OK;
    if (state->stop_after && state->visited == state->stop_after) result = state->stop_code;
    if (result == RDB_CB_DELETE_NODE || result == RDB_CB_DELETE_NODE_AND_ABORT) {
        state->removed |= 1U << record->id;
        queue_remove(state, KEY_FIFO, record->id);
        queue_remove(state, KEY_LIFO, record->id);
        state->records[record->id] = NULL;
    }
    return result;
}

static void cleanup(void *data, void *opaque)
{
    item *record = data;
    struct traversal *state = opaque;
    CHECK(record->id < state->count);
    unsigned bit = 1U << record->id;
    CHECK((state->removed & bit) && !(state->cleaned & bit));
    CHECK(record->number == state->numbers[record->id]);
    // rDB frees and clears the indexed string before invoking cleanup.
    CHECK(record->text == NULL);
    state->cleaned |= bit;
    verify(state); /* Every index and the count must already exclude this item. */
    free(record);
}

static void run_one(int active, int shift, const unsigned *order, unsigned count,
                    unsigned delete_mask, unsigned reindex_mask, int direction,
                    unsigned stop_after, int stop_code, int with_cleanup, int collisions)
{
    const int flags[] = {RDB_KINT32, RDB_KPSTR, RDB_KFIFO, RDB_KLIFO};
    struct traversal state = {0};
    state.active = active;
    state.shift = shift;
    state.count = count;
    state.delete_mask = delete_mask;
    state.reindex_mask = reindex_mask;
    state.direction = direction;
    state.stop_after = stop_after;
    state.stop_code = stop_code;
    state.collisions = collisions;
    state.pool = rdb_register_um_pool("iterate-companion", INDEXES,
            key_offset(shift), RDB_BTREE | flags[shift], NULL, 0);
    CHECK(state.pool);
    for (int index = 1; index < INDEXES; ++index) {
        int kind = (index + shift) % INDEXES;
        CHECK(rdb_register_um_idx(state.pool, index, key_offset(kind),
                    RDB_BTREE | flags[kind], NULL, 0) == index);
    }
    for (unsigned n = 0; n < count; ++n) {
        unsigned id = order[n];
        item *record = checked_calloc(1, sizeof(*record));
        record->id = id;
        record->number = state.numbers[id] = (int32_t)id;
        CHECK(snprintf(state.texts[id], sizeof(state.texts[id]), "m-%02u", ITEMS - id) > 0);
        record->text = strdup(state.texts[id]);
        CHECK(record->text);
        state.records[id] = record;
        queue_add(&state, KEY_FIFO, id);
        queue_add(&state, KEY_LIFO, id);
        CHECK(rdb_insert(state.pool, record) == INDEXES);
    }
    CHECK(ordered_ids(&state, active, state.expected) == count);
    unsigned expected_visits = stop_after ? stop_after : count;
    unsigned expected_seen = 0, expected_removed = 0;
    for (unsigned n = 0; n < expected_visits; ++n) {
        unsigned bit = 1U << state.expected[n];
        expected_seen |= bit;
        int remove = (delete_mask & bit) != 0;
        if (stop_after && n + 1 == stop_after) remove = stop_code == RDB_CB_DELETE_NODE_AND_ABORT;
        if (remove) expected_removed |= bit;
    }
    verify(&state);
    CHECK(rdb_lock(state.pool, "iterate-companion-test") == 0);
    rdb_iterate(state.pool, index_for(&state, active), visit, &state,
                with_cleanup ? cleanup : NULL, &state);
    rdb_unlock(state.pool, "iterate-companion-test");
    CHECK(state.visited == expected_visits && state.seen == expected_seen);
    CHECK(state.removed == expected_removed);
    CHECK(state.reindexed == (expected_seen & reindex_mask));
    CHECK(state.cleaned == (with_cleanup ? expected_removed : 0));
    if (collisions && count > 1 && reindex_mask == (1U << ITEMS) - 1)
        CHECK(state.rejected > 0);
    verify(&state);
    rdb_flush(state.pool, NULL, NULL);
    CHECK(state.pool->record_count == 0);
    for (int index = 0; index < INDEXES; ++index)
        CHECK(state.pool->root[index] == NULL && state.pool->tail[index] == NULL);
    rdb_drop_pool(state.pool);
}

static void selective(int active)
{
    const unsigned reindex_masks[] = {0x55, 0x2a, 0x7f};
    for (int shift = 0; shift < INDEXES; ++shift)
        for (int cleanup_mode = 0; cleanup_mode < 2; ++cleanup_mode)
            for (int direction = 0; direction < 2; ++direction) {
                run_one(active, shift, orders[2], 0, 0, 0x7f, direction, 0, 0, cleanup_mode, 0);
                run_one(active, shift, orders[2], 1, 0, 0x7f, direction, 0, 0, cleanup_mode, 0);
                run_one(active, shift, orders[2], 1, 1, 0x7f, direction, 0, 0, cleanup_mode, 0);
                for (size_t order = 0; order < ARRAY_COUNT(orders); ++order)
                    for (unsigned mask = 0; mask < (1U << ITEMS); ++mask)
                        for (size_t change = 0; change < ARRAY_COUNT(reindex_masks); ++change)
                            run_one(active, shift, orders[order], ITEMS, mask,
                                    reindex_masks[change], direction, 0, 0, cleanup_mode, 0);
            }
}

static void numeric(void) { selective(KEY_NUMBER); }
static void strings(void) { selective(KEY_STRING); }
static void fifo(void) { selective(KEY_FIFO); }
static void lifo(void) { selective(KEY_LIFO); }

static void aborts(void)
{
    const unsigned masks[] = {0, 0x55, 0x2a, 0x7f};
    const int codes[] = {RDB_CB_ABORT, RDB_CB_DELETE_NODE_AND_ABORT};
    for (int active = KEY_NUMBER; active <= KEY_LIFO; ++active)
        for (int shift = 0; shift < INDEXES; ++shift)
            for (size_t order = 0; order < ARRAY_COUNT(orders); ++order)
                for (size_t mask = 0; mask < ARRAY_COUNT(masks); ++mask)
                    for (unsigned stop = 1; stop <= ITEMS; ++stop)
                        for (size_t code = 0; code < ARRAY_COUNT(codes); ++code)
                            for (int cleanup_mode = 0; cleanup_mode < 2; ++cleanup_mode)
                                run_one(active, shift, orders[order], ITEMS, masks[mask],
                                        0x7f, (int)(stop & 1U), stop, codes[code], cleanup_mode, 0);
}

static void collisions(void)
{
    const unsigned masks[] = {0, 0x55, 0x2a, 0x7f};
    for (int active = KEY_NUMBER; active <= KEY_LIFO; ++active)
        for (int shift = 0; shift < INDEXES; ++shift)
            for (size_t order = 0; order < ARRAY_COUNT(orders); ++order)
                for (size_t mask = 0; mask < ARRAY_COUNT(masks); ++mask)
                    for (int direction = 0; direction < 2; ++direction)
                        for (int cleanup_mode = 0; cleanup_mode < 2; ++cleanup_mode)
                            run_one(active, shift, orders[order], ITEMS, masks[mask],
                                    0x7f, direction, 0, 0, cleanup_mode, 1);
}

int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"numeric", numeric}, {"strings", strings}, {"fifo", fifo}, {"lifo", lifo},
        {"aborts", aborts}, {"collisions", collisions}
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
