#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdb.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

typedef struct {
    rdb_bpp_t links[2];
    int32_t id;
    char *name;
} record_t;

typedef struct {
    rdb_bpp_t link;
    char key[16];
} inline_record_t;

typedef struct {
    char *name;
    int32_t number;
} compound_key_t;

typedef struct {
    rdb_bpp_t link;
    compound_key_t key;
} compound_record_t;

typedef struct {
    char code[8];
    uint16_t rank;
} inline_compound_key_t;

typedef struct {
    rdb_bpp_t link;
    inline_compound_key_t key;
} inline_compound_record_t;

static int compare_compound(void *old_key, void *new_key)
{
    compound_key_t *old = old_key;
    compound_key_t *new_value = new_key;
    int compare = strcmp(new_value->name, old->name);
    if (compare) return compare;
    return (new_value->number > old->number) -
            (new_value->number < old->number);
}

static int compare_inline_compound(void *old_key, void *new_key)
{
    inline_compound_key_t *old = old_key;
    inline_compound_key_t *new_value = new_key;
    int compare = strcmp(new_value->code, old->code);
    if (compare) return compare;
    return (new_value->rank > old->rank) - (new_value->rank < old->rank);
}

static int compare_string_pointer(void *old_key, void *new_key)
{
    char *old_value = *(char **)old_key;
    char *new_value = *(char **)new_key;
    return strcmp(new_value, old_value);
}

static void reset_rdb(uint32_t flags)
{
    rdb_clean(0);
    CHECK(rdb_init_ex(flags) == 0);
}

static rdb_pool_t *make_record_pool(void)
{
    int offset = (int)offsetof(record_t, id) - (int)(sizeof(rdb_bpp_t) * 2);
    int name_offset = (int)offsetof(record_t, name) -
            (int)(sizeof(rdb_bpp_t) * 2);
    rdb_pool_t *pool = rdb_register_um_pool("reindex-record", 2, offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL);
    if (!pool) return NULL;
    CHECK(rdb_register_um_idx(pool, 1, name_offset,
                RDB_BTREE | RDB_KPSTR, NULL, 0) == 1);
    return pool;
}

static record_t *new_record(int32_t id, const char *name)
{
    record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!record) return NULL;
    record->id = id;
    record->name = strdup(name);
    CHECK(record->name != NULL);
    return record;
}

typedef struct {
    rdb_pool_t *pool;
    int visits;
    int staged_checks;
} rename_state_t;

static int update_keys(void *data, void *user_data, rdb_reindex_ctx_t *ctx)
{
    record_t *record = data;
    rename_state_t *state = user_data;
    record_t candidate = *record;
    ++state->visits;

    if (record->id == 10) {
        char *edit;
        candidate.id = 40;
        CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 1, 0,
                    32, &edit) == 0);
        strcpy(edit, "zulu");
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                    RDB_INDEX_MASK(0) | RDB_INDEX_MASK(1)) == 0);
    } else if (record->id == 20) {
        int32_t old_id = 10;
        ++state->staged_checks;
        CHECK(rdb_get(state->pool, 0, &old_id) != NULL);
        CHECK(rdb_get(state->pool, 1, "alpha") != NULL);
        CHECK(rdb_get(state->pool, 1, "zulu") == NULL);
        candidate.id = 10;
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                    RDB_INDEX_MASK(0)) == 0);
    }
    return RDB_CB_OK;
}

static void test_staged_main_and_kpstr(void)
{
    rdb_pool_t *pool;
    record_t *first, *second, *third;
    int32_t id;
    rename_state_t state;

    reset_rdb(0);
    pool = make_record_pool();
    first = new_record(10, "alpha");
    second = new_record(20, "bravo");
    third = new_record(30, "charlie");
    CHECK(rdb_insert(pool, first) == 2);
    CHECK(rdb_insert(pool, second) == 2);
    CHECK(rdb_insert(pool, third) == 2);

    memset(&state, 0, sizeof(state));
    state.pool = pool;
    CHECK(rdb_iterate_reindex(pool, 0, update_keys, &state, NULL, NULL) == 0);
    CHECK(state.visits == 3);
    CHECK(state.staged_checks == 1);
    id = 40;
    CHECK(rdb_get(pool, 0, &id) == first);
    id = 10;
    CHECK(rdb_get(pool, 0, &id) == second);
    CHECK(rdb_get(pool, 1, "alpha") == NULL);
    CHECK(rdb_get(pool, 1, "zulu") == first);
    CHECK(rdb_pool_get_fault(pool, NULL) == 0);
    rdb_flush(pool, NULL, NULL);
}

typedef struct {
    int visits;
} conflict_state_t;

static int conflict_after_one_update(void *data, void *user_data,
        rdb_reindex_ctx_t *ctx)
{
    record_t *record = data;
    record_t candidate = *record;
    conflict_state_t *state = user_data;
    ++state->visits;
    if (record->id == 1) {
        candidate.id = 4;
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                    RDB_INDEX_MASK(0)) == 0);
    } else if (record->id == 2) {
        candidate.id = 3;
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                    RDB_INDEX_MASK(0)) == RDB_ERR_REINDEX_CONFLICT);
    }
    return RDB_CB_OK;
}

static int omit_changed_index(void *data, void *unused, rdb_reindex_ctx_t *ctx)
{
    record_t *record = data;
    record_t candidate = *record;
    (void)unused;
    candidate.id += 1;
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate, 0) ==
            RDB_ERR_REINDEX_UNMARKED);
    return RDB_CB_OK;
}

static void test_unmarked_index_is_rejected(void)
{
    rdb_pool_t *pool;
    record_t *record;
    int32_t id = 1;

    reset_rdb(0);
    pool = make_record_pool();
    record = new_record(id, "one");
    CHECK(rdb_insert(pool, record) == 2);
    CHECK(rdb_iterate_reindex(pool, 0, omit_changed_index,
                NULL, NULL, NULL) == RDB_ERR_REINDEX_UNMARKED);
    CHECK(rdb_get(pool, 0, &id) == record);
    CHECK(rdb_pool_get_fault(pool, NULL) == 0);
    rdb_flush(pool, NULL, NULL);
}

static void test_conflict_aborts_and_flushes_prior(void)
{
    rdb_pool_t *pool;
    record_t *one, *two, *three;
    conflict_state_t state = {0};
    int32_t id;

    reset_rdb(0);
    pool = make_record_pool();
    one = new_record(1, "one");
    two = new_record(2, "two");
    three = new_record(3, "three");
    CHECK(rdb_insert(pool, one) == 2);
    CHECK(rdb_insert(pool, two) == 2);
    CHECK(rdb_insert(pool, three) == 2);
    CHECK(rdb_iterate_reindex(pool, 0, conflict_after_one_update,
                &state, NULL, NULL) == RDB_ERR_REINDEX_CONFLICT);
    CHECK(state.visits == 2);
    id = 4;
    CHECK(rdb_get(pool, 0, &id) == one);
    id = 2;
    CHECK(rdb_get(pool, 0, &id) == two);
    CHECK(rdb_pool_get_fault(pool, NULL) == 0);
    rdb_flush(pool, NULL, NULL);
}

static int change_inline_key(void *data, void *unused, rdb_reindex_ctx_t *ctx)
{
    inline_record_t *record = data;
    inline_record_t candidate = *record;
    (void)unused;
    if (!strcmp(record->key, "alpha")) {
        strcpy(candidate.key, "delta");
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                    RDB_INDEX_MASK(0)) == 0);
    }
    return RDB_CB_OK;
}

static void test_inline_kstr(void)
{
    int offset = (int)offsetof(inline_record_t, key) - (int)sizeof(rdb_bpp_t);
    rdb_pool_t *pool;
    inline_record_t *record;

    reset_rdb(0);
    CHECK(rdb_register_um_pool("reindex-kstr-missing-size", 1, offset,
                RDB_BTREE | RDB_KSTR, NULL, 0) == NULL);
    pool = rdb_register_um_pool("reindex-kstr", 1, offset,
            RDB_BTREE | RDB_KSTR, NULL, sizeof(((inline_record_t *)0)->key));
    CHECK(pool != NULL);
    record = calloc(1, sizeof(*record));
    strcpy(record->key, "alpha");
    CHECK(rdb_insert(pool, record) == 1);
    CHECK(rdb_iterate_reindex(pool, 0, change_inline_key, NULL,
                NULL, NULL) == 0);
    CHECK(rdb_get(pool, 0, "alpha") == NULL);
    CHECK(rdb_get(pool, 0, "delta") == record);
    rdb_flush(pool, NULL, NULL);
}

static int reject_unterminated_key(void *data, void *unused,
        rdb_reindex_ctx_t *ctx)
{
    inline_record_t *record = data;
    inline_record_t candidate = *record;
    (void)unused;
    memset(candidate.key, 'x', sizeof(candidate.key));
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                RDB_INDEX_MASK(0)) == RDB_ERR_REINDEX_INVALID);
    return RDB_CB_OK;
}

static void test_inline_kstr_bounds(void)
{
    int offset = (int)offsetof(inline_record_t, key) - (int)sizeof(rdb_bpp_t);
    rdb_pool_t *pool;
    inline_record_t *record;

    reset_rdb(0);
    pool = rdb_register_um_pool("reindex-kstr-bounds", 1, offset,
            RDB_BTREE | RDB_KSTR, NULL, sizeof(((inline_record_t *)0)->key));
    record = calloc(1, sizeof(*record));
    strcpy(record->key, "alpha");
    CHECK(rdb_insert(pool, record) == 1);
    CHECK(rdb_iterate_reindex(pool, 0, reject_unterminated_key,
                NULL, NULL, NULL) == RDB_ERR_REINDEX_INVALID);
    CHECK(rdb_get(pool, 0, "alpha") == record);
    rdb_flush(pool, NULL, NULL);
}

typedef struct {
    int mode;
} kpstr_edit_state_t;

static int reject_kpstr_edit(void *data, void *user_data,
        rdb_reindex_ctx_t *ctx)
{
    record_t *record = data;
    kpstr_edit_state_t *state = user_data;
    record_t candidate = *record;
    char *edit = NULL;

    if (state->mode == 0) {
        CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 0, 0,
                    32, &edit) == RDB_ERR_REINDEX_UNSUPPORTED);
    } else if (state->mode == 1) {
        CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 1, 1,
                    32, &edit) == RDB_ERR_REINDEX_UNSUPPORTED);
    } else if (state->mode == 2) {
        CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 1, 0,
                    2, &edit) == RDB_ERR_REINDEX_INVALID);
    } else if (state->mode == 3) {
        candidate.name = NULL;
        CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 1, 0,
                    32, &edit) == RDB_ERR_REINDEX_INVALID);
    } else {
        CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 1, 0,
                    8, &edit) == 0);
        memset(edit, 'x', 8);
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                    RDB_INDEX_MASK(1)) == RDB_ERR_REINDEX_INVALID);
    }
    return RDB_CB_OK;
}

static void test_kpstr_edit_rejections(void)
{
    const int expected[] = {
        RDB_ERR_REINDEX_UNSUPPORTED,
        RDB_ERR_REINDEX_UNSUPPORTED,
        RDB_ERR_REINDEX_INVALID,
        RDB_ERR_REINDEX_INVALID,
        RDB_ERR_REINDEX_INVALID
    };

    for (int mode = 0; mode < (int)(sizeof(expected) / sizeof(expected[0]));
            ++mode) {
        rdb_pool_t *pool;
        record_t *record;
        kpstr_edit_state_t state = {mode};

        reset_rdb(0);
        pool = make_record_pool();
        record = new_record(1, "alpha");
        CHECK(rdb_insert(pool, record) == 2);
        CHECK(rdb_iterate_reindex(pool, 0, reject_kpstr_edit,
                    &state, NULL, NULL) == expected[mode]);
        CHECK(rdb_get(pool, 1, "alpha") == record);
        rdb_flush(pool, NULL, NULL);
    }
}

static int change_compound_key(void *data, void *unused,
        rdb_reindex_ctx_t *ctx)
{
    compound_record_t *record = data;
    compound_record_t candidate = *record;
    char *edit;
    (void)unused;
    if (record->key.number == 1) {
        candidate.key.number = 2;
        CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 0,
                    offsetof(compound_key_t, name), 32, &edit) == 0);
        strcpy(edit, "beta");
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                    RDB_INDEX_MASK(0)) == 0);
    }
    return RDB_CB_OK;
}

static void test_kcf_layout(void)
{
    int offset = (int)offsetof(compound_record_t, key) - (int)sizeof(rdb_bpp_t);
    rdb_kcf_key_part_t parts[] = {
        {offsetof(compound_key_t, name), RDB_KPSTR, 0},
        {offsetof(compound_key_t, number), RDB_KINT32, 0}
    };
    rdb_pool_t *pool;
    compound_record_t *record;

    reset_rdb(0);
    pool = rdb_register_um_pool("reindex-kcf", 1, offset,
            RDB_BTREE | RDB_KCF, (void *)compare_compound, 0);
    CHECK(pool != NULL);
    CHECK(rdb_register_kcf_key_parts(pool, 0, parts, 2) == 0);
    record = calloc(1, sizeof(*record));
    record->key.name = strdup("alpha");
    record->key.number = 1;
    CHECK(rdb_insert(pool, record) == 1);
    CHECK(rdb_iterate_reindex(pool, 0, change_compound_key,
                NULL, NULL, NULL) == 0);
    CHECK(rdb_get(pool, 0, &(compound_key_t){"beta", 2}) == record);
    CHECK(rdb_get(pool, 0, &(compound_key_t){"alpha", 1}) == NULL);
    rdb_flush(pool, NULL, NULL);
}

static int change_inline_compound(void *data, void *unused,
        rdb_reindex_ctx_t *ctx)
{
    inline_compound_record_t *record = data;
    inline_compound_record_t candidate = *record;
    (void)unused;
    strcpy(candidate.key.code, "zulu");
    candidate.key.rank = 9;
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                RDB_INDEX_MASK(0)) == 0);
    return RDB_CB_OK;
}

static void test_kcf_inline_string_layout(void)
{
    int offset = (int)(offsetof(inline_compound_record_t, key) -
            sizeof(rdb_bpp_t));
    rdb_kcf_key_part_t parts[] = {
        {offsetof(inline_compound_key_t, code), RDB_KSTR,
            sizeof(((inline_compound_key_t *)0)->code)},
        {offsetof(inline_compound_key_t, rank), RDB_KUINT16, 0}
    };
    rdb_pool_t *pool;
    inline_compound_record_t *record;

    reset_rdb(0);
    pool = rdb_register_um_pool("reindex-kcf-inline", 1, offset,
            RDB_BTREE | RDB_KCF, (void *)compare_inline_compound, 0);
    CHECK(pool != NULL);
    if (!pool) return;
    CHECK(rdb_register_kcf_key_parts(pool, 0, parts, 2) == 0);
    record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!record) {
        rdb_drop_pool(pool);
        return;
    }
    strcpy(record->key.code, "alpha");
    record->key.rank = 1;
    CHECK(rdb_insert(pool, record) == 1);
    CHECK(rdb_iterate_reindex(pool, 0, change_inline_compound,
                NULL, NULL, NULL) == 0);
    CHECK(rdb_get(pool, 0, &(inline_compound_key_t){"alpha", 1}) == NULL);
    CHECK(rdb_get(pool, 0, &(inline_compound_key_t){"zulu", 9}) == record);
    rdb_flush(pool, NULL, NULL);
}

static int reject_unterminated_kcf_string(void *data, void *unused,
        rdb_reindex_ctx_t *ctx)
{
    inline_compound_record_t *record = data;
    inline_compound_record_t candidate = *record;
    (void)unused;
    memset(candidate.key.code, 'x', sizeof(candidate.key.code));
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                RDB_INDEX_MASK(0)) == RDB_ERR_REINDEX_INVALID);
    return RDB_CB_OK;
}

static void test_kcf_string_bounds(void)
{
    int offset = (int)(offsetof(inline_compound_record_t, key) -
            sizeof(rdb_bpp_t));
    rdb_kcf_key_part_t parts[] = {
        {offsetof(inline_compound_key_t, code), RDB_KSTR,
            sizeof(((inline_compound_key_t *)0)->code)},
        {offsetof(inline_compound_key_t, rank), RDB_KUINT16, 0}
    };
    rdb_pool_t *pool;
    inline_compound_record_t *record;

    reset_rdb(0);
    pool = rdb_register_um_pool("reindex-kcf-inline-bounds", 1, offset,
            RDB_BTREE | RDB_KCF, (void *)compare_inline_compound, 0);
    CHECK(pool != NULL);
    if (!pool) return;
    CHECK(rdb_register_kcf_key_parts(pool, 0, parts, 2) == 0);
    record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!record) {
        rdb_drop_pool(pool);
        return;
    }
    strcpy(record->key.code, "alpha");
    record->key.rank = 1;
    CHECK(rdb_insert(pool, record) == 1);
    CHECK(rdb_iterate_reindex(pool, 0, reject_unterminated_kcf_string,
                NULL, NULL, NULL) == RDB_ERR_REINDEX_INVALID);
    CHECK(rdb_get(pool, 0, &(inline_compound_key_t){"alpha", 1}) == record);
    rdb_flush(pool, NULL, NULL);
}

static int count_callback(void *record, void *data, rdb_reindex_ctx_t *ctx)
{
    int *count = data;
    (void)record;
    (void)ctx;
    ++*count;
    return RDB_CB_OK;
}

typedef struct {
    int deletes;
} delete_state_t;

static int delete_every_record(void *record, void *unused,
        rdb_reindex_ctx_t *ctx)
{
    (void)record;
    (void)unused;
    (void)ctx;
    return RDB_CB_DELETE_NODE;
}

static void delete_record_cleanup(void *data, void *user_data)
{
    record_t *record = data;
    delete_state_t *state = user_data;
    CHECK(record->name == NULL);
    ++state->deletes;
    free(record);
}

static void test_delete_cleanup_owns_kpstr(void)
{
    rdb_pool_t *pool;
    delete_state_t state = {0};

    reset_rdb(0);
    pool = make_record_pool();
    CHECK(rdb_insert(pool, new_record(1, "one")) == 2);
    CHECK(rdb_insert(pool, new_record(2, "two")) == 2);
    CHECK(rdb_iterate_reindex(pool, 0, delete_every_record,
                NULL, delete_record_cleanup, &state) == 0);
    CHECK(state.deletes == 2);
    CHECK(pool->root[0] == NULL && pool->root[1] == NULL);
}

static int try_kcfut_change(void *data, void *unused, rdb_reindex_ctx_t *ctx)
{
    record_t *record = data;
    record_t candidate = *record;
    (void)unused;
    candidate.name = "changed";
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                RDB_INDEX_MASK(1)) == RDB_ERR_REINDEX_UNSUPPORTED);
    return RDB_CB_OK;
}

static void test_kcfut_modes(void)
{
    int first_offset = (int)offsetof(record_t, id) - (int)(sizeof(rdb_bpp_t) * 2);
    int second_offset = (int)offsetof(record_t, name) -
            (int)(sizeof(rdb_bpp_t) * 2);
    rdb_pool_t *pool;
    record_t *record;
    int visits = 0;

    reset_rdb(0);
    pool = rdb_register_um_pool("reindex-kcfut-strict", 2, first_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(rdb_register_um_idx(pool, 1, second_offset,
                RDB_BTREE | RDB_KCFUT, (void *)compare_string_pointer, 0) == 1);
    record = new_record(1, "one");
    CHECK(rdb_insert(pool, record) == 2);
    CHECK(rdb_iterate_reindex(pool, 0, count_callback, &visits,
                NULL, NULL) == RDB_ERR_REINDEX_UNSUPPORTED);
    CHECK(visits == 0);
    rdb_flush(pool, NULL, NULL);

    reset_rdb(RDB_INIT_ALLOW_KCFUT);
    pool = rdb_register_um_pool("reindex-kcfut-allow", 2, first_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(rdb_register_um_idx(pool, 1, second_offset,
                RDB_BTREE | RDB_KCFUT, (void *)compare_string_pointer, 0) == 1);
    record = new_record(1, "one");
    CHECK(rdb_insert(pool, record) == 2);
    CHECK(rdb_iterate_reindex(pool, 0, try_kcfut_change,
                NULL, NULL, NULL) == RDB_ERR_REINDEX_UNSUPPORTED);
    CHECK(rdb_pool_get_fault(pool, NULL) == 0);
    rdb_flush(pool, NULL, NULL);
}

typedef struct {
    int visits;
} abort_state_t;

static int update_then_abort(void *data, void *user_data,
        rdb_reindex_ctx_t *ctx)
{
    record_t *record = data;
    abort_state_t *state = user_data;
    ++state->visits;
    if (record->id == 1) {
        record_t candidate = *record;
        candidate.id = 10;
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                    RDB_INDEX_MASK(0)) == 0);
        return RDB_CB_OK;
    }
    return RDB_ITERATE_ABORTED;
}

static void test_abort_applies_prior_changes(void)
{
    rdb_pool_t *pool;
    record_t *one, *two, *three;
    abort_state_t state = {0};
    int32_t id;

    reset_rdb(0);
    pool = make_record_pool();
    one = new_record(1, "one");
    two = new_record(2, "two");
    three = new_record(3, "three");
    CHECK(rdb_insert(pool, one) == 2);
    CHECK(rdb_insert(pool, two) == 2);
    CHECK(rdb_insert(pool, three) == 2);
    CHECK(rdb_iterate_reindex(pool, 0, update_then_abort,
                &state, NULL, NULL) == RDB_ITERATE_ABORTED);
    CHECK(state.visits == 2);
    id = 10;
    CHECK(rdb_get(pool, 0, &id) == one);
    id = 2;
    CHECK(rdb_get(pool, 0, &id) == two);
    id = 3;
    CHECK(rdb_get(pool, 0, &id) == three);
    rdb_flush(pool, NULL, NULL);
}

typedef struct {
    int cleaned;
} staged_delete_state_t;

static int stage_and_delete_first(void *data, void *unused,
        rdb_reindex_ctx_t *ctx)
{
    record_t *record = data;
    (void)unused;
    record_t candidate = *record;
    candidate.id = 100;
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                RDB_INDEX_MASK(0)) == 0);
    return RDB_CB_DELETE_NODE_AND_ABORT;
}

static void staged_delete_cleanup(void *data, void *user_data)
{
    record_t *record = data;
    staged_delete_state_t *state = user_data;
    CHECK(record->name == NULL);
    ++state->cleaned;
    free(record);
}

static void test_delete_abort_discards_current_update(void)
{
    rdb_pool_t *pool;
    record_t *one, *two;
    staged_delete_state_t state = {0};
    int32_t id;

    reset_rdb(0);
    pool = make_record_pool();
    one = new_record(1, "one");
    two = new_record(2, "two");
    CHECK(rdb_insert(pool, one) == 2);
    CHECK(rdb_insert(pool, two) == 2);
    CHECK(rdb_iterate_reindex(pool, 0, stage_and_delete_first,
                NULL, staged_delete_cleanup, &state) == RDB_ITERATE_ABORTED);
    CHECK(state.cleaned == 1);
    CHECK(pool->record_count == 1);
    id = 1;
    CHECK(rdb_get(pool, 0, &id) == NULL);
    id = 100;
    CHECK(rdb_get(pool, 0, &id) == NULL);
    id = 2;
    CHECK(rdb_get(pool, 0, &id) == two);
    rdb_flush(pool, NULL, NULL);
}

static void test_iterate_reindex_argument_validation(void)
{
    int offset = (int)(offsetof(record_t, id) - sizeof(rdb_bpp_t) * 2);
    rdb_pool_t *pool;
    rdb_pool_t *queue;
    rdb_pool_t *unregistered;
    int32_t visits = 0;

    reset_rdb(0);
    CHECK(rdb_iterate_reindex(NULL, 0, count_callback, &visits,
                NULL, NULL) == RDB_ERR_REINDEX_INVALID);
    pool = make_record_pool();
    CHECK(rdb_iterate_reindex(pool, 0, count_callback, &visits,
                NULL, NULL) == 0);
    CHECK(visits == 0);
    CHECK(rdb_iterate_reindex(pool, -1, count_callback, &visits,
                NULL, NULL) == RDB_ERR_REINDEX_INVALID);
    CHECK(rdb_iterate_reindex(pool, 0, NULL, &visits,
                NULL, NULL) == RDB_ERR_REINDEX_INVALID);

    queue = rdb_register_um_pool("reindex-queue", 1, 0,
            RDB_BTREE | RDB_KFIFO, NULL, 0);
    CHECK(queue != NULL);
    if (queue)
        CHECK(rdb_iterate_reindex(queue, 0, count_callback, &visits,
                    NULL, NULL) == RDB_ERR_REINDEX_UNSUPPORTED);

    unregistered = rdb_register_um_pool("reindex-unregistered", 2, offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(unregistered != NULL);
    if (unregistered)
        CHECK(rdb_iterate_reindex(unregistered, 0, count_callback, &visits,
                    NULL, NULL) == RDB_ERR_REINDEX_INVALID);
    int custom_offset = (int)(offsetof(compound_record_t, key) -
            sizeof(rdb_bpp_t));
    rdb_pool_t *missing_kcf = rdb_register_um_pool("reindex-missing-kcf",
            1, custom_offset, RDB_BTREE | RDB_KCF,
            (void *)compare_compound, 0);
    CHECK(missing_kcf != NULL);
    if (missing_kcf)
        CHECK(rdb_iterate_reindex(missing_kcf, 0, count_callback, &visits,
                    NULL, NULL) == RDB_ERR_REINDEX_UNSUPPORTED);
    CHECK(visits == 0);
    rdb_clean(0);
}

int main(void)
{
    test_staged_main_and_kpstr();
    test_conflict_aborts_and_flushes_prior();
    test_unmarked_index_is_rejected();
    test_inline_kstr();
    test_inline_kstr_bounds();
    test_kpstr_edit_rejections();
    test_kcf_layout();
    test_kcf_inline_string_layout();
    test_kcf_string_bounds();
    test_delete_cleanup_owns_kpstr();
    test_kcfut_modes();
    test_abort_applies_prior_changes();
    test_delete_abort_discards_current_update();
    test_iterate_reindex_argument_validation();
    rdb_clean(0);
    if (failures) return 1;
    puts("Ok");
    return 0;
}
