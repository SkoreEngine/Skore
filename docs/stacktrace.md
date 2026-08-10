# Stacktrace collection and crash handler

Portable stack capture, symbolization, and fatal-fault reporting for Skore.

| Module | Headers | Library |
|--------|---------|---------|
| Stacktrace | `core/stacktrace.h` | `sk-core` |
| Crash handler | `core/crash.h` | `sk-core` |

`sk_app_init` installs the crash handler at process startup. Embedders may call `sk_crash_uninstall()` to opt out.

---

## Public API

### Stacktrace (`stacktrace.h`)

| Function | Role |
|----------|------|
| `sk_stacktrace_init()` | One-time backend setup (Win32: `SymInitialize`). No-op on POSIX. Returns `0` on success. |
| `sk_stacktrace_shutdown()` | Release backend resources. Safe if never initialized. |
| `sk_stacktrace_capture(frames, capacity, skip_frames)` | Walk the call stack into a caller-owned `sk_stacktrace_frame_t` array. **No allocation.** Returns frame count (`0` if unavailable). |
| `sk_stacktrace_resolve(frames, count)` | Fill module / symbol / source fields in place. Not signal/exception-safe. |
| `sk_stacktrace_format(frames, count, out, out_cap)` | Human-readable multi-line trace into a caller buffer. Always NUL-terminates when `out_cap > 0`. |

Frame fields (fixed capacity; empty string / zero = unknown):

- `address` — instruction pointer (`NULL` = unused frame)
- `module_name`, `module_offset`
- `symbol_name`, `symbol_offset`
- `source_file`, `line` (Windows PDB line info only; POSIX `dladdr` has no line numbers)

Example (diagnostic path, not inside a signal handler):

```c
#include "stacktrace.h"

sk_stacktrace_frame_t frames[64];
(void)sk_stacktrace_init();
u32 n = sk_stacktrace_capture(frames, 64u, 1u); /* skip this wrapper */
sk_stacktrace_resolve(frames, n);

char buf[4096];
if (sk_stacktrace_format(frames, n, buf, (u32)sizeof(buf)) >= 0) {
    fputs(buf, stderr);
    fputc('\n', stderr);
}
sk_stacktrace_shutdown();
```

### Crash handler (`crash.h`)

| Function | Role |
|----------|------|
| `sk_crash_install()` | Register fatal-fault handlers. Idempotent. Returns `0` on success. |
| `sk_crash_uninstall()` | Restore previous handlers. Idempotent; safe when never installed. |

**POSIX** (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`, `SIGABRT`): `sigaction` + dedicated `sigaltstack` so stack-overflow faults can still print. The handler writes a report to stderr, restores default disposition, and re-raises so the exit status and core dump are preserved.

**Windows**: `SetUnhandledExceptionFilter` (primary), a first-chance vectored observer (reentrancy backstop only), and on MSVC the CRT invalid-parameter / purecall hooks. Returns `EXCEPTION_CONTINUE_SEARCH` so Windows Error Reporting still runs.

**Crash path:** never allocates and never calls `printf`. Capture is allocation-free; the handler then **best-effort** calls `sk_stacktrace_resolve` so frames can include module / symbol / `file:line` when the backend has info (DbgHelp + PDB on Windows, `dladdr` on POSIX). Addresses always print. `sk_crash_install` pre-inits the stacktrace backend so first-use setup is not mid-fault. Resolve is not strictly async-signal-safe; if it fails, you still get addresses (offline: debugger, `addr2line`, `llvm-symbolizer`).

---

## Install / uninstall

```c
#include "crash.h"

/* Typical app: sk_app_init already calls sk_crash_install(). */
if (sk_crash_install() != 0) {
    /* rare: sigaltstack/sigaction failed; process handlers unchanged */
}

/* Embedder or test host that wants the previous OS default: */
sk_crash_uninstall();
```

`sk_app_destroy` / app teardown uninstalls so process exit does not leave Skore handlers registered on a dying process.

---

## Required build / link settings

### All platforms

- Link **`sk-core`** (static). Crash + stacktrace live there.
- Prefer **Debug** or **RelWithDebInfo** when you need names/lines in `sk_stacktrace_resolve`.

### Linux (ELF)

| Need | Setting |
|------|---------|
| `dladdr` | Link `libdl` when required (older glibc). CMake already does `find_library(dl)` on `sk-core`. |
| Resolve symbols in **your executable** | Export the symbols you care about (`-rdynamic`, or targeted `--export-dynamic-symbol=name`). Full `--export-dynamic` on hosts that `dlopen` plugins can interpose over each plugin’s static `sk-core` — prefer exporting only specific symbols (see `tests/CMakeLists.txt` for `sk-tests`). |
| Resolve symbols in **shared plugins** | Default visibility (`SK_API`) is enough for those DSO exports. |

`sk-player` deliberately does **not** use full `--export-dynamic` on Linux for the interposition reason above; crash reports still print addresses, and module + offset (plus `dladdr` symbols when exports allow) are filled by resolve when available.

### Apple (Mach-O)

| Need | Setting |
|------|---------|
| Symbol names | Unstripped binary; `dladdr` reads the Mach-O symbol table. |
| Player export | `sk-player` uses `-export_dynamic` (safe under Apple’s two-level namespace). |

### Windows

| Need | Setting |
|------|---------|
| Capture | `CaptureStackBackTrace` (Vista+; project sets `_WIN32_WINNT` ≥ `0x0601`). |
| Symbolization | Link **`dbghelp`** (`sk-core` PUBLIC-links it). |
| PDB | Debug info format **ProgramDatabase** (`/Zi` / CMake `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`). Root `CMakeLists.txt` sets this for MSVC Debug/RelWithDebInfo. |
| Release | Without a PDB next to the `.exe`, resolve falls back to **module name + RVA** (still useful). |

---

## Known limitations

| Limitation | Detail |
|------------|--------|
| Resolve in the crash handler is best-effort | `dladdr` / DbgHelp are not async-signal / fault safe. Crash still always prints addresses; names fill when resolve succeeds. |
| No C++ demangling on POSIX | `dladdr` emits mangled names as-is. Windows uses `SYMOPT_UNDNAME` where DbgHelp supports it. |
| Release / LTO / inlining | Frames may be missing or attributed to an outer function. Helpers used in tests are `noinline`. |
| Stripped binaries | POSIX: empty `symbol_name`. Windows: module + RVA only without PDB. |
| No source lines on POSIX | `dladdr` has no debug-line API; use `addr2line` / `llvm-symbolizer` offline. |
| Stack overflow | POSIX uses an alternate signal stack; Windows still reports `STACK_OVERFLOW` when the filter runs, but stack walks can be shallow. |
| Optimized tail calls | May collapse frames; capture cannot recover them. |

---

## Opt-in crash trigger (tests only)

Production code does **not** ship a deliberate crash path. Under `BUILD_TESTING`, CMake builds:

```text
build/bin/sk-crash-trigger
```

```bash
# From the runtime output directory (next to sk-tests):
./sk-crash-trigger null                 # SIGSEGV / ACCESS_VIOLATION
./sk-crash-trigger abort                # SIGABRT / CRT abort
./sk-crash-trigger fpe                  # SIGFPE / INTEGER_DIVIDE_BY_ZERO
./sk-crash-trigger ill                  # SIGILL / ILLEGAL_INSTRUCTION
./sk-crash-trigger bus                  # SIGBUS (POSIX; AV on Windows)
```

Aliases: `segfault`, `access-violation`, `divide-by-zero`, `illegal-instruction`.

Automated tests in `core/crash.c` spawn this tool as a child, capture stderr, and assert the report header plus at least one numbered frame (`#0 0x…`). Symbolization tests live in `core/stacktrace.c` and **skip cleanly** when the toolchain cannot resolve names (stripped binary, missing PDB).

---

## Sample crash-handler output

Addresses and thread ids vary per run. Format is stable.

### Linux (GCC / glibc)

Captured with `./build/bin/sk-crash-trigger null` (Debug, unstripped):

```text
=== skore crash handler ===
signal: SIGSEGV (segmentation violation) (SEGV_MAPERR)
faulting address: 0x0
thread id: 3107630
stacktrace:
#0 0x5e1296ba590a
#1 0x5e1296ba5d86
#2 0x5e1296ba5f0c
#3 0x7016d4c45330
#4 0x5e1296ba53e3
#5 0x5e1296ba5546
#6 0x7016d4c2a1ca
#7 0x7016d4c2a28b
#8 0x5e1296ba5305
```

`raise(SIGFPE)` / `raise(SIGILL)` / `raise(SIGBUS)` produce the same banner with the matching `signal:` line and no faulting address when `si_code` is user-generated (`SI_USER` / `SI_TKILL`). Example (`sk-crash-trigger fpe`):

```text
=== skore crash handler ===
signal: SIGFPE (floating-point exception)
thread id: 3108504
stacktrace:
#0 0x57621dd4290a
#1 0x57621dd42d86
#2 0x57621dd42f0c
#3 0x7d6477045330
```

### Apple (macOS / AppleClang)

Same POSIX backend and report shape as Linux (`thread id` from `pthread_threadid_np`):

```text
=== skore crash handler ===
signal: SIGSEGV (segmentation violation) (SEGV_MAPERR)
faulting address: 0x0
thread id: 0x1f3a4b
stacktrace:
#0 0x104a01f20
#1 0x104a01f80
#2 0x104a02010
#3 0x19a1b3f28
```

### Windows (MSVC)

Captured with `sk-crash-trigger null` (Debug + PDB; exception filter path):

```text
=== skore crash handler ===
exception: 0xc0000005 (ACCESS_VIOLATION)
faulting address: 0x0
thread id: 8420
stacktrace:
#0 0x7ff612340120
#1 0x7ff6123401a0
#2 0x7ff6123402c0
#3 0x7ffe1234abcd
```

Other kinds (via `RaiseException` in the trigger tool):

```text
exception: 0xc0000094 (INTEGER_DIVIDE_BY_ZERO)
exception: 0xc000001d (ILLEGAL_INSTRUCTION)
```

---

## Sample resolved stacktrace (API, not crash path)

`sk_stacktrace_capture` + `resolve` + `format` on the nested test helpers (`stacktrace_test_middle` → `stacktrace_test_leaf`):

### Linux

```text
#0 0x55aabb01a100  stacktrace_test_leaf (sk-tests +0x1a100)
#1 0x55aabb01a180  stacktrace_test_middle (sk-tests +0x1a180)
#2 0x55aabb01b200  sk_test_run_all (sk-tests +0x1b200)
```

### Apple

```text
#0 0x104a03f00  stacktrace_test_leaf (sk-tests +0x3f00)
#1 0x104a03f80  stacktrace_test_middle (sk-tests +0x3f80)
#2 0x104a04a00  sk_test_run_all (sk-tests +0x4a00)
```

### Windows (with PDB)

```text
#0 0x7ff61235a100  stacktrace_test_leaf (sk-tests.exe +0x1a100) at core\stacktrace.c:606
#1 0x7ff61235a180  stacktrace_test_middle (sk-tests.exe +0x1a180) at core\stacktrace.c:613
#2 0x7ff61235b200  sk_test_run_all (sk-tests.exe +0x1b200) at core\test.c:42
```

Without a PDB, Windows still fills `module_name` + `module_offset`; `symbol_name` / `source_file` stay empty.

---

## See also

- `core/stacktrace.h` / `core/stacktrace.c` — capture backends (POSIX `backtrace`/`_Unwind` + `dladdr`; Win32 `CaptureStackBackTrace` + DbgHelp)
- `core/crash.h` / `core/crash.c` — fatal-fault reporting
- `tests/crash_trigger/main.c` — opt-in fault injector
- `AGENTS.md` — project test and platform rules
