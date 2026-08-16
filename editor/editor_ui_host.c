/**
 * @file editor_ui_host.c
 * @brief Dual-stack host: sk-ui Console + ImGui-path shell in one frame.
 */

#include "editor_ui_host.h"

#include "allocator.h"
#include "logger.h"

#include <string.h>

struct sk_editor_ui_host_t {
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_console_panel_t* console;
	sk_editor_imgui_shell_t* imgui;
	const sk_logger_api_t* logger_api;
	sk_logger_context_t* log_ctx;
	sk_ui_node_t console_root;
	sk_ui_node_t layout_root; /* full-window column holding console dock */

	f32 width;
	f32 height;
	f32 scale_x;
	f32 scale_y;
	f32 pointer_x;
	f32 pointer_y;
	i32 pointer_down;

	sk_editor_ui_input_target_t last_pointer_target;
	i32 laid_out;

	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;
};

static i32 host_point_in_rect(f32 x, f32 y, const sk_ui_rect_t* r) {
	if (r == NULL) {
		return 0;
	}
	return (x >= r->x && y >= r->y && x < r->x + r->width && y < r->y + r->height) ? 1 : 0;
}

static void host_place_console(sk_editor_ui_host_t* host) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_style_props_t p;
	sk_ui_node_t root;
	sk_ui_node_t top_spacer;
	sk_ui_node_t bottom_row;
	sk_ui_node_t left_spacer;

	/* Layout: column with flexible top spacer, then bottom dock row with
	 * left flexible spacer + console panel (~half width, ~40% height).
	 * Hierarchy (imgui) occupies top-left in its own coordinate system. */
	root = ui->context_root(host->ctx);
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.background_color = sk_ui_rgba(0.07f, 0.08f, 0.10f, 1.0f);
	(void)ui->node_set_inline_style(host->ctx, root, &p);

	top_spacer = ui->widget_view(host->ctx, root, "editor-top-spacer");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_MIN_HEIGHT;
	p.layout.flex_grow = 1.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.min_height = sk_ui_pt(40.0f);
	(void)ui->node_set_inline_style(host->ctx, top_spacer, &p);

	bottom_row = ui->widget_view(host->ctx, root, "editor-bottom-row");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_COLUMN_GAP | SK_UI_SP_PADDING | SK_UI_SP_FLEX_SHRINK;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(40.0f);
	p.layout.column_gap = 8.0f;
	p.layout.padding.left = 8.0f;
	p.layout.padding.right = 8.0f;
	p.layout.padding.bottom = 8.0f;
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(host->ctx, bottom_row, &p);

	left_spacer = ui->widget_view(host->ctx, bottom_row, "editor-left-spacer");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_HEIGHT;
	p.layout.flex_grow = 0.4f;
	p.layout.height = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(host->ctx, left_spacer, &p);

	host->console = sk_editor_console_panel_create(ui, host->ctx, bottom_row, host->logger_api, host->log_ctx);
	if (host->console != NULL) {
		host->console_root = sk_editor_console_panel_root(host->console);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH;
		p.layout.flex_grow = 1.0f;
		p.layout.height = sk_ui_percent(100.0f);
		p.layout.min_width = sk_ui_pt(280.0f);
		(void)ui->node_merge_inline_style(host->ctx, host->console_root, &p);
	}
	host->layout_root = root;
}

sk_editor_ui_host_t* sk_editor_ui_host_create(const sk_ui_api_t* ui, const sk_logger_api_t* logger_api, sk_logger_context_t* log_ctx) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_ui_host_t* host;

	if (ui == NULL || logger_api == NULL || log_ctx == NULL) {
		return NULL;
	}
	host = (sk_editor_ui_host_t*)alloc->alloc(alloc->instance, sizeof(sk_editor_ui_host_t));
	if (host == NULL) {
		return NULL;
	}
	memset(host, 0, sizeof(*host));
	host->ui = ui;
	host->logger_api = logger_api;
	host->log_ctx = log_ctx;
	host->scale_x = 1.0f;
	host->scale_y = 1.0f;
	host->width = 1280.0f;
	host->height = 720.0f;

	if (ui->init() != 0) {
		alloc->free(alloc->instance, host);
		return NULL;
	}
	host->ctx = ui->context_create(NULL);
	if (host->ctx == NULL) {
		ui->shutdown();
		alloc->free(alloc->instance, host);
		return NULL;
	}

	host->imgui = sk_editor_imgui_shell_create();
	if (host->imgui == NULL) {
		ui->context_destroy(host->ctx);
		ui->shutdown();
		alloc->free(alloc->instance, host);
		return NULL;
	}

	host_place_console(host);
	if (host->console == NULL) {
		sk_editor_ui_host_destroy(host);
		return NULL;
	}
	return host;
}

void sk_editor_ui_host_destroy(sk_editor_ui_host_t* host) {
	const sk_allocator_t* alloc;
	if (host == NULL) {
		return;
	}
	if (host->console != NULL) {
		sk_editor_console_panel_destroy(host->console);
		host->console = NULL;
	}
	if (host->imgui != NULL) {
		sk_editor_imgui_shell_destroy(host->imgui);
		host->imgui = NULL;
	}
	if (host->ui != NULL) {
		if (host->ctx != NULL) {
			host->ui->context_destroy(host->ctx);
			host->ctx = NULL;
		}
		host->ui->shutdown();
	}
	alloc = sk_allocator_default();
	alloc->free(alloc->instance, host);
}

sk_ui_context_t* sk_editor_ui_host_context(const sk_editor_ui_host_t* host) {
	return host != NULL ? host->ctx : NULL;
}

sk_editor_console_panel_t* sk_editor_ui_host_console(const sk_editor_ui_host_t* host) {
	return host != NULL ? host->console : NULL;
}

sk_editor_imgui_shell_t* sk_editor_ui_host_imgui(const sk_editor_ui_host_t* host) {
	return host != NULL ? host->imgui : NULL;
}

i32 sk_editor_ui_host_frame(sk_editor_ui_host_t* host, f32 width, f32 height, f32 scale_x, f32 scale_y) {
	const sk_ui_api_t* ui;
	sk_ui_paint_params_t paint;
	if (host == NULL || host->ui == NULL || host->ctx == NULL) {
		return -1;
	}
	ui = host->ui;
	host->width = width > 1.0f ? width : 1.0f;
	host->height = height > 1.0f ? height : 1.0f;
	host->scale_x = scale_x > 0.0f ? scale_x : 1.0f;
	host->scale_y = scale_y > 0.0f ? scale_y : 1.0f;

	/* 1) Retained console: sync log labels from sink/filter state. */
	(void)sk_editor_console_panel_sync(host->console);

	/* 2) sk-ui pipeline (retained). */
	if (ui->style_resolve(host->ctx) != 0) {
		return -1;
	}
	if (ui->layout(host->ctx, host->width, host->height) != 0) {
		return -1;
	}
	if (ui->layout_apply_scale(host->ctx, host->scale_x, host->scale_y) != 0) {
		return -1;
	}
	memset(&paint, 0, sizeof(paint));
	paint.font_system = host->fonts;
	paint.font = host->font;
	if (ui->paint(host->ctx, &paint) != 0) {
		return -1;
	}
	host->laid_out = 1;

	/* 3) ImGui-path immediate panel (same frame, after sk-ui layout so
	 *    hit regions for arbitration can use console abs rect). */
	sk_editor_imgui_shell_begin_frame(host->imgui, host->width, host->height);
	sk_editor_imgui_shell_set_pointer(host->imgui, host->pointer_x, host->pointer_y, host->pointer_down, 0);
	sk_editor_imgui_shell_draw(host->imgui);
	sk_editor_imgui_shell_end_frame(host->imgui);

	return 0;
}

void sk_editor_ui_host_pointer(sk_editor_ui_host_t* host, f32 x, f32 y, i32 button, i32 down) {
	const sk_ui_api_t* ui;
	sk_ui_input_event_t ev;
	sk_ui_rect_t console_rect;
	i32 over_console = 0;
	i32 sk_ui_capturing = 0;
	i32 route_sk_ui = 0;
	i32 edge_click = 0;

	if (host == NULL || host->ui == NULL) {
		return;
	}
	ui = host->ui;
	edge_click = (down != 0 && host->pointer_down == 0) ? 1 : 0;
	host->pointer_x = x;
	host->pointer_y = y;
	host->pointer_down = down != 0 ? 1 : 0;

	/* Ensure layout exists for hit tests. */
	if (!host->laid_out) {
		(void)sk_editor_ui_host_frame(host, host->width, host->height, host->scale_x, host->scale_y);
	}

	if (sk_editor_console_panel_abs_rect(host->console, &console_rect) == 0) {
		over_console = host_point_in_rect(x, y, &console_rect);
	}
	sk_ui_capturing = sk_ui_node_is_valid(ui->pointer_capture_get(host->ctx)) || ui->wants_keyboard(host->ctx);

	/*
	 * Arbitration: sk-ui first when pointer is over the console dock, or when
	 * sk-ui already holds capture/focus. Otherwise ImGui-path shell.
	 * (Matches "topmost interactive wins" — console is drawn second / on top.)
	 */
	route_sk_ui = (over_console || sk_ui_capturing) ? 1 : 0;

	if (route_sk_ui) {
		memset(&ev, 0, sizeof(ev));
		ev.x = x;
		ev.y = y;
		if (button >= 0) {
			ev.kind = SK_UI_INPUT_POINTER_BUTTON;
			ev.button = button;
			ev.down = down != 0 ? 1 : 0;
		} else {
			ev.kind = SK_UI_INPUT_POINTER_MOVE;
		}
		(void)ui->input_dispatch(host->ctx, &ev);
		host->last_pointer_target = SK_EDITOR_UI_INPUT_SK_UI;
	} else {
		sk_editor_imgui_shell_set_pointer(host->imgui, x, y, down, edge_click);
		/* Immediate shell reacts on next draw; apply edge click now for tests. */
		if (edge_click) {
			sk_editor_imgui_shell_begin_frame(host->imgui, host->width, host->height);
			sk_editor_imgui_shell_set_pointer(host->imgui, x, y, down, 1);
			sk_editor_imgui_shell_draw(host->imgui);
			sk_editor_imgui_shell_end_frame(host->imgui);
		}
		host->last_pointer_target = SK_EDITOR_UI_INPUT_IMGUI;
	}
}

void sk_editor_ui_host_key(sk_editor_ui_host_t* host, i32 key, i32 down, u32 mods) {
	const sk_ui_api_t* ui;
	sk_ui_input_event_t ev;
	if (host == NULL || host->ui == NULL) {
		return;
	}
	ui = host->ui;
	if (ui->wants_keyboard(host->ctx)) {
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_KEY;
		ev.key = key;
		ev.down = down != 0 ? 1 : 0;
		ev.mods = mods;
		(void)ui->input_dispatch(host->ctx, &ev);
		return;
	}
	/* ImGui shell has no text focus in v1 stand-in. */
	(void)key;
	(void)down;
	(void)mods;
}

void sk_editor_ui_host_text(sk_editor_ui_host_t* host, const_chr_t utf8) {
	const sk_ui_api_t* ui;
	sk_ui_input_event_t ev;
	if (host == NULL || host->ui == NULL || utf8 == NULL) {
		return;
	}
	ui = host->ui;
	if (ui->wants_keyboard(host->ctx)) {
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_TEXT;
		ev.text = utf8;
		(void)ui->input_dispatch(host->ctx, &ev);
	}
}

sk_editor_ui_input_target_t sk_editor_ui_host_last_pointer_target(const sk_editor_ui_host_t* host) {
	return host != NULL ? host->last_pointer_target : SK_EDITOR_UI_INPUT_NONE;
}

const sk_ui_draw_list_t* sk_editor_ui_host_sk_ui_draw_list(const sk_editor_ui_host_t* host) {
	if (host == NULL || host->ui == NULL || host->ctx == NULL) {
		return NULL;
	}
	return host->ui->get_draw_list(host->ctx);
}

const sk_editor_imgui_draw_item_t* sk_editor_ui_host_imgui_draw_items(const sk_editor_ui_host_t* host, u32* out_count) {
	if (host == NULL) {
		if (out_count) {
			*out_count = 0u;
		}
		return NULL;
	}
	return sk_editor_imgui_shell_draw_items(host->imgui, out_count);
}

i32 sk_editor_ui_host_want_capture_mouse(const sk_editor_ui_host_t* host) {
	if (host == NULL || host->ui == NULL) {
		return 0;
	}
	return host->ui->wants_mouse(host->ctx) || sk_editor_imgui_shell_want_capture_mouse(host->imgui);
}

i32 sk_editor_ui_host_want_capture_keyboard(const sk_editor_ui_host_t* host) {
	if (host == NULL || host->ui == NULL) {
		return 0;
	}
	return host->ui->wants_keyboard(host->ctx) || sk_editor_imgui_shell_want_capture_keyboard(host->imgui);
}

void sk_editor_ui_host_set_fonts(sk_editor_ui_host_t* host, sk_ui_font_system_t* fonts, sk_ui_font_t* font) {
	if (host == NULL) {
		return;
	}
	host->fonts = fonts;
	host->font = font;
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "app.h"
#include "editor_api.h"
#include "filesystem.h"
#include "path.h"
#include "test.h"

static i32 host_test_plugin_path(const_chr_t name, char* out, u32 cap) {
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

static const sk_ui_api_t* host_test_load_ui(sk_app_context_t* app_ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-ui.dylib";
#else
	const_chr_t name = "sk-ui.so";
#endif
	if (host_test_plugin_path(name, path, (u32)sizeof(path)) == 0) {
		(void)app_api->load_plugin(app_ctx, path);
	}
	return (const sk_ui_api_t*)app_api->get_api(app_ctx, SK_UI_API_TYPE_ID);
}

SK_TEST(editor_ui_host_dual_stack_same_frame) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app_ctx = boot.context;
	const sk_editor_api_t* editor;
	const sk_ui_api_t* ui;
	sk_editor_ui_host_t* host;
	const sk_ui_draw_list_t* sk_dl;
	u32 imgui_count = 0u;
	const sk_editor_imgui_draw_item_t* imgui_items;
	sk_ui_rect_t console_rect;
	f32 hx, hy, hw, hh;

	TEST_ASSERT_NOT_NULL(app_ctx);

	/* Editor boot: register the single editor API table, then resolve it via
	 * the app registry (same path hosts use). */
	sk_editor_bind_tables(app_ctx, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app_ctx, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	ui = host_test_load_ui(app_ctx, boot.api);
	if (ui == NULL) {
		sk_app_shutdown(app_ctx);
		TEST_IGNORE_MESSAGE("sk-ui plugin not available");
		return;
	}

	host = editor->ui_host_create(ui, boot.api->logger_api(app_ctx), boot.api->logger_context(app_ctx));
	TEST_ASSERT_NOT_NULL(host);
	editor->ui_host_set_fonts(host, NULL, NULL);

	/* One frame: both stacks produce draw output. */
	TEST_ASSERT_EQUAL_INT(0, editor->ui_host_frame(host, 960.0f, 540.0f, 1.0f, 1.0f));
	sk_dl = editor->ui_host_sk_ui_draw_list(host);
	TEST_ASSERT_NOT_NULL(sk_dl);
	TEST_ASSERT_TRUE(sk_dl->command_count > 0u || sk_dl->vertex_count > 0u);

	imgui_items = editor->ui_host_imgui_draw_items(host, &imgui_count);
	TEST_ASSERT_NOT_NULL(imgui_items);
	TEST_ASSERT_TRUE(imgui_count > 0u);

	/* Console has abs rect; hierarchy has its own rect — both live. */
	TEST_ASSERT_EQUAL_INT(0, editor->console_abs_rect(editor->ui_host_console(host), &console_rect));
	TEST_ASSERT_TRUE(console_rect.width > 0.0f && console_rect.height > 0.0f);
	editor->imgui_hierarchy_rect(editor->ui_host_imgui(host), &hx, &hy, &hw, &hh);
	TEST_ASSERT_TRUE(hw > 0.0f && hh > 0.0f);

	/* Pointer over hierarchy → ImGui path. */
	editor->ui_host_pointer(host, hx + hw * 0.5f, hy + 40.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_UI_INPUT_IMGUI, (int)editor->ui_host_last_pointer_target(host));
	editor->ui_host_pointer(host, hx + hw * 0.5f, hy + 40.0f, SK_UI_POINTER_BUTTON_LEFT, 0);

	/* Pointer over console dock → sk-ui. */
	editor->ui_host_pointer(host, console_rect.x + console_rect.width * 0.5f, console_rect.y + console_rect.height * 0.5f, -1, 0);
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_UI_INPUT_SK_UI, (int)editor->ui_host_last_pointer_target(host));

	/* Console still retains log lines across frames (not rebuilt-from-scratch). */
	{
		u32 before = editor->console_line_count(editor->ui_host_console(host));
		editor->console_push(editor->ui_host_console(host), SK_LOGGER_TYPE_WARN, "host", "frame-2");
		TEST_ASSERT_EQUAL_INT(0, editor->ui_host_frame(host, 960.0f, 540.0f, 1.0f, 1.0f));
		TEST_ASSERT_EQUAL_UINT(before + 1u, editor->console_line_count(editor->ui_host_console(host)));
	}

	editor->ui_host_destroy(host);
	sk_app_shutdown(app_ctx);
}

SK_TEST(editor_imgui_shell_immediate_selection) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app_ctx = boot.context;
	const sk_editor_api_t* editor;
	sk_editor_imgui_shell_t* shell;
	f32 x, y, w, h;

	TEST_ASSERT_NOT_NULL(app_ctx);

	/* Editor boot: register the single editor API table, then resolve it via
	 * the app registry (same path hosts use). */
	sk_editor_bind_tables(app_ctx, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app_ctx, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	shell = editor->imgui_create();
	TEST_ASSERT_NOT_NULL(shell);
	editor->imgui_begin_frame(shell, 800.0f, 600.0f);
	editor->imgui_hierarchy_rect(shell, &x, &y, &w, &h);
	/* Click first entity row. */
	editor->imgui_set_pointer(shell, x + 20.0f, y + 36.0f, 1, 1);
	editor->imgui_draw(shell);
	editor->imgui_end_frame(shell);
	TEST_ASSERT_EQUAL_INT(0, editor->imgui_selected_index(shell));
	TEST_ASSERT_TRUE(editor->imgui_want_capture_mouse(shell));
	editor->imgui_destroy(shell);
	sk_app_shutdown(app_ctx);
}

#endif /* SK_TESTS */
