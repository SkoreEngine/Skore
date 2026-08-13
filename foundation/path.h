#pragma once

/**
 * @file path.h
 * @brief Pure path string utilities (no filesystem I/O).
 *
 * Operates on UTF-8 path slices (sk_str_view_t). Functions that build a new
 * path write a null-terminated result into a caller-provided buffer and return
 * the length written (excluding NUL), or a negative status on failure.
 *
 * Separators '/' and '\\' are both accepted as input. Output uses
 * SK_PATH_SEPARATOR for the host platform.
 */

#include "common.h"

#include <stddef.h> /* NULL */

#ifdef __cplusplus
extern "C" {
#endif

/* Host directory separator (matches C++ SK_PATH_SEPARATOR). */
#if defined(_WIN32)
#define SK_PATH_SEPARATOR '\\'
#define SK_PATH_SEPARATOR_STR "\\"
#else
#define SK_PATH_SEPARATOR '/'
#define SK_PATH_SEPARATOR_STR "/"
#endif

/**
 * Non-owning UTF-8 string slice. @p data may be NULL when @p size is 0.
 * Not required to be null-terminated.
 */
typedef struct sk_str_view_t {
	const_chr_t data;
	u32 size;
} sk_str_view_t;

/** Empty string view. */
#define SK_STR_VIEW_EMPTY ((sk_str_view_t){NULL, 0u})

/**
 * Build a string view from a pointer and byte length.
 * @param s    Pointer to bytes (may be NULL if size == 0).
 * @param size Number of bytes.
 */
SK_FINLINE sk_str_view_t sk_str_view_make(const_chr_t s, u32 size) {
	sk_str_view_t v;
	v.data = s;
	v.size = size;
	return v;
}

/**
 * Build a string view from a null-terminated C string.
 * @param s C string; NULL is treated as empty.
 */
SK_FINLINE sk_str_view_t sk_str_view_cstr(const_chr_t s) {
	u32 n = 0u;
	if (s != NULL) {
		while (s[n] != '\0') {
			++n;
		}
	}
	return sk_str_view_make(s, n);
}

/**
 * Non-zero if @p c is a directory separator ('/' or '\\').
 */
SK_FINLINE i32 sk_path_is_sep(char c) {
	return (c == '/' || c == '\\') ? 1 : 0;
}

/**
 * File extension of the final path component, including the leading '.'.
 * The returned view points into @p path (or is empty).
 *
 * @param path Input path.
 * @return Extension view (e.g. ".txt"), or empty if none.
 */
sk_str_view_t sk_path_extension(sk_str_view_t path);

/**
 * Write the parent directory of @p path into @p out (null-terminated).
 * Empty result if @p path has no parent separator.
 *
 * @param path    Input path.
 * @param out     Destination buffer; may be NULL only when out_cap == 0.
 * @param out_cap Capacity of @p out in bytes (including space for NUL).
 * @return Length written excluding NUL on success, or negative on failure
 *         (-1 invalid args / buffer too small).
 */
i32 sk_path_parent(sk_str_view_t path, char* out, u32 out_cap);

/**
 * Write the base name of @p path without its extension into @p out.
 * Trailing separators are ignored (e.g. "dir/folder/" → "folder").
 *
 * @param path    Input path.
 * @param out     Destination buffer.
 * @param out_cap Capacity of @p out in bytes (including NUL).
 * @return Length written excluding NUL, or negative on failure.
 */
i32 sk_path_name(sk_str_view_t path, char* out, u32 out_cap);

/**
 * Join two path segments into @p out, inserting SK_PATH_SEPARATOR when needed
 * and normalizing '/' and '\\' to the host separator. Empty segments are skipped.
 * Trailing separators on a segment are dropped (except when alone as absolute root).
 *
 * @param a       First segment.
 * @param b       Second segment.
 * @param out     Destination buffer.
 * @param out_cap Capacity of @p out in bytes (including NUL).
 * @return Length written excluding NUL, or negative on failure.
 */
i32 sk_path_join(sk_str_view_t a, sk_str_view_t b, char* out, u32 out_cap);

/**
 * Join @p count path segments from @p parts into @p out (same rules as sk_path_join).
 *
 * @param parts   Array of segments (may be NULL if count == 0).
 * @param count   Number of segments.
 * @param out     Destination buffer.
 * @param out_cap Capacity of @p out in bytes (including NUL).
 * @return Length written excluding NUL, or negative on failure.
 */
i32 sk_path_join_n(const sk_str_view_t* parts, u32 count, char* out, u32 out_cap);

/**
 * Extract the suffix of @p path after the first @p parent.size bytes, stripping
 * all host path separators from that suffix (matches C++ Path::ExtractName).
 *
 * @param parent  Prefix length reference (content is not compared).
 * @param path    Full path.
 * @param out     Destination buffer.
 * @param out_cap Capacity of @p out in bytes (including NUL).
 * @return Length written excluding NUL, or negative on failure.
 *         Empty result if parent.size >= path.size.
 */
i32 sk_path_extract_name(sk_str_view_t parent, sk_str_view_t path, char* out, u32 out_cap);

#ifdef __cplusplus
}
#endif
