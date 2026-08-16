/**
 * @file packages_window.c
 * @brief Packages window (APX-374): mock package list on the v2 shell.
 *
 * Port of main-branch Skore::PackagesWindow (migration manifest §3.5): an
 * on-demand floating window (dock position None / workspace mask 0 — never
 * auto-opened) that lists the project's package folders (Name / Path /
 * remove) with an Add Package button. The v2 project model has no
 * `packages:` list yet (C++ persists it in the .skore project file), so the
 * list is **MOCK session data** seeded at open; `Platform::PickFolder` does
 * not exist in v2, so the Add button records a pending flag instead of
 * opening a folder dialog.
 *
 * Because the window is never docked (SK_EDITOR_DOCK_NONE), its chrome is a
 * floating `widget_window` hosted on the shell context root (absolute,
 * centered) — the v2 equivalent of the C++ `ImGuiCenterWindow` on-demand
 * window. The window publishes one `sk_editor_packages_ops_t` table
 * registered with add_impl (window-table-pattern.md §7); the shell's
 * Edit/Packages menu routes through it.
 */

#include "packages_window.h"

#include "allocator.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SK_EDITOR_PACKAGES_STATE_TYPE_ID SK_TYPE_ID("sk.editor.packages.state", 0x7c3d9e0f1a2b4c5dULL, 0x6e8f90a1b2c3d4e5ULL)

/* ------------------------------------------------------------------ */
/*  Internal types                                                    */
/* ------------------------------------------------------------------ */

typedef struct packages_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;

	/* Mock package folder list (session-only; C++ Editor::GetProjectPackages). */
	char paths[SK_EDITOR_PACKAGES_MAX][SK_EDITOR_PACKAGES_PATH_CAP];
	u32 count;
	i32 add_pending; /* Add Package clicked (MOCK folder dialog) */

	i32 open; /* widget_window close flag (floating chrome) */

	/* sk-ui handles; live only while a ui context hosts the window chrome. */
	sk_ui_node_t win;
	sk_ui_node_t table;
	u32 last_synced_revision;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;
} packages_state_t;

typedef struct packages_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
} packages_class_state_t;

static void packages_init(sk_editor_window_t* window);
static void packages_draw(sk_editor_window_t* window, i32* open);
static void packages_destroy(sk_editor_window_t* window);

static sk_editor_window_t packages_window = {
	.title = "Packages",
	.dock_id = "sk.editor_window.packages",
	.dock_position = SK_EDITOR_DOCK_NONE,
	.order = 0,
	.workspace_mask = 0u, /* menu-opened, not default-docked */
	.init = packages_init,
	.draw = packages_draw,
	.render = NULL,
	.destroy = packages_destroy,
};

static packages_class_state_t* packages_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (packages_class_state_t*)app_api->get_api(app_context, SK_EDITOR_PACKAGES_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Package list logic (ui-independent)                               */
/* ------------------------------------------------------------------ */

static i32 packages_add(packages_state_t* state, const_chr_t path) {
	const_chr_t p = path != NULL ? path : "";
	u32 i;
	if (state->count >= SK_EDITOR_PACKAGES_MAX) {
		return -1;
	}
	for (i = 0u; i < state->count; ++i) {
		if (strcmp(state->paths[i], p) == 0) {
			return 0; /* already present */
		}
	}
	(void)snprintf(state->paths[state->count], SK_EDITOR_PACKAGES_PATH_CAP, "%s", p);
	state->count += 1u;
	return 0;
}

static void packages_remove(packages_state_t* state, const_chr_t path) {
	u32 i;
	for (i = 0u; i < state->count; ++i) {
		if (strcmp(state->paths[i], path != NULL ? path : "") == 0) {
			memmove(&state->paths[i], &state->paths[i + 1u], sizeof(char[SK_EDITOR_PACKAGES_PATH_CAP]) * (state->count - i - 1u));
			state->count -= 1u;
			return;
		}
	}
}

/* Path basename (C++ Path::Name). */
static void packages_basename(const_chr_t path, char* out, u32 cap) {
	const_chr_t base = path;
	const_chr_t p;
	if (path == NULL) {
		(void)snprintf(out, cap, "%s", "");
		return;
	}
	for (p = path; *p != '\0'; ++p) {
		if (*p == '/' || *p == '\\') {
			base = p + 1;
		}
	}
	(void)snprintf(out, cap, "%s", base);
}

/* ------------------------------------------------------------------ */
/*  Widget callbacks                                                   */
/* ------------------------------------------------------------------ */

static void packages_on_add_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	packages_state_t* state = (packages_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	if (state != NULL) {
		/* MOCK: no Platform::PickFolder in v2 — record the request. */
		state->add_pending = 1;
	}
	event->consumed = 1;
}

static void packages_on_remove_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	packages_state_t* state = (packages_state_t*)user;
	const_chr_t id;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK || state == NULL) {
		return;
	}
	/* The button carries its row index in the test id ("packages.remove.<i>"). */
	id = state->ui != NULL ? state->ui->node_get_id(state->ui_ctx, node) : NULL;
	if (id != NULL) {
		const_chr_t marker = strstr(id, "packages.remove.");
		if (marker != NULL) {
			char* end = NULL;
			unsigned long v = strtoul(marker + strlen("packages.remove."), &end, 10);
			if (end != marker + strlen("packages.remove.") && v < (unsigned long)state->count) {
				packages_remove(state, state->paths[(u32)v]);
			}
		}
	}
	event->consumed = 1;
}

/* ------------------------------------------------------------------ */
/*  Chrome build + per-frame sync                                     */
/* ------------------------------------------------------------------ */

/* Rebuild the packages table (3 columns: Name / Path / remove). The table
 * model owns its rows internally, so the whole widget is recreated on count
 * change (node_destroy frees it recursively; stable id recycles). */
static void packages_rebuild_table(packages_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	if (ui == NULL) {
		return;
	}
	if (sk_ui_node_is_valid(state->table) && ui->node_alive(ctx, state->table)) {
		(void)ui->node_destroy(ctx, state->table);
	}
	state->table = ui->widget_table(ctx, ui->find_by_id(ctx, "packages.content"), "packages.table", 3,
									SK_UI_TABLE_FLAG_ROW_BG | SK_UI_TABLE_FLAG_BORDERS | SK_UI_TABLE_FLAG_SCROLL_Y, 0.0f, 0.0f);
	if (!sk_ui_node_is_valid(state->table)) {
		return;
	}
	(void)ui->table_setup_column(ctx, state->table, "Name", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 0.30f);
	(void)ui->table_setup_column(ctx, state->table, "Path", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 0.70f);
	(void)ui->table_setup_column(ctx, state->table, "", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 36.0f);
	(void)ui->table_headers_row(ctx, state->table);

	for (i = 0u; i < state->count; ++i) {
		sk_ui_node_t cell;
		sk_ui_node_t btn;
		char id[64];
		sk_ui_node_callbacks_t cbs;
		char name[SK_EDITOR_PACKAGES_PATH_CAP];

		(void)ui->table_next_row(ctx, state->table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f);

		(void)ui->table_next_column(ctx, state->table);
		cell = ui->table_current_cell(ctx, state->table);
		packages_basename(state->paths[i], name, sizeof(name));
		(void)ui->widget_label(ctx, cell, name, NULL);

		(void)ui->table_next_column(ctx, state->table);
		cell = ui->table_current_cell(ctx, state->table);
		(void)ui->widget_label(ctx, cell, state->paths[i], NULL);

		(void)ui->table_next_column(ctx, state->table);
		cell = ui->table_current_cell(ctx, state->table);
		(void)snprintf(id, sizeof(id), "packages.remove.%u", i);
		btn = ui->widget_small_button(ctx, cell, "x", id);
		if (sk_ui_node_is_valid(btn)) {
			memset(&cbs, 0, sizeof(cbs));
			cbs.on_click = packages_on_remove_click;
			cbs.user = state;
			(void)ui->node_set_callbacks(ctx, btn, &cbs);
		}
	}
	(void)ui->table_end(ctx, state->table);
	state->last_synced_revision = state->count;
}

static i32 packages_sync(packages_state_t* state) {
	u32 revision;
	if (state == NULL || state->ui == NULL) {
		return -1;
	}
	if (!sk_ui_node_is_valid(state->win)) {
		state->win = state->ui->find_by_id(state->ui_ctx, "sk.editor_window.packages");
	}
	revision = state->count;
	if (revision != state->last_synced_revision && sk_ui_node_is_valid(state->table)) {
		packages_rebuild_table(state);
	}
	return 0;
}

/* Build the floating chrome on the shell context root: a widget_window
 * (title bar + close button) centered like the C++ ImGuiCenterWindow. */
static void packages_build_ui(packages_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t root) {
	sk_ui_style_props_t p;
	sk_ui_node_t content;
	sk_ui_node_t toolbar;
	sk_ui_node_t btn;
	sk_ui_node_callbacks_t cbs;

	state->ui = ui;
	state->ui_ctx = ctx;

	state->win = ui->widget_window(ctx, root, "Packages", "sk.editor_window.packages", &state->open);
	if (!sk_ui_node_is_valid(state->win)) {
		return;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_percent(25.0f);
	p.layout.top = sk_ui_percent(15.0f);
	p.layout.width = sk_ui_percent(50.0f);
	p.layout.height = sk_ui_percent(70.0f);
	(void)ui->node_set_inline_style(ctx, state->win, &p);

	content = ui->editor_window_content(ctx, state->win);
	if (!sk_ui_node_is_valid(content)) {
		return;
	}
	{
		sk_ui_style_props_t cp;
		memset(&cp, 0, sizeof(cp));
		cp.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_ROW_GAP | SK_UI_SP_PADDING;
		cp.layout.flex_direction = SK_UI_FLEX_COLUMN;
		cp.layout.width = sk_ui_percent(100.0f);
		cp.layout.height = sk_ui_percent(100.0f);
		cp.layout.row_gap = 6.0f;
		cp.layout.padding.left = 8.0f;
		cp.layout.padding.top = 8.0f;
		cp.layout.padding.right = 8.0f;
		cp.layout.padding.bottom = 8.0f;
		(void)ui->node_set_inline_style(ctx, content, &cp);
	}
	/* Give the content a stable id so the table rebuild can find it. */
	(void)ui->node_set_id(ctx, content, "packages.content");

	/* Toolbar: Add Package button + hint. */
	toolbar = ui->widget_view(ctx, content, "packages.toolbar");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 8.0f;
	p.layout.width = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, toolbar, &p);

	btn = ui->widget_button(ctx, toolbar, "Add Package", "packages.add");
	if (sk_ui_node_is_valid(btn)) {
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = packages_on_add_click;
		cbs.user = state;
		(void)ui->node_set_callbacks(ctx, btn, &cbs);
	}
	(void)ui->widget_text_disabled(ctx, toolbar, "A package is a folder containing an \"Assets\" and/or \"Binaries\" folder.", "packages.hint");

	packages_rebuild_table(state);
	state->last_synced_revision = state->count;
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void packages_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	packages_class_state_t* cls = (packages_class_state_t*)window->user_data;
	packages_state_t* state;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (packages_state_t*)alloc->alloc(alloc->instance, sizeof(packages_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	state->open = 1;

	/* Seed the mock project package list (v2 project model has no packages). */
	(void)packages_add(state, "D:/Projects/SkoreGame");
	(void)packages_add(state, "D:/Projects/EnginePlugins");
	window->user_data = state;
}

static void packages_draw(sk_editor_window_t* window, i32* open) {
	packages_state_t* state = (packages_state_t*)window->user_data;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_ui_node_t root;
	sk_ui_node_t win;
	*open = 1;
	if (state == NULL) {
		*open = 0;
		return;
	}
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	if (ctx == NULL) {
		return;
	}
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ui == NULL) {
		return;
	}
	root = ui->context_root(ctx);
	win = ui->find_by_id(ctx, "sk.editor_window.packages");
	if (!sk_ui_node_is_valid(win) || !ui->node_alive(ctx, win)) {
		packages_build_ui(state, ui, ctx, root);
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	(void)packages_sync(state);
	if (state->open == 0) {
		*open = 0; /* close button → close the window */
	}
}

static void packages_destroy(sk_editor_window_t* window) {
	packages_state_t* state = (packages_state_t*)window->user_data;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_workspace_t* ws;
	sk_ui_context_t* ctx;
	const sk_ui_api_t* ui;
	if (state == NULL) {
		return;
	}
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ctx != NULL && ui != NULL && sk_ui_node_is_valid(state->win) && ui->node_alive(ctx, state->win)) {
		(void)ui->node_destroy(ctx, state->win);
	}
	state->win = SK_UI_NODE_INVALID;
	alloc->free(alloc->instance, state);
	window->user_data = NULL;
}

/* ------------------------------------------------------------------ */
/*  Ops table (add_impl; never direct symbols)                        */
/* ------------------------------------------------------------------ */

static sk_editor_window_t* packages_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_PACKAGES);
}

static packages_state_t* packages_state_of(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	sk_editor_window_t* window = sk_editor_window_by_type(app_context, app_api, SK_EDITOR_WINDOW_PACKAGES);
	if (window == NULL) {
		return NULL;
	}
	return (packages_state_t*)window->user_data;
}

static void packages_ops_add_package(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path) {
	packages_state_t* state = packages_state_of(app_context, app_api);
	if (state != NULL) {
		(void)packages_add(state, path);
	}
}

static void packages_ops_remove_package(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path) {
	packages_state_t* state = packages_state_of(app_context, app_api);
	if (state != NULL) {
		packages_remove(state, path);
	}
}

static void packages_ops_clear(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	packages_state_t* state = packages_state_of(app_context, app_api);
	if (state != NULL) {
		state->count = 0u;
	}
}

static u32 packages_ops_get_package_count(const sk_editor_window_t* window) {
	const packages_state_t* state = (const packages_state_t*)window->user_data;
	return state != NULL ? state->count : 0u;
}

static const_chr_t packages_ops_package_path_at(const sk_editor_window_t* window, u32 index) {
	const packages_state_t* state = (const packages_state_t*)window->user_data;
	if (state == NULL || index >= state->count) {
		return NULL;
	}
	return state->paths[index];
}

static const_chr_t packages_ops_package_name_at(const sk_editor_window_t* window, u32 index) {
	const packages_state_t* state = (const packages_state_t*)window->user_data;
	static char name[SK_EDITOR_PACKAGES_PATH_CAP];
	if (state == NULL || index >= state->count) {
		return NULL;
	}
	packages_basename(state->paths[index], name, sizeof(name));
	return name;
}

static i32 packages_ops_is_add_pending(const sk_editor_window_t* window) {
	const packages_state_t* state = (const packages_state_t*)window->user_data;
	return state != NULL ? state->add_pending : 0;
}

static i32 packages_ops_consume_add_pending(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	packages_state_t* state = (packages_state_t*)window->user_data;
	i32 prev;
	(void)app_context;
	(void)app_api;
	if (state == NULL) {
		return 0;
	}
	prev = state->add_pending;
	state->add_pending = 0;
	return prev;
}

static const sk_editor_packages_ops_t packages_ops = {
	packages_open,
	packages_ops_add_package,
	packages_ops_remove_package,
	packages_ops_clear,
	packages_ops_get_package_count,
	packages_ops_package_path_at,
	packages_ops_package_name_at,
	packages_ops_is_add_pending,
	packages_ops_consume_add_pending,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_packages_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	packages_class_state_t* cls = packages_class(app_context, app_api);
	if (cls != NULL) {
		packages_window.user_data = cls;
		return;
	}
	cls = (packages_class_state_t*)alloc->alloc(alloc->instance, sizeof(packages_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_PACKAGES_STATE_TYPE_ID, cls);

	packages_window.type_id = SK_EDITOR_WINDOW_PACKAGES;
	packages_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &packages_window);
	app_api->add_impl(app_context, SK_EDITOR_PACKAGES_OPS_TYPE_ID, &packages_ops);
}

void sk_editor_packages_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	packages_class_state_t* cls = packages_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &packages_window);
	app_api->remove_impl(app_context, SK_EDITOR_PACKAGES_OPS_TYPE_ID, &packages_ops);
	app_api->set_api(app_context, SK_EDITOR_PACKAGES_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (packages_window.user_data == cls) {
		packages_window.user_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "test.h"

/* Ops-table path (no ui): seeded mock list, add/remove/clear and the
 * Add Package pending flag. */
SK_TEST(editor_packages_window_ops_mock_list) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_packages_ops_t* ops;
	sk_editor_window_t* window;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	TEST_ASSERT_NULL(sk_editor_packages_ops(app, boot.api));
	sk_editor_packages_register(app, boot.api);
	sk_editor_packages_register(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_PACKAGES_OPS_TYPE_ID));

	ops = sk_editor_packages_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Packages", window->title);
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_PACKAGES));
	/* On-demand: never default-docked. */
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_NONE, window->dock_position);
	TEST_ASSERT_EQUAL_UINT(0u, window->workspace_mask);

	/* Seeded mock packages. */
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_package_count(window));
	TEST_ASSERT_EQUAL_STRING("SkoreGame", ops->package_name_at(window, 0u));
	TEST_ASSERT_EQUAL_STRING("D:/Projects/SkoreGame", ops->package_path_at(window, 0u));

	/* Add (dedupes) / remove / clear. */
	ops->add_package(app, boot.api, "D:/Projects/SkoreGame");
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_package_count(window));
	ops->add_package(app, boot.api, "C:/Libs/ThirdParty");
	TEST_ASSERT_EQUAL_UINT(3u, ops->get_package_count(window));
	TEST_ASSERT_EQUAL_STRING("ThirdParty", ops->package_name_at(window, 2u));
	ops->remove_package(app, boot.api, "D:/Projects/SkoreGame");
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_package_count(window));
	TEST_ASSERT_EQUAL_STRING("EnginePlugins", ops->package_name_at(window, 0u));
	ops->clear(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_package_count(window));

	/* Add button pending flag (MOCK folder dialog). */
	TEST_ASSERT_EQUAL_INT(0, ops->is_add_pending(window));
	TEST_ASSERT_EQUAL_INT(0, ops->consume_add_pending(app, boot.api, window));
	editor->window_close(app, boot.api, window);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_PACKAGES));
	TEST_ASSERT_NOT_NULL(sk_editor_packages_ops(app, boot.api));

	sk_editor_packages_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_packages_ops(app, boot.api));
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
