/*
 * image_compare.c — golden-image comparison with tolerance and diff artifacts (APX-229).
 *
 * Compares a captured sk_ui_cpu_image_t against either another in-memory image
 * or a committed golden PNG. Failures produce actionable output:
 *   - clear size-mismatch message (no silent pixel scan on wrong dimensions)
 *   - per-pixel compare with configurable channel tolerance + max fail fraction
 *   - on mismatch: actual / expected / visual-diff PNGs under the test-artifact
 *     root, plus differ count, max channel delta, and differing-region bbox
 *   - opt-in bless mode only (params->update_golden or SK_UI_REGEN_GOLDENS);
 *     never the default
 */

#include "ui_internal.h"

#include "filesystem.h"
#include "logger.h"
#include "path.h"

#include "stb_image.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Logging                                                                    */
/* -------------------------------------------------------------------------- */

static sk_logger_t* ui_image_compare_logger(void) {
	static sk_logger_t* log = NULL;
	const sk_logger_api_t* api = ui_logger_api();
	sk_logger_context_t* log_ctx = ui_logger_context();
	if (log == NULL && api != NULL && log_ctx != NULL) {
		log = api->create_logger(log_ctx, "ui-image-compare");
	}
	return log;
}

static void ui_image_compare_info(const_chr_t fmt, ...) {
	const sk_logger_api_t* api = ui_logger_api();
	sk_logger_t* log = ui_image_compare_logger();
	va_list args;
	if (api == NULL || log == NULL) {
		return;
	}
	va_start(args, fmt);
	sk_log_messagev(api, SK_LOGGER_TYPE_INFO, log, fmt, args);
	va_end(args);
}

static void ui_image_compare_fail(const_chr_t fmt, ...) {
	const sk_logger_api_t* api = ui_logger_api();
	sk_logger_t* log = ui_image_compare_logger();
	va_list args;
	if (api == NULL || log == NULL) {
		return;
	}
	va_start(args, fmt);
	sk_log_messagev(api, SK_LOGGER_TYPE_ERROR, log, fmt, args);
	va_end(args);
}

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static u32 ui_ic_abs_u8(u8 a, u8 b) {
	return a > b ? (u32)(a - b) : (u32)(b - a);
}

static void ui_ic_stats_clear(sk_ui_image_compare_stats_t* stats) {
	if (stats == NULL) {
		return;
	}
	memset(stats, 0, sizeof(*stats));
}

/** Non-zero when blessing is requested via flag or SK_UI_REGEN_GOLDENS. */
static i32 ui_ic_env_regen_goldens(void) {
	const char* v = getenv("SK_UI_REGEN_GOLDENS");
	if (v == NULL || v[0] == '\0') {
		return 0;
	}
	if (v[0] == '0' && v[1] == '\0') {
		return 0;
	}
	return 1;
}

static i32 ui_ic_should_update(const sk_ui_image_compare_params_t* params) {
	if (params != NULL && params->update_golden != 0) {
		return 1;
	}
	return ui_ic_env_regen_goldens();
}

/**
 * Build {root}/{sanitized}_{suffix}.png under the shared test-artifact root.
 * @return 0 on success.
 */
static i32 ui_ic_artifact_path(const sk_filesystem_api_t* fs, const_chr_t name, const_chr_t suffix, char* out, u32 out_cap) {
	char root[SK_FS_PATH_MAX];
	char base[256];
	char file[320];
	const char* n = (name != NULL && name[0] != '\0') ? name : "image_compare";
	i32 sn;
	i32 jn;
	u32 di = 0u;
	u32 i;

	if (out == NULL || out_cap == 0u || suffix == NULL) {
		return -1;
	}
	out[0] = '\0';

	if (ui_test_artifact_root_impl(fs, root, (u32)sizeof(root)) != 0) {
		return -1;
	}

	/* Same sanitization rules as image_write (single path component). */
	for (i = 0u; n[i] != '\0' && di + 1u < (u32)sizeof(base); ++i) {
		char c = n[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
			base[di++] = c;
		} else if (di == 0u || base[di - 1u] != '_') {
			base[di++] = '_';
		}
	}
	while (di > 0u && base[di - 1u] == '_') {
		--di;
	}
	if (di == 0u) {
		const char* fb = "image_compare";
		for (i = 0u; fb[i] != '\0' && di + 1u < (u32)sizeof(base); ++i) {
			base[di++] = fb[i];
		}
	}
	base[di] = '\0';

	sn = snprintf(file, sizeof(file), "%s_%s.png", base, suffix);
	if (sn < 0 || (u32)sn >= (u32)sizeof(file)) {
		return -1;
	}
	jn = sk_path_join(sk_str_view_cstr(root), sk_str_view_cstr(file), out, out_cap);
	return (jn < 0) ? -1 : 0;
}

static void ui_ic_print_summary(const sk_ui_image_compare_stats_t* stats, i32 rc) {
	const char* label;
	f64 frac_pct;

	if (stats == NULL) {
		return;
	}
	switch (rc) {
	case SK_UI_IMAGE_COMPARE_OK:
		label = "ok";
		break;
	case SK_UI_IMAGE_COMPARE_MISMATCH:
		label = "mismatch";
		break;
	case SK_UI_IMAGE_COMPARE_SIZE_MISMATCH:
		label = "size_mismatch";
		break;
	case SK_UI_IMAGE_COMPARE_MISSING_GOLDEN:
		label = "missing_golden";
		break;
	default:
		label = "error";
		break;
	}

	if (stats->size_mismatch != 0) {
		ui_image_compare_fail("image_compare: %s — dimensions differ: actual %ux%u vs expected %ux%u "
							  "(pixel scan skipped)",
							  label, stats->actual_width, stats->actual_height, stats->expected_width, stats->expected_height);
	} else {
		frac_pct = stats->pixel_count > 0u ? (100.0 * (f64)stats->differ_count / (f64)stats->pixel_count) : 0.0;
		ui_image_compare_fail("image_compare: %s — differ_pixels=%u/%u (%.2f%%) max_channel_delta=%u "
							  "bbox=[%u,%u]..[%u,%u]",
							  label, stats->differ_count, stats->pixel_count, frac_pct, stats->max_channel_delta, stats->bbox_min_x, stats->bbox_min_y, stats->bbox_max_x,
							  stats->bbox_max_y);
	}
	if (stats->actual_path[0] != '\0') {
		ui_image_compare_fail("image_compare: actual  → %s", stats->actual_path);
	}
	if (stats->expected_path[0] != '\0') {
		ui_image_compare_fail("image_compare: expected → %s", stats->expected_path);
	}
	if (stats->diff_path[0] != '\0') {
		ui_image_compare_fail("image_compare: diff     → %s", stats->diff_path);
	}
}

/* -------------------------------------------------------------------------- */
/* In-memory compare                                                          */
/* -------------------------------------------------------------------------- */

i32 ui_cpu_image_compare_impl(const sk_ui_cpu_image_t* actual, const sk_ui_cpu_image_t* expected, u32 channel_tolerance, f32 max_diff_fraction,
							  sk_ui_image_compare_stats_t* out_stats, u8* out_diff_rgba) {
	sk_ui_image_compare_stats_t local;
	sk_ui_image_compare_stats_t* stats = (out_stats != NULL) ? out_stats : &local;
	u32 channels;
	u32 x;
	u32 y;
	u32 w;
	u32 h;
	i32 bbox_init = 0;
	f32 frac;

	ui_ic_stats_clear(stats);

	if (actual == NULL || expected == NULL || actual->pixels == NULL || expected->pixels == NULL) {
		ui_image_compare_fail("image_compare: null actual/expected image");
		return SK_UI_IMAGE_COMPARE_ERROR;
	}
	if (actual->width == 0u || actual->height == 0u || expected->width == 0u || expected->height == 0u) {
		ui_image_compare_fail("image_compare: empty image dimensions");
		return SK_UI_IMAGE_COMPARE_ERROR;
	}
	if (actual->channels == 0u || actual->channels > 4u || expected->channels == 0u || expected->channels > 4u) {
		ui_image_compare_fail("image_compare: unsupported channel count actual=%u expected=%u", actual->channels, expected->channels);
		return SK_UI_IMAGE_COMPARE_ERROR;
	}

	stats->actual_width = actual->width;
	stats->actual_height = actual->height;
	stats->expected_width = expected->width;
	stats->expected_height = expected->height;

	if (actual->width != expected->width || actual->height != expected->height) {
		stats->size_mismatch = 1;
		ui_image_compare_fail("image_compare: size mismatch — actual %ux%u vs expected %ux%u "
							  "(failing immediately; no per-pixel scan)",
							  actual->width, actual->height, expected->width, expected->height);
		return SK_UI_IMAGE_COMPARE_SIZE_MISMATCH;
	}

	/* Use the smaller channel count so RGB vs RGBA still compares RGB. */
	channels = actual->channels < expected->channels ? actual->channels : expected->channels;
	w = actual->width;
	h = actual->height;
	stats->pixel_count = w * h;

	for (y = 0u; y < h; ++y) {
		for (x = 0u; x < w; ++x) {
			const u32 pix = y * w + x;
			const u32 ia = pix * actual->channels;
			const u32 ie = pix * expected->channels;
			const u32 id = pix * 4u; /* visual diff always RGBA8 */
			u32 c;
			u32 maxd = 0u;
			i32 fail = 0;

			for (c = 0u; c < channels; ++c) {
				u32 d = ui_ic_abs_u8(actual->pixels[ia + c], expected->pixels[ie + c]);
				if (d > maxd) {
					maxd = d;
				}
			}
			if (maxd > stats->max_channel_delta) {
				stats->max_channel_delta = maxd;
			}
			if (maxd > channel_tolerance) {
				fail = 1;
				stats->differ_count += 1u;
				if (bbox_init == 0) {
					stats->bbox_min_x = x;
					stats->bbox_min_y = y;
					stats->bbox_max_x = x;
					stats->bbox_max_y = y;
					bbox_init = 1;
				} else {
					if (x < stats->bbox_min_x) {
						stats->bbox_min_x = x;
					}
					if (y < stats->bbox_min_y) {
						stats->bbox_min_y = y;
					}
					if (x > stats->bbox_max_x) {
						stats->bbox_max_x = x;
					}
					if (y > stats->bbox_max_y) {
						stats->bbox_max_y = y;
					}
				}
			}

			if (out_diff_rgba != NULL) {
				if (fail != 0) {
					/* Highlight differing pixels in opaque red. */
					out_diff_rgba[id + 0u] = 255u;
					out_diff_rgba[id + 1u] = 0u;
					out_diff_rgba[id + 2u] = 0u;
					out_diff_rgba[id + 3u] = 255u;
				} else {
					/* Dim actual so the red stand-outs are obvious. */
					u8 r = (actual->channels > 0u) ? actual->pixels[ia + 0u] : 0u;
					u8 g = (actual->channels > 1u) ? actual->pixels[ia + 1u] : r;
					u8 b = (actual->channels > 2u) ? actual->pixels[ia + 2u] : r;
					out_diff_rgba[id + 0u] = (u8)(r / 4u);
					out_diff_rgba[id + 1u] = (u8)(g / 4u);
					out_diff_rgba[id + 2u] = (u8)(b / 4u);
					out_diff_rgba[id + 3u] = 255u;
				}
			}
		}
	}

	frac = (stats->pixel_count > 0u) ? (f32)stats->differ_count / (f32)stats->pixel_count : 0.0f;
	if (frac > max_diff_fraction) {
		return SK_UI_IMAGE_COMPARE_MISMATCH;
	}
	return SK_UI_IMAGE_COMPARE_OK;
}

/* -------------------------------------------------------------------------- */
/* Golden file compare + artifacts + bless                                    */
/* -------------------------------------------------------------------------- */

i32 ui_cpu_image_compare_golden_impl(const sk_ui_cpu_image_t* actual, const_chr_t golden_path, const sk_ui_image_compare_params_t* params, const sk_filesystem_api_t* fs,
									 sk_ui_image_compare_stats_t* out_stats) {
	sk_ui_image_compare_stats_t local;
	sk_ui_image_compare_stats_t* stats = (out_stats != NULL) ? out_stats : &local;
	sk_ui_image_compare_params_t def;
	const sk_ui_image_compare_params_t* p;
	sk_ui_cpu_image_t expected;
	u8* golden_pixels = NULL;
	u8* diff_pixels = NULL;
	int gw = 0;
	int gh = 0;
	int gch = 0;
	i32 rc;
	i32 write_rc;
	u32 channel_tol;
	f32 max_frac;
	const_chr_t name;

	ui_ic_stats_clear(stats);

	if (actual == NULL || actual->pixels == NULL || golden_path == NULL || golden_path[0] == '\0') {
		ui_image_compare_fail("image_compare_golden: invalid actual or golden_path");
		return SK_UI_IMAGE_COMPARE_ERROR;
	}
	if (actual->width == 0u || actual->height == 0u || actual->channels == 0u || actual->channels > 4u) {
		ui_image_compare_fail("image_compare_golden: empty/unsupported actual image");
		return SK_UI_IMAGE_COMPARE_ERROR;
	}

	memset(&def, 0, sizeof(def));
	def.name = "image_compare";
	p = (params != NULL) ? params : &def;
	channel_tol = p->channel_tolerance;
	max_frac = p->max_diff_fraction;
	if (max_frac < 0.0f) {
		max_frac = 0.0f;
	}
	name = (p->name != NULL && p->name[0] != '\0') ? p->name : "image_compare";

	stats->actual_width = actual->width;
	stats->actual_height = actual->height;

	/* ---- Explicit bless / update (never default) ---- */
	if (ui_ic_should_update(p) != 0) {
		write_rc = ui_cpu_image_write_png_impl(actual, fs, golden_path);
		if (write_rc != 0) {
			ui_image_compare_fail("image_compare_golden: failed to write blessed golden '%s'", golden_path);
			return SK_UI_IMAGE_COMPARE_ERROR;
		}
		stats->updated = 1;
		stats->expected_width = actual->width;
		stats->expected_height = actual->height;
		stats->pixel_count = actual->width * actual->height;
		ui_image_compare_info("image_compare_golden: BLESSED golden %s (%ux%u) — review before commit", golden_path, actual->width, actual->height);
		return SK_UI_IMAGE_COMPARE_OK;
	}

	/* ---- Load golden PNG ---- */
	golden_pixels = stbi_load(golden_path, &gw, &gh, &gch, (int)actual->channels);
	if (golden_pixels == NULL || gw <= 0 || gh <= 0) {
		/* Still dump actual so the developer can inspect / decide to bless. */
		if (ui_ic_artifact_path(fs, name, "actual", stats->actual_path, (u32)sizeof(stats->actual_path)) == 0) {
			(void)ui_cpu_image_write_png_impl(actual, fs, stats->actual_path);
		}
		ui_image_compare_fail("image_compare_golden: missing or unreadable golden '%s' — "
							  "run with SK_UI_REGEN_GOLDENS=1 or params.update_golden=1 to create it; "
							  "actual written to '%s'",
							  golden_path, stats->actual_path[0] != '\0' ? stats->actual_path : "(none)");
		return SK_UI_IMAGE_COMPARE_MISSING_GOLDEN;
	}

	memset(&expected, 0, sizeof(expected));
	expected.width = (u32)gw;
	expected.height = (u32)gh;
	expected.channels = actual->channels;
	expected.pixels = golden_pixels;
	stats->expected_width = expected.width;
	stats->expected_height = expected.height;

	/* Size mismatch: fail immediately, still write actual + expected. */
	if (actual->width != expected.width || actual->height != expected.height) {
		stats->size_mismatch = 1;
		if (ui_ic_artifact_path(fs, name, "actual", stats->actual_path, (u32)sizeof(stats->actual_path)) == 0) {
			(void)ui_cpu_image_write_png_impl(actual, fs, stats->actual_path);
		}
		if (ui_ic_artifact_path(fs, name, "expected", stats->expected_path, (u32)sizeof(stats->expected_path)) == 0) {
			(void)ui_cpu_image_write_png_impl(&expected, fs, stats->expected_path);
		}
		ui_ic_print_summary(stats, SK_UI_IMAGE_COMPARE_SIZE_MISMATCH);
		stbi_image_free(golden_pixels);
		return SK_UI_IMAGE_COMPARE_SIZE_MISMATCH;
	}

	diff_pixels = (u8*)malloc((size_t)actual->width * (size_t)actual->height * 4u);
	if (diff_pixels == NULL) {
		ui_image_compare_fail("image_compare_golden: OOM allocating diff buffer");
		stbi_image_free(golden_pixels);
		return SK_UI_IMAGE_COMPARE_ERROR;
	}

	rc = ui_cpu_image_compare_impl(actual, &expected, channel_tol, max_frac, stats, diff_pixels);

	if (rc == SK_UI_IMAGE_COMPARE_MISMATCH) {
		sk_ui_cpu_image_t diff_img;
		if (ui_ic_artifact_path(fs, name, "actual", stats->actual_path, (u32)sizeof(stats->actual_path)) == 0) {
			(void)ui_cpu_image_write_png_impl(actual, fs, stats->actual_path);
		}
		if (ui_ic_artifact_path(fs, name, "expected", stats->expected_path, (u32)sizeof(stats->expected_path)) == 0) {
			(void)ui_cpu_image_write_png_impl(&expected, fs, stats->expected_path);
		}
		memset(&diff_img, 0, sizeof(diff_img));
		diff_img.width = actual->width;
		diff_img.height = actual->height;
		diff_img.channels = 4u;
		diff_img.pixels = diff_pixels;
		if (ui_ic_artifact_path(fs, name, "diff", stats->diff_path, (u32)sizeof(stats->diff_path)) == 0) {
			(void)ui_cpu_image_write_png_impl(&diff_img, fs, stats->diff_path);
		}
		ui_ic_print_summary(stats, rc);
	}

	free(diff_pixels);
	stbi_image_free(golden_pixels);
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Unit tests — synthetic images                                              */
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

static const sk_ui_api_t* ui_ic_test_api(void) {
	return ui_get_api_table();
}

static sk_file_status_t ui_ic_mock_status(const_chr_t path) {
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

static i32 ui_ic_mock_mkdir(const_chr_t path) {
#if defined(_WIN32)
	if (_mkdir(path) == 0) {
		return 0;
	}
#else
	if (mkdir(path, 0755) == 0) {
		return 0;
	}
	if (errno == EEXIST && ui_ic_mock_status(path) == SK_FILE_STATUS_DIRECTORY) {
		return 0;
	}
#endif
	return ui_ic_mock_status(path) == SK_FILE_STATUS_DIRECTORY ? 0 : -1;
}

static i32 ui_ic_mock_temp(char* out, u32 out_cap) {
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

static void ui_ic_fill_mock_fs(sk_filesystem_api_t* fs) {
	memset(fs, 0, sizeof(*fs));
	fs->get_file_status = ui_ic_mock_status;
	fs->create_directory = ui_ic_mock_mkdir;
	fs->temp_folder = ui_ic_mock_temp;
}

/** Fill a W×H RGBA buffer with solid color. */
static void ui_ic_fill_solid(u8* px, u32 w, u32 h, u8 r, u8 g, u8 b, u8 a) {
	u32 i;
	u32 n = w * h;
	for (i = 0u; i < n; ++i) {
		u32 o = i * 4u;
		px[o + 0u] = r;
		px[o + 1u] = g;
		px[o + 2u] = b;
		px[o + 3u] = a;
	}
}

SK_TEST(ui_image_compare_identical) {
	const sk_ui_api_t* ui = ui_ic_test_api();
	u8 a[4u * 4u * 4u];
	u8 b[4u * 4u * 4u];
	sk_ui_cpu_image_t ia;
	sk_ui_cpu_image_t ib;
	sk_ui_image_compare_stats_t stats;
	i32 rc;

	ui_ic_fill_solid(a, 4u, 4u, 10u, 20u, 30u, 255u);
	memcpy(b, a, sizeof(a));
	memset(&ia, 0, sizeof(ia));
	ia.width = 4u;
	ia.height = 4u;
	ia.channels = 4u;
	ia.pixels = a;
	ib = ia;
	ib.pixels = b;

	rc = ui->cpu_image_compare(&ia, &ib, 0u, 0.0f, &stats, NULL);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_COMPARE_OK, rc);
	TEST_ASSERT_EQUAL_UINT(0u, stats.differ_count);
	TEST_ASSERT_EQUAL_UINT(0u, stats.max_channel_delta);
	TEST_ASSERT_EQUAL_UINT(16u, stats.pixel_count);
	TEST_ASSERT_EQUAL_INT(0, stats.size_mismatch);
}

SK_TEST(ui_image_compare_single_pixel_within_tolerance) {
	const sk_ui_api_t* ui = ui_ic_test_api();
	u8 a[4u * 4u * 4u];
	u8 b[4u * 4u * 4u];
	sk_ui_cpu_image_t ia;
	sk_ui_cpu_image_t ib;
	sk_ui_image_compare_stats_t stats;
	i32 rc;

	ui_ic_fill_solid(a, 4u, 4u, 100u, 100u, 100u, 255u);
	memcpy(b, a, sizeof(a));
	/* One pixel off by 2 in R — within channel_tolerance=2. */
	b[0] = 102u;

	memset(&ia, 0, sizeof(ia));
	ia.width = 4u;
	ia.height = 4u;
	ia.channels = 4u;
	ia.pixels = a;
	ib = ia;
	ib.pixels = b;

	rc = ui->cpu_image_compare(&ia, &ib, /*channel_tolerance=*/2u, 0.0f, &stats, NULL);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_COMPARE_OK, rc);
	TEST_ASSERT_EQUAL_UINT(0u, stats.differ_count);
	TEST_ASSERT_EQUAL_UINT(2u, stats.max_channel_delta);
}

SK_TEST(ui_image_compare_single_pixel_beyond_tolerance) {
	const sk_ui_api_t* ui = ui_ic_test_api();
	u8 a[4u * 4u * 4u];
	u8 b[4u * 4u * 4u];
	u8 diff[4u * 4u * 4u];
	sk_ui_cpu_image_t ia;
	sk_ui_cpu_image_t ib;
	sk_ui_image_compare_stats_t stats;
	i32 rc;

	ui_ic_fill_solid(a, 4u, 4u, 50u, 60u, 70u, 255u);
	memcpy(b, a, sizeof(a));
	/* Pixel (2,1): R delta = 10 > tolerance 3. */
	{
		u32 o = (1u * 4u + 2u) * 4u;
		b[o + 0u] = 60u;
	}

	memset(&ia, 0, sizeof(ia));
	ia.width = 4u;
	ia.height = 4u;
	ia.channels = 4u;
	ia.pixels = a;
	ib = ia;
	ib.pixels = b;

	rc = ui->cpu_image_compare(&ia, &ib, /*channel_tolerance=*/3u, 0.0f, &stats, diff);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_COMPARE_MISMATCH, rc);
	TEST_ASSERT_EQUAL_UINT(1u, stats.differ_count);
	TEST_ASSERT_EQUAL_UINT(10u, stats.max_channel_delta);
	TEST_ASSERT_EQUAL_UINT(2u, stats.bbox_min_x);
	TEST_ASSERT_EQUAL_UINT(1u, stats.bbox_min_y);
	TEST_ASSERT_EQUAL_UINT(2u, stats.bbox_max_x);
	TEST_ASSERT_EQUAL_UINT(1u, stats.bbox_max_y);
	/* Diff highlights the failing pixel red. */
	{
		u32 o = (1u * 4u + 2u) * 4u;
		TEST_ASSERT_EQUAL_UINT(255u, (u32)diff[o + 0u]);
		TEST_ASSERT_EQUAL_UINT(0u, (u32)diff[o + 1u]);
		TEST_ASSERT_EQUAL_UINT(0u, (u32)diff[o + 2u]);
		TEST_ASSERT_EQUAL_UINT(255u, (u32)diff[o + 3u]);
	}

	/* Same pixel off, but max_diff_fraction allows 1/16 of pixels. */
	rc = ui->cpu_image_compare(&ia, &ib, 3u, 1.0f / 16.0f + 0.001f, &stats, NULL);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_COMPARE_OK, rc);
	TEST_ASSERT_EQUAL_UINT(1u, stats.differ_count);
}

SK_TEST(ui_image_compare_size_mismatch) {
	const sk_ui_api_t* ui = ui_ic_test_api();
	u8 a[4u * 4u * 4u];
	u8 b[2u * 2u * 4u];
	sk_ui_cpu_image_t ia;
	sk_ui_cpu_image_t ib;
	sk_ui_image_compare_stats_t stats;
	i32 rc;

	ui_ic_fill_solid(a, 4u, 4u, 1u, 2u, 3u, 255u);
	ui_ic_fill_solid(b, 2u, 2u, 1u, 2u, 3u, 255u);
	memset(&ia, 0, sizeof(ia));
	ia.width = 4u;
	ia.height = 4u;
	ia.channels = 4u;
	ia.pixels = a;
	memset(&ib, 0, sizeof(ib));
	ib.width = 2u;
	ib.height = 2u;
	ib.channels = 4u;
	ib.pixels = b;

	rc = ui->cpu_image_compare(&ia, &ib, 0u, 0.0f, &stats, NULL);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_COMPARE_SIZE_MISMATCH, rc);
	TEST_ASSERT_EQUAL_INT(1, stats.size_mismatch);
	TEST_ASSERT_EQUAL_UINT(4u, stats.actual_width);
	TEST_ASSERT_EQUAL_UINT(4u, stats.actual_height);
	TEST_ASSERT_EQUAL_UINT(2u, stats.expected_width);
	TEST_ASSERT_EQUAL_UINT(2u, stats.expected_height);
	/* No pixel scan on size mismatch. */
	TEST_ASSERT_EQUAL_UINT(0u, stats.differ_count);
	TEST_ASSERT_EQUAL_UINT(0u, stats.pixel_count);
}

SK_TEST(ui_image_compare_golden_roundtrip_and_artifacts) {
	const sk_ui_api_t* ui = ui_ic_test_api();
	sk_filesystem_api_t fs;
	sk_ui_cpu_image_t img;
	sk_ui_cpu_image_t bad;
	sk_ui_image_compare_params_t params;
	sk_ui_image_compare_stats_t stats;
	u8 pixels[8u * 8u * 4u];
	u8 bad_pixels[8u * 8u * 4u];
	char golden_path[SK_FS_PATH_MAX];
	char root[SK_FS_PATH_MAX];
	i32 rc;

	ui_ic_fill_mock_fs(&fs);
	ui_ic_fill_solid(pixels, 8u, 8u, 40u, 80u, 120u, 255u);
	memcpy(bad_pixels, pixels, sizeof(pixels));
	bad_pixels[0] = 255u; /* large R delta */

	memset(&img, 0, sizeof(img));
	img.width = 8u;
	img.height = 8u;
	img.channels = 4u;
	img.pixels = pixels;
	bad = img;
	bad.pixels = bad_pixels;

	TEST_ASSERT_EQUAL_INT(0, ui->test_artifact_root(&fs, root, (u32)sizeof(root)));
	TEST_ASSERT_EQUAL_INT(0, ui->test_artifact_png_path(&fs, "apx229_golden_src", golden_path, (u32)sizeof(golden_path)));

	/* Bless: write golden (flag, not env — env may be set in some hosts). */
	memset(&params, 0, sizeof(params));
	params.update_golden = 1;
	params.name = "apx229_bless";
	rc = ui->cpu_image_compare_golden(&img, golden_path, &params, &fs, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_COMPARE_OK, rc);
	TEST_ASSERT_EQUAL_INT(1, stats.updated);

	/* Identical match against the blessed golden. */
	memset(&params, 0, sizeof(params));
	params.name = "apx229_match";
	params.channel_tolerance = 0u;
	params.max_diff_fraction = 0.0f;
	rc = ui->cpu_image_compare_golden(&img, golden_path, &params, &fs, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_COMPARE_OK, rc);
	TEST_ASSERT_EQUAL_INT(0, stats.updated);
	TEST_ASSERT_EQUAL_UINT(0u, stats.differ_count);

	/* Beyond-tolerance mismatch writes three artifacts. */
	memset(&params, 0, sizeof(params));
	params.name = "apx229_mismatch";
	params.channel_tolerance = 0u;
	params.max_diff_fraction = 0.0f;
	rc = ui->cpu_image_compare_golden(&bad, golden_path, &params, &fs, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_COMPARE_MISMATCH, rc);
	TEST_ASSERT_TRUE(stats.differ_count >= 1u);
	TEST_ASSERT_TRUE(stats.actual_path[0] != '\0');
	TEST_ASSERT_TRUE(stats.expected_path[0] != '\0');
	TEST_ASSERT_TRUE(stats.diff_path[0] != '\0');
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, fs.get_file_status(stats.actual_path));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, fs.get_file_status(stats.expected_path));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, fs.get_file_status(stats.diff_path));

	/* Size mismatch against a different-size capture. */
	{
		u8 small[2u * 2u * 4u];
		sk_ui_cpu_image_t small_img;
		ui_ic_fill_solid(small, 2u, 2u, 1u, 1u, 1u, 255u);
		memset(&small_img, 0, sizeof(small_img));
		small_img.width = 2u;
		small_img.height = 2u;
		small_img.channels = 4u;
		small_img.pixels = small;
		memset(&params, 0, sizeof(params));
		params.name = "apx229_size";
		rc = ui->cpu_image_compare_golden(&small_img, golden_path, &params, &fs, &stats);
		TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_COMPARE_SIZE_MISMATCH, rc);
		TEST_ASSERT_EQUAL_INT(1, stats.size_mismatch);
	}

	(void)root;
}

#endif /* SK_TESTS */
