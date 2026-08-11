/**
 * @file imgui_shell.c
 * @brief Immediate-mode editor shell stand-in for dual-stack coexistence.
 */

#include "imgui_shell.h"

#include "allocator.h"

#include <stdio.h>
#include <string.h>

enum { HIERARCHY_ENTITY_COUNT = 5 };

struct sk_editor_imgui_shell_t {
	f32 display_w;
	f32 display_h;
	f32 pointer_x;
	f32 pointer_y;
	i32 pointer_down;
	i32 pointer_clicked;

	i32 want_mouse;
	i32 want_keyboard;
	i32 hovered;
	i32 selected; /* entity index or -1 */

	/* Hierarchy panel placement (bottom-left dock stand-in is top-left here). */
	f32 panel_x;
	f32 panel_y;
	f32 panel_w;
	f32 panel_h;

	sk_editor_imgui_draw_item_t items[SK_EDITOR_IMGUI_MAX_DRAW_ITEMS];
	u32 item_count;

	const_chr_t entity_names[HIERARCHY_ENTITY_COUNT];
};

static u32 shell_pack_rgba(f32 r, f32 g, f32 b, f32 a) {
	return ((u32)(r * 255.0f + 0.5f)) | (((u32)(g * 255.0f + 0.5f)) << 8) | (((u32)(b * 255.0f + 0.5f)) << 16) | (((u32)(a * 255.0f + 0.5f)) << 24);
}

static i32 shell_point_in(f32 x, f32 y, f32 rx, f32 ry, f32 rw, f32 rh) {
	return (x >= rx && y >= ry && x < rx + rw && y < ry + rh) ? 1 : 0;
}

static void shell_push_rect(sk_editor_imgui_shell_t* shell, f32 x, f32 y, f32 w, f32 h, u32 color) {
	sk_editor_imgui_draw_item_t* it;
	if (shell->item_count >= SK_EDITOR_IMGUI_MAX_DRAW_ITEMS) {
		return;
	}
	it = &shell->items[shell->item_count++];
	memset(it, 0, sizeof(*it));
	it->kind = SK_EDITOR_IMGUI_DRAW_RECT;
	it->x = x;
	it->y = y;
	it->w = w;
	it->h = h;
	it->color_rgba8 = color;
}

static void shell_push_text(sk_editor_imgui_shell_t* shell, f32 x, f32 y, const_chr_t text, u32 color) {
	sk_editor_imgui_draw_item_t* it;
	if (shell->item_count >= SK_EDITOR_IMGUI_MAX_DRAW_ITEMS) {
		return;
	}
	it = &shell->items[shell->item_count++];
	memset(it, 0, sizeof(*it));
	it->kind = SK_EDITOR_IMGUI_DRAW_TEXT;
	it->x = x;
	it->y = y;
	it->w = 0.0f;
	it->h = 14.0f;
	it->color_rgba8 = color;
	if (text != NULL) {
		(void)snprintf(it->text, sizeof(it->text), "%s", text);
	}
}

sk_editor_imgui_shell_t* sk_editor_imgui_shell_create(void) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_imgui_shell_t* shell = (sk_editor_imgui_shell_t*)alloc->alloc(alloc->instance, sizeof(sk_editor_imgui_shell_t));
	if (shell == NULL) {
		return NULL;
	}
	memset(shell, 0, sizeof(*shell));
	shell->selected = -1;
	shell->panel_x = 8.0f;
	shell->panel_y = 8.0f;
	shell->panel_w = 220.0f;
	shell->panel_h = 280.0f;
	shell->entity_names[0] = "Root";
	shell->entity_names[1] = "  Camera";
	shell->entity_names[2] = "  Light";
	shell->entity_names[3] = "  Mesh_A";
	shell->entity_names[4] = "  Mesh_B";
	return shell;
}

void sk_editor_imgui_shell_destroy(sk_editor_imgui_shell_t* shell) {
	const sk_allocator_t* alloc;
	if (shell == NULL) {
		return;
	}
	alloc = sk_allocator_default();
	alloc->free(alloc->instance, shell);
}

void sk_editor_imgui_shell_begin_frame(sk_editor_imgui_shell_t* shell, f32 display_w, f32 display_h) {
	if (shell == NULL) {
		return;
	}
	shell->display_w = display_w > 1.0f ? display_w : 1.0f;
	shell->display_h = display_h > 1.0f ? display_h : 1.0f;
	shell->item_count = 0u;
	shell->want_mouse = 0;
	shell->want_keyboard = 0;
	shell->hovered = 0;
	/* Keep hierarchy docked top-left; clamp to display. */
	if (shell->panel_w > shell->display_w - 16.0f) {
		shell->panel_w = shell->display_w - 16.0f;
	}
	if (shell->panel_h > shell->display_h - 16.0f) {
		shell->panel_h = shell->display_h - 16.0f;
	}
}

void sk_editor_imgui_shell_set_pointer(sk_editor_imgui_shell_t* shell, f32 x, f32 y, i32 button_down, i32 clicked) {
	if (shell == NULL) {
		return;
	}
	shell->pointer_x = x;
	shell->pointer_y = y;
	shell->pointer_down = button_down != 0 ? 1 : 0;
	shell->pointer_clicked = clicked != 0 ? 1 : 0;
}

void sk_editor_imgui_shell_draw(sk_editor_imgui_shell_t* shell) {
	f32 x;
	f32 y;
	f32 w;
	f32 h;
	f32 row_y;
	i32 i;
	u32 bg;
	u32 title_bg;
	u32 text_col;
	u32 sel_bg;

	if (shell == NULL) {
		return;
	}

	x = shell->panel_x;
	y = shell->panel_y;
	w = shell->panel_w;
	h = shell->panel_h;
	bg = shell_pack_rgba(0.12f, 0.13f, 0.16f, 0.94f);
	title_bg = shell_pack_rgba(0.18f, 0.20f, 0.26f, 1.0f);
	text_col = shell_pack_rgba(0.92f, 0.92f, 0.95f, 1.0f);
	sel_bg = shell_pack_rgba(0.22f, 0.40f, 0.70f, 0.85f);

	/* Entire panel background + title — rebuilt every frame (immediate). */
	shell_push_rect(shell, x, y, w, h, bg);
	shell_push_rect(shell, x, y, w, 24.0f, title_bg);
	shell_push_text(shell, x + 8.0f, y + 5.0f, "Hierarchy (ImGui path)", text_col);

	if (shell_point_in(shell->pointer_x, shell->pointer_y, x, y, w, h)) {
		shell->hovered = 1;
	}

	row_y = y + 32.0f;
	for (i = 0; i < HIERARCHY_ENTITY_COUNT; ++i) {
		f32 row_h = 20.0f;
		i32 over = shell_point_in(shell->pointer_x, shell->pointer_y, x + 4.0f, row_y, w - 8.0f, row_h);
		if (over) {
			shell->hovered = 1;
			if (shell->pointer_clicked) {
				shell->selected = i;
			}
		}
		if (shell->selected == i) {
			shell_push_rect(shell, x + 4.0f, row_y, w - 8.0f, row_h, sel_bg);
		}
		shell_push_text(shell, x + 10.0f, row_y + 2.0f, shell->entity_names[i], text_col);
		row_y += row_h + 2.0f;
	}

	/* Footer hint rebuilt every frame (immediate-mode text). */
	shell_push_text(shell, x + 8.0f, y + h - 20.0f, "Immediate rebuild / frame", shell_pack_rgba(0.55f, 0.55f, 0.60f, 1.0f));
}

void sk_editor_imgui_shell_end_frame(sk_editor_imgui_shell_t* shell) {
	if (shell == NULL) {
		return;
	}
	shell->want_mouse = shell->hovered;
	/* Hierarchy has no text field; keyboard capture stays off. */
	shell->want_keyboard = 0;
	shell->pointer_clicked = 0;
}

i32 sk_editor_imgui_shell_want_capture_mouse(const sk_editor_imgui_shell_t* shell) {
	return shell != NULL ? shell->want_mouse : 0;
}

i32 sk_editor_imgui_shell_want_capture_keyboard(const sk_editor_imgui_shell_t* shell) {
	return shell != NULL ? shell->want_keyboard : 0;
}

const sk_editor_imgui_draw_item_t* sk_editor_imgui_shell_draw_items(const sk_editor_imgui_shell_t* shell, u32* out_count) {
	if (out_count != NULL) {
		*out_count = shell != NULL ? shell->item_count : 0u;
	}
	return shell != NULL ? shell->items : NULL;
}

i32 sk_editor_imgui_shell_selected_index(const sk_editor_imgui_shell_t* shell) {
	return shell != NULL ? shell->selected : -1;
}

void sk_editor_imgui_shell_hierarchy_rect(const sk_editor_imgui_shell_t* shell, f32* x, f32* y, f32* w, f32* h) {
	if (shell == NULL) {
		if (x) {
			*x = 0.0f;
		}
		if (y) {
			*y = 0.0f;
		}
		if (w) {
			*w = 0.0f;
		}
		if (h) {
			*h = 0.0f;
		}
		return;
	}
	if (x) {
		*x = shell->panel_x;
	}
	if (y) {
		*y = shell->panel_y;
	}
	if (w) {
		*w = shell->panel_w;
	}
	if (h) {
		*h = shell->panel_h;
	}
}
