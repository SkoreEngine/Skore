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

### Tests

In-source `SK_TEST` blocks (see `AGENTS.md`). Host runs core/app then each plugin DLL.

```bash
cmake --build build --target sk-tests
ctest --test-dir build --output-on-failure
# or: build/bin/sk-tests
```

Release builds never compile test bodies into plugins.

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
