# OffsetAllocator migration — build wiring and conformance verification (APX-206)

Goal: finish the migration of sebbbi's OffsetAllocator
(https://github.com/sebbbi/OffsetAllocator, MIT, (c) 2023 Sebastian Aaltonen)
into the skore v2 core as a pure C module, so that it is actually built by
the default build and its tests run as part of the standard test target.

The port itself (implemented in `core/offset_allocator.c` /
`core/offset_allocator.h`) was landed first; this document records the
build/test wiring verification and the side-by-side conformance review of
the ported C code against the upstream C++ implementation.

---

## 1. Build registration (how the module is wired in)

The core module collects sources with the repo's documented convention
(AGENTS.md: "Collect sources with `file(GLOB_RECURSE ... *.h *.c)` per
target"):

```cmake
# core/CMakeLists.txt
file(GLOB_RECURSE SKORE_CORE_SOURCES CONFIGURE_DEPENDS *.h *.c)
```

`core/offset_allocator.c` and `core/offset_allocator.h` therefore compile
into **sk-core** (production, no test code) and **sk-core-tests**
(recompiled with `SK_TESTS`, which enables the in-source `SK_TEST` bodies).
`sk-tests` (tests/CMakeLists.txt) links `sk-core-tests` with
`--whole-archive`/`/WHOLEARCHIVE` and is registered with CTest
(`add_test(NAME sk-tests ...)`), so the OffsetAllocator tests run as part of
the standard `ctest` invocation. No CMakeLists edit was needed — the GLOB
registration is the convention and is effective (verified below).

## 2. Clean build and full test run (recorded output)

Environment: Linux x86_64, CMake 3.30 + Ninja, GCC, Debug,
`-DBUILD_TESTING=ON -DSK_ENABLE_CLANG_TIDY=ON` (matches CI).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DSK_ENABLE_CLANG_TIDY=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Build result: clean — the only warning in the whole tree is a pre-existing
`-Wformat-truncation` in vendored `thirdparty/nativefiledialog/src/nfd_zenity.c`
(thirdparty is exempt from the project's `-Werror` set and that file is
untouched by this migration). `offset_allocator.c` compiles under the full
first-party flag set (`-Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow ... -Wstrict-prototypes -Wmissing-prototypes`)
in both `sk-core` and `sk-core-tests`, and `clang-tidy
-warnings-as-errors=*` passes on it (including the `SK_TESTS` build).

```text
$ ctest --test-dir build --output-on-failure
    Start 1: sk-tests
1/2 Test #1: sk-tests .........................   Passed    9.50 sec
    Start 2: sk-integration-tests
2/2 Test #2: sk-integration-tests .............   Passed    0.23 sec

100% tests passed, 0 tests failed out of 2

Total Test time (real) =  9.73 sec

$ build/bin/sk-tests
======== TOTAL: ran=522 failed=0 ========
```

All 18 OffsetAllocator SK_TESTs (indices 56–73 of the host registry) pass:

```text
offset_allocator_small_float_numbers:PASS
offset_allocator_basic_alloc_free:PASS
offset_allocator_allocate_simple:PASS
offset_allocator_merge_trivial:PASS
offset_allocator_reuse_trivial:PASS
offset_allocator_reuse_complex:PASS
offset_allocator_zero_fragmentation:PASS
offset_allocator_free_front_first:PASS
offset_allocator_free_middle_first:PASS
offset_allocator_free_back_first:PASS
offset_allocator_allocation_size:PASS
offset_allocator_storage_report_full:PASS
offset_allocator_no_space_arena_exhausted:PASS
offset_allocator_no_space_node_pool_exhausted:PASS
offset_allocator_reset_pristine:PASS
offset_allocator_boundary_sizes:PASS
offset_allocator_large_size_near_u32:PASS
offset_allocator_destroy_live_allocations_no_leak:PASS
```

`sk-integration-tests` also passes (0 failures). `scripts/format.sh --check`
reports all 81 project files already formatted.

## 3. Conformance review (ported C vs upstream C++)

Reviewed line-by-line against upstream `offsetAllocator.hpp` /
`offsetAllocator.cpp` (sebbbi/OffsetAllocator @ 3610a73). The port mirrors
the algorithm, constants, and control flow; only language plumbing
(types, allocator hook, naming) differs. Findings per review area:

### 3.1 Round-up vs round-down float conversion on the correct paths

- `oa_uint_to_float_round_up` == `SmallFloat::uintToFloatRoundUp` (denorm
  `size < 8` → `mantissa = size`; normalized path with `low_bits_mask`
  round-up; `(exp << 3) + mantissa` — `+` so mantissa overflow carries into
  the exponent). Used on **allocate** to derive the minimum fitting bin.
- `oa_uint_to_float_round_down` == `SmallFloat::uintToFloatRoundDown`
  (same, but no round-up; `(exp << 3) | mantissa`). Used on **insert** and
  **remove** (`insertNodeIntoBin` / `removeNodeFromBin`).
- `oa_float_to_uint` == `floatToUint` (denorm → mantissa; else
  `(mantissa | 8) << (exp - 1)`). Used by both storage reports.
- One deliberate, behavior-preserving change: the port writes
  `1u << 31`-style unsigned shifts where upstream relies on `1 << 31`
  (defined behavior in C for `unsigned`; in upstream C++ this particular
  shift only ever receives `start_bit_index <= 31` and is unobservable on
  every supported compiler). No semantic difference.

### 3.2 Top/leaf bitmask update on insert and remove

- Insert (`oa_insert_node_into_bin`): sets leaf bit then top bit when the
  bin was empty — same order and values as upstream (leaf byte is `u8`,
  explicit `(u8)` cast where upstream relies on integer promotion; same
  result).
- Allocate: after popping the bin head, updates `bin_indices[bin] =
  node.bin_list_next`, clears the popped node's successor's `bin_list_prev`,
  then clears the leaf bit when the bin empties and the top bit when the
  leaf byte hits zero — identical order to upstream.
- Remove (`oa_remove_node_from_bin`): the "easy case" (has `bin_list_prev`)
  unlinks mid-list; the "hard case" (bin head) recomputes the bin via
  `oa_uint_to_float_round_down(node->data_size)`, updates the head, and
  applies the same leaf/top bit clearing. Identical to upstream, including
  the recompute-from-`data_size` (which is why `removeNodeFromBin` is called
  before the combined node's fields are mutated in `free`).

### 3.3 Neighbour-coalescing branches

`sk_offset_allocator_free` reproduces upstream `free` exactly: merge with
`neighbor_prev` when it is a free node (offset ← prev offset, sizes summed,
`removeNodeFromBin(prev)`, assert `prev->neighbor_next == node`,
`node->neighbor_prev = prev->neighbor_prev`), then the same for
`neighbor_next`; snapshot both neighbor links; push the freed node index on
the stack; `insertNodeIntoBin(size, offset)` for the combined region; then
re-link the new combined node into the neighbor chain (both directions,
with the upstream asserts `nextNode.neighborPrev == nodeIndex` /
`prevNode.neighborNext == nodeIndex` preserved).

### 3.4 Free-node stack ordering

- Reset: `free_offset = max_allocs - 1`, `free_nodes[i] = max_allocs - i - 1`
  (inverse order so node 0 pops first) — identical.
- Pop: `free_nodes[free_offset--]` in insert; push:
  `free_nodes[++free_offset]` in `free` and `removeNodeFromBin` — identical.
- `allocate` early-out at `free_offset == 0` and the report's
  `free_offset > 0` guard — identical (the report intentionally reports
  0/0 when only one node slot remains, matching upstream).

### 3.5 Storage-report math

- `storageReport`: `totalFreeSpace = free_storage` (exact) and
  `largestFreeRegion = floatToUint(highest top bit << 3 | highest leaf bit)`
  — the bin lower bound, not the exact region size; the upstream
  `assert(totalFreeSpace >= largestFreeRegion)` is preserved.
- `storageReportFull`: per-bin linked-list walk counting regions and
  `size = floatToUint(bin_index)` per bin — identical.

### 3.6 Differential execution

Beyond the unit tests (which carry upstream's expected values verbatim),
the port and the upstream C++ were compiled side by side and driven with
three identical deterministic 20,000-op sequences (same LCG, same
alloc/free pattern) while printing every allocation's
offset/metadata/size and every storage report (total, largest, per-bin
count hash):

1. 1 MiB arena, 4096 nodes, sizes 1..4096 — **byte-identical** (20,022 lines)
2. 0xf0000000-byte arena, 128 nodes, sizes 1..16 MiB (near-u32 bin edges) —
   **byte-identical**
3. 4 KiB arena, 64 nodes, sizes 1..64 KiB (99.9% of ops hit NO_SPACE; node
   exhaustion + arena exhaustion paths) — **byte-identical**

No semantic drift found. Deliberate deviations (all API-shape, none
algorithmic): no move constructor / moved-from guard (`if (!m_nodes)`)
— the C API is create/destroy/reset; `reset()` reuses the arrays allocated
at create (upstream reallocates); node indices are always `u32` (upstream's
optional 16-bit mode is not ported); exceptions are replaced by the
`SK_OFFSET_ALLOCATOR_NO_SPACE` sentinel.

## 4. License and vendoring

- Both new files carry the verbatim MIT block "Copyright (c) 2023
  Sebastian Aaltonen" (identical in the two files, and textually identical
  to the upstream LICENSE file, wrapped in the project's `/* */` header
  style).
- The upstream C++ sources were **never vendored** under `thirdparty/` —
  the port lives directly in `core/` as a from-scratch C reimplementation
  (no upstream code is compiled or linked; `git ls-tree thirdparty` shows no
  OffsetAllocator). Nothing was removed and nothing was added to
  `thirdparty/`; the repo convention is that vendored code sits in
  `thirdparty/` with its own CMake target, and since the port is a
  reimplementation (not a vendored copy) the convention is to keep it in
  core with the MIT header. This is the same arrangement the port commits
  established; it is documented here so reviewers can confirm no upstream
  copy is accidentally left behind.

## 5. Public C API surface

```c
typedef struct sk_offset_allocator_t sk_offset_allocator_t;

typedef struct sk_offset_allocator_allocation_t { u32 offset; u32 metadata; } ...;
typedef struct sk_offset_allocator_storage_report_t { u32 total_free_space; u32 largest_free_region; } ...;
typedef struct sk_offset_allocator_free_region_t { u32 size; u32 count; } ...;
typedef struct sk_offset_allocator_storage_report_full_t { sk_offset_allocator_free_region_t free_regions[256]; } ...;

sk_offset_allocator_t* sk_offset_allocator_create(u32 size, u32 max_allocs, const sk_allocator_t* allocator);
void sk_offset_allocator_destroy(sk_offset_allocator_t* allocator);
void sk_offset_allocator_reset(sk_offset_allocator_t* allocator);
sk_offset_allocator_allocation_t sk_offset_allocator_allocate(sk_offset_allocator_t* allocator, u32 size);
void sk_offset_allocator_free(sk_offset_allocator_t* allocator, sk_offset_allocator_allocation_t allocation);
u32 sk_offset_allocator_allocation_size(const sk_offset_allocator_t* allocator, sk_offset_allocator_allocation_t allocation);
sk_offset_allocator_storage_report_t sk_offset_allocator_storage_report(const sk_offset_allocator_t* allocator);
sk_offset_allocator_storage_report_full_t sk_offset_allocator_storage_report_full(const sk_offset_allocator_t* allocator);
```

Constants: `SK_OFFSET_ALLOCATOR_NO_SPACE` (0xffffffff),
`SK_OFFSET_ALLOCATOR_DEFAULT_MAX_ALLOCS` (128*1024), plus the bin-encoding
constants (`NUM_TOP_BINS` 32, `BINS_PER_LEAF` 8, shifts/masks). The header
is `extern "C"`-guarded and includes only `allocator.h` / `common.h`.

## 6. Reviewer checklist

- Confirm the two new core files carry the verbatim MIT header (see §4).
- Confirm no `thirdparty/` changes exist in the branch diff.
- Confirm `ctest` output shows `sk-tests` passing with the 18
  `offset_allocator_*` tests (see §2).
- Sanity-check the three areas most sensitive to port drift: the
  round-up path in `allocate` vs round-down in insert/remove (§3.1), the
  leaf/top bitmask clearing order (§3.2), and the neighbor re-link in
  `free` (§3.3).
