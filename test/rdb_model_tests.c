#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdb.h"

enum { INDEXES = 3, POOLS = 2, ITEMS = 12, QUEUE_ROLE = -1 };

typedef struct {
    rdb_bpp_t links[INDEXES];
    int32_t key[INDEXES];
    unsigned id;
} record_t;

typedef struct {
    rdb_pool_t *pool[POOLS];
    record_t records[ITEMS];
    int owner[ITEMS];
    int key_field[INDEXES];
    uint32_t flags[INDEXES];
    unsigned queue[POOLS][INDEXES][ITEMS];
    size_t queue_count[POOLS][INDEXES];
    uint32_t random;
    unsigned step;
    unsigned layout;
} model_t;

typedef struct {
    record_t *records[ITEMS];
    size_t count;
} visit_data_t;

typedef struct {
    model_t *model;
    int pool;
    int delete_modulus;
    unsigned visits;
} iterate_data_t;

static unsigned failures;
static model_t *active_model;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: step=%u layout=%u check failed: %s\n", \
                __FILE__, __LINE__, active_model ? active_model->step : 0, \
                active_model ? active_model->layout : 0, #condition); \
        ++failures; \
    } \
} while (0)

static const int layouts[][INDEXES] = {
    {0, QUEUE_ROLE, 2},
    {0, 2, QUEUE_ROLE},
    {QUEUE_ROLE, 0, 2},
    {QUEUE_ROLE, 2, 0},
    {2, 0, QUEUE_ROLE},
    {2, QUEUE_ROLE, 0}
};

static const int32_t keys0[ITEMS] = {
    50, 30, 70, 20, 40, 60, 80, 10, 35, 45, 65, 75
};
static const int32_t keys2[ITEMS] = {
    65, 25, 85, 15, 45, 75, 95, 5, 35, 55, 90, 100
};

static uint32_t random_next(model_t *model)
{
    uint32_t value = model->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    model->random = value;
    return value;
}

static int key_offset(int field)
{
    return (int)(offsetof(record_t, key[field]) -
            sizeof(rdb_bpp_t) * INDEXES);
}

static rdb_pool_t *make_pool(model_t *model, int pool_index)
{
    char name[48];
    snprintf(name, sizeof(name), "model-%u-%d", model->layout, pool_index);
    int first_field = model->key_field[0];
    int first_offset = first_field == QUEUE_ROLE ? 0 : key_offset(first_field);
    uint32_t first_type = model->flags[0];
    rdb_pool_t *pool = rdb_register_um_pool(name, INDEXES, first_offset,
            RDB_BTREE | first_type, NULL, 0);
    CHECK(pool != NULL);
    if (!pool)
        return NULL;

    for (int index = 1; index < INDEXES; ++index) {
        int field = model->key_field[index];
        int offset = field == QUEUE_ROLE ? 0 : key_offset(field);
        CHECK(rdb_register_um_idx(pool, index, offset,
                    RDB_BTREE | model->flags[index], NULL, 0) == index);
    }
    return pool;
}

static void queue_insert(model_t *model, int pool, int index, unsigned id)
{
    if (!(model->flags[index] & RDB_NOKEYS))
        return;
    unsigned *queue = model->queue[pool][index];
    size_t count = model->queue_count[pool][index];
    CHECK(count < ITEMS);
    if (count >= ITEMS)
        return;
    if (model->flags[index] == RDB_KFIFO)
        queue[count] = id;
    else {
        memmove(queue + 1, queue, count * sizeof(*queue));
        queue[0] = id;
    }
    model->queue_count[pool][index] = count + 1;
}

static void queue_remove(model_t *model, int pool, int index, unsigned id)
{
    if (!(model->flags[index] & RDB_NOKEYS))
        return;
    unsigned *queue = model->queue[pool][index];
    size_t count = model->queue_count[pool][index], position = 0;
    while (position < count && queue[position] != id)
        ++position;
    CHECK(position < count);
    if (position >= count)
        return;
    memmove(queue + position, queue + position + 1,
            (count - position - 1) * sizeof(*queue));
    model->queue_count[pool][index] = count - 1;
}

static void model_add(model_t *model, int pool, unsigned id)
{
    CHECK(id < ITEMS && model->owner[id] == -1);
    if (id >= ITEMS || model->owner[id] != -1)
        return;
    model->owner[id] = pool;
    for (int index = 0; index < INDEXES; ++index)
        queue_insert(model, pool, index, id);
}

static void model_remove(model_t *model, unsigned id)
{
    CHECK(id < ITEMS && model->owner[id] >= 0 && model->owner[id] < POOLS);
    if (id >= ITEMS || model->owner[id] < 0 || model->owner[id] >= POOLS)
        return;
    int pool = model->owner[id];
    for (int index = 0; index < INDEXES; ++index)
        queue_remove(model, pool, index, id);
    model->owner[id] = -1;
}

static int choose_record(model_t *model, int pool, int inactive)
{
    unsigned start = random_next(model) % ITEMS;
    for (unsigned offset = 0; offset < ITEMS; ++offset) {
        unsigned id = (start + offset) % ITEMS;
        if ((inactive && model->owner[id] == -1) ||
                (!inactive && model->owner[id] == pool))
            return (int)id;
    }
    return -1;
}

static int collect(void *data, void *opaque)
{
    visit_data_t *visit = opaque;
    CHECK(visit->count < ITEMS);
    if (visit->count < ITEMS)
        visit->records[visit->count++] = data;
    return RDB_CB_OK;
}

static void keep_record(void *data, void *unused)
{
    (void)data;
    (void)unused;
}

static size_t expected_order(model_t *model, int pool, int index,
        unsigned ordered[ITEMS])
{
    if (model->flags[index] & RDB_NOKEYS) {
        size_t count = model->queue_count[pool][index];
        memcpy(ordered, model->queue[pool][index], count * sizeof(*ordered));
        return count;
    }

    size_t count = 0;
    int field = model->key_field[index];
    for (unsigned id = 0; id < ITEMS; ++id) {
        if (model->owner[id] != pool)
            continue;
        size_t position = count++;
        while (position && model->records[ordered[position - 1]].key[field] >
                model->records[id].key[field]) {
            ordered[position] = ordered[position - 1];
            --position;
        }
        ordered[position] = id;
    }
    return count;
}

static unsigned pool_count(model_t *model, int pool)
{
    unsigned count = 0;
    for (unsigned id = 0; id < ITEMS; ++id)
        count += model->owner[id] == pool;
    return count;
}

static void verify_model(model_t *model)
{
    for (int pool_index = 0; pool_index < POOLS; ++pool_index) {
        rdb_pool_t *pool = model->pool[pool_index];
        CHECK(pool != NULL);
        if (!pool)
            continue;
        CHECK(pool->record_count == pool_count(model, pool_index));

        for (int index = 0; index < INDEXES; ++index) {
            unsigned expected[ITEMS];
            visit_data_t actual = {{NULL}, 0};
            size_t count = expected_order(model, pool_index, index, expected);
            rdb_iterate(pool, index, collect, &actual, NULL, NULL);
            CHECK(actual.count == count);
            for (size_t n = 0; n < count && n < actual.count; ++n)
                CHECK(actual.records[n] == &model->records[expected[n]]);

            if (model->flags[index] & RDB_NOKEYS) {
                rdb_bpp_t *head = count
                    ? (rdb_bpp_t *)&model->records[expected[0]] : NULL;
                rdb_bpp_t *tail = count
                    ? (rdb_bpp_t *)&model->records[expected[count - 1]] : NULL;
                CHECK(pool->root[index] == (rdb_bpp_t *)head);
                CHECK(pool->tail[index] == (rdb_bpp_t *)tail);
                CHECK(rdb_get(pool, index, NULL) ==
                        (count ? &model->records[expected[0]] : NULL));
                for (size_t n = 0; n < count; ++n) {
                    rdb_bpp_t *link = (rdb_bpp_t *)&model->records[expected[n]] + index;
                    rdb_bpp_t *previous = n
                        ? (rdb_bpp_t *)&model->records[expected[n - 1]] + index : NULL;
                    rdb_bpp_t *next = n + 1 < count
                        ? (rdb_bpp_t *)&model->records[expected[n + 1]] + index : NULL;
                    CHECK(link->left == previous && link->right == next);
                }
            } else {
                int field = model->key_field[index];
                for (unsigned id = 0; id < ITEMS; ++id)
                    if (model->owner[id] == pool_index)
                        CHECK(rdb_get(pool, index,
                                    &model->records[id].key[field]) ==
                                &model->records[id]);
            }
        }
    }
}

static void insert_record(model_t *model)
{
    unsigned id = (unsigned)choose_record(model, -1, 1);
    if (id >= ITEMS)
        return;
    int pool = (int)(random_next(model) % POOLS);
    CHECK(rdb_insert(model->pool[pool], &model->records[id]) == INDEXES);
    model_add(model, pool, id);
}

static void delete_record(model_t *model)
{
    int pool_index = (int)(random_next(model) % POOLS);
    int index = (int)(random_next(model) % INDEXES);
    int id;
    void *key;
    if (model->flags[index] & RDB_NOKEYS) {
        size_t count = model->queue_count[pool_index][index];
        if (!count)
            return;
        id = (int)model->queue[pool_index][index][0];
        key = NULL;
    } else {
        id = choose_record(model, pool_index, 0);
        if (id < 0)
            return;
        key = &model->records[id].key[model->key_field[index]];
    }
    CHECK(rdb_delete(model->pool[pool_index], index, key) == &model->records[id]);
    model_remove(model, (unsigned)id);
}

static void move_record(model_t *model)
{
    int source = (int)(random_next(model) % POOLS);
    int destination = 1 - source;
    int index = (int)(random_next(model) % INDEXES);
    int id;
    void *key;
    if (model->flags[index] & RDB_NOKEYS) {
        size_t count = model->queue_count[source][index];
        if (!count)
            return;
        id = (int)model->queue[source][index][0];
        key = NULL;
    } else {
        id = choose_record(model, source, 0);
        if (id < 0)
            return;
        key = &model->records[id].key[model->key_field[index]];
    }
    CHECK(rdb_move(model->pool[destination], model->pool[source], index, key) ==
            &model->records[id]);
    model_remove(model, (unsigned)id);
    model_add(model, destination, (unsigned)id);
}

static void lookup_record(model_t *model)
{
    int pool = (int)(random_next(model) % POOLS);
    int index = (int)(random_next(model) % INDEXES);
    if (model->flags[index] & RDB_NOKEYS) {
        size_t count = model->queue_count[pool][index];
        void *expected = count
            ? &model->records[model->queue[pool][index][0]] : NULL;
        CHECK(rdb_get(model->pool[pool], index, NULL) == expected);
    } else {
        int id = choose_record(model, pool, 0);
        if (id >= 0)
            CHECK(rdb_get(model->pool[pool], index,
                        &model->records[id].key[model->key_field[index]]) ==
                    &model->records[id]);
        else
            CHECK(rdb_get(model->pool[pool], index, NULL) == NULL);
    }
}

static int iterate_delete(void *data, void *opaque)
{
    record_t *record = data;
    iterate_data_t *iteration = opaque;
    CHECK(record->id < ITEMS);
    CHECK(iteration->model->owner[record->id] == iteration->pool);
    ++iteration->visits;
    if ((int)(record->id % 4) != iteration->delete_modulus)
        return RDB_CB_OK;
    model_remove(iteration->model, record->id);
    return RDB_CB_DELETE_NODE;
}

static void iterate_and_delete(model_t *model)
{
    int pool = (int)(random_next(model) % POOLS);
    int index = (int)(random_next(model) % INDEXES);
    iterate_data_t iteration = {
        model, pool, (int)(random_next(model) % 4), 0
    };
    rdb_iterate(model->pool[pool], index, iterate_delete, &iteration,
            keep_record, NULL);
    CHECK(iteration.visits <= ITEMS);
}

static void flush_record(void *data, void *opaque)
{
    model_t *model = opaque;
    record_t *record = data;
    CHECK(record->id < ITEMS);
    CHECK(model->owner[record->id] >= 0);
    model_remove(model, record->id);
}

static void flush_pool(model_t *model, int pool)
{
    rdb_flush(model->pool[pool], flush_record, model);
}

static void run_sequence(unsigned layout, uint32_t seed)
{
    model_t model;
    memset(&model, 0, sizeof(model));
    active_model = &model;
    model.layout = layout;
    model.random = seed ? seed : 1;

    for (int index = 0; index < INDEXES; ++index) {
        model.key_field[index] = layouts[layout][index];
        if (model.key_field[index] == QUEUE_ROLE)
            model.flags[index] = (layout & 1) ? RDB_KLIFO : RDB_KFIFO;
        else
            model.flags[index] = RDB_KINT32;
    }
    for (unsigned id = 0; id < ITEMS; ++id) {
        model.owner[id] = -1;
        model.records[id].id = id;
        model.records[id].key[0] = keys0[id];
        model.records[id].key[1] = (int32_t)(id * 17 + 3);
        model.records[id].key[2] = keys2[id];
    }
    for (int pool = 0; pool < POOLS; ++pool)
        model.pool[pool] = make_pool(&model, pool);
    if (!model.pool[0] || !model.pool[1])
        goto cleanup;

    for (model.step = 0; model.step < 240; ++model.step) {
        switch (random_next(&model) % 5) {
        case 0: insert_record(&model); break;
        case 1: delete_record(&model); break;
        case 2: move_record(&model); break;
        case 3: lookup_record(&model); break;
        case 4: iterate_and_delete(&model); break;
        }
        verify_model(&model);
        if (model.step % 79 == 78) {
            flush_pool(&model, 0);
            flush_pool(&model, 1);
            verify_model(&model);
        }
    }

cleanup:
    if (model.pool[0]) {
        flush_pool(&model, 0);
        rdb_drop_pool(model.pool[0]);
    }
    if (model.pool[1]) {
        flush_pool(&model, 1);
        rdb_drop_pool(model.pool[1]);
    }
    active_model = NULL;
}

int main(void)
{
    static const uint32_t seeds[] = {UINT32_C(0x17), UINT32_C(0x243f6a88), UINT32_C(0x9e3779b9)};
    rdb_init();
    for (unsigned layout = 0; layout < sizeof(layouts) / sizeof(layouts[0]); ++layout)
        for (size_t seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); ++seed)
            run_sequence(layout, seeds[seed]);
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
