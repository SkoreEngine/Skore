/**
 * @file clay_adapter.c
 * @brief Clay-backed layout adapter for retained sk-ui trees (APX-214 / APX-232).
 *
 * Maps the per-frame layout pass onto Clay's immediate-mode lifecycle and
 * writes resulting boxes back into slot layout rects so hit-testing, scale,
 * paint, and queries keep working unchanged.
 *
 * APX-232 (panel surfaces): after the custom flex baseline pass, every panel
 * root (widget prop "panel" / class ui-panel) and its nested containers are
 * re-declared as Clay elements with stable IDs so hover/click/scroll state
 * resolves across frames. Nested containers become full Clay equivalents —
 * no hybrid custom/Clay subtrees under a migrated panel.
 */

#include "ui_internal.h"

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
	Clay_ElementId* ids;	/**< Parallel to slots: clay id per slot index. */
	Clay_BoundingBox* abs;	/**< Clay absolute bounding box per slot. */
	u8* present;			/**< Non-zero if node was declared this pass. */
	u32 cap;				/**< Capacity of the parallel arrays. */
	i32 panel_layout_count; /**< Panels laid out via Clay this frame. */
	i32 limitation_logged;	/**< Avoid spamming the logger every frame. */
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

static i32 ui_clay_is_panel_slot(const ui_node_slot_t* slot) {
	const_chr_t w;
	u32 i;
	if (slot == NULL) {
		return 0;
	}
	w = ui_clay_prop_str(slot, "widget");
	if (w != NULL && strcmp(w, "panel") == 0) {
		return 1;
	}
	for (i = 0u; i < slot->classes.count; ++i) {
		if (slot->classes.items[i] != NULL && strcmp(slot->classes.items[i], SK_UI_CLASS_PANEL) == 0) {
			return 1;
		}
	}
	return 0;
}

static i32 ui_clay_needs_stable_id(const ui_node_slot_t* slot) {
	const_chr_t w;
	if (slot == NULL) {
		return 0;
	}
	/* Interactive / scrollable surfaces need stable Clay IDs for pointer + scroll. */
	if (slot->focusable != 0u || slot->clip_children != 0u) {
		return 1;
	}
	if (slot->callbacks.on_click != NULL || slot->callbacks.on_event != NULL || slot->callbacks.on_pointer_enter != NULL) {
		return 1;
	}
	w = ui_clay_prop_str(slot, "widget");
	if (w == NULL) {
		return 0;
	}
	if (strcmp(w, "panel") == 0 || strcmp(w, "button") == 0 || strcmp(w, "checkbox") == 0 || strcmp(w, "slider") == 0 || strcmp(w, "text_input") == 0 ||
		strcmp(w, "scroll_view") == 0 || strcmp(w, "scroll_content") == 0) {
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
 * Prefer the public node id string (CLAY_SID); otherwise CLAY_SIDI("skui_n", index).
 * Loop rows with explicit ids (menu-log-0) or auto indices both hash stably.
 */
static Clay_ElementId ui_clay_make_id(const ui_node_slot_t* slot, sk_ui_node_t node) {
	static const Clay_String k_fallback = {.isStaticallyAllocated = true, .length = 6, .chars = "skui_n"};
	if (slot != NULL && slot->id != NULL && slot->id[0] != '\0') {
		return CLAY_SID(ui_clay_cstr(slot->id));
	}
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
	/* Main-axis packing is only approximated: Clay has no space-between/evenly. */
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
	case SK_UI_JUSTIFY_SPACE_BETWEEN:
	case SK_UI_JUSTIFY_SPACE_AROUND:
	case SK_UI_JUSTIFY_SPACE_EVENLY:
		/* Clay has no main-axis space distribution; keep start alignment. */
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
 * POINT → FIXED, PERCENT → PERCENT (0-1), AUTO + grow → GROW, else FIT.
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
		axis.type = CLAY__SIZING_TYPE_FIXED;
		axis.size.minMax.min = len.value;
		axis.size.minMax.max = len.value;
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

static void ui_clay_log_limitation(ui_clay_frame_t* fr, const_chr_t panel_id, const_chr_t detail) {
	sk_logger_t* logger;
	if (fr == NULL || fr->limitation_logged != 0) {
		return;
	}
	fr->limitation_logged = 1;
	logger = sk_logger_api()->create_logger("clay_adapter");
	if (logger != NULL) {
		sk_log_message(sk_logger_api(), SK_LOGGER_TYPE_WARN, logger, "panel '%s' Clay limitation: %s (kept best-effort mapping)", panel_id != NULL ? panel_id : "(anon)",
					   detail != NULL ? detail : "unknown");
		sk_logger_api()->destroy_logger(logger);
	}
}

/* Forward */
static void ui_clay_declare_node(sk_ui_context_t* ctx, sk_ui_node_t node, f32 parent_w, f32 parent_h, i32 parent_def, const_chr_t panel_id, i32* limitations);

static void ui_clay_declare_text_content(const ui_node_slot_t* slot) {
	const_chr_t text = ui_clay_prop_str(slot, "text");
	Clay_TextElementConfig cfg;
	Clay_String s;
	f32 font_size;
	if (text == NULL) {
		text = "";
	}
	font_size = slot->computed.font_size > 0.0f ? slot->computed.font_size : 14.0f;
	memset(&cfg, 0, sizeof(cfg));
	cfg.fontId = 0u;
	cfg.fontSize = ui_clay_u16_clamp(font_size);
	cfg.textColor = ui_clay_color(slot->computed.color);
	/* WRAP_NONE avoids per-word measure-cache blowups on long labels. */
	cfg.wrapMode = CLAY_TEXT_WRAP_NONE;
	cfg.textAlignment = CLAY_TEXT_ALIGN_LEFT;
	s = ui_clay_cstr(text);
	Clay__OpenTextElement(s, Clay__StoreTextElementConfig(cfg));
}

// NOLINTBEGIN(misc-no-recursion)
static void ui_clay_declare_node(sk_ui_context_t* ctx, sk_ui_node_t node, f32 parent_w, f32 parent_h, i32 parent_def, const_chr_t panel_id, i32* limitations) {
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
	i32 reverse = 0;
	u32 i;
	const_chr_t widget;
	i32 is_scroll;
	i32 is_text_kind;
	i32 force_id;

	if (slot == NULL || fr == NULL) {
		return;
	}
	if (node.index >= fr->cap) {
		return;
	}

	ls = &slot->layout_style;
	widget = ui_clay_prop_str(slot, "widget");
	is_scroll = (widget != NULL && strcmp(widget, "scroll_view") == 0) || slot->clip_children != 0u;
	is_text_kind = (slot->kind == (u8)SK_UI_NODE_KIND_TEXT) || (widget != NULL && strcmp(widget, "label") == 0);

	if (ls->flex_wrap == SK_UI_FLEX_WRAP || ls->flex_wrap == SK_UI_FLEX_WRAP_REVERSE) {
		if (limitations != NULL) {
			*limitations |= 1;
		}
		ui_clay_log_limitation(fr, panel_id, "flex-wrap is not supported by Clay; children stay on a single line");
	}
	if (ls->position == SK_UI_POSITION_ABSOLUTE) {
		if (limitations != NULL) {
			*limitations |= 2;
		}
		ui_clay_log_limitation(fr, panel_id, "absolute positioning mapped via Clay floating; constraints may differ");
	}
	if (ls->margin.left > 0.0001f || ls->margin.right > 0.0001f || ls->margin.top > 0.0001f || ls->margin.bottom > 0.0001f) {
		if (limitations != NULL) {
			*limitations |= 4;
		}
		ui_clay_log_limitation(fr, panel_id, "margins are not a Clay primitive; margin is ignored under panel Clay path");
	}

	memset(&decl, 0, sizeof(decl));
	force_id = ui_clay_needs_stable_id(slot);
	eid = ui_clay_make_id(slot, node);
	/* Always assign a stable id so writeback, hover, and tests can resolve the node. */
	decl.id = eid;
	fr->ids[node.index] = eid;
	(void)force_id;

	decl.layout.layoutDirection = ui_clay_map_direction(ls->flex_direction, &reverse);
	if (reverse != 0) {
		if (limitations != NULL) {
			*limitations |= 8;
		}
		ui_clay_log_limitation(fr, panel_id, "flex reverse directions are not supported by Clay; using forward axis");
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
	decl.layout.childGap = ui_clay_u16_clamp(gap);

	decl.layout.sizing.width = ui_clay_map_axis(ls->width, ls->min_width, ls->max_width, ls->flex_grow, parent_w, parent_def);
	decl.layout.sizing.height = ui_clay_map_axis(ls->height, ls->min_height, ls->max_height, ls->flex_grow, parent_h, parent_def);

	/* Stretch on cross axis: approximate with GROW when align_items is stretch and size is auto. */
	if (ls->align_self == SK_UI_ALIGN_STRETCH || (ls->align_self == SK_UI_ALIGN_AUTO /* parent stretch handled on parent children only */)) {
		/* no-op here; stretch is a parent concern */
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

	if (is_scroll) {
		decl.clip.horizontal = true;
		decl.clip.vertical = true;
		/* Engine scroll props are content offset; Clay childOffset shifts children. */
		decl.clip.childOffset.x = -ui_clay_prop_f32(slot, "scroll_x", 0.0f);
		decl.clip.childOffset.y = -ui_clay_prop_f32(slot, "scroll_y", 0.0f);
	}

	if (ls->position == SK_UI_POSITION_ABSOLUTE) {
		decl.floating.attachTo = CLAY_ATTACH_TO_PARENT;
		decl.floating.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE;
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

	/* Intrinsic text only when the box is not fully fixed-sized (FIT needs it).
	 * Fully fixed boxes skip Clay text leaves — engine paint draws the glyphs. */
	{
		i32 fixed_w = ls->width.unit == SK_UI_LENGTH_POINT ? 1 : 0;
		i32 fixed_h = ls->height.unit == SK_UI_LENGTH_POINT ? 1 : 0;
		i32 need_text = 0;
		if (is_text_kind && !(fixed_w && fixed_h)) {
			need_text = 1;
		}
		if (widget != NULL && (strcmp(widget, "button") == 0 || strcmp(widget, "text_input") == 0) && !(fixed_w && fixed_h)) {
			need_text = 1;
		}
		if (need_text != 0 && ui_clay_prop_str(slot, "text") != NULL) {
			ui_clay_declare_text_content(slot);
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

		/* Scroll content: prefer explicit content_width/height props when set. */
		if (widget != NULL && strcmp(widget, "scroll_content") == 0) {
			/* Parent scroll_view holds content size props — read from parent. */
			const ui_node_slot_t* pslot = sk_ui_node_is_valid(slot->parent) ? ui_slot(ctx, slot->parent) : NULL;
			if (pslot != NULL) {
				f32 cw = ui_clay_prop_f32(pslot, "content_width", 0.0f);
				f32 ch = ui_clay_prop_f32(pslot, "content_height", 0.0f);
				(void)cw;
				(void)ch;
			}
		}

		for (i = 0u; i < slot->children.count; ++i) {
			ui_clay_declare_node(ctx, slot->children.items[i], child_pw, child_ph, child_def, panel_id, limitations);
		}
	}

	Clay__CloseElement();
}
// NOLINTEND(misc-no-recursion)

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

	if (slot == NULL || fr == NULL || node.index >= fr->cap) {
		return;
	}
	if (fr->present[node.index] == 0u) {
		/* Still walk children in case of partial declare. */
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

	content_abs_x = bb.x + border_l + pad_l;
	content_abs_y = bb.y + border_t + pad_t;

	slot->dirty = (u16)((u32)slot->dirty & ~(u32)SK_UI_DIRTY_LAYOUT);

	for (i = 0u; i < slot->children.count; ++i) {
		ui_clay_writeback_node(ctx, slot->children.items[i], content_abs_x, content_abs_y);
	}
}
// NOLINTEND(misc-no-recursion)

/**
 * Re-layout one panel surface (and all nested containers) via Clay into the
 * panel's already-computed content box from the custom baseline pass.
 */
static i32 ui_clay_relayout_panel(sk_ui_context_t* ctx, sk_ui_node_t panel) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, panel);
	ui_clay_frame_t* fr;
	f32 cw;
	f32 ch;
	const_chr_t panel_id;
	i32 limitations = 0;
	u32 i;
	Clay_Vector2 pointer;
	i32 pointer_down;

	if (slot == NULL) {
		return -1;
	}
	cw = slot->layout_content.width;
	ch = slot->layout_content.height;
	if (cw < 1.0f && ch < 1.0f) {
		/* Degenerate panel — leave custom baseline rects. */
		return 0;
	}
	/* Ensure a minimum positive viewport so Clay does not collapse. */
	if (cw < 1.0f) {
		cw = 1.0f;
	}
	if (ch < 1.0f) {
		ch = 1.0f;
	}

	if (ui_clay_frame_ensure(ctx, ctx->slots.count) != 0) {
		return -1;
	}
	fr = ctx->clay_frame;
	panel_id = slot->id;

	if (ui_clay_ensure_init(ctx->allocator, cw, ch, NULL, NULL) != 0) {
		return -1;
	}
	/* Drop measure-cache entries between panel passes so the arena stays healthy. */
	Clay_ResetMeasureTextCache();

	/* Feed pointer so Clay_Hovered / scroll containers resolve across frames. */
	pointer.x = ctx->pointer_x;
	pointer.y = ctx->pointer_y;
	/* Convert to panel-content-local if possible: subtract absolute panel content origin.
	 * Baseline layout stores relative rects; walk ancestors for abs origin. */
	{
		f32 abs_x = slot->layout_content.x;
		f32 abs_y = slot->layout_content.y;
		sk_ui_node_t walk = slot->parent;
		while (sk_ui_node_is_valid(walk)) {
			const ui_node_slot_t* ps = ui_slot(ctx, walk);
			if (ps == NULL) {
				break;
			}
			abs_x += ps->layout_content.x;
			abs_y += ps->layout_content.y;
			walk = ps->parent;
		}
		pointer.x = ctx->pointer_x - abs_x;
		pointer.y = ctx->pointer_y - abs_y;
	}
	pointer_down = (ctx->pointer_buttons & 1u) != 0u ? 1 : 0;
	Clay_SetPointerState(pointer, pointer_down != 0);
	{
		Clay_Vector2 scroll_delta;
		scroll_delta.x = ctx->scroll_delta_x;
		scroll_delta.y = ctx->scroll_delta_y;
		Clay_UpdateScrollContainers(false, scroll_delta, 1.0f / 60.0f);
		ctx->scroll_delta_x = 0.0f;
		ctx->scroll_delta_y = 0.0f;
	}

	Clay_BeginLayout();

	/* Synthetic root = panel content box: direction/gap/align from panel, no extra pad. */
	{
		Clay_ElementDeclaration root_decl;
		static const Clay_String k_root = {.isStaticallyAllocated = true, .length = 13, .chars = "sk_panel_root"};
		i32 reverse = 0;
		memset(&root_decl, 0, sizeof(root_decl));
		root_decl.id = CLAY_SIDI(k_root, panel.index);
		root_decl.layout.sizing.width = CLAY_SIZING_FIXED(cw);
		root_decl.layout.sizing.height = CLAY_SIZING_FIXED(ch);
		root_decl.layout.layoutDirection = ui_clay_map_direction(slot->layout_style.flex_direction, &reverse);
		if (reverse != 0) {
			limitations |= 8;
			ui_clay_log_limitation(fr, panel_id, "flex reverse directions are not supported by Clay; using forward axis");
		}
		ui_clay_map_child_align(&slot->layout_style, &root_decl.layout.childAlignment);
		{
			f32 gap = slot->layout_style.flex_direction == SK_UI_FLEX_ROW || slot->layout_style.flex_direction == SK_UI_FLEX_ROW_REVERSE ? slot->layout_style.column_gap :
																																		   slot->layout_style.row_gap;
			if (gap <= 0.0f) {
				gap = ui_clay_fmaxf(slot->layout_style.row_gap, slot->layout_style.column_gap);
			}
			root_decl.layout.childGap = ui_clay_u16_clamp(gap);
		}
		/* Padding already applied outside content; root is pure content box. */
		root_decl.userData = (void*)(uintptr_t)panel.index;

		Clay__OpenElement();
		Clay__ConfigureOpenElement(root_decl);

		for (i = 0u; i < slot->children.count; ++i) {
			ui_clay_declare_node(ctx, slot->children.items[i], cw, ch, 1, panel_id, &limitations);
		}

		Clay__CloseElement();
	}

	(void)Clay_EndLayout();

	/* Write Clay boxes back as relative rects under the panel content origin. */
	for (i = 0u; i < slot->children.count; ++i) {
		ui_clay_writeback_node(ctx, slot->children.items[i], 0.0f, 0.0f);
	}

	fr->panel_layout_count += 1;
	(void)limitations;
	return 0;
}

// NOLINTBEGIN(misc-no-recursion)
static i32 ui_clay_panel_has_panel_ancestor(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return 0;
	}
	while (sk_ui_node_is_valid(slot->parent)) {
		const ui_node_slot_t* ps = ui_slot(ctx, slot->parent);
		if (ps == NULL) {
			break;
		}
		if (ui_clay_is_panel_slot(ps)) {
			return 1;
		}
		slot = ps;
	}
	return 0;
}
// NOLINTEND(misc-no-recursion)

static void ui_clay_relayout_all_panels(sk_ui_context_t* ctx) {
	u32 i;
	/* Panel roots only: nested panels are declared as Clay children of the outer panel. */
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		sk_ui_node_t node;
		if (slot->alive == 0u) {
			continue;
		}
		if (!ui_clay_is_panel_slot(slot)) {
			continue;
		}
		node.index = i;
		node.generation = slot->generation;
		if (ui_clay_panel_has_panel_ancestor(ctx, node)) {
			continue;
		}
		(void)ui_clay_relayout_panel(ctx, node);
	}
}

/* -------------------------------------------------------------------------- */
/* Public adapter API                                                         */
/* -------------------------------------------------------------------------- */

i32 ui_clay_layout_impl(sk_ui_context_t* ctx, f32 root_width, f32 root_height) {
	i32 rc;
	if (ctx == NULL) {
		return -1;
	}
	/*
	 * Baseline: custom flex solver for the whole tree (keeps non-panel tests
	 * and unsupported features working). Then migrate every panel surface —
	 * and its nested containers — onto Clay.
	 */
	rc = ui_layout_impl(ctx, root_width, root_height);
	if (rc != 0) {
		return rc;
	}
	ui_clay_relayout_all_panels(ctx);
	return 0;
}

i32 ui_clay_paint_impl(sk_ui_context_t* ctx, const sk_ui_paint_params_t* params) {
	/*
	 * Paint still walks the retained tree using slot layout rects written by
	 * the Clay layout pass. Engine widget decorations (scrollbars, caret,
	 * checkbox marks, slider thumbs) stay on the existing paint path.
	 * Full Clay render-command translation is deferred until all surfaces
	 * migrate (avoids dual draw sources mid-migration).
	 */
	return ui_paint_impl(ctx, params);
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

static void ui_clay_assert_rect_near(const sk_ui_rect_t* r, f32 x, f32 y, f32 w, f32 h) {
	TEST_ASSERT_FLOAT_WITHIN(1.0f, x, r->x);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, y, r->y);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, w, r->width);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, h, r->height);
}

SK_TEST(ui_clay_panel_column_children_laid_out) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t panel;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_style_props_t p;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	panel = ui->widget_panel(ctx, root, "clay-panel");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(panel));

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_PADDING;
	p.layout.width = sk_ui_pt(200.0f);
	p.layout.height = sk_ui_pt(120.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.padding.left = p.layout.padding.right = p.layout.padding.top = p.layout.padding.bottom = 0.0f;
	/* Clear default panel padding/border so child y positions are exact. */
	p.mask |= SK_UI_SP_BORDER_WIDTH;
	p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, panel, &p));

	a = ui->widget_view(ctx, panel, "clay-child-a");
	b = ui->widget_view(ctx, panel, "clay-child-b");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(200.0f);
	p.layout.height = sk_ui_pt(40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	/* Column under panel content: A at top, B below A. */
	ui_clay_assert_rect_near(&ra, 0.0f, 0.0f, 200.0f, 40.0f);
	ui_clay_assert_rect_near(&rb, 0.0f, 40.0f, 200.0f, 40.0f);

	/* Clay path must have been exercised (frame state allocated). */
	TEST_ASSERT_NOT_NULL(ctx->clay_frame);
	TEST_ASSERT_TRUE(ctx->clay_frame->panel_layout_count >= 1);

	ui->context_destroy(ctx);
}

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
	ui_clay_assert_rect_near(&rl, 0.0f, 0.0f, 80.0f, 40.0f);
	ui_clay_assert_rect_near(&rr, 80.0f, 0.0f, 80.0f, 40.0f);

	/* Stable string ids resolve through Clay after the layout pass. */
	eid = Clay_GetElementId(ui_clay_cstr("row-left"));
	ed = Clay_GetElementData(eid);
	TEST_ASSERT_TRUE(ed.found);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 80.0f, ed.boundingBox.width);

	ui->context_destroy(ctx);
}

SK_TEST(ui_clay_nested_container_under_panel) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t panel;
	sk_ui_node_t row;
	sk_ui_node_t c0;
	sk_ui_node_t c1;
	sk_ui_rect_t rr;
	sk_ui_rect_t r0;
	sk_ui_rect_t r1;
	sk_ui_style_props_t p;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	panel = ui->widget_panel(ctx, root, "nest-panel");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_PADDING | SK_UI_SP_BORDER_WIDTH;
	p.layout.width = sk_ui_pt(220.0f);
	p.layout.height = sk_ui_pt(80.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.padding.left = p.layout.padding.right = p.layout.padding.top = p.layout.padding.bottom = 0.0f;
	p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, panel, &p));

	row = ui->widget_view(ctx, panel, "nest-row");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION;
	p.layout.width = sk_ui_pt(220.0f);
	p.layout.height = sk_ui_pt(40.0f);
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, row, &p));

	c0 = ui->widget_view(ctx, row, "nest-c0");
	c1 = ui->widget_view(ctx, row, "nest-c1");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(100.0f);
	p.layout.height = sk_ui_pt(40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c0, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c1, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, row, &rr, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c0, &r0, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c1, &r1, NULL));
	ui_clay_assert_rect_near(&rr, 0.0f, 0.0f, 220.0f, 40.0f);
	/* Children relative to row content origin. */
	ui_clay_assert_rect_near(&r0, 0.0f, 0.0f, 100.0f, 40.0f);
	ui_clay_assert_rect_near(&r1, 100.0f, 0.0f, 100.0f, 40.0f);

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
