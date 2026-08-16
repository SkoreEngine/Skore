/**
 * @file history_window.c
 * @brief History window (APX-374): mock undo/redo list on the v2 shell.
 *
 * Port of main-branch Skore::HistoryWindow (migration manifest §3.4): a
 * Scene-workspace RightTop window that renders the editor undo/redo stacks
 * as a flat list (C++ `ImGuiDrawUndoRedoActions` — Undo scopes most recent
 * first, then Redo). The v2 editor has no undo/redo stack yet (shell
 * toolbar Undo/Redo are inert), so the stacks are session MOCK data the ops
 * table seeds / pushes / pops / clears; the same entries will carry the real
 * undo stack once it lands.
 *
 * Public entry points go through the `sk_editor_history_ops_t` table
 * registered with add_impl (window-table-pattern.md §7); the shell's
 * Window/History menu routes through it.
 */

#include "history_window.h"

#include "allocator.h"

#include <stdio.h>
#include <string.h>

#define SK_EDITOR_HISTORY_STATE_TYPE_ID SK_TYPE_ID("sk.editor.history.state", 0x8a1f6b2c4d5e7089ULL, 0x31b5c7d9e0f2a4c6ULL)

/* ------------------------------------------------------------------ */
/*  Internal types                                                    */
/* ------------------------------------------------------------------ */

typedef struct history_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;

	/* Mock undo/redo stacks (session-only; C++ Editor.cpp module arrays). */
	char undo[SK_EDITOR_HISTORY_MAX_ENTRIES][SK_EDITOR_HISTORY_NAME_CAP];
	u32 undo_count;
	char redo[SK_EDITOR_HISTORY_MAX_ENTRIES][SK_EDITOR_HISTORY_NAME_CAP];
	u32 redo_count;

	/* sk-ui handles; live only while a ui context hosts the window chrome. */
	sk_ui_node_t root;
	sk_ui_node_t undo_scroll;
	sk_ui_node_t undo_rows;
	sk_ui_node_t redo_scroll;
	sk_ui_node_t redo_rows;
	u32 last_synced_revision;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;
} history_state_t;

typedef struct history_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
} history_class_state_t;

static void history_init(sk_editor_window_t* window);
static void history_draw(sk_editor_window_t* window, i32* open);
static void history_destroy(sk_editor_window_t* window);

static sk_editor_window_t history_window = {
	.title = "History",
	.dock_id = "sk.editor_window.history",
	.dock_position = SK_EDITOR_DOCK_RIGHT_TOP,
	.order = 10,
	.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE),
	.init = history_init,
	.draw = history_draw,
	.render = NULL,
	.destroy = history_destroy,
};

static history_class_state_t* history_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (history_class_state_t*)app_api->get_api(app_context, SK_EDITOR_HISTORY_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Stack logic (ui-independent)                                      */
/* ------------------------------------------------------------------ */

static i32 history_push(char stack[][SK_EDITOR_HISTORY_NAME_CAP], u32* count, const_chr_t name) {
	if (*count >= SK_EDITOR_HISTORY_MAX_ENTRIES) {
		/* Drop the oldest (C++ undoActions is a fixed-cap array too). */
		memmove(&stack[0], &stack[1], sizeof(char[SK_EDITOR_HISTORY_NAME_CAP]) * (SK_EDITOR_HISTORY_MAX_ENTRIES - 1u));
		*count = SK_EDITOR_HISTORY_MAX_ENTRIES - 1u;
	}
	(void)snprintf(stack[*count], SK_EDITOR_HISTORY_NAME_CAP, "%s", name != NULL ? name : "Action");
	*count += 1u;
	return 0;
}

static const_chr_t history_pop(char stack[][SK_EDITOR_HISTORY_NAME_CAP], u32* count, char* out, u32 cap) {
	if (*count == 0u) {
		return NULL;
	}
	*count -= 1u;
	if (out != NULL && cap > 0u) {
		(void)snprintf(out, cap, "%s", stack[*count]);
		return out;
	}
	return stack[*count];
}

/* ------------------------------------------------------------------ */
/*  Widget styles                                                     */
/* ------------------------------------------------------------------ */

static void history_apply_section_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_FLEX_DIRECTION;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.flex_shrink = 1.0f;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void history_apply_header_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR;
	p.color = sk_ui_rgba(0.78f, 0.80f, 0.86f, 1.0f);
	p.font_size = 13.0f;
	p.layout.padding.left = 6.0f;
	p.layout.padding.top = 4.0f;
	p.layout.padding.right = 6.0f;
	p.layout.padding.bottom = 4.0f;
	p.background_color = sk_ui_rgba(0.13f, 0.14f, 0.17f, 1.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void history_apply_row_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_PADDING;
	p.color = sk_ui_rgba(0.90f, 0.91f, 0.94f, 1.0f);
	p.font_size = 12.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_shrink = 0.0f;
	p.layout.padding.left = 6.0f;
	p.layout.padding.top = 2.0f;
	p.layout.padding.right = 6.0f;
	p.layout.padding.bottom = 2.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

/* ------------------------------------------------------------------ */
/*  Chrome build + per-frame sync                                     */
/* ------------------------------------------------------------------ */

/* Rebuild one stack section's rows under its scroll content. The section
 * owns a stable `rows` view; node_destroy frees it recursively so a fresh
 * one can be built in place (scroll_view_content returns the stored content
 * node and never recreates it). */
static void history_rebuild_section(history_state_t* state, char stack[][SK_EDITOR_HISTORY_NAME_CAP], u32 count, sk_ui_node_t scroll, sk_ui_node_t* out_rows,
									const_chr_t id_prefix) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t content;
	sk_ui_node_t rows;
	char id[64];
	f32 h = 4.0f;
	u32 i;

	content = ui->scroll_view_content(ctx, scroll);
	/* One layout surface per window chrome: two NULL-id scroll_content nodes
	 * would map to the same Clay id (widget+sibling-index hash). Give each
	 * content a stable unique id so the ids stay distinct. */
	{
		char content_id[64];
		(void)snprintf(content_id, sizeof(content_id), "%s.content", id_prefix);
		(void)ui->node_set_id(ctx, content, content_id);
	}
	if (sk_ui_node_is_valid(*out_rows) && ui->node_alive(ctx, *out_rows)) {
		(void)ui->node_destroy(ctx, *out_rows);
	}
	rows = ui->widget_view(ctx, content, id_prefix);
	*out_rows = rows;
	{
		sk_ui_style_props_t p;
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH;
		p.layout.flex_direction = SK_UI_FLEX_COLUMN;
		p.layout.width = sk_ui_percent(100.0f);
		(void)ui->node_set_inline_style(ctx, rows, &p);
	}

	if (count == 0u) {
		(void)snprintf(id, sizeof(id), "%s.empty", id_prefix);
		(void)ui->widget_text_disabled(ctx, rows, "No actions.", id);
		return;
	}
	for (i = 0u; i < count; ++i) {
		sk_ui_node_t node;
		(void)snprintf(id, sizeof(id), "%s.row.%u", id_prefix, i);
		node = ui->widget_label(ctx, rows, stack[i], id);
		if (!sk_ui_node_is_valid(node)) {
			break;
		}
		history_apply_row_style(ui, ctx, node);
		(void)ui->label_set_wrap(ctx, node, 0);
		h += 18.0f;
	}
	(void)ui->scroll_view_set_content_size(ctx, scroll, 400.0f, h);
}

static void history_rebuild(history_state_t* state) {
	if (state->ui == NULL || !sk_ui_node_is_valid(state->root)) {
		return;
	}
	if (!sk_ui_node_is_valid(state->undo_scroll)) {
		state->undo_scroll = state->ui->find_by_id(state->ui_ctx, "history.undo.scroll");
	}
	if (!sk_ui_node_is_valid(state->redo_scroll)) {
		state->redo_scroll = state->ui->find_by_id(state->ui_ctx, "history.redo.scroll");
	}
	history_rebuild_section(state, state->undo, state->undo_count, state->undo_scroll, &state->undo_rows, "history.undo");
	history_rebuild_section(state, state->redo, state->redo_count, state->redo_scroll, &state->redo_rows, "history.redo");
}

static i32 history_sync(history_state_t* state) {
	u32 revision;
	if (state == NULL || state->ui == NULL) {
		return -1;
	}
	revision = state->undo_count + state->redo_count;
	if (revision != state->last_synced_revision) {
		history_rebuild(state);
		state->last_synced_revision = revision;
	}
	return 0;
}

/* Build the chrome under @p parent (the window's dock content node): an
 * Undo section (most recent first) and a Redo section — the C++
 * ImGuiDrawUndoRedoActions surface. */
static void history_build_ui(history_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	sk_ui_node_t undo_section;
	sk_ui_node_t redo_section;
	sk_ui_node_t undo_header;
	sk_ui_node_t redo_header;

	state->ui = ui;
	state->ui_ctx = ctx;

	state->root = ui->widget_view(ctx, parent, "history.content");
	if (!sk_ui_node_is_valid(state->root)) {
		return;
	}
	{
		sk_ui_style_props_t p;
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_ROW_GAP | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR;
		p.layout.flex_direction = SK_UI_FLEX_COLUMN;
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.height = sk_ui_percent(100.0f);
		p.layout.row_gap = 6.0f;
		p.layout.padding.left = 6.0f;
		p.layout.padding.top = 6.0f;
		p.layout.padding.right = 6.0f;
		p.layout.padding.bottom = 6.0f;
		p.background_color = sk_ui_rgba(0.10f, 0.11f, 0.14f, 0.96f);
		(void)ui->node_set_inline_style(ctx, state->root, &p);
	}

	/* Undo section. */
	undo_section = ui->widget_view(ctx, state->root, "history.undo.section");
	history_apply_section_style(ui, ctx, undo_section);
	undo_header = ui->widget_label(ctx, undo_section, "Undo", "history.undo.header");
	history_apply_header_style(ui, ctx, undo_header);
	state->undo_scroll = ui->widget_scroll_view(ctx, undo_section, "history.undo.scroll");
	history_apply_section_style(ui, ctx, state->undo_scroll);

	/* Redo section. */
	redo_section = ui->widget_view(ctx, state->root, "history.redo.section");
	history_apply_section_style(ui, ctx, redo_section);
	redo_header = ui->widget_label(ctx, redo_section, "Redo", "history.redo.header");
	history_apply_header_style(ui, ctx, redo_header);
	state->redo_scroll = ui->widget_scroll_view(ctx, redo_section, "history.redo.scroll");
	history_apply_section_style(ui, ctx, state->redo_scroll);

	history_rebuild(state);
	state->last_synced_revision = state->undo_count + state->redo_count;
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void history_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	history_class_state_t* cls = (history_class_state_t*)window->user_data;
	history_state_t* state;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (history_state_t*)alloc->alloc(alloc->instance, sizeof(history_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;

	/* Seed the mock session history (v2 has no undo stack yet). */
	history_push(state->undo, &state->undo_count, "Create Entity");
	history_push(state->undo, &state->undo_count, "Move Entity");
	history_push(state->undo, &state->undo_count, "Delete Entity");
	history_push(state->undo, &state->undo_count, "Rename Asset");
	window->user_data = state;
}

static void history_draw(sk_editor_window_t* window, i32* open) {
	history_state_t* state = (history_state_t*)window->user_data;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_ui_node_t chrome;
	sk_ui_node_t content;
	sk_ui_node_t root;
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
	chrome = ui->find_by_id(ctx, window->dock_id);
	if (!sk_ui_node_is_valid(chrome)) {
		return;
	}
	content = ui->editor_window_content(ctx, chrome);
	if (!sk_ui_node_is_valid(content)) {
		return;
	}
	root = ui->find_by_id(ctx, "history.content");
	if (!sk_ui_node_is_valid(root)) {
		history_build_ui(state, ui, ctx, content);
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	(void)history_sync(state);
}

static void history_destroy(sk_editor_window_t* window) {
	history_state_t* state = (history_state_t*)window->user_data;
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
	if (ctx != NULL && ui != NULL && sk_ui_node_is_valid(state->root) && ui->node_alive(ctx, state->root)) {
		(void)ui->node_destroy(ctx, state->root);
	}
	state->root = SK_UI_NODE_INVALID;
	alloc->free(alloc->instance, state);
	window->user_data = NULL;
}

/* ------------------------------------------------------------------ */
/*  Ops table (add_impl; never direct symbols)                        */
/* ------------------------------------------------------------------ */

static sk_editor_window_t* history_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_HISTORY);
}

static history_state_t* history_state_of(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	sk_editor_window_t* window = sk_editor_window_by_type(app_context, app_api, SK_EDITOR_WINDOW_HISTORY);
	if (window == NULL) {
		return NULL;
	}
	return (history_state_t*)window->user_data;
}

static void history_ops_push_undo(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t name) {
	history_state_t* state = history_state_of(app_context, app_api);
	if (state != NULL) {
		(void)history_push(state->undo, &state->undo_count, name);
	}
}

static void history_ops_push_redo(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t name) {
	history_state_t* state = history_state_of(app_context, app_api);
	if (state != NULL) {
		(void)history_push(state->redo, &state->redo_count, name);
	}
}

static const_chr_t history_ops_pop_undo(sk_app_context_t* app_context, const sk_app_api_t* app_api, char* out, u32 cap) {
	history_state_t* state = history_state_of(app_context, app_api);
	if (state != NULL) {
		return history_pop(state->undo, &state->undo_count, out, cap);
	}
	return NULL;
}

static const_chr_t history_ops_pop_redo(sk_app_context_t* app_context, const sk_app_api_t* app_api, char* out, u32 cap) {
	history_state_t* state = history_state_of(app_context, app_api);
	if (state != NULL) {
		return history_pop(state->redo, &state->redo_count, out, cap);
	}
	return NULL;
}

static void history_ops_clear(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	history_state_t* state = history_state_of(app_context, app_api);
	if (state != NULL) {
		state->undo_count = 0u;
		state->redo_count = 0u;
	}
}

static u32 history_ops_undo_count(const sk_editor_window_t* window) {
	const history_state_t* state = (const history_state_t*)window->user_data;
	return state != NULL ? state->undo_count : 0u;
}

static u32 history_ops_redo_count(const sk_editor_window_t* window) {
	const history_state_t* state = (const history_state_t*)window->user_data;
	return state != NULL ? state->redo_count : 0u;
}

static const_chr_t history_ops_undo_name_at(const sk_editor_window_t* window, u32 index) {
	const history_state_t* state = (const history_state_t*)window->user_data;
	if (state == NULL || index >= state->undo_count) {
		return NULL;
	}
	/* index 0 = most recent; the stack stores oldest first. */
	return state->undo[state->undo_count - 1u - index];
}

static const_chr_t history_ops_redo_name_at(const sk_editor_window_t* window, u32 index) {
	const history_state_t* state = (const history_state_t*)window->user_data;
	if (state == NULL || index >= state->redo_count) {
		return NULL;
	}
	return state->redo[state->redo_count - 1u - index];
}

static const sk_editor_history_ops_t history_ops = {
	history_open,	   history_ops_push_undo,  history_ops_push_redo,  history_ops_pop_undo,	 history_ops_pop_redo,
	history_ops_clear, history_ops_undo_count, history_ops_redo_count, history_ops_undo_name_at, history_ops_redo_name_at,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_history_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	history_class_state_t* cls = history_class(app_context, app_api);
	if (cls != NULL) {
		history_window.user_data = cls;
		return;
	}
	cls = (history_class_state_t*)alloc->alloc(alloc->instance, sizeof(history_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_HISTORY_STATE_TYPE_ID, cls);

	history_window.type_id = SK_EDITOR_WINDOW_HISTORY;
	history_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &history_window);
	app_api->add_impl(app_context, SK_EDITOR_HISTORY_OPS_TYPE_ID, &history_ops);
}

void sk_editor_history_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	history_class_state_t* cls = history_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &history_window);
	app_api->remove_impl(app_context, SK_EDITOR_HISTORY_OPS_TYPE_ID, &history_ops);
	app_api->set_api(app_context, SK_EDITOR_HISTORY_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (history_window.user_data == cls) {
		history_window.user_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "test.h"

/* Ops-table path (no ui): open, seeded mock history, push/pop/clear and the
 * listing queries — index 0 is the most recent scope. */
SK_TEST(editor_history_window_ops_mock_stacks) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_history_ops_t* ops;
	sk_editor_window_t* window;
	char buf[SK_EDITOR_HISTORY_NAME_CAP];

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	TEST_ASSERT_NULL(sk_editor_history_ops(app, boot.api));
	sk_editor_history_register(app, boot.api);
	sk_editor_history_register(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_HISTORY_OPS_TYPE_ID));

	ops = sk_editor_history_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("History", window->title);
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_HISTORY));
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_RIGHT_TOP, window->dock_position);
	TEST_ASSERT_EQUAL_INT(10, window->order);
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_SCENE));
	TEST_ASSERT_FALSE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_GRAPH));

	/* Seeded mock history: 4 undo scopes, most recent first. */
	TEST_ASSERT_EQUAL_UINT(4u, ops->undo_count(window));
	TEST_ASSERT_EQUAL_UINT(0u, ops->redo_count(window));
	TEST_ASSERT_EQUAL_STRING("Rename Asset", ops->undo_name_at(window, 0u));
	TEST_ASSERT_EQUAL_STRING("Create Entity", ops->undo_name_at(window, 3u));

	/* Push/pop both stacks. */
	ops->push_undo(app, boot.api, "Change Layer");
	TEST_ASSERT_EQUAL_UINT(5u, ops->undo_count(window));
	TEST_ASSERT_EQUAL_STRING("Change Layer", ops->undo_name_at(window, 0u));
	ops->push_redo(app, boot.api, "Redo Rename");
	TEST_ASSERT_EQUAL_UINT(1u, ops->redo_count(window));
	TEST_ASSERT_EQUAL_STRING("Redo Rename", ops->redo_name_at(window, 0u));

	TEST_ASSERT_EQUAL_STRING("Change Layer", ops->pop_undo(app, boot.api, buf, sizeof(buf)));
	TEST_ASSERT_EQUAL_UINT(4u, ops->undo_count(window));
	TEST_ASSERT_EQUAL_STRING("Redo Rename", ops->pop_redo(app, boot.api, buf, sizeof(buf)));
	TEST_ASSERT_EQUAL_UINT(0u, ops->redo_count(window));
	TEST_ASSERT_NULL(ops->pop_redo(app, boot.api, buf, sizeof(buf)));

	/* Clear wipes both stacks. */
	ops->clear(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(0u, ops->undo_count(window));
	TEST_ASSERT_EQUAL_UINT(0u, ops->redo_count(window));

	editor->window_close(app, boot.api, window);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_HISTORY));
	TEST_ASSERT_NOT_NULL(sk_editor_history_ops(app, boot.api));

	sk_editor_history_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_history_ops(app, boot.api));
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
