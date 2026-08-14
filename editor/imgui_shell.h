#pragma once

/**
 * @file imgui_shell.h
 * @brief Immediate-mode stand-in for the remaining Dear ImGui editor path.
 *
 * v2 does not vendor Dear ImGui yet. This module models the **frame contract**
 * main's ImGui stack exposes (NewFrame → draw panels → EndFrame / capture flags)
 * so the dual host can prove coexistence with the sk-ui Console port without
 * expanding APX-139 into a full ImGui rehost.
 *
 * One self-contained leftover panel is drawn each frame in **immediate** style
 * (Hierarchy stub rebuilt from scratch — no retained node handles).
 *
 * When a real ImGui integration lands, replace this shell's begin/end/draw with
 * ImGui::NewFrame / workspace DrawWindows / ImGui::Render and keep the same
 * host arbitration surface (want_capture_*).
 *
 * Host-facing surface (APX-328): the functions below are the internal wiring
 * behind sk_editor_api_t. Hosts and tests resolve the editor only via
 * app_api->get_api(ctx, SK_EDITOR_API_TYPE_ID) (see editor_api.h).
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sk_editor_imgui_shell_t sk_editor_imgui_shell_t;

/** Simple draw item kinds emitted by the shell (CPU-side, no GPU). */
typedef enum sk_editor_imgui_draw_kind_t {
	SK_EDITOR_IMGUI_DRAW_RECT = 0,
	SK_EDITOR_IMGUI_DRAW_TEXT = 1,
} sk_editor_imgui_draw_kind_t;

typedef struct sk_editor_imgui_draw_item_t {
	sk_editor_imgui_draw_kind_t kind;
	f32 x;
	f32 y;
	f32 w;
	f32 h;
	u32 color_rgba8; /**< R in low byte. */
	char text[96];	 /**< Valid when kind == TEXT. */
} sk_editor_imgui_draw_item_t;

/** Max immediate draw items per frame. */
#define SK_EDITOR_IMGUI_MAX_DRAW_ITEMS 128u

/** Create shell (default Hierarchy panel open). */
sk_editor_imgui_shell_t* sk_editor_imgui_shell_create(void);

/** Destroy shell. Safe on NULL. */
void sk_editor_imgui_shell_destroy(sk_editor_imgui_shell_t* shell);

/**
 * Begin a new immediate frame (clears last draw list / per-frame hover).
 * Call once per host frame before feeding input and drawing.
 */
void sk_editor_imgui_shell_begin_frame(sk_editor_imgui_shell_t* shell, f32 display_w, f32 display_h);

/**
 * Inject pointer state in **logical** pixels (same space as sk-ui layout).
 * @p button_down is left button held; @p clicked is edge this frame.
 */
void sk_editor_imgui_shell_set_pointer(sk_editor_imgui_shell_t* shell, f32 x, f32 y, i32 button_down, i32 clicked);

/**
 * Draw remaining ImGui-path panels for this frame (Hierarchy stub).
 * Rebuilds the draw list from scratch (immediate style).
 */
void sk_editor_imgui_shell_draw(sk_editor_imgui_shell_t* shell);

/**
 * End frame: finalize want_capture_* from hover/active widgets.
 * Call after draw, before the host reads capture flags.
 */
void sk_editor_imgui_shell_end_frame(sk_editor_imgui_shell_t* shell);

/** Non-zero if the shell wants mouse (hovered/active widget this frame). */
i32 sk_editor_imgui_shell_want_capture_mouse(const sk_editor_imgui_shell_t* shell);

/** Non-zero if the shell wants keyboard (focused text-like control). */
i32 sk_editor_imgui_shell_want_capture_keyboard(const sk_editor_imgui_shell_t* shell);

/** Immediate draw list for this frame (valid until next begin_frame). */
const sk_editor_imgui_draw_item_t* sk_editor_imgui_shell_draw_items(const sk_editor_imgui_shell_t* shell, u32* out_count);

/** Selected hierarchy entity index (-1 if none). */
i32 sk_editor_imgui_shell_selected_index(const sk_editor_imgui_shell_t* shell);

/** Panel rect (logical) for hit-region tests / dual host layout. */
void sk_editor_imgui_shell_hierarchy_rect(const sk_editor_imgui_shell_t* shell, f32* x, f32* y, f32* w, f32* h);

#ifdef __cplusplus
}
#endif
