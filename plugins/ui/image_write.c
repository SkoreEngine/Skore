/*
 * image_write.c — PNG output for sk_ui_cpu_image_t (APX-227).
 *
 * Writes tightly packed RGBA8 CPU images (from headless capture readback)
 * as PNG via vendored stb_image_write. Parent directories are created as
 * needed through the host filesystem API. Test artifacts share a single
 * configurable root (env SK_TEST_ARTIFACT_DIR, else compile-time default,
 * else {temp}/skore-test-artifacts) so nothing is scattered under source.
 */

#include "ui.internal.h"

#include "filesystem.h"
#include "logger.h"
#include "path.h"

#include "stb_image_write.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Logging                                                                    */
/* -------------------------------------------------------------------------- */

static sk_logger_t* ui_image_write_logger(void) {
	static sk_logger_t* log = NULL;
	if (log == NULL) {
		log = sk_logger_api()->create_logger("ui-image-write");
	}
	return log;
}

static void ui_image_write_fail(const_chr_t fmt, ...) {
	va_list args;
	va_start(args, fmt);
	sk_log_messagev(sk_logger_api(), SK_LOGGER_TYPE_ERROR, ui_image_write_logger(), fmt, args);
	va_end(args);
}

/* -------------------------------------------------------------------------- */
/* Parent directories                                                         */
/* -------------------------------------------------------------------------- */

/**
 * Create every missing directory on the path to @p path's parent.
 * @p path is a full file path; only parent segments are created.
 * @return 0 on success (parent exists or was created), non-zero on failure.
 */
static i32 ui_ensure_parent_dirs(const sk_filesystem_api_t* fs, const_chr_t path) {
	char parent[SK_FS_PATH_MAX];
	char partial[SK_FS_PATH_MAX];
	u32 i;
	u32 len;
	i32 n;

	if (fs == NULL || path == NULL || path[0] == '\0') {
		return -1;
	}

	n = sk_path_parent(sk_str_view_cstr(path), parent, (u32)sizeof(parent));
	if (n < 0) {
		return -1;
	}
	if (parent[0] == '\0') {
		return 0; /* no parent (bare filename in cwd) */
	}

	/* Already a directory? */
	if (fs->get_file_status(parent) == SK_FILE_STATUS_DIRECTORY) {
		return 0;
	}

	/*
	 * Walk segments left-to-right, creating each prefix. Separators '/' and
	 * '\\' are both accepted; absolute roots ("/" or "C:\") are preserved.
	 */
	len = 0u;
	for (i = 0u; parent[i] != '\0'; ++i) {
		char c = parent[i];
		if (len + 1u >= (u32)sizeof(partial)) {
			return -1;
		}
		partial[len++] = c;
		partial[len] = '\0';

		if (!sk_path_is_sep(c)) {
			continue;
		}
		/* Skip consecutive separators and the root-only prefix. */
		if (len <= 1u) {
			continue;
		}
		/* Drop trailing separator for create_directory. */
		partial[len - 1u] = '\0';
		if (partial[0] == '\0') {
			partial[len - 1u] = c;
			continue;
		}
		if (fs->get_file_status(partial) == SK_FILE_STATUS_DIRECTORY) {
			partial[len - 1u] = c;
			continue;
		}
		if (fs->create_directory(partial) != 0) {
			/* Race: another process may have created it. */
			if (fs->get_file_status(partial) != SK_FILE_STATUS_DIRECTORY) {
				ui_image_write_fail("cpu_image_write_png: failed to create parent directory '%s' for '%s'", partial, path);
				return -1;
			}
		}
		partial[len - 1u] = c;
	}

	/* Final segment (no trailing separator). */
	if (fs->get_file_status(parent) != SK_FILE_STATUS_DIRECTORY) {
		if (fs->create_directory(parent) != 0 && fs->get_file_status(parent) != SK_FILE_STATUS_DIRECTORY) {
			ui_image_write_fail("cpu_image_write_png: failed to create parent directory '%s' for '%s'", parent, path);
			return -1;
		}
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Artifact root + deterministic naming                                       */
/* -------------------------------------------------------------------------- */

/**
 * Sanitize a test/scene name into a single path component (no separators).
 * Unsafe / path-like characters become '_'. Empty input becomes "capture".
 */
static void ui_sanitize_artifact_name(const_chr_t name, char* out, u32 out_cap) {
	u32 di = 0u;
	u32 i;

	if (out_cap == 0u) {
		return;
	}
	if (name == NULL || name[0] == '\0') {
		const char* fallback = "capture";
		for (i = 0u; fallback[i] != '\0' && di + 1u < out_cap; ++i) {
			out[di++] = fallback[i];
		}
		out[di] = '\0';
		return;
	}

	for (i = 0u; name[i] != '\0' && di + 1u < out_cap; ++i) {
		char c = name[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
			out[di++] = c;
		} else {
			/* Collapse path separators and other junk to underscore. */
			if (di == 0u || out[di - 1u] != '_') {
				out[di++] = '_';
			}
		}
	}
	/* Trim trailing underscores. */
	while (di > 0u && out[di - 1u] == '_') {
		--di;
	}
	if (di == 0u) {
		const char* fallback = "capture";
		for (i = 0u; fallback[i] != '\0' && di + 1u < out_cap; ++i) {
			out[di++] = fallback[i];
		}
	}
	out[di] = '\0';
}

i32 ui_test_artifact_root_impl(const sk_filesystem_api_t* fs, char* out, u32 out_cap) {
	const char* env;
	char temp[SK_FS_PATH_MAX];
	i32 n;

	if (out == NULL || out_cap == 0u) {
		return -1;
	}
	out[0] = '\0';

	/* 1) Runtime override — never scatter into the source tree. */
	env = getenv("SK_TEST_ARTIFACT_DIR");
	if (env != NULL && env[0] != '\0') {
		n = (i32)strlen(env);
		if ((u32)n + 1u > out_cap) {
			return -1;
		}
		memcpy(out, env, (size_t)n + 1u);
		return 0;
	}

	/* 2) Compile-time default from the build system (build/test-artifacts). */
#if defined(SK_TEST_ARTIFACT_DIR)
	{
		const char* def = SK_TEST_ARTIFACT_DIR;
		n = (i32)strlen(def);
		if ((u32)n + 1u > out_cap) {
			return -1;
		}
		memcpy(out, def, (size_t)n + 1u);
		return 0;
	}
#endif

	/* 3) Fallback: {temp_folder}/skore-test-artifacts (still off source tree). */
	if (fs == NULL) {
		ui_image_write_fail("test_artifact_root: no SK_TEST_ARTIFACT_DIR and no filesystem for temp fallback");
		return -1;
	}
	if (fs->temp_folder(temp, (u32)sizeof(temp)) != 0 || temp[0] == '\0') {
		ui_image_write_fail("test_artifact_root: temp_folder failed");
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(temp), sk_str_view_cstr("skore-test-artifacts"), out, out_cap);
	return (n < 0) ? -1 : 0;
}

i32 ui_test_artifact_png_path_impl(const sk_filesystem_api_t* fs, const_chr_t name, char* out, u32 out_cap) {
	char root[SK_FS_PATH_MAX];
	char base[256];
	char file[288];
	i32 n;

	if (out == NULL || out_cap == 0u) {
		return -1;
	}
	out[0] = '\0';

	if (ui_test_artifact_root_impl(fs, root, (u32)sizeof(root)) != 0) {
		return -1;
	}
	ui_sanitize_artifact_name(name, base, (u32)sizeof(base));
	n = snprintf(file, sizeof(file), "%s.png", base);
	if (n < 0 || (u32)n >= (u32)sizeof(file)) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(root), sk_str_view_cstr(file), out, out_cap);
	return (n < 0) ? -1 : 0;
}

/* -------------------------------------------------------------------------- */
/* PNG write                                                                  */
/* -------------------------------------------------------------------------- */

i32 ui_cpu_image_write_png_impl(const sk_ui_cpu_image_t* image, const sk_filesystem_api_t* fs, const_chr_t path) {
	i32 stride;
	i32 ok;

	if (image == NULL || image->pixels == NULL || path == NULL || path[0] == '\0') {
		ui_image_write_fail("cpu_image_write_png: invalid image or path");
		return -1;
	}
	if (image->width == 0u || image->height == 0u || image->channels == 0u) {
		ui_image_write_fail("cpu_image_write_png: empty image (w=%u h=%u ch=%u) path='%s'", image->width, image->height, image->channels, path);
		return -1;
	}
	/* Capture path always uses 4; allow 1–4 for generality. */
	if (image->channels > 4u) {
		ui_image_write_fail("cpu_image_write_png: unsupported channel count %u (max 4) path='%s'", image->channels, path);
		return -1;
	}

	if (fs != NULL) {
		if (ui_ensure_parent_dirs(fs, path) != 0) {
			return -1;
		}
	}

	/* Tight pack: stride = width * channels (matches sk_ui_cpu_image_t). */
	stride = (i32)(image->width * image->channels);
	ok = stbi_write_png(path, (int)image->width, (int)image->height, (int)image->channels, image->pixels, stride);
	if (ok == 0) {
		ui_image_write_fail("cpu_image_write_png: stbi_write_png failed for '%s' (%ux%u ch=%u)", path, image->width, image->height, image->channels);
		return -1;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Tests — write known image, read back with stb_image                        */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS

#include "test.h"

#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <errno.h>
#include <sys/stat.h>
#endif

/* Declarations only — implementation is the stb_image static lib (one TU). */
#include "stb_image.h"

static const sk_ui_api_t* ui_image_write_test_api(void) {
	return ui_get_api_table();
}

/*
 * Minimal filesystem mock for unit tests (plugin TU cannot call sk_filesystem_api).
 * create_directory / get_file_status use portable host calls only inside SK_TESTS.
 */

static sk_file_status_t ui_iw_mock_status(const_chr_t path) {
	struct stat st;
	if (path == NULL || path[0] == '\0') {
		return SK_FILE_STATUS_NOT_FOUND;
	}
	if (stat(path, &st) != 0) {
		return SK_FILE_STATUS_NOT_FOUND;
	}
	if (S_ISDIR(st.st_mode)) {
		return SK_FILE_STATUS_DIRECTORY;
	}
	if (S_ISREG(st.st_mode)) {
		return SK_FILE_STATUS_FILE;
	}
	return SK_FILE_STATUS_OTHER;
}

static i32 ui_iw_mock_mkdir(const_chr_t path) {
#if defined(_WIN32)
	if (_mkdir(path) == 0) {
		return 0;
	}
#else
	if (mkdir(path, 0755) == 0) {
		return 0;
	}
	if (errno == EEXIST && ui_iw_mock_status(path) == SK_FILE_STATUS_DIRECTORY) {
		return 0;
	}
#endif
	return ui_iw_mock_status(path) == SK_FILE_STATUS_DIRECTORY ? 0 : -1;
}

static i32 ui_iw_mock_temp(char* out, u32 out_cap) {
	const char* t = getenv("TMPDIR");
	if (t == NULL || t[0] == '\0') {
		t = getenv("TEMP");
	}
	if (t == NULL || t[0] == '\0') {
#if defined(_WIN32)
		t = ".";
#else
		t = "/tmp";
#endif
	}
	if ((u32)strlen(t) + 1u > out_cap) {
		return -1;
	}
	memcpy(out, t, strlen(t) + 1u);
	return 0;
}

static void ui_iw_fill_mock_fs(sk_filesystem_api_t* fs) {
	memset(fs, 0, sizeof(*fs));
	fs->get_file_status = ui_iw_mock_status;
	fs->create_directory = ui_iw_mock_mkdir;
	fs->temp_folder = ui_iw_mock_temp;
}

SK_TEST(ui_cpu_image_write_png_roundtrip) {
	const sk_ui_api_t* ui = ui_image_write_test_api();
	sk_filesystem_api_t fs;
	sk_ui_cpu_image_t img;
	char path[SK_FS_PATH_MAX];
	u8 pixels[4u * 4u * 4u]; /* 4x4 RGBA */
	u32 x;
	u32 y;
	int rw = 0;
	int rh = 0;
	int rch = 0;
	u8* loaded = NULL;
	u32 i;

	ui_iw_fill_mock_fs(&fs);

	/* Deterministic known pattern: R=x*40, G=y*40, B=128, A=255. */
	for (y = 0u; y < 4u; ++y) {
		for (x = 0u; x < 4u; ++x) {
			u32 o = (y * 4u + x) * 4u;
			pixels[o + 0u] = (u8)(x * 40u);
			pixels[o + 1u] = (u8)(y * 40u);
			pixels[o + 2u] = 128u;
			pixels[o + 3u] = 255u;
		}
	}
	memset(&img, 0, sizeof(img));
	img.width = 4u;
	img.height = 4u;
	img.channels = 4u;
	img.pixels = pixels;

	TEST_ASSERT_EQUAL_INT(0, ui->test_artifact_png_path(&fs, "ui_cpu_image_write_png_roundtrip", path, (u32)sizeof(path)));
	TEST_ASSERT_TRUE(strlen(path) > 4u);
	/* Ends with .png */
	TEST_ASSERT_EQUAL_STRING(".png", path + strlen(path) - 4u);

	TEST_ASSERT_EQUAL_INT_MESSAGE(0, ui->cpu_image_write_png(&img, &fs, path), "PNG write failed — see ui-image-write log");

	loaded = stbi_load(path, &rw, &rh, &rch, 4);
	TEST_ASSERT_NOT_NULL_MESSAGE(loaded, "stbi_load failed to read written PNG");
	TEST_ASSERT_EQUAL_INT(4, rw);
	TEST_ASSERT_EQUAL_INT(4, rh);
	TEST_ASSERT_EQUAL_INT(4, rch);

	for (i = 0u; i < 4u * 4u * 4u; ++i) {
		if (loaded[i] != pixels[i]) {
			char msg[160];
			snprintf(msg, sizeof(msg), "pixel byte mismatch at %u: got %u expected %u", i, (u32)loaded[i], (u32)pixels[i]);
			stbi_image_free(loaded);
			TEST_FAIL_MESSAGE(msg);
		}
	}
	stbi_image_free(loaded);
}

SK_TEST(ui_test_artifact_png_path_sanitizes_name) {
	const sk_ui_api_t* ui = ui_image_write_test_api();
	sk_filesystem_api_t fs;
	char path[SK_FS_PATH_MAX];
	char root[SK_FS_PATH_MAX];

	ui_iw_fill_mock_fs(&fs);
	TEST_ASSERT_EQUAL_INT(0, ui->test_artifact_root(&fs, root, (u32)sizeof(root)));
	TEST_ASSERT_TRUE(root[0] != '\0');

	TEST_ASSERT_EQUAL_INT(0, ui->test_artifact_png_path(&fs, "scene/foo bar:baz", path, (u32)sizeof(path)));
	/* No path separators inside the file component — only under the root. */
	{
		const char* base = path;
		const char* p;
		for (p = path; *p != '\0'; ++p) {
			if (sk_path_is_sep(*p)) {
				base = p + 1;
			}
		}
		TEST_ASSERT_TRUE(strstr(base, "/") == NULL);
		TEST_ASSERT_TRUE(strstr(base, "\\") == NULL);
		TEST_ASSERT_TRUE(strstr(base, " ") == NULL);
		TEST_ASSERT_TRUE(strstr(base, ":") == NULL);
		TEST_ASSERT_TRUE(strstr(base, ".png") != NULL);
	}
}

SK_TEST(ui_cpu_image_write_png_rejects_bad_image) {
	const sk_ui_api_t* ui = ui_image_write_test_api();
	sk_filesystem_api_t fs;
	sk_ui_cpu_image_t img;
	u8 px[4] = {1u, 2u, 3u, 4u};

	ui_iw_fill_mock_fs(&fs);
	memset(&img, 0, sizeof(img));
	TEST_ASSERT_NOT_EQUAL(0, ui->cpu_image_write_png(NULL, &fs, "/tmp/x.png"));
	TEST_ASSERT_NOT_EQUAL(0, ui->cpu_image_write_png(&img, &fs, "/tmp/x.png"));
	img.pixels = px;
	img.width = 1u;
	img.height = 1u;
	img.channels = 0u;
	TEST_ASSERT_NOT_EQUAL(0, ui->cpu_image_write_png(&img, &fs, "/tmp/x.png"));
}

#endif /* SK_TESTS */
