#pragma once

/**
 * @file scene_view_window.h
 * @brief Scene Viewport window (APX-372): v2 migration.
 *
 * Port of main-branch Skore::SceneViewWindow (migration manifest §3.9) onto
 * the v2 editor shell. Per the manifest, real scene rendering is **out of
 * scope** (§2.3 / §4): this window ports the shell only — a dockable
 * Center/Scene window whose toolbar + options popups mirror the C++ Draw()
 * chrome, and whose viewport area draws a **placeholder texture** (a CPU
 * generated RGBA8 checkerboard/gradient "Scene Viewport" mock) instead of a
 * rendered scene. Gizmo / camera / entity-picking controls are mocks:
 *
 *  - Gizmo operation (select/translate/rotate/scale), world/local mode, snap
 *    toggle + snap size, 2D/3D view type, play/stop simulation and the
 *    viewport-options toggles (drawIcons, drawGrid, drawSelectionOutline,
 *    drawDebugPhysics, showAllPhysicsShapes, lockCameraFrustum, drawMeshAABB,
 *    drawNavMesh, drawComponentGizmos) are session-only state on the window
 *    (C++ EditorSerialize fields; state persistence is a separate layout
 *    work item). The camera options popup edits cameraFov / snap.
 *  - `ViewEntity` (C++ double-click "frame in SceneView" from the Entity
 *    Tree) records the requested entity RID on the window; there is no
 *    camera to move in v2 yet.
 *  - `IsSceneInteractionDisabled` mirrors the C++ (simulation running or a
 *    UI document is selected; v2 tracks a session-only read-only flag).
 *  - Duplicate/Delete context-menu hotkeys (Ctrl+D / Delete) are no-ops
 *    until the v2 entity ops exist.
 *
 * The viewport texture follows the editor_icons host-binding pattern
 * (APX-367): the GPU host creates a placeholder texture registry
 * (sk_editor_scene_view_texture_create) and registers it on the app context;
 * the window resolves it (sk_editor_scene_view_texture_resolve) for the
 * texture id + source size and draws a `widget_image_rect` letterboxed to
 * the content area (aspect handling mirrors C++ `size = contentRegionAvail`
 * + `aspectRatio = width/height`). The host passes the combined host image
 * array (zero slot + icon atlas + this texture at
 * SK_EDITOR_SCENE_VIEW_TEX_SLOT) to the UI renderer every frame the window
 * is drawn. Without a registered texture the window still draws the chrome
 * and the image node falls back to a bordered placeholder (texture id 0).
 *
 * Public entry points (OpenSceneView / ViewEntity / IsSceneInteractionDisabled /
 * AddMenuItem / the viewport state getters+setters) follow the APX-365
 * pattern: never exported as free symbols. The window publishes one
 * process-lifetime `sk_editor_scene_view_ops_t` table registered with
 * `app_api->add_impl` under SK_EDITOR_SCENE_VIEW_OPS_TYPE_ID; callers look
 * it up via `sk_editor_scene_view_ops(ctx, api)` and call through the
 * pointers. Every viewport state change (toolbar / popups / ops setters)
 * announces through the notify.h VIEWPORT_STATE observer struct.
 */

#include "editor_window.h"
#include "editor_icons.h" /* SK_EDITOR_ICON_COUNT (host image slot convention) */
#include "main_windows.h"
#include "notify.h"
#include "project_browser_window.h" /* sk_editor_menu_item_desc_t */
#include "render_device.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_SCENE_VIEW_OPS_TYPE_ID SK_TYPE_ID("sk.editor.scene_view.ops", 0x8f41b3d26c0a5e77ULL, 0x1a92e8f35b7c04ddULL)

/** Type id under which a live placeholder texture registry is registered on the app context. */
#define SK_EDITOR_SCENE_VIEW_TEX_TYPE_ID SK_TYPE_ID("sk.editor.scene_view.tex", 0x2b57c8e9d1f4a063ULL, 0x9c18e07a53d2f8b4ULL)

/* C++ SceneViewWindow enums (ViewType / ImGuizmo::OPERATION). */
#define SK_EDITOR_SCENE_VIEW_TYPE_3D 0
#define SK_EDITOR_SCENE_VIEW_TYPE_2D 1
#define SK_EDITOR_GIZMO_OP_SELECT 0u
#define SK_EDITOR_GIZMO_OP_TRANSLATE 1u
#define SK_EDITOR_GIZMO_OP_ROTATE 2u
#define SK_EDITOR_GIZMO_OP_SCALE 3u
/* C++ guizmoMode: 0 world, 1 local. */
#define SK_EDITOR_GIZMO_MODE_WORLD 0
#define SK_EDITOR_GIZMO_MODE_LOCAL 1

/**
 * Host image slot of the Scene View placeholder texture. The host binds one
 * combined images array: slot 0 stays zero ("no texture"), slots 1..5 are
 * the icon atlas (sk_editor_icons_views, APX-367), slot
 * SK_EDITOR_SCENE_VIEW_TEX_SLOT is this window's placeholder view. The
 * window uses the resolved texture id in widget_image_rect; out-of-range /
 * missing host bindings fall back to a white quad (tests, no-GPU hosts).
 */
#define SK_EDITOR_SCENE_VIEW_TEX_SLOT (1u + (u32)SK_EDITOR_ICON_COUNT)

/** Placeholder texture lookup result (texture id + source pixel size). */
typedef struct sk_editor_scene_view_tex_t {
	u32 texture_id; /* index into the host images array (>= 1) */
	u32 width;		/* source pixel width (aspect-ratio base) */
	u32 height;		/* source pixel height */
} sk_editor_scene_view_tex_t;

/** Opaque placeholder texture registry (owns the GPU texture/view). */
typedef struct sk_editor_scene_view_tex_registry_t sk_editor_scene_view_tex_registry_t;

/**
 * Scene View public table. Looked up via add_impl; never call the
 * implementations by symbol. Every function takes the app context / api pair
 * (plus the window instance when the operation is per-instance).
 */
typedef struct sk_editor_scene_view_ops_t {
	/** Open (or focus) the Scene Viewport window (C++ OpenSceneView). */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/**
	 * Frame @p entity (C++ ViewEntity(RID); the Entity* overload is not
	 * portable — v2 has RIDs). MOCK: records the RID on the window; no
	 * camera exists yet.
	 */
	void (*view_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid);

	/** Last entity requested through view_entity (SK_RID_ZERO before any). */
	sk_rid_t (*get_last_viewed_entity)(const sk_editor_window_t* window);

	/** C++ IsSceneInteractionDisabled: simulation running or read-only. */
	i32 (*is_scene_interaction_disabled)(const sk_editor_window_t* window);

	/** Register a context-menu entry (C++ SceneViewWindow::AddMenuItem; Duplicate/Delete hotkeys). */
	void (*add_menu_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_desc_t* item);

	/** C++ sceneEditor accessor — v2 has no SceneEditor; always NULL (mock). */
	void* (*get_scene_editor)(const sk_editor_window_t* window);

	/** Copy the current viewport state snapshot into @p out. */
	void (*get_state)(const sk_editor_window_t* window, sk_editor_viewport_state_t* out);

	/** Replace the viewport state snapshot (announces VIEWPORT_STATE). */
	void (*set_state)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const sk_editor_viewport_state_t* state);

	/** C++ sceneEditor->StartSimulation (MOCK: session flag only). */
	void (*start_simulation)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** C++ sceneEditor->StopSimulation (MOCK: session flag only). */
	void (*stop_simulation)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** C++ SceneEditor::SetReadOnly (drives IsSceneInteractionDisabled). */
	void (*set_read_only)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 ro);

	/** Viewport display size the placeholder texture is drawn at (logical px, 0 before layout). */
	void (*get_viewport_size)(const sk_editor_window_t* window, f32* out_w, f32* out_h);

	/** Aspect ratio of the last laid-out viewport (C++ aspectRatio; 0 before layout). */
	f32 (*get_aspect_ratio)(const sk_editor_window_t* window);
} sk_editor_scene_view_ops_t;

/** First registered Scene View ops table, or NULL before register. */
SK_FINLINE const sk_editor_scene_view_ops_t* sk_editor_scene_view_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_scene_view_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_SCENE_VIEW_OPS_TYPE_ID);
}

/**
 * Register the Scene View window impl + ops table on @p app_context.
 * Idempotent per context. Call before sk_editor_windows_register_impls so
 * the real impl wins window_open over the APX-330 scaffold.
 * Pair with sk_editor_scene_view_shutdown in tests.
 */
void sk_editor_scene_view_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/** Drop the ops/window impls and free per-context class state. No-op when the
 *  window was never registered on this context. Close open instances first. */
void sk_editor_scene_view_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/* ------------------------------------------------------------------ */
/*  Placeholder viewport texture (host-created, like editor_icons)    */
/* ------------------------------------------------------------------ */

/**
 * Create the Scene View placeholder texture: generate a small RGBA8 mock
 * viewport image (checkerboard grid + gradient + border, 16:9) on the CPU,
 * upload it through @p rd (staging buffer → barrier → copy_buffer_to_texture
 * → texture view), and register the resulting binding on @p app_context so
 * windows resolve it. @p app_context / @p app_api may be NULL (pure upload
 * path used by tests / hosts that only need the view).
 * @return Registry, or NULL on allocation / device failure.
 */
sk_editor_scene_view_tex_registry_t* sk_editor_scene_view_texture_create(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_render_device_api_t* rd,
																		 sk_render_device_t device);

/** Destroy the registry and all owned GPU resources. Safe on NULL. */
void sk_editor_scene_view_texture_destroy(sk_editor_scene_view_tex_registry_t* registry);

/**
 * Host image binding for the UI renderer: the placeholder texture view.
 * @p out_count receives 1. The host places it at index
 * SK_EDITOR_SCENE_VIEW_TEX_SLOT in the combined images array passed to
 * sk_ui_renderer_images_t (zero slot + icons + this).
 */
const sk_texture_view_t* sk_editor_scene_view_texture_view(const sk_editor_scene_view_tex_registry_t* registry);

/** Register a live registry on the app context (windows resolve it). */
void sk_editor_scene_view_texture_register(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_scene_view_tex_registry_t* registry);

/** Resolve the registered registry, or NULL when none is registered. */
sk_editor_scene_view_tex_registry_t* sk_editor_scene_view_texture_resolve(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/** Placeholder texture lookup (texture_id + source size). Zero size when @p registry is NULL. */
sk_editor_scene_view_tex_t sk_editor_scene_view_texture_get(const sk_editor_scene_view_tex_registry_t* registry);

#ifdef __cplusplus
}
#endif
