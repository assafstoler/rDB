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

typedef union {
    int8_t i8;
    uint8_t u8;
    int16_t i16;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    size_t size;
    ssize_t ssize;
    void *pointer;
#ifdef USE_128_BIT_TYPES
    __int128_t i128;
    __uint128_t u128;
#endif
} key_value_t;

typedef struct {
    rdb_bpp_t link;
    key_value_t key;
} record_t;

typedef struct {
    uint32_t kind;
    key_value_t replacement;
    int visits;
} edit_state_t;

static void set_key(key_value_t *key, uint32_t kind, const key_value_t *value)
{
    switch (kind) {
        case RDB_KINT8: key->i8 = value->i8; break;
        case RDB_KUINT8: key->u8 = value->u8; break;
        case RDB_KINT16: key->i16 = value->i16; break;
        case RDB_KUINT16: key->u16 = value->u16; break;
        case RDB_KINT32: key->i32 = value->i32; break;
        case RDB_KUINT32: key->u32 = value->u32; break;
        case RDB_KINT64: key->i64 = value->i64; break;
        case RDB_KUINT64: key->u64 = value->u64; break;
        case RDB_KSIZE_t: key->size = value->size; break;
        case RDB_KSSIZE_t: key->ssize = value->ssize; break;
        case RDB_KPTR: key->pointer = value->pointer; break;
#ifdef USE_128_BIT_TYPES
        case RDB_KINT128: key->i128 = value->i128; break;
        case RDB_KUINT128: key->u128 = value->u128; break;
#endif
    }
}

static void *key_address(key_value_t *key, uint32_t kind)
{
    switch (kind) {
        case RDB_KINT8: return &key->i8;
        case RDB_KUINT8: return &key->u8;
        case RDB_KINT16: return &key->i16;
        case RDB_KUINT16: return &key->u16;
        case RDB_KINT32: return &key->i32;
        case RDB_KUINT32: return &key->u32;
        case RDB_KINT64: return &key->i64;
        case RDB_KUINT64: return &key->u64;
        case RDB_KSIZE_t: return &key->size;
        case RDB_KSSIZE_t: return &key->ssize;
        case RDB_KPTR: return &key->pointer;
#ifdef USE_128_BIT_TYPES
        case RDB_KINT128: return &key->i128;
        case RDB_KUINT128: return &key->u128;
#endif
        default: return NULL;
    }
}

static int edit_key(void *data, void *user_data, rdb_reindex_ctx_t *ctx)
{
    record_t *record = data;
    edit_state_t *state = user_data;
    record_t candidate = *record;
    ++state->visits;
    set_key(&candidate.key, state->kind, &state->replacement);
    CHECK(rdb_reindex_check_apply(ctx, record, &candidate,
                RDB_INDEX_MASK(0)) == 0);
    return RDB_CB_OK;
}

static void check_scalar_reindex(const char *name, uint32_t kind,
        key_value_t original, key_value_t replacement)
{
    int offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *pool;
    record_t *record;
    edit_state_t state;

    pool = rdb_register_um_pool((char *)name, 1, offset,
            RDB_BTREE | (int)kind, NULL, 0);
    CHECK(pool != NULL);
    if (!pool) return;

    record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!record) {
        rdb_drop_pool(pool);
        return;
    }
    set_key(&record->key, kind, &original);
    state.kind = kind;
    state.replacement = replacement;
    state.visits = 0;

    CHECK(rdb_insert(pool, record) == 1);
    CHECK(rdb_iterate_reindex(pool, 0, edit_key, &state, NULL, NULL) == 0);
    CHECK(state.visits == 1);
    CHECK(rdb_get(pool, 0, key_address(&original, kind)) == NULL);
    CHECK(rdb_get(pool, 0, key_address(&replacement, kind)) == record);
    rdb_flush(pool, NULL, NULL);
}

int main(void)
{
    static int pointer_before, pointer_after;
    key_value_t original = {0}, replacement = {0};

    rdb_init();

    original.i8 = -7; replacement.i8 = 8;
    check_scalar_reindex("reindex-i8", RDB_KINT8, original, replacement);
    original.u8 = 7; replacement.u8 = 8;
    check_scalar_reindex("reindex-u8", RDB_KUINT8, original, replacement);
    original.i16 = -700; replacement.i16 = 800;
    check_scalar_reindex("reindex-i16", RDB_KINT16, original, replacement);
    original.u16 = 700; replacement.u16 = 800;
    check_scalar_reindex("reindex-u16", RDB_KUINT16, original, replacement);
    original.i32 = -70000; replacement.i32 = 80000;
    check_scalar_reindex("reindex-i32", RDB_KINT32, original, replacement);
    original.u32 = 70000; replacement.u32 = 80000;
    check_scalar_reindex("reindex-u32", RDB_KUINT32, original, replacement);
    original.i64 = -700000; replacement.i64 = 800000;
    check_scalar_reindex("reindex-i64", RDB_KINT64, original, replacement);
    original.u64 = 700000; replacement.u64 = 800000;
    check_scalar_reindex("reindex-u64", RDB_KUINT64, original, replacement);
    original.size = 70; replacement.size = 80;
    check_scalar_reindex("reindex-size", RDB_KSIZE_t, original, replacement);
    original.ssize = -70; replacement.ssize = 80;
    check_scalar_reindex("reindex-ssize", RDB_KSSIZE_t, original, replacement);
    original.pointer = &pointer_before;
    replacement.pointer = &pointer_after;
    check_scalar_reindex("reindex-pointer", RDB_KPTR, original, replacement);
#ifdef USE_128_BIT_TYPES
    original.i128 = -((__int128_t)1 << 80);
    replacement.i128 = ((__int128_t)1 << 80);
    check_scalar_reindex("reindex-i128", RDB_KINT128, original, replacement);
    original.u128 = ((__uint128_t)1 << 100) + 17;
    replacement.u128 = ((__uint128_t)1 << 100) + 18;
    check_scalar_reindex("reindex-u128", RDB_KUINT128, original, replacement);
#endif

    rdb_clean(0);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
