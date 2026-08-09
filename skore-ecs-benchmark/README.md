# skore-ecs-benchmark

Benchmark harness for comparing ECS implementations (Skore ECS vs. Flecs) on the Skore Engine.

> **Status: Skore + Flecs comparison wired in.** Skore (branch `v2`) is pinned as
> a git submodule under `thirdparty/skore`; Flecs 4.1.6 is vendored under
> `thirdparty/flecs`. The harness runs equivalent scenarios on both ECSs and
> prints + saves a comparison.

## Layout

```
skore-ecs-benchmark/
├── CMakeLists.txt                 # top-level build definition
├── include/
│   └── skore_ecs_benchmark/       # public headers (version.hpp, benchmark.hpp)
├── src/
│   └── benchmark/                 # benchmark sources + entrypoint (main.cpp)
├── thirdparty/
│   ├── skore/                     # Skore engine (git submodule, branch v2)
│   └── flecs/                     # Flecs 4.1.6 (vendored single-file library)
├── results/
│   └── comparison.md              # latest side-by-side comparison output
├── .gitignore
└── README.md
```

## Requirements

- CMake 3.22 or newer
- A C++20 compiler (GCC, Clang, or MSVC)
- A build generator such as Ninja or Make

## Dependencies

Skore is pinned as a git submodule tracking branch `v2`. Clone/update it with:

```sh
git submodule update --init skore-ecs-benchmark/thirdparty/skore
```

Its ECS (the `sk-entities` plugin and the `sk-core` engine library) is compiled
and linked directly from the benchmark build. Skore sources are never modified
by this project. The Skore build's clang-tidy gate and test host are disabled
for benchmark builds; pass `-DSK_ENABLE_CLANG_TIDY=ON` or `-DBUILD_TESTING=ON`
to override. Skore is added with `EXCLUDE_FROM_ALL`, so its own executables
(`sk-player`, editor, tests) are not part of the default build — only the
targets the benchmark depends on (`sk-core`, `sk-app`, `sk-entities`) are built.

Flecs 4.1.6 is vendored in-tree as the single-file amalgamation (`flecs.c` +
`flecs.h`, MIT license, kept in `thirdparty/flecs/LICENSE`). It is built as a
static library and linked into the benchmark. No package managers or
`FetchContent` are used.

## Building

Configure and build out of source (keeps the source tree clean):

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

The `skore_ecs_benchmark` executable is written to `build/bin/`.

## Running

```sh
./build/bin/skore_ecs_benchmark
```

This runs the full Skore suite, then the equivalent Flecs suite, prints each
suite's timings plus a side-by-side comparison to stdout, and writes the same
comparison to `results/comparison.md` (overwritten on each run).

## Options

| Option | Description |
| --- | --- |
| `--entity-count N` | Entities per scenario (default `100000`) |
| `--query-iterations N` | Query/system iterations per timed run (default `100`) |
| `--warmup-runs N` | Untimed warm-up passes (default `2`) |
| `--timed-runs N` | Timed passes per scenario (default `5`) |
| `--out PATH` | Comparison results file (default `results/comparison.md`) |
| `--help` | Show usage |

Example:

```sh
./build/bin/skore_ecs_benchmark --entity-count 100000 --timed-runs 7 --out results/comparison.md
```

CMake options:

| Option | Description |
| --- | --- |
| `-DSKORE_ECS_BENCH_ENABLE_SANITIZERS=ON` | Build with address/UB sanitizers |

## Scenarios

The same eight scenarios run on both backends, with identical entity counts,
component shapes (Position + Velocity + Health) and iteration counts:

| Scenario | What it measures |
| --- | --- |
| `create_entities` | Spawn N entities holding Position+Velocity |
| `destroy_entities` | Spawn N, then despawn all N |
| `add_component` | Spawn N with Position, add Velocity to each |
| `remove_component` | Spawn N with Position+Velocity, remove Velocity from each |
| `iterate_position` | Query Position and update x (`query_iterations` passes) |
| `iterate_position_velocity` | Query Position+Velocity, `position += velocity` |
| `system_update` | Run a registered system doing the same update |
| `deferred_spawn` | Record N deferred spawns, flush once |

Per scenario the reported value is the average wall time over `--timed-runs`
(after `--warmup-runs`), with best/worst and derived throughput. The
`skore/flecs` ratio in the results table is `skore_avg / flecs_avg`, so a ratio
above `1.0x` means Skore was slower on that scenario for that run.

## Interpreting results

The benchmark measures the unmodified Skore ECS (no changes are made to it —
goal is results only). Results vary by machine, compiler, and allocator, so the
captured `results/comparison.md` is a snapshot; re-run locally for fresh
numbers. A sanity checksum of all entity-update work is printed and stored so
the reader can confirm both backends actually performed the same iteration work.
