# Foundation Refactor — Static State & Process-Wide API Inventory

Status: read-only audit (APX-277). No code changes were made; this document is the
checked-in inventory used to plan the "big one" refactor (make static state
context-owned and rewire the app <-> core boundary).

Method: every `static` line in `app/` and `core/` was extracted and classified
(`grep -rn '^static\|^\s*static ' app core` = 1027 lines). Section 1 lists every
variable; Section 1.7 accounts for the 792 static function definitions and 28
static forward declarations. All `sk_*_api(void)` accessors and every call site
were enumerated from a repo-wide grep (Section 2). Dependency edges (Section 3),
consumers (Section 4), and CMake targets (Section 5) come from source includes
and the CMake files. Line numbers are current as of commit `25cdf8c` (branch
`feature/refactor-big-one`).

Legend: [F] file scope · [L] function scope · [TLS] thread-local · [T] compiled
only under `SK_TESTS` (sk-core-tests / sk-app-tests / sk-tests) · [C] const /
effectively immutable · [3P] touches third-party global state.

---

## 1. Static state inventory

### 1.1 Known offenders (the five the refactor must kill first)

| file:line | declaration | mutators | context-ownership |
|---|---|---|---|
| `core/logger.c:23` | [F] `static sk_log_sink_t sinks[SK_LOGGER_MAX_SINKS];` (16 slots) | `ensure_module_ready` (:67), `sk_logger_add_sink` (:277-278), `sk_logger_remove_sink` (:285-291) | Directly movable into an `sk_logger_t`-owned sink array; sinks are plain data + fn pointers |
| `core/logger.c:24` | [F] `static u32 sink_count = 0;` | :67, :278, :290-291 | Same |
| `core/logger.c:25` | [F] `static i32 module_ready = 0;` (lazy "stdout sink preinstalled" flag) | `ensure_module_ready` (:62-70) | Lazy init becomes constructor / explicit init |
| `core/logger.c:28` | [F] `static const sk_logger_api_t* bound_api = NULL;` (host table bound into a plugin's private core copy) | `sk_logger_bind_api` (:304-306) | The host-bind indirection exists because plugins statically link their own `sk-core` copy (each DLL = one module instance). A context-owned logger removes the need to bind at all — see OPEN QUESTIONS §6.1 |
| `core/resource_asset_builtins.c:28` | [F] `static sk_repository_t* builtins_repository = NULL;` | `sk_resource_asset_builtins_bind_repository` (:30-32); read at :36, :40, :44, :55, :65, :72, :591, :890, :894 | Move onto the `sk_resource_assets_context_t` (it is only ever used inside builtins handlers that already receive the context); `bind_repository` becomes a context field set at `create()` |

### 1.2 Mutable file-scope statics — app

| file:line | declaration | mutators | notes |
|---|---|---|---|
| `app/filesystem_unix.c:24` | [F] `static char temp_override[SK_FS_TEMP_CAP];` | `sk_fs_set_temp_folder` (:70-79) | Per-process filesystem override. Move into an `sk_fs_context_t` or keyed-by-app-context state |
| `app/filesystem_unix.c:25` | [F] `static int temp_override_set;` | :71, :76, :79 | Same |
| `app/filesystem_unix.c:54` | [F] `static sk_fs_view_entry_t* view_list;` (leaked single-linked list of mmap views) | `map_view_of_file` (:466-467), `unmap_view_of_file` (:476-491) | Move onto the filesystem context; also fixes the current shutdown leak (no teardown anywhere) |
| `app/filesystem_win32.c:20` | [F] `static char temp_override[SK_FS_TEMP_CAP];` | mirror of unix :24 | Win32 twin |
| `app/filesystem_win32.c:21` | [F] `static int temp_override_set;` | | Win32 twin |
| `app/filesystem_win32.c:46` | [F] `static sk_fs_view_entry_t* view_list;` | | Win32 twin of the mmap view list |

### 1.3 Mutable file-scope statics — core

| file:line | declaration | mutators | notes |
|---|---|---|---|
| `core/stacktrace.c:111` | [F] `static CRITICAL_SECTION dbghelp_cs;` (Win32 only) | `win32_init` (:118), `win32_shutdown` | DbgHelp is process-wide; the CS guards Sym* calls. Context-owning means owning the DbgHelp session itself (SymInitialize per context) — OPEN QUESTION §6.4 |
| `core/stacktrace.c:112` | [F] `static BOOL dbghelp_cs_ready;` (Win32) | :118-120 | |
| `core/stacktrace.c:113` | [F] `static BOOL dbghelp_initialized;` (Win32) | :115-116, :137-138, `win32_shutdown` | |
| `core/crash.c:18` | [F] `static sk_stacktrace_frame_t crash_frames[CRASH_MAX_FRAMES];` | crash handler fill | Scratch buffer used only inside the signal/VEH handler; the handler is per-process by OS design — OPEN QUESTION §6.2 |
| `core/crash.c:19` | [F] `static char crash_line[CRASH_LINE_CAP];` | crash handler | same |
| `core/crash.c:20` | [F] `static u32 crash_line_len;` | crash handler | same |
| `core/crash.c:210` | [F] `static struct sigaction crash_old_actions[CRASH_SIGNAL_COUNT];` (POSIX) | `crash_install_posix` (:415, :445), uninstall (:378) | Restores previous signal dispositions — inherently process-wide |
| `core/crash.c:211` | [F] `static stack_t crash_old_stack;` (POSIX, sigaltstack save) | :380-385 | |
| `core/crash.c:212` | [F] `static u32 crash_signal_count_installed;` (POSIX) | :415, :445 | |
| `core/crash.c:213` | [F] `static i32 crash_stack_ready;` (POSIX) | :380-385 | |
| `core/crash.c:216` | [F] `static volatile sig_atomic_t crash_active;` (POSIX reentrancy guard) | :486-489 | |
| `core/crash.c:220` | [F] `static union { u8 bytes[64KiB]; max_align_t align; } crash_alt_stack_mem;` (POSIX alt stack storage) | written by stack use only | |
| `core/crash.c:473` | [F] `static volatile LONG crash_active;` (Win32 reentrancy guard) | :558+ | |
| `core/crash.c:475` | [F] `static crash_filter_fn crash_old_filter;` (Win32) | :592, :609 | |
| `core/crash.c:476` | [F] `static PVOID crash_vectored_handle;` (Win32) | :597, :612 | |
| `core/crash.c:478` | [F] `static crash_invalid_param_fn crash_old_invalid_param;` (Win32 MSVC) | :600 | Hooks the **CRT** invalid-parameter handler — process-global by design |
| `core/crash.c:479` | [F] `static crash_purecall_fn crash_old_purecall;` (Win32 MSVC) | :603 | CRT global |
| `core/crash.c:653` | [F] `static u32 crash_install_count;` (nested install refcount) | `sk_crash_install` (:656-672), `sk_crash_uninstall` (:675-685) | Refcount is fine to move onto a crash state object, but the underlying handlers are still process-wide |
| `core/crash.c:1072` | [T] [F] `static u64* volatile crash_test_null_target;` | test :1091-1092 | SK_TESTS-only fault-injection target |
| `core/test.c:35` | [F] `static sk_test_entry_t tests[SK_TEST_MAX];` | `sk_test_register` (:38+) | The unit-test registry (sk-test lib). Not shipped in production `sk-core` (test.c is excluded there) |
| `core/test.c:36` | [F] `static u32 test_count = 0u;` | `sk_test_register`, runner | same |
| `core/resource_assets.c:2199` | [T] [F] `static sk_repository_t* ra_test_payload_repo = NULL;` | test helpers | SK_TESTS-only (test region :1800-2893) |
| `core/resource_assets.c:2200` | [T] [F] `static i32 ra_test_direct_import_count = 0;` | test helpers | SK_TESTS-only |
| `core/resource_serialize.c:3886` | [T] [F] `static u32 ser_it_seq;` (per-test temp-dir sequence) | `ser_it_make_temp_dir` (:3892) | SK_TESTS-only (test region :1299-4934) |
| `core/compression.c:636` | [T] [F] `static u32 stub_alloc_calls = 0u;` | counting-allocator test | SK_TESTS-only |
| `core/compression.c:1211` | [T] [F] `static u32 zstd_stub_alloc_calls = 0u;` | zstd counting stub | SK_TESTS-only |
| `core/compression.c:1212` | [T] [F] `static u32 zstd_stub_free_calls = 0u;` | zstd counting stub | SK_TESTS-only |
| `core/compression.c:1498` | [T] [F] `static u32 zlib_stub_alloc_calls = 0u;` | zlib counting stub | SK_TESTS-only |
| `core/compression.c:1499` | [T] [F] `static u32 zlib_stub_free_calls = 0u;` | zlib counting stub | SK_TESTS-only |
| `core/compression.c:1745` | [T] [F] `static u8 compat_compressible_payload[768];` | compat fixture builder | SK_TESTS-only |
| `core/compression.c:1746` | [T] [F] `static u8 compat_incompressible_payload[768];` | compat fixture builder | SK_TESTS-only |
| `core/compression.c:1815` | [T] [F] `static compression_compat_run_t compression_compat_runs[4];` | compat harness | SK_TESTS-only |
| `core/compression.c:1816` | [T] [F] `static u32 compression_compat_run_count = 0u;` | compat harness | SK_TESTS-only |

### 1.4 Function-scope mutable statics (also process-wide)

| file:line | declaration | notes |
|---|---|---|
| `app/app.c:1116` | [T] [L] `static int dummy_api = 42;` | inside `test_api_registry`-family tests |
| `app/app.c:1135` | [T] [L] `static char marker = 'x';` | |
| `app/app.c:1147` | [T] [L] `static char a='a', b='b', c='c';` | |
| `app/app.c:1168` | [T] [L] `static char a='a', b='b', c='c';` | |
| `app/app.c:1185` | [T] [L] `static char a='a';` | |
| `app/app.c:1207` | [T] [L] `static char a='a', b='b', c='c';` | |
| `app/app.c:1226` | [T] [L] `static char a='a', b='b';` | |
| `app/app.c:1227` | [T] [L] `static char other_x='x', other_y='y';` | |
| `app/app.c:1250` | [T] [L] `static char a='a', b='b';` | |
| `app/app.c:1264` | [T] [L] `static char a='a';` | |
| `app/app.c:1278` | [T] [L] `static char impl='i';` | |
| `app/app.c:1279` | [T] [L] `static int api_val = 7;` | |
| `app/platform_win32.c:78` | [L] `static LARGE_INTEGER freq = {0};` (QPC frequency, lazy-init) | `monotonic_seconds` (:77-87) |
| `core/compression.c:1751` | [T] [L] `static i32 built = 0;` (compat fixture built-once flag) | |
| `core/compression.c:2234` | [T] [L] `static u8 binary_payload[512];` (harness corpus) | |
| `core/compression.c:2235` | [T] [L] `static i32 binary_init = 0;` | |
| `core/resource_assets.c:2002` | [T] [L] `static dummy_handler_state_t state;` (handler fixture state) | |
| `core/resource_assets.c:2003` | [T] [L] `static sk_resource_asset_handler_t handler;` (fixture) | |
| `core/resource_assets.c:1829` | [T] [L] `static sk_resource_asset_handler_t handler = {...};` (fixture table) | non-const but never mutated |
| `core/resource_assets.c:1851` | [T] [L] `static sk_resource_asset_importer_t importer = {...};` (fixture table) | non-const, never mutated |
| `core/resource_assets.c:2131` | [T] [L] `static sk_resource_asset_handler_t empty = {...};` (zero handler) | non-const, never mutated |

### 1.5 Thread-local statics

| file:line | declaration | notes |
|---|---|---|
| `app/platform_unix.c:19` | [TLS] [F] `static SK_PLATFORM_THREAD_LOCAL char platform_err[SK_PLATFORM_ERR_CAP];` | per-thread err buffer for `sk_platform_error`; safe for concurrency but is process-wide per-thread state, not per-context — OPEN QUESTION §6.5 |
| `app/platform_win32.c:19` | [TLS] [F] `static __declspec(thread) char platform_err[SK_PLATFORM_ERR_CAP];` | Win32 twin |

### 1.6 Immutable / effectively-immutable static tables (const or write-once)

All of these are candidate state but are read-only after init, so they do **not**
block the refactor; they can stay file-static or move to context-owned const
tables. Complete list (file:line):

- `app/app.c:174` — `static const sk_app_api_t app_api` (the process-wide app table; returned by `sk_app_api()`)
- `app/platform_unix.c:99` — `static const sk_platform_api_t platform_api`
- `app/platform_win32.c:92` — `static const sk_platform_api_t platform_api`
- `app/filesystem_unix.c:541` — `static const sk_filesystem_api_t filesystem_api`
- `app/filesystem_win32.c:524` — `static const sk_filesystem_api_t filesystem_api`
- `core/logger.c:57` — `static const sk_log_sink_t stdout_sink`
- `core/logger.c:298` — `static const sk_logger_api_t logger_api`
- `core/logger.c:602, 646` — [L] `static const char* base` (file-sink test names)
- `core/allocator_mimalloc.c:21` — `static const sk_allocator_t mimalloc_allocator` (process-default mimalloc heap — see §6.3)
- `core/compression.c:85, 238, 392, 546` — `static const sk_compression_codec_t {none,zstd,lz4,zlib}_codec`
- `core/compression.c:573` — `static const sk_compression_codec_t* codecs[]` (build-time registration, no runtime mutation)
- `core/compression.c:1555-1703` — 15 × `static const u8 compat_*_frame[]` [T]
- `core/compression.c:1743-1744` — [T] `static const u8 compat_{one_byte,fixture}_payload[]`
- `core/compression.c:1780, 1790, 1800` — [T] `static const compression_compat_vector_t compression_{zstd,lz4,zlib}_compat_vectors[]`
- `core/compression.c:1917` — [T] [L] `static const u64 cut_points[]`
- `core/compression.c:2183` — [T] `static const compression_harness_v1_adapter_t harness_v1_adapters[]`
- `core/compression.c:2230` — [T] [L] `static const u8 text_payload[]`
- `core/compression.c:2346, 2367` — [T] [L] `static const char* boundary_names[]`, `static const u8 one[]`
- `core/crash.c:207` — `static const int crash_signals[]` (POSIX)
- `core/crash.c:97` — [L] `static const char digits[]`
- `core/crash.c:483` — `static const struct {u32 code; const char* name;} crash_exception_names[]` (Win32)
- `core/offset_allocator.c:600` — [L] `static const struct {...}` (test table)
- `core/offset_allocator.c:1159, 1160, 1179` — [T] [L] `static const u32 straddle_sizes[]` (1159), `straddle_offsets[]` (1160), `big_straddle[]` (1179)
- `core/repository.c:3091` — `static const sk_repository_api_t repository_api`
- `core/repository.c:3196, 3201, 3573, 5489, 5751` — [T] `static const sk_resource_field_t {test_payload_fields, test_int_field, rt_fields, ext_fields, buf_fields}[]`
- `core/repository.c:5786` — [T] [L] `static const u8 payload[8]`
- `core/resource_assets_types.c:13, 30, 64, 88, 109, 178, 199, 215` — `static const sk_resource_field_t ...[]`
- `core/resource_assets_types.c:235, 244, 253, 262, 271, 280, 289, 298` — `static const sk_resource_type_desc_t ..._desc`
- `core/resource_assets_types.c:307` — `static const sk_resource_type_desc_t* const resource_asset_type_descs[]`
- `core/resource_asset_builtins.c:98, 102, 107` — `static const sk_resource_field_t {named,content,audio}_resource_fields[]`
- `core/resource_asset_builtins.c:113, 118` — macro-generated `static const sk_resource_type_desc_t` (per-type `SK_DEFINE_RESOURCE_TYPE` desc)
- `core/resource_asset_builtins.c:124` — `static const sk_resource_type_desc_t audio_resource_type_desc`
- `core/resource_asset_builtins.c:148` — `static const sk_resource_type_desc_t* const builtin_type_descs[]`
- `core/resource_asset_builtins.c:224, 257, 294, 364, 401, 434, 471, 509, 542, 635, 683, 721, 761, 782, 803, 824, 857, 896` — 18 × `static sk_resource_asset_handler_t <name>_handler` (non-const structs, registered once via `add_impl`, never mutated afterwards)
- `core/resource_asset_builtins.c:957, 996, 1035, 1074, 1102, 1191` — 6 × `static sk_resource_asset_importer_t <name>_importer` (same pattern)
- `core/resource_asset_builtins.c:929, 970, 1009, 1059, 1087, 1115` — [L] `static const_chr_t exts[]` inside importer `extensions()` fns
- `core/resource_asset_builtins.c:1206` — `static sk_resource_asset_handler_t* const builtin_handlers[]`
- `core/resource_asset_builtins.c:1227` — `static sk_resource_asset_importer_t* const builtin_importers[]`
- `core/resource_assets.c:2191, 2195` — [T] `static const sk_resource_field_t ra_test_resource_fields[]`, `static const sk_resource_type_desc_t ra_test_resource_desc`
- `core/resource_assets.c:2273` — [T] `static const sk_resource_asset_handler_t ra_test_handler`
- `core/resource_assets.c:2292, 2347` — [T] [L] `static const char ext[]`
- `core/resource_assets.c:2334, 2380` — [T] `static const sk_resource_asset_importer_t ra_test_{,generic_}importer`
- `core/resource_assets.c:1766` — [L] `static const sk_resource_assets_api_t api` inside `sk_resource_assets_api()`
- `core/resource_serialize.c:1431` — `static const sk_resource_field_t ser_trivial_fields[]`
- `core/resource_serialize.c:1769, 1779, 1896, 3327, 3562, 3640` — [T] [L] `static const_chr_t {named_types, content_types, types}[]`
- `core/stacktrace.c:232, 359` — `static const stacktrace_backend_t {win32,posix}_backend`
- `core/stacktrace.c:371, 373` — `static const stacktrace_backend_t* const stacktrace_backend = &{win32,posix}_backend` (compile-time platform pick)

### 1.7 Static functions (not state — accounted for)

The remaining grep hits are TU-local functions: **792 static function
definitions** and **28 static forward declarations** across app/core. They hold
no storage and are not refactor targets, but they are included in the
`grep -rn '^static...'` count for completeness. Per-file static-line totals:

| file | static lines | of which functions/decls |
|---|---|---|
| `app/app.c` | 50 | 38 |
| `app/filesystem_unix.c` | 30 | 27 |
| `app/filesystem_win32.c` | 30 | 27 |
| `app/platform_unix.c` | 9 | 7 |
| `app/platform_win32.c` | 10 | 8 |
| `core/allocator_mimalloc.c` | 4 | 3 |
| `core/array.c` | 0 | 0 |
| `core/compression.c` | 101 | 76 |
| `core/crash.c` | 58 | 44 |
| `core/hashmap.c` | 11 | 11 |
| `core/logger.c` | 25 | 20 |
| `core/math3d.c` | 0 | 0 |
| `core/mutex.c` | 4 | 4 |
| `core/offset_allocator.c` | 20 | 16 |
| `core/path.c` | 1 | 1 |
| `core/repository.c` | 174 | 165 |
| `core/resource_asset_builtins.c` | 166 | 144 |
| `core/resource_assets.c` | 126 | 110 |
| `core/resource_assets_types.c` | 18 | 0 (all const tables, §1.6) |
| `core/resource_serialize.c` | 43 | 35 |
| `core/serialization.c` | 109 | 109 |
| `core/stacktrace.c` | 25 | 21 |
| `core/test.c` | 3 | 1 |
| `core/thread.c` | 4 | 4 |
| **Total** | **1027** | **871** (792 defs + 28 decls = 820… remainder are multi-line signature lines of those defs) |

Multi-line static function signatures (definition lines that continue past the
`grep` line — they are functions, not state, but are listed so every grep hit
is traceable): `core/resource_assets.c:471, 837, 1547, 2265`
(`resource_assets_create`, `create_unique_asset_name`, `create_imported_asset_wrapper`,
`ra_test_asset_name_fn`), `core/repository.c:125, 2866` (`sk_repo_scope_push_change`),
`core/repository.c:1140, 1348` (`sk_repo_propagate_prototype_changes`),
`core/resource_asset_builtins.c:1425` (`bi_assert_import`).

(Exact arithmetic: `resource_assets_types.c` and the macro lines
`resource_asset_builtins.c:113/118`, the 9 multi-line function-signature lines
above, and the `test.h` SK_TEST macros `core/test.h:70/75/90` account for the
difference between 207 variable-like lines and 194 real declarations.)

Verification: `grep -rn '^static\|^\s*static ' app core` → 1027 hits. Every hit
is either (a) listed in §1.1–§1.6, (b) a static function definition/declaration
(counted in §1.7), or (c) the `test.h` SK_TEST macro bodies. No entry is missing.

---

## 2. Process-wide API accessors (`sk_*_api(void)`) and call sites

Exactly **six** process-wide accessors exist. Each returns a single immutable
table for the whole process/module-instance; each is the "global API" the
refactor wants to turn into an explicit, context-passed table.

| accessor | declaration | definition | returns |
|---|---|---|---|
| `sk_app_api(void)` | `core/app.h:174` | `app/app.c:236` (`static const sk_app_api_t app_api`, app.c:174) | app table (registry, plugins, timing, shutdown) |
| `sk_logger_api(void)` | `core/logger.h:118` | `core/logger.c:308` (`logger_api`, logger.c:298) | this module instance's table, or `bound_api` when bound |
| `sk_filesystem_api(void)` | `core/filesystem.h:333` | `app/filesystem_unix.c:551` / `app/filesystem_win32.c:534` | host fs table |
| `sk_platform_api(void)` | `core/platform.h:99` | `app/platform_unix.c:107` / `app/platform_win32.c:100` | host platform table |
| `sk_repository_api(void)` | `core/repository.h:818` | `core/repository.c:3180` (`repository_api`, repository.c:3091) | repository table |
| `sk_resource_assets_api(void)` | `core/resource_assets.h:267` | `core/resource_assets.c:1765` | resource-assets engine table |

Related free functions (not `_api(void)` but part of the same surface):
`sk_logger_bind_api(const sk_logger_api_t*)` — decl `core/logger.h:128`, def
`core/logger.c:304` (writes the `bound_api` static, §1.1); `sk_logger_get_api(out)`
— decl `core/logger.h:134`, def `core/logger.c:316` (copy helper, used at
`core/logger.c:468` in a test).

### 2.1 Call-site census (repo-wide, `--include='*.c' --include='*.h'`, excluding `thirdparty/` and build dirs)

Raw `sk_*_api()` grep hits: **552** — this includes the 18 declaration/definition
lines of §2 (`sk_app_api`/`sk_logger_api`/`sk_filesystem_api`/`sk_platform_api`/
`sk_repository_api`/`sk_resource_assets_api` × decl+def, plus
`sk_logger_bind_api`/`sk_logger_get_api` decl+def) and doc-comment mentions in
headers (e.g. `plugins/ui/ui.h`); pure call sites ≈ 500. Per accessor × module:

| module | sk_app_api | sk_logger_api | sk_filesystem_api | sk_platform_api | sk_repository_api | sk_resource_assets_api |
|---|---|---|---|---|---|---|
| `app/` (app.c + backends) | 64 | 25 | 15 | 13 (11 app.c + 2 backend defs) | 0 | 0 |
| `core/` | 6 (all test-region) | 21 (self + engine + logger.h decl) | 28 (engine + filesystem.h decl) | 1 (platform.h decl) | 177 (self + engine + repository.h decl) | 20 (self + builtins + resource_assets.h decl) |
| `player/` | 3 | 12 | 1 | 0 | 0 | 0 |
| `editor/` | 7 | 5 | 6 | 0 | 4 | 6 |
| `plugins/` | 0 (types only) | 26 | 2 (doc comments in ui.h) | 0 | 0 | 0 |
| `tests/` (incl. integration) | 40 | 3 | 59 | 2 | 6 | 0 |
| **totals** | **120** | **92** | **111** | **16** | **187** | **26** |

`sk_app_api()` in `core/` is test-only: `core/resource_assets.c:2039` and
`core/resource_asset_builtins.c:1289/1291` (both inside the `#ifdef SK_TESTS`
regions `resource_assets.c:1800-2893` and `resource_asset_builtins.c:1246-1559`).

Representative assignment-style call sites (the `X = sk_Y_api();` pattern the
task asks to grep for):

- `app/app.c:75` `const sk_logger_api_t* logger_api = sk_logger_api();`
- `app/app.c:380` `const sk_filesystem_api_t* fs = sk_filesystem_api();`
- `app/app.c:787` `const sk_platform_api_t* api = sk_platform_api();`
- `app/app.c:531` `const sk_logger_api_t* logger_api = sk_logger_api();`
- `app/app.c:1088` `const sk_app_api_t* api = sk_app_api();`
- `core/resource_asset_builtins.c:32` `const sk_repository_api_t* repo = sk_repository_api();`
- `core/resource_serialize.c:93` (representative of 93 sites) `const sk_repository_api_t* repo = sk_repository_api();`
- `editor/project.c:42` `const sk_repository_api_t* repo_api = sk_repository_api();`
- `editor/project.c:60` `const sk_resource_assets_api_t* assets_api = sk_resource_assets_api();`
- `player/main.c` — 12 `sk_logger_api()`, 3 `sk_app_api()`, 1 `sk_filesystem_api()` sites
- `tests/main.c:1070-1081` — table shape assertions on `sk_app_api()`

---

## 3. app <-> core dependency edges

### 3.1 app → core (the intended direction)

**Headers** (included by `app/*.c`): `app.h`, `platform.h`, `filesystem.h`,
`logger.h`, `allocator.h`, `array.h`, `crash.h`, `hashmap.h`, `path.h`
(`core/test.h` only under `SK_TESTS`, `app/app.c:731`).

Per file:
- `app/app.c` — app.h, allocator.h, array.h, crash.h, filesystem.h, hashmap.h, logger.h, path.h, platform.h (+ test.h, and plugin headers platform_window.h/entities.h/dxc_compiler.h/render_graph.h/render_pipeline.h under SK_TESTS, :731-741)
- `app/platform_unix.c` — platform.h, app.h
- `app/platform_win32.c` — platform.h, app.h
- `app/filesystem_unix.c` — filesystem.h
- `app/filesystem_win32.c` — filesystem.h

**Symbols consumed from core**: `sk_logger_api()` / `sk_logger_bind_api` /
`SK_LOGGER_API_TYPE_ID` (logger.h), `sk_platform_api()` (platform.h),
`sk_filesystem_api()` (filesystem.h), `sk_allocator_default()` (allocator.h),
hashmap/array/path helpers, `sk_crash_install/uninstall` (crash.h),
`sk_app_*` declarations implemented in app itself (app.h — see §3.3).

**CMake**: `app/CMakeLists.txt:20` `target_link_libraries(sk-app PUBLIC sk-core)`;
include dirs PUBLIC = app dir (no headers there); PRIVATE plugin header dirs
`plugins/profiler`, `plugins/render_device` (:25-28). `sk-app-tests` mirrors the
same with `sk-core-tests` and the plugin header dirs (:38-63).

### 3.2 core → app

- `core/app.h` **lives in core** and defines the app surface: `sk_app_context_t`
  (opaque), `sk_app_api_t` (:41-149), and free functions `sk_app_create/destroy/
  api/startup/init/tick/run` (:150-232). Per its own doc comment (:7-10) and
  AGENTS.md ("core declares; app implements"): the **types** are core's; the
  **implementations** live in `sk-app` (`app/app.c`).
- `core/resource_assets.h:6` and `core/resource_asset_builtins.h:9` both
  `#include "app.h"` — so core's public header surface pulls in the app API
  types. (Include-graph-wise this is core→core; symbol-implementation-wise it is
  core headers → app implementation.)
- **Production core `.c` code calls zero app symbols** (verified: every
  `sk_app_*` reference in core .c is inside `#ifdef SK_TESTS`).
- **Test builds create a latent core→app link edge**: `sk-core-tests` compiles
  `resource_assets.c` (test region) and `resource_asset_builtins.c` (test
  region) which call `sk_app_create/sk_app_destroy/sk_app_api`
  (`resource_assets.c:2037-2039, 2124, 2538-2888`; `resource_asset_builtins.c:
  1289-1291, 1301, 1418`). `sk-core-tests` alone has unresolved references to
  sk-app symbols; they resolve only in the `sk-tests` executable, which
  whole-archives **both** `sk-core-tests` and `sk-app-tests`
  (`tests/CMakeLists.txt:97-115`). CMake-wise `sk-core-tests` does not link
  sk-app-tests (no declared cycle), but at the symbol level the cycle exists in
  test builds only.

### 3.3 The one real cycle and why it survives

`sk_app_api()` (and `sk_app_init/tick/run`) are declared in core (`app.h`) and
implemented in app (`app.c`), while `app.c` also consumes core. The cycle is
broken today only by convention + link order:
- production: `sk-app → sk-core` (one direction; core never references app
  symbols), so no link cycle.
- test: `sk-tests` whole-archive links both static libs; the undefined
  `sk_app_*` refs in `sk-core-tests` are satisfied by `sk-app-tests` objects.

**Runtime cycle by design**: every plugin DLL statically links its own copy of
`sk-core` (AGENTS.md: "Plugins are SHARED libraries that statically link
sk-core"). The host (sk-app) resolves `sk_logger_bind_api` in each plugin at
load (`app/app.c:558-561`) and binds the host `sk_logger_api_t` in — the
`bound_api` static (§1.1) is exactly this per-module-instance indirection.
`cmake/cmake_functions.cmake:174-183` force-exports `sk_logger_bind_api` from
each plugin so the host can find it.

---

## 4. Consumers of the API symbols

| consumer | what it consumes | how it links |
|---|---|---|
| `app/app.c` (sk-app) | implements `sk_app_api`; calls `sk_logger_api`, `sk_filesystem_api`, `sk_platform_api`; binds `sk_logger_bind_api` into plugins (:558-561) | links `sk-core` PUBLIC |
| `app/platform_*.c`, `app/filesystem_*.c` | implement `sk_platform_api` / `sk_filesystem_api`; call `sk_allocator_default`, core helpers | via sk-app |
| `core/logger.c`, `core/repository.c`, `core/resource_assets.c`, `core/resource_asset_builtins.c`, `core/resource_serialize.c`, `core/resource_assets_types.c` | implement `sk_logger_api`, `sk_repository_api`, `sk_resource_assets_api`; consume each other's accessors internally | sk-core itself |
| `player/main.c` (sk-player) | `sk_app_api`, `sk_logger_api`, `sk_filesystem_api` | `sk-player` → `sk-app` (player/CMakeLists.txt) |
| `editor/{main,console_panel,editor_ui_host,project}.c` (sk-editor) | `sk_app_api`, `sk_logger_api`, `sk_filesystem_api`, `sk_repository_api`, `sk_resource_assets_api` | `sk-editor-lib` → `sk-app` (editor/CMakeLists.txt) |
| plugins (9 shared libs: platform_window, render_device, entities, vulkan_render_device, dxc_compiler, test_render_device, render_graph, profiler, ui) | `sk_logger_api` (ui, profiler, dxc_compiler, vulkan_render_device); app.h **types** (`sk_app_context_t`, `sk_app_api_t`) passed in at entry; core headers (allocator/common/repository/…) | each plugin links `sk-core` PRIVATE (sk_add_plugin); **never** sk-app |
| `tests/main.c` + `tests/integration/*` (sk-tests) | all six accessors (see §2.1 table) | `sk-tests` → `sk-app-tests` + `sk-editor-tests` + plugin `-lib` interfaces, whole-archive |
| `tests/header_checks/compression_header.c` | core/compression.h self-containment (OBJECT lib, compile-only) | sk-header-checks |
| `tests/conformance/compression_mapping.c` | production `sk-core` symbol surface | sk-compression-conformance |
| `tests/crash_trigger/main.c` | crash handler | sk-crash-trigger → sk-core |
| `tests/msdf_atlas_smoke.cpp` | atlas tooling | sk-msdf-atlas-smoke |
| examples/ | **none** — no `examples/` directory exists in this repo | — |

Plugins deliberately call `sk_*_api()` only for the logger; they receive the app
table as a parameter from the host (`sk_plugin_entry_point(context, app_api)`,
e.g. `plugins/platform_window/plugin_entry_point.c:20`), and get all other
core tables either as parameters (e.g. `sk_filesystem_api_t* fs` params in
`plugins/ui/ui.h:1778/1915/1930/2392`) or via their private static core copy.

---

## 5. Build targets and install/export rules

Target order (root `CMakeLists.txt:261-266`): `core`, `app`, `editor`, `player`,
`plugins`, `tests`. Third-party first (:111). No package config, no CPack, no
first-party `install()` / `EXPORT` anywhere — the only install rules in the tree
belong to vendored `thirdparty/glfw` (`thirdparty/glfw/src/CMakeLists.txt:362-363`,
`thirdparty/glfw/CMakeLists.txt:141-151`). Root sets only
`CMAKE_INSTALL_RPATH` (`CMakeLists.txt:88, 93`) with no install targets.

| target | type | sources | defines/links | notes |
|---|---|---|---|---|
| `sk-core` | STATIC | all `core/*.c` except `test.c` (core/CMakeLists.txt:7-14) | PRIVATE mimalloc-static, yyjson, (zstd/lz4/miniz if enabled); PUBLIC Threads, m, dl, dbghelp | PIC ON; include dir PUBLIC = core/ |
| `sk-core-lib` | INTERFACE | — | include dir core/ | header-only alias (core/CMakeLists.txt:77-79) |
| `sk-test` | STATIC | `core/test.c` only | SK_TESTS PUBLIC, unity | the unit-test registry (core/CMakeLists.txt:84-89) |
| `sk-core-tests` | STATIC | all core sources **with SK_TESTS** | SK_TESTS, links sk-test + same privates | test twin of sk-core (core/CMakeLists.txt:91-110) |
| `sk-app` | STATIC | `app/*.c`, platform/filesystem backend picked per-OS (app/CMakeLists.txt:1-16) | PUBLIC sk-core, CMAKE_DL_LIBS, shell32; PRIVATE plugin header dirs profiler/render_device | include dir PUBLIC = app/ (no headers live there) |
| `sk-app-tests` | STATIC | same sources **with SK_TESTS** | links sk-core-tests; PRIVATE plugin header dirs incl. platform_window, entities, dxc_compiler, render_graph, render_device, profiler | (app/CMakeLists.txt:32-63) |
| `sk-player` | EXE | `player/*` | PRIVATE sk-app; APPLE `-export_dynamic`; plugin include dirs | player/CMakeLists.txt |
| `sk-editor-lib` | STATIC | `editor/*.c` minus main.c | PUBLIC sk-app + plugin includes | editor/CMakeLists.txt |
| `sk-editor` | EXE | `editor/main.c` | sk-editor-lib | |
| `sk-editor-tests` | STATIC | editor sources with SK_TESTS | sk-app-tests + sk-test | |
| 9 × `sk-<plugin>` | SHARED | per plugin | PRIVATE sk-core; forced export of `sk_logger_bind_api`; output to `bin/plugins/` | `sk_add_plugin` (cmake/cmake_functions.cmake:133-203) |
| 9 × `sk-<plugin>-lib` | INTERFACE | header export for host/tests | include dir = plugin dir | |
| `sk-tests` | EXE | `tests/main.c` | whole-archive sk-core-tests, sk-app-tests, sk-editor-tests, sk-test; plugin `-lib`s; adds plugin build deps | runs from `bin/` (tests/CMakeLists.txt:61-131) |
| `sk-header-checks` | OBJECT | `tests/header_checks/compression_header.c` | include core/ | APX-169 self-containment check |
| `sk-compression-conformance` | EXE | `tests/conformance/compression_mapping.c` | production sk-core | APX-171 |
| `sk-crash-trigger` | EXE | `tests/crash_trigger/main.c` | sk-core | crash demo tool |
| `sk-msdf-atlas-smoke` | EXE | `tests/msdf_atlas_smoke.cpp` | msdf-atlas tooling | |

**Public-header flow**: there are no per-module "exported header sets". Core's
headers are public via `target_include_directories(sk-core PUBLIC core/)` and
every consumer (app, plugins, player, editor, tests) adds core headers directly.
The app module ships **no headers**; its public surface is `core/app.h`
(types + prototypes) implemented by `sk-app`. Plugins' public headers are
exposed through the `sk-<name>-lib` INTERFACE targets and copied include dirs in
hosts (player/editor/tests). No `install(FILES ...)` exists for any first-party
header; headers are consumed in-tree only.

**Header hygiene gates**: `sk_check_header_isolation` (cmake/cmake_functions.cmake)
fails configure if any first-party header includes OS threading/mutex headers;
`sk_embed_type_ids_in_dir` rewrites `SK_TYPE_ID("name")` → hashed form in
app/core/player/editor/plugins/tests sources at configure time
(root CMakeLists.txt:52-63).

---

## 6. OPEN QUESTIONS — state that cannot trivially become context-owned

These are flagged for the refactor to decide, not decided here.

### 6.1 Logger host-bind indirection (`bound_api`)
The `bound_api` static exists because each plugin DLL has a private `sk-core`
copy (its own sinks + `stdout_sink`). The host injects its table per-DLL at load
(`app/app.c:558-561`, forced export in `cmake_functions.cmake:174-183`). If the
logger becomes context-owned, decide: (a) one logger per context passed
everywhere (removes bind entirely but requires threading the logger into every
plugin entry — entry signature change), or (b) keep per-DLL default + bind as
the migration path. This also touches the `FILE*` sinks in
`core/logger.c` (`sk_log_file_sink_t` holds a `FILE*` — C-runtime global state;
who owns the `FILE*` lifecycle across plugin unload?).

### 6.2 Crash handler / stacktrace state (crash.c, stacktrace.c)
Signal dispositions (`sigaction`), `SetUnhandledExceptionFilter`,
`AddVectoredExceptionHandler`, the CRT invalid-parameter/purecall hooks
(`_set_invalid_parameter_handler`, `_set_purecall_handler`), and DbgHelp
`SymInitialize` are all **process-global by OS definition** — there is one
handler chain per process, not per context. `crash_install_count`
(`crash.c:653`) makes install/uninstall nestable, but two independent contexts
cannot own two crash handlers. The refactor must define who owns crash
install/uninstall at process scope (host only?) and whether DbgHelp sessions
can be per-context (they can — `SymInitialize(hProcess)` is per-process handle,
but multiple sessions are not supported; see 7.4).

### 6.3 Third-party library globals
- **mimalloc** (`core/allocator_mimalloc.c:21-33`): the default heap is the
  process global `mi_heap` with per-thread caches; `mimalloc_allocator` has
  `NULL` per-instance state by design. A context-owned heap requires
  `mi_heap_new()` per context and routing `sk_allocator_t.instance`. Costs:
  every allocation site already passes the allocator (good), but "default
  allocator" call sites (`sk_allocator_default()`, e.g. `app/app.c:181`,
  `core/compression.c:1244`) are process-wide by contract.
- **yyjson** (`core/serialization.c`): has a global default allocator
  (`yyjson_set_mem_alloc`); `serialization.c` already injects a per-doc custom
  allocator (`json_yy_alc`, :560) but the global remains settable.
- **zstd** (`core/compression.c:124`): per-context `ZSTD_customMem` is already
  injected; internal FSE/Huffman tables are const. Low risk.
- **miniz / lz4**: miniz routes through global `MZ_MALLOC/MZ_FREE` macros
  (default libc malloc) when used without an explicit allocator — verify every
  miniz entry point used by compression.c passes state; otherwise it is a
  libc-singleton dependency.
- **vendored UI/test deps** (unity, clay, stb_image*, freetype, msdf-atlas-gen,
  nativefiledialog, vma, volk, vulkan headers): plugin/editor-scope, but each
  has its own process-global state (e.g. unity's per-TU registry, stb's global
  allocators) if ever pulled into core.

### 6.4 DbgHelp single-session constraint (`core/stacktrace.c:111-113`)
DbgHelp allows one `SymInitialize` per process by default
(`SymInitialize` is documented as usable once per process without
`SYMOPT_*`/`SymInitializeW` re-entry tricks). The win32 statics
(`dbghelp_cs`, `dbghelp_cs_ready`, `dbghelp_initialized`) guard that single
session. Making stacktrace context-owned means either (a) serializing access to
one process-wide session (what exists today, just relocated), or (b) using
per-context `SymInitialize` + `SymCleanup` with the documented per-process
limits. Needs a spike before committing to (b).

### 6.5 Thread-local state (`platform_err`, `app/platform_{unix,win32}.c:19`)
TLS is per-thread, not per-context: two contexts on one thread share the buffer,
one context on two threads gets two buffers. If platform error strings must be
context-scoped, the API needs an explicit out-buffer (`sk_platform_error(ctx,
buf, cap)`) or the TLS buffer must be keyed by context pointer. Also flag
`app/platform_win32.c:78` (`QPC freq` lazy-init) — trivially movable to a
context, but it is per-process invariant data (perf-counter frequency), so
per-context copies are wasteful; keep as module-level `const` after first init
or accept the tiny race.

### 6.6 C-runtime singletons (inherent, cannot own)
`stdout`/`stderr` `FILE*` (`logger.c` stdout sink, `crash.c` stderr writer via
`GetStdHandle`), `time(NULL)`/`localtime_r` (`logger.c:41-52`), `fopen`
rotation (`file_sink_rotate`), `snprintf`, libc `malloc/free` inside zstd/miniz
default paths, and the CRT invalid-parameter handler. These are process
singletons; "context-owned" for them means *explicitly injected at the boundary*
(allocator tables, FILE* sinks owned by the logger context), not removal.

### 6.7 Test-harness globals
`core/test.c:35-36` (registry), `SK_TEST` constructor registration
(`core/test.h:65-90`, `__attribute__((constructor))` / MSVC `CRT$XCU`), the
per-TU `tests[]` in each compiled unit, and all §1.3/§1.4 `[T]` counters are
global per test binary. A context-owned test registry means the Unity runner
host (`tests/main.c`) owns the registry and tests register into it explicitly —
a cross-cutting change to the SK_TEST macro contract.

---

## 7. Refactor checklist (derived from this inventory)

1. Kill the five known offenders (§1.1): sinks/sink_count/module_ready/bound_api
   → context logger; builtins_repository → field on `sk_resource_assets_context_t`.
2. Move `temp_override`, `view_list` (both platforms) onto a filesystem state
   object (§1.2); fix the view_list leak.
3. Decide ownership for crash/stacktrace/DbgHelp (§6.2/6.4) — likely remains
   process-scope with a single owner (sk-app), made explicit in headers.
4. Thread one `sk_logger_t*` (and where needed `sk_fs_*`/`sk_platform_*` state)
   through `sk_app_context_t` → plugin entry → core engine calls, replacing
   `sk_logger_api()`/`sk_filesystem_api()`/`sk_platform_api()` call sites
   (§2.1: 120 + 111 + 16 sites).
5. Move `core/app.h` free functions into core-owned impl or make app.h a pure
   interface header; break the test-only core→app link edge (§3.2) by moving
   `sk_app_*`-dependent tests out of `resource_assets.c` /
   `resource_asset_builtins.c` test regions.
6. Revisit `sk_logger_bind_api` + per-DLL static core (AGENTS.md) — the
   deepest process-wide coupling; see §6.1.
7. Decide test-registry ownership (§6.7) if sk-test must become context-owned.
