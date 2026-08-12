#pragma once

/**
 * @file crash.h
 * @brief Fatal crash handler: prints a stacktrace to stderr, then lets the
 * platform terminate the process normally so the exit status, core dump, and
 * OS crash reporting (WER) are preserved.
 *
 * Pure engine utility implemented in sk-foundation. sk_crash_install registers
 * POSIX sigaction handlers (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT) on a
 * dedicated sigaltstack so stack-overflow faults still produce output, or the
 * Win32 unhandled-exception filter (plus a first-chance vectored observer and
 * the CRT invalid-parameter / purecall hooks), and reserves the static state
 * the handler uses. sk_crash_uninstall restores the previous handlers.
 *
 * The crash path writes directly to stderr through pre-allocated static
 * buffers and never allocates or calls printf. Frames are captured via
 * sk_stacktrace_capture (no allocation), then best-effort symbolized with
 * sk_stacktrace_resolve (module / symbol / file:line when the backend can).
 * Resolve uses dladdr/DbgHelp and is not strictly async-signal-safe; install
 * pre-inits the stacktrace backend so the common path avoids first-use setup
 * mid-fault. Addresses always print; names are filled when resolve succeeds.
 *
 * sk_app_init installs the handler at application startup; sk_app_destroy
 * balances that with one uninstall. Install/uninstall are refcounted so a
 * process-level install (e.g. the test host) survives nested app_init/destroy.
 * Embedders that want to opt out call sk_crash_uninstall until the count hits
 * zero. Tests that want to verify a fault run their own child process.
 *
 * Deliberate fault injection for verification lives in the BUILD_TESTING-only
 * tool `sk-crash-trigger` (see tests/crash_trigger/ and docs/stacktrace.md).
 * Production binaries do not ship a crash-trigger code path.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Install the crash handler: reserve the handler's static state and register
 * the platform fatal-fault handlers (POSIX signals / Win32 unhandled filter).
 * Refcounted: the first successful install registers platform handlers; further
 * installs only increment the count (must be balanced by sk_crash_uninstall).
 *
 * @return 0 on success; non-zero when the platform refused installation
 *         (e.g. sigaltstack/sigaction failed). On failure the process state
 *         is left unchanged (previous handlers restored; count not increased).
 */
i32 sk_crash_install(void);

/**
 * Drop one install reference. When the count reaches zero, restore the
 * previously registered handlers / alternate stack. Safe to call when never
 * installed and more than once (no-op at zero).
 */
void sk_crash_uninstall(void);

#ifdef __cplusplus
}
#endif
