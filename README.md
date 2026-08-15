<img src="https://capsule-render.vercel.app/api?type=waving&height=400&text=N-Lib&fontAlign=80&fontAlignY=40&color=gradient" />

<p align="center">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Header-only" src="https://img.shields.io/badge/header--only-INTERFACE-4c1" />
  <img alt="Tests: GoogleTest" src="https://img.shields.io/badge/tests-GoogleTest-blue?logo=google&logoColor=white" />
  <img alt="License: Apache 2.0" src="https://img.shields.io/badge/license-Apache%202.0-D22128" />
</p>

Project of NowQuant.

A set of data structures for trading system. Header-only, C++23, namespace `nq`,
no dependencies beyond the standard library.

Authors:
- Zhouzhou (https://zhouzhouzhang.co.uk)

## Components

| Header | Type | What it is |
|---|---|---|
| [`nlib/hive.h`](include/nlib/hive.h) | `nq::hive<T>` | Minimal C++26 `std::hive` (P0447). Unordered element pool with O(1) insert and erase and **stable element addresses**; erased slots are tracked as jump-counting skipfield runs, so iteration skips a whole run in O(1). |
| [`nlib/map.h`](include/nlib/map.h) | `nq::map<Key, T, Hash, KeyEqual>` | Flat open-addressing hash map: one allocation holding the slot array plus a control byte per slot (empty, tombstone, or a 7-bit hash fragment that pre-filters comparisons). Power-of-two capacity, linear probing, rehash at 3/4 load. |
| [`nlib/single_queue.h`](include/nlib/single_queue.h) | `nq::single_queue<T>` | Bounded lock-free SPSC ring buffer. Capacity rounds up to a power of two at construction; each side owns a cache line holding its counter plus a cached copy of the other side's, so the hot path touches no shared line. |
| [`nlib/pool.h`](include/nlib/pool.h) | `nq::pool<T>` | Growable object pool. `emplace()` returns an index handle, `release()` recycles the slot. Handles stay valid until released; growth may move elements, so it invalidates references, never handles. |
| [`nlib/memory_pool.h`](include/nlib/memory_pool.h) | `nq::memory_pool` | Fixed-capacity fixed-size-block allocator over one contiguous aligned buffer. The LIFO free list is threaded through the freed blocks themselves, so there is no per-block metadata. |

Shared conventions across the containers:

- **Move-only entry.** Elements enter by move or in-place construction; the
  containers themselves are not copyable. Nothing copies an element silently.
- **Thread-compatible** unless stated otherwise: concurrent `const` access is
  safe, writes need external synchronization. `single_queue` is the exception —
  it is the one type with a concurrent contract (one producer, one consumer).
- Iteration order is unspecified for `hive` and `map`.

## Using it

Header-only, exported as the `nlib::nlib` INTERFACE target:

```cmake
add_subdirectory(nlib)
target_link_libraries(my_app PRIVATE nlib::nlib)
```

```cpp
#include <nlib/map.h>

nq::map<int, int> m;
m.try_emplace(42, 7);
if (auto it = m.find(42); it != m.end()) use(it->second);
```

## Building the tests

Tests are built when nlib is the top-level project (`NLIB_BUILD_TESTS`, default
`ON` there, `OFF` when consumed via `add_subdirectory`). GoogleTest v1.17.0 is
fetched by CMake, so the first configure needs network access.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Benchmark

`tests/map_bench.cpp` is a benchmark, not a test — it is built but not
registered with CTest, and it is only meaningful from an optimized build:

```bash
./build/tests/map_bench   # prints CSV: container,workload,ns/op
```

1,000,000 shuffled keys, best of three runs, setup and teardown untimed:

<img src="tests/map_bench.svg" alt="nq::map vs std::unordered_map, ns/op" width="640" />

`nq::map` wins on insert and erase, where the flat layout avoids a node
allocation per element; lookups are comparable, since a hit costs a probe and a
key comparison either way.

## Contribution
Fork and make a PR if you would like to give some improvements.

## License
Apache 2.0
