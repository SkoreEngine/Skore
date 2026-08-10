/**
 * @file main.c
 * @brief Opt-in crash-trigger tool (BUILD_TESTING only).
 *
 * Deliberately raises each fault kind the crash handler covers so the handler
 * can be exercised without shipping a crashing path in production binaries.
 *
 * Usage:
 *   sk-crash-trigger <kind>
 *
 * Kinds (portable names first; platform aliases accepted):
 *   null | segfault | access-violation  — null pointer dereference
 *   abort                               — abort() / SIGABRT
 *   fpe | divide-by-zero                — floating/integer divide fault (raise)
 *   ill | illegal-instruction           — illegal instruction (raise / opcode)
 *   bus                                 — bus error (POSIX raise only)
 *
 * The process installs sk_crash_install(), triggers the fault, and never
 * returns on success. Exit code 2 means bad usage; 1 means install failed.
 */

#include "crash.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 /* NOLINT(bugprone-reserved-identifier): Win7 floor */
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stdlib.h>
#include <windows.h>
#if defined(_MSC_VER)
#include <crtdbg.h> /* _CrtSetReportMode — silence Debug CRT abort/assert UI */
#endif

#else /* POSIX */

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#endif

#if defined(__GNUC__) || defined(__clang__)
#define CRASH_TRIGGER_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define CRASH_TRIGGER_NOINLINE __declspec(noinline)
#else
#define CRASH_TRIGGER_NOINLINE
#endif

/** Volatile null target so optimizers cannot eliminate the dereference. */
static u64* volatile null_target;

/** Separate noinline load so -Wnull-dereference cannot see a constant NULL. */
CRASH_TRIGGER_NOINLINE
static void crash_trigger_fault_at(const volatile u64* addr) {
	(void)*addr;
}

CRASH_TRIGGER_NOINLINE
static void crash_trigger_null_deref(void) {
	null_target = NULL;
	crash_trigger_fault_at(null_target);
}

CRASH_TRIGGER_NOINLINE
static void crash_trigger_abort(void) {
	abort();
}

#if defined(_WIN32)

CRASH_TRIGGER_NOINLINE
static void crash_trigger_fpe(void) {
	/* RaiseException is the portable Win32 way to inject a divide-by-zero
	 * exception without relying on C undefined-behaviour divide. */
	RaiseException(0xC0000094u /* STATUS_INTEGER_DIVIDE_BY_ZERO */, 0u, 0u, NULL);
}

CRASH_TRIGGER_NOINLINE
static void crash_trigger_ill(void) {
	RaiseException(0xC000001Du /* STATUS_ILLEGAL_INSTRUCTION */, 0u, 0u, NULL);
}

CRASH_TRIGGER_NOINLINE
static void crash_trigger_bus(void) {
	/* No dedicated bus-error exception on Win32; map to access violation. */
	RaiseException(0xC0000005u /* STATUS_ACCESS_VIOLATION */, 0u, 0u, NULL);
}

#else /* POSIX */

CRASH_TRIGGER_NOINLINE
static void crash_trigger_fpe(void) {
	/* raise() exercises the SIGFPE handler without depending on CPU FP traps
	 * (many builds keep FE_DIVBYZERO masked). */
	(void)raise(SIGFPE);
}

CRASH_TRIGGER_NOINLINE
static void crash_trigger_ill(void) {
	(void)raise(SIGILL);
}

CRASH_TRIGGER_NOINLINE
static void crash_trigger_bus(void) {
	(void)raise(SIGBUS);
}

#endif

static void print_usage(const char* argv0) {
	fprintf(stderr,
			"usage: %s <kind>\n"
			"  kinds: null|segfault|access-violation|abort|fpe|divide-by-zero|"
			"ill|illegal-instruction|bus\n"
			"  Installs the skore crash handler then raises the requested fault.\n"
			"  Test-only tool (BUILD_TESTING); not shipped with production binaries.\n",
			argv0);
}

int main(int argc, char* argv[]) {
	if (argc != 2) {
		print_usage(argc > 0 ? argv[0] : "sk-crash-trigger");
		return 2;
	}

#if defined(_WIN32)
	/*
	 * Headless/CI: after the unhandled-exception filter prints and returns
	 * EXCEPTION_CONTINUE_SEARCH, Windows (and the MSVC Debug CRT on abort)
	 * may otherwise show a dialog or wait on WER and never exit. Suppress
	 * those boxes so the process terminates and the parent pipe unblocks.
	 */
	(void)SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#if defined(_MSC_VER)
	(void)_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
	(void)_CrtSetReportMode(_CRT_WARN, 0);
	(void)_CrtSetReportMode(_CRT_ERROR, 0);
	(void)_CrtSetReportMode(_CRT_ASSERT, 0);
#endif
#endif
#endif

	const char* kind = argv[1];
	if (sk_crash_install() != 0) {
		fprintf(stderr, "sk-crash-trigger: sk_crash_install failed\n");
		return 1;
	}

	if (strcmp(kind, "null") == 0 || strcmp(kind, "segfault") == 0 || strcmp(kind, "access-violation") == 0) {
		crash_trigger_null_deref();
	} else if (strcmp(kind, "abort") == 0) {
		crash_trigger_abort();
	} else if (strcmp(kind, "fpe") == 0 || strcmp(kind, "divide-by-zero") == 0) {
		crash_trigger_fpe();
	} else if (strcmp(kind, "ill") == 0 || strcmp(kind, "illegal-instruction") == 0) {
		crash_trigger_ill();
	} else if (strcmp(kind, "bus") == 0) {
		crash_trigger_bus();
	} else {
		print_usage(argv[0]);
		return 2;
	}

	/* Unreachable when the fault is delivered. */
	fprintf(stderr, "sk-crash-trigger: fault did not terminate the process\n");
	return 1;
}
