/**
 * @file console_window.c
 * @brief Console / log output window (APX-373): message list bound to the v2
 * logger, severity filtering + colouring, search, clear, auto-scroll.
 *
 * Port of main-branch Skore::ConsoleWindow (migration manifest §3.1) onto the
 * v2 shell. Public entry points (AddMessage / Clear / level visibility /
 * filter) live on the `sk_editor_console_ops_t` table registered with
 * add_impl — never exported as free symbols (window-table-pattern.md).
 *
 * The message list is bound to the v2 logger: opening the window registers an
 * sk_log_sink_t on the app logger context, so every sk_log_* line lands in a
 * fixed 256-line ring. Draw rebuilds the C++ toolbar surface on the window's
 * dock chrome (window_content): Clear, six severity checkboxes, Collapse,
 * Auto-scroll, the Filter text input and the colored scrolling region. The
 * retained model from console_panel.c (APX-139) is reused: chrome is built
 * once, log rows only when `version` bumps.
 */

#include "console_window.h"

#include "allocator.h"

#include <stdio.h>
#include <string.h>

#define SK_EDITOR_CONSOLE_STATE_TYPE_ID SK_TYPE_ID("sk.editor.console.state", 0xfe1c4a93d58b07c2ULL, 0x76e3a0f42b81d9c5ULL)

/* ------------------------------------------------------------------ */
/*  Internal types                                                    */
/* ------------------------------------------------------------------ */

typedef struct console_line_t {
	sk_logger_type_t level;
	char text[SK_EDITOR_CONSOLE_LINE_MAX];
	u32 count; /**< Collapse count when identical to previous line. */
} console_line_t;

typedef struct console_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;

	/* v2 logger sink (registered while the window is open). */
	sk_log_sink_t sink;
	i32 sink_registered;

	console_line_t lines[SK_EDITOR_CONSOLE_MAX_LINES];
	u32 line_count;
	u32 version;
	u32 last_synced_version;

	i32 show_level[6]; /* SK_LOGGER_TYPE_TRACE..FATAL */
	i32 collapse;
	i32 autoscroll;
	char last_filter[128];

	/* sk-ui handles; live only while a ui context hosts the window chrome. */
	sk_ui_node_t root;
	sk_ui_node_t clear_btn;
	sk_ui_node_t level_cb[6];
	sk_ui_node_t collapse_cb;
	sk_ui_node_t autoscroll_cb;
	sk_ui_node_t filter_input;
	sk_ui_node_t scroll;
	sk_ui_node_t content;
	sk_ui_node_t line_nodes[SK_EDITOR_CONSOLE_MAX_LINES];
	u32 line_node_count;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;
} console_state_t;

typedef struct console_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
} console_class_state_t;

static void console_init(sk_editor_window_t* window);
static void console_draw(sk_editor_window_t* window, i32* open);
static void console_destroy(sk_editor_window_t* window);

static sk_editor_window_t console_window = {
	.title = "Console",
	.dock_id = "sk.editor_window.console",
	.dock_position = SK_EDITOR_DOCK_BOTTOM_RIGHT,
	.order = 10,
	.workspace_mask = SK_EDITOR_WORKSPACE_ALL,
	.init = console_init,
	.draw = console_draw,
	.render = NULL,
	.destroy = console_destroy,
};

static console_class_state_t* console_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (console_class_state_t*)app_api->get_api(app_context, SK_EDITOR_CONSOLE_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Line ring + filter logic (ui-independent)                         */
/* ------------------------------------------------------------------ */

static i32 console_level_enabled(const console_state_t* state, sk_logger_type_t level) {
	i32 idx = (i32)level;
	if (idx < 0 || idx > 5) {
		return 1;
	}
	return state->show_level[idx] != 0;
}

static const_chr_t console_active_filter(const console_state_t* state) {
	if (state->ui != NULL && sk_ui_node_is_valid(state->filter_input)) {
		const_chr_t t = state->ui->text_input_get_text(state->ui_ctx, state->filter_input);
		if (t != NULL) {
			return t;
		}
	}
	return state->last_filter;
}

static i32 console_filter_pass(const console_state_t* state, const_chr_t text) {
	const_chr_t filter = console_active_filter(state);
	if (filter[0] == '\0') {
		return 1;
	}
	if (text == NULL) {
		return 0;
	}
	/* Case-sensitive substring match (ImGuiTextFilter subset; same as the
	 * APX-139 console panel). */
	return strstr(text, filter) != NULL ? 1 : 0;
}

static void console_compose_line(char* out, u32 cap, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	(void)snprintf(out, cap, "[%s] [%s] %s", sk_logger_type_name(level), logger_name != NULL ? logger_name : "", message != NULL ? message : "");
}

static void console_push_line(console_state_t* state, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	console_line_t* dest;
	char composed[SK_EDITOR_CONSOLE_LINE_MAX];

	console_compose_line(composed, sizeof(composed), level, logger_name, message);

	if (state->collapse && state->line_count > 0u) {
		console_line_t* last = &state->lines[state->line_count - 1u];
		if (last->level == level && strcmp(last->text, composed) == 0) {
			last->count += 1u;
			state->version += 1u;
			return;
		}
	}
	if (state->line_count >= SK_EDITOR_CONSOLE_MAX_LINES) {
		/* Drop oldest. */
		memmove(&state->lines[0], &state->lines[1], sizeof(console_line_t) * (SK_EDITOR_CONSOLE_MAX_LINES - 1u));
		state->line_count = SK_EDITOR_CONSOLE_MAX_LINES - 1u;
	}
	dest = &state->lines[state->line_count++];
	dest->level = level;
	dest->count = 1u;
	(void)snprintf(dest->text, sizeof(dest->text), "%s", composed);
	state->version += 1u;
}

static void console_sink_print(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	console_state_t* state = (console_state_t*)user_data;
	if (state != NULL) {
		console_push_line(state, level, logger_name, message);
	}
}

/* Number of rows the current severity + filter pass would render (collapse
 * merges identical consecutive lines into one row). */
static u32 console_compute_visible(const console_state_t* state) {
	u32 i;
	u32 count = 0u;
	for (i = 0u; i < state->line_count; ++i) {
		const console_line_t* ln = &state->lines[i];
		if (!console_level_enabled(state, ln->level)) {
			continue;
		}
		if (!console_filter_pass(state, ln->text)) {
			continue;
		}
		count += state->collapse ? 1u : ln->count;
	}
	return count;
}

/* ------------------------------------------------------------------ */
/*  Widget styles (same look as the APX-139 console panel)            */
/* ------------------------------------------------------------------ */

static void console_apply_panel_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR |
			 SK_UI_SP_BORDER_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.padding.left = 8.0f;
	p.layout.padding.top = 8.0f;
	p.layout.padding.right = 8.0f;
	p.layout.padding.bottom = 8.0f;
	p.layout.row_gap = 6.0f;
	p.background_color = sk_ui_rgba(0.10f, 0.11f, 0.14f, 0.96f);
	p.border_color = sk_ui_rgba(0.25f, 0.28f, 0.34f, 1.0f);
	p.layout.border.left = 1.0f;
	p.layout.border.top = 1.0f;
	p.layout.border.right = 1.0f;
	p.layout.border.bottom = 1.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void console_apply_row_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 6.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void console_apply_scroll_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.min_height = sk_ui_pt(80.0f);
	p.layout.padding.left = 4.0f;
	p.layout.padding.top = 4.0f;
	p.layout.padding.right = 4.0f;
	p.layout.padding.bottom = 4.0f;
	p.background_color = sk_ui_rgba(0.06f, 0.07f, 0.09f, 1.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void console_apply_filter_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.width = sk_ui_auto();
	p.layout.height = sk_ui_pt(24.0f);
	p.layout.flex_grow = 1.0f;
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static void console_apply_line_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, sk_logger_type_t level) {
	sk_ui_style_props_t p;
	sk_ui_color_t c;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK;
	/* Same severity palette as the C++ ConsoleWindow::Draw getLogColor. */
	switch (level) {
	case SK_LOGGER_TYPE_TRACE:
		c = sk_ui_rgba(0.50f, 0.50f, 0.50f, 1.0f);
		break;
	case SK_LOGGER_TYPE_DEBUG:
		c = sk_ui_rgba(0.80f, 0.80f, 0.80f, 1.0f);
		break;
	case SK_LOGGER_TYPE_INFO:
		c = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
		break;
	case SK_LOGGER_TYPE_WARN:
		c = sk_ui_rgba(1.0f, 0.80f, 0.60f, 1.0f);
		break;
	case SK_LOGGER_TYPE_ERROR:
		c = sk_ui_rgba(1.0f, 0.40f, 0.40f, 1.0f);
		break;
	case SK_LOGGER_TYPE_FATAL:
	default:
		c = sk_ui_rgba(0.90f, 0.10f, 0.10f, 1.0f);
		break;
	}
	p.color = c;
	p.font_size = 12.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

/* ------------------------------------------------------------------ */
/*  Retained log rows                                                  */
/* ------------------------------------------------------------------ */

static void console_clear_line_nodes(console_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	u32 i;
	for (i = 0u; i < state->line_node_count; ++i) {
		if (sk_ui_node_is_valid(state->line_nodes[i])) {
			(void)ui->node_destroy(state->ui_ctx, state->line_nodes[i]);
			state->line_nodes[i] = SK_UI_NODE_INVALID;
		}
	}
	state->line_node_count = 0u;
}

static void console_rebuild_lines(console_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	f32 content_h = 4.0f;

	console_clear_line_nodes(state);

	if (state->collapse) {
		for (i = 0u; i < state->line_count; ++i) {
			const console_line_t* ln = &state->lines[i];
			char buf[SK_EDITOR_CONSOLE_LINE_MAX + 16];
			sk_ui_node_t node;
			char id[32];

			if (!console_level_enabled(state, ln->level)) {
				continue;
			}
			if (!console_filter_pass(state, ln->text)) {
				continue;
			}
			if (ln->count > 1u) {
				(void)snprintf(buf, sizeof(buf), "%s [%u]", ln->text, ln->count);
			} else {
				(void)snprintf(buf, sizeof(buf), "%s", ln->text);
			}
			(void)snprintf(id, sizeof(id), "console-line-%u", state->line_node_count);
			node = ui->widget_label(ctx, state->content, buf, id);
			if (!sk_ui_node_is_valid(node)) {
				break;
			}
			console_apply_line_style(ui, ctx, node, ln->level);
			(void)ui->label_set_wrap(ctx, node, 0);
			state->line_nodes[state->line_node_count++] = node;
			content_h += 16.0f;
			if (state->line_node_count >= SK_EDITOR_CONSOLE_MAX_LINES) {
				break;
			}
		}
	} else {
		/* Non-collapse: expand multi-count lines as repeated rows (matches
		 * main ConsoleWindow when collapse is off). */
		for (i = 0u; i < state->line_count; ++i) {
			const console_line_t* ln = &state->lines[i];
			u32 rep;
			if (!console_level_enabled(state, ln->level)) {
				continue;
			}
			if (!console_filter_pass(state, ln->text)) {
				continue;
			}
			for (rep = 0u; rep < ln->count; ++rep) {
				sk_ui_node_t node;
				char id[32];
				(void)snprintf(id, sizeof(id), "console-line-%u", state->line_node_count);
				node = ui->widget_label(ctx, state->content, ln->text, id);
				if (!sk_ui_node_is_valid(node)) {
					break;
				}
				console_apply_line_style(ui, ctx, node, ln->level);
				(void)ui->label_set_wrap(ctx, node, 0);
				state->line_nodes[state->line_node_count++] = node;
				content_h += 16.0f;
				if (state->line_node_count >= SK_EDITOR_CONSOLE_MAX_LINES) {
					break;
				}
			}
			if (state->line_node_count >= SK_EDITOR_CONSOLE_MAX_LINES) {
				break;
			}
		}
	}

	state->last_synced_version = state->version;
	(void)ui->scroll_view_set_content_size(ctx, state->scroll, 400.0f, content_h);
	if (state->autoscroll) {
		(void)ui->scroll_view_set_scroll(ctx, state->scroll, 0.0f, content_h);
	}
	state->last_synced_version = state->version;
}

/* ------------------------------------------------------------------ */
/*  Widget callbacks                                                   */
/* ------------------------------------------------------------------ */

static void console_on_clear_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	console_state_t* state = (console_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	state->line_count = 0u;
	state->version += 1u;
	event->consumed = 1;
}

static void console_on_level_change(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	console_state_t* state = (console_state_t*)user;
	u32 i;
	(void)value;
	if (state == NULL) {
		return;
	}
	for (i = 0u; i < 6u; ++i) {
		if (sk_ui_node_eq(state->level_cb[i], node)) {
			state->show_level[i] = state->ui->checkbox_get_checked(ctx, node);
			state->version += 1u;
			return;
		}
	}
}

static void console_on_collapse_change(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	console_state_t* state = (console_state_t*)user;
	(void)ctx;
	(void)node;
	if (state == NULL) {
		return;
	}
	state->collapse = value != 0 ? 1 : 0;
	state->version += 1u;
}

static void console_on_autoscroll_change(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	console_state_t* state = (console_state_t*)user;
	(void)ctx;
	(void)node;
	if (state == NULL) {
		return;
	}
	state->autoscroll = value != 0 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Chrome build + per-frame sync                                      */
/* ------------------------------------------------------------------ */

/* Build the console chrome under @p parent (the window's dock content node):
 * Clear button, severity checkboxes, Collapse / Auto-scroll, Filter input
 * and the scrolling log region — the C++ ConsoleWindow::Draw surface. */
static void console_build_ui(console_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	static const char* level_names[6] = {"Trace", "Debug", "Info", "Warn", "Error", "Fatal"};
	static const char* level_ids[6] = {"console-lv-trace", "console-lv-debug", "console-lv-info", "console-lv-warn", "console-lv-error", "console-lv-fatal"};
	sk_ui_node_t toolbar;
	sk_ui_node_t levels;
	sk_ui_node_t options;
	sk_ui_node_t filter_row;
	sk_ui_node_callbacks_t cbs;
	u32 i;

	state->ui = ui;
	state->ui_ctx = ctx;

	state->root = ui->widget_view(ctx, parent, "console.content");
	if (!sk_ui_node_is_valid(state->root)) {
		return;
	}
	console_apply_panel_style(ui, ctx, state->root);

	/* Toolbar: Clear + severity checkboxes + Collapse / Auto-scroll. */
	toolbar = ui->widget_view(ctx, state->root, "console-toolbar");
	console_apply_row_style(ui, ctx, toolbar);
	state->clear_btn = ui->widget_button(ctx, toolbar, "Clear", "console-clear");
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = console_on_clear_click;
	cbs.user = state;
	(void)ui->node_set_callbacks(ctx, state->clear_btn, &cbs);

	levels = ui->widget_view(ctx, toolbar, "console-levels");
	console_apply_row_style(ui, ctx, levels);
	for (i = 0u; i < 6u; ++i) {
		state->level_cb[i] = ui->widget_checkbox(ctx, levels, state->show_level[i], level_ids[i]);
		(void)ui->widget_label(ctx, levels, level_names[i], NULL);
		(void)ui->checkbox_set_on_change(ctx, state->level_cb[i], console_on_level_change, state);
	}

	options = ui->widget_view(ctx, toolbar, "console-options");
	console_apply_row_style(ui, ctx, options);
	state->collapse_cb = ui->widget_checkbox(ctx, options, state->collapse, "console-collapse");
	(void)ui->widget_label(ctx, options, "Collapse", NULL);
	(void)ui->checkbox_set_on_change(ctx, state->collapse_cb, console_on_collapse_change, state);
	state->autoscroll_cb = ui->widget_checkbox(ctx, options, state->autoscroll, "console-autoscroll");
	(void)ui->widget_label(ctx, options, "Auto-scroll", NULL);
	(void)ui->checkbox_set_on_change(ctx, state->autoscroll_cb, console_on_autoscroll_change, state);

	/* Filter (search) row. */
	filter_row = ui->widget_view(ctx, state->root, "console-filter-row");
	console_apply_row_style(ui, ctx, filter_row);
	(void)ui->widget_label(ctx, filter_row, "Filter", NULL);
	state->filter_input = ui->widget_text_input(ctx, filter_row, state->last_filter, "console-filter");
	console_apply_filter_style(ui, ctx, state->filter_input);

	/* Scroll log region. */
	state->scroll = ui->widget_scroll_view(ctx, state->root, "console-scroll");
	console_apply_scroll_style(ui, ctx, state->scroll);
	state->content = ui->scroll_view_content(ctx, state->scroll);
}

/* Re-read control state and rebuild log rows when the version bumped. */
static i32 console_sync(console_state_t* state) {
	const_chr_t filter;
	i32 need_rebuild;
	if (state == NULL || state->ui == NULL) {
		return -1;
	}
	if (sk_ui_node_is_valid(state->collapse_cb)) {
		i32 v = state->ui->checkbox_get_checked(state->ui_ctx, state->collapse_cb) != 0 ? 1 : 0;
		if (v != state->collapse) {
			state->collapse = v;
			state->version += 1u;
		}
	}
	if (sk_ui_node_is_valid(state->autoscroll_cb)) {
		state->autoscroll = state->ui->checkbox_get_checked(state->ui_ctx, state->autoscroll_cb) != 0 ? 1 : 0;
	}
	{
		u32 i;
		for (i = 0u; i < 6u; ++i) {
			if (sk_ui_node_is_valid(state->level_cb[i])) {
				i32 v = state->ui->checkbox_get_checked(state->ui_ctx, state->level_cb[i]) != 0 ? 1 : 0;
				if (v != state->show_level[i]) {
					state->show_level[i] = v;
					state->version += 1u;
				}
			}
		}
	}
	filter = console_active_filter(state);
	if (strcmp(state->last_filter, filter) != 0) {
		(void)snprintf(state->last_filter, sizeof(state->last_filter), "%s", filter);
		state->version += 1u;
	}
	need_rebuild = state->version != state->last_synced_version ? 1 : 0;
	if (need_rebuild) {
		console_rebuild_lines(state);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void console_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	console_class_state_t* cls = (console_class_state_t*)window->user_data;
	console_state_t* state;
	const sk_logger_api_t* logger_api;
	sk_logger_context_t* log_ctx;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (console_state_t*)alloc->alloc(alloc->instance, sizeof(console_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	/* Match main defaults: Info+ on, Trace/Debug off. */
	state->show_level[SK_LOGGER_TYPE_TRACE] = 0;
	state->show_level[SK_LOGGER_TYPE_DEBUG] = 0;
	state->show_level[SK_LOGGER_TYPE_INFO] = 1;
	state->show_level[SK_LOGGER_TYPE_WARN] = 1;
	state->show_level[SK_LOGGER_TYPE_ERROR] = 1;
	state->show_level[SK_LOGGER_TYPE_FATAL] = 1;
	state->collapse = 1;
	state->autoscroll = 1;

	/* Bind to the v2 logger: every sk_log_* line is forwarded to the ring. */
	state->sink.user_data = state;
	state->sink.print = console_sink_print;
	logger_api = cls->app_api->logger_api(cls->app_context);
	log_ctx = cls->app_api->logger_context(cls->app_context);
	if (logger_api != NULL && log_ctx != NULL && logger_api->add_sink(log_ctx, &state->sink) == 0) {
		state->sink_registered = 1;
	}
	window->user_data = state;
}

static void console_draw(sk_editor_window_t* window, i32* open) {
	console_state_t* state = (console_state_t*)window->user_data;
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
	/* Draw contract: the window may clear *open to request close. */
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	if (ctx == NULL) {
		return; /* no ui host (plain unit tests keep the ops/logic path only) */
	}
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ui == NULL) {
		return;
	}
	chrome = ui->find_by_id(ctx, window->dock_id);
	if (!sk_ui_node_is_valid(chrome)) {
		return; /* not docked in the active workspace yet */
	}
	content = ui->editor_window_content(ctx, chrome);
	if (!sk_ui_node_is_valid(content)) {
		return;
	}
	/* Rebuild the chrome when the dock teardown destroyed it (workspace
	 * switch / tab close) — stable ids survive node recycling. */
	root = ui->find_by_id(ctx, "console.content");
	if (!sk_ui_node_is_valid(root)) {
		console_build_ui(state, ui, ctx, content);
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	(void)console_sync(state);
}

static void console_destroy(sk_editor_window_t* window) {
	console_state_t* state = (console_state_t*)window->user_data;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_log_sink_t sink;
	sk_editor_workspace_t* ws;
	sk_ui_context_t* ctx;
	const sk_ui_api_t* ui;
	if (state == NULL) {
		return;
	}
	if (state->sink_registered) {
		sink.user_data = state;
		sink.print = console_sink_print;
		(void)state->app_api->logger_api(state->app_context)->remove_sink(state->app_api->logger_context(state->app_context), &sink);
		state->sink_registered = 0;
	}
	/* Tear down the chrome with the shell's ui context (valid while the
	 * shell owns the frame; dock teardown may have destroyed the nodes
	 * already — node_alive guards that case). */
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

static sk_editor_window_t* console_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_CONSOLE);
}

static console_state_t* console_state_of(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	sk_editor_window_t* window = sk_editor_window_by_type(app_context, app_api, SK_EDITOR_WINDOW_CONSOLE);
	if (window == NULL) {
		return NULL;
	}
	return (console_state_t*)window->user_data;
}

static void console_ops_add_message(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	console_state_t* state = console_state_of(app_context, app_api);
	if (state != NULL) {
		console_push_line(state, level, logger_name, message);
	}
}

static void console_ops_clear(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	console_state_t* state = console_state_of(app_context, app_api);
	if (state != NULL) {
		state->line_count = 0u;
		state->version += 1u;
	}
}

static u32 console_ops_line_count(const sk_editor_window_t* window) {
	const console_state_t* state = (const console_state_t*)window->user_data;
	return state != NULL ? state->line_count : 0u;
}

static u32 console_ops_visible_count(const sk_editor_window_t* window) {
	const console_state_t* state = (const console_state_t*)window->user_data;
	return state != NULL ? console_compute_visible(state) : 0u;
}

static void console_ops_set_level_visible(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_logger_type_t level, i32 visible) {
	console_state_t* state = console_state_of(app_context, app_api);
	i32 idx = (i32)level;
	if (state == NULL || idx < 0 || idx > 5) {
		return;
	}
	state->show_level[idx] = visible != 0 ? 1 : 0;
	state->version += 1u;
	if (state->ui != NULL && sk_ui_node_is_valid(state->level_cb[idx])) {
		(void)state->ui->checkbox_set_checked(state->ui_ctx, state->level_cb[idx], state->show_level[idx]);
	}
}

static i32 console_ops_is_level_visible(const sk_editor_window_t* window, sk_logger_type_t level) {
	const console_state_t* state = (const console_state_t*)window->user_data;
	i32 idx = (i32)level;
	if (state == NULL || idx < 0 || idx > 5) {
		return 1;
	}
	return state->show_level[idx];
}

static void console_ops_set_filter(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t filter) {
	console_state_t* state = console_state_of(app_context, app_api);
	const_chr_t f = filter != NULL ? filter : "";
	if (state == NULL) {
		return;
	}
	(void)snprintf(state->last_filter, sizeof(state->last_filter), "%s", f);
	state->version += 1u;
	if (state->ui != NULL && sk_ui_node_is_valid(state->filter_input)) {
		(void)state->ui->text_input_set_text(state->ui_ctx, state->filter_input, state->last_filter);
	}
}

static const sk_editor_console_ops_t console_ops = {
	console_open,
	console_ops_add_message,
	console_ops_clear,
	console_ops_line_count,
	console_ops_visible_count,
	console_ops_set_level_visible,
	console_ops_is_level_visible,
	console_ops_set_filter,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_console_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	console_class_state_t* cls = console_class(app_context, app_api);
	if (cls != NULL) {
		console_window.user_data = cls;
		return;
	}
	cls = (console_class_state_t*)alloc->alloc(alloc->instance, sizeof(console_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_CONSOLE_STATE_TYPE_ID, cls);

	console_window.type_id = SK_EDITOR_WINDOW_CONSOLE;
	console_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &console_window);
	app_api->add_impl(app_context, SK_EDITOR_CONSOLE_OPS_TYPE_ID, &console_ops);
}

void sk_editor_console_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	console_class_state_t* cls = console_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &console_window);
	app_api->remove_impl(app_context, SK_EDITOR_CONSOLE_OPS_TYPE_ID, &console_ops);
	app_api->set_api(app_context, SK_EDITOR_CONSOLE_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (console_window.user_data == cls) {
		console_window.user_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "filesystem.h"
#include "path.h"
#include "test.h"

static i32 console_test_plugin_path(const_chr_t name, char* out, u32 cap) {
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

static const sk_ui_api_t* console_test_load_ui(sk_app_context_t* app_ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-ui.dylib";
#else
	const_chr_t name = "sk-ui.so";
#endif
	if (console_test_plugin_path(name, path, (u32)sizeof(path)) == 0) {
		(void)app_api->load_plugin(app_ctx, path);
	}
	return (const sk_ui_api_t*)app_api->get_api(app_ctx, SK_UI_API_TYPE_ID);
}

/* Ops-table path (no ui): AddMessage/Clear/level visibility/filter on the
 * registered table, plus the v2 logger sink binding — emit real sk_log_*
 * lines at every severity and watch them land in the ring and pass the
 * severity filter. */
SK_TEST(editor_console_window_ops_and_logger_sink) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_console_ops_t* ops;
	sk_editor_window_t* window;
	sk_logger_t* log;
	const sk_logger_api_t* logger_api = boot.api->logger_api(app);
	sk_logger_context_t* log_ctx = boot.api->logger_context(app);

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	TEST_ASSERT_NULL(sk_editor_console_ops(app, boot.api));
	sk_editor_console_register(app, boot.api);
	sk_editor_console_register(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_CONSOLE_OPS_TYPE_ID));

	ops = sk_editor_console_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);
	TEST_ASSERT_EQUAL_PTR(&console_ops, ops);
	TEST_ASSERT_NOT_NULL(ops->add_message);
	TEST_ASSERT_NOT_NULL(ops->clear);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Console", window->title);
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_CONSOLE));
	/* Default dock metadata matches the APX-330 scaffold row. */
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_BOTTOM_RIGHT, window->dock_position);
	TEST_ASSERT_EQUAL_INT(10, window->order);
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_SCENE));

	/* Main defaults: Trace/Debug hidden, Info+ shown. */
	TEST_ASSERT_EQUAL_INT(0, ops->is_level_visible(window, SK_LOGGER_TYPE_TRACE));
	TEST_ASSERT_EQUAL_INT(0, ops->is_level_visible(window, SK_LOGGER_TYPE_DEBUG));
	TEST_ASSERT_EQUAL_INT(1, ops->is_level_visible(window, SK_LOGGER_TYPE_INFO));
	TEST_ASSERT_EQUAL_INT(1, ops->is_level_visible(window, SK_LOGGER_TYPE_FATAL));

	/* AddMessage at every severity. */
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_TRACE, "test", "trace line");
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_DEBUG, "test", "debug line");
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_INFO, "test", "info line");
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_WARN, "test", "warn line");
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_ERROR, "test", "error line");
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_FATAL, "test", "fatal line");
	TEST_ASSERT_EQUAL_UINT(6u, ops->line_count(window));
	/* Trace + Debug are hidden by default → 4 rows visible. */
	TEST_ASSERT_EQUAL_UINT(4u, ops->visible_count(window));

	/* Severity filtering: show Trace, hide Info. */
	ops->set_level_visible(app, boot.api, SK_LOGGER_TYPE_TRACE, 1);
	ops->set_level_visible(app, boot.api, SK_LOGGER_TYPE_INFO, 0);
	TEST_ASSERT_EQUAL_INT(1, ops->is_level_visible(window, SK_LOGGER_TYPE_TRACE));
	TEST_ASSERT_EQUAL_INT(0, ops->is_level_visible(window, SK_LOGGER_TYPE_INFO));
	TEST_ASSERT_EQUAL_UINT(4u, ops->visible_count(window)); /* trace, warn, error, fatal */

	/* Search: substring filter narrows the visible set. */
	ops->set_filter(app, boot.api, "warn");
	TEST_ASSERT_EQUAL_UINT(1u, ops->visible_count(window));
	ops->set_filter(app, boot.api, "");
	TEST_ASSERT_EQUAL_UINT(4u, ops->visible_count(window));

	/* The v2 logger sink path: sk_log_* lines land in the ring. */
	log = logger_api->create_logger(log_ctx, "console-test");
	sk_log_trace(logger_api, log, "sink trace %d", 1);
	sk_log_debug(logger_api, log, "sink debug %d", 2);
	sk_log_error(logger_api, log, "sink error %d", 3);
	TEST_ASSERT_EQUAL_UINT(9u, ops->line_count(window));
	TEST_ASSERT_EQUAL_UINT(6u, ops->visible_count(window)); /* +trace +error (debug hidden, info hidden) */
	logger_api->destroy_logger(log_ctx, log);

	/* Clear wipes both stored and visible. */
	ops->clear(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(0u, ops->line_count(window));
	TEST_ASSERT_EQUAL_UINT(0u, ops->visible_count(window));

	/* Collapse default-on merges consecutive identical lines in the ring. */
	ops->set_level_visible(app, boot.api, SK_LOGGER_TYPE_INFO, 1); /* re-enable for the dup check */
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_INFO, "test", "dup");
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_INFO, "test", "dup");
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_INFO, "test", "dup");
	TEST_ASSERT_EQUAL_UINT(1u, ops->line_count(window)); /* merged by collapse */
	TEST_ASSERT_EQUAL_UINT(1u, ops->visible_count(window));

	/* Close drops the instance; the ops table (class-level) survives. */
	editor->window_close(app, boot.api, window);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_CONSOLE));
	TEST_ASSERT_NOT_NULL(sk_editor_console_ops(app, boot.api));

	/* add_message with no open window is a safe no-op. */
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_INFO, "test", "dropped");

	sk_editor_console_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_console_ops(app, boot.api));
	sk_app_shutdown(app);
}

/* ui-hosted path: the dock chrome carries the console UI, real sk_log_*
 * lines at every severity render with the severity filter, the Filter
 * search box narrows the visible rows, and Clear / close/reopen work. */
SK_TEST(editor_console_window_ui_dock_chrome_and_filter) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_console_ops_t* ops;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_editor_window_t* window;
	sk_logger_t* log;
	const sk_logger_api_t* logger_api = boot.api->logger_api(app);
	sk_logger_context_t* log_ctx = boot.api->logger_context(app);
	sk_ui_node_t filter;
	sk_ui_node_t err_cb;
	i32 open = 1;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	ui = console_test_load_ui(app, boot.api);
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
	/* Real console impl first so its ops table / window win window_open. */
	sk_editor_console_register(app, boot.api);
	sk_editor_windows_register_impls(app, boot.api);
	ops = sk_editor_console_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	/* Host the Scene dockspace on the test context (shell path). */
	ws = editor->workspace_create(app, boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(ws);
	sk_editor_workspace_set_dock_context(ws, ctx);
	sk_editor_workspace_switch(ws);
	editor->dockspace_init(ws);
	window = editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_CONSOLE);
	TEST_ASSERT_NOT_NULL(window);

	/* Draw builds the chrome on the dock content node. */
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(1, open);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-clear")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-filter")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-lv-error")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-lv-trace")));

	/* Emit one line at each severity through the v2 logger. */
	log = logger_api->create_logger(log_ctx, "console-ui");
	sk_log_trace(logger_api, log, "trace line");
	sk_log_debug(logger_api, log, "debug line");
	sk_log_info(logger_api, log, "info line");
	sk_log_warn(logger_api, log, "warn line");
	sk_log_error(logger_api, log, "error line");
	sk_log_fatal(logger_api, log, "fatal line");
	window->draw(window, &open);
	/* The ring also captured the dockspace-create DEBUG line emitted by the
	 * ui.dock plugin right after the sink registered — the console is bound
	 * to the whole v2 logger. DEBUG is hidden by default, so the visible
	 * count below stays exact. */
	TEST_ASSERT_TRUE(ops->line_count(window) >= 6u);
	/* Trace + Debug hidden by default → 4 rows. */
	TEST_ASSERT_EQUAL_UINT(4u, ops->visible_count(window));

	/* Search: type into the Filter input, draw re-syncs the rows. */
	filter = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-filter");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(filter));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, filter, "warn"));
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_UINT(1u, ops->visible_count(window));

	/* Severity toggle through the checkbox: hide Error (search cleared). */
	err_cb = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-lv-error");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(err_cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_checked(ctx, err_cb, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, filter, ""));
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(0, ops->is_level_visible(window, SK_LOGGER_TYPE_ERROR));
	TEST_ASSERT_EQUAL_UINT(3u, ops->visible_count(window)); /* trace hidden, debug hidden, error hidden */

	/* Collapse off expands duplicate rows (render count, ring unchanged). */
	{
		sk_ui_node_t collapse_cb = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-collapse");
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(collapse_cb));
		TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_checked(ctx, collapse_cb, 0));
	}
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_INFO, "console-ui", "dup");
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_INFO, "console-ui", "dup");
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_UINT(5u, ops->visible_count(window)); /* 3 rows + 2 dup rows (info visible) */

	/* Clear button path: ops->clear is the same call the button runs. */
	ops->clear(app, boot.api);
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_UINT(0u, ops->visible_count(window));

	/* Close (dock tab teardown) then reopen: chrome + sink come back. */
	editor->window_close(app, boot.api, window);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_CONSOLE));
	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	window->draw(window, &open);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-clear")));

	logger_api->destroy_logger(log_ctx, log);
	editor->window_close(app, boot.api, window);
	editor->workspace_destroy(ws);
	ui->context_destroy(ctx);
	ui->shutdown();
	sk_editor_console_shutdown(app, boot.api);
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
