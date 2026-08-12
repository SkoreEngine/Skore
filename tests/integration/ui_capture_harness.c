/*
 * ui_capture_harness.c — reusable UI capture test harness (APX-228 / APX-250).
 *
 * Wraps the low-level headless capture (plugins/ui/capture.c, APX-226) and
 * the PNG artifact writers (plugins/ui/image_write.c, APX-227) behind a
 * one-call test API: bootstrap the headless stack, render exactly one
 * frame, read back pixels, write the PNG, and return the image for
 * assertions — see ui_capture_harness.h for the determinism contract.
 *
 * Each call performs a complete setup/teardown cycle (fresh app context →
 * plugins → device → adapter → capture → frame → artifact → teardown).
 * All the pieces are idempotent across repeated cycles in one process
 * (sk_app_init/destroy, plugin load/unload, ui/dxc init/shutdown, device
 * create/destroy, capture create/destroy), so calling the harness N times
 * in one test run is safe and never shares mutable state between captures.
 *
 * The PNG is written before the caller receives the image, so a later
 * assertion failure still leaves an inspectable artifact on disk.
 *
 * Fonts (APX-250): when params.load_test_font is set, the harness loads
 * plugins/ui/testdata/DejaVuSans.ttf (exact file from main) at a fixed
 * atlas size. Missing or wrong-sized assets fail hard — there is no
 * fallback to a system font or the embedded skore_test_font subset.
 * Content scale is always SK_UI_CAPTURE_HARNESS_CONTENT_SCALE (1.0).
 */

#include "ui_capture_harness.h"

#include "allocator.h"
#include "app.h"
#include "dxc_compiler.h"
#include "filesystem.h"
#include "path.h"
#include "render_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Plugin bootstrap (same recipe as tests/integration/ui_capture.c)           */
/* -------------------------------------------------------------------------- */

static i32 uich_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char base[SK_FS_PATH_MAX];
	char plugins[SK_FS_PATH_MAX];
	i32 n;

	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}
	n = sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins, (u32)sizeof(plugins));
	if (n < 0) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(plugins), sk_str_view_cstr(plugin_filename), out, out_cap);
	return (n < 0) ? -1 : 0;
}

static const sk_render_device_api_t* uich_load_vulkan_api(sk_app_context_t* ctx) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-vulkan-render-device.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-vulkan-render-device.dylib";
#else
	const_chr_t plugin_name = "sk-vulkan-render-device.so";
#endif
	if (uich_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		sk_app_api()->load_plugin(ctx, path);
	}
	return (const sk_render_device_api_t*)sk_app_api()->get_api(ctx, SK_RENDER_DEVICE_API_TYPE_ID);
}

static const sk_dxc_compiler_api_t* uich_load_dxc_api(sk_app_context_t* ctx) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-dxc-compiler.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-dxc-compiler.dylib";
#else
	const_chr_t plugin_name = "sk-dxc-compiler.so";
#endif
	if (uich_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		sk_app_api()->load_plugin(ctx, path);
	}
	return (const sk_dxc_compiler_api_t*)sk_app_api()->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
}

static const sk_ui_api_t* uich_load_ui_api(sk_app_context_t* ctx) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-ui.dylib";
#else
	const_chr_t plugin_name = "sk-ui.so";
#endif
	if (uich_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		sk_app_api()->load_plugin(ctx, path);
	}
	return (const sk_ui_api_t*)sk_app_api()->get_api(ctx, SK_UI_API_TYPE_ID);
}

static sk_adapter_t uich_select_adapter(const sk_render_device_api_t* api, sk_render_device_t dev, u32* out_count) {
	u32 adapter_count = api->get_adapter_count(dev);
	sk_adapter_t best = sk_adapter_t_zero();
	u32 best_score = 0u;
	u32 i;

	if (adapter_count == 0u) {
		*out_count = 0u;
		return sk_adapter_t_zero();
	}
	for (i = 0u; i < adapter_count; ++i) {
		sk_adapter_t candidate = api->get_adapter(dev, i);
		u32 score = api->get_adapter_score(dev, candidate);
		if (score > best_score) {
			best_score = score;
			best = candidate;
		}
	}
	*out_count = adapter_count;
	return best;
}

/* -------------------------------------------------------------------------- */
/* Deterministic frame pipeline                                               */
/* -------------------------------------------------------------------------- */

/* Fixed pipeline settings (documented contract, see header): content scale
 * from params (0 → SK_UI_CAPTURE_HARNESS_CONTENT_SCALE, the 1x / 96 DPI
 * reference), optional pinned font via load_test_font, full-surface viewport.
 * Never queries host DPI/scale. */
static i32 uich_refresh(const sk_ui_api_t* ui, sk_ui_context_t* ctx, u32 width, u32 height, f32 content_scale, sk_ui_text_renderer_t renderer, sk_ui_font_system_t* fonts,
						sk_ui_font_t* font) {
	sk_ui_paint_params_t paint_params;
	const f32 scale = content_scale > 0.0f ? content_scale : SK_UI_CAPTURE_HARNESS_CONTENT_SCALE;
	/* layout() takes logical units; the framebuffer is physical (logical * scale). */
	const f32 logical_w = (f32)width / scale;
	const f32 logical_h = (f32)height / scale;

	if (ui->style_resolve(ctx) != 0) {
		return -1;
	}
	if (ui->layout(ctx, logical_w, logical_h) != 0) {
		return -1;
	}
	if (ui->layout_apply_scale(ctx, scale, scale) != 0) {
		return -1;
	}
	memset(&paint_params, 0, sizeof(paint_params));
	paint_params.font_system = fonts;
	paint_params.font = font;
	paint_params.text_renderer = renderer;
	if (ui->paint(ctx, &paint_params) != 0) {
		return -1;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

i32 sk_ui_capture_harness_test_font_path(char* out, u32 out_cap) {
	i32 n;

	if (out == NULL || out_cap == 0u) {
		return -1;
	}
	out[0] = '\0';

#if defined(SK_UI_GOLDEN_DIR)
	/* Compile-time absolute (or source-relative) path to plugins/ui/testdata. */
	n = snprintf(out, out_cap, "%s/%s", SK_UI_GOLDEN_DIR, SK_UI_CAPTURE_HARNESS_FONT_FILENAME);
	if (n < 0 || (u32)n >= out_cap) {
		out[0] = '\0';
		return -1;
	}
	return 0;
#else
	/* Fallback: {app_folder|cwd}/plugins/ui/testdata/DejaVuSans.ttf */
	{
		const sk_filesystem_api_t* fs = sk_filesystem_api();
		char base[SK_FS_PATH_MAX];
		sk_str_view_t parts[5];

		if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
			if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
				return -1;
			}
		}
		parts[0] = sk_str_view_cstr(base);
		parts[1] = sk_str_view_cstr("plugins");
		parts[2] = sk_str_view_cstr("ui");
		parts[3] = sk_str_view_cstr("testdata");
		parts[4] = sk_str_view_cstr(SK_UI_CAPTURE_HARNESS_FONT_FILENAME);
		n = sk_path_join_n(parts, 5u, out, out_cap);
		if (n < 0) {
			out[0] = '\0';
			return -1;
		}
	}
	return 0;
#endif
}

i32 sk_ui_capture_harness_load_test_font(const sk_ui_api_t* ui, sk_ui_font_system_t** out_system, sk_ui_font_t** out_font) {
	const sk_filesystem_api_t* fs;
	char path[SK_FS_PATH_MAX];
	sk_file_handle_t file;
	u64 size_u64;
	sk_ui_font_system_t* sys = NULL;
	sk_ui_font_t* font = NULL;

	if (out_system != NULL) {
		*out_system = NULL;
	}
	if (out_font != NULL) {
		*out_font = NULL;
	}
	if (ui == NULL || out_system == NULL || out_font == NULL) {
		return -1;
	}

	if (sk_ui_capture_harness_test_font_path(path, (u32)sizeof(path)) != 0 || path[0] == '\0') {
		fprintf(stderr, "ui_capture_harness: failed to resolve path for %s\n", SK_UI_CAPTURE_HARNESS_FONT_FILENAME);
		return -1;
	}

	fs = sk_filesystem_api();
	if (fs->get_file_status(path) != SK_FILE_STATUS_FILE) {
		fprintf(stderr,
				"ui_capture_harness: missing vendored test font '%s' "
				"(expected under UI test assets; copy from main Content/Fonts/DejaVuSans.ttf — no system/built-in fallback)\n",
				path);
		return -1;
	}

	file = fs->open_file(path, SK_FILE_ACCESS_READ);
	if (file == NULL) {
		fprintf(stderr, "ui_capture_harness: cannot open test font '%s'\n", path);
		return -1;
	}
	size_u64 = fs->get_file_size(file);
	fs->close_file(file);
	if (size_u64 != (u64)SK_UI_CAPTURE_HARNESS_FONT_FILE_SIZE) {
		/* size fits u32 for the pinned asset; cast for portable printf. */
		const u32 actual = size_u64 > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)size_u64;
		fprintf(stderr,
				"ui_capture_harness: test font '%s' size %u != pinned %u "
				"(refusing built-in/subset fallback; restore main Content/Fonts/DejaVuSans.ttf)\n",
				path, actual, SK_UI_CAPTURE_HARNESS_FONT_FILE_SIZE);
		return -1;
	}

	sys = ui->font_system_create(NULL, SK_UI_CAPTURE_HARNESS_FONT_ATLAS_W, SK_UI_CAPTURE_HARNESS_FONT_ATLAS_H);
	if (sys == NULL) {
		fprintf(stderr, "ui_capture_harness: font_system_create failed\n");
		return -1;
	}
	font = ui->font_load_path(sys, fs, path);
	if (font == NULL) {
		fprintf(stderr, "ui_capture_harness: font_load_path failed for '%s' (no built-in fallback)\n", path);
		ui->font_system_destroy(sys);
		return -1;
	}

	/* Sanity: face must provide ASCII glyphs (guards empty/corrupt TTF). */
	if (ui->font_glyph_index(font, (u32)'A') == 0u || ui->font_glyph_index(font, (u32)'U') == 0u) {
		fprintf(stderr, "ui_capture_harness: test font '%s' missing expected glyphs\n", path);
		ui->font_destroy(font);
		ui->font_system_destroy(sys);
		return -1;
	}

	/* Pin: physical pixel size at harness content scale must match constant. */
	if (sk_ui_font_pixel_size(SK_UI_CAPTURE_HARNESS_FONT_LOGICAL_SIZE, SK_UI_CAPTURE_HARNESS_CONTENT_SCALE) != SK_UI_CAPTURE_HARNESS_FONT_PIXEL_SIZE) {
		fprintf(stderr, "ui_capture_harness: font pixel-size pin mismatch\n");
		ui->font_destroy(font);
		ui->font_system_destroy(sys);
		return -1;
	}

	*out_system = sys;
	*out_font = font;
	return 0;
}

i32 sk_ui_capture_harness_capture(const sk_ui_capture_harness_params_t* params, sk_ui_capture_scene_fn scene, void* user, sk_ui_cpu_image_t* out_image) {
	sk_app_context_t* app = NULL;
	const sk_render_device_api_t* api = NULL;
	const sk_dxc_compiler_api_t* dxc = NULL;
	const sk_ui_api_t* ui = NULL;
	sk_render_device_t dev = sk_render_device_t_zero();
	sk_ui_context_t* ui_ctx = NULL;
	sk_ui_capture_t* capture = NULL;
	sk_ui_font_system_t* fonts = NULL;
	sk_ui_font_t* font = NULL;
	sk_ui_cpu_image_t img; /* capture-owned readback; valid until capture_destroy */
	sk_ui_draw_list_t empty_dl;
	sk_ui_capture_scene_t scene_info;
	sk_ui_capture_desc_t cdesc;
	sk_ui_capture_frame_info_t finfo;
	const sk_ui_draw_list_t* dl;
	const sk_allocator_t* alloc;
	const sk_filesystem_api_t* fs;
	char png_path[SK_FS_PATH_MAX];
	size_t bytes;
	i32 rc = SK_UI_CAPTURE_HARNESS_RC_ERROR;

	sk_ui_text_renderer_t renderer;
	f32 content_scale;

	if (out_image != NULL) {
		memset(out_image, 0, sizeof(*out_image));
	}
	if (params == NULL || params->scene_name == NULL || params->scene_name[0] == '\0' || params->width == 0u || params->height == 0u || out_image == NULL) {
		return SK_UI_CAPTURE_HARNESS_RC_ERROR;
	}
	/* Explicit renderer per capture: DEFAULT resolves to the harness's
	 * documented FreeType default so goldens stay comparable and captures
	 * never inherit the process-wide switch from a previous call. */
	renderer = params->text_renderer != SK_UI_TEXT_RENDERER_DEFAULT ? params->text_renderer : SK_UI_TEXT_RENDERER_FREETYPE;
	content_scale = params->content_scale > 0.0f ? params->content_scale : SK_UI_CAPTURE_HARNESS_CONTENT_SCALE;

	app = sk_app_init(0, NULL);
	if (app == NULL) {
		return SK_UI_CAPTURE_HARNESS_RC_ERROR;
	}

	/* Plugins + module inits. */
	api = uich_load_vulkan_api(app);
	dxc = uich_load_dxc_api(app);
	ui = uich_load_ui_api(app);
	if (api == NULL || dxc == NULL || ui == NULL) {
		goto out;
	}
	if (ui->init() != 0 || dxc->init() != 0) {
		goto out;
	}

	/* Device + adapter (headless; no window/surface). */
	dev = api->init(app, NULL);
	if (!sk_render_device_t_is_valid(dev)) {
		rc = SK_UI_CAPTURE_HARNESS_RC_SKIPPED; /* no Vulkan loader/ICD */
		goto out;
	}
	{
		u32 adapter_count = 0u;
		sk_adapter_t best = uich_select_adapter(api, dev, &adapter_count);
		if (adapter_count == 0u || !sk_adapter_t_is_valid(best)) {
			rc = SK_UI_CAPTURE_HARNESS_RC_SKIPPED;
			goto out;
		}
		if (api->select_adapter(dev, best) != 0) {
			goto out;
		}
	}

	/* Offscreen capture: fixed viewport + fixed clear color. */
	memset(&cdesc, 0, sizeof(cdesc));
	cdesc.device_api = api;
	cdesc.device = dev;
	cdesc.dxc = dxc;
	cdesc.width = params->width;
	cdesc.height = params->height;
	if (params->clear_color_set != 0) {
		cdesc.clear_color.color.r = params->clear_color.r;
		cdesc.clear_color.color.g = params->clear_color.g;
		cdesc.clear_color.color.b = params->clear_color.b;
		cdesc.clear_color.color.a = params->clear_color.a;
	} else {
		cdesc.clear_color.color.r = 1.0f;
		cdesc.clear_color.color.g = 1.0f;
		cdesc.clear_color.color.b = 1.0f;
		cdesc.clear_color.color.a = 1.0f;
	}
	cdesc.debug_name = params->scene_name;
	capture = ui->capture_create(&cdesc);
	if (capture == NULL) {
		goto out;
	}

	/* Fresh UI context + scene callback (fixed logical time, no wall clock). */
	ui_ctx = ui->context_create(NULL);
	if (ui_ctx == NULL) {
		goto out;
	}
	memset(&scene_info, 0, sizeof(scene_info));
	scene_info.ui = ui;
	scene_info.ctx = ui_ctx;
	scene_info.time_seconds = params->time_seconds;
	scene_info.width = params->width;
	scene_info.height = params->height;

	/* Optional pinned DejaVuSans.ttf — loaded before the scene so text nodes
	 * can paint immediately. Missing asset fails the capture (no fallback). */
	if (params->load_test_font != 0) {
		if (sk_ui_capture_harness_load_test_font(ui, &fonts, &font) != 0) {
			goto out;
		}
		scene_info.font_system = fonts;
		scene_info.font = font;
	}
	/* Text renderer for this capture; MSDF bakes the pinned font so simple
	 * scenes paint without extra setup (a scene may replace font_system/
	 * font; then it is responsible for baking its own face). */
	ui->set_text_renderer(renderer);
	if (renderer == SK_UI_TEXT_RENDERER_MSDF && font != NULL && ui->font_msdf_bake(font) != 0) {
		goto out;
	}

	if (scene != NULL && scene(&scene_info, user) != 0) {
		goto out;
	}
	/* Scene may replace the font pair; take ownership of whatever is set. */
	fonts = scene_info.font_system;
	font = scene_info.font;
	if (uich_refresh(ui, ui_ctx, params->width, params->height, content_scale, renderer, fonts, font) != 0) {
		goto out;
	}

	/* Exactly one frame: paint's draw list → offscreen render → readback. */
	dl = ui->get_draw_list(ui_ctx);
	memset(&finfo, 0, sizeof(finfo));
	if (dl != NULL) {
		finfo.draw_list = dl;
	} else {
		memset(&empty_dl, 0, sizeof(empty_dl));
		finfo.draw_list = &empty_dl;
	}
	finfo.font_system = fonts;
	finfo.font = font;
	if (ui->capture_frame(capture, &finfo, &img) != 0) {
		goto out;
	}

	/* Move the readback into caller-owned memory (capture is destroyed below). */
	bytes = (size_t)img.width * img.height * img.channels;
	alloc = sk_allocator_default();
	out_image->pixels = (u8*)alloc->alloc(alloc->instance, bytes);
	if (out_image->pixels == NULL) {
		goto out;
	}
	memcpy(out_image->pixels, img.pixels, bytes);
	out_image->width = img.width;
	out_image->height = img.height;
	out_image->channels = img.channels;
	rc = SK_UI_CAPTURE_HARNESS_RC_OK;

	/* PNG artifact — written before returning so a later assertion failure
	 * still leaves an inspectable frame (cpu_image_write_png logs failures). */
	fs = sk_filesystem_api();
	if (params->output_subdir != NULL && params->output_subdir[0] != '\0') {
		if (ui->test_artifact_png_path_in(fs, params->output_subdir, params->scene_name, png_path, (u32)sizeof(png_path)) != 0 ||
			ui->cpu_image_write_png(out_image, fs, png_path) != 0) {
			rc = SK_UI_CAPTURE_HARNESS_RC_ERROR;
		}
	} else if (ui->test_artifact_png_path(fs, params->scene_name, png_path, (u32)sizeof(png_path)) != 0 || ui->cpu_image_write_png(out_image, fs, png_path) != 0) {
		rc = SK_UI_CAPTURE_HARNESS_RC_ERROR;
	}

out:
	if (font != NULL && ui != NULL) {
		ui->font_destroy(font);
	}
	if (fonts != NULL && ui != NULL) {
		ui->font_system_destroy(fonts);
	}
	if (capture != NULL) {
		ui->capture_destroy(capture);
	}
	if (ui_ctx != NULL) {
		ui->context_destroy(ui_ctx);
	}
	if (sk_render_device_t_is_valid(dev)) {
		api->destroy(dev);
	}
	if (dxc != NULL) {
		dxc->shutdown();
	}
	if (ui != NULL) {
		/* Leave the process-wide renderer at the harness default so later
		 * captures/tests never inherit an MSDF switch from this call. */
		ui->set_text_renderer(SK_UI_TEXT_RENDERER_FREETYPE);
		ui->shutdown();
	}
	sk_app_destroy(app);
	return rc;
}

void sk_ui_capture_harness_image_free(sk_ui_cpu_image_t* image) {
	if (image == NULL) {
		return;
	}
	if (image->pixels != NULL) {
		const sk_allocator_t* alloc = sk_allocator_default();
		alloc->free(alloc->instance, image->pixels);
	}
	memset(image, 0, sizeof(*image));
}

/* -------------------------------------------------------------------------- */
/* Harness tests                                                              */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS

#include "test.h"

#define UI_CH_W 64u
#define UI_CH_H 64u

/* Exact straight-alpha RGBA8 (R in the low byte), all channels 0 or 255. */
#define UI_CH_PX_RED 0xFF0000FFu
#define UI_CH_PX_GREEN 0xFF00FF00u
#define UI_CH_PX_BLUE 0xFFFF0000u
#define UI_CH_PX_BLACK 0xFF000000u
#define UI_CH_PX_WHITE 0xFFFFFFFFu

static u32 uich_pixel(const sk_ui_cpu_image_t* img, u32 x, u32 y) {
	const u8* p;
	if (img->pixels == NULL || x >= img->width || y >= img->height) {
		return 0u;
	}
	p = img->pixels + ((size_t)y * img->width + x) * img->channels;
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void uich_assert_pixel(const sk_ui_cpu_image_t* img, u32 x, u32 y, u32 expected) {
	char msg[128];
	const u32 actual = uich_pixel(img, x, y);
	snprintf(msg, sizeof(msg), "pixel (%u,%u) = 0x%08X, expected 0x%08X", x, y, actual, expected);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(expected, actual, msg);
}

/* Opaque box with absolute placement (fixed coordinates only). */
static void uich_box(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent, f32 x, f32 y, f32 w, f32 h, sk_ui_color_t color) {
	sk_ui_node_t node = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	sk_ui_style_props_t props;

	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.background_color = color;
	props.layout.width = sk_ui_pt(w);
	props.layout.height = sk_ui_pt(h);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(x);
	props.layout.top = sk_ui_pt(y);
	ui->node_set_inline_style(ctx, node, &props);
}

/*
 * Static, time-pinned scene used by the determinism test:
 *   - root: opaque red fill over the whole viewport;
 *   - green 16x16 box at (8,8);
 *   - blue bar at (0,40), height 8, width = 8 + floor(time * 8) — animated
 *     progress pinned to the harness's fixed logical time. With
 *     time_seconds = 0.5 the bar spans x in [0,12); a wall-clock-driven
 *     scene would produce a different width on every call and fail the
 *     byte-identical check.
 */
static i32 uich_scene_static_pinned(sk_ui_capture_scene_t* scene, void* user) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_context_t* ctx = scene->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_style_props_t props;
	u32 bar_w;

	(void)user;
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.background_color = sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f);
	props.layout.width = sk_ui_pt((f32)scene->width);
	props.layout.height = sk_ui_pt((f32)scene->height);
	ui->node_set_inline_style(ctx, root, &props);

	uich_box(ui, ctx, root, 8.0f, 8.0f, 16.0f, 16.0f, sk_ui_rgba(0.0f, 1.0f, 0.0f, 1.0f));
	bar_w = 8u + (u32)(scene->time_seconds * 8.0);
	if (bar_w > scene->width) {
		bar_w = scene->width;
	}
	uich_box(ui, ctx, root, 0.0f, 40.0f, (f32)bar_w, 8.0f, sk_ui_rgba(0.0f, 0.0f, 1.0f, 1.0f));
	return 0;
}

/*
 * APX-228 determinism check: capture the same scene twice in one run and
 * require the two buffers to be byte-identical. Both calls run a complete
 * setup/teardown cycle, which also proves repeated harness use is safe in
 * one process.
 */
SK_TEST(ui_capture_harness_deterministic) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t a;
	sk_ui_cpu_image_t b;
	i32 rc;

	memset(&params, 0, sizeof(params));
	params.scene_name = "ui_capture_harness_deterministic";
	params.width = UI_CH_W;
	params.height = UI_CH_H;
	params.clear_color_set = 1;
	params.clear_color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
	params.time_seconds = 0.5;

	rc = sk_ui_capture_harness_capture(&params, uich_scene_static_pinned, NULL, &a);
	if (rc == SK_UI_CAPTURE_HARNESS_RC_SKIPPED) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping UI capture harness test");
	}
	TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
	TEST_ASSERT_EQUAL_UINT(UI_CH_W, a.width);
	TEST_ASSERT_EQUAL_UINT(UI_CH_H, a.height);
	TEST_ASSERT_EQUAL_UINT(4u, a.channels);

	/* Second capture: full fresh setup/teardown cycle. */
	rc = sk_ui_capture_harness_capture(&params, uich_scene_static_pinned, NULL, &b);
	TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
	TEST_ASSERT_EQUAL_UINT(UI_CH_W, b.width);
	TEST_ASSERT_EQUAL_UINT(UI_CH_H, b.height);
	TEST_ASSERT_EQUAL_UINT(4u, b.channels);

	if (a.pixels == NULL || b.pixels == NULL) {
		sk_ui_capture_harness_image_free(&a);
		sk_ui_capture_harness_image_free(&b);
		return;
	}

	/* Byte-identical determinism across two independent full captures. */
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(a.pixels, b.pixels, (size_t)a.width * a.height * a.channels), "two captures of the same scene must be byte-identical");

	/* Content sanity at fixed coordinates (also proves the scene rendered). */
	uich_assert_pixel(&a, 0u, 0u, UI_CH_PX_RED);
	uich_assert_pixel(&a, UI_CH_W - 1u, UI_CH_H - 1u, UI_CH_PX_RED);
	uich_assert_pixel(&a, 8u, 8u, UI_CH_PX_GREEN);
	uich_assert_pixel(&a, 23u, 23u, UI_CH_PX_GREEN);
	uich_assert_pixel(&a, 7u, 7u, UI_CH_PX_RED);   /* left/top of green box */
	uich_assert_pixel(&a, 24u, 8u, UI_CH_PX_RED);  /* right of green box */
	uich_assert_pixel(&a, 6u, 44u, UI_CH_PX_BLUE); /* inside time-pinned bar */
	uich_assert_pixel(&a, 11u, 44u, UI_CH_PX_BLUE);
	uich_assert_pixel(&a, 13u, 44u, UI_CH_PX_RED); /* past bar end (width 12) */
	uich_assert_pixel(&a, 6u, 39u, UI_CH_PX_RED);  /* above bar */
	uich_assert_pixel(&a, 6u, 48u, UI_CH_PX_RED);  /* below bar */

	/* rc == OK implies the PNG artifact was written; verify on disk using the
	 * same path resolution as the harness (plugin test_artifact_png_path), not
	 * a reimplementation that can diverge when the plugin was built with a
	 * different SK_TEST_ARTIFACT_DIR (e.g. parallel build-debug vs build). */
	{
		const sk_filesystem_api_t* fs = sk_filesystem_api();
		sk_app_context_t* app = sk_app_init(0, NULL);
		const sk_ui_api_t* ui = NULL;
		char path[SK_FS_PATH_MAX];

		TEST_ASSERT_NOT_NULL(app);
		ui = uich_load_ui_api(app);
		TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin required to resolve artifact path");
		TEST_ASSERT_NOT_NULL(ui->test_artifact_png_path);
		TEST_ASSERT_EQUAL_INT(0, ui->test_artifact_png_path(fs, params.scene_name, path, (u32)sizeof(path)));
		TEST_ASSERT_EQUAL_INT_MESSAGE(SK_FILE_STATUS_FILE, fs->get_file_status(path), "capture PNG artifact must exist on disk");
		sk_app_destroy(app);
	}

	sk_ui_capture_harness_image_free(&a);
	sk_ui_capture_harness_image_free(&b);
}

/* Fixed clear color determinism: a clear-only frame (NULL scene) must be
 * exactly the documented default, and exactly an explicit clear color. */
SK_TEST(ui_capture_harness_clear_color) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t img;
	i32 rc;
	u32 x;
	u32 y;

	memset(&params, 0, sizeof(params));
	params.scene_name = "ui_capture_harness_clear_color";
	params.width = UI_CH_W;
	params.height = UI_CH_H;
	/* clear_color_set = 0 → fixed default (opaque white). */
	rc = sk_ui_capture_harness_capture(&params, NULL, NULL, &img);
	if (rc == SK_UI_CAPTURE_HARNESS_RC_SKIPPED) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping UI capture harness test");
	}
	TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
	TEST_ASSERT_EQUAL_UINT(UI_CH_W, img.width);
	TEST_ASSERT_EQUAL_UINT(UI_CH_H, img.height);
	TEST_ASSERT_EQUAL_UINT(4u, img.channels);
	if (img.pixels == NULL) {
		sk_ui_capture_harness_image_free(&img);
		return;
	}
	for (y = 0u; y < img.height; ++y) {
		for (x = 0u; x < img.width; ++x) {
			uich_assert_pixel(&img, x, y, UI_CH_PX_WHITE);
		}
	}
	sk_ui_capture_harness_image_free(&img);

	/* Explicit fixed clear color (opaque black). */
	memset(&params, 0, sizeof(params));
	params.scene_name = "ui_capture_harness_clear_color";
	params.width = UI_CH_W;
	params.height = UI_CH_H;
	params.clear_color_set = 1;
	params.clear_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 1.0f);
	rc = sk_ui_capture_harness_capture(&params, NULL, NULL, &img);
	TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
	if (img.pixels == NULL) {
		sk_ui_capture_harness_image_free(&img);
		return;
	}
	for (y = 0u; y < img.height; ++y) {
		for (x = 0u; x < img.width; ++x) {
			uich_assert_pixel(&img, x, y, UI_CH_PX_BLACK);
		}
	}
	sk_ui_capture_harness_image_free(&img);
}

/*
 * APX-250 guard: the vendored DejaVuSans.ttf must exist at the pinned size,
 * load through the harness helper, and never be replaced by a silent
 * built-in/system fallback. Fails loudly with stderr diagnostics from
 * sk_ui_capture_harness_load_test_font when the asset is missing/wrong.
 */
SK_TEST(ui_capture_harness_test_font_asset_guard) {
	sk_app_context_t* app = NULL;
	const sk_ui_api_t* ui = NULL;
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char path[SK_FS_PATH_MAX];
	sk_file_handle_t file;
	u64 size_u64;
	sk_ui_font_system_t* sys = NULL;
	sk_ui_font_t* font = NULL;
	sk_ui_font_metrics_t metrics;
	u32 px;
	u32 gi;

	/* 1. Path resolution + file present. */
	TEST_ASSERT_EQUAL_INT(0, sk_ui_capture_harness_test_font_path(path, (u32)sizeof(path)));
	TEST_ASSERT_TRUE_MESSAGE(path[0] != '\0', "test font path must be non-empty");
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_FILE_STATUS_FILE, fs->get_file_status(path),
								  "DejaVuSans.ttf missing under UI test assets — copy from main Content/Fonts/DejaVuSans.ttf (no system/built-in fallback)");

	/* 2. Exact byte size pin (rejects subset/built-in stand-ins). */
	file = fs->open_file(path, SK_FILE_ACCESS_READ);
	TEST_ASSERT_NOT_NULL(file);
	size_u64 = fs->get_file_size(file);
	fs->close_file(file);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(SK_UI_CAPTURE_HARNESS_FONT_FILE_SIZE, (u32)size_u64, "DejaVuSans.ttf size must match main Content/Fonts/DejaVuSans.ttf (757076 bytes)");

	/* 3. Harness loader succeeds (hard-fails on missing/wrong asset). */
	app = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(app);
	ui = uich_load_ui_api(app);
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required for font guard");
	if (ui == NULL) {
		sk_app_destroy(app);
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, ui->init());
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, sk_ui_capture_harness_load_test_font(ui, &sys, &font),
								  "harness must load vendored DejaVuSans.ttf — must not fall back to embedded skore_test_font");
	TEST_ASSERT_NOT_NULL(sys);
	TEST_ASSERT_NOT_NULL(font);

	/* 4. Fixed pixel-size / content-scale pins. */
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, SK_UI_CAPTURE_HARNESS_CONTENT_SCALE);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 96.0f, SK_UI_CAPTURE_HARNESS_DPI);
	px = sk_ui_font_pixel_size(SK_UI_CAPTURE_HARNESS_FONT_LOGICAL_SIZE, SK_UI_CAPTURE_HARNESS_CONTENT_SCALE);
	TEST_ASSERT_EQUAL_UINT(SK_UI_CAPTURE_HARNESS_FONT_PIXEL_SIZE, px);
	TEST_ASSERT_EQUAL_INT(0, ui->font_get_metrics(font, px, &metrics));
	TEST_ASSERT_TRUE(metrics.ascent > 0.0f);
	TEST_ASSERT_TRUE(metrics.line_height > 0.0f);

	/* 5. Distinguish from the tiny embedded subset: full DejaVu face has many
	 * more glyphs; 'A' and non-ASCII must resolve (subset may lack some). */
	gi = ui->font_glyph_index(font, (u32)'A');
	TEST_ASSERT_TRUE(gi != 0u);
	/* Em dash / Latin-1: present in full DejaVuSans, absent from ASCII subset. */
	TEST_ASSERT_TRUE_MESSAGE(ui->font_glyph_index(font, 0x2014u) != 0u || ui->font_glyph_index(font, 0x00E9u) != 0u,
							 "loaded face looks like the built-in ASCII subset, not full DejaVuSans.ttf");

	/* 6. capture with load_test_font must not skip/error when the asset is present
	 * (Vulkan may still skip — only assert that font load itself is not the cause
	 * of RC_ERROR when we force a clear-only scene without GPU work via helper). */
	{
		sk_ui_font_system_t* sys2 = NULL;
		sk_ui_font_t* font2 = NULL;
		/* Second load: proves the path is stable across repeated calls (no
		 * once-only init, no shared mutable font state). */
		TEST_ASSERT_EQUAL_INT(0, sk_ui_capture_harness_load_test_font(ui, &sys2, &font2));
		TEST_ASSERT_NOT_NULL(sys2);
		TEST_ASSERT_NOT_NULL(font2);
		ui->font_destroy(font2);
		ui->font_system_destroy(sys2);
	}

	ui->font_destroy(font);
	ui->font_system_destroy(sys);
	ui->shutdown();
	sk_app_destroy(app);
}

/*
 * APX-250: two consecutive captures of the same text scene with the pinned
 * DejaVuSans face must be byte-identical (scale 1x, fixed atlas, fixed FT
 * raster flags). Skips cleanly without a Vulkan ICD.
 */
static i32 uich_scene_text_pinned(sk_ui_capture_scene_t* scene, void* user) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_context_t* ctx = scene->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t label;
	sk_ui_style_props_t props;

	(void)user;
	/* Font must already be installed by load_test_font — never load built-in. */
	if (scene->font_system == NULL || scene->font == NULL) {
		fprintf(stderr, "ui_capture_harness: text scene missing pinned test font (load_test_font required)\n");
		return -1;
	}

	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.background_color = sk_ui_rgba(0.10f, 0.12f, 0.16f, 1.0f);
	props.layout.width = sk_ui_pt((f32)scene->width);
	props.layout.height = sk_ui_pt((f32)scene->height);
	ui->node_set_inline_style(ctx, root, &props);

	label = ui->widget_label(ctx, root, "UI 42", "lbl-pin");
	if (!sk_ui_node_is_valid(label)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.color = sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f);
	props.font_size = SK_UI_CAPTURE_HARNESS_FONT_LOGICAL_SIZE;
	props.layout.width = sk_ui_pt(160.0f);
	props.layout.height = sk_ui_pt(32.0f);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(16.0f);
	props.layout.top = sk_ui_pt(16.0f);
	ui->node_set_inline_style(ctx, label, &props);
	return 0;
}

SK_TEST(ui_capture_harness_text_font_byte_stable) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t a;
	sk_ui_cpu_image_t b;
	i32 rc;
	size_t nbytes;

	memset(&params, 0, sizeof(params));
	params.scene_name = "ui_capture_harness_text_font_byte_stable";
	params.width = 192u;
	params.height = 96u;
	params.time_seconds = 0.0;
	params.load_test_font = 1; /* pinned DejaVuSans.ttf only */

	rc = sk_ui_capture_harness_capture(&params, uich_scene_text_pinned, NULL, &a);
	if (rc == SK_UI_CAPTURE_HARNESS_RC_SKIPPED) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping UI capture harness text stability test");
	}
	TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
	TEST_ASSERT_NOT_NULL(a.pixels);

	rc = sk_ui_capture_harness_capture(&params, uich_scene_text_pinned, NULL, &b);
	TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
	TEST_ASSERT_NOT_NULL(b.pixels);
	TEST_ASSERT_EQUAL_UINT(a.width, b.width);
	TEST_ASSERT_EQUAL_UINT(a.height, b.height);
	TEST_ASSERT_EQUAL_UINT(a.channels, b.channels);

	nbytes = (size_t)a.width * a.height * a.channels;
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(a.pixels, b.pixels, nbytes), "two consecutive DejaVuSans text captures must be byte-identical");

	sk_ui_capture_harness_image_free(&a);
	sk_ui_capture_harness_image_free(&b);
}

#endif /* SK_TESTS */
