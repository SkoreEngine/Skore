# Foundation refactor — migration note (APX-286)

Downstream hosts, plugins, and tests that still call the pre-refactor
`sk-core` / `sk-app` surface will not compile. There are no compatibility
shims. The contract is the public headers under `foundation/` (sketches in
`docs/foundation/include/`; design in `docs/foundation-refactor-design.md`).

## What changed

`app/` and `core/` are gone. One static library, **`sk-foundation`**, owns
engine utilities and host lifecycle. Public include style is unchanged
(`#include "app.h"`). CMake: `add_subdirectory(foundation)`; link
`sk-foundation` (production) or `sk-foundation-tests` (test host). Deleted
targets: `sk-core`, `sk-app`, `sk-core-tests`, `sk-app-tests`.

Plugins still statically link `sk-foundation` and still must not call
`sk_app_init`. They receive `(context, app_api)` at `sk_plugin_entry_point`.

## Breaking API

Deleted free-function accessors (use `sk_app_boot_t.api` / table methods):

| Deleted | Replacement |
|---|---|
| `sk_app_api()` | `boot.api` from `sk_app_init` / `sk_app_startup` / `sk_app_create` |
| `sk_logger_api()` / `sk_logger_bind_api` / `sk_logger_get_api` | `app_api->logger_api(ctx)` |
| `sk_repository_api()` | `app_api->repository_api(ctx)` |
| `sk_filesystem_api()` / `sk_filesystem_get_api` | `app_api->filesystem_api(ctx)` |
| `sk_platform_api()` / `sk_platform_get_api` | `app_api->platform_api(ctx)` |
| `sk_resource_assets_api()` | `app_api->resource_assets_api(ctx)` |
| `sk_app_destroy` | `sk_app_shutdown` |
| `sk_resource_asset_builtins_bind_repository` | assets context owns the builtins repository |

`sk_app_init(argc, argv)` (and `create` / `startup`) now returns both the
context and the immutable table:

```c
/* before */
sk_app_context_t* ctx = sk_app_init(argc, argv);
const sk_app_api_t* app_api = sk_app_api();
const sk_logger_api_t* logger_api = sk_logger_api();
sk_app_destroy(ctx);

/* after */
sk_app_boot_t boot = sk_app_init(argc, argv);
sk_app_context_t* ctx = boot.context;
const sk_app_api_t* app_api = boot.api;
if (ctx == NULL) {
    return 1; /* boot.api is also NULL */
}
const sk_logger_api_t* logger_api = app_api->logger_api(ctx);
sk_logger_context_t* log_ctx = app_api->logger_context(ctx);
sk_logger_t* log = logger_api->create_logger(log_ctx, "app");
/* ... */
logger_api->destroy_logger(log_ctx, log);
sk_app_shutdown(ctx);
```

On failure both `boot.context` and `boot.api` are NULL
(`sk_app_boot_failed()`). Success never mixes a NULL field.

`sk_logger_context_t` holds the former logger file-scope state (`sinks[]`,
`sink_count`; `module_ready` / `bound_api` are deleted). It is reachable
from the host table as `app_api->logger_context(ctx)`. Every logger table
entry takes that context first. Plugins must not keep a private sink list
or call a bind helper.

Serialize / type-register helpers take `const sk_repository_api_t* repo_api`
after `repository`. The two file I/O helpers also take
`const sk_filesystem_api_t* fs`.

CMake no longer force-exports `sk_logger_bind_api` from plugins.

## Escalations (not patched in this task)

APX-286 verifies the completed refactor. Residual gaps are listed here
instead of being papered over.

1. **Filesystem engine state is still file-scope.** Design §1.2 / data-model
   required `temp_override`, `temp_override_set`, and `view_list` to live on
   `sk_filesystem_context_t`. A context object exists (`filesystem.c`) but
   only stores an allocator. The backends still use process statics
   (`foundation/filesystem_unix.c`, `foundation/filesystem_win32.c`),
   grandfathered in `no-statics-allowlist.txt`. Multi-context tests can
   still leak temp override and mmap views across boots.

2. **Other foundation file-scope statics remain on the allowlist.** They
   match the design exceptions: crash/DbgHelp process ownership (§6.2,
   §6.4), TLS `platform_err` (§6.5), SK_TESTS-only fixtures (§6.7), and
   builtin handler/importer tables that are never mutated after
   registration. These are not the deleted logger/repository accessors.

3. **SK_TESTS helpers cache table pointers in function-scope statics**
   (`sk_test_filesystem_table` / `sk_test_platform_table` in `app.h`). The
   no-statics guard only scans column-0 declarations. Out of this
   verification's patch scope.

Do not add new file-scope mutable statics in `foundation/` or `plugins/`.
`scripts/check-no-statics.py` (ctest `sk-no-statics-guard`) fails on
unguarded statics and on any reintroduction of `sk_app_api(`,
`sk_logger_api(`, or `sk_repository_api(`.

## Verification (APX-286)

Recorded on Linux x86_64 (GCC 13), CMake + Ninja, `BUILD_TESTING=ON`,
`SK_ENABLE_CLANG_TIDY=ON` (same flags as `.github/workflows/ci.yml`).
Headless Vulkan via lavapipe (`VK_ICD_FILENAMES=…/lvp_icd.json`).
This agent did not run the Windows or macOS CI cells.

| Check | Result |
|---|---|
| `app/` and `core/` directories | Absent |
| CMake targets `sk-core` / `sk-app` | Absent; `sk-foundation` is the engine target |
| Grep `sk_app_api(` / `sk_logger_api(` / `sk_repository_api(` in first-party `*.c`/`*.h` (comments/docs may still name the deleted APIs) | Zero call sites |
| `sk_app_init` returns `sk_app_boot_t` `{context, api}` | Confirmed (`foundation/app.h`) |
| `sk_logger_context_t` + `app_api->logger_context` | Confirmed; sinks live on the context |
| `python3 scripts/check-no-statics.py` | OK (112 allowlisted file-scope statics; 0 accessor hits) |
| Clean Debug configure + build | Green (307/307) |
| Clean Release configure + build | Green (307/307) |
| Full ctest Debug | 8/8 passed (630s), including `sk-no-statics-guard` |
| Full ctest Release | 8/8 passed (713s) |
| `sk-tests` Debug (cwd `{build}/bin`) | `ran=866 failed=0`; host 372 + 9 plugins loaded/unloaded |
| `sk-tests` Release | `ran=372 failed=0` (plugin `sk_plugin_run_tests` omitted, as designed) |
| Shared-state regression | `plugin_logs_reach_host_sink:PASS` |
| Plugin load/unload cycles (`sk-repro-cycles`, 3 boots) | Debug and Release: `ALL CYCLES OK`, each cycle `rc=0` |
