# skore-new

C game/engine project. Pure C public API, CMake multi-target layout.
Supports **Windows**, **Linux**, and **macOS**.

## Building

Requires **CMake 3.22+**, **Ninja**, and a C compiler. Always use the Ninja generator and an out-of-tree build directory.

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Useful targets:

| Target | Kind | Role |
|--------|------|------|
| `sk-core` | static lib | Engine core (linked by apps, sk-app, plugins) |
| `sk-app` | static lib | Host app layer (`sk_app_init`, platform API impl) |
| `sk-player` | executable | Game runner |
| `sk-tests` | executable | Test host (in-source tests + plugin `sk_plugin_run_tests`) |
| `sk-example-plugin` | shared lib | Example plugin (`.dll` / `.so` / `.dylib`; static-links core) |

Build a single target:

```bash
cmake --build build --target sk-player
```

### Docking demo

`sk-player --dock-demo` opens a **fixed 1280×720** workspace built with `sample_dock_demo_build` (no persist / `.ini` restore):

| Region | Window id | Content label |
|--------|-----------|---------------|
| Left leaf | `dock-demo-hierarchy` | LEFT — Hierarchy |
| Center tabs | `dock-demo-scene`, `dock-demo-game` | CENTER — Scene / Game |
| Right top | `dock-demo-inspector` | RIGHT TOP — Inspector |
| Right bottom | `dock-demo-console` | RIGHT BOTTOM — Console |

The split tree and ratios are hardcoded. Two consecutive launches produce the same dock geometry (screenshots are comparable). The window is not resizable so the first frame stays at 1280×720.

```bash
cmake --build build --target sk-player
./build/bin/sk-player --dock-demo
# Headless geometry dump (no window). The `dock-demo-layout:` line is
# identical across consecutive runs:
./build/bin/sk-player --dock-demo --dump-layout | grep '^dock-demo-layout:'
```

See **[docs/ui-plugin.md](docs/ui-plugin.md)** §7.10 for the API.

### Tests

In-source `SK_TEST` blocks (see `AGENTS.md`). Host runs core/app then each plugin DLL.

```bash
cmake --build build --target sk-tests
ctest --test-dir build --output-on-failure
# or: build/bin/sk-tests
```

Release builds never compile test bodies into plugins.

**UI integration suites** (widget vision, flexbox vision, interaction engine) —
one command, CI artifact upload, vision credential gating:

```bash
./scripts/run-ui-integration-tests.sh
```

See **[docs/ui-integration-test-workflow.md](docs/ui-integration-test-workflow.md)**
for how to add a widget test, how vision rubrics work, and how to write
interaction tests with the engine.

### Stacktrace and crash handler

See **[docs/stacktrace.md](docs/stacktrace.md)** for the public API (`sk_stacktrace_*`, `sk_crash_install` / `sk_crash_uninstall`), required link flags (`-rdynamic` / export-dynamic, PDB, `dbghelp`), limitations, and sample output on Linux, macOS, and Windows.

Under `BUILD_TESTING`, `sk-crash-trigger` deliberately raises each handled fault kind (child-process tests and local sample capture only — not shipped in production):

```bash
./build/bin/sk-crash-trigger null   # or: abort, fpe, ill, bus
```

### Compression

See **[docs/compression-api.md](docs/compression-api.md)** for the compression codec abstraction (`sk_compression_codec_*`): one-shot usage examples, the (deferred) streaming contract, codec selection guidance (none / zstd / lz4 / zlib), and the `SK_COMPRESSION_ZSTD` / `SK_COMPRESSION_LZ4` / `SK_COMPRESSION_MINIZ` build flags for the optional codecs. The authoritative design and the main-branch migration record are in `docs/compression-design-v2.md`; the codec evaluation with measurements is in `docs/compression-codecs-evaluation.md`.

### Prerequisites

All platforms need:

- CMake 3.22+
- Ninja
- A C compiler on `PATH`

**Windows**

- Compiler: MSVC (Developer PowerShell / VS), clang-cl, or MinGW
- Ninja: `winget install Ninja-build.Ninja`, or install via your toolchain

**Linux**

- Compiler: GCC or Clang
- X11 / OpenGL headers for vendored GLFW (do **not** vendor `.deb` packages into the repo)
- Example (Debian/Ubuntu):

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build \
  libx11-dev libxcursor-dev libxi-dev libxinerama-dev \
  libxrandr-dev libxkbcommon-dev libgl-dev
```

- Example (Fedora):

```bash
sudo dnf install -y gcc cmake ninja-build \
  libX11-devel libXcursor-devel libXi-devel libXinerama-devel \
  libXrandr-devel libxkbcommon-devel mesa-libGL-devel
```

**macOS**

- Compiler: Apple Clang via Xcode Command Line Tools: `xcode-select --install`
- CMake / Ninja: `brew install cmake ninja`
