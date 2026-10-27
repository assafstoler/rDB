# Build and test rDB

Build the user-space library and run its CTest suite with:

```sh
cmake -S . -B build -DSKIP_KERNEL=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The default single-configuration build is Debug with `-O2` and symbols. Release
also uses `-O2`. Regression targets are enabled by default; configure with
`-DBUILD_TESTING=OFF` to build the library and demo without them. Kernel module
builds require a matching kernel development tree. Leave `SKIP_KERNEL` unset to
include that build when the required headers are available.

The legacy output-comparison tests also require a zero exit status and empty
stderr, so expected output cannot hide a crash or sanitizer failure. Their
subprocess runner is checked with passing output, a mismatch, a nonzero exit,
stderr output and signal termination. Run these cases with
`ctest --test-dir build -L legacy --output-on-failure`; each has a 30-second
timeout. The fixtures release their records, strings and pools so leak
detection can remain enabled.

The fault-containment contract tests add nine cases for invalid insert
layouts, failed insert undo, failed move rollback, partial multi-index unlink,
unexpected two-child deletion, inconsistent queue links and faults raised
inside reindex callbacks, staged apply/unlink failures and metadata registration
on a faulted custom-key pool. Move failures cover destination undo, source
reinsertion and restoration of the source queue position. The fixtures
deliberately alter comparators or links to reach these defensive paths; normal
callers must keep keys/comparators stable and leave internal links alone.

After a fault, checks require the first diagnostic to remain unchanged,
record operations to refuse further work, and visit/cleanup callbacks to stay
unused. An unrelated pool must keep working; lookup, locks and metadata
teardown remain available on the damaged pool. Reindex callback faults must
prevent deletion regardless of the callback result, discard staged edits,
and preserve records with either default or custom cleanup. Fixtures retain
their own allocation lists and free them after dropping damaged pools, without
traversing their indexes. All checks remain enabled in Release. Each case has
a 30-second timeout; run them with
`ctest --test-dir build -L fault_contract --output-on-failure`.

The scalar boundary tests cover minimum/maximum values, rejection immediately
outside each narrower key type's range, and pointer values that would otherwise
alias after truncation. They exercise get/delete/move in both index positions
and custom comparators attached to either custom or built-in type flags. The
API edge tests check NULL/root queries, empty operations, registry removal,
and repeated single-index deletion misses without changing records or indexes.
Run these cases with:

```sh
ctest --test-dir build -L 'const_boundary|api_edge' --output-on-failure
```

Dump tests compare exact output for numeric boundaries, custom keys, empty
strings, NULL pointer keys, separators and multiple index positions. Threaded
dumps use six independent locked pools with tree/FIFO/LIFO/mixed layouts;
interleaving is allowed, but each pool's exact sequence must survive unchanged.
Record snapshots and lookups check that dumping does not modify records. Run
them with `ctest --test-dir build -L dump --output-on-failure`.

The staged-reindex contract tests cover invalid callback requests, sticky
validation errors, repeated bounded string edits, deep-copy isolation, queue
and opaque companion-index masks, partially copied composite strings, and
default/custom cleanup of aliased strings during staged deletion and abort.
Run them with `ctest --test-dir build -L reindex_contract --output-on-failure`.
The Linux allocation-failure test additionally fails each successive allocation
through inline-key metadata creation and complete direct/composite reindexing.
It requires unchanged records after failure and a successful retry on the same
pool. Sanitizers check that rejected/replaced proposals and owned strings are
released without leaks, double frees or use-after-free. These new cases use
always-enabled checks; each CTest case has a 30-second timeout.

The Linux move-layout tests add two independent regression cases. The matrix
covers 256 source/destination pairs with 1, 2, 3 and the maximum supported
number of indexes (currently 24), using tree, FIFO, LIFO and mixed layouts.
A fixed seed drives 128 attempts per pair through all three move APIs,
including same-pool moves, missing destinations and late key collisions.
An independent model checks membership, counts, exact queue links/order and
AVL ordering/balance after each attempt.

The threaded case performs 32,000 moves across eight threads, locking both
pools in a consistent order. It checks membership and counts on each move,
samples the complete model while holding both locks, and validates the final
state after joining all workers. Both cases use always-enabled checks and
have 30-second watchdogs; neither asserts an execution-time threshold. Run
them with `ctest --test-dir build -L move --output-on-failure`.

The move contract tests add 12 cases for all three move APIs with caller-held
pool locks. They exercise repeated rejection at each keyed destination index,
head/middle/tail selection, FIFO/LIFO head moves, same-pool moves, missing and
invalid destinations, and successful retry after removing a collision. Typed
collisions cover inline strings, separately allocated pointer strings, pointer
values and custom composite keys. String cases reject after both destination
queues have accepted the record and check that key storage survives rollback.

Saved traversal order and counts check both pools after rejection; queue checks
also validate heads, tails and reciprocal links. The fixtures deliberately
inject a late comparator rejection or invalid destination metadata to exercise
defensive paths; real callers must keep keys and comparators stable. A separate
comparison-budget case requires successful moves to use the same number of key
comparisons as a direct delete followed by insert, with no timing threshold.
All checks remain enabled in Release, and each case has a 30-second timeout.
Run these cases with `ctest --test-dir build -L move_contract --output-on-failure`.

The mixed-operation model tests extend the existing numeric model with seven
tree/FIFO/LIFO layouts, numeric and string keys, all applicable move APIs,
single-index reindexing, rejected operations, selective/aborted iteration,
and flush/reuse across two pools. Defaults are four fixed seeds, a 32-record
capacity and 1,200 steps per seed. The inline-string and pointer-string cases
each repeat all seven layouts; their all-tree layout mixes numeric, inline
and pointer-string keys.

After each step, independent copied keys and queues determine the expected
membership, counts, traversal order, link directions and AVL balance. The
tests also check allocation/cleanup counts and that iteration/flush clear
indexed pointer strings before invoking cleanup. Each default case runs in
a separate process with a 30-second timeout and checks enabled in Release.
Run them with `ctest --test-dir build -L model --output-on-failure`.

Failures print the seed, layout, step, last 16 operations and a replay command.
For longer runs or a specific replay:

```sh
# All numeric/string layouts: eight seeds, 128-record capacity, 5,000 steps.
cmake --build build --target model_stress
build/test/rdb_mixed_model_tests pointer_strings --seed 0x1dba5eed --items 128 --steps 5000
```

Cases accept `--seed` (1..4294967295), `--seeds` (1..4096), `--items` (1..512)
and `--steps` (1..1000000), using decimal or hexadecimal values. An explicit
seed selects one run unless `--seeds` is also supplied. Invalid options return
usage status 2. The stress target is an opt-in correctness check, outside the
default CTest run; it does not impose timing thresholds.

The companion-index iteration tests explicitly unlink/edit/reinsert other
indexes inside ordinary `rdb_iterate` callbacks while keeping the active
index and key fixed. They traverse numeric, pointer-string, FIFO and LIFO
indexes across every index position, five insertion orders, all 128 deletion
masks, three reindex masks, keys moving earlier/later, and default/custom
cleanup. Empty and singleton pools are included. Separate cases exercise
both abort results at every visit position and duplicate-key rejection
followed by restoring and reinserting the original key.

Independent copied keys and queue IDs check exact visits, lookups, AVL balance,
queue links/endpoints, unchanged active-index links, and counts. Cleanup must
run exactly once after all indexes exclude the record and rDB has cleared
its indexed string. The six cases have 30-second timeouts and run with
`ctest --test-dir build -L iterate_companion --output-on-failure`.

The Linux concurrency contract tests add eight cases alongside the existing
thread tests. Six workers exercise registry creation/removal, independent
lookup readers with a permanent pool or an empty registry, and simultaneous
registration of one name (exactly one winner). A shared-pool case uses barriers
to check all records in sorted order between locked insertion and deletion
phases. Lookup readers borrow only a pool kept alive until they join; temporary
pool handles remain owned by their writers.

Statistics readers run alongside locked insert/delete/move/iterate/flush
operations and registry churn. Reports must contain valid names and bounded
counts; tiny buffers must remain terminated without overwriting their guards.
Writers register/drop pools while holding a separate pool lock, and another
case requests statistics while already holding the reported pool's lock, to
catch lock-order regressions. Checks remain enabled in Release, with separate
30-second CTest timeouts and no timing thresholds. Run these eight cases with
`ctest --test-dir build -L concurrency --output-on-failure`.

Install to a staging directory without changing system libraries:

```sh
DESTDIR="$PWD/build/stage" cmake --install build
```

The normal library directory is `/usr/lib`. Configure with `-DLEGACY_BUILD=ON`
to install under `/usr/lib64` instead. Both modes install the public headers
under `/usr/include/rdb`.

The `rdb_installed_consumers` test stages this install in the build directory
and builds eight independent consumers: C/C++, normal/no-counter libraries,
and `RDB_POOL_COUNTERS` defined/undefined in the consumer. The alternate library
is test-only and is copied into the isolated stage; it is never installed on
the system or linked alongside the normal library. Statistics verify which
variant was loaded. A deliberately incomplete unlink checks that fault fields
read through the installed header match those returned by the library.

Consumer builds preserve the configured compiler and linker flags, including
configuration-specific sanitizer flags, while using only staged public headers
and libraries. All consumers explicitly undefine `USE_128_BIT_TYPES`, so the
installed configuration header must provide the correct scalar ABI.

Separate C/C++ header tests expand the public logging macros with `DEBUG` on
and off, include the header twice without prerequisite includes, and exercise
pool locks. Scalar-consumer tests check the configured argument width,
rejection of values that would narrow onto another record, and full-width
positive/negative get/move/delete calls (128 bits when enabled, otherwise 64).
Run all consumer checks with `ctest --test-dir build -L abi --output-on-failure`.
Checks remain enabled in Release; ordinary cases have 30-second timeouts and
the installed build/test matrix has a 120-second timeout.

## ThreadSanitizer

Use a separate Linux/GCC build to instrument the library and both C/C++ test
consumers. ThreadSanitizer must not be combined with AddressSanitizer or
LeakSanitizer in the same build:

```sh
cmake -S . -B build/tsan -DSKIP_KERNEL=ON -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build/tsan -j"$(nproc)"
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/tsan \
  --output-on-failure -j2 -E '^coverage_runner_tests$'
```

The excluded test exercises the Python coverage-report tooling; run it in the
normal build. All native tests, including the installed-consumer build/test
matrix, remain enabled. Sanitizer failures must produce a failing test; do not
suppress reports to obtain a passing run.

On Linux x86-64, GCC's TSan runtime can fail before `main()` with
`FATAL: ThreadSanitizer: unexpected memory mapping` when randomized executable
or library addresses conflict with its reserved memory layout. For this startup
failure, launch the test process and its children with a fixed address layout:

```sh
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/tsan \
  --output-on-failure -j2 -E '^coverage_runner_tests$'
```

This changes the address-layout setting only for that process tree; it does not
change system-wide ASLR settings or disable race detection. The process must be
allowed to use `personality(ADDR_NO_RANDOMIZE)`, which some sandboxes restrict.
This workaround was verified with GCC 13 and libtsan2 14 on x86-64 Linux, using
both a clean mutex-protected probe and a deliberate race before running the
native suite. Other runtimes or architectures may need a different remedy; see
[LLVM's sanitizer address-layout handling](https://github.com/llvm/llvm-project/pull/97125).

## Coverage

Coverage is optional and does not instrument or rebuild the normal binaries.
The `coverage` target creates a separate `build/coverage/` build, runs the
registered tests, and publishes its report under
`build/coverage/coverage-report/`:

```sh
python3 -m venv build/coverage-tools
build/coverage-tools/bin/pip install -r tests/coverage-requirements.txt
cmake --build build --target coverage
```

Coverage reporting requires a native GCC 8+ toolchain, CMake 3.21+ (CTest JUnit
output), Python 3.8+, and gcovr 8.6. The helper finds the reporting tools in
`build/coverage-tools` when installed by the commands above. To tune build and
test/report parallelism, set `TEAL_COVERAGE_JOBS` when configuring the normal
build. The generated report includes HTML, Cobertura XML, CTest JUnit results,
coverage summaries, and run metadata. Kernel and demo code are outside the
library coverage scope.

A coverage build can also be configured directly:

```sh
cmake -S . -B build/coverage -DSKIP_KERNEL=ON -DBUILD_TESTING=ON \
  -DTEAL_ENABLE_COVERAGE=ON \
  -DTEAL_COVERAGE_GCOVR="$PWD/build/coverage-tools/bin/gcovr"
cmake --build build/coverage --target coverage -j"$(nproc)"
```

Run the report tooling's unit tests independently with
`python3 -B tests/test_coverage_runner.py`. Compiler-backed report tests run
when gcovr is available.
