#pragma once

/**
 * @file editor_api.h
 * @brief Single editor API table (APX-328).
 *
 * All editor host surfaces (project open/close/import, dual-stack UI host
 * frame/input, console panel, imgui shell) are grouped behind one immutable
 * `sk_editor_api_t` table. The table is registered on the app context at
 * editor boot:
 *
 *   sk_editor_bind_tables(ctx, app_api);   (once, after sk_app_init)
 *   const sk_editor_api_t* editor =
 *       (const sk_editor_api_t*)app_api->get_api(ctx, SK_EDITOR_API_TYPE_ID);
 *   sk_editor_project_t* p = editor->project_open(ctx, app_api, "Game", path);
 *
 * Hosts and tests resolve the editor **only** via
 * `app_api->get_api(ctx, SK_EDITOR_API_TYPE_ID)` — there is no process-wide
 * `sk_editor_api()` accessor and the individual operations are not exported
 * plugin symbols. Implementations stay inside the editor executable/lib; the
 * free functions declared in project.h / editor_ui_host.h / console_panel.h /
 * imgui_shell.h are the internal wiring the table points at.
 *
 * Window/workspace scaffolding (APX-329/330) is on the table: window impls
 * and workspace types register via app_api->add_impl and the entries below
 * create/destroy/switch/list workspaces, open/close/iterate windows by
 * type_id, and init/reset dockspaces. The 14 main editor windows register
 * as static sk_editor_window_t impls (main_windows.h/c); dockspace
 * init/reset project the default layout onto a workspace-owned sk-ui
 * dockspace and the window registry wires chrome close/undock/redock/tab
 * ops through the sk-ui dock APIs (editor_window.h).
 */

#include "app.h"
#include "console_panel.h"
#include "editor_layout.h"
#include "editor_ui_host.h"
#include "editor_window.h"
#include "imgui_shell.h"
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_editor_api_t in the app registry. */
#define SK_EDITOR_API_TYPE_ID SK_TYPE_ID("sk.editor_api", 0xbff12a4ed8315bf1ULL, 0xff4ec92125c4567dULL)

/**
 * Immutable editor API table. One pointer is handed to every host/test that
 * resolves SK_EDITOR_API_TYPE_ID; do not copy the struct.
 *
 * Entries mirror the free functions of the same name minus the `sk_editor_`
 * prefix (project.h / editor_ui_host.h / console_panel.h / imgui_shell.h).
 */
typedef struct sk_editor_api_t {
	/* ---- project (open/close/import via core repository-assets) ---- */

	/** @see sk_editor_project_open */
	sk_editor_project_t* (*project_open)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t package_name, const_chr_t package_path);

	/** @see sk_editor_project_close */
	void (*project_close)(sk_editor_project_t* project);

	/** @see sk_editor_project_assets */
	sk_resource_assets_context_t* (*project_assets)(const sk_editor_project_t* project);

	/** @see sk_editor_project_repository */
	sk_repository_t* (*project_repository)(const sk_editor_project_t* project);

	/** @see sk_editor_project_root_directory */
	sk_rid_t (*project_root_directory)(const sk_editor_project_t* project);

	/** @see sk_editor_project_import */
	i32 (*project_import)(sk_editor_project_t* project, const_chr_t path);

	/** @see sk_editor_project_import_into */
	i32 (*project_import_into)(sk_editor_project_t* project, sk_rid_t parent, const_chr_t path);

	/** @see sk_editor_project_open_asset */
	void (*project_open_asset)(sk_editor_project_t* project, sk_rid_t rid);

	/* ---- dual-stack UI host (frame / input) ---- */

	/** @see sk_editor_ui_host_create */
	sk_editor_ui_host_t* (*ui_host_create)(const sk_ui_api_t* ui, const sk_logger_api_t* logger_api, sk_logger_context_t* log_ctx);

	/** @see sk_editor_ui_host_destroy */
	void (*ui_host_destroy)(sk_editor_ui_host_t* host);

	/** @see sk_editor_ui_host_context */
	sk_ui_context_t* (*ui_host_context)(const sk_editor_ui_host_t* host);

	/** @see sk_editor_ui_host_console */
	sk_editor_console_panel_t* (*ui_host_console)(const sk_editor_ui_host_t* host);

	/** @see sk_editor_ui_host_imgui */
	sk_editor_imgui_shell_t* (*ui_host_imgui)(const sk_editor_ui_host_t* host);

	/** @see sk_editor_ui_host_frame */
	i32 (*ui_host_frame)(sk_editor_ui_host_t* host, f32 width, f32 height, f32 scale_x, f32 scale_y);

	/** @see sk_editor_ui_host_pointer */
	void (*ui_host_pointer)(sk_editor_ui_host_t* host, f32 x, f32 y, i32 button, i32 down);

	/** @see sk_editor_ui_host_key */
	void (*ui_host_key)(sk_editor_ui_host_t* host, i32 key, i32 down, u32 mods);

	/** @see sk_editor_ui_host_text */
	void (*ui_host_text)(sk_editor_ui_host_t* host, const_chr_t utf8);

	/** @see sk_editor_ui_host_last_pointer_target */
	sk_editor_ui_input_target_t (*ui_host_last_pointer_target)(const sk_editor_ui_host_t* host);

	/** @see sk_editor_ui_host_sk_ui_draw_list */
	const sk_ui_draw_list_t* (*ui_host_sk_ui_draw_list)(const sk_editor_ui_host_t* host);

	/** @see sk_editor_ui_host_imgui_draw_items */
	const sk_editor_imgui_draw_item_t* (*ui_host_imgui_draw_items)(const sk_editor_ui_host_t* host, u32* out_count);

	/** @see sk_editor_ui_host_want_capture_mouse */
	i32 (*ui_host_want_capture_mouse)(const sk_editor_ui_host_t* host);

	/** @see sk_editor_ui_host_want_capture_keyboard */
	i32 (*ui_host_want_capture_keyboard)(const sk_editor_ui_host_t* host);

	/* ---- console panel (sk-ui retained log panel) ---- */

	/** @see sk_editor_console_panel_create */
	sk_editor_console_panel_t* (*console_create)(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent, const sk_logger_api_t* logger_api, sk_logger_context_t* log_ctx);

	/** @see sk_editor_console_panel_destroy */
	void (*console_destroy)(sk_editor_console_panel_t* panel);

	/** @see sk_editor_console_panel_root */
	sk_ui_node_t (*console_root)(const sk_editor_console_panel_t* panel);

	/** @see sk_editor_console_panel_sync */
	i32 (*console_sync)(sk_editor_console_panel_t* panel);

	/** @see sk_editor_console_panel_push */
	void (*console_push)(sk_editor_console_panel_t* panel, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message);

	/** @see sk_editor_console_panel_clear */
	void (*console_clear)(sk_editor_console_panel_t* panel);

	/** @see sk_editor_console_panel_line_count */
	u32 (*console_line_count)(const sk_editor_console_panel_t* panel);

	/** @see sk_editor_console_panel_visible_count */
	u32 (*console_visible_count)(const sk_editor_console_panel_t* panel);

	/** @see sk_editor_console_panel_abs_rect */
	i32 (*console_abs_rect)(const sk_editor_console_panel_t* panel, sk_ui_rect_t* out);

	/* ---- imgui shell (immediate-mode stand-in) ---- */

	/** @see sk_editor_imgui_shell_create */
	sk_editor_imgui_shell_t* (*imgui_create)(void);

	/** @see sk_editor_imgui_shell_destroy */
	void (*imgui_destroy)(sk_editor_imgui_shell_t* shell);

	/** @see sk_editor_imgui_shell_begin_frame */
	void (*imgui_begin_frame)(sk_editor_imgui_shell_t* shell, f32 display_w, f32 display_h);

	/** @see sk_editor_imgui_shell_set_pointer */
	void (*imgui_set_pointer)(sk_editor_imgui_shell_t* shell, f32 x, f32 y, i32 button_down, i32 clicked);

	/** @see sk_editor_imgui_shell_draw */
	void (*imgui_draw)(sk_editor_imgui_shell_t* shell);

	/** @see sk_editor_imgui_shell_end_frame */
	void (*imgui_end_frame)(sk_editor_imgui_shell_t* shell);

	/** @see sk_editor_imgui_shell_want_capture_mouse */
	i32 (*imgui_want_capture_mouse)(const sk_editor_imgui_shell_t* shell);

	/** @see sk_editor_imgui_shell_want_capture_keyboard */
	i32 (*imgui_want_capture_keyboard)(const sk_editor_imgui_shell_t* shell);

	/** @see sk_editor_imgui_shell_draw_items */
	const sk_editor_imgui_draw_item_t* (*imgui_draw_items)(const sk_editor_imgui_shell_t* shell, u32* out_count);

	/** @see sk_editor_imgui_shell_selected_index */
	i32 (*imgui_selected_index)(const sk_editor_imgui_shell_t* shell);

	/** @see sk_editor_imgui_shell_hierarchy_rect */
	void (*imgui_hierarchy_rect)(const sk_editor_imgui_shell_t* shell, f32* x, f32* y, f32* w, f32* h);

	/* ---- workspaces (APX-329 scaffolding) ---- */

	/** @see sk_editor_workspace_create */
	sk_editor_workspace_t* (*workspace_create)(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_type_id);

	/** @see sk_editor_workspace_destroy */
	void (*workspace_destroy)(sk_editor_workspace_t* workspace);

	/** @see sk_editor_workspace_switch */
	void (*workspace_switch)(sk_editor_workspace_t* workspace);

	/** @see sk_editor_workspace_list */
	u32 (*workspace_list)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_workspace_t** out, u32 out_cap);

	/** @see sk_editor_workspace_active */
	sk_editor_workspace_t* (*workspace_active)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** @see sk_editor_workspace_dock_context */
	sk_ui_context_t* (*workspace_dock_context)(const sk_editor_workspace_t* workspace);

	/* ---- windows (APX-329 scaffolding) ---- */

	/** @see sk_editor_window_open */
	sk_editor_window_t* (*window_open)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t window_type_id);

	/** @see sk_editor_window_close */
	void (*window_close)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** @see sk_editor_window_by_type */
	sk_editor_window_t* (*window_by_type)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t window_type_id);

	/** @see sk_editor_window_iterate */
	u32 (*window_iterate)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t** out, u32 out_cap);

	/* ---- dockspace (APX-329 scaffolding) ---- */

	/** @see sk_editor_dockspace_init */
	void (*dockspace_init)(sk_editor_workspace_t* workspace);

	/** @see sk_editor_dockspace_reset */
	void (*dockspace_reset)(sk_editor_workspace_t* workspace);

	/* ---- workspaces persist (APX-368) ---- */

	/** @see sk_editor_workspace_type_id */
	u32 (*workspace_type_id)(const sk_editor_workspace_t* workspace);

	/** @see sk_editor_layout_init */
	void (*layout_init)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** @see sk_editor_layout_shutdown */
	void (*layout_shutdown)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** @see sk_editor_layout_set_path */
	void (*layout_set_path)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path);

	/** @see sk_editor_layout_save */
	i32 (*layout_save)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** @see sk_editor_layout_load */
	i32 (*layout_load)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** @see sk_editor_layout_has_saved */
	i32 (*layout_has_saved)(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_type_id);

	/** @see sk_editor_workspace_capture */
	i32 (*workspace_capture)(sk_editor_workspace_t* workspace);

	/** @see sk_editor_workspace_restore */
	i32 (*workspace_restore)(sk_editor_workspace_t* workspace);

	/** @see sk_editor_workspace_reset_to_preset */
	void (*workspace_reset_to_preset)(sk_editor_workspace_t* workspace);
} sk_editor_api_t;

/**
 * Register the editor API table on @p context under SK_EDITOR_API_TYPE_ID.
 * Called once at editor boot (after sk_app_init) and by tests after
 * sk_app_create / sk_app_init. Replaces any previous registration.
 *
 * @param context App context (must not be NULL).
 * @param app_api Process app API table (must not be NULL).
 */
void sk_editor_bind_tables(sk_app_context_t* context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
