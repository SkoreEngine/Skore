/*
 * ui_capture_harness.c — reusable UI capture test harness (APX-228).
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
 * 1x, no fonts unless the scene opts in, full-surface viewport. */
static i32 uich_refresh(const sk_ui_api_t* ui, sk_ui_context_t* ctx, u32 width, u32 height, sk_ui_font_system_t* fonts, sk_ui_font_t* font) {
	sk_ui_paint_params_t paint_params;

	if (ui->style_resolve(ctx) != 0) {
		return -1;
	}
	if (ui->layout(ctx, (f32)width, (f32)height) != 0) {
		return -1;
	}
	if (ui->layout_apply_scale(ctx, 1.0f, 1.0f) != 0) {
		return -1;
	}
	memset(&paint_params, 0, sizeof(paint_params));
	paint_params.font_system = fonts;
	paint_params.font = font;
	if (ui->paint(ctx, &paint_params) != 0) {
		return -1;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

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

	if (out_image != NULL) {
		memset(out_image, 0, sizeof(*out_image));
	}
	if (params == NULL || params->scene_name == NULL || params->scene_name[0] == '\0' || params->width == 0u || params->height == 0u || out_image == NULL) {
		return SK_UI_CAPTURE_HARNESS_RC_ERROR;
	}

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
	if (scene != NULL && scene(&scene_info, user) != 0) {
		goto out;
	}
	fonts = scene_info.font_system;
	font = scene_info.font;
	if (uich_refresh(ui, ui_ctx, params->width, params->height, fonts, font) != 0) {
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
	if (ui->test_artifact_png_path(fs, params->scene_name, png_path, (u32)sizeof(png_path)) != 0 || ui->cpu_image_write_png(out_image, fs, png_path) != 0) {
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

/* Expected artifact path for a simple scene name (mirrors the plugin's
 * documented resolution order: env → compile-time → {temp}/skore-test-artifacts). */
static void uich_artifact_path(const_chr_t scene_name, char* out, u32 out_cap) {
	const_chr_t env = getenv("SK_TEST_ARTIFACT_DIR");
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	(void)fs; /* only needed by the temp-folder fallback */
	if (env != NULL && env[0] != '\0') {
		snprintf(out, out_cap, "%s/%s.png", env, scene_name);
		return;
	}
#if defined(SK_TEST_ARTIFACT_DIR)
	snprintf(out, out_cap, SK_TEST_ARTIFACT_DIR "/%s.png", scene_name);
#else
	{
		char tmp[SK_FS_PATH_MAX];
		if (fs->temp_folder(tmp, (u32)sizeof(tmp)) == 0) {
			snprintf(out, out_cap, "%s/skore-test-artifacts/%s.png", tmp, scene_name);
			return;
		}
		snprintf(out, out_cap, "%s.png", scene_name);
	}
#endif
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

	/* rc == OK implies the PNG artifact was written; verify on disk. */
	{
		const sk_filesystem_api_t* fs = sk_filesystem_api();
		char path[SK_FS_PATH_MAX];
		uich_artifact_path(params.scene_name, path, (u32)sizeof(path));
		TEST_ASSERT_EQUAL_INT_MESSAGE(SK_FILE_STATUS_FILE, fs->get_file_status(path), "capture PNG artifact must exist on disk");
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

#endif /* SK_TESTS */
