/**
 * @file console_panel.c
 * @brief Retained sk-ui port of main ConsoleWindow (APX-139).
 */

#include "console_panel.h"

#include "allocator.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Internal types                                                             */
/* -------------------------------------------------------------------------- */

typedef struct console_line_t {
	sk_logger_type_t level;
	char text[SK_EDITOR_CONSOLE_LINE_MAX];
	u32 count; /**< Collapse count when identical to previous. */
} console_line_t;

struct sk_editor_console_panel_t {
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t clear_btn;
	sk_ui_node_t filter_input;
	sk_ui_node_t level_cb[6];
	sk_ui_node_t collapse_cb;
	sk_ui_node_t autoscroll_cb;
	sk_ui_node_t scroll;
	sk_ui_node_t content;
	sk_ui_node_t line_nodes[SK_EDITOR_CONSOLE_MAX_LINES];
	u32 line_node_count;

	console_line_t lines[SK_EDITOR_CONSOLE_MAX_LINES];
	u32 line_count;
	u32 version;
	u32 last_synced_version;
	u32 visible_count;

	i32 show_level[6]; /* TRACE..FATAL */
	i32 collapse;
	i32 autoscroll;
	i32 sink_registered;
	char last_filter[128];
};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

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
	switch (level) {
	case SK_LOGGER_TYPE_TRACE:
		c = sk_ui_rgba(0.55f, 0.55f, 0.55f, 1.0f);
		break;
	case SK_LOGGER_TYPE_DEBUG:
		c = sk_ui_rgba(0.80f, 0.80f, 0.80f, 1.0f);
		break;
	case SK_LOGGER_TYPE_INFO:
		c = sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f);
		break;
	case SK_LOGGER_TYPE_WARN:
		c = sk_ui_rgba(1.0f, 0.80f, 0.55f, 1.0f);
		break;
	case SK_LOGGER_TYPE_ERROR:
		c = sk_ui_rgba(1.0f, 0.40f, 0.40f, 1.0f);
		break;
	case SK_LOGGER_TYPE_FATAL:
	default:
		c = sk_ui_rgba(0.95f, 0.15f, 0.15f, 1.0f);
		break;
	}
	p.color = c;
	p.font_size = 12.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static i32 console_level_enabled(const sk_editor_console_panel_t* panel, sk_logger_type_t level) {
	i32 idx = (i32)level;
	if (idx < 0 || idx > 5) {
		return 1;
	}
	return panel->show_level[idx] != 0;
}

static i32 console_filter_pass(const sk_editor_console_panel_t* panel, const_chr_t text) {
	const_chr_t filter;
	if (panel->ui == NULL || !sk_ui_node_is_valid(panel->filter_input)) {
		return 1;
	}
	filter = panel->ui->text_input_get_text(panel->ctx, panel->filter_input);
	if (filter == NULL || filter[0] == '\0') {
		return 1;
	}
	if (text == NULL) {
		return 0;
	}
	/* Case-sensitive substring match (ImGuiTextFilter subset; good enough for v1). */
	return strstr(text, filter) != NULL ? 1 : 0;
}

static void console_clear_line_nodes(sk_editor_console_panel_t* panel) {
	const sk_ui_api_t* ui = panel->ui;
	u32 i;
	for (i = 0u; i < panel->line_node_count; ++i) {
		if (sk_ui_node_is_valid(panel->line_nodes[i])) {
			(void)ui->node_destroy(panel->ctx, panel->line_nodes[i]);
			panel->line_nodes[i] = SK_UI_NODE_INVALID;
		}
	}
	panel->line_node_count = 0u;
	panel->visible_count = 0u;
}

static void console_rebuild_lines(sk_editor_console_panel_t* panel) {
	const sk_ui_api_t* ui = panel->ui;
	sk_ui_context_t* ctx = panel->ctx;
	u32 i;
	f32 content_h = 4.0f;

	console_clear_line_nodes(panel);

	if (panel->collapse) {
		for (i = 0u; i < panel->line_count; ++i) {
			const console_line_t* ln = &panel->lines[i];
			char buf[SK_EDITOR_CONSOLE_LINE_MAX + 16];
			sk_ui_node_t node;
			char id[32];

			if (!console_level_enabled(panel, ln->level)) {
				continue;
			}
			if (!console_filter_pass(panel, ln->text)) {
				continue;
			}
			if (ln->count > 1u) {
				(void)snprintf(buf, sizeof(buf), "%s [%u]", ln->text, ln->count);
			} else {
				(void)snprintf(buf, sizeof(buf), "%s", ln->text);
			}
			(void)snprintf(id, sizeof(id), "console-line-%u", panel->line_node_count);
			node = ui->widget_label(ctx, panel->content, buf, id);
			if (!sk_ui_node_is_valid(node)) {
				break;
			}
			console_apply_line_style(ui, ctx, node, ln->level);
			(void)ui->label_set_wrap(ctx, node, 0);
			panel->line_nodes[panel->line_node_count++] = node;
			content_h += 16.0f;
			if (panel->line_node_count >= SK_EDITOR_CONSOLE_MAX_LINES) {
				break;
			}
		}
	} else {
		/* Non-collapse: expand multi-count lines as repeated rows (matches main when collapse off). */
		for (i = 0u; i < panel->line_count; ++i) {
			const console_line_t* ln = &panel->lines[i];
			u32 rep;
			if (!console_level_enabled(panel, ln->level)) {
				continue;
			}
			if (!console_filter_pass(panel, ln->text)) {
				continue;
			}
			for (rep = 0u; rep < ln->count; ++rep) {
				sk_ui_node_t node;
				char id[32];
				(void)snprintf(id, sizeof(id), "console-line-%u", panel->line_node_count);
				node = ui->widget_label(ctx, panel->content, ln->text, id);
				if (!sk_ui_node_is_valid(node)) {
					break;
				}
				console_apply_line_style(ui, ctx, node, ln->level);
				(void)ui->label_set_wrap(ctx, node, 0);
				panel->line_nodes[panel->line_node_count++] = node;
				content_h += 16.0f;
				if (panel->line_node_count >= SK_EDITOR_CONSOLE_MAX_LINES) {
					break;
				}
			}
			if (panel->line_node_count >= SK_EDITOR_CONSOLE_MAX_LINES) {
				break;
			}
		}
	}

	panel->visible_count = panel->line_node_count;
	(void)ui->scroll_view_set_content_size(ctx, panel->scroll, 400.0f, content_h);
	if (panel->autoscroll) {
		f32 max_y = content_h;
		(void)ui->scroll_view_set_scroll(ctx, panel->scroll, 0.0f, max_y);
	}
	panel->last_synced_version = panel->version;
}

static void console_push_internal(sk_editor_console_panel_t* panel, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	console_line_t* dest;
	char composed[SK_EDITOR_CONSOLE_LINE_MAX];
	const_chr_t name = logger_name != NULL ? logger_name : "";
	const_chr_t msg = message != NULL ? message : "";

	(void)snprintf(composed, sizeof(composed), "[%s] [%s] %s", sk_logger_type_name(level), name, msg);

	if (panel->collapse && panel->line_count > 0u) {
		console_line_t* last = &panel->lines[panel->line_count - 1u];
		if (last->level == level && strcmp(last->text, composed) == 0) {
			last->count += 1u;
			panel->version += 1u;
			return;
		}
	}

	if (panel->line_count >= SK_EDITOR_CONSOLE_MAX_LINES) {
		/* Drop oldest. */
		memmove(&panel->lines[0], &panel->lines[1], sizeof(console_line_t) * (SK_EDITOR_CONSOLE_MAX_LINES - 1u));
		panel->line_count = SK_EDITOR_CONSOLE_MAX_LINES - 1u;
	}
	dest = &panel->lines[panel->line_count++];
	dest->level = level;
	dest->count = 1u;
	(void)snprintf(dest->text, sizeof(dest->text), "%s", composed);
	panel->version += 1u;
}

static void console_sink_print(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	sk_editor_console_panel_t* panel = (sk_editor_console_panel_t*)user_data;
	if (panel == NULL) {
		return;
	}
	console_push_internal(panel, level, logger_name, message);
}

static void console_on_clear_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	sk_editor_console_panel_t* panel = (sk_editor_console_panel_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	sk_editor_console_panel_clear(panel);
	event->consumed = 1;
}

static void console_on_level_change(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	sk_editor_console_panel_t* panel = (sk_editor_console_panel_t*)user;
	u32 i;
	(void)ctx;
	(void)value;
	if (panel == NULL) {
		return;
	}
	for (i = 0u; i < 6u; ++i) {
		if (sk_ui_node_eq(panel->level_cb[i], node)) {
			panel->show_level[i] = panel->ui->checkbox_get_checked(panel->ctx, node);
			panel->version += 1u;
			return;
		}
	}
}

static void console_on_collapse_change(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	sk_editor_console_panel_t* panel = (sk_editor_console_panel_t*)user;
	(void)ctx;
	(void)node;
	if (panel == NULL) {
		return;
	}
	panel->collapse = value != 0 ? 1 : 0;
	panel->version += 1u;
}

static void console_on_autoscroll_change(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	sk_editor_console_panel_t* panel = (sk_editor_console_panel_t*)user;
	(void)ctx;
	(void)node;
	if (panel == NULL) {
		return;
	}
	panel->autoscroll = value != 0 ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

sk_editor_console_panel_t* sk_editor_console_panel_create(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_console_panel_t* panel;
	sk_ui_node_t toolbar;
	sk_ui_node_t levels;
	sk_ui_node_t options;
	sk_ui_node_t filter_row;
	sk_ui_node_callbacks_t cbs;
	sk_log_sink_t sink;
	static const char* level_names[6] = {"Trace", "Debug", "Info", "Warn", "Error", "Fatal"};
	static const char* level_ids[6] = {"console-lv-trace", "console-lv-debug", "console-lv-info", "console-lv-warn", "console-lv-error", "console-lv-fatal"};
	u32 i;

	if (ui == NULL || ctx == NULL) {
		return NULL;
	}

	panel = (sk_editor_console_panel_t*)alloc->alloc(alloc->instance, sizeof(sk_editor_console_panel_t));
	if (panel == NULL) {
		return NULL;
	}
	memset(panel, 0, sizeof(*panel));
	panel->ui = ui;
	panel->ctx = ctx;
	/* Match main defaults: Info+ on, Trace/Debug off. */
	panel->show_level[SK_LOGGER_TYPE_TRACE] = 0;
	panel->show_level[SK_LOGGER_TYPE_DEBUG] = 0;
	panel->show_level[SK_LOGGER_TYPE_INFO] = 1;
	panel->show_level[SK_LOGGER_TYPE_WARN] = 1;
	panel->show_level[SK_LOGGER_TYPE_ERROR] = 1;
	panel->show_level[SK_LOGGER_TYPE_FATAL] = 1;
	panel->collapse = 1;
	panel->autoscroll = 1;

	if (!sk_ui_node_is_valid(parent)) {
		parent = ui->context_root(ctx);
	}

	panel->root = ui->widget_panel(ctx, parent, "console-panel");
	if (!sk_ui_node_is_valid(panel->root)) {
		alloc->free(alloc->instance, panel);
		return NULL;
	}
	console_apply_panel_style(ui, ctx, panel->root);

	/* Title */
	(void)ui->widget_label(ctx, panel->root, "Console", "console-title");

	/* Toolbar: Clear */
	toolbar = ui->widget_view(ctx, panel->root, "console-toolbar");
	console_apply_row_style(ui, ctx, toolbar);
	panel->clear_btn = ui->widget_button(ctx, toolbar, "Clear", "console-clear");
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = console_on_clear_click;
	cbs.user = panel;
	(void)ui->node_set_callbacks(ctx, panel->clear_btn, &cbs);

	/* Level filters */
	levels = ui->widget_view(ctx, panel->root, "console-levels");
	console_apply_row_style(ui, ctx, levels);
	for (i = 0u; i < 6u; ++i) {
		panel->level_cb[i] = ui->widget_checkbox(ctx, levels, panel->show_level[i], level_ids[i]);
		(void)ui->widget_label(ctx, levels, level_names[i], NULL);
		(void)ui->checkbox_set_on_change(ctx, panel->level_cb[i], console_on_level_change, panel);
	}

	/* Collapse / auto-scroll */
	options = ui->widget_view(ctx, panel->root, "console-options");
	console_apply_row_style(ui, ctx, options);
	panel->collapse_cb = ui->widget_checkbox(ctx, options, panel->collapse, "console-collapse");
	(void)ui->widget_label(ctx, options, "Collapse", "console-collapse-lbl");
	(void)ui->checkbox_set_on_change(ctx, panel->collapse_cb, console_on_collapse_change, panel);
	panel->autoscroll_cb = ui->widget_checkbox(ctx, options, panel->autoscroll, "console-autoscroll");
	(void)ui->widget_label(ctx, options, "Auto-scroll", "console-autoscroll-lbl");
	(void)ui->checkbox_set_on_change(ctx, panel->autoscroll_cb, console_on_autoscroll_change, panel);

	/* Filter */
	filter_row = ui->widget_view(ctx, panel->root, "console-filter-row");
	console_apply_row_style(ui, ctx, filter_row);
	(void)ui->widget_label(ctx, filter_row, "Filter", "console-filter-lbl");
	panel->filter_input = ui->widget_text_input(ctx, filter_row, "", "console-filter");
	console_apply_filter_style(ui, ctx, panel->filter_input);
	/* Filter refilter is polled in sync() so we never overwrite text_input callbacks. */

	/* Scroll log region */
	panel->scroll = ui->widget_scroll_view(ctx, panel->root, "console-scroll");
	console_apply_scroll_style(ui, ctx, panel->scroll);
	panel->content = ui->scroll_view_content(ctx, panel->scroll);

	/* Logger sink */
	sink.user_data = panel;
	sink.print = console_sink_print;
	if (sk_logger_api()->add_sink(&sink) == 0) {
		panel->sink_registered = 1;
	}

	/* Seed a welcome line so the panel is non-empty before any host logs. */
	console_push_internal(panel, SK_LOGGER_TYPE_INFO, "editor", "Console panel ready (sk-ui port)");
	(void)sk_editor_console_panel_sync(panel);
	return panel;
}

void sk_editor_console_panel_destroy(sk_editor_console_panel_t* panel) {
	const sk_allocator_t* alloc;
	sk_log_sink_t sink;
	if (panel == NULL) {
		return;
	}
	if (panel->sink_registered) {
		sink.user_data = panel;
		sink.print = console_sink_print;
		(void)sk_logger_api()->remove_sink(&sink);
		panel->sink_registered = 0;
	}
	if (panel->ui != NULL && panel->ctx != NULL && sk_ui_node_is_valid(panel->root)) {
		/* Destroying the panel root also destroys descendants (line nodes). */
		(void)panel->ui->node_destroy(panel->ctx, panel->root);
		panel->root = SK_UI_NODE_INVALID;
	}
	alloc = sk_allocator_default();
	alloc->free(alloc->instance, panel);
}

sk_ui_node_t sk_editor_console_panel_root(const sk_editor_console_panel_t* panel) {
	if (panel == NULL) {
		return SK_UI_NODE_INVALID;
	}
	return panel->root;
}

i32 sk_editor_console_panel_sync(sk_editor_console_panel_t* panel) {
	const_chr_t filter;
	i32 need_rebuild;
	if (panel == NULL || panel->ui == NULL) {
		return -1;
	}
	/* Always re-read control state (checkbox callbacks may have run mid-frame). */
	if (sk_ui_node_is_valid(panel->collapse_cb)) {
		i32 v = panel->ui->checkbox_get_checked(panel->ctx, panel->collapse_cb) != 0 ? 1 : 0;
		if (v != panel->collapse) {
			panel->collapse = v;
			panel->version += 1u;
		}
	}
	if (sk_ui_node_is_valid(panel->autoscroll_cb)) {
		panel->autoscroll = panel->ui->checkbox_get_checked(panel->ctx, panel->autoscroll_cb) != 0 ? 1 : 0;
	}
	{
		u32 i;
		for (i = 0u; i < 6u; ++i) {
			if (sk_ui_node_is_valid(panel->level_cb[i])) {
				i32 v = panel->ui->checkbox_get_checked(panel->ctx, panel->level_cb[i]) != 0 ? 1 : 0;
				if (v != panel->show_level[i]) {
					panel->show_level[i] = v;
					panel->version += 1u;
				}
			}
		}
	}
	filter = "";
	if (sk_ui_node_is_valid(panel->filter_input)) {
		const_chr_t t = panel->ui->text_input_get_text(panel->ctx, panel->filter_input);
		if (t != NULL) {
			filter = t;
		}
	}
	if (strcmp(panel->last_filter, filter) != 0) {
		(void)snprintf(panel->last_filter, sizeof(panel->last_filter), "%s", filter);
		panel->version += 1u;
	}
	need_rebuild = panel->version != panel->last_synced_version ? 1 : 0;
	if (need_rebuild) {
		console_rebuild_lines(panel);
	}
	return 0;
}

void sk_editor_console_panel_push(sk_editor_console_panel_t* panel, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	if (panel == NULL) {
		return;
	}
	console_push_internal(panel, level, logger_name, message);
}

void sk_editor_console_panel_clear(sk_editor_console_panel_t* panel) {
	if (panel == NULL) {
		return;
	}
	panel->line_count = 0u;
	panel->version += 1u;
	console_rebuild_lines(panel);
}

u32 sk_editor_console_panel_line_count(const sk_editor_console_panel_t* panel) {
	return panel != NULL ? panel->line_count : 0u;
}

u32 sk_editor_console_panel_visible_count(const sk_editor_console_panel_t* panel) {
	return panel != NULL ? panel->visible_count : 0u;
}

i32 sk_editor_console_panel_abs_rect(const sk_editor_console_panel_t* panel, sk_ui_rect_t* out) {
	if (panel == NULL || out == NULL || panel->ui == NULL) {
		return -1;
	}
	return panel->ui->node_get_abs_rect(panel->ctx, panel->root, out, NULL);
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "test.h"

#include <stdio.h>

static i32 editor_test_plugin_path(const_chr_t name, char* out, u32 cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
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

static const sk_ui_api_t* editor_test_load_ui(sk_app_context_t* app_ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-ui.dylib";
#else
	const_chr_t name = "sk-ui.so";
#endif
	if (editor_test_plugin_path(name, path, (u32)sizeof(path)) == 0) {
		(void)app_api->load_plugin(app_ctx, path);
	}
	return (const sk_ui_api_t*)app_api->get_api(app_ctx, SK_UI_API_TYPE_ID);
}

SK_TEST(editor_console_panel_retained_logs_and_filter) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app_ctx = boot.context;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx = NULL;
	sk_editor_console_panel_t* panel = NULL;
	sk_ui_node_t filter;
	sk_ui_node_t clear_btn;

	TEST_ASSERT_NOT_NULL(app_ctx);
	ui = editor_test_load_ui(app_ctx, boot.api);
	if (ui == NULL) {
		/* Plugin not built/copied next to tests — skip rather than fail CI config. */
		sk_app_shutdown(app_ctx);
		TEST_IGNORE_MESSAGE("sk-ui plugin not available");
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, ui->init());
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	panel = sk_editor_console_panel_create(ui, ctx, SK_UI_NODE_INVALID);
	TEST_ASSERT_NOT_NULL(panel);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_console_panel_root(panel)));
	TEST_ASSERT_TRUE(sk_editor_console_panel_line_count(panel) >= 1u);

	sk_editor_console_panel_push(panel, SK_LOGGER_TYPE_INFO, "test", "hello console");
	sk_editor_console_panel_push(panel, SK_LOGGER_TYPE_ERROR, "test", "boom");
	sk_editor_console_panel_push(panel, SK_LOGGER_TYPE_TRACE, "test", "hidden-by-default");
	TEST_ASSERT_EQUAL_INT(0, sk_editor_console_panel_sync(panel));
	/* Trace off by default → not visible; info + error + welcome are. */
	TEST_ASSERT_TRUE(sk_editor_console_panel_visible_count(panel) >= 2u);

	filter = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-filter");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(filter));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, filter, "boom"));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_console_panel_sync(panel));
	TEST_ASSERT_EQUAL_UINT(1u, sk_editor_console_panel_visible_count(panel));

	clear_btn = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "console-clear");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(clear_btn));
	/* Layout so automation geometry is valid; clear via panel API (same path as
	 * the button callback) — hit-test depends on flex chrome sizing. */
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 480.0f, 360.0f));
	sk_editor_console_panel_clear(panel);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_console_panel_sync(panel));
	TEST_ASSERT_EQUAL_UINT(0u, sk_editor_console_panel_line_count(panel));
	TEST_ASSERT_EQUAL_UINT(0u, sk_editor_console_panel_visible_count(panel));

	sk_editor_console_panel_destroy(panel);
	ui->context_destroy(ctx);
	ui->shutdown();
	sk_app_shutdown(app_ctx);
}

#endif /* SK_TESTS */
