/**
 * @file color.c
 * @brief ColorEdit / ColorPicker family (APX-357; WIDGET_MANIFEST.md §15).
 *
 * ColorButton (property-column swatch) opens a §12 popup_menu hosting
 * ColorPicker4 (HSV SV square, hue bar, alpha bar, half-alpha preview).
 * ColorEdit3 is a compact float[3] RGB row. Color swatch is read-only
 * 14×14 with NoPicker | NoTooltip. Binding is a plain float* / Color* —
 * no retained item-array. ColorEdit4 / ColorPicker3 / SetColorEditOptions
 * are not implemented.
 */

#include "ui.internal.h"

#include "allocator.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SK_TESTS
#include "test.h"
#endif

enum {
	UI_COLOR_KIND_BUTTON = 1,
	UI_COLOR_KIND_PICKER = 2,
	UI_COLOR_KIND_EDIT3 = 3,
	UI_COLOR_KIND_SWATCH = 4,
};

enum {
	UI_COLOR_DRAG_NONE = 0,
	UI_COLOR_DRAG_SV = 1,
	UI_COLOR_DRAG_HUE = 2,
	UI_COLOR_DRAG_ALPHA = 3,
};

#define UI_COLOR_SV_SIZE 180.0f
#define UI_COLOR_BAR_W 18.0f
#define UI_COLOR_PREVIEW_W 80.0f
#define UI_COLOR_PREVIEW_H 32.0f
#define UI_COLOR_EDIT_SWATCH 22.0f

typedef struct ui_color_data_t {
	u32 kind;
	u32 flags;
	f32 rgba[4];
	f32 hsv[3];
	f32* bound_f32;
	i32 bound_count;
	sk_ui_color_t* bound_color;
	i32 edge_changed;
	i32 edge_committed;
	i32 editing;
	i32 dirty_since_open;
	i32 was_open;
	i32 drag_part;
	sk_ui_node_t host;
	sk_ui_node_t owner;
	sk_ui_node_t popup;
	sk_ui_node_t picker;
	sk_ui_node_t sv;
	sk_ui_node_t hue;
	sk_ui_node_t alpha;
	sk_ui_node_t preview;
	sk_ui_node_t rgb[4];
	sk_ui_node_t hsv_fields[3];
	sk_ui_node_t tip;
} ui_color_data_t;

static const sk_ui_api_t* color_api(void) {
	return ui_get_api_table();
}

static i32 color_bits_eq(f32 a, f32 b) {
	u32 ua;
	u32 ub;
	memcpy(&ua, &a, sizeof(ua));
	memcpy(&ub, &b, sizeof(ub));
	return ua == ub ? 1 : 0;
}

static f32 color_clampf(f32 v) {
	if (v < 0.0f) {
		return 0.0f;
	}
	if (v > 1.0f) {
		return 1.0f;
	}
	return v;
}

static f32 color_wrap_hue(f32 h) {
	while (h < 0.0f) {
		h += 1.0f;
	}
	while (h >= 1.0f) {
		h -= 1.0f;
	}
	return h;
}

void ui_color_rgb_to_hsv_impl(f32 r, f32 g, f32 b, f32* h, f32* s, f32* v) {
	f32 mx;
	f32 mn;
	f32 d;
	r = color_clampf(r);
	g = color_clampf(g);
	b = color_clampf(b);
	mx = r > g ? r : g;
	if (b > mx) {
		mx = b;
	}
	mn = r < g ? r : g;
	if (b < mn) {
		mn = b;
	}
	d = mx - mn;
	if (v != NULL) {
		*v = mx;
	}
	if (s != NULL) {
		*s = (mx > 0.000001f) ? (d / mx) : 0.0f;
	}
	if (h == NULL) {
		return;
	}
	if (d <= 0.000001f) {
		*h = 0.0f;
		return;
	}
	if (mx <= r) {
		*h = (g - b) / d + (g < b ? 6.0f : 0.0f);
	} else if (mx <= g) {
		*h = (b - r) / d + 2.0f;
	} else {
		*h = (r - g) / d + 4.0f;
	}
	*h *= (1.0f / 6.0f);
}

void ui_color_hsv_to_rgb_impl(f32 h, f32 s, f32 v, f32* r, f32* g, f32* b) {
	f32 i;
	f32 f;
	f32 p;
	f32 q;
	f32 t;
	i32 sector;
	h = color_wrap_hue(h);
	s = color_clampf(s);
	v = color_clampf(v);
	if (s <= 0.000001f) {
		if (r != NULL) {
			*r = v;
		}
		if (g != NULL) {
			*g = v;
		}
		if (b != NULL) {
			*b = v;
		}
		return;
	}
	i = h * 6.0f;
	sector = (i32)i;
	if (sector >= 6) {
		sector = 5;
	}
	if (sector < 0) {
		sector = 0;
	}
	f = i - (f32)sector;
	p = v * (1.0f - s);
	q = v * (1.0f - f * s);
	t = v * (1.0f - (1.0f - f) * s);
	switch (sector) {
	case 0:
		if (r != NULL) {
			*r = v;
		}
		if (g != NULL) {
			*g = t;
		}
		if (b != NULL) {
			*b = p;
		}
		break;
	case 1:
		if (r != NULL) {
			*r = q;
		}
		if (g != NULL) {
			*g = v;
		}
		if (b != NULL) {
			*b = p;
		}
		break;
	case 2:
		if (r != NULL) {
			*r = p;
		}
		if (g != NULL) {
			*g = v;
		}
		if (b != NULL) {
			*b = t;
		}
		break;
	case 3:
		if (r != NULL) {
			*r = p;
		}
		if (g != NULL) {
			*g = q;
		}
		if (b != NULL) {
			*b = v;
		}
		break;
	case 4:
		if (r != NULL) {
			*r = t;
		}
		if (g != NULL) {
			*g = p;
		}
		if (b != NULL) {
			*b = v;
		}
		break;
	default:
		if (r != NULL) {
			*r = v;
		}
		if (g != NULL) {
			*g = p;
		}
		if (b != NULL) {
			*b = q;
		}
		break;
	}
}

static ui_color_data_t* color_data(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL || slot->user_data == NULL) {
		return NULL;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COLOR_DATA_TYPE_ID)) {
		return NULL;
	}
	return (ui_color_data_t*)slot->user_data;
}

static const ui_color_data_t* color_data_const(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return color_data(SK_CONST_CAST(sk_ui_context_t*, ctx), node);
}

void ui_color_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot) {
	if (slot == NULL || slot->user_data == NULL) {
		return;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COLOR_DATA_TYPE_ID)) {
		return;
	}
	ctx->allocator->free(ctx->allocator->instance, slot->user_data);
	slot->user_data = NULL;
	slot->user_data_type = SK_TYPE_ID_ZERO;
}

static ui_color_data_t* color_ensure(sk_ui_context_t* ctx, sk_ui_node_t node, u32 kind) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	ui_color_data_t* d;
	if (slot == NULL) {
		return NULL;
	}
	if (slot->user_data != NULL && SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COLOR_DATA_TYPE_ID)) {
		d = (ui_color_data_t*)slot->user_data;
		d->kind = kind;
		d->host = node;
		return d;
	}
	if (slot->user_data != NULL) {
		return NULL;
	}
	d = (ui_color_data_t*)ctx->allocator->alloc(ctx->allocator->instance, sizeof(ui_color_data_t));
	if (d == NULL) {
		return NULL;
	}
	memset(d, 0, sizeof(*d));
	d->kind = kind;
	d->host = node;
	d->owner = SK_UI_NODE_INVALID;
	d->popup = SK_UI_NODE_INVALID;
	d->picker = SK_UI_NODE_INVALID;
	d->sv = SK_UI_NODE_INVALID;
	d->hue = SK_UI_NODE_INVALID;
	d->alpha = SK_UI_NODE_INVALID;
	d->preview = SK_UI_NODE_INVALID;
	d->tip = SK_UI_NODE_INVALID;
	d->rgba[0] = 1.0f;
	d->rgba[1] = 1.0f;
	d->rgba[2] = 1.0f;
	d->rgba[3] = 1.0f;
	d->hsv[0] = 0.0f;
	d->hsv[1] = 0.0f;
	d->hsv[2] = 1.0f;
	{
		i32 i;
		for (i = 0; i < 4; ++i) {
			d->rgb[i] = SK_UI_NODE_INVALID;
		}
		for (i = 0; i < 3; ++i) {
			d->hsv_fields[i] = SK_UI_NODE_INVALID;
		}
	}
	slot->user_data = d;
	slot->user_data_type = SK_UI_COLOR_DATA_TYPE_ID;
	return d;
}

static u32 color_effective_flags(u32 flags, u32 kind) {
	if (kind == (u32)UI_COLOR_KIND_SWATCH) {
		return flags | SK_UI_COLOR_FLAG_SWATCH;
	}
	if (kind == (u32)UI_COLOR_KIND_EDIT3) {
		return flags | SK_UI_COLOR_FLAG_NO_ALPHA;
	}
	if (kind == (u32)UI_COLOR_KIND_PICKER && flags == 0u) {
		return SK_UI_COLOR_FLAG_PICKER_DEFAULT;
	}
	return flags;
}

static i32 color_has_alpha(const ui_color_data_t* d) {
	if (d == NULL) {
		return 1;
	}
	if ((d->flags & SK_UI_COLOR_FLAG_NO_ALPHA) != 0u) {
		return 0;
	}
	if (d->kind == (u32)UI_COLOR_KIND_EDIT3) {
		return 0;
	}
	return 1;
}

static void color_write_bind(ui_color_data_t* d) {
	i32 n;
	i32 i;
	if (d == NULL) {
		return;
	}
	if (d->kind == (u32)UI_COLOR_KIND_SWATCH) {
		return;
	}
	n = d->bound_count;
	if (n < 3) {
		n = color_has_alpha(d) != 0 ? 4 : 3;
	}
	if (d->bound_f32 != NULL) {
		for (i = 0; i < n && i < 4; ++i) {
			d->bound_f32[i] = d->rgba[i];
		}
	}
	if (d->bound_color != NULL) {
		d->bound_color->r = d->rgba[0];
		d->bound_color->g = d->rgba[1];
		d->bound_color->b = d->rgba[2];
		d->bound_color->a = d->rgba[3];
	}
}

static void color_write_props(sk_ui_context_t* ctx, sk_ui_node_t node, const ui_color_data_t* d) {
	const sk_ui_api_t* ui = color_api();
	if (!sk_ui_node_is_valid(node) || d == NULL) {
		return;
	}
	(void)ui->node_set_prop_f32(ctx, node, "r", d->rgba[0]);
	(void)ui->node_set_prop_f32(ctx, node, "g", d->rgba[1]);
	(void)ui->node_set_prop_f32(ctx, node, "b", d->rgba[2]);
	(void)ui->node_set_prop_f32(ctx, node, "a", d->rgba[3]);
	(void)ui->node_set_prop_f32(ctx, node, "h", d->hsv[0]);
	(void)ui->node_set_prop_f32(ctx, node, "s", d->hsv[1]);
	(void)ui->node_set_prop_f32(ctx, node, "v", d->hsv[2]);
	(void)ui->node_set_prop_i32(ctx, node, "flags", (i32)d->flags);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
}

static void color_refresh_tooltip(sk_ui_context_t* ctx, ui_color_data_t* d) {
	const sk_ui_api_t* ui = color_api();
	char buf[80];
	sk_ui_node_t text;
	if (d == NULL || !sk_ui_node_is_valid(d->tip)) {
		return;
	}
	(void)snprintf(buf, sizeof(buf), "R:%.3f G:%.3f B:%.3f A:%.3f", (double)d->rgba[0], (double)d->rgba[1], (double)d->rgba[2], (double)d->rgba[3]);
	text = ui->node_child_at(ctx, d->tip, 0u);
	if (sk_ui_node_is_valid(text)) {
		(void)ui->node_set_prop_str(ctx, text, "text", buf);
	} else {
		(void)ui->widget_text(ctx, d->tip, buf, NULL);
	}
}

static void color_push_visual(sk_ui_context_t* ctx, ui_color_data_t* d) {
	if (d == NULL) {
		return;
	}
	color_write_props(ctx, d->host, d);
	color_write_props(ctx, d->picker, d);
	color_write_props(ctx, d->sv, d);
	color_write_props(ctx, d->hue, d);
	color_write_props(ctx, d->alpha, d);
	color_write_props(ctx, d->preview, d);
	color_refresh_tooltip(ctx, d);
}

static void color_apply_rgba(sk_ui_context_t* ctx, ui_color_data_t* d, f32 r, f32 g, f32 b, f32 a, i32 user_edit) {
	f32 nr = color_clampf(r);
	f32 ng = color_clampf(g);
	f32 nb = color_clampf(b);
	f32 na = color_clampf(a);
	i32 changed = 0;
	if (d == NULL) {
		return;
	}
	if (color_has_alpha(d) == 0) {
		na = 1.0f;
	}
	if (color_bits_eq(nr, d->rgba[0]) == 0 || color_bits_eq(ng, d->rgba[1]) == 0 || color_bits_eq(nb, d->rgba[2]) == 0 || color_bits_eq(na, d->rgba[3]) == 0) {
		changed = 1;
	}
	d->rgba[0] = nr;
	d->rgba[1] = ng;
	d->rgba[2] = nb;
	d->rgba[3] = na;
	ui_color_rgb_to_hsv_impl(nr, ng, nb, &d->hsv[0], &d->hsv[1], &d->hsv[2]);
	color_write_bind(d);
	color_push_visual(ctx, d);
	if (d->kind == (u32)UI_COLOR_KIND_EDIT3) {
		const sk_ui_api_t* ui = color_api();
		u32 n = ui->node_child_count(ctx, d->host);
		u32 i;
		for (i = 0u; i < n; ++i) {
			sk_ui_node_t ch = ui->node_child_at(ctx, d->host, i);
			ui_color_data_t* cd = color_data(ctx, ch);
			if (cd != NULL && cd->kind == (u32)UI_COLOR_KIND_BUTTON) {
				cd->rgba[0] = d->rgba[0];
				cd->rgba[1] = d->rgba[1];
				cd->rgba[2] = d->rgba[2];
				cd->rgba[3] = 1.0f;
				ui_color_rgb_to_hsv_impl(cd->rgba[0], cd->rgba[1], cd->rgba[2], &cd->hsv[0], &cd->hsv[1], &cd->hsv[2]);
				color_push_visual(ctx, cd);
			}
		}
		for (i = 0u; i < 3u; ++i) {
			if (sk_ui_node_is_valid(d->rgb[i])) {
				(void)ui->slider_set_value(ctx, d->rgb[i], d->rgba[i]);
			}
		}
	}
	if (user_edit != 0 && changed != 0 && d->kind != (u32)UI_COLOR_KIND_SWATCH) {
		d->edge_changed = 1;
		d->edge_committed = 1;
		d->dirty_since_open = 1;
		if (sk_ui_node_is_valid(d->owner)) {
			ui_color_data_t* od = color_data(ctx, d->owner);
			if (od != NULL && od != d) {
				od->edge_changed = 1;
				od->edge_committed = 1;
				od->dirty_since_open = 1;
			}
		}
	}
}

static void color_apply_hsv(sk_ui_context_t* ctx, ui_color_data_t* d, f32 h, f32 s, f32 v, f32 a, i32 user_edit) {
	f32 r;
	f32 g;
	f32 b;
	if (d == NULL) {
		return;
	}
	h = color_wrap_hue(h);
	s = color_clampf(s);
	v = color_clampf(v);
	d->hsv[0] = h;
	d->hsv[1] = s;
	d->hsv[2] = v;
	ui_color_hsv_to_rgb_impl(h, s, v, &r, &g, &b);
	color_apply_rgba(ctx, d, r, g, b, a, user_edit);
	d->hsv[0] = h;
	d->hsv[1] = s;
	d->hsv[2] = v;
	color_push_visual(ctx, d);
}

static void color_apply_next_item_width(sk_ui_context_t* ctx, sk_ui_node_t n) {
	const sk_ui_api_t* ui = color_api();
	sk_ui_style_props_t box;
	if (ctx->has_next_item_width == 0u) {
		return;
	}
	{
		f32 w = ctx->next_item_width;
		ctx->has_next_item_width = 0u;
		memset(&box, 0, sizeof(box));
		if (w < 0.0f) {
			box.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW;
			box.layout.width = sk_ui_percent(100.0f);
			box.layout.flex_grow = 1.0f;
		} else {
			box.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH;
			box.layout.width = sk_ui_pt(w);
			box.layout.min_width = sk_ui_pt(w);
		}
		(void)ui->node_merge_inline_style(ctx, n, &box);
	}
}

static const_chr_t color_assign_id(sk_ui_context_t* ctx, sk_ui_node_t n, const_chr_t id, const_chr_t prefix) {
	const sk_ui_api_t* ui = color_api();
	char auto_id[64];
	if (id != NULL && id[0] != '\0') {
		(void)ui->node_set_id(ctx, n, id);
		return ui->node_get_id(ctx, n);
	}
	ctx->widget_id_seq += 1u;
	(void)snprintf(auto_id, sizeof(auto_id), "%s-%u", prefix != NULL ? prefix : "ui-color", ctx->widget_id_seq);
	(void)ui->node_set_id(ctx, n, auto_id);
	return ui->node_get_id(ctx, n);
}

static void color_make_child_id(char* out, u32 cap, const_chr_t base, const_chr_t suffix) {
	if (out == NULL || cap == 0u) {
		return;
	}
	if (base != NULL && base[0] != '\0') {
		(void)snprintf(out, cap, "%s-%s", base, suffix);
	} else {
		(void)snprintf(out, cap, "ui-color-%s", suffix);
	}
}

static sk_ui_node_t color_make_part(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t class_name, const_chr_t widget, const_chr_t id, f32 w, f32 h) {
	const sk_ui_api_t* ui = color_api();
	sk_ui_node_t n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	sk_ui_style_props_t p;
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_add_class(ctx, n, class_name);
	(void)ui->node_set_prop_str(ctx, n, "widget", widget);
	if (id != NULL && id[0] != '\0') {
		(void)ui->node_set_id(ctx, n, id);
	}
	(void)ui->node_set_focusable(ctx, n, 1);
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	(void)ui->node_merge_inline_style(ctx, n, &p);
	return n;
}

static void color_map_sv(sk_ui_context_t* ctx, ui_color_data_t* d, sk_ui_node_t node, f32 x, f32 y) {
	sk_ui_rect_t r;
	f32 s;
	f32 v;
	if (d == NULL || color_api()->node_get_abs_rect(ctx, node, &r, NULL) != 0 || r.width <= 0.0f || r.height <= 0.0f) {
		return;
	}
	s = (x - r.x) / r.width;
	v = 1.0f - (y - r.y) / r.height;
	color_apply_hsv(ctx, d, d->hsv[0], s, v, d->rgba[3], 1);
}

static void color_map_hue(sk_ui_context_t* ctx, ui_color_data_t* d, sk_ui_node_t node, f32 y) {
	sk_ui_rect_t r;
	f32 h;
	if (d == NULL || color_api()->node_get_abs_rect(ctx, node, &r, NULL) != 0 || r.height <= 0.0f) {
		return;
	}
	h = (y - r.y) / r.height;
	color_apply_hsv(ctx, d, h, d->hsv[1], d->hsv[2], d->rgba[3], 1);
}

static void color_map_alpha(sk_ui_context_t* ctx, ui_color_data_t* d, sk_ui_node_t node, f32 y) {
	sk_ui_rect_t r;
	f32 a;
	if (d == NULL || color_api()->node_get_abs_rect(ctx, node, &r, NULL) != 0 || r.height <= 0.0f) {
		return;
	}
	a = 1.0f - (y - r.y) / r.height;
	color_apply_rgba(ctx, d, d->rgba[0], d->rgba[1], d->rgba[2], a, 1);
}

static void color_part_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = color_api();
	ui_color_data_t* d = (ui_color_data_t*)user;
	i32 part = UI_COLOR_DRAG_NONE;
	if (d == NULL || event == NULL) {
		return;
	}
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	if (sk_ui_node_eq(node, d->sv)) {
		part = UI_COLOR_DRAG_SV;
	} else if (sk_ui_node_eq(node, d->hue)) {
		part = UI_COLOR_DRAG_HUE;
	} else if (sk_ui_node_eq(node, d->alpha)) {
		part = UI_COLOR_DRAG_ALPHA;
	}
	if (part == UI_COLOR_DRAG_NONE) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN) {
		d->drag_part = part;
		d->editing = 1;
		(void)ui->pointer_capture_set(ctx, node);
		if (part == UI_COLOR_DRAG_SV) {
			color_map_sv(ctx, d, node, event->x, event->y);
		} else if (part == UI_COLOR_DRAG_HUE) {
			color_map_hue(ctx, d, node, event->y);
		} else {
			color_map_alpha(ctx, d, node, event->y);
		}
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_MOVE && d->drag_part == part) {
		if (part == UI_COLOR_DRAG_SV) {
			color_map_sv(ctx, d, node, event->x, event->y);
		} else if (part == UI_COLOR_DRAG_HUE) {
			color_map_hue(ctx, d, node, event->y);
		} else {
			color_map_alpha(ctx, d, node, event->y);
		}
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP || event->type == SK_UI_EVENT_CLICK) {
		if (d->drag_part == part || d->dirty_since_open != 0) {
			if (d->dirty_since_open != 0) {
				d->edge_committed = 1;
				if (sk_ui_node_is_valid(d->owner)) {
					ui_color_data_t* od = color_data(ctx, d->owner);
					if (od != NULL && od != d) {
						od->edge_committed = 1;
					}
				}
			}
			d->drag_part = UI_COLOR_DRAG_NONE;
			d->editing = 0;
		}
		if (sk_ui_node_eq(ui->pointer_capture_get(ctx), node)) {
			(void)ui->pointer_capture_set(ctx, SK_UI_NODE_INVALID);
		}
		event->consumed = 1;
	}
}

static void color_button_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = color_api();
	ui_color_data_t* d = (ui_color_data_t*)user;
	if (d == NULL || event == NULL) {
		return;
	}
	if (d->kind == (u32)UI_COLOR_KIND_SWATCH) {
		return;
	}
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	if ((d->flags & SK_UI_COLOR_FLAG_NO_PICKER) != 0u) {
		return;
	}
	if (event->type == SK_UI_EVENT_CLICK || (event->type == SK_UI_EVENT_POINTER_UP && event->button == SK_UI_POINTER_BUTTON_LEFT)) {
		if (sk_ui_node_is_valid(d->popup)) {
			i32 open = ui->popup_get_open(ctx, d->popup);
			if (open == 0) {
				(void)ui->popup_open(ctx, d->popup);
				d->was_open = 1;
				d->dirty_since_open = 0;
			}
		}
		event->consumed = 1;
	}
}

static void color_drag_on_change(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	ui_color_data_t* d = (ui_color_data_t*)user;
	i32 i;
	f32 rgb[4];
	if (d == NULL) {
		return;
	}
	rgb[0] = d->rgba[0];
	rgb[1] = d->rgba[1];
	rgb[2] = d->rgba[2];
	rgb[3] = d->rgba[3];
	for (i = 0; i < 4; ++i) {
		if (sk_ui_node_eq(d->rgb[i], node)) {
			rgb[i] = value;
			color_apply_rgba(ctx, d, rgb[0], rgb[1], rgb[2], rgb[3], 1);
			return;
		}
	}
}

static void color_attach_part_events(sk_ui_context_t* ctx, sk_ui_node_t node, ui_color_data_t* d) {
	const sk_ui_api_t* ui = color_api();
	sk_ui_node_callbacks_t cbs;
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = color_part_on_event;
	cbs.user = d;
	(void)ui->node_set_callbacks(ctx, node, &cbs);
}

static void color_build_picker_body(sk_ui_context_t* ctx, ui_color_data_t* d, sk_ui_node_t host, const_chr_t base_id) {
	const sk_ui_api_t* ui = color_api();
	sk_ui_node_t row;
	sk_ui_node_t rgb_row;
	char idbuf[80];
	sk_ui_layout_style_t ls;
	u32 flags = d->flags;
	i32 show_alpha = ((flags & SK_UI_COLOR_FLAG_NO_ALPHA) == 0u && (flags & SK_UI_COLOR_FLAG_ALPHA_BAR) != 0u) ? 1 : 0;
	i32 show_rgb = ((flags & SK_UI_COLOR_FLAG_DISPLAY_MASK) == 0u || (flags & SK_UI_COLOR_FLAG_DISPLAY_RGB) != 0u) ? 1 : 0;

	ui_layout_style_init_default(&ls);
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	ls.row_gap = 8.0f;
	(void)ui->node_set_layout_style(ctx, host, &ls);

	row = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, host);
	if (sk_ui_node_is_valid(row)) {
		sk_ui_style_props_t p;
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_BACKGROUND_COLOR;
		p.layout.flex_direction = SK_UI_FLEX_ROW;
		p.layout.align_items = SK_UI_ALIGN_FLEX_START;
		p.layout.column_gap = 6.0f;
		p.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
		(void)ui->node_merge_inline_style(ctx, row, &p);
		ui_layout_style_init_default(&ls);
		ls.flex_direction = SK_UI_FLEX_ROW;
		ls.column_gap = 6.0f;
		ls.align_items = SK_UI_ALIGN_FLEX_START;
		(void)ui->node_set_layout_style(ctx, row, &ls);
	}

	color_make_child_id(idbuf, (u32)sizeof(idbuf), base_id, "sv");
	d->sv = color_make_part(ctx, row, SK_UI_CLASS_COLOR_SV, "color_sv", idbuf, UI_COLOR_SV_SIZE, UI_COLOR_SV_SIZE);
	color_attach_part_events(ctx, d->sv, d);

	color_make_child_id(idbuf, (u32)sizeof(idbuf), base_id, "hue");
	d->hue = color_make_part(ctx, row, SK_UI_CLASS_COLOR_HUE, "color_hue", idbuf, UI_COLOR_BAR_W, UI_COLOR_SV_SIZE);
	color_attach_part_events(ctx, d->hue, d);

	if (show_alpha != 0) {
		color_make_child_id(idbuf, (u32)sizeof(idbuf), base_id, "alpha");
		d->alpha = color_make_part(ctx, row, SK_UI_CLASS_COLOR_ALPHA, "color_alpha", idbuf, UI_COLOR_BAR_W, UI_COLOR_SV_SIZE);
		color_attach_part_events(ctx, d->alpha, d);
	}

	color_make_child_id(idbuf, (u32)sizeof(idbuf), base_id, "preview");
	d->preview = color_make_part(ctx, host, SK_UI_CLASS_COLOR_PREVIEW, "color_preview", idbuf, UI_COLOR_PREVIEW_W, UI_COLOR_PREVIEW_H);
	(void)ui->node_set_prop_i32(ctx, d->preview, "flags", (i32)flags);
	(void)ui->node_set_focusable(ctx, d->preview, 0);

	if (show_rgb != 0) {
		i32 ncomp = show_alpha != 0 ? 4 : 3;
		i32 i;
		static const char* const names[4] = {"r", "g", "b", "a"};
		rgb_row = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, host);
		if (sk_ui_node_is_valid(rgb_row)) {
			sk_ui_style_props_t rp;
			memset(&rp, 0, sizeof(rp));
			rp.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP;
			rp.layout.flex_direction = SK_UI_FLEX_ROW;
			rp.layout.align_items = SK_UI_ALIGN_CENTER;
			rp.layout.column_gap = 4.0f;
			(void)ui->node_merge_inline_style(ctx, rgb_row, &rp);
			ui_layout_style_init_default(&ls);
			ls.flex_direction = SK_UI_FLEX_ROW;
			ls.column_gap = 4.0f;
			ls.align_items = SK_UI_ALIGN_CENTER;
			(void)ui->node_set_layout_style(ctx, rgb_row, &ls);
		}
		for (i = 0; i < ncomp; ++i) {
			sk_ui_style_props_t p;
			color_make_child_id(idbuf, (u32)sizeof(idbuf), base_id, names[i]);
			d->rgb[i] = ui->widget_input_float(ctx, rgb_row, &d->rgba[i], idbuf);
			memset(&p, 0, sizeof(p));
			p.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH | SK_UI_SP_FLEX_GROW;
			p.layout.width = sk_ui_pt(56.0f);
			p.layout.min_width = sk_ui_pt(40.0f);
			p.layout.flex_grow = 1.0f;
			(void)ui->node_merge_inline_style(ctx, d->rgb[i], &p);
			{
				f32 z = 0.0f;
				f32 one = 1.0f;
				(void)ui->input_scalar_set_range(ctx, d->rgb[i], &z, &one);
			}
		}
	}
	color_push_visual(ctx, d);
}

static sk_ui_node_t color_make_host(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t class_name, const_chr_t widget, const_chr_t id, const_chr_t prefix) {
	const sk_ui_api_t* ui = color_api();
	sk_ui_node_t n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_add_class(ctx, n, class_name);
	(void)ui->node_set_prop_str(ctx, n, "widget", widget);
	(void)color_assign_id(ctx, n, id, prefix);
	(void)ui->node_set_focusable(ctx, n, 1);
	if (ctx->disabled_active > 0u) {
		(void)ui->node_set_state(ctx, n, ui->node_get_state(ctx, n) | (u32)SK_UI_STATE_DISABLED);
	}
	color_apply_next_item_width(ctx, n);
	return n;
}

static void color_attach_tooltip(sk_ui_context_t* ctx, ui_color_data_t* d, const_chr_t base_id) {
	const sk_ui_api_t* ui = color_api();
	char idbuf[80];
	if (d == NULL || (d->flags & SK_UI_COLOR_FLAG_NO_TOOLTIP) != 0u) {
		return;
	}
	color_make_child_id(idbuf, (u32)sizeof(idbuf), base_id, "tip");
	d->tip = ui->widget_tooltip(ctx, d->host, idbuf);
	if (sk_ui_node_is_valid(d->tip)) {
		(void)ui->tooltip_set_anchor(ctx, d->tip, d->host);
		color_refresh_tooltip(ctx, d);
	}
}

static void color_seed_from(ui_color_data_t* d, const f32* col, i32 count) {
	f32 rgba[4];
	i32 i;
	rgba[0] = 1.0f;
	rgba[1] = 1.0f;
	rgba[2] = 1.0f;
	rgba[3] = 1.0f;
	if (col != NULL) {
		for (i = 0; i < count && i < 4; ++i) {
			rgba[i] = color_clampf(col[i]);
		}
	}
	if (d->kind == (u32)UI_COLOR_KIND_EDIT3 || (d->flags & SK_UI_COLOR_FLAG_NO_ALPHA) != 0u) {
		rgba[3] = 1.0f;
	}
	d->rgba[0] = rgba[0];
	d->rgba[1] = rgba[1];
	d->rgba[2] = rgba[2];
	d->rgba[3] = rgba[3];
	ui_color_rgb_to_hsv_impl(rgba[0], rgba[1], rgba[2], &d->hsv[0], &d->hsv[1], &d->hsv[2]);
}

sk_ui_node_t ui_widget_color_picker4_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, f32* col, u32 flags, const_chr_t id) {
	const sk_ui_api_t* ui = color_api();
	ui_color_data_t* d;
	sk_ui_node_t n;
	const_chr_t use_id;
	(void)label;
	n = color_make_host(ctx, parent, SK_UI_CLASS_COLOR_PICKER, "color_picker", id, "ui-color-picker");
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	d = color_ensure(ctx, n, UI_COLOR_KIND_PICKER);
	if (d == NULL) {
		(void)ui->node_destroy(ctx, n);
		return SK_UI_NODE_INVALID;
	}
	d->flags = color_effective_flags(flags, UI_COLOR_KIND_PICKER);
	d->picker = n;
	d->bound_f32 = col;
	d->bound_count = ((d->flags & SK_UI_COLOR_FLAG_NO_ALPHA) != 0u) ? 3 : 4;
	color_seed_from(d, col, d->bound_count);
	use_id = ui->node_get_id(ctx, n);
	color_build_picker_body(ctx, d, n, use_id);
	color_write_bind(d);
	return n;
}

sk_ui_node_t ui_widget_color_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const f32 col[4], u32 flags, f32 width, f32 height, const_chr_t id) {
	const sk_ui_api_t* ui = color_api();
	ui_color_data_t* d;
	sk_ui_node_t n;
	sk_ui_node_callbacks_t cbs;
	sk_ui_style_props_t p;
	const_chr_t use_id;
	char popup_id[80];
	char picker_id[80];
	f32 h = height > 0.0f ? height : SK_UI_COLOR_BUTTON_HEIGHT;

	n = color_make_host(ctx, parent, SK_UI_CLASS_COLOR_BUTTON, "color_button", id, "ui-color-button");
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT;
	p.layout.height = sk_ui_pt(h);
	p.layout.min_height = sk_ui_pt(h);
	if (width > 0.0f) {
		p.mask |= SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH;
		p.layout.width = sk_ui_pt(width);
		p.layout.min_width = sk_ui_pt(width);
	} else {
		p.mask |= SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_WIDTH;
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.flex_grow = 1.0f;
		p.layout.min_width = sk_ui_pt(40.0f);
	}
	(void)ui->node_merge_inline_style(ctx, n, &p);

	d = color_ensure(ctx, n, UI_COLOR_KIND_BUTTON);
	if (d == NULL) {
		(void)ui->node_destroy(ctx, n);
		return SK_UI_NODE_INVALID;
	}
	d->flags = color_effective_flags(flags, UI_COLOR_KIND_BUTTON);
	color_seed_from(d, col, 4);
	use_id = ui->node_get_id(ctx, n);

	if ((d->flags & SK_UI_COLOR_FLAG_NO_PICKER) == 0u) {
		color_make_child_id(popup_id, (u32)sizeof(popup_id), use_id, "popup");
		d->popup = ui->widget_popup_menu(ctx, n, popup_id);
		if (sk_ui_node_is_valid(d->popup)) {
			sk_ui_layout_style_t ls;
			(void)ui->node_get_layout_style(ctx, d->popup, &ls);
			ls.width = sk_ui_pt(268.0f);
			ls.min_width = sk_ui_pt(268.0f);
			ls.top = sk_ui_pt(h);
			(void)ui->node_set_layout_style(ctx, d->popup, &ls);
			color_make_child_id(picker_id, (u32)sizeof(picker_id), use_id, "picker");
			d->picker = ui_widget_color_picker4_impl(ctx, d->popup, NULL, d->rgba, d->flags | SK_UI_COLOR_FLAG_PICKER_DEFAULT, picker_id);
			if (sk_ui_node_is_valid(d->picker)) {
				ui_color_data_t* pd = color_data(ctx, d->picker);
				if (pd != NULL) {
					d->sv = pd->sv;
					d->hue = pd->hue;
					d->alpha = pd->alpha;
					d->preview = pd->preview;
					{
						i32 i;
						for (i = 0; i < 4; ++i) {
							d->rgb[i] = pd->rgb[i];
						}
					}
					pd->owner = n;
					/* Child picker writes d->rgba; host bind is the source of truth. */
					pd->bound_f32 = d->rgba;
					pd->bound_count = 4;
				}
			}
		}
	}
	color_attach_tooltip(ctx, d, use_id);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = color_button_on_event;
	cbs.user = d;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	color_push_visual(ctx, d);
	return n;
}

sk_ui_node_t ui_widget_color_swatch_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const f32 col[4], f32 width, f32 height, const_chr_t id) {
	const sk_ui_api_t* ui = color_api();
	ui_color_data_t* d;
	sk_ui_node_t n;
	sk_ui_style_props_t p;
	f32 w = width > 0.0f ? width : SK_UI_COLOR_SWATCH_SIZE;
	f32 h = height > 0.0f ? height : SK_UI_COLOR_SWATCH_SIZE;

	n = color_make_host(ctx, parent, SK_UI_CLASS_COLOR_SWATCH, "color_swatch", id, "ui-color-swatch");
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	(void)ui->node_merge_inline_style(ctx, n, &p);
	(void)ui->node_set_focusable(ctx, n, 0);
	(void)ui->node_set_pointer_events(ctx, n, SK_UI_POINTER_EVENTS_NONE);

	d = color_ensure(ctx, n, UI_COLOR_KIND_SWATCH);
	if (d == NULL) {
		(void)ui->node_destroy(ctx, n);
		return SK_UI_NODE_INVALID;
	}
	d->flags = SK_UI_COLOR_FLAG_SWATCH;
	color_seed_from(d, col, 4);
	color_push_visual(ctx, d);
	return n;
}

sk_ui_node_t ui_widget_color_edit3_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, f32* col, u32 flags, const_chr_t id) {
	const sk_ui_api_t* ui = color_api();
	ui_color_data_t* d;
	sk_ui_node_t n;
	sk_ui_layout_style_t ls;
	const_chr_t use_id;
	char idbuf[80];
	i32 i;
	static const char* const names[3] = {"r", "g", "b"};
	(void)label;

	n = color_make_host(ctx, parent, SK_UI_CLASS_COLOR_EDIT, "color_edit3", id, "ui-color-edit3");
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	ui_layout_style_init_default(&ls);
	ls.flex_direction = SK_UI_FLEX_ROW;
	ls.column_gap = 4.0f;
	ls.align_items = SK_UI_ALIGN_CENTER;
	(void)ui->node_set_layout_style(ctx, n, &ls);

	d = color_ensure(ctx, n, UI_COLOR_KIND_EDIT3);
	if (d == NULL) {
		(void)ui->node_destroy(ctx, n);
		return SK_UI_NODE_INVALID;
	}
	d->flags = color_effective_flags(flags, UI_COLOR_KIND_EDIT3);
	d->bound_f32 = col;
	d->bound_count = 3;
	color_seed_from(d, col, 3);
	use_id = ui->node_get_id(ctx, n);

	color_make_child_id(idbuf, (u32)sizeof(idbuf), use_id, "btn");
	{
		sk_ui_node_t btn = ui_widget_color_button_impl(ctx, n, d->rgba, d->flags | SK_UI_COLOR_FLAG_NO_ALPHA, UI_COLOR_EDIT_SWATCH, UI_COLOR_EDIT_SWATCH, idbuf);
		if (sk_ui_node_is_valid(btn)) {
			ui_color_data_t* bd = color_data(ctx, btn);
			if (bd != NULL) {
				bd->bound_f32 = d->rgba;
				bd->bound_count = 3;
			}
			d->popup = ui_color_popup_impl(ctx, btn);
			d->picker = (bd != NULL) ? bd->picker : SK_UI_NODE_INVALID;
			d->sv = (bd != NULL) ? bd->sv : SK_UI_NODE_INVALID;
			d->hue = (bd != NULL) ? bd->hue : SK_UI_NODE_INVALID;
			d->alpha = (bd != NULL) ? bd->alpha : SK_UI_NODE_INVALID;
			d->preview = (bd != NULL) ? bd->preview : SK_UI_NODE_INVALID;
		}
	}

	for (i = 0; i < 3; ++i) {
		sk_ui_style_props_t p;
		color_make_child_id(idbuf, (u32)sizeof(idbuf), use_id, names[i]);
		d->rgb[i] = ui->widget_drag_float(ctx, n, 0.01f, 0.0f, 1.0f, d->rgba[i], "%.3f", idbuf);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_WIDTH;
		p.layout.flex_grow = 1.0f;
		p.layout.min_width = sk_ui_pt(36.0f);
		(void)ui->node_merge_inline_style(ctx, d->rgb[i], &p);
		(void)ui->slider_set_on_change(ctx, d->rgb[i], color_drag_on_change, d);
	}
	color_push_visual(ctx, d);
	return n;
}

i32 ui_color_bind_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32* col, i32 components) {
	ui_color_data_t* d = color_data(ctx, node);
	if (d == NULL) {
		return -1;
	}
	if (d->kind == (u32)UI_COLOR_KIND_SWATCH) {
		d->bound_f32 = col;
		d->bound_count = (components >= 4) ? 4 : 3;
		return 0;
	}
	d->bound_f32 = col;
	d->bound_count = (components >= 4) ? 4 : 3;
	d->bound_color = NULL;
	if (sk_ui_node_is_valid(d->picker)) {
		ui_color_data_t* pd = color_data(ctx, d->picker);
		if (pd != NULL && pd != d) {
			pd->bound_f32 = col;
			pd->bound_count = d->bound_count;
		}
	}
	if (col != NULL) {
		color_apply_rgba(ctx, d, col[0], col[1], col[2], d->bound_count >= 4 ? col[3] : 1.0f, 0);
	}
	return 0;
}

i32 ui_color_bind_color_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_color_t* col) {
	ui_color_data_t* d = color_data(ctx, node);
	if (d == NULL) {
		return -1;
	}
	if (d->kind == (u32)UI_COLOR_KIND_SWATCH) {
		d->bound_color = col;
		return 0;
	}
	d->bound_color = col;
	d->bound_f32 = NULL;
	if (col != NULL) {
		color_apply_rgba(ctx, d, col->r, col->g, col->b, col->a, 0);
	}
	return 0;
}

i32 ui_color_set_values_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const f32* col, i32 count) {
	ui_color_data_t* d = color_data(ctx, node);
	if (d == NULL || col == NULL || count < 3) {
		return -1;
	}
	color_apply_rgba(ctx, d, col[0], col[1], col[2], count >= 4 ? col[3] : d->rgba[3], 0);
	return 0;
}

i32 ui_color_get_values_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, f32* out, i32 count) {
	const ui_color_data_t* d = color_data_const(ctx, node);
	i32 i;
	if (d == NULL || out == NULL || count <= 0) {
		return -1;
	}
	for (i = 0; i < count && i < 4; ++i) {
		out[i] = d->rgba[i];
	}
	return 0;
}

i32 ui_color_set_rgba_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 r, f32 g, f32 b, f32 a) {
	ui_color_data_t* d = color_data(ctx, node);
	if (d == NULL) {
		return -1;
	}
	color_apply_rgba(ctx, d, r, g, b, a, 0);
	return 0;
}

i32 ui_color_get_rgba_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, f32 out[4]) {
	return ui_color_get_values_impl(ctx, node, out, 4);
}

i32 ui_color_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	ui_color_data_t* d = color_data(ctx, node);
	if (d == NULL) {
		return -1;
	}
	d->flags = color_effective_flags(flags, d->kind);
	color_push_visual(ctx, d);
	return 0;
}

u32 ui_color_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_color_data_t* d = color_data_const(ctx, node);
	return d != NULL ? d->flags : 0u;
}

i32 ui_color_set_size_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height) {
	const sk_ui_api_t* ui = color_api();
	sk_ui_style_props_t p;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
	p.layout.width = sk_ui_pt(width);
	p.layout.height = sk_ui_pt(height);
	p.layout.min_width = sk_ui_pt(width);
	p.layout.min_height = sk_ui_pt(height);
	return ui->node_merge_inline_style(ctx, node, &p);
}

static i32 color_take_edge(ui_color_data_t* d, i32 committed) {
	i32 v;
	if (d == NULL) {
		return 0;
	}
	if (committed != 0) {
		v = d->edge_committed;
		d->edge_committed = 0;
	} else {
		v = d->edge_changed;
		d->edge_changed = 0;
	}
	return v;
}

i32 ui_color_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_color_data_t* d = color_data(ctx, node);
	i32 v = color_take_edge(d, 0);
	if (d != NULL && sk_ui_node_is_valid(d->picker)) {
		ui_color_data_t* pd = color_data(ctx, d->picker);
		if (pd != NULL && pd != d) {
			if (color_take_edge(pd, 0) != 0) {
				v = 1;
			}
		}
	}
	return v;
}

i32 ui_color_committed_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_color_data_t* d = color_data(ctx, node);
	i32 v = color_take_edge(d, 1);
	if (d != NULL && sk_ui_node_is_valid(d->picker)) {
		ui_color_data_t* pd = color_data(ctx, d->picker);
		if (pd != NULL && pd != d) {
			if (color_take_edge(pd, 1) != 0) {
				v = 1;
			}
		}
	}
	return v;
}

i32 ui_color_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_color_data_t* d = color_data_const(ctx, node);
	if (d == NULL) {
		return 0;
	}
	if (sk_ui_node_is_valid(d->popup)) {
		return color_api()->popup_get_open(ctx, d->popup);
	}
	return 0;
}

i32 ui_color_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 open) {
	const sk_ui_api_t* ui = color_api();
	ui_color_data_t* d = color_data(ctx, node);
	if (d == NULL || !sk_ui_node_is_valid(d->popup)) {
		return -1;
	}
	if (open != 0) {
		d->was_open = 1;
		d->dirty_since_open = 0;
		return ui->popup_open(ctx, d->popup);
	}
	return ui->popup_close_current(ctx);
}

sk_ui_node_t ui_color_popup_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_color_data_t* d = color_data_const(ctx, node);
	return d != NULL ? d->popup : SK_UI_NODE_INVALID;
}

sk_ui_node_t ui_color_sv_square_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_color_data_t* d = color_data_const(ctx, node);
	if (d != NULL && sk_ui_node_is_valid(d->sv)) {
		return d->sv;
	}
	if (d != NULL && sk_ui_node_is_valid(d->picker)) {
		const ui_color_data_t* pd = color_data_const(ctx, d->picker);
		if (pd != NULL) {
			return pd->sv;
		}
	}
	return SK_UI_NODE_INVALID;
}

sk_ui_node_t ui_color_hue_bar_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_color_data_t* d = color_data_const(ctx, node);
	if (d != NULL && sk_ui_node_is_valid(d->hue)) {
		return d->hue;
	}
	if (d != NULL && sk_ui_node_is_valid(d->picker)) {
		const ui_color_data_t* pd = color_data_const(ctx, d->picker);
		if (pd != NULL) {
			return pd->hue;
		}
	}
	return SK_UI_NODE_INVALID;
}

sk_ui_node_t ui_color_alpha_bar_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_color_data_t* d = color_data_const(ctx, node);
	if (d != NULL && sk_ui_node_is_valid(d->alpha)) {
		return d->alpha;
	}
	if (d != NULL && sk_ui_node_is_valid(d->picker)) {
		const ui_color_data_t* pd = color_data_const(ctx, d->picker);
		if (pd != NULL) {
			return pd->alpha;
		}
	}
	return SK_UI_NODE_INVALID;
}

sk_ui_node_t ui_color_preview_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_color_data_t* d = color_data_const(ctx, node);
	if (d != NULL && sk_ui_node_is_valid(d->preview)) {
		return d->preview;
	}
	if (d != NULL && sk_ui_node_is_valid(d->picker)) {
		const ui_color_data_t* pd = color_data_const(ctx, d->picker);
		if (pd != NULL) {
			return pd->preview;
		}
	}
	return SK_UI_NODE_INVALID;
}

i32 ui_color_component_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, i32 index, sk_ui_node_t* out_field) {
	const ui_color_data_t* d = color_data_const(ctx, node);
	sk_ui_node_t field = SK_UI_NODE_INVALID;
	if (out_field != NULL) {
		*out_field = SK_UI_NODE_INVALID;
	}
	if (d == NULL || index < 0 || index > 3) {
		return -1;
	}
	field = d->rgb[index];
	if (!sk_ui_node_is_valid(field) && sk_ui_node_is_valid(d->picker)) {
		const ui_color_data_t* pd = color_data_const(ctx, d->picker);
		if (pd != NULL) {
			field = pd->rgb[index];
		}
	}
	if (!sk_ui_node_is_valid(field)) {
		return -1;
	}
	if (out_field != NULL) {
		*out_field = field;
	}
	return 0;
}

static void color_pull_bound(sk_ui_context_t* ctx, ui_color_data_t* d) {
	const sk_ui_api_t* ui = color_api();
	if (d == NULL || d->editing != 0 || d->kind == (u32)UI_COLOR_KIND_SWATCH) {
		return;
	}
	if (sk_ui_node_is_valid(d->popup) && ui->popup_get_open(ctx, d->popup) != 0) {
		return;
	}
	if (sk_ui_node_is_valid(d->picker)) {
		ui_color_data_t* pd = color_data(ctx, d->picker);
		if (pd != NULL && pd->editing != 0) {
			return;
		}
	}
	if (d->bound_f32 != NULL) {
		f32 a = d->bound_count >= 4 ? d->bound_f32[3] : d->rgba[3];
		if (color_bits_eq(d->bound_f32[0], d->rgba[0]) == 0 || color_bits_eq(d->bound_f32[1], d->rgba[1]) == 0 || color_bits_eq(d->bound_f32[2], d->rgba[2]) == 0 ||
			(d->bound_count >= 4 && color_bits_eq(d->bound_f32[3], d->rgba[3]) == 0)) {
			color_apply_rgba(ctx, d, d->bound_f32[0], d->bound_f32[1], d->bound_f32[2], a, 0);
		}
	} else if (d->bound_color != NULL) {
		if (color_bits_eq(d->bound_color->r, d->rgba[0]) == 0 || color_bits_eq(d->bound_color->g, d->rgba[1]) == 0 || color_bits_eq(d->bound_color->b, d->rgba[2]) == 0 ||
			color_bits_eq(d->bound_color->a, d->rgba[3]) == 0) {
			color_apply_rgba(ctx, d, d->bound_color->r, d->bound_color->g, d->bound_color->b, d->bound_color->a, 0);
		}
	}
}

static void color_sync_inputs(sk_ui_context_t* ctx, ui_color_data_t* d) {
	const sk_ui_api_t* ui = color_api();
	i32 i;
	i32 committed = 0;
	if (d == NULL) {
		return;
	}
	for (i = 0; i < 4; ++i) {
		if (!sk_ui_node_is_valid(d->rgb[i])) {
			continue;
		}
		if (ui->text_input_committed(ctx, d->rgb[i]) != 0) {
			committed = 1;
		}
		if (ui->slider_changed(ctx, d->rgb[i]) != 0) {
			d->edge_changed = 1;
			d->dirty_since_open = 1;
			d->edge_committed = 1;
		}
	}
	if (sk_ui_node_is_valid(d->picker)) {
		ui_color_data_t* pd = color_data(ctx, d->picker);
		if (pd != NULL && pd != d) {
			i32 pi;
			for (pi = 0; pi < 4; ++pi) {
				if (!sk_ui_node_is_valid(pd->rgb[pi])) {
					continue;
				}
				if (ui->text_input_committed(ctx, pd->rgb[pi]) != 0) {
					committed = 1;
					color_apply_rgba(ctx, d, pd->rgba[0], pd->rgba[1], pd->rgba[2], pd->rgba[3], 1);
				}
			}
			if (pd->edge_changed != 0) {
				d->edge_changed = 1;
				pd->edge_changed = 0;
				color_apply_rgba(ctx, d, pd->rgba[0], pd->rgba[1], pd->rgba[2], pd->rgba[3], 0);
				d->edge_changed = 1;
				d->dirty_since_open = 1;
			}
			if (pd->edge_committed != 0) {
				d->edge_committed = 1;
				pd->edge_committed = 0;
			}
		}
	}
	if (committed != 0) {
		color_apply_rgba(ctx, d, d->rgba[0], d->rgba[1], d->rgba[2], d->rgba[3], 1);
		d->edge_committed = 1;
	}
}

void ui_color_sync_all(sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = color_api();
	u32 i;
	if (ctx == NULL) {
		return;
	}
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		ui_color_data_t* d;
		i32 open_now;
		if (slot->alive == 0u || slot->user_data == NULL) {
			continue;
		}
		if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COLOR_DATA_TYPE_ID)) {
			continue;
		}
		d = (ui_color_data_t*)slot->user_data;
		color_pull_bound(ctx, d);
		color_sync_inputs(ctx, d);
		open_now = 0;
		if (sk_ui_node_is_valid(d->popup)) {
			open_now = ui->popup_get_open(ctx, d->popup);
		}
		if (d->was_open != 0 && open_now == 0) {
			if (d->dirty_since_open != 0) {
				d->edge_committed = 1;
			}
			d->was_open = 0;
			d->dirty_since_open = 0;
		}
		if (open_now != 0) {
			d->was_open = 1;
		}
	}
}

void ui_color_place_popups(sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = color_api();
	u32 i;
	if (ctx == NULL) {
		return;
	}
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		ui_color_data_t* d;
		sk_ui_node_t node;
		sk_ui_rect_t cr;
		sk_ui_rect_t pr;
		ui_node_slot_t* ps;
		f32 desired_y;
		f32 delta;
		i32 need_flip;
		if (slot->alive == 0u || slot->user_data == NULL) {
			continue;
		}
		if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COLOR_DATA_TYPE_ID)) {
			continue;
		}
		d = (ui_color_data_t*)slot->user_data;
		if (d->kind != (u32)UI_COLOR_KIND_BUTTON || !sk_ui_node_is_valid(d->popup)) {
			continue;
		}
		node.index = i;
		node.generation = slot->generation;
		if (ui->popup_get_open(ctx, d->popup) == 0) {
			continue;
		}
		if (ui->node_get_abs_rect(ctx, node, &cr, NULL) != 0 || ui->node_get_abs_rect(ctx, d->popup, &pr, NULL) != 0) {
			continue;
		}
		if (pr.height < 1.0f) {
			continue;
		}
		need_flip = (cr.y + cr.height + pr.height > ctx->root_height + 0.5f && cr.y - pr.height >= -0.5f) ? 1 : 0;
		desired_y = need_flip != 0 ? (cr.y - pr.height) : (cr.y + cr.height);
		delta = desired_y - pr.y;
		if (delta > -0.5f && delta < 0.5f) {
			continue;
		}
		ps = ui_slot_mut(ctx, d->popup);
		if (ps == NULL) {
			continue;
		}
		ps->layout_border.y += delta;
		ps->layout_content.y += delta;
	}
}

#ifdef SK_TESTS

static const sk_ui_api_t* wcolor_api(void) {
	return ui_get_api_table();
}

static void wcolor_layout(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 w, f32 h) {
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, w, h));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
}

static void wcolor_pointer(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x, f32 y, i32 down) {
	sk_ui_input_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = x;
	ev.y = y;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = down;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

SK_TEST(ui_color_rgb_hsv_roundtrip) {
	const sk_ui_api_t* ui = wcolor_api();
	static const f32 samples[][3] = {
		{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f},	   {0.0f, 1.0f, 1.0f},
		{1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.25f, 0.50f, 0.75f}, {0.80f, 0.20f, 0.10f},
	};
	u32 i;
	for (i = 0u; i < (u32)(sizeof(samples) / sizeof(samples[0])); ++i) {
		f32 h, s, v;
		f32 r, g, b;
		ui->color_rgb_to_hsv(samples[i][0], samples[i][1], samples[i][2], &h, &s, &v);
		ui->color_hsv_to_rgb(h, s, v, &r, &g, &b);
		TEST_ASSERT_FLOAT_WITHIN(0.002f, samples[i][0], r);
		TEST_ASSERT_FLOAT_WITHIN(0.002f, samples[i][1], g);
		TEST_ASSERT_FLOAT_WITHIN(0.002f, samples[i][2], b);
	}
}

SK_TEST(ui_color_alpha_bind_writeback_changed_committed_clamp_swatch) {
	const sk_ui_api_t* ui = wcolor_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;
	sk_ui_node_t edit;
	sk_ui_node_t sw;
	sk_ui_node_t picker;
	sk_ui_node_t sv;
	sk_ui_rect_t r;
	f32 col4[4] = {0.20f, 0.40f, 0.60f, 0.50f};
	f32 col3[3] = {0.10f, 0.20f, 0.30f};
	f32 swatch_v[4] = {0.90f, 0.10f, 0.20f, 0.80f};
	f32 out[4];
	sk_ui_color_t cc;

	btn = ui->widget_color_button(ctx, root, col4, SK_UI_COLOR_FLAG_PICKER_DEFAULT, 160.0f, 22.0f, "c-btn");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(btn));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, btn, SK_UI_CLASS_COLOR_BUTTON));
	TEST_ASSERT_EQUAL_INT(0, ui->color_bind(ctx, btn, col4, 4));
	TEST_ASSERT_EQUAL_INT(0, ui->color_get_rgba(ctx, btn, out));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.20f, out[0]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.50f, out[3]);
	TEST_ASSERT_EQUAL_INT(0, ui->color_changed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->color_committed(ctx, btn));

	/* Clamp out-of-range programmatic set; does not fire changed. */
	TEST_ASSERT_EQUAL_INT(0, ui->color_set_rgba(ctx, btn, 2.0f, -1.0f, 0.5f, 4.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->color_get_rgba(ctx, btn, out));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, out[0]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, out[1]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, out[2]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, out[3]);
	TEST_ASSERT_EQUAL_INT(0, ui->color_changed(ctx, btn));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, col4[0]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, col4[1]);

	cc.r = 0.1f;
	cc.g = 0.2f;
	cc.b = 0.3f;
	cc.a = 0.4f;
	TEST_ASSERT_EQUAL_INT(0, ui->color_bind_color(ctx, btn, &cc));
	TEST_ASSERT_EQUAL_INT(0, ui->color_set_rgba(ctx, btn, 0.8f, 0.1f, 0.2f, 0.6f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, cc.r);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, cc.a);

	TEST_ASSERT_EQUAL_INT(0, ui->color_bind(ctx, btn, col4, 4));
	TEST_ASSERT_EQUAL_INT(0, ui->color_set_rgba(ctx, btn, 0.80f, 0.20f, 0.10f, 0.50f));
	wcolor_layout(ui, ctx, 400.0f, 360.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->color_get_open(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->color_set_open(ctx, btn, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->color_get_open(ctx, btn));
	wcolor_layout(ui, ctx, 400.0f, 360.0f);

	sv = ui->color_sv_square(ctx, btn);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sv));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->color_hue_bar(ctx, btn)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->color_alpha_bar(ctx, btn)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->color_preview(ctx, btn)));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sv, &r, NULL));
	TEST_ASSERT_TRUE(r.width > 8.0f);
	(void)ui->color_changed(ctx, btn);
	wcolor_pointer(ui, ctx, r.x + r.width * 0.75f, r.y + r.height * 0.25f, 1);
	wcolor_pointer(ui, ctx, r.x + r.width * 0.75f, r.y + r.height * 0.25f, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->color_changed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->color_changed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(1, ui->color_committed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->color_committed(ctx, btn));
	TEST_ASSERT_TRUE(col4[0] > 0.0f);

	/* Compact ColorEdit3 bind write-back. */
	edit = ui->widget_color_edit3(ctx, root, "##v", col3, 0u, "c-edit");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(edit));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, edit, SK_UI_CLASS_COLOR_EDIT));
	TEST_ASSERT_EQUAL_INT(0, ui->color_get_values(ctx, edit, out, 3));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.10f, out[0]);
	{
		f32 clamp_in[3] = {1.5f, -0.2f, 0.4f};
		TEST_ASSERT_EQUAL_INT(0, ui->color_set_values(ctx, edit, clamp_in, 3));
	}
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, col3[0]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, col3[1]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, col3[2]);

	/* Read-only swatch never mutates. */
	sw = ui->widget_color_swatch(ctx, root, swatch_v, 0.0f, 0.0f, "c-sw");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sw));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, sw, SK_UI_CLASS_COLOR_SWATCH));
	TEST_ASSERT_EQUAL_INT(0, ui->color_bind(ctx, sw, swatch_v, 4));
	TEST_ASSERT_EQUAL_UINT(SK_UI_COLOR_FLAG_SWATCH, ui->color_get_flags(ctx, sw));
	wcolor_layout(ui, ctx, 400.0f, 360.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sw, &r, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, SK_UI_COLOR_SWATCH_SIZE, r.width);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, SK_UI_COLOR_SWATCH_SIZE, r.height);
	wcolor_pointer(ui, ctx, r.x + 4.0f, r.y + 4.0f, 1);
	wcolor_pointer(ui, ctx, r.x + 4.0f, r.y + 4.0f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->color_get_open(ctx, sw));
	TEST_ASSERT_EQUAL_INT(0, ui->color_changed(ctx, sw));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.90f, swatch_v[0]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.80f, swatch_v[3]);
	TEST_ASSERT_EQUAL_INT(0, ui->color_set_rgba(ctx, sw, 0.1f, 0.2f, 0.3f, 0.4f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.90f, swatch_v[0]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.80f, swatch_v[3]);

	picker = ui->widget_color_picker4(ctx, root, NULL, col4, SK_UI_COLOR_FLAG_PICKER_DEFAULT, "c-pk");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(picker));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->color_sv_square(ctx, picker)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->color_alpha_bar(ctx, picker)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->color_preview(ctx, picker)));

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
