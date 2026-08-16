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

#ifdef SK_TESTS
#include "skore_test_font_ttf.h"
#endif

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

	/* Last scroll content box applied to the log body (APX-390): the rebuild
	 * writes a per-row estimate, console_sync_scroll_size refines it from the
	 * laid-out rows; only apply when it moved so steady frames stay clean. */
	f32 last_content_w;
	f32 last_content_h;
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
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_FLEX_WRAP;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 4.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_shrink = 0.0f;
	p.layout.flex_wrap = SK_UI_FLEX_WRAP;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

/* Rows nested inside the toolbar (severity `levels`, Collapse/Auto-scroll
 * `options`): size from their content instead of the 100% toolbar width, so
 * Clear + severity pairs + Collapse/Auto-scroll share the toolbar row and a
 * wrapped line keeps every checkbox+label pair intact (APX-382).
 * flex_shrink 0 keeps the measured widths; flex_wrap degrades to whole-item
 * lines on narrow hosts instead of compressing glyphs. */
static void console_apply_toolbar_row_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_FLEX_WRAP;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 4.0f;
	p.layout.flex_shrink = 0.0f;
	p.layout.flex_wrap = SK_UI_FLEX_WRAP;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

/* Toolbar label at the 13px size the C++ ConsoleWindow used (ImGui default).
 * The shell binds the host fonts for layout measurement, so the label box is
 * the real glyph advance of its text — never a wider estimate that would wrap
 * Trace/Debug/Info/Warn/Error/Fatal mid-word ('Debu g', 'War n'). flex_shrink
 * 0 is kept as intent so no solver pass can compress the measured width. */
static sk_ui_node_t console_add_toolbar_label(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	sk_ui_style_props_t p;
	sk_ui_node_t label = ui->widget_label(ctx, parent, text, id);
	if (!sk_ui_node_is_valid(label)) {
		return label;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_SHRINK | SK_UI_SP_FONT_SIZE;
	p.layout.flex_shrink = 0.0f;
	p.font_size = 13.0f;
	(void)ui->node_merge_inline_style(ctx, label, &p);
	return label;
}

static void console_apply_scroll_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.min_height = sk_ui_pt(48.0f);
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
			/* APX-390: wrap long lines at the panel width so the tail of a long
			 * message is readable (was cut at the panel edge, e.g. the Clay
			 * limitation WARN). A row can now be taller than one line; the scroll
			 * body height is refined after layout in console_sync_scroll_size. */
			(void)ui->label_set_wrap(ctx, node, 1);
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
				/* APX-390: wrap long lines at the panel width (see collapse path). */
				(void)ui->label_set_wrap(ctx, node, 1);
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
	/* Per-row estimate (16px/line): the layout fonts are only bound during the
	 * shell's layout pass, so wrapped row heights are not measurable here.
	 * console_sync_scroll_size refines this box from the laid-out rows on the
	 * next frames; auto-scroll is set to the estimate so the newest line is
	 * reachable even before the first refinement lands. */
	state->last_content_w = 400.0f;
	state->last_content_h = content_h;
	(void)ui->scroll_view_set_content_size(ctx, state->scroll, 400.0f, content_h);
	if (state->autoscroll) {
		(void)ui->scroll_view_set_scroll(ctx, state->scroll, 0.0f, content_h);
	}
	state->last_synced_version = state->version;
}

/* Keep the log body's scroll content sized to its laid-out rows (APX-390).
 * Log lines wrap at the panel width, so a row can be taller than one line;
 * rebuild writes a per-row estimate, this refines the content box from the
 * previous frame's measured row stack (same pattern as the Debugger
 * statistics host, APX-389) and, when Auto-scroll is on, lands on the newest
 * line. Runs every frame; applies only when the box moved so steady-state
 * frames don't re-dirty the subtree. */
static void console_sync_scroll_size(console_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t content;
	sk_ui_rect_t r;
	f32 w = 400.0f;
	f32 h = 4.0f;
	if (ui == NULL || !sk_ui_node_is_valid(state->scroll)) {
		return;
	}
	content = ui->scroll_view_content(ctx, state->scroll);
	/* Refine the height from the last laid-out row once layout landed.
	 * Freshly rebuilt nodes have no rect until the next layout pass; before
	 * that the rebuild's per-row estimate stands, so do not touch the box. */
	if (state->line_node_count > 0u) {
		sk_ui_node_t last = state->line_nodes[state->line_node_count - 1u];
		sk_ui_rect_t lr;
		sk_ui_rect_t cr;
		if (!sk_ui_node_is_valid(last) || !sk_ui_node_is_valid(content) || ui->node_get_abs_rect(ctx, last, &lr, NULL) != 0 || lr.height <= 0.5f ||
			ui->node_get_abs_rect(ctx, content, &cr, NULL) != 0) {
			return;
		}
		h = (lr.y + lr.height) - cr.y + 4.0f; /* panel bottom padding */
	}
	/* Content width tracks the scroll viewport so wrapped lines break at the
	 * panel edge instead of at the fixed 400px estimate (4px padding each
	 * side of the log body). */
	if (ui->node_get_abs_rect(ctx, state->scroll, &r, NULL) == 0 && r.width > 1.0f) {
		w = r.width - 8.0f;
	}
	if (w >= state->last_content_w - 0.5f && w <= state->last_content_w + 0.5f && h >= state->last_content_h - 0.5f && h <= state->last_content_h + 0.5f) {
		return;
	}
	(void)ui->scroll_view_set_content_size(ctx, state->scroll, w, h);
	/* Mirror POINT sizes into the content node's inline style so style_resolve
	 * (which runs whenever a descendant turns style-dirty) keeps the box. */
	if (sk_ui_node_is_valid(content)) {
		sk_ui_style_props_t p;
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
		p.layout.width = sk_ui_pt(w);
		p.layout.height = sk_ui_pt(h);
		(void)ui->node_merge_inline_style(ctx, content, &p);
	}
	state->last_content_w = w;
	state->last_content_h = h;
	if (state->autoscroll) {
		/* Clamped to the bottom by ui_scroll_clamp: newest line fully on screen
		 * even when the last row wraps to several lines. */
		(void)ui->scroll_view_set_scroll(ctx, state->scroll, 0.0f, h);
	}
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
	static const char* level_lbl_ids[6] = {"console-lv-trace-lbl", "console-lv-debug-lbl", "console-lv-info-lbl",
										   "console-lv-warn-lbl",  "console-lv-error-lbl", "console-lv-fatal-lbl"};
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
	console_apply_toolbar_row_style(ui, ctx, levels);
	for (i = 0u; i < 6u; ++i) {
		state->level_cb[i] = ui->widget_checkbox(ctx, levels, state->show_level[i], level_ids[i]);
		(void)console_add_toolbar_label(ui, ctx, levels, level_names[i], level_lbl_ids[i]);
		(void)ui->checkbox_set_on_change(ctx, state->level_cb[i], console_on_level_change, state);
	}

	options = ui->widget_view(ctx, toolbar, "console-options");
	console_apply_toolbar_row_style(ui, ctx, options);
	state->collapse_cb = ui->widget_checkbox(ctx, options, state->collapse, "console-collapse");
	(void)console_add_toolbar_label(ui, ctx, options, "Collapse", "console-collapse-lbl");
	(void)ui->checkbox_set_on_change(ctx, state->collapse_cb, console_on_collapse_change, state);
	state->autoscroll_cb = ui->widget_checkbox(ctx, options, state->autoscroll, "console-autoscroll");
	(void)console_add_toolbar_label(ui, ctx, options, "Auto-scroll", "console-autoscroll-lbl");
	(void)ui->checkbox_set_on_change(ctx, state->autoscroll_cb, console_on_autoscroll_change, state);

	/* Filter (search) row. */
	filter_row = ui->widget_view(ctx, state->root, "console-filter-row");
	console_apply_row_style(ui, ctx, filter_row);
	(void)console_add_toolbar_label(ui, ctx, filter_row, "Filter", "console-filter-lbl");
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
	/* Refine the log body box from the laid-out rows (wrapped rows can be
	 * taller than one line) and keep auto-scroll on the newest line. */
	console_sync_scroll_size(state);
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
	/* Seed Info+ lines so the dock body is populated at rest. Startup
	 * sk_log_* lines fire before this sink is registered (window open is
	 * after dockspace init), so without a seed the ring is empty. */
	console_push_line(state, SK_LOGGER_TYPE_INFO, "Editor", "Scene workspace ready");
	console_push_line(state, SK_LOGGER_TYPE_INFO, "Assets", "Mock project: Assets/Scenes, Assets/Textures");
	console_push_line(state, SK_LOGGER_TYPE_INFO, "Project", "No project attached; using mock content");
	console_push_line(state, SK_LOGGER_TYPE_INFO, "Renderer", "Placeholder viewport texture bound");
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
	/* 4 seed Info lines + 6 AddMessage. */
	TEST_ASSERT_EQUAL_UINT(10u, ops->line_count(window));
	/* Seed Info (4) + Info/Warn/Error/Fatal (4); Trace + Debug hidden. */
	TEST_ASSERT_EQUAL_UINT(8u, ops->visible_count(window));

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
	TEST_ASSERT_EQUAL_UINT(13u, ops->line_count(window));
	TEST_ASSERT_EQUAL_UINT(6u, ops->visible_count(window)); /* +trace +error (debug hidden, info hidden incl. seed) */
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
	TEST_ASSERT_TRUE(ops->line_count(window) >= 10u);
	/* 4 seed Info + info/warn/error/fatal. Trace + Debug hidden. */
	TEST_ASSERT_EQUAL_UINT(8u, ops->visible_count(window));

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
	TEST_ASSERT_EQUAL_UINT(7u, ops->visible_count(window)); /* 4 seed + info/warn/fatal; error/trace/debug hidden */

	/* Collapse off expands duplicate rows (render count, ring unchanged). */
	{
		sk_ui_node_t collapse_cb = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-collapse");
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(collapse_cb));
		TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_checked(ctx, collapse_cb, 0));
	}
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_INFO, "console-ui", "dup");
	ops->add_message(app, boot.api, SK_LOGGER_TYPE_INFO, "console-ui", "dup");
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_UINT(9u, ops->visible_count(window)); /* 7 rows + 2 dup rows (info visible) */

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

	/* APX-382: with the layout fonts bound (mirrors sk_editor_shell_frame),
	 * the toolbar measures each severity label from its text: every label is
	 * one line tall, stays inside the toolbar and clear of the next
	 * checkbox, and Clear / Collapse / Auto-scroll share the row inside the
	 * 1280x720 window (the old 100%-wide rows pushed the options column
	 * off-screen past x=1320 and under-measured labels wrapped mid-word). */
	{
		static const char* cb_ids[6] = {"console-lv-trace", "console-lv-debug", "console-lv-info", "console-lv-warn", "console-lv-error", "console-lv-fatal"};
		static const char* lbl_ids[6] = {"console-lv-trace-lbl", "console-lv-debug-lbl", "console-lv-info-lbl",
										 "console-lv-warn-lbl",	 "console-lv-error-lbl", "console-lv-fatal-lbl"};
		sk_ui_font_system_t* lfonts;
		sk_ui_font_t* lfont;
		sk_ui_rect_t toolbar;
		sk_ui_rect_t clear_r;
		sk_ui_rect_t options;
		u32 i;
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
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-toolbar"), &toolbar, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-clear"), &clear_r, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-options"), &options, NULL));
		/* Clear, severity pairs and Collapse/Auto-scroll share the toolbar row. */
		TEST_ASSERT_TRUE(clear_r.y >= toolbar.y - 0.5f);
		TEST_ASSERT_TRUE(options.y >= toolbar.y - 0.5f);
		TEST_ASSERT_TRUE(options.y + options.height <= toolbar.y + toolbar.height + 0.5f);
		/* The options row is inside the window (was pushed off-screen at x>=1320). */
		TEST_ASSERT_TRUE(options.x >= toolbar.x - 0.5f);
		TEST_ASSERT_TRUE(options.x + options.width <= 1280.0f - 4.0f);
		for (i = 0u; i < 6u; ++i) {
			sk_ui_rect_t cb;
			sk_ui_rect_t lbl;
			TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, cb_ids[i]), &cb, NULL));
			TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, lbl_ids[i]), &lbl, NULL));
			/* Label sized from its text: one line tall, inside the toolbar,
			 * and clear of the next checkbox (no mid-word wrap). */
			TEST_ASSERT_TRUE(lbl.height < 20.0f);
			TEST_ASSERT_TRUE(lbl.x >= toolbar.x - 0.5f);
			TEST_ASSERT_TRUE(lbl.x + lbl.width <= toolbar.x + toolbar.width + 0.5f);
			TEST_ASSERT_TRUE(lbl.y >= toolbar.y - 0.5f);
			TEST_ASSERT_TRUE(lbl.y + lbl.height <= toolbar.y + toolbar.height + 0.5f);
			if (i + 1u < 6u) {
				sk_ui_rect_t ncb;
				TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, cb_ids[i + 1u]), &ncb, NULL));
				TEST_ASSERT_TRUE(lbl.x + lbl.width <= ncb.x + 0.5f);
			}
		}

		/* APX-390: long log lines wrap at the panel width (label_set_wrap 1)
		 * instead of being cut at the panel edge, and the log body scroll box
		 * is sized to the laid-out rows so Auto-scroll lands on the newest
		 * line even when a row wraps to several lines (the vision gate reads
		 * the full Clay-limitation WARN on frame 01).
		 *
		 * Note: this host drives raw ui->layout() cycles (sk_editor_shell_frame
		 * on the sandbox drives identical steps plus per-frame font binds); the
		 * wrap LAYOUT height can vary with the layout-font binding, so the
		 * assertions here pin the contract that matters: wrap is enabled, the
		 * scroll body reaches past the last laid-out row (nothing is cut at the
		 * panel edge) and Auto-scroll lands the newest line fully on screen. */
		{
			sk_ui_node_t scroll = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-scroll");
			sk_ui_node_t content;
			sk_ui_node_t last;
			sk_ui_rect_t cr;
			sk_ui_rect_t lr;
			sk_ui_rect_t hr;
			sk_ui_prop_value_t pv;
			u32 n;
			u32 k;
			f32 content_h = 0.0f;
			f32 scroll_y = -1.0f;
			TEST_ASSERT_TRUE(sk_ui_node_is_valid(scroll));
			content = ui->scroll_view_content(ctx, scroll);
			TEST_ASSERT_TRUE(sk_ui_node_is_valid(content));
			/* Flush any late logger lines (the Clay limitation WARN fires once
			 * per context on the first absolute-position layout), then seed a
			 * body taller than the leaf so auto-scroll is exercised. */
			ops->clear(app, boot.api);
			window->draw(window, &open);
			TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
			TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1280.0f, 720.0f));
			ops->clear(app, boot.api);
			for (k = 0u; k < 30u; ++k) {
				char msg[300];
				/* Copy of the real frame-01 WARN (clay_adapter): 'element' id varies
				 * per row so collapse does not merge them. */
				(void)snprintf(msg, sizeof(msg),
							   "element 'sk.editor.dock.ws1/%u' Clay limitation: absolute positioning mapped via Clay floating; constraints may differ (kept best-effort mapping)",
							   k);
				ops->add_message(app, boot.api, SK_LOGGER_TYPE_WARN, "clay_adapter", msg);
			}
			/* Rebuild + refine converge over a few frames (rebuild writes the
			 * per-row estimate; console_sync_scroll_size measures after layout). */
			for (k = 0u; k < 3u; ++k) {
				window->draw(window, &open);
				TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
				TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1280.0f, 720.0f));
				TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
			}
			n = ui->node_child_count(ctx, content);
			TEST_ASSERT_TRUE(n >= 30u);
			last = ui->node_child_at(ctx, content, n - 1u);
			TEST_ASSERT_TRUE(sk_ui_node_is_valid(last));
			/* Wrap is on for log rows (was hard-disabled, clipping the tail at
			 * the panel edge). */
			memset(&pv, 0, sizeof(pv));
			TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, last, "wrap", &pv));
			TEST_ASSERT_EQUAL_INT(1, pv.data.i32_value);
			TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, last, &lr, NULL));
			TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, content, &cr, NULL));
			TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, scroll, &hr, NULL));
			memset(&pv, 0, sizeof(pv));
			TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, scroll, "content_height", &pv));
			content_h = pv.data.f32_value;
			/* The scroll body box reaches past the last laid-out row. */
			TEST_ASSERT_TRUE(content_h >= (lr.y + lr.height) - cr.y - 0.5f);
			TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, scroll, NULL, &scroll_y));
			/* Body taller than the leaf: auto-scroll actually scrolled... */
			TEST_ASSERT_TRUE(content_h > hr.height + 0.5f);
			TEST_ASSERT_TRUE(scroll_y > 0.5f);
			/* ...and pinned to the bottom so the newest (possibly wrapped) line
			 * is fully on screen. */
			TEST_ASSERT_TRUE(content_h - scroll_y <= hr.height + 1.5f);
			TEST_ASSERT_TRUE((lr.y + lr.height) - cr.y - scroll_y <= hr.height + 1.5f);
		}
		ui->set_layout_fonts(ctx, NULL, NULL);
		ui->font_system_destroy(lfonts);
	}
	logger_api->destroy_logger(log_ctx, log);
	editor->window_close(app, boot.api, window);
	editor->workspace_destroy(ws);
	ui->context_destroy(ctx);
	ui->shutdown();
	sk_editor_console_shutdown(app, boot.api);
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
