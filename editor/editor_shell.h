#pragma once

/**
 * @file editor_shell.h
 * @brief v2 editor shell (APX-366): app frame, menu bar, toolbar, dock host.
 *
 * The shell is the retained host that owns the editor window. It brings up
 * the frame every migrated window lives in:
 *
 *   - application frame: one sk-ui context holding a column of menu bar,
 *     toolbar, and the docking host (active workspace's dockspace).
 *   - main menu bar: File / Edit / Build / Tools / Window / Help rebuilt
 *     from the C++ editor's menu surface (MenuItemContext). Window-toggling
 *     entries always run through the struct-table registries (APX-329/330
 *     window impls, APX-365 per-window ops tables) — never direct calls.
 *   - toolbar: Save All / Undo / Redo / Play / Pause / Stop / Reset Layout;
 *     unimplemented actions stay inert or mocked instead of being dropped.
 *   - docking host: embeds the active workspace's sk-ui dockspace model
 *     (APX-330). Window open/close/focus are registry-driven:
 *     sk_editor_window_open / sk_editor_window_close and dock tab
 *     activation on the active workspace's dock model.
 *
 * Workspace switcher tabs (Scene/Graph/Animator/Material) live on the right
 * of the menu bar; "+" opens a popup of the registered workspace types.
 * Only the active workspace's dock model is live at a time — switching
 * tears the previous model down and rebuilds the new one on the shared
 * context (window chrome ids are process-global; see
 * sk_editor_workspace_clear_dockspace).
 *
 * Known plugin limitation: an OPEN menu popup is painted in sk-ui tree
 * order (painter's algorithm), so it renders under the toolbar / dock
 * siblings that follow the menu bar in the frame. Input still routes to the
 * popup through Clay's floating z-index (menu item clicks work — see the
 * tests); the popup render floats above the bar only once the plugin paints
 * floating nodes last.
 *
 * Graph node editor menus are not ported (migration manifest §4).
 *
 * The shell is editor-internal (hosted by sk-editor main.c and tests via
 * sk-editor-tests); it is not part of sk_editor_api_t.
 */

#include "app.h"
#include "editor_window.h"
#include "main_windows.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sk_editor_shell_t sk_editor_shell_t;

/**
 * Menu item descriptor for the shell menu bar registry. @p item_name is a
 * slash path ("Window/Project Browser", "Edit/Packages"); the shell builds
 * the menu tree (MenuItemContext analogue) from it. @p shortcut is display
 * only. All callbacks may be NULL; the shell keeps the struct by value.
 */
typedef struct sk_editor_shell_menu_item_t {
	const_chr_t item_name; /* slash-separated path; borrowed, process-lifetime */
	i32 priority;		   /* children of one menu sort by this (lower first) */
	const_chr_t shortcut;  /* display string ("Ctrl+S") or NULL */
	void (*action)(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user);
	i32 (*enabled)(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user);
	i32 (*visible)(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user);
	i32 (*selected)(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user);
	void* user;
} sk_editor_shell_menu_item_t;

/**
 * Create the editor shell: registers the default menu surface (File / Edit /
 * Build / Tools / Window / Help with the C++ editor entries; graph node
 * menus excluded), builds the frame (menu bar, workspace switcher, toolbar,
 * dock host) on a new owned ui context, creates the default Scene workspace
 * and initializes its dockspace on the shared context.
 *
 * @param ui Live sk_ui_api_t (init will be called here).
 * @return Shell, or NULL on failure.
 */
sk_editor_shell_t* sk_editor_shell_create(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_ui_api_t* ui);

/** Destroy the shell (owned ui context and all frame nodes). Safe on NULL. */
void sk_editor_shell_destroy(sk_editor_shell_t* shell);

/** Owned sk-ui context backing the frame + the active workspace's dockspace. */
sk_ui_context_t* sk_editor_shell_context(const sk_editor_shell_t* shell);

/**
 * Attach the host's font system + default face. The shell passes them in
 * its per-frame paint call (borrowed; the shell never destroys them). Text
 * glyphs only emit when both are set; call before the first frame.
 */
void sk_editor_shell_set_fonts(sk_editor_shell_t* shell, sk_ui_font_system_t* fonts, sk_ui_font_t* font);

/**
 * Run one shell frame: sync menu/toolbar/workspace state and click
 * dispatch, run every open window's draw (close-request contract), then
 * style_resolve → layout → layout_apply_scale → paint.
 * @return 0 on success.
 */
i32 sk_editor_shell_frame(sk_editor_shell_t* shell, f32 width, f32 height, f32 scale_x, f32 scale_y);

/** Feed pointer move/button events (dispatched to the shell ui context). */
void sk_editor_shell_pointer(sk_editor_shell_t* shell, f32 x, f32 y, i32 button, i32 down);

/** Feed key events (dispatched to the shell ui context). */
void sk_editor_shell_key(sk_editor_shell_t* shell, i32 key, i32 down, u32 mods);

/** Feed UTF-8 text (dispatched to the shell ui context). */
void sk_editor_shell_text(sk_editor_shell_t* shell, const_chr_t utf8);

/** Feed wheel/scroll offsets (dispatched to the shell ui context). */
void sk_editor_shell_scroll(sk_editor_shell_t* shell, f32 offset_x, f32 offset_y);

/**
 * Register a menu item in the shell's menu registry. Default items are
 * registered at create; hosts and window classes may add more. The item is
 * kept by value; the shell rebuilds the affected menu node if needed.
 */
void sk_editor_shell_add_menu_item(sk_editor_shell_t* shell, const sk_editor_shell_menu_item_t* item);

/**
 * Open (or focus, when already open) the window of type @p window_type_id
 * through the struct-table registries: the Project Browser goes through its
 * registered ops table (sk_editor_project_browser_ops->open), every other
 * window through sk_editor_window_open. Focus activates the window's dock
 * tab on the active workspace's dock model.
 * @return The open window, or NULL when the type is not registered.
 */
sk_editor_window_t* sk_editor_shell_open_window(sk_editor_shell_t* shell, sk_type_id_t window_type_id);

/** Close an open window through sk_editor_window_close (registry). */
void sk_editor_shell_close_window(sk_editor_shell_t* shell, sk_editor_window_t* window);

/** Focus an open window: activate its dock tab on the active workspace. */
void sk_editor_shell_focus_window(sk_editor_shell_t* shell, sk_editor_window_t* window);

/**
 * Make the workspace of type @p workspace_type_id active: create it on
 * demand, clear the previous workspace's live dock model and rebuild the new
 * one on the shell context, then switch the registry's active workspace.
 * @return The active workspace, or NULL when the type is not registered.
 */
sk_editor_workspace_t* sk_editor_shell_switch_workspace(sk_editor_shell_t* shell, u32 workspace_type_id);

/* ---- test / automation hooks ---- */

/** Menu bar node (widget=menu_bar) of the frame. */
sk_ui_node_t sk_editor_shell_menu_bar(const sk_editor_shell_t* shell);
/** Toolbar row node of the frame. */
sk_ui_node_t sk_editor_shell_toolbar(const sk_editor_shell_t* shell);
/** Dock host view node of the frame (children = active workspace dock host). */
sk_ui_node_t sk_editor_shell_dock_host(const sk_editor_shell_t* shell);
/**
 * Find a built menu node by stable id ("shell.menu.window.project_browser").
 * Returns SK_UI_NODE_INVALID when the item was never registered/built.
 */
sk_ui_node_t sk_editor_shell_find_menu(const sk_editor_shell_t* shell, const_chr_t id);
/** Non-zero when the mock Play/Pause/Stop toolbar state says simulation runs. */
i32 sk_editor_shell_sim_playing(const sk_editor_shell_t* shell);

#ifdef __cplusplus
}
#endif
