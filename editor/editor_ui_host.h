#pragma once

/**
 * @file editor_ui_host.h
 * @brief Dual-stack editor UI host: sk-ui Console + ImGui-path shell.
 *
 * Proves APX-139 coexistence: both systems active in the same frame with
 * documented input arbitration and render ordering.
 *
 * Frame order (see docs/ui-editor-migration.md):
 *   1. begin_frame (sizes / content scale)
 *   2. input arbitration → dispatch to sk-ui and/or imgui shell
 *   3. console_panel_sync (retained state → nodes)
 *   4. sk-ui: style_resolve → layout → apply_scale → paint
 *   5. imgui shell: begin → draw → end (immediate rebuild)
 *   6. render order: ImGui-path draw list first, sk-ui draw list second (on top)
 */

#include "common.h"
#include "console_panel.h"
#include "imgui_shell.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sk_editor_ui_host_t sk_editor_ui_host_t;

/** Which system received the last pointer event. */
typedef enum sk_editor_ui_input_target_t {
	SK_EDITOR_UI_INPUT_NONE = 0,
	SK_EDITOR_UI_INPUT_SK_UI = 1,
	SK_EDITOR_UI_INPUT_IMGUI = 2,
} sk_editor_ui_input_target_t;

/**
 * Create dual host: owns sk-ui context, console panel, and imgui shell.
 * @param ui         Live sk_ui_api_t (init already called or will be called here).
 * @param logger_api Host logger table used to register the console sink.
 * @param log_ctx    Host logger context that owns sinks.
 * @return Host, or NULL on failure.
 */
sk_editor_ui_host_t* sk_editor_ui_host_create(const sk_ui_api_t* ui, const sk_logger_api_t* logger_api, sk_logger_context_t* log_ctx);

/** Destroy host and all owned UI resources. Safe on NULL. */
void sk_editor_ui_host_destroy(sk_editor_ui_host_t* host);

/** Owned sk-ui context. */
sk_ui_context_t* sk_editor_ui_host_context(const sk_editor_ui_host_t* host);

/** Owned console panel. */
sk_editor_console_panel_t* sk_editor_ui_host_console(const sk_editor_ui_host_t* host);

/** Owned imgui shell. */
sk_editor_imgui_shell_t* sk_editor_ui_host_imgui(const sk_editor_ui_host_t* host);

/**
 * Run one dual-stack frame.
 * @param width/height Logical root size.
 * @param scale_x/y    Content scale for layout_apply_scale.
 * @return 0 on success.
 */
i32 sk_editor_ui_host_frame(sk_editor_ui_host_t* host, f32 width, f32 height, f32 scale_x, f32 scale_y);

/**
 * Feed a pointer move/button event with arbitration.
 * sk-ui Console gets first refusal when the pointer is over its laid-out rect
 * or when sk-ui already holds pointer capture / keyboard focus; otherwise the
 * event goes to the imgui shell.
 */
void sk_editor_ui_host_pointer(sk_editor_ui_host_t* host, f32 x, f32 y, i32 button, i32 down);

/** Feed a key event (routed by wants_keyboard priority: sk-ui then imgui). */
void sk_editor_ui_host_key(sk_editor_ui_host_t* host, i32 key, i32 down, u32 mods);

/** Feed UTF-8 text (same priority as key). */
void sk_editor_ui_host_text(sk_editor_ui_host_t* host, const_chr_t utf8);

/** Last pointer routing target (for tests / diagnostics). */
sk_editor_ui_input_target_t sk_editor_ui_host_last_pointer_target(const sk_editor_ui_host_t* host);

/** sk-ui draw list after the last frame (may be empty). */
const sk_ui_draw_list_t* sk_editor_ui_host_sk_ui_draw_list(const sk_editor_ui_host_t* host);

/** ImGui-path draw items after the last frame. */
const sk_editor_imgui_draw_item_t* sk_editor_ui_host_imgui_draw_items(const sk_editor_ui_host_t* host, u32* out_count);

/**
 * Combined capture: non-zero if either stack wants mouse/keyboard.
 * Hosts skip viewport/gameplay input when set.
 */
i32 sk_editor_ui_host_want_capture_mouse(const sk_editor_ui_host_t* host);
i32 sk_editor_ui_host_want_capture_keyboard(const sk_editor_ui_host_t* host);

#ifdef __cplusplus
}
#endif
