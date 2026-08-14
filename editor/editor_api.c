/**
 * @file editor_api.c
 * @brief Single editor API table (APX-328): registration + SK_TESTS wiring.
 *
 * The immutable table lives here (one copy per editor lib/executable). It is
 * placed on the app registry by sk_editor_bind_tables (editor boot); hosts and
 * tests resolve it only via app_api->get_api(ctx, SK_EDITOR_API_TYPE_ID).
 * Implementations of every entry stay in the editor TUs they already live in
 * (project.c / editor_ui_host.c / console_panel.c / imgui_shell.c) — no new
 * SK_API function exports are added for these surfaces.
 */

#include "editor_api.h"

static const sk_editor_api_t editor_api = {
	/* project (open/close/import) */
	sk_editor_project_open,
	sk_editor_project_close,
	sk_editor_project_assets,
	sk_editor_project_repository,
	sk_editor_project_root_directory,
	sk_editor_project_import,
	sk_editor_project_import_into,
	sk_editor_project_open_asset,

	/* dual-stack UI host */
	sk_editor_ui_host_create,
	sk_editor_ui_host_destroy,
	sk_editor_ui_host_context,
	sk_editor_ui_host_console,
	sk_editor_ui_host_imgui,
	sk_editor_ui_host_frame,
	sk_editor_ui_host_pointer,
	sk_editor_ui_host_key,
	sk_editor_ui_host_text,
	sk_editor_ui_host_last_pointer_target,
	sk_editor_ui_host_sk_ui_draw_list,
	sk_editor_ui_host_imgui_draw_items,
	sk_editor_ui_host_want_capture_mouse,
	sk_editor_ui_host_want_capture_keyboard,

	/* console panel */
	sk_editor_console_panel_create,
	sk_editor_console_panel_destroy,
	sk_editor_console_panel_root,
	sk_editor_console_panel_sync,
	sk_editor_console_panel_push,
	sk_editor_console_panel_clear,
	sk_editor_console_panel_line_count,
	sk_editor_console_panel_visible_count,
	sk_editor_console_panel_abs_rect,

	/* imgui shell */
	sk_editor_imgui_shell_create,
	sk_editor_imgui_shell_destroy,
	sk_editor_imgui_shell_begin_frame,
	sk_editor_imgui_shell_set_pointer,
	sk_editor_imgui_shell_draw,
	sk_editor_imgui_shell_end_frame,
	sk_editor_imgui_shell_want_capture_mouse,
	sk_editor_imgui_shell_want_capture_keyboard,
	sk_editor_imgui_shell_draw_items,
	sk_editor_imgui_shell_selected_index,
	sk_editor_imgui_shell_hierarchy_rect,

	/* workspaces / windows / dockspace (APX-329) */
	sk_editor_workspace_create,
	sk_editor_workspace_destroy,
	sk_editor_workspace_switch,
	sk_editor_workspace_list,
	sk_editor_workspace_active,
	sk_editor_workspace_dock_context,
	sk_editor_window_open,
	sk_editor_window_close,
	sk_editor_window_by_type,
	sk_editor_window_iterate,
	sk_editor_dockspace_init,
	sk_editor_dockspace_reset,
};

void sk_editor_bind_tables(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_EDITOR_API_TYPE_ID, &editor_api);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "test.h"
#include "unity.h"

SK_TEST(editor_api_resolves_via_app_registry) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;

	TEST_ASSERT_NOT_NULL(app);

	/* Not registered until editor boot binds the table. */
	TEST_ASSERT_NULL(boot.api->get_api(app, SK_EDITOR_API_TYPE_ID));

	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	/* The table is the single surface: every grouped entry is wired. */
	TEST_ASSERT_NOT_NULL(editor->project_open);
	TEST_ASSERT_NOT_NULL(editor->project_close);
	TEST_ASSERT_NOT_NULL(editor->project_import);
	TEST_ASSERT_NOT_NULL(editor->ui_host_create);
	TEST_ASSERT_NOT_NULL(editor->ui_host_frame);
	TEST_ASSERT_NOT_NULL(editor->ui_host_pointer);
	TEST_ASSERT_NOT_NULL(editor->console_create);
	TEST_ASSERT_NOT_NULL(editor->console_push);
	TEST_ASSERT_NOT_NULL(editor->imgui_create);
	TEST_ASSERT_NOT_NULL(editor->imgui_draw);

	/* APX-329 window/workspace/dockspace surface. */
	TEST_ASSERT_NOT_NULL(editor->workspace_create);
	TEST_ASSERT_NOT_NULL(editor->workspace_destroy);
	TEST_ASSERT_NOT_NULL(editor->workspace_switch);
	TEST_ASSERT_NOT_NULL(editor->workspace_list);
	TEST_ASSERT_NOT_NULL(editor->workspace_active);
	TEST_ASSERT_NOT_NULL(editor->window_open);
	TEST_ASSERT_NOT_NULL(editor->window_close);
	TEST_ASSERT_NOT_NULL(editor->window_by_type);
	TEST_ASSERT_NOT_NULL(editor->window_iterate);
	TEST_ASSERT_NOT_NULL(editor->dockspace_init);
	TEST_ASSERT_NOT_NULL(editor->dockspace_reset);

	/* Re-bind replaces the previous registration (idempotent). */
	sk_editor_bind_tables(app, boot.api);
	TEST_ASSERT_EQUAL_PTR(editor, boot.api->get_api(app, SK_EDITOR_API_TYPE_ID));

	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
