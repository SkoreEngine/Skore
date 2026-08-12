#pragma once

/**
 * @file stacktrace.h
 * @brief Platform-agnostic stacktrace capture, symbolization and formatting.
 *
 * Pure engine utility implemented in sk-foundation. The capture path never
 * allocates: callers supply a fixed-size frame array and, for formatting, a
 * char buffer; frame string fields are fixed capacity. Backends that need
 * one-time setup (e.g. Win32 dbghelp) hook in through sk_stacktrace_init /
 * sk_stacktrace_shutdown.
 *
 * Backends: POSIX (Linux, Apple) captures via backtrace()/_Unwind_Backtrace
 * and symbolizes via dladdr (module, nearest symbol, byte offsets); Windows
 * captures via CaptureStackBackTrace and symbolizes via DbgHelp
 * (SymFromAddr + SymGetLineFromAddr64, falling back to module name + RVA
 * when no PDB is present). Neither backend fails hard: unknown frames keep
 * their zeroed / empty fields, and capture always yields zero frames instead
 * of erroring when a backend is unavailable.
 */

#include "common.h"

#include <stddef.h> /* NULL */

#ifdef __cplusplus
extern "C" {
#endif

/** Fixed capacity of the short string fields in sk_stacktrace_frame_t (bytes, including NUL). */
#define SK_STACKTRACE_NAME_CAP 128

/** Fixed capacity of the source-file field in sk_stacktrace_frame_t (bytes, including NUL). */
#define SK_STACKTRACE_FILE_CAP 256

/**
 * One stack frame. String fields are fixed-capacity NUL-terminated buffers;
 * an empty string means the value is unknown. Zero values mean unknown
 * (line 0, offsets 0). A frame with a NULL address is empty / unused.
 */
typedef struct sk_stacktrace_frame_t {
	/** Instruction pointer / return address of the frame; NULL = empty frame. */
	void_ptr_t address;

	/** Byte offset of @p address from the start of its module; 0 = unknown. */
	u64 module_offset;

	/** Byte offset of @p address from the start of its symbol; 0 = unknown. */
	u64 symbol_offset;

	/** 1-based source line; 0 = unknown. */
	u32 line;

	/** Explicit padding: keeps the 64-bit fields aligned on 32/64-bit ABIs. */
	u32 _pad0;

	/** Module / binary display name (e.g. "sk-player", "libc.so.6"). */
	char module_name[SK_STACKTRACE_NAME_CAP];

	/** Demangled function symbol name (e.g. "sk_app_init"). */
	char symbol_name[SK_STACKTRACE_NAME_CAP];

	/** Source file path (best effort, e.g. "core/app.c"). */
	char source_file[SK_STACKTRACE_FILE_CAP];
} sk_stacktrace_frame_t;

/**
 * One-time backend setup (e.g. Win32 SymInitialize).
 * No-op on the fallback backend. May be called more than once; repeated calls
 * after a successful init are no-ops.
 *
 * @return 0 on success, non-zero if the backend failed to initialize.
 */
i32 sk_stacktrace_init(void);

/**
 * Release backend resources acquired by sk_stacktrace_init.
 * No-op on the fallback backend and when init was never called (or failed).
 * Safe to call more than once.
 */
void sk_stacktrace_shutdown(void);

/**
 * Capture the current call stack into @p frames.
 *
 * The capture path performs no allocation; @p frames must be caller-owned
 * storage for at most @p capacity frames. Captured frames may only carry an
 * address until sk_stacktrace_resolve fills in names / source info.
 *
 * @param frames      Destination array; may be NULL only when @p capacity == 0.
 * @param capacity    Maximum number of frames to write.
 * @param skip_frames Number of innermost frames to skip after the backend
 *                    drops its own capture frame (0 = keep the
 *                    sk_stacktrace_capture wrapper as frame[0]; 1 = start at
 *                    the immediate caller of sk_stacktrace_capture; pass 1-2
 *                    when hiding the capture helper is desired).
 * @return Number of frames written (<= @p capacity); 0 when the platform
 *         backend is unavailable (fallback).
 */
u32 sk_stacktrace_capture(sk_stacktrace_frame_t* frames, u32 capacity, u32 skip_frames);

/**
 * Resolve/symbolize @p count captured frames in place: fills module, symbol,
 * source file, line and byte offsets. Frames the backend cannot symbolize
 * keep their zeroed / empty fields.
 *
 * @param frames Frame array from sk_stacktrace_capture.
 * @param count  Number of valid frames in @p frames.
 */
void sk_stacktrace_resolve(sk_stacktrace_frame_t* frames, u32 count);

/**
 * Write a human-readable trace for @p count frames into @p out.
 *
 * Frames are written one per line ("#i 0x<addr> [symbol] [(module +off)]
 * [at file:line]"), joined by newlines without a trailing newline. Empty
 * frames (NULL address) are skipped. When @p count == 0 (e.g. the fallback
 * backend), writes "stacktrace unavailable on this platform". The buffer is
 * always NUL-terminated when @p out_cap > 0.
 *
 * @param frames  Frame array; may be NULL when @p count == 0.
 * @param count   Number of frames.
 * @param out     Destination buffer; may be NULL only when @p out_cap == 0.
 * @param out_cap Capacity of @p out in bytes (including space for NUL).
 * @return Length written excluding NUL on success, or -1 when @p out_cap is
 *         too small to hold the full trace (partial, NUL-terminated output).
 */
i32 sk_stacktrace_format(const sk_stacktrace_frame_t* frames, u32 count, char* out, u32 out_cap);

#ifdef __cplusplus
}
#endif
