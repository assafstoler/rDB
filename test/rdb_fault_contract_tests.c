/* Controlled consistency failures using live, owned fixtures. Comparators are
 * intentionally made unstable only in these cases; production callers must
 * serialize access and keep keys/comparators stable throughout an operation. */
#include "rdb_test_support.h"

enum { INDEXES = 3, QUIET, HIDE_UNDO, REJECT_RESTORE, BREAK_RESTORE_QUEUE };
typedef struct { rdb_bpp_t links[INDEXES]; int32_t key[INDEXES]; char *name; } item;
static item *victim, *other;
static int injection, armed, triggered, visits, cleanups;

static int compare(void *a, void *b)
{
    int32_t x = *(int32_t *)a, y = *(int32_t *)b;
    return (y > x) - (y < x);
}
static int compare_unstable(void *a, void *b)
{
    if (triggered && b == &victim->key[0])
        return injection == HIDE_UNDO ? -1 : 0;
    return compare(a, b);
}
static int compare_reject(void *a, void *b)
{
    int rc = compare(a, b);
    if (armed && b == &victim->key[2] && rc == 0) triggered = 1;
    return rc;
}
static int compare_restore(void *a, void *b)
{
    if (triggered && injection == BREAK_RESTORE_QUEUE && b == &victim->key[2])
        other->links[1].right = NULL; // Break one reciprocal link after reinsertion.
    return compare(a, b);
}

static rdb_pool_t *make_pool(char *name, int queue, void *first, void *last)
{
    rdb_pool_t *p = rdb_register_um_pool(name, INDEXES, KEY_OFFSET(item, key[0], INDEXES),
            RDB_BTREE | RDB_KINT32, first, 0);
    CHECK(p);
    CHECK(rdb_register_um_idx(p, 1, 0, RDB_BTREE | queue, NULL, 0) == 1);
    CHECK(rdb_register_um_idx(p, 2, KEY_OFFSET(item, key[2], INDEXES),
                RDB_BTREE | RDB_KINT32, last, 0) == 2);
    return p;
}
static item *new_item(int key, int last)
{
    item *p = checked_calloc(1, sizeof(*p));
    p->key[0] = p->key[1] = key;
    p->key[2] = last;
    return p;
}
static int count_visit(void *record, void *unused)
{
    (void)record; (void)unused;
    ++visits;
    return RDB_CB_DELETE_NODE;
}
static void count_cleanup(void *record, void *unused)
{
    (void)record; (void)unused;
    ++cleanups; // Deliberately do not free: assert that cleanup never happened.
}

static int count_reindex_visit(void *record, void *unused, rdb_reindex_ctx_t *ctx)
{
    (void)ctx;
    return count_visit(record, unused);
}

/* All public record operations must refuse an already faulted pool, without
 * allocating a replacement diagnostic, invoking callbacks, or changing links.
 * Metadata/lock lifetime operations remain available for orderly teardown. */
static void check_faulted(rdb_pool_t *p, item *record, int index)
{
    rdb_fault_info_t fault;
    CHECK(rdb_pool_get_fault(p, &fault) == 1);
    CHECK(fault.code == RDB_ERR_FAULTED && fault.record == record && fault.index == index);
    CHECK(fault.operation && fault.reason);
    CHECK(rdb_pool_get_fault(p, NULL) == 1);
    rdb_error("unrelated later diagnostic");
    rdb_fault_info_t again;
    CHECK(rdb_pool_get_fault(p, &again) == 1);
    CHECK(memcmp(&again, &fault, sizeof(fault)) == 0);
    rdb_pool_t *good = make_pool("unrelated", RDB_KFIFO, NULL, NULL);
    item *kept = new_item(77, 77);
    CHECK(rdb_insert(good, kept) == INDEXES);
    rdb_pool_t snapshot = *p; // Registration legitimately changes registry links.
    CHECK(rdb_pool_get_fault(good, &again) == 0 && again.code == 0 && !again.record);
    CHECK(rdb_pool_get_fault(NULL, &again) == -1);
    CHECK(rdb_insert(p, kept) == RDB_ERR_FAULTED);
    CHECK(rdb_insert_one(p, 0, kept) == RDB_ERR_FAULTED);
    CHECK(rdb_delete_one(p, 0, record) == RDB_ERR_FAULTED);
    CHECK(rdb_register_um_idx(p, 1, 0, RDB_BTREE | RDB_KFIFO, NULL, 0) == RDB_ERR_FAULTED);
    CHECK(!rdb_get(p, 0, &record->key[0]));
    CHECK(!rdb_get_const(p, 0, record->key[0]));
    void *before = kept, *after = kept;
    CHECK(!rdb_get_neigh(p, 0, &record->key[0], &before, &after));
    CHECK(!before && !after);
    CHECK(!rdb_delete(p, 0, &record->key[0]));
    CHECK(!rdb_delete_const(p, 0, record->key[0]));
    CHECK(!rdb_move(good, p, 0, &record->key[0]));
    CHECK(!rdb_move_const(good, p, 0, record->key[0]));
    CHECK(rdb_move2(good, p, 0, &record->key[0]) == RDB_ERR_FAULTED);
    CHECK(!rdb_move(p, good, 0, &kept->key[0]));
    CHECK(!rdb_move_const(p, good, 0, kept->key[0]));
    CHECK(rdb_move2(p, good, 0, &kept->key[0]) == RDB_ERR_FAULTED);
    visits = cleanups = 0;
    rdb_iterate(p, 0, count_visit, NULL, count_cleanup, NULL);
    rdb_iterate(p, 1, count_visit, NULL, count_cleanup, NULL);
    CHECK(rdb_iterate_reindex(p, 0, count_reindex_visit, NULL,
                count_cleanup, NULL) == RDB_ERR_FAULTED);
    rdb_flush(p, count_cleanup, NULL);
    rdb_dump(p, 0, ",");
    CHECK(visits == 0 && cleanups == 0);
    CHECK(memcmp(p, &snapshot, sizeof(snapshot)) == 0);
    CHECK(rdb_get_const(good, 0, 77) == kept && good->record_count == 1);
    CHECK(rdb_find_pool_by_name(p->name) == p);
    CHECK(rdb_lock(p, __func__) == 0);
    rdb_unlock(p, __func__);
    rdb_flush(good, NULL, NULL);
    rdb_drop_pool(good);
}

static void invalid_insert_layout(void)
{
    rdb_pool_t *p = make_pool("invalid-insert-layout", RDB_KFIFO, NULL, NULL);
    item *kept = new_item(10, 10), *candidate = new_item(20, 20);
    CHECK(rdb_insert(p, kept) == INDEXES);
    const unsigned counts[] = {0, RDB_POOL_MAX_IDX + 1, 255};
    for (unsigned i = 0; i < ARRAY_COUNT(counts); ++i) {
        p->indexCount = counts[i]; // Explicit malformed-metadata fixture.
        rdb_pool_t before = *p;
        item original = *candidate;
        CHECK(rdb_insert(p, candidate) == -1);
        CHECK(rdb_error_string && strstr(rdb_error_string, "invalid index count"));
        CHECK(strstr(rdb_error_string, "invalid-insert-layout"));
        CHECK(rdb_pool_get_fault(p, NULL) == 0);
        CHECK(memcmp(p, &before, sizeof(before)) == 0);
        CHECK(memcmp(candidate, &original, sizeof(original)) == 0);
        p->indexCount = INDEXES;
        CHECK(rdb_get_const(p, 0, 10) == kept);
    }
    CHECK(rdb_insert(p, candidate) == INDEXES);
    CHECK(rdb_pool_get_fault(p, NULL) == 0 && p->record_count == 2);
    rdb_flush(p, NULL, NULL);
    rdb_drop_pool(p);
}

static void insert_undo(void)
{
    for (int queue = RDB_KFIFO; queue <= RDB_KLIFO; ++queue) {
        injection = HIDE_UNDO; triggered = armed = 0;
        rdb_pool_t *p = make_pool("insert-undo", queue, compare_unstable, compare_reject);
        other = new_item(10, 7);
        victim = new_item(20, 7);
        CHECK(rdb_insert(p, other) == INDEXES);
        armed = 1;
        CHECK(rdb_insert(p, victim) == RDB_ERR_FAULTED);
        CHECK(triggered);
        CHECK(strstr(p->fault.operation, "insert rollback"));
        CHECK(p->record_count == 1);
        CHECK(other->links[0].right == victim); // Previously reported as ordinary rejection.
        check_faulted(p, victim, 0);
        rdb_drop_pool(p);
        free(victim); free(other); // Fixture owns all allocations, not damaged indexes.
    }
}

static void move_rollback(void)
{
    for (int mode = 0; mode < 3; ++mode) {
        triggered = armed = 0;
        injection = mode == 0 ? HIDE_UNDO : mode == 1 ? REJECT_RESTORE : BREAK_RESTORE_QUEUE;
        rdb_pool_t *src = make_pool("move-source", RDB_KFIFO,
                mode == 1 ? compare_unstable : NULL, mode == 2 ? compare_restore : NULL);
        rdb_pool_t *dst = make_pool("move-destination", RDB_KFIFO,
                mode == 0 ? compare_unstable : NULL, compare_reject);
        other = new_item(10, 10);
        victim = new_item(20, 20);
        item *conflict = new_item(mode == 0 ? 10 : 30, 20);
        CHECK(rdb_insert(src, other) == INDEXES);
        CHECK(rdb_insert(src, victim) == INDEXES);
        CHECK(rdb_insert(dst, conflict) == INDEXES);
        armed = 1;
        CHECK(rdb_move2(dst, src, 0, &victim->key[0]) == RDB_ERR_FAULTED);
        CHECK(triggered && src->fault.record == victim);
        if (mode == 0) {
            CHECK(dst->fault.record == victim);
            check_faulted(dst, victim, 0);
        } else {
            CHECK(rdb_pool_get_fault(dst, NULL) == 0);
            CHECK(rdb_get_const(dst, 0, conflict->key[0]) == conflict);
            CHECK(dst->record_count == 1);
        }
        check_faulted(src, victim, mode == 2 ? 1 : 0);
        rdb_drop_pool(src); rdb_drop_pool(dst);
        free(victim); free(other); free(conflict);
    }
}

/* Delete an index deliberately, then request a full record deletion. A partial
 * unlink must not report success, decrement count, call cleanup, or free data. */
static void partial_unlink(void)
{
    for (int method = 0; method < 10; ++method) {
        triggered = armed = 0;
        rdb_pool_t *p = make_pool("partial", method & 1 ? RDB_KLIFO : RDB_KFIFO, NULL, NULL);
        victim = new_item(20, 20);
        CHECK(rdb_insert(p, victim) == INDEXES);
        CHECK(rdb_delete_one(p, 2, victim) >= 0);
        visits = cleanups = 0;
        rdb_pool_t *dst = make_pool("untouched-destination", RDB_KFIFO, NULL, NULL);
        switch (method) {
        case 0: CHECK(!rdb_delete(p, 0, &victim->key[0])); break;
        case 1: CHECK(!rdb_delete_const(p, 0, 20)); break;
        case 2: CHECK(!rdb_move(dst, p, 0, &victim->key[0])); break;
        case 3: CHECK(!rdb_move_const(dst, p, 0, 20)); break;
        case 4: CHECK(rdb_move2(dst, p, 0, &victim->key[0]) == RDB_ERR_FAULTED); break;
        case 5: CHECK(!rdb_delete(p, 1, NULL)); break;
        default:
            rdb_iterate(p, method & 1, method < 8 ? count_visit : NULL, NULL,
                    method < 8 ? count_cleanup : NULL, NULL);
            break;
        }
        CHECK(rdb_pool_get_fault(p, NULL) == 1 && p->record_count == 1);
        CHECK(!rdb_pool_get_fault(dst, NULL) && dst->record_count == 0);
        CHECK(cleanups == 0 && victim->key[0] == 20); // ASan catches any default free.
        check_faulted(p, victim, 2);
        rdb_drop_pool(p); rdb_drop_pool(dst);
        free(victim);
    }
}

/* Explicitly call the internal helper with the condition that used to exit(1).
 * This symbol is not a public API; normal deletion swaps two-child nodes first. */
extern int _rdb_delete_by_pointer(rdb_pool_t *, void *, int, rdb_bpp_t *, int);
static int fault_in_callback(void *record, void *p)
{
    (void)record;
    ++visits;
    rdb_pool_t *pool = p;
    CHECK(_rdb_delete_by_pointer(pool, NULL, 0, pool->root[0], 0) == RDB_ERR_FAULTED);
    return RDB_CB_OK;
}
static void formerly_fatal(void)
{
    for (int active = -1; active <= 1; ++active) {
        triggered = armed = 0;
        rdb_pool_t *p = make_pool("two-children", RDB_KFIFO, NULL, NULL);
        item *items[] = {new_item(20, 20), new_item(10, 10), new_item(30, 30)};
        for (unsigned i = 0; i < ARRAY_COUNT(items); ++i)
            CHECK(rdb_insert(p, items[i]) == INDEXES);
        CHECK(p->root[0] == (rdb_bpp_t *)items[0]);
        visits = cleanups = 0;
        if (active < 0)
            CHECK(_rdb_delete_by_pointer(p, NULL, 0, p->root[0], 0) == RDB_ERR_FAULTED);
        else {
            rdb_iterate(p, active, fault_in_callback, p, count_cleanup, NULL);
            CHECK(visits == 1 && cleanups == 0);
        }
        CHECK(strstr(p->fault.reason, "two-child"));
        check_faulted(p, items[0], 0);
        rdb_drop_pool(p);
        for (unsigned i = 0; i < ARRAY_COUNT(items); ++i) free(items[i]);
    }
}

/* The public reindex iterator must stop before handling a deletion request
 * when the callback itself has faulted the pool. Cleanup of internal staging
 * allocations is still required; application records must remain owned. */
struct reindex_fault_request { rdb_pool_t *pool; int result, stage_edit; };
static int fault_in_reindex_callback(void *record, void *opaque, rdb_reindex_ctx_t *ctx)
{
    struct reindex_fault_request *request = opaque;
    ++visits;
    if (request->stage_edit) {
        item candidate = *(item *)record;
        candidate.key[2] += 100;
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate, RDB_INDEX_MASK(2)) == 0);
    }
    CHECK(_rdb_delete_by_pointer(request->pool, NULL, 0,
                request->pool->root[0], 0) == RDB_ERR_FAULTED);
    return request->result;
}
static void reindex_callback_fault(void)
{
    const int results[] = {RDB_CB_OK, RDB_CB_ABORT,
        RDB_CB_DELETE_NODE, RDB_CB_DELETE_NODE_AND_ABORT};
    for (size_t result = 0; result < ARRAY_COUNT(results); ++result)
    for (int custom_cleanup = 0; custom_cleanup < 2; ++custom_cleanup)
    for (int stage_edit = 0; stage_edit < 2; ++stage_edit) {
        rdb_pool_t *p = make_pool("reindex-callback-fault", RDB_KFIFO, NULL, NULL);
        item *items[] = {new_item(20, 20), new_item(10, 10), new_item(30, 30)};
        item before[ARRAY_COUNT(items)];
        for (size_t i = 0; i < ARRAY_COUNT(items); ++i)
            CHECK(rdb_insert(p, items[i]) == INDEXES);
        CHECK(p->root[0] == (rdb_bpp_t *)items[0]);
        for (size_t i = 0; i < ARRAY_COUNT(items); ++i)
            memcpy(&before[i], items[i], sizeof(before[i]));
        visits = cleanups = 0;
        struct reindex_fault_request request = {p, results[result], stage_edit};
        CHECK(rdb_iterate_reindex(p, 0, fault_in_reindex_callback, &request,
                    custom_cleanup ? count_cleanup : NULL, NULL) == RDB_ERR_FAULTED);
        if (visits != 1 || cleanups || p->record_count != ARRAY_COUNT(items))
            fprintf(stderr, "callback result=%d, custom cleanup=%d, staged edit=%d: "
                    "visits=%d cleanups=%d remaining=%u\n", request.result,
                    custom_cleanup, stage_edit, visits, cleanups, p->record_count);
        CHECK(visits == 1 && cleanups == 0 && p->record_count == ARRAY_COUNT(items));
        for (size_t i = 0; i < ARRAY_COUNT(items); ++i)
            CHECK(memcmp(items[i], &before[i], sizeof(before[i])) == 0);
        check_faulted(p, items[0], 0);
        rdb_drop_pool(p);
        for (size_t i = 0; i < ARRAY_COUNT(items); ++i) free(items[i]);
    }
}

static void queue_links(void)
{
    for (int queue = RDB_KFIFO; queue <= RDB_KLIFO; ++queue) {
        triggered = armed = 0;
        rdb_pool_t *p = make_pool("queue-links", queue, NULL, NULL);
        item *items[] = {new_item(10, 10), new_item(20, 20), new_item(30, 30)};
        for (unsigned i = 0; i < ARRAY_COUNT(items); ++i)
            CHECK(rdb_insert(p, items[i]) == INDEXES);
        item *middle = items[1];
        ((rdb_bpp_t *)middle->links[1].left)->right = NULL;
        CHECK(!rdb_delete(p, 0, &middle->key[0]));
        CHECK(strstr(p->fault.reason, "queue"));
        check_faulted(p, middle, 1);
        rdb_drop_pool(p);
        for (unsigned i = 0; i < ARRAY_COUNT(items); ++i) free(items[i]);
    }
}

enum { APPLY_UNLINK, APPLY_REINSERT, DELETE_UNLINK };
static int apply_injection;
static int compare_reindex_string(void *a, void *b)
{
    const char *old = *(char **)a, *next = *(char **)b;
    /* Match the live field address: staging comparisons must remain valid.
     * Inject only during apply's unlink/reinsert after the callback returns. */
    if (armed && b == &victim->name) {
        if (apply_injection == APPLY_UNLINK && strcmp(next, "alpha") == 0) {
            ++triggered;
            return 1; // Hide the existing left child from unlink.
        }
        if (apply_injection == APPLY_REINSERT && strcmp(next, "charlie") == 0) {
            ++triggered;
            return 0; // Reject the otherwise unique replacement at reinsertion.
        }
    }
    return strcmp(next, old);
}

struct apply_request { int mode; };
static int stage_fault(void *data, void *opaque, rdb_reindex_ctx_t *ctx)
{
    item *record = data;
    struct apply_request *request = opaque;
    ++visits;
    if (record == victim) {
        item candidate = *record;
        candidate.name = "charlie";
        CHECK(rdb_reindex_check_apply(ctx, record, &candidate, RDB_INDEX_MASK(2)) == 0);
        CHECK(strcmp(record->name, "alpha") == 0);
        if (request->mode == DELETE_UNLINK) {
            /* A live, owned neighbour is altered only to test containment. */
            ((rdb_bpp_t *)record->links[1].left)->right = NULL;
            return RDB_CB_DELETE_NODE;
        }
    }
    if (record == other) armed = 1;
    return RDB_CB_OK;
}

static void reindex_apply_fault(void)
{
    for (int mode = APPLY_UNLINK; mode <= DELETE_UNLINK; ++mode)
    for (int custom_cleanup = 0; custom_cleanup < 2; ++custom_cleanup) {
        apply_injection = mode;
        armed = triggered = visits = cleanups = 0;
        rdb_pool_t *p = rdb_register_um_pool("reindex-apply-fault", INDEXES,
                KEY_OFFSET(item, key[0], INDEXES), RDB_BTREE | RDB_KINT32, NULL, 0);
        CHECK(p);
        CHECK(rdb_register_um_idx(p, 1, 0, RDB_BTREE | RDB_KFIFO, NULL, 0) == 1);
        CHECK(rdb_register_um_idx(p, 2, KEY_OFFSET(item, name, INDEXES),
                    RDB_BTREE | RDB_KPSTR, compare_reindex_string, 0) == 2);
        other = new_item(20, 20);
        victim = new_item(10, 10);
        other->name = strdup("bravo");
        victim->name = strdup("alpha");
        CHECK(other->name && victim->name);
        CHECK(rdb_insert(p, other) == INDEXES);
        CHECK(rdb_insert(p, victim) == INDEXES);
        struct apply_request request = {mode};
        CHECK(rdb_iterate_reindex(p, 0, stage_fault, &request,
                    custom_cleanup ? count_cleanup : NULL, NULL) == RDB_ERR_FAULTED);
        CHECK(visits == (mode == DELETE_UNLINK ? 1 : 2));
        CHECK(cleanups == 0 && p->record_count == 2);
        CHECK(mode == DELETE_UNLINK || triggered > 0);
        CHECK(strcmp(victim->name, mode == APPLY_REINSERT ? "charlie" : "alpha") == 0);
        CHECK(strcmp(other->name, "bravo") == 0);
        if (mode != DELETE_UNLINK) CHECK(strcmp(p->fault.operation, "reindex apply") == 0);
        check_faulted(p, victim, mode == DELETE_UNLINK ? 1 : 2);
        rdb_drop_pool(p);
        /* Old keys and rejected proposals belong to rDB; surviving live fields
         * remain application-owned after a fault. Never traverse broken links. */
        free(victim->name); free(other->name);
        free(victim); free(other);
    }
    armed = 0;
}

static void reindex_metadata_fault(void)
{
    rdb_pool_t *p = rdb_register_um_pool("kcf-fault-metadata", 1,
            KEY_OFFSET(item, key[0], 1), RDB_BTREE | RDB_KCF, compare, 0);
    CHECK(p);
    const rdb_kcf_key_part_t part = {0, RDB_KINT32, 0};
    CHECK(rdb_register_kcf_key_parts(p, 0, &part, 1) == 0);
    item *records[] = {new_item(20, 20), new_item(10, 10), new_item(30, 30)};
    for (size_t i = 0; i < ARRAY_COUNT(records); ++i)
        CHECK(rdb_insert(p, records[i]) == 1);
    CHECK(p->root[0] == (rdb_bpp_t *)records[0]);
    CHECK(_rdb_delete_by_pointer(p, NULL, 0, p->root[0], 0) == RDB_ERR_FAULTED);
    rdb_pool_t before;
    memcpy(&before, p, sizeof(before));
    char *diagnostic = rdb_error_string;
    CHECK(rdb_register_kcf_key_parts(p, 0, &part, 1) == RDB_ERR_FAULTED);
    CHECK(memcmp(p, &before, sizeof(before)) == 0 && rdb_error_string == diagnostic);
    check_faulted(p, records[0], 0);
    rdb_drop_pool(p);
    for (size_t i = 0; i < ARRAY_COUNT(records); ++i) free(records[i]);
}

int main(int argc, char **argv)
{
    const struct test_case cases[] = {
        {"invalid_insert_layout", invalid_insert_layout}, {"insert_undo", insert_undo}, {"move_rollback", move_rollback},
        {"partial_unlink", partial_unlink}, {"formerly_fatal", formerly_fatal},
        {"queue_links", queue_links}, {"reindex_callback_fault", reindex_callback_fault},
        {"reindex_apply_fault", reindex_apply_fault}, {"reindex_metadata_fault", reindex_metadata_fault}
    };
    return run_case(argc, argv, cases, ARRAY_COUNT(cases));
}
