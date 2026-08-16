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

/* Floating-window geometry (C++ ImGuiCenterWindow on-demand mock). Width/height
 * are border-box; the editor-window class adds 1px borders on every side. */
#define SK_EDITOR_PACKAGES_WIN_W 500.0f
#define SK_EDITOR_PACKAGES_WIN_H 280.0f
/* Chrome subtracted when the table has not been laid out yet (same box as
 * the 500px floating window: 1px editor-window borders, 8px content pad,
 * 1px table borders). Used only as the first-frame resolve width — column
 * boxes themselves come from measured glyph advance (APX-392), not from a
 * 0.30/0.70 guess. */
#define SK_EDITOR_PACKAGES_WIN_BORDER 1.0f
#define SK_EDITOR_PACKAGES_CONTENT_PAD 8.0f
#define SK_EDITOR_PACKAGES_TABLE_BORDER 1.0f
#define SK_EDITOR_PACKAGES_CELL_PAD 12.0f /* ui-table-cell pad 6+6 */
#define SK_EDITOR_PACKAGES_REMOVE_W 36.0f
#define SK_EDITOR_PACKAGES_LABEL_PX 13.0f

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

/* Cell labels size from Clay's layout-font glyph advance (APX-382): wrap
 * off so a single token is never mid-word clipped, flex_shrink 0 so the
 * solver cannot compress the measured box. */
static void packages_style_cell_label(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t label) {
	sk_ui_style_props_t p;
	if (!sk_ui_node_is_valid(label)) {
		return;
	}
	(void)ui->label_set_wrap(ctx, label, 0);
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_SHRINK | SK_UI_SP_FONT_SIZE;
	p.layout.flex_shrink = 0.0f;
	p.font_size = SK_EDITOR_PACKAGES_LABEL_PX;
	(void)ui->node_merge_inline_style(ctx, label, &p);
}

static f32 packages_derived_table_w(void) {
	return SK_EDITOR_PACKAGES_WIN_W - 2.0f * SK_EDITOR_PACKAGES_WIN_BORDER - 2.0f * SK_EDITOR_PACKAGES_CONTENT_PAD - 2.0f * SK_EDITOR_PACKAGES_TABLE_BORDER;
}

/* Prefer the laid-out content box; fall back to the window-derived leftover
 * so the first frame (no abs rects yet) does not resolve against 3×80px. */
static f32 packages_table_avail(packages_state_t* state) {
	sk_ui_rect_t r;
	sk_ui_node_t content;
	if (state == NULL || state->ui == NULL) {
		return packages_derived_table_w();
	}
	content = state->ui->find_by_id(state->ui_ctx, "packages.content");
	if (sk_ui_node_is_valid(content) && state->ui->node_get_abs_rect(state->ui_ctx, content, &r, NULL) == 0 && r.width > 32.0f) {
		f32 inner = r.width - 2.0f * SK_EDITOR_PACKAGES_CONTENT_PAD - 2.0f * SK_EDITOR_PACKAGES_TABLE_BORDER;
		if (inner > 32.0f) {
			return inner;
		}
	}
	return packages_derived_table_w();
}

/* Size Name from the widest laid-out basename box, keep Path as stretch of
 * the leftover after Name + the reserved 36px remove column. Grow the
 * floating window if a measured path cannot fit that leftover. */
static void packages_resolve_and_fit(packages_state_t* state) {
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	f32 avail;
	f32 name_need = 0.0f;
	f32 path_need = 0.0f;
	u32 i;
	if (state == NULL || state->ui == NULL || !sk_ui_node_is_valid(state->table)) {
		return;
	}
	ui = state->ui;
	ctx = state->ui_ctx;
	avail = packages_table_avail(state);
	for (i = 0u; i < state->count; ++i) {
		char nameid[48];
		char pathid[48];
		sk_ui_rect_t lr;
		sk_ui_node_t n;
		(void)snprintf(nameid, sizeof(nameid), "packages.name.%u", i);
		n = ui->find_by_id(ctx, nameid);
		if (sk_ui_node_is_valid(n) && ui->node_get_abs_rect(ctx, n, &lr, NULL) == 0 && lr.width > 1.0f) {
			f32 need = lr.width + SK_EDITOR_PACKAGES_CELL_PAD;
			if (need > name_need) {
				name_need = need;
			}
		}
		(void)snprintf(pathid, sizeof(pathid), "packages.path.%u", i);
		n = ui->find_by_id(ctx, pathid);
		if (sk_ui_node_is_valid(n) && ui->node_get_abs_rect(ctx, n, &lr, NULL) == 0 && lr.width > 1.0f) {
			f32 need = lr.width + SK_EDITOR_PACKAGES_CELL_PAD;
			if (need > path_need) {
				path_need = need;
			}
		}
	}
	if (name_need > 8.0f) {
		(void)ui->table_set_column_width(ctx, state->table, 0, name_need);
	}
	(void)ui->table_set_column_width(ctx, state->table, 2, SK_EDITOR_PACKAGES_REMOVE_W);
	(void)ui->table_resolve_column_widths(ctx, state->table, avail);
	if (name_need > 8.0f && path_need > 8.0f && name_need + path_need + SK_EDITOR_PACKAGES_REMOVE_W > avail + 0.5f && sk_ui_node_is_valid(state->win)) {
		sk_ui_style_props_t p;
		f32 extra = (name_need + path_need + SK_EDITOR_PACKAGES_REMOVE_W) - avail;
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_WIDTH;
		p.layout.width = sk_ui_pt(SK_EDITOR_PACKAGES_WIN_W + extra + 4.0f);
		(void)ui->node_merge_inline_style(ctx, state->win, &p);
	}
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
	/* outer_width 0 → 100% of packages.content (no guessed 480px box).
	 * Column resolve uses packages_table_avail / measured label boxes. */
	state->table = ui->widget_table(ctx, ui->find_by_id(ctx, "packages.content"), "packages.table", 3,
									SK_UI_TABLE_FLAG_ROW_BG | SK_UI_TABLE_FLAG_BORDERS | SK_UI_TABLE_FLAG_SCROLL_Y, 0.0f, 0.0f);
	if (!sk_ui_node_is_valid(state->table)) {
		return;
	}
	/* INDENT_DISABLE: col 0 otherwise steals 6+14px tree indent from Name.
	 * Stretch weights only share leftover after the reserved 36px remove
	 * column; Name is later pinned to the measured basename box. */
	(void)ui->table_setup_column(ctx, state->table, "Name", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH | SK_UI_TABLE_COLUMN_FLAG_INDENT_DISABLE, 1.0f);
	(void)ui->table_setup_column(ctx, state->table, "Path", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 2.0f);
	(void)ui->table_setup_column(ctx, state->table, "", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, SK_EDITOR_PACKAGES_REMOVE_W);
	(void)ui->table_headers_row(ctx, state->table);

	for (i = 0u; i < state->count; ++i) {
		sk_ui_node_t cell;
		sk_ui_node_t btn;
		char id[64];
		char name_id[48];
		char path_id[48];
		sk_ui_node_callbacks_t cbs;
		char name[SK_EDITOR_PACKAGES_PATH_CAP];

		(void)ui->table_next_row(ctx, state->table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f);

		(void)ui->table_next_column(ctx, state->table);
		cell = ui->table_current_cell(ctx, state->table);
		packages_basename(state->paths[i], name, sizeof(name));
		(void)snprintf(name_id, sizeof(name_id), "packages.name.%u", i);
		packages_style_cell_label(ui, ctx, ui->widget_label(ctx, cell, name, name_id));
		(void)ui->node_set_clip_children(ctx, cell, 1);

		(void)ui->table_next_column(ctx, state->table);
		cell = ui->table_current_cell(ctx, state->table);
		(void)snprintf(path_id, sizeof(path_id), "packages.path.%u", i);
		packages_style_cell_label(ui, ctx, ui->widget_label(ctx, cell, state->paths[i], path_id));
		/* Clip so a long path cannot paint under the reserved 36px remove column. */
		(void)ui->node_set_clip_children(ctx, cell, 1);

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
	packages_resolve_and_fit(state);
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
	} else {
		packages_resolve_and_fit(state);
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
	sk_ui_node_t hint;
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
	p.layout.left = sk_ui_pt(36.0f);
	p.layout.top = sk_ui_pt(78.0f);
	p.layout.width = sk_ui_pt(SK_EDITOR_PACKAGES_WIN_W);
	p.layout.height = sk_ui_pt(SK_EDITOR_PACKAGES_WIN_H);
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
		cp.layout.padding.left = SK_EDITOR_PACKAGES_CONTENT_PAD;
		cp.layout.padding.top = SK_EDITOR_PACKAGES_CONTENT_PAD;
		cp.layout.padding.right = SK_EDITOR_PACKAGES_CONTENT_PAD;
		cp.layout.padding.bottom = SK_EDITOR_PACKAGES_CONTENT_PAD;
		(void)ui->node_set_inline_style(ctx, content, &cp);
	}
	/* Give the content a stable id so the table rebuild can find it. */
	(void)ui->node_set_id(ctx, content, "packages.content");

	/* Toolbar: Add Package on its own row, hint sentence below it (APX-392).
	 * The sentence is wider than the leftover next to the button, so a single
	 * toolbar row cut it mid-quote. Wrap + full-row width is the overflow
	 * path; 13px is the editor default so the mock sentence stays one line
	 * inside the 500px window. */
	toolbar = ui->widget_view(ctx, content, "packages.toolbar");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_ROW_GAP | SK_UI_SP_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	p.layout.row_gap = 6.0f;
	p.layout.width = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, toolbar, &p);

	btn = ui->widget_button(ctx, toolbar, "Add Package", "packages.add");
	if (sk_ui_node_is_valid(btn)) {
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = packages_on_add_click;
		cbs.user = state;
		(void)ui->node_set_callbacks(ctx, btn, &cbs);
	}
	hint = ui->widget_text_disabled(ctx, toolbar, "A package is a folder containing an \"Assets\" and/or \"Binaries\" folder.", "packages.hint");
	if (sk_ui_node_is_valid(hint)) {
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK;
		p.font_size = SK_EDITOR_PACKAGES_LABEL_PX;
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.flex_shrink = 0.0f;
		(void)ui->node_merge_inline_style(ctx, hint, &p);
		(void)ui->node_set_prop_i32(ctx, hint, "wrap", 1);
	}

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
#include "filesystem.h"
#include "path.h"
#include "skore_test_font_ttf.h"
#include "test.h"

static i32 packages_test_plugin_path(const_chr_t name, char* out, u32 cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	char base[SK_FS_PATH_MAX];
	char plugins[SK_FS_PATH_MAX];
	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}
	if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins, (u32)sizeof(plugins)) < 0) {
		return -1;
	}
	if (sk_path_join(sk_str_view_cstr(plugins), sk_str_view_cstr(name), out, cap) < 0) {
		return -1;
	}
	return 0;
}

static const sk_ui_api_t* packages_test_load_ui(sk_app_context_t* app_ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-ui.dylib";
#else
	const_chr_t name = "sk-ui.so";
#endif
	if (packages_test_plugin_path(name, path, (u32)sizeof(path)) == 0) {
		(void)app_api->load_plugin(app_ctx, path);
	}
	return (const sk_ui_api_t*)app_api->get_api(app_ctx, SK_UI_API_TYPE_ID);
}

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

/* ui-hosted path (APX-392): the floating Packages window's toolbar hint
 * sentence wraps on its own row below Add Package, and every Name / Path
 * label is sized from real glyph advance so it fits its cell without
 * overlapping the reserved 36px remove button. Laid out at 1280x720 with
 * the host fonts bound (mirrors sk_editor_shell_frame). */
SK_TEST(editor_packages_window_ui_table_and_hint) {
	static const char hint_text[] = "A package is a folder containing an \"Assets\" and/or \"Binaries\" folder.";
	static const char* seeded_names[2] = {"SkoreGame", "EnginePlugins"};
	static const char* seeded_paths[2] = {"D:/Projects/SkoreGame", "D:/Projects/EnginePlugins"};
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_ui_api_t* ui;
	const sk_editor_packages_ops_t* ops;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_editor_window_t* window;
	sk_ui_font_system_t* lfonts;
	sk_ui_font_t* lfont;
	f32 hint_w = 0.0f, hint_h = 0.0f;
	sk_ui_rect_t win_r, hint_r, add_r, remove_r;
	i32 open = 1;
	u32 i;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	ui = packages_test_load_ui(app, boot.api);
	if (ui == NULL) {
		/* Plugin not built/copied next to tests — skip rather than fail CI. */
		sk_app_shutdown(app);
		TEST_IGNORE_MESSAGE("sk-ui plugin not available");
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, ui->init());
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	sk_editor_workspace_register_impls(app, boot.api);
	sk_editor_packages_register(app, boot.api);

	/* Host the floating window on the workspace's dock context root. */
	ws = editor->workspace_create(app, boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(ws);
	sk_editor_workspace_set_dock_context(ws, ctx);
	sk_editor_workspace_switch(ws);
	editor->dockspace_init(ws);

	ops = sk_editor_packages_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);
	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_PACKAGES));
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_package_count(window));

	/* Draw builds the floating chrome + table. */
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(1, open);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "packages.hint")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "packages.add")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "packages.table")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "packages.remove.0")));

	/* Bind the layout fonts and resolve + lay out at 1280x720. */
	lfonts = ui->font_system_create(NULL);
	TEST_ASSERT_NOT_NULL(lfonts);
	lfont = ui->font_load_memory(lfonts, skore_test_font_ttf, (u32)sizeof(skore_test_font_ttf));
	TEST_ASSERT_NOT_NULL(lfont);
	(void)ui->font_msdf_bake(lfont);
	TEST_ASSERT_NOT_NULL(ui->set_layout_fonts);
	ui->set_layout_fonts(ctx, lfonts, lfont);
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1280.0f, 720.0f));
	/* Second pass: packages_resolve_and_fit pins Name from the laid-out
	 * glyph boxes (sandbox does several shell frames). */
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1280.0f, 720.0f));

	/* Hint sentence: measured real advance must be available on the label
	 * box, on a single line, below the Add Package button, inside the window. */
	TEST_ASSERT_EQUAL_INT(0, ui->font_measure_text(lfonts, lfont, 13u, hint_text, &hint_w, &hint_h));
	TEST_ASSERT_TRUE(hint_w > 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "packages.hint"), &hint_r, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "packages.add"), &add_r, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "sk.editor_window.packages"), &win_r, NULL));
	TEST_ASSERT_TRUE(hint_r.width >= hint_w - 1.0f);			 /* whole sentence fits on the row */
	TEST_ASSERT_TRUE(hint_r.height <= hint_h + 0.5f);			 /* one line (no mid-sentence wrap) */
	TEST_ASSERT_TRUE(hint_r.y >= add_r.y + add_r.height - 0.5f); /* its own row below the button */
	TEST_ASSERT_TRUE(hint_r.x >= win_r.x - 0.5f);
	TEST_ASSERT_TRUE(hint_r.x + hint_r.width <= win_r.x + win_r.width + 0.5f);

	/* Every Name / Path label fits inside its cell, stays clear of the
	 * remove-button column, and is one line tall. */
	for (i = 0u; i < 2u; ++i) {
		char nameid[40], pathid[40], cellname[48], cellpath[48], cellrm[48];
		sk_ui_rect_t name_cell, path_cell, rm_cell, name_lbl, path_lbl;
		sk_ui_node_t node;
		f32 name_w = 0.0f, path_w = 0.0f;

		(void)snprintf(nameid, sizeof(nameid), "packages.name.%u", i);
		(void)snprintf(pathid, sizeof(pathid), "packages.path.%u", i);
		(void)snprintf(cellname, sizeof(cellname), "packages.table/c%u-0", i + 1u);
		(void)snprintf(cellpath, sizeof(cellpath), "packages.table/c%u-1", i + 1u);
		(void)snprintf(cellrm, sizeof(cellrm), "packages.table/c%u-2", i + 1u);
		node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, cellname);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, node, &name_cell, NULL));
		node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, cellpath);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, node, &path_cell, NULL));
		node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, cellrm);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, node, &rm_cell, NULL));
		node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, nameid);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, node, &name_lbl, NULL));
		node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, pathid);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, node, &path_lbl, NULL));

		TEST_ASSERT_EQUAL_INT(0, ui->font_measure_text(lfonts, lfont, 13u, seeded_names[i], &name_w, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->font_measure_text(lfonts, lfont, 13u, seeded_paths[i], &path_w, NULL));
		TEST_ASSERT_TRUE(name_w > 0.0f);
		TEST_ASSERT_TRUE(path_w > 0.0f);
		/* Label box is the real advance, not a clipped/guessed width. */
		TEST_ASSERT_TRUE(name_lbl.width >= name_w - 1.0f);
		TEST_ASSERT_TRUE(path_lbl.width >= path_w - 1.0f);

		/* Name label fully inside its cell and one line tall (no 'Skore' clip). */
		TEST_ASSERT_TRUE(name_lbl.x >= name_cell.x - 0.5f);
		TEST_ASSERT_TRUE(name_lbl.x + name_lbl.width <= name_cell.x + name_cell.width + 0.5f);
		TEST_ASSERT_TRUE(name_lbl.y >= name_cell.y - 0.5f);
		TEST_ASSERT_TRUE(name_lbl.y + name_lbl.height <= name_cell.y + name_cell.height + 0.5f);
		TEST_ASSERT_TRUE(name_lbl.height < 20.0f);

		/* Path label fully inside its cell (no 'D:/Projects/SkoreGa' clip) and
		 * clear of the remove-button column (no 'x' + stray 's' overlap). */
		TEST_ASSERT_TRUE(path_lbl.x >= path_cell.x - 0.5f);
		TEST_ASSERT_TRUE(path_lbl.x + path_lbl.width <= path_cell.x + path_cell.width + 0.5f);
		TEST_ASSERT_TRUE(path_lbl.x + path_lbl.width <= rm_cell.x + 0.5f);
		TEST_ASSERT_TRUE(path_lbl.height < 20.0f);

		/* The remove glyph button sits inside its fixed 36px column. */
		(void)snprintf(nameid, sizeof(nameid), "packages.remove.%u", i);
		node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, nameid);
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(node));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, node, &remove_r, NULL));
		TEST_ASSERT_TRUE(remove_r.x >= rm_cell.x - 0.5f);
		TEST_ASSERT_TRUE(remove_r.x + remove_r.width <= rm_cell.x + rm_cell.width + 0.5f);
	}

	ui->set_layout_fonts(ctx, NULL, NULL);
	ui->font_system_destroy(lfonts);
	window = editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_PACKAGES);
	if (window != NULL) {
		editor->window_close(app, boot.api, window);
	}
	editor->workspace_destroy(ws);
	sk_editor_packages_shutdown(app, boot.api);
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
