/* Staged edits own copies, keep live records unchanged until apply, and must
 * resolve every temporary on rejection, replacement, deletion and abort. */
#include "rdb_test_support.h"

enum { INDEXES = 3 };
typedef struct {
    rdb_bpp_t links[INDEXES];
    int32_t id, tag;
    char *name;
} item;

static int compare_i32(void *a, void *b)
{
    int32_t x = *(int32_t *)a, y = *(int32_t *)b;
    return (y > x) - (y < x);
}

static rdb_pool_t *make_pool(int last_kind)
{
    rdb_pool_t *pool = rdb_register_um_pool("reindex-contract", INDEXES,
            KEY_OFFSET(item, id, INDEXES), RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool);
    CHECK(rdb_register_um_idx(pool, 1, KEY_OFFSET(item, name, INDEXES),
                RDB_BTREE | RDB_KPSTR, NULL, 0) == 1);
    CHECK(rdb_register_um_idx(pool, 2, KEY_OFFSET(item, tag, INDEXES),
                RDB_BTREE | last_kind,
                last_kind == RDB_KCFUT ? compare_i32 : NULL, 0) == 2);
    return pool;
}

static item *insert(rdb_pool_t *pool, int32_t id, const char *name)
{
    item *record = checked_calloc(1, sizeof(*record));
    record->id = id;
    record->tag = id + 100;
    record->name = strdup(name);
    CHECK(record->name);
    CHECK(rdb_insert(pool, record) == INDEXES);
    return record;
}

static void check_unchanged(rdb_pool_t *pool, item *record, const item *before)
{
    CHECK(memcmp(record, before, sizeof(*record)) == 0);
    CHECK(rdb_pool_get_fault(pool, NULL) == 0);
    CHECK(rdb_get(pool, 0, &before->id) == record);
    CHECK(rdb_get(pool, 1, "alpha") == record);
    CHECK(strcmp(record->name, "alpha") == 0);
    CHECK(pool->record_count == 1);
}

enum bad_request {
    APPLY_NULL_RECORD, APPLY_NULL_CANDIDATE, APPLY_OTHER_RECORD, APPLY_BAD_MASK,
    EDIT_NULL_RECORD, EDIT_NULL_CANDIDATE, EDIT_OTHER_RECORD, EDIT_NULL_OUTPUT,
    EDIT_NEGATIVE_INDEX, EDIT_OUTSIDE_INDEX, EDIT_ZERO_CAPACITY, BAD_REQUEST_COUNT
};
struct request_state { int mode, visits; };

static int reject_request(void *data, void *opaque, rdb_reindex_ctx_t *ctx)
{
    item *record = data, candidate = *record, other_record = *record;
    struct request_state *state = opaque;
    char *edit = NULL;
    int rc;
    ++state->visits;
    switch (state->mode) {
    case APPLY_NULL_RECORD:
        rc = rdb_reindex_check_apply(ctx, NULL, &candidate, 1); break;
    case APPLY_NULL_CANDIDATE:
        rc = rdb_reindex_check_apply(ctx, record, NULL, 1); break;
    case APPLY_OTHER_RECORD:
        rc = rdb_reindex_check_apply(ctx, &other_record, &candidate, 1); break;
    case APPLY_BAD_MASK:
        rc = rdb_reindex_check_apply(ctx, record, &candidate, RDB_INDEX_MASK(INDEXES)); break;
    default:
        rc = rdb_reindex_kpstr_edit(ctx,
                state->mode == EDIT_NULL_RECORD ? NULL :
                    state->mode == EDIT_OTHER_RECORD ? &other_record : record,
                state->mode == EDIT_NULL_CANDIDATE ? NULL : &candidate,
                state->mode == EDIT_NEGATIVE_INDEX ? -1 :
                    state->mode == EDIT_OUTSIDE_INDEX ? INDEXES : 1,
                0, state->mode == EDIT_ZERO_CAPACITY ? 0 : 16,
                state->mode == EDIT_NULL_OUTPUT ? NULL : &edit);
    }
    CHECK(rc == RDB_ERR_REINDEX_INVALID);
    char *first_error = rdb_error_string;
    CHECK(first_error);
    candidate.id = 50;
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate, 1) == rc);
    CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 1, 0, 16, &edit) == rc);
    CHECK(rdb_error_string == first_error);
    /* An error must stop before honoring this deletion request. */
    return RDB_CB_DELETE_NODE;
}

static void invalid_requests(void)
{
    CHECK(rdb_reindex_check_apply(NULL, NULL, NULL, 0) == RDB_ERR_REINDEX_INVALID);
    CHECK(rdb_reindex_kpstr_edit(NULL, NULL, NULL, 0, 0, 0, NULL) == RDB_ERR_REINDEX_INVALID);
    for (int mode = 0; mode < BAD_REQUEST_COUNT; ++mode) {
        rdb_pool_t *pool = make_pool(RDB_KFIFO);
        item *record = insert(pool, 1, "alpha"), before;
        memcpy(&before, record, sizeof(before));
        struct request_state state = {mode, 0};
        CHECK(rdb_iterate_reindex(pool, 0, reject_request, &state, NULL, NULL)
                == RDB_ERR_REINDEX_INVALID);
        CHECK(state.visits == 1);
        check_unchanged(pool, record, &before);
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}

enum edit_mode { REPLACE, SMALLER, UNTERMINATED };
static int edit_twice(void *data, void *opaque, rdb_reindex_ctx_t *ctx)
{
    item *record = data, candidate = *record;
    int mode = *(int *)opaque;
    char *first, *second;
    CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 1, 0, 16, &first) == 0);
    strcpy(first, "bravo");
    CHECK(candidate.name == first && record->name != first);
    CHECK(strcmp(record->name, "alpha") == 0);
    if (mode == UNTERMINATED) memset(first, 'x', 16);
    int rc = rdb_reindex_kpstr_edit(ctx, record, &candidate, 1, 0,
            mode == SMALLER ? 5 : 6, &second);
    if (mode != REPLACE) {
        CHECK(rc == RDB_ERR_REINDEX_INVALID);
        return RDB_CB_OK;
    }
    CHECK(rc == 0 && second != first && candidate.name == second);
    CHECK(strcmp(second, "bravo") == 0);
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate, RDB_INDEX_MASK(1)) == 0);
    strcpy(second, "delta");
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate, RDB_INDEX_MASK(1)) == 0);
    /* The accepted edit is a deep copy, independent of subsequent edits. */
    strcpy(second, "other");
    CHECK(strcmp(record->name, "alpha") == 0);
    return RDB_CB_OK;
}

static void repeated_string_edits(void)
{
    for (int mode = REPLACE; mode <= UNTERMINATED; ++mode) {
        rdb_pool_t *pool = make_pool(RDB_KFIFO);
        item *record = insert(pool, 1, "alpha"), before;
        memcpy(&before, record, sizeof(before));
        CHECK(rdb_iterate_reindex(pool, 0, edit_twice, &mode, NULL, NULL)
                == (mode == REPLACE ? 0 : RDB_ERR_REINDEX_INVALID));
        if (mode == REPLACE) {
            CHECK(strcmp(record->name, "delta") == 0);
            CHECK(rdb_get(pool, 1, "delta") == record);
            CHECK(!rdb_get(pool, 1, "alpha") && !rdb_get(pool, 1, "bravo"));
            CHECK(!rdb_get(pool, 1, "other"));
            CHECK(pool->record_count == 1 && rdb_pool_get_fault(pool, NULL) == 0);
        } else check_unchanged(pool, record, &before);
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}

struct mask_state { int reject, visits; };
static int change_with_companion(void *data, void *opaque, rdb_reindex_ctx_t *ctx)
{
    item *record = data, candidate = *record;
    struct mask_state *state = opaque;
    ++state->visits;
    candidate.id += 10;
    rdb_index_mask_t mask = RDB_INDEX_MASK(0);
    if (state->reject) mask |= RDB_INDEX_MASK(2);
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate, mask)
            == (state->reject ? RDB_ERR_REINDEX_UNSUPPORTED : 0));
    return RDB_CB_OK;
}

static void companion_masks(void)
{
    const int kinds[] = {RDB_KFIFO, RDB_KLIFO, RDB_KCFUT};
    for (size_t kind = 0; kind < ARRAY_COUNT(kinds); ++kind)
    for (int reject = 0; reject < 2; ++reject) {
        rdb_clean(0);
        CHECK(rdb_init_ex(RDB_INIT_ALLOW_KCFUT) == 0);
        rdb_pool_t *pool = make_pool(kinds[kind]);
        item *records[] = {insert(pool, 1, "alpha"), insert(pool, 2, "bravo")};
        rdb_bpp_t links[2];
        for (int i = 0; i < 2; ++i)
            memcpy(&links[i], &records[i]->links[2], sizeof(links[i]));
        void *head = pool->root[2], *tail = pool->tail[2];
        struct mask_state state = {reject, 0};
        CHECK(rdb_iterate_reindex(pool, 0, change_with_companion, &state, NULL, NULL)
                == (reject ? RDB_ERR_REINDEX_UNSUPPORTED : 0));
        CHECK(state.visits == (reject ? 1 : 2));
        CHECK(pool->record_count == 2 && rdb_pool_get_fault(pool, NULL) == 0);
        CHECK(pool->root[2] == head && pool->tail[2] == tail);
        for (int i = 0; i < 2; ++i) {
            CHECK(memcmp(&records[i]->links[2], &links[i], sizeof(links[i])) == 0);
            CHECK(records[i]->id == i + 1 + (reject ? 0 : 10));
            CHECK(records[i]->tag == i + 101);
            CHECK(rdb_get(pool, 0, &records[i]->id) == records[i]);
            CHECK(rdb_get(pool, 1, i ? "bravo" : "alpha") == records[i]);
            if (kinds[kind] == RDB_KCFUT)
                CHECK(rdb_get(pool, 2, &records[i]->tag) == records[i]);
        }
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}

enum { COMPOUND_INDEXES = 4 };
typedef struct { char *first, *second; char code[8]; } compound_key;
typedef struct {
    rdb_bpp_t links[COMPOUND_INDEXES];
    int32_t id;
    compound_key key;
} compound_item;

static int compare_compound(void *a, void *b)
{
    const compound_key *old = a, *next = b;
    int rc = strcmp(next->first, old->first);
    if (!rc) rc = strcmp(next->second, old->second);
    if (!rc) rc = strcmp(next->code, old->code);
    return rc;
}

static rdb_pool_t *compound_pool(void)
{
    rdb_pool_t *pool = rdb_register_um_pool("reindex-compound", COMPOUND_INDEXES,
            KEY_OFFSET(compound_item, id, COMPOUND_INDEXES),
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool);
    CHECK(rdb_register_um_idx(pool, 1, KEY_OFFSET(compound_item, key, COMPOUND_INDEXES),
                RDB_BTREE | RDB_KCF, compare_compound, 0) == 1);
    const rdb_kcf_key_part_t parts[] = {
        {offsetof(compound_key, first), RDB_KPSTR, 0},
        {offsetof(compound_key, second), RDB_KPSTR, 0},
        {offsetof(compound_key, code), RDB_KSTR, sizeof(((compound_key *)0)->code)}
    };
    CHECK(rdb_register_kcf_key_parts(pool, 1, parts, ARRAY_COUNT(parts)) == 0);
    CHECK(rdb_register_um_idx(pool, 2, KEY_OFFSET(compound_item, key.first, COMPOUND_INDEXES),
                RDB_BTREE | RDB_KPSTR, NULL, 0) == 2);
    CHECK(rdb_register_um_idx(pool, 3, 0, RDB_BTREE | RDB_KFIFO, NULL, 0) == 3);
    return pool;
}

static compound_item *insert_compound(rdb_pool_t *pool, int id)
{
    compound_item *record = checked_calloc(1, sizeof(*record));
    record->id = id;
    char name[32];
    int length = snprintf(name, sizeof(name), "name-%d", id);
    CHECK(length > 0 && (size_t)length < sizeof(name));
    record->key.first = strdup(name);
    CHECK(record->key.first);
    /* Aliases within one record must be cleared/freed exactly once. */
    record->key.second = record->key.first;
    strcpy(record->key.code, "code");
    CHECK(rdb_insert(pool, record) == COMPOUND_INDEXES);
    return record;
}

static int reject_compound_string(void *data, void *opaque, rdb_reindex_ctx_t *ctx)
{
    compound_item *record = data, candidate = *record;
    int mode = *(int *)opaque;
    if (mode == 0) candidate.key.second = NULL;
    else {
        char *edit;
        CHECK(rdb_reindex_kpstr_edit(ctx, record, &candidate, 1,
                    offsetof(compound_key, second), 16, &edit) == 0);
        memset(edit, 'x', 16);
    }
    /* Rejecting the second owned component must release the first copy. */
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate, RDB_INDEX_MASK(1))
            == RDB_ERR_REINDEX_INVALID);
    return RDB_CB_OK;
}

static void compound_string_rejection(void)
{
    for (int mode = 0; mode < 2; ++mode) {
        rdb_pool_t *pool = compound_pool();
        compound_item *record = insert_compound(pool, 1), before;
        memcpy(&before, record, sizeof(before));
        CHECK(rdb_iterate_reindex(pool, 0, reject_compound_string, &mode, NULL, NULL)
                == RDB_ERR_REINDEX_INVALID);
        CHECK(memcmp(record, &before, sizeof(before)) == 0);
        CHECK(strcmp(record->key.first, "name-1") == 0);
        CHECK(rdb_get(pool, 1, &before.key) == record);
        CHECK(rdb_get(pool, 2, "name-1") == record);
        CHECK(pool->record_count == 1 && rdb_pool_get_fault(pool, NULL) == 0);
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}

struct delete_state { rdb_pool_t *pool; int abort, visits, cleaned; };
static int stage_and_delete(void *data, void *opaque, rdb_reindex_ctx_t *ctx)
{
    compound_item *record = data, candidate = *record;
    struct delete_state *state = opaque;
    ++state->visits;
    candidate.id += 10;
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate, RDB_INDEX_MASK(0)) == 0);
    return state->abort ? RDB_CB_DELETE_NODE_AND_ABORT : RDB_CB_DELETE_NODE;
}

static void deleted_compound(void *data, void *opaque)
{
    compound_item *record = data;
    struct delete_state *state = opaque;
    CHECK(!record->key.first && !record->key.second);
    CHECK(!rdb_get(state->pool, 0, &record->id));
    CHECK(state->pool->record_count == (unsigned)(3 - state->visits));
    ++state->cleaned;
    free(record);
}

static void staged_delete_cleanup(void)
{
    for (int custom = 0; custom < 2; ++custom)
    for (int abort = 0; abort < 2; ++abort) {
        rdb_pool_t *pool = compound_pool();
        compound_item *records[3];
        for (int i = 0; i < 3; ++i) records[i] = insert_compound(pool, i + 1);
        struct delete_state state = {pool, abort, 0, 0};
        CHECK(rdb_iterate_reindex(pool, 0, stage_and_delete, &state,
                    custom ? deleted_compound : NULL, &state)
                == (abort ? RDB_ITERATE_ABORTED : 0));
        CHECK(state.visits == (abort ? 1 : 3));
        CHECK(state.cleaned == (custom ? state.visits : 0));
        CHECK(pool->record_count == (unsigned)(3 - state.visits));
        CHECK(rdb_pool_get_fault(pool, NULL) == 0);
        for (int i = 0; i < 3; ++i) {
            CHECK(!rdb_get_const(pool, 0, i + 11));
            CHECK(rdb_get_const(pool, 0, i + 1) == (i < state.visits ? NULL : records[i]));
        }
        if (!abort) for (int index = 0; index < COMPOUND_INDEXES; ++index)
            CHECK(!pool->root[index] && !pool->tail[index]);
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}

int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"invalid_requests", invalid_requests}, {"repeated_string_edits", repeated_string_edits},
        {"companion_masks", companion_masks}, {"compound_string_rejection", compound_string_rejection},
        {"staged_delete_cleanup", staged_delete_cleanup}
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
