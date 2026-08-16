/* Headless host: render the v2 editor shell (APX-366) to a PNG.
 *
 * Boots the editor registries + shell on a GPU capture context and writes a
 * frame showing the application frame: menu bar (File/Edit/Tools/Build/
 * Window/Help), workspace switcher (Scene tab + "+"), toolbar (Save All /
 * Undo / Redo / Play / Pause / Stop / Reset Layout), the Scene dockspace
 * (console, debugger, project browser, ... chrome), and — APX-367 — the
 * C++ editor icon set from Content/Images: an icon sample strip pinned
 * under the frame plus the Project Browser's mock content grid, both drawn
 * through sk_editor_icons_get (id → texture handle) and captured with the
 * icon atlas bound as the host image (finfo.images). The Scene dockspace
 * also auto-opens the Entity Tree window (APX-370), which draws its mock
 * scene hierarchy (Demo Scene / Main Camera / Directional Light / Player /
 * Character Mesh rows), and the Scene Viewport window (APX-372), which
 * draws its toolbar chrome + a 512x288 placeholder viewport texture
 * (checkerboard/gradient, generated on the CPU) letterboxed into the dock
 * content — the placeholder view rides the same host images array at slot
 * SK_EDITOR_SCENE_VIEW_TEX_SLOT. Same offscreen capture
 * recipe as the dock preview (docs/widget-lavapipe-png-review.md).
 *
 * Note: an OPEN menu popup is painted in tree order (sk-ui painter's
 * algorithm), i.e. under the later toolbar/dock siblings — input still
 * routes through Clay's floating z-index, but the popup render is occluded
 * until the plugin sorts floating nodes last. The capture therefore shows
 * the closed menu bar (see editor_shell.h).
 *
 * Usage:
 *   sk-sandbox-shell [--out <path>]
 *   (default out: ./editor_shell.png)
 *
 * Not a test; not registered with CTest.
 */

#include "app.h"
#include "dxc_compiler.h"
#include "editor_api.h"
#include "editor_icons.h"
#include "editor_shell.h"
#include "filesystem.h"
#include "main_windows.h"
#include "path.h"
#include "render_device.h"
#include "ui.h"
#include "windows/console_window.h"
#include "windows/entity_tree_window.h"
#include "windows/project_browser_window.h"
#include "windows/scene_view_window.h"

#include "skore_test_font_ttf.h"

#include <stdio.h>
#include <string.h>

#define SANDBOX_W 1280u
#define SANDBOX_H 720u
#define SANDBOX_DEFAULT_PNG "editor_shell.png"

typedef struct shell_sandbox_t {
	const sk_app_api_t* app_api;
	sk_app_context_t* app;
	const sk_filesystem_api_t* fs;
	const sk_ui_api_t* ui;
	const sk_render_device_api_t* rd;
	const sk_dxc_compiler_api_t* dxc;
	sk_render_device_t device;
	sk_editor_shell_t* shell;
	sk_ui_context_t* ctx;
	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;
	sk_ui_capture_t* capture;
	sk_editor_icons_t* icons;
	/* APX-372: Scene View placeholder texture + combined host image array
	 * (zero slot + icon atlas + placeholder view at TEX_SLOT). */
	sk_editor_scene_view_tex_registry_t* sv_tex;
	sk_texture_view_t images[SK_EDITOR_ICON_COUNT + 2u];
	u32 image_count;
} shell_sandbox_t;

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
			"usage: %s [--out <path>]\n"
			"  renders the v2 editor shell frame (menu bar, toolbar,\n"
			"  workspace switcher, Scene dockspace) to a PNG\n"
			"  --out    PNG file (default: ./%s)\n"
			"  --help   this message\n",
			prog != NULL ? prog : "sk-sandbox-shell", SANDBOX_DEFAULT_PNG);
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

static i32 sandbox_drive_shell(shell_sandbox_t* s) {
	sk_ui_node_t window_menu;
	u32 i;
	for (i = 0u; i < 3u; ++i) {
		if (sk_editor_shell_frame(s->shell, (f32)SANDBOX_W, (f32)SANDBOX_H, 1.0f, 1.0f) != 0) {
			fprintf(stderr, "sk-sandbox: shell frame %u failed\n", i);
			return -1;
		}
	}
	window_menu = sk_editor_shell_find_menu(s->shell, "shell.menu.window");
	if (!sk_ui_node_is_valid(window_menu)) {
		fprintf(stderr, "sk-sandbox: Window menu missing\n");
		return -1;
	}

	/* The Window menu exists (ids verified by tests); an open popup is painted
	 * under later tree siblings (plugin painter's algorithm), so capture the
	 * closed bar. */
	if (!sk_ui_node_is_valid(window_menu)) {
		fprintf(stderr, "sk-sandbox: Window menu missing\n");
		return -1;
	}

	/* APX-370: the Entity Tree window is open in the Scene workspace and its
	 * mock scene tree lists the seeded entities (verifies the window opens
	 * against a scene). */
	{
		sk_editor_window_t* tree_window = sk_editor_window_by_type(s->app, s->app_api, SK_EDITOR_WINDOW_ENTITY_TREE);
		const sk_editor_entity_tree_ops_t* et_ops = sk_editor_entity_tree_ops(s->app, s->app_api);
		if (tree_window == NULL || et_ops == NULL) {
			fprintf(stderr, "sk-sandbox: Entity Tree window not open\n");
			return -1;
		}
		if (et_ops->entity_count(tree_window) < 5u || strcmp(et_ops->entity_name_at(tree_window, 0u), "Demo Scene") != 0) {
			fprintf(stderr, "sk-sandbox: Entity Tree mock scene missing\n");
			return -1;
		}
	}

	/* APX-372: the Scene Viewport window is open, its toolbar chrome exists
	 * and its placeholder image node is laid out inside the dock content
	 * (aspect-handled: image ratio matches the 512x288 placeholder). */
	{
		sk_editor_window_t* sv_window = sk_editor_window_by_type(s->app, s->app_api, SK_EDITOR_WINDOW_SCENE_VIEW);
		const sk_editor_scene_view_ops_t* sv_ops = sk_editor_scene_view_ops(s->app, s->app_api);
		sk_ui_node_t image = SK_UI_NODE_INVALID;
		sk_ui_node_t tool = SK_UI_NODE_INVALID;
		sk_ui_style_props_t style;
		f32 iw = 0.0f;
		f32 ih = 0.0f;
		f32 ratio = 0.0f;
		if (sv_window == NULL || sv_ops == NULL) {
			fprintf(stderr, "sk-sandbox: Scene Viewport window not open\n");
			return -1;
		}
		image = s->ui->find_by_id(s->ctx, "sv.viewport.image");
		tool = s->ui->find_by_id(s->ctx, "sv.tool.rot");
		if (!sk_ui_node_is_valid(image) || !sk_ui_node_is_valid(tool)) {
			fprintf(stderr, "sk-sandbox: Scene Viewport chrome missing\n");
			return -1;
		}
		memset(&style, 0, sizeof(style));
		if (s->ui->node_get_inline_style(s->ctx, image, &style) == 0) {
			iw = style.layout.width.value;
			ih = style.layout.height.value;
		}
		if (iw > 1.0f && ih > 1.0f) {
			ratio = iw / ih;
		}
		if (ratio < 1.5f || ratio > 1.9f) {
			fprintf(stderr, "sk-sandbox: Scene Viewport image ratio %.3f not 16:9\n", (double)ratio);
			return -1;
		}
	}
	return 0;
}

/* APX-367 icon sample: one tile per icon id, drawn via the registry lookup
 * (sk_editor_icons_get → widget_image_rect) into the shell frame, so the
 * capture proves the Content/Images set renders end-to-end. The Project
 * Browser window draws its own folder/file tiles through the same lookup. */
static i32 sandbox_build_icon_strip(shell_sandbox_t* s) {
	const sk_ui_api_t* ui = s->ui;
	sk_ui_context_t* ctx = s->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t strip;
	sk_ui_node_t title;
	sk_ui_style_props_t p;
	sk_editor_icon_id_t id;

	s->icons = sk_editor_icons_create(s->rd, s->device);
	if (s->icons == NULL) {
		fprintf(stderr, "sk-sandbox: editor_icons_create failed (decode/atlas/upload)\n");
		return -1;
	}
	/* Register so windows resolve the lookup (Project Browser content grid). */
	sk_editor_icons_register(s->app, s->app_api, s->icons);

	strip = ui->widget_view(ctx, root, "sandbox.icon_strip");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_WIDTH |
			 SK_UI_SP_BORDER_COLOR | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 16.0f;
	p.layout.padding.left = 10.0f;
	p.layout.padding.right = 10.0f;
	p.layout.padding.top = 8.0f;
	p.layout.padding.bottom = 8.0f;
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(0.0f);
	p.layout.top = sk_ui_pt((f32)SANDBOX_H - 80.0f); /* pinned above the overflowing dock host */
	p.background_color = sk_ui_rgba(0.09f, 0.10f, 0.13f, 1.0f);
	p.border_color = sk_ui_rgba(0.17f, 0.18f, 0.22f, 1.0f);
	(void)ui->node_set_inline_style(ctx, strip, &p);

	title = ui->widget_label(ctx, strip, "Editor icons (Content/Images):", "sandbox.icon_strip.title");
	(void)title;
	for (id = (sk_editor_icon_id_t)0; id < SK_EDITOR_ICON_COUNT; ++id) {
		const sk_editor_icon_t* ic = sk_editor_icons_get(s->icons, id);
		char idbuf[48];
		sk_ui_node_t tile;
		sk_ui_node_t icon_node;
		sk_ui_node_t label;

		if (ic == NULL) {
			continue;
		}
		(void)snprintf(idbuf, sizeof(idbuf), "sandbox.icon.tile.%u", (u32)id);
		tile = ui->widget_view(ctx, strip, idbuf);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_ROW_GAP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
		p.layout.flex_direction = SK_UI_FLEX_COLUMN;
		p.layout.align_items = SK_UI_ALIGN_CENTER;
		p.layout.row_gap = 2.0f;
		p.layout.width = sk_ui_pt(64.0f);
		p.layout.height = sk_ui_pt(64.0f);
		(void)ui->node_set_inline_style(ctx, tile, &p);

		(void)snprintf(idbuf, sizeof(idbuf), "sandbox.icon.img.%u", (u32)id);
		icon_node = ui->widget_image_rect(ctx, tile, (i32)ic->view_index, 40.0f, 40.0f, ic->uv0x, ic->uv0y, ic->uv1x, ic->uv1y, NULL, NULL, idbuf);
		(void)icon_node;
		(void)snprintf(idbuf, sizeof(idbuf), "sandbox.icon.label.%u", (u32)id);
		label = ui->widget_label(ctx, tile, ic->name, idbuf);
		(void)label;
	}
	return 0;
}

static i32 sandbox_paint_and_capture(shell_sandbox_t* s, const_chr_t png_path) {
	sk_ui_paint_params_t paint;
	sk_ui_capture_frame_info_t finfo;
	sk_ui_cpu_image_t img;
	const sk_ui_draw_list_t* dl;
	sk_ui_draw_list_t empty;

	/* The shell frame painted with the attached fonts (set_fonts); paint once
	 * more right before capture so the final draw list is current. */
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
	/* APX-367: bind the icon atlas as the host image so IMAGE draw commands
	 * (icon tiles) sample real pixels instead of the white fallback.
	 * APX-372: the Scene View placeholder texture shares the array at slot
	 * SK_EDITOR_SCENE_VIEW_TEX_SLOT (built in sandbox_init). */
	if (s->image_count > 0u) {
		finfo.images.views = s->images;
		finfo.images.count = s->image_count;
	} else if (s->icons != NULL) {
		u32 view_count = 0u;
		finfo.images.views = sk_editor_icons_views(s->icons, &view_count);
		finfo.images.count = view_count;
	}
	memset(&img, 0, sizeof(img));
	if (s->ui->capture_frame(s->capture, &finfo, &img) != 0) {
		fprintf(stderr, "sk-sandbox: capture_frame failed\n");
		return -1;
	}
	if (s->ui->cpu_image_write_png(&img, s->fs, png_path) != 0) {
		fprintf(stderr, "sk-sandbox: cpu_image_write_png failed for '%s'\n", png_path);
		return -1;
	}
	printf("sk-sandbox: wrote %ux%u '%s'\n", img.width, img.height, png_path);
	return 0;
}

static void sandbox_shutdown(shell_sandbox_t* s) {
	/* The shell owns the ui context + ui->init/shutdown; release everything
	 * that depends on the live ui (capture/fonts/icons/texture) before
	 * destroying it. */
	if (s->sv_tex != NULL) {
		sk_editor_scene_view_texture_destroy(s->sv_tex);
		s->sv_tex = NULL;
	}
	if (s->icons != NULL) {
		sk_editor_icons_destroy(s->icons);
		s->icons = NULL;
	}
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
	if (s->shell != NULL) {
		sk_editor_shell_destroy(s->shell);
		s->shell = NULL;
	}
	s->ctx = NULL;
	if (s->rd != NULL && sk_render_device_t_is_valid(s->device)) {
		s->rd->destroy(s->device);
		s->device = sk_render_device_t_zero();
	}
	if (s->dxc != NULL) {
		s->dxc->shutdown();
		s->dxc = NULL;
	}
	s->ui = NULL;
	if (s->app != NULL) {
		sk_app_shutdown(s->app);
		s->app = NULL;
	}
}

static i32 sandbox_init(shell_sandbox_t* s, int argc, char* argv[]) {
	sk_app_boot_t boot;
	sk_adapter_t adapter;
	sk_ui_capture_desc_t cdesc;
	const sk_ui_api_t* ui;

	memset(s, 0, sizeof(*s));
	boot = sk_app_init(argc, argv);
	s->app = boot.context;
	s->app_api = boot.api;
	if (s->app == NULL) {
		fprintf(stderr, "sk-sandbox: sk_app_init failed\n");
		return -1;
	}

	s->fs = s->app_api->filesystem_api(s->app);
	ui = (const sk_ui_api_t*)s->app_api->get_api(s->app, SK_UI_API_TYPE_ID);
	s->rd = (const sk_render_device_api_t*)s->app_api->get_api(s->app, SK_RENDER_DEVICE_API_TYPE_ID);
	s->dxc = (const sk_dxc_compiler_api_t*)s->app_api->get_api(s->app, SK_DXC_COMPILER_API_TYPE_ID);
	if (ui == NULL || s->rd == NULL || s->dxc == NULL) {
		fprintf(stderr, "sk-sandbox: missing plugin API (need sk-ui, sk-vulkan-render-device, sk-dxc-compiler)\n");
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

	/* Editor boot + shell (the shell owns its ui context; ui->init runs here). */
	sk_editor_bind_tables(s->app, s->app_api);
	sk_editor_workspace_register_impls(s->app, s->app_api);
	/* Project Browser + Console + Entity Tree + Scene View first so their
	 * real impls win window_open (pattern doc); the scaffolds follow. The
	 * Scene dockspace auto-opens the Entity Tree + Scene Viewport (Scene-only
	 * mask); the Scene Viewport draws its placeholder texture in the frame. */
	sk_editor_project_browser_register(s->app, s->app_api);
	sk_editor_console_register(s->app, s->app_api);
	sk_editor_entity_tree_register(s->app, s->app_api);
	sk_editor_scene_view_register(s->app, s->app_api);
	sk_editor_windows_register_impls(s->app, s->app_api);
	s->shell = sk_editor_shell_create(s->app, s->app_api, ui);
	if (s->shell == NULL) {
		fprintf(stderr, "sk-sandbox: editor shell create failed\n");
		return -1;
	}
	s->ui = ui;
	s->ctx = sk_editor_shell_context(s->shell);

	/* APX-367: load + atlas the C++ editor Content/Images icons and pin an
	 * icon sample strip under the frame (the Project Browser window also
	 * draws folder/file tiles through the same lookup). */
	if (sandbox_build_icon_strip(s) != 0) {
		fprintf(stderr, "sk-sandbox: icon strip build failed\n");
		return -1;
	}

	/* APX-372: create the Scene View placeholder texture and build the
	 * combined host image array the UI renderer samples: slot 0 stays zero
	 * ("no texture"), slots 1..5 are the icon atlas, slot 6 is the
	 * placeholder view (SK_EDITOR_SCENE_VIEW_TEX_SLOT). The Scene Viewport
	 * window resolves the registry and draws its viewport texture. */
	{
		const sk_texture_view_t* icon_views = NULL;
		const sk_texture_view_t* sv_view = NULL;
		u32 icon_count = 0u;
		u32 i;
		s->sv_tex = sk_editor_scene_view_texture_create(s->app, s->app_api, s->rd, s->device);
		if (s->sv_tex == NULL) {
			fprintf(stderr, "sk-sandbox: scene view texture create failed\n");
			return -1;
		}
		sk_editor_scene_view_texture_register(s->app, s->app_api, s->sv_tex);
		if (s->icons != NULL) {
			icon_views = sk_editor_icons_views(s->icons, &icon_count);
		}
		sv_view = sk_editor_scene_view_texture_view(s->sv_tex);
		memset(s->images, 0, sizeof(s->images));
		s->image_count = 0u;
		for (i = 0u; i < icon_count && i < SK_EDITOR_ICON_COUNT + 1u; ++i) {
			s->images[s->image_count++] = icon_views[i];
		}
		if (sv_view != NULL && s->image_count < (u32)(sizeof(s->images) / sizeof(s->images[0]))) {
			s->images[s->image_count++] = *sv_view;
		}
	}

	memset(&cdesc, 0, sizeof(cdesc));
	cdesc.device_api = s->rd;
	cdesc.device = s->device;
	cdesc.dxc = s->dxc;
	cdesc.width = SANDBOX_W;
	cdesc.height = SANDBOX_H;
	cdesc.clear_color.color.r = 0.05f;
	cdesc.clear_color.color.g = 0.06f;
	cdesc.clear_color.color.b = 0.08f;
	cdesc.clear_color.color.a = 1.0f;
	cdesc.debug_name = "sk-sandbox-shell";
	s->capture = s->ui->capture_create(&cdesc);
	if (s->capture == NULL) {
		fprintf(stderr, "sk-sandbox: capture_create failed\n");
		return -1;
	}

	s->fonts = s->ui->font_system_create(NULL);
	if (s->fonts != NULL) {
		s->font = s->ui->font_load_memory(s->fonts, skore_test_font_ttf, (u32)sizeof(skore_test_font_ttf));
		if (s->font != NULL) {
			(void)s->ui->font_msdf_bake(s->font);
		}
	}
	/* Text glyphs emit only when the shell frame paints with a font. */
	sk_editor_shell_set_fonts(s->shell, s->fonts, s->font);
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

static i32 sandbox_resolve_path(shell_sandbox_t* s, const_chr_t requested, const_chr_t fallback, char* out, u32 out_cap) {
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

static i32 sandbox_has_arg(int argc, char* argv[], const_chr_t flag) {
	i32 i;
	for (i = 1; i < argc; ++i) {
		if (argv[i] != NULL && strcmp(argv[i], flag) == 0) {
			return 1;
		}
	}
	return 0;
}

int main(int argc, char* argv[]) {
	shell_sandbox_t s;
	char out_path[SK_FS_PATH_MAX];
	const_chr_t out_arg;
	i32 rc;

	if (sandbox_has_arg(argc, argv, "--help") != 0 || sandbox_has_arg(argc, argv, "-h") != 0) {
		sandbox_usage(argv[0]);
		return 0;
	}
	out_arg = sandbox_arg_value(argc, argv, "--out");
	if (sandbox_init(&s, argc, argv) != 0) {
		sandbox_shutdown(&s);
		return 1;
	}
	if (sandbox_resolve_path(&s, out_arg, SANDBOX_DEFAULT_PNG, out_path, (u32)sizeof(out_path)) != 0) {
		fprintf(stderr, "sk-sandbox: cannot resolve output path\n");
		sandbox_shutdown(&s);
		return 1;
	}
	if (sandbox_drive_shell(&s) != 0) {
		sandbox_shutdown(&s);
		return 1;
	}
	rc = sandbox_paint_and_capture(&s, out_path);
	sandbox_shutdown(&s);
	return rc == 0 ? 0 : 1;
}
