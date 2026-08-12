/*
 * ui_text_screenshot.c — deterministic text rendering screenshot harness
 * (APX-268).
 *
 * Renders a fixed suite of text samples on the headless offscreen renderer
 * (same pipeline as sk_ui_capture_harness_capture) and writes PNG captures
 * into {artifact-root}/text-screenshot/{mode}/ so the legacy FreeType path
 * and the MSDF path can be compared side by side. See ui_text_screenshot.h
 * for the determinism contract, sample coverage, and verification rules.
 *
 * Implementation notes:
 *   - Every sample capture goes through sk_ui_capture_harness_capture with
 *     the renderer pinned per call (text_renderer param) so captures never
 *     inherit the process-wide switch, and the harness restores FreeType
 *     before returning.
 *   - The atlas dump must happen inside the scene callback: the FreeType R8
 *     atlas only fills during rasterization, and the harness owns/destroys
 *     the font system after capture. The glyph_grid callback warms the full
 *     printable-ASCII set at a fixed 16px before dumping page 0.
 *   - verify mode re-captures every sample and byte-compares raw readback
 *     AND PNG artifact bytes; stb_image_write PNG encoding is deterministic
 *     for identical pixels, so byte equality of the files proves the whole
 *     artifact pipeline is stable.
 */

#include "ui_text_screenshot.h"

#include "filesystem.h"
#include "path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Fixed suite content                                                        */
/* -------------------------------------------------------------------------- */

/* Alphanumeric + punctuation pangram (printable ASCII only; the MSDF bake
 * covers exactly this range plus space, and the FreeType path rasters any
 * codepoint, so both modes must render every character here). */
#define TS_PANGRAM                                            \
	"The quick brown fox jumps over the lazy dog 0123456789 " \
	"!@#$%^&*()-_=+[]{};:'\",.<>/?\\|`~"

/* Several font sizes, very small through very large (logical px). */
static const f32 ts_font_sizes[] = {8.0f, 12.0f, 20.0f, 48.0f, 96.0f};
#define TS_FONT_SIZES_COUNT ((u32)(sizeof(ts_font_sizes) / sizeof(ts_font_sizes[0])))

#define TS_GRID_CHARS_PER_ROW 16u
#define TS_GRID_FONT_SIZE 16.0f

/* Fixed clear color for every sample (dark, opaque). */
static const sk_ui_color_t ts_clear = {SK_UI_TEXT_SCREENSHOT_CLEAR_R, SK_UI_TEXT_SCREENSHOT_CLEAR_G, SK_UI_TEXT_SCREENSHOT_CLEAR_B, SK_UI_TEXT_SCREENSHOT_CLEAR_A};

/* -------------------------------------------------------------------------- */
/* Sample context (forwarded to scene callbacks)                              */
/* -------------------------------------------------------------------------- */

typedef struct ts_ctx_t {
	sk_ui_text_screenshot_mode_t mode;
	const_chr_t mode_dir; /* "text-screenshot/{mode}" under the artifact root */
	f32 content_scale;	  /* pinned per sample (1.0, or 2.0 for the scaled sample) */
} ts_ctx_t;

/* -------------------------------------------------------------------------- */
/* Artifact path helpers (mirror of ui->test_artifact_* for the suite's own
 * file management; captures themselves use the harness output_subdir)        */
/* -------------------------------------------------------------------------- */

static i32 ts_artifact_root(char* out, u32 out_cap) {
	const char* env;

	if (out == NULL || out_cap == 0u) {
		return -1;
	}
	out[0] = '\0';
	env = getenv("SK_TEST_ARTIFACT_DIR");
	if (env != NULL && env[0] != '\0') {
		const size_t n = strlen(env);
		if (n + 1u > out_cap) {
			return -1;
		}
		memcpy(out, env, n + 1u);
		return 0;
	}
#if defined(SK_TEST_ARTIFACT_DIR)
	{
		const char* def = SK_TEST_ARTIFACT_DIR;
		const size_t n = strlen(def);
		if (n + 1u > out_cap) {
			return -1;
		}
		memcpy(out, def, n + 1u);
		return 0;
	}
#else
	{
		const sk_filesystem_api_t* fs = sk_filesystem_api();
		char temp[SK_FS_PATH_MAX];
		if (fs == NULL || fs->temp_folder(temp, (u32)sizeof(temp)) != 0) {
			return -1;
		}
		return sk_path_join(sk_str_view_cstr(temp), sk_str_view_cstr("skore-test-artifacts"), out, out_cap) < 0 ? -1 : 0;
	}
#endif
}

/* Same sanitize contract as the ui plugin: single file component, unsafe
 * characters become '_'. */
static void ts_sanitize(const_chr_t name, char* out, u32 out_cap) {
	u32 di = 0u;
	u32 i;

	if (out_cap == 0u) {
		return;
	}
	if (name == NULL || name[0] == '\0') {
		out[0] = '\0';
		return;
	}
	for (i = 0u; name[i] != '\0' && di + 1u < out_cap; ++i) {
		const char c = name[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
			out[di++] = c;
		} else if (di == 0u || out[di - 1u] != '_') {
			out[di++] = '_';
		}
	}
	while (di > 0u && out[di - 1u] == '_') {
		--di;
	}
	if (di == 0u) {
		out[di++] = 'c';
		out[di++] = 'a';
		out[di++] = 'p';
	}
	out[di] = '\0';
}

/* {root}/{subdir}/{sanitized_name}.png */
static i32 ts_path_png(const_chr_t subdir, const_chr_t name, char* out, u32 out_cap) {
	char root[SK_FS_PATH_MAX];
	char joined[SK_FS_PATH_MAX];
	char file[288];
	char base[256];
	i32 n;

	if (ts_artifact_root(root, (u32)sizeof(root)) != 0) {
		return -1;
	}
	ts_sanitize(name, base, (u32)sizeof(base));
	n = snprintf(file, sizeof(file), "%s.png", base);
	if (n < 0 || (u32)n >= (u32)sizeof(file)) {
		return -1;
	}
	if (subdir != NULL && subdir[0] != '\0') {
		if (sk_path_join(sk_str_view_cstr(root), sk_str_view_cstr(subdir), joined, (u32)sizeof(joined)) < 0) {
			return -1;
		}
		return sk_path_join(sk_str_view_cstr(joined), sk_str_view_cstr(file), out, out_cap) < 0 ? -1 : 0;
	}
	return sk_path_join(sk_str_view_cstr(root), sk_str_view_cstr(file), out, out_cap) < 0 ? -1 : 0;
}

/* {root}/{subdir}/{filename} (no sanitize / extension). */
static i32 ts_path_raw(const_chr_t subdir, const_chr_t filename, char* out, u32 out_cap) {
	char root[SK_FS_PATH_MAX];
	char joined[SK_FS_PATH_MAX];

	if (ts_artifact_root(root, (u32)sizeof(root)) != 0) {
		return -1;
	}
	if (subdir != NULL && subdir[0] != '\0') {
		if (sk_path_join(sk_str_view_cstr(root), sk_str_view_cstr(subdir), joined, (u32)sizeof(joined)) < 0) {
			return -1;
		}
		return sk_path_join(sk_str_view_cstr(joined), sk_str_view_cstr(filename), out, out_cap) < 0 ? -1 : 0;
	}
	return sk_path_join(sk_str_view_cstr(root), sk_str_view_cstr(filename), out, out_cap) < 0 ? -1 : 0;
}

static const sk_filesystem_api_t* ts_fs(void) {
	return sk_filesystem_api();
}

/* -------------------------------------------------------------------------- */
/* Scene helpers                                                              */
/* -------------------------------------------------------------------------- */

static sk_ui_node_t ts_root_node(sk_ui_capture_scene_t* scene) {
	return scene->ui->context_root(scene->ctx);
}

/* Fill the root with @p bg at the logical viewport size (physical / scale). */
static i32 ts_root(sk_ui_capture_scene_t* scene, const ts_ctx_t* ctx, sk_ui_color_t bg) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_node_t root = ts_root_node(scene);
	sk_ui_style_props_t props;

	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.background_color = bg;
	props.layout.width = sk_ui_pt((f32)scene->width / ctx->content_scale);
	props.layout.height = sk_ui_pt((f32)scene->height / ctx->content_scale);
	ui->node_set_inline_style(scene->ctx, root, &props);
	return 0;
}

/* One absolutely-placed text label at a fixed logical position/size. */
static i32 ts_label(sk_ui_capture_scene_t* scene, sk_ui_node_t parent, const_chr_t text, f32 x, f32 y, f32 w, f32 h, f32 font_size, sk_ui_color_t color) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_node_t label = ui->widget_label(scene->ctx, parent, text, NULL);
	sk_ui_style_props_t props;

	if (!sk_ui_node_is_valid(label)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.color = color;
	props.font_size = font_size;
	props.layout.width = sk_ui_pt(w);
	props.layout.height = sk_ui_pt(h);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(x);
	props.layout.top = sk_ui_pt(y);
	ui->node_set_inline_style(scene->ctx, label, &props);
	return 0;
}

/* One absolutely-placed solid box (alpha-blend background for text). */
static i32 ts_box(sk_ui_capture_scene_t* scene, sk_ui_node_t parent, f32 x, f32 y, f32 w, f32 h, sk_ui_color_t color) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_node_t box = ui->node_create(scene->ctx, SK_UI_NODE_KIND_BOX, parent);
	sk_ui_style_props_t props;

	if (!sk_ui_node_is_valid(box)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.background_color = color;
	props.layout.width = sk_ui_pt(w);
	props.layout.height = sk_ui_pt(h);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(x);
	props.layout.top = sk_ui_pt(y);
	ui->node_set_inline_style(scene->ctx, box, &props);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Sample scenes                                                              */
/* -------------------------------------------------------------------------- */

/* 1. Alphanumeric + punctuation pangram, wrapped label. */
static i32 ts_scene_pangram(sk_ui_capture_scene_t* scene, void* user) {
	const ts_ctx_t* ctx = (const ts_ctx_t*)user;
	sk_ui_node_t parent;

	if (scene->font_system == NULL || scene->font == NULL || ctx == NULL) {
		return -1;
	}
	if (ts_root(scene, ctx, ts_clear) != 0) {
		return -1;
	}
	parent = ts_root_node(scene);
	return ts_label(scene, parent, TS_PANGRAM, 16.0f, 16.0f, 608.0f / ctx->content_scale, 300.0f / ctx->content_scale, 20.0f, sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f));
}

/* 2. The same phrase at several sizes, very small through very large. */
static i32 ts_scene_sizes(sk_ui_capture_scene_t* scene, void* user) {
	const ts_ctx_t* ctx = (const ts_ctx_t*)user;
	sk_ui_node_t parent;
	u32 i;
	f32 y = 8.0f;

	if (scene->font_system == NULL || scene->font == NULL || ctx == NULL) {
		return -1;
	}
	if (ts_root(scene, ctx, ts_clear) != 0) {
		return -1;
	}
	parent = ts_root_node(scene);
	for (i = 0u; i < TS_FONT_SIZES_COUNT; ++i) {
		const f32 size = ts_font_sizes[i];
		const f32 h = size + 20.0f;
		char text[96];
		(void)snprintf(text, sizeof(text), "The quick brown fox %gpx", (double)size);
		if (ts_label(scene, parent, text, 12.0f, y, 616.0f / ctx->content_scale, h, size, sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f)) != 0) {
			return -1;
		}
		y += h + 4.0f;
	}
	return 0;
}

/* 3. Colored and alpha-blended text over colored boxes. */
static i32 ts_scene_colors_alpha(sk_ui_capture_scene_t* scene, void* user) {
	const ts_ctx_t* ctx = (const ts_ctx_t*)user;
	sk_ui_node_t parent;

	if (scene->font_system == NULL || scene->font == NULL || ctx == NULL) {
		return -1;
	}
	if (ts_root(scene, ctx, ts_clear) != 0) {
		return -1;
	}
	parent = ts_root_node(scene);
	if (ts_box(scene, parent, 16.0f, 16.0f, 296.0f, 220.0f, sk_ui_rgba(0.55f, 0.10f, 0.10f, 1.0f)) != 0) {
		return -1;
	}
	if (ts_box(scene, parent, 328.0f, 16.0f, 296.0f, 220.0f, sk_ui_rgba(0.10f, 0.15f, 0.55f, 1.0f)) != 0) {
		return -1;
	}
	if (ts_label(scene, parent, "Opaque white", 28.0f, 24.0f, 260.0f, 30.0f, 20.0f, sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f)) != 0) {
		return -1;
	}
	if (ts_label(scene, parent, "Opaque yellow", 340.0f, 24.0f, 260.0f, 30.0f, 20.0f, sk_ui_rgba(1.0f, 1.0f, 0.0f, 1.0f)) != 0) {
		return -1;
	}
	if (ts_label(scene, parent, "White alpha 0.50", 28.0f, 64.0f, 260.0f, 30.0f, 20.0f, sk_ui_rgba(1.0f, 1.0f, 1.0f, 0.50f)) != 0) {
		return -1;
	}
	if (ts_label(scene, parent, "Cyan alpha 0.25", 28.0f, 104.0f, 260.0f, 30.0f, 20.0f, sk_ui_rgba(0.0f, 1.0f, 1.0f, 0.25f)) != 0) {
		return -1;
	}
	if (ts_label(scene, parent, "Magenta alpha 0.50", 340.0f, 64.0f, 260.0f, 30.0f, 20.0f, sk_ui_rgba(1.0f, 0.0f, 1.0f, 0.50f)) != 0) {
		return -1;
	}
	if (ts_label(scene, parent, "Blue alpha 0.35", 340.0f, 104.0f, 260.0f, 30.0f, 20.0f, sk_ui_rgba(0.3f, 0.5f, 1.0f, 0.35f)) != 0) {
		return -1;
	}
	if (ts_label(scene, parent, "Green alpha 0.60 on root", 28.0f, 160.0f, 560.0f, 40.0f, 24.0f, sk_ui_rgba(0.0f, 1.0f, 0.3f, 0.60f)) != 0) {
		return -1;
	}
	return 0;
}

/* 4. Scaled text: fixed content scale 2.0 (logical viewport = physical/2).
 *    Rotation is not supported by the UI (no transform API) — scaling only. */
static i32 ts_scene_scaled(sk_ui_capture_scene_t* scene, void* user) {
	const ts_ctx_t* ctx = (const ts_ctx_t*)user;
	sk_ui_node_t parent;

	if (scene->font_system == NULL || scene->font == NULL || ctx == NULL) {
		return -1;
	}
	if (ts_root(scene, ctx, ts_clear) != 0) {
		return -1;
	}
	parent = ts_root_node(scene);
	if (ts_label(scene, parent, "Scaled text at 2.0x content scale", 8.0f, 8.0f, 300.0f, 40.0f, 14.0f, sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f)) != 0) {
		return -1;
	}
	if (ts_label(scene, parent, "28px physical glyphs from 14px logical", 8.0f, 56.0f, 300.0f, 40.0f, 14.0f, sk_ui_rgba(0.7f, 0.9f, 1.0f, 0.80f)) != 0) {
		return -1;
	}
	if (ts_label(scene, parent, "Very small: 6px logical", 8.0f, 104.0f, 300.0f, 20.0f, 6.0f, sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f)) != 0) {
		return -1;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Atlas dump                                                                 */
/* -------------------------------------------------------------------------- */

/* Write @p px (tightly packed, @p channels per pixel) as PNG to @p path. */
static i32 ts_write_png(const sk_ui_api_t* ui, const_chr_t path, u32 w, u32 h, u32 channels, const u8* px) {
	sk_ui_cpu_image_t img;

	if (px == NULL || w == 0u || h == 0u) {
		return -1;
	}
	memset(&img, 0, sizeof(img));
	img.width = w;
	img.height = h;
	img.channels = channels;
	img.pixels = (u8*)SK_CONST_CAST(void*, px);
	if (ui->cpu_image_write_png(&img, sk_filesystem_api(), path) != 0) {
		fprintf(stderr, "ui_text_screenshot: atlas PNG write failed: %s\n", path);
		return -1;
	}
	return 0;
}

/* FreeType: warm printable ASCII at a fixed 16px, then dump page 0 (the
 * fixed warm set packs into the first 256x256 page). */
static i32 ts_dump_freetype_atlas(const sk_ui_api_t* ui, const ts_ctx_t* ctx, sk_ui_font_system_t* sys, sk_ui_font_t* font, u32 pixel_size) {
	sk_ui_atlas_page_t page;
	char path[SK_FS_PATH_MAX];
	u32 cp;

	if (ui->font_atlas_page_count(sys) == 0u) {
		return -1;
	}
	for (cp = 0x20u; cp <= 0x7Eu; ++cp) {
		const u32 gi = ui->font_glyph_index(font, cp);
		sk_ui_glyph_t g;
		if (gi == 0u || ui->font_get_glyph(sys, font, pixel_size, gi, &g) != 0) {
			return -1;
		}
	}
	if (ui->font_atlas_page_count(sys) != 1u) {
		fprintf(stderr, "ui_text_screenshot: FreeType atlas grew to %u pages at %upx; dump pins page 0\n", ui->font_atlas_page_count(sys), pixel_size);
	}
	if (ui->font_atlas_get_page(sys, 0u, &page) != 0 || page.pixels == NULL) {
		return -1;
	}
	if (ts_path_png(ctx->mode_dir, "atlas_freetype", path, (u32)sizeof(path)) != 0) {
		return -1;
	}
	return ts_write_png(ui, path, page.width, page.height, 1u, page.pixels);
}

/* MSDF: dump the baked RGB8 atlas (bake is done by the harness). */
static i32 ts_dump_msdf_atlas(const sk_ui_api_t* ui, const ts_ctx_t* ctx, sk_ui_font_t* font) {
	sk_ui_msdf_atlas_t atlas;
	char path[SK_FS_PATH_MAX];

	if (ui->font_msdf_get_atlas(font, &atlas) != 0 || atlas.pixels == NULL) {
		return -1;
	}
	if (ts_path_png(ctx->mode_dir, "atlas_msdf", path, (u32)sizeof(path)) != 0) {
		return -1;
	}
	return ts_write_png(ui, path, atlas.width, atlas.height, atlas.channels, atlas.pixels);
}

/* 5. Glyph grid: printable ASCII laid out in fixed rows, plus the raw atlas
 *    dump written next to the capture. */
static i32 ts_scene_glyph_grid(sk_ui_capture_scene_t* scene, void* user) {
	const ts_ctx_t* ctx = (const ts_ctx_t*)user;
	sk_ui_node_t parent;
	char row[TS_GRID_CHARS_PER_ROW + 1u];
	u32 row_i;
	f32 y = 8.0f;

	if (scene->font_system == NULL || scene->font == NULL || ctx == NULL) {
		return -1;
	}
	if (ctx->mode == SK_UI_TEXT_SCREENSHOT_MODE_MSDF) {
		if (ts_dump_msdf_atlas(scene->ui, ctx, scene->font) != 0) {
			return -1;
		}
	} else if (ts_dump_freetype_atlas(scene->ui, ctx, scene->font_system, scene->font, (u32)TS_GRID_FONT_SIZE) != 0) {
		return -1;
	}
	if (ts_root(scene, ctx, ts_clear) != 0) {
		return -1;
	}
	parent = ts_root_node(scene);
	for (row_i = 0u; row_i * TS_GRID_CHARS_PER_ROW < 95u; ++row_i) {
		u32 col;
		u32 n = 0u;
		for (col = 0u; col < TS_GRID_CHARS_PER_ROW; ++col) {
			const u32 cp = 0x20u + row_i * TS_GRID_CHARS_PER_ROW + col;
			if (cp > 0x7Eu) {
				break;
			}
			row[n++] = (char)cp;
		}
		row[n] = '\0';
		if (ts_label(scene, parent, row, 12.0f, y, 616.0f / ctx->content_scale, TS_GRID_FONT_SIZE + 10.0f, TS_GRID_FONT_SIZE, sk_ui_rgba(0.85f, 0.9f, 1.0f, 1.0f)) != 0) {
			return -1;
		}
		y += TS_GRID_FONT_SIZE + 10.0f;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Suite table                                                                */
/* -------------------------------------------------------------------------- */

typedef struct ts_sample_t {
	const_chr_t name;
	f32 content_scale;
	i32 (*scene)(sk_ui_capture_scene_t* scene, void* user);
} ts_sample_t;

static const ts_sample_t ts_samples[] = {
	{"pangram", 1.0f, ts_scene_pangram}, {"sizes", 1.0f, ts_scene_sizes},			{"colors_alpha", 1.0f, ts_scene_colors_alpha},
	{"scaled", 2.0f, ts_scene_scaled},	 {"glyph_grid", 1.0f, ts_scene_glyph_grid},
};
#define TS_SAMPLES_COUNT ((u32)(sizeof(ts_samples) / sizeof(ts_samples[0])))

/* -------------------------------------------------------------------------- */
/* Mode naming / expected set                                                 */
/* -------------------------------------------------------------------------- */

const_chr_t sk_ui_text_screenshot_mode_name(sk_ui_text_screenshot_mode_t mode) {
	switch (mode) {
	case SK_UI_TEXT_SCREENSHOT_MODE_FREETYPE:
		return "freetype";
	case SK_UI_TEXT_SCREENSHOT_MODE_MSDF:
		return "msdf";
	case SK_UI_TEXT_SCREENSHOT_MODE_COUNT:
		break;
	}
	return "unknown";
}

static const_chr_t ts_atlas_base(sk_ui_text_screenshot_mode_t mode) {
	return mode == SK_UI_TEXT_SCREENSHOT_MODE_MSDF ? "atlas_msdf" : "atlas_freetype";
}

u32 sk_ui_text_screenshot_expected_count(void) {
	/* 5 samples + 1 atlas dump + 1 manifest. */
	return TS_SAMPLES_COUNT + 2u;
}

const_chr_t sk_ui_text_screenshot_expected_name(sk_ui_text_screenshot_mode_t mode, u32 i) {
	if (i < TS_SAMPLES_COUNT) {
		return ts_samples[i].name;
	}
	if (i == TS_SAMPLES_COUNT) {
		/* ts_path_png appends ".png" — return the base name. */
		return ts_atlas_base(mode);
	}
	if (i == TS_SAMPLES_COUNT + 1u) {
		return "manifest.txt";
	}
	return NULL;
}

/* Resolve one expected capture to its absolute path under @p subdir. */
static i32 ts_expected_path_in(const_chr_t subdir, sk_ui_text_screenshot_mode_t mode, u32 i, char* out, u32 out_cap) {
	char mode_dir[SK_FS_PATH_MAX];
	const_chr_t name;

	if (sk_path_join(sk_str_view_cstr(subdir), sk_str_view_cstr(sk_ui_text_screenshot_mode_name(mode)), mode_dir, (u32)sizeof(mode_dir)) < 0) {
		return -1;
	}
	name = sk_ui_text_screenshot_expected_name(mode, i);
	if (name == NULL) {
		return -1;
	}
	if (strcmp(name, "manifest.txt") == 0) {
		return ts_path_raw(mode_dir, name, out, out_cap);
	}
	return ts_path_png(mode_dir, name, out, out_cap);
}

/* Same with the default suite subdirectory (tests only). */
#ifdef SK_TESTS
static i32 ts_expected_path(sk_ui_text_screenshot_mode_t mode, u32 i, char* out, u32 out_cap) {
	return ts_expected_path_in(SK_UI_TEXT_SCREENSHOT_SUBDIR, mode, i, out, out_cap);
}
#endif

/* -------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* -------------------------------------------------------------------------- */

static i32 ts_read_file(const_chr_t path, u8** out, u32* out_size) {
	const sk_filesystem_api_t* fs = ts_fs();
	sk_file_handle_t f;
	u64 size64;
	u8* buf;

	*out = NULL;
	*out_size = 0u;
	f = fs->open_file(path, SK_FILE_ACCESS_READ);
	if (f == NULL) {
		return -1;
	}
	size64 = fs->get_file_size(f);
	if (size64 > 0u && size64 < ((u64)1u << 31u)) {
		/* Bounded above; one cast to a project fixed width (portable across
		 * LP64 / LLP64 where size_t differs). */
		const u32 n = (u32)size64;
		buf = (u8*)malloc(n);
		if (buf != NULL && fs->read_file(f, buf, n) == n) {
			*out = buf;
			*out_size = n;
		} else {
			free(buf);
			buf = NULL;
		}
	}
	fs->close_file(f);
	return (*out != NULL) ? 0 : -1;
}

static void ts_log(const sk_ui_text_screenshot_params_t* p, const_chr_t line) {
	if (p != NULL && p->log_fn != NULL) {
		p->log_fn(p->log_user, line);
	} else {
		fprintf(stderr, "%s\n", line);
	}
}

/* -------------------------------------------------------------------------- */
/* Suite runner                                                               */
/* -------------------------------------------------------------------------- */

i32 sk_ui_text_screenshot_run(const sk_ui_text_screenshot_params_t* params) {
	char mode_dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	char line[SK_FS_PATH_MAX + 256u];
	const sk_filesystem_api_t* fs;
	const_chr_t subdir;
	sk_ui_cpu_image_t img_a;
	sk_ui_cpu_image_t img_b;
	u32 i;

	memset(&img_a, 0, sizeof(img_a));
	memset(&img_b, 0, sizeof(img_b));

	if (params == NULL || params->mode >= SK_UI_TEXT_SCREENSHOT_MODE_COUNT) {
		return SK_UI_TEXT_SCREENSHOT_RC_ERROR;
	}
	subdir = (params->subdir != NULL && params->subdir[0] != '\0') ? params->subdir : SK_UI_TEXT_SCREENSHOT_SUBDIR;
	if (sk_path_join(sk_str_view_cstr(subdir), sk_str_view_cstr(sk_ui_text_screenshot_mode_name(params->mode)), mode_dir, (u32)sizeof(mode_dir)) < 0) {
		return SK_UI_TEXT_SCREENSHOT_RC_ERROR;
	}
	fs = ts_fs();
	if (fs == NULL) {
		return SK_UI_TEXT_SCREENSHOT_RC_ERROR;
	}

	/* One capture per sample; each runs its own full bootstrap so repeated
	 * runs share no mutable state (the harness determinism contract). */
	for (i = 0u; i < TS_SAMPLES_COUNT; ++i) {
		sk_ui_capture_harness_params_t cp;
		ts_ctx_t ctx;
		i32 rc;

		memset(&cp, 0, sizeof(cp));
		cp.scene_name = ts_samples[i].name;
		cp.width = SK_UI_TEXT_SCREENSHOT_VIEW_W;
		cp.height = SK_UI_TEXT_SCREENSHOT_VIEW_H;
		cp.clear_color_set = 1;
		cp.clear_color = ts_clear;
		cp.time_seconds = 0.0;
		cp.load_test_font = 1;
		cp.content_scale = ts_samples[i].content_scale;
		cp.text_renderer = params->mode == SK_UI_TEXT_SCREENSHOT_MODE_MSDF ? SK_UI_TEXT_RENDERER_MSDF : SK_UI_TEXT_RENDERER_FREETYPE;
		cp.output_subdir = mode_dir;

		memset(&ctx, 0, sizeof(ctx));
		ctx.mode = params->mode;
		ctx.mode_dir = mode_dir;
		ctx.content_scale = ts_samples[i].content_scale;

		rc = sk_ui_capture_harness_capture(&cp, ts_samples[i].scene, &ctx, &img_a);
		if (rc == SK_UI_CAPTURE_HARNESS_RC_SKIPPED) {
			return SK_UI_TEXT_SCREENSHOT_RC_SKIPPED;
		}
		if (rc != SK_UI_CAPTURE_HARNESS_RC_OK || img_a.pixels == NULL) {
			(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: sample '%s' capture failed (rc=%d)", sk_ui_text_screenshot_mode_name(params->mode), ts_samples[i].name, rc);
			ts_log(params, line);
			goto fail;
		}

		/* Optional determinism check: re-capture and byte-compare the raw
		 * readback AND the PNG artifact bytes of the two runs. The artifact
		 * is read right after each capture (capture 2 overwrites the file). */
		if (params->verify != 0) {
			u8* file_a = NULL;
			u8* file_b = NULL;
			u8* atlas_a = NULL;
			u8* atlas_b = NULL;
			u32 size_a = 0u;
			u32 size_b = 0u;
			u32 atlas_size_a = 0u;
			u32 atlas_size_b = 0u;
			i32 same_pixels;
			i32 same_png;
			i32 same_atlas = 1;
			const i32 is_grid = (strcmp(ts_samples[i].name, "glyph_grid") == 0) ? 1 : 0;
			char atlas_path[SK_FS_PATH_MAX];

			if (ts_path_png(mode_dir, ts_samples[i].name, path, (u32)sizeof(path)) != 0) {
				goto fail;
			}
			/* Run 1 artifact bytes. */
			if (ts_read_file(path, &file_a, &size_a) != 0) {
				(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: sample '%s' verify could not read artifact '%s'", sk_ui_text_screenshot_mode_name(params->mode),
							   ts_samples[i].name, path);
				ts_log(params, line);
				free(file_a);
				goto fail;
			}
			if (is_grid != 0) {
				if (ts_path_png(mode_dir, ts_atlas_base(params->mode), atlas_path, (u32)sizeof(atlas_path)) != 0 || ts_read_file(atlas_path, &atlas_a, &atlas_size_a) != 0) {
					(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: glyph_grid verify could not read atlas dump", sk_ui_text_screenshot_mode_name(params->mode));
					ts_log(params, line);
					free(file_a);
					free(atlas_a);
					goto fail;
				}
			}
			rc = sk_ui_capture_harness_capture(&cp, ts_samples[i].scene, &ctx, &img_b);
			if (rc != SK_UI_CAPTURE_HARNESS_RC_OK || img_b.pixels == NULL) {
				free(file_a);
				free(atlas_a);
				(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: sample '%s' verify re-capture failed (rc=%d)", sk_ui_text_screenshot_mode_name(params->mode),
							   ts_samples[i].name, rc);
				ts_log(params, line);
				goto fail;
			}
			/* Run 2 artifact bytes (file was overwritten by capture 2). */
			if (ts_read_file(path, &file_b, &size_b) != 0) {
				(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: sample '%s' verify could not re-read artifact '%s'", sk_ui_text_screenshot_mode_name(params->mode),
							   ts_samples[i].name, path);
				ts_log(params, line);
				free(file_a);
				free(file_b);
				free(atlas_a);
				goto fail;
			}
			if (is_grid != 0) {
				if (ts_read_file(atlas_path, &atlas_b, &atlas_size_b) != 0) {
					(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: glyph_grid verify could not re-read atlas dump", sk_ui_text_screenshot_mode_name(params->mode));
					ts_log(params, line);
					free(file_a);
					free(file_b);
					free(atlas_a);
					free(atlas_b);
					goto fail;
				}
				same_atlas = (atlas_size_a == atlas_size_b && memcmp(atlas_a, atlas_b, atlas_size_a) == 0) ? 1 : 0;
			}
			same_pixels = (img_a.width == img_b.width && img_a.height == img_b.height && img_a.channels == img_b.channels &&
						   memcmp(img_a.pixels, img_b.pixels, (size_t)img_a.width * img_a.height * img_a.channels) == 0);
			same_png = (size_a == size_b && memcmp(file_a, file_b, size_a) == 0);
			free(file_a);
			free(file_b);
			free(atlas_a);
			free(atlas_b);
			if (!same_pixels || !same_png || !same_atlas) {
				(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: sample '%s' NOT deterministic (pixels %s, png %s, atlas %s)",
							   sk_ui_text_screenshot_mode_name(params->mode), ts_samples[i].name, same_pixels ? "same" : "DIFF", same_png ? "same" : "DIFF",
							   same_atlas ? "same" : "DIFF");
				ts_log(params, line);
				goto fail;
			}
			(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: sample '%s' deterministic (%u bytes png)", sk_ui_text_screenshot_mode_name(params->mode),
						   ts_samples[i].name, size_a);
			ts_log(params, line);
		} else {
			(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: captured '%s'", sk_ui_text_screenshot_mode_name(params->mode), ts_samples[i].name);
			ts_log(params, line);
		}
		sk_ui_capture_harness_image_free(&img_a);
		sk_ui_capture_harness_image_free(&img_b);
	}

	/* Manifest: deterministic listing of the captures (name + byte size). */
	if (ts_path_raw(mode_dir, "manifest.txt", path, (u32)sizeof(path)) != 0) {
		goto fail;
	}
	{
		sk_file_handle_t mf = fs->open_file(path, SK_FILE_ACCESS_WRITE);
		if (mf == NULL) {
			goto fail;
		}
		for (i = 0u; i < sk_ui_text_screenshot_expected_count(); ++i) {
			const_chr_t name = sk_ui_text_screenshot_expected_name(params->mode, i);
			char entry[SK_FS_PATH_MAX];
			u64 size = 0u;
			i32 n;

			if (name == NULL || ts_expected_path_in(subdir, params->mode, i, entry, (u32)sizeof(entry)) != 0) {
				continue;
			}
			size = fs->get_path_size(entry);
			n = snprintf(entry, sizeof(entry), "%s %llu\n", name, size);
			if (n > 0) {
				(void)fs->write_file(mf, entry, (size_t)n);
			}
		}
		fs->close_file(mf);
	}

	/* Completeness: every expected capture exists and is non-empty. */
	{
		u32 missing = 0u;
		for (i = 0u; i < sk_ui_text_screenshot_expected_count(); ++i) {
			if (ts_expected_path_in(subdir, params->mode, i, path, (u32)sizeof(path)) != 0) {
				++missing;
				continue;
			}
			if (fs->get_file_status(path) != SK_FILE_STATUS_FILE || fs->get_path_size(path) == 0u) {
				(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: missing/empty expected capture '%s'", sk_ui_text_screenshot_mode_name(params->mode),
							   sk_ui_text_screenshot_expected_name(params->mode, i));
				ts_log(params, line);
				++missing;
			}
		}
		if (missing != 0u) {
			(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: completeness check failed (%u missing)", sk_ui_text_screenshot_mode_name(params->mode), missing);
			ts_log(params, line);
			goto fail;
		}
	}

	(void)snprintf(line, sizeof(line), "ui_text_screenshot[%s]: complete set written under {artifact-root}/%s/", sk_ui_text_screenshot_mode_name(params->mode), mode_dir);
	ts_log(params, line);
	return SK_UI_TEXT_SCREENSHOT_RC_OK;

fail:
	sk_ui_capture_harness_image_free(&img_a);
	sk_ui_capture_harness_image_free(&img_b);
	return SK_UI_TEXT_SCREENSHOT_RC_ERROR;
}

/* -------------------------------------------------------------------------- */
/* SK_TESTS (compiled into sk-integration-tests only)                         */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS

#include "test.h"

/* One sample captured twice through the harness must be byte-identical in
 * both modes — the determinism core of the screenshot suite. */
SK_TEST(ui_text_screenshot_determinism) {
	static const struct {
		const_chr_t name;
		sk_ui_text_renderer_t renderer;
	} modes[] = {
		{"freetype", SK_UI_TEXT_RENDERER_FREETYPE},
		{"msdf", SK_UI_TEXT_RENDERER_MSDF},
	};
	u32 m;

	for (m = 0u; m < (u32)(sizeof(modes) / sizeof(modes[0])); ++m) {
		sk_ui_capture_harness_params_t p1;
		sk_ui_capture_harness_params_t p2;
		sk_ui_cpu_image_t a;
		sk_ui_cpu_image_t b;
		ts_ctx_t ctx;
		i32 rc;

		memset(&p1, 0, sizeof(p1));
		memset(&p2, 0, sizeof(p2));
		memset(&a, 0, sizeof(a));
		memset(&b, 0, sizeof(b));
		memset(&ctx, 0, sizeof(ctx));
		ctx.mode = m == 0u ? SK_UI_TEXT_SCREENSHOT_MODE_FREETYPE : SK_UI_TEXT_SCREENSHOT_MODE_MSDF;
		ctx.mode_dir = SK_UI_TEXT_SCREENSHOT_SUBDIR;
		ctx.content_scale = 1.0f;

		p1.scene_name = "ui_text_screenshot_determinism_a";
		p1.width = 320u;
		p1.height = 96u;
		p1.clear_color_set = 1;
		p1.clear_color = ts_clear;
		p1.time_seconds = 0.0;
		p1.load_test_font = 1;
		p1.text_renderer = modes[m].renderer;
		p2 = p1;
		p2.scene_name = "ui_text_screenshot_determinism_b";

		rc = sk_ui_capture_harness_capture(&p1, ts_scene_pangram, &ctx, &a);
		if (rc == SK_UI_CAPTURE_HARNESS_RC_SKIPPED) {
			TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping text screenshot determinism");
		}
		TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
		TEST_ASSERT_NOT_NULL(a.pixels);
		rc = sk_ui_capture_harness_capture(&p2, ts_scene_pangram, &ctx, &b);
		TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
		TEST_ASSERT_NOT_NULL(b.pixels);
		TEST_ASSERT_EQUAL_UINT(a.width, b.width);
		TEST_ASSERT_EQUAL_UINT(a.height, b.height);
		TEST_ASSERT_EQUAL_UINT(a.channels, b.channels);
		TEST_ASSERT_EQUAL_INT(0, memcmp(a.pixels, b.pixels, (size_t)a.width * a.height * a.channels));
		sk_ui_capture_harness_image_free(&a);
		sk_ui_capture_harness_image_free(&b);
	}
}

/* Both path modes must produce complete, parallel capture sets. */
SK_TEST(ui_text_screenshot_suite_completeness) {
	const sk_filesystem_api_t* fs = ts_fs();
	sk_ui_text_screenshot_params_t p;
	sk_ui_text_screenshot_mode_t mode;
	u32 i;

	TEST_ASSERT_NOT_NULL(fs);
	for (mode = SK_UI_TEXT_SCREENSHOT_MODE_FREETYPE; mode < SK_UI_TEXT_SCREENSHOT_MODE_COUNT; mode = (sk_ui_text_screenshot_mode_t)((u32)mode + 1u)) {
		char path[SK_FS_PATH_MAX];
		i32 rc;

		memset(&p, 0, sizeof(p));
		p.mode = mode;
		rc = sk_ui_text_screenshot_run(&p);
		if (rc == SK_UI_TEXT_SCREENSHOT_RC_SKIPPED) {
			TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping text screenshot suite");
		}
		TEST_ASSERT_EQUAL_INT(SK_UI_TEXT_SCREENSHOT_RC_OK, rc);
		if (rc != SK_UI_TEXT_SCREENSHOT_RC_OK) {
			return;
		}
		for (i = 0u; i < sk_ui_text_screenshot_expected_count(); ++i) {
			if (ts_expected_path(mode, i, path, (u32)sizeof(path)) != 0) {
				TEST_FAIL_MESSAGE("cannot resolve expected screenshot path");
			}
			TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, fs->get_file_status(path));
			TEST_ASSERT_TRUE(fs->get_path_size(path) > 0u);
		}
	}

	/* The sample captures must exist under identical names in both modes
	 * (parallel sets); only the atlas dump differs by the mode prefix. */
	for (i = 0u; i < TS_SAMPLES_COUNT; ++i) {
		char d1[SK_FS_PATH_MAX];
		char d2[SK_FS_PATH_MAX];
		TEST_ASSERT_EQUAL_INT(0, ts_expected_path(SK_UI_TEXT_SCREENSHOT_MODE_FREETYPE, i, d1, (u32)sizeof(d1)));
		TEST_ASSERT_EQUAL_INT(0, ts_expected_path(SK_UI_TEXT_SCREENSHOT_MODE_MSDF, i, d2, (u32)sizeof(d2)));
		TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, fs->get_file_status(d1));
		TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, fs->get_file_status(d2));
	}
}

#endif /* SK_TESTS */
