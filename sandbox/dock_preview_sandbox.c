/* Headless host: offscreen GPU capture to PNG.
 *
 * Default scene: sk-player docking drop preview (same as `sk-player --dock-demo`).
 * Widget review: `--widget <name> --out <dir>` writes `{name}_{state}.png`
 * (see docs/widget-lavapipe-png-review.md). Not a test; not registered with CTest.
 *
 * Usage:
 *   sk-sandbox [--widget <name>] [--state <name>] [--out <path>] [--list] [--help]
 */

#include "app.h"
#include "dxc_compiler.h"
#include "filesystem.h"
#include "path.h"
#include "render_device.h"
#include "ui.h"
#include "widget_review.h"

#include "skore_test_font_ttf.h"

#include <stdio.h>
#include <string.h>

#define SANDBOX_W 1280u
#define SANDBOX_H 720u
#define SANDBOX_TAB "dock-demo-hierarchy-dock-tab"
#define SANDBOX_PREVIEW "dock-demo-drop-preview"
#define SANDBOX_DEFAULT_PNG "dock_preview.png"
#define SANDBOX_DEFAULT_WIDGET_DIR "widget-review"

typedef struct sandbox_t {
	const sk_app_api_t* app_api;
	sk_app_context_t* app;
	const sk_filesystem_api_t* fs;
	const sk_ui_api_t* ui;
	const sk_render_device_api_t* rd;
	const sk_dxc_compiler_api_t* dxc;
	sk_render_device_t device;
	sk_ui_context_t* ctx;
	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;
	sk_ui_capture_t* capture;
	sk_ui_rect_t preview;
	i32 torn;
	i32 hover_dir;
} sandbox_t;

static i32 sandbox_has_arg(int argc, char* argv[], const_chr_t flag) {
	i32 i;
	for (i = 1; i < argc; ++i) {
		if (argv[i] != NULL && strcmp(argv[i], flag) == 0) {
			return 1;
		}
	}
	return 0;
}

static const_chr_t sandbox_arg_value(int argc, char* argv[], const_chr_t flag) {
	i32 i;
	for (i = 1; i < argc - 1; ++i) {
		if (argv[i] != NULL && strcmp(argv[i], flag) == 0) {
			return argv[i + 1];
		}
	}
	return NULL;
}

static void sandbox_usage(const_chr_t prog) {
	fprintf(stderr,
			"usage: %s [--widget <name>] [--state <name>] [--out <path>] [--list] [--help]\n"
			"  --widget  manifest family (button, text, checkbox, …); omit for dock preview\n"
			"  --state   default|hovered|pressed|disabled|focused|all (widget mode only)\n"
			"  --out     dock: PNG file (default: ./%s)\n"
			"            --widget: output directory (default: ./widget-review)\n"
			"  --list    print widget names and states; no GPU\n"
			"  --help    this message\n"
			"Recipe: docs/widget-lavapipe-png-review.md\n",
			prog, SANDBOX_DEFAULT_PNG);
}

static const_chr_t sandbox_dir_name(i32 dir) {
	switch ((sk_ui_dock_dir_t)dir) {
	case SK_UI_DOCK_DIR_LEFT:
		return "LEFT";
	case SK_UI_DOCK_DIR_RIGHT:
		return "RIGHT";
	case SK_UI_DOCK_DIR_UP:
		return "UP";
	case SK_UI_DOCK_DIR_DOWN:
		return "DOWN";
	case SK_UI_DOCK_DIR_CENTER:
		return "CENTER";
	case SK_UI_DOCK_DIR_NONE:
	default:
		return "NONE";
	}
}

static sk_adapter_t sandbox_select_adapter(const sk_render_device_api_t* api, sk_render_device_t dev) {
	u32 adapter_count = api->get_adapter_count(dev);
	sk_adapter_t best = sk_adapter_t_zero();
	u32 best_score = 0u;
	u32 i;
	for (i = 0u; i < adapter_count; ++i) {
		sk_adapter_t candidate = api->get_adapter(dev, i);
		u32 score = api->get_adapter_score(dev, candidate);
		if (score > best_score) {
			best_score = score;
			best = candidate;
		}
	}
	return best;
}

static void sandbox_ptr(sk_ui_input_event_t* ev, sk_ui_input_kind_t kind, f32 x, f32 y, i32 down) {
	memset(ev, 0, sizeof(*ev));
	ev->kind = kind;
	ev->x = x;
	ev->y = y;
	ev->button = SK_UI_POINTER_BUTTON_LEFT;
	ev->down = down;
}

static i32 sandbox_layout(sandbox_t* s, f32 w, f32 h) {
	if (s->ui->style_resolve(s->ctx) != 0) {
		return -1;
	}
	if (s->ui->layout(s->ctx, w, h) != 0) {
		return -1;
	}
	return s->ui->layout_apply_scale(s->ctx, 1.0f, 1.0f);
}

static i32 sandbox_drive_preview(sandbox_t* s) {
	const sk_ui_api_t* ui = s->ui;
	sk_ui_context_t* ctx = s->ctx;
	const f32 w = (f32)SANDBOX_W;
	const f32 h = (f32)SANDBOX_H;
	sk_ui_node_t tab;
	sk_ui_node_t preview;
	sk_ui_dock_node_t space;
	sk_ui_dock_node_t scene_leaf;
	sk_ui_rect_t tab_r;
	sk_ui_rect_t scene_r;
	sk_ui_rect_t space_r;
	sk_ui_input_event_t ev;
	sk_ui_dock_dir_t dir;
	f32 tx;
	f32 ty;
	f32 hx;
	f32 hy;

	if (!sk_ui_node_is_valid(ui->sample_dock_demo_build(ctx, SK_UI_NODE_INVALID))) {
		fprintf(stderr, "sk-sandbox: sample_dock_demo_build failed\n");
		return -1;
	}
	if (sandbox_layout(s, w, h) != 0) {
		fprintf(stderr, "sk-sandbox: initial layout failed\n");
		return -1;
	}

	space = ui->dockspace_find(ctx, "dock-demo");
	space_r.x = 0.0f;
	space_r.y = 0.0f;
	space_r.width = w;
	space_r.height = h;
	if (sk_ui_dock_node_is_valid(space)) {
		(void)ui->dockspace_layout(ctx, space, &space_r);
	}

	tab = ui->find_by_id(ctx, SANDBOX_TAB);
	if (!sk_ui_node_is_valid(tab) || ui->node_get_abs_rect(ctx, tab, &tab_r, NULL) != 0) {
		fprintf(stderr, "sk-sandbox: hierarchy tab missing after layout\n");
		return -1;
	}

	tx = tab_r.x + 8.0f;
	ty = tab_r.y + 8.0f;
	sandbox_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, tx, ty, 0);
	(void)ui->input_dispatch(ctx, &ev);
	sandbox_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, tx, ty, 1);
	(void)ui->input_dispatch(ctx, &ev);
	sandbox_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, tx, ty + 80.0f, 1);
	(void)ui->input_dispatch(ctx, &ev);
	if (sandbox_layout(s, w, h) != 0) {
		fprintf(stderr, "sk-sandbox: post-tear layout failed\n");
		return -1;
	}

	s->torn = ui->dock_window_is_docked(ctx, "dock-demo-hierarchy") == 0 ? 1 : 0;
	if (sk_ui_dock_node_is_valid(space)) {
		(void)ui->dockspace_layout(ctx, space, &space_r);
	}
	scene_leaf = ui->dock_find_node_for_window(ctx, "dock-demo-scene");
	if (!sk_ui_dock_node_is_valid(scene_leaf) || ui->dock_node_get_rect(ctx, scene_leaf, &scene_r) != 0) {
		fprintf(stderr, "sk-sandbox: scene leaf missing after tear\n");
		return -1;
	}

	hx = scene_r.x + 80.0f;
	hy = scene_r.y + scene_r.height * 0.5f;
	if (hx < 20.0f) {
		hx = 20.0f;
	}
	sandbox_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, hx, hy, 1);
	(void)ui->input_dispatch(ctx, &ev);
	if (sandbox_layout(s, w, h) != 0) {
		fprintf(stderr, "sk-sandbox: post-hover layout failed\n");
		return -1;
	}

	(void)ui->dock_node_at_point(ctx, hx, hy, &dir);
	s->hover_dir = (i32)dir;
	preview = ui->find_by_id(ctx, SANDBOX_PREVIEW);
	if (sk_ui_node_is_valid(preview)) {
		(void)ui->node_get_abs_rect(ctx, preview, &s->preview, NULL);
	}
	return 0;
}

static i32 sandbox_paint_and_capture(sandbox_t* s, const_chr_t png_path) {
	sk_ui_paint_params_t paint;
	sk_ui_capture_frame_info_t finfo;
	sk_ui_cpu_image_t img;
	const sk_ui_draw_list_t* dl;
	sk_ui_draw_list_t empty;

	memset(&paint, 0, sizeof(paint));
	paint.font_system = s->fonts;
	paint.font = s->font;
	if (s->ui->paint(s->ctx, &paint) != 0) {
		fprintf(stderr, "sk-sandbox: paint failed\n");
		return -1;
	}

	dl = s->ui->get_draw_list(s->ctx);
	memset(&finfo, 0, sizeof(finfo));
	if (dl != NULL) {
		finfo.draw_list = dl;
	} else {
		memset(&empty, 0, sizeof(empty));
		finfo.draw_list = &empty;
	}
	finfo.font_system = s->fonts;
	finfo.font = s->font;
	memset(&img, 0, sizeof(img));
	if (s->ui->capture_frame(s->capture, &finfo, &img) != 0) {
		fprintf(stderr, "sk-sandbox: capture_frame failed\n");
		return -1;
	}
	if (s->ui->cpu_image_write_png(&img, s->fs, png_path) != 0) {
		fprintf(stderr, "sk-sandbox: cpu_image_write_png failed for '%s'\n", png_path);
		return -1;
	}
	printf("sk-sandbox: wrote %ux%u '%s' torn=%d hover=%s preview=[%.1f,%.1f,%.1f,%.1f]\n", img.width, img.height, png_path, s->torn, sandbox_dir_name(s->hover_dir),
		   (double)s->preview.x, (double)s->preview.y, (double)s->preview.width, (double)s->preview.height);
	return 0;
}

static void sandbox_shutdown(sandbox_t* s) {
	if (s->ui != NULL && s->capture != NULL) {
		s->ui->capture_destroy(s->capture);
		s->capture = NULL;
	}
	if (s->ui != NULL && s->font != NULL) {
		s->ui->font_destroy(s->font);
		s->font = NULL;
	}
	if (s->ui != NULL && s->fonts != NULL) {
		s->ui->font_system_destroy(s->fonts);
		s->fonts = NULL;
	}
	if (s->ui != NULL && s->ctx != NULL) {
		s->ui->context_destroy(s->ctx);
		s->ctx = NULL;
	}
	if (s->rd != NULL && sk_render_device_t_is_valid(s->device)) {
		sandbox_widget_image_tex_release();
		s->rd->destroy(s->device);
		s->device = sk_render_device_t_zero();
	}
	if (s->dxc != NULL) {
		s->dxc->shutdown();
		s->dxc = NULL;
	}
	if (s->ui != NULL) {
		s->ui->shutdown();
		s->ui = NULL;
	}
	if (s->app != NULL) {
		sk_app_shutdown(s->app);
		s->app = NULL;
	}
}

static i32 sandbox_init(sandbox_t* s, int argc, char* argv[], u32 width, u32 height) {
	sk_app_boot_t boot;
	sk_adapter_t adapter;
	sk_ui_capture_desc_t cdesc;

	memset(s, 0, sizeof(*s));
	boot = sk_app_init(argc, argv);
	s->app = boot.context;
	s->app_api = boot.api;
	if (s->app == NULL) {
		fprintf(stderr, "sk-sandbox: sk_app_init failed\n");
		return -1;
	}

	s->fs = s->app_api->filesystem_api(s->app);
	s->ui = (const sk_ui_api_t*)s->app_api->get_api(s->app, SK_UI_API_TYPE_ID);
	s->rd = (const sk_render_device_api_t*)s->app_api->get_api(s->app, SK_RENDER_DEVICE_API_TYPE_ID);
	s->dxc = (const sk_dxc_compiler_api_t*)s->app_api->get_api(s->app, SK_DXC_COMPILER_API_TYPE_ID);
	if (s->ui == NULL || s->rd == NULL || s->dxc == NULL) {
		fprintf(stderr, "sk-sandbox: missing plugin API (need sk-ui, sk-vulkan-render-device, sk-dxc-compiler)\n");
		return -1;
	}
	if (s->ui->init() != 0) {
		fprintf(stderr, "sk-sandbox: ui init failed\n");
		return -1;
	}
	if (s->dxc->init() != 0) {
		fprintf(stderr, "sk-sandbox: dxc init failed\n");
		return -1;
	}

	s->device = s->rd->init(s->app, NULL);
	if (!sk_render_device_t_is_valid(s->device)) {
		fprintf(stderr, "sk-sandbox: render device init failed (no GPU/ICD)\n");
		return -1;
	}
	adapter = sandbox_select_adapter(s->rd, s->device);
	if (!sk_adapter_t_is_valid(adapter) || s->rd->select_adapter(s->device, adapter) != 0) {
		fprintf(stderr, "sk-sandbox: no suitable GPU adapter\n");
		return -1;
	}

	memset(&cdesc, 0, sizeof(cdesc));
	cdesc.device_api = s->rd;
	cdesc.device = s->device;
	cdesc.dxc = s->dxc;
	cdesc.width = width;
	cdesc.height = height;
	cdesc.clear_color.color.r = 0.05f;
	cdesc.clear_color.color.g = 0.06f;
	cdesc.clear_color.color.b = 0.08f;
	cdesc.clear_color.color.a = 1.0f;
	cdesc.debug_name = "sk-sandbox";
	s->capture = s->ui->capture_create(&cdesc);
	if (s->capture == NULL) {
		fprintf(stderr, "sk-sandbox: capture_create failed\n");
		return -1;
	}

	s->ctx = s->ui->context_create(NULL);
	if (s->ctx == NULL) {
		fprintf(stderr, "sk-sandbox: context_create failed\n");
		return -1;
	}
	s->fonts = s->ui->font_system_create(NULL);
	if (s->fonts != NULL) {
		s->font = s->ui->font_load_memory(s->fonts, skore_test_font_ttf, (u32)sizeof(skore_test_font_ttf));
		if (s->font != NULL) {
			(void)s->ui->font_msdf_bake(s->font);
		}
	}
	return 0;
}

static i32 sandbox_path_is_absolute(const_chr_t path) {
	if (path == NULL || path[0] == '\0') {
		return 0;
	}
	if (path[0] == '/' || path[0] == '\\') {
		return 1;
	}
	if (path[1] == ':' && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))) {
		return 1;
	}
	return 0;
}

static i32 sandbox_resolve_path(sandbox_t* s, const_chr_t requested, const_chr_t fallback, char* out, u32 out_cap) {
	char cwd[SK_FS_PATH_MAX];
	const_chr_t name = (requested != NULL && requested[0] != '\0') ? requested : fallback;
	if (sandbox_path_is_absolute(name) != 0) {
		if (snprintf(out, out_cap, "%s", name) < 0 || out[0] == '\0') {
			return -1;
		}
		return 0;
	}
	if (s->fs->current_dir(cwd, (u32)sizeof(cwd)) != 0 || cwd[0] == '\0') {
		if (snprintf(out, out_cap, "%s", name) < 0) {
			return -1;
		}
		return 0;
	}
	return sk_path_join(sk_str_view_cstr(cwd), sk_str_view_cstr(name), out, out_cap) < 0 ? -1 : 0;
}

int main(int argc, char* argv[]) {
	sandbox_t s;
	char out_path[SK_FS_PATH_MAX];
	const_chr_t out_arg;
	const_chr_t widget_arg;
	const_chr_t state_arg;
	u32 width;
	u32 height;
	i32 widget_ready;
	i32 rc;

	if (sandbox_has_arg(argc, argv, "--help") != 0 || sandbox_has_arg(argc, argv, "-h") != 0) {
		sandbox_usage(argv[0]);
		return 0;
	}
	if (sandbox_has_arg(argc, argv, "--list") != 0) {
		sandbox_widget_list();
		return 0;
	}

	out_arg = sandbox_arg_value(argc, argv, "--out");
	widget_arg = sandbox_arg_value(argc, argv, "--widget");
	state_arg = sandbox_arg_value(argc, argv, "--state");
	if (sandbox_has_arg(argc, argv, "--widget") != 0 && (widget_arg == NULL || widget_arg[0] == '\0')) {
		fprintf(stderr, "sk-sandbox: --widget requires a name (use --list)\n");
		return 1;
	}

	if (widget_arg != NULL) {
		if (sandbox_widget_lookup(widget_arg, &width, &height, &widget_ready) != 0) {
			fprintf(stderr, "sk-sandbox: unknown --widget '%s' (use --list)\n", widget_arg);
			return 1;
		}
		if (widget_ready == 0) {
			fprintf(stderr, "sk-sandbox: widget '%s' has no scene yet; add a builder in sandbox/widget_review.c\n", widget_arg);
			return 1;
		}
	} else {
		width = SANDBOX_W;
		height = SANDBOX_H;
	}

	if (sandbox_init(&s, argc, argv, width, height) != 0) {
		sandbox_shutdown(&s);
		return 1;
	}

	if (widget_arg != NULL) {
		sandbox_widget_host_t host;
		if (sandbox_resolve_path(&s, out_arg, SANDBOX_DEFAULT_WIDGET_DIR, out_path, (u32)sizeof(out_path)) != 0) {
			fprintf(stderr, "sk-sandbox: cannot resolve output directory\n");
			sandbox_shutdown(&s);
			return 1;
		}
		memset(&host, 0, sizeof(host));
		host.ui = s.ui;
		host.fs = s.fs;
		host.ctx = s.ctx;
		host.fonts = s.fonts;
		host.font = s.font;
		host.capture = s.capture;
		host.rd = s.rd;
		host.device = s.device;
		host.width = width;
		host.height = height;
		rc = sandbox_widget_run(&host, widget_arg, state_arg, out_path);
		sandbox_shutdown(&s);
		return rc == 0 ? 0 : 1;
	}

	if (sandbox_resolve_path(&s, out_arg, SANDBOX_DEFAULT_PNG, out_path, (u32)sizeof(out_path)) != 0) {
		fprintf(stderr, "sk-sandbox: cannot resolve output path\n");
		sandbox_shutdown(&s);
		return 1;
	}
	if (sandbox_drive_preview(&s) != 0) {
		sandbox_shutdown(&s);
		return 1;
	}
	rc = sandbox_paint_and_capture(&s, out_path);
	sandbox_shutdown(&s);
	return rc == 0 ? 0 : 1;
}
