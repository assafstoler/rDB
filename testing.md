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

Install to a staging directory without changing system libraries:

```sh
DESTDIR="$PWD/build/stage" cmake --install build
```

The normal library directory is `/usr/lib`. Configure with `-DLEGACY_BUILD=ON`
to install under `/usr/lib64` instead. Both modes install the public headers
under `/usr/include/rdb`.

The `rdb_installed_consumers` test stages this install in the build directory
and builds independent C and C++ consumers against it. Their build preserves
the configured compiler and linker flags, including configuration-specific
sanitizer flags, while using only the installed public headers and library.
It does not install anything into the system directories.

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
