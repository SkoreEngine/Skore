/**
 * @file smoke_tests.c
 * @brief Editor smoke tests (APX-376).
 *
 * Boots the full editor window registry and verifies the invariants that
 * keep the v2 shell usable after every window migration:
 *
 *   1. every window registers and is reachable through its struct table
 *      (enumerate SK_EDITOR_WINDOW_IMPL_TYPE_ID, then open / by_type);
 *   2. every window opens and draws a frame without crashing or requesting
 *      close (shell frame loop with all 14 main windows open);
 *   3. add_impl observer notifications reach subscribers, including through
 *      real publisher windows (the Entity Tree publishes selection / create /
 *      rename / delete and both an external subscriber and the Properties
 *      window's internal observer set receive them);
 *   4. a workspace save/switch/restore round-trip preserves layout — the
 *      shell-level round trip below (capture Scene → switch Graph → switch
 *      back) plus the byte-exact dock JSON compare in
 *      editor_layout_roundtrip_save_switch_restore_compare (editor_layout.c).
 *
 * SK_TESTS-only; editor/CMakeLists.txt GLOBs this file into sk-editor-tests,
 * production targets compile it to nothing.
 */

#ifdef SK_TESTS

#include <string.h>

#include "editor_api.h"
#include "editor_shell.h"
#include "filesystem.h"
#include "main_windows.h"
#include "notify.h"
#include "path.h"
#include "platform.h"
#include "test.h"
#include "ui.h"
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

#define SMOKE_WINDOW_COUNT 14u
#define SMOKE_IMPL_CAP 32u

/* ------------------------------------------------------------------ */
/*  Window table helpers                                               */
/* ------------------------------------------------------------------ */

/* The 14 main editor window classes (main_windows.h). Returns
 * SK_TYPE_ID_ZERO for out-of-range indices. */
static sk_type_id_t smoke_window_type(u32 index) {
	switch (index) {
	case 0u:
		return SK_EDITOR_WINDOW_CONSOLE;
	case 1u:
		return SK_EDITOR_WINDOW_DEBUGGER;
	case 2u:
		return SK_EDITOR_WINDOW_ENTITY_TREE;
	case 3u:
		return SK_EDITOR_WINDOW_HISTORY;
	case 4u:
		return SK_EDITOR_WINDOW_PROPERTIES;
	case 5u:
		return SK_EDITOR_WINDOW_PROJECT_BROWSER;
	case 6u:
		return SK_EDITOR_WINDOW_SCENE_VIEW;
	case 7u:
		return SK_EDITOR_WINDOW_GRAPH_EDITOR;
	case 8u:
		return SK_EDITOR_WINDOW_MATERIAL_GRAPH_EDITOR;
	case 9u:
		return SK_EDITOR_WINDOW_ANIMATOR_GRAPH;
	case 10u:
		return SK_EDITOR_WINDOW_ANIMATOR_TREE_VIEW;
	case 11u:
		return SK_EDITOR_WINDOW_RESOURCE_DEBUGGER;
	case 12u:
		return SK_EDITOR_WINDOW_PACKAGES;
	case 13u:
		return SK_EDITOR_WINDOW_SETTINGS;
	default:
		return SK_TYPE_ID_ZERO;
	}
}

/* Register every window class: the 10 migrated windows first so their real
 * impls win window_open over the APX-330 scaffolds, then the 14 scaffolds
 * (docs/editor/window-table-pattern.md). Idempotent per context. */
static void smoke_register_windows(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	sk_editor_project_browser_register(app_context, app_api);
	sk_editor_console_register(app_context, app_api);
	sk_editor_entity_tree_register(app_context, app_api);
	sk_editor_scene_view_register(app_context, app_api);
	sk_editor_history_register(app_context, app_api);
	sk_editor_packages_register(app_context, app_api);
	sk_editor_settings_register(app_context, app_api);
	sk_editor_debugger_register(app_context, app_api);
	sk_editor_resource_debugger_register(app_context, app_api);
	sk_editor_properties_register(app_context, app_api);
	sk_editor_windows_register_impls(app_context, app_api);
}

/* ------------------------------------------------------------------ */
/*  sk-ui fixture (loads the shared plugin like tests/main.c)          */
/* ------------------------------------------------------------------ */

typedef struct smoke_fixture_t {
	sk_app_boot_t boot;
	const sk_ui_api_t* ui;
	sk_shared_lib_t lib;
} smoke_fixture_t;

static i32 smoke_fixture_start(smoke_fixture_t* fx) {
	const sk_platform_api_t* plat;
	const sk_filesystem_api_t* fs;
	sk_directory_iterator_t it;
	char base[SK_FS_PATH_MAX];
	char plugins_dir[SK_FS_PATH_MAX];
	char full_path[SK_FS_PATH_MAX];
	char name[SK_FS_PATH_MAX];
	i32 found = 0;
	typedef int (*smoke_plugin_entry_fn)(sk_app_context_t* context, const sk_app_api_t* app_api);

	memset(fx, 0, sizeof(*fx));
	fx->boot = sk_app_startup();
	if (fx->boot.context == NULL) {
		return -1;
	}
	plat = fx->boot.api->platform_api(fx->boot.context);
	fs = fx->boot.api->filesystem_api(fx->boot.context);

	if ((fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') && (fs->current_dir(base, (u32)sizeof(base)) != 0 || base[0] == '\0')) {
		return -1;
	}
	if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins_dir, (u32)sizeof(plugins_dir)) < 0) {
		return -1;
	}
	it = fs->open_directory(plugins_dir);
	if (it == NULL) {
		return -1;
	}
	while (fs->next_directory(it, name, (u32)sizeof(name)) == 0) {
		if (strncmp(name, "sk-ui.", 6u) != 0 || sk_is_shared_library_filename(name) == 0) {
			continue;
		}
		if (sk_path_join(sk_str_view_cstr(plugins_dir), sk_str_view_cstr(name), full_path, (u32)sizeof(full_path)) < 0) {
			continue;
		}
		fx->lib = plat->lib_open(full_path);
		if (fx->lib == NULL) {
			continue;
		}
		found = 1;
		break;
	}
	fs->close_directory(it);
	if (found == 0) {
		return -1;
	}
	{
		void_ptr_t raw = plat->lib_symbol(fx->lib, "sk_plugin_entry_point");
		smoke_plugin_entry_fn entry;
		if (raw == NULL) {
			plat->lib_close(fx->lib);
			fx->lib = NULL;
			return -1;
		}
		entry = SK_PTR_TO_FN(smoke_plugin_entry_fn, raw);
		(void)entry(fx->boot.context, fx->boot.api);
	}
	fx->ui = (const sk_ui_api_t*)fx->boot.api->get_api(fx->boot.context, SK_UI_API_TYPE_ID);
	return fx->ui != NULL ? 0 : -1;
}

static void smoke_fixture_stop(smoke_fixture_t* fx) {
	const sk_platform_api_t* plat = NULL;
	if (fx->boot.context != NULL) {
		plat = fx->boot.api->platform_api(fx->boot.context);
		sk_app_shutdown(fx->boot.context);
	}
	if (fx->lib != NULL && plat != NULL) {
		plat->lib_close(fx->lib);
	}
	memset(fx, 0, sizeof(*fx));
}

/* Bind the editor registries + shell on a ui fixture. Isolates the layout
 * persist path so shell_create's layout_init never picks up another test's
 * EditorLayout.json. */
static sk_editor_shell_t* smoke_boot_shell(smoke_fixture_t* fx) {
	const sk_filesystem_api_t* fs;
	char tmp[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	sk_editor_shell_t* shell;

	sk_editor_bind_tables(fx->boot.context, fx->boot.api);
	sk_editor_workspace_register_impls(fx->boot.context, fx->boot.api);
	smoke_register_windows(fx->boot.context, fx->boot.api);

	fs = fx->boot.api->filesystem_api(fx->boot.context);
	if (fs != NULL && fs->temp_folder(tmp, (u32)sizeof(tmp)) == 0) {
		if (sk_path_join(sk_str_view_cstr(tmp), sk_str_view_cstr("skore-apx376-smoke-layout.json"), path, (u32)sizeof(path)) >= 0) {
			if (fs->get_file_status(path) == SK_FILE_STATUS_FILE) {
				(void)fs->remove(path);
			}
			sk_editor_layout_set_path(fx->boot.context, fx->boot.api, path);
		}
	}
	shell = sk_editor_shell_create(fx->boot.context, fx->boot.api, fx->ui);
	return shell;
}

/* ------------------------------------------------------------------ */
/*  Observer scaffolding (notify.h subscribers)                        */
/* ------------------------------------------------------------------ */

typedef struct smoke_obs_t {
	u32 sel_changed;
	u32 entity_sel;
	u32 entity_desel;
	u32 created;
	u32 renamed;
	u32 deleted;
	sk_rid_t last_rid;
	const_chr_t last_name;
} smoke_obs_t;

typedef struct smoke_obs_set_t {
	sk_editor_on_selection_changed_t sel;
	sk_editor_on_entity_selection_t esel;
	sk_editor_on_entity_deselection_t edesel;
	sk_editor_on_entity_created_t created;
	sk_editor_on_entity_renamed_t renamed;
	sk_editor_on_entity_deleted_t deleted;
} smoke_obs_set_t;

static void smoke_on_sel_changed(void* user) {
	smoke_obs_t* obs = (smoke_obs_t*)user;
	obs->sel_changed++;
}

static void smoke_on_entity_sel(void* user, u32 workspace_id, sk_rid_t rid) {
	smoke_obs_t* obs = (smoke_obs_t*)user;
	(void)workspace_id;
	obs->entity_sel++;
	obs->last_rid = rid;
}

static void smoke_on_entity_desel(void* user, u32 workspace_id, sk_rid_t rid) {
	smoke_obs_t* obs = (smoke_obs_t*)user;
	(void)workspace_id;
	(void)rid;
	obs->entity_desel++;
}

static void smoke_on_created(void* user, u32 workspace_id, sk_rid_t rid) {
	smoke_obs_t* obs = (smoke_obs_t*)user;
	(void)workspace_id;
	obs->created++;
	obs->last_rid = rid;
}

static void smoke_on_renamed(void* user, u32 workspace_id, sk_rid_t rid, const_chr_t name) {
	smoke_obs_t* obs = (smoke_obs_t*)user;
	(void)workspace_id;
	(void)rid;
	obs->renamed++;
	obs->last_name = name;
}

static void smoke_on_deleted(void* user, u32 workspace_id, sk_rid_t rid) {
	smoke_obs_t* obs = (smoke_obs_t*)user;
	(void)workspace_id;
	(void)rid;
	obs->deleted++;
}

static void smoke_obs_register(sk_app_context_t* app_context, const sk_app_api_t* app_api, smoke_obs_t* obs, smoke_obs_set_t* set) {
	memset(obs, 0, sizeof(*obs));
	memset(set, 0, sizeof(*set));

	set->sel.user = obs;
	set->sel.on_selection_changed = smoke_on_sel_changed;
	set->esel.user = obs;
	set->esel.on_entity_selection = smoke_on_entity_sel;
	set->edesel.user = obs;
	set->edesel.on_entity_deselection = smoke_on_entity_desel;
	set->created.user = obs;
	set->created.on_entity_created = smoke_on_created;
	set->renamed.user = obs;
	set->renamed.on_entity_renamed = smoke_on_renamed;
	set->deleted.user = obs;
	set->deleted.on_entity_deleted = smoke_on_deleted;

	app_api->add_impl(app_context, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &set->sel);
	app_api->add_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_SELECTION, &set->esel);
	app_api->add_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_DESELECTION, &set->edesel);
	app_api->add_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_CREATED, &set->created);
	app_api->add_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_RENAMED, &set->renamed);
	app_api->add_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_DELETED, &set->deleted);
}

static void smoke_obs_unregister(sk_app_context_t* app_context, const sk_app_api_t* app_api, smoke_obs_set_t* set) {
	app_api->remove_impl(app_context, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &set->sel);
	app_api->remove_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_SELECTION, &set->esel);
	app_api->remove_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_DESELECTION, &set->edesel);
	app_api->remove_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_CREATED, &set->created);
	app_api->remove_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_RENAMED, &set->renamed);
	app_api->remove_impl(app_context, SK_EDITOR_NOTIFY_ENTITY_DELETED, &set->deleted);
}

/* ------------------------------------------------------------------ */
/*  Smoke tests                                                        */
/* ------------------------------------------------------------------ */

/* 1. Every window registers and is reachable through its struct table. */
SK_TEST(editor_smoke_every_window_registers_and_is_reachable) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const_ptr_t impls[SMOKE_IMPL_CAP];
	sk_type_id_t seen[SMOKE_WINDOW_COUNT];
	u32 n;
	u32 unique;
	u32 i;
	u32 j;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	sk_editor_workspace_register_impls(app, boot.api);
	smoke_register_windows(app, boot.api);

	/* The struct table holds a valid impl row for every window: non-zero
	 * type id, title and dock id (dock ids drive the sk-ui dock chrome). */
	n = boot.api->get_all_impls(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID, impls, SMOKE_IMPL_CAP);
	TEST_ASSERT_TRUE(n >= SMOKE_WINDOW_COUNT);
	for (i = 0u; i < n; ++i) {
		const sk_editor_window_t* impl = (const sk_editor_window_t*)impls[i];
		TEST_ASSERT_TRUE(!SK_TYPE_ID_EQ(impl->type_id, SK_TYPE_ID_ZERO));
		TEST_ASSERT_NOT_NULL(impl->title);
		TEST_ASSERT_NOT_NULL(impl->dock_id);
	}

	/* The table covers exactly the 14 main window classes. */
	unique = 0u;
	memset(seen, 0, sizeof(seen));
	for (i = 0u; i < n; ++i) {
		const sk_editor_window_t* impl = (const sk_editor_window_t*)impls[i];
		i32 already = 0;
		for (j = 0u; j < unique; ++j) {
			if (SK_TYPE_ID_EQ(seen[j], impl->type_id)) {
				already = 1;
				break;
			}
		}
		if (already == 0 && unique < SMOKE_WINDOW_COUNT) {
			seen[unique] = impl->type_id;
			unique++;
		}
	}
	TEST_ASSERT_EQUAL_UINT(SMOKE_WINDOW_COUNT, unique);
	for (i = 0u; i < SMOKE_WINDOW_COUNT; ++i) {
		sk_type_id_t type = smoke_window_type(i);
		u32 found = 0u;
		for (j = 0u; j < unique; ++j) {
			if (SK_TYPE_ID_EQ(seen[j], type)) {
				found++;
			}
		}
		TEST_ASSERT_EQUAL_UINT(1u, found);
	}

	/* Reachability: open by type id returns an instance (init runs), by_type
	 * resolves the same instance, reopen is idempotent (one per class). */
	for (i = 0u; i < SMOKE_WINDOW_COUNT; ++i) {
		sk_type_id_t type = smoke_window_type(i);
		sk_editor_window_t* window = sk_editor_window_open(app, boot.api, type);
		TEST_ASSERT_NOT_NULL(window);
		TEST_ASSERT_EQUAL_PTR(window, sk_editor_window_by_type(app, boot.api, type));
		TEST_ASSERT_EQUAL_PTR(window, sk_editor_window_open(app, boot.api, type));
	}
	TEST_ASSERT_EQUAL_UINT(SMOKE_WINDOW_COUNT, sk_editor_window_iterate(app, boot.api, NULL, 0u));

	/* Close each open window; the registry drains. */
	for (i = 0u; i < SMOKE_WINDOW_COUNT; ++i) {
		sk_editor_window_t* window = sk_editor_window_by_type(app, boot.api, smoke_window_type(i));
		if (window != NULL) {
			sk_editor_window_close(app, boot.api, window);
		}
	}
	TEST_ASSERT_EQUAL_UINT(0u, sk_editor_window_iterate(app, boot.api, NULL, 0u));

	sk_app_shutdown(app);
}

/* 2. Every window opens and draws a frame without crashing. */
SK_TEST(editor_smoke_every_window_opens_and_draws) {
	smoke_fixture_t fx;
	sk_editor_shell_t* shell;
	const sk_ui_api_t* ui;
	const sk_ui_draw_list_t* dl;
	sk_app_context_t* app;
	const sk_app_api_t* api;
	u32 frame;
	u32 i;

	TEST_ASSERT_EQUAL_INT(0, smoke_fixture_start(&fx));
	shell = smoke_boot_shell(&fx);
	TEST_ASSERT_NOT_NULL(shell);
	ui = fx.ui;
	app = fx.boot.context;
	api = fx.boot.api;

	/* Open every main window through the registry (on-demand windows like
	 * Resource Debugger / Packages / Settings included). */
	for (i = 0u; i < SMOKE_WINDOW_COUNT; ++i) {
		sk_type_id_t type = smoke_window_type(i);
		sk_editor_window_t* window = sk_editor_shell_open_window(shell, type);
		TEST_ASSERT_NOT_NULL(window);
		TEST_ASSERT_EQUAL_PTR(window, sk_editor_window_by_type(app, api, type));
	}
	TEST_ASSERT_EQUAL_UINT(SMOKE_WINDOW_COUNT, sk_editor_window_iterate(app, api, NULL, 0u));

	/* Several full shell frames: every window draws without crashing and
	 * none requests close (open flag stays set). */
	for (frame = 0u; frame < 3u; ++frame) {
		TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
		for (i = 0u; i < SMOKE_WINDOW_COUNT; ++i) {
			TEST_ASSERT_NOT_NULL(sk_editor_window_by_type(app, api, smoke_window_type(i)));
		}
	}

	/* The frame pipeline produced draw output. */
	dl = ui->get_draw_list(sk_editor_shell_context(shell));
	TEST_ASSERT_NOT_NULL(dl);
	TEST_ASSERT_TRUE(dl->command_count > 0u || dl->vertex_count > 0u);

	/* Close everything through the registry; the open set drains. */
	for (i = 0u; i < SMOKE_WINDOW_COUNT; ++i) {
		sk_editor_window_t* window = sk_editor_window_by_type(app, api, smoke_window_type(i));
		if (window != NULL) {
			sk_editor_window_close(app, api, window);
		}
	}
	TEST_ASSERT_EQUAL_UINT(0u, sk_editor_window_iterate(app, api, NULL, 0u));

	sk_editor_shell_destroy(shell);
	smoke_fixture_stop(&fx);
}

/* 3. Observer notifications reach subscribers (real publisher windows). */
SK_TEST(editor_smoke_observer_notifications_reach_subscribers) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const sk_editor_entity_tree_ops_t* et_ops;
	const sk_editor_properties_ops_t* props_ops;
	sk_editor_window_t* et_window;
	sk_editor_window_t* props_window;
	smoke_obs_t obs;
	smoke_obs_set_t obs_set;
	sk_rid_t player;
	sk_rid_t created;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	smoke_register_windows(app, boot.api);
	et_ops = sk_editor_entity_tree_ops(app, boot.api);
	props_ops = sk_editor_properties_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(et_ops);
	TEST_ASSERT_NOT_NULL(props_ops);

	/* Two live subscribers: the Properties window (its init registers the
	 * internal observer set) and the smoke observer registered below. */
	props_window = props_ops->open(app, boot.api);
	et_window = et_ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(props_window);
	TEST_ASSERT_NOT_NULL(et_window);
	smoke_obs_register(app, boot.api, &obs, &obs_set);

	/* Mock scene: Player at tree index 3 (see entity tree ops tests). */
	player = et_ops->entity_rid_at(et_window, 3u);
	TEST_ASSERT_TRUE(player.id != 0u);

	/* select_entity publishes SELECTION_CHANGED + ENTITY_SELECTION: the
	 * smoke subscriber and the Properties window both receive the same
	 * payload. */
	et_ops->select_entity(app, boot.api, et_window, player, 1, NULL);
	TEST_ASSERT_EQUAL_UINT(1u, obs.sel_changed);
	TEST_ASSERT_EQUAL_UINT(1u, obs.entity_sel);
	TEST_ASSERT_EQUAL_UINT64(player.id, obs.last_rid.id);
	TEST_ASSERT_EQUAL_UINT64(player.id, props_ops->get_selected_rid(props_window).id);

	/* clear_selection publishes ENTITY_DESELECTION. */
	et_ops->clear_selection(app, boot.api, et_window, NULL);
	TEST_ASSERT_TRUE(obs.entity_desel >= 1u);

	/* create_entity publishes ENTITY_CREATED (and re-selects). */
	created = et_ops->create_entity(app, boot.api, et_window, player, NULL);
	TEST_ASSERT_TRUE(created.id != 0u);
	TEST_ASSERT_EQUAL_UINT(1u, obs.created);
	TEST_ASSERT_EQUAL_UINT64(created.id, obs.last_rid.id);
	TEST_ASSERT_EQUAL_UINT64(created.id, props_ops->get_selected_rid(props_window).id);

	/* rename_entity publishes ENTITY_RENAMED with the new name. */
	et_ops->rename_entity(app, boot.api, et_window, player, "Hero");
	TEST_ASSERT_EQUAL_UINT(1u, obs.renamed);
	TEST_ASSERT_EQUAL_STRING("Hero", obs.last_name);

	/* delete_entity publishes ENTITY_DELETED for the subtree. */
	{
		u32 before = et_ops->entity_count(et_window);
		et_ops->select_entity(app, boot.api, et_window, player, 1, NULL);
		et_ops->delete_entity(app, boot.api, et_window, NULL);
		TEST_ASSERT_TRUE(et_ops->entity_count(et_window) < before);
		TEST_ASSERT_TRUE(obs.deleted >= 1u);
	}

	/* Closing the Properties window unbinds its observer set: only the
	 * smoke subscriber remains on ENTITY_SELECTION. */
	sk_editor_window_close(app, boot.api, props_window);
	{
		const_ptr_t impls[8];
		u32 subscribers = boot.api->get_all_impls(app, SK_EDITOR_NOTIFY_ENTITY_SELECTION, impls, 8u);
		TEST_ASSERT_EQUAL_UINT(1u, subscribers);
	}

	sk_editor_window_close(app, boot.api, et_window);
	smoke_obs_unregister(app, boot.api, &obs_set);
	sk_app_shutdown(app);
}

/* 4. Workspace save/switch/restore round-trip preserves layout. */
SK_TEST(editor_smoke_workspace_roundtrip_preserves_layout) {
	smoke_fixture_t fx;
	sk_editor_shell_t* shell;
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* scene_ws;
	sk_editor_workspace_t* graph_ws;
	sk_ui_context_t* ctx;
	char dock_before[65536];
	char dock_after[65536];
	u32 before_len = 0u;
	u32 after_len = 0u;
	sk_app_context_t* app;
	const sk_app_api_t* api;

	TEST_ASSERT_EQUAL_INT(0, smoke_fixture_start(&fx));
	shell = smoke_boot_shell(&fx);
	TEST_ASSERT_NOT_NULL(shell);
	ui = fx.ui;
	app = fx.boot.context;
	api = fx.boot.api;

	/* Undock the console so the captured layout differs from the default
	 * preset; the round-trip must restore the captured tree, not the
	 * preset. */
	scene_ws = sk_editor_workspace_active(app, api);
	TEST_ASSERT_NOT_NULL(scene_ws);
	ctx = sk_editor_workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "sk.editor_window.console"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "sk.editor_window.console"));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_workspace_save_dock_json(scene_ws, dock_before, (u32)sizeof(dock_before), &before_len));
	TEST_ASSERT_TRUE(before_len > 0u);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_workspace_capture(scene_ws));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_layout_save(app, api));

	/* Switch to Graph: its dock model builds on the shared context, the
	 * scene-only windows close, the graph window opens. */
	graph_ws = sk_editor_shell_switch_workspace(shell, SK_EDITOR_WORKSPACE_GRAPH);
	TEST_ASSERT_NOT_NULL(graph_ws);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
	TEST_ASSERT_NOT_NULL(sk_editor_window_by_type(app, api, SK_EDITOR_WINDOW_GRAPH_EDITOR));
	TEST_ASSERT_NULL(sk_editor_window_by_type(app, api, SK_EDITOR_WINDOW_SCENE_VIEW));

	/* Switch back to Scene: the captured layout restores — scene defaults
	 * are back and the undocked console stays undocked. */
	(void)sk_editor_workspace_capture(graph_ws);
	scene_ws = sk_editor_shell_switch_workspace(shell, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
	TEST_ASSERT_NOT_NULL(sk_editor_window_by_type(app, api, SK_EDITOR_WINDOW_SCENE_VIEW));
	TEST_ASSERT_NULL(sk_editor_window_by_type(app, api, SK_EDITOR_WINDOW_GRAPH_EDITOR));
	ctx = sk_editor_workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "sk.editor_window.console"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.scene_view"));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_workspace_save_dock_json(scene_ws, dock_after, (u32)sizeof(dock_after), &after_len));
	TEST_ASSERT_EQUAL_UINT(before_len, after_len);
	TEST_ASSERT_EQUAL_INT(0, memcmp(dock_before, dock_after, (size_t)before_len));

	sk_editor_shell_destroy(shell);
	smoke_fixture_stop(&fx);
}

#endif /* SK_TESTS */
