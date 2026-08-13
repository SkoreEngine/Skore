/**
 * @file main.c
 * @brief sk-editor host entry.
 *
 * Modes:
 *   sk-editor <package_path> [--name ...] [--import ...]
 *     Open a package via core repository-assets (existing path).
 *
 *   sk-editor --ui-migration
 *     APX-139 dual-stack proof: sk-ui Console panel + ImGui-path Hierarchy
 *     shell active in the same frame (windowed host loop).
 *
 * Asset handlers/importers come from core; dual UI does not require a package.
 */

#include "app.h"
#include "editor_ui_host.h"
#include "logger.h"
#include "platform_window.h"
#include "project.h"
#include "repository.h"
#include "resource_assets_types.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const_chr_t argv0) {
	fprintf(stderr,
			"Usage:\n"
			"  %s <package_path> [--name <package_name>] [--import <path>]...\n"
			"  %s --ui-migration\n"
			"\n"
			"  package_path   Directory that contains Assets/\n"
			"  --name         Package name for path ids (default: Game)\n"
			"  --import       Import a source file or directory via core importers\n"
			"  --ui-migration Dual-stack editor UI demo (sk-ui Console + ImGui shell)\n"
			"\n"
			"Asset handlers/importers come from core (add_impl registration).\n"
			"No editor-side ResourceAssetHandler hierarchy; no thumbnails.\n",
			argv0 != NULL ? argv0 : "sk-editor", argv0 != NULL ? argv0 : "sk-editor");
}

static i32 count_root_children(sk_editor_project_t* project) {
	sk_repository_t* repository = sk_editor_project_repository(project);
	sk_rid_t root = sk_editor_project_root_directory(project);
	if (repository == NULL || root.id == 0u) {
		return -1;
	}
	const sk_repository_api_t* repo = sk_repository_api();
	sk_resource_object_t view = repo->read(repository, root);
	u32 count = 0u;
	(void)repo->get_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
	return (i32)count;
}

static i32 run_ui_migration(sk_app_context_t* app, const sk_app_api_t* app_api, sk_logger_t* log) {
	const sk_logger_api_t* logger_api = app_api->logger_api(app);
	const sk_platform_window_api_t* win_api;
	const sk_ui_api_t* ui;
	sk_window_t window;
	sk_editor_ui_host_t* host;
	u32 frames = 0u;

	win_api = (const sk_platform_window_api_t*)app_api->get_api(app, SK_PLATFORM_WINDOW_API_TYPE_ID);
	ui = (const sk_ui_api_t*)app_api->get_api(app, SK_UI_API_TYPE_ID);
	if (win_api == NULL) {
		sk_log_error(logger_api, log, "platform_window API missing (need sk-platform-window plugin)");
		return 1;
	}
	if (ui == NULL) {
		sk_log_error(logger_api, log, "ui API missing (need sk-ui plugin)");
		return 1;
	}

	if (win_api->init() != 0) {
		sk_log_error(logger_api, log, "platform_window init failed");
		return 1;
	}

	window = win_api->create_window("Skore Editor — UI migration (Console sk-ui + Hierarchy ImGui path)", 1280u, 720u, (u32)(SK_WINDOW_FLAG_RESIZABLE));
	if (window == NULL) {
		sk_log_error(logger_api, log, "create_window failed");
		return 1;
	}

	host = sk_editor_ui_host_create(ui, logger_api, app_api->logger_context(app));
	if (host == NULL) {
		sk_log_error(logger_api, log, "editor UI host create failed");
		return 1;
	}

	sk_log_info(logger_api, log, "APX-139 dual stack: sk-ui Console + ImGui-path Hierarchy");
	sk_log_info(logger_api, log, "Close the window to exit. Input is arbitrated by panel hit region.");

	while (sk_app_tick(app)) {
		sk_extent_t logical;
		sk_content_scale_t scale;
		f32 sx;
		f32 sy;

		win_api->poll_events();
		logical = win_api->get_window_size(window);
		scale = win_api->get_window_content_scale(window);
		sx = scale.x > 0.0f ? scale.x : 1.0f;
		sy = scale.y > 0.0f ? scale.y : 1.0f;

		/* Note: platform_window now exposes key/char/scroll callbacks and polled
		 * mouse state (see the player host), but the dual editor host is not
		 * wired to them yet. It accepts synthetic input from tests; windowed
		 * mode still runs style/layout/paint for both stacks each frame. */
		(void)sk_editor_ui_host_frame(host, (f32)logical.width, (f32)logical.height, sx, sy);

		if ((frames++ % 120u) == 0u) {
			const sk_ui_draw_list_t* dl = sk_editor_ui_host_sk_ui_draw_list(host);
			u32 imgui_n = 0u;
			(void)sk_editor_ui_host_imgui_draw_items(host, &imgui_n);
			sk_log_info(logger_api, log, "frame %u: sk-ui verts=%u cmds=%u; imgui items=%u; want_mouse=%d", frames, dl != NULL ? dl->vertex_count : 0u,
						dl != NULL ? dl->command_count : 0u, imgui_n, sk_editor_ui_host_want_capture_mouse(host));
		}

		if (win_api->window_should_close(window)) {
			app_api->request_shutdown(app);
		}
	}

	sk_editor_ui_host_destroy(host);
	return 0;
}

static i32 run_package_mode(sk_app_context_t* app, const sk_app_api_t* app_api, sk_logger_t* log, const_chr_t package_path, const_chr_t package_name, const_chr_t* import_paths,
							u32 import_count) {
	const sk_logger_api_t* logger_api = app_api->logger_api(app);
	sk_editor_project_t* project = sk_editor_project_open(app, app_api, package_name, package_path);
	i32 child_count;
	u32 i;

	if (project == NULL) {
		sk_log_error(logger_api, log, "failed to open project at %s (need Assets/ under package root)", package_path);
		return 1;
	}

	sk_log_info(logger_api, log, "opened package '%s' at %s via core resource assets", package_name, package_path);
	sk_log_info(logger_api, log, "handlers/importers registered through sk_resource_asset_builtins_register_impls");

	for (i = 0u; i < import_count; ++i) {
		i32 rc = sk_editor_project_import(project, import_paths[i]);
		if (rc != 0) {
			sk_log_error(logger_api, log, "import failed (%d): %s", rc, import_paths[i]);
			sk_editor_project_close(project);
			return 1;
		}
		sk_log_info(logger_api, log, "imported via core: %s", import_paths[i]);
	}

	child_count = count_root_children(project);
	sk_log_info(logger_api, log, "package root has %d child asset node(s); thumbnails not generated (dropped)", child_count);

	sk_editor_project_close(project);
	return 0;
}

int main(int argc, char* argv[]) {
	const_chr_t package_path = NULL;
	const_chr_t package_name = "Game";
	const_chr_t import_paths[64];
	u32 import_count = 0u;
	i32 ui_migration = 0;
	sk_app_context_t* app;
	const sk_app_api_t* app_api;
	const sk_logger_api_t* logger_api;
	sk_logger_t* log;
	i32 rc;
	int i;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--ui-migration") == 0) {
			ui_migration = 1;
			continue;
		}
		if (strcmp(argv[i], "--name") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "sk-editor: --name requires a value\n");
				return 1;
			}
			package_name = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--import") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "sk-editor: --import requires a path\n");
				return 1;
			}
			if (import_count >= (u32)(sizeof(import_paths) / sizeof(import_paths[0]))) {
				fprintf(stderr, "sk-editor: too many --import paths\n");
				return 1;
			}
			import_paths[import_count++] = argv[++i];
			continue;
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "sk-editor: unknown option %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
		if (package_path != NULL) {
			fprintf(stderr, "sk-editor: unexpected argument %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
		package_path = argv[i];
	}

	if (!ui_migration && package_path == NULL) {
		print_usage(argc > 0 ? argv[0] : "sk-editor");
		return 1;
	}

	sk_app_boot_t boot = sk_app_init(argc, argv);

	app = boot.context;
	if (app == NULL) {
		fprintf(stderr, "sk-editor: sk_app_init failed\n");
		return 1;
	}

	app_api = boot.api;
	logger_api = app_api->logger_api(app);
	log = logger_api->create_logger(app_api->logger_context(app), "editor");

	if (ui_migration) {
		rc = run_ui_migration(app, app_api, log);
	} else {
		rc = run_package_mode(app, app_api, log, package_path, package_name, import_paths, import_count);
	}

	logger_api->destroy_logger(app_api->logger_context(app), log);
	sk_app_shutdown(app);
	return rc;
}
