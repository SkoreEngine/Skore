# Jolt Physics (vendored)

Rigid-body / soft-body physics engine, vendored as the foundation for the
engine's physics integration (goal: `physics: jolt integration`).

- Upstream: https://github.com/jrouwe/joltphysics
- Version: **v5.6.0** (release tag `v5.6.0`, commit `e77f175595e64cb44218cc9d9d56fc365ad0e36a`)
- License: MIT (see `LICENSE`)

## Version record

| Field     | Value                                                            |
|-----------|------------------------------------------------------------------|
| tag       | v5.6.0                                                           |
| commit    | e77f175595e64cb44218cc9d9d56fc365ad0e36a                         |
| upstream  | https://github.com/jrouwe/joltphysics                             |
| license   | MIT (`LICENSE`)                                                   |
| vendored  | 2025-08-12                                                        |

## Vendored tree

Only what is needed to build the library is kept: the complete `Jolt/`
source tree (sources, headers, `.inl`, compute shader data and wrappers)
plus the MIT `LICENSE`. Everything else from the upstream repository is
excluded — `Samples`, `UnitTests`, `TestFramework`, `PerformanceTest`,
`HelloWorld`, `JoltViewer`, `Docs`, `Assets`, `Build/` (CI scripts),
`Doxyfile` / `doxygen-awesome.css` / `run_doxygen.bat`,
`sonar-project.properties` (SonarQube CI), and the upstream `README.md` /
`ContributorAgreement.md`.

Two files inside `Jolt/` are also trimmed as editor/tooling extras:
`Jolt/Jolt.cmake` (upstream CMake module — skore declares its own target in
`CMakeLists.txt` instead) and `Jolt/Jolt.natvis` (Visual Studio debugger
visualizer).

## Build integration

`thirdparty/CMakeLists.txt` adds this directory; the `jolt` static library
target compiles the 153 translation units listed in `CMakeLists.txt`, which
mirror the `JOLT_PHYSICS_SRC_FILES` list from upstream `Jolt/Jolt.cmake` at
the pinned commit. GPU compute (DX12/VK/MTL/CPU), the debug renderer, and
the ObjectStream RTTI layer are compiled out via upstream defaults
(`JPH_USE_*` / `JPH_DEBUG_RENDERER` / `JPH_OBJECT_STREAM` are not defined);
the affected TUs are internally `#ifdef`-guarded and compile to no-ops.

Consumers include `<Jolt/Jolt.h>` (include root is this directory, matching
upstream's `PHYSICS_REPO_ROOT`) and link the `jolt` target. The target sets
`POSITION_INDEPENDENT_CODE ON` so it can be linked into shared plugins, and
threads `-pthread` through on Unix like upstream.

## Updating

Re-vendor from the pinned tag (or a newer tag) by re-copying `Jolt/` and
`LICENSE`, regenerating the source list from the upstream `Jolt.cmake`, and
updating this version record.
