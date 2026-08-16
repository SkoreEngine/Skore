/* Headless host: render the APX-139 --ui-migration dual stack to a PNG.
 *
 * Boots the editor dual host (sk-ui Console + ImGui-path Hierarchy) on a
 * GPU capture context and writes one frame. Used to verify the Console
 * panel actually paints (windowed --ui-migration used to skip swapchain
 * present entirely).
 *
 * Usage:
 *   sk-sandbox-ui-migration [--out <path>]
 *   (default out: ./ui_migration.png)
 *
 * Not a test; not registered with CTest.
 */

#include "app.h"
#include "dxc_compiler.h"
#include "editor_api.h"
#include "filesystem.h"
#include "path.h"
#include "render_device.h"
#include "ui.h"

#include "skore_test_font_ttf.h"

#include <stdio.h>
#include <string.h>

#define SANDBOX_W 1280u
#define SANDBOX_H 720u
#define SANDBOX_DEFAULT_PNG "ui_migration.png"

typedef struct ui_mig_sandbox_t {
	const sk_app_api_t* app_api;
	sk_app_context_t* app;
	const sk_filesystem_api_t* fs;
	const sk_ui_api_t* ui;
	const sk_render_device_api_t* rd;
	const sk_dxc_compiler_api_t* dxc;
	sk_render_device_t device;
	const sk_editor_api_t* editor;
	sk_editor_ui_host_t* host;
	sk_ui_context_t* ctx;
	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;
	sk_ui_capture_t* capture;
} ui_mig_sandbox_t;

static const_chr_t sandbox_arg_value(int argc, char* argv[], const_chr_t flag) {
	i32 i;
	for (i = 1; i < argc - 1; ++i) {
		if (argv[i] != NULL && strcmp(argv[i], flag) == 0) {
			return argv[i + 1];
		}
	}
	return NULL;
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

static void sandbox_usage(const_chr_t prog) {
	fprintf(stderr,
			"usage: %s [--out <path>]\n"
			"  renders the --ui-migration dual stack (sk-ui Console) to a PNG\n"
			"  --out    PNG file (default: ./%s)\n"
			"  --help   this message\n",
			prog != NULL ? prog : "sk-sandbox-ui-migration", SANDBOX_DEFAULT_PNG);
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

static i32 sandbox_paint_and_capture(ui_mig_sandbox_t* s, const_chr_t png_path) {
	sk_ui_capture_frame_info_t finfo;
	sk_ui_cpu_image_t img;
	const sk_ui_draw_list_t* dl;
	sk_ui_draw_list_t empty;

	if (s->editor->ui_host_frame(s->host, (f32)SANDBOX_W, (f32)SANDBOX_H, 1.0f, 1.0f) != 0) {
		fprintf(stderr, "sk-sandbox: ui_host_frame failed\n");
		return -1;
	}
	dl = s->editor->ui_host_sk_ui_draw_list(s->host);
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
	printf("sk-sandbox: wrote %ux%u '%s' (verts=%u cmds=%u)\n", img.width, img.height, png_path, dl != NULL ? dl->vertex_count : 0u, dl != NULL ? dl->command_count : 0u);
	return 0;
}

static void sandbox_shutdown(ui_mig_sandbox_t* s) {
	if (s->ui != NULL && s->capture != NULL) {
		s->ui->capture_destroy(s->capture);
		s->capture = NULL;
	}
	if (s->editor != NULL && s->host != NULL) {
		s->editor->ui_host_destroy(s->host);
		s->host = NULL;
	}
	s->ctx = NULL;
	if (s->ui != NULL && s->fonts != NULL) {
		s->ui->font_system_destroy(s->fonts);
		s->fonts = NULL;
		s->font = NULL;
	}
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

static i32 sandbox_init(ui_mig_sandbox_t* s, int argc, char* argv[]) {
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

	sk_editor_bind_tables(s->app, s->app_api);
	s->editor = (const sk_editor_api_t*)s->app_api->get_api(s->app, SK_EDITOR_API_TYPE_ID);
	s->host = s->editor->ui_host_create(ui, s->app_api->logger_api(s->app), s->app_api->logger_context(s->app));
	if (s->host == NULL) {
		fprintf(stderr, "sk-sandbox: ui_host_create failed\n");
		return -1;
	}
	s->ui = ui;
	s->ctx = s->editor->ui_host_context(s->host);

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
	cdesc.debug_name = "sk-sandbox-ui-migration";
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
	s->editor->ui_host_set_fonts(s->host, s->fonts, s->font);
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

static i32 sandbox_resolve_path(ui_mig_sandbox_t* s, const_chr_t requested, const_chr_t fallback, char* out, u32 out_cap) {
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
	ui_mig_sandbox_t s;
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
	rc = sandbox_paint_and_capture(&s, out_path);
	sandbox_shutdown(&s);
	return rc == 0 ? 0 : 1;
}
