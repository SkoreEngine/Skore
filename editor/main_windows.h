#pragma once

/**
 * @file main_windows.h
 * @brief The 14 main editor windows (APX-330).
 *
 * One static `sk_editor_window_t` per main window, registered with
 * `app_api->add_impl` under SK_EDITOR_WINDOW_IMPL_TYPE_ID by
 * sk_editor_windows_register_impls (editor boot). Titles and docking
 * metadata only — every window has an empty Draw and no per-window UI yet
 * (window contents land in later tasks).
 *
 * Default dock metadata (dock position / order / workspace availability):
 *
 *   Console              BottomRight   order 10   All
 *   Debugger             BottomRight   order 20   All
 *   EntityTree           RightTop      order  0   Scene
 *   History              RightTop      order 10   Scene
 *   Properties           RightBottom   order  0   All
 *   ProjectBrowser       BottomLeft    order  0   All
 *   SceneView            Center        order  0   Scene
 *   GraphEditor          Center        order 20   Graph
 *   MaterialGraphEditor  Center        order  0   Material
 *   AnimatorGraph        Center        order  0   Animator
 *   AnimatorTreeView     Left          order  0   Animator
 *   ResourceDebugger     Center        order 50   on-demand (mask 0)
 *   Packages             (none)        menu-opened, not default-docked
 *   Settings             (none)        menu-opened, not default-docked
 *
 * On-demand / menu-opened windows carry workspace_mask 0 so dockspace init
 * never auto-opens them; sk_editor_window_open still works and docks them
 * into the active workspace's dock model when the ui plugin is present.
 */

#include "app.h"
#include "editor_window.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Window type ids (one per main window class). */
#define SK_EDITOR_WINDOW_CONSOLE SK_TYPE_ID("sk.editor_window.console", 0xd570533a045caec3ULL, 0x181f7b4a7b626f74ULL)
#define SK_EDITOR_WINDOW_DEBUGGER SK_TYPE_ID("sk.editor_window.debugger", 0x200eeec9a19b40c2ULL, 0x887cff34876dbd6eULL)
#define SK_EDITOR_WINDOW_ENTITY_TREE SK_TYPE_ID("sk.editor_window.entity_tree", 0x91494012d91e201dULL, 0xa13ea23d421f55a4ULL)
#define SK_EDITOR_WINDOW_HISTORY SK_TYPE_ID("sk.editor_window.history", 0xe74fee2b9a180ff1ULL, 0x5f93e0cae40c99acULL)
#define SK_EDITOR_WINDOW_PROPERTIES SK_TYPE_ID("sk.editor_window.properties", 0xe941ff4e53a7fba5ULL, 0x5873b5c7dddddd32ULL)
#define SK_EDITOR_WINDOW_PROJECT_BROWSER SK_TYPE_ID("sk.editor_window.project_browser", 0x7b9eb0d6407b065dULL, 0x2a58e76b965fc3bfULL)
#define SK_EDITOR_WINDOW_SCENE_VIEW SK_TYPE_ID("sk.editor_window.scene_view", 0xe716a3d784a32fb5ULL, 0xc521c449cf689524ULL)
#define SK_EDITOR_WINDOW_GRAPH_EDITOR SK_TYPE_ID("sk.editor_window.graph_editor", 0x11a713a277e43b27ULL, 0xa6710966a92918b3ULL)
#define SK_EDITOR_WINDOW_MATERIAL_GRAPH_EDITOR SK_TYPE_ID("sk.editor_window.material_graph_editor", 0x13f9892554e40aaaULL, 0x78cae93f28b0d1daULL)
#define SK_EDITOR_WINDOW_ANIMATOR_GRAPH SK_TYPE_ID("sk.editor_window.animator_graph", 0x6280e7e246aee56bULL, 0x001ce29dc64a57e8ULL)
#define SK_EDITOR_WINDOW_ANIMATOR_TREE_VIEW SK_TYPE_ID("sk.editor_window.animator_tree_view", 0xf7898d4a6d730ec9ULL, 0x6e016ac9ac481b35ULL)
#define SK_EDITOR_WINDOW_RESOURCE_DEBUGGER SK_TYPE_ID("sk.editor_window.resource_debugger", 0x91688227d6e015f7ULL, 0xc6cec6903697bffcULL)
#define SK_EDITOR_WINDOW_PACKAGES SK_TYPE_ID("sk.editor_window.packages", 0xb37094d078993433ULL, 0x49566c384f606a20ULL)
#define SK_EDITOR_WINDOW_SETTINGS SK_TYPE_ID("sk.editor_window.settings", 0x4d7cc6d17f1b1dc7ULL, 0xe877d738f1d4b09dULL)

/**
 * Register the 14 main editor windows as SK_EDITOR_WINDOW_IMPL_TYPE_ID impls
 * on @p app_context. Call once at editor boot (after
 * sk_editor_workspace_register_impls). Repeated calls append duplicates;
 * callers register once.
 */
void sk_editor_windows_register_impls(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
