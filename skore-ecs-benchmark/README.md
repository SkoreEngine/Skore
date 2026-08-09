# skore-ecs-benchmark

Benchmark harness for comparing ECS implementations (Skore ECS vs. Flecs) on the Skore Engine.

> **Status: scaffold only.** The project structure and an empty build are in place. No
> Skore or Flecs integration yet — that lands in a later step.

## Layout

```
skore-ecs-benchmark/
├── CMakeLists.txt                 # top-level build definition
├── include/
│   └── skore_ecs_benchmark/       # public headers (version.hpp, benchmark.hpp)
├── src/
│   └── benchmark/                 # benchmark sources + entrypoint (main.cpp)
├── .gitignore
└── README.md
```

## Requirements

- CMake 3.22 or newer
- A C++20 compiler (GCC, Clang, or MSVC)
- A build generator such as Ninja or Make

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
