# rDB — RAM database

rDB is a C library for indexing application-owned, in-memory records. A pool
can expose several unique-key AVL indexes and FIFO/LIFO indexes over the same
records. Records embed one `rdb_bpp_t` pointer pack for each index. The
user-space API is usable from C and C++ on Linux.

The public API is in [src/rdb.h](src/rdb.h). This repository implements
unmanaged pools: the application allocates records and chooses when to unlink
or release them. Managed allocation pools, skip lists and `RDB_LIST` indexes
are not supported by this API.

## Build and test

```sh
cmake -S . -B build -DSKIP_KERNEL=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

See [testing.md](testing.md) for build variants, installation, sanitizers,
consumer checks and coverage. Kernel builds require a matching kernel build
tree; the native user-space tests do not validate loading a kernel module.

Install or stage `rdb.h`, the generated `rdb_build_config.h`, and the shared
library from the same build. The configuration header selects the library's
64- or 128-bit scalar argument width; applications should not independently
define `USE_128_BIT_TYPES`. Rebuild consumers after a public layout or argument
width change. `RDB_POOL_COUNTERS` controls library counting/reporting and does
not change the public pool layout or require a matching application define.

## Quick start

This single-threaded example uses an integer ID and an inline-string name as
two unique indexes. It retains its one allocation separately, drops the pool
metadata, then frees the record. Save it as `example.c`:

```c
#include <rdb/rdb.h>
#include <string.h>

typedef struct {
    rdb_bpp_t links[2];
    int32_t id;
    char name[64];
} user_record_t;

static int print_user(void *data, void *unused)
{
    const user_record_t *record = (const user_record_t *)data;
    (void)unused;
    printf("%d %s\n", (int)record->id, record->name);
    return RDB_CB_OK;
}

int main(void)
{
    int result = EXIT_FAILURE;
    char pool_name[] = "users";
    int32_t id = 7;
    rdb_pool_t *pool = NULL;
    user_record_t *record = NULL;

    rdb_init();
    pool = rdb_register_um_pool(pool_name, 2,
        (int)(offsetof(user_record_t, id) - 2 * sizeof(rdb_bpp_t)),
        RDB_BTREE | RDB_KINT32, NULL, 0);
    if (!pool) goto done;
    if (rdb_register_um_idx(pool, 1,
            (int)(offsetof(user_record_t, name) - 2 * sizeof(rdb_bpp_t)),
            RDB_BTREE | RDB_KSTR, NULL, sizeof(record->name)) != 1)
        goto done;

    record = (user_record_t *)calloc(1, sizeof(*record));
    if (!record) goto done;
    record->id = id;
    strcpy(record->name, "alpha");
    if (rdb_insert(pool, record) != 2) goto done;
    if (rdb_get(pool, 0, &id) != record) goto done;
    if (rdb_get(pool, 1, "alpha") != record) goto done;
    rdb_iterate(pool, 0, print_user, NULL, NULL, NULL);
    result = EXIT_SUCCESS;

done:
    rdb_drop_pool(pool);  /* Metadata only; record remains application-owned. */
    free(record);
    rdb_clean(0);
    return result;
}
```

For a default build staged without changing system libraries:

```sh
DESTDIR="$PWD/build/stage" cmake --install build
cc -std=c11 -Wall -Wextra example.c -I"$PWD/build/stage/usr/include" \
  -L"$PWD/build/stage/usr/lib" -Wl,-rpath,"$PWD/build/stage/usr/lib" \
  -lrdb -pthread -o example
./example
```

The output is `7 alpha`. A `LEGACY_BUILD` installation uses `usr/lib64`;
adjust the library paths accordingly. More examples are in
[demo/rDBDemo.c](demo/rDBDemo.c) and [test/](test/).

## Registering records and indexes

Create a pool with `rdb_register_um_pool()`. Its index count is fixed at
creation and must be from 1 through `RDB_POOL_MAX_IDX` (currently 24). Index 0
is configured by that call; `rdb_register_um_idx()` configures reserved slots
1 through `indexCount - 1`. Configure every slot before inserting records or
making the pool available to record users. A slot cannot be redefined, and
secondary registration cannot grow the layout.

Place the pointer packs at the start of each record. A key's registration
offset is `offsetof(record_type, field) - indexCount * sizeof(rdb_bpp_t)`;
this accounts for normal compiler padding. Keys use the natural alignment of
their types and need not be stored in `rdb_key_union`. The same pointer packs
must not be linked into two pools at once.

Every index requires `RDB_BTREE` and exactly one supported built-in key type,
`RDB_KCF`, `RDB_KCFUT`, `RDB_KFIFO` or `RDB_KLIFO`. Ascending order is the
default; `RDB_KASC` is optional. Queues may also specify `RDB_NO_IDX`.
`RDB_LIST`, `RDB_KDEC`, conflicting types, unknown bits, and unavailable key
types are rejected. Both custom-key flags require a comparator; a comparator
can also override a built-in keyed type, but cannot be attached to a queue.
Comparators receive `(stored_key, candidate_key)` and return a negative, zero
or positive result when the candidate sorts before, equal to or after the
stored key.

The final registration argument is the inline field capacity for `RDB_KSTR`,
and zero for other types. Creation returns NULL on failure. Secondary
registration returns the index on success, -1 for index 0, -2 for an invalid
slot, -3 for an already configured slot, or -4 for invalid flags/key size.
Metadata allocation failure returns `RDB_ERR_REINDEX_NOMEM`; a faulted pool
returns `RDB_ERR_FAULTED`. User-space mutex initialization failure releases
partially initialized resources without publishing a pool or reserving its
name. `rdb_add_pool()` is a low-level helper without registry locking or
duplicate-name checks; use `rdb_register_um_pool()` for normal creation.

`rdb_insert()` returns the number of indexes on success, 0 after ordinary
rejection and complete undo, -1 for invalid arguments/index count, or
`RDB_ERR_FAULTED` after a detected consistency failure. A duplicate key in any
keyed index rejects the whole insertion. Keep registered keys and comparators
stable during operations.

## Ownership and cleanup

| Operation | Record and resource lifetime |
| --- | --- |
| `rdb_delete()` / `rdb_delete_const()` | Unlink and return the record; the caller handles all cleanup. |
| Successful move | Transfer the same record between pools without copying or freeing it. |
| Deletion during either iterator; `rdb_flush()` | Free registered pointer-string keys first, then invoke the cleanup callback; without a callback, also free the record. |
| `rdb_drop_pool()` / global cleanup | Release pool metadata, not unmanaged records. |

In this public API, registered `RDB_KPSTR` strings are owned by rDB for
iteration/flush cleanup. It frees each distinct non-NULL allocation within a
record once and clears every registered alias before invoking the cleanup
callback. This also covers `RDB_KPSTR` components declared in a custom
`RDB_KCF` key layout. The callback handles the record and any other resources;
it cannot prevent this string cleanup. Do not use borrowed, interior, static
or cross-record shared pointers for these owned string keys. Other pointer
fields remain subject to application ownership; `RDB_KPTR` does not own the
pointed-to object.

`rdb_flush()` empties a healthy pool, clears all index endpoints and resets
the record count, while keeping the pool registered for reuse. Its cleanup
callbacks must not traverse or change the pool being flushed. Use a callback
when records themselves must be retained or released by a different allocator.
Ordinary deletion leaves both records and strings untouched after unlinking.
Handle a fault separately: a failed operation does not establish that a
record has been fully unlinked or may safely be freed.

## Locking and pool lifetime

Record operations do not acquire pool locks automatically. Serialize shared
record access, including lookups, iteration, key edits, cleanup and fault
inspection, with `rdb_lock()`/`rdb_unlock()` or equivalent application locking.
The user-space pool lock is nonrecursive. Hold it across an entire iteration
and its callbacks; do not reacquire it inside a callback. Keep returned records
alive for as long as their pointers are used.

`rdb_find_pool_by_name()` is synchronized with normal registration and pool
drop, so unrelated pools may be created or removed concurrently. Its result is
borrowed, not reference-counted. Prevent the requested pool from being dropped
during lookup and use; locking its records does not retain its lifetime.
Before dropping a pool, stop users and waiters and release both of its mutexes.

Call `rdb_init()` or `rdb_init_ex()` before creating pools. Coordinate
initialization, global cleanup and `rdb_print_pools()` with all registry users.
`rdb_clean(0)` drops every pool and releases the global error message.
`rdb_gc()` / `rdb_clean(1)` drop pools marked with `pool->drop`; they do not
free records or check that those pools are empty. Arrange record cleanup and
pool lifetime before calling them.

## Lookups and scalar queries

For numeric keys, pass the address of a value of the registered type. For
`RDB_KPTR`, pass the address of the pointer value: `void *key = buffer;`
followed by `rdb_get(pool, index, &key)`. Pointer keys compare numeric
`uintptr_t` addresses without dereferencing the objects. A stored NULL pointer
is queried using `key = NULL` and still passing `&key`.

For `RDB_KSTR` and `RDB_KPSTR`, pass the query string itself, such as `"alpha"`.
**A NULL query has different meanings:** `rdb_get()` selects a tree root or
queue head; deletion and movement select a FIFO/LIFO head but do not delete or
move a tree root. Queue lookups ignore the supplied key.

`rdb_get_neigh()` operates on keyed indexes using the same query format. It
returns an exact match with both neighbor outputs NULL; on a miss it returns
NULL and supplies the nearest lower/higher records when present. A NULL-key
root query also clears both outputs. Always supply valid writable `before`
and `after` pointer arguments.

The `_const` get/delete/move APIs accept an integer value instead of its
address. With built-in numeric/pointer comparators, values outside the key's
range return NULL without narrowing onto another record. Full-width unsigned
keys use the signed argument's bit pattern. Custom comparators receive the
address of the full scalar argument; queues ignore it. Use string queries
through the address-based APIs.

Indexed record APIs require `0 <= index < pool->indexCount`; moves check the
source index. Invalid indexes leave records and pool metadata unchanged:
pointer APIs return NULL, `rdb_insert_one()` / `rdb_delete_one()` / `rdb_move2()`
return -1, and ordinary iterate/dump report a diagnostic without traversal.
These guards do not make stale pointers or unconfigured slots safe to use.

## Moving records

`rdb_move(dst, src, index, key)` and `rdb_move2(dst, src, index, key)`
perform the **same operation through the same implementation**: look up one
record in the source's selected index, unlink it from all source indexes, and
insert it into all destination indexes. Neither copies or frees the record.
Both take a **lookup key**, using the conventions above, rather than the record
pointer itself.

**The only difference is what they return:**

| Outcome | `rdb_move()` returns | `rdb_move2()` returns |
| --- | --- | --- |
| Move succeeded | Pointer to the moved record | `0` |
| Invalid source pool/index, or no matching source record | `NULL` | `-1` |
| Destination rejected the record and source restoration succeeded | `NULL` | `-2` |
| Either pool was already faulted, or unlink/rollback detected a consistency failure | `NULL` | `RDB_ERR_FAULTED` (`-5`) |

Use `rdb_move()` when you need the moved record pointer and a success/failure
result is enough. Use `rdb_move2()` when you need to distinguish a missing
source record, a recoverable destination rejection, and a consistency fault.
Its success result is zero, not a record pointer or index count. For example,
a duplicate destination key produces `NULL` from `rdb_move()` or `-2` from
`rdb_move2()`, with the record restored to the source in either case.

`rdb_move_const(dst, src, index, value)` uses the same move operation and the
same pointer/NULL return contract as `rdb_move()`. Its distinction is the
input: it accepts a scalar key value directly, as described under scalar
queries above.

Both layouts must fit the record. Hold both pool locks for the entire move,
using one consistent application-defined order. Lock only once for a same-pool
move. After ordinary rejection, both pools retain their membership, counts and
exact FIFO/LIFO order; trees may rebalance. A NULL or incompletely configured
destination is rejected too. Remove the conflict before retrying. A successful
same-pool move still unlinks/reinserts and can reorder its queues.

## Detected consistency failures

A detected unlink or rollback failure stores the first `rdb_fault_info_t` in
the affected pool. Record operations then refuse further work. Integer record
APIs with valid arguments return `RDB_ERR_FAULTED`; pointer APIs return NULL.
Iteration stops without cleaning up a partially unlinked record, and flush
and dump refuse faulted pools. Unrelated pools remain usable. These checks
detect specific inconsistencies, not arbitrary memory corruption.

`rdb_pool_get_fault(pool, &fault)` returns 1 for a faulted pool, 0 for a healthy
pool, or -1 for NULL. The output is optional. The saved record address, index,
operation and reason require no allocation and survive later error messages.
Synchronize reads with pool operations. Name lookup, lock/unlock, diagnostics
and metadata drop remain available.

A faulted move does not promise source restoration. Inspect **both** pools:
failed destination undo can leave the record linked there and fault both.
Do not free or reinsert a record just because its address appears in a
diagnostic. Stop users, retain application allocation/ownership information,
drop affected pool metadata and rebuild the pools. Do not traverse damaged
indexes to recover allocations. Counts may not reflect a partial operation,
and there is no reset operation that makes a damaged pool usable again.

## Iteration and key changes

`rdb_iterate()` visits the selected index in order. Its callback returns:

| Result | Action |
| --- | --- |
| `RDB_CB_OK` | Keep the record and continue. |
| `RDB_CB_DELETE_NODE` | Unlink from all indexes, clean up, then continue. |
| `RDB_CB_ABORT` | Stop without deleting the current record. |
| `RDB_CB_DELETE_NODE_AND_ABORT` | Delete the current record, then stop. |

A NULL visit callback requests deletion of all records. Deletions follow the
ownership rules above. Within a callback, a current record's **other** indexes
may be updated by `rdb_delete_one()`, changing the key, then `rdb_insert_one()`.
Check reinsertion and restore the old key/membership after rejection. These
single-index operations do not adjust the record count. Keep the active
index's keys and links unchanged, including fields shared with other indexes.
`rdb_insert_one()` returns a nonnegative value on success. The historical
0/1 results from `rdb_delete_one()` reflect AVL height changes and do not
reliably distinguish a missing record from a successful removal.
`RDB_CB_REINDEX_ALL` and nonnegative callback results do not request reindexing.

`rdb_iterate_reindex()` supports staged key changes, including to the traversal
key. It requires a keyed traversal index and a callback. Make a shallow copy
of the record, edit only that copy, and submit every changed supported index
with `rdb_reindex_check_apply()`. Accepted proposals retain key copies; the live
keys change after callbacks finish. Key swaps are unsupported, but an earlier
accepted proposal may vacate a key for a later record.

`rename_selected()` below is an application callback, not an rDB library
function. It demonstrates changing **both the ID and the name of one record
while iterating by ID**, including changing the key of the index being walked.
Editing those live keys directly would leave the indexes ordered by their old
values. Staging the edits lets rDB update the keys and index links together
after traversal.

Assume a pool of `rename_record_t` records, with numeric ID index 0 and an
owned `RDB_KPSTR` name index 1. Unlike the quick-start example, the name here
is a separately allocated string. The intended change is from
`{ id = 7, name = "alpha" }` to `{ id = 42, name = "new name" }`, leaving other
records unchanged. The new ID and name must not conflict with another record:

```c
#include <rdb/rdb.h>
#include <string.h>

typedef struct {
    rdb_bpp_t links[2];
    int32_t id;
    char *name;
} rename_record_t;

int rename_selected(void *data, void *wanted_id, rdb_reindex_ctx_t *ctx)
{
    rename_record_t *record = (rename_record_t *)data;
    if (record->id != *(const int32_t *)wanted_id) return RDB_CB_OK;
    rename_record_t candidate = *record;
    char *new_name;
    candidate.id = 42;
    if (rdb_reindex_kpstr_edit(ctx, record, &candidate, 1, 0,
                64, &new_name) < 0)
        return RDB_CB_ABORT;
    strcpy(new_name, "new name");
    if (rdb_reindex_check_apply(ctx, record, &candidate,
                RDB_INDEX_MASK(0) | RDB_INDEX_MASK(1)) < 0)
        return RDB_CB_ABORT;
    return RDB_CB_OK;
}
```

For that already registered and populated pool, invoke it as follows, either
in a single-threaded application or while holding the pool lock:

```c
int32_t wanted_id = 7;
int rc = rdb_iterate_reindex(pool, 0, rename_selected, &wanted_id, NULL, NULL);
```

The `0` selects ID order for traversal. rDB calls `rename_selected()` for each
record, passing `&wanted_id` through its second argument and supplying the
staging context as its third argument. The callback works in four steps:

1. Skip records whose ID is not 7, returning `RDB_CB_OK` without proposing edits.
2. Make a shallow copy and set its ID to 42. The live record still has ID 7.
3. Obtain a private string edit buffer with `rdb_reindex_kpstr_edit()`. A shallow
   copy initially shares the original name pointer, so writing through that
   pointer would change the live indexed string. The helper instead points
   `candidate.name` and `new_name` at a temporary copy. Its arguments select
   index 1, component offset 0, and a 64-byte capacity including the terminator;
   the capacity must fit both the existing string and the replacement.
4. Write `"new name"` into that buffer and submit both changed index bits to
   `rdb_reindex_check_apply()`. This checks uniqueness and saves the proposed
   keys; despite its name, it does not change the live record during the callback.

After traversal, the iterator unlinks the affected index entries, installs the
accepted keys on the **same record**, and reinserts it in both indexes. If ID 7
existed and `rc == 0`, that record is now found by ID 42 or name `"new name"`,
and no longer by ID 7 or name `"alpha"`. If ID 7 was absent, this callback makes
no changes and the iterator can still return 0. A conflicting new ID or name
rejects this proposal and causes a negative iterator result; check `rc`.

The temporary edit buffer belongs to rDB and expires when the callback returns;
an accepted proposal retains its own copy. For staged iteration over a pool
containing `RDB_KCF` indexes, first describe each comparator's key fields with
`rdb_register_kcf_key_parts()`, before inserting records. Inline-string parts
include their capacity. `RDB_KCFUT` indexes are opaque and cannot be reindexed;
by default their presence rejects staged iteration. Initialize with
`rdb_init_ex(RDB_INIT_ALLOW_KCFUT)` to allow traversal with those indexes unchanged.

The staged iterator returns 0 for completion, `RDB_ITERATE_ABORTED` for an
explicit callback stop, or a negative error. Validation errors and ordinary
aborts still apply earlier accepted proposals; deletions already completed
also remain effective. This is not an all-or-nothing transaction. If a callback
faults the pool, iteration stops before honoring its deletion request and
discards pending edits. A failure while applying edits can leave partial
changes and faults the pool; use the fault-recovery procedure above.

## Reporting and diagnostics

`rdb_print_pool_stats()` holds the registry lock and samples counters without
taking pool locks, so it can run alongside normal locked updates and pool
registration/drop, or while the caller holds a pool lock. Counts are sampled
individually, not as a simultaneous snapshot. Direct reads of `record_count`
still need record synchronization. Counting-disabled libraries leave the
field zero and report that counting is disabled.

The statistics buffer size includes its terminating NUL. Truncated reports end
in `...` (fewer dots for tiny buffers). NULL buffers or nonpositive sizes cause
no writes. `rdb_dump()` prints a selected index with the supplied separator
after each item and no implicit newline. Synchronize against record writers;
concurrent dumps can interleave on stdout.

`rdb_error()` replaces the global diagnostic with a copy; `rdb_error_value()`
also returns its supplied status unchanged. On allocation failure, the message
is printed and the old saved diagnostic remains. Inputs may alias the saved
message, including a suffix. Error setters serialize their writes, but readers
must coordinate against replacement and global cleanup; do not retain the old
pointer after replacement. Per-pool fault diagnostics are independent of this
replaceable message.

## License

See [LICENSE](LICENSE) and the source-file notices.
