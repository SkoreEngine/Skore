# AGENTS.md — Skore

C game/engine project. **Multiplatform** (Windows, Linux, macOS, and future targets). Pure C public API (with `extern "C"` for C++). CMake multi-target layout.

All code must be written for **every supported platform**. Prefer platform-abstracted APIs and portable logic; isolate OS/API differences behind thin platform layers. Do not assume a single OS, compiler, or windowing/graphics stack.

## Do / don’t

Data-oriented ECS engine. Prefer structure-of-arrays, systems, and plain data over object hierarchies.

### Do

**Project / modules**

- Put shared types and `SK_API` / platform flags in `common.h` (or evolve it carefully).
- **Platform abstraction first.** Public and shared code must not call Win32, POSIX, Cocoa, or other OS APIs directly. Use portable project APIs (`sk_*`) and confine OS-specific code to dedicated platform backends / `#ifdef` blocks behind those APIs so the same call sites run on all targets.
- Design every feature, API, and test to **build and run on all supported platforms** unless the user explicitly scopes work to one OS.
- Prefer fixed-width types (`u8`…`u64`, `i8`…`i64`, `f32`/`f64`), path helpers, and endian-safe I/O over host-dependent sizes (`long`, bare `int` for layout, `wchar_t` paths in shared code).
- **Portable casts / printf (LP64 vs LLP64).** Linux/macOS x64 are LP64 (`long` and `uintptr_t` are 64-bit). Windows x64 is LLP64 (`long` is 32-bit; `uintptr_t` is `unsigned long long`). Code that only builds on Linux can still fail Windows clang-tidy (`readability-redundant-casting`) or mis-format values.
  - Use project fixed widths (`u32`/`u64`) in shared code; do **not** use `long` for sizes, pointers, or file offsets.
  - For printf of pointers / pointer-sized ints: **one** cast that matches the format — e.g. `(unsigned long long)ptr` with `%llx`, or `(uintptr_t)ptr` with `"%" PRIxPTR` from `<inttypes.h>`.
  - **Never** stack casts “for portability”: `(unsigned long long)(uintptr_t)x` is often **needed** on Linux and **redundant** on Windows (fails CI tidy). Pick one cast.
  - Do **not** cast when the expression is already the destination type (e.g. no `(u64)` on a `u64`, no `(unsigned long long)` on a value that is already `unsigned long long` under the active ABI).
  - After first-party C/C++ edits on a Linux agent: run native build/tests **and** `./scripts/check-windows-abi.sh` (MinGW + clang-tidy LLP64). See **Building**.
- Use `sk_*_api_t` **only** for a single global module surface (one table for the whole process/module), e.g. `sk_render_device_api_t`. Do **not** use the `_api_t` suffix for ordinary values/objects that happen to hold function pointers (e.g. `sk_allocator_t` — many instances, not one global API).
- Keep plugins loadable as shared libraries (DLL / `.so` / `.dylib`) that **statically link** `sk-foundation`.
- Use `sk_` prefix on public symbols (types, functions). **Do not** prefix file names with `sk_`.
- Plugins **register** data + systems from `sk_plugin_entry_point` (components, systems, resources via APIs) — no free-running global constructors or hidden side effects at load beyond registration.
- Couple plugins only through foundation-registered IDs, events, and headers — never by including another plugin’s `.c` or linking its binary.
- **Search the project before adding a utility.** Reuse existing helpers. If something new is needed, implement it for **broad reuse**, not a one-off for a single call site.
- Prefer placing shared utilities in **`foundation`** when they are engine-general.
- **Host lifecycle and engine utilities live in `foundation` (`sk-foundation`).** Process lifecycle (`sk_app_init`), OS backends (`platform_*.c`, `filesystem_*.c`), and pure engine utilities (math, containers, default allocator) all implement in `foundation/`. Plugins statically link `sk-foundation`; they must not call `sk_app_init`.
- **No mutable file-scope `static` variables in `foundation/` or `plugins/`.** Every plugin statically links its own copy of `sk-foundation`, so a file-scope static would give host and plugin *separate* copies of the same state (the pre-refactor split-logger bug). Only `static const` data and `static` functions are allowed; any other file-scope static must be justified in `no-statics-allowlist.txt`. Enforced by `scripts/check-no-statics.py`, wired into ctest as `sk-no-statics-guard` (APX-284). The same guard fails on reintroduction of the deleted `sk_app_api(`, `sk_logger_api(`, `sk_repository_api(` accessors.
- If logic is shared by **several plugins** but not core-wide, add a **common plugin** (e.g. `common-render`, `common-audio`) that those plugins depend on via headers / registration — do not copy-paste the same helper into each plugin.
- Third-party code (only when explicitly requested) is **vendored under `thirdparty/`** — see that section below. No package managers, no `FetchContent`, no git submodules.
- **Prefer opaque / forward type declarations.** In headers, use `typedef struct sk_foo_t sk_foo_t;` (incomplete type) and pass pointers. Define the full `struct sk_foo_t { ... }` in the `.c` (or a private header) unless the type’s **layout is part of the module/plugin public API** (e.g. POD components, a global `sk_*_api_t`, other data callers must size or field-access). Do not leak implementation structs just for convenience.
- **Modules, not micro-files.** Group related types and APIs into a coherent module file (e.g. `math3d.h` / `math3d.c` holding vectors, matrices, quaternions, etc.). Do **not** split one domain into many tiny headers (`vector.h`, `mat4.h`, `quat.h`, …). Prefer extending an existing module over creating a new file. New files only when there is a real new module boundary.
- **Invest heavily in tests.** New behavior is incomplete without tests. Prefer writing tests early (with or right after the code), not as an afterthought.
- Ship **both unit tests and integration tests** for every meaningful area (core utilities, ECS, plugins, loaders, etc.). See **Tests** below.
- **Render work is incomplete without a sandbox PNG check.** Any task that changes what is drawn (UI, docking, GPU encode, fonts, capture, shaders, layout→paint) must add or update a host under `sandbox/` in the style of `sandbox/dock_preview_sandbox.c`, run it, and **read the written image** before calling the task done. This is a host app, not a `SK_TEST`. See **Render sandboxes**.

**Memory**

- Allocate at known boundaries (init, load, scene change). Prefer arenas, pools, and pre-sized buffers over ad-hoc heap use.
- State ownership and lifetime next to any allocation site (who frees, when, which allocator).
- Reserve pools/chunks at world init or scene load — not per-entity heap on spawn in gameplay paths.

**Alignment / cache**

- Treat **64 bytes** as the cache line. Hot structs (per-entity, per-frame, inner-loop state) should fit in **one cache line (≤ 64 bytes)** when practical, or be split into hot/cold parts.
- Align hot arrays/structs to 64 bytes when they are walked tightly.
- **No silent padding** — if layout has holes, add explicit fields (`u8 _pad0[N]`, etc.) and comment why.
- Prefer fixed-size members over types that change layout across platforms.
- Split cold data (names, editor metadata, debug strings) out of hot component arrays.

**ECS / data-oriented design**

- Prefer **SoA over AoS** for hot data: parallel arrays / chunks per component type, not fat `Entity` structs holding every component.
- **Systems own iteration**, not entities — systems query component sets and run in a declared order (or dependency graph). No “entity ticks itself” via virtual methods.
- **Components are plain data** — POD / `sk_*_t` blobs only. No vtables; no hidden per-component heap pointers unless cold/rare and documented.
- Public entity references are **stable IDs with generation** (handles). Dense indices may recycle — never store raw array indices across frames without a generation check.
- **Structural changes are deferred** — create/destroy entities and add/remove components go through command buffers (or phase barriers), not mid-query iteration.
- **Frame phases are explicit** (e.g. input → sim → late-sim → render-extract → render). Each system declares which phase it runs in.
- **Resources are singleton data** (time, input, device, etc.) accessed as explicit resource handles — not hidden globals. If a global is unavoidable, use a plain variable name (no `sk_` / `sk_g_` prefix) and document ownership.
- Every array pass carries **count / capacity** — no sentinel-terminated hot paths.
- Prefer **deterministic-friendly** defaults where it matters: fixed tick for sim when needed; no hash-map iteration order as gameplay truth; seed RNGs explicitly.
- **Editor uses the same ECS world model** — editor behavior is extra systems/resources, not a second object hierarchy.
- **Sim vs render:** simulation systems write data; render extract reads a snapshot or command stream. Do not drive GPU/renderer from sim systems.

**Errors (no exceptions)**

- Recoverable failures return an `i32` / project status code (`0` = success, non-zero = failure) or out-param + status; document the contract on every public API.
- Use sentinel values only when the domain already has an obvious invalid id (and document it).
- **No defensive parameter policing.** Callers are trusted. Do **not** soft-fail on “parameters must be valid” cases that only happen from buggy call sites — e.g. `if (context == NULL || app_api == NULL || app_api->set_api == NULL) return -1;`. That path will never fire in correct engine use; it hides bugs, bloats code, and is useless in a game engine. Document the contract (non-null, initialized, etc.) and use the pointers/fields directly.
- **Check only real, possible runtime failures:** device/driver init failed, resource create failed, file/plugin load failed, out of memory at a known allocation boundary, OS/API call returned an error, capacity exceeded when growth is allowed, etc. Those are expected-failure paths; return an error code (and log if useful).
- **Programmer errors** (null that must exist, invariant broken, bad API use, dead handle): optional `assert` / debug trap in debug builds only — never a production `return -1` “just in case.” Prefer no check at all over a soft defensive return. Validate ECS access (generation/alive) in debug builds when useful.
- **Expected runtime failures** (file missing, plugin load fail, GPU create fail): return an error code; log if useful; do not assert.
- Do not “log and continue” past a corrupted engine state — fail the operation cleanly or abort if the process cannot continue safely.

**Threading**

- Default is **main-thread ownership** of engine and world state.
- Plugin entry and normal API table calls run on the **main thread** unless a function is explicitly marked worker-safe.
- When parallel work exists (job system / workers): **lock-free queues or explicit job completion on main**; document ownership (main-only, immutable after publish, single-writer). Prefer “produce on worker, apply on main” over locking.
- Parallel systems only on **disjoint queries** (or documented single-writer rules). Document any API safe off main.

### Don’t

**Project / modules**

- Link plugins against `sk-foundation-lib` only (headers without the static library) — plugins and apps must **statically link** `sk-foundation`.
- Call `sk_app_init` / `sk_app_run` from a plugin — those are host-only (`player` / `editor` / `sandbox` / test hosts).
- Skip `extern "C"` on public headers.
- Introduce C++ in public headers without a clear exception.
- Use unprefixed public types/functions.
- Prefix variables with `sk_`, `sk_g_`, `g_`, etc. (`sk_g_platform_err` → wrong; use `platform_err`).
- Introduce RTTI, exceptions, or C++ heavy containers in foundation public headers — shared surface stays C and POD-friendly.
- **Hard-code a single platform** in shared or game-facing code (e.g. raw `LoadLibrary` / `CreateFileW` / `pthread_*` / Cocoa calls outside a platform backend). No `#ifdef _WIN32` sprawl across gameplay, ECS, or plugin logic — keep OS branches in platform abstraction layers only.
- Ship or accept code that only builds or runs on one OS when the feature is meant to be engine-wide.
- Stack “portable” printf casts such as `(unsigned long long)(uintptr_t)…` or cast to a type the expression already has — Linux may accept it; Windows LLP64 clang-tidy will fail (`readability-redundant-casting`). Use one cast or `PRIxPTR` / fixed-width types instead.
- Couple plugins by linking or compiling against each other — only shared headers + foundation registration.
- Expose full `struct` definitions in public headers for types that are not part of the module/plugin API — no “I needed the fields in three places” leakage of internal state; keep those opaque and use accessors or free functions.
- Name every function-pointer struct `sk_*_api_t` — reserve `_api_t` for **one global** module table; multi-instance or pass-by-value FP bags use normal `sk_*_t` names (`sk_allocator_t`, not `sk_allocator_api_t`).
- **Mirror a whole `sk_*_api_t` as free functions** (`sk_window_create` + `api->create_window`, etc.) — table only; implementations are `static` in the providing `.c`.
- **Export plugin “get the static table” free helpers** for host use (`sk_platform_window_api()`, `sk_*_get_api`) — hosts use `app_api->get_api` after plugin registration.
- Create a new source/header pair for a single type or tiny helper when it belongs in an existing module — no `vector.h` / `mat4.h` style fragmentation; use modules like `math3d.h`.
- Name source/header files with an `sk_` prefix — use plain module names (`math3d.h`, `common.h`, not `sk_math.h`). Avoid names that shadow C library headers (do not use `math.h`).
- **Do not add external libraries** to solve a problem unless the user explicitly asks for that dependency.
- When a library **is** requested: copy its **source into `thirdparty/<lib_name>/`**, strip non-essential baggage (`.git`, docs, examples, upstream tests, CI, etc.), **always keep the license** (and required NOTICE/COPYING), add a CMake target, and register it with `add_subdirectory(<lib_name>)` in `thirdparty/CMakeLists.txt` (and ensure the root builds `thirdparty`). Do **not** use package managers, CMake `FetchContent`, git submodules, or other network/download dependency hooks. If it cannot be vendored that way, **do not add it** — stop and say so.
- Do not invent a narrow one-shot helper without checking for an existing one; do not duplicate the same utility across plugins — lift to `foundation` or a `common-*` plugin.
- Ship features **without tests**, or only “happy path” checks for foundation/ECS/plugin behavior that can fail in subtle ways (handles, structural changes, deferred commands, plugin load).
- Skip integration coverage because unit tests exist (or the reverse) — **both** layers are required.
- Close a **render** task because unit tests passed or the sandbox binary exited 0 — the PNG must be opened and judged.

**Memory / layout**

- Allow implicit heap allocation in hot-path code — no `malloc` / `free` / `new` / `delete` (or hidden equivalents: growing containers, string ops, temporary vectors) in per-frame / per-tick / inner-loop paths. If heap is required, it must be explicit, off the hot path, and documented.
- Allocate per entity on spawn in gameplay paths — use reserved pools/chunks.
- Rely on compiler padding or reordering for layout — no “mystery” holes in hot structs; no unaligned hot loads where avoidable.
- Put fat pointers, STL, or growing strings inside components — use fixed sizes and IDs into string/resource tables.

**ECS / data-oriented design**

- Hide behavior in component “methods” — logic lives in systems (or pure functions systems call).
- Iterate “all entities” with giant tag checks when a proper query/archetype would skip empty sets.
- Mutate the component graph while a query is live — no add/remove/destroy mid-foreach without a defer path.
- Share writable component pointers across threads without a documented single-writer / disjoint-query rule.
- Treat the editor as a separate object model forever — same ECS, different systems.
- Call renderer/GPU APIs from simulation systems — extract, then render.

**Errors / concurrency**

- Use C++ exceptions, or invent ad-hoc error styles per module (mix of magic ints, errno, and silent `void` failures).
- Write defensive “null / bad param shape” guards that soft-return on impossible call-site bugs (`if (ctx == NULL || api == NULL || api->fn == NULL) return -1;`). Trust the caller; only handle failures that can actually occur at runtime (init, create, load, OOM, OS/API errors).
- Treat programmer misuse as a recoverable error code path — that is defensive noise. Fix the caller or `assert` in debug; do not paper over bad API use with `-1`.
- Spawn threads or touch main-owned state from a plugin without a documented, engine-provided concurrency path.

## Layout

```
skore-new/
├── CMakeLists.txt          # root: add_subdirectory(foundation, editor, player, plugins, …)
├── foundation/             # static library sk-foundation (+ optional header interface sk-foundation-lib)
│   ├── common.h            # fixed-width types, platform macros, SK_API
│   ├── app.h / app.c       # sk_app_* registry, init/run, bootstrap runtime
│   ├── platform_*.c        # sk_platform_api_t backends (OS-specific)
│   └── *.h / *.c           # module files without sk_ file prefix
├── player/                 # executable sk-player (game runner)
├── editor/                 # editor app (WIP)
├── sandbox/                # host apps that render offscreen → PNG (not tests)
│   └── dock_preview_sandbox.c  # sk-sandbox: docking drop-preview capture
├── plugins/
│   └── example_plugin/     # SHARED dll sk-example-plugin (template for all plugins)
├── tests/
│   ├── main.c              # bootstrap only (host registry + plugin scan)
│   └── CMakeLists.txt      # sk-tests + CTest
└── thirdparty/             # vendored third-party libraries (in-tree only)
    ├── CMakeLists.txt      # list: add_subdirectory(<lib_name>) per library
    └── <lib_name>/         # full source tree of that library + its CMakeLists.txt
```

| Target | Kind | Links | Role |
|--------|------|-------|------|
| `sk-foundation` | STATIC | — | Engine + host lifecycle (types, modules, `sk_app_init`, OS backends); linked by apps and plugins (**no** `SK_TESTS`) |
| `sk-foundation-lib` | INTERFACE | headers only | Optional header-only access; prefer linking `sk-foundation` |
| `sk-test` | STATIC | unity | Test registry only (`BUILD_TESTING`); not linked into Release plugins / player |
| `sk-foundation-tests` | STATIC | `sk-test` | Foundation sources with `SK_TESTS` — host tests only |
| `sk-player` | EXECUTABLE | `sk-foundation` (static) | Game entry (`main` → `sk_app_init`) |
| `sk-sandbox` | EXECUTABLE | `sk-foundation` (static) | Headless render sandbox (`sandbox/dock_preview_sandbox.c`) |
| `sk-tests` | EXECUTABLE | `sk-foundation-tests` | Test host: in-process foundation + scan plugins for `sk_plugin_run_tests` |
| `sk-*-plugin` | SHARED | `sk-foundation` (static); + `sk-test` when non-Release testing | Dynamically loaded DLL; no static twin |
| `sk-*-plugin-lib` | INTERFACE | plugin headers | Optional header export for that plugin |

## Third-party vendoring (`thirdparty/`)

This is the **only** way to add external libraries.

1. User must **explicitly ask** for the dependency.
2. Create `thirdparty/<lib_name>/` and put the **library source code in that folder** (not a submodule, not a download at configure time).
3. **Trim the vendor tree:** remove files that are not needed to build/use the library — e.g. `.git` / `.github`, docs, examples, demos, benchmarks, upstream tests, CI configs, IDE project files, unrelated assets. **Always keep the license file** (and any NOTICE/COPYING/AUTHORS the license requires). Prefer keeping only the sources, public headers, and whatever CMake/build bits you need.
4. Add `thirdparty/<lib_name>/CMakeLists.txt` that defines the library target (sources, includes, etc.). Prefer a **project-owned** CMakeLists that lists the vendored sources explicitly over relying on upstream’s full project if that pulls in extras.
5. Register it in `thirdparty/CMakeLists.txt`:
   ```cmake
   add_subdirectory(<lib_name>)
   ```
6. Wire `thirdparty` from the root `CMakeLists.txt` with `add_subdirectory(thirdparty)` when the root should build vendored libs (order: typically before targets that link them).
7. Consumers `target_link_libraries(... <vendored_target>)` as needed.

**Forbidden:** package managers (vcpkg, Conan, NuGet, …), CMake `FetchContent` / `ExternalProject` download flows, git submodules, “install system-wide and find_package” as the primary path.

**If it cannot be vendored this way:** do not add the library; report that and stop.

**Never delete** license / required attribution files from a vendored library.

Template layout (see repo):

```
thirdparty/
├── CMakeLists.txt              # add_subdirectory(some_library) …
└── some_library/
    ├── CMakeLists.txt          # define the lib target
    └── …                       # library sources live here
```

## Tests

Testing is a **first-class part of the engine**, not optional polish. Prefer over-testing critical paths (ECS, memory, handles, plugin boundaries) over shipping untested behavior.

### Architecture (in-source, plugin-local)

Tests live **in the same `.c` file as production code** (Rust/Zig style), not in separate `*_test.c` files. A thin host under `tests/` only bootstraps and aggregates.

| Piece | Role |
|-------|------|
| `foundation/test.h` / `sk-test` | Registry macros (`SK_TEST`), constructor auto-registration, `sk_test_run_all`, report types. Links Unity. |
| `SK_TESTS` compile def | Enables test bodies. **Never set on Release / MinSizeRel plugins or production `sk-foundation` / `sk-player`.** |
| `#ifdef SK_TESTS` … `#endif` | Wraps every test section so Release preprocessor-strips them entirely. |
| `sk-foundation-tests` | Same sources as production, compiled **with** `SK_TESTS`. Host-only. |
| `sk-tests` executable | Runs host registry, then loads each plugin DLL and calls `sk_plugin_run_tests`. |
| Plugin `sk_plugin_run_tests` | Exported only under `SK_TESTS`. Host skips the symbol when missing (Release). |

**No static plugin twins** (`sk-*-static` removed). Unit tests for plugin code compile into the real SHARED plugin when `SK_TESTS` is on (non-Release).

**Flow**

```
sk-tests (host)                 — default ctest / Apex fast
  1. sk_test_run_all()          — foundation (linked sk-foundation-tests, whole-archive)
  2. for each dll in {app_folder}/plugins:
       load → sk_plugin_run_tests(&report) → unload
  3. aggregate ran/failed → process exit code

sk-integration-tests            — lives in the skore-test-suite repo
  Vulkan / UI capture / resource+entity fixtures. Built there against this engine.
```

Plugin tests are **plugin-local**: each DLL that links `sk-test` has its own registry + Unity instance. The host only aggregates `{ran, failed}`. Do not share Unity globals across the host↔DLL boundary.

### Writing a test

```c
/* at bottom of math3d.c (or any module .c) */
#ifdef SK_TESTS
#include "test.h"

SK_TEST(vec3_dot_unit_axes)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, sk_vec3_dot(sk_vec3_right(), sk_vec3_right()));
}
#endif /* SK_TESTS */
```

- Name describes behavior (`vec3_dot_unit_axes`), not `test_1`.
- Use Unity asserts (`TEST_ASSERT_*`) inside `SK_TEST`.
- Always wrap the whole section in `#ifdef SK_TESTS`.
- Header-only modules (e.g. `common.h`): put tests in `foundation/test_cases.c` (host-only; not linked into plugins).

### Plugin export

Every plugin must export both:

```c
SK_API int sk_plugin_entry_point(sk_app_context_t* context, const sk_app_api_t* app_api);
#ifdef SK_TESTS
SK_API i32 sk_plugin_run_tests(sk_test_report_t* out);  /* omit entire function when !SK_TESTS */
#endif
```

`sk_add_plugin` wires non-Release `SK_TESTS` + `sk-test` link. Release plugins omit the export entirely.

### Required layers

| Layer | Where it lives | What it covers |
|-------|----------------|----------------|
| **Unit** | Same `.c` as the module (`SK_TEST` under `#ifdef SK_TESTS`) | Pure helpers, math, containers, API table shape, error codes |
| **Integration** | Host-side (`app.c` under `SK_TESTS`) and/or plugin-local | Plugin load + registration, app bootstrap, multi-module paths |

Both layers are **always** expected to exist. Default `ctest` / Apex fast runs
the **unit** layer only (`sk-tests`). GPU / lavapipe / UI-capture cases live in
**skore-test-suite** (this repo keeps the harness under `tests/integration/`).
Do not run the GPU suite on every engine-agent iteration.

### What “invest a lot” means in practice

- New public API or system → `SK_TEST` cases for contracts, edge cases, and failure paths (not only success).
- ECS structural ops → generation handles, mid-iteration rules, post-apply state.
- Plugins → in-DLL unit tests for the module API; host tests that load the real DLL and use registered surfaces; export `sk_plugin_run_tests` under `SK_TESTS` only.
- Bugs → add a **regression `SK_TEST`** that fails before the fix and passes after.
- Refactors → keep the suite green; do not delete tests without replacing coverage.
- Prefer deterministic tests (fixed seeds, no wall-clock flakiness, no hidden machine-specific paths when avoidable).

### Placement

```
foundation/math3d.c        # production + #ifdef SK_TESTS { SK_TEST(...) }
foundation/test.h / test.c # registry (sk-test target; linked by host + plugins)
foundation/test_cases.c    # host-only common.h / atomics / harness SK_TESTs
foundation/app.c           # host/app + platform/fs/bootstrap integration tests
plugins/foo/foo.c          # plugin unit tests in-file
plugins/foo/plugin_entry_point.c  # sk_plugin_entry_point + sk_plugin_run_tests
tests/main.c               # bootstrap only (host run + plugin scan)
tests/CMakeLists.txt       # sk-tests + CTest
tests/integration/         # reusable harness only (cases live in skore-test-suite)
```

### Build & run

- Same stack: **CMake + Ninja**.
- `include(CTest)` / `BUILD_TESTING` (default ON).
- Typical loop:

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure          # unit + smoke (sk-tests)
# or: ./build/bin/sk-tests
# or: ./build/bin/sk-tests --filter=vec3_*
# integration (Vulkan / UI capture / lavapipe) — skore-test-suite repo:
#   cmake -S ../skore-test-suite -B ../skore-test-suite/build -G Ninja
#   ctest --test-dir ../skore-test-suite/build -L integration --output-on-failure
#   # SK_RUN_INTEGRATION=0 skips the integration binaries under ctest
# widget family automation (check #2, no GPU) is also in that repo:
#   point SKORE_DIR at origin/feature/review-necessary-widgets-for-skore-edito
#   (fetch first — a local feature/ checkout can be a stale SHA; APX-363)
#   ctest --test-dir ../skore-test-suite/build --output-on-failure
#   SK_TEST_FILTER='ui_author_*'  and  scripts/run-widget-automation.sh
```

- Optional: `sk-tests [--list] [--filter=<tokens>] [plugins_dir]` — `plugins_dir` overrides `{app_folder}/plugins`. `--filter` is comma-separated exact names and/or `prefix*` (same as `SK_TEST_FILTER`). `--list` prints registered names and exits.
- Unity is vendored under `thirdparty/unity/` (project already includes it).
- Host links `sk-foundation-tests` with **whole-archive** so constructor-registered tests are not dropped by the linker.

### Do / don’t (tests)

**Do**

- Put tests in the same `.c` as the code under `#ifdef SK_TESTS` + `SK_TEST(name)`.
- Keep `tests/` as **bootstrap only** (`main.c` + CMake).
- Export `sk_plugin_run_tests` from every plugin under `#ifdef SK_TESTS` (omit the whole function in Release).
- Write unit + integration coverage for non-trivial changes.
- Keep tests hermetic: no network, no writes outside a temp/build area.

**Don’t**

- Add separate `*_test.c` files or a growing `tests/unit` mirror of the tree.
- Ship `SK_TESTS` / Unity / test bodies in **Release** (or MinSizeRel) plugins or `sk-player`.
- Reintroduce static plugin twins for testing.
- Link Unity into both host and plugins expecting one shared runner state — plugin-local only.
- Merge with no tests for new behavior; disable failing tests without a replacement.

## Render sandboxes

Render / GPU / UI-paint work is **not done** when unit tests pass. The agent must produce a frame, look at it, and say whether it matches the intended picture.

This is a **host application** under `sandbox/`, same kind as `player` / `editor`. It is **not** a `SK_TEST`, not CTest, and not a skore-test-suite binary.

**When this applies**

Any task that changes what pixels come out: UI widgets, docking, layout→paint, GPU encode, shaders, fonts/MSDF, capture/readback, offscreen targets, clear colors, blend, or a host present path.

**What to do**

1. Add or update a sandbox `.c` under `sandbox/` following `sandbox/dock_preview_sandbox.c` (reuse `sk-sandbox` when the same scene still applies; otherwise add another host in `sandbox/` with the same pattern). Widget families go through `--widget` in `sandbox/widget_review.c` — see `docs/widget-lavapipe-png-review.md`.
2. Boot like a real app: `sk_app_init` → plugin APIs via `get_api` → `capture_create` (offscreen RGBA8 texture, no window) → build the scene → `paint` → `capture_frame` → `cpu_image_write_png`.
3. Build the **engine** tree and run from that `bin/` (so `{app_folder}/plugins` resolves):
   ```bash
   cmake --build build --target sk-sandbox
   export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
   export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
   (cd build/bin && ./sk-sandbox --widget button --out ../widget-review)
   ```
   Dock drop-preview (no `--widget`): `(cd build/bin && ./sk-sandbox --out dock_preview.png)`.
   On Windows: `skore\cmake-build-debug\bin\sk-sandbox.exe`.
4. **Open and read the PNG.** Exit code 0 is not a visual check. Compare the frame to the intended layout/colors/preview. Report what is right and what is wrong. If it is wrong, fix and recapture in the same session. Never shell out to grok or another LLM to grade the image.

**Do / don’t**

- Do keep sandboxes as thin hosts that link only `sk-foundation` and load plugins at runtime.
- Do write the PNG next to cwd / `--out` so the path is printed and inspectable.
- Do not wrap this in `#ifdef SK_TESTS` or register it with CTest.
- Do not put it in skore-test-suite or use `ui_capture_harness` / `sk_test_*`.
- Do not declare a render task complete without having looked at the image.

## Architecture rules

1. **`sk-foundation` is always statically linked.** Apps (`player` / `editor` / `sandbox`) and plugins link production `sk-foundation` (no `SK_TESTS`). The test host links `sk-foundation-tests` instead. Free functions from static-linked foundation are available after that link.
2. **No DLL import linking for host↔plugin engine APIs.** Prefer a **single global** function-pointer table (`sk_*_api_t`) for a module surface that crosses a shared-library boundary without a static link (e.g. host fill of `sk_render_device_api_t`, host callbacks into a plugin). Callers fill/use that one table rather than importing symbols from another DLL. This naming/pattern is **not** for every struct that embeds function pointers — see Naming.
3. **Plugins are SHARED libraries that statically link `sk-foundation`.** A plugin is built as SHARED and `target_link_libraries(... PRIVATE sk-foundation)`. It is still loaded at runtime (`LoadLibrary` / `GetProcAddress` on Win32; Linux/macOS equivalents). Do not link other plugins. Plugins must **not** call `sk_app_init`.
3b. **Plugin / host module APIs are table-only.** Publish a `sk_*_api_t` (types + table layout in a header), fill one static table in the `.c`, register it with `app_api->set_api`. Hosts call **only** through pointers from `get_api` (or the table they registered). **Never** add free-function mirrors of every table entry (`sk_window_create` next to `api->create_window`, etc.), and **never** expose public `sk_*_get_api` / `sk_*_api()` accessors on plugins for host use — registration + registry lookup is enough. Implementations of table entries stay `static` in the plugin `.c`.
4. **`sk-foundation` owns process lifecycle and host API implementations.** `sk_app_init`, OS backends (`platform_*.c`, `filesystem_*.c`), and engine utilities all live in `sk-foundation`. `player` / `editor` / `sandbox` link `sk-foundation`.
5. **One foundation module.** Public headers and their implementations live together under `foundation/` (types, `sk_*_api_t` layouts, free-function prototypes, and the `.c` backends). Include style stays `#include "app.h"` (PUBLIC include dir = `foundation/`).
6. **Plugin entry points** (exported from every SHARED plugin):
   ```c
   SK_API int sk_plugin_entry_point(sk_app_context_t* context, const sk_app_api_t* app_api);
   #ifdef SK_TESTS
   SK_API i32 sk_plugin_run_tests(sk_test_report_t* out); /* not present when !SK_TESTS */
   #endif
   ```
7. **App entry** lives in `player`, `editor`, or `sandbox`. `sk-foundation` owns init:
   ```c
   sk_app_boot_t sk_app_init(int argc, char* argv[]); /* {context, api}; no sk_app_api() */}
   ```
8. **Tests are in-source** (`SK_TEST` under `#ifdef SK_TESTS`). Host `tests/main.c` runs foundation then scans plugins via `sk_plugin_run_tests`. Never ship tests in Release — see **Tests**.

## Naming

| Kind | Pattern | Example |
|------|---------|---------|
| Files (modules) | `{module}.h` / `{module}.c` — one domain per pair, not one type per file; **no** `sk_` file prefix | `math3d.h` (not `vector.h` + `mat4.h`) |
| Types / structs | `sk_{name}_t` | `sk_vec3_t`, `sk_allocator_t` |
| Global API tables | `sk_{name}_api_t` — **only** a single process/module-wide table of entry points | `sk_render_device_api_t` |
| Free functions | `sk_{module}_{action}` | `sk_vec3_dot`, `sk_allocator_default` |
| CMake targets | `sk-{name}` | `sk-foundation`, `sk-player`, `sk-example-plugin` |
| Fixed integers | `u8` `u16` `u32` `u64` `i8` `i16` `i32` `i64` | from `common.h` |
| Fixed floats | `f32` `f64` | from `common.h` |
| Pointer aliases | `void_ptr_t` `const_ptr_t` `char_ptr_t` `const_chr_t` | from `common.h` |
| Variables (local, static, file-scope, globals) | plain name — **no** `sk_`, `sk_g_`, or other project prefix | `platform_err`, not `sk_g_platform_err` |

- Prefer project typedefs over raw `int` / `float` / `unsigned` in public headers.
- **Never prefix variables.** Locals, parameters, statics, and file-scope / global data use plain descriptive names (`platform_err`, `count`, `allocator`). Do **not** use `sk_`, `sk_g_`, `g_`, or similar prefixes on variables. The `sk_` prefix is for **public types and functions** only — not for file names or data identifiers inside `.c` files.
- Internal helpers: `static` in the `.c` file; no public-symbol prefix required (function names may still use `sk_` if exported; file-local `static` functions need no prefix).

### `sk_*_api_t` vs other function-pointer structs

**`sk_*_api_t` = one global module surface.** There is effectively a single table for that capability in the process (or one table filled by the host and handed around as *the* API for that module). Example shape:

```c
/* Global module API — one table, not an instance type */
typedef struct sk_render_device_api_t {
    /**
     * Create a GPU buffer.
     * @param size Bytes to allocate on the device.
     * @return 0 on success, non-zero on failure.
     */
    i32 (*create_buffer)(u64 size);
    void (*destroy_buffer)(u32 buffer_id);
} sk_render_device_api_t;
```

**Not every FP table is an `_api_t`.** If the type is a **value/object** (many instances, copied or passed as data, backends swapped per use), use a normal `sk_*_t` name even when it holds function pointers + instance state. The allocator is the canonical example (see `foundation/allocator.h`):

```c
/* Multi-instance / pass-by-value — NOT sk_allocator_api_t */
typedef struct sk_allocator_t {
    void_ptr_t instance;
    void_ptr_t (*alloc)(void_ptr_t instance, size_t size);
    void (*free)(void_ptr_t instance, void_ptr_t ptr);
    void_ptr_t (*realloc)(void_ptr_t instance, void_ptr_t ptr, size_t size);
} sk_allocator_t;
```

| Use | Name | When |
|-----|------|------|
| Single global module entry table | `sk_{name}_api_t` | Host↔plugin or “the” device/module API for the process |
| Object / strategy / backend bag with FPs | `sk_{name}_t` | Multiple instances, pluggable backends, passed as data (`sk_allocator_t`) |

Free functions remain fine for **pure utilities** statically linked from `sk-foundation` (math, containers, paths). Do **not** invent an `_api_t` just because a type has function pointers.

**Never** duplicate a global `sk_*_api_t` as free functions. If the surface is an API table, call sites use the table (especially plugins / host modules that cross process or DLL boundaries via the app registry).

## Header / source standards

**Headers**

- `#pragma once`
- Include `common.h` (or a module that pulls it in) when using project types.
- Always wrap public declarations in `extern "C"` for C++:
  ```c
  #ifdef __cplusplus
  extern "C" {
  #endif
  // ...
  #ifdef __cplusplus
  }
  #endif
  ```
- **Opaque types by default.** Prefer a forward declaration in the header and keep the full `struct` definition in the `.c` (or a private header):
  ```c
  /* public header — handle only */
  typedef struct sk_world_t sk_world_t;

  sk_world_t* sk_world_create(void);
  void sk_world_destroy(sk_world_t* world);
  ```
  Only define the full struct in a header when that layout is **part of the module/plugin public API** (e.g. POD components, a global `sk_*_api_t` callers fill, multi-instance FP bags like `sk_allocator_t`, data the consumer is meant to read/write by field). Internal engine state, runtime objects, and implementation details stay opaque.
- Document public functions and public function-pointer fields with Doxygen-style comments (`@param`, `@return`).

### Complete module shape (illustrative)

All of the following live in AGENTS.md as the canonical pattern — there is no `sk_test_module_*` sample code in the tree. Real modules follow this shape (`math3d`, `allocator`, plugins, etc.).

**Public header** (`foo.h`):

```c
#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* POD type — layout is public when callers field-access it */
typedef struct sk_foo_state_t {
    u32 flags;
    f32 weight;
} sk_foo_state_t;

/*
 * Global module API table: sk_{name}_api_t
 * Use *_api_t ONLY for a single process/module-wide entry table
 * (e.g. sk_render_device_api_t). Multi-instance FP bags stay sk_{name}_t.
 */
typedef struct sk_foo_api_t {
    /**
     * Combine two integers.
     * @param a first value
     * @param b second value
     * @return a + b
     */
    i32 (*sum_int)(i32 a, i32 b);

    /**
     * Combine two floats.
     * @param a first value
     * @param b second value
     * @return a + b
     */
    f32 (*sum_float)(f32 a, f32 b);
} sk_foo_api_t;

/*
 * Free functions from sk-foundation are fine for plugins/apps that statically
 * link sk-foundation. Use a single sk_*_api_t only for global module surfaces
 * that cross DLL boundaries without a static link.
 */

/**
 * Sum two integers (free function; static-linked core).
 * @param a first value
 * @param b second value
 * @return a + b
 */
i32 sk_foo_sum(i32 a, i32 b);

#ifdef __cplusplus
}
#endif
```

**Source** (`foo.c`):

```c
#include "foo.h"

/* Non-exported helpers stay static; no public prefix required */
static i32 sk_foo_sum_impl(i32 a, i32 b)
{
    return a + b;
}

i32 sk_foo_sum(i32 a, i32 b)
{
    return sk_foo_sum_impl(a, b);
}
```

**Multi-instance FP object** (not `*_api_t`) — same idea as `foundation/allocator.h`:

```c
typedef struct sk_allocator_t {
    void_ptr_t instance;
    void_ptr_t (*alloc)(void_ptr_t instance, size_t size);
    void (*free)(void_ptr_t instance, void_ptr_t ptr);
    void_ptr_t (*realloc)(void_ptr_t instance, void_ptr_t ptr, size_t size);
} sk_allocator_t;
```

**Sources (rules)**

- Include the matching header first.
- Keep non-exported helpers `static`.
- Platform export for plugin symbols: `SK_API` (currently Win64: `__declspec(dllexport)` via `SK_WIN`).
- **Initialize at declaration — never split declare-then-assign.** C11 mid-block decls are fine. Do **not** write C89-style “declare at the top of the block, assign later” when the first use is an assignment (CLion: *Declaration and assignment can be joined*). Join them, and declare at the first use when that keeps scope tight.
  - Good: `u32 count = bootstrap.plugins.count;`
  - Good: `const sk_platform_api_t* plat = app_platform_api();`
  - Bad: `u32 count;` then later `count = bootstrap.plugins.count;`
  - Prefer `for (u32 i = 0u; i < n; i++)` over a separate `u32 i;` + `for (i = 0u; …)`.
  - Exception: leave uninitialized only when the first write is not a simple assignment (e.g. out-param fill, multi-branch init that cannot share one expression). Never invent a dummy init just to silence a warning.
- **Prefer single-line statements.** Do not break a call, assignment, or condition across lines when it fits comfortably (~100–120 columns). Break only when the line is genuinely long (many arguments, nested expressions) or readability suffers — not by default after the first comma.
  - Good: `app_api->set_api(context, SK_PLATFORM_WINDOW_API_TYPE_ID, (const void_ptr_t)&platform_window_api);`
  - Bad: wrapping that same call onto two lines for no reason.

## CMake conventions

- Minimum CMake **3.22**; project name `skore`.
- Collect sources with `file(GLOB_RECURSE ... *.h *.c)` per target (matches existing examples).
- **Foundation**
  - `add_library(sk-foundation STATIC ...)`
  - `target_include_directories(sk-foundation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})`
  - `POSITION_INDEPENDENT_CODE ON` so foundation can be linked into shared plugins.
  - Optional companion `sk-foundation-lib` INTERFACE (headers only); prefer linking `sk-foundation`.
  - Owns `sk_app_init`, app registry backends, platform/filesystem OS backends, and engine utilities.
- **Player / apps**
  - `add_executable(...)`
  - `target_link_libraries(... PRIVATE sk-foundation)`
  - Render sandboxes live in `sandbox/` (`sk-sandbox`); same link rules, no `SK_TESTS`.
- **Plugins**
  - `add_library(sk-... SHARED ...)`
  - Private includes for the plugin dir.
  - `target_link_libraries(... PRIVATE sk-foundation)` — **always** statically link foundation into plugins. Plugins must not call `sk_app_init`.
  - Optional `sk-...-lib` INTERFACE to re-export that plugin’s headers.
- Root `CMakeLists.txt` adds subdirs: `foundation`, `editor`, `player`, `plugins`, `sandbox`, and `thirdparty` when vendored libs are in use.
- New plugins: add a folder under `plugins/`, `add_subdirectory` from `plugins/CMakeLists.txt`, copy the `example_plugin` pattern.
- New third-party libs: folder under `thirdparty/<lib_name>/` with sources + `CMakeLists.txt`, then `add_subdirectory(<lib_name>)` in `thirdparty/CMakeLists.txt`.

## Reference files (examples of the standards)

Patterns for types, global `sk_*_api_t`, free functions, `static` helpers, and multi-instance FP bags are documented in **Header / source standards** and **`sk_*_api_t` vs other function-pointer structs** above (illustrative `sk_foo_*` snippets). Live code that matches those rules:

| File | What it demonstrates |
|------|----------------------|
| `foundation/common.h` | Integer/float aliases, `SK_API`, platform defines |
| `foundation/math3d.h` / `foundation/math3d.c` | Module pair, public POD types, free functions, in-source `SK_TEST`s |
| `foundation/test.h` / `foundation/test.c` | Test registry, `SK_TEST`, filter/CLI, `sk_plugin_run_tests` contract |
| `foundation/test_cases.c` | Host-only common.h / atomics / harness SK_TESTs (not linked into plugins) |
| `foundation/allocator.h` | Multi-instance FP bag as `sk_allocator_t` (not `*_api_t`) |
| `foundation/app.h` / `foundation/app.c` | App registry API (+ host integration tests) |
| `foundation/platform.h` / `foundation/platform_*.c` | Platform API + OS backends |
| `foundation/app.c` | App registry, process entry (`sk_app_init` / `sk_app_run`), bootstrap |
| `player/main.c` | Thin `main` calling `sk_app_init` |
| `sandbox/dock_preview_sandbox.c` | Render sandbox host: offscreen `capture_create` → PNG |
| `plugins/example_plugin/plugin_entry_point.c` | `sk_plugin_entry_point` + `sk_plugin_run_tests` |
| `plugins/example_plugin/CMakeLists.txt` | SHARED + static link `sk-foundation` via `sk_add_plugin` |
| `tests/main.c` | Test host bootstrap only |
| `plugins/example_plugin/README.md` | Dynamically loaded plugins that statically link core |

## Building

Always use the **Ninja** generator. Out-of-tree build (CLion often uses `cmake-build-debug`).

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Useful targets: `sk-foundation`, `sk-player`, `sk-sandbox`, `sk-tests`, `sk-example-plugin`.

### Linux agent / host: Windows ABI check

Native Linux builds do **not** see Windows LLP64 type widths. After changing first-party C/C++:

```bash
# 1) Normal host build + unit tests (existing). Integration lives in skore-test-suite
#    and runs there by default (SK_RUN_INTEGRATION=0 to skip).
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure
#    ctest --test-dir ../skore-test-suite/build -L integration --output-on-failure

# 2) Windows data-model tidy (no MSVC; uses MinGW headers + clang-tidy)
./scripts/check-windows-abi.sh
```

**Packages on the Linux machine** (one-time):

| Distro | Install |
|--------|---------|
| Debian / Ubuntu | `sudo apt-get install -y mingw-w64 clang clang-tidy g++-mingw-w64-x86-64` |
| Fedora / RHEL | `sudo dnf install -y mingw64-gcc mingw64-headers clang clang-tools-extra` |

Needs **clang** (resource-dir / intrinsics), **clang-tidy**, and **MinGW** headers. Do not feed GCC’s `lib/gcc/.../include` into clang-tidy — that breaks `<windows.h>` parses. You do **not** need MSVC, Wine, or a Windows VM.

Optional: pass specific files (`./scripts/check-windows-abi.sh foundation/stacktrace.c`) or `JOBS=8` for parallelism.

CI: `.github/workflows/ci.yml` also runs **Windows ABI (MinGW tidy)** and **Cppcheck** on `ubuntu-latest` (alongside the multi-OS build matrix).

Apex: `.apex/checks.yaml` includes a blocking **windows-abi** fast-stage check (`bash scripts/check-windows-abi.sh`) so agent/goal runs hit the same gate before build/test.
