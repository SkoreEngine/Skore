#if !defined(_GNU_SOURCE)
/* dladdr() is a GNU extension on glibc: its prototype in <dlfcn.h> is gated
 * behind _GNU_SOURCE. Harmless on Apple / musl / BSDs, which expose it
 * unconditionally. Must precede any system include. */
#define _GNU_SOURCE 1 /* NOLINT(bugprone-reserved-identifier): deliberate feature-test macro for dladdr() */
#endif

#include "stacktrace.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* printf-style attribute for variadic helpers (clang-tidy -Wmissing-format-attribute;
 * empty on MSVC, which has no __attribute__). */
#if defined(__GNUC__) || defined(__clang__)
#define STACKTRACE_PRINTF(fmt_index, first_vararg) __attribute__((format(printf, fmt_index, first_vararg)))
#else
#define STACKTRACE_PRINTF(fmt_index, first_vararg)
#endif

/* ------------------------------------------------------------------------- */
/* Backend-shared helpers                                                    */
/* ------------------------------------------------------------------------- */

/** Copy @p src into @p dst, bounded by @p cap bytes; always NUL-terminated. */
static void stacktrace_copy_name(char* dst, u32 cap, const_chr_t src) {
	if (cap == 0u) {
		return;
	}
	u32 i = 0u;
	while (i + 1u < cap && src[i] != '\0') {
		dst[i] = src[i];
		++i;
	}
	dst[i] = '\0';
}

/** Basename of a native path (after the last '/' or '\\'); whole string when neither. */
static const_chr_t stacktrace_base_name(const_chr_t path) {
	const_chr_t base = path;
	for (const_chr_t p = path; *p != '\0'; ++p) {
		if (*p == '/' || *p == '\\') {
			base = p + 1;
		}
	}
	return base;
}

/** Max frames a backend buffers on the stack during capture (256 * 8 B on 64-bit, fine on a signal handler stack). */
#define STACKTRACE_MAX_FRAMES 256u

/**
 * Drop @p skip innermost frames (the backend's own capture frame plus the
 * caller's skip_frames) and compact the remainder to the front.
 * @return Number of remaining frames.
 */
static u32 stacktrace_apply_skip(sk_stacktrace_frame_t* frames, u32 count, u32 skip) {
	if (skip >= count) {
		return 0u;
	}
	if (skip > 0u) {
		memmove(frames, frames + skip, (size_t)(count - skip) * sizeof(*frames));
	}
	return count - skip;
}

/* ------------------------------------------------------------------------- */
/* Backend interface                                                         */
/* ------------------------------------------------------------------------- */

/**
 * Platform backend for the stacktrace module. Implementations live in this
 * file, selected by the platform dispatch below (same #if defined(_WIN32) /
 * #else convention as mutex.c and thread.c).
 */
typedef struct stacktrace_backend_t {
	/** One-time setup; 0 = ok. May be called again after shutdown. */
	i32 (*init)(void);
	/** Release resources acquired by init. */
	void (*shutdown)(void);
	/** Capture up to @p capacity frames, skipping @p skip_frames innermost frames. */
	u32 (*capture)(sk_stacktrace_frame_t* frames, u32 capacity, u32 skip_frames);
	/** Symbolize @p count frames in place. */
	void (*resolve)(sk_stacktrace_frame_t* frames, u32 count);
} stacktrace_backend_t;

#if defined(_WIN32)

/* ---- Win32 backend (CaptureStackBackTrace + DbgHelp) ---- */

/* CaptureStackBackTrace is gated on _WIN32_WINNT >= 0x0600 in the Windows
 * SDK and MinGW-w64 headers; declare the Vista floor before windows.h. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 /* NOLINT(bugprone-reserved-identifier): Win7 floor for CaptureStackBackTrace */
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

/*
 * DbgHelp is not thread-safe: every Sym* call is serialized through the
 * critical section below. The section is created by win32_init and only
 * destroyed by win32_shutdown, so resolve can rely on it existing whenever
 * dbghelp_initialized is set. init / resolve / shutdown run on the main
 * thread per the engine default; the lock additionally makes resolve safe
 * when a future job system symbolizes off-main.
 */
static CRITICAL_SECTION dbghelp_cs;
static BOOL dbghelp_cs_ready;
static BOOL dbghelp_initialized;

static i32 win32_init(void) {
	if (dbghelp_initialized != 0) {
		return 0;
	}
	if (dbghelp_cs_ready == 0) {
		InitializeCriticalSection(&dbghelp_cs);
		dbghelp_cs_ready = TRUE;
	}

	EnterCriticalSection(&dbghelp_cs);
	if (dbghelp_initialized != 0) {
		LeaveCriticalSection(&dbghelp_cs);
		return 0;
	}

	/* Deferred loads + undecorated names + line info (for SymGetLineFromAddr64). */
	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
	i32 rc = 0;
	if (SymInitialize(GetCurrentProcess(), NULL, TRUE) != 0) {
		dbghelp_initialized = TRUE;
	} else {
		/* No dbghelp / PDBs: capture and the module+RVA fallback still work;
		 * a later init call retries. */
		rc = -1;
	}
	LeaveCriticalSection(&dbghelp_cs);
	return rc;
}

static void win32_shutdown(void) {
	if (dbghelp_cs_ready == 0) {
		return;
	}
	EnterCriticalSection(&dbghelp_cs);
	if (dbghelp_initialized != 0) {
		SymCleanup(GetCurrentProcess());
		dbghelp_initialized = FALSE;
	}
	LeaveCriticalSection(&dbghelp_cs);
	DeleteCriticalSection(&dbghelp_cs);
	dbghelp_cs_ready = FALSE;
}

static u32 win32_capture(sk_stacktrace_frame_t* frames, u32 capacity, u32 skip_frames) {
	if (frames == NULL || capacity == 0u) {
		return 0u;
	}
	if (capacity > STACKTRACE_MAX_FRAMES) {
		capacity = STACKTRACE_MAX_FRAMES;
	}
	/* CaptureStackBackTrace writes a contiguous PVOID array, so collect into
	 * a local stack buffer first, then copy the addresses into the caller's
	 * larger frames (sk_stacktrace_frame_t is much larger than void*). No
	 * allocation, so capture stays safe for crash handlers. */
	void_ptr_t addrs[STACKTRACE_MAX_FRAMES];
	USHORT n = CaptureStackBackTrace(0u, (DWORD)capacity, addrs, NULL);
	if (n == 0u) {
		return 0u;
	}
	for (u32 i = 0u; i < (u32)n; ++i) {
		frames[i].address = addrs[i];
	}
	return stacktrace_apply_skip(frames, (u32)n, skip_frames + 1u);
}

static void win32_resolve(sk_stacktrace_frame_t* frames, u32 count) {
	for (u32 i = 0u; i < count; ++i) {
		sk_stacktrace_frame_t* frame = &frames[i];
		if (frame->address == NULL) {
			continue;
		}
		uintptr_t addr = (uintptr_t)frame->address;
		DWORD64 addr64 = addr; /* DbgHelp address (identity on 64-bit Windows) */

		/* Module name + RVA via kernel32 only: works with and without PDBs
		 * and never depends on DbgHelp's internal module list. */
		HMODULE module = NULL;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &module) != 0) {
			frame->module_offset = addr - (uintptr_t)module;
			char path[MAX_PATH];
			if (GetModuleFileNameA(module, path, (DWORD)sizeof(path)) > 0u) {
				stacktrace_copy_name(frame->module_name, SK_STACKTRACE_NAME_CAP, stacktrace_base_name(path));
			}
		}

		if (dbghelp_initialized == 0) {
			/* DbgHelp unavailable (init failed or never called): module+RVA only. */
			continue;
		}
		EnterCriticalSection(&dbghelp_cs);

		/* SymFromAddr: nearest symbol + displacement (undecorated via SYMOPT_UNDNAME). */
		DWORD64 symbol_disp = 0u;
		union {
			SYMBOL_INFO info;
			char bytes[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)];
		} symbol = {0};
		symbol.info.SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol.info.MaxNameLen = MAX_SYM_NAME;
		if (SymFromAddr(GetCurrentProcess(), addr64, &symbol_disp, &symbol.info) != 0) {
			stacktrace_copy_name(frame->symbol_name, SK_STACKTRACE_NAME_CAP, symbol.info.Name);
			frame->symbol_offset = symbol_disp;
		}

		/* SymGetLineFromAddr64: source file + line (needs PDB line info). */
		IMAGEHLP_LINE64 line_info = {0};
		line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
		DWORD line_disp = 0u;
		if (SymGetLineFromAddr64(GetCurrentProcess(), addr64, &line_disp, &line_info) != 0) {
			stacktrace_copy_name(frame->source_file, SK_STACKTRACE_FILE_CAP, line_info.FileName);
			frame->line = (u32)line_info.LineNumber;
		}

		LeaveCriticalSection(&dbghelp_cs);
	}
}

static const stacktrace_backend_t win32_backend = {
	win32_init,
	win32_shutdown,
	win32_capture,
	win32_resolve,
};

#endif /* defined(_WIN32) */

/* ------------------------------------------------------------------------- */
/* POSIX backends (Linux, Apple, BSDs, ...)                                  */
/* ------------------------------------------------------------------------- */

#if !defined(_WIN32)

#include <dlfcn.h> /* dladdr */

/*
 * Capture strategy:
 * - backtrace() from <execinfo.h> when available (glibc, Apple, BSDs);
 * - _Unwind_Backtrace from <unwind.h> otherwise (musl ships no execinfo.h;
 *   libgcc / compiler-rt provides the unwind entry points).
 * Both walk the stack without allocating, so capture stays signal-safe.
 * Symbolization (dladdr) is not signal-safe and is confined to resolve, per
 * the header contract, so a crash handler can capture in the handler and
 * resolve afterwards.
 */
#if defined(__has_include)
#if __has_include(<execinfo.h>)
#define STACKTRACE_POSIX_HAS_EXECINFO 1
#include <execinfo.h>
#endif
#endif

#if defined(STACKTRACE_POSIX_HAS_EXECINFO)

static u32 posix_capture(sk_stacktrace_frame_t* frames, u32 capacity, u32 skip_frames) {
	if (frames == NULL || capacity == 0u) {
		return 0u;
	}
	if (capacity > STACKTRACE_MAX_FRAMES) {
		capacity = STACKTRACE_MAX_FRAMES;
	}
	/* backtrace() fills a contiguous void* array, so collect into a local
	 * stack buffer first, then copy the addresses into the caller's frames
	 * (sk_stacktrace_frame_t is much larger than void*). No allocation, so
	 * capture stays signal-safe. */
	void_ptr_t addrs[STACKTRACE_MAX_FRAMES];
	int n = backtrace(addrs, (int)capacity);
	if (n <= 0) {
		return 0u;
	}
	for (int i = 0; i < n; ++i) {
		frames[i].address = addrs[i];
	}
	return stacktrace_apply_skip(frames, (u32)n, skip_frames + 1u);
}

#else /* !STACKTRACE_POSIX_HAS_EXECINFO */

#include <unwind.h>

typedef struct unwind_trace_t {
	sk_stacktrace_frame_t* frames;
	u32 capacity;
	u32 count;
} unwind_trace_t;

static _Unwind_Reason_Code unwind_trace_cb(struct _Unwind_Context* ctx, void_ptr_t arg) {
	unwind_trace_t* trace = (unwind_trace_t*)arg;
	if (trace->count >= trace->capacity) {
		return _URC_END_OF_STACK;
	}
	trace->frames[trace->count].address = (void_ptr_t)_Unwind_GetIP(ctx);
	trace->count += 1u;
	return _URC_NO_REASON;
}

static u32 posix_capture(sk_stacktrace_frame_t* frames, u32 capacity, u32 skip_frames) {
	if (frames == NULL || capacity == 0u) {
		return 0u;
	}
	unwind_trace_t trace = {frames, capacity, 0u};
	_Unwind_Backtrace(unwind_trace_cb, &trace);
	return stacktrace_apply_skip(frames, trace.count, skip_frames + 1u);
}

#endif /* STACKTRACE_POSIX_HAS_EXECINFO */

/** Symbolize via dladdr: module basename + base offset, nearest symbol + offset. */
static void posix_resolve(sk_stacktrace_frame_t* frames, u32 count) {
	for (u32 i = 0u; i < count; ++i) {
		sk_stacktrace_frame_t* frame = &frames[i];
		if (frame->address == NULL) {
			continue;
		}
		Dl_info info = {0};
		if (dladdr(frame->address, &info) == 0) {
			/* Address is not in a mapped object: keep the raw address only. */
			continue;
		}
		if (info.dli_fname != NULL) {
			stacktrace_copy_name(frame->module_name, SK_STACKTRACE_NAME_CAP, stacktrace_base_name(info.dli_fname));
		}
		if (info.dli_fbase != NULL) {
			frame->module_offset = (u64)((uintptr_t)frame->address - (uintptr_t)info.dli_fbase);
		}
		if (info.dli_sname != NULL) {
			/* C symbol names only: mangled C++ names are emitted verbatim
			 * (no demangler dependency). */
			stacktrace_copy_name(frame->symbol_name, SK_STACKTRACE_NAME_CAP, info.dli_sname);
			if (info.dli_saddr != NULL) {
				frame->symbol_offset = (u64)((uintptr_t)frame->address - (uintptr_t)info.dli_saddr);
			}
		}
	}
}

static i32 posix_init(void) {
	/* backtrace/dladdr need no one-time setup. */
	return 0;
}

static void posix_shutdown(void) {
	/* nothing to release */
}

static const stacktrace_backend_t posix_backend = {
	posix_init,
	posix_shutdown,
	posix_capture,
	posix_resolve,
};

#endif /* !defined(_WIN32) */

/* ---- backend selection (platform dispatch) ---- */

#if defined(_WIN32)
static const stacktrace_backend_t* const stacktrace_backend = &win32_backend;
#else
static const stacktrace_backend_t* const stacktrace_backend = &posix_backend;
#endif

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

i32 sk_stacktrace_init(void) {
	return stacktrace_backend->init();
}

void sk_stacktrace_shutdown(void) {
	stacktrace_backend->shutdown();
}

u32 sk_stacktrace_capture(sk_stacktrace_frame_t* frames, u32 capacity, u32 skip_frames) {
	return stacktrace_backend->capture(frames, capacity, skip_frames);
}

void sk_stacktrace_resolve(sk_stacktrace_frame_t* frames, u32 count) {
	stacktrace_backend->resolve(frames, count);
}

/* ------------------------------------------------------------------------- */
/* Formatting                                                                */
/* ------------------------------------------------------------------------- */

/** Bounded writer over a caller-provided buffer; never allocates. */
typedef struct trace_writer_t {
	char* out;
	u32 cap;
	u32 pos;
	i32 truncated;
} trace_writer_t;

/**
 * Append @p len bytes of @p text to @p w, keeping the buffer NUL-terminated.
 * Sets @p w.truncated when the text (or its NUL) does not fully fit.
 */
static void trace_append(trace_writer_t* w, const_chr_t text, u32 len) {
	if (w->pos >= w->cap) {
		w->truncated = 1;
		return;
	}
	u32 space = w->cap - 1u - w->pos;
	u32 n = len < space ? len : space;
	if (n > 0u) {
		memcpy(w->out + w->pos, text, n);
		w->pos += n;
	}
	w->out[w->pos] = '\0';
	if (len > space) {
		w->truncated = 1;
	}
}

/** Append a printf-formatted piece to @p line at @p pos, bounded by @p line_cap. */
static void line_append(char* line, u32 line_cap, u32* pos, const_chr_t fmt, ...) STACKTRACE_PRINTF(4, 5);

static void line_append(char* line, u32 line_cap, u32* pos, const_chr_t fmt, ...) {
	u32 space = line_cap - *pos;
	if (space == 0u) {
		return;
	}
	va_list args;
	va_start(args, fmt);
	int written = vsnprintf(line + *pos, (size_t)space, fmt, args);
	va_end(args);
	if (written > 0) {
		*pos += ((u32)written < space) ? (u32)written : (space - 1u);
	}
}

/** Append one frame line ("#i 0x<addr> [symbol] [(module +off)] [at file:line]"). */
static void format_frame(trace_writer_t* w, u32 index, const sk_stacktrace_frame_t* frame) {
	char line[SK_STACKTRACE_FILE_CAP + 224u];
	u32 pos = 0u;

	line_append(line, (u32)sizeof(line), &pos, "#%u 0x%llx", index, (unsigned long long)frame->address);
	if (frame->symbol_name[0] != '\0') {
		line_append(line, (u32)sizeof(line), &pos, "  %s", frame->symbol_name);
	}
	if (frame->module_name[0] != '\0') {
		if (frame->module_offset != 0u) {
			line_append(line, (u32)sizeof(line), &pos, " (%s +0x%llx)", frame->module_name, (unsigned long long)frame->module_offset);
		} else {
			line_append(line, (u32)sizeof(line), &pos, " (%s)", frame->module_name);
		}
	}
	if (frame->source_file[0] != '\0') {
		line_append(line, (u32)sizeof(line), &pos, " at %s:%u", frame->source_file, frame->line);
	}

	trace_append(w, line, pos);
}

i32 sk_stacktrace_format(const sk_stacktrace_frame_t* frames, u32 count, char* out, u32 out_cap) {
	trace_writer_t w = {out, out_cap, 0u, 0};

	if (count == 0u) {
		const char* unavailable = "stacktrace unavailable on this platform";
		trace_append(&w, unavailable, (u32)sizeof("stacktrace unavailable on this platform") - 1u);
	} else {
		for (u32 i = 0u; i < count; ++i) {
			if (frames[i].address == NULL) {
				continue;
			}
			if (w.pos > 0u) {
				trace_append(&w, "\n", 1u);
			}
			format_frame(&w, i, &frames[i]);
		}
	}

	if (w.truncated != 0) {
		return -1;
	}
	return (i32)w.pos;
}

/* ------------------------------------------------------------------------- */
/* Tests (host only; stripped from Release)                                   */
/* ------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

SK_TEST(stacktrace_capture_empty_is_safe) {
	/* Degenerate empty call is safe on every backend (NULL + capacity 0). */
	TEST_ASSERT_EQUAL_UINT32(0u, sk_stacktrace_capture(NULL, 0u, 1u));
}

SK_TEST(stacktrace_resolve_unresolvable_address_keeps_frame) {
	sk_stacktrace_frame_t frames[2];
	memset(frames, 0, sizeof(frames));
	frames[0].address = (void_ptr_t)(uintptr_t)0x1234u;

	sk_stacktrace_resolve(frames, 2u);

	TEST_ASSERT_EQUAL_PTR(frames[0].address, (void_ptr_t)(uintptr_t)0x1234u);
	TEST_ASSERT_EQUAL_STRING("", frames[0].symbol_name);
	TEST_ASSERT_EQUAL_STRING("", frames[1].module_name);
	TEST_ASSERT_EQUAL_UINT32(0u, frames[1].line);
}

SK_TEST(stacktrace_init_shutdown_idempotent) {
	TEST_ASSERT_EQUAL_INT(0, sk_stacktrace_init());
	TEST_ASSERT_EQUAL_INT(0, sk_stacktrace_init());
	sk_stacktrace_shutdown();
	sk_stacktrace_shutdown();
}

SK_TEST(stacktrace_format_empty_writes_unavailable_message) {
	char out[128];
	i32 len = sk_stacktrace_format(NULL, 0u, out, (u32)sizeof(out));

	TEST_ASSERT_TRUE(len > 0);
	TEST_ASSERT_NOT_NULL(strstr(out, "stacktrace unavailable on this platform"));
	TEST_ASSERT_EQUAL_INT((i32)strlen(out), len);
}

SK_TEST(stacktrace_format_frame_line) {
	sk_stacktrace_frame_t frame;
	memset(&frame, 0, sizeof(frame));
	frame.address = (void_ptr_t)(uintptr_t)0x12345678u;
	frame.module_offset = 0x100u;
	frame.line = 42u;
	memcpy(frame.module_name, "sk-player", 10u);
	memcpy(frame.symbol_name, "sk_app_init", 12u);
	memcpy(frame.source_file, "core/app.c", 10u);

	char out[512];
	i32 len = sk_stacktrace_format(&frame, 1u, out, (u32)sizeof(out));

	TEST_ASSERT_TRUE(len > 0);
	TEST_ASSERT_NOT_NULL(strstr(out, "0x12345678"));
	TEST_ASSERT_NOT_NULL(strstr(out, "sk_app_init"));
	TEST_ASSERT_NOT_NULL(strstr(out, "sk-player"));
	TEST_ASSERT_NOT_NULL(strstr(out, "core/app.c:42"));
	TEST_ASSERT_EQUAL_INT((i32)strlen(out), len);
}

SK_TEST(stacktrace_format_skips_empty_frames) {
	sk_stacktrace_frame_t frames[3];
	memset(frames, 0, sizeof(frames));
	frames[1].address = (void_ptr_t)(uintptr_t)0x1u;
	memcpy(frames[1].symbol_name, "second", 7u);

	char out[256];
	i32 len = sk_stacktrace_format(frames, 3u, out, (u32)sizeof(out));

	TEST_ASSERT_TRUE(len > 0);
	TEST_ASSERT_NOT_NULL(strstr(out, "second"));
	TEST_ASSERT_NULL(strstr(out, "#0"));
	TEST_ASSERT_NOT_NULL(strstr(out, "#1"));
	TEST_ASSERT_NULL(strstr(out, "#2"));
	TEST_ASSERT_EQUAL_INT((i32)strlen(out), len);
}

SK_TEST(stacktrace_format_too_small_buffer) {
	char out[8];
	i32 len = sk_stacktrace_format(NULL, 0u, out, (u32)sizeof(out));

	TEST_ASSERT_TRUE(len < 0);
	TEST_ASSERT_EQUAL_STRING("stacktr", out); /* partial, NUL-terminated */
}

/*
 * Nested-call-chain helpers shared by the POSIX and Win32 capture tests.
 * They stay non-static so the test host carries their names for the
 * backends to resolve (dladdr via --export-dynamic-symbol on Linux; DbgHelp
 * via the executable PDB on Windows). Noinline plus a non-tail return keep
 * the frames from being inlined or tail-call-eliminated in optimized test
 * builds. Test-only; stripped from Release with the rest of the SK_TESTS
 * section.
 */
#if defined(_MSC_VER)
#define STACKTRACE_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define STACKTRACE_TEST_NOINLINE __attribute__((noinline))
#else
#define STACKTRACE_TEST_NOINLINE
#endif

typedef u32 (*stacktrace_capture_fn_t)(sk_stacktrace_frame_t* frames, u32 capacity, u32 skip_frames);

STACKTRACE_TEST_NOINLINE
u32 stacktrace_test_leaf(stacktrace_capture_fn_t capture, sk_stacktrace_frame_t* frames, u32 capacity);
STACKTRACE_TEST_NOINLINE
u32 stacktrace_test_middle(stacktrace_capture_fn_t capture, sk_stacktrace_frame_t* frames, u32 capacity);

STACKTRACE_TEST_NOINLINE
u32 stacktrace_test_leaf(stacktrace_capture_fn_t capture, sk_stacktrace_frame_t* frames, u32 capacity) {
	u32 count = capture(frames, capacity, 0u);
	/* Unfoldable correction keeps the call out of tail position. */
	return count + (count < 1000u ? 0u : 1u);
}

STACKTRACE_TEST_NOINLINE
u32 stacktrace_test_middle(stacktrace_capture_fn_t capture, sk_stacktrace_frame_t* frames, u32 capacity) {
	u32 count = stacktrace_test_leaf(capture, frames, capacity);
	return count + (count < 1000u ? 0u : 1u);
}

#if defined(_WIN32)

/** True when DbgHelp resolved at least one symbol (PDB present). */
static int stacktrace_windows_any_symbol(const sk_stacktrace_frame_t* frames, u32 count) {
	for (u32 i = 0u; i < count; ++i) {
		if (frames[i].symbol_name[0] != '\0') {
			return 1;
		}
	}
	return 0;
}

/** True when DbgHelp resolved at least one file/line (PDB line info present). */
static int stacktrace_windows_any_line(const sk_stacktrace_frame_t* frames, u32 count) {
	for (u32 i = 0u; i < count; ++i) {
		if (frames[i].source_file[0] != '\0' && frames[i].line != 0u) {
			return 1;
		}
	}
	return 0;
}

SK_TEST(stacktrace_windows_capture_nested_chain) {
	/* DbgHelp needs init; repeated init/shutdown must stay harmless. */
	TEST_ASSERT_EQUAL_INT(0, sk_stacktrace_init());
	TEST_ASSERT_EQUAL_INT(0, sk_stacktrace_init());

	sk_stacktrace_frame_t frames[32];
	memset(frames, 0, sizeof(frames));

	u32 count = stacktrace_test_middle(sk_stacktrace_capture, frames, 32u);
	TEST_ASSERT_TRUE(count > 0u);
	TEST_ASSERT_TRUE(count <= 32u);
	TEST_ASSERT_NOT_NULL(frames[0].address);

	/* Symbolize in place, then format. */
	sk_stacktrace_resolve(frames, count);

	char out[8192];
	i32 len = sk_stacktrace_format(frames, count, out, (u32)sizeof(out));
	TEST_ASSERT_TRUE(len > 0);
	TEST_ASSERT_NOT_NULL(strstr(out, "0x"));

	/* Module + RVA fallback must work with or without PDBs. */
	TEST_ASSERT_TRUE(frames[0].module_name[0] != '\0');
	TEST_ASSERT_TRUE(frames[0].module_offset > 0u);

	/* PDBs are present in Debug/RelWithDebInfo: DbgHelp resolves the exported
	 * helper names and line info. Release (no PDB) falls back to module+RVA. */
	if (stacktrace_windows_any_symbol(frames, count)) {
		TEST_ASSERT_NOT_NULL(strstr(out, "stacktrace_test_leaf"));
		TEST_ASSERT_NOT_NULL(strstr(out, "stacktrace_test_middle"));
		if (stacktrace_windows_any_line(frames, count)) {
			TEST_ASSERT_NOT_NULL(strstr(out, ".c:"));
		}
	}

	sk_stacktrace_shutdown();
	sk_stacktrace_shutdown();
}

#else /* POSIX backends */

SK_TEST(stacktrace_posix_capture_nested_chain) {
	sk_stacktrace_frame_t frames[32];
	memset(frames, 0, sizeof(frames));

	u32 count = stacktrace_test_middle(sk_stacktrace_capture, frames, 32u);
	TEST_ASSERT_TRUE(count > 0u);
	TEST_ASSERT_TRUE(count <= 32u);
	TEST_ASSERT_NOT_NULL(frames[0].address);

	/* Symbolize in place, then format: the exported helper names must show
	 * up (dladdr via the executable's dynamic symbol table). */
	sk_stacktrace_resolve(frames, count);

	char out[8192];
	i32 len = sk_stacktrace_format(frames, count, out, (u32)sizeof(out));
	TEST_ASSERT_TRUE(len > 0);

	TEST_ASSERT_NOT_NULL(strstr(out, "stacktrace_test_leaf"));
	TEST_ASSERT_NOT_NULL(strstr(out, "stacktrace_test_middle"));
	TEST_ASSERT_NOT_NULL(strstr(out, "0x"));
}

#endif /* _WIN32 */

#endif /* SK_TESTS */
