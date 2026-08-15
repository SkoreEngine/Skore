/**
 * @file tooltip.c
 * @brief Tooltip family (APX-354; WIDGET_MANIFEST.md §20).
 *
 * BeginTooltip / EndTooltip analog: a floating surface shown while the
 * previous sibling (or an explicit anchor) is hovered. Project Browser
 * gates the asset hover card on ImGuiHoveredFlags_DelayNormal (0.40s).
 * Children are arbitrary (multi-line card / coloured profiler text).
 * The surface follows the cursor, clamps to the viewport, and never
 * captures hover or click (POINTER_EVENTS_NONE + Clay passthrough).
 */

#include "ui.internal.h"

#include "allocator.h"

#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS
#include "test.h"
#endif

typedef struct ui_tooltip_data_t {
	sk_ui_node_t host;
	sk_ui_node_t anchor;
	f32 delay;
	f32 hover_sec;
	i32 visible;
	i32 forced;
} ui_tooltip_data_t;

static const sk_ui_api_t* tooltip_api(void) {
	return ui_get_api_table();
}

static ui_tooltip_data_t* tooltip_data(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL || slot->user_data == NULL) {
		return NULL;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_TOOLTIP_DATA_TYPE_ID)) {
		return NULL;
	}
	return (ui_tooltip_data_t*)slot->user_data;
}

static const ui_tooltip_data_t* tooltip_data_const(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return tooltip_data(SK_CONST_CAST(sk_ui_context_t*, ctx), node);
}

void ui_tooltip_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot) {
	if (slot == NULL || slot->user_data == NULL) {
		return;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_TOOLTIP_DATA_TYPE_ID)) {
		return;
	}
	ctx->allocator->free(ctx->allocator->instance, slot->user_data);
	slot->user_data = NULL;
	slot->user_data_type = SK_TYPE_ID_ZERO;
}

static sk_ui_node_t tooltip_prev_sibling(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = tooltip_api();
	sk_ui_node_t parent;
	u32 n;
	u32 i;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return SK_UI_NODE_INVALID;
	}
	parent = ui->node_parent(ctx, node);
	if (!sk_ui_node_is_valid(parent)) {
		return SK_UI_NODE_INVALID;
	}
	n = ui->node_child_count(ctx, parent);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t ch = ui->node_child_at(ctx, parent, i);
		if (sk_ui_node_eq(ch, node) != 0) {
			if (i == 0u) {
				return SK_UI_NODE_INVALID;
			}
			return ui->node_child_at(ctx, parent, i - 1u);
		}
	}
	return SK_UI_NODE_INVALID;
}

static sk_ui_node_t tooltip_resolve_anchor(const sk_ui_context_t* ctx, sk_ui_node_t tip, const ui_tooltip_data_t* d) {
	if (d != NULL && sk_ui_node_is_valid(d->anchor) && tooltip_api()->node_alive(ctx, d->anchor) != 0) {
		return d->anchor;
	}
	return tooltip_prev_sibling(ctx, tip);
}

static i32 tooltip_hover_contains(const sk_ui_context_t* ctx, sk_ui_node_t anchor) {
	const sk_ui_api_t* ui = tooltip_api();
	sk_ui_node_t cur;
	if (!sk_ui_node_is_valid(anchor) || ctx == NULL) {
		return 0;
	}
	cur = ctx->hover;
	while (sk_ui_node_is_valid(cur)) {
		if (sk_ui_node_eq(cur, anchor) != 0) {
			return 1;
		}
		cur = ui->node_parent(ctx, cur);
	}
	if ((ui->node_get_state(ctx, anchor) & (u32)SK_UI_STATE_HOVER) != 0u) {
		return 1;
	}
	return 0;
}

// NOLINTBEGIN(misc-no-recursion)
static void tooltip_set_pointer_none_walk(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = tooltip_api();
	u32 i;
	u32 n;
	(void)ui->node_set_pointer_events(ctx, node, SK_UI_POINTER_EVENTS_NONE);
	(void)ui->node_set_focusable(ctx, node, 0);
	n = ui->node_child_count(ctx, node);
	for (i = 0u; i < n; ++i) {
		tooltip_set_pointer_none_walk(ctx, ui->node_child_at(ctx, node, i));
	}
}
// NOLINTEND(misc-no-recursion)

static void tooltip_apply_visible(sk_ui_context_t* ctx, sk_ui_node_t tip, ui_tooltip_data_t* d, i32 visible) {
	const sk_ui_api_t* ui = tooltip_api();
	i32 v = visible != 0 ? 1 : 0;
	if (d == NULL) {
		return;
	}
	d->visible = v;
	(void)ui->node_set_prop_i32(ctx, tip, "open", v);
	(void)ui->node_set_prop_i32(ctx, tip, "hidden", v != 0 ? 0 : 1);
	tooltip_set_pointer_none_walk(ctx, tip);
	ui_mark_dirty_up(ctx, tip, (u32)SK_UI_DIRTY_LAYOUT | (u32)SK_UI_DIRTY_PAINT);
}

static f32 tooltip_clamp(f32 v, f32 lo, f32 hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static void tooltip_compute_pos(const sk_ui_context_t* ctx, f32 width, f32 height, f32* out_x, f32* out_y) {
	f32 vw;
	f32 vh;
	f32 x;
	f32 y;
	vw = ctx->root_width > 1.0f ? ctx->root_width : 640.0f;
	vh = ctx->root_height > 1.0f ? ctx->root_height : 400.0f;
	x = ctx->pointer_x + SK_UI_TOOLTIP_OFFSET_X;
	y = ctx->pointer_y + SK_UI_TOOLTIP_OFFSET_Y;
	if (width < 1.0f) {
		width = 1.0f;
	}
	if (height < 1.0f) {
		height = 1.0f;
	}
	if (x + width > vw) {
		x = vw - width;
	}
	if (y + height > vh) {
		y = vh - height;
	}
	x = tooltip_clamp(x, 0.0f, vw > width ? (vw - width) : 0.0f);
	y = tooltip_clamp(y, 0.0f, vh > height ? (vh - height) : 0.0f);
	if (out_x != NULL) {
		*out_x = x;
	}
	if (out_y != NULL) {
		*out_y = y;
	}
}

static void tooltip_tick_one(sk_ui_context_t* ctx, sk_ui_node_t tip, ui_tooltip_data_t* d, f32 dt) {
	sk_ui_node_t anchor;
	i32 hovered;
	if (d == NULL) {
		return;
	}
	if (dt < 0.0f) {
		dt = 0.0f;
	}
	tooltip_set_pointer_none_walk(ctx, tip);
	if (d->forced != 0) {
		if (d->visible == 0) {
			tooltip_apply_visible(ctx, tip, d, 1);
		}
		return;
	}
	anchor = tooltip_resolve_anchor(ctx, tip, d);
	hovered = tooltip_hover_contains(ctx, anchor);
	if (hovered == 0) {
		d->hover_sec = 0.0f;
		(void)tooltip_api()->node_set_prop_f32(ctx, tip, "hover_sec", 0.0f);
		if (d->visible != 0) {
			tooltip_apply_visible(ctx, tip, d, 0);
		}
		return;
	}
	d->hover_sec += dt;
	(void)tooltip_api()->node_set_prop_f32(ctx, tip, "hover_sec", d->hover_sec);
	if (d->hover_sec + 0.0001f >= d->delay) {
		if (d->visible == 0) {
			tooltip_apply_visible(ctx, tip, d, 1);
		}
	}
}

void ui_tooltip_tick_impl(sk_ui_context_t* ctx, f32 dt) {
	u32 i;
	if (ctx == NULL) {
		return;
	}
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		sk_ui_node_t node;
		if (slot->alive == 0u || slot->user_data == NULL) {
			continue;
		}
		if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_TOOLTIP_DATA_TYPE_ID)) {
			continue;
		}
		node.index = i;
		node.generation = slot->generation;
		tooltip_tick_one(ctx, node, (ui_tooltip_data_t*)slot->user_data, dt);
	}
}

void ui_tooltip_place(sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = tooltip_api();
	u32 i;
	if (ctx == NULL) {
		return;
	}
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		ui_tooltip_data_t* d;
		sk_ui_node_t node;
		sk_ui_rect_t r;
		sk_ui_layout_style_t ls;
		f32 x;
		f32 y;
		f32 w;
		f32 h;
		if (slot->alive == 0u || slot->user_data == NULL) {
			continue;
		}
		if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_TOOLTIP_DATA_TYPE_ID)) {
			continue;
		}
		d = (ui_tooltip_data_t*)slot->user_data;
		node.index = i;
		node.generation = slot->generation;
		if (d->visible == 0) {
			continue;
		}
		if (ui->node_get_abs_rect(ctx, node, &r, NULL) != 0) {
			continue;
		}
		w = r.width;
		h = r.height;
		if (w < 1.0f) {
			w = slot->layout_border.width;
		}
		if (h < 1.0f) {
			h = slot->layout_border.height;
		}
		tooltip_compute_pos(ctx, w, h, &x, &y);
		if (ui->node_get_layout_style(ctx, node, &ls) == 0) {
			ls.position = SK_UI_POSITION_ABSOLUTE;
			ls.left = sk_ui_pt(x);
			ls.top = sk_ui_pt(y);
			(void)ui->node_set_layout_style(ctx, node, &ls);
		}
		{
			f32 dx = x - r.x;
			f32 dy = y - r.y;
			if (dx < -0.5f || dx > 0.5f || dy < -0.5f || dy > 0.5f) {
				slot->layout_border.x += dx;
				slot->layout_border.y += dy;
				slot->layout_content.x += dx;
				slot->layout_content.y += dy;
			}
		}
	}
}

sk_ui_node_t ui_widget_tooltip_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = tooltip_api();
	sk_ui_node_t n;
	ui_tooltip_data_t* d;
	sk_ui_layout_style_t ls;
	char auto_id[64];
	const_chr_t use_id = id;

	if (ctx == NULL) {
		return SK_UI_NODE_INVALID;
	}
	if (use_id == NULL || use_id[0] == '\0') {
		ctx->widget_id_seq += 1u;
		(void)snprintf(auto_id, sizeof(auto_id), "ui-tooltip-%u", ctx->widget_id_seq);
		use_id = auto_id;
	}
	n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_add_class(ctx, n, SK_UI_CLASS_TOOLTIP);
	(void)ui->node_set_prop_str(ctx, n, "widget", "tooltip");
	(void)ui->node_set_id(ctx, n, use_id);
	(void)ui->node_set_prop_i32(ctx, n, "open", 0);
	(void)ui->node_set_prop_i32(ctx, n, "hidden", 1);
	(void)ui->node_set_prop_i32(ctx, n, "z_index", 400);
	(void)ui->node_set_pointer_events(ctx, n, SK_UI_POINTER_EVENTS_NONE);
	(void)ui->node_set_focusable(ctx, n, 0);
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	ls.left = sk_ui_pt(0.0f);
	ls.top = sk_ui_pt(0.0f);
	ls.min_width = sk_ui_pt(32.0f);
	ls.min_height = sk_ui_pt(20.0f);
	(void)ui->node_set_layout_style(ctx, n, &ls);

	d = (ui_tooltip_data_t*)ctx->allocator->alloc(ctx->allocator->instance, sizeof(ui_tooltip_data_t));
	if (d == NULL) {
		return n;
	}
	memset(d, 0, sizeof(*d));
	d->host = n;
	d->anchor = SK_UI_NODE_INVALID;
	d->delay = SK_UI_TOOLTIP_DELAY_NORMAL;
	d->hover_sec = 0.0f;
	d->visible = 0;
	d->forced = 0;
	{
		ui_node_slot_t* slot = ui_slot_mut(ctx, n);
		if (slot != NULL) {
			slot->user_data = d;
			slot->user_data_type = SK_UI_TOOLTIP_DATA_TYPE_ID;
		} else {
			ctx->allocator->free(ctx->allocator->instance, d);
		}
	}
	return n;
}

i32 ui_tooltip_get_visible_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_tooltip_data_t* d = tooltip_data_const(ctx, node);
	if (d == NULL) {
		return 0;
	}
	return d->visible != 0 ? 1 : 0;
}

i32 ui_tooltip_set_visible_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 visible) {
	ui_tooltip_data_t* d = tooltip_data(ctx, node);
	if (d == NULL) {
		return -1;
	}
	if (visible != 0) {
		d->forced = 1;
		d->hover_sec = d->delay;
		tooltip_apply_visible(ctx, node, d, 1);
	} else {
		d->forced = 0;
		d->hover_sec = 0.0f;
		tooltip_apply_visible(ctx, node, d, 0);
	}
	return 0;
}

i32 ui_tooltip_set_delay_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 seconds) {
	ui_tooltip_data_t* d = tooltip_data(ctx, node);
	if (d == NULL) {
		return -1;
	}
	d->delay = seconds < 0.0f ? 0.0f : seconds;
	(void)tooltip_api()->node_set_prop_f32(ctx, node, "delay", d->delay);
	return 0;
}

f32 ui_tooltip_get_delay_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_tooltip_data_t* d = tooltip_data_const(ctx, node);
	return d != NULL ? d->delay : 0.0f;
}

i32 ui_tooltip_set_anchor_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t item) {
	ui_tooltip_data_t* d = tooltip_data(ctx, node);
	if (d == NULL) {
		return -1;
	}
	d->anchor = item;
	return 0;
}

sk_ui_node_t ui_tooltip_get_anchor_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_tooltip_data_t* d = tooltip_data_const(ctx, node);
	if (d == NULL) {
		return SK_UI_NODE_INVALID;
	}
	return tooltip_resolve_anchor(ctx, node, d);
}

#ifdef SK_TESTS

static const sk_ui_api_t* wtip_api(void) {
	return ui_get_api_table();
}

static void wtip_layout(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 w, f32 h) {
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, w, h));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
}

static void wtip_move(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x, f32 y) {
	sk_ui_input_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = x;
	ev.y = y;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

static void wtip_pointer(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x, f32 y, i32 button, i32 down) {
	sk_ui_input_event_t ev;
	wtip_move(ui, ctx, x, y);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = x;
	ev.y = y;
	ev.button = button;
	ev.down = down;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

static void wtip_place_abs(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
	sk_ui_style_props_t p;
	sk_ui_layout_style_t ls;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, node, &ls));
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(x);
	ls.top = sk_ui_pt(y);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, node, &ls));
}

SK_TEST(ui_widget_tooltip_hover_delay_and_passthrough) {
	const sk_ui_api_t* ui = wtip_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t item;
	sk_ui_node_t tip;
	sk_ui_node_t line;
	sk_ui_rect_t ir;
	sk_ui_prop_value_t pv;

	item = ui->widget_button(ctx, root, "Asset", "tt-item");
	wtip_place_abs(ui, ctx, item, 16.0f, 16.0f, 80.0f, 24.0f);
	tip = ui->widget_tooltip(ctx, root, "tt-tip");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tip));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, tip, SK_UI_CLASS_TOOLTIP));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, tip, "widget", &pv));
	TEST_ASSERT_EQUAL_STRING("tooltip", pv.data.str_value);
	TEST_ASSERT_EQUAL_INT(SK_UI_POINTER_EVENTS_NONE, ui->node_get_pointer_events(ctx, tip));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, SK_UI_TOOLTIP_DELAY_NORMAL, ui->tooltip_get_delay(ctx, tip));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->tooltip_get_anchor(ctx, tip), item));
	line = ui->widget_text(ctx, tip, "mesh_hero.skmesh", "tt-name");
	(void)ui->widget_text(ctx, tip, "Type  Mesh", "tt-type");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(line));

	wtip_layout(ui, ctx, 320.0f, 200.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_get_visible(ctx, tip));
	TEST_ASSERT_EQUAL_INT(0, ui->node_is_visible(ctx, tip));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, item, &ir, NULL));
	wtip_move(ui, ctx, ir.x + 12.0f, ir.y + 10.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_hovered(ctx, item));
	/* DelayNormal: still closed after a short hover. */
	ui_tooltip_tick_impl(ctx, 0.20f);
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_get_visible(ctx, tip));
	ui_tooltip_tick_impl(ctx, 0.25f);
	TEST_ASSERT_EQUAL_INT(1, ui->tooltip_get_visible(ctx, tip));
	wtip_layout(ui, ctx, 320.0f, 200.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->node_is_visible(ctx, tip));
	TEST_ASSERT_EQUAL_INT(1, ui->node_is_visible(ctx, line));

	/* Leave the item: tooltip closes and the timer resets. */
	wtip_move(ui, ctx, 300.0f, 180.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_get_visible(ctx, tip));
	ui_tooltip_tick_impl(ctx, 0.50f);
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_get_visible(ctx, tip));

	/* Immediate delay: hover shows on the next tick. */
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_set_delay(ctx, tip, 0.0f));
	wtip_move(ui, ctx, ir.x + 12.0f, ir.y + 10.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->tooltip_get_visible(ctx, tip));
	wtip_layout(ui, ctx, 320.0f, 200.0f);

	/* Tooltip must not become the hovered / active item. Click still hits Asset. */
	{
		sk_ui_rect_t tr;
		sk_ui_node_t hit;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tip, &tr, NULL));
		TEST_ASSERT_TRUE(tr.width > 8.0f);
		TEST_ASSERT_TRUE(tr.height > 8.0f);
		wtip_move(ui, ctx, ir.x + 12.0f, ir.y + 10.0f);
		hit = ui->hit_test(ctx, ir.x + 12.0f, ir.y + 10.0f);
		TEST_ASSERT_TRUE(sk_ui_node_eq(hit, item));
		TEST_ASSERT_FALSE(sk_ui_node_eq(hit, tip));
		TEST_ASSERT_EQUAL_INT(1, ui->button_is_hovered(ctx, item));
		TEST_ASSERT_EQUAL_INT(0, (i32)(ui->node_get_state(ctx, tip) & (u32)SK_UI_STATE_HOVER));
		TEST_ASSERT_EQUAL_INT(0, (i32)(ui->node_get_state(ctx, tip) & (u32)SK_UI_STATE_ACTIVE));
		(void)ui->button_clicked(ctx, item);
		wtip_pointer(ui, ctx, ir.x + 12.0f, ir.y + 10.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
		TEST_ASSERT_FALSE(sk_ui_node_eq(ui->pointer_capture_get(ctx), tip));
		TEST_ASSERT_TRUE(sk_ui_node_eq(ctx->active, item) || ui->button_is_active(ctx, item) != 0);
		wtip_pointer(ui, ctx, ir.x + 12.0f, ir.y + 10.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
		TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, item));
		TEST_ASSERT_EQUAL_INT(0, (i32)(ui->node_get_state(ctx, tip) & (u32)SK_UI_STATE_ACTIVE));
	}

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_tooltip_clamps_to_viewport_edges) {
	const sk_ui_api_t* ui = wtip_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t item;
	sk_ui_node_t tip;
	sk_ui_style_props_t p;
	const f32 vw = 200.0f;
	const f32 vh = 160.0f;
	const f32 tw = 90.0f;
	const f32 th = 50.0f;
	struct {
		f32 px;
		f32 py;
	} edges[4];
	u32 e;

	item = ui->widget_button(ctx, root, "Seg", "tt-edge-item");
	wtip_place_abs(ui, ctx, item, 4.0f, 4.0f, 40.0f, 20.0f);
	tip = ui->widget_tooltip(ctx, root, "tt-edge");
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_set_delay(ctx, tip, 0.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_set_visible(ctx, tip, 1));
	(void)ui->widget_text(ctx, tip, "1.24 ms", "tt-edge-text");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.min_width = sk_ui_pt(tw);
	p.layout.min_height = sk_ui_pt(th);
	p.layout.width = sk_ui_pt(tw);
	p.layout.height = sk_ui_pt(th);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, tip, &p));

	edges[0].px = 8.0f;
	edges[0].py = 8.0f; /* top-left */
	edges[1].px = vw - 6.0f;
	edges[1].py = 10.0f; /* top-right */
	edges[2].px = 10.0f;
	edges[2].py = vh - 6.0f; /* bottom-left */
	edges[3].px = vw - 4.0f;
	edges[3].py = vh - 4.0f; /* bottom-right */

	wtip_layout(ui, ctx, vw, vh);
	for (e = 0u; e < 4u; ++e) {
		sk_ui_rect_t r;
		wtip_move(ui, ctx, edges[e].px, edges[e].py);
		TEST_ASSERT_EQUAL_INT(1, ui->tooltip_get_visible(ctx, tip));
		wtip_layout(ui, ctx, vw, vh);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tip, &r, NULL));
		TEST_ASSERT_TRUE(r.width + 0.5f >= 8.0f);
		TEST_ASSERT_TRUE(r.height + 0.5f >= 8.0f);
		TEST_ASSERT_TRUE(r.x + 0.5f >= 0.0f);
		TEST_ASSERT_TRUE(r.y + 0.5f >= 0.0f);
		TEST_ASSERT_TRUE(r.x + r.width <= vw + 0.5f);
		TEST_ASSERT_TRUE(r.y + r.height <= vh + 0.5f);
	}

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
