/*
 * ui_capture.c — headless offscreen UI render + CPU readback test (APX-226).
 *
 * Drives the sk_ui_api_t capture path end to end against the real Vulkan
 * backend: an offscreen RGBA8 color target (no swapchain / window / surface),
 * explicit clear color, the standard UI GPU renderer, and copy_texture_to_buffer
 * readback into a tightly-packed CPU image.
 *
 * Verifies, with fixed (never random) coordinates:
 *   1. Clear-only frame: empty draw list → whole buffer == explicit clear color.
 *   2. Solid-color fill frame: full-surface opaque fill → corners + interior
 *      points == exact RGBA8 (straight alpha, not premultiplied).
 *   3. Region frame: opaque child box on top of the fill → inside/outside
 *      pixels differ per region (proves row-stride / tight packing).
 *   4. Reuse + teardown: many frames on one capture, then several
 *      create/destroy cycles in the same process (clean resource release).
 *
 * Skips cleanly when no Vulkan loader/ICD is present (same policy as
 * ui_render.c).
 */

#include "app.h"
#include "dxc_compiler.h"
#include "filesystem.h"
#include "path.h"
#include "render_device.h"
#include "test.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS

#define UI_CAP_W 64u
#define UI_CAP_H 64u
#define UI_CAP_CHANNELS 4u

/* Exact straight-alpha RGBA8 (R in the low byte) — all channels 0 or 255 so
 * the float→unorm round trip through the shader is lossless. */
#define UI_CAP_PX_RED 0xFF0000FFu
#define UI_CAP_PX_GREEN 0xFF00FF00u
#define UI_CAP_PX_WHITE 0xFFFFFFFFu

/* -------------------------------------------------------------------------- */
/* Paths / plugins                                                            */
/* -------------------------------------------------------------------------- */

static i32 ui_cap_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
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

static const sk_render_device_api_t* ui_cap_load_vulkan_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-vulkan-render-device.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-vulkan-render-device.dylib";
#else
	const_chr_t plugin_name = "sk-vulkan-render-device.so";
#endif
	if (ui_cap_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_render_device_api_t*)app_api->get_api(ctx, SK_RENDER_DEVICE_API_TYPE_ID);
}

static const sk_dxc_compiler_api_t* ui_cap_load_dxc_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-dxc-compiler.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-dxc-compiler.dylib";
#else
	const_chr_t plugin_name = "sk-dxc-compiler.so";
#endif
	if (ui_cap_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_dxc_compiler_api_t*)app_api->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
}

static const sk_ui_api_t* ui_cap_load_ui_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-ui.dylib";
#else
	const_chr_t plugin_name = "sk-ui.so";
#endif
	if (ui_cap_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_ui_api_t*)app_api->get_api(ctx, SK_UI_API_TYPE_ID);
}

static sk_adapter_t ui_cap_select_adapter(const sk_render_device_api_t* api, sk_render_device_t dev, u32* out_count) {
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
/* Pixel helpers (fixed coordinates only)                                     */
/* -------------------------------------------------------------------------- */

static u32 ui_cap_pixel(const sk_ui_cpu_image_t* img, u32 x, u32 y) {
	const u8* p;
	if (img->pixels == NULL || x >= img->width || y >= img->height) {
		return 0u;
	}
	p = img->pixels + ((size_t)y * img->width + x) * img->channels;
	/* R in the low byte — matches sk_ui_pack_color / vertex layout. */
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void ui_cap_assert_pixel(const sk_ui_cpu_image_t* img, u32 x, u32 y, u32 expected) {
	char msg[128];
	const u32 actual = ui_cap_pixel(img, x, y);
	snprintf(msg, sizeof(msg), "pixel (%u,%u) = 0x%08X, expected 0x%08X", x, y, actual, expected);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(expected, actual, msg);
}

static void ui_cap_assert_all_pixels(const sk_ui_cpu_image_t* img, u32 expected) {
	u32 x;
	u32 y;
	for (y = 0u; y < img->height; ++y) {
		for (x = 0u; x < img->width; ++x) {
			ui_cap_assert_pixel(img, x, y, expected);
		}
	}
}

/* -------------------------------------------------------------------------- */
/* Fixture tree                                                               */
/* -------------------------------------------------------------------------- */

static void ui_cap_set_root_fill(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_color_t color) {
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_style_props_t props;

	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.background_color = color;
	props.layout.width = sk_ui_pt((f32)UI_CAP_W);
	props.layout.height = sk_ui_pt((f32)UI_CAP_H);
	ui->node_set_inline_style(ctx, root, &props);
}

/* Green opaque 16x16 box absolutely placed at (8,8) inside the root. */
static sk_ui_node_t ui_cap_add_child_box(const sk_ui_api_t* ui, sk_ui_context_t* ctx) {
	sk_ui_node_t child = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, ui->context_root(ctx));
	sk_ui_style_props_t props;

	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.background_color = sk_ui_rgba(0.0f, 1.0f, 0.0f, 1.0f);
	props.layout.width = sk_ui_pt(16.0f);
	props.layout.height = sk_ui_pt(16.0f);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(8.0f);
	props.layout.top = sk_ui_pt(8.0f);
	ui->node_set_inline_style(ctx, child, &props);
	return child;
}

static i32 ui_cap_refresh(const sk_ui_api_t* ui, sk_ui_context_t* ctx) {
	if (ui->style_resolve(ctx) != 0) {
		return -1;
	}
	if (ui->layout(ctx, (f32)UI_CAP_W, (f32)UI_CAP_H) != 0) {
		return -1;
	}
	if (ui->layout_apply_scale(ctx, 1.0f, 1.0f) != 0) {
		return -1;
	}
	{
		sk_ui_paint_params_t paint_params;
		memset(&paint_params, 0, sizeof(paint_params));
		paint_params.font_system = NULL;
		paint_params.font = NULL;
		if (ui->paint(ctx, &paint_params) != 0) {
			return -1;
		}
	}
	return 0;
}

/* Create a capture (fixed size + explicit clear color), render a red fill
 * frame, verify corners + fixed points + full scan, then destroy. */
static void ui_cap_cycle(const sk_ui_api_t* ui, const sk_render_device_api_t* api, sk_render_device_t dev, const sk_dxc_compiler_api_t* dxc) {
	sk_ui_context_t* ctx;
	sk_ui_capture_t* capture;
	const sk_ui_draw_list_t* dl;
	sk_ui_cpu_image_t img;
	sk_ui_capture_desc_t cdesc;
	sk_ui_capture_frame_info_t finfo;

	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	if (ctx == NULL) {
		return;
	}
	ui_cap_set_root_fill(ui, ctx, sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui_cap_refresh(ui, ctx));
	dl = ui->get_draw_list(ctx);
	TEST_ASSERT_NOT_NULL(dl);
	TEST_ASSERT_TRUE(dl->command_count > 0u);

	memset(&cdesc, 0, sizeof(cdesc));
	cdesc.device_api = api;
	cdesc.device = dev;
	cdesc.dxc = dxc;
	cdesc.width = UI_CAP_W;
	cdesc.height = UI_CAP_H;
	cdesc.clear_color.color.r = 1.0f;
	cdesc.clear_color.color.g = 1.0f;
	cdesc.clear_color.color.b = 1.0f;
	cdesc.clear_color.color.a = 1.0f;
	cdesc.debug_name = "ui-capture-test";
	capture = ui->capture_create(&cdesc);
	TEST_ASSERT_NOT_NULL_MESSAGE(capture, "capture create must succeed");
	if (capture == NULL) {
		ui->context_destroy(ctx);
		return;
	}

	memset(&finfo, 0, sizeof(finfo));
	finfo.draw_list = dl;
	TEST_ASSERT_EQUAL_INT(0, ui->capture_frame(capture, &finfo, &img));
	TEST_ASSERT_EQUAL_UINT(UI_CAP_W, img.width);
	TEST_ASSERT_EQUAL_UINT(UI_CAP_H, img.height);
	TEST_ASSERT_EQUAL_UINT(UI_CAP_CHANNELS, img.channels);
	TEST_ASSERT_NOT_NULL(img.pixels);
	if (img.pixels == NULL) {
		ui->capture_destroy(capture);
		ui->context_destroy(ctx);
		return;
	}

	/* Fixed coordinates: the four corners plus several interior points. */
	ui_cap_assert_pixel(&img, 0u, 0u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, UI_CAP_W - 1u, 0u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 0u, UI_CAP_H - 1u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, UI_CAP_W - 1u, UI_CAP_H - 1u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 32u, 32u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 16u, 48u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 48u, 16u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 8u, 8u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 56u, 56u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 40u, 24u, UI_CAP_PX_RED);
	/* Solid fill: every pixel must be the exact color (row-stride proof). */
	ui_cap_assert_all_pixels(&img, UI_CAP_PX_RED);

	ui->capture_destroy(capture);
	ui->context_destroy(ctx);
}

/* -------------------------------------------------------------------------- */
/* Test                                                                       */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_capture_offscreen_readback) {
	sk_app_context_t* ctx = NULL;
	const sk_render_device_api_t* api = NULL;
	const sk_dxc_compiler_api_t* dxc = NULL;
	const sk_ui_api_t* ui = NULL;
	sk_render_device_t dev = sk_render_device_t_zero();
	sk_ui_context_t* ui_ctx = NULL;
	sk_ui_capture_t* capture = NULL;
	sk_ui_capture_desc_t cdesc;
	sk_ui_capture_frame_info_t finfo;
	sk_ui_cpu_image_t img;
	const sk_ui_draw_list_t* dl;
	sk_ui_draw_list_t empty_dl;
	sk_ui_node_t child;
	i32 cycle;

	sk_app_boot_t boot = sk_app_init(0, NULL);

	ctx = boot.context;
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	api = ui_cap_load_vulkan_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "vulkan render device API required");
	dxc = ui_cap_load_dxc_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(dxc, "dxc compiler API required");
	ui = ui_cap_load_ui_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui API required");
	if (api == NULL || dxc == NULL || ui == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, ui->init());
	{
		const i32 dxc_rc = dxc->init();
		if (dxc_rc != 0) {
			ui->shutdown();
			sk_app_shutdown(ctx);
		}
		TEST_ASSERT_EQUAL_INT32_MESSAGE(0, dxc_rc, "DXC runtime must load");
	}

	dev = api->init(ctx, NULL);
	if (!sk_render_device_t_is_valid(dev)) {
		dxc->shutdown();
		ui->shutdown();
		TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping UI capture test");
		sk_app_shutdown(ctx);
		return;
	}
	{
		u32 adapter_count = 0u;
		sk_adapter_t best = ui_cap_select_adapter(api, dev, &adapter_count);
		if (adapter_count == 0u || !sk_adapter_t_is_valid(best)) {
			api->destroy(dev);
			dxc->shutdown();
			ui->shutdown();
			TEST_IGNORE_MESSAGE("no suitable adapter; skipping UI capture test");
			sk_app_shutdown(ctx);
			return;
		}
		TEST_ASSERT_EQUAL_INT(0, api->select_adapter(dev, best));
	}

	memset(&cdesc, 0, sizeof(cdesc));
	cdesc.device_api = api;
	cdesc.device = dev;
	cdesc.dxc = dxc;
	cdesc.width = UI_CAP_W;
	cdesc.height = UI_CAP_H;
	cdesc.clear_color.color.r = 1.0f;
	cdesc.clear_color.color.g = 1.0f;
	cdesc.clear_color.color.b = 1.0f;
	cdesc.clear_color.color.a = 1.0f;
	cdesc.debug_name = "ui-capture-test";

	/* UI tree: root red fill; the green child box is added only for the
	 * region frame so the plain fill frame is a uniform solid color. */
	ui_ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ui_ctx);
	ui_cap_set_root_fill(ui, ui_ctx, sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui_cap_refresh(ui, ui_ctx));
	dl = ui->get_draw_list(ui_ctx);
	TEST_ASSERT_NOT_NULL(dl);
	TEST_ASSERT_TRUE(dl->command_count > 0u);

	capture = ui->capture_create(&cdesc);
	TEST_ASSERT_NOT_NULL_MESSAGE(capture, "capture create must succeed");
	if (capture == NULL) {
		goto cleanup;
	}

	memset(&finfo, 0, sizeof(finfo));

	/* Frame 1: empty draw list → only the explicit clear color (white). */
	memset(&empty_dl, 0, sizeof(empty_dl));
	finfo.draw_list = &empty_dl;
	TEST_ASSERT_EQUAL_INT(0, ui->capture_frame(capture, &finfo, &img));
	TEST_ASSERT_EQUAL_UINT(UI_CAP_W, img.width);
	TEST_ASSERT_EQUAL_UINT(UI_CAP_H, img.height);
	TEST_ASSERT_EQUAL_UINT(UI_CAP_CHANNELS, img.channels);
	TEST_ASSERT_NOT_NULL(img.pixels);
	ui_cap_assert_pixel(&img, 0u, 0u, UI_CAP_PX_WHITE);
	ui_cap_assert_pixel(&img, UI_CAP_W - 1u, UI_CAP_H - 1u, UI_CAP_PX_WHITE);
	ui_cap_assert_pixel(&img, 32u, 32u, UI_CAP_PX_WHITE);
	ui_cap_assert_all_pixels(&img, UI_CAP_PX_WHITE);

	/* Frame 2: solid red fill over the whole target. */
	finfo.draw_list = dl;
	TEST_ASSERT_EQUAL_INT(0, ui->capture_frame(capture, &finfo, &img));
	ui_cap_assert_pixel(&img, 0u, 0u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, UI_CAP_W - 1u, 0u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 0u, UI_CAP_H - 1u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, UI_CAP_W - 1u, UI_CAP_H - 1u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 32u, 32u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 16u, 48u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 48u, 16u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 8u, 8u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 56u, 56u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 40u, 24u, UI_CAP_PX_RED);
	ui_cap_assert_all_pixels(&img, UI_CAP_PX_RED);

	/* Frame 3: non-uniform content — green box at (8,8)..(24,24). Proves the
	 * readback rows are unpacked correctly (a wrong row stride would smear). */
	child = ui_cap_add_child_box(ui, ui_ctx);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(child));
	TEST_ASSERT_EQUAL_INT(0, ui_cap_refresh(ui, ui_ctx));
	dl = ui->get_draw_list(ui_ctx);
	TEST_ASSERT_NOT_NULL(dl);
	TEST_ASSERT_TRUE(dl->command_count > 0u);
	finfo.draw_list = dl;
	TEST_ASSERT_EQUAL_INT(0, ui->capture_frame(capture, &finfo, &img));
	ui_cap_assert_pixel(&img, 8u, 8u, UI_CAP_PX_GREEN);
	ui_cap_assert_pixel(&img, 16u, 16u, UI_CAP_PX_GREEN);
	ui_cap_assert_pixel(&img, 23u, 23u, UI_CAP_PX_GREEN);
	ui_cap_assert_pixel(&img, 8u, 23u, UI_CAP_PX_GREEN);
	ui_cap_assert_pixel(&img, 23u, 8u, UI_CAP_PX_GREEN);
	ui_cap_assert_pixel(&img, 7u, 7u, UI_CAP_PX_RED);  /* left/top of box */
	ui_cap_assert_pixel(&img, 24u, 8u, UI_CAP_PX_RED); /* right of box */
	ui_cap_assert_pixel(&img, 8u, 24u, UI_CAP_PX_RED); /* below box */
	ui_cap_assert_pixel(&img, 40u, 40u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 0u, 0u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, UI_CAP_W - 1u, UI_CAP_H - 1u, UI_CAP_PX_RED);

	/* Frames 4-5: same capture reused (resources must be reusable). The
	 * draw list is context-owned and is invalidated by repaint (rebuild
	 * reallocs), so re-query the current list instead of holding old
	 * pointers across frames. */
	finfo.draw_list = &empty_dl;
	TEST_ASSERT_EQUAL_INT(0, ui->capture_frame(capture, &finfo, &img));
	ui_cap_assert_all_pixels(&img, UI_CAP_PX_WHITE);
	finfo.draw_list = dl; /* current list (child box still in tree) */
	TEST_ASSERT_EQUAL_INT(0, ui->capture_frame(capture, &finfo, &img));
	ui_cap_assert_pixel(&img, 8u, 8u, UI_CAP_PX_GREEN);
	ui_cap_assert_pixel(&img, 40u, 40u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, 0u, 0u, UI_CAP_PX_RED);
	ui_cap_assert_pixel(&img, UI_CAP_W - 1u, UI_CAP_H - 1u, UI_CAP_PX_RED);

	/* PNG artifact under the single test-artifact root (APX-227). */
	{
		const sk_filesystem_api_t* fs = sk_test_filesystem_table();
		char png_path[SK_FS_PATH_MAX];
		TEST_ASSERT_EQUAL_INT(0, ui->test_artifact_png_path(fs, "ui_capture_offscreen_readback", png_path, (u32)sizeof(png_path)));
		TEST_ASSERT_EQUAL_INT_MESSAGE(0, ui->cpu_image_write_png(&img, fs, png_path), "failed to write capture PNG artifact");
	}

	/* Clean teardown: repeated create/destroy cycles in one process. */
	ui->capture_destroy(capture);
	capture = NULL;
	for (cycle = 0; cycle < 3; ++cycle) {
		ui_cap_cycle(ui, api, dev, dxc);
	}

cleanup:
	if (capture != NULL) {
		ui->capture_destroy(capture);
	}
	if (ui_ctx != NULL) {
		ui->context_destroy(ui_ctx);
	}
	dxc->shutdown();
	ui->shutdown();
	if (sk_render_device_t_is_valid(dev)) {
		api->destroy(dev);
	}
	sk_app_shutdown(ctx);
}

#endif /* SK_TESTS */
