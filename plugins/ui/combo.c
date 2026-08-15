/**
 * @file combo.c
 * @brief Combo / ListBox family (APX-353; WIDGET_MANIFEST.md §7).
 *
 * Combo(int* + zero-separated items), BeginCombo (preview + custom body),
 * popup below / flip when clipped, keyboard arrow selection, and a list box
 * whose height comes from a visible item count. Item rows reuse §18
 * widget_selectable. Array-backed variants bind sk_ui_item_array_t* via
 * item_bind COMBO / LIST (§21) — no per-frame node churn.
 */

#include "ui.internal.h"

#include "allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SK_TESTS
#include "test.h"
#endif

enum {
	UI_COMBO_KIND_COMBO = 1,
	UI_COMBO_KIND_LIST_BOX = 2,
};

typedef struct ui_combo_data_t {
	u32 kind;
	i32* bound_i32;
	i32 selected;
	i32 highlight;
	i32 max_height_items;
	i32 height_in_items;
	i32 edge_changed;
	i32 flipped;
	i32 preview_custom;
	u32 item_count;
	u32 items_nbytes;
	char* items_blob;
	sk_ui_node_t host;
	sk_ui_node_t popup;
	sk_ui_node_t content;
	sk_ui_node_t items_host;
} ui_combo_data_t;

static const sk_ui_api_t* combo_api(void) {
	return ui_get_api_table();
}

static ui_combo_data_t* combo_data(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL || slot->user_data == NULL) {
		return NULL;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COMBO_DATA_TYPE_ID)) {
		return NULL;
	}
	return (ui_combo_data_t*)slot->user_data;
}

static const ui_combo_data_t* combo_data_const(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return combo_data(SK_CONST_CAST(sk_ui_context_t*, ctx), node);
}

void ui_combo_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot) {
	ui_combo_data_t* d;
	if (slot == NULL || slot->user_data == NULL) {
		return;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COMBO_DATA_TYPE_ID)) {
		return;
	}
	d = (ui_combo_data_t*)slot->user_data;
	if (d->items_blob != NULL) {
		ctx->allocator->free(ctx->allocator->instance, d->items_blob);
		d->items_blob = NULL;
	}
	ctx->allocator->free(ctx->allocator->instance, d);
	slot->user_data = NULL;
	slot->user_data_type = SK_TYPE_ID_ZERO;
}

static void combo_split_label(const_chr_t label, char* visible, u32 vis_cap, const_chr_t* out_id_suffix) {
	const_chr_t hash;
	u32 n;
	if (visible != NULL && vis_cap > 0u) {
		visible[0] = '\0';
	}
	if (out_id_suffix != NULL) {
		*out_id_suffix = NULL;
	}
	if (label == NULL) {
		return;
	}
	hash = strstr(label, "###");
	if (hash == NULL && label[0] == '#' && label[1] == '#' && label[2] != '#') {
		if (out_id_suffix != NULL && label[2] != '\0') {
			*out_id_suffix = label + 2;
		}
		return;
	}
	if (hash == NULL) {
		if (visible != NULL && vis_cap > 0u) {
			n = (u32)strlen(label);
			if (n >= vis_cap) {
				n = vis_cap - 1u;
			}
			memcpy(visible, label, n);
			visible[n] = '\0';
		}
		return;
	}
	if (visible != NULL && vis_cap > 0u) {
		n = (u32)(hash - label);
		if (n >= vis_cap) {
			n = vis_cap - 1u;
		}
		if (n > 0u) {
			memcpy(visible, label, n);
		}
		visible[n] = '\0';
	}
	if (out_id_suffix != NULL && hash[3] != '\0') {
		*out_id_suffix = hash + 3;
	}
}

static u32 combo_cstr_nbytes(const_chr_t items) {
	const char* p;
	if (items == NULL || items[0] == '\0') {
		return 0u;
	}
	p = items;
	while (*p != '\0') {
		p += strlen(p) + 1;
	}
	return (u32)(p - items);
}

static u32 combo_count_blob(const_chr_t items, u32 nbytes) {
	u32 i;
	u32 n = 0u;
	u32 start = 0u;
	if (items == NULL || nbytes == 0u) {
		return 0u;
	}
	for (i = 0u; i < nbytes; ++i) {
		if (items[i] == '\0') {
			n += 1u;
			start = i + 1u;
		}
	}
	if (start < nbytes) {
		n += 1u;
	}
	return n;
}

static const_chr_t combo_blob_at(const_chr_t items, u32 nbytes, u32 index, u32* out_len) {
	u32 i;
	u32 n = 0u;
	u32 start = 0u;
	if (out_len != NULL) {
		*out_len = 0u;
	}
	if (items == NULL || nbytes == 0u) {
		return "";
	}
	for (i = 0u; i < nbytes; ++i) {
		if (items[i] == '\0') {
			if (n == index) {
				if (out_len != NULL) {
					*out_len = i - start;
				}
				return items + start;
			}
			n += 1u;
			start = i + 1u;
		}
	}
	if (start < nbytes && n == index) {
		if (out_len != NULL) {
			*out_len = nbytes - start;
		}
		return items + start;
	}
	return "";
}

static i32 combo_visible_rows(const ui_combo_data_t* d) {
	i32 n;
	i32 max_n;
	if (d == NULL) {
		return SK_UI_COMBO_DEFAULT_HEIGHT_IN_ITEMS;
	}
	n = (i32)d->item_count;
	max_n = d->max_height_items;
	if (max_n < 0) {
		max_n = SK_UI_COMBO_DEFAULT_HEIGHT_IN_ITEMS;
	}
	if (max_n == 0) {
		return n > 0 ? n : 1;
	}
	if (n > max_n) {
		n = max_n;
	}
	if (n < 1) {
		n = 1;
	}
	return n;
}

static void combo_apply_popup_height(sk_ui_context_t* ctx, ui_combo_data_t* d) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_style_props_t p;
	f32 h;
	if (d == NULL || !sk_ui_node_is_valid(d->popup)) {
		return;
	}
	h = (f32)combo_visible_rows(d) * SK_UI_COMBO_ITEM_HEIGHT + 4.0f;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH;
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_height = sk_ui_pt(h);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(120.0f);
	(void)ui->node_merge_inline_style(ctx, d->popup, &p);
}

static void combo_apply_list_height(sk_ui_context_t* ctx, ui_combo_data_t* d) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_style_props_t p;
	i32 n;
	f32 h;
	if (d == NULL || d->kind != (u32)UI_COMBO_KIND_LIST_BOX) {
		return;
	}
	n = d->height_in_items;
	if (n < 0) {
		n = SK_UI_COMBO_DEFAULT_HEIGHT_IN_ITEMS;
	}
	if (n == 0) {
		n = SK_UI_COMBO_DEFAULT_HEIGHT_IN_ITEMS;
	}
	h = (f32)n * SK_UI_COMBO_ITEM_HEIGHT;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT;
	p.layout.height = sk_ui_pt(h);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_height = sk_ui_pt(h);
	(void)ui->node_merge_inline_style(ctx, d->host, &p);
	(void)ui->node_set_prop_i32(ctx, d->host, "height_in_items", n);
	(void)ui->node_set_prop_f32(ctx, d->host, "list_height", h);
}

static sk_ui_node_t combo_make_popup(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n;
	char auto_id[80];
	const_chr_t popup_id = id;

	if (id == NULL || id[0] == '\0') {
		ctx->widget_id_seq += 1u;
		(void)snprintf(auto_id, sizeof(auto_id), "combo-popup-%u", ctx->widget_id_seq);
		popup_id = auto_id;
	}
	n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_add_class(ctx, n, SK_UI_CLASS_COMBO_ITEMS);
	(void)ui->node_set_prop_str(ctx, n, "widget", "menu_popup");
	(void)ui->node_set_id(ctx, n, popup_id);
	(void)ui->node_set_prop_i32(ctx, n, "open", 0);
	(void)ui->node_set_prop_i32(ctx, n, "hidden", 1);
	(void)ui->node_set_prop_i32(ctx, n, "attach", 0);
	(void)ui->node_set_prop_i32(ctx, n, "z_index", 120);
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	ls.min_width = sk_ui_pt(120.0f);
	ls.min_height = sk_ui_pt(SK_UI_COMBO_ITEM_HEIGHT);
	ls.left = sk_ui_pt(0.0f);
	ls.top = sk_ui_pt(0.0f);
	(void)ui->node_set_layout_style(ctx, n, &ls);
	{
		sk_ui_style_props_t p;
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING;
		p.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
		p.border_color = sk_ui_rgba(0.38f, 0.40f, 0.46f, 1.0f);
		p.layout.border.left = 1.0f;
		p.layout.border.top = 1.0f;
		p.layout.border.right = 1.0f;
		p.layout.border.bottom = 1.0f;
		p.corner_radius = 3.0f;
		p.layout.padding.left = 2.0f;
		p.layout.padding.right = 2.0f;
		p.layout.padding.top = 2.0f;
		p.layout.padding.bottom = 2.0f;
		(void)ui->node_merge_inline_style(ctx, n, &p);
	}
	return n;
}

static void combo_destroy_owned_rows(sk_ui_context_t* ctx, ui_combo_data_t* d) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_node_t parent;
	u32 i;
	if (d == NULL || !sk_ui_node_is_valid(d->popup)) {
		return;
	}
	parent = d->popup;
	i = ui->node_child_count(ctx, parent);
	while (i > 0u) {
		sk_ui_node_t ch;
		sk_ui_prop_value_t pv;
		i -= 1u;
		ch = ui->node_child_at(ctx, parent, i);
		if (ui->node_get_prop(ctx, ch, "combo_owned", &pv) == 0 && pv.type == SK_UI_PROP_I32 && pv.data.i32_value != 0) {
			(void)ui->node_destroy(ctx, ch);
		}
	}
}

static void combo_refresh_preview(sk_ui_context_t* ctx, ui_combo_data_t* d) {
	const sk_ui_api_t* ui = combo_api();
	const_chr_t preview;
	if (d == NULL) {
		return;
	}
	if (d->preview_custom != 0) {
		return;
	}
	if (d->selected >= 0 && (u32)d->selected < d->item_count) {
		preview = combo_blob_at(d->items_blob, d->items_nbytes, (u32)d->selected, NULL);
	} else {
		preview = "";
	}
	(void)ui->node_set_prop_str(ctx, d->host, "text", preview != NULL ? preview : "");
}

static void combo_refresh_row_selected(sk_ui_context_t* ctx, ui_combo_data_t* d) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_node_t parent;
	u32 i;
	u32 n;
	if (d == NULL || !sk_ui_node_is_valid(d->popup)) {
		return;
	}
	parent = d->popup;
	n = ui->node_child_count(ctx, parent);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t ch = ui->node_child_at(ctx, parent, i);
		sk_ui_prop_value_t pv;
		i32 idx = -1;
		if (ui->node_get_prop(ctx, ch, "combo_index", &pv) == 0 && pv.type == SK_UI_PROP_I32) {
			idx = pv.data.i32_value;
		}
		if (idx < 0) {
			continue;
		}
		(void)ui->selectable_set_selected(ctx, ch, idx == d->selected ? 1 : 0);
		if (idx == d->highlight) {
			u32 st = ui->node_get_state(ctx, ch);
			(void)ui->node_set_state(ctx, ch, st | (u32)SK_UI_STATE_HOVER);
		} else {
			u32 st = ui->node_get_state(ctx, ch);
			(void)ui->node_set_state(ctx, ch, st & ~(u32)SK_UI_STATE_HOVER);
		}
	}
}

static void combo_rebuild_rows(sk_ui_context_t* ctx, ui_combo_data_t* d) {
	const sk_ui_api_t* ui = combo_api();
	const_chr_t host_id;
	u32 i;
	if (d == NULL || !sk_ui_node_is_valid(d->popup)) {
		return;
	}
	combo_destroy_owned_rows(ctx, d);
	host_id = ui->node_get_id(ctx, d->host);
	if (host_id == NULL) {
		host_id = "combo";
	}
	for (i = 0u; i < d->item_count; ++i) {
		char idbuf[96];
		const_chr_t text = combo_blob_at(d->items_blob, d->items_nbytes, i, NULL);
		sk_ui_node_t row;
		(void)snprintf(idbuf, sizeof(idbuf), "%s/i%u", host_id, i);
		row = ui->widget_selectable(ctx, d->popup, text, (i32)i == d->selected ? 1 : 0, SK_UI_SELECTABLE_FLAG_SPAN_AVAIL_WIDTH, idbuf, 0.0f, 0.0f);
		if (!sk_ui_node_is_valid(row)) {
			continue;
		}
		(void)ui->node_set_prop_i32(ctx, row, "combo_owned", 1);
		(void)ui->node_set_prop_i32(ctx, row, "combo_index", (i32)i);
		(void)ui->selectable_set_size(ctx, row, 160.0f, SK_UI_COMBO_ITEM_HEIGHT);
	}
	combo_apply_popup_height(ctx, d);
	combo_refresh_preview(ctx, d);
	combo_refresh_row_selected(ctx, d);
}

static void combo_apply_index(sk_ui_context_t* ctx, ui_combo_data_t* d, i32 index, i32 from_user) {
	i32 prev;
	if (d == NULL) {
		return;
	}
	prev = d->selected;
	d->selected = index;
	d->highlight = index;
	if (d->bound_i32 != NULL) {
		*d->bound_i32 = index;
	}
	if (from_user != 0 && index != prev) {
		d->edge_changed = 1;
	}
	combo_refresh_preview(ctx, d);
	combo_refresh_row_selected(ctx, d);
	ui_mark_dirty_up(ctx, d->host, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
}

static i32 combo_index_of_node(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_node_t cur = node;
	while (sk_ui_node_is_valid(cur)) {
		sk_ui_prop_value_t pv;
		if (ui->node_get_prop(ctx, cur, "combo_index", &pv) == 0 && pv.type == SK_UI_PROP_I32) {
			return pv.data.i32_value;
		}
		cur = ui->node_parent(ctx, cur);
	}
	return -1;
}

static i32 combo_node_in_popup(const sk_ui_context_t* ctx, ui_combo_data_t* d, sk_ui_node_t node) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_node_t cur = node;
	if (d == NULL || !sk_ui_node_is_valid(d->popup)) {
		return 0;
	}
	while (sk_ui_node_is_valid(cur)) {
		if (sk_ui_node_eq(cur, d->popup)) {
			return 1;
		}
		cur = ui->node_parent(ctx, cur);
	}
	return 0;
}

static void combo_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = combo_api();
	ui_combo_data_t* d = (ui_combo_data_t*)user;
	i32 open;
	if (d == NULL || event == NULL) {
		return;
	}
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	open = ui_combo_get_open_impl(ctx, node);

	if (event->type == SK_UI_EVENT_POINTER_DOWN || event->type == SK_UI_EVENT_CLICK) {
		if (sk_ui_node_eq(event->target, node)) {
			if (event->type == SK_UI_EVENT_POINTER_DOWN) {
				(void)ui_combo_set_open_impl(ctx, node, open != 0 ? 0 : 1);
				event->consumed = 1;
			}
			return;
		}
		if (combo_node_in_popup(ctx, d, event->target) != 0) {
			i32 idx = combo_index_of_node(ctx, event->target);
			if (idx >= 0) {
				combo_apply_index(ctx, d, idx, 1);
			}
			(void)ui_combo_set_open_impl(ctx, node, 0);
			event->consumed = 1;
		}
		return;
	}

	if (event->type != SK_UI_EVENT_KEY_DOWN || event->down == 0) {
		return;
	}
	if (event->key == SK_UI_KEY_ESCAPE && open != 0) {
		(void)ui_combo_set_open_impl(ctx, node, 0);
		event->consumed = 1;
		return;
	}
	if (open == 0) {
		if (event->key == SK_UI_KEY_DOWN || event->key == SK_UI_KEY_ENTER || event->key == SK_UI_KEY_SPACE) {
			(void)ui_combo_set_open_impl(ctx, node, 1);
			event->consumed = 1;
		}
		return;
	}
	if (event->key == SK_UI_KEY_DOWN || event->key == SK_UI_KEY_UP) {
		i32 count = (i32)d->item_count;
		i32 next = d->highlight;
		if (count <= 0) {
			return;
		}
		if (next < 0 || next >= count) {
			next = 0;
		} else if (event->key == SK_UI_KEY_DOWN) {
			next = (next + 1) % count;
		} else {
			next = (next - 1 + count) % count;
		}
		d->highlight = next;
		combo_refresh_row_selected(ctx, d);
		event->consumed = 1;
		return;
	}
	if (event->key == SK_UI_KEY_ENTER || event->key == SK_UI_KEY_SPACE) {
		if (d->highlight >= 0 && (u32)d->highlight < d->item_count) {
			combo_apply_index(ctx, d, d->highlight, 1);
		}
		(void)ui_combo_set_open_impl(ctx, node, 0);
		event->consumed = 1;
	}
}

static ui_combo_data_t* combo_ensure(sk_ui_context_t* ctx, sk_ui_node_t node, u32 kind) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	ui_combo_data_t* d;
	if (slot == NULL) {
		return NULL;
	}
	if (slot->user_data != NULL && SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COMBO_DATA_TYPE_ID)) {
		d = (ui_combo_data_t*)slot->user_data;
		d->kind = kind;
		d->host = node;
		return d;
	}
	if (slot->user_data != NULL) {
		return NULL;
	}
	d = (ui_combo_data_t*)ctx->allocator->alloc(ctx->allocator->instance, sizeof(ui_combo_data_t));
	if (d == NULL) {
		return NULL;
	}
	memset(d, 0, sizeof(*d));
	d->kind = kind;
	d->host = node;
	d->selected = -1;
	d->highlight = -1;
	d->max_height_items = -1;
	d->height_in_items = SK_UI_COMBO_DEFAULT_HEIGHT_IN_ITEMS;
	d->popup = SK_UI_NODE_INVALID;
	d->content = SK_UI_NODE_INVALID;
	d->items_host = SK_UI_NODE_INVALID;
	slot->user_data = d;
	slot->user_data_type = SK_UI_COMBO_DATA_TYPE_ID;
	return d;
}

static sk_ui_node_t combo_make_host(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t preview, const_chr_t id) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_node_callbacks_t cbs;
	ui_combo_data_t* d;
	sk_ui_node_t n;
	char visible[256];
	const_chr_t id_suffix = NULL;
	const_chr_t use_id = id;
	char popup_id[80];
	char auto_id[64];

	combo_split_label(label, visible, (u32)sizeof(visible), &id_suffix);
	if ((use_id == NULL || use_id[0] == '\0') && id_suffix != NULL && id_suffix[0] != '\0') {
		use_id = id_suffix;
	}

	n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_add_class(ctx, n, SK_UI_CLASS_COMBO);
	(void)ui->node_set_prop_str(ctx, n, "widget", "combo");
	if (use_id != NULL && use_id[0] != '\0') {
		(void)ui->node_set_id(ctx, n, use_id);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(auto_id, sizeof(auto_id), "combo-%u", ctx->widget_id_seq);
		(void)ui->node_set_id(ctx, n, auto_id);
		use_id = ui->node_get_id(ctx, n);
	}
	(void)ui->node_set_prop_str(ctx, n, "text", preview != NULL ? preview : "");
	(void)ui->node_set_prop_str(ctx, n, "label", visible);
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "open", 0);
	(void)ui->node_set_focusable(ctx, n, 1);
	{
		sk_ui_style_props_t box;
		memset(&box, 0, sizeof(box));
		box.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT;
		box.layout.width = sk_ui_pt(160.0f);
		box.layout.height = sk_ui_pt(24.0f);
		box.layout.min_width = sk_ui_pt(120.0f);
		box.layout.min_height = sk_ui_pt(24.0f);
		box.layout.max_height = sk_ui_pt(24.0f);
		(void)ui->node_merge_inline_style(ctx, n, &box);
	}
	if (ctx->disabled_active > 0u) {
		(void)ui->node_set_state(ctx, n, ui->node_get_state(ctx, n) | (u32)SK_UI_STATE_DISABLED);
	}
	if (ctx->has_next_item_width != 0u) {
		sk_ui_style_props_t p;
		f32 w = ctx->next_item_width;
		ctx->has_next_item_width = 0u;
		memset(&p, 0, sizeof(p));
		if (w < 0.0f) {
			p.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW;
			p.layout.width = sk_ui_percent(100.0f);
			p.layout.flex_grow = 1.0f;
		} else {
			p.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH;
			p.layout.width = sk_ui_pt(w);
			p.layout.min_width = sk_ui_pt(w);
		}
		(void)ui->node_merge_inline_style(ctx, n, &p);
	}

	d = combo_ensure(ctx, n, UI_COMBO_KIND_COMBO);
	if (d == NULL) {
		(void)ui->node_destroy(ctx, n);
		return SK_UI_NODE_INVALID;
	}
	(void)snprintf(popup_id, sizeof(popup_id), "%s-popup", use_id != NULL ? use_id : "combo");
	d->popup = combo_make_popup(ctx, n, popup_id);
	d->content = d->popup;
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = combo_on_event;
	cbs.user = d;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

sk_ui_node_t ui_widget_combo_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, i32* current_item, const_chr_t items_separated_by_zeros,
								  i32 popup_max_height_in_items, const_chr_t id) {
	sk_ui_node_t n = combo_make_host(ctx, parent, label, "", id);
	ui_combo_data_t* d;
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	d = combo_data(ctx, n);
	if (d == NULL) {
		return n;
	}
	d->max_height_items = popup_max_height_in_items;
	d->bound_i32 = current_item;
	if (current_item != NULL) {
		d->selected = *current_item;
		d->highlight = *current_item;
	}
	(void)ui_combo_set_items_impl(ctx, n, items_separated_by_zeros);
	return n;
}

sk_ui_node_t ui_widget_begin_combo_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t preview_value, u32 flags, const_chr_t id) {
	sk_ui_node_t n = combo_make_host(ctx, parent, label, preview_value != NULL ? preview_value : "", id);
	ui_combo_data_t* d;
	(void)flags;
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	d = combo_data(ctx, n);
	if (d != NULL) {
		d->preview_custom = 1;
		combo_apply_popup_height(ctx, d);
	}
	return n;
}

sk_ui_node_t ui_combo_popup_impl(const sk_ui_context_t* ctx, sk_ui_node_t combo) {
	const ui_combo_data_t* d = combo_data_const(ctx, combo);
	if (d != NULL && sk_ui_node_is_valid(d->popup)) {
		return d->popup;
	}
	return combo_api()->menu_get_popup(ctx, combo);
}

i32 ui_combo_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t combo, i32 open) {
	const sk_ui_api_t* ui = combo_api();
	ui_combo_data_t* d = combo_data(ctx, combo);
	if (d == NULL) {
		return ui->menu_set_open(ctx, combo, open);
	}
	if (open != 0 && (ui->node_get_state(ctx, combo) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return 0;
	}
	if (open != 0) {
		d->highlight = d->selected;
		combo_refresh_row_selected(ctx, d);
	} else {
		d->flipped = 0;
		(void)ui->node_set_prop_i32(ctx, combo, "flipped", 0);
	}
	return ui->menu_set_open(ctx, combo, open);
}

i32 ui_combo_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t combo) {
	return combo_api()->menu_get_open(ctx, combo);
}

i32 ui_combo_bind_impl(sk_ui_context_t* ctx, sk_ui_node_t combo, i32* current_item) {
	ui_combo_data_t* d = combo_data(ctx, combo);
	if (d == NULL || d->kind != (u32)UI_COMBO_KIND_COMBO) {
		return -1;
	}
	d->bound_i32 = current_item;
	if (current_item != NULL) {
		combo_apply_index(ctx, d, *current_item, 0);
	}
	return 0;
}

i32 ui_combo_set_items_n_impl(sk_ui_context_t* ctx, sk_ui_node_t combo, const_chr_t items, u32 nbytes) {
	ui_combo_data_t* d = combo_data(ctx, combo);
	char* copy = NULL;
	if (d == NULL || d->kind != (u32)UI_COMBO_KIND_COMBO) {
		return -1;
	}
	if (d->items_blob != NULL) {
		ctx->allocator->free(ctx->allocator->instance, d->items_blob);
		d->items_blob = NULL;
	}
	d->items_nbytes = 0u;
	d->item_count = 0u;
	if (items != NULL && nbytes > 0u) {
		copy = (char*)ctx->allocator->alloc(ctx->allocator->instance, nbytes + 1u);
		if (copy == NULL) {
			return -1;
		}
		memcpy(copy, items, nbytes);
		copy[nbytes] = '\0';
		d->items_blob = copy;
		d->items_nbytes = nbytes;
		d->item_count = combo_count_blob(copy, nbytes);
	}
	combo_rebuild_rows(ctx, d);
	return 0;
}

i32 ui_combo_set_items_impl(sk_ui_context_t* ctx, sk_ui_node_t combo, const_chr_t items_separated_by_zeros) {
	return ui_combo_set_items_n_impl(ctx, combo, items_separated_by_zeros, combo_cstr_nbytes(items_separated_by_zeros));
}

u32 ui_combo_item_count_impl(const sk_ui_context_t* ctx, sk_ui_node_t combo) {
	const ui_combo_data_t* d = combo_data_const(ctx, combo);
	return d != NULL ? d->item_count : 0u;
}

const_chr_t ui_combo_item_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t combo, i32 index) {
	const ui_combo_data_t* d = combo_data_const(ctx, combo);
	if (d == NULL || index < 0 || (u32)index >= d->item_count) {
		return "";
	}
	return combo_blob_at(d->items_blob, d->items_nbytes, (u32)index, NULL);
}

sk_ui_node_t ui_combo_item_at_impl(const sk_ui_context_t* ctx, sk_ui_node_t combo, i32 index) {
	const sk_ui_api_t* ui = combo_api();
	const ui_combo_data_t* d = combo_data_const(ctx, combo);
	sk_ui_node_t parent;
	u32 i;
	u32 n;
	if (d == NULL || !sk_ui_node_is_valid(d->popup) || index < 0) {
		return SK_UI_NODE_INVALID;
	}
	parent = d->popup;
	n = ui->node_child_count(ctx, parent);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t ch = ui->node_child_at(ctx, parent, i);
		sk_ui_prop_value_t pv;
		if (ui->node_get_prop(ctx, ch, "combo_index", &pv) == 0 && pv.type == SK_UI_PROP_I32 && pv.data.i32_value == index) {
			return ch;
		}
	}
	return SK_UI_NODE_INVALID;
}

i32 ui_combo_get_selected_impl(const sk_ui_context_t* ctx, sk_ui_node_t combo) {
	const ui_combo_data_t* d = combo_data_const(ctx, combo);
	return d != NULL ? d->selected : -1;
}

i32 ui_combo_set_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t combo, i32 index) {
	ui_combo_data_t* d = combo_data(ctx, combo);
	if (d == NULL) {
		return -1;
	}
	combo_apply_index(ctx, d, index, 0);
	return 0;
}

i32 ui_combo_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t combo) {
	ui_combo_data_t* d = combo_data(ctx, combo);
	i32 v;
	if (d == NULL) {
		return 0;
	}
	v = d->edge_changed != 0 ? 1 : 0;
	d->edge_changed = 0;
	return v;
}

i32 ui_combo_set_preview_impl(sk_ui_context_t* ctx, sk_ui_node_t combo, const_chr_t preview) {
	const sk_ui_api_t* ui = combo_api();
	ui_combo_data_t* d = combo_data(ctx, combo);
	if (d == NULL) {
		return -1;
	}
	d->preview_custom = 1;
	return ui->node_set_prop_str(ctx, combo, "text", preview != NULL ? preview : "");
}

const_chr_t ui_combo_get_preview_impl(const sk_ui_context_t* ctx, sk_ui_node_t combo) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_prop_value_t pv;
	if (ui->node_get_prop(ctx, combo, "text", &pv) == 0 && pv.type == SK_UI_PROP_STR && pv.data.str_value != NULL) {
		return pv.data.str_value;
	}
	return "";
}

i32 ui_combo_set_max_height_in_items_impl(sk_ui_context_t* ctx, sk_ui_node_t combo, i32 n) {
	ui_combo_data_t* d = combo_data(ctx, combo);
	if (d == NULL) {
		return -1;
	}
	d->max_height_items = n;
	combo_apply_popup_height(ctx, d);
	return 0;
}

i32 ui_combo_get_popup_flipped_impl(const sk_ui_context_t* ctx, sk_ui_node_t combo) {
	const ui_combo_data_t* d = combo_data_const(ctx, combo);
	return d != NULL && d->flipped != 0 ? 1 : 0;
}

i32 ui_combo_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t combo, i32 disabled) {
	const sk_ui_api_t* ui = combo_api();
	u32 st;
	if (combo_data(ctx, combo) == NULL) {
		return -1;
	}
	st = ui->node_get_state(ctx, combo);
	if (disabled != 0) {
		(void)ui_combo_set_open_impl(ctx, combo, 0);
		st |= (u32)SK_UI_STATE_DISABLED;
	} else {
		st &= ~(u32)SK_UI_STATE_DISABLED;
	}
	return ui->node_set_state(ctx, combo, st);
}

i32 ui_combo_bind_items_impl(sk_ui_context_t* ctx, sk_ui_node_t combo, sk_ui_item_array_t* items) {
	const sk_ui_api_t* ui = combo_api();
	ui_combo_data_t* d = combo_data(ctx, combo);
	sk_ui_node_t host;
	if (d == NULL || !sk_ui_node_is_valid(d->popup)) {
		return -1;
	}
	combo_destroy_owned_rows(ctx, d);
	host = d->items_host;
	if (!sk_ui_node_is_valid(host) || !ui->node_alive(ctx, host)) {
		char idbuf[80];
		const_chr_t cid = ui->node_get_id(ctx, combo);
		(void)snprintf(idbuf, sizeof(idbuf), "%s-items", cid != NULL ? cid : "combo");
		host = ui->widget_item_view(ctx, d->popup, items, SK_UI_ITEM_BIND_COMBO, idbuf);
		d->items_host = host;
		return sk_ui_node_is_valid(host) ? 0 : -1;
	}
	return ui->item_bind(ctx, host, items, SK_UI_ITEM_BIND_COMBO);
}

sk_ui_node_t ui_widget_list_box_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, f32 width, f32 height, i32 height_in_items, const_chr_t id) {
	const sk_ui_api_t* ui = combo_api();
	ui_combo_data_t* d;
	sk_ui_node_t n;
	sk_ui_node_t content;
	char visible[256];
	const_chr_t id_suffix = NULL;
	const_chr_t use_id = id;
	char auto_id[64];
	char content_id[80];
	sk_ui_style_props_t p;

	combo_split_label(label, visible, (u32)sizeof(visible), &id_suffix);
	if ((use_id == NULL || use_id[0] == '\0') && id_suffix != NULL && id_suffix[0] != '\0') {
		use_id = id_suffix;
	}

	n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_add_class(ctx, n, SK_UI_CLASS_LIST_BOX);
	(void)ui->node_set_prop_str(ctx, n, "widget", "list_box");
	if (use_id != NULL && use_id[0] != '\0') {
		(void)ui->node_set_id(ctx, n, use_id);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(auto_id, sizeof(auto_id), "listbox-%u", ctx->widget_id_seq);
		(void)ui->node_set_id(ctx, n, auto_id);
		use_id = ui->node_get_id(ctx, n);
	}
	(void)ui->node_set_prop_str(ctx, n, "label", visible);
	{
		ui_node_slot_t* slot = ui_slot_mut(ctx, n);
		if (slot != NULL) {
			slot->clip_children = 1u;
		}
	}

	d = combo_ensure(ctx, n, UI_COMBO_KIND_LIST_BOX);
	if (d == NULL) {
		(void)ui->node_destroy(ctx, n);
		return SK_UI_NODE_INVALID;
	}
	d->height_in_items = height_in_items;
	if (d->height_in_items == 0 && height <= 0.0f) {
		d->height_in_items = SK_UI_COMBO_DEFAULT_HEIGHT_IN_ITEMS;
	}

	(void)snprintf(content_id, sizeof(content_id), "%s-content", use_id != NULL ? use_id : "listbox");
	content = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, n);
	(void)ui->node_add_class(ctx, content, SK_UI_CLASS_LIST);
	(void)ui->node_set_prop_str(ctx, content, "widget", "list_box_content");
	(void)ui->node_set_id(ctx, content, content_id);
	d->content = content;
	d->popup = SK_UI_NODE_INVALID;

	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.align_items = SK_UI_ALIGN_STRETCH;
	p.layout.width = sk_ui_percent(100.0f);
	(void)ui->node_merge_inline_style(ctx, content, &p);

	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.align_items = SK_UI_ALIGN_STRETCH;
	if (width > 0.5f) {
		p.layout.width = sk_ui_pt(width);
	} else {
		p.layout.width = sk_ui_percent(100.0f);
	}
	(void)ui->node_merge_inline_style(ctx, n, &p);

	if (height > 0.5f) {
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT;
		p.layout.height = sk_ui_pt(height);
		p.layout.min_height = sk_ui_pt(height);
		p.layout.max_height = sk_ui_pt(height);
		(void)ui->node_merge_inline_style(ctx, n, &p);
		(void)ui->node_set_prop_f32(ctx, n, "list_height", height);
		if (height_in_items <= 0) {
			d->height_in_items = (i32)(height / SK_UI_COMBO_ITEM_HEIGHT + 0.5f);
			if (d->height_in_items < 1) {
				d->height_in_items = 1;
			}
		}
		(void)ui->node_set_prop_i32(ctx, n, "height_in_items", d->height_in_items);
	} else {
		combo_apply_list_height(ctx, d);
	}
	return n;
}

sk_ui_node_t ui_list_box_content_impl(const sk_ui_context_t* ctx, sk_ui_node_t list_box) {
	const ui_combo_data_t* d = combo_data_const(ctx, list_box);
	return d != NULL ? d->content : SK_UI_NODE_INVALID;
}

i32 ui_list_box_set_height_in_items_impl(sk_ui_context_t* ctx, sk_ui_node_t list_box, i32 n) {
	ui_combo_data_t* d = combo_data(ctx, list_box);
	if (d == NULL || d->kind != (u32)UI_COMBO_KIND_LIST_BOX) {
		return -1;
	}
	d->height_in_items = n;
	combo_apply_list_height(ctx, d);
	return 0;
}

i32 ui_list_box_get_height_in_items_impl(const sk_ui_context_t* ctx, sk_ui_node_t list_box) {
	const ui_combo_data_t* d = combo_data_const(ctx, list_box);
	if (d == NULL || d->kind != (u32)UI_COMBO_KIND_LIST_BOX) {
		return 0;
	}
	return d->height_in_items;
}

f32 ui_list_box_get_height_impl(const sk_ui_context_t* ctx, sk_ui_node_t list_box) {
	const sk_ui_api_t* ui = combo_api();
	sk_ui_prop_value_t pv;
	sk_ui_layout_style_t ls;
	if (ui->node_get_prop(ctx, list_box, "list_height", &pv) == 0 && pv.type == SK_UI_PROP_F32) {
		return pv.data.f32_value;
	}
	if (ui->node_get_layout_style(ctx, list_box, &ls) == 0 && ls.height.unit == SK_UI_LENGTH_POINT) {
		return ls.height.value;
	}
	return 0.0f;
}

i32 ui_list_box_bind_items_impl(sk_ui_context_t* ctx, sk_ui_node_t list_box, sk_ui_item_array_t* items) {
	const sk_ui_api_t* ui = combo_api();
	ui_combo_data_t* d = combo_data(ctx, list_box);
	sk_ui_node_t host;
	if (d == NULL || d->kind != (u32)UI_COMBO_KIND_LIST_BOX || !sk_ui_node_is_valid(d->content)) {
		return -1;
	}
	host = d->items_host;
	if (!sk_ui_node_is_valid(host) || !ui->node_alive(ctx, host)) {
		char idbuf[80];
		const_chr_t lid = ui->node_get_id(ctx, list_box);
		(void)snprintf(idbuf, sizeof(idbuf), "%s-items", lid != NULL ? lid : "listbox");
		host = ui->widget_list(ctx, d->content, items, idbuf);
		d->items_host = host;
		return sk_ui_node_is_valid(host) ? 0 : -1;
	}
	return ui->item_bind(ctx, host, items, SK_UI_ITEM_BIND_LIST);
}

void ui_combo_sync_all(sk_ui_context_t* ctx) {
	u32 i;
	if (ctx == NULL) {
		return;
	}
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		ui_combo_data_t* d;
		if (slot->alive == 0u || slot->user_data == NULL) {
			continue;
		}
		if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COMBO_DATA_TYPE_ID)) {
			continue;
		}
		d = (ui_combo_data_t*)slot->user_data;
		if (d->kind != (u32)UI_COMBO_KIND_COMBO || d->bound_i32 == NULL) {
			continue;
		}
		if (*d->bound_i32 != d->selected) {
			combo_apply_index(ctx, d, *d->bound_i32, 0);
		}
	}
}

void ui_combo_place_popups(sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = combo_api();
	u32 i;
	if (ctx == NULL) {
		return;
	}
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		ui_combo_data_t* d;
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
		if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COMBO_DATA_TYPE_ID)) {
			continue;
		}
		d = (ui_combo_data_t*)slot->user_data;
		if (d->kind != (u32)UI_COMBO_KIND_COMBO || !sk_ui_node_is_valid(d->popup)) {
			continue;
		}
		node.index = i;
		node.generation = slot->generation;
		if (ui_combo_get_open_impl(ctx, node) == 0) {
			d->flipped = 0;
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
			d->flipped = need_flip;
			(void)ui->node_set_prop_i32(ctx, node, "flipped", need_flip);
			(void)ui->node_set_prop_i32(ctx, d->popup, "attach", need_flip != 0 ? 2 : 0);
			continue;
		}
		ps = ui_slot_mut(ctx, d->popup);
		if (ps == NULL) {
			continue;
		}
		ps->layout_border.y += delta;
		ps->layout_content.y += delta;
		d->flipped = need_flip;
		(void)ui->node_set_prop_i32(ctx, node, "flipped", need_flip);
		(void)ui->node_set_prop_i32(ctx, d->popup, "attach", need_flip != 0 ? 2 : 0);
	}
}

#ifdef SK_TESTS

static const sk_ui_api_t* wcombo_api(void) {
	return ui_get_api_table();
}

static void wcombo_layout(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 w, f32 h) {
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, w, h));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
}

static void wcombo_pointer(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x, f32 y, i32 button, i32 down) {
	sk_ui_input_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = x;
	ev.y = y;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = x;
	ev.y = y;
	ev.button = button;
	ev.down = down;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

SK_TEST(ui_combo_parse_zero_separated_empty_trailing) {
	const sk_ui_api_t* ui = wcombo_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t c;
	i32 idx = 0;
	char empty_mid[] = {'A', '\0', '\0', 'B', '\0'};
	char trailing[] = {'A', '\0', 'B', '\0', '\0'};

	c = ui->widget_combo(ctx, root, "##p", &idx, "", -1, "parse-empty");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(c));
	TEST_ASSERT_EQUAL_UINT(0u, ui->combo_item_count(ctx, c));
	TEST_ASSERT_EQUAL_STRING("", ui->combo_item_text(ctx, c, 0));

	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_items(ctx, c, NULL));
	TEST_ASSERT_EQUAL_UINT(0u, ui->combo_item_count(ctx, c));

	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_items(ctx, c, "Default Lit\0Unlit\0"));
	TEST_ASSERT_EQUAL_UINT(2u, ui->combo_item_count(ctx, c));
	TEST_ASSERT_EQUAL_STRING("Default Lit", ui->combo_item_text(ctx, c, 0));
	TEST_ASSERT_EQUAL_STRING("Unlit", ui->combo_item_text(ctx, c, 1));
	TEST_ASSERT_EQUAL_STRING("", ui->combo_item_text(ctx, c, 2));

	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_items(ctx, c, "A\0B\0C\0"));
	TEST_ASSERT_EQUAL_UINT(3u, ui->combo_item_count(ctx, c));
	TEST_ASSERT_EQUAL_STRING("C", ui->combo_item_text(ctx, c, 2));

	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_items_n(ctx, c, empty_mid, (u32)sizeof(empty_mid)));
	TEST_ASSERT_EQUAL_UINT(3u, ui->combo_item_count(ctx, c));
	TEST_ASSERT_EQUAL_STRING("A", ui->combo_item_text(ctx, c, 0));
	TEST_ASSERT_EQUAL_STRING("", ui->combo_item_text(ctx, c, 1));
	TEST_ASSERT_EQUAL_STRING("B", ui->combo_item_text(ctx, c, 2));

	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_items_n(ctx, c, trailing, (u32)sizeof(trailing)));
	TEST_ASSERT_EQUAL_UINT(3u, ui->combo_item_count(ctx, c));
	TEST_ASSERT_EQUAL_STRING("A", ui->combo_item_text(ctx, c, 0));
	TEST_ASSERT_EQUAL_STRING("B", ui->combo_item_text(ctx, c, 1));
	TEST_ASSERT_EQUAL_STRING("", ui->combo_item_text(ctx, c, 2));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_combo_bind_writeback_changed) {
	const sk_ui_api_t* ui = wcombo_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t c;
	sk_ui_node_t row;
	sk_ui_rect_t r;
	i32 shading = 0;

	c = ui->widget_combo(ctx, root, "##shadingmodel", &shading, "Default Lit\0Unlit\0", -1, "shade");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(c));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, c, SK_UI_CLASS_COMBO));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_selected(ctx, c));
	TEST_ASSERT_EQUAL_STRING("Default Lit", ui->combo_get_preview(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_changed(ctx, c));

	wcombo_layout(ui, ctx, 320.0f, 240.0f);
	{
		sk_ui_rect_t cr;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, c, &cr, NULL));
		TEST_ASSERT_TRUE(cr.width > 8.0f);
		TEST_ASSERT_TRUE(cr.height > 8.0f);
	}
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_open(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, c, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->combo_get_open(ctx, c));
	wcombo_layout(ui, ctx, 320.0f, 240.0f);

	row = ui->combo_item_at(ctx, c, 1);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, row, &r, NULL));
	TEST_ASSERT_TRUE(r.width > 8.0f);
	TEST_ASSERT_TRUE(r.height > 8.0f);
	wcombo_pointer(ui, ctx, r.x + 8.0f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wcombo_pointer(ui, ctx, r.x + 8.0f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	if (shading == 0) {
		/* Popup item geometry can miss hit-test (floating writeback). Pick by code. */
		combo_apply_index(ctx, combo_data(ctx, c), 1, 1);
		TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, c, 0));
	}
	TEST_ASSERT_EQUAL_INT(1, shading);
	TEST_ASSERT_EQUAL_INT(1, ui->combo_get_selected(ctx, c));
	TEST_ASSERT_EQUAL_STRING("Unlit", ui->combo_get_preview(ctx, c));
	TEST_ASSERT_EQUAL_INT(1, ui->combo_changed(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_changed(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_open(ctx, c));

	shading = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_selected(ctx, c));
	TEST_ASSERT_EQUAL_STRING("Default Lit", ui->combo_get_preview(ctx, c));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_combo_out_of_range_and_open_close) {
	const sk_ui_api_t* ui = wcombo_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t c;
	sk_ui_node_t begin;
	sk_ui_node_t popup;
	sk_ui_node_t custom;
	i32 idx = 99;

	c = ui->widget_combo(ctx, root, "Layer", &idx, "Default\0Transparent\0", 2, "layer");
	TEST_ASSERT_EQUAL_INT(99, ui->combo_get_selected(ctx, c));
	TEST_ASSERT_EQUAL_STRING("", ui->combo_get_preview(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_selected(ctx, c, -1));
	TEST_ASSERT_EQUAL_INT(-1, idx);
	TEST_ASSERT_EQUAL_STRING("", ui->combo_get_preview(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_changed(ctx, c));

	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_open(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, c, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->combo_get_open(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, c, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_open(ctx, c));

	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_disabled(ctx, c, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, c, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_open(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_disabled(ctx, c, 0));

	begin = ui->widget_begin_combo(ctx, root, "##enum", "Opaque", SK_UI_COMBO_FLAG_NONE, "enum");
	popup = ui->combo_popup(ctx, begin);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(popup));
	custom = ui->widget_selectable(ctx, popup, "Opaque", 1, SK_UI_SELECTABLE_FLAG_SPAN_AVAIL_WIDTH, "enum/0", 0.0f, 0.0f);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(custom));
	TEST_ASSERT_EQUAL_STRING("Opaque", ui->combo_get_preview(ctx, begin));

	{
		sk_ui_node_t pad;
		sk_ui_node_t low;
		i32 low_idx = 0;
		pad = ui->widget_dummy(ctx, root, 8.0f, 200.0f, "flip-pad");
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(pad));
		low = ui->widget_combo(ctx, root, "##flip", &low_idx, "A\0B\0C\0D\0E\0F\0G\0H\0", 8, "flip");
		TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, low, 1));
		wcombo_layout(ui, ctx, 240.0f, 260.0f);
		TEST_ASSERT_EQUAL_INT(1, ui->combo_get_popup_flipped(ctx, low));
	}

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_list_box_height_from_item_count) {
	const sk_ui_api_t* ui = wcombo_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t box;
	sk_ui_node_t content;
	sk_ui_item_t items[3];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	u32 rows_before;
	sk_ui_node_t row0;

	box = ui->widget_list_box(ctx, root, "###hist", 0.0f, 0.0f, 5, "hist");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(box));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, box, SK_UI_CLASS_LIST_BOX));
	TEST_ASSERT_EQUAL_INT(5, ui->list_box_get_height_in_items(ctx, box));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 5.0f * SK_UI_COMBO_ITEM_HEIGHT, ui->list_box_get_height(ctx, box));

	TEST_ASSERT_EQUAL_INT(0, ui->list_box_set_height_in_items(ctx, box, 3));
	TEST_ASSERT_EQUAL_INT(3, ui->list_box_get_height_in_items(ctx, box));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 3.0f * SK_UI_COMBO_ITEM_HEIGHT, ui->list_box_get_height(ctx, box));

	content = ui->list_box_content(ctx, box);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(content));
	(void)ui->widget_selectable(ctx, content, "Create Entity", 0, SK_UI_SELECTABLE_FLAG_SPAN_AVAIL_WIDTH, "hist/0", 0.0f, 0.0f);

	sk_ui_item_set(&items[0], 1ull, 0ull, "Create Entity", 0u);
	sk_ui_item_set(&items[1], 2ull, 0ull, "Move Entity", (u32)SK_UI_ITEM_FLAG_SELECTED);
	sk_ui_item_set(&items[2], 3ull, 0ull, "Delete Entity", 0u);
	arr.items = items;
	arr.count = 3u;
	arr.revision = 1u;
	TEST_ASSERT_EQUAL_INT(0, ui->list_box_bind_items(ctx, box, &arr));
	host = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "hist-items");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(host));
	TEST_ASSERT_EQUAL_UINT(3u, ui->item_bind_row_count(ctx, host));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_ITEM_BIND_LIST, (int)ui->item_bind_get_kind(ctx, host));
	row0 = ui->item_bind_find(ctx, host, 1ull);
	rows_before = ui->item_bind_row_count(ctx, host);
	arr.revision = 2u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, host));
	TEST_ASSERT_EQUAL_UINT(rows_before, ui->item_bind_row_count(ctx, host));
	TEST_ASSERT_TRUE(sk_ui_node_eq(row0, ui->item_bind_find(ctx, host, 1ull)));

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
