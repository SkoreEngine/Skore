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
#include "editor_api.h"
#include "editor_gpu.h"
#include "editor_shell.h"
#include "editor_ui_host.h"
#include "logger.h"
#include "main_windows.h"
#include "platform_window.h"
#include "project.h"
#include "repository.h"
#include "resource_assets_types.h"
#include "ui.h"

#include "skore_test_font_ttf.h"
#include "windows/console_window.h"
#include "windows/debugger_window.h"
#include "windows/entity_tree_window.h"
#include "windows/history_window.h"
#include "windows/packages_window.h"
#include "windows/project_browser_window.h"
#include "windows/properties_window.h"
#include "windows/resource_debugger_window.h"
#include "windows/scene_view_window.h"
#include "windows/settings_window.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const_chr_t argv0) {
	fprintf(stderr,
			"Usage:\n"
			"  %s <package_path> [--name <package_name>] [--import <path>]...\n"
			"  %s --ui-migration\n"
			"  %s --shell [<package_path>] [--name <package_name>]\n"
			"\n"
			"  package_path   Directory that contains Assets/ (shell mode attaches it\n"
			"                 to the Project Browser; mock data without one)\n"
			"  --name         Package name for path ids (default: Game)\n"
			"  --import       Import a source file or directory via core importers\n"
			"  --ui-migration Dual-stack editor UI demo (sk-ui Console + ImGui shell)\n"
			"  --shell        APX-366 v2 editor shell: frame + menu bar + toolbar +\n"
			"                 dock host (window open/close/focus via the registries)\n"
			"\n"
			"Asset handlers/importers come from core (add_impl registration).\n"
			"No editor-side ResourceAssetHandler hierarchy; no thumbnails.\n",
			argv0 != NULL ? argv0 : "sk-editor", argv0 != NULL ? argv0 : "sk-editor", argv0 != NULL ? argv0 : "sk-editor");
}

static i32 count_root_children(sk_editor_project_t* project, sk_app_context_t* app, const sk_app_api_t* app_api, const sk_editor_api_t* editor) {
	sk_repository_t* repository = editor->project_repository(project);
	sk_rid_t root = editor->project_root_directory(project);
	if (repository == NULL || root.id == 0u) {
		return -1;
	}
	const sk_repository_api_t* repo = app_api->repository_api(app);
	sk_resource_object_t view = repo->read(repository, root);
	u32 count = 0u;
	(void)repo->get_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
	return (i32)count;
}

static void editor_load_ui_fonts(const sk_ui_api_t* ui, sk_ui_font_system_t** out_fonts, sk_ui_font_t** out_font) {
	sk_ui_font_system_t* fonts = ui->font_system_create(NULL);
	sk_ui_font_t* font = NULL;
	if (fonts != NULL) {
		font = ui->font_load_memory(fonts, skore_test_font_ttf, (u32)sizeof(skore_test_font_ttf));
		if (font != NULL) {
			(void)ui->font_msdf_bake(font);
		}
	}
	*out_fonts = fonts;
	*out_font = font;
}

static void editor_destroy_ui_fonts(const sk_ui_api_t* ui, sk_ui_font_system_t* fonts) {
	if (ui != NULL && fonts != NULL) {
		ui->font_system_destroy(fonts);
	}
}

static i32 run_ui_migration(sk_app_context_t* app, const sk_app_api_t* app_api, const sk_editor_api_t* editor, sk_logger_t* log) {
	const sk_logger_api_t* logger_api = app_api->logger_api(app);
	const sk_platform_window_api_t* win_api;
	const sk_ui_api_t* ui;
	sk_window_t window;
	sk_editor_ui_host_t* host;
	sk_editor_gpu_t* gpu;
	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;
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

	host = editor->ui_host_create(ui, logger_api, app_api->logger_context(app));
	if (host == NULL) {
		sk_log_error(logger_api, log, "editor UI host create failed");
		win_api->destroy_window(window);
		return 1;
	}

	editor_load_ui_fonts(ui, &fonts, &font);
	editor->ui_host_set_fonts(host, fonts, font);

	gpu = sk_editor_gpu_create(app, app_api, ui, win_api, window, logger_api, log);
	if (gpu == NULL) {
		sk_log_warn(logger_api, log, "GPU present unavailable — CPU UI still runs (window stays blank)");
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
		(void)editor->ui_host_frame(host, (f32)logical.width, (f32)logical.height, sx, sy);
		if (gpu != NULL) {
			(void)sk_editor_gpu_present(gpu, editor->ui_host_context(host), fonts, win_api, window);
		}

		if ((frames++ % 120u) == 0u) {
			const sk_ui_draw_list_t* dl = editor->ui_host_sk_ui_draw_list(host);
			u32 imgui_n = 0u;
			(void)editor->ui_host_imgui_draw_items(host, &imgui_n);
			sk_log_info(logger_api, log, "frame %u: sk-ui verts=%u cmds=%u; imgui items=%u; want_mouse=%d gpu=%d", frames, dl != NULL ? dl->vertex_count : 0u,
						dl != NULL ? dl->command_count : 0u, imgui_n, editor->ui_host_want_capture_mouse(host), gpu != NULL ? 1 : 0);
		}

		if (win_api->window_should_close(window)) {
			app_api->request_shutdown(app);
		}
	}

	sk_editor_gpu_destroy(gpu);
	editor->ui_host_destroy(host);
	editor_destroy_ui_fonts(ui, fonts);
	win_api->destroy_window(window);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  APX-366 shell mode                                                */
/* ------------------------------------------------------------------ */

typedef struct shell_host_t {
	sk_editor_shell_t* shell;
	const sk_platform_window_api_t* win_api;
	sk_window_t window;
	sk_editor_project_t* project; /* optional attached project (APX-369) */
	f32 pointer_x;
	f32 pointer_y;
	i32 pointer_down;
} shell_host_t;

/* Platform key/mods values match sk-ui (shared ASCII + named 1000.. range), so
 * the shell receives them unchanged (same pass-through as the player host). */
static void shell_host_on_key(sk_window_t window, i32 key, i32 down, i32 repeat, u32 mods, void_ptr_t user_data) {
	shell_host_t* host = (shell_host_t*)user_data;
	(void)window;
	(void)repeat;
	if (host != NULL) {
		sk_editor_shell_key(host->shell, key, down != 0 ? 1 : 0, mods);
	}
}

static void shell_host_on_char(sk_window_t window, const_chr_t utf8, void_ptr_t user_data) {
	shell_host_t* host = (shell_host_t*)user_data;
	(void)window;
	if (host != NULL) {
		sk_editor_shell_text(host->shell, utf8);
	}
}

static void shell_host_on_scroll(sk_window_t window, f32 offset_x, f32 offset_y, void_ptr_t user_data) {
	shell_host_t* host = (shell_host_t*)user_data;
	(void)window;
	if (host != NULL) {
		sk_editor_shell_scroll(host->shell, offset_x, offset_y);
	}
}

/* Polled mouse -> shell pointer (move/press/release edges, player-style). */
static void shell_host_dispatch_mouse(shell_host_t* host, const sk_platform_window_api_t* win_api, sk_window_t window) {
	f32 x = 0.0f;
	f32 y = 0.0f;
	i32 down;
	i32 moved;
	i32 pressed;
	i32 released;

	if (win_api->get_cursor_pos == NULL || win_api->get_mouse_button == NULL) {
		return;
	}
	win_api->get_cursor_pos(window, &x, &y);
	down = win_api->get_mouse_button(window, SK_MOUSE_BUTTON_LEFT);
	moved = (x > host->pointer_x + 1.0e-3f || x < host->pointer_x - 1.0e-3f || y > host->pointer_y + 1.0e-3f || y < host->pointer_y - 1.0e-3f) ? 1 : 0;
	pressed = (down != 0 && host->pointer_down == 0) ? 1 : 0;
	released = (down == 0 && host->pointer_down != 0) ? 1 : 0;

	if (moved != 0) {
		sk_editor_shell_pointer(host->shell, x, y, -1, 0);
	}
	if (pressed != 0) {
		sk_editor_shell_pointer(host->shell, x, y, SK_MOUSE_BUTTON_LEFT, 1);
	}
	if (released != 0) {
		sk_editor_shell_pointer(host->shell, x, y, SK_MOUSE_BUTTON_LEFT, 0);
	}
	host->pointer_x = x;
	host->pointer_y = y;
	host->pointer_down = down;
}

static i32 run_shell_mode(sk_app_context_t* app, const sk_app_api_t* app_api, sk_logger_t* log, const_chr_t package_path, const_chr_t package_name) {
	const sk_logger_api_t* logger_api = app_api->logger_api(app);
	const sk_platform_window_api_t* win_api;
	const sk_ui_api_t* ui;
	shell_host_t host;
	sk_window_t window;
	sk_editor_gpu_t* gpu;
	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;
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

	/* Real window impls first so their registered ops tables / impls win
	 * window_open over the APX-330 scaffolds (docs/editor/window-table-pattern.md);
	 * the 14 scaffolds follow. */
	sk_editor_project_browser_register(app, app_api);
	sk_editor_console_register(app, app_api);
	sk_editor_entity_tree_register(app, app_api);
	sk_editor_scene_view_register(app, app_api);
	sk_editor_history_register(app, app_api);
	sk_editor_packages_register(app, app_api);
	sk_editor_settings_register(app, app_api);
	sk_editor_debugger_register(app, app_api);
	sk_editor_resource_debugger_register(app, app_api);
	sk_editor_properties_register(app, app_api);
	sk_editor_windows_register_impls(app, app_api);

	if (win_api->init() != 0) {
		sk_log_error(logger_api, log, "platform_window init failed");
		return 1;
	}

	window = win_api->create_window("Skore Editor — v2 shell", 1280u, 720u, (u32)(SK_WINDOW_FLAG_RESIZABLE));
	if (window == NULL) {
		sk_log_error(logger_api, log, "create_window failed");
		return 1;
	}

	memset(&host, 0, sizeof(host));
	host.win_api = win_api;
	host.window = window;
	host.shell = sk_editor_shell_create(app, app_api, ui);
	if (host.shell == NULL) {
		sk_log_error(logger_api, log, "editor shell create failed");
		return 1;
	}

	/* APX-369: when a package path was given, open it and attach it to the
	 * Project Browser so the shell lists a real project (not mock data). */
	host.project = NULL;
	if (package_path != NULL) {
		const sk_editor_api_t* editor = (const sk_editor_api_t*)app_api->get_api(app, SK_EDITOR_API_TYPE_ID);
		host.project = editor != NULL ? editor->project_open(app, app_api, package_name, package_path) : NULL;
		if (host.project == NULL) {
			sk_log_error(logger_api, log, "failed to open project at %s (need Assets/ under package root); Project Browser uses mock data", package_path);
		} else {
			const sk_editor_project_browser_ops_t* ops = sk_editor_project_browser_ops(app, app_api);
			sk_editor_window_t* browser = sk_editor_window_by_type(app, app_api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
			if (ops != NULL && browser != NULL && ops->set_project != NULL) {
				ops->set_project(app, app_api, browser, host.project);
				sk_log_info(logger_api, log, "Project Browser attached to package '%s' at %s", package_name, package_path);
			}
		}
	}
	win_api->set_window_key_callback(window, shell_host_on_key, &host);
	win_api->set_window_char_callback(window, shell_host_on_char, &host);
	win_api->set_window_scroll_callback(window, shell_host_on_scroll, &host);

	editor_load_ui_fonts(ui, &fonts, &font);
	sk_editor_shell_set_fonts(host.shell, fonts, font);

	gpu = sk_editor_gpu_create(app, app_api, ui, win_api, window, logger_api, log);
	if (gpu == NULL) {
		sk_log_warn(logger_api, log, "GPU present unavailable — CPU UI still runs (window stays blank)");
	}

	sk_log_info(logger_api, log, "APX-366 v2 editor shell: frame + menu bar + toolbar + dock host");
	sk_log_info(logger_api, log, "Window menu entries toggle windows through the registered tables");
	sk_log_info(logger_api, log, "Close the window to exit.");

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

		shell_host_dispatch_mouse(&host, win_api, window);
		(void)sk_editor_shell_frame(host.shell, (f32)logical.width, (f32)logical.height, sx, sy);
		if (gpu != NULL) {
			(void)sk_editor_gpu_present(gpu, sk_editor_shell_context(host.shell), fonts, win_api, window);
		}

		if ((frames++ % 120u) == 0u) {
			const sk_ui_draw_list_t* dl = sk_editor_shell_context(host.shell) != NULL ? ui->get_draw_list(sk_editor_shell_context(host.shell)) : NULL;
			u32 open = sk_editor_window_iterate(app, app_api, NULL, 0u);
			sk_log_info(logger_api, log, "frame %u: verts=%u cmds=%u open_windows=%u gpu=%d", frames, dl != NULL ? dl->vertex_count : 0u, dl != NULL ? dl->command_count : 0u, open,
						gpu != NULL ? 1 : 0);
		}

		if (win_api->window_should_close(window)) {
			app_api->request_shutdown(app);
		}
	}

	(void)sk_editor_layout_save(app, app_api);
	if (host.project != NULL) {
		const sk_editor_api_t* editor = (const sk_editor_api_t*)app_api->get_api(app, SK_EDITOR_API_TYPE_ID);
		editor->project_close(host.project);
		host.project = NULL;
	}
	sk_editor_gpu_destroy(gpu);
	sk_editor_shell_destroy(host.shell);
	editor_destroy_ui_fonts(ui, fonts);
	win_api->destroy_window(window);
	return 0;
}

static i32 run_package_mode(sk_app_context_t* app, const sk_app_api_t* app_api, const sk_editor_api_t* editor, sk_logger_t* log, const_chr_t package_path, const_chr_t package_name,
							const_chr_t* import_paths, u32 import_count) {
	const sk_logger_api_t* logger_api = app_api->logger_api(app);
	sk_editor_project_t* project = editor->project_open(app, app_api, package_name, package_path);
	i32 child_count;
	u32 i;

	if (project == NULL) {
		sk_log_error(logger_api, log, "failed to open project at %s (need Assets/ under package root)", package_path);
		return 1;
	}

	sk_log_info(logger_api, log, "opened package '%s' at %s via core resource assets", package_name, package_path);
	sk_log_info(logger_api, log, "handlers/importers registered through sk_resource_asset_builtins_register_impls");

	for (i = 0u; i < import_count; ++i) {
		i32 rc = editor->project_import(project, import_paths[i]);
		if (rc != 0) {
			sk_log_error(logger_api, log, "import failed (%d): %s", rc, import_paths[i]);
			editor->project_close(project);
			return 1;
		}
		sk_log_info(logger_api, log, "imported via core: %s", import_paths[i]);
	}

	child_count = count_root_children(project, app, app_api, editor);
	sk_log_info(logger_api, log, "package root has %d child asset node(s); thumbnails not generated (dropped)", child_count);

	editor->project_close(project);
	return 0;
}

int main(int argc, char* argv[]) {
	const_chr_t package_path = NULL;
	const_chr_t package_name = "Game";
	const_chr_t import_paths[64];
	u32 import_count = 0u;
	i32 ui_migration = 0;
	i32 shell_mode = 0;
	sk_app_context_t* app;
	const sk_app_api_t* app_api;
	const sk_editor_api_t* editor;
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
		if (strcmp(argv[i], "--shell") == 0) {
			shell_mode = 1;
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

	if (!ui_migration && !shell_mode && package_path == NULL) {
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

	/* Editor boot: register the single editor API table, then resolve it via
	 * the app registry (hosts never call the underlying free functions). */
	sk_editor_bind_tables(app, app_api);
	/* APX-329: register the four built-in workspace types (Scene/Graph/Animator/Material). */
	sk_editor_workspace_register_impls(app, app_api);
	/* APX-330: register the 14 main editor windows (titles + dock metadata).
	 * The shell mode registers the Project Browser ops table first so its real
	 * impl wins window_open (see run_shell_mode); other modes keep the pure
	 * scaffold set. */
	if (shell_mode == 0) {
		sk_editor_windows_register_impls(app, app_api);
	}
	editor = (const sk_editor_api_t*)app_api->get_api(app, SK_EDITOR_API_TYPE_ID);

	logger_api = app_api->logger_api(app);
	log = logger_api->create_logger(app_api->logger_context(app), "editor");

	if (ui_migration) {
		rc = run_ui_migration(app, app_api, editor, log);
	} else if (shell_mode) {
		rc = run_shell_mode(app, app_api, log, package_path, package_name);
	} else {
		rc = run_package_mode(app, app_api, editor, log, package_path, package_name, import_paths, import_count);
	}

	logger_api->destroy_logger(app_api->logger_context(app), log);
	sk_app_shutdown(app);
	return rc;
}
