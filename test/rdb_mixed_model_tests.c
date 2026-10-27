/* Reproducible mixed-operation sequences against an independent array model.
 * No timing thresholds, external pools, production overrides or hidden retries. */
#include "rdb_test_support.h"
#include <limits.h>
#include <stdbool.h>
#include <errno.h>
#include <stdarg.h>

enum { INDEXES = 3, POOLS = 2, MAX_ITEMS = 512, TRACE_SIZE = 16 };
static struct {
    unsigned items, steps, runs;
    uint32_t seed;
    bool custom, seed_given, runs_given;
} options = {32, 1200, 4, 1, false, false, false};
#define ITEMS (options.items)
#define STEPS (options.steps)
typedef struct {
    rdb_bpp_t links[INDEXES];
    int32_t key[INDEXES];
    char inline_key[INDEXES][16];
    char *string_key[INDEXES];
    unsigned id;
} item;

struct model {
    rdb_pool_t *pools[POOLS];
    item *items[MAX_ITEMS];
    int owner[MAX_ITEMS], flags[INDEXES];
    int32_t keys[MAX_ITEMS][INDEXES];
    unsigned queues[POOLS][INDEXES][MAX_ITEMS], counts[POOLS][INDEXES];
    unsigned allocated, freed, operations[7];
    uint32_t random;
};

/* Include the seed and step in every failure so a sequence can be replayed. */
static uint32_t active_seed;
static unsigned active_step, active_layout;
static const char *active_case;
static const char *program_name;
static char trace[TRACE_SIZE][128];
static unsigned trace_count;
static void note_operation(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(trace[trace_count++ % TRACE_SIZE], sizeof(trace[0]), format, args);
    va_end(args);
}
static void dump_trace(void)
{
    fprintf(stderr, "Replay: \"%s\" %s --seed 0x%08x --items %u --steps %u\nLayout: %u\n",
            program_name, active_case, active_seed, ITEMS, STEPS, active_layout);
    for (unsigned n = trace_count > TRACE_SIZE ? trace_count - TRACE_SIZE : 0;
            n < trace_count; ++n)
        fprintf(stderr, "  %s\n", trace[n % TRACE_SIZE]);
}
#undef CHECK
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s: seed=0x%08x step=%u CHECK(%s) failed\n", \
                __FILE__, __LINE__, __func__, active_seed, active_step, #condition); \
        dump_trace(); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static bool string_index(const struct model *model, int index)
{
    return (model->flags[index] & (RDB_KSTR | RDB_KPSTR)) != 0;
}

// Fixed-width decimal encoding preserves signed numeric ordering. The oracle
// keeps independent integer keys; it never asks rDB to compute expected order.
static void encode_key(char text[16], int32_t key)
{
    snprintf(text, 16, "%010u", (unsigned)((uint32_t)key ^ UINT32_C(0x80000000)));
}
static void set_key(item *record, int index, int32_t key, int kind)
{
    record->key[index] = key;
    if (kind == RDB_KSTR) encode_key(record->inline_key[index], key);
    if (kind == RDB_KPSTR) {
        free(record->string_key[index]);
        record->string_key[index] = checked_calloc(16, 1);
        encode_key(record->string_key[index], key);
    }
}
static void free_item(item *record)
{
    for (int i = 0; i < INDEXES; ++i) free(record->string_key[i]);
    free(record);
}
static void *query_key(const struct model *model, int index, int32_t *value, char text[16])
{
    if (model->flags[index] & RDB_NOKEYS) return NULL;
    if (!string_index(model, index)) return value;
    encode_key(text, *value);
    return text;
}
static int key_offset(const struct model *model, int index)
{
    if (model->flags[index] == RDB_KSTR) return KEY_OFFSET(item, inline_key[index], INDEXES);
    if (model->flags[index] == RDB_KPSTR) return KEY_OFFSET(item, string_key[index], INDEXES);
    return KEY_OFFSET(item, key[index], INDEXES);
}

static size_t key_capacity(const struct model *model, int index)
{
    return model->flags[index] == RDB_KSTR ? sizeof(((item *)0)->inline_key[0]) : 0;
}

static uint32_t next_random(struct model *model)
{
    uint32_t value = model->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return model->random = value;
}

static void queue_add(struct model *model, int pool, int index, unsigned id)
{
    if (!(model->flags[index] & RDB_NOKEYS)) return;
    unsigned *queue = model->queues[pool][index];
    unsigned count = model->counts[pool][index];
    CHECK(count < ITEMS);
    if (model->flags[index] == RDB_KFIFO) queue[count] = id;
    else {
        memmove(queue + 1, queue, count * sizeof(*queue));
        queue[0] = id;
    }
    model->counts[pool][index] = count + 1;
}

static void queue_remove(struct model *model, int pool, int index, unsigned id)
{
    if (!(model->flags[index] & RDB_NOKEYS)) return;
    unsigned *queue = model->queues[pool][index];
    unsigned count = model->counts[pool][index], position = 0;
    while (position < count && queue[position] != id) ++position;
    CHECK(position < count);
    memmove(queue + position, queue + position + 1,
            (count - position - 1) * sizeof(*queue));
    model->counts[pool][index] = count - 1;
}

static void model_add(struct model *model, int pool, unsigned id)
{
    CHECK(model->owner[id] == -1 && model->items[id]);
    model->owner[id] = pool;
    for (int i = 0; i < INDEXES; ++i) queue_add(model, pool, i, id);
}

static void model_remove(struct model *model, unsigned id)
{
    int pool = model->owner[id];
    CHECK(pool >= 0 && pool < POOLS && model->items[id]);
    for (int i = 0; i < INDEXES; ++i) queue_remove(model, pool, i, id);
    model->owner[id] = -1;
}

/* Record accounting is shared by explicit deletion and cleanup callbacks.
 * Do not query a pool mid-flush, when its roots have not been cleared yet. */
static void release_record(void *data, void *opaque)
{
    struct model *model = opaque;
    item *record = data;
    unsigned id = record->id;
    CHECK(id < ITEMS && model->items[id] == record);
    CHECK(memcmp(record->key, model->keys[id], sizeof(record->key)) == 0);
    model_remove(model, id);
    model->items[id] = NULL;
    ++model->freed;
    free_item(record);
}

static void cleanup_record(void *data, void *opaque)
{
    struct model *model = opaque;
    item *record = data;
    // Iteration and flush own indexed strings; plain delete leaves them to us.
    for (int i = 0; i < INDEXES; ++i)
        if (model->flags[i] == RDB_KPSTR) CHECK(record->string_key[i] == NULL);
    release_record(data, opaque);
}

static unsigned ordered_ids(const struct model *model, int pool, int index,
                            unsigned ids[MAX_ITEMS])
{
    if (model->flags[index] & RDB_NOKEYS) {
        unsigned count = model->counts[pool][index];
        memcpy(ids, model->queues[pool][index], count * sizeof(*ids));
        return count;
    }
    unsigned count = 0;
    for (unsigned id = 0; id < ITEMS; ++id) {
        if (model->owner[id] != pool) continue;
        unsigned n = count++;
        while (n && model->keys[ids[n - 1]][index] > model->keys[id][index]) {
            ids[n] = ids[n - 1];
            --n;
        }
        ids[n] = id;
    }
    return count;
}

static int verify_tree(const struct model *model, int pool, int index,
                       const item *node, bool seen[MAX_ITEMS], int64_t low, int64_t high)
{
    if (!node) return 0;
    /* Recognize the address before reading it; malformed links/cycles fail. */
    unsigned id = 0;
    while (id < ITEMS && model->items[id] != node) ++id;
    CHECK(id < ITEMS && model->owner[id] == pool && !seen[id]);
    seen[id] = true;
    int32_t key = model->keys[id][index];
    CHECK(key > low && key < high && node->key[index] == key);
    int left = verify_tree(model, pool, index, node->links[index].left, seen, low, key);
    int right = verify_tree(model, pool, index, node->links[index].right, seen, key, high);
    CHECK(abs(left - right) <= 1);
    CHECK(node->links[index].balance == right - left);
    return 1 + (left > right ? left : right);
}

struct visit {
    struct model *model;
    unsigned ids[MAX_ITEMS], count, visited, modulus, stop;
    int stop_code;
};

static int visit_record(void *data, void *opaque)
{
    struct visit *visit = opaque;
    CHECK(visit->visited < visit->count);
    unsigned id = visit->ids[visit->visited++];
    CHECK(data == visit->model->items[id]);
    if (visit->stop && visit->visited == visit->stop) return visit->stop_code;
    return id % 7 == visit->modulus ? RDB_CB_DELETE_NODE : RDB_CB_OK;
}

static void verify_model(struct model *model)
{
    unsigned live = 0;
    for (unsigned id = 0; id < ITEMS; ++id) {
        if (!model->items[id]) { CHECK(model->owner[id] == -1); continue; }
        ++live;
        CHECK(model->owner[id] >= 0 && model->owner[id] < POOLS);
        CHECK(model->items[id]->id == id);
        CHECK(memcmp(model->items[id]->key, model->keys[id], sizeof(model->keys[id])) == 0);
        for (int index = 0; index < INDEXES; ++index) {
            if (!string_index(model, index)) continue;
            char text[16];
            encode_key(text, model->keys[id][index]);
            const char *actual = model->flags[index] == RDB_KSTR ?
                model->items[id]->inline_key[index] : model->items[id]->string_key[index];
            CHECK(actual && strcmp(actual, text) == 0);
        }
    }
    CHECK(model->allocated == model->freed + live);
    for (int p = 0; p < POOLS; ++p) {
        rdb_pool_t *pool = model->pools[p];
        unsigned count = 0;
        for (unsigned id = 0; id < ITEMS; ++id) count += model->owner[id] == p;
        CHECK(pool->record_count == count);
        for (int index = 0; index < INDEXES; ++index) {
            struct visit visit = {.model = model, .modulus = 7};
            visit.count = ordered_ids(model, p, index, visit.ids);
            CHECK(visit.count == count);
            if (model->flags[index] & RDB_NOKEYS) {
                CHECK((item *)pool->root[index] == (count ? model->items[visit.ids[0]] : NULL));
                CHECK((item *)pool->tail[index] == (count ? model->items[visit.ids[count - 1]] : NULL));
                for (unsigned n = 0; n < count; ++n) {
                    const rdb_bpp_t *link = &model->items[visit.ids[n]]->links[index];
                    CHECK(link->left == (n ? &model->items[visit.ids[n - 1]]->links[index] : NULL));
                    CHECK(link->right == (n + 1 < count ? &model->items[visit.ids[n + 1]]->links[index] : NULL));
                }
                CHECK(rdb_get(pool, index, NULL) == (count ? model->items[visit.ids[0]] : NULL));
            } else {
                bool seen[MAX_ITEMS] = {false};
                verify_tree(model, p, index, (item *)pool->root[index], seen, INT64_MIN, INT64_MAX);
                for (unsigned id = 0; id < ITEMS; ++id) {
                    CHECK(seen[id] == (model->owner[id] == p));
                    if (model->owner[id] == p) {
                        char text[16];
                        CHECK(rdb_get(pool, index,
                            query_key(model, index, &model->keys[id][index], text)) == model->items[id]);
                    }
                }
            }
            rdb_iterate(pool, index, visit_record, &visit, NULL, NULL);
            CHECK(visit.visited == count);
        }
    }
}

static bool can_insert(const struct model *model, int pool, const int32_t keys[INDEXES],
                       int except_id)
{
    for (unsigned id = 0; id < ITEMS; ++id) {
        if (model->owner[id] != pool || (int)id == except_id) continue;
        for (int i = 0; i < INDEXES; ++i)
            if (!(model->flags[i] & RDB_NOKEYS) && model->keys[id][i] == keys[i])
                return false;
    }
    return true;
}

static void insert_record(struct model *model, int pool)
{
    unsigned id = next_random(model) % ITEMS, scanned = 0;
    while (model->items[id] && scanned++ < ITEMS) id = (id + 1) % ITEMS;
    if (model->items[id]) return;
    item *record = checked_calloc(1, sizeof(*record));
    ++model->allocated;
    record->id = id;
    for (int i = 0; i < INDEXES; ++i)
        set_key(record, i, (int32_t)(next_random(model) % (ITEMS * 8 + 1)) - (int32_t)(ITEMS * 4),
                model->flags[i]);
    note_operation("%u insert pool=%d id=%u key=%d", active_step, pool, id, record->key[0]);
    bool accepted = can_insert(model, pool, record->key, -1);
    int rc = rdb_insert(model->pools[pool], record);
    CHECK(accepted ? rc == INDEXES : rc == 0);
    if (accepted) {
        model->items[id] = record;
        memcpy(model->keys[id], record->key, sizeof(record->key));
        model_add(model, pool, id);
    } else {
        ++model->freed;
        free_item(record);
    }
    ++model->operations[0];
}

/* Choose a real key, or the head for queue APIs whose query is ignored. */
static int select_record(struct model *model, int pool, int index)
{
    unsigned ids[MAX_ITEMS], count = ordered_ids(model, pool, index, ids);
    if (!count) return -1;
    return ids[(model->flags[index] & RDB_NOKEYS) ? 0 : next_random(model) % count];
}

static void delete_record(struct model *model, int pool, int index, unsigned id)
{
    int32_t key = model->keys[id][index];
    void *record;
    char text[16];
    note_operation("%u delete pool=%d index=%d id=%u key=%d", active_step, pool, index, id, key);
    if ((next_random(model) & 1) && !string_index(model, index))
        record = rdb_delete_const(model->pools[pool], index, key);
    else
        record = rdb_delete(model->pools[pool], index, query_key(model, index, &key, text));
    CHECK(record == model->items[id]);
    release_record(record, model);
    ++model->operations[1];
}

static void reindex_record(struct model *model, int pool, int index, unsigned id)
{
    item *record = model->items[id];
    int32_t key = (int32_t)(next_random(model) % (ITEMS * 8 + 1)) - (int32_t)(ITEMS * 4);
    if (!(model->flags[index] & RDB_NOKEYS)) {
        /* Find an unused key in this index without consulting the library. */
        bool conflict;
        do {
            conflict = false;
            for (unsigned other = 0; other < ITEMS; ++other)
                if (other != id && model->owner[other] == pool &&
                        model->keys[other][index] == key) conflict = true;
            if (conflict) ++key;
        } while (conflict);
    }
    note_operation("%u reindex pool=%d index=%d id=%u new-key=%d", active_step, pool, index, id, key);
    CHECK(rdb_delete_one(model->pools[pool], index, record) >= 0);
    queue_remove(model, pool, index, id);
    model->keys[id][index] = key;
    set_key(record, index, key, model->flags[index]);
    CHECK(rdb_insert_one(model->pools[pool], index, record) >= 0);
    queue_add(model, pool, index, id);
    ++model->operations[2];
}

static void move_record(struct model *model, int pool, int index, unsigned id, bool reject)
{
    int destination = (next_random(model) % 4 == 0) ? pool : 1 - pool;
    rdb_pool_t *target = reject ? NULL : model->pools[destination];
    bool accepted = !reject && can_insert(model, destination, model->keys[id], (int)id);
    int32_t key = model->keys[id][index];
    char text[16];
    void *query = query_key(model, index, &key, text);
    void *expected = accepted ? model->items[id] : NULL;
    unsigned api = next_random(model) % 3;
    if (api == 1 && string_index(model, index)) api = 2; // No string _const API.
    note_operation("%u move pool=%d to=%d index=%d id=%u reject=%d api=%u",
            active_step, pool, destination, index, id, reject, api);
    switch (api) {
        case 0: CHECK(rdb_move(target, model->pools[pool], index, query) == expected); break;
        case 1: CHECK(rdb_move_const(target, model->pools[pool], index, key) == expected); break;
        case 2: CHECK(rdb_move2(target, model->pools[pool], index, query) == (accepted ? 0 : -2)); break;
    }
    if (accepted) {
        model_remove(model, id);
        model_add(model, destination, id);
    }
    ++model->operations[reject ? 4 : 3];
}

static void iterate_records(struct model *model, int pool, int index)
{
    struct visit visit = {.model = model, .modulus = next_random(model) % 8};
    visit.count = ordered_ids(model, pool, index, visit.ids);
    if (visit.count && next_random(model) % 3) {
        visit.stop = 1 + next_random(model) % visit.count;
        visit.stop_code = (next_random(model) & 1) ? RDB_CB_ABORT : RDB_CB_DELETE_NODE_AND_ABORT;
    }
    unsigned visits = visit.stop ? visit.stop : visit.count, removed = 0;
    for (unsigned n = 0; n < visits; ++n) {
        bool remove = visit.ids[n] % 7 == visit.modulus;
        if (visit.stop && n + 1 == visit.stop)
            remove = visit.stop_code == RDB_CB_DELETE_NODE_AND_ABORT;
        removed += remove;
    }
    unsigned freed = model->freed;
    note_operation("%u iterate pool=%d index=%d stop=%u code=%d", active_step, pool, index,
            visit.stop, visit.stop_code);
    rdb_iterate(model->pools[pool], index, visit_record, &visit, cleanup_record, model);
    CHECK(visit.visited == visits && model->freed == freed + removed);
    ++model->operations[5];
}

static void run_typed_model(unsigned layout, int kind)
{
    const int layouts[][INDEXES] = {
        {RDB_KINT32, RDB_KINT32, RDB_KINT32},
        {RDB_KINT32, RDB_KFIFO, RDB_KLIFO}, {RDB_KINT32, RDB_KLIFO, RDB_KFIFO},
        {RDB_KFIFO, RDB_KINT32, RDB_KLIFO}, {RDB_KLIFO, RDB_KINT32, RDB_KFIFO},
        {RDB_KFIFO, RDB_KLIFO, RDB_KINT32}, {RDB_KLIFO, RDB_KFIFO, RDB_KINT32}
    };
    const uint32_t seeds[] = {1, UINT32_C(0x1dba5eed), UINT32_C(0xc0ffee), UINT32_MAX};
    char names[][16] = {"model-left", "model-right"};
    for (unsigned run = 0; run < options.runs; ++run) {
        uint32_t seed = !options.seed_given && !options.runs_given ? seeds[run] :
            options.seed + UINT32_C(0x9e3779b9) * run;
        if (seed == 0) seed = UINT32_MAX; // xorshift's zero state is stationary.
        struct model model = {.random = seed};
        active_seed = seed;
        active_step = trace_count = 0;
        active_layout = layout;
        for (unsigned id = 0; id < ITEMS; ++id) model.owner[id] = -1;
        memcpy(model.flags, layouts[layout], sizeof(model.flags));
        for (int i = 0; i < INDEXES; ++i) {
            if (model.flags[i] != RDB_KINT32 || kind == RDB_KINT32) continue;
            // The all-tree case mixes numeric/inline/pointer strings; queue
            // permutations each exercise the requested string representation.
            model.flags[i] = layout == 0 ?
                (i == 0 ? RDB_KINT32 : i == 1 ? RDB_KSTR : RDB_KPSTR) : kind;
        }
        for (int p = 0; p < POOLS; ++p) {
            model.pools[p] = rdb_register_um_pool(names[p], INDEXES,
                    key_offset(&model, 0), RDB_BTREE | model.flags[0], NULL,
                    key_capacity(&model, 0));
            CHECK(model.pools[p]);
            for (int i = 1; i < INDEXES; ++i)
                CHECK(rdb_register_um_idx(model.pools[p], i, key_offset(&model, i),
                        RDB_BTREE | model.flags[i], NULL, key_capacity(&model, i)) == i);
        }
        for (unsigned n = 0; n < ITEMS; ++n) {
            insert_record(&model, n % POOLS);
            verify_model(&model);
        }
        for (active_step = 1; active_step <= STEPS; ++active_step) {
            int pool = next_random(&model) % POOLS, index = next_random(&model) % INDEXES;
            unsigned operation = next_random(&model) % 16;
            int id = select_record(&model, pool, index);
            if (active_step % 257 == 0) {
                note_operation("%u flush pool=%d", active_step, pool);
                rdb_flush(model.pools[pool], cleanup_record, &model);
                ++model.operations[6];
            } else if (operation < 6 || id < 0) insert_record(&model, pool);
            else if (operation < 8) delete_record(&model, pool, index, (unsigned)id);
            else if (operation < 10) reindex_record(&model, pool, index, (unsigned)id);
            else if (operation < 13) move_record(&model, pool, index, (unsigned)id, false);
            else if (operation == 13) move_record(&model, pool, index, (unsigned)id, true);
            else iterate_records(&model, pool, index);
            verify_model(&model);
        }
        if (!options.custom)
            for (size_t op = 0; op < ARRAY_COUNT(model.operations); ++op)
                CHECK(model.operations[op] > 0);
        for (int p = 0; p < POOLS; ++p) {
            rdb_flush(model.pools[p], cleanup_record, &model);
            verify_model(&model);
        }
        CHECK(model.allocated == model.freed);
        for (int p = 0; p < POOLS; ++p) rdb_drop_pool(model.pools[p]);
    }
}

static void run_model(unsigned layout) { run_typed_model(layout, RDB_KINT32); }
static void inline_strings(void) { for (unsigned i = 0; i < 7; ++i) run_typed_model(i, RDB_KSTR); }
static void pointer_strings(void) { for (unsigned i = 0; i < 7; ++i) run_typed_model(i, RDB_KPSTR); }
static void stress(void)
{
    for (unsigned i = 0; i < 7; ++i) run_model(i);
    inline_strings();
    pointer_strings();
}
static void trees(void) { run_model(0); }
static void tree_fifo_lifo(void) { run_model(1); }
static void tree_lifo_fifo(void) { run_model(2); }
static void fifo_tree_lifo(void) { run_model(3); }
static void lifo_tree_fifo(void) { run_model(4); }
static void fifo_lifo_tree(void) { run_model(5); }
static void lifo_fifo_tree(void) { run_model(6); }

int main(int argc, char **argv)
{
    program_name = argv[0];
    active_case = argc > 1 ? argv[1] : "CASE";
    for (int arg = 2; arg < argc; arg += 2) {
        if (arg + 1 >= argc || argv[arg + 1][0] == '-') {
            fprintf(stderr, "Expected a positive integer after %s\n", argv[arg]);
            return 2;
        }
        errno = 0;
        char *end;
        unsigned long long value = strtoull(argv[arg + 1], &end, 0);
        if (errno || !argv[arg + 1][0] || *end || value == 0 || value > UINT32_MAX) {
            fprintf(stderr, "Invalid positive integer: %s\n", argv[arg + 1]);
            return 2;
        }
        options.custom = true;
        if (strcmp(argv[arg], "--seed") == 0) {
            options.seed = (uint32_t)value;
            options.seed_given = true;
        } else if (strcmp(argv[arg], "--seeds") == 0 && value <= 4096) {
            options.runs = (unsigned)value;
            options.runs_given = true;
        } else if (strcmp(argv[arg], "--items") == 0 && value <= MAX_ITEMS) {
            options.items = (unsigned)value;
        } else if (strcmp(argv[arg], "--steps") == 0 && value <= 1000000) {
            options.steps = (unsigned)value;
        } else {
            fprintf(stderr, "Invalid option/range: %s (seed: 1..UINT32_MAX, seeds: 1..4096, "
                    "items: 1..%d, steps: 1..1000000)\n", argv[arg], MAX_ITEMS);
            return 2;
        }
    }
    if (options.seed_given && !options.runs_given) options.runs = 1;
    const struct test_case cases[] = {
        {"trees", trees}, {"tree_fifo_lifo", tree_fifo_lifo}, {"tree_lifo_fifo", tree_lifo_fifo},
        {"fifo_tree_lifo", fifo_tree_lifo}, {"lifo_tree_fifo", lifo_tree_fifo},
        {"fifo_lifo_tree", fifo_lifo_tree}, {"lifo_fifo_tree", lifo_fifo_tree},
        {"inline_strings", inline_strings}, {"pointer_strings", pointer_strings}, {"stress", stress}
    };
    return run_case(argc >= 2 ? 2 : argc, argv, cases, ARRAY_COUNT(cases));
}
