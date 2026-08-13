#include "path.h"

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static i32 path_write(char* out, u32 out_cap, const char* src, u32 len) {
	if (out_cap == 0u || len >= out_cap) {
		return -1;
	}
	for (u32 i = 0u; i < len; ++i) {
		out[i] = src[i];
	}
	out[len] = '\0';
	return (i32)len;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

sk_str_view_t sk_path_extension(sk_str_view_t path) {
	if (path.data == NULL || path.size == 0u) {
		return SK_STR_VIEW_EMPTY;
	}

	/* Find start of final component (after last separator). */
	u32 component_start = 0u;
	for (u32 i = 0u; i < path.size; ++i) {
		if (sk_path_is_sep(path.data[i])) {
			component_start = i + 1u;
		}
	}

	u32 i = path.size;
	while (i > component_start) {
		--i;
		if (path.data[i] == '.') {
			/* Leading-dot only (".git") is not treated as an extension. */
			if (i == component_start) {
				return SK_STR_VIEW_EMPTY;
			}
			return sk_str_view_make(path.data + i, path.size - i);
		}
	}
	return SK_STR_VIEW_EMPTY;
}

i32 sk_path_parent(sk_str_view_t path, char* out, u32 out_cap) {
	if (out_cap == 0u) {
		return -1;
	}
	if (path.data == NULL || path.size == 0u) {
		out[0] = '\0';
		return 0;
	}

	/*
     * Match C++ Path::Parent: walk from the last character toward begin.
     * After the first separator is seen, collect preceding characters.
     * Collected RTL, then reversed into @p out. Needs path.size + 1 bytes.
     */
	if (out_cap <= path.size) {
		return -1;
	}

	i32 found_sep = 0;
	u32 out_len = 0u;

	u32 i = path.size;
	for (;;) {
		--i;
		if (found_sep) {
			out[out_len++] = path.data[i];
		}
		if (sk_path_is_sep(path.data[i])) {
			found_sep = 1;
		}
		if (i == 0u) {
			break;
		}
	}

	if (out_len == 0u) {
		out[0] = '\0';
		return 0;
	}

	/* Characters were collected right-to-left → reverse in place. */
	{
		u32 a = 0u;
		u32 b = out_len - 1u;
		while (a < b) {
			char t = out[a];
			out[a] = out[b];
			out[b] = t;
			++a;
			--b;
		}
	}
	out[out_len] = '\0';
	return (i32)out_len;
}

i32 sk_path_name(sk_str_view_t path, char* out, u32 out_cap) {
	if (out_cap == 0u) {
		return -1;
	}
	if (path.data == NULL || path.size == 0u) {
		out[0] = '\0';
		return 0;
	}

	/* Trim trailing separators (e.g. "/path/folder/"). */
	u32 end = path.size;
	while (end > 0u && sk_path_is_sep(path.data[end - 1u])) {
		--end;
	}
	if (end == 0u) {
		out[0] = '\0';
		return 0;
	}

	/* Final component starts after last separator. */
	u32 start = 0u;
	for (u32 i = 0u; i < end; ++i) {
		if (sk_path_is_sep(path.data[i])) {
			start = i + 1u;
		}
	}

	u32 name_end = end;
	sk_str_view_t ext = sk_path_extension(sk_str_view_make(path.data, end));
	if (ext.size > 0u) {
		name_end = end - ext.size;
		if (name_end < start) {
			name_end = start;
		}
	}

	return path_write(out, out_cap, path.data + start, name_end - start);
}

i32 sk_path_join_n(const sk_str_view_t* parts, u32 count, char* out, u32 out_cap) {
	if (out_cap == 0u) {
		return -1;
	}

	u32 out_len = 0u;
	out[0] = '\0';

	for (u32 p = 0u; p < count; ++p) {
		sk_str_view_t seg = parts[p];

		if (seg.data == NULL || seg.size == 0u) {
			continue;
		}

		char first = seg.data[0];
		if (!sk_path_is_sep(first) && out_len > 0u) {
			char last = out[out_len - 1u];
			if (!sk_path_is_sep(last)) {
				if (out_len + 1u >= out_cap) {
					return -1;
				}
				out[out_len++] = (char)SK_PATH_SEPARATOR;
			}
		}

		for (u32 i = 0u; i < seg.size; ++i) {
			char c = seg.data[i];
			if (sk_path_is_sep(c)) {
				/* Drop trailing separator of this segment (match C++ Join). */
				if (i + 1u < seg.size) {
					if (out_len + 1u >= out_cap) {
						return -1;
					}
					out[out_len++] = (char)SK_PATH_SEPARATOR;
				}
			} else {
				if (out_len + 1u >= out_cap) {
					return -1;
				}
				out[out_len++] = c;
			}
		}
	}

	out[out_len] = '\0';
	return (i32)out_len;
}

i32 sk_path_join(sk_str_view_t a, sk_str_view_t b, char* out, u32 out_cap) {
	sk_str_view_t parts[2];
	parts[0] = a;
	parts[1] = b;
	return sk_path_join_n(parts, 2u, out, out_cap);
}

i32 sk_path_extract_name(sk_str_view_t parent, sk_str_view_t path, char* out, u32 out_cap) {
	if (out_cap == 0u) {
		return -1;
	}
	if (path.data == NULL) {
		path.size = 0u;
	}
	if (parent.size >= path.size) {
		out[0] = '\0';
		return 0;
	}

	u32 out_len = 0u;
	for (u32 i = parent.size; i < path.size; ++i) {
		char c = path.data[i];
		/* Match C++: strip host SK_PATH_SEPARATOR only (not the alternate). */
		if (c != (char)SK_PATH_SEPARATOR) {
			if (out_len + 1u >= out_cap) {
				return -1;
			}
			out[out_len++] = c;
		}
	}
	out[out_len] = '\0';
	return (i32)out_len;
}

#ifdef SK_TESTS
#include "test.h"
#include "path.h"
#include <string.h>

SK_TEST(str_view_cstr_and_empty) {
	sk_str_view_t empty = sk_str_view_cstr(NULL);
	sk_str_view_t hi = sk_str_view_cstr("hi");

	TEST_ASSERT_EQUAL_UINT32(0u, empty.size);
	TEST_ASSERT_EQUAL_UINT32(2u, hi.size);
	TEST_ASSERT_EQUAL_STRING("hi", hi.data);
	TEST_ASSERT_EQUAL_UINT32(0u, SK_STR_VIEW_EMPTY.size);
}

SK_TEST(path_extension_basic) {
	sk_str_view_t ext = sk_path_extension(sk_str_view_cstr("file.txt"));
	TEST_ASSERT_EQUAL_UINT32(4u, ext.size);
	TEST_ASSERT_EQUAL_INT(0, memcmp(ext.data, ".txt", 4));

	ext = sk_path_extension(sk_str_view_cstr("path/to/archive.tar.gz"));
	TEST_ASSERT_EQUAL_UINT32(3u, ext.size);
	TEST_ASSERT_EQUAL_INT(0, memcmp(ext.data, ".gz", 3));

	ext = sk_path_extension(sk_str_view_cstr("no_extension"));
	TEST_ASSERT_EQUAL_UINT32(0u, ext.size);

	ext = sk_path_extension(sk_str_view_cstr("dir.with.dots/file"));
	TEST_ASSERT_EQUAL_UINT32(0u, ext.size);

	ext = sk_path_extension(sk_str_view_cstr(".hidden"));
	TEST_ASSERT_EQUAL_UINT32(0u, ext.size);

	ext = sk_path_extension(SK_STR_VIEW_EMPTY);
	TEST_ASSERT_EQUAL_UINT32(0u, ext.size);
}

SK_TEST(path_parent_basic) {
	char buf[64];

	i32 n = sk_path_parent(sk_str_view_cstr("a/b/c"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(3, n);
	TEST_ASSERT_EQUAL_STRING("a/b", buf);

	n = sk_path_parent(sk_str_view_cstr("a/b/"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(3, n);
	TEST_ASSERT_EQUAL_STRING("a/b", buf);

	n = sk_path_parent(sk_str_view_cstr("file"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(0, n);
	TEST_ASSERT_EQUAL_STRING("", buf);

	n = sk_path_parent(sk_str_view_cstr("/file"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(0, n);
	TEST_ASSERT_EQUAL_STRING("", buf);

	n = sk_path_parent(sk_str_view_cstr("/a/b"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(2, n);
	TEST_ASSERT_EQUAL_STRING("/a", buf);

	n = sk_path_parent(SK_STR_VIEW_EMPTY, buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(0, n);
	TEST_ASSERT_EQUAL_STRING("", buf);

	TEST_ASSERT_EQUAL_INT(-1, sk_path_parent(sk_str_view_cstr("a/b"), buf, 2));
}

SK_TEST(path_name_basic) {
	char buf[64];

	i32 n = sk_path_name(sk_str_view_cstr("path/to/file.txt"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(4, n);
	TEST_ASSERT_EQUAL_STRING("file", buf);

	n = sk_path_name(sk_str_view_cstr("file.txt"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(4, n);
	TEST_ASSERT_EQUAL_STRING("file", buf);

	n = sk_path_name(sk_str_view_cstr("path/to/folder"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(6, n);
	TEST_ASSERT_EQUAL_STRING("folder", buf);

	n = sk_path_name(sk_str_view_cstr("path/to/folder/"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(6, n);
	TEST_ASSERT_EQUAL_STRING("folder", buf);

	n = sk_path_name(sk_str_view_cstr(".hidden"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(7, n);
	TEST_ASSERT_EQUAL_STRING(".hidden", buf);

	n = sk_path_name(sk_str_view_cstr(""), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(0, n);
	TEST_ASSERT_EQUAL_STRING("", buf);
}

SK_TEST(path_join_basic) {
	char buf[128];
	char sep = (char)SK_PATH_SEPARATOR;
	char expected[64];

	i32 n = sk_path_join(sk_str_view_cstr("assets"), sk_str_view_cstr("meshes"), buf, sizeof(buf));
	TEST_ASSERT_TRUE(n > 0);
	expected[0] = 'a';
	expected[1] = 's';
	expected[2] = 's';
	expected[3] = 'e';
	expected[4] = 't';
	expected[5] = 's';
	expected[6] = sep;
	expected[7] = 'm';
	expected[8] = 'e';
	expected[9] = 's';
	expected[10] = 'h';
	expected[11] = 'e';
	expected[12] = 's';
	expected[13] = '\0';
	TEST_ASSERT_EQUAL_STRING(expected, buf);

	/* Already has trailing sep on left → no double sep. */
	{
		char left[16];
		left[0] = 'a';
		left[1] = sep;
		left[2] = '\0';
		n = sk_path_join(sk_str_view_cstr(left), sk_str_view_cstr("b"), buf, sizeof(buf));
		TEST_ASSERT_EQUAL_INT(3, n);
		TEST_ASSERT_EQUAL_CHAR('a', buf[0]);
		TEST_ASSERT_EQUAL_CHAR(sep, buf[1]);
		TEST_ASSERT_EQUAL_CHAR('b', buf[2]);
	}

	/* Empty segment skipped. */
	n = sk_path_join(sk_str_view_cstr("a"), sk_str_view_cstr(""), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(1, n);
	TEST_ASSERT_EQUAL_STRING("a", buf);

	/* Normalize alternate separator in segment. */
	n = sk_path_join(sk_str_view_cstr("a\\b"), sk_str_view_cstr("c/d"), buf, sizeof(buf));
	TEST_ASSERT_TRUE(n > 0);
	TEST_ASSERT_EQUAL_CHAR('a', buf[0]);
	TEST_ASSERT_EQUAL_CHAR(sep, buf[1]);
	TEST_ASSERT_EQUAL_CHAR('b', buf[2]);
	TEST_ASSERT_EQUAL_CHAR(sep, buf[3]);
	TEST_ASSERT_EQUAL_CHAR('c', buf[4]);
	TEST_ASSERT_EQUAL_CHAR(sep, buf[5]);
	TEST_ASSERT_EQUAL_CHAR('d', buf[6]);
	TEST_ASSERT_EQUAL_CHAR('\0', buf[7]);

	/* join_n three parts */
	{
		sk_str_view_t parts[3];
		parts[0] = sk_str_view_cstr("a");
		parts[1] = sk_str_view_cstr("b");
		parts[2] = sk_str_view_cstr("c");
		n = sk_path_join_n(parts, 3u, buf, sizeof(buf));
		TEST_ASSERT_EQUAL_INT(5, n);
		TEST_ASSERT_EQUAL_CHAR('a', buf[0]);
		TEST_ASSERT_EQUAL_CHAR(sep, buf[1]);
		TEST_ASSERT_EQUAL_CHAR('b', buf[2]);
		TEST_ASSERT_EQUAL_CHAR(sep, buf[3]);
		TEST_ASSERT_EQUAL_CHAR('c', buf[4]);
	}

	TEST_ASSERT_EQUAL_INT(-1, sk_path_join(sk_str_view_cstr("a"), sk_str_view_cstr("b"), buf, 2));
}

SK_TEST(path_extract_name) {
	char buf[64];
	char parent_with_sep[16];
	char full[32];
	char sep = (char)SK_PATH_SEPARATOR;

	parent_with_sep[0] = 'a';
	parent_with_sep[1] = 's';
	parent_with_sep[2] = 's';
	parent_with_sep[3] = 'e';
	parent_with_sep[4] = 't';
	parent_with_sep[5] = 's';
	parent_with_sep[6] = sep;
	parent_with_sep[7] = '\0';

	full[0] = 'a';
	full[1] = 's';
	full[2] = 's';
	full[3] = 'e';
	full[4] = 't';
	full[5] = 's';
	full[6] = sep;
	full[7] = 'f';
	full[8] = 'o';
	full[9] = 'o';
	full[10] = sep;
	full[11] = 'b';
	full[12] = 'a';
	full[13] = 'r';
	full[14] = '\0';

	/* Strips host separators from the suffix → "foobar". */
	i32 n = sk_path_extract_name(sk_str_view_cstr(parent_with_sep), sk_str_view_cstr(full), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(6, n);
	TEST_ASSERT_EQUAL_STRING("foobar", buf);

	n = sk_path_extract_name(sk_str_view_cstr("assets"), sk_str_view_cstr("assets"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(0, n);
	TEST_ASSERT_EQUAL_STRING("", buf);

	n = sk_path_extract_name(sk_str_view_cstr("longer_than_path"), sk_str_view_cstr("short"), buf, sizeof(buf));
	TEST_ASSERT_EQUAL_INT(0, n);
	TEST_ASSERT_EQUAL_STRING("", buf);
}

SK_TEST(path_is_sep) {
	TEST_ASSERT_EQUAL_INT(1, sk_path_is_sep('/'));
	TEST_ASSERT_EQUAL_INT(1, sk_path_is_sep('\\'));
	TEST_ASSERT_EQUAL_INT(0, sk_path_is_sep('.'));
	TEST_ASSERT_EQUAL_INT(0, sk_path_is_sep('a'));
}
#endif /* SK_TESTS */
