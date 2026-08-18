# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

`nlib` — header-only C++23 data structures and wire records for a trading
system, namespace `nlib`. No dependencies beyond the standard library;
GoogleTest is fetched by CMake for the tests only. See [README.md](README.md)
for the component list.

## Layout

```
include/nlib/*.h      the library — one container per header, no .cpp files
include/nlib/common.h the wire records: order, trade, book, metrics + shared enums
tests/*_test.cpp      one GoogleTest binary per header
tests/map_bench.cpp   benchmark, built but not registered with CTest
CMakeLists.txt        nlib INTERFACE target + nlib::nlib alias
```

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the full suite before committing; it takes well under a second. The
benchmark needs an optimized build: `./build/tests/map_bench`.

## Conventions

- **Header-only.** A container is one self-contained header under
  `include/nlib/`. No source files, no separate implementation.
- **Move-only entry.** Elements enter by move or in-place construction only;
  containers are not copyable. Do not add `insert(const T&)`-style overloads —
  they were deliberately removed so no insertion path copies silently.
- **State the contract in the class comment**: iterator/reference invalidation,
  address stability, exception behavior, and the thread contract
  (thread-compatible unless the type is explicitly concurrent, as
  `single_queue` is). These comments are the documentation; keep them accurate
  when behavior changes.
- **Comment style**: Google C++ Style Guide, dense and short — the
  `cpp-comments` skill in `~/.claude/skills` codifies the exact rules used here.
- Formatting: 2-space indent, 100-column lines, `snake_case` for container
  types and members mirroring the standard library.
- Prefer removing a component over keeping a weaker duplicate of a standard one
  — `vector` was deleted for exactly that reason (`85b89dd`).

## `common.h` is not a container

`include/nlib/common.h` holds the wire records (`order`, `trade`, `book`,
`metrics`) and the constants and enums they are built from. It follows
different rules from the containers:

- **Keep every record trivially copyable and standard layout.** The
  `static_assert`s at the bottom of the file enforce it. That rules out
  constructors, virtuals, private members, `std::string`, and owning pointers —
  a record must stay memcpy-able and mappable into shared memory.
- **A field change is a breaking change for every consumer.** These structs are
  the interface between the feed, the book, and storage; changing a field's
  meaning, order, or width breaks anything already reading the layout. Add
  fields at the end, and say so in the commit body.
- **Prices and quantities are fixed point**, in units of `1/price_scale` and
  `1/qty_scale`; times are Unix-epoch nanoseconds, split into `event_ns`
  (exchange) and `recv_ns` (stamped on receipt). Do not introduce a float on
  this path.
- Only genuinely shared vocabulary belongs here. A type used by exactly one
  consumer stays in that consumer's repository — `nqbook`'s `Node` wrapper was
  deleted rather than promoted, once `order` carried its own list hooks.

## Adding a container

1. Write `include/nlib/<name>.h` with a class comment covering the contract
   above.
2. Write `tests/<name>_test.cpp`.
3. Register it in `tests/CMakeLists.txt` (`add_executable` +
   `target_link_libraries` with `nlib::nlib GTest::gtest_main` +
   `gtest_discover_tests`).
4. Add a row to the component table in `README.md`.

## Commits

One logical change per commit, imperative subject, body explaining why. Run the
test suite first.
