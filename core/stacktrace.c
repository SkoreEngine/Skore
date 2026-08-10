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

/* ---- fallback backend: no platform backend available ---- */

static i32 fallback_init(void) {
	return 0;
}

static void fallback_shutdown(void) {
	/* nothing to release */
}

static u32 fallback_capture(sk_stacktrace_frame_t* frames, u32 capacity, u32 skip_frames) {
	(void)frames;
	(void)capacity;
	(void)skip_frames;
	return 0u;
}

static void fallback_resolve(sk_stacktrace_frame_t* frames, u32 count) {
	(void)frames;
	(void)count;
	/* nothing to symbolize */
}

static const stacktrace_backend_t fallback_backend = {
	fallback_init,
	fallback_shutdown,
	fallback_capture,
	fallback_resolve,
};

/* ---- backend selection (platform dispatch) ---- */

#if defined(_WIN32)
/* Win32 backend (CaptureStackBackTrace + dbghelp, needs sk_stacktrace_init)
 * lands in a follow-up task; the fallback backend is used until then. */
static const stacktrace_backend_t* const stacktrace_backend = &fallback_backend;
#else
/* POSIX backends (backtrace + dladdr) land in follow-up tasks; the fallback
 * backend is used until then. */
static const stacktrace_backend_t* const stacktrace_backend = &fallback_backend;
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

	line_append(line, (u32)sizeof(line), &pos, "#%u 0x%llx", index, (unsigned long long)(uintptr_t)frame->address);
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

SK_TEST(stacktrace_fallback_capture_returns_zero) {
	sk_stacktrace_frame_t frames[4];
	memset(frames, 0, sizeof(frames));

	u32 count = sk_stacktrace_capture(frames, 4u, 0u);
	TEST_ASSERT_EQUAL_UINT32(0u, count);

	/* Degenerate empty call is safe (NULL + capacity 0). */
	TEST_ASSERT_EQUAL_UINT32(0u, sk_stacktrace_capture(NULL, 0u, 1u));
}

SK_TEST(stacktrace_fallback_resolve_is_noop) {
	sk_stacktrace_frame_t frames[2];
	memset(frames, 0, sizeof(frames));
	frames[0].address = (void_ptr_t)(uintptr_t)0x1234u;

	sk_stacktrace_resolve(frames, 2u);

	TEST_ASSERT_EQUAL_PTR(frames[0].address, (void_ptr_t)(uintptr_t)0x1234u);
	TEST_ASSERT_EQUAL_STRING("", frames[0].symbol_name);
	TEST_ASSERT_EQUAL_STRING("", frames[1].module_name);
	TEST_ASSERT_EQUAL_UINT32(0u, frames[1].line);
}

SK_TEST(stacktrace_fallback_init_shutdown_idempotent) {
	TEST_ASSERT_EQUAL_INT(0, sk_stacktrace_init());
	TEST_ASSERT_EQUAL_INT(0, sk_stacktrace_init());
	sk_stacktrace_shutdown();
	sk_stacktrace_shutdown();
}

SK_TEST(stacktrace_fallback_format_writes_unavailable_message) {
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

#endif /* SK_TESTS */
