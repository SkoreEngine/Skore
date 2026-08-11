/**
 * @file layout.c
 * @brief Flexbox layout solver for the retained UI element tree.
 *
 * Custom pure-C solver (not Yoga): keeps the plugin C-only, matches the
 * engine allocator model, and integrates a host measure callback without
 * pulling a C++ dependency. Algorithm follows CSS Flexbox for the v1
 * property set: direction (+ reverse), wrap, justify/align, grow/shrink/
 * basis, min/max, padding/margin/border, gap, and absolute positioning
 * against the nearest positioned ancestor.
 *
 * All geometry is logical pixels. HiDPI scale is applied only in
 * layout_apply_scale so the solver never sees content scale.
 */

#include "ui_internal.h"

#include "allocator.h"

#include <math.h>

/* -------------------------------------------------------------------------- */
/* Constants / helpers                                                        */
/* -------------------------------------------------------------------------- */

#define UI_LAYOUT_EPS 0.0001f
#define UI_LAYOUT_MAX_FLEX_ITERS 8

static f32 ui_fmaxf(f32 a, f32 b) {
	return a > b ? a : b;
}

static f32 ui_fabsf(f32 a) {
	return a < 0.0f ? -a : a;
}

static i32 ui_is_row(sk_ui_flex_direction_t d) {
	return d == SK_UI_FLEX_ROW || d == SK_UI_FLEX_ROW_REVERSE;
}

static i32 ui_is_reverse_main(sk_ui_flex_direction_t d) {
	return d == SK_UI_FLEX_ROW_REVERSE || d == SK_UI_FLEX_COLUMN_REVERSE;
}

static i32 ui_length_is_definite(sk_ui_length_t l) {
	return l.unit == SK_UI_LENGTH_POINT || l.unit == SK_UI_LENGTH_PERCENT;
}

/** Resolve length against a definite parent axis size. AUTO → NaN-like -1. */
static f32 ui_resolve_length(sk_ui_length_t l, f32 parent_size, i32 parent_definite) {
	if (l.unit == SK_UI_LENGTH_POINT) {
		return l.value;
	}
	if (l.unit == SK_UI_LENGTH_PERCENT) {
		if (!parent_definite || parent_size < 0.0f) {
			return -1.0f;
		}
		return parent_size * (l.value / 100.0f);
	}
	return -1.0f;
}

static f32 ui_resolve_min(sk_ui_length_t l, f32 parent_size, i32 parent_definite) {
	f32 v = ui_resolve_length(l, parent_size, parent_definite);
	return v < 0.0f ? 0.0f : v;
}

static f32 ui_resolve_max(sk_ui_length_t l, f32 parent_size, i32 parent_definite) {
	f32 v = ui_resolve_length(l, parent_size, parent_definite);
	return v < 0.0f ? 1.0e30f : v;
}

static f32 ui_clamp(f32 v, f32 lo, f32 hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

/* -------------------------------------------------------------------------- */
/* Public style defaults                                                      */
/* -------------------------------------------------------------------------- */

void ui_layout_style_init_default(sk_ui_layout_style_t* style) {
	memset(style, 0, sizeof(*style));
	style->flex_direction = SK_UI_FLEX_COLUMN;
	style->flex_wrap = SK_UI_FLEX_NOWRAP;
	style->justify_content = SK_UI_JUSTIFY_FLEX_START;
	style->align_items = SK_UI_ALIGN_STRETCH;
	style->align_self = SK_UI_ALIGN_AUTO;
	style->align_content = SK_UI_ALIGN_FLEX_START;
	style->flex_grow = 0.0f;
	style->flex_shrink = 1.0f;
	style->flex_basis = sk_ui_auto();
	style->width = sk_ui_auto();
	style->height = sk_ui_auto();
	style->min_width = sk_ui_pt(0.0f);
	style->min_height = sk_ui_pt(0.0f);
	style->max_width = sk_ui_auto();
	style->max_height = sk_ui_auto();
	style->position = SK_UI_POSITION_RELATIVE;
	style->left = sk_ui_auto();
	style->top = sk_ui_auto();
	style->right = sk_ui_auto();
	style->bottom = sk_ui_auto();
}

/* -------------------------------------------------------------------------- */
/* Style / query API                                                          */
/* -------------------------------------------------------------------------- */

i32 ui_node_set_layout_style_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_layout_style_t* style) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	slot->layout_style = *style;
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_LAYOUT);
	return 0;
}

i32 ui_node_get_layout_style_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_layout_style_t* out) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (out != NULL) {
		*out = slot->layout_style;
	}
	return 0;
}

i32 ui_node_get_layout_rect_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (out_border != NULL) {
		*out_border = slot->layout_border;
	}
	if (out_content != NULL) {
		*out_content = slot->layout_content;
	}
	return 0;
}

i32 ui_node_get_layout_rect_scaled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (out_border != NULL) {
		*out_border = slot->layout_border_scaled;
	}
	if (out_content != NULL) {
		*out_content = slot->layout_content_scaled;
	}
	return 0;
}

void ui_set_measure_fn_impl(sk_ui_context_t* ctx, sk_ui_measure_fn fn, void_ptr_t user) {
	ctx->measure_fn = fn;
	ctx->measure_user = user;
}

void ui_layout_get_content_scale_impl(const sk_ui_context_t* ctx, f32* out_scale_x, f32* out_scale_y) {
	if (out_scale_x != NULL) {
		*out_scale_x = ctx->content_scale_x;
	}
	if (out_scale_y != NULL) {
		*out_scale_y = ctx->content_scale_y;
	}
}

/* -------------------------------------------------------------------------- */
/* Scratch item / line structures                                             */
/* -------------------------------------------------------------------------- */

typedef struct ui_flex_item_t {
	sk_ui_node_t node;
	ui_node_slot_t* slot;
	const sk_ui_layout_style_t* style;

	f32 margin_main_start;
	f32 margin_main_end;
	f32 margin_cross_start;
	f32 margin_cross_end;
	f32 border_pad_main;  /* border + padding on main axis */
	f32 border_pad_cross; /* border + padding on cross axis */

	f32 flex_base;	  /* inner main size before grow/shrink */
	f32 hypo_main;	  /* clamped base */
	f32 target_main;  /* after grow/shrink (inner) */
	f32 target_cross; /* inner cross size */
	f32 outer_main;	  /* target_main + margins + (already in target for border box?) */
	f32 min_main;
	f32 max_main;
	f32 min_cross;
	f32 max_cross;

	f32 grow;
	f32 shrink;
	i32 frozen;
	i32 is_absolute;

	/* Final border-box placement relative to parent content origin */
	f32 main_pos;
	f32 cross_pos;
	f32 main_size;	/* border box main */
	f32 cross_size; /* border box cross */

	sk_ui_align_t align;
	u32 line_index;
} ui_flex_item_t;

typedef struct ui_flex_line_t {
	u32 first;
	u32 count;
	f32 main_size;	/* sum of outer main sizes + gaps between items */
	f32 cross_size; /* max outer cross on the line */
	f32 cross_pos;	/* offset of line on cross axis */
} ui_flex_line_t;

/* Forward */
static void ui_layout_node(sk_ui_context_t* ctx, sk_ui_node_t node, f32 avail_w, f32 avail_h, i32 avail_w_def, i32 avail_h_def, f32 parent_content_w, f32 parent_content_h);

static void ui_call_measure(sk_ui_context_t* ctx, sk_ui_node_t node, f32 w, sk_ui_measure_mode_t wm, f32 h, sk_ui_measure_mode_t hm, sk_ui_size_t* out) {
	sk_ui_measure_constraint_t c;
	out->width = 0.0f;
	out->height = 0.0f;
	if (ctx->measure_fn == NULL) {
		return;
	}
	c.width = w;
	c.height = h;
	c.width_mode = wm;
	c.height_mode = hm;
	ctx->measure_fn(ctx, node, &c, out, ctx->measure_user);
}

/**
 * Compute preferred inner size for a node given parent content size and
 * available space (for percent / measure).
 */
static void ui_compute_inner_pref(sk_ui_context_t* ctx, ui_node_slot_t* slot, sk_ui_node_t node, f32 parent_w, f32 parent_h, i32 parent_w_def, i32 parent_h_def, f32* out_w,
								  f32* out_h) {
	const sk_ui_layout_style_t* s = &slot->layout_style;
	f32 w = ui_resolve_length(s->width, parent_w, parent_w_def);
	f32 h = ui_resolve_length(s->height, parent_h, parent_h_def);
	f32 min_w = ui_resolve_min(s->min_width, parent_w, parent_w_def);
	f32 min_h = ui_resolve_min(s->min_height, parent_h, parent_h_def);
	f32 max_w = ui_resolve_max(s->max_width, parent_w, parent_w_def);
	f32 max_h = ui_resolve_max(s->max_height, parent_h, parent_h_def);
	sk_ui_size_t measured;

	if (w < 0.0f || h < 0.0f) {
		sk_ui_measure_mode_t wm = SK_UI_MEASURE_UNDEFINED;
		sk_ui_measure_mode_t hm = SK_UI_MEASURE_UNDEFINED;
		f32 mw = 0.0f;
		f32 mh = 0.0f;
		if (w >= 0.0f) {
			wm = SK_UI_MEASURE_EXACTLY;
			mw = w;
		} else if (parent_w_def) {
			wm = SK_UI_MEASURE_AT_MOST;
			mw = ui_fmaxf(0.0f, parent_w - s->margin.left - s->margin.right);
		}
		if (h >= 0.0f) {
			hm = SK_UI_MEASURE_EXACTLY;
			mh = h;
		} else if (parent_h_def) {
			hm = SK_UI_MEASURE_AT_MOST;
			mh = ui_fmaxf(0.0f, parent_h - s->margin.top - s->margin.bottom);
		}
		ui_call_measure(ctx, node, mw, wm, mh, hm, &measured);
		if (w < 0.0f) {
			w = measured.width;
		}
		if (h < 0.0f) {
			h = measured.height;
		}
	}

	if (w < 0.0f) {
		w = 0.0f;
	}
	if (h < 0.0f) {
		h = 0.0f;
	}
	*out_w = ui_clamp(w, min_w, max_w);
	*out_h = ui_clamp(h, min_h, max_h);
}

static f32 ui_inner_to_border_main(const ui_flex_item_t* it, f32 inner_main) {
	return inner_main + it->border_pad_main;
}

static f32 ui_inner_to_border_cross(const ui_flex_item_t* it, f32 inner_cross) {
	return inner_cross + it->border_pad_cross;
}

static f32 ui_outer_main(const ui_flex_item_t* it) {
	return ui_inner_to_border_main(it, it->target_main) + it->margin_main_start + it->margin_main_end;
}

static f32 ui_outer_cross(const ui_flex_item_t* it) {
	return ui_inner_to_border_cross(it, it->target_cross) + it->margin_cross_start + it->margin_cross_end;
}

/* Distribute free space with integer-friendly rounding of remainders. */
static void ui_distribute_grow(ui_flex_item_t* items, u32 first, u32 count, f32 free_space) {
	u32 iter;
	for (iter = 0u; iter < UI_LAYOUT_MAX_FLEX_ITERS; ++iter) {
		f32 sum_grow = 0.0f;
		u32 i;
		f32 used = 0.0f;
		u32 unfrozen = 0u;
		if (free_space <= UI_LAYOUT_EPS) {
			break;
		}
		for (i = 0u; i < count; ++i) {
			ui_flex_item_t* it = &items[first + i];
			if (it->frozen || it->grow <= 0.0f) {
				continue;
			}
			sum_grow += it->grow;
			unfrozen += 1u;
		}
		if (unfrozen == 0u || sum_grow <= 0.0f) {
			break;
		}
		for (i = 0u; i < count; ++i) {
			ui_flex_item_t* it = &items[first + i];
			f32 share;
			f32 next;
			if (it->frozen || it->grow <= 0.0f) {
				continue;
			}
			share = free_space * (it->grow / sum_grow);
			next = it->target_main + share;
			if (next > it->max_main) {
				used += it->max_main - it->target_main;
				it->target_main = it->max_main;
				it->frozen = 1;
			} else {
				used += share;
				it->target_main = next;
			}
		}
		free_space -= used;
		if (ui_fabsf(used) < UI_LAYOUT_EPS) {
			break;
		}
	}
}

static void ui_distribute_shrink(ui_flex_item_t* items, u32 first, u32 count, f32 overflow) {
	u32 iter;
	for (iter = 0u; iter < UI_LAYOUT_MAX_FLEX_ITERS; ++iter) {
		f32 sum_scaled = 0.0f;
		u32 i;
		f32 used = 0.0f;
		u32 unfrozen = 0u;
		if (overflow <= UI_LAYOUT_EPS) {
			break;
		}
		for (i = 0u; i < count; ++i) {
			ui_flex_item_t* it = &items[first + i];
			if (it->frozen || it->shrink <= 0.0f) {
				continue;
			}
			sum_scaled += it->shrink * it->flex_base;
			unfrozen += 1u;
		}
		if (unfrozen == 0u || sum_scaled <= UI_LAYOUT_EPS) {
			break;
		}
		for (i = 0u; i < count; ++i) {
			ui_flex_item_t* it = &items[first + i];
			f32 scaled;
			f32 ratio;
			f32 delta;
			f32 next;
			if (it->frozen || it->shrink <= 0.0f) {
				continue;
			}
			scaled = it->shrink * it->flex_base;
			ratio = scaled / sum_scaled;
			delta = overflow * ratio;
			next = it->target_main - delta;
			if (next < it->min_main) {
				used += it->target_main - it->min_main;
				it->target_main = it->min_main;
				it->frozen = 1;
			} else {
				used += delta;
				it->target_main = next;
			}
		}
		overflow -= used;
		if (ui_fabsf(used) < UI_LAYOUT_EPS) {
			break;
		}
	}
}

/**
 * Round target_main (inner) values to whole logical pixels while preserving
 * the pre-round outer-main sum of the line (so grow/shrink totals stay exact).
 * Does not force items to fill the container when free space was not consumed.
 */
static void ui_round_main_sizes(ui_flex_item_t* items, u32 first, u32 count) {
	f32 sum_before = 0.0f;
	f32 sum_after = 0.0f;
	f32 diff;
	u32 i;
	if (count == 0u) {
		return;
	}
	for (i = 0u; i < count; ++i) {
		sum_before += items[first + i].target_main;
	}
	for (i = 0u; i < count; ++i) {
		f32 v = items[first + i].target_main;
		items[first + i].target_main = floorf(v + 0.5f);
		sum_after += items[first + i].target_main;
	}
	diff = sum_before - sum_after;
	/* Distribute ±1px remainders so rounded inners match the flex total. */
	if (ui_fabsf(diff) >= 0.5f) {
		i32 step = diff > 0.0f ? 1 : -1;
		i32 left = (i32)floorf(ui_fabsf(diff) + 0.5f);
		u32 guard = 0u;
		while (left > 0 && guard < count * 4u) {
			ui_flex_item_t* it = &items[first + (guard % count)];
			f32 next = it->target_main + (f32)step;
			if (next >= it->min_main - UI_LAYOUT_EPS && next <= it->max_main + UI_LAYOUT_EPS) {
				it->target_main = next;
				left -= 1;
			}
			guard += 1u;
		}
	}
}

static void ui_place_main_axis(ui_flex_item_t* items, u32 first, u32 count, f32 gap, f32 container_main, sk_ui_justify_t justify, i32 reverse) {
	f32 sum_outer = 0.0f;
	f32 free_space;
	f32 main_pos;
	f32 between = gap;
	f32 around_extra = 0.0f;
	u32 i;
	u32 n = count;

	if (n == 0u) {
		return;
	}
	for (i = 0u; i < n; ++i) {
		sum_outer += ui_outer_main(&items[first + i]);
	}
	sum_outer += gap * (f32)(n > 0u ? n - 1u : 0u);
	free_space = container_main - sum_outer;
	if (free_space < 0.0f) {
		free_space = 0.0f;
	}

	main_pos = 0.0f;
	switch (justify) {
	case SK_UI_JUSTIFY_FLEX_END:
		main_pos = free_space;
		break;
	case SK_UI_JUSTIFY_CENTER:
		main_pos = free_space * 0.5f;
		break;
	case SK_UI_JUSTIFY_SPACE_BETWEEN:
		if (n > 1u) {
			between = gap + free_space / (f32)(n - 1u);
		}
		break;
	case SK_UI_JUSTIFY_SPACE_AROUND:
		if (n > 0u) {
			around_extra = free_space / (f32)n;
			main_pos = around_extra * 0.5f;
			between = gap + around_extra;
		}
		break;
	case SK_UI_JUSTIFY_SPACE_EVENLY:
		if (n > 0u) {
			around_extra = free_space / (f32)(n + 1u);
			main_pos = around_extra;
			between = gap + around_extra;
		}
		break;
	case SK_UI_JUSTIFY_FLEX_START:
	default:
		break;
	}

	if (!reverse) {
		for (i = 0u; i < n; ++i) {
			ui_flex_item_t* it = &items[first + i];
			it->main_pos = main_pos + it->margin_main_start;
			it->main_size = ui_inner_to_border_main(it, it->target_main);
			main_pos += ui_outer_main(it) + (i + 1u < n ? between : 0.0f);
		}
	} else {
		/* Place from main-end toward start. */
		main_pos = container_main - main_pos;
		for (i = 0u; i < n; ++i) {
			ui_flex_item_t* it = &items[first + i];
			f32 outer = ui_outer_main(it);
			main_pos -= outer;
			it->main_pos = main_pos + it->margin_main_start;
			it->main_size = ui_inner_to_border_main(it, it->target_main);
			if (i + 1u < n) {
				main_pos -= between;
			}
		}
	}
}

static void ui_place_cross_in_line(ui_flex_item_t* items, u32 first, u32 count, f32 line_cross, f32 line_cross_pos, sk_ui_align_t container_align, i32 row) {
	u32 i;
	for (i = 0u; i < count; ++i) {
		ui_flex_item_t* it = &items[first + i];
		sk_ui_align_t align = it->align;
		f32 outer_cross;
		f32 free_c;
		if (align == SK_UI_ALIGN_AUTO) {
			align = container_align;
		}
		if (align == SK_UI_ALIGN_STRETCH) {
			const sk_ui_layout_style_t* s = it->style;
			i32 cross_auto = row ? !ui_length_is_definite(s->height) : !ui_length_is_definite(s->width);
			if (cross_auto) {
				f32 inner = line_cross - it->margin_cross_start - it->margin_cross_end - it->border_pad_cross;
				inner = ui_clamp(inner, it->min_cross, it->max_cross);
				if (inner < 0.0f) {
					inner = 0.0f;
				}
				it->target_cross = inner;
			}
		}

		outer_cross = ui_outer_cross(it);
		free_c = line_cross - outer_cross;
		if (free_c < 0.0f) {
			free_c = 0.0f;
		}
		switch (align) {
		case SK_UI_ALIGN_FLEX_END:
			it->cross_pos = line_cross_pos + free_c + it->margin_cross_start;
			break;
		case SK_UI_ALIGN_CENTER:
			it->cross_pos = line_cross_pos + free_c * 0.5f + it->margin_cross_start;
			break;
		case SK_UI_ALIGN_STRETCH:
		case SK_UI_ALIGN_FLEX_START:
		case SK_UI_ALIGN_AUTO:
		default:
			it->cross_pos = line_cross_pos + it->margin_cross_start;
			break;
		}
		it->cross_size = ui_inner_to_border_cross(it, it->target_cross);
	}
}

static void ui_place_lines(ui_flex_line_t* lines, u32 line_count, f32 container_cross, sk_ui_align_t align_content, f32 row_gap, i32 wrap_reverse) {
	f32 sum_cross = 0.0f;
	f32 free_space;
	f32 cross_pos;
	f32 between;
	u32 i;
	if (line_count == 0u) {
		return;
	}
	for (i = 0u; i < line_count; ++i) {
		sum_cross += lines[i].cross_size;
	}
	sum_cross += row_gap * (f32)(line_count > 0u ? line_count - 1u : 0u);
	free_space = container_cross - sum_cross;
	if (free_space < 0.0f) {
		free_space = 0.0f;
	}
	cross_pos = 0.0f;
	between = row_gap;
	switch (align_content) {
	case SK_UI_ALIGN_FLEX_END:
		cross_pos = free_space;
		break;
	case SK_UI_ALIGN_CENTER:
		cross_pos = free_space * 0.5f;
		break;
	case SK_UI_ALIGN_STRETCH:
		if (line_count > 0u && free_space > 0.0f) {
			f32 extra = free_space / (f32)line_count;
			for (i = 0u; i < line_count; ++i) {
				lines[i].cross_size += extra;
			}
			free_space = 0.0f;
		}
		break;
	case SK_UI_ALIGN_FLEX_START:
	case SK_UI_ALIGN_AUTO:
	default:
		break;
	}
	/* space-between/around for align-content: map from justify-like via align_content only has flex values in our enum; stretch/start/end/center covered. */

	if (!wrap_reverse) {
		for (i = 0u; i < line_count; ++i) {
			lines[i].cross_pos = cross_pos;
			cross_pos += lines[i].cross_size + (i + 1u < line_count ? between : 0.0f);
		}
	} else {
		cross_pos = container_cross - cross_pos;
		for (i = 0u; i < line_count; ++i) {
			cross_pos -= lines[i].cross_size;
			lines[i].cross_pos = cross_pos;
			if (i + 1u < line_count) {
				cross_pos -= between;
			}
		}
	}
}

static sk_ui_align_t ui_resolve_align_self(const sk_ui_layout_style_t* child, sk_ui_align_t items) {
	if (child->align_self == SK_UI_ALIGN_AUTO) {
		return items;
	}
	return child->align_self;
}

static void ui_init_flex_item(sk_ui_context_t* ctx, ui_flex_item_t* it, sk_ui_node_t node, ui_node_slot_t* slot, i32 row, f32 parent_w, f32 parent_h, i32 parent_w_def,
							  i32 parent_h_def) {
	const sk_ui_layout_style_t* s = &slot->layout_style;
	f32 pref_w;
	f32 pref_h;
	f32 basis;
	f32 main_pref;
	f32 cross_pref;

	memset(it, 0, sizeof(*it));
	it->node = node;
	it->slot = slot;
	it->style = s;
	it->is_absolute = (s->position == SK_UI_POSITION_ABSOLUTE) ? 1 : 0;
	it->grow = s->flex_grow > 0.0f ? s->flex_grow : 0.0f;
	it->shrink = s->flex_shrink > 0.0f ? s->flex_shrink : 0.0f;
	it->align = ui_resolve_align_self(s, SK_UI_ALIGN_AUTO);

	if (row) {
		it->margin_main_start = s->margin.left;
		it->margin_main_end = s->margin.right;
		it->margin_cross_start = s->margin.top;
		it->margin_cross_end = s->margin.bottom;
		it->border_pad_main = s->border.left + s->border.right + s->padding.left + s->padding.right;
		it->border_pad_cross = s->border.top + s->border.bottom + s->padding.top + s->padding.bottom;
		it->min_main = ui_resolve_min(s->min_width, parent_w, parent_w_def);
		it->max_main = ui_resolve_max(s->max_width, parent_w, parent_w_def);
		it->min_cross = ui_resolve_min(s->min_height, parent_h, parent_h_def);
		it->max_cross = ui_resolve_max(s->max_height, parent_h, parent_h_def);
	} else {
		it->margin_main_start = s->margin.top;
		it->margin_main_end = s->margin.bottom;
		it->margin_cross_start = s->margin.left;
		it->margin_cross_end = s->margin.right;
		it->border_pad_main = s->border.top + s->border.bottom + s->padding.top + s->padding.bottom;
		it->border_pad_cross = s->border.left + s->border.right + s->padding.left + s->padding.right;
		it->min_main = ui_resolve_min(s->min_height, parent_h, parent_h_def);
		it->max_main = ui_resolve_max(s->max_height, parent_h, parent_h_def);
		it->min_cross = ui_resolve_min(s->min_width, parent_w, parent_w_def);
		it->max_cross = ui_resolve_max(s->max_width, parent_w, parent_w_def);
	}

	ui_compute_inner_pref(ctx, slot, node, parent_w, parent_h, parent_w_def, parent_h_def, &pref_w, &pref_h);
	main_pref = row ? pref_w : pref_h;
	cross_pref = row ? pref_h : pref_w;

	/* flex-basis */
	if (s->flex_basis.unit == SK_UI_LENGTH_AUTO) {
		/* Prefer width/height if definite on main axis, else measured pref. */
		if (row && ui_length_is_definite(s->width)) {
			basis = ui_resolve_length(s->width, parent_w, parent_w_def);
		} else if (!row && ui_length_is_definite(s->height)) {
			basis = ui_resolve_length(s->height, parent_h, parent_h_def);
		} else {
			basis = main_pref;
		}
	} else {
		basis = ui_resolve_length(s->flex_basis, row ? parent_w : parent_h, row ? parent_w_def : parent_h_def);
		if (basis < 0.0f) {
			basis = main_pref;
		}
	}

	it->flex_base = ui_fmaxf(0.0f, basis);
	it->hypo_main = ui_clamp(it->flex_base, it->min_main, it->max_main);
	it->target_main = it->hypo_main;
	it->target_cross = ui_clamp(cross_pref, it->min_cross, it->max_cross);
	it->frozen = 0;
}

static void ui_write_item_rect(ui_flex_item_t* it, i32 row, f32 content_origin_x, f32 content_origin_y) {
	sk_ui_rect_t* b = &it->slot->layout_border;
	sk_ui_rect_t* c = &it->slot->layout_content;
	const sk_ui_layout_style_t* s = it->style;
	f32 x;
	f32 y;
	f32 w;
	f32 h;

	if (row) {
		x = content_origin_x + it->main_pos;
		y = content_origin_y + it->cross_pos;
		w = it->main_size;
		h = it->cross_size;
	} else {
		x = content_origin_x + it->cross_pos;
		y = content_origin_y + it->main_pos;
		w = it->cross_size;
		h = it->main_size;
	}
	b->x = x;
	b->y = y;
	b->width = ui_fmaxf(0.0f, w);
	b->height = ui_fmaxf(0.0f, h);

	c->x = x + s->border.left + s->padding.left;
	c->y = y + s->border.top + s->padding.top;
	c->width = ui_fmaxf(0.0f, b->width - s->border.left - s->border.right - s->padding.left - s->padding.right);
	c->height = ui_fmaxf(0.0f, b->height - s->border.top - s->border.bottom - s->padding.top - s->padding.bottom);
}

// NOLINTBEGIN(misc-no-recursion)
static void ui_layout_absolute_child(sk_ui_context_t* ctx, sk_ui_node_t child, ui_node_slot_t* child_slot, f32 cb_w, f32 cb_h, i32 cb_w_def, i32 cb_h_def) {
	const sk_ui_layout_style_t* s = &child_slot->layout_style;
	f32 left = ui_resolve_length(s->left, cb_w, cb_w_def);
	f32 right = ui_resolve_length(s->right, cb_w, cb_w_def);
	f32 top = ui_resolve_length(s->top, cb_h, cb_h_def);
	f32 bottom = ui_resolve_length(s->bottom, cb_h, cb_h_def);
	f32 width = ui_resolve_length(s->width, cb_w, cb_w_def);
	f32 height = ui_resolve_length(s->height, cb_h, cb_h_def);
	f32 min_w = ui_resolve_min(s->min_width, cb_w, cb_w_def);
	f32 min_h = ui_resolve_min(s->min_height, cb_h, cb_h_def);
	f32 max_w = ui_resolve_max(s->max_width, cb_w, cb_w_def);
	f32 max_h = ui_resolve_max(s->max_height, cb_h, cb_h_def);
	f32 ml = s->margin.left;
	f32 mr = s->margin.right;
	f32 mt = s->margin.top;
	f32 mb = s->margin.bottom;
	f32 x = 0.0f;
	f32 y = 0.0f;
	f32 bw;
	f32 bh;
	sk_ui_size_t measured;

	/* Resolve width */
	if (width < 0.0f && left >= 0.0f && right >= 0.0f && cb_w_def) {
		width = cb_w - left - right - ml - mr;
	}
	if (height < 0.0f && top >= 0.0f && bottom >= 0.0f && cb_h_def) {
		height = cb_h - top - bottom - mt - mb;
	}
	if (width < 0.0f || height < 0.0f) {
		sk_ui_measure_mode_t wm = width >= 0.0f ? SK_UI_MEASURE_EXACTLY : (cb_w_def ? SK_UI_MEASURE_AT_MOST : SK_UI_MEASURE_UNDEFINED);
		sk_ui_measure_mode_t hm = height >= 0.0f ? SK_UI_MEASURE_EXACTLY : (cb_h_def ? SK_UI_MEASURE_AT_MOST : SK_UI_MEASURE_UNDEFINED);
		f32 mw = width >= 0.0f ? width : (cb_w_def ? cb_w : 0.0f);
		f32 mh = height >= 0.0f ? height : (cb_h_def ? cb_h : 0.0f);
		ui_call_measure(ctx, child, mw, wm, mh, hm, &measured);
		if (width < 0.0f) {
			width = measured.width;
		}
		if (height < 0.0f) {
			height = measured.height;
		}
	}
	if (width < 0.0f) {
		width = 0.0f;
	}
	if (height < 0.0f) {
		height = 0.0f;
	}
	width = ui_clamp(width, min_w, max_w);
	height = ui_clamp(height, min_h, max_h);

	/* Border box includes padding+border for absolute: width/height are border-box in our model when set as style width (CSS content-box differs; we treat style width as border-box for simplicity matching common UI kits). */
	bw = width;
	bh = height;

	if (left >= 0.0f) {
		x = left + ml;
	} else if (right >= 0.0f && cb_w_def) {
		x = cb_w - right - mr - bw;
	} else {
		x = ml;
	}
	if (top >= 0.0f) {
		y = top + mt;
	} else if (bottom >= 0.0f && cb_h_def) {
		y = cb_h - bottom - mb - bh;
	} else {
		y = mt;
	}

	child_slot->layout_border.x = x;
	child_slot->layout_border.y = y;
	child_slot->layout_border.width = ui_fmaxf(0.0f, bw);
	child_slot->layout_border.height = ui_fmaxf(0.0f, bh);
	child_slot->layout_content.x = x + s->border.left + s->padding.left;
	child_slot->layout_content.y = y + s->border.top + s->padding.top;
	child_slot->layout_content.width = ui_fmaxf(0.0f, bw - s->border.left - s->border.right - s->padding.left - s->padding.right);
	child_slot->layout_content.height = ui_fmaxf(0.0f, bh - s->border.top - s->border.bottom - s->padding.top - s->padding.bottom);

	/* Recurse into absolute subtree with its content box as available space. */
	ui_layout_node(ctx, child, child_slot->layout_content.width, child_slot->layout_content.height, 1, 1, child_slot->layout_content.width, child_slot->layout_content.height);
}

/**
 * Layout children of a flex container whose border box is already known on slot.
 * Child positions are relative to the parent's content box origin in parent space
 * (i.e. layout_border of children is relative to parent content origin).
 */
static i32 ui_layout_flex_children(sk_ui_context_t* ctx, sk_ui_node_t node, ui_node_slot_t* slot) {
	const sk_ui_layout_style_t* cs = &slot->layout_style;
	(void)node;
	const sk_allocator_t* a = ctx->allocator;
	i32 row = ui_is_row(cs->flex_direction);
	i32 reverse = ui_is_reverse_main(cs->flex_direction);
	i32 wrap = (cs->flex_wrap != SK_UI_FLEX_NOWRAP) ? 1 : 0;
	i32 wrap_reverse = (cs->flex_wrap == SK_UI_FLEX_WRAP_REVERSE) ? 1 : 0;
	f32 content_w = slot->layout_content.width;
	f32 content_h = slot->layout_content.height;
	f32 main_size = row ? content_w : content_h;
	f32 cross_size = row ? content_h : content_w;
	f32 main_gap = row ? cs->column_gap : cs->row_gap;
	f32 cross_gap = row ? cs->row_gap : cs->column_gap;
	u32 child_count = slot->children.count;
	ui_flex_item_t* items = NULL;
	ui_flex_line_t* lines = NULL;
	u32 flex_count = 0u;
	u32 abs_count = 0u;
	u32 line_count = 0u;
	u32 i;
	u32 li;
	i32 rc = 0;

	if (child_count == 0u) {
		return 0;
	}

	items = (ui_flex_item_t*)a->alloc(a->instance, sizeof(ui_flex_item_t) * (size_t)child_count);
	if (items == NULL) {
		return -1;
	}
	lines = (ui_flex_line_t*)a->alloc(a->instance, sizeof(ui_flex_line_t) * (size_t)child_count);
	if (lines == NULL) {
		a->free(a->instance, items);
		return -1;
	}

	/* Collect flex vs absolute children */
	for (i = 0u; i < child_count; ++i) {
		sk_ui_node_t ch = slot->children.items[i];
		ui_node_slot_t* ch_slot = ui_slot_mut(ctx, ch);
		ui_flex_item_t tmp;
		if (ch_slot == NULL) {
			continue;
		}
		ui_init_flex_item(ctx, &tmp, ch, ch_slot, row, content_w, content_h, 1, 1);
		tmp.align = ui_resolve_align_self(&ch_slot->layout_style, cs->align_items);
		if (tmp.is_absolute) {
			/* stash absolute at end region later — handle after flex */
			items[child_count - 1u - abs_count] = tmp;
			abs_count += 1u;
		} else {
			items[flex_count] = tmp;
			flex_count += 1u;
		}
	}

	/* Pack into lines */
	if (!wrap || flex_count == 0u) {
		if (flex_count > 0u) {
			lines[0].first = 0u;
			lines[0].count = flex_count;
			line_count = 1u;
		}
	} else {
		f32 line_main = 0.0f;
		u32 line_first = 0u;
		u32 line_n = 0u;
		for (i = 0u; i < flex_count; ++i) {
			f32 outer = ui_outer_main(&items[i]);
			f32 need = (line_n == 0u) ? outer : (line_main + main_gap + outer);
			if (line_n > 0u && need > main_size + UI_LAYOUT_EPS) {
				lines[line_count].first = line_first;
				lines[line_count].count = line_n;
				line_count += 1u;
				line_first = i;
				line_n = 0u;
				line_main = 0.0f;
			}
			if (line_n == 0u) {
				line_main = outer;
			} else {
				line_main += main_gap + outer;
			}
			line_n += 1u;
		}
		if (line_n > 0u) {
			lines[line_count].first = line_first;
			lines[line_count].count = line_n;
			line_count += 1u;
		}
	}

	/* Grow/shrink + place each line */
	for (li = 0u; li < line_count; ++li) {
		ui_flex_line_t* line = &lines[li];
		f32 used = 0.0f;
		f32 free_sp;
		u32 j;
		f32 max_cross = 0.0f;

		for (j = 0u; j < line->count; ++j) {
			ui_flex_item_t* it = &items[line->first + j];
			it->target_main = it->hypo_main;
			it->frozen = 0;
			used += ui_outer_main(it);
		}
		used += main_gap * (f32)(line->count > 0u ? line->count - 1u : 0u);
		free_sp = main_size - used;

		if (free_sp > UI_LAYOUT_EPS) {
			ui_distribute_grow(items, line->first, line->count, free_sp);
		} else if (free_sp < -UI_LAYOUT_EPS) {
			ui_distribute_shrink(items, line->first, line->count, -free_sp);
		}

		ui_round_main_sizes(items, line->first, line->count);

		/* Cross size of line = max outer cross after stretch prep (pre-stretch uses target_cross) */
		for (j = 0u; j < line->count; ++j) {
			f32 oc = ui_outer_cross(&items[line->first + j]);
			if (oc > max_cross) {
				max_cross = oc;
			}
		}
		line->main_size = main_size;
		line->cross_size = max_cross;

		ui_place_main_axis(items, line->first, line->count, main_gap, main_size, cs->justify_content, reverse);
	}

	/* align-content across lines */
	if (line_count == 1u) {
		lines[0].cross_size = cross_size; /* single line uses full cross for stretch */
		lines[0].cross_pos = 0.0f;
	} else {
		ui_place_lines(lines, line_count, cross_size, cs->align_content, cross_gap, wrap_reverse);
	}

	for (li = 0u; li < line_count; ++li) {
		ui_flex_line_t* line = &lines[li];
		ui_place_cross_in_line(items, line->first, line->count, line->cross_size, line->cross_pos, cs->align_items, row);
	}

	/* Write rects and recurse */
	for (i = 0u; i < flex_count; ++i) {
		ui_flex_item_t* it = &items[i];
		f32 child_avail_w;
		f32 child_avail_h;
		ui_write_item_rect(it, row, 0.0f, 0.0f);
		/* Children layout relative to their own content box; recursive call uses content size as available. */
		child_avail_w = it->slot->layout_content.width;
		child_avail_h = it->slot->layout_content.height;
		ui_layout_node(ctx, it->node, child_avail_w, child_avail_h, 1, 1, child_avail_w, child_avail_h);
	}

	/* Absolute children: containing block = padding edge of this node ≈ content+padding = border inset by border only.
	 * CSS uses padding edge; we use content box origin with size = padding box (content + padding). */
	{
		f32 pad_w = content_w + cs->padding.left + cs->padding.right;
		f32 pad_h = content_h + cs->padding.top + cs->padding.bottom;
		/* Position absolute relative to padding box; convert to content-relative by subtracting padding. */
		for (i = 0u; i < abs_count; ++i) {
			ui_flex_item_t* it = &items[child_count - 1u - i];
			ui_layout_absolute_child(ctx, it->node, it->slot, pad_w, pad_h, 1, 1);
			/* Absolute positions from ui_layout_absolute_child are relative to padding box top-left.
			 * Convert to parent content origin: subtract padding (content origin is padding inset). */
			it->slot->layout_border.x -= cs->padding.left;
			it->slot->layout_border.y -= cs->padding.top;
			it->slot->layout_content.x -= cs->padding.left;
			it->slot->layout_content.y -= cs->padding.top;
		}
	}

	a->free(a->instance, lines);
	a->free(a->instance, items);
	return rc;
}

/**
 * Resolve this node's border box given available size from parent, then layout children.
 * On entry for non-root, parent has already set layout_border/content for this node when
 * placing as a flex item. For the root, we set it here from root_width/height.
 *
 * When called after placement, avail_w/h are the content box constraints for laying out
 * *this* node's children — and this node's border box is already written.
 *
 * Special case: first call on root sets root border to full root size.
 */
static void ui_layout_node(sk_ui_context_t* ctx, sk_ui_node_t node, f32 avail_w, f32 avail_h, i32 avail_w_def, i32 avail_h_def, f32 parent_content_w, f32 parent_content_h) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	const sk_ui_layout_style_t* s;
	(void)parent_content_w;
	(void)parent_content_h;
	(void)avail_w_def;
	(void)avail_h_def;
	if (slot == NULL) {
		return;
	}
	s = &slot->layout_style;

	/* If this is the root (no parent), establish border box from available size. */
	if (!sk_ui_node_is_valid(slot->parent)) {
		f32 w = avail_w;
		f32 h = avail_h;
		if (ui_length_is_definite(s->width)) {
			f32 rw = ui_resolve_length(s->width, avail_w, 1);
			if (rw >= 0.0f) {
				w = rw;
			}
		}
		if (ui_length_is_definite(s->height)) {
			f32 rh = ui_resolve_length(s->height, avail_h, 1);
			if (rh >= 0.0f) {
				h = rh;
			}
		}
		w = ui_clamp(w, ui_resolve_min(s->min_width, avail_w, 1), ui_resolve_max(s->max_width, avail_w, 1));
		h = ui_clamp(h, ui_resolve_min(s->min_height, avail_h, 1), ui_resolve_max(s->max_height, avail_h, 1));
		slot->layout_border.x = s->margin.left;
		slot->layout_border.y = s->margin.top;
		slot->layout_border.width = ui_fmaxf(0.0f, w - s->margin.left - s->margin.right);
		slot->layout_border.height = ui_fmaxf(0.0f, h - s->margin.top - s->margin.bottom);
		slot->layout_content.x = slot->layout_border.x + s->border.left + s->padding.left;
		slot->layout_content.y = slot->layout_border.y + s->border.top + s->padding.top;
		slot->layout_content.width = ui_fmaxf(0.0f, slot->layout_border.width - s->border.left - s->border.right - s->padding.left - s->padding.right);
		slot->layout_content.height = ui_fmaxf(0.0f, slot->layout_border.height - s->border.top - s->border.bottom - s->padding.top - s->padding.bottom);
	}

	/* Layout flex children into this content box. */
	(void)ui_layout_flex_children(ctx, node, slot);
}
// NOLINTEND(misc-no-recursion)

// NOLINTBEGIN(misc-no-recursion)
static void ui_walk_clear_layout(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	u32 i;
	if (slot == NULL) {
		return;
	}
	for (i = 0u; i < slot->children.count; ++i) {
		ui_walk_clear_layout(ctx, slot->children.items[i]);
	}
	slot->dirty = (u16)((u32)slot->dirty & ~(u32)SK_UI_DIRTY_LAYOUT);
}
// NOLINTEND(misc-no-recursion)

i32 ui_layout_impl(sk_ui_context_t* ctx, f32 root_width, f32 root_height) {
	sk_ui_node_t root = ctx->root;
	ctx->root_width = root_width;
	ctx->root_height = root_height;
	ui_layout_node(ctx, root, root_width, root_height, 1, 1, root_width, root_height);
	ui_walk_clear_layout(ctx, root);
	return 0;
}

i32 ui_layout_apply_scale_impl(sk_ui_context_t* ctx, f32 scale_x, f32 scale_y) {
	u32 i;
	u32 old_x_bits;
	u32 old_y_bits;
	u32 new_x_bits;
	u32 new_y_bits;
	i32 scale_changed;
	memcpy(&old_x_bits, &ctx->content_scale_x, sizeof(old_x_bits));
	memcpy(&old_y_bits, &ctx->content_scale_y, sizeof(old_y_bits));
	memcpy(&new_x_bits, &scale_x, sizeof(new_x_bits));
	memcpy(&new_y_bits, &scale_y, sizeof(new_y_bits));
	scale_changed = (old_x_bits != new_x_bits) || (old_y_bits != new_y_bits) ? 1 : 0;
	ctx->content_scale_x = scale_x;
	ctx->content_scale_y = scale_y;
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		if (slot->alive == 0u) {
			continue;
		}
		slot->layout_border_scaled.x = slot->layout_border.x * scale_x;
		slot->layout_border_scaled.y = slot->layout_border.y * scale_y;
		slot->layout_border_scaled.width = slot->layout_border.width * scale_x;
		slot->layout_border_scaled.height = slot->layout_border.height * scale_y;
		slot->layout_content_scaled.x = slot->layout_content.x * scale_x;
		slot->layout_content_scaled.y = slot->layout_content.y * scale_y;
		slot->layout_content_scaled.width = slot->layout_content.width * scale_x;
		slot->layout_content_scaled.height = slot->layout_content.height * scale_y;
	}
	/* Geometry in the draw list is physical; scale changes force a repaint. */
	if (scale_changed != 0 && sk_ui_node_is_valid(ctx->root)) {
		ui_mark_dirty_up(ctx, ctx->root, (u32)SK_UI_DIRTY_PAINT);
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Unit tests                                                                 */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

static void ui_assert_rect_near(const sk_ui_rect_t* r, f32 x, f32 y, f32 w, f32 h) {
	TEST_ASSERT_FLOAT_WITHIN(0.51f, x, r->x);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, y, r->y);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, w, r->width);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, h, r->height);
}

static sk_ui_layout_style_t ui_style_box(f32 w, f32 h) {
	sk_ui_layout_style_t s;
	ui_layout_style_init_default(&s);
	s.width = sk_ui_pt(w);
	s.height = sk_ui_pt(h);
	return s;
}

static void ui_measure_fixed_100x20(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_measure_constraint_t* c, sk_ui_size_t* out, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)c;
	(void)user;
	out->width = 100.0f;
	out->height = 20.0f;
}

static void ui_measure_wrap_text(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_measure_constraint_t* c, sk_ui_size_t* out, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)user;
	/* Simulate text: prefer 200x16; if width constrained, wrap to multiple lines. */
	if (c->width_mode == SK_UI_MEASURE_AT_MOST || c->width_mode == SK_UI_MEASURE_EXACTLY) {
		f32 w = c->width < 200.0f ? c->width : 200.0f;
		if (w < 1.0f) {
			w = 1.0f;
		}
		out->width = w;
		out->height = 16.0f * ceilf(200.0f / w);
	} else {
		out->width = 200.0f;
		out->height = 16.0f;
	}
}

SK_TEST(ui_layout_row_basic) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = ui_style_box(40.0f, 20.0f);
		sk_ui_layout_style_t sb = ui_style_box(60.0f, 30.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &sb));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	/* Child rects are relative to parent content origin. */
	ui_assert_rect_near(&ra, 0.0f, 0.0f, 40.0f, 20.0f);
	ui_assert_rect_near(&rb, 40.0f, 0.0f, 60.0f, 30.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_column_basic) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_COLUMN;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = ui_style_box(40.0f, 20.0f);
		sk_ui_layout_style_t sb = ui_style_box(60.0f, 30.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &sb));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	ui_assert_rect_near(&ra, 0.0f, 0.0f, 40.0f, 20.0f);
	ui_assert_rect_near(&rb, 0.0f, 20.0f, 60.0f, 30.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_row_reverse) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW_REVERSE;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = ui_style_box(40.0f, 10.0f);
		sk_ui_layout_style_t sb = ui_style_box(60.0f, 10.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &sb));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 50.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	/* reverse: first item against the end */
	ui_assert_rect_near(&ra, 160.0f, 0.0f, 40.0f, 10.0f);
	ui_assert_rect_near(&rb, 100.0f, 0.0f, 60.0f, 10.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_justify_content_variants) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	rs.justify_content = SK_UI_JUSTIFY_CENTER;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = ui_style_box(20.0f, 10.0f);
		sk_ui_layout_style_t sb = ui_style_box(20.0f, 10.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &sb));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 50.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	ui_assert_rect_near(&ra, 30.0f, 0.0f, 20.0f, 10.0f);
	ui_assert_rect_near(&rb, 50.0f, 0.0f, 20.0f, 10.0f);

	rs.justify_content = SK_UI_JUSTIFY_SPACE_BETWEEN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 50.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	ui_assert_rect_near(&ra, 0.0f, 0.0f, 20.0f, 10.0f);
	ui_assert_rect_near(&rb, 80.0f, 0.0f, 20.0f, 10.0f);

	rs.justify_content = SK_UI_JUSTIFY_FLEX_END;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 50.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	ui_assert_rect_near(&ra, 60.0f, 0.0f, 20.0f, 10.0f);
	ui_assert_rect_near(&rb, 80.0f, 0.0f, 20.0f, 10.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_align_items_and_self) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_CENTER;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = ui_style_box(20.0f, 20.0f);
		sk_ui_layout_style_t sb = ui_style_box(20.0f, 20.0f);
		sb.align_self = SK_UI_ALIGN_FLEX_END;
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &sb));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	ui_assert_rect_near(&ra, 0.0f, 40.0f, 20.0f, 20.0f);
	ui_assert_rect_near(&rb, 20.0f, 80.0f, 20.0f, 20.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_align_stretch) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_STRETCH;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa;
		ui_layout_style_init_default(&sa);
		sa.width = sk_ui_pt(50.0f);
		/* height auto → stretch to 100 */
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	ui_assert_rect_near(&ra, 0.0f, 0.0f, 50.0f, 100.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_flex_grow) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = ui_style_box(0.0f, 10.0f);
		sk_ui_layout_style_t sb = ui_style_box(0.0f, 10.0f);
		sa.flex_grow = 1.0f;
		sa.flex_basis = sk_ui_pt(0.0f);
		sb.flex_grow = 3.0f;
		sb.flex_basis = sk_ui_pt(0.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &sb));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 50.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	ui_assert_rect_near(&ra, 0.0f, 0.0f, 25.0f, 10.0f);
	ui_assert_rect_near(&rb, 25.0f, 0.0f, 75.0f, 10.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_flex_shrink_rounding) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t rc;
	f32 sum_w;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	c = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t s = ui_style_box(50.0f, 10.0f);
		s.flex_shrink = 1.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &s));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &s));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, c, &s));
	}

	/* 150 natural into 100 → shrink; rounded sizes should sum to 100. */
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 40.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
	sum_w = ra.width + rb.width + rc.width;
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, sum_w);
	/* Equal shrink: sizes within 1px of each other after rounding. */
	TEST_ASSERT_FLOAT_WITHIN(1.01f, ra.width, rb.width);
	TEST_ASSERT_FLOAT_WITHIN(1.01f, rb.width, rc.width);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_gap_padding_margin_border) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t ca;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	rs.padding.left = 10.0f;
	rs.padding.top = 5.0f;
	rs.padding.right = 10.0f;
	rs.padding.bottom = 5.0f;
	rs.column_gap = 8.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = ui_style_box(30.0f, 20.0f);
		sk_ui_layout_style_t sb = ui_style_box(30.0f, 20.0f);
		sa.margin.left = 2.0f;
		sa.border.left = 1.0f;
		sa.border.right = 1.0f;
		sa.border.top = 1.0f;
		sa.border.bottom = 1.0f;
		sa.padding.left = 3.0f;
		sa.padding.right = 3.0f;
		sa.padding.top = 3.0f;
		sa.padding.bottom = 3.0f;
		/* style width is content/inner in our basis model — border box = inner + pad + border */
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &sb));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, &ca));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	/* Relative to root content: margin 2 (root padding is outside content origin). */
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 2.0f, ra.x);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 0.0f, ra.y);
	/* border box width = 30 + 6 pad + 2 border = 38 */
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 38.0f, ra.width);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 28.0f, ra.height);
	/* content inside a */
	TEST_ASSERT_FLOAT_WITHIN(0.51f, ra.x + 1.0f + 3.0f, ca.x);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 30.0f, ca.width);
	/* b after a outer + gap; a margin-end is 0 */
	TEST_ASSERT_FLOAT_WITHIN(0.51f, ra.x + ra.width + 8.0f, rb.x);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_min_max_constraints) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = ui_style_box(10.0f, 10.0f);
		sa.flex_grow = 1.0f;
		sa.max_width = sk_ui_pt(40.0f);
		sa.min_height = sk_ui_pt(50.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 40.0f, ra.width);
	TEST_ASSERT_TRUE(ra.height >= 49.5f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_percent_width) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_COLUMN;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa;
		ui_layout_style_init_default(&sa);
		sa.width = sk_ui_percent(50.0f);
		sa.height = sk_ui_pt(20.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	ui_assert_rect_near(&ra, 0.0f, 0.0f, 100.0f, 20.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_wrap_and_align_content) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t n[4];
	sk_ui_layout_style_t rs;
	sk_ui_rect_t r0;
	sk_ui_rect_t r1;
	sk_ui_rect_t r2;
	u32 i;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.flex_wrap = SK_UI_FLEX_WRAP;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	rs.align_content = SK_UI_ALIGN_FLEX_START;
	rs.row_gap = 4.0f;
	rs.column_gap = 4.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	for (i = 0u; i < 4u; ++i) {
		sk_ui_layout_style_t s = ui_style_box(60.0f, 20.0f);
		n[i] = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, n[i], &s));
	}

	/* width 130 → two per line (60+4+60=124), third wraps */
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 130.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, n[0], &r0, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, n[1], &r1, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, n[2], &r2, NULL));
	ui_assert_rect_near(&r0, 0.0f, 0.0f, 60.0f, 20.0f);
	ui_assert_rect_near(&r1, 64.0f, 0.0f, 60.0f, 20.0f);
	ui_assert_rect_near(&r2, 0.0f, 24.0f, 60.0f, 20.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_nested) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t row;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t rrow;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_COLUMN;
	rs.padding.left = 10.0f;
	rs.padding.top = 10.0f;
	rs.padding.right = 10.0f;
	rs.padding.bottom = 10.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	row = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t row_s;
		ui_layout_style_init_default(&row_s);
		row_s.flex_direction = SK_UI_FLEX_ROW;
		row_s.width = sk_ui_percent(100.0f);
		row_s.height = sk_ui_pt(40.0f);
		row_s.align_items = SK_UI_ALIGN_STRETCH;
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, row, &row_s));
	}
	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, row);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, row);
	{
		sk_ui_layout_style_t sa;
		sk_ui_layout_style_t sb;
		ui_layout_style_init_default(&sa);
		ui_layout_style_init_default(&sb);
		sa.flex_grow = 1.0f;
		sa.flex_basis = sk_ui_pt(0.0f);
		sb.flex_grow = 1.0f;
		sb.flex_basis = sk_ui_pt(0.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &sb));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 220.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, row, &rrow, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	/* row is relative to root content (padding already applied as content origin). */
	ui_assert_rect_near(&rrow, 0.0f, 0.0f, 200.0f, 40.0f);
	/* children relative to row content origin */
	ui_assert_rect_near(&ra, 0.0f, 0.0f, 100.0f, 40.0f);
	ui_assert_rect_near(&rb, 100.0f, 0.0f, 100.0f, 40.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_absolute_positioning) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t flow;
	sk_ui_node_t abs;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t rflow;
	sk_ui_rect_t rabs;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_COLUMN;
	rs.padding.left = 5.0f;
	rs.padding.top = 5.0f;
	rs.padding.right = 5.0f;
	rs.padding.bottom = 5.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	flow = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	abs = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sf = ui_style_box(50.0f, 20.0f);
		sk_ui_layout_style_t sa;
		ui_layout_style_init_default(&sa);
		sa.position = SK_UI_POSITION_ABSOLUTE;
		sa.width = sk_ui_pt(30.0f);
		sa.height = sk_ui_pt(30.0f);
		sa.right = sk_ui_pt(0.0f);
		sa.bottom = sk_ui_pt(0.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, flow, &sf));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, abs, &sa));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, flow, &rflow, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, abs, &rabs, NULL));
	/* flow at content origin (absolute is out of flow) */
	ui_assert_rect_near(&rflow, 0.0f, 0.0f, 50.0f, 20.0f);
	/* absolute bottom-right of padding box, content-relative: (65,65) */
	ui_assert_rect_near(&rabs, 65.0f, 65.0f, 30.0f, 30.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_measure_callback) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t text;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t rt;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	ui->set_measure_fn(ctx, ui_measure_fixed_100x20, NULL);
	text = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, root);
	{
		sk_ui_layout_style_t st;
		ui_layout_style_init_default(&st);
		/* auto size → measure */
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, text, &st));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 300.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, text, &rt, NULL));
	ui_assert_rect_near(&rt, 0.0f, 0.0f, 100.0f, 20.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_measure_constrained) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t text;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t rt;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_COLUMN;
	rs.align_items = SK_UI_ALIGN_STRETCH;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	ui->set_measure_fn(ctx, ui_measure_wrap_text, NULL);
	text = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, root);
	{
		sk_ui_layout_style_t st;
		ui_layout_style_init_default(&st);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, text, &st));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 200.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, text, &rt, NULL));
	/* stretched width 100, height from wrap ≈ 32 */
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 100.0f, rt.width);
	TEST_ASSERT_TRUE(rt.height >= 31.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_apply_scale_hidpi) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t logical;
	sk_ui_rect_t scaled;
	f32 sx;
	f32 sy;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));
	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = ui_style_box(40.0f, 20.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 50.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 2.0f, 2.0f));
	ui->layout_get_content_scale(ctx, &sx, &sy);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, sx);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, sy);

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &logical, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect_scaled(ctx, a, &scaled, NULL));
	ui_assert_rect_near(&logical, 0.0f, 0.0f, 40.0f, 20.0f);
	ui_assert_rect_near(&scaled, 0.0f, 0.0f, 80.0f, 40.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_fixture_toolbar) {
	/* Fixture: [icon 24][title grow][btn 64] row, 320 logical wide, padding 8, gap 8 */
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t icon;
	sk_ui_node_t title;
	sk_ui_node_t btn;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t r_icon;
	sk_ui_rect_t r_title;
	sk_ui_rect_t r_btn;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_CENTER;
	rs.padding.left = rs.padding.right = rs.padding.top = rs.padding.bottom = 8.0f;
	rs.column_gap = 8.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	icon = ui->node_create(ctx, SK_UI_NODE_KIND_IMAGE, root);
	title = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, root);
	btn = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, root);
	{
		sk_ui_layout_style_t si = ui_style_box(24.0f, 24.0f);
		sk_ui_layout_style_t st;
		sk_ui_layout_style_t sb = ui_style_box(64.0f, 28.0f);
		ui_layout_style_init_default(&st);
		st.height = sk_ui_pt(20.0f);
		st.flex_grow = 1.0f;
		st.flex_basis = sk_ui_pt(0.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, icon, &si));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, title, &st));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, btn, &sb));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 320.0f, 44.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, icon, &r_icon, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, title, &r_title, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, btn, &r_btn, NULL));

	/* Content-relative: content is 304x28; icon 24 + gap 8 + title 200 + gap 8 + btn 64.
	 * Cross align center on 28px content height. */
	ui_assert_rect_near(&r_icon, 0.0f, 2.0f, 24.0f, 24.0f);
	ui_assert_rect_near(&r_title, 32.0f, 4.0f, 200.0f, 20.0f);
	ui_assert_rect_near(&r_btn, 240.0f, 0.0f, 64.0f, 28.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_clears_dirty_flag) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 100.0f));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, a, (u32)SK_UI_DIRTY_LAYOUT));

	ui->context_destroy(ctx);
}

SK_TEST(ui_layout_space_evenly) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_layout_style_t rs;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.justify_content = SK_UI_JUSTIFY_SPACE_EVENLY;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));
	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t s = ui_style_box(20.0f, 10.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &s));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &s));
	}
	/* free = 100-40=60; evenly → 20 before, between, after */
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 50.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
	ui_assert_rect_near(&ra, 20.0f, 0.0f, 20.0f, 10.0f);
	ui_assert_rect_near(&rb, 60.0f, 0.0f, 20.0f, 10.0f);

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
