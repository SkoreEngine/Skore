/**
 * @file automation.c
 * @brief UI automation query/action API and headless test harness.
 *
 * Contract for a future Selenium-style UI tester: find elements by test id /
 * class / widget type / text (optionally scoped), read layout/style/visibility/
 * enabled, drive interactions by synthesizing real input through input_dispatch,
 * and step frames with a stable clock. Soft-render is optional offscreen RGBA
 * for golden comparison without a window or GPU.
 */

#include "ui.internal.h"

#include "allocator.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Query helpers                                                              */
/* -------------------------------------------------------------------------- */

static const sk_ui_api_t* auto_api(void) {
	return ui_get_api_table();
}

static i32 ui_node_is_under_scope(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t scope) {
	const sk_ui_api_t* ui = auto_api();
	sk_ui_node_t cur;
	if (!sk_ui_node_is_valid(scope)) {
		return 1;
	}
	if (!ui->node_alive(ctx, scope) || !ui->node_alive(ctx, node)) {
		return 0;
	}
	cur = node;
	while (sk_ui_node_is_valid(cur)) {
		if (sk_ui_node_eq(cur, scope)) {
			return 1;
		}
		cur = ui->node_parent(ctx, cur);
	}
	return 0;
}

static sk_ui_node_t ui_query_scope_root(const sk_ui_context_t* ctx, sk_ui_node_t scope) {
	const sk_ui_api_t* ui = auto_api();
	if (sk_ui_node_is_valid(scope) && ui->node_alive(ctx, scope)) {
		return scope;
	}
	return ui->context_root(ctx);
}

static const_chr_t ui_slot_prop_str(const ui_node_slot_t* slot, const_chr_t key) {
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

static i32 ui_slot_prop_i32(const ui_node_slot_t* slot, const_chr_t key, i32* out) {
	u32 i;
	if (slot == NULL || key == NULL) {
		return -1;
	}
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_I32 && e->key != NULL && strcmp(e->key, key) == 0) {
			if (out != NULL) {
				*out = e->data.i32_value;
			}
			return 0;
		}
	}
	return -1;
}

typedef struct ui_query_walk_t {
	const sk_ui_context_t* ctx;
	const_chr_t needle;
	sk_ui_node_t first;
	sk_ui_node_t* out;
	u32 max_out;
	u32 count;
	u32 mode; /* 0=class 1=widget 2=text */
} ui_query_walk_t;

static i32 ui_query_match(const ui_query_walk_t* w, sk_ui_node_t node) {
	const sk_ui_api_t* ui = auto_api();
	const ui_node_slot_t* slot;
	const_chr_t s;
	if (w->needle == NULL) {
		return 0;
	}
	if (w->mode == 0u) {
		return ui->node_has_class(w->ctx, node, w->needle) != 0 ? 1 : 0;
	}
	slot = ui_slot(w->ctx, node);
	if (slot == NULL) {
		return 0;
	}
	if (w->mode == 1u) {
		s = ui_slot_prop_str(slot, "widget");
		return (s != NULL && strcmp(s, w->needle) == 0) ? 1 : 0;
	}
	/* mode 2: exact text match on prop "text" */
	s = ui_slot_prop_str(slot, "text");
	return (s != NULL && strcmp(s, w->needle) == 0) ? 1 : 0;
}

static i32 ui_query_visit(sk_ui_context_t* ctx, sk_ui_node_t node, u32 depth, void_ptr_t user) {
	ui_query_walk_t* w = (ui_query_walk_t*)user;
	(void)ctx;
	(void)depth;
	if (ui_query_match(w, node) == 0) {
		return 0;
	}
	if (!sk_ui_node_is_valid(w->first)) {
		w->first = node;
	}
	if (w->out != NULL && w->count < w->max_out) {
		w->out[w->count] = node;
	}
	w->count += 1u;
	/* For first-only callers max_out==1 and out==NULL still counts; stop early when only first needed. */
	if (w->out == NULL && w->max_out == 1u && sk_ui_node_is_valid(w->first)) {
		return 1; /* abort walk: found first */
	}
	return 0;
}

static sk_ui_node_t ui_query_first(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t needle, u32 mode) {
	const sk_ui_api_t* ui = auto_api();
	ui_query_walk_t w;
	sk_ui_node_t root;
	if (ctx == NULL || needle == NULL || needle[0] == '\0') {
		return SK_UI_NODE_INVALID;
	}
	root = ui_query_scope_root(ctx, scope);
	if (!sk_ui_node_is_valid(root)) {
		return SK_UI_NODE_INVALID;
	}
	memset(&w, 0, sizeof(w));
	w.ctx = ctx;
	w.needle = needle;
	w.first = SK_UI_NODE_INVALID;
	w.out = NULL;
	w.max_out = 1u;
	w.count = 0u;
	w.mode = mode;
	(void)ui->traverse_preorder(SK_CONST_CAST(sk_ui_context_t*, ctx), root, ui_query_visit, &w);
	return w.first;
}

static u32 ui_query_all(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t needle, u32 mode, sk_ui_node_t* out, u32 max_out) {
	const sk_ui_api_t* ui = auto_api();
	ui_query_walk_t w;
	sk_ui_node_t root;
	if (ctx == NULL || needle == NULL || needle[0] == '\0') {
		return 0u;
	}
	root = ui_query_scope_root(ctx, scope);
	if (!sk_ui_node_is_valid(root)) {
		return 0u;
	}
	memset(&w, 0, sizeof(w));
	w.ctx = ctx;
	w.needle = needle;
	w.first = SK_UI_NODE_INVALID;
	w.out = out;
	w.max_out = max_out;
	w.count = 0u;
	w.mode = mode;
	(void)ui->traverse_preorder(SK_CONST_CAST(sk_ui_context_t*, ctx), root, ui_query_visit, &w);
	return w.count;
}

sk_ui_node_t ui_query_by_test_id_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t test_id) {
	const sk_ui_api_t* ui = auto_api();
	sk_ui_node_t n;
	if (ctx == NULL || test_id == NULL || test_id[0] == '\0') {
		return SK_UI_NODE_INVALID;
	}
	n = ui->find_by_id(ctx, test_id);
	if (!sk_ui_node_is_valid(n)) {
		return SK_UI_NODE_INVALID;
	}
	if (!ui_node_is_under_scope(ctx, n, scope)) {
		return SK_UI_NODE_INVALID;
	}
	return n;
}

sk_ui_node_t ui_query_by_class_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t class_name) {
	return ui_query_first(ctx, scope, class_name, 0u);
}

sk_ui_node_t ui_query_by_widget_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t widget_type) {
	return ui_query_first(ctx, scope, widget_type, 1u);
}

sk_ui_node_t ui_query_by_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t text) {
	return ui_query_first(ctx, scope, text, 2u);
}

u32 ui_query_all_by_class_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t class_name, sk_ui_node_t* out, u32 max_out) {
	return ui_query_all(ctx, scope, class_name, 0u, out, max_out);
}

u32 ui_query_all_by_widget_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t widget_type, sk_ui_node_t* out, u32 max_out) {
	return ui_query_all(ctx, scope, widget_type, 1u, out, max_out);
}

u32 ui_query_all_by_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t text, sk_ui_node_t* out, u32 max_out) {
	return ui_query_all(ctx, scope, text, 2u, out, max_out);
}

/* -------------------------------------------------------------------------- */
/* Accessors                                                                  */
/* -------------------------------------------------------------------------- */

i32 ui_node_is_visible_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = auto_api();
	const ui_node_slot_t* slot;
	sk_ui_rect_t border;
	sk_ui_computed_style_t cs;
	i32 hidden = 0;

	if (ctx == NULL || !ui->node_alive(ctx, node)) {
		return 0;
	}
	slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return 0;
	}
	if (ui_slot_prop_i32(slot, "hidden", &hidden) == 0 && hidden != 0) {
		return 0;
	}
	if (ui->node_get_computed_style(ctx, node, &cs) == 0) {
		if (cs.opacity <= 0.0001f) {
			return 0;
		}
	}
	if (ui->node_get_abs_rect(ctx, node, &border, NULL) != 0) {
		return 0;
	}
	if (border.width <= 0.0f || border.height <= 0.0f) {
		return 0;
	}
	return 1;
}

i32 ui_node_is_enabled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = auto_api();
	sk_ui_node_t cur;
	if (ctx == NULL || !ui->node_alive(ctx, node)) {
		return 0;
	}
	cur = node;
	while (sk_ui_node_is_valid(cur)) {
		if ((ui->node_get_state(ctx, cur) & (u32)SK_UI_STATE_DISABLED) != 0u) {
			return 0;
		}
		cur = ui->node_parent(ctx, cur);
	}
	return 1;
}

const_chr_t ui_node_get_visible_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t t;
	if (slot == NULL) {
		return "";
	}
	t = ui_slot_prop_str(slot, "text");
	return t != NULL ? t : "";
}

/* -------------------------------------------------------------------------- */
/* Actions (synthesize real input via input_dispatch)                         */
/* -------------------------------------------------------------------------- */

static i32 ui_action_center(const sk_ui_context_t* ctx, sk_ui_node_t node, f32* out_x, f32* out_y) {
	const sk_ui_api_t* ui = auto_api();
	sk_ui_rect_t border;
	if (ui->node_get_abs_rect(ctx, node, &border, NULL) != 0) {
		return -1;
	}
	if (border.width <= 0.0f || border.height <= 0.0f) {
		return -1;
	}
	*out_x = border.x + border.width * 0.5f;
	*out_y = border.y + border.height * 0.5f;
	return 0;
}

i32 ui_action_click_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = auto_api();
	sk_ui_input_event_t ev;
	f32 x, y;

	if (ctx == NULL || !ui->node_alive(ctx, node)) {
		return -1;
	}
	if (ui_action_center(ctx, node, &x, &y) != 0) {
		return -1;
	}

	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = x;
	ev.y = y;
	if (ui->input_dispatch(ctx, &ev) != 0) {
		return -1;
	}

	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = x;
	ev.y = y;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 1;
	if (ui->input_dispatch(ctx, &ev) != 0) {
		return -1;
	}

	ev.down = 0;
	if (ui->input_dispatch(ctx, &ev) != 0) {
		return -1;
	}
	return 0;
}

i32 ui_action_type_text_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text) {
	const sk_ui_api_t* ui = auto_api();
	sk_ui_input_event_t ev;

	if (ctx == NULL || !ui->node_alive(ctx, node)) {
		return -1;
	}
	if (ui->focus_set(ctx, node) != 0) {
		return -1;
	}
	if (text == NULL) {
		text = "";
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_TEXT;
	ev.text = text;
	return ui->input_dispatch(ctx, &ev);
}

i32 ui_action_scroll_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 scroll_x, f32 scroll_y) {
	const sk_ui_api_t* ui = auto_api();
	sk_ui_input_event_t ev;
	f32 x, y;

	if (ctx == NULL || !ui->node_alive(ctx, node)) {
		return -1;
	}
	if (ui_action_center(ctx, node, &x, &y) != 0) {
		return -1;
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_WHEEL;
	ev.x = x;
	ev.y = y;
	ev.scroll_x = scroll_x;
	ev.scroll_y = scroll_y;
	return ui->input_dispatch(ctx, &ev);
}

i32 ui_action_focus_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	return auto_api()->focus_set(ctx, node);
}

/* -------------------------------------------------------------------------- */
/* Soft-render (CPU triangles → RGBA8; golden path)                           */
/* -------------------------------------------------------------------------- */

static void soft_clear(u8* px, u32 w, u32 h, u8 r, u8 g, u8 b, u8 a) {
	u32 i;
	u32 n = w * h;
	for (i = 0u; i < n; ++i) {
		px[i * 4u + 0u] = r;
		px[i * 4u + 1u] = g;
		px[i * 4u + 2u] = b;
		px[i * 4u + 3u] = a;
	}
}

static void soft_put(u8* px, u32 w, u32 h, i32 x, i32 y, u32 color) {
	u8* p;
	u8 sr, sg, sb, sa;
	u8 dr, dg, db, da;
	u32 inv;
	if (x < 0 || y < 0 || (u32)x >= w || (u32)y >= h) {
		return;
	}
	p = px + ((u32)y * w + (u32)x) * 4u;
	sr = (u8)(color & 0xFFu);
	sg = (u8)((color >> 8) & 0xFFu);
	sb = (u8)((color >> 16) & 0xFFu);
	sa = (u8)((color >> 24) & 0xFFu);
	if (sa >= 250u) {
		p[0] = sr;
		p[1] = sg;
		p[2] = sb;
		p[3] = sa;
		return;
	}
	if (sa == 0u) {
		return;
	}
	dr = p[0];
	dg = p[1];
	db = p[2];
	da = p[3];
	inv = 255u - (u32)sa;
	p[0] = (u8)(((u32)sr * (u32)sa + (u32)dr * inv) / 255u);
	p[1] = (u8)(((u32)sg * (u32)sa + (u32)dg * inv) / 255u);
	p[2] = (u8)(((u32)sb * (u32)sa + (u32)db * inv) / 255u);
	p[3] = (u8)((u32)sa + ((u32)da * inv) / 255u);
}

static f32 soft_edge(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy) {
	return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

static void soft_tri(u8* px, u32 w, u32 h, f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2, u32 color) {
	i32 minx, maxx, miny, maxy, x, y;
	f32 area;
	minx = (i32)(x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2));
	maxx = (i32)(x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2));
	miny = (i32)(y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2));
	maxy = (i32)(y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2));
	if (minx < 0) {
		minx = 0;
	}
	if (miny < 0) {
		miny = 0;
	}
	if (maxx >= (i32)w) {
		maxx = (i32)w - 1;
	}
	if (maxy >= (i32)h) {
		maxy = (i32)h - 1;
	}
	area = soft_edge(x0, y0, x1, y1, x2, y2);
	if (area > -1.0e-6f && area < 1.0e-6f) {
		return;
	}
	for (y = miny; y <= maxy; ++y) {
		for (x = minx; x <= maxx; ++x) {
			f32 px_c = (f32)x + 0.5f;
			f32 py_c = (f32)y + 0.5f;
			f32 w0 = soft_edge(x1, y1, x2, y2, px_c, py_c);
			f32 w1 = soft_edge(x2, y2, x0, y0, px_c, py_c);
			f32 w2 = soft_edge(x0, y0, x1, y1, px_c, py_c);
			if (area > 0.0f) {
				if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
					soft_put(px, w, h, x, y, color);
				}
			} else {
				if (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f) {
					soft_put(px, w, h, x, y, color);
				}
			}
		}
	}
}

static i32 soft_in_clip(f32 x, f32 y, const sk_ui_rect_t* clip) {
	if (clip == NULL || clip->width <= 0.0f || clip->height <= 0.0f) {
		return 1;
	}
	return (x >= clip->x && y >= clip->y && x < clip->x + clip->width && y < clip->y + clip->height) ? 1 : 0;
}

static void soft_raster_draw_list(const sk_ui_draw_list_t* dl, u8* px, u32 w, u32 h) {
	u32 c;
	soft_clear(px, w, h, 30, 30, 34, 255);
	if (dl == NULL) {
		return;
	}
	for (c = 0u; c < dl->command_count; ++c) {
		const sk_ui_draw_cmd_t* cmd = &dl->commands[c];
		u32 i;
		if (cmd->kind != SK_UI_DRAW_CMD_MESH) {
			continue;
		}
		if (cmd->texture_kind == SK_UI_DRAW_TEX_MSDF) {
			/* Coverage is GPU-decoded from the MSDF atlas; skip solid tofu in soft raster. */
			continue;
		}
		for (i = 0u; i + 2u < cmd->index_count; i += 3u) {
			u32 i0 = dl->indices[cmd->index_offset + i + 0u];
			u32 i1 = dl->indices[cmd->index_offset + i + 1u];
			u32 i2 = dl->indices[cmd->index_offset + i + 2u];
			const sk_ui_draw_vertex_t* v0 = &dl->vertices[i0];
			const sk_ui_draw_vertex_t* v1 = &dl->vertices[i1];
			const sk_ui_draw_vertex_t* v2 = &dl->vertices[i2];
			f32 cx = (v0->x + v1->x + v2->x) / 3.0f;
			f32 cy = (v0->y + v1->y + v2->y) / 3.0f;
			if (!soft_in_clip(cx, cy, &cmd->clip)) {
				continue;
			}
			soft_tri(px, w, h, v0->x, v0->y, v1->x, v1->y, v2->x, v2->y, v0->color);
		}
	}
}

/* -------------------------------------------------------------------------- */
/* Harness                                                                    */
/* -------------------------------------------------------------------------- */

struct sk_ui_harness_t {
	const sk_allocator_t* allocator;
	sk_ui_context_t* ctx;
	f32 width;
	f32 height;
	f32 content_scale;
	f64 time_sec;
	f32 last_delta;
	u32 frame_index;
	i32 soft_render;
	u32 pixel_w;
	u32 pixel_h;
	u8* pixels;
	sk_ui_paint_params_t paint;
};

static u32 ui_harness_phys_dim(f32 logical, f32 scale) {
	f32 s = scale > 0.0f ? scale : 1.0f;
	f32 v = logical * s;
	if (v <= 0.0f) {
		return 1u;
	}
	{
		u32 r = (u32)(v + 0.5f);
		return r < 1u ? 1u : r;
	}
}

static i32 ui_harness_alloc_pixels(sk_ui_harness_t* h) {
	const sk_allocator_t* a = h->allocator;
	u32 w = ui_harness_phys_dim(h->width, h->content_scale);
	u32 hgt = ui_harness_phys_dim(h->height, h->content_scale);
	size_t bytes;
	u8* buf;
	if (!h->soft_render) {
		if (h->pixels != NULL) {
			a->free(a->instance, h->pixels);
			h->pixels = NULL;
		}
		h->pixel_w = 0u;
		h->pixel_h = 0u;
		return 0;
	}
	if (h->pixels != NULL && h->pixel_w == w && h->pixel_h == hgt) {
		return 0;
	}
	if (h->pixels != NULL) {
		a->free(a->instance, h->pixels);
		h->pixels = NULL;
	}
	/* size_t matches allocator / memset; reject overflow before multiply. */
	if (w == 0u || hgt == 0u || w > 0x1fffffffu || hgt > 0x1fffffffu) {
		return -1;
	}
	bytes = (size_t)w * (size_t)hgt * 4u;
	buf = (u8*)a->alloc(a->instance, bytes);
	if (buf == NULL) {
		return -1;
	}
	memset(buf, 0, bytes);
	h->pixels = buf;
	h->pixel_w = w;
	h->pixel_h = hgt;
	return 0;
}

sk_ui_harness_t* ui_harness_create_impl(const sk_ui_harness_desc_t* desc) {
	const sk_ui_api_t* ui = auto_api();
	const sk_allocator_t* a;
	sk_ui_harness_t* h;
	sk_ui_harness_desc_t d;

	if (desc == NULL) {
		memset(&d, 0, sizeof(d));
	} else {
		d = *desc;
	}
	a = d.allocator != NULL ? d.allocator : sk_allocator_default();
	h = (sk_ui_harness_t*)a->alloc(a->instance, sizeof(sk_ui_harness_t));
	if (h == NULL) {
		return NULL;
	}
	memset(h, 0, sizeof(*h));
	h->allocator = a;
	h->width = d.width > 0.0f ? d.width : 800.0f;
	h->height = d.height > 0.0f ? d.height : 600.0f;
	h->content_scale = d.content_scale > 0.0f ? d.content_scale : 1.0f;
	h->soft_render = d.soft_render != 0 ? 1 : 0;
	h->ctx = ui->context_create(a);
	if (h->ctx == NULL) {
		a->free(a->instance, h);
		return NULL;
	}
	if (ui_harness_alloc_pixels(h) != 0) {
		ui->context_destroy(h->ctx);
		a->free(a->instance, h);
		return NULL;
	}
	return h;
}

void ui_harness_destroy_impl(sk_ui_harness_t* harness) {
	const sk_ui_api_t* ui;
	const sk_allocator_t* a;
	if (harness == NULL) {
		return;
	}
	ui = auto_api();
	a = harness->allocator;
	if (harness->ctx != NULL) {
		ui->context_destroy(harness->ctx);
		harness->ctx = NULL;
	}
	if (harness->pixels != NULL) {
		a->free(a->instance, harness->pixels);
		harness->pixels = NULL;
	}
	a->free(a->instance, harness);
}

sk_ui_context_t* ui_harness_context_impl(sk_ui_harness_t* harness) {
	return harness != NULL ? harness->ctx : NULL;
}

i32 ui_harness_step_impl(sk_ui_harness_t* harness, f32 delta_seconds) {
	const sk_ui_api_t* ui = auto_api();
	const sk_ui_draw_list_t* dl;
	if (harness == NULL || harness->ctx == NULL) {
		return -1;
	}
	if (delta_seconds < 0.0f) {
		delta_seconds = 0.0f;
	}
	harness->time_sec += (f64)delta_seconds;
	harness->last_delta = delta_seconds;
	harness->frame_index += 1u;

	ui_tooltip_tick_impl(harness->ctx, delta_seconds);
	/* Expire a delivered payload after the accept frame (mouse already up). */
	if ((harness->ctx->pointer_buttons & (1u << (u32)SK_UI_POINTER_BUTTON_LEFT)) == 0u) {
		ui_drag_drop_on_pointer(harness->ctx, SK_UI_POINTER_BUTTON_LEFT, -1);
	}

	if (ui->style_resolve(harness->ctx) != 0) {
		return -1;
	}
	if (ui->layout(harness->ctx, harness->width, harness->height) != 0) {
		return -1;
	}
	if (ui->layout_apply_scale(harness->ctx, harness->content_scale, harness->content_scale) != 0) {
		return -1;
	}
	if (ui->paint(harness->ctx, &harness->paint) != 0) {
		return -1;
	}
	if (harness->soft_render) {
		if (ui_harness_alloc_pixels(harness) != 0) {
			return -1;
		}
		dl = ui->get_draw_list(harness->ctx);
		soft_raster_draw_list(dl, harness->pixels, harness->pixel_w, harness->pixel_h);
	}
	return 0;
}

f64 ui_harness_time_impl(const sk_ui_harness_t* harness) {
	return harness != NULL ? harness->time_sec : 0.0;
}

f32 ui_harness_last_delta_impl(const sk_ui_harness_t* harness) {
	return harness != NULL ? harness->last_delta : 0.0f;
}

u32 ui_harness_frame_index_impl(const sk_ui_harness_t* harness) {
	return harness != NULL ? harness->frame_index : 0u;
}

i32 ui_harness_set_size_impl(sk_ui_harness_t* harness, f32 width, f32 height) {
	const sk_ui_api_t* ui;
	if (harness == NULL || harness->ctx == NULL) {
		return -1;
	}
	if (width > 0.0f) {
		harness->width = width;
	}
	if (height > 0.0f) {
		harness->height = height;
	}
	ui = auto_api();
	(void)ui->node_mark_dirty(harness->ctx, ui->context_root(harness->ctx), (u32)SK_UI_DIRTY_ALL);
	return 0;
}

i32 ui_harness_set_content_scale_impl(sk_ui_harness_t* harness, f32 scale) {
	const sk_ui_api_t* ui;
	if (harness == NULL || harness->ctx == NULL) {
		return -1;
	}
	if (scale > 0.0f) {
		harness->content_scale = scale;
	}
	ui = auto_api();
	/* Layout stays logical; paint + scaled rects rebuild on the next step.
	 * Mark layout+paint so dirty-gated hosts still re-run the full pipeline. */
	(void)ui->node_mark_dirty(harness->ctx, ui->context_root(harness->ctx), (u32)SK_UI_DIRTY_ALL);
	return 0;
}

const u8* ui_harness_pixels_impl(const sk_ui_harness_t* harness) {
	return harness != NULL ? harness->pixels : NULL;
}

void ui_harness_pixel_size_impl(const sk_ui_harness_t* harness, u32* out_w, u32* out_h) {
	if (out_w != NULL) {
		*out_w = harness != NULL ? harness->pixel_w : 0u;
	}
	if (out_h != NULL) {
		*out_h = harness != NULL ? harness->pixel_h : 0u;
	}
}

void ui_harness_set_font_impl(sk_ui_harness_t* harness, sk_ui_font_system_t* system, sk_ui_font_t* font) {
	if (harness == NULL) {
		return;
	}
	harness->paint.font_system = system;
	harness->paint.font = font;
}

const sk_ui_draw_list_t* ui_harness_draw_list_impl(const sk_ui_harness_t* harness) {
	if (harness == NULL || harness->ctx == NULL) {
		return NULL;
	}
	return auto_api()->get_draw_list(harness->ctx);
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

#include "testdata/skore_test_font_ttf.h"

static const sk_ui_api_t* atest_api(void) {
	return ui_get_api_table();
}

static void atest_set_size(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, f32 w, f32 h) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &p));
}

static i32 g_auto_click_count;
static void auto_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	(void)user;
	g_auto_click_count += 1;
}

SK_TEST(ui_auto_query_by_id_class_widget_text_scoped) {
	const sk_ui_api_t* ui = atest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t panel = ui->widget_panel(ctx, root, "panel-a");
	sk_ui_node_t panel_b = ui->widget_panel(ctx, root, "panel-b");
	sk_ui_node_t btn_a = ui->widget_button(ctx, panel, "Save", "btn-save");
	sk_ui_node_t btn_b = ui->widget_button(ctx, panel_b, "Cancel", "btn-cancel");
	sk_ui_node_t lbl = ui->widget_label(ctx, panel, "Title", "lbl-title");
	sk_ui_node_t found;
	sk_ui_node_t all[8];
	u32 n;

	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "btn-save"), btn_a));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->query_by_test_id(ctx, panel, "btn-save"), btn_a));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, panel, "btn-cancel")));

	found = ui->query_by_class(ctx, panel, SK_UI_CLASS_BUTTON);
	TEST_ASSERT_TRUE(sk_ui_node_eq(found, btn_a));

	found = ui->query_by_widget(ctx, SK_UI_NODE_INVALID, "label");
	TEST_ASSERT_TRUE(sk_ui_node_eq(found, lbl));

	found = ui->query_by_text(ctx, panel_b, "Cancel");
	TEST_ASSERT_TRUE(sk_ui_node_eq(found, btn_b));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->query_by_text(ctx, panel, "Cancel")));

	n = ui->query_all_by_class(ctx, SK_UI_NODE_INVALID, SK_UI_CLASS_BUTTON, all, 8u);
	TEST_ASSERT_EQUAL_UINT(2u, n);
	n = ui->query_all_by_widget(ctx, panel, "button", NULL, 0u);
	TEST_ASSERT_EQUAL_UINT(1u, n);
	n = ui->query_all_by_text(ctx, SK_UI_NODE_INVALID, "Title", all, 8u);
	TEST_ASSERT_EQUAL_UINT(1u, n);

	ui->context_destroy(ctx);
}

SK_TEST(ui_auto_accessors_layout_style_visible_enabled) {
	const sk_ui_api_t* ui = atest_api();
	sk_ui_harness_desc_t desc;
	sk_ui_harness_t* h;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t btn;
	sk_ui_rect_t border;
	sk_ui_computed_style_t cs;

	memset(&desc, 0, sizeof(desc));
	desc.width = 200.0f;
	desc.height = 100.0f;
	desc.content_scale = 1.0f;
	h = ui->harness_create(&desc);
	TEST_ASSERT_NOT_NULL(h);
	ctx = ui->harness_context(h);
	root = ui->context_root(ctx);
	btn = ui->widget_button(ctx, root, "Go", "btn");
	atest_set_size(ui, ctx, btn, 80.0f, 28.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 1.0f / 60.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, btn, &border, NULL));
	TEST_ASSERT_TRUE(border.width > 0.0f);
	TEST_ASSERT_TRUE(border.height > 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, btn, &cs));
	TEST_ASSERT_TRUE(cs.opacity > 0.0f);
	TEST_ASSERT_TRUE(ui->node_is_visible(ctx, btn));
	TEST_ASSERT_TRUE(ui->node_is_enabled(ctx, btn));
	TEST_ASSERT_EQUAL_STRING("Go", ui->node_get_visible_text(ctx, btn));

	TEST_ASSERT_EQUAL_INT(0, ui->button_set_disabled(ctx, btn, 1));
	TEST_ASSERT_FALSE(ui->node_is_enabled(ctx, btn));

	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, btn, "hidden", 1));
	TEST_ASSERT_FALSE(ui->node_is_visible(ctx, btn));

	ui->harness_destroy(h);
}

SK_TEST(ui_auto_action_click_button_callback_and_state) {
	const sk_ui_api_t* ui = atest_api();
	sk_ui_harness_desc_t desc;
	sk_ui_harness_t* h;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t btn;
	sk_ui_node_callbacks_t cbs;
	sk_ui_computed_style_t after;
	u32 st;

	memset(&desc, 0, sizeof(desc));
	desc.width = 200.0f;
	desc.height = 80.0f;
	h = ui->harness_create(&desc);
	TEST_ASSERT_NOT_NULL(h);
	ctx = ui->harness_context(h);
	root = ui->context_root(ctx);
	btn = ui->widget_button(ctx, root, "OK", "ok");
	atest_set_size(ui, ctx, btn, 100.0f, 32.0f);

	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = auto_on_click;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, btn, &cbs));

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 0.016f));

	g_auto_click_count = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->action_click(ctx, btn));
	TEST_ASSERT_EQUAL_INT(1, g_auto_click_count);

	/* Pointer path leaves hover and/or focused state (visual style variants apply). */
	st = ui->node_get_state(ctx, btn);
	TEST_ASSERT_TRUE((st & (u32)SK_UI_STATE_HOVER) != 0u || (st & (u32)SK_UI_STATE_FOCUSED) != 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 0.016f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, btn, &after));
	TEST_ASSERT_TRUE(after.opacity > 0.0f);

	ui->harness_destroy(h);
}

SK_TEST(ui_auto_action_type_text_input_model_and_pixels) {
	const sk_ui_api_t* ui = atest_api();
	sk_ui_harness_desc_t desc;
	sk_ui_harness_t* h;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t ti;
	sk_ui_font_system_t* fs;
	sk_ui_font_t* font;
	const u8* px;
	u8* snap = NULL;
	u32 w, ht;
	u32 i;
	u32 nbytes;
	i32 differ = 0;

	memset(&desc, 0, sizeof(desc));
	desc.width = 240.0f;
	desc.height = 60.0f;
	desc.soft_render = 1;
	h = ui->harness_create(&desc);
	TEST_ASSERT_NOT_NULL(h);
	ctx = ui->harness_context(h);
	root = ui->context_root(ctx);

	fs = ui->font_system_create(NULL);
	TEST_ASSERT_NOT_NULL(fs);
	font = ui->font_load_memory(fs, skore_test_font_ttf, (u32)sizeof(skore_test_font_ttf));
	TEST_ASSERT_NOT_NULL(font);
	ui->harness_set_font(h, fs, font);

	ti = ui->widget_text_input(ctx, root, "", "ti");
	atest_set_size(ui, ctx, ti, 200.0f, 28.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 1.0f / 60.0f));
	px = ui->harness_pixels(h);
	TEST_ASSERT_NOT_NULL(px);
	ui->harness_pixel_size(h, &w, &ht);
	TEST_ASSERT_TRUE(w > 0u && ht > 0u);
	nbytes = w * ht * 4u;
	snap = (u8*)malloc((size_t)nbytes);
	TEST_ASSERT_NOT_NULL(snap);
	memcpy(snap, px, (size_t)nbytes);

	TEST_ASSERT_EQUAL_INT(0, ui->action_type_text(ctx, ti, "Hi"));
	TEST_ASSERT_EQUAL_STRING("Hi", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("Hi", ui->node_get_visible_text(ctx, ti));

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 1.0f / 60.0f));
	px = ui->harness_pixels(h);
	TEST_ASSERT_NOT_NULL(px);
	for (i = 0u; i < nbytes; ++i) {
		if (snap[i] != px[i]) {
			differ = 1;
			break;
		}
	}
	/* Model value and rendered pixels both observed after type + step. */
	TEST_ASSERT_EQUAL_STRING("Hi", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_TRUE(differ != 0);

	free(snap);
	ui->harness_destroy(h);
	ui->font_destroy(font);
	ui->font_system_destroy(fs);
}

SK_TEST(ui_auto_action_scroll_view_clipping) {
	const sk_ui_api_t* ui = atest_api();
	sk_ui_harness_desc_t desc;
	sk_ui_harness_t* h;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t sv;
	sk_ui_node_t content;
	sk_ui_node_t child;
	sk_ui_style_props_t p;
	f32 sx = 0.0f, sy = 0.0f;
	const sk_ui_draw_list_t* dl;
	u32 c;
	i32 saw_clip = 0;

	memset(&desc, 0, sizeof(desc));
	desc.width = 120.0f;
	desc.height = 80.0f;
	desc.soft_render = 1;
	h = ui->harness_create(&desc);
	TEST_ASSERT_NOT_NULL(h);
	ctx = ui->harness_context(h);
	root = ui->context_root(ctx);

	sv = ui->widget_scroll_view(ctx, root, "sv");
	atest_set_size(ui, ctx, sv, 100.0f, 60.0f);
	content = ui->scroll_view_content(ctx, sv);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(content));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, sv, 100.0f, 300.0f));

	child = ui->widget_panel(ctx, content, "tall");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.width = sk_ui_pt(100.0f);
	p.layout.height = sk_ui_pt(300.0f);
	p.background_color = sk_ui_rgba(0.2f, 0.6f, 0.9f, 1.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, child, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 0.016f));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, sy);

	TEST_ASSERT_EQUAL_INT(0, ui->action_scroll(ctx, sv, 0.0f, -40.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_TRUE(sy > 0.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 0.016f));
	dl = ui->harness_draw_list(h);
	TEST_ASSERT_NOT_NULL(dl);
	for (c = 0u; c < dl->command_count; ++c) {
		if (dl->commands[c].kind == SK_UI_DRAW_CMD_PUSH_CLIP ||
			(dl->commands[c].kind == SK_UI_DRAW_CMD_MESH && dl->commands[c].clip.width > 0.0f && dl->commands[c].clip.height > 0.0f && dl->commands[c].clip.height < 300.0f)) {
			saw_clip = 1;
			break;
		}
	}
	TEST_ASSERT_TRUE(saw_clip);
	TEST_ASSERT_TRUE(ui->node_get_clip_children(ctx, sv));

	ui->harness_destroy(h);
}

SK_TEST(ui_auto_harness_deterministic_clock) {
	const sk_ui_api_t* ui = atest_api();
	sk_ui_harness_desc_t desc;
	sk_ui_harness_t* h;
	f64 t0, t1, t2;

	memset(&desc, 0, sizeof(desc));
	desc.width = 64.0f;
	desc.height = 64.0f;
	h = ui->harness_create(&desc);
	TEST_ASSERT_NOT_NULL(h);
	TEST_ASSERT_EQUAL_UINT(0u, ui->harness_frame_index(h));
	TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, ui->harness_time(h));

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 0.5f));
	t0 = ui->harness_time(h);
	TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.5, t0);
	TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, ui->harness_last_delta(h));
	TEST_ASSERT_EQUAL_UINT(1u, ui->harness_frame_index(h));

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 0.25f));
	t1 = ui->harness_time(h);
	TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.75, t1);
	TEST_ASSERT_EQUAL_UINT(2u, ui->harness_frame_index(h));

	/* Same deltas always produce the same clock — no wall time. */
	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 0.0f));
	t2 = ui->harness_time(h);
	TEST_ASSERT_DOUBLE_WITHIN(1e-9, t1, t2);
	TEST_ASSERT_EQUAL_UINT(3u, ui->harness_frame_index(h));

	ui->harness_destroy(h);
}

SK_TEST(ui_auto_harness_query_and_click_e2e) {
	const sk_ui_api_t* ui = atest_api();
	sk_ui_harness_desc_t desc;
	sk_ui_harness_t* h;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t panel;
	sk_ui_node_t btn;
	sk_ui_node_t found;
	sk_ui_node_callbacks_t cbs;

	memset(&desc, 0, sizeof(desc));
	desc.width = 320.0f;
	desc.height = 200.0f;
	desc.soft_render = 1;
	h = ui->harness_create(&desc);
	TEST_ASSERT_NOT_NULL(h);
	ctx = ui->harness_context(h);
	root = ui->context_root(ctx);
	panel = ui->widget_panel(ctx, root, "main");
	atest_set_size(ui, ctx, panel, 300.0f, 180.0f);
	btn = ui->widget_button(ctx, panel, "Submit", "submit");
	atest_set_size(ui, ctx, btn, 120.0f, 36.0f);

	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = auto_on_click;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, btn, &cbs));

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 1.0f / 60.0f));
	found = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "submit");
	TEST_ASSERT_TRUE(sk_ui_node_eq(found, btn));
	found = ui->query_by_text(ctx, panel, "Submit");
	TEST_ASSERT_TRUE(sk_ui_node_eq(found, btn));
	TEST_ASSERT_TRUE(ui->node_is_visible(ctx, btn));
	TEST_ASSERT_TRUE(ui->node_is_enabled(ctx, btn));

	g_auto_click_count = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->action_click(ctx, found));
	TEST_ASSERT_EQUAL_INT(1, g_auto_click_count);
	TEST_ASSERT_NOT_NULL(ui->harness_pixels(h));

	ui->harness_destroy(h);
}

#endif /* SK_TESTS */
