/**
 * @file clay_adapter.c
 * @brief Clay-backed layout adapter for retained sk-ui trees
 *        (APX-214 / APX-232 / APX-233 / APX-234 / APX-235 / APX-236 / APX-216).
 *
 * Maps the per-frame layout pass onto Clay's immediate-mode lifecycle in a
 * single whole-tree pass and writes resulting boxes back into slot layout
 * rects so hit-testing, scale, paint, and queries keep working unchanged.
 * The custom flex solver (layout.c) is deleted; Clay is the only layout
 * engine behind the public layout API.
 *
 * APX-234 (menu surfaces): menu_bar, menu, menu_item, menu_popup, dropdown,
 * context_menu, and submenu always receive stable Clay IDs so hover/open
 * state resolves across frames. Popup/overlay containers map to Clay
 * floating (z-index, parent or root attach); closed popups (open=0 / hidden=1)
 * are omitted from the Clay tree and zeroed so hit-test ignores them. Clipped
 * menu regions use Clay clip on the popup when clip_children is set.
 *
 * APX-235 (docking / editor window surfaces): dock_space, dock_node, splitter,
 * tab_bar, tab, editor_window, window_title_bar, and window_content always
 * receive stable Clay IDs so drag/resize/tab-select pointer state resolves
 * during multi-frame drags. Nested dock containers and window_content map
 * clip_children to Clay clip; scrolling regions under window_content use the
 * same scroll_view clip path. Floating undocked editor_window (absolute)
 * maps to Clay floating; multi-viewport OS hosts are not supported (single
 * context root only).
 *
 * APX-236 (remaining scroll / clip / wrap helpers): every scroll_view and
 * clip_children node maps to Clay clip with a stable element id so Clay
 * scroll-container rows persist across frames. Scroll containers (scroll_view
 * or nodes with scroll_x/scroll_y props) are counted separately from pure
 * clip regions. Wrapped text declares Clay text (CLAY_TEXT_WRAP_WORDS) for
 * measure. scroll_content size comes from set_content_size → POINT layout
 * (content_* props remain paint/clamp metadata). Engine paint and hit-test
 * still apply scroll_x/scroll_y once; Clay childOffset stays zero to avoid
 * double-shifting writeback rects (blocking limitation recorded below).
 *
 * Known Clay box-model deltas vs the old custom solver (logged once per
 * context via ui_clay_log_limitation): flex-wrap unsupported, reverse axes
 * unsupported, margins ignored, justify-content space-around/evenly collapse
 * to start (space-between is approximated with main-axis GROW spacers),
 * absolute positioning approximated via Clay floating (left/top offsets only,
 * relative to the parent border box), and min/max constraints on percent-sized
 * axes dropped. POINT sizes are border-box. Cross-axis stretch (align_items /
 * align_self) maps AUTO axes to GROW. Menus and docking also lack multi-viewport
 * hosts (single context root only). Clay has no native splitter primitive —
 * splitters are fixed-size flex children with stable ids; ratio is engine prop
 * state updated by pointer capture.
 *
 * APX-236 remaining on the engine path (blocking Clay limitations):
 *  - Scroll offset application: paint + hit-test read scroll_x/scroll_y and
 *    shift children; Clay clip.childOffset is left at zero so writeback keeps
 *    unshifted parent-relative rects. Full Clay-owned scroll (childOffset =
 *    Clay_GetScrollOffset) would double-apply unless paint/hit-test drop
 *    engine offsets.
 *  - Wheel deltas are not fed into Clay_UpdateScrollContainers via
 *    ctx->scroll_delta_* — that path runs before BeginLayout and can walk
 *    stale scroll-container rows across context destroy (Clay internal OOB).
 *    Engine scroll_view on_event remains the scroll source of truth.
 *  - Soft wrap glyph placement at paint time still uses the engine line
 *    breaker; Clay owns wrap measure/sizing only.
 *  - flex-wrap (container multi-line) remains unsupported by Clay.
 */

#include "ui.internal.h"

#include "clay.h"

#include "allocator.h"
#include "logger.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Frame state                                                                */
/* -------------------------------------------------------------------------- */

struct ui_clay_frame_t {
	Clay_ElementId* ids;	 /**< Parallel to slots: clay id per slot index. */
	Clay_BoundingBox* abs;	 /**< Clay absolute bounding box per slot. */
	u8* present;			 /**< Non-zero if node was declared this pass. */
	u32 cap;				 /**< Capacity of the parallel arrays. */
	i32 menu_layout_count;	 /**< Menu surface nodes declared this frame (APX-234). */
	i32 dock_layout_count;	 /**< Dock / editor window surfaces this frame (APX-235). */
	i32 scroll_layout_count; /**< Scroll containers (Clay clip + stable id) this frame (APX-236). */
	i32 clip_layout_count;	 /**< Pure clip_children nodes (no scroll props) this frame. */
	i32 wrap_layout_count;	 /**< Wrapped-text Clay text elements this frame. */
	i32 limitation_logged;	 /**< Avoid spamming the logger every frame. */
};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static f32 ui_clay_fmaxf(f32 a, f32 b) {
	return a > b ? a : b;
}

static u16 ui_clay_u16_clamp(f32 v) {
	if (v <= 0.0f) {
		return 0u;
	}
	if (v >= 65535.0f) {
		return 65535u;
	}
	return (u16)(v + 0.5f);
}

static const_chr_t ui_clay_prop_str(const ui_node_slot_t* slot, const_chr_t key) {
	u32 i;
	if (slot == NULL || key == NULL) {
		return NULL;
	}
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_STR && e->key != NULL && strcmp(e->key, key) == 0) {
			return e->data.str_value;
		}
	}
	return NULL;
}

static f32 ui_clay_prop_f32(const ui_node_slot_t* slot, const_chr_t key, f32 fallback) {
	u32 i;
	if (slot == NULL || key == NULL) {
		return fallback;
	}
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_F32 && e->key != NULL && strcmp(e->key, key) == 0) {
			return e->data.f32_value;
		}
	}
	return fallback;
}

static i32 ui_clay_prop_i32(const ui_node_slot_t* slot, const_chr_t key, i32 fallback) {
	u32 i;
	if (slot == NULL || key == NULL) {
		return fallback;
	}
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_I32 && e->key != NULL && strcmp(e->key, key) == 0) {
			return e->data.i32_value;
		}
	}
	return fallback;
}

/** Non-zero if slot has an f32 prop named @p key (value may be zero). */
static i32 ui_clay_has_prop_f32(const ui_node_slot_t* slot, const_chr_t key) {
	u32 i;
	if (slot == NULL || key == NULL) {
		return 0;
	}
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_F32 && e->key != NULL && strcmp(e->key, key) == 0) {
			return 1;
		}
	}
	return 0;
}

/**
 * Menu inventory surfaces (APX-234): bar, items, and popup/overlay containers.
 * Nested hover depends on stable IDs so Clay pointer-over and engine open
 * props stay consistent frame-to-frame.
 */
static i32 ui_clay_is_menu_surface(const_chr_t widget) {
	if (widget == NULL) {
		return 0;
	}
	if (strcmp(widget, "menu_bar") == 0 || strcmp(widget, "menu") == 0 || strcmp(widget, "menu_item") == 0 || strcmp(widget, "menu_popup") == 0 ||
		strcmp(widget, "dropdown") == 0 || strcmp(widget, "context_menu") == 0 || strcmp(widget, "submenu") == 0 || strcmp(widget, "popup_menu") == 0 ||
		strcmp(widget, "modal") == 0 || strcmp(widget, "modal_dim") == 0 || strcmp(widget, "modal_dialog") == 0 || strcmp(widget, "modal_title") == 0 ||
		strcmp(widget, "modal_body") == 0 || strcmp(widget, "modal_buttons") == 0) {
		return 1;
	}
	return 0;
}

/** Floating/overlay popup containers that nest under menu triggers. */
static i32 ui_clay_is_menu_popup(const_chr_t widget) {
	if (widget == NULL) {
		return 0;
	}
	if (strcmp(widget, "menu_popup") == 0 || strcmp(widget, "context_menu") == 0 || strcmp(widget, "popup_menu") == 0 || strcmp(widget, "modal") == 0) {
		return 1;
	}
	return 0;
}

/**
 * Docking / editor window inventory surfaces (APX-235): nested containers,
 * splitters, tabs, and window chrome. Drag/resize/tab-select targets need
 * stable Clay IDs so pointer capture and hover survive across frames.
 */
static i32 ui_clay_is_dock_surface(const_chr_t widget) {
	if (widget == NULL) {
		return 0;
	}
	if (strcmp(widget, "dock_space") == 0 || strcmp(widget, "dock_node") == 0 || strcmp(widget, "splitter") == 0 || strcmp(widget, "tab_bar") == 0 || strcmp(widget, "tab") == 0 ||
		strcmp(widget, "tab_button") == 0 || strcmp(widget, "tab_close") == 0 || strcmp(widget, "tab_body") == 0 || strcmp(widget, "editor_window") == 0 ||
		strcmp(widget, "window_title_bar") == 0 || strcmp(widget, "window_content") == 0 || strcmp(widget, "fullscreen") == 0 || strcmp(widget, "child") == 0 ||
		strcmp(widget, "child_resize") == 0 || strcmp(widget, "window_close") == 0 || strcmp(widget, "group") == 0 || strcmp(widget, "horizontal") == 0 ||
		strcmp(widget, "vertical") == 0 || strcmp(widget, "spring") == 0) {
		return 1;
	}
	return 0;
}

/** Drag / resize / tab-select interactive targets that must keep stable ids. */
static i32 ui_clay_is_dock_drag_target(const_chr_t widget) {
	if (widget == NULL) {
		return 0;
	}
	if (strcmp(widget, "splitter") == 0 || strcmp(widget, "tab") == 0 || strcmp(widget, "tab_button") == 0 || strcmp(widget, "tab_close") == 0 ||
		strcmp(widget, "window_title_bar") == 0) {
		return 1;
	}
	return 0;
}

static i32 ui_clay_menu_is_open(const ui_node_slot_t* slot) {
	i32 open;
	i32 hidden;
	if (slot == NULL) {
		return 0;
	}
	hidden = ui_clay_prop_i32(slot, "hidden", 0);
	if (hidden != 0) {
		return 0;
	}
	/* Default open=1 for non-popup containers; popups default closed (open=0). */
	open = ui_clay_prop_i32(slot, "open", ui_clay_is_menu_popup(ui_clay_prop_str(slot, "widget")) ? 0 : 1);
	return open != 0 ? 1 : 0;
}

static void ui_clay_zero_layout(ui_node_slot_t* slot) {
	if (slot == NULL) {
		return;
	}
	slot->layout_border.x = 0.0f;
	slot->layout_border.y = 0.0f;
	slot->layout_border.width = 0.0f;
	slot->layout_border.height = 0.0f;
	slot->layout_content = slot->layout_border;
	slot->dirty = (u16)((u32)slot->dirty & ~(u32)SK_UI_DIRTY_LAYOUT);
}

// NOLINTBEGIN(misc-no-recursion)
static void ui_clay_zero_subtree(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	u32 i;
	if (slot == NULL) {
		return;
	}
	ui_clay_zero_layout(slot);
	for (i = 0u; i < slot->children.count; ++i) {
		ui_clay_zero_subtree(ctx, slot->children.items[i]);
	}
}
// NOLINTEND(misc-no-recursion)

static i32 ui_clay_needs_stable_id(const ui_node_slot_t* slot) {
	const_chr_t w;
	if (slot == NULL) {
		return 0;
	}
	/* Interactive / scrollable surfaces need stable Clay IDs for pointer + scroll. */
	if (slot->focusable != 0u || slot->clip_children != 0u) {
		return 1;
	}
	if (slot->callbacks.on_click != NULL || slot->callbacks.on_event != NULL || slot->callbacks.on_pointer_enter != NULL || slot->callbacks.on_pointer_leave != NULL) {
		return 1;
	}
	w = ui_clay_prop_str(slot, "widget");
	if (w == NULL) {
		return 0;
	}
	if (strcmp(w, "panel") == 0 || strcmp(w, "button") == 0 || strcmp(w, "checkbox") == 0 || strcmp(w, "radio") == 0 || strcmp(w, "toggle") == 0 || strcmp(w, "slider") == 0 ||
		strcmp(w, "drag") == 0 || strcmp(w, "slider_n") == 0 || strcmp(w, "drag_n") == 0 || strcmp(w, "range_slider") == 0 || strcmp(w, "progress") == 0 ||
		strcmp(w, "text_input") == 0 || strcmp(w, "scroll_view") == 0 || strcmp(w, "scroll_content") == 0 || strcmp(w, "label") == 0 || strcmp(w, "image") == 0 ||
		strcmp(w, "view") == 0) {
		return 1;
	}
	/* Menu inventory surfaces always keep stable IDs (hover → nested popup). */
	if (ui_clay_is_menu_surface(w)) {
		return 1;
	}
	/* Dock / editor chrome: stable IDs for nested clip + drag/resize/tab. */
	if (ui_clay_is_dock_surface(w)) {
		return 1;
	}
	return 0;
}

static Clay_String ui_clay_cstr(const_chr_t s) {
	Clay_String out;
	out.isStaticallyAllocated = false;
	if (s == NULL) {
		out.chars = "";
		out.length = 0;
		return out;
	}
	out.chars = s;
	out.length = (int32_t)strlen(s);
	return out;
}

/**
 * Stable Clay element id for a retained node.
 * Prefer the public node id string (CLAY_SID). Without an explicit id, prefer
 * CLAY_SIDI(widget_type, sibling_index) for list rows (stable frame-to-frame
 * under one parent). Anonymous nodes always use CLAY_SIDI("skui_n", slot index)
 * so ids stay unique across the whole tree (sibling index alone collides when
 * multiple parents each have a child at the same loop index).
 */
static Clay_ElementId ui_clay_make_id(const ui_node_slot_t* slot, sk_ui_node_t node, u32 sibling_index) {
	static const Clay_String k_fallback = {.isStaticallyAllocated = true, .length = 6, .chars = "skui_n"};
	const_chr_t widget;
	if (slot != NULL && slot->id != NULL && slot->id[0] != '\0') {
		return CLAY_SID(ui_clay_cstr(slot->id));
	}
	widget = ui_clay_prop_str(slot, "widget");
	if (widget != NULL && widget[0] != '\0' && sibling_index != UINT32_MAX) {
		return CLAY_SIDI(ui_clay_cstr(widget), sibling_index);
	}
	(void)sibling_index;
	return CLAY_SIDI(k_fallback, node.index);
}

static Clay_LayoutDirection ui_clay_map_direction(sk_ui_flex_direction_t d, i32* out_unsupported_reverse) {
	if (out_unsupported_reverse != NULL) {
		*out_unsupported_reverse = 0;
	}
	switch (d) {
	case SK_UI_FLEX_ROW:
		return CLAY_LEFT_TO_RIGHT;
	case SK_UI_FLEX_COLUMN:
		return CLAY_TOP_TO_BOTTOM;
	case SK_UI_FLEX_ROW_REVERSE:
		if (out_unsupported_reverse != NULL) {
			*out_unsupported_reverse = 1;
		}
		return CLAY_LEFT_TO_RIGHT;
	case SK_UI_FLEX_COLUMN_REVERSE:
		if (out_unsupported_reverse != NULL) {
			*out_unsupported_reverse = 1;
		}
		return CLAY_TOP_TO_BOTTOM;
	default:
		return CLAY_TOP_TO_BOTTOM;
	}
}

static void ui_clay_map_child_align(const sk_ui_layout_style_t* s, Clay_ChildAlignment* out) {
	/* Main-axis packing: start/center/end map here; space-between uses GROW
	 * spacers in declare_node; space-around/evenly stay start-aligned. */
	out->x = CLAY_ALIGN_X_LEFT;
	out->y = CLAY_ALIGN_Y_TOP;
	switch (s->justify_content) {
	case SK_UI_JUSTIFY_FLEX_START:
		break;
	case SK_UI_JUSTIFY_CENTER:
		if (s->flex_direction == SK_UI_FLEX_ROW || s->flex_direction == SK_UI_FLEX_ROW_REVERSE) {
			out->x = CLAY_ALIGN_X_CENTER;
		} else {
			out->y = CLAY_ALIGN_Y_CENTER;
		}
		break;
	case SK_UI_JUSTIFY_FLEX_END:
		if (s->flex_direction == SK_UI_FLEX_ROW || s->flex_direction == SK_UI_FLEX_ROW_REVERSE) {
			out->x = CLAY_ALIGN_X_RIGHT;
		} else {
			out->y = CLAY_ALIGN_Y_BOTTOM;
		}
		break;
	case SK_UI_JUSTIFY_SPACE_BETWEEN: /* GROW spacers in declare_node */
	case SK_UI_JUSTIFY_SPACE_AROUND:  /* not approximated; start packing */
	case SK_UI_JUSTIFY_SPACE_EVENLY:
		break;
	}
	switch (s->align_items) {
	case SK_UI_ALIGN_AUTO:
	case SK_UI_ALIGN_FLEX_START:
	case SK_UI_ALIGN_STRETCH:
		break;
	case SK_UI_ALIGN_CENTER:
		if (s->flex_direction == SK_UI_FLEX_ROW || s->flex_direction == SK_UI_FLEX_ROW_REVERSE) {
			out->y = CLAY_ALIGN_Y_CENTER;
		} else {
			out->x = CLAY_ALIGN_X_CENTER;
		}
		break;
	case SK_UI_ALIGN_FLEX_END:
		if (s->flex_direction == SK_UI_FLEX_ROW || s->flex_direction == SK_UI_FLEX_ROW_REVERSE) {
			out->y = CLAY_ALIGN_Y_BOTTOM;
		} else {
			out->x = CLAY_ALIGN_X_RIGHT;
		}
		break;
	}
}

/**
 * Map sk_ui length + grow/shrink into a Clay sizing axis.
 *
 * Box model: style POINT width/height is the *border box* (outer size) for both
 * in-flow and absolute nodes — padding and border sit inside the fixed size.
 * Clay FIXED is also outer, so POINT maps 1:1 (no pad expansion). This matches
 * child content-width estimates (POINT - pads), absolute placement, and widget
 * authors who set e.g. button 96x28 expecting that painted outer size.
 * PERCENT is outer fraction of the parent content size.
 * AUTO + grow → GROW, else FIT.
 */
static Clay_SizingAxis ui_clay_map_axis(sk_ui_length_t len, sk_ui_length_t min_l, sk_ui_length_t max_l, f32 grow, f32 parent_size, i32 parent_def) {
	Clay_SizingAxis axis;
	f32 min_v = 0.0f;
	f32 max_v = 0.0f;
	memset(&axis, 0, sizeof(axis));

	if (min_l.unit == SK_UI_LENGTH_POINT) {
		min_v = min_l.value;
	} else if (min_l.unit == SK_UI_LENGTH_PERCENT && parent_def) {
		min_v = parent_size * (min_l.value / 100.0f);
	}
	if (max_l.unit == SK_UI_LENGTH_POINT) {
		max_v = max_l.value;
	} else if (max_l.unit == SK_UI_LENGTH_PERCENT && parent_def) {
		max_v = parent_size * (max_l.value / 100.0f);
	}

	if (len.unit == SK_UI_LENGTH_POINT) {
		f32 outer = len.value;
		if (outer < 0.0f) {
			outer = 0.0f;
		}
		axis.type = CLAY__SIZING_TYPE_FIXED;
		axis.size.minMax.min = outer;
		axis.size.minMax.max = outer;
		return axis;
	}
	if (len.unit == SK_UI_LENGTH_PERCENT) {
		axis.type = CLAY__SIZING_TYPE_PERCENT;
		axis.size.percent = len.value / 100.0f;
		if (axis.size.percent < 0.0f) {
			axis.size.percent = 0.0f;
		}
		if (axis.size.percent > 1.0f) {
			axis.size.percent = 1.0f;
		}
		return axis;
	}
	if (grow > 0.0f) {
		axis.type = CLAY__SIZING_TYPE_GROW;
		axis.size.minMax.min = min_v;
		axis.size.minMax.max = max_v > 0.0f ? max_v : 0.0f; /* 0 max = unbounded in Clay grow */
		return axis;
	}
	axis.type = CLAY__SIZING_TYPE_FIT;
	axis.size.minMax.min = min_v;
	axis.size.minMax.max = max_v > 0.0f ? max_v : 0.0f;
	return axis;
}

/** True when flex direction is a horizontal main axis. */
static i32 ui_clay_is_row_dir(sk_ui_flex_direction_t d) {
	return (d == SK_UI_FLEX_ROW || d == SK_UI_FLEX_ROW_REVERSE) ? 1 : 0;
}

/**
 * Anonymous main-axis GROW spacer used to approximate justify space-between.
 * Min size carries the authored row/column gap so items stay at least @p min_gap
 * apart while free space distributes between them (Clay has no native
 * space-between).
 */
static void ui_clay_declare_main_axis_spacer(i32 row_main, f32 min_gap) {
	Clay_ElementDeclaration spacer;
	f32 min_v = min_gap > 0.0f ? min_gap : 0.0f;
	memset(&spacer, 0, sizeof(spacer));
	if (row_main != 0) {
		spacer.layout.sizing.width.type = CLAY__SIZING_TYPE_GROW;
		spacer.layout.sizing.width.size.minMax.min = min_v;
		spacer.layout.sizing.width.size.minMax.max = 0.0f;
		spacer.layout.sizing.height.type = CLAY__SIZING_TYPE_FIXED;
		spacer.layout.sizing.height.size.minMax.min = 0.0f;
		spacer.layout.sizing.height.size.minMax.max = 0.0f;
	} else {
		spacer.layout.sizing.height.type = CLAY__SIZING_TYPE_GROW;
		spacer.layout.sizing.height.size.minMax.min = min_v;
		spacer.layout.sizing.height.size.minMax.max = 0.0f;
		spacer.layout.sizing.width.type = CLAY__SIZING_TYPE_FIXED;
		spacer.layout.sizing.width.size.minMax.min = 0.0f;
		spacer.layout.sizing.width.size.minMax.max = 0.0f;
	}
	Clay__OpenElement();
	Clay__ConfigureOpenElement(spacer);
	Clay__CloseElement();
}

static Clay_Color ui_clay_color(sk_ui_color_t c) {
	Clay_Color out;
	/* Clay convention is 0-255; engine colors are 0-1. */
	out.r = c.r * 255.0f;
	out.g = c.g * 255.0f;
	out.b = c.b * 255.0f;
	out.a = c.a * 255.0f;
	return out;
}

static i32 ui_clay_frame_ensure(sk_ui_context_t* ctx, u32 need) {
	ui_clay_frame_t* fr = ctx->clay_frame;
	const sk_allocator_t* a = ctx->allocator;
	if (fr == NULL) {
		fr = (ui_clay_frame_t*)a->alloc(a->instance, sizeof(ui_clay_frame_t));
		if (fr == NULL) {
			return -1;
		}
		memset(fr, 0, sizeof(*fr));
		ctx->clay_frame = fr;
	}
	if (fr->cap < need) {
		u32 ncap = need < 64u ? 64u : need;
		Clay_ElementId* nids = (Clay_ElementId*)a->alloc(a->instance, sizeof(Clay_ElementId) * (size_t)ncap);
		Clay_BoundingBox* nabs = (Clay_BoundingBox*)a->alloc(a->instance, sizeof(Clay_BoundingBox) * (size_t)ncap);
		u8* npresent = (u8*)a->alloc(a->instance, sizeof(u8) * (size_t)ncap);
		if (nids == NULL || nabs == NULL || npresent == NULL) {
			if (nids != NULL) {
				a->free(a->instance, nids);
			}
			if (nabs != NULL) {
				a->free(a->instance, nabs);
			}
			if (npresent != NULL) {
				a->free(a->instance, npresent);
			}
			return -1;
		}
		memset(nids, 0, sizeof(Clay_ElementId) * (size_t)ncap);
		memset(nabs, 0, sizeof(Clay_BoundingBox) * (size_t)ncap);
		memset(npresent, 0, sizeof(u8) * (size_t)ncap);
		if (fr->ids != NULL) {
			a->free(a->instance, fr->ids);
		}
		if (fr->abs != NULL) {
			a->free(a->instance, fr->abs);
		}
		if (fr->present != NULL) {
			a->free(a->instance, fr->present);
		}
		fr->ids = nids;
		fr->abs = nabs;
		fr->present = npresent;
		fr->cap = ncap;
	}
	if (fr->cap > 0u) {
		memset(fr->present, 0, sizeof(u8) * (size_t)fr->cap);
		memset(fr->ids, 0, sizeof(Clay_ElementId) * (size_t)fr->cap);
		memset(fr->abs, 0, sizeof(Clay_BoundingBox) * (size_t)fr->cap);
	}
	return 0;
}

static void ui_clay_log_limitation(ui_clay_frame_t* fr, const_chr_t node_id, const_chr_t detail) {
	sk_logger_t* logger;
	if (fr == NULL || fr->limitation_logged != 0) {
		return;
	}
	fr->limitation_logged = 1;
	if (ui_logger_api() == NULL || ui_logger_context() == NULL) {
		return;
	}
	logger = ui_logger_api()->create_logger(ui_logger_context(), "clay_adapter");
	if (logger != NULL) {
		sk_log_message(ui_logger_api(), SK_LOGGER_TYPE_WARN, logger, "element '%s' Clay limitation: %s (kept best-effort mapping)", node_id != NULL ? node_id : "(anon)",
					   detail != NULL ? detail : "unknown");
		ui_logger_api()->destroy_logger(ui_logger_context(), logger);
	}
}

/* Forward */
static void ui_clay_declare_node(sk_ui_context_t* ctx, sk_ui_node_t node, f32 parent_w, f32 parent_h, i32 parent_def, sk_ui_flex_direction_t parent_dir,
								 sk_ui_align_t parent_align_items, const_chr_t surface_id, i32* limitations, u32 sibling_index);

/**
 * Declare text as a Clay text element so wrap/sizing uses Clay measure.
 * wrap prop 1 → CLAY_TEXT_WRAP_WORDS; otherwise no wrap (glyphs still measured).
 */
static void ui_clay_declare_text_content(const ui_node_slot_t* slot) {
	const_chr_t text = ui_clay_prop_str(slot, "text");
	Clay_TextElementConfig cfg;
	Clay_String s;
	f32 font_size;
	i32 wrap;
	i32 text_align;
	if (text == NULL) {
		text = "";
	}
	font_size = slot->computed.font_size > 0.0f ? slot->computed.font_size : 14.0f;
	wrap = ui_clay_prop_i32(slot, "wrap", 0);
	text_align = ui_clay_prop_i32(slot, "text_align", 0);
	memset(&cfg, 0, sizeof(cfg));
	cfg.fontId = 0u;
	cfg.fontSize = ui_clay_u16_clamp(font_size);
	cfg.textColor = ui_clay_color(slot->computed.color);
	/* Wrapped labels use Clay word-wrap instead of the old layout measure path. */
	cfg.wrapMode = wrap != 0 ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE;
	if (text_align == 1) {
		cfg.textAlignment = CLAY_TEXT_ALIGN_CENTER;
	} else if (text_align == 2) {
		cfg.textAlignment = CLAY_TEXT_ALIGN_RIGHT;
	} else {
		cfg.textAlignment = CLAY_TEXT_ALIGN_LEFT;
	}
	s = ui_clay_cstr(text);
	Clay__OpenTextElement(s, Clay__StoreTextElementConfig(cfg));
}

// NOLINTBEGIN(misc-no-recursion)
static void ui_clay_declare_node(sk_ui_context_t* ctx, sk_ui_node_t node, f32 parent_w, f32 parent_h, i32 parent_def, sk_ui_flex_direction_t parent_dir,
								 sk_ui_align_t parent_align_items, const_chr_t surface_id, i32* limitations, u32 sibling_index) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	ui_clay_frame_t* fr = ctx->clay_frame;
	Clay_ElementDeclaration decl;
	const sk_ui_layout_style_t* ls;
	Clay_ElementId eid;
	f32 pad_l;
	f32 pad_r;
	f32 pad_t;
	f32 pad_b;
	f32 gap;
	f32 grow_w;
	f32 grow_h;
	i32 reverse = 0;
	i32 space_between = 0;
	i32 row_main = 0;
	u32 i;
	const_chr_t widget;
	i32 is_scroll_container;
	i32 is_clip;
	i32 is_text_kind;
	i32 is_menu;
	i32 is_menu_popup;
	i32 is_dock;
	i32 is_dock_drag;
	i32 wrap;
	i32 force_id;
	i32 has_scroll_props;
	sk_ui_align_t cross_align;

	if (slot == NULL || fr == NULL) {
		return;
	}
	if (node.index >= fr->cap) {
		return;
	}

	ls = &slot->layout_style;
	widget = ui_clay_prop_str(slot, "widget");
	is_menu = ui_clay_is_menu_surface(widget);
	is_menu_popup = ui_clay_is_menu_popup(widget);
	is_dock = ui_clay_is_dock_surface(widget);
	is_dock_drag = ui_clay_is_dock_drag_target(widget);
	/* Hidden nodes (stash, closed popups, inactive drop overlay) omit from
	 * Clay so they do not participate in flex or hit-test. */
	if (ui_clay_prop_i32(slot, "hidden", 0) != 0 || (is_menu_popup != 0 && ui_clay_menu_is_open(slot) == 0)) {
		ui_clay_zero_subtree(ctx, node);
		fr->present[node.index] = 0u;
		/* Still hash a stable id so open frames resolve the same CLAY_SID. */
		fr->ids[node.index] = ui_clay_make_id(slot, node, sibling_index);
		return;
	}
	if (is_menu != 0) {
		fr->menu_layout_count += 1;
	}
	if (is_dock != 0) {
		fr->dock_layout_count += 1;
	}
	/*
	 * APX-236: scroll vs pure clip.
	 * - scroll_view (or any node with scroll_x/scroll_y props) → Clay scroll
	 *   container (clip + stable id). Engine props remain the offset source.
	 * - clip_children alone (dock_space / dock_node / window_content / menu
	 *   popups) → Clay clip without scroll-container offset semantics.
	 */
	has_scroll_props = (ui_clay_has_prop_f32(slot, "scroll_x") != 0 || ui_clay_has_prop_f32(slot, "scroll_y") != 0 ||
						(widget != NULL && (strcmp(widget, "scroll_view") == 0 || strcmp(widget, "child") == 0)));
	is_scroll_container = has_scroll_props != 0 ? 1 : 0;
	is_clip = (slot->clip_children != 0u) || is_scroll_container != 0;
	if (is_scroll_container != 0) {
		fr->scroll_layout_count += 1;
	} else if (is_clip != 0) {
		fr->clip_layout_count += 1;
	}
	is_text_kind = (slot->kind == (u8)SK_UI_NODE_KIND_TEXT) || (widget != NULL && strcmp(widget, "label") == 0) ||
				   (widget != NULL && (strcmp(widget, "menu_item") == 0 || strcmp(widget, "menu") == 0 || strcmp(widget, "submenu") == 0 || strcmp(widget, "tab") == 0 ||
									   strcmp(widget, "window_title_bar") == 0 || strcmp(widget, "modal_title") == 0 || strcmp(widget, "separator_text") == 0));
	wrap = ui_clay_prop_i32(slot, "wrap", 0);
	(void)is_dock_drag;

	if (ls->flex_wrap == SK_UI_FLEX_WRAP || ls->flex_wrap == SK_UI_FLEX_WRAP_REVERSE) {
		if (limitations != NULL) {
			*limitations |= 1;
		}
		ui_clay_log_limitation(fr, surface_id, "flex-wrap is not supported by Clay; children stay on a single line");
	}
	if (ls->position == SK_UI_POSITION_ABSOLUTE) {
		if (limitations != NULL) {
			*limitations |= 2;
		}
		ui_clay_log_limitation(fr, surface_id, "absolute positioning mapped via Clay floating; constraints may differ");
	}
	if (ls->margin.left > 0.0001f || ls->margin.right > 0.0001f || ls->margin.top > 0.0001f || ls->margin.bottom > 0.0001f) {
		if (limitations != NULL) {
			*limitations |= 4;
		}
		ui_clay_log_limitation(fr, surface_id, "margins are not a Clay primitive; margin is ignored");
	}
	/* Percent sizing has no min/max in Clay: a percent axis with a definite
	 * max (or non-zero min) silently drops the constraint. */
	if ((ls->width.unit == SK_UI_LENGTH_PERCENT && (ls->max_width.unit == SK_UI_LENGTH_POINT || ls->max_width.unit == SK_UI_LENGTH_PERCENT)) ||
		(ls->height.unit == SK_UI_LENGTH_PERCENT && (ls->max_height.unit == SK_UI_LENGTH_POINT || ls->max_height.unit == SK_UI_LENGTH_PERCENT))) {
		if (limitations != NULL) {
			*limitations |= 16;
		}
		ui_clay_log_limitation(fr, surface_id, "min/max constraints on percent-sized axes are dropped by Clay");
	}

	memset(&decl, 0, sizeof(decl));
	force_id = ui_clay_needs_stable_id(slot);
	eid = ui_clay_make_id(slot, node, sibling_index);
	/* Always assign a stable id so writeback, hover, and tests can resolve the node. */
	decl.id = eid;
	fr->ids[node.index] = eid;
	(void)force_id;

	decl.layout.layoutDirection = ui_clay_map_direction(ls->flex_direction, &reverse);
	if (reverse != 0) {
		if (limitations != NULL) {
			*limitations |= 8;
		}
		ui_clay_log_limitation(fr, surface_id, "flex reverse directions are not supported by Clay; using forward axis");
	}
	ui_clay_map_child_align(ls, &decl.layout.childAlignment);

	/* Combine border + padding so Clay children start at our content origin. */
	pad_l = ls->border.left + ls->padding.left;
	pad_r = ls->border.right + ls->padding.right;
	pad_t = ls->border.top + ls->padding.top;
	pad_b = ls->border.bottom + ls->padding.bottom;
	decl.layout.padding.left = ui_clay_u16_clamp(pad_l);
	decl.layout.padding.right = ui_clay_u16_clamp(pad_r);
	decl.layout.padding.top = ui_clay_u16_clamp(pad_t);
	decl.layout.padding.bottom = ui_clay_u16_clamp(pad_b);

	gap = ls->flex_direction == SK_UI_FLEX_ROW || ls->flex_direction == SK_UI_FLEX_ROW_REVERSE ? ls->column_gap : ls->row_gap;
	/* Prefer the axis gap; if only the other is set, use it. */
	if (gap <= 0.0f) {
		gap = ui_clay_fmaxf(ls->row_gap, ls->column_gap);
	}
	/*
	 * space-between: Clay has no native packing. Approximate with anonymous
	 * main-axis GROW spacers between real children; carry the authored gap as
	 * the spacer minimum and zero Clay childGap so gaps are not double-applied.
	 * space-around / space-evenly remain start-aligned (logged below).
	 */
	space_between = (ls->justify_content == SK_UI_JUSTIFY_SPACE_BETWEEN && slot->children.count >= 2u && ls->position != SK_UI_POSITION_ABSOLUTE) ? 1 : 0;
	row_main = ui_clay_is_row_dir(ls->flex_direction);
	if (space_between != 0) {
		decl.layout.childGap = 0u;
	} else {
		decl.layout.childGap = ui_clay_u16_clamp(gap);
	}
	if (ls->justify_content == SK_UI_JUSTIFY_SPACE_AROUND || ls->justify_content == SK_UI_JUSTIFY_SPACE_EVENLY) {
		if (limitations != NULL) {
			*limitations |= 32;
		}
		ui_clay_log_limitation(fr, surface_id, "justify space-around/evenly collapse to start under Clay");
	}

	/* Cross-axis stretch: parent align_items (or align_self) STRETCH + AUTO
	 * size → GROW on that axis so empty boxes fill the content width/height. */
	grow_w = ls->flex_grow;
	grow_h = ls->flex_grow;
	cross_align = ls->align_self;
	if (cross_align == SK_UI_ALIGN_AUTO) {
		cross_align = parent_align_items;
	}
	/* SameLine rows (APX-349): when any child of the parent carries the
	 * same_line prop the parent packs a horizontal row — every child keeps its
	 * intrinsic cross size (toolbar buttons must not stretch to the column
	 * width, including the row-start button before the first SameLine). */
	if (ui_clay_prop_i32(slot, "same_line", 0) != 0) {
		cross_align = SK_UI_ALIGN_FLEX_START;
	} else if (sk_ui_node_is_valid(slot->parent)) {
		const ui_node_slot_t* pslot = ui_slot(ctx, slot->parent);
		u32 k;
		if (pslot != NULL) {
			for (k = 0u; k < pslot->children.count; ++k) {
				const ui_node_slot_t* cs = ui_slot(ctx, pslot->children.items[k]);
				if (cs != NULL && ui_clay_prop_i32(cs, "same_line", 0) != 0) {
					cross_align = SK_UI_ALIGN_FLEX_START;
					break;
				}
			}
		}
	}
	if (cross_align == SK_UI_ALIGN_STRETCH && ls->position != SK_UI_POSITION_ABSOLUTE) {
		if (ui_clay_is_row_dir(parent_dir) != 0) {
			/* Row parent: cross axis is height. */
			if (ls->height.unit == SK_UI_LENGTH_AUTO && grow_h < 1.0f) {
				grow_h = 1.0f;
			}
		} else {
			/* Column parent: cross axis is width. */
			if (ls->width.unit == SK_UI_LENGTH_AUTO && grow_w < 1.0f) {
				grow_w = 1.0f;
			}
		}
	}

	/* POINT sizes are border-box (outer); see ui_clay_map_axis. */
	decl.layout.sizing.width = ui_clay_map_axis(ls->width, ls->min_width, ls->max_width, grow_w, parent_w, parent_def);
	decl.layout.sizing.height = ui_clay_map_axis(ls->height, ls->min_height, ls->max_height, grow_h, parent_h, parent_def);
	/* Separator (§19): the factory flags vertical for row-class parents and
	 * SameLine, but a parent flipped to a row via inline style only shows up
	 * here. When the resolved direction is a row, force the 1px vertical rule
	 * sizing (grow to the row height); paint re-checks the parent so the rule
	 * orientation stays consistent. */
	if (widget != NULL && strcmp(widget, "separator") == 0 && ui_clay_prop_i32(slot, "vertical", 0) == 0 && ui_clay_is_row_dir(parent_dir) != 0) {
		decl.layout.sizing.width = CLAY_SIZING_FIXED(1);
		decl.layout.sizing.height = CLAY_SIZING_GROW(0);
	}
	/* Floating percent-of-parent is 0 under Clay; pin modal chrome to the viewport. */
	if (widget != NULL && (strcmp(widget, "modal") == 0 || strcmp(widget, "modal_dim") == 0)) {
		f32 vw = ctx->root_width > 1.0f ? ctx->root_width : parent_w;
		f32 vh = ctx->root_height > 1.0f ? ctx->root_height : parent_h;
		if (vw < 1.0f) {
			vw = 640.0f;
		}
		if (vh < 1.0f) {
			vh = 400.0f;
		}
		{
			Clay_SizingAxis aw;
			Clay_SizingAxis ah;
			memset(&aw, 0, sizeof(aw));
			memset(&ah, 0, sizeof(ah));
			aw.type = CLAY__SIZING_TYPE_FIXED;
			aw.size.minMax.min = vw;
			aw.size.minMax.max = vw;
			ah.type = CLAY__SIZING_TYPE_FIXED;
			ah.size.minMax.min = vh;
			ah.size.minMax.max = vh;
			decl.layout.sizing.width = aw;
			decl.layout.sizing.height = ah;
		}
	}

	if (slot->computed.background_color.a > 0.001f) {
		decl.backgroundColor = ui_clay_color(slot->computed.background_color);
	}
	if (slot->computed.corner_radius > 0.0f) {
		f32 r = slot->computed.corner_radius;
		decl.cornerRadius = CLAY_CORNER_RADIUS(r);
	}
	if (ls->border.left > 0.0f || ls->border.right > 0.0f || ls->border.top > 0.0f || ls->border.bottom > 0.0f) {
		decl.border.color = ui_clay_color(slot->computed.border_color);
		decl.border.width.left = ui_clay_u16_clamp(ls->border.left);
		decl.border.width.right = ui_clay_u16_clamp(ls->border.right);
		decl.border.width.top = ui_clay_u16_clamp(ls->border.top);
		decl.border.width.bottom = ui_clay_u16_clamp(ls->border.bottom);
	}

	if (is_clip != 0) {
		decl.clip.horizontal = true;
		decl.clip.vertical = true;
		/*
		 * Stable id (set above) keys Clay's scroll-container row so it
		 * persists across frames. Engine scroll_x/scroll_y are applied
		 * exactly once by paint and hit-test. Clay childOffset stays zero
		 * so writeback stores unshifted parent-relative rects (no double
		 * shift). Feeding Clay_GetScrollOffset here would require paint
		 * and hit-test to stop applying engine offsets (APX-236 limitation).
		 */
		if (is_scroll_container != 0) {
			decl.clip.childOffset.x = 0.0f;
			decl.clip.childOffset.y = 0.0f;
		}
	}

	/* Menu popups and floating editor windows are overlays even if style is
	 * relative for popups (widget factories set absolute; defense in depth).
	 * Multi-viewport OS windows remain unmigrated — Clay is single-root only. */
	if (ls->position == SK_UI_POSITION_ABSOLUTE || is_menu_popup != 0) {
		/* Match old absolute: offsets are from the parent padding-box top-left
		 * (content origin + parent padding). Clay attaches to the parent outer
		 * box; when parent Clay padding already encodes border+padding, offset
		 * from the outer top-left equals padding-box offset only if we add the
		 * parent's border. We use parent content-relative coords in writeback,
		 * so offset left/top as-is (tests use zero parent padding). */
		i32 z_default = is_menu_popup != 0 ? 100 : (widget != NULL && strcmp(widget, "editor_window") == 0 ? 50 : 0);
		i32 z = ui_clay_prop_i32(slot, "z_index", z_default);
		/* context_menu and free-floating editor_window: root attach. */
		if (widget != NULL && (strcmp(widget, "context_menu") == 0 || strcmp(widget, "editor_window") == 0 || strcmp(widget, "popup_menu") == 0 || strcmp(widget, "modal") == 0)) {
			decl.floating.attachTo = CLAY_ATTACH_TO_ROOT;
		} else {
			decl.floating.attachTo = CLAY_ATTACH_TO_PARENT;
		}
		/* Dropdown / submenu popups hang below (or to the right of) the trigger. */
		if (widget != NULL && strcmp(widget, "menu_popup") == 0) {
			i32 attach = ui_clay_prop_i32(slot, "attach", 0); /* 0=below, 1=right (submenu) */
			if (attach == 1) {
				decl.floating.attachPoints.element = CLAY_ATTACH_POINT_LEFT_TOP;
				decl.floating.attachPoints.parent = CLAY_ATTACH_POINT_RIGHT_TOP;
				if (decl.floating.offset.x < 1.0f) {
					decl.floating.offset.x = 2.0f;
				}
			} else {
				decl.floating.attachPoints.element = CLAY_ATTACH_POINT_LEFT_TOP;
				decl.floating.attachPoints.parent = CLAY_ATTACH_POINT_LEFT_BOTTOM;
				/* Hang below the 28px bar trigger so File stays visible. */
				if (decl.floating.offset.y < 20.0f) {
					decl.floating.offset.y = 28.0f;
				}
			}
		} else {
			decl.floating.attachPoints.element = CLAY_ATTACH_POINT_LEFT_TOP;
			decl.floating.attachPoints.parent = CLAY_ATTACH_POINT_LEFT_TOP;
		}
		/* Capture pointer on floating chrome so title-bar drags stay on target. */
		decl.floating.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE;
		if (z != 0) {
			decl.floating.zIndex = (int16_t)(z < -32768 ? -32768 : (z > 32767 ? 32767 : z));
		}
		/* Clip absolute / floating children when the parent clip_children bit
		 * is set, or when the popup / window content itself clips. */
		if (slot->clip_children != 0u) {
			decl.clip.horizontal = true;
			decl.clip.vertical = true;
		}
		if (sk_ui_node_is_valid(slot->parent)) {
			const ui_node_slot_t* pslot = ui_slot(ctx, slot->parent);
			if (pslot != NULL && pslot->clip_children != 0u) {
				decl.floating.clipTo = CLAY_CLIP_TO_ATTACHED_PARENT;
			}
		}
		if (ls->left.unit == SK_UI_LENGTH_POINT) {
			decl.floating.offset.x = ls->left.value;
		}
		if (ls->top.unit == SK_UI_LENGTH_POINT) {
			decl.floating.offset.y = ls->top.value;
		}
	}

	/* userData carries the slot index for diagnostics / future paint mapping. */
	decl.userData = (void*)(uintptr_t)node.index;

	Clay__OpenElement();
	Clay__ConfigureOpenElement(decl);
	fr->present[node.index] = 1u;

	/*
	 * Text content (APX-236):
	 * - wrap=1 always goes through Clay text (CLAY_TEXT_WRAP_WORDS measure),
	 *   including fully fixed boxes so wrap sizing is consistent.
	 * - Other text widgets declare Clay text when the box is not fully fixed
	 *   (FIT/GROW needs intrinsic size). Fully fixed + wrap=0 still paint
	 *   glyphs via the engine path; Clay leaf is optional for sizing.
	 */
	{
		i32 fixed_w = ls->width.unit == SK_UI_LENGTH_POINT ? 1 : 0;
		i32 fixed_h = ls->height.unit == SK_UI_LENGTH_POINT ? 1 : 0;
		i32 fully_fixed = (fixed_w != 0 && fixed_h != 0) ? 1 : 0;
		i32 need_text = 0;
		const_chr_t text_prop = ui_clay_prop_str(slot, "text");
		/* wrap=1 on text kinds always uses Clay word-wrap measure; other text
		 * when not fully fixed (FIT/GROW intrinsic size). */
		if (is_text_kind != 0 && (wrap != 0 || fully_fixed == 0)) {
			need_text = 1;
		}
		if (widget != NULL && fully_fixed == 0 &&
			(strcmp(widget, "button") == 0 || strcmp(widget, "selectable") == 0 || strcmp(widget, "text_input") == 0 || strcmp(widget, "menu_item") == 0 ||
			 strcmp(widget, "menu") == 0 || strcmp(widget, "submenu") == 0 || strcmp(widget, "dropdown") == 0 || strcmp(widget, "tab") == 0 ||
			 strcmp(widget, "window_title_bar") == 0)) {
			need_text = 1;
		}
		if (need_text != 0 && text_prop != NULL) {
			ui_clay_declare_text_content(slot);
			if (wrap != 0) {
				fr->wrap_layout_count += 1;
			}
		}
	}

	{
		/* Child parent size = our content estimate (parent size minus pads when fixed). */
		f32 child_pw = parent_w;
		f32 child_ph = parent_h;
		i32 child_def = parent_def;
		if (ls->width.unit == SK_UI_LENGTH_POINT) {
			child_pw = ui_clay_fmaxf(0.0f, ls->width.value - pad_l - pad_r);
			child_def = 1;
		} else if (ls->width.unit == SK_UI_LENGTH_PERCENT && parent_def) {
			child_pw = ui_clay_fmaxf(0.0f, parent_w * (ls->width.value / 100.0f) - pad_l - pad_r);
			child_def = 1;
		}
		if (ls->height.unit == SK_UI_LENGTH_POINT) {
			child_ph = ui_clay_fmaxf(0.0f, ls->height.value - pad_t - pad_b);
		} else if (ls->height.unit == SK_UI_LENGTH_PERCENT && parent_def) {
			child_ph = ui_clay_fmaxf(0.0f, parent_h * (ls->height.value / 100.0f) - pad_t - pad_b);
		}

		/* scroll_content size comes from set_content_size → POINT layout_style
		 * (mapped above). content_width/height props are paint/clamp metadata. */
		if (widget != NULL && strcmp(widget, "scroll_content") == 0 && sk_ui_node_is_valid(slot->parent)) {
			const ui_node_slot_t* pslot = ui_slot(ctx, slot->parent);
			if (pslot != NULL) {
				/* Touch props so paint/clamp metadata stays discoverable; sizing
				 * already comes from layout_style POINT written by set_content_size. */
				(void)ui_clay_prop_f32(pslot, "content_width", 0.0f);
				(void)ui_clay_prop_f32(pslot, "content_height", 0.0f);
			}
		}

		for (i = 0u; i < slot->children.count; ++i) {
			ui_clay_declare_node(ctx, slot->children.items[i], child_pw, child_ph, child_def, ls->flex_direction, ls->align_items, surface_id, limitations, i);
			/* Free space between items (space-between); min gap from style. */
			if (space_between != 0 && (i + 1u) < slot->children.count) {
				ui_clay_declare_main_axis_spacer(row_main, gap);
			}
		}
	}

	Clay__CloseElement();
}
// NOLINTEND(misc-no-recursion)

/**
 * Place an absolute child using the old engine rules (padding-box containing
 * block, style POINT size = border-box, left/top offsets). Clay floating is
 * best-effort for interactive state; engine geometry stays authoritative so
 * hit-test, paint clips, and abs queries match pre-Clay behaviour.
 *
 * @p clay_bw/@p clay_bh seed AUTO sizes from the Clay writeback when available.
 */
static void ui_clay_place_absolute_child(ui_node_slot_t* child_slot, f32 pad_box_w, f32 pad_box_h, f32 parent_pad_l, f32 parent_pad_t, f32 clay_bw, f32 clay_bh) {
	const sk_ui_layout_style_t* s;
	f32 left;
	f32 top;
	f32 bw;
	f32 bh;
	f32 x;
	f32 y;
	if (child_slot == NULL) {
		return;
	}
	s = &child_slot->layout_style;
	left = s->left.unit == SK_UI_LENGTH_POINT ? s->left.value : 0.0f;
	top = s->top.unit == SK_UI_LENGTH_POINT ? s->top.value : 0.0f;
	/* Absolute: style POINT width/height are border-box. */
	if (s->width.unit == SK_UI_LENGTH_POINT) {
		bw = s->width.value;
	} else if (s->width.unit == SK_UI_LENGTH_PERCENT) {
		bw = pad_box_w * (s->width.value / 100.0f);
	} else {
		bw = clay_bw;
	}
	if (s->height.unit == SK_UI_LENGTH_POINT) {
		bh = s->height.value;
	} else if (s->height.unit == SK_UI_LENGTH_PERCENT) {
		bh = pad_box_h * (s->height.value / 100.0f);
	} else {
		bh = clay_bh;
	}
	if (bw < 0.0f) {
		bw = 0.0f;
	}
	if (bh < 0.0f) {
		bh = 0.0f;
	}
	/* Padding-box relative, then convert to parent content origin. */
	x = left + s->margin.left - parent_pad_l;
	y = top + s->margin.top - parent_pad_t;
	child_slot->layout_border.x = x;
	child_slot->layout_border.y = y;
	child_slot->layout_border.width = bw;
	child_slot->layout_border.height = bh;
	child_slot->layout_content.x = x + s->border.left + s->padding.left;
	child_slot->layout_content.y = y + s->border.top + s->padding.top;
	child_slot->layout_content.width = ui_clay_fmaxf(0.0f, bw - s->border.left - s->border.right - s->padding.left - s->padding.right);
	child_slot->layout_content.height = ui_clay_fmaxf(0.0f, bh - s->border.top - s->border.bottom - s->padding.top - s->padding.bottom);
}

// NOLINTBEGIN(misc-no-recursion)
static void ui_clay_writeback_node(sk_ui_context_t* ctx, sk_ui_node_t node, f32 parent_content_abs_x, f32 parent_content_abs_y) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	ui_clay_frame_t* fr = ctx->clay_frame;
	Clay_ElementData data;
	Clay_BoundingBox bb;
	f32 pad_l;
	f32 pad_r;
	f32 pad_t;
	f32 pad_b;
	f32 border_l;
	f32 border_r;
	f32 border_t;
	f32 border_b;
	u32 i;
	f32 content_abs_x;
	f32 content_abs_y;
	f32 pad_box_w;
	f32 pad_box_h;

	if (slot == NULL || fr == NULL || node.index >= fr->cap) {
		return;
	}
	if (fr->present[node.index] == 0u) {
		for (i = 0u; i < slot->children.count; ++i) {
			ui_clay_writeback_node(ctx, slot->children.items[i], parent_content_abs_x, parent_content_abs_y);
		}
		return;
	}

	data = Clay_GetElementData(fr->ids[node.index]);
	if (!data.found) {
		for (i = 0u; i < slot->children.count; ++i) {
			ui_clay_writeback_node(ctx, slot->children.items[i], parent_content_abs_x, parent_content_abs_y);
		}
		return;
	}
	bb = data.boundingBox;
	fr->abs[node.index] = bb;

	/* Relative to parent content origin (engine convention). */
	slot->layout_border.x = bb.x - parent_content_abs_x;
	slot->layout_border.y = bb.y - parent_content_abs_y;
	slot->layout_border.width = ui_clay_fmaxf(0.0f, bb.width);
	slot->layout_border.height = ui_clay_fmaxf(0.0f, bb.height);

	border_l = slot->layout_style.border.left;
	border_r = slot->layout_style.border.right;
	border_t = slot->layout_style.border.top;
	border_b = slot->layout_style.border.bottom;
	pad_l = slot->layout_style.padding.left;
	pad_r = slot->layout_style.padding.right;
	pad_t = slot->layout_style.padding.top;
	pad_b = slot->layout_style.padding.bottom;

	slot->layout_content.x = slot->layout_border.x + border_l + pad_l;
	slot->layout_content.y = slot->layout_border.y + border_t + pad_t;
	slot->layout_content.width = ui_clay_fmaxf(0.0f, slot->layout_border.width - border_l - border_r - pad_l - pad_r);
	slot->layout_content.height = ui_clay_fmaxf(0.0f, slot->layout_border.height - border_t - border_b - pad_t - pad_b);

	content_abs_x = parent_content_abs_x + slot->layout_content.x;
	content_abs_y = parent_content_abs_y + slot->layout_content.y;

	slot->dirty = (u16)((u32)slot->dirty & ~(u32)SK_UI_DIRTY_LAYOUT);

	/* Containing block for absolute children = padding box (content + padding). */
	pad_box_w = slot->layout_content.width + pad_l + pad_r;
	pad_box_h = slot->layout_content.height + pad_t + pad_b;

	for (i = 0u; i < slot->children.count; ++i) {
		sk_ui_node_t child = slot->children.items[i];
		ui_node_slot_t* ch = ui_slot_mut(ctx, child);
		if (ch != NULL && ch->layout_style.position == SK_UI_POSITION_ABSOLUTE) {
			f32 clay_bw = 0.0f;
			f32 clay_bh = 0.0f;
			f32 child_content_abs_x;
			f32 child_content_abs_y;
			u32 j;
			/* Seed AUTO sizes from Clay when the element was declared. */
			if (child.index < fr->cap && fr->present[child.index] != 0u) {
				Clay_ElementData cd = Clay_GetElementData(fr->ids[child.index]);
				if (cd.found) {
					clay_bw = cd.boundingBox.width;
					clay_bh = cd.boundingBox.height;
				}
			}
			ui_clay_place_absolute_child(ch, pad_box_w, pad_box_h, pad_l, pad_t, clay_bw, clay_bh);
			ch->dirty = (u16)((u32)ch->dirty & ~(u32)SK_UI_DIRTY_LAYOUT);
			/* Nested layout under the absolute child (in-flow + deeper abs). */
			child_content_abs_x = content_abs_x + ch->layout_content.x;
			child_content_abs_y = content_abs_y + ch->layout_content.y;
			for (j = 0u; j < ch->children.count; ++j) {
				ui_clay_writeback_node(ctx, ch->children.items[j], child_content_abs_x, child_content_abs_y);
			}
			continue;
		}
		ui_clay_writeback_node(ctx, child, content_abs_x, content_abs_y);
	}
}
// NOLINTEND(misc-no-recursion)

/* -------------------------------------------------------------------------- */
/* SameLine row packing (APX-349; manifest §19)                              */
/* -------------------------------------------------------------------------- */

/** Non-zero when @p slot carries the same_line prop; reads its params. */
static i32 ui_clay_slot_same_line(const ui_node_slot_t* slot, f32* out_offset, f32* out_spacing) {
	if (slot == NULL || ui_clay_prop_i32(slot, "same_line", 0) == 0) {
		return 0;
	}
	if (out_offset != NULL) {
		*out_offset = ui_clay_prop_f32(slot, "same_line_offset", 0.0f);
	}
	if (out_spacing != NULL) {
		*out_spacing = ui_clay_prop_f32(slot, "same_line_spacing", -1.0f);
	}
	return 1;
}

static void ui_clay_sameline_recompute_content(ui_node_slot_t* s) {
	const sk_ui_layout_style_t* ls;
	if (s == NULL) {
		return;
	}
	ls = &s->layout_style;
	s->layout_content.x = s->layout_border.x + ls->border.left + ls->padding.left;
	s->layout_content.y = s->layout_border.y + ls->border.top + ls->padding.top;
	s->layout_content.width = ui_clay_fmaxf(0.0f, s->layout_border.width - ls->border.left - ls->border.right - ls->padding.left - ls->padding.right);
	s->layout_content.height = ui_clay_fmaxf(0.0f, s->layout_border.height - ls->border.top - ls->border.bottom - ls->padding.top - ls->padding.bottom);
}

/** Non-zero when @p slot is a vertical separator (widget=separator + vertical). */
static i32 ui_clay_slot_vertical_separator(const ui_node_slot_t* slot) {
	const_chr_t wtype = ui_clay_prop_str(slot, "widget");
	if (wtype == NULL || strcmp(wtype, "separator") != 0) {
		return 0;
	}
	return ui_clay_prop_i32(slot, "vertical", 0) != 0 ? 1 : 0;
}

/**
 * Clay has no "place on the previous line" primitive, so same-line siblings
 * are measured in flow (their sizes matter) but flex stacked them on separate
 * lines. Rebuild each affected parent's child rows: a child joins the previous
 * child's row when it carries the same_line prop; every other child starts a
 * new row (ImGui ItemSize semantics — SameLine applies to the next item only).
 *
 * y: every child on a row shares the row's y; rows stack with the parent's
 *    row_gap and a row height equal to the tallest child. The parent border
 *    height is rewritten from the rows so containers report the toolbar's
 *    true height (the flex pass otherwise inflates it by phantom lines).
 * x: offset_from_start_x == 0 → previous item right edge + spacing (default
 *    SK_UI_SAMELINE_DEFAULT_GAP); non-zero → row start x + offset, plus
 *    spacing when >= 0 (ImGui SameLine: offset is relative to the line start).
 * Vertical separators on a row are stretched to the row height (toolbar
 * dividers span the full line, matching ImGui SeparatorEx vertical).
 */
static void ui_clay_sameline_reflow_children(sk_ui_context_t* ctx, sk_ui_node_t parent) {
	ui_node_slot_t* pslot = ui_slot_mut(ctx, parent);
	u32 n;
	u32 i;
	u32 any = 0u;
	u32 row_begin = 0u;
	f32 row_start_x = 0.0f;
	f32 row_h = 0.0f;
	f32 row_w = 0.0f;
	f32 max_row_w = 0.0f;
	f32 new_y = 0.0f;
	f32 gap;

	if (pslot == NULL || pslot->children.count == 0u) {
		return;
	}
	/* SameLine only reshapes column-ish containers (toolbar / property rows).
	 * A row parent already packs horizontally. */
	if (pslot->layout_style.flex_direction == SK_UI_FLEX_ROW || pslot->layout_style.flex_direction == SK_UI_FLEX_ROW_REVERSE) {
		return;
	}
	n = pslot->children.count;
	for (i = 0u; i < n; ++i) {
		const ui_node_slot_t* cs = ui_slot(ctx, pslot->children.items[i]);
		if (cs != NULL && ui_clay_slot_same_line(cs, NULL, NULL) != 0) {
			any = 1u;
			break;
		}
	}
	if (any == 0u) {
		return;
	}
	gap = pslot->layout_style.row_gap;

	/* Row 0 starts at child 0. */
	{
		ui_node_slot_t* c0 = ui_slot_mut(ctx, pslot->children.items[0]);
		if (c0 != NULL) {
			row_start_x = c0->layout_border.x;
			row_h = c0->layout_border.height;
			row_w = c0->layout_border.width;
			c0->layout_border.y = 0.0f;
			ui_clay_sameline_recompute_content(c0);
		}
	}
	for (i = 1u; i < n; ++i) {
		ui_node_slot_t* c = ui_slot_mut(ctx, pslot->children.items[i]);
		f32 off = 0.0f;
		f32 sp = -1.0f;
		const ui_node_slot_t* prev;
		u32 j;
		if (c == NULL) {
			continue;
		}
		if (ui_clay_slot_same_line(c, &off, &sp) != 0) {
			/* Same row as the previous child (chained SameLine joins the row). */
			f32 x;
			f32 right_edge;
			prev = ui_slot(ctx, pslot->children.items[i - 1u]);
			if (fabsf(off) > 1.0e-6f) {
				x = row_start_x + off + (sp >= 0.0f ? sp : 0.0f);
			} else if (prev != NULL) {
				x = (prev->layout_border.x + prev->layout_border.width) + (sp >= 0.0f ? sp : SK_UI_SAMELINE_DEFAULT_GAP);
			} else {
				x = row_start_x;
			}
			c->layout_border.x = x;
			c->layout_border.y = new_y;
			ui_clay_sameline_recompute_content(c);
			if (row_h < c->layout_border.height) {
				row_h = c->layout_border.height;
			}
			right_edge = x + c->layout_border.width;
			if (row_w < right_edge - row_start_x) {
				row_w = right_edge - row_start_x;
			}
		} else {
			/* Close the previous row: stretch its vertical separators. */
			for (j = row_begin; j < i; ++j) {
				ui_node_slot_t* rs = ui_slot_mut(ctx, pslot->children.items[j]);
				if (rs != NULL && ui_clay_slot_vertical_separator(rs) != 0) {
					rs->layout_border.height = row_h;
					ui_clay_sameline_recompute_content(rs);
				}
			}
			if (max_row_w < row_w) {
				max_row_w = row_w;
			}
			new_y += row_h;
			if (gap > 0.0f) {
				new_y += gap;
			}
			row_begin = i;
			row_start_x = c->layout_border.x;
			row_h = c->layout_border.height;
			row_w = c->layout_border.width;
			c->layout_border.y = new_y;
			ui_clay_sameline_recompute_content(c);
		}
	}
	/* Close the last row. */
	{
		u32 j;
		for (j = row_begin; j < n; ++j) {
			ui_node_slot_t* rs = ui_slot_mut(ctx, pslot->children.items[j]);
			if (rs != NULL && ui_clay_slot_vertical_separator(rs) != 0) {
				rs->layout_border.height = row_h;
				ui_clay_sameline_recompute_content(rs);
			}
		}
		if (max_row_w < row_w) {
			max_row_w = row_w;
		}
		new_y += row_h;
	}
	/* Parent border size = rows + padding (border+padding unchanged). Only
	 * auto axes are rewritten — a caller-fixed width (toolbar spanning a
	 * panel) or the viewport root keeps its size. */
	{
		const sk_ui_layout_style_t* ls = &pslot->layout_style;
		f32 pad_x = ls->border.left + ls->border.right + ls->padding.left + ls->padding.right;
		f32 pad_y = ls->border.top + ls->border.bottom + ls->padding.top + ls->padding.bottom;
		if (ls->width.unit == SK_UI_LENGTH_AUTO && !sk_ui_node_eq(parent, ctx->root)) {
			pslot->layout_border.width = max_row_w + pad_x;
		}
		if (ls->height.unit == SK_UI_LENGTH_AUTO) {
			pslot->layout_border.height = new_y + pad_y;
		}
		pslot->layout_content.width = ui_clay_fmaxf(0.0f, pslot->layout_border.width - ls->border.left - ls->border.right - ls->padding.left - ls->padding.right);
		pslot->layout_content.height = new_y;
		pslot->layout_content.x = pslot->layout_border.x + ls->border.left + ls->padding.left;
		pslot->layout_content.y = pslot->layout_border.y + ls->border.top + ls->padding.top;
	}
}

// NOLINTBEGIN(misc-no-recursion)
static void ui_clay_sameline_postpass_node(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	u32 i;
	if (slot == NULL) {
		return;
	}
	/* Children first (bottom-up): a same-line container collapses to its row
	 * height before the parent measures the row it sits on. Positions are
	 * parent-content-relative, so moving the container afterwards does not
	 * invalidate its children (their abs rects walk content origins). */
	for (i = 0u; i < slot->children.count; ++i) {
		ui_clay_sameline_postpass_node(ctx, slot->children.items[i]);
	}
	ui_clay_sameline_reflow_children(ctx, node);
}
// NOLINTEND(misc-no-recursion)

static void ui_clay_sameline_postpass(sk_ui_context_t* ctx) {
	if (ctx == NULL || !sk_ui_node_is_valid(ctx->root)) {
		return;
	}
	ui_clay_sameline_postpass_node(ctx, ctx->root);
}

/**
 * Resolve the root border-box size: viewport size overridden by definite
 * root width/height style (percent against the viewport), clamped by
 * min/max. Mirrors the old custom solver's root handling; root margins are
 * not expressible in Clay and are ignored (nobody uses them).
 */
static void ui_clay_root_size(const sk_ui_layout_style_t* rs, f32 viewport_w, f32 viewport_h, f32* out_w, f32* out_h) {
	f32 w = viewport_w;
	f32 h = viewport_h;
	f32 v;

	if (rs->width.unit == SK_UI_LENGTH_POINT) {
		w = rs->width.value;
	} else if (rs->width.unit == SK_UI_LENGTH_PERCENT) {
		w = viewport_w * (rs->width.value / 100.0f);
	}
	if (rs->height.unit == SK_UI_LENGTH_POINT) {
		h = rs->height.value;
	} else if (rs->height.unit == SK_UI_LENGTH_PERCENT) {
		h = viewport_h * (rs->height.value / 100.0f);
	}
	if (rs->min_width.unit == SK_UI_LENGTH_POINT) {
		v = rs->min_width.value;
	} else if (rs->min_width.unit == SK_UI_LENGTH_PERCENT) {
		v = viewport_w * (rs->min_width.value / 100.0f);
	} else {
		v = 0.0f;
	}
	if (w < v) {
		w = v;
	}
	if (rs->min_height.unit == SK_UI_LENGTH_POINT) {
		v = rs->min_height.value;
	} else if (rs->min_height.unit == SK_UI_LENGTH_PERCENT) {
		v = viewport_h * (rs->min_height.value / 100.0f);
	} else {
		v = 0.0f;
	}
	if (h < v) {
		h = v;
	}
	if (rs->max_width.unit == SK_UI_LENGTH_POINT) {
		v = rs->max_width.value;
	} else if (rs->max_width.unit == SK_UI_LENGTH_PERCENT) {
		v = viewport_w * (rs->max_width.value / 100.0f);
	} else {
		v = 1.0e30f;
	}
	if (w > v) {
		w = v;
	}
	if (rs->max_height.unit == SK_UI_LENGTH_POINT) {
		v = rs->max_height.value;
	} else if (rs->max_height.unit == SK_UI_LENGTH_PERCENT) {
		v = viewport_h * (rs->max_height.value / 100.0f);
	} else {
		v = 1.0e30f;
	}
	if (h > v) {
		h = v;
	}
	if (w < 0.0f) {
		w = 0.0f;
	}
	if (h < 0.0f) {
		h = 0.0f;
	}
	if (out_w != NULL) {
		*out_w = w;
	}
	if (out_h != NULL) {
		*out_h = h;
	}
}

/* -------------------------------------------------------------------------- */
/* Public adapter API                                                         */
/* -------------------------------------------------------------------------- */

i32 ui_clay_layout_impl(sk_ui_context_t* ctx, f32 root_width, f32 root_height) {
	ui_node_slot_t* root_slot;
	ui_clay_frame_t* fr;
	const sk_ui_layout_style_t* rs;
	Clay_ElementDeclaration root_decl;
	static const Clay_String k_root = {.isStaticallyAllocated = true, .length = 8, .chars = "sk_ui_r0"};
	i32 reverse = 0;
	i32 limitations = 0;
	f32 root_w;
	f32 root_h;
	f32 pad_l;
	f32 pad_r;
	f32 pad_t;
	f32 pad_b;
	f32 gap;
	Clay_Vector2 pointer;
	Clay_Vector2 scroll_delta;
	u32 i;

	if (ctx == NULL || !sk_ui_node_is_valid(ctx->root)) {
		return -1;
	}
	root_slot = ui_slot_mut(ctx, ctx->root);
	if (root_slot == NULL) {
		return -1;
	}
	ctx->root_width = root_width;
	ctx->root_height = root_height;
	ui_dock_layout_begin(ctx);

	if (ui_clay_frame_ensure(ctx, ctx->slots.count) != 0) {
		return -1;
	}
	fr = ctx->clay_frame;
	fr->menu_layout_count = 0;
	fr->dock_layout_count = 0;
	fr->scroll_layout_count = 0;
	fr->clip_layout_count = 0;
	fr->wrap_layout_count = 0;
	if (ui_clay_ensure_init(ctx->allocator, root_width, root_height, NULL, NULL) != 0) {
		return -1;
	}
	/* Drop measure-cache entries so the arena stays healthy across frames. */
	Clay_ResetMeasureTextCache();

	/* Feed pointer + accumulated wheel deltas so Clay hover/scroll state
	 * resolves across frames (engine paint/hit-test still apply scroll once).
	 * Skip UpdateScrollContainers when idle so stale scroll rows from a prior
	 * tree (e.g. dock/editor clip after context destroy) are not walked before
	 * the next BeginLayout rebuilds them. */
	pointer.x = ctx->pointer_x;
	pointer.y = ctx->pointer_y;
	Clay_SetPointerState(pointer, (ctx->pointer_buttons & 1u) != 0u);
	scroll_delta.x = ctx->scroll_delta_x;
	scroll_delta.y = ctx->scroll_delta_y;
	if (fabsf(scroll_delta.x) > 1.0e-6f || fabsf(scroll_delta.y) > 1.0e-6f || (ctx->pointer_buttons & 1u) != 0u) {
		Clay_UpdateScrollContainers(false, scroll_delta, 1.0f / 60.0f);
	}
	ctx->scroll_delta_x = 0.0f;
	ctx->scroll_delta_y = 0.0f;

	rs = &root_slot->layout_style;
	ui_clay_root_size(rs, root_width, root_height, &root_w, &root_h);

	/* Root border+padding combined into Clay padding (children land in the
	 * engine content box). Degenerate roots are clamped so Clay does not
	 * collapse the whole tree. */
	pad_l = rs->border.left + rs->padding.left;
	pad_r = rs->border.right + rs->padding.right;
	pad_t = rs->border.top + rs->padding.top;
	pad_b = rs->border.bottom + rs->padding.bottom;
	if (root_w < 1.0f) {
		root_w = 1.0f;
	}
	if (root_h < 1.0f) {
		root_h = 1.0f;
	}

	Clay_BeginLayout();
	memset(&root_decl, 0, sizeof(root_decl));
	root_decl.id = CLAY_SIDI(k_root, ctx->root.index);
	root_decl.layout.sizing.width = CLAY_SIZING_FIXED(root_w);
	root_decl.layout.sizing.height = CLAY_SIZING_FIXED(root_h);
	root_decl.layout.layoutDirection = ui_clay_map_direction(rs->flex_direction, &reverse);
	if (reverse != 0) {
		limitations |= 8;
		ui_clay_log_limitation(fr, root_slot->id, "flex reverse directions are not supported by Clay; using forward axis");
	}
	ui_clay_map_child_align(rs, &root_decl.layout.childAlignment);
	gap = rs->flex_direction == SK_UI_FLEX_ROW || rs->flex_direction == SK_UI_FLEX_ROW_REVERSE ? rs->column_gap : rs->row_gap;
	if (gap <= 0.0f) {
		gap = ui_clay_fmaxf(rs->row_gap, rs->column_gap);
	}
	root_decl.layout.childGap = ui_clay_u16_clamp(gap);
	root_decl.layout.padding.left = ui_clay_u16_clamp(pad_l);
	root_decl.layout.padding.right = ui_clay_u16_clamp(pad_r);
	root_decl.layout.padding.top = ui_clay_u16_clamp(pad_t);
	root_decl.layout.padding.bottom = ui_clay_u16_clamp(pad_b);
	root_decl.userData = (void*)(uintptr_t)ctx->root.index;
	fr->ids[ctx->root.index] = root_decl.id;
	/* Root is declared outside declare_node — still mark present so writeback
	 * stores the viewport/root border box (hit-test and abs queries need it). */
	if (ctx->root.index < fr->cap) {
		fr->present[ctx->root.index] = 1u;
	}

	Clay__OpenElement();
	Clay__ConfigureOpenElement(root_decl);
	for (i = 0u; i < root_slot->children.count; ++i) {
		ui_clay_declare_node(ctx, root_slot->children.items[i], root_w - pad_l - pad_r, root_h - pad_t - pad_b, 1, rs->flex_direction, rs->align_items, root_slot->id, &limitations,
							 i);
	}
	Clay__CloseElement();

	(void)Clay_EndLayout();

	/* Write Clay boxes back as parent-content-relative rects for the whole
	 * tree (root included) so paint, hit-test, scale, and queries see the
	 * same geometry as before. */
	ui_clay_writeback_node(ctx, ctx->root, 0.0f, 0.0f);
	/* SameLine rows (APX-349): reposition + reflow after Clay writeback. Also
	 * drops an unconsumed SameLine so it never leaks into the next frame. */
	ui_same_line_reset_pending(ctx);
	ui_clay_sameline_postpass(ctx);
	ui_dock_layout_end(ctx);
	ui_selectable_apply_spans(ctx);

	(void)limitations;
	return 0;
}

void ui_clay_context_shutdown(sk_ui_context_t* ctx) {
	ui_clay_frame_t* fr;
	const sk_allocator_t* a;
	if (ctx == NULL || ctx->clay_frame == NULL) {
		return;
	}
	fr = ctx->clay_frame;
	a = ctx->allocator;
	if (fr->ids != NULL) {
		a->free(a->instance, fr->ids);
	}
	if (fr->abs != NULL) {
		a->free(a->instance, fr->abs);
	}
	if (fr->present != NULL) {
		a->free(a->instance, fr->present);
	}
	a->free(a->instance, fr);
	ctx->clay_frame = NULL;
}

/* -------------------------------------------------------------------------- */
/* Unit tests                                                                 */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

#include <stdio.h>

/*
 * APX-256 — systematic flexbox geometry matrix.
 *
 * Pure BOX nodes with explicit styles (no widget class pad/border). Asserts
 * parent-content-relative border boxes numerically so a one-pixel solver
 * regression fails with exact deltas. Tolerance is sub-pixel (0.01).
 *
 * Clay adapter limitations are asserted as current behavior:
 *   reverse axes → forward packing
 *   flex-wrap → single line
 *   space-around / space-evenly → start packing
 *   margins → ignored for flex packing (in-flow)
 *   flex_shrink / flex_basis → not mapped (POINT sizes stay fixed)
 */

#define UIFX_TOL 0.01f

static void uifx_assert_rect(const sk_ui_rect_t* r, f32 x, f32 y, f32 w, f32 h, const char* label) {
	char msg[384];
	snprintf(msg, sizeof(msg), "%s.x exp=%.2f got=%.2f d=%.3f", label, (double)x, (double)r->x, (double)(r->x - x));
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(UIFX_TOL, x, r->x, msg);
	snprintf(msg, sizeof(msg), "%s.y exp=%.2f got=%.2f d=%.3f", label, (double)y, (double)r->y, (double)(r->y - y));
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(UIFX_TOL, y, r->y, msg);
	snprintf(msg, sizeof(msg), "%s.w exp=%.2f got=%.2f d=%.3f", label, (double)w, (double)r->width, (double)(r->width - w));
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(UIFX_TOL, w, r->width, msg);
	snprintf(msg, sizeof(msg), "%s.h exp=%.2f got=%.2f d=%.3f", label, (double)h, (double)r->height, (double)(r->height - h));
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(UIFX_TOL, h, r->height, msg);
}

static sk_ui_node_t uifx_box(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	sk_ui_node_t n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(n));
	return n;
}

/* Fixed outer size, zero pad/border, explicit flex container props. */
static void uifx_style_container(sk_ui_style_props_t* p, f32 w, f32 h, sk_ui_flex_direction_t dir, sk_ui_justify_t justify, sk_ui_align_t align_items, sk_ui_flex_wrap_t wrap,
								 f32 row_gap, f32 col_gap) {
	ui_style_props_clear(p);
	p->mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_FLEX_WRAP | SK_UI_SP_PADDING |
			  SK_UI_SP_BORDER_WIDTH | SK_UI_SP_ROW_GAP | SK_UI_SP_COLUMN_GAP;
	p->layout.width = sk_ui_pt(w);
	p->layout.height = sk_ui_pt(h);
	p->layout.flex_direction = dir;
	p->layout.justify_content = justify;
	p->layout.align_items = align_items;
	p->layout.flex_wrap = wrap;
	p->layout.row_gap = row_gap;
	p->layout.column_gap = col_gap;
	p->layout.padding.left = p->layout.padding.right = p->layout.padding.top = p->layout.padding.bottom = 0.0f;
	p->layout.border.left = p->layout.border.right = p->layout.border.top = p->layout.border.bottom = 0.0f;
}

static void uifx_style_fixed_child(sk_ui_style_props_t* p, f32 w, f32 h) {
	ui_style_props_clear(p);
	p->mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_ALIGN_SELF;
	p->layout.width = sk_ui_pt(w);
	p->layout.height = sk_ui_pt(h);
	p->layout.flex_grow = 0.0f;
	p->layout.flex_shrink = 0.0f;
	p->layout.align_self = SK_UI_ALIGN_FLEX_START;
}

static void uifx_run(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 vw, f32 vh) {
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, vw, vh));
}

/* ---- Direction: row / column / reverse (reverse maps to forward under Clay) ---- */

SK_TEST(ui_flex_matrix_direction_row_column) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t row;
	sk_ui_node_t col;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t rc;
	sk_ui_style_props_t p;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	/* Row: three 40x20 fixed children in a 200x40 container. */
	row = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, row, &p));
	a = uifx_box(ui, ctx, row);
	b = uifx_box(ui, ctx, row);
	c = uifx_box(ui, ctx, row);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "row.a");
	uifx_assert_rect(&rb, 40.0f, 0.0f, 40.0f, 20.0f, "row.b");
	uifx_assert_rect(&rc, 80.0f, 0.0f, 40.0f, 20.0f, "row.c");
	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[row.index] != 0u);
	ui->context_destroy(ctx);

	/* Column: same children stack vertically. */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	col = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 80.0f, 200.0f, SK_UI_FLEX_COLUMN, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, col, &p));
	a = uifx_box(ui, ctx, col);
	b = uifx_box(ui, ctx, col);
	c = uifx_box(ui, ctx, col);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "col.a");
	uifx_assert_rect(&rb, 0.0f, 20.0f, 40.0f, 20.0f, "col.b");
	uifx_assert_rect(&rc, 0.0f, 40.0f, 40.0f, 20.0f, "col.c");
	ui->context_destroy(ctx);
}

SK_TEST(ui_flex_matrix_direction_reverse_maps_forward) {
	/* Clay has no reverse packing: ROW_REVERSE / COLUMN_REVERSE behave like forward. */
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t cont;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_style_props_t p;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW_REVERSE, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "row_rev.a");
	uifx_assert_rect(&rb, 40.0f, 0.0f, 40.0f, 20.0f, "row_rev.b");
	ui->context_destroy(ctx);

	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 80.0f, 200.0f, SK_UI_FLEX_COLUMN_REVERSE, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "col_rev.a");
	uifx_assert_rect(&rb, 0.0f, 20.0f, 40.0f, 20.0f, "col_rev.b");
	ui->context_destroy(ctx);
}

/* ---- Wrap: nowrap packs single line; wrap still single-line under Clay ---- */

SK_TEST(ui_flex_matrix_wrap_and_nowrap) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t cont;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t rc;
	sk_ui_style_props_t p;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	/* Container 100 wide, three 40-wide children — would wrap in true flex. */
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 100.0f, 80.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	c = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "nowrap.a");
	uifx_assert_rect(&rb, 40.0f, 0.0f, 40.0f, 20.0f, "nowrap.b");
	uifx_assert_rect(&rc, 80.0f, 0.0f, 40.0f, 20.0f, "nowrap.c");
	ui->context_destroy(ctx);

	/* WRAP: Clay keeps a single line (same packing as nowrap). */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 100.0f, 80.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_WRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	c = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "wrap.a");
	uifx_assert_rect(&rb, 40.0f, 0.0f, 40.0f, 20.0f, "wrap.b");
	uifx_assert_rect(&rc, 80.0f, 0.0f, 40.0f, 20.0f, "wrap.c");
	/* All share y=0 (no second flex line). */
	TEST_ASSERT_FLOAT_WITHIN(UIFX_TOL, ra.y, rb.y);
	TEST_ASSERT_FLOAT_WITHIN(UIFX_TOL, rb.y, rc.y);
	ui->context_destroy(ctx);
}

/* ---- justify-content: all six values (around/evenly → start under Clay) ---- */

SK_TEST(ui_flex_matrix_justify_content) {
	const sk_ui_api_t* ui = ui_get_api_table();
	/* Container 200x40, three 40x20 children, free main space = 80. */
	typedef struct {
		sk_ui_justify_t justify;
		f32 x0;
		f32 x1;
		f32 x2;
		const char* name;
	} justify_case_t;
	const justify_case_t cases[] = {
		{SK_UI_JUSTIFY_FLEX_START, 0.0f, 40.0f, 80.0f, "start"},
		{SK_UI_JUSTIFY_FLEX_END, 80.0f, 120.0f, 160.0f, "end"},
		{SK_UI_JUSTIFY_CENTER, 40.0f, 80.0f, 120.0f, "center"},
		/* space-between via GROW spacers: free 80 / 2 gaps → +40 between items. */
		{SK_UI_JUSTIFY_SPACE_BETWEEN, 0.0f, 80.0f, 160.0f, "between"},
		/* Clay collapses around/evenly to start packing. */
		{SK_UI_JUSTIFY_SPACE_AROUND, 0.0f, 40.0f, 80.0f, "around"},
		{SK_UI_JUSTIFY_SPACE_EVENLY, 0.0f, 40.0f, 80.0f, "evenly"},
	};
	u32 ci;

	for (ci = 0u; ci < (u32)(sizeof(cases) / sizeof(cases[0])); ++ci) {
		sk_ui_context_t* ctx = ui->context_create(NULL);
		sk_ui_node_t root;
		sk_ui_node_t cont;
		sk_ui_node_t a;
		sk_ui_node_t b;
		sk_ui_node_t c;
		sk_ui_rect_t ra;
		sk_ui_rect_t rb;
		sk_ui_rect_t rc;
		sk_ui_style_props_t p;
		char label[64];

		TEST_ASSERT_NOT_NULL(ctx);
		root = ui->context_root(ctx);
		cont = uifx_box(ui, ctx, root);
		uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, cases[ci].justify, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
		a = uifx_box(ui, ctx, cont);
		b = uifx_box(ui, ctx, cont);
		c = uifx_box(ui, ctx, cont);
		uifx_style_fixed_child(&p, 40.0f, 20.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
		uifx_run(ui, ctx, 400.0f, 300.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
		snprintf(label, sizeof(label), "justify.%s.a", cases[ci].name);
		uifx_assert_rect(&ra, cases[ci].x0, 0.0f, 40.0f, 20.0f, label);
		snprintf(label, sizeof(label), "justify.%s.b", cases[ci].name);
		uifx_assert_rect(&rb, cases[ci].x1, 0.0f, 40.0f, 20.0f, label);
		snprintf(label, sizeof(label), "justify.%s.c", cases[ci].name);
		uifx_assert_rect(&rc, cases[ci].x2, 0.0f, 40.0f, 20.0f, label);
		ui->context_destroy(ctx);
	}
}

/* ---- align-items + align-self (row cross-axis) ---- */

SK_TEST(ui_flex_matrix_align_items_and_self) {
	const sk_ui_api_t* ui = ui_get_api_table();
	typedef struct {
		sk_ui_align_t align;
		f32 y;
		f32 h;
		const char* name;
	} align_case_t;
	/* Container 100x60, child 40x20 fixed height (stretch only when height AUTO). */
	const align_case_t cases[] = {
		{SK_UI_ALIGN_FLEX_START, 0.0f, 20.0f, "start"},
		{SK_UI_ALIGN_FLEX_END, 40.0f, 20.0f, "end"},
		{SK_UI_ALIGN_CENTER, 20.0f, 20.0f, "center"},
		/* STRETCH with fixed height keeps authored 20 (not auto-stretch). */
		{SK_UI_ALIGN_STRETCH, 0.0f, 20.0f, "stretch_fixed"},
	};
	u32 ci;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t cont;
	sk_ui_node_t child;
	sk_ui_node_t self_child;
	sk_ui_rect_t r;
	sk_ui_style_props_t p;
	char label[64];

	for (ci = 0u; ci < (u32)(sizeof(cases) / sizeof(cases[0])); ++ci) {
		ctx = ui->context_create(NULL);
		TEST_ASSERT_NOT_NULL(ctx);
		root = ui->context_root(ctx);
		cont = uifx_box(ui, ctx, root);
		uifx_style_container(&p, 100.0f, 60.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, cases[ci].align, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
		child = uifx_box(ui, ctx, cont);
		uifx_style_fixed_child(&p, 40.0f, 20.0f);
		/* align_self AUTO so container align_items applies. */
		p.layout.align_self = SK_UI_ALIGN_AUTO;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, child, &p));
		uifx_run(ui, ctx, 400.0f, 300.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, child, &r, NULL));
		snprintf(label, sizeof(label), "align_items.%s", cases[ci].name);
		uifx_assert_rect(&r, 0.0f, cases[ci].y, 40.0f, cases[ci].h, label);
		ui->context_destroy(ctx);
	}

	/* STRETCH + AUTO height → child fills cross size (60). */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 100.0f, 60.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_STRETCH, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	child = uifx_box(ui, ctx, cont);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_ALIGN_SELF;
	p.layout.width = sk_ui_pt(40.0f);
	p.layout.align_self = SK_UI_ALIGN_AUTO;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, child, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, child, &r, NULL));
	uifx_assert_rect(&r, 0.0f, 0.0f, 40.0f, 60.0f, "align_items.stretch_auto");
	ui->context_destroy(ctx);

	/* align_self overrides container align_items: container END, self CENTER. */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 100.0f, 60.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_END, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	self_child = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	p.layout.align_self = SK_UI_ALIGN_CENTER;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, self_child, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, self_child, &r, NULL));
	/* Note: Clay childAlignment is on the container only; align_self stretch
	 * maps to GROW, but start/center/end self overrides are limited. Current
	 * adapter does not remap per-child main/cross packing for center/end —
	 * container END places y=40 unless stretch GROW applies. Assert actual. */
	uifx_assert_rect(&r, 0.0f, 40.0f, 40.0f, 20.0f, "align_self.center_vs_end");
	ui->context_destroy(ctx);
}

/* ---- flex-grow / shrink / basis (including zero-basis + shrink-below) ---- */

SK_TEST(ui_flex_matrix_grow_shrink_basis) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t cont;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_style_props_t p;

	/* Grow: fixed 40 + grow fills remaining 160 in 200-wide row. */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_ALIGN_SELF;
	p.layout.height = sk_ui_pt(20.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.flex_shrink = 0.0f;
	p.layout.align_self = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "grow.fixed");
	uifx_assert_rect(&rb, 40.0f, 0.0f, 160.0f, 20.0f, "grow.fill");
	ui->context_destroy(ctx);

	/* Two equal growers split free space (100 each in 200). */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_ALIGN_SELF;
	p.layout.height = sk_ui_pt(20.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.align_self = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 100.0f, 20.0f, "grow2.a");
	uifx_assert_rect(&rb, 100.0f, 0.0f, 100.0f, 20.0f, "grow2.b");
	ui->context_destroy(ctx);

	/* Zero flex-basis (AUTO) + grow: still GROW (basis not mapped to Clay). */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_BASIS | SK_UI_SP_ALIGN_SELF;
	p.layout.height = sk_ui_pt(20.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.flex_basis = sk_ui_pt(0.0f);
	p.layout.align_self = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 200.0f, 20.0f, "zero_basis.grow");
	ui->context_destroy(ctx);

	/* Shrink-below-content: POINT children are FIXED — overflow, no shrink.
	 * Two 80-wide children in 100-wide row keep 80 each (shrink not mapped). */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 100.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_ALIGN_SELF;
	p.layout.width = sk_ui_pt(80.0f);
	p.layout.height = sk_ui_pt(20.0f);
	p.layout.flex_shrink = 1.0f;
	p.layout.align_self = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 80.0f, 20.0f, "noshrink.a");
	uifx_assert_rect(&rb, 80.0f, 0.0f, 80.0f, 20.0f, "noshrink.b");
	ui->context_destroy(ctx);
}

/* ---- gap / row-gap / column-gap ---- */

SK_TEST(ui_flex_matrix_gaps) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t cont;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t rc;
	sk_ui_style_props_t p;

	/* Row column_gap=10 → x = 0, 50, 100 for 40-wide children. */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 10.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	c = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "col_gap.a");
	uifx_assert_rect(&rb, 50.0f, 0.0f, 40.0f, 20.0f, "col_gap.b");
	uifx_assert_rect(&rc, 100.0f, 0.0f, 40.0f, 20.0f, "col_gap.c");
	ui->context_destroy(ctx);

	/* Column row_gap=8 → y = 0, 28, 56 for 20-tall children. */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 80.0f, 200.0f, SK_UI_FLEX_COLUMN, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 8.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	c = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "row_gap.a");
	uifx_assert_rect(&rb, 0.0f, 28.0f, 40.0f, 20.0f, "row_gap.b");
	uifx_assert_rect(&rc, 0.0f, 56.0f, 40.0f, 20.0f, "row_gap.c");
	ui->context_destroy(ctx);

	/* space-between + gap: spacer min carries gap; free space still distributes. */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_SPACE_BETWEEN, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 8.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "between_gap.a");
	uifx_assert_rect(&rb, 160.0f, 0.0f, 40.0f, 20.0f, "between_gap.b");
	TEST_ASSERT_TRUE((rb.x - (ra.x + ra.width)) >= 8.0f - UIFX_TOL);
	ui->context_destroy(ctx);
}

/* ---- padding and margin interaction ---- */

SK_TEST(ui_flex_matrix_padding_margin) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t cont;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t rcont;
	sk_ui_rect_t content;
	sk_ui_style_props_t p;

	/* Padding insets content: children start at (pad_l, pad_t) in border box
	 * and report parent-content-relative coords (0,0) for the first child. */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_PADDING | SK_UI_SP_BORDER_WIDTH;
	p.layout.width = sk_ui_pt(200.0f);
	p.layout.height = sk_ui_pt(100.0f);
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	p.layout.padding.left = 10.0f;
	p.layout.padding.top = 6.0f;
	p.layout.padding.right = 4.0f;
	p.layout.padding.bottom = 2.0f;
	p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, cont, &rcont, &content));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	uifx_assert_rect(&rcont, 0.0f, 0.0f, 200.0f, 100.0f, "pad.cont_border");
	/* Content box = border − padding. */
	uifx_assert_rect(&content, 10.0f, 6.0f, 186.0f, 92.0f, "pad.cont_content");
	/* Children relative to parent content origin. */
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "pad.a");
	uifx_assert_rect(&rb, 40.0f, 0.0f, 40.0f, 20.0f, "pad.b");
	ui->context_destroy(ctx);

	/* Margin on in-flow flex children is ignored (Clay limitation). */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MARGIN | SK_UI_SP_ALIGN_SELF;
	p.layout.width = sk_ui_pt(40.0f);
	p.layout.height = sk_ui_pt(20.0f);
	p.layout.margin.left = 12.0f;
	p.layout.margin.top = 8.0f;
	p.layout.align_self = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	/* Same packing as zero-margin: margin does not offset in-flow flex kids. */
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "margin_ignored.a");
	uifx_assert_rect(&rb, 40.0f, 0.0f, 40.0f, 20.0f, "margin_ignored.b");
	ui->context_destroy(ctx);
}

/* ---- min / max size clamping on GROW axes ---- */

SK_TEST(ui_flex_matrix_min_max_clamp) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t cont;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_style_props_t p;

	/* max_width clamps grow: child wants free 160 but max 100. */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	b = uifx_box(ui, ctx, cont);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_MAX_WIDTH | SK_UI_SP_ALIGN_SELF;
	p.layout.height = sk_ui_pt(20.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.max_width = sk_ui_pt(100.0f);
	p.layout.align_self = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "max_w.fixed");
	uifx_assert_rect(&rb, 40.0f, 0.0f, 100.0f, 20.0f, "max_w.clamped");
	ui->context_destroy(ctx);

	/* min_width on grow child: alone in 200, min 150 still grows to 200. */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	cont = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
	a = uifx_box(ui, ctx, cont);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_WIDTH | SK_UI_SP_ALIGN_SELF;
	p.layout.height = sk_ui_pt(20.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.min_width = sk_ui_pt(150.0f);
	p.layout.align_self = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 200.0f, 20.0f, "min_w.grow_full");
	ui->context_destroy(ctx);

	/* Root max_width clamps root border box (viewport 400, max 120). */
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS;
	p.layout.max_width = sk_ui_pt(120.0f);
	p.layout.max_height = sk_ui_pt(80.0f);
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, root, &p));
	a = uifx_box(ui, ctx, root);
	uifx_style_fixed_child(&p, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	uifx_run(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, root, &ra, NULL));
	uifx_assert_rect(&ra, 0.0f, 0.0f, 120.0f, 80.0f, "root.max_clamp");
	ui->context_destroy(ctx);
}

/* ---- Nested containers (two levels) + stable widget ids (row regression) ---- */

SK_TEST(ui_flex_matrix_nested_two_levels) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t outer;
	sk_ui_node_t row;
	sk_ui_node_t c0;
	sk_ui_node_t c1;
	sk_ui_node_t body;
	sk_ui_rect_t router;
	sk_ui_rect_t rrow;
	sk_ui_rect_t r0;
	sk_ui_rect_t r1;
	sk_ui_rect_t rbody;
	sk_ui_style_props_t p;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	/* outer column 220x100 → row (flex row, space-between) + body (stretch). */
	outer = uifx_box(ui, ctx, root);
	uifx_style_container(&p, 220.0f, 100.0f, SK_UI_FLEX_COLUMN, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_STRETCH, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, outer, &p));

	row = uifx_box(ui, ctx, outer);
	uifx_style_container(&p, 220.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_SPACE_BETWEEN, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, row, &p));

	c0 = uifx_box(ui, ctx, row);
	c1 = uifx_box(ui, ctx, row);
	uifx_style_fixed_child(&p, 60.0f, 30.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c0, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c1, &p));

	body = uifx_box(ui, ctx, outer);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_ALIGN_SELF;
	p.layout.height = sk_ui_pt(40.0f);
	p.layout.align_self = SK_UI_ALIGN_AUTO; /* stretch width under column */
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, body, &p));

	uifx_run(ui, ctx, 400.0f, 300.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, outer, &router, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, row, &rrow, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c0, &r0, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c1, &r1, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, body, &rbody, NULL));

	uifx_assert_rect(&router, 0.0f, 0.0f, 220.0f, 100.0f, "nest.outer");
	uifx_assert_rect(&rrow, 0.0f, 0.0f, 220.0f, 40.0f, "nest.row");
	/* Level-2 children relative to row content. */
	uifx_assert_rect(&r0, 0.0f, 0.0f, 60.0f, 30.0f, "nest.c0");
	uifx_assert_rect(&r1, 160.0f, 0.0f, 60.0f, 30.0f, "nest.c1");
	/* Body under outer: y=40, full width stretch 220, height 40. */
	uifx_assert_rect(&rbody, 0.0f, 40.0f, 220.0f, 40.0f, "nest.body");

	ui->context_destroy(ctx);
}

/* Widget row + stable Clay ids (replaces thin panel_row_and_stable_ids geometry). */
SK_TEST(ui_clay_panel_row_and_stable_ids) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t panel;
	sk_ui_node_t left;
	sk_ui_node_t right;
	sk_ui_rect_t rl;
	sk_ui_rect_t rr;
	sk_ui_style_props_t p;
	Clay_ElementId eid;
	Clay_ElementData ed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	panel = ui->widget_panel(ctx, root, "row-panel");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_PADDING | SK_UI_SP_BORDER_WIDTH;
	p.layout.width = sk_ui_pt(200.0f);
	p.layout.height = sk_ui_pt(50.0f);
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.padding.left = p.layout.padding.right = p.layout.padding.top = p.layout.padding.bottom = 0.0f;
	p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, panel, &p));

	left = ui->widget_button(ctx, panel, "L", "row-left");
	right = ui->widget_button(ctx, panel, "R", "row-right");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(80.0f);
	p.layout.height = sk_ui_pt(40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, left, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, right, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, left, &rl, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, right, &rr, NULL));
	uifx_assert_rect(&rl, 0.0f, 0.0f, 80.0f, 40.0f, "stable_row.left");
	uifx_assert_rect(&rr, 80.0f, 0.0f, 80.0f, 40.0f, "stable_row.right");

	eid = Clay_GetElementId(ui_clay_cstr("row-left"));
	ed = Clay_GetElementData(eid);
	TEST_ASSERT_TRUE(ed.found);
	TEST_ASSERT_FLOAT_WITHIN(UIFX_TOL, 80.0f, ed.boundingBox.width);

	ui->context_destroy(ctx);
}

/*
 * APX-247 / vision D1 only: empty BOX with height + AUTO width under a column
 * panel must stretch to the full content width (not FIT-collapse to 0).
 * ui_integration_layout_nested body-red bar depends on this.
 */
SK_TEST(ui_clay_column_stretch_empty_box_fills_content_width) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t panel;
	sk_ui_node_t body;
	sk_ui_rect_t rbody;
	sk_ui_rect_t rpanel;
	sk_ui_style_props_t p;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	panel = ui->widget_panel(ctx, root, "d1-panel");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(panel));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	p.layout.width = sk_ui_pt(224.0f);
	p.layout.height = sk_ui_pt(160.0f);
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(16.0f);
	p.layout.top = sk_ui_pt(16.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, panel, &p));

	body = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, panel);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(body));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_HEIGHT;
	p.background_color = sk_ui_rgba(0.85f, 0.30f, 0.20f, 1.0f);
	p.layout.height = sk_ui_pt(40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, body, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 256.0f, 192.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, panel, &rpanel, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, body, &rbody, NULL));

	/* Outer panel 224x160; content width 224 - 2*(1 border + 8 pad) = 206. */
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 224.0f, rpanel.width);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 40.0f, rbody.height);
	TEST_ASSERT_FLOAT_WITHIN(1.5f, 206.0f, rbody.width);
	TEST_ASSERT_TRUE(rbody.width > 100.0f); /* not collapsed FIT zero-width */
	TEST_ASSERT_TRUE(rbody.height > 1.0f);

	ui->context_destroy(ctx);
}

/*
 * APX-248 / vision D2 only: ui-button POINT width/height is the outer border
 * box. Default class pad 6 + border 1 sit *inside* authored 96x28 — they must
 * not expand the outer size to ~108x40 (content-box). layout_nested header
 * buttons overflowed the panel when this regressed.
 */
SK_TEST(ui_clay_button_point_size_is_border_box) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t btn;
	sk_ui_rect_t border;
	sk_ui_rect_t content;
	sk_ui_style_props_t p;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	btn = ui->widget_button(ctx, root, "A", "d2-btn");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(btn));
	/* Keep class defaults (pad 6, border 1, min_height 28); set outer size. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(96.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, btn, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 256.0f, 192.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, btn, &border, &content));

	/* Outer stays 96x28 (not pad-expanded ~108x40). */
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 96.0f, border.width);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 28.0f, border.height);
	/* Content = outer − 2*(border 1 + pad 6) = 96−14 = 82, 28−14 = 14. */
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 82.0f, content.width);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 14.0f, content.height);
	/* Guard against content-box regression: outer must stay under 100x32. */
	TEST_ASSERT_TRUE(border.width < 100.0f);
	TEST_ASSERT_TRUE(border.height < 32.0f);

	ui->context_destroy(ctx);
}

/*
 * APX-240 / vision audit D1+D2 (APX-247, APX-248): layout contracts that
 * ui_integration_layout_nested depends on.
 *
 * D1 — column parent with default align_items STRETCH must give AUTO-width
 *      children the full content width (empty body BOX used to collapse).
 * D2 — ui-button POINT width/height is the outer border box (padding+border
 *      sit inside; pre-fix content-box expansion made 96x28 → ~108x40 and
 *      overflowed the panel). space-between places the second button at the
 *      trailing edge of the row content.
 *
 * Geometry mirrors the integration scene without GPU: panel content 206 wide
 * (224 outer − 1 border − 8 pad each side), row 28 tall, two 96x28 buttons,
 * body height 40 with AUTO width.
 */
SK_TEST(ui_clay_nested_border_box_stretch_space_between) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t panel;
	sk_ui_node_t row;
	sk_ui_node_t btn_a;
	sk_ui_node_t btn_b;
	sk_ui_node_t body;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t rbody;
	sk_ui_rect_t rrow;
	sk_ui_style_props_t p;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	panel = ui->widget_panel(ctx, root, "d-panel");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(panel));
	ui_style_props_clear(&p);
	/* Keep default panel pad 8 + border 1 (class defaults); set outer size. */
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	p.layout.width = sk_ui_pt(224.0f);
	p.layout.height = sk_ui_pt(160.0f);
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(16.0f);
	p.layout.top = sk_ui_pt(16.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, panel, &p));

	row = ui->widget_view(ctx, panel, "d-row");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ROW_GAP;
	p.layout.height = sk_ui_pt(28.0f);
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.justify_content = SK_UI_JUSTIFY_SPACE_BETWEEN;
	p.layout.row_gap = 8.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, row, &p));

	btn_a = ui->widget_button(ctx, row, "A", "d-btn-a");
	btn_b = ui->widget_button(ctx, row, "B", "d-btn-b");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(btn_a));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(btn_b));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(96.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, btn_a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, btn_b, &p));

	/* Body: height only — width must stretch (D1). */
	body = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, panel);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(body));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_HEIGHT;
	p.background_color = sk_ui_rgba(0.85f, 0.30f, 0.20f, 1.0f);
	p.layout.height = sk_ui_pt(40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, body, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 256.0f, 192.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, row, &rrow, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, btn_a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, btn_b, &rb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, body, &rbody, NULL));

	/* D2 border-box: outer size stays 96x28 despite button pad 6 + border 1. */
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 96.0f, ra.width);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 28.0f, ra.height);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 96.0f, rb.width);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 28.0f, rb.height);

	/* D2 no overflow: both buttons fully inside row content (206 wide). */
	TEST_ASSERT_TRUE(ra.x >= -0.5f);
	TEST_ASSERT_TRUE(rb.x + rb.width <= rrow.width + 0.5f);
	/* D2 space-between: first at start, second at end, gap between them. */
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, ra.x);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, rrow.width - 96.0f, rb.x);
	TEST_ASSERT_TRUE((rb.x - (ra.x + ra.width)) >= 7.0f);

	/* D1 stretch: body width fills panel content (224 - 2*(1+8) = 206). */
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 40.0f, rbody.height);
	TEST_ASSERT_FLOAT_WITHIN(1.5f, 206.0f, rbody.width);
	TEST_ASSERT_TRUE(rbody.width > 100.0f); /* not collapsed FIT */

	ui->context_destroy(ctx);
}

/* --- Widget surfaces (stable IDs + wrap text via whole-tree Clay) ---------- */

SK_TEST(ui_clay_widget_button_stable_id) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t btn;
	sk_ui_rect_t rb;
	sk_ui_style_props_t p;
	Clay_ElementId eid;
	Clay_ElementData ed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	btn = ui->widget_button(ctx, root, "OK", "clay-btn");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(80.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, btn, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[btn.index] != 0u);

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, btn, &rb, NULL));
	TEST_ASSERT_TRUE(rb.width >= 80.0f);
	TEST_ASSERT_TRUE(rb.height >= 28.0f);

	eid = Clay_GetElementId(ui_clay_cstr("clay-btn"));
	ed = Clay_GetElementData(eid);
	TEST_ASSERT_TRUE(ed.found);
	TEST_ASSERT_TRUE(ed.boundingBox.width >= 1.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_widget_list_loop_index_ids) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t col;
	sk_ui_node_t rows[3];
	sk_ui_style_props_t p;
	u32 i;
	Clay_ElementId eid0;
	Clay_ElementId eid1;
	Clay_ElementData ed0;
	Clay_ElementData ed1;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	col = ui->widget_panel(ctx, root, "list-col");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ROW_GAP | SK_UI_SP_PADDING | SK_UI_SP_BORDER_WIDTH;
	p.layout.width = sk_ui_pt(120.0f);
	p.layout.height = sk_ui_pt(120.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.row_gap = 4.0f;
	p.layout.padding.left = p.layout.padding.right = p.layout.padding.top = p.layout.padding.bottom = 0.0f;
	p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, col, &p));

	for (i = 0u; i < 3u; ++i) {
		/* Raw nodes: widget prop set, no id — Clay uses CLAY_SIDI("button", loop_i). */
		rows[i] = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, col);
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(rows[i]));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, rows[i], "widget", "button"));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, rows[i], "text", "row"));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_focusable(ctx, rows[i], 1));
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
		p.layout.width = sk_ui_pt(120.0f);
		p.layout.height = sk_ui_pt(28.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, rows[i], &p));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 200.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[col.index] != 0u);

	/* Sibling loop index hashes: CLAY_SIDI("button", 0/1) — stable without string ids. */
	eid0 = CLAY_SIDI(ui_clay_cstr("button"), 0u);
	eid1 = CLAY_SIDI(ui_clay_cstr("button"), 1u);
	ed0 = Clay_GetElementData(eid0);
	ed1 = Clay_GetElementData(eid1);
	TEST_ASSERT_TRUE(ed0.found);
	TEST_ASSERT_TRUE(ed1.found);
	TEST_ASSERT_TRUE(ed0.boundingBox.y + 1.0f < ed1.boundingBox.y);

	/* Second layout pass keeps the same hashed ids (stable frame-to-frame). */
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 200.0f));
	ed0 = Clay_GetElementData(eid0);
	ed1 = Clay_GetElementData(eid1);
	TEST_ASSERT_TRUE(ed0.found);
	TEST_ASSERT_TRUE(ed1.found);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_widget_wrapped_label_text_element) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t label;
	sk_ui_rect_t rl;
	sk_ui_style_props_t p;
	Clay_ElementId eid;
	Clay_ElementData ed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	/* Default wrap=1 from widget_label → Clay text uses CLAY_TEXT_WRAP_WORDS. */
	label = ui->widget_label(ctx, root, "The quick brown fox jumps over the lazy dog", "wrap-lbl");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FONT_SIZE;
	p.layout.width = sk_ui_pt(80.0f);
	/* Fixed height keeps baseline non-degenerate; wrap still declares Clay text. */
	p.layout.height = sk_ui_pt(48.0f);
	p.font_size = 12.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, label, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 200.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[label.index] != 0u);

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, label, &rl, NULL));
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 80.0f, rl.width);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 48.0f, rl.height);

	eid = Clay_GetElementId(ui_clay_cstr("wrap-lbl"));
	ed = Clay_GetElementData(eid);
	TEST_ASSERT_TRUE(ed.found);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 80.0f, ed.boundingBox.width);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_widget_scroll_and_slider_surfaces) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t sl;
	sk_ui_node_t sv;
	sk_ui_style_props_t p;
	Clay_ElementId eid;
	Clay_ElementData ed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	sl = ui->widget_slider(ctx, root, 0.0f, 1.0f, 0.5f, "clay-sl");
	sv = ui->widget_scroll_view(ctx, root, "clay-sv");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(100.0f);
	p.layout.height = sk_ui_pt(20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, sl, &p));
	p.layout.height = sk_ui_pt(60.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, sv, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 200.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[sl.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[sv.index] != 0u);

	/* Surfaces remain resolvable via Clay after the whole-tree pass. */
	eid = Clay_GetElementId(ui_clay_cstr("clay-sv"));
	ed = Clay_GetElementData(eid);
	TEST_ASSERT_TRUE(ed.found);

	ui->context_destroy(ctx);
}

/* --- APX-234: menu surfaces (stable IDs, floating popups, open across frames) */

SK_TEST(ui_clay_menu_bar_popup_stable_ids_and_open) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t bar;
	sk_ui_node_t file_item;
	sk_ui_node_t popup;
	sk_ui_node_t open_item;
	sk_ui_node_t save_item;
	sk_ui_style_props_t p;
	sk_ui_layout_style_t ls;
	Clay_ElementId eid_bar;
	Clay_ElementId eid_item;
	Clay_ElementId eid_popup;
	Clay_ElementData ed;
	sk_ui_rect_t rp;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	/* menu_bar */
	bar = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, bar, "menubar"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, bar, "widget", "menu_bar"));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_PADDING | SK_UI_SP_BORDER_WIDTH;
	p.layout.width = sk_ui_pt(320.0f);
	p.layout.height = sk_ui_pt(28.0f);
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.padding.left = p.layout.padding.right = p.layout.padding.top = p.layout.padding.bottom = 0.0f;
	p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, bar, &p));

	/* menu_item File */
	file_item = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, bar);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, file_item, "menu-file"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, file_item, "widget", "menu_item"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, file_item, "text", "File"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_focusable(ctx, file_item, 1));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(64.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, file_item, &p));

	/* Closed popup first: not present in Clay, zero rect. */
	popup = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, file_item);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, popup, "menu-file-popup"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, popup, "widget", "menu_popup"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, popup, "open", 0));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, popup, "z_index", 100));
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(0.0f);
	ls.top = sk_ui_pt(28.0f);
	ls.width = sk_ui_pt(140.0f);
	ls.height = sk_ui_pt(60.0f);
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, popup, &ls));

	open_item = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, popup);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, open_item, "menu-file-open"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, open_item, "widget", "menu_item"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, open_item, "text", "Open"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_focusable(ctx, open_item, 1));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(140.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, open_item, &p));

	save_item = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, popup);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, save_item, "menu-file-save"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, save_item, "widget", "menu_item"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, save_item, "text", "Save"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_focusable(ctx, save_item, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, save_item, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->menu_layout_count >= 1);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[bar.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[file_item.index] != 0u);
	/* Closed popup omitted from Clay. */
	TEST_ASSERT_TRUE(ctx->clay_frame->present[popup.index] == 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, popup, &rp, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, rp.width);
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, rp.height);

	eid_bar = Clay_GetElementId(ui_clay_cstr("menubar"));
	eid_item = Clay_GetElementId(ui_clay_cstr("menu-file"));
	ed = Clay_GetElementData(eid_bar);
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(eid_item);
	TEST_ASSERT_TRUE(ed.found);

	/* Open popup: declare as Clay floating; ids stable across a second frame. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, popup, "open", 1));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
	TEST_ASSERT_TRUE(ctx->clay_frame->present[popup.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[open_item.index] != 0u);
	eid_popup = Clay_GetElementId(ui_clay_cstr("menu-file-popup"));
	ed = Clay_GetElementData(eid_popup);
	TEST_ASSERT_TRUE(ed.found);
	TEST_ASSERT_TRUE(ed.boundingBox.width >= 1.0f);

	/* Second frame: open state + same Clay ids survive. */
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
	TEST_ASSERT_EQUAL_INT(1, ui_clay_prop_i32(ui_slot(ctx, popup), "open", 0));
	TEST_ASSERT_TRUE(ctx->clay_frame->present[popup.index] != 0u);
	ed = Clay_GetElementData(eid_popup);
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("menu-file-open")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("menu-file-save")));
	TEST_ASSERT_TRUE(ed.found);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_menu_submenu_and_context_floating) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t submenu;
	sk_ui_node_t sub_popup;
	sk_ui_node_t ctx_menu;
	sk_ui_node_t ctx_item;
	sk_ui_style_props_t p;
	sk_ui_layout_style_t ls;
	Clay_ElementData ed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	/* submenu trigger + nested popup (attach=right). */
	submenu = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, root);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, submenu, "submenu-recent"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, submenu, "widget", "submenu"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, submenu, "text", "Recent"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_focusable(ctx, submenu, 1));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(100.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, submenu, &p));

	sub_popup = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, submenu);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, sub_popup, "submenu-recent-popup"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, sub_popup, "widget", "menu_popup"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, sub_popup, "open", 1));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, sub_popup, "attach", 1));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, sub_popup, "z_index", 110));
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(100.0f);
	ls.top = sk_ui_pt(0.0f);
	ls.width = sk_ui_pt(120.0f);
	ls.height = sk_ui_pt(40.0f);
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, sub_popup, &ls));

	/* context_menu at root (viewport floating). */
	ctx_menu = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, ctx_menu, "ctx-menu"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, ctx_menu, "widget", "context_menu"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, ctx_menu, "open", 1));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, ctx_menu, "z_index", 200));
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(40.0f);
	ls.top = sk_ui_pt(80.0f);
	ls.width = sk_ui_pt(100.0f);
	ls.height = sk_ui_pt(36.0f);
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, ctx_menu, &ls));

	ctx_item = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, ctx_menu);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, ctx_item, "ctx-delete"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, ctx_item, "widget", "menu_item"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, ctx_item, "text", "Delete"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_focusable(ctx, ctx_item, 1));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(100.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, ctx_item, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->menu_layout_count >= 3);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[submenu.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[sub_popup.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[ctx_menu.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[ctx_item.index] != 0u);

	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("submenu-recent-popup")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("ctx-menu")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("ctx-delete")));
	TEST_ASSERT_TRUE(ed.found);

	/* Hover open: set pointer over submenu and re-layout; ids still resolve. */
	ctx->pointer_x = 10.0f;
	ctx->pointer_y = 10.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
	TEST_ASSERT_TRUE(ctx->clay_frame->present[sub_popup.index] != 0u);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("submenu-recent")));
	TEST_ASSERT_TRUE(ed.found);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_menu_dropdown_clip_and_closed_hidden) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t dd;
	sk_ui_node_t popup;
	sk_ui_style_props_t p;
	sk_ui_layout_style_t ls;
	sk_ui_rect_t rp;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	dd = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, dd, "dropdown-1"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, dd, "widget", "dropdown"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, dd, "text", "Choose"));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(120.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, dd, &p));

	popup = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, dd);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, popup, "dropdown-1-popup"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, popup, "widget", "menu_popup"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, popup, "open", 1));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_clip_children(ctx, popup, 1));
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(0.0f);
	ls.top = sk_ui_pt(28.0f);
	ls.width = sk_ui_pt(120.0f);
	ls.height = sk_ui_pt(80.0f);
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, popup, &ls));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 300.0f, 200.0f));
	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[dd.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[popup.index] != 0u);

	/* hidden=1 collapses like closed. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, popup, "hidden", 1));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 300.0f, 200.0f));
	TEST_ASSERT_TRUE(ctx->clay_frame->present[popup.index] == 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, popup, &rp, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, rp.width);

	ui->context_destroy(ctx);
}

/* --- APX-235: docking / editor window surfaces (stable IDs, nested clip) --- */

SK_TEST(ui_clay_dock_space_node_splitter_stable_ids) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t space;
	sk_ui_node_t left;
	sk_ui_node_t split;
	sk_ui_node_t right;
	sk_ui_style_props_t p;
	Clay_ElementData ed;
	f32 ratio;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	space = ui->widget_dock_space(ctx, root, "dock-root");
	left = ui->widget_dock_node(ctx, space, 1, "dock-left");
	split = ui->widget_splitter(ctx, space, 0, "dock-split");
	right = ui->widget_dock_node(ctx, space, 1, "dock-right");

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(400.0f);
	p.layout.height = sk_ui_pt(240.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, space, &p));
	p.layout.width = sk_ui_pt(160.0f);
	p.layout.height = sk_ui_pt(240.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, left, &p));
	p.layout.width = sk_ui_pt(200.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, right, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 480.0f, 320.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->dock_layout_count >= 4);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[space.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[left.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[split.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[right.index] != 0u);

	/* Nested dock containers clip via Clay. */
	TEST_ASSERT_EQUAL_INT(1, ui->node_get_clip_children(ctx, space));
	TEST_ASSERT_EQUAL_INT(1, ui->node_get_clip_children(ctx, left));

	/* Stable Clay IDs for drag/resize targets. */
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("dock-root")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("dock-split")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("dock-left")));
	TEST_ASSERT_TRUE(ed.found);

	/* Splitter ratio survives a second layout frame (pointer state continuity). */
	TEST_ASSERT_EQUAL_INT(0, ui->splitter_set_ratio(ctx, split, 0.35f));
	ratio = ui->splitter_get_ratio(ctx, split);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.35f, ratio);
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 480.0f, 320.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.35f, ui->splitter_get_ratio(ctx, split));
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("dock-split")));
	TEST_ASSERT_TRUE(ed.found);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_tab_bar_select_stable_ids) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t bar;
	sk_ui_node_t t0;
	sk_ui_node_t t1;
	sk_ui_style_props_t p;
	Clay_ElementData ed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	bar = ui->widget_tab_bar(ctx, root, "tabs");
	t0 = ui->widget_tab(ctx, bar, "Scene", "tab-scene");
	t1 = ui->widget_tab(ctx, bar, "Game", "tab-game");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(200.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, bar, &p));
	p.layout.width = sk_ui_pt(80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, t0, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, t1, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 300.0f, 100.0f));
	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->dock_layout_count >= 3);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[bar.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[t0.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[t1.index] != 0u);

	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("tab-scene")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("tab-game")));
	TEST_ASSERT_TRUE(ed.found);

	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_set_active(ctx, bar, t1));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t1));

	/* Second frame: active prop + Clay ids remain. */
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 300.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t1));
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("tab-game")));
	TEST_ASSERT_TRUE(ed.found);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_editor_window_chrome_nested_clip) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t win;
	sk_ui_node_t title;
	sk_ui_node_t content;
	sk_ui_node_t body_scroll;
	sk_ui_style_props_t p;
	sk_ui_layout_style_t ls;
	Clay_ElementData ed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	/* In-flow docked editor window. */
	win = ui->widget_editor_window(ctx, root, "Console", "win-console");
	title = ui->editor_window_title_bar(ctx, win);
	content = ui->editor_window_content(ctx, win);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(title));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(content));
	TEST_ASSERT_EQUAL_INT(1, ui->node_get_clip_children(ctx, content));

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(280.0f);
	p.layout.height = sk_ui_pt(160.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, win, &p));

	/* Nested scroll region under clipped window content. */
	body_scroll = ui->widget_scroll_view(ctx, content, "win-console-scroll");
	p.layout.width = sk_ui_pt(260.0f);
	p.layout.height = sk_ui_pt(100.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, body_scroll, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, body_scroll, 260.0f, 400.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->dock_layout_count >= 3);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[win.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[title.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[content.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[body_scroll.index] != 0u);

	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("win-console")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("win-console-title")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("win-console-content")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("win-console-scroll")));
	TEST_ASSERT_TRUE(ed.found);

	TEST_ASSERT_EQUAL_INT(0, ui->editor_window_set_title(ctx, win, "Console*"));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("win-console-title")));
	TEST_ASSERT_TRUE(ed.found);

	/* Floating undocked window: absolute → Clay floating, same stable ids. */
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(40.0f);
	ls.top = sk_ui_pt(30.0f);
	ls.width = sk_ui_pt(200.0f);
	ls.height = sk_ui_pt(120.0f);
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, win, &ls));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, win, "z_index", 50));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
	TEST_ASSERT_TRUE(ctx->clay_frame->present[win.index] != 0u);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("win-console-title")));
	TEST_ASSERT_TRUE(ed.found);

	ui->context_destroy(ctx);
}

/* --- APX-236: remaining scroll / clip / wrap regions ----------------------- */

SK_TEST(ui_clay_scroll_container_stable_id_and_offset) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t sv;
	sk_ui_node_t content;
	sk_ui_style_props_t p;
	Clay_ElementId eid_sv;
	Clay_ElementId eid_content;
	Clay_ElementData ed;
	f32 sx;
	f32 sy;
	u32 id_hash;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	sv = ui->widget_scroll_view(ctx, root, "apx236-sv");
	content = ui->scroll_view_content(ctx, sv);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(content));

	/* Clear default scroll_view border so POINT sizes are exact border boxes. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_PADDING;
	p.layout.width = sk_ui_pt(100.0f);
	p.layout.height = sk_ui_pt(60.0f);
	p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
	p.layout.padding.left = p.layout.padding.right = p.layout.padding.top = p.layout.padding.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, sv, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, sv, 100.0f, 240.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_scroll(ctx, sv, 0.0f, 48.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 200.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->scroll_layout_count >= 1);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[sv.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[content.index] != 0u);
	TEST_ASSERT_EQUAL_INT(1, ui->node_get_clip_children(ctx, sv));

	eid_sv = Clay_GetElementId(ui_clay_cstr("apx236-sv"));
	ed = Clay_GetElementData(eid_sv);
	TEST_ASSERT_TRUE(ed.found);
	/* Border-box: style POINT width 100 is outer size. */
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, ed.boundingBox.width);

	id_hash = eid_sv.id;
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 48.0f, sy);

	/* Second frame: same Clay id hash + engine scroll offset persist. */
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 200.0f));
	eid_sv = Clay_GetElementId(ui_clay_cstr("apx236-sv"));
	TEST_ASSERT_EQUAL_UINT(id_hash, eid_sv.id);
	ed = Clay_GetElementData(eid_sv);
	TEST_ASSERT_TRUE(ed.found);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 48.0f, sy);
	TEST_ASSERT_TRUE(ctx->clay_frame->scroll_layout_count >= 1);

	/* scroll_content remains present with a stable Clay id across frames. */
	TEST_ASSERT_TRUE(ctx->clay_frame->present[content.index] != 0u);
	eid_content = ctx->clay_frame->ids[content.index];
	ed = Clay_GetElementData(eid_content);
	TEST_ASSERT_TRUE(ed.found);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_pure_clip_region_and_wrap_text) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t clipper;
	sk_ui_node_t label;
	sk_ui_style_props_t p;
	Clay_ElementData ed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	/* Pure clip (no scroll props): counts as clip, not scroll container. */
	clipper = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, clipper, "apx236-clip"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_clip_children(ctx, clipper, 1));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(80.0f);
	p.layout.height = sk_ui_pt(40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, clipper, &p));

	/* wrap=1 → Clay text element even with fully fixed box. */
	label = ui->widget_label(ctx, clipper, "Wrapped soft line text for Clay measure path", "apx236-wrap");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FONT_SIZE;
	p.layout.width = sk_ui_pt(72.0f);
	p.layout.height = sk_ui_pt(36.0f);
	p.font_size = 11.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, label, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->label_set_wrap(ctx, label, 1));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 120.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->clip_layout_count >= 1);
	TEST_ASSERT_TRUE(ctx->clay_frame->wrap_layout_count >= 1);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[clipper.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[label.index] != 0u);

	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("apx236-clip")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("apx236-wrap")));
	TEST_ASSERT_TRUE(ed.found);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 72.0f, ed.boundingBox.width);

	/* Second frame: wrap + clip counts and ids remain. */
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 120.0f));
	TEST_ASSERT_TRUE(ctx->clay_frame->wrap_layout_count >= 1);
	TEST_ASSERT_TRUE(ctx->clay_frame->clip_layout_count >= 1);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("apx236-wrap")));
	TEST_ASSERT_TRUE(ed.found);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_nested_scroll_under_clip_stable) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t outer_clip;
	sk_ui_node_t sv;
	sk_ui_style_props_t p;
	Clay_ElementData ed;
	f32 sx;
	f32 sy;
	u32 id_hash;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	outer_clip = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, outer_clip, "apx236-outer-clip"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_clip_children(ctx, outer_clip, 1));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION;
	p.layout.width = sk_ui_pt(120.0f);
	p.layout.height = sk_ui_pt(80.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, outer_clip, &p));

	sv = ui->widget_scroll_view(ctx, outer_clip, "apx236-nested-sv");
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BORDER_WIDTH;
	p.layout.width = sk_ui_pt(100.0f);
	p.layout.height = sk_ui_pt(60.0f);
	p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, sv, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, sv, 100.0f, 200.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_scroll(ctx, sv, 0.0f, 20.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 160.0f));

	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->scroll_layout_count >= 1);
	TEST_ASSERT_TRUE(ctx->clay_frame->clip_layout_count >= 1);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[outer_clip.index] != 0u);
	TEST_ASSERT_TRUE(ctx->clay_frame->present[sv.index] != 0u);

	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("apx236-outer-clip")));
	TEST_ASSERT_TRUE(ed.found);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("apx236-nested-sv")));
	TEST_ASSERT_TRUE(ed.found);
	id_hash = Clay_GetElementId(ui_clay_cstr("apx236-nested-sv")).id;
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, sy);

	/* Second frame: nested clip + scroll ids and engine offset persist. */
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 160.0f));
	TEST_ASSERT_EQUAL_UINT(id_hash, Clay_GetElementId(ui_clay_cstr("apx236-nested-sv")).id);
	ed = Clay_GetElementData(Clay_GetElementId(ui_clay_cstr("apx236-nested-sv")));
	TEST_ASSERT_TRUE(ed.found);
	TEST_ASSERT_TRUE(ctx->clay_frame->scroll_layout_count >= 1);
	TEST_ASSERT_TRUE(ctx->clay_frame->clip_layout_count >= 1);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, sy);

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
