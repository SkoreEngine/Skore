#include "crash.h"

#include "stacktrace.h"

#include <stddef.h> /* max_align_t */
#include <string.h> /* memcpy, strlen */

/* ------------------------------------------------------------------------- */
/* Handler state (reserved at install time; the crash path never allocates)  */
/* ------------------------------------------------------------------------- */

/** Capacity of the frame array the handler captures into (536 B/frame on 64-bit). */
#define CRASH_MAX_FRAMES 64u

/** One output line; lines are flushed to stderr as they complete. */
#define CRASH_LINE_CAP 512u

static sk_stacktrace_frame_t crash_frames[CRASH_MAX_FRAMES];
static char crash_line[CRASH_LINE_CAP];
static u32 crash_line_len;

/* ------------------------------------------------------------------------- */
/* Fault-safe stderr writer (platform backends)                              */
/* ------------------------------------------------------------------------- */

#if defined(_WIN32)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 /* NOLINT(bugprone-reserved-identifier): Win7 floor, same as stacktrace.c */
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static void crash_write_stderr(const char* text, u32 len) {
	HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
	if (err != INVALID_HANDLE_VALUE && err != NULL) {
		DWORD written = 0u;
		(void)WriteFile(err, text, (DWORD)len, &written, NULL);
	}
}

#else /* POSIX */

#include <signal.h>
#include <unistd.h>

static void crash_write_stderr(const char* text, u32 len) {
	/* Capture the result so -Wunused-result (glibc fortify, -O2+) stays quiet;
	 * errors are irrelevant on the crash path. */
	ssize_t written = write(STDERR_FILENO, text, len);
	(void)written;
}

#endif

/* ------------------------------------------------------------------------- */
/* Shared line builder (no malloc, no printf — only write()/WriteFile)       */
/* ------------------------------------------------------------------------- */

static void crash_reset_line(void) {
	crash_line_len = 0u;
}

/** Append @p len bytes of @p text, bounded by the line buffer. */
static void crash_append_raw(const char* text, u32 len) {
	u32 space = CRASH_LINE_CAP - 1u - crash_line_len;
	u32 n = (len < space) ? len : space;
	if (n > 0u) {
		memcpy(crash_line + crash_line_len, text, n);
		crash_line_len += n;
	}
}

static void crash_append_cstr(const char* text) {
	crash_append_raw(text, (u32)strlen(text));
}

/** Append @p value as unsigned decimal. */
static void crash_append_u64(u64 value) {
	char tmp[20];
	u32 n = 0u;
	do {
		tmp[n] = (char)('0' + (value % 10u));
		n += 1u;
		value /= 10u;
	} while (value != 0u);
	while (n > 0u) {
		n -= 1u;
		crash_append_raw(&tmp[n], 1u);
	}
}

/** Append @p value as "0x<hex>" with leading zeros trimmed (always >= "0x0"). */
static void crash_append_hex(u64 value) {
	static const char digits[] = "0123456789abcdef";
	char tmp[2u + 16u];
	u32 n = 2u;
	u32 started = 0u;
	tmp[0] = '0';
	tmp[1] = 'x';
	for (u32 i = 0u; i < 16u; ++i) {
		u32 shift = (15u - i) * 4u;
		u8 nibble = (u8)((value >> shift) & 0xFu);
		if (nibble != 0u || started != 0u || i == 15u) {
			tmp[n] = digits[nibble];
			n += 1u;
			started = 1u;
		}
	}
	crash_append_raw(tmp, n);
}

/** Write the current line to stderr and reset the builder. */
static void crash_flush_line(void) {
	if (crash_line_len == 0u) {
		return;
	}
	crash_write_stderr(crash_line, crash_line_len);
	crash_reset_line();
}

/** Capture and print the numbered raw-address frame list. */
static void crash_print_frames(void) {
	u32 count = sk_stacktrace_capture(crash_frames, CRASH_MAX_FRAMES, 1u);

	crash_reset_line();
	crash_append_cstr("stacktrace (raw addresses):\n");
	crash_flush_line();

	for (u32 i = 0u; i < count; ++i) {
		if (crash_frames[i].address == NULL) {
			continue;
		}
		crash_reset_line();
		crash_append_cstr("#");
		crash_append_u64(i);
		crash_append_cstr(" ");
		crash_append_hex((u64)crash_frames[i].address);
		crash_append_cstr("\n");
		crash_flush_line();
	}
}

/* ------------------------------------------------------------------------- */
/* POSIX backend (Linux, Apple, BSDs): sigaction + sigaltstack               */
/* ------------------------------------------------------------------------- */

#if !defined(_WIN32)

/* <signal.h> / <unistd.h> are already included above for crash_write_stderr. */

#if defined(__linux__)
#include <sys/syscall.h>
#endif

#if defined(__APPLE__)
#include <pthread.h>
#endif

/* The fatal signals the handler covers. */
static const int crash_signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
#define CRASH_SIGNAL_COUNT (u32)(sizeof(crash_signals) / sizeof(crash_signals[0]))

static struct sigaction crash_old_actions[CRASH_SIGNAL_COUNT];
static stack_t crash_old_stack;
static u32 crash_signal_count_installed;
static i32 crash_stack_ready;

/** Reentrancy guard: a fault inside the handler must not loop forever. */
static volatile sig_atomic_t crash_active;

/** Dedicated alternate stack so stack-overflow faults still produce output. */
#define CRASH_ALT_STACK_SIZE (64u * 1024u)
static union {
	u8 bytes[CRASH_ALT_STACK_SIZE];
	max_align_t align;
} crash_alt_stack_mem;

/** Best-effort thread id (Linux tid, Apple pthread id, else process id). */
static u64 crash_thread_id(void) {
#if defined(__linux__)
	return (u64)syscall(SYS_gettid);
#elif defined(__APPLE__)
	u64 tid = 0u;
	(void)pthread_threadid_np(NULL, &tid);
	return tid;
#else
	return (u64)(long)getpid();
#endif
}

static const char* crash_signal_name(int sig) {
	switch (sig) {
	case SIGSEGV:
		return "SIGSEGV (segmentation violation)";
	case SIGBUS:
		return "SIGBUS (bus error)";
	case SIGFPE:
		return "SIGFPE (floating-point exception)";
	case SIGILL:
		return "SIGILL (illegal instruction)";
	case SIGABRT:
		return "SIGABRT (abort)";
	default:
		return "fatal signal";
	}
}

/** Human name for a kernel si_code, or NULL when unknown / user-generated. */
static const char* crash_signal_code_name(int sig, int code) {
	switch (sig) {
	case SIGSEGV:
		switch (code) {
		case SEGV_MAPERR:
			return "SEGV_MAPERR";
		case SEGV_ACCERR:
			return "SEGV_ACCERR";
		default:
			break;
		}
		break;
	case SIGBUS:
		switch (code) {
		case BUS_ADRALN:
			return "BUS_ADRALN";
		case BUS_ADRERR:
			return "BUS_ADRERR";
		case BUS_OBJERR:
			return "BUS_OBJERR";
		default:
			break;
		}
		break;
	case SIGFPE:
		switch (code) {
		case FPE_INTDIV:
			return "FPE_INTDIV";
		case FPE_INTOVF:
			return "FPE_INTOVF";
		case FPE_FLTDIV:
			return "FPE_FLTDIV";
		case FPE_FLTOVF:
			return "FPE_FLTOVF";
		case FPE_FLTUND:
			return "FPE_FLTUND";
		case FPE_FLTRES:
			return "FPE_FLTRES";
		case FPE_FLTINV:
			return "FPE_FLTINV";
		default:
			break;
		}
		break;
	case SIGILL:
		switch (code) {
		case ILL_ILLOPC:
			return "ILL_ILLOPC";
		case ILL_ILLOPN:
			return "ILL_ILLOPN";
		case ILL_ILLADR:
			return "ILL_ILLADR";
		case ILL_ILLTRP:
			return "ILL_ILLTRP";
		case ILL_PRVOPC:
			return "ILL_PRVOPC";
		case ILL_PRVREG:
			return "ILL_PRVREG";
		case ILL_COPROC:
			return "ILL_COPROC";
		case ILL_BADSTK:
			return "ILL_BADSTK";
		default:
			break;
		}
		break;
	default:
		break;
	}
	return NULL;
}

/** True when @p si_addr is meaningful for this kernel-reported fault. */
static int crash_si_has_address(int sig, int code) {
	if (code <= 0) {
		return 0;
	}
	return sig == SIGSEGV || sig == SIGBUS || sig == SIGFPE || sig == SIGILL;
}

static void crash_print_report(int sig, siginfo_t* info) {
	const char* code_name = crash_signal_code_name(sig, info->si_code);

	crash_reset_line();
	crash_append_cstr("=== skore crash handler ===\n");
	crash_flush_line();

	crash_reset_line();
	crash_append_cstr("signal: ");
	crash_append_cstr(crash_signal_name(sig));
	if (code_name != NULL) {
		crash_append_cstr(" (");
		crash_append_cstr(code_name);
		crash_append_cstr(")");
	}
	crash_append_cstr("\n");
	crash_flush_line();

	if (crash_si_has_address(sig, info->si_code)) {
		crash_reset_line();
		crash_append_cstr("faulting address: ");
		crash_append_hex((u64)(uintptr_t)info->si_addr);
		crash_append_cstr("\n");
		crash_flush_line();
	}

	crash_reset_line();
	crash_append_cstr("thread id: ");
	crash_append_u64(crash_thread_id());
	crash_append_cstr("\n");
	crash_flush_line();

	crash_print_frames();
}

/** Restore the default disposition for every covered signal (pre-re-raise). */
static void crash_restore_defaults(void) {
	for (u32 i = 0u; i < CRASH_SIGNAL_COUNT; ++i) {
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		(void)sigaction(crash_signals[i], &sa, NULL);
	}
}

/** Re-raise @p sig with its default action so exit status + core dump survive. */
static void crash_rerase(int sig) {
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, sig);
	/* SA_NODEFER keeps the signal unblocked; this also covers any other
	 * block state so the re-raise is delivered immediately. */
	(void)sigprocmask(SIG_UNBLOCK, &set, NULL);
	raise(sig);
	/* Unreachable unless the signal was ignored: keep a sane fallback. */
	_exit(128 + sig);
}

static void crash_signal_handler(int sig, siginfo_t* info, void_ptr_t ucontext) {
	(void)ucontext;

	if (crash_active != 0) {
		/* A fault inside the handler: die immediately, never loop. */
		crash_restore_defaults();
		crash_rerase(sig);
		return;
	}
	crash_active = 1;

	crash_print_report(sig, info);

	crash_active = 0;
	crash_restore_defaults();
	crash_rerase(sig);
}

static void crash_uninstall_posix(void) {
	for (u32 i = 0u; i < crash_signal_count_installed; ++i) {
		(void)sigaction(crash_signals[i], &crash_old_actions[i], NULL);
	}
	crash_signal_count_installed = 0u;
	if (crash_stack_ready != 0) {
		(void)sigaltstack(&crash_old_stack, NULL);
		crash_stack_ready = 0;
	}
	crash_active = 0;
}

static i32 crash_install_posix(void) {
	stack_t alt;
	memset(&alt, 0, sizeof(alt));
	alt.ss_sp = crash_alt_stack_mem.bytes;
	alt.ss_size = sizeof(crash_alt_stack_mem.bytes);
	alt.ss_flags = 0;
	if (sigaltstack(&alt, &crash_old_stack) != 0) {
		return -1;
	}
	crash_stack_ready = 1;

	for (u32 i = 0u; i < CRASH_SIGNAL_COUNT; ++i) {
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = crash_signal_handler;
		sigemptyset(&sa.sa_mask);
		/* SA_SIGINFO: si_code/si_addr for the report. SA_ONSTACK: run on the
		 * alternate stack. SA_NODEFER: keep the signal unblocked so the final
		 * re-raise is delivered even if the same signal recurs mid-handler. */
		sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
		if (sigaction(crash_signals[i], &sa, &crash_old_actions[i]) != 0) {
			crash_uninstall_posix();
			return -1;
		}
		crash_signal_count_installed += 1u;
	}
	return 0;
}

/* ------------------------------------------------------------------------- */
/* Win32 backend: unhandled-exception filter + vectored observer + CRT hooks */
/* ------------------------------------------------------------------------- */

#else /* defined(_WIN32) */

#include <windows.h>

#if defined(_MSC_VER)
#include <stdlib.h> /* _set_invalid_parameter_handler, _set_purecall_handler, abort */
#endif

typedef LONG(WINAPI* crash_filter_fn)(EXCEPTION_POINTERS* info);

#if defined(_MSC_VER)
typedef void(__cdecl* crash_invalid_param_fn)(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved);
typedef void(__cdecl* crash_purecall_fn)(void);
#endif

static volatile LONG crash_active;

static crash_filter_fn crash_old_filter;
static PVOID crash_vectored_handle;
#if defined(_MSC_VER)
static crash_invalid_param_fn crash_old_invalid_param;
static crash_purecall_fn crash_old_purecall;
#endif

/** Common fatal exception codes (fallback: raw hex). */
static const struct {
	u32 code;
	const char* name;
} crash_exception_names[] = {
	{0xC0000005u, "ACCESS_VIOLATION"},		{0xC0000006u, "IN_PAGE_ERROR"},			 {0xC000001Du, "ILLEGAL_INSTRUCTION"},
	{0xC000008Cu, "ARRAY_BOUNDS_EXCEEDED"}, {0xC0000094u, "INTEGER_DIVIDE_BY_ZERO"}, {0xC0000095u, "INTEGER_OVERFLOW"},
	{0xC00000B6u, "UNALIGNED_ACCESS"},		{0xC00000FDu, "STACK_OVERFLOW"},		 {0x80000003u, "BREAKPOINT"},
	{0x80000004u, "SINGLE_STEP"},
};

static const char* crash_exception_name(u32 code) {
	for (u32 i = 0u; i < (u32)(sizeof(crash_exception_names) / sizeof(crash_exception_names[0])); ++i) {
		if (crash_exception_names[i].code == code) {
			return crash_exception_names[i].name;
		}
	}
	return NULL;
}

static void crash_print_report(EXCEPTION_POINTERS* info) {
	const EXCEPTION_RECORD* record = info->ExceptionRecord;
	u32 code = (u32)record->ExceptionCode;
	const char* name = crash_exception_name(code);

	crash_reset_line();
	crash_append_cstr("=== skore crash handler ===\n");
	crash_flush_line();

	crash_reset_line();
	crash_append_cstr("exception: ");
	crash_append_hex(code);
	if (name != NULL) {
		crash_append_cstr(" (");
		crash_append_cstr(name);
		crash_append_cstr(")");
	}
	crash_append_cstr("\n");
	crash_flush_line();

	/* ACCESS_VIOLATION / IN_PAGE_ERROR: ExceptionInformation[1] is the
	 * faulting address, [0] the access kind (0 = read, 1 = write). */
	if ((code == 0xC0000005u || code == 0xC0000006u) && record->NumberParameters >= 2u) {
		crash_reset_line();
		crash_append_cstr("faulting address: ");
		crash_append_hex(record->ExceptionInformation[1]);
		crash_append_cstr("\n");
		crash_flush_line();
	}

	crash_reset_line();
	crash_append_cstr("thread id: ");
	crash_append_u64((u64)GetCurrentThreadId());
	crash_append_cstr("\n");
	crash_flush_line();

	crash_print_frames();
}

static LONG WINAPI crash_exception_filter(EXCEPTION_POINTERS* info) {
	if (crash_active != 0) {
		/* Reentrant fault while printing: do not print again; WER takes over. */
		return EXCEPTION_CONTINUE_SEARCH;
	}
	crash_active = 1;

	crash_print_report(info);

	crash_active = 0;
	/* CONTINUE_SEARCH (not EXECUTE_HANDLER) so Windows Error Reporting still
	 * runs: the process terminates with the original exception code and WER
	 * crash dumps/dialogs are not silently swallowed. */
	return EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK crash_vectored_handler(EXCEPTION_POINTERS* info) {
	(void)info;
	if (crash_active != 0) {
		/* A fault while the crash handler was printing: emit one short line
		 * and continue searching so WER terminates the process instead of
		 * retrying the faulting instruction forever. */
		crash_write_stderr("skore: fault while printing crash report\n", (u32)(sizeof("skore: fault while printing crash report\n") - 1u));
		return EXCEPTION_CONTINUE_SEARCH;
	}
	/* Observer only: never interfere with exceptions the app handles itself. */
	return EXCEPTION_CONTINUE_SEARCH;
}

#if defined(_MSC_VER)

static void crash_invalid_parameter_handler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved) {
	(void)expression;
	(void)function;
	(void)file;
	(void)line;
	(void)reserved;
	crash_write_stderr("skore: CRT invalid parameter\n", (u32)(sizeof("skore: CRT invalid parameter\n") - 1u));
	/* The CRT contract forbids returning; abort() terminates like the
	 * default handler (SIGABRT -> exit code 3). */
	abort();
}

static void crash_purecall_handler(void) {
	crash_write_stderr("skore: pure virtual function call\n", (u32)(sizeof("skore: pure virtual function call\n") - 1u));
	abort();
}

#endif /* defined(_MSC_VER) */

static i32 crash_install_win32(void) {
	crash_old_filter = SetUnhandledExceptionFilter(crash_exception_filter);

	/* First-chance vectored observer: only acts as a reentrancy backstop
	 * while the crash handler is printing. Failure is non-fatal (the
	 * unhandled filter is the primary path). */
	crash_vectored_handle = AddVectoredExceptionHandler(0, crash_vectored_handler);

#if defined(_MSC_VER)
	crash_old_invalid_param = _set_invalid_parameter_handler(crash_invalid_parameter_handler);
	crash_old_purecall = _set_purecall_handler(crash_purecall_handler);
#endif

	return 0;
}

static void crash_uninstall_win32(void) {
#if defined(_MSC_VER)
	if (crash_old_invalid_param != NULL) {
		(void)_set_invalid_parameter_handler(crash_old_invalid_param);
		crash_old_invalid_param = NULL;
	}
	if (crash_old_purecall != NULL) {
		(void)_set_purecall_handler(crash_old_purecall);
		crash_old_purecall = NULL;
	}
#endif
	if (crash_vectored_handle != NULL) {
		(void)RemoveVectoredExceptionHandler(crash_vectored_handle);
		crash_vectored_handle = NULL;
	}
	if (crash_old_filter != NULL) {
		(void)SetUnhandledExceptionFilter(crash_old_filter);
		crash_old_filter = NULL;
	}
	crash_active = 0;
}

#endif /* defined(_WIN32) */

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

#if defined(_WIN32)
static i32 crash_install_platform(void) {
	return crash_install_win32();
}
static void crash_uninstall_platform(void) {
	crash_uninstall_win32();
}
#else
static i32 crash_install_platform(void) {
	return crash_install_posix();
}
static void crash_uninstall_platform(void) {
	crash_uninstall_posix();
}
#endif

static u32 crash_installed;

i32 sk_crash_install(void) {
	if (crash_installed != 0u) {
		return 0;
	}
	if (crash_install_platform() != 0) {
		return -1;
	}
	crash_installed = 1u;
	return 0;
}

void sk_crash_uninstall(void) {
	if (crash_installed == 0u) {
		return;
	}
	crash_uninstall_platform();
	crash_installed = 0u;
}

/* ------------------------------------------------------------------------- */
/* Tests (host only; stripped from Release)                                   */
/* ------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

SK_TEST(crash_install_uninstall_idempotent) {
	TEST_ASSERT_EQUAL_INT(0, sk_crash_install());
	TEST_ASSERT_EQUAL_INT(0, sk_crash_install());
	sk_crash_uninstall();
	sk_crash_uninstall();
	TEST_ASSERT_EQUAL_INT(0, sk_crash_install());
	sk_crash_uninstall();
}

#if defined(_WIN32)

SK_TEST(crash_windows_exception_name_lookup) {
	TEST_ASSERT_EQUAL_STRING("ACCESS_VIOLATION", crash_exception_name(0xC0000005u));
	TEST_ASSERT_EQUAL_STRING("STACK_OVERFLOW", crash_exception_name(0xC00000FDu));
	TEST_ASSERT_NULL(crash_exception_name(0x12345678u));
}

#else /* POSIX */

SK_TEST(crash_posix_signal_name_lookup) {
	TEST_ASSERT_NOT_NULL(strstr(crash_signal_name(SIGSEGV), "SIGSEGV"));
	TEST_ASSERT_NOT_NULL(strstr(crash_signal_name(SIGABRT), "SIGABRT"));
	TEST_ASSERT_EQUAL_STRING("SEGV_MAPERR", crash_signal_code_name(SIGSEGV, SEGV_MAPERR));
	TEST_ASSERT_NULL(crash_signal_code_name(SIGSEGV, 12345));
}

/*
 * Integration: a child process deliberately null-dereferences with the crash
 * handler installed; the parent checks that a numbered raw-address stacktrace
 * was printed to stderr and that the child died of SIGSEGV (correct status).
 */
#if !defined(_WIN32)

#include <sys/resource.h> /* setrlimit */
#include <sys/wait.h>	  /* waitpid, WIFSIGNALED */

#if defined(__GNUC__) || defined(__clang__)
#define CRASH_TEST_NOINLINE __attribute__((noinline))
#else
#define CRASH_TEST_NOINLINE
#endif

/** Dereference @p addr; callers pass NULL to force a segmentation fault. */
CRASH_TEST_NOINLINE
static void crash_test_fault_at(const volatile u64* addr) {
	(void)*addr;
}

/** Volatile NULL target: the optimizer cannot constprop the dereference. */
static u64* volatile crash_test_null_target;

SK_TEST(crash_posix_null_deref_prints_stacktrace_and_signal_death) {
	int pipe_fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(pipe_fds));

	pid_t pid = fork();
	TEST_ASSERT_TRUE(pid >= 0);
	if (pid == 0) {
		/* Child: install the handler, redirect stderr into the pipe, crash. */
		(void)dup2(pipe_fds[1], STDERR_FILENO);
		(void)close(pipe_fds[0]);
		(void)close(pipe_fds[1]);

		struct rlimit limit;
		limit.rlim_cur = 0;
		limit.rlim_max = 0;
		(void)setrlimit(RLIMIT_CORE, &limit);

		(void)sk_crash_install();
		crash_test_null_target = NULL;
		crash_test_fault_at(crash_test_null_target);
		_exit(0); /* unreachable */
	}

	(void)close(pipe_fds[1]);

	char output[8192];
	size_t remaining = sizeof(output) - 1u;
	u32 total = 0u;
	while (remaining > 0u) {
		ssize_t n = read(pipe_fds[0], output + total, remaining);
		if (n <= 0) {
			break;
		}
		u32 got = (u32)n;
		total += got;
		remaining -= (size_t)got;
	}
	output[total] = '\0';
	(void)close(pipe_fds[0]);

	int status = 0;
	pid_t waited = waitpid(pid, &status, 0);
	TEST_ASSERT_TRUE(waited == pid);
	TEST_ASSERT_TRUE(WIFSIGNALED(status));
	TEST_ASSERT_EQUAL_INT(SIGSEGV, WTERMSIG(status));

	/* The report names the signal and prints a numbered frame list. */
	TEST_ASSERT_NOT_NULL(strstr(output, "SIGSEGV"));
	TEST_ASSERT_NOT_NULL(strstr(output, "thread id"));
	TEST_ASSERT_NOT_NULL(strstr(output, "faulting address"));
	TEST_ASSERT_NOT_NULL(strstr(output, "#0 0x"));
	TEST_ASSERT_NOT_NULL(strstr(output, "#2 0x"));
}

#endif /* !defined(_WIN32) */

#endif /* POSIX test section */
#endif /* SK_TESTS */
