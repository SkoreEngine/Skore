/**
 * @file settings_window.c
 * @brief Settings window (APX-374): mock settings tree on the v2 shell.
 *
 * Port of main-branch Skore::SettingsWindow (migration manifest §3.10): an
 * on-demand floating window (dock position None / workspace mask 0 — never
 * auto-opened) titled by the opened group ("Editor Settings" / "Project
 * Settings"). Body = collapsing tree of setting groups (left) + the selected
 * group's entries (right) with per-entry editors and — for the Physics group
 * — the layer-collision matrix, mocked per the manifest until physics
 * settings exist in v2.
 *
 * The v2 engine has no EditableSettings registry yet, so the tree is MOCK
 * session data: seeded groups + entry values the ops table reads/edits;
 * nothing is persisted (the C++ window has no EditorSerialize fields).
 * Because the window is never docked, its chrome is a floating
 * `widget_window` on the shell context root (absolute, centered) — the v2
 * equivalent of the C++ ImGuiCenterWindow on-demand window. The window
 * publishes one `sk_editor_settings_ops_t` table registered with add_impl
 * (window-table-pattern.md §7); the shell's Edit/Editor Settings and
 * Edit/Project Settings menus route through it.
 */

#include "settings_window.h"

#include "allocator.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define SK_EDITOR_SETTINGS_STATE_TYPE_ID SK_TYPE_ID("sk.editor.settings.state", 0x4d1f3a5b7c9e0f2aULL, 0x6b8d0c2e4f6a8b0dULL)

/* ------------------------------------------------------------------ */
/*  Mock settings model (session data; C++ EditableSettings registry) */
/* ------------------------------------------------------------------ */

typedef struct settings_entry_t {
	char label[SK_EDITOR_SETTINGS_LABEL_CAP];
	sk_editor_settings_entry_kind_t kind;
	i32 b;								  /* BOOL */
	i32 i;								  /* INT */
	f32 f;								  /* FLOAT */
	char s[SK_EDITOR_SETTINGS_LABEL_CAP]; /* STRING */
} settings_entry_t;

typedef struct settings_group_t {
	char label[SK_EDITOR_SETTINGS_LABEL_CAP];
	u32 parent; /* tree parent group index (0 = root) */
	settings_entry_t entries[SK_EDITOR_SETTINGS_MAX_ENTRIES];
	u32 entry_count;
} settings_group_t;

/* ------------------------------------------------------------------ */
/*  Window state                                                       */
/* ------------------------------------------------------------------ */

typedef struct settings_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;

	u32 group; /* SK_EDITOR_SETTINGS_GROUP_EDITOR / PROJECT */
	char title[SK_EDITOR_SETTINGS_LABEL_CAP];

	settings_group_t groups[SK_EDITOR_SETTINGS_MAX_GROUPS];
	u32 group_count;
	u32 selected_group;
	char search[SK_EDITOR_SETTINGS_LABEL_CAP];

	/* Mock layer-collision matrix (C++ PhysicsSettings; session-only). */
	char layer_names[SK_EDITOR_SETTINGS_LAYER_COUNT][SK_EDITOR_SETTINGS_LABEL_CAP];
	u8 collision[SK_EDITOR_SETTINGS_LAYER_COUNT][SK_EDITOR_SETTINGS_LAYER_COUNT];

	i32 open; /* widget_window close flag (floating chrome) */

	/* sk-ui handles; live only while a ui context hosts the window chrome. */
	sk_ui_node_t win;
	sk_ui_node_t tree;
	sk_ui_node_t right;
	sk_ui_node_t search_input;
	sk_ui_item_t tree_items[SK_EDITOR_SETTINGS_MAX_GROUPS];
	sk_ui_item_array_t tree_arr;
	u32 last_synced_revision;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;
} settings_state_t;

typedef struct settings_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
} settings_class_state_t;

static void settings_init(sk_editor_window_t* window);
static void settings_draw(sk_editor_window_t* window, i32* open);
static void settings_destroy(sk_editor_window_t* window);

static sk_editor_window_t settings_window = {
	.title = "Settings",
	.dock_id = "sk.editor_window.settings",
	.dock_position = SK_EDITOR_DOCK_NONE,
	.order = 0,
	.workspace_mask = 0u, /* menu-opened, not default-docked */
	.init = settings_init,
	.draw = settings_draw,
	.render = NULL,
	.destroy = settings_destroy,
};

static settings_class_state_t* settings_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (settings_class_state_t*)app_api->get_api(app_context, SK_EDITOR_SETTINGS_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Mock model seeding (ui-independent)                               */
/* ------------------------------------------------------------------ */

static settings_entry_t* settings_add_entry(settings_group_t* g, const_chr_t label, sk_editor_settings_entry_kind_t kind) {
	settings_entry_t* e;
	if (g->entry_count >= SK_EDITOR_SETTINGS_MAX_ENTRIES) {
		return NULL;
	}
	e = &g->entries[g->entry_count++];
	memset(e, 0, sizeof(*e));
	(void)snprintf(e->label, sizeof(e->label), "%s", label);
	e->kind = kind;
	return e;
}

static void settings_seed(settings_state_t* state) {
	settings_group_t* g;
	settings_entry_t* e;
	u32 i;

	/* Mock layer names (C++ LayerSystem). */
	static const char* layer_seed[SK_EDITOR_SETTINGS_LAYER_COUNT] = {"Default", "Player", "Enemy", "Environment", "Projectile", "Trigger", "UI", "Custom"};
	for (i = 0u; i < SK_EDITOR_SETTINGS_LAYER_COUNT; ++i) {
		(void)snprintf(state->layer_names[i], sizeof(state->layer_names[i]), "%s", layer_seed[i]);
	}
	/* Diagonal = collide with self, off-diagonal defaults on. */
	memset(state->collision, 1, sizeof(state->collision));

	/* Project group tree. */
	g = &state->groups[state->group_count++];
	(void)snprintf(g->label, sizeof(g->label), "%s", "General");
	g->parent = 0u;
	e = settings_add_entry(g, "Project Name", SK_EDITOR_SETTINGS_ENTRY_STRING);
	if (e != NULL) {
		(void)snprintf(e->s, sizeof(e->s), "%s", "MyGame");
	}
	e = settings_add_entry(g, "Company Name", SK_EDITOR_SETTINGS_ENTRY_STRING);
	if (e != NULL) {
		(void)snprintf(e->s, sizeof(e->s), "%s", "Skore");
	}
	e = settings_add_entry(g, "Auto Save", SK_EDITOR_SETTINGS_ENTRY_BOOL);
	if (e != NULL) {
		e->b = 1;
	}

	g = &state->groups[state->group_count++];
	(void)snprintf(g->label, sizeof(g->label), "%s", "Rendering");
	g->parent = 0u;
	e = settings_add_entry(g, "Default Renderer", SK_EDITOR_SETTINGS_ENTRY_STRING);
	if (e != NULL) {
		(void)snprintf(e->s, sizeof(e->s), "%s", "Vulkan");
	}
	e = settings_add_entry(g, "VSync", SK_EDITOR_SETTINGS_ENTRY_BOOL);
	if (e != NULL) {
		e->b = 1;
	}
	e = settings_add_entry(g, "Max FPS", SK_EDITOR_SETTINGS_ENTRY_INT);
	if (e != NULL) {
		e->i = 144;
	}

	g = &state->groups[state->group_count++];
	(void)snprintf(g->label, sizeof(g->label), "%s", "Audio");
	g->parent = 0u;
	e = settings_add_entry(g, "Master Volume", SK_EDITOR_SETTINGS_ENTRY_FLOAT);
	if (e != NULL) {
		e->f = 0.8f;
	}

	g = &state->groups[state->group_count++];
	(void)snprintf(g->label, sizeof(g->label), "%s", "Physics");
	g->parent = 0u;
	e = settings_add_entry(g, "Layer Collision Matrix", SK_EDITOR_SETTINGS_ENTRY_COLLISION_MATRIX);
	if (e != NULL) {
		e->b = 1;
	}

	g = &state->groups[state->group_count++];
	(void)snprintf(g->label, sizeof(g->label), "%s", "Editor");
	g->parent = 0u;
	e = settings_add_entry(g, "Undo Limit", SK_EDITOR_SETTINGS_ENTRY_INT);
	if (e != NULL) {
		e->i = 100;
	}

	state->selected_group = 0u;
}

static void settings_apply_group_title(settings_state_t* state) {
	if (state->group == SK_EDITOR_SETTINGS_GROUP_EDITOR) {
		(void)snprintf(state->title, sizeof(state->title), "%s", "Editor Settings");
	} else {
		(void)snprintf(state->title, sizeof(state->title), "%s", "Project Settings");
	}
}

/* ------------------------------------------------------------------ */
/*  Widget callbacks                                                   */
/* ------------------------------------------------------------------ */

static void settings_on_tree_activate(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, void_ptr_t user) {
	settings_state_t* state = (settings_state_t*)user;
	(void)ctx;
	(void)host;
	if (state != NULL && item_id > 0u && item_id <= (u64)state->group_count) {
		state->selected_group = (u32)(item_id - 1u);
	}
}

static void settings_on_search_change(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text, void_ptr_t user) {
	settings_state_t* state = (settings_state_t*)user;
	(void)ctx;
	(void)node;
	if (state != NULL) {
		(void)snprintf(state->search, sizeof(state->search), "%s", text != NULL ? text : "");
	}
}

/* ------------------------------------------------------------------ */
/*  Chrome build + per-frame sync                                     */
/* ------------------------------------------------------------------ */

static void settings_apply_row_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_PADDING | SK_UI_SP_FLEX_SHRINK;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 8.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.padding.left = 4.0f;
	p.layout.padding.top = 3.0f;
	p.layout.padding.right = 4.0f;
	p.layout.padding.bottom = 3.0f;
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void settings_build_tree(settings_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	u32 visible = 0u;
	if (ui == NULL) {
		return;
	}
	state->tree_arr.items = state->tree_items;
	state->tree_arr.count = 0u;
	state->tree_arr.revision = 0u;
	for (i = 0u; i < state->group_count; ++i) {
		settings_group_t* g = &state->groups[i];
		sk_ui_item_t* item;
		if (state->search[0] != '\0' && strstr(g->label, state->search) == NULL) {
			continue; /* search filter (C++ ImGuiTextFilter on display names) */
		}
		item = &state->tree_items[visible++];
		sk_ui_item_set(item, (u64)i + 1u, (u64)g->parent, g->label, 0u);
	}
	state->tree_arr.count = visible;
	state->tree_arr.items = visible > 0u ? state->tree_items : NULL;
	if (sk_ui_node_is_valid(state->tree)) {
		(void)ui->item_bind_set_array(ctx, state->tree, &state->tree_arr);
		(void)ui->item_bind_sync(ctx, state->tree);
		/* Reflect the selected group on the rows. */
		for (i = 0u; i < state->group_count; ++i) {
			(void)ui->item_bind_set_selected(ctx, state->tree, (u64)i + 1u, i == state->selected_group ? 1 : 0);
		}
	}
}

static void settings_rebuild_right(settings_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	settings_group_t* g;
	u32 i;
	if (ui == NULL || !sk_ui_node_is_valid(state->right)) {
		return;
	}
	/* Rebuild: destroy the right panel children and re-create. The right
	 * panel node itself is stable (find_by_id); its children are a single
	 * `content` view destroyed recursively. */
	{
		sk_ui_node_t content = ui->find_by_id(ctx, "settings.right.content");
		if (sk_ui_node_is_valid(content) && ui->node_alive(ctx, content)) {
			(void)ui->node_destroy(ctx, content);
		}
		content = ui->widget_view(ctx, state->right, "settings.right.content");
		{
			sk_ui_style_props_t p;
			memset(&p, 0, sizeof(p));
			p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_ROW_GAP | SK_UI_SP_PADDING;
			p.layout.flex_direction = SK_UI_FLEX_COLUMN;
			p.layout.width = sk_ui_percent(100.0f);
			p.layout.row_gap = 4.0f;
			p.layout.padding.left = 8.0f;
			p.layout.padding.top = 8.0f;
			p.layout.padding.right = 8.0f;
			p.layout.padding.bottom = 8.0f;
			(void)ui->node_set_inline_style(ctx, content, &p);
		}

		if (state->selected_group >= state->group_count) {
			(void)ui->widget_text_disabled(ctx, content, "Select a setting group from the tree.", NULL);
			return;
		}
		g = &state->groups[state->selected_group];

		/* Header: FormatName of the group label. */
		(void)ui->widget_label(ctx, content, g->label, "settings.right.header");

		for (i = 0u; i < g->entry_count; ++i) {
			settings_entry_t* e = &g->entries[i];
			sk_ui_node_t row;
			sk_ui_node_t value;
			char id[64];
			char buf[SK_EDITOR_SETTINGS_LABEL_CAP + 16];

			row = ui->widget_view(ctx, content, NULL);
			settings_apply_row_style(ui, ctx, row);
			(void)ui->widget_label(ctx, row, e->label, NULL);

			(void)snprintf(id, sizeof(id), "settings.entry.%u", i);
			switch (e->kind) {
			case SK_EDITOR_SETTINGS_ENTRY_BOOL:
				value = ui->widget_checkbox(ctx, row, e->b, id);
				(void)value;
				break;
			case SK_EDITOR_SETTINGS_ENTRY_INT:
				value = ui->widget_drag_int(ctx, row, 1.0f, 0, 100000, e->i, "%d", id);
				(void)value;
				break;
			case SK_EDITOR_SETTINGS_ENTRY_FLOAT:
				value = ui->widget_drag_float(ctx, row, 0.01f, 0.0f, 1.0f, e->f, "%.2f", id);
				(void)value;
				break;
			case SK_EDITOR_SETTINGS_ENTRY_STRING:
				value = ui->widget_text_input(ctx, row, e->s, id);
				(void)value;
				break;
			case SK_EDITOR_SETTINGS_ENTRY_COLLISION_MATRIX:
			default: {
				u32 a;
				sk_ui_node_t matrix;
				(void)snprintf(buf, sizeof(buf), "Layer Collision Matrix (%u layers)", SK_EDITOR_SETTINGS_LAYER_COUNT);
				(void)ui->widget_label(ctx, row, buf, NULL);
				matrix = ui->widget_view(ctx, content, "settings.collision.matrix");
				{
					sk_ui_style_props_t p;
					memset(&p, 0, sizeof(p));
					p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_COLUMN_GAP | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR;
					p.layout.flex_direction = SK_UI_FLEX_COLUMN;
					p.layout.width = sk_ui_percent(100.0f);
					p.layout.column_gap = 2.0f;
					p.layout.padding.left = 8.0f;
					p.layout.padding.top = 6.0f;
					p.layout.padding.right = 8.0f;
					p.layout.padding.bottom = 6.0f;
					p.background_color = sk_ui_rgba(0.13f, 0.14f, 0.17f, 1.0f);
					(void)ui->node_set_inline_style(ctx, matrix, &p);
				}
				for (a = 0u; a < SK_EDITOR_SETTINGS_LAYER_COUNT; ++a) {
					u32 b;
					sk_ui_node_t row2 = ui->widget_view(ctx, matrix, NULL);
					settings_apply_row_style(ui, ctx, row2);
					(void)ui->widget_label(ctx, row2, state->layer_names[a], NULL);
					for (b = a; b < SK_EDITOR_SETTINGS_LAYER_COUNT; ++b) {
						sk_ui_node_t cb;
						(void)snprintf(id, sizeof(id), "settings.collision.%u.%u", a, b);
						cb = ui->widget_checkbox(ctx, row2, state->collision[a][b] != 0u ? 1 : 0, id);
						(void)cb;
					}
				}
				break;
			}
			}
		}
	}
}

static void settings_sync(settings_state_t* state) {
	u32 revision;
	if (state == NULL || state->ui == NULL) {
		return;
	}
	revision = state->selected_group + state->group_count + (u32)strlen(state->search);
	if (revision != state->last_synced_revision) {
		settings_build_tree(state);
		settings_rebuild_right(state);
		state->last_synced_revision = revision;
	}
	/* Search input sync (typed text lands in state->search via the callback). */
	if (sk_ui_node_is_valid(state->search_input)) {
		const_chr_t t = state->ui->text_input_get_text(state->ui_ctx, state->search_input);
		if (t != NULL && strcmp(t, state->search) != 0) {
			(void)snprintf(state->search, sizeof(state->search), "%s", t);
			revision = state->selected_group + state->group_count + (u32)strlen(state->search);
			if (revision != state->last_synced_revision) {
				settings_build_tree(state);
				settings_rebuild_right(state);
				state->last_synced_revision = revision;
			}
		}
	}
}

static void settings_build_ui(settings_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t root) {
	sk_ui_style_props_t p;
	sk_ui_node_t content;
	sk_ui_node_t body;
	sk_ui_node_t left;
	sk_ui_node_t left_scroll;
	sk_ui_node_t right;

	state->ui = ui;
	state->ui_ctx = ctx;

	state->win = ui->widget_window(ctx, root, state->title, "sk.editor_window.settings", &state->open);
	if (!sk_ui_node_is_valid(state->win)) {
		return;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(560.0f);
	p.layout.top = sk_ui_pt(90.0f);
	p.layout.width = sk_ui_pt(560.0f);
	p.layout.height = sk_ui_pt(300.0f);
	(void)ui->node_set_inline_style(ctx, state->win, &p);

	content = ui->editor_window_content(ctx, state->win);
	if (!sk_ui_node_is_valid(content)) {
		return;
	}

	/* Two-pane body: left tree + right entries (C++ BeginTable 2 cols). */
	body = ui->widget_view(ctx, content, "settings.body");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, body, &p);

	left = ui->widget_view(ctx, body, "settings.left");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_ROW_GAP | SK_UI_SP_PADDING | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_pt(260.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.row_gap = 6.0f;
	p.layout.padding.left = 8.0f;
	p.layout.padding.top = 8.0f;
	p.layout.padding.right = 8.0f;
	p.layout.padding.bottom = 8.0f;
	p.layout.flex_shrink = 0.0f;
	p.background_color = sk_ui_rgba(0.086f, 0.09f, 0.10f, 1.0f);
	(void)ui->node_set_inline_style(ctx, left, &p);

	state->search_input = ui->widget_search_input(ctx, left, state->search, "settings.search");
	(void)ui->text_input_set_on_change(ctx, state->search_input, settings_on_search_change, state);

	left_scroll = ui->widget_scroll_view(ctx, left, "settings.left.scroll");
	state->tree = ui->widget_tree(ctx, left_scroll, &state->tree_arr, "settings.tree");
	(void)ui->item_bind_set_on_activate(ctx, state->tree, settings_on_tree_activate, state);

	right = ui->widget_view(ctx, body, "settings.right");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, right, &p);
	state->right = right;

	settings_build_tree(state);
	settings_rebuild_right(state);
	state->last_synced_revision = state->selected_group + state->group_count + (u32)strlen(state->search);
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void settings_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	settings_class_state_t* cls = (settings_class_state_t*)window->user_data;
	settings_state_t* state;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (settings_state_t*)alloc->alloc(alloc->instance, sizeof(settings_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	state->open = 1;
	state->group = SK_EDITOR_SETTINGS_GROUP_EDITOR; /* C++ OpenAction default: Editor Settings */
	settings_apply_group_title(state);
	settings_seed(state);
	window->user_data = state;
}

static void settings_draw(sk_editor_window_t* window, i32* open) {
	settings_state_t* state = (settings_state_t*)window->user_data;
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
	win = ui->find_by_id(ctx, "sk.editor_window.settings");
	if (!sk_ui_node_is_valid(win) || !ui->node_alive(ctx, win)) {
		settings_build_ui(state, ui, ctx, root);
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	settings_sync(state);
	if (state->open == 0) {
		*open = 0; /* close button → close the window */
	}
}

static void settings_destroy(sk_editor_window_t* window) {
	settings_state_t* state = (settings_state_t*)window->user_data;
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

static sk_editor_window_t* settings_open(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 group) {
	sk_editor_window_t* window = sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_SETTINGS);
	settings_state_t* state = window != NULL ? (settings_state_t*)window->user_data : NULL;
	if (state != NULL) {
		state->group = group;
		settings_apply_group_title(state);
	}
	return window;
}

static sk_editor_window_t* settings_open_editor(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return settings_open(app_context, app_api, SK_EDITOR_SETTINGS_GROUP_EDITOR);
}

static sk_editor_window_t* settings_open_project(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return settings_open(app_context, app_api, SK_EDITOR_SETTINGS_GROUP_PROJECT);
}

static u32 settings_ops_get_group(const sk_editor_window_t* window) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	return state != NULL ? state->group : SK_EDITOR_SETTINGS_GROUP_EDITOR;
}

static const_chr_t settings_ops_get_title(const sk_editor_window_t* window) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	return state != NULL ? state->title : "Settings";
}

static u32 settings_ops_get_group_count(const sk_editor_window_t* window) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	return state != NULL ? state->group_count : 0u;
}

static const_chr_t settings_ops_group_label_at(const sk_editor_window_t* window, u32 index) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	if (state == NULL || index >= state->group_count) {
		return NULL;
	}
	return state->groups[index].label;
}

static u32 settings_ops_group_parent_at(const sk_editor_window_t* window, u32 index) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	if (state == NULL || index >= state->group_count) {
		return 0u;
	}
	return state->groups[index].parent;
}

static u32 settings_ops_entry_count(const sk_editor_window_t* window, u32 group_index) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	if (state == NULL || group_index >= state->group_count) {
		return 0u;
	}
	return state->groups[group_index].entry_count;
}

static const_chr_t settings_ops_entry_label_at(const sk_editor_window_t* window, u32 group_index, u32 entry_index) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	if (state == NULL || group_index >= state->group_count || entry_index >= state->groups[group_index].entry_count) {
		return NULL;
	}
	return state->groups[group_index].entries[entry_index].label;
}

static sk_editor_settings_entry_kind_t settings_ops_entry_kind_at(const sk_editor_window_t* window, u32 group_index, u32 entry_index) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	if (state == NULL || group_index >= state->group_count || entry_index >= state->groups[group_index].entry_count) {
		return SK_EDITOR_SETTINGS_ENTRY_BOOL;
	}
	return state->groups[group_index].entries[entry_index].kind;
}

static void settings_ops_get_entry_value(const sk_editor_window_t* window, u32 group_index, u32 entry_index, i32* out_bool, i32* out_int, f32* out_float, char* out_string,
										 u32 out_string_cap) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	const settings_entry_t* e;
	if (state == NULL || group_index >= state->group_count || entry_index >= state->groups[group_index].entry_count) {
		return;
	}
	e = &state->groups[group_index].entries[entry_index];
	if (out_bool != NULL) {
		*out_bool = e->b;
	}
	if (out_int != NULL) {
		*out_int = e->i;
	}
	if (out_float != NULL) {
		*out_float = e->f;
	}
	if (out_string != NULL && out_string_cap > 0u) {
		(void)snprintf(out_string, out_string_cap, "%s", e->s);
	}
}

static void settings_ops_set_entry_value(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 group_index, u32 entry_index, i32 set_bool,
										 i32 set_int, f32 set_float, const_chr_t set_string) {
	settings_state_t* state = (settings_state_t*)window->user_data;
	settings_entry_t* e;
	(void)app_context;
	(void)app_api;
	if (state == NULL || group_index >= state->group_count || entry_index >= state->groups[group_index].entry_count) {
		return;
	}
	e = &state->groups[group_index].entries[entry_index];
	e->b = set_bool;
	e->i = set_int;
	e->f = set_float;
	if (set_string != NULL) {
		(void)snprintf(e->s, sizeof(e->s), "%s", set_string);
	}
}

static u32 settings_ops_get_selected_group(const sk_editor_window_t* window) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	return state != NULL ? state->selected_group : 0u;
}

static void settings_ops_set_selected_group(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 group_index) {
	settings_state_t* state = (settings_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL && group_index < state->group_count) {
		state->selected_group = group_index;
	}
}

static const_chr_t settings_ops_get_search(const sk_editor_window_t* window) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	return state != NULL ? state->search : "";
}

static void settings_ops_set_search(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const_chr_t search) {
	settings_state_t* state = (settings_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		(void)snprintf(state->search, sizeof(state->search), "%s", search != NULL ? search : "");
	}
}

static const_chr_t settings_ops_layer_name(u32 layer) {
	static const char* names[SK_EDITOR_SETTINGS_LAYER_COUNT] = {"Default", "Player", "Enemy", "Environment", "Projectile", "Trigger", "UI", "Custom"};
	if (layer >= SK_EDITOR_SETTINGS_LAYER_COUNT) {
		return NULL;
	}
	return names[layer];
}

static i32 settings_ops_get_collision(const sk_editor_window_t* window, u32 layer_a, u32 layer_b) {
	const settings_state_t* state = (const settings_state_t*)window->user_data;
	if (state == NULL || layer_a >= SK_EDITOR_SETTINGS_LAYER_COUNT || layer_b >= SK_EDITOR_SETTINGS_LAYER_COUNT) {
		return 0;
	}
	return state->collision[layer_a][layer_b] != 0u ? 1 : 0;
}

static void settings_ops_set_collision(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 layer_a, u32 layer_b, i32 collide) {
	settings_state_t* state = (settings_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL && layer_a < SK_EDITOR_SETTINGS_LAYER_COUNT && layer_b < SK_EDITOR_SETTINGS_LAYER_COUNT) {
		state->collision[layer_a][layer_b] = (u8)(collide != 0 ? 1 : 0);
		state->collision[layer_b][layer_a] = (u8)(collide != 0 ? 1 : 0); /* symmetric (C++ GetCollisionMask both sides) */
	}
}

static const sk_editor_settings_ops_t settings_ops = {
	settings_open,
	settings_open_editor,
	settings_open_project,
	settings_ops_get_group,
	settings_ops_get_title,
	settings_ops_get_group_count,
	settings_ops_group_label_at,
	settings_ops_group_parent_at,
	settings_ops_entry_count,
	settings_ops_entry_label_at,
	settings_ops_entry_kind_at,
	settings_ops_get_entry_value,
	settings_ops_set_entry_value,
	settings_ops_get_selected_group,
	settings_ops_set_selected_group,
	settings_ops_get_search,
	settings_ops_set_search,
	settings_ops_layer_name,
	settings_ops_get_collision,
	settings_ops_set_collision,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_settings_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	settings_class_state_t* cls = settings_class(app_context, app_api);
	if (cls != NULL) {
		settings_window.user_data = cls;
		return;
	}
	cls = (settings_class_state_t*)alloc->alloc(alloc->instance, sizeof(settings_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_SETTINGS_STATE_TYPE_ID, cls);

	settings_window.type_id = SK_EDITOR_WINDOW_SETTINGS;
	settings_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &settings_window);
	app_api->add_impl(app_context, SK_EDITOR_SETTINGS_OPS_TYPE_ID, &settings_ops);
}

void sk_editor_settings_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	settings_class_state_t* cls = settings_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &settings_window);
	app_api->remove_impl(app_context, SK_EDITOR_SETTINGS_OPS_TYPE_ID, &settings_ops);
	app_api->set_api(app_context, SK_EDITOR_SETTINGS_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (settings_window.user_data == cls) {
		settings_window.user_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "test.h"

/* Ops-table path (no ui): open by group, seeded mock tree, entry read/write,
 * selection, search and the mocked collision matrix. */
SK_TEST(editor_settings_window_ops_mock_tree) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_settings_ops_t* ops;
	sk_editor_window_t* window;
	i32 b = 0;
	i32 iv = 0;
	f32 fv = 0.0f;
	char sv[SK_EDITOR_SETTINGS_LABEL_CAP];

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	TEST_ASSERT_NULL(sk_editor_settings_ops(app, boot.api));
	sk_editor_settings_register(app, boot.api);
	sk_editor_settings_register(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_SETTINGS_OPS_TYPE_ID));

	ops = sk_editor_settings_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	window = ops->open_editor_settings(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Settings", window->title); /* scaffold title; ops title differs */
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_SETTINGS));
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_NONE, window->dock_position);
	TEST_ASSERT_EQUAL_UINT(0u, window->workspace_mask);

	/* Title varies by opened group. */
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_SETTINGS_GROUP_EDITOR, ops->get_group(window));
	TEST_ASSERT_EQUAL_STRING("Editor Settings", ops->get_title(window));

	/* Seeded mock tree: 5 groups, "General" first. */
	TEST_ASSERT_EQUAL_UINT(5u, ops->get_group_count(window));
	TEST_ASSERT_EQUAL_STRING("General", ops->group_label_at(window, 0u));
	TEST_ASSERT_EQUAL_UINT(0u, ops->group_parent_at(window, 0u));

	/* Entry kinds + values on the General group. */
	TEST_ASSERT_EQUAL_UINT(3u, ops->entry_count(window, 0u));
	TEST_ASSERT_EQUAL_STRING("Project Name", ops->entry_label_at(window, 0u, 0u));
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_SETTINGS_ENTRY_STRING, (int)ops->entry_kind_at(window, 0u, 0u));
	ops->get_entry_value(window, 0u, 0u, NULL, NULL, NULL, sv, sizeof(sv));
	TEST_ASSERT_EQUAL_STRING("MyGame", sv);
	ops->get_entry_value(window, 0u, 2u, &b, NULL, NULL, NULL, 0u);
	TEST_ASSERT_EQUAL_INT(1, b); /* Auto Save on */

	/* Write one entry (mock, session). */
	ops->set_entry_value(app, boot.api, window, 0u, 0u, 0, 0, 0.0f, "SkoreDemo");
	ops->get_entry_value(window, 0u, 0u, NULL, NULL, NULL, sv, sizeof(sv));
	TEST_ASSERT_EQUAL_STRING("SkoreDemo", sv);

	/* Rendering group: int + float entries. */
	TEST_ASSERT_EQUAL_UINT(3u, ops->entry_count(window, 1u));
	ops->get_entry_value(window, 1u, 2u, NULL, &iv, NULL, NULL, 0u);
	TEST_ASSERT_EQUAL_INT(144, iv);
	ops->set_entry_value(app, boot.api, window, 1u, 2u, 0, 60, 0.0f, NULL);
	ops->get_entry_value(window, 1u, 2u, NULL, &iv, NULL, NULL, 0u);
	TEST_ASSERT_EQUAL_INT(60, iv);
	(void)fv;

	/* Selection + search. */
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_selected_group(window));
	ops->set_selected_group(app, boot.api, window, 2u);
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_selected_group(window));
	TEST_ASSERT_EQUAL_STRING("", ops->get_search(window));
	ops->set_search(app, boot.api, window, "Audio");
	TEST_ASSERT_EQUAL_STRING("Audio", ops->get_search(window));

	/* Collision matrix mock: symmetric set/get. */
	TEST_ASSERT_EQUAL_INT(1, ops->get_collision(window, 0u, 1u));
	ops->set_collision(app, boot.api, window, 0u, 1u, 0);
	TEST_ASSERT_EQUAL_INT(0, ops->get_collision(window, 0u, 1u));
	TEST_ASSERT_EQUAL_INT(0, ops->get_collision(window, 1u, 0u));
	TEST_ASSERT_EQUAL_STRING("Player", ops->layer_name(1u));

	/* Reopen with the other group retitles the window. */
	ops->open_project_settings(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_SETTINGS_GROUP_PROJECT, ops->get_group(window));
	TEST_ASSERT_EQUAL_STRING("Project Settings", ops->get_title(window));

	editor->window_close(app, boot.api, window);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_SETTINGS));
	TEST_ASSERT_NOT_NULL(sk_editor_settings_ops(app, boot.api));

	sk_editor_settings_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_settings_ops(app, boot.api));
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
