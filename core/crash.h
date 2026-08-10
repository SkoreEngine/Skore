#pragma once

/**
 * @file crash.h
 * @brief Fatal crash handler: prints a stacktrace to stderr, then lets the
 * platform terminate the process normally so the exit status, core dump, and
 * OS crash reporting (WER) are preserved.
 *
 * Pure engine utility implemented in sk-core. sk_crash_install registers
 * POSIX sigaction handlers (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT) on a
 * dedicated sigaltstack so stack-overflow faults still produce output, or the
 * Win32 unhandled-exception filter (plus a first-chance vectored observer and
 * the CRT invalid-parameter / purecall hooks), and reserves the static state
 * the handler uses. sk_crash_uninstall restores the previous handlers.
 *
 * The crash path is deliberately fault-safe: it writes directly to stderr
 * through pre-allocated static buffers and never allocates or calls printf on
 * POSIX. Frames are captured via sk_stacktrace_capture (capture-only, no
 * allocation) and printed as raw addresses; symbolization is intentionally
 * skipped inside the handler because dladdr/DbgHelp are not fault-safe.
 *
 * sk_app_init installs the handler at application startup. Embedders and
 * tests that want to opt out call sk_crash_uninstall (idempotent), and tests
 * that want to verify a fault run their own child process.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Install the crash handler: reserve the handler's static state and register
 * the platform fatal-fault handlers (POSIX signals / Win32 unhandled filter).
 * Idempotent: repeated calls while installed are no-ops.
 *
 * @return 0 on success; non-zero when the platform refused installation
 *         (e.g. sigaltstack/sigaction failed). On failure the process state
 *         is left unchanged (previous handlers restored).
 */
i32 sk_crash_install(void);

/**
 * Uninstall the crash handler installed by sk_crash_install and restore the
 * previously registered handlers / alternate stack. Safe to call when never
 * installed and more than once.
 */
void sk_crash_uninstall(void);

#ifdef __cplusplus
}
#endif
