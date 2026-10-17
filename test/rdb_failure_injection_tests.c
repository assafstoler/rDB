#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdb.h"

static int failures;
static int fail_allocation;
static int watch_stats_format;
static int fail_stats_format;
static int format_attempts;
static int partial_format;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);

void *__wrap_malloc(size_t size)
{
    if (fail_allocation && --fail_allocation == 0)
        return NULL;
    return __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    if (fail_allocation && --fail_allocation == 0)
        return NULL;
    return __real_calloc(count, size);
}

static int should_fail_format(char *buffer, size_t size, const char *format)
{
    if (!watch_stats_format || strcmp(format, "Pool: %s : %u\n") != 0)
        return 0;
    ++format_attempts;
    if (!fail_stats_format || --fail_stats_format != 0)
        return 0;
    if (partial_format && size) {
        memset(buffer, '?', size - 1);
        buffer[size - 1] = '\0';
    }
    errno = EILSEQ;
    return 1;
}

int __wrap_snprintf(char *buffer, size_t size, const char *format, ...)
{
    if (should_fail_format(buffer, size, format))
        return -1;
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

#ifdef __GLIBC__
extern int __vsnprintf_chk(char *, size_t, int, size_t, const char *, va_list);
int __wrap___snprintf_chk(char *buffer, size_t size, int flags,
        size_t object_size, const char *format, ...)
{
    if (size <= object_size && should_fail_format(buffer, size, format))
        return -1;
    va_list args;
    va_start(args, format);
    int result = __vsnprintf_chk(buffer, size, flags, object_size, format, args);
    va_end(args);
    return result;
}
#endif

typedef struct {
    rdb_bpp_t link;
    int32_t key;
} record_t;

typedef struct {
    rdb_bpp_t links[2];
    int32_t id;
    char *name;
} reindex_record_t;

static int key_offset(void)
{
    return (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
}

static int compare_i32(void *old_key, void *new_key)
{
    int32_t old_value = *(int32_t *)old_key;
    int32_t new_value = *(int32_t *)new_key;
    return (new_value > old_value) - (new_value < old_value);
}

static rdb_pool_t *make_reindex_pool(const char *name, reindex_record_t **out)
{
    int id_offset = (int)(offsetof(reindex_record_t, id) - sizeof(rdb_bpp_t) * 2);
    int name_offset = (int)(offsetof(reindex_record_t, name) - sizeof(rdb_bpp_t) * 2);
    rdb_pool_t *pool = rdb_register_um_pool((char *)name, 2, id_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(pool != NULL);
    if (!pool)
        return NULL;
    CHECK(rdb_register_um_idx(pool, 1, name_offset,
                RDB_BTREE | RDB_KPSTR, NULL, 0) == 1);

    reindex_record_t *record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!record) {
        rdb_drop_pool(pool);
        return NULL;
    }
    record->id = 1;
    record->name = malloc(sizeof("alpha"));
    CHECK(record->name != NULL);
    if (!record->name) {
        free(record);
        rdb_drop_pool(pool);
        return NULL;
    }
    memcpy(record->name, "alpha", sizeof("alpha"));
    CHECK(rdb_insert(pool, record) == 2);
    *out = record;
    return pool;
}

typedef struct {
    int fail_at;
    int apply_result;
} reindex_failure_t;

static int fail_reindex_key_allocation(void *data, void *opaque,
        rdb_reindex_ctx_t *ctx)
{
    reindex_record_t *record = data;
    reindex_record_t candidate = *record;
    reindex_failure_t *state = opaque;
    candidate.id = 2;
    fail_allocation = state->fail_at <= 2 ? state->fail_at : 0;
    state->apply_result = rdb_reindex_check_apply(ctx, record, &candidate,
            RDB_INDEX_MASK(0));
    return RDB_CB_OK;
}

static void reindex_key_allocation_failures(void)
{
    for (int fail_at = 1; fail_at <= 2; ++fail_at) {
        reindex_record_t *record = NULL;
        rdb_pool_t *pool = make_reindex_pool("reindex-key-allocation", &record);
        if (!pool || !record) {
            if (pool) rdb_drop_pool(pool);
            continue;
        }
        reindex_failure_t state = {fail_at, 0};
        CHECK(rdb_iterate_reindex(pool, 0, fail_reindex_key_allocation,
                    &state, NULL, NULL) == RDB_ERR_REINDEX_NOMEM);
        CHECK(state.apply_result == RDB_ERR_REINDEX_NOMEM);
        CHECK(fail_allocation == 0);
        CHECK(rdb_pool_get_fault(pool, NULL) == 0);
        CHECK(pool->record_count == 1);
        CHECK(rdb_get_const(pool, 0, 1) == record);
        CHECK(rdb_get_const(pool, 0, 2) == NULL);
        CHECK(rdb_get(pool, 1, "alpha") == record);
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}

static int edit_name_with_failure(void *data, void *opaque,
        rdb_reindex_ctx_t *ctx)
{
    reindex_record_t *record = data;
    reindex_record_t candidate = *record;
    reindex_failure_t *state = opaque;
    char *edit_buffer = NULL;
    fail_allocation = state->fail_at <= 2 ? state->fail_at : 0;
    state->apply_result = rdb_reindex_kpstr_edit(ctx, record, &candidate,
            1, 0, 16, &edit_buffer);
    if (state->apply_result < 0)
        return RDB_CB_OK;
    strcpy(edit_buffer, "changed");
    state->apply_result = rdb_reindex_check_apply(ctx, record, &candidate,
            RDB_INDEX_MASK(1));
    if (!state->apply_result && state->fail_at == 3)
        fail_allocation = 1; /* Fail the old-string list in the apply phase. */
    return RDB_CB_OK;
}

static void reindex_string_allocation_failures(void)
{
    for (int fail_at = 1; fail_at <= 3; ++fail_at) {
        reindex_record_t *record = NULL;
        rdb_pool_t *pool = make_reindex_pool("reindex-string-allocation", &record);
        if (!pool || !record) {
            if (pool) rdb_drop_pool(pool);
            continue;
        }
        reindex_failure_t state = {fail_at, 0};
        CHECK(rdb_iterate_reindex(pool, 0, edit_name_with_failure,
                    &state, NULL, NULL) == RDB_ERR_REINDEX_NOMEM);
        CHECK(state.apply_result == (fail_at == 3 ? 0 : RDB_ERR_REINDEX_NOMEM));
        CHECK(fail_allocation == 0);
        CHECK(rdb_pool_get_fault(pool, NULL) == 0);
        CHECK(pool->record_count == 1);
        CHECK(rdb_get_const(pool, 0, 1) == record);
        CHECK(rdb_get(pool, 1, "alpha") == record);
        CHECK(rdb_get(pool, 1, "changed") == NULL);
        rdb_flush(pool, NULL, NULL);
        rdb_drop_pool(pool);
    }
}

static void kcf_metadata_allocation_failures(void)
{
    rdb_kcf_key_part_t part = {0, RDB_KINT32, 0};
    for (int fail_at = 1; fail_at <= 2; ++fail_at) {
        char name[] = "kcf-metadata-allocation";
        int offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
        rdb_pool_t *pool = rdb_register_um_pool(name, 1, offset,
                RDB_BTREE | RDB_KCF, (void *)compare_i32, 0);
        CHECK(pool != NULL);
        if (!pool)
            continue;
        fail_allocation = fail_at;
        CHECK(rdb_register_kcf_key_parts(pool, 0, &part, 1) ==
                RDB_ERR_REINDEX_NOMEM);
        CHECK(fail_allocation == 0);
        CHECK(rdb_register_kcf_key_parts(pool, 0, &part, 1) == 0);
        rdb_drop_pool(pool);
    }
}

static void check_registry(rdb_pool_t **pools, int count)
{
    for (int i = 0; i < count; ++i) {
        CHECK(rdb_find_pool_by_name(pools[i]->name) == pools[i]);
        CHECK(pools[i]->prev == (i + 1 < count ? pools[i + 1] : NULL));
        CHECK(pools[i]->next == (i ? pools[i - 1] : NULL));
    }
}

static void registration_allocation_failures(void)
{
    static const char *names[] = {"allocation-tail", "allocation-middle", "allocation-head"};
    for (int count = 0; count <= 3; ++count) {
        rdb_pool_t *pools[3] = {NULL};
        for (int i = 0; i < count; ++i) {
            pools[i] = rdb_register_um_pool((char *)names[i], 1, key_offset(),
                    RDB_BTREE | RDB_KINT32, NULL, 0);
            CHECK(pools[i] != NULL);
        }

        for (int allocation = 1; allocation <= 2; ++allocation) {
            for (int attempt = 0; attempt < 2; ++attempt) {
                fail_allocation = allocation;
                CHECK(rdb_register_um_pool("allocation-failed", 1, key_offset(),
                            RDB_BTREE | RDB_KINT32, NULL, 0) == NULL);
                CHECK(fail_allocation == 0);
                CHECK(rdb_find_pool_by_name("allocation-failed") == NULL);
                CHECK(rdb_error_string && strstr(rdb_error_string, "allocation"));
                check_registry(pools, count);
            }
        }

        rdb_pool_t *retry = rdb_register_um_pool("allocation-failed", 1,
                key_offset(), RDB_BTREE | RDB_KINT32, NULL, 0);
        CHECK(retry != NULL);
        if (retry) {
            CHECK(rdb_find_pool_by_name("allocation-failed") == retry);
            CHECK(retry->prev == NULL);
            CHECK(retry->next == (count ? pools[count - 1] : NULL));
            rdb_drop_pool(retry);
        }
        for (int i = count - 1; i >= 0; --i)
            rdb_drop_pool(pools[i]);
    }
}

static void stats_format_failure(void)
{
    static const char *names[] = {"format-tail", "format-middle", "format-head"};
    rdb_pool_t *pools[3];
    for (int i = 0; i < 3; ++i) {
        pools[i] = rdb_register_um_pool((char *)names[i], 1, key_offset(),
                RDB_BTREE | RDB_KINT32, NULL, 0);
        CHECK(pools[i] != NULL);
    }

    for (int partial = 0; partial < 2; ++partial) {
        unsigned char storage[258];
        memset(storage, 0xa5, sizeof(storage));
        char *buffer = (char *)storage + 1;
        const char expected[] = "Pool: format-head : 0\n:END:";
        rdb_error("previous formatting diagnostic");
        char *saved_error = rdb_error_string;
        fail_stats_format = 2;
        format_attempts = 0;
        partial_format = partial;
        watch_stats_format = 1;
        CHECK(rdb_print_pool_stats(buffer, 256) == buffer);
        watch_stats_format = 0;

        CHECK(fail_stats_format == 0 && format_attempts == 2);
        CHECK(strcmp(buffer, expected) == 0);
        CHECK(storage[0] == 0xa5);
        for (size_t i = 257; i < sizeof(storage); ++i)
            CHECK(storage[i] == 0xa5);
        CHECK(rdb_error_string != saved_error);
        CHECK(rdb_error_string && strstr(rdb_error_string, "Failure during error print"));
        check_registry(pools, 3);

        rdb_pool_t *probe = rdb_register_um_pool("format-probe", 1,
                key_offset(), RDB_BTREE | RDB_KINT32, NULL, 0);
        CHECK(probe != NULL);
        if (probe) rdb_drop_pool(probe);
    }

    char report[128];
    CHECK(rdb_print_pool_stats(report, sizeof(report)) == report);
    CHECK(strstr(report, "Pool: format-head : 0\n") == report);
    CHECK(strstr(report, ":END:") != NULL);
    for (int i = 0; i < 3; ++i)
        rdb_drop_pool(pools[i]);
}

int main(void)
{
    rdb_init();
    registration_allocation_failures();
    reindex_key_allocation_failures();
    reindex_string_allocation_failures();
    kcf_metadata_allocation_failures();
    stats_format_failure();
    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
