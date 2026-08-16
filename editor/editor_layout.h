#pragma once

/**
 * @file editor_layout.h
 * @brief First-class workspace layouts (APX-368): save, load, switch, presets.
 *
 * A workspace layout is the live dock tree (sk-ui dock JSON) plus each open
 * window's optional save() blob. Switching captures the outgoing workspace
 * and restores the incoming one (saved layout or the shipped default preset)
 * without leaking window instances or double-registering impls.
 *
 * On-disk document (pretty JSON, AppFolder/Skore/EditorLayout.json):
 *
 *   { "version": 1, "active": <type>, "workspaces": [
 *       { "type": 1, "dock": "<dock json>", "windows": [
 *           { "id": "sk.editor_window.console" },
 *           { "id": "sk.editor_window.project_browser", "state": "{...}" }
 *       ]}
 *   ]}
 *
 * Version policy matches the dock format: only version 1 is accepted.
 * Unknown / missing window ids, a missing file, and unparseable JSON do
 * not crash — the host keeps or applies the default preset.
 *
 * Default presets are the four C++ workspace types from the migration
 * manifest (Scene / Graph / Animator / Material) with the InitDockSpace
 * split tree and the windows whose workspace_mask admits that type.
 */

#include "app.h"
#include "editor_window.h"

#ifdef __cplusplus
extern "C" {
#endif

/** On-disk / in-memory layout document version. */
#define SK_EDITOR_LAYOUT_VERSION 1

/** Type id for the per-context layout store (app registry). */
#define SK_EDITOR_LAYOUT_STORE_TYPE_ID SK_TYPE_ID("sk.editor_layout_store", 0xa4c81e7d2b0f93e1ULL, 0x61d5c8a04e7b2f19ULL)

/** One shipped default workspace preset (C++ EditorWorkspaceTypeDesc). */
typedef struct sk_editor_workspace_preset_t {
	u32 type_id;
	const_chr_t name;
} sk_editor_workspace_preset_t;

/** Number of shipped default presets (Scene, Graph, Animator, Material). */
u32 sk_editor_workspace_preset_count(void);

/** Preset at @p index, or NULL when out of range. */
const sk_editor_workspace_preset_t* sk_editor_workspace_preset_at(u32 index);

/** Preset for @p workspace_type_id, or NULL when that type is not shipped. */
const sk_editor_workspace_preset_t* sk_editor_workspace_preset_by_type(u32 workspace_type_id);

/**
 * Copy default-preset window dock ids for @p workspace_type_id (registered
 * impls whose workspace_mask admits the type, excluding on-demand mask 0).
 * @return Total count (NULL/0 queries the count only).
 */
u32 sk_editor_workspace_preset_window_ids(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_type_id, const_chr_t* out, u32 out_cap);

/**
 * Create the per-context layout store (if needed) and load EditorLayout.json
 * when it exists. A missing file is not an error. A stale / corrupt file is
 * ignored (store stays empty; callers apply the default preset).
 */
void sk_editor_layout_init(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/** Free the layout store. Safe when init was never called. */
void sk_editor_layout_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Override the persist path (tests). Empty / NULL restores the default
 * AppFolder/Skore/EditorLayout.json. Does not load; call load afterwards
 * if the new path should be read.
 */
void sk_editor_layout_set_path(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path);

/** Persist path currently in use (borrowed; valid until set_path / shutdown). */
const_chr_t sk_editor_layout_path(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Capture every open workspace into the store and write the document.
 * @return 0 on success, non-zero on I/O or serialization failure.
 */
i32 sk_editor_layout_save(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Replace the in-memory store from the persist path.
 * @return 0 on success (including missing file → empty store), non-zero
 *         when the file exists but is stale / corrupt (store unchanged).
 */
i32 sk_editor_layout_load(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/** Non-zero when the store holds a captured layout for @p workspace_type_id. */
i32 sk_editor_layout_has_saved(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_type_id);

/**
 * Snapshot @p workspace's dock JSON + each open window's save() blob into
 * the store (marks dirty). Windows without save() are recorded by id only.
 * @return 0 on success.
 */
i32 sk_editor_workspace_capture(sk_editor_workspace_t* workspace);

/**
 * Apply the saved layout for @p workspace's type, or the shipped default
 * preset when nothing is saved. Unknown window ids are skipped; a corrupt
 * dock blob falls back to the default split tree.
 * @return 0 on success.
 */
i32 sk_editor_workspace_restore(sk_editor_workspace_t* workspace);

/**
 * Drop any saved layout for @p workspace's type and rebuild the shipped
 * default preset (C++ ResetLayout).
 */
void sk_editor_workspace_reset_to_preset(sk_editor_workspace_t* workspace);

#ifdef __cplusplus
}
#endif
