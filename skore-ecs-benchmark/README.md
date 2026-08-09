# skore-ecs-benchmark

Benchmark harness for comparing ECS implementations (Skore ECS vs. Flecs) on the Skore Engine.

> **Status: Skore wired in.** Skore (branch `v2`) is pinned as a git submodule under
> `thirdparty/skore` and linked into the build. The ECS benchmark scenarios
> themselves are not implemented yet.

## Layout

```
skore-ecs-benchmark/
├── CMakeLists.txt                 # top-level build definition
├── include/
│   └── skore_ecs_benchmark/       # public headers (version.hpp, benchmark.hpp)
├── src/
│   └── benchmark/                 # benchmark sources + entrypoint (main.cpp)
├── thirdparty/
│   └── skore/                     # Skore engine (git submodule, branch v2)
├── .gitignore
└── README.md
```

## Requirements

- CMake 3.22 or newer
- A C++20 compiler (GCC, Clang, or MSVC)
- A build generator such as Ninja or Make

## Skore submodule

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
targets the benchmark depends on (`sk-core`, `sk-entities`) are built.

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

## Options

| Option | Description |
| --- | --- |
| `-DSKORE_ECS_BENCH_ENABLE_SANITIZERS=ON` | Build with address/UB sanitizers |

Example:

```sh
cmake -S . -B build -G Ninja -DSKORE_ECS_BENCH_ENABLE_SANITIZERS=ON
cmake --build build
```
