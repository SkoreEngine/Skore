/**
 * @file item_bind.c
 * @brief Retained caller-owned item-array binding (APX-338).
 *
 * Widget hosts store a pointer to a caller-owned sk_ui_item_array_t and
 * diff by stable item id. Used by tree / list / combo / table; not a full
 * TreeNode chrome pass.
 */

#include "ui.internal.h"

#include "allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SK_TESTS
#include "test.h"
#endif

typedef SK_HASH_MAP(u64, sk_ui_node_t) ui_item_row_map_t;
typedef SK_HASH_MAP(u64, u32) ui_item_index_map_t;
typedef SK_HASH_SET(u64) ui_item_id_set_t;
typedef SK_ARRAY(u32) ui_u32_array_t;
typedef SK_ARRAY(u64) ui_u64_array_t;

typedef struct ui_item_bind_data_t {
	sk_ui_item_array_t* array;
	u32 kind;
	u32 last_count;
	u32 last_revision;
	i32 last_was_arrow;
	u64 last_activate;
	sk_ui_node_t host;
	sk_ui_item_id_fn on_activate;
	sk_ui_item_id_fn on_toggle;
	void_ptr_t cb_user;
	ui_item_row_map_t rows;
	ui_item_row_map_t arrows;
	ui_item_id_set_t open;
	ui_item_id_set_t selected;
	ui_item_id_set_t seeded;
	ui_item_id_set_t visible_ids;
	ui_item_index_map_t index_of;
	ui_u32_array_t visible;
	ui_u64_array_t doomed;
} ui_item_bind_data_t;

static const sk_ui_api_t* ib_api(void) {
	return ui_get_api_table();
}

static const_chr_t ib_widget_type(u32 kind) {
	if (kind == (u32)SK_UI_ITEM_BIND_LIST) {
		return "list";
	}
	if (kind == (u32)SK_UI_ITEM_BIND_COMBO) {
		return "combo";
	}
	if (kind == (u32)SK_UI_ITEM_BIND_TABLE) {
		return "table";
	}
	return "tree";
}

static const_chr_t ib_host_class(u32 kind) {
	if (kind == (u32)SK_UI_ITEM_BIND_LIST) {
		return SK_UI_CLASS_LIST;
	}
	if (kind == (u32)SK_UI_ITEM_BIND_COMBO) {
		return SK_UI_CLASS_COMBO_ITEMS;
	}
	if (kind == (u32)SK_UI_ITEM_BIND_TABLE) {
		return SK_UI_CLASS_TABLE;
	}
	return SK_UI_CLASS_TREE;
}

static ui_item_bind_data_t* ib_data(sk_ui_context_t* ctx, sk_ui_node_t host) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, host);
	if (slot == NULL || slot->user_data == NULL) {
		return NULL;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_ITEM_BIND_DATA_TYPE_ID)) {
		return NULL;
	}
	return (ui_item_bind_data_t*)slot->user_data;
}

static ui_item_bind_data_t* ib_data_const(const sk_ui_context_t* ctx, sk_ui_node_t host) {
	return ib_data(SK_CONST_CAST(sk_ui_context_t*, ctx), host);
}

static void ib_unreg(sk_ui_context_t* ctx, sk_ui_node_t host) {
	u32 i;
	for (i = 0u; i < ctx->item_binds.count; ++i) {
		if (sk_ui_node_eq(ctx->item_binds.items[i], host)) {
			u32 j;
			for (j = i; j + 1u < ctx->item_binds.count; ++j) {
				ctx->item_binds.items[j] = ctx->item_binds.items[j + 1u];
			}
			ctx->item_binds.count -= 1u;
			return;
		}
	}
}

void ui_item_bind_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot) {
	ui_item_bind_data_t* b;
	if (slot == NULL || slot->user_data == NULL) {
		return;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_ITEM_BIND_DATA_TYPE_ID)) {
		return;
	}
	b = (ui_item_bind_data_t*)slot->user_data;
	ib_unreg(ctx, b->host);
	sk_hash_map_free(&b->rows);
	sk_hash_map_free(&b->arrows);
	sk_hash_set_free(&b->open);
	sk_hash_set_free(&b->selected);
	sk_hash_set_free(&b->seeded);
	sk_hash_set_free(&b->visible_ids);
	sk_hash_map_free(&b->index_of);
	sk_array_free(&b->visible);
	sk_array_free(&b->doomed);
	ctx->allocator->free(ctx->allocator->instance, b);
	slot->user_data = NULL;
	slot->user_data_type = SK_TYPE_ID_ZERO;
}

static void ib_apply_kind(sk_ui_context_t* ctx, sk_ui_node_t host, u32 kind) {
	const sk_ui_api_t* ui = ib_api();
	(void)ui->node_add_class(ctx, host, ib_host_class(kind));
	(void)ui->node_set_prop_str(ctx, host, "widget", ib_widget_type(kind));
}

static const_chr_t ib_host_id(sk_ui_context_t* ctx, sk_ui_node_t host) {
	const sk_ui_api_t* ui = ib_api();
	const_chr_t id = ui->node_get_id(ctx, host);
	char auto_id[64];
	if (id != NULL && id[0] != '\0') {
		return id;
	}
	ctx->widget_id_seq += 1u;
	(void)snprintf(auto_id, sizeof(auto_id), "item-view-%u", ctx->widget_id_seq);
	(void)ui->node_set_id(ctx, host, auto_id);
	return ui->node_get_id(ctx, host);
}

static void ib_fmt_id(char* out, u32 cap, const_chr_t host_id, char kind, u64 item_id) {
	(void)snprintf(out, cap, "%s/%c%llu", host_id != NULL ? host_id : "item", (int)kind, item_id);
}

static u64 ib_parse_item_id(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ib_api();
	sk_ui_prop_value_t pv;
	if (ui->node_get_prop(ctx, node, "item_id", &pv) != 0 || pv.type != SK_UI_PROP_STR || pv.data.str_value == NULL) {
		return SK_UI_ITEM_ID_NONE;
	}
	return strtoull(pv.data.str_value, NULL, 10);
}

static const_chr_t ib_prop_widget(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ib_api();
	sk_ui_prop_value_t pv;
	if (ui->node_get_prop(ctx, node, "widget", &pv) != 0 || pv.type != SK_UI_PROP_STR) {
		return NULL;
	}
	return pv.data.str_value;
}

static sk_ui_item_t* ib_find_item(ui_item_bind_data_t* b, u64 id) {
	u32 idx;
	if (b == NULL || b->array == NULL || b->array->items == NULL || id == SK_UI_ITEM_ID_NONE) {
		return NULL;
	}
	if (sk_hash_map_get(&b->index_of, id, &idx) != 0 || idx >= b->array->count) {
		return NULL;
	}
	return &b->array->items[idx];
}

static void ib_rebuild_index(ui_item_bind_data_t* b) {
	u32 i;
	sk_hash_map_clear(&b->index_of);
	if (b->array == NULL || b->array->items == NULL) {
		return;
	}
	for (i = 0u; i < b->array->count; ++i) {
		u64 id = b->array->items[i].id;
		if (id == SK_UI_ITEM_ID_NONE) {
			continue;
		}
		if (sk_hash_map_contains(&b->index_of, id)) {
			continue;
		}
		(void)sk_hash_map_put(&b->index_of, id, i);
	}
}

static void ib_seed_item(ui_item_bind_data_t* b, const sk_ui_item_t* it) {
	if (sk_hash_set_contains(&b->seeded, it->id)) {
		return;
	}
	(void)sk_hash_set_add(&b->seeded, it->id);
	if ((it->flags & (u32)SK_UI_ITEM_FLAG_OPEN) != 0u) {
		(void)sk_hash_set_add(&b->open, it->id);
	}
	if ((it->flags & (u32)SK_UI_ITEM_FLAG_SELECTED) != 0u) {
		(void)sk_hash_set_add(&b->selected, it->id);
	}
}

static void ib_writeback(ui_item_bind_data_t* b) {
	u32 i;
	if (b->array == NULL || b->array->items == NULL) {
		return;
	}
	for (i = 0u; i < b->array->count; ++i) {
		sk_ui_item_t* it = &b->array->items[i];
		if (it->id == SK_UI_ITEM_ID_NONE) {
			continue;
		}
		if (sk_hash_set_contains(&b->open, it->id)) {
			it->flags |= (u32)SK_UI_ITEM_FLAG_OPEN;
		} else {
			it->flags &= ~(u32)SK_UI_ITEM_FLAG_OPEN;
		}
		if (sk_hash_set_contains(&b->selected, it->id)) {
			it->flags |= (u32)SK_UI_ITEM_FLAG_SELECTED;
		} else {
			it->flags &= ~(u32)SK_UI_ITEM_FLAG_SELECTED;
		}
	}
}

static i32 ib_is_open(const ui_item_bind_data_t* b, u64 id) {
	return sk_hash_set_contains(SK_CONST_CAST(ui_item_id_set_t*, &b->open), id);
}

typedef struct ib_walk_t {
	u64 parent_id;
	u32 parent_idx;
	u32 pos;
	u8 mode; /**< 0 = choose, 1 = packed range, 2 = parent_id scan. */
	u8 depth;
	u8 _pad0[2];
} ib_walk_t;

static i32 ib_push_visible(ui_item_bind_data_t* b, const sk_ui_item_t* it, u32 idx) {
	if (it->id == SK_UI_ITEM_ID_NONE) {
		return 0;
	}
	if (sk_hash_set_contains(&b->visible_ids, it->id)) {
		return 0;
	}
	ib_seed_item(b, it);
	(void)sk_array_push(&b->visible, idx);
	(void)sk_hash_set_add(&b->visible_ids, it->id);
	return 1;
}

static i32 ib_should_descend(const ui_item_bind_data_t* b, const sk_ui_item_t* it) {
	return (b->kind == (u32)SK_UI_ITEM_BIND_TREE && (it->flags & (u32)SK_UI_ITEM_FLAG_LEAF) == 0u && ib_is_open(b, it->id)) ? 1 : 0;
}

static void ib_collect_visible(ui_item_bind_data_t* b) {
	const sk_ui_item_t* items;
	u32 n;
	u32 i;
	ib_walk_t stack[65];
	u32 sp;
	sk_array_clear(&b->visible);
	sk_hash_set_clear(&b->visible_ids);
	if (b->array == NULL || b->array->items == NULL || b->array->count == 0u) {
		return;
	}
	items = b->array->items;
	n = b->array->count;
	if (b->kind != (u32)SK_UI_ITEM_BIND_TREE) {
		for (i = 0u; i < n; ++i) {
			(void)ib_push_visible(b, &items[i], i);
		}
		return;
	}

	stack[0].parent_id = SK_UI_ITEM_ID_NONE;
	stack[0].parent_idx = SK_UI_ITEM_NONE;
	stack[0].pos = 0u;
	stack[0].mode = 0u;
	stack[0].depth = 0u;
	stack[0]._pad0[0] = 0u;
	stack[0]._pad0[1] = 0u;
	sp = 1u;
	while (sp > 0u) {
		ib_walk_t* fr = &stack[sp - 1u];
		u32 first = 0u;
		u32 cnt = 0u;
		i32 have_range = 0;
		if (fr->depth > 64u) {
			--sp;
			continue;
		}
		if (fr->parent_idx != SK_UI_ITEM_NONE && fr->parent_idx < n && items[fr->parent_idx].child_count > 0u) {
			first = items[fr->parent_idx].first_child;
			cnt = items[fr->parent_idx].child_count;
			if (first < n && (u64)first + (u64)cnt <= (u64)n) {
				have_range = 1;
			}
		}
		if (fr->mode == 0u) {
			fr->mode = have_range != 0 ? 1u : 2u;
			fr->pos = 0u;
			continue;
		}
		if (fr->mode == 1u) {
			u32 idx;
			const sk_ui_item_t* it;
			if (fr->pos >= cnt) {
				--sp;
				continue;
			}
			idx = first + fr->pos;
			fr->pos += 1u;
			if (idx >= n) {
				continue;
			}
			it = &items[idx];
			if (ib_push_visible(b, it, idx) != 0 && ib_should_descend(b, it) != 0 && sp < 65u) {
				ib_walk_t* ch = &stack[sp];
				ch->parent_id = it->id;
				ch->parent_idx = idx;
				ch->pos = 0u;
				ch->mode = 0u;
				ch->depth = (u8)(fr->depth + 1u);
				ch->_pad0[0] = 0u;
				ch->_pad0[1] = 0u;
				sp += 1u;
			}
			continue;
		}
		while (fr->pos < n && (items[fr->pos].id == SK_UI_ITEM_ID_NONE || items[fr->pos].parent_id != fr->parent_id)) {
			fr->pos += 1u;
		}
		if (fr->pos >= n) {
			--sp;
			continue;
		}
		{
			u32 idx = fr->pos;
			const sk_ui_item_t* it = &items[idx];
			fr->pos += 1u;
			if (ib_push_visible(b, it, idx) != 0 && ib_should_descend(b, it) != 0 && sp < 65u) {
				ib_walk_t* ch = &stack[sp];
				ch->parent_id = it->id;
				ch->parent_idx = idx;
				ch->pos = 0u;
				ch->mode = 0u;
				ch->depth = (u8)(fr->depth + 1u);
				ch->_pad0[0] = 0u;
				ch->_pad0[1] = 0u;
				sp += 1u;
			}
		}
	}
}

static u32 ib_item_depth(ui_item_bind_data_t* b, u64 id) {
	u32 depth = 0u;
	u32 guard = 0u;
	while (id != SK_UI_ITEM_ID_NONE && guard < 64u) {
		sk_ui_item_t* it = ib_find_item(b, id);
		if (it == NULL || it->parent_id == SK_UI_ITEM_ID_NONE) {
			break;
		}
		id = it->parent_id;
		depth += 1u;
		guard += 1u;
	}
	return depth;
}

static void ib_style_row(sk_ui_context_t* ctx, sk_ui_node_t row, u32 depth, i32 selected, i32 error) {
	const sk_ui_api_t* ui = ib_api();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR;
	p.layout.padding.left = 4.0f + (f32)depth * 14.0f;
	p.layout.padding.top = 2.0f;
	p.layout.padding.right = 4.0f;
	p.layout.padding.bottom = 2.0f;
	if (selected != 0) {
		p.background_color = sk_ui_rgba(0.24f, 0.36f, 0.58f, 1.0f);
	} else {
		p.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	}
	if (error != 0) {
		p.color = sk_ui_rgba(0.92f, 0.38f, 0.34f, 1.0f);
	} else {
		p.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	}
	(void)ui->node_merge_inline_style(ctx, row, &p);
}

static sk_ui_node_t ib_row_label(const sk_ui_context_t* ctx, sk_ui_node_t row) {
	const sk_ui_api_t* ui = ib_api();
	u32 i;
	u32 n = ui->node_child_count(ctx, row);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t c = ui->node_child_at(ctx, row, i);
		const_chr_t w = ib_prop_widget(ctx, c);
		if (w != NULL && strcmp(w, "label") == 0) {
			return c;
		}
	}
	return SK_UI_NODE_INVALID;
}

static void ib_destroy_row(sk_ui_context_t* ctx, ui_item_bind_data_t* b, u64 id) {
	const sk_ui_api_t* ui = ib_api();
	sk_ui_node_t row;
	if (sk_hash_map_get(&b->rows, id, &row) == 0 && sk_ui_node_is_valid(row) && ui->node_alive(ctx, row)) {
		(void)ui->node_destroy(ctx, row);
	}
	(void)sk_hash_map_remove(&b->rows, id);
	(void)sk_hash_map_remove(&b->arrows, id);
}

static void ib_on_arrow_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);
static void ib_on_row_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);

static void ib_ensure_arrow(sk_ui_context_t* ctx, ui_item_bind_data_t* b, sk_ui_node_t row, const sk_ui_item_t* it, const_chr_t host_id) {
	const sk_ui_api_t* ui = ib_api();
	sk_ui_node_t arrow = SK_UI_NODE_INVALID;
	i32 want = (b->kind == (u32)SK_UI_ITEM_BIND_TREE && (it->flags & (u32)SK_UI_ITEM_FLAG_LEAF) == 0u) ? 1 : 0;
	char idbuf[96];
	if (sk_hash_map_get(&b->arrows, it->id, &arrow) != 0 || !ui->node_alive(ctx, arrow)) {
		arrow = SK_UI_NODE_INVALID;
	}
	if (want == 0) {
		if (sk_ui_node_is_valid(arrow)) {
			(void)ui->node_destroy(ctx, arrow);
		}
		(void)sk_hash_map_remove(&b->arrows, it->id);
		return;
	}
	if (!sk_ui_node_is_valid(arrow)) {
		sk_ui_node_callbacks_t cbs;
		arrow = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, row);
		(void)ui->node_add_class(ctx, arrow, SK_UI_CLASS_TREE_ARROW);
		(void)ui->node_set_prop_str(ctx, arrow, "widget", "tree_arrow");
		ib_fmt_id(idbuf, (u32)sizeof(idbuf), host_id, 'a', it->id);
		(void)ui->node_set_id(ctx, arrow, idbuf);
		(void)snprintf(idbuf, sizeof(idbuf), "%llu", it->id);
		(void)ui->node_set_prop_str(ctx, arrow, "item_id", idbuf);
		(void)ui->node_set_focusable(ctx, arrow, 1);
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = ib_on_arrow_click;
		cbs.user = b;
		(void)ui->node_set_callbacks(ctx, arrow, &cbs);
		(void)ui->node_set_child_index(ctx, row, arrow, 0u);
		(void)sk_hash_map_put(&b->arrows, it->id, arrow);
		{
			sk_ui_style_props_t ap;
			ui_style_props_clear(&ap);
			ap.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
			ap.layout.width = sk_ui_pt(16.0f);
			ap.layout.height = sk_ui_pt(16.0f);
			ap.layout.min_width = sk_ui_pt(16.0f);
			ap.layout.min_height = sk_ui_pt(16.0f);
			(void)ui->node_merge_inline_style(ctx, arrow, &ap);
		}
	}
	(void)ui->node_set_prop_str(ctx, arrow, "text", ib_is_open(b, it->id) ? "v" : ">");
}

static sk_ui_node_t ib_ensure_row(sk_ui_context_t* ctx, ui_item_bind_data_t* b, const sk_ui_item_t* it, const_chr_t host_id) {
	const sk_ui_api_t* ui = ib_api();
	sk_ui_node_t row = SK_UI_NODE_INVALID;
	sk_ui_node_t label;
	const_chr_t text = it->label != NULL ? it->label : "";
	char idbuf[96];
	u32 depth = ib_item_depth(b, it->id);
	i32 selected = sk_hash_set_contains(&b->selected, it->id);
	i32 error = (it->flags & (u32)SK_UI_ITEM_FLAG_ERROR) != 0u ? 1 : 0;

	if (sk_hash_map_get(&b->rows, it->id, &row) != 0 || !ui->node_alive(ctx, row)) {
		sk_ui_node_callbacks_t cbs;
		row = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, b->host);
		(void)ui->node_add_class(ctx, row, SK_UI_CLASS_ITEM_ROW);
		(void)ui->node_set_prop_str(ctx, row, "widget", b->kind == (u32)SK_UI_ITEM_BIND_TREE ? "tree_row" : "list_row");
		ib_fmt_id(idbuf, (u32)sizeof(idbuf), host_id, 'i', it->id);
		(void)ui->node_set_id(ctx, row, idbuf);
		(void)snprintf(idbuf, sizeof(idbuf), "%llu", it->id);
		(void)ui->node_set_prop_str(ctx, row, "item_id", idbuf);
		(void)ui->node_set_focusable(ctx, row, 1);
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = ib_on_row_click;
		cbs.user = b;
		(void)ui->node_set_callbacks(ctx, row, &cbs);
		(void)sk_hash_map_put(&b->rows, it->id, row);
	} else if (!sk_ui_node_eq(ui->node_parent(ctx, row), b->host)) {
		(void)ui->node_reparent(ctx, row, b->host, ui->node_child_count(ctx, b->host));
	}

	(void)ui->node_set_prop_str(ctx, row, "text", text);
	(void)ui->node_set_prop_i32(ctx, row, "selected", selected);
	(void)ui->node_set_prop_i32(ctx, row, "icon", (i32)it->icon);
	if ((it->flags & (u32)SK_UI_ITEM_FLAG_DISABLED) != 0u) {
		(void)ui->node_set_state(ctx, row, ui->node_get_state(ctx, row) | (u32)SK_UI_STATE_DISABLED);
	} else {
		(void)ui->node_set_state(ctx, row, ui->node_get_state(ctx, row) & ~(u32)SK_UI_STATE_DISABLED);
	}
	ib_style_row(ctx, row, depth, selected, error);
	ib_ensure_arrow(ctx, b, row, it, host_id);

	label = ib_row_label(ctx, row);
	if (!sk_ui_node_is_valid(label)) {
		label = ui->widget_label(ctx, row, text, NULL);
		(void)ui->node_set_pointer_events(ctx, label, SK_UI_POINTER_EVENTS_NONE);
	} else {
		(void)ui->label_set_text(ctx, label, text);
	}
	if (error != 0) {
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_COLOR;
		p.color = sk_ui_rgba(0.92f, 0.38f, 0.34f, 1.0f);
		(void)ui->node_merge_inline_style(ctx, label, &p);
	}
	return row;
}

static void ib_destroy_missing(sk_ui_context_t* ctx, ui_item_bind_data_t* b) {
	u32 s;
	u32 cap;
	sk_array_clear(&b->doomed);
	cap = b->rows._hm.capacity;
	for (s = 0u; s < cap; ++s) {
		const u64* key;
		if (sk_hash_map_slot_occupied_(&b->rows._hm, s) == 0) {
			continue;
		}
		key = (const u64*)sk_hash_map_key_at_(&b->rows._hm, s);
		if (key == NULL) {
			continue;
		}
		if (sk_hash_set_contains(&b->visible_ids, *key) == 0) {
			(void)sk_array_push(&b->doomed, *key);
		}
	}
	for (s = 0u; s < b->doomed.count; ++s) {
		ib_destroy_row(ctx, b, b->doomed.items[s]);
	}
}

i32 ui_item_bind_sync_impl(sk_ui_context_t* ctx, sk_ui_node_t host) {
	const sk_ui_api_t* ui = ib_api();
	ui_item_bind_data_t* b = ib_data(ctx, host);
	const_chr_t host_id;
	u32 i;
	if (b == NULL) {
		return -1;
	}
	ib_rebuild_index(b);
	ib_collect_visible(b);
	ib_destroy_missing(ctx, b);
	host_id = ib_host_id(ctx, host);
	for (i = 0u; i < b->visible.count; ++i) {
		u32 idx = b->visible.items[i];
		sk_ui_node_t row;
		if (b->array == NULL || b->array->items == NULL || idx >= b->array->count) {
			continue;
		}
		row = ib_ensure_row(ctx, b, &b->array->items[idx], host_id);
		(void)ui->node_set_child_index(ctx, host, row, i);
	}
	if (b->array != NULL) {
		b->last_count = b->array->count;
		b->last_revision = b->array->revision;
	} else {
		b->last_count = 0u;
		b->last_revision = 0u;
	}
	ib_writeback(b);
	return 0;
}

void ui_item_bind_sync_all(sk_ui_context_t* ctx) {
	u32 i = 0u;
	while (i < ctx->item_binds.count) {
		sk_ui_node_t host = ctx->item_binds.items[i];
		if (ib_data(ctx, host) == NULL) {
			ib_unreg(ctx, host);
			continue;
		}
		(void)ui_item_bind_sync_impl(ctx, host);
		++i;
	}
}

static ui_item_bind_data_t* ib_ensure(sk_ui_context_t* ctx, sk_ui_node_t host, u32 kind) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, host);
	ui_item_bind_data_t* b;
	const sk_allocator_t* a;
	if (slot == NULL) {
		return NULL;
	}
	if (slot->user_data != NULL && SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_ITEM_BIND_DATA_TYPE_ID)) {
		b = (ui_item_bind_data_t*)slot->user_data;
		b->kind = kind;
		b->host = host;
		return b;
	}
	if (slot->user_data != NULL && SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_WIDGET_DATA_TYPE_ID)) {
		ctx->allocator->free(ctx->allocator->instance, slot->user_data);
		slot->user_data = NULL;
		slot->user_data_type = SK_TYPE_ID_ZERO;
	} else if (slot->user_data != NULL) {
		return NULL;
	}
	a = ctx->allocator;
	b = (ui_item_bind_data_t*)a->alloc(a->instance, sizeof(ui_item_bind_data_t));
	if (b == NULL) {
		return NULL;
	}
	memset(b, 0, sizeof(*b));
	b->kind = kind;
	b->host = host;
	b->last_activate = SK_UI_ITEM_ID_NONE;
	sk_hash_map_init(&b->rows, a, NULL, NULL);
	sk_hash_map_init(&b->arrows, a, NULL, NULL);
	sk_hash_set_init(&b->open, a, NULL, NULL);
	sk_hash_set_init(&b->selected, a, NULL, NULL);
	sk_hash_set_init(&b->seeded, a, NULL, NULL);
	sk_hash_set_init(&b->visible_ids, a, NULL, NULL);
	sk_hash_map_init(&b->index_of, a, NULL, NULL);
	sk_array_init(&b->visible, a);
	sk_array_init(&b->doomed, a);
	slot->user_data = b;
	slot->user_data_type = SK_UI_ITEM_BIND_DATA_TYPE_ID;
	(void)sk_array_push(&ctx->item_binds, host);
	return b;
}

i32 ui_item_bind_impl(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_array_t* items, sk_ui_item_bind_kind_t kind) {
	ui_item_bind_data_t* b = ib_ensure(ctx, host, (u32)kind);
	if (b == NULL) {
		return -1;
	}
	b->array = items;
	b->kind = (u32)kind;
	ib_apply_kind(ctx, host, (u32)kind);
	(void)ib_host_id(ctx, host);
	return ui_item_bind_sync_impl(ctx, host);
}

i32 ui_item_bind_set_array_impl(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_array_t* items) {
	ui_item_bind_data_t* b = ib_data(ctx, host);
	if (b == NULL) {
		return -1;
	}
	b->array = items;
	return ui_item_bind_sync_impl(ctx, host);
}

sk_ui_item_array_t* ui_item_bind_get_array_impl(const sk_ui_context_t* ctx, sk_ui_node_t host) {
	ui_item_bind_data_t* b = ib_data_const(ctx, host);
	return b != NULL ? b->array : NULL;
}

sk_ui_item_bind_kind_t ui_item_bind_get_kind_impl(const sk_ui_context_t* ctx, sk_ui_node_t host) {
	ui_item_bind_data_t* b = ib_data_const(ctx, host);
	return b != NULL ? (sk_ui_item_bind_kind_t)b->kind : SK_UI_ITEM_BIND_TREE;
}

sk_ui_node_t ui_item_bind_find_impl(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id) {
	ui_item_bind_data_t* b = ib_data_const(ctx, host);
	sk_ui_node_t row = SK_UI_NODE_INVALID;
	if (b == NULL) {
		return SK_UI_NODE_INVALID;
	}
	if (sk_hash_map_get(&b->rows, item_id, &row) != 0) {
		return SK_UI_NODE_INVALID;
	}
	if (!ib_api()->node_alive(ctx, row)) {
		return SK_UI_NODE_INVALID;
	}
	return row;
}

u32 ui_item_bind_row_count_impl(const sk_ui_context_t* ctx, sk_ui_node_t host) {
	ui_item_bind_data_t* b = ib_data_const(ctx, host);
	return b != NULL ? sk_hash_map_count(&b->rows) : 0u;
}

i32 ui_item_bind_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id) {
	ui_item_bind_data_t* b = ib_data_const(ctx, host);
	return (b != NULL && sk_hash_set_contains(&b->open, item_id)) ? 1 : 0;
}

i32 ui_item_bind_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, i32 open) {
	ui_item_bind_data_t* b = ib_data(ctx, host);
	if (b == NULL || item_id == SK_UI_ITEM_ID_NONE) {
		return -1;
	}
	(void)sk_hash_set_add(&b->seeded, item_id);
	if (open != 0) {
		(void)sk_hash_set_add(&b->open, item_id);
	} else {
		(void)sk_hash_set_remove(&b->open, item_id);
	}
	return ui_item_bind_sync_impl(ctx, host);
}

i32 ui_item_bind_get_selected_impl(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id) {
	ui_item_bind_data_t* b = ib_data_const(ctx, host);
	return (b != NULL && sk_hash_set_contains(&b->selected, item_id)) ? 1 : 0;
}

i32 ui_item_bind_set_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, i32 selected) {
	ui_item_bind_data_t* b = ib_data(ctx, host);
	if (b == NULL || item_id == SK_UI_ITEM_ID_NONE) {
		return -1;
	}
	(void)sk_hash_set_add(&b->seeded, item_id);
	if (selected != 0) {
		(void)sk_hash_set_add(&b->selected, item_id);
	} else {
		(void)sk_hash_set_remove(&b->selected, item_id);
	}
	ib_writeback(b);
	return ui_item_bind_sync_impl(ctx, host);
}

u64 ui_item_bind_last_activate_impl(const sk_ui_context_t* ctx, sk_ui_node_t host) {
	ui_item_bind_data_t* b = ib_data_const(ctx, host);
	return b != NULL ? b->last_activate : SK_UI_ITEM_ID_NONE;
}

i32 ui_item_bind_last_was_arrow_impl(const sk_ui_context_t* ctx, sk_ui_node_t host) {
	ui_item_bind_data_t* b = ib_data_const(ctx, host);
	return b != NULL ? b->last_was_arrow : 0;
}

i32 ui_item_bind_clear_state_impl(sk_ui_context_t* ctx, sk_ui_node_t host) {
	ui_item_bind_data_t* b = ib_data(ctx, host);
	if (b == NULL) {
		return -1;
	}
	sk_hash_set_clear(&b->open);
	sk_hash_set_clear(&b->selected);
	sk_hash_set_clear(&b->seeded);
	b->last_activate = SK_UI_ITEM_ID_NONE;
	b->last_was_arrow = 0;
	return ui_item_bind_sync_impl(ctx, host);
}

i32 ui_item_bind_set_on_activate_impl(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_id_fn fn, void_ptr_t user) {
	ui_item_bind_data_t* b = ib_data(ctx, host);
	if (b == NULL) {
		return -1;
	}
	b->on_activate = fn;
	b->cb_user = user;
	return 0;
}

i32 ui_item_bind_set_on_toggle_impl(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_id_fn fn, void_ptr_t user) {
	ui_item_bind_data_t* b = ib_data(ctx, host);
	if (b == NULL) {
		return -1;
	}
	b->on_toggle = fn;
	b->cb_user = user;
	return 0;
}

static i32 ib_item_disabled(ui_item_bind_data_t* b, u64 id) {
	sk_ui_item_t* it = ib_find_item(b, id);
	return (it != NULL && (it->flags & (u32)SK_UI_ITEM_FLAG_DISABLED) != 0u) ? 1 : 0;
}

static void ib_on_arrow_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	ui_item_bind_data_t* b = (ui_item_bind_data_t*)user;
	u64 id;
	if (b == NULL) {
		return;
	}
	id = ib_parse_item_id(ctx, node);
	if (id == SK_UI_ITEM_ID_NONE || ib_item_disabled(b, id)) {
		return;
	}
	if (sk_hash_set_contains(&b->open, id)) {
		(void)sk_hash_set_remove(&b->open, id);
	} else {
		(void)sk_hash_set_add(&b->open, id);
	}
	(void)sk_hash_set_add(&b->seeded, id);
	b->last_was_arrow = 1;
	(void)ui_item_bind_sync_impl(ctx, b->host);
	if (b->on_toggle != NULL) {
		b->on_toggle(ctx, b->host, id, b->cb_user);
	}
	if (event != NULL) {
		event->consumed = 1;
	}
}

static void ib_on_row_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ib_api();
	ui_item_bind_data_t* b = (ui_item_bind_data_t*)user;
	u64 id;
	sk_ui_rect_t border;
	if (b == NULL) {
		return;
	}
	id = ib_parse_item_id(ctx, node);
	if (id == SK_UI_ITEM_ID_NONE || ib_item_disabled(b, id)) {
		return;
	}
	/* Indent + arrow width: expand when the dedicated arrow node is missed. */
	if (b->kind == (u32)SK_UI_ITEM_BIND_TREE && event != NULL && ui->node_get_abs_rect(ctx, node, &border, NULL) == 0) {
		f32 gutter = 4.0f + (f32)ib_item_depth(b, id) * 14.0f + 16.0f;
		if (event->x < border.x + gutter) {
			ib_on_arrow_click(ctx, node, event, user);
			return;
		}
	}
	sk_hash_set_clear(&b->selected);
	(void)sk_hash_set_add(&b->selected, id);
	(void)sk_hash_set_add(&b->seeded, id);
	b->last_activate = id;
	b->last_was_arrow = 0;
	(void)ui_item_bind_sync_impl(ctx, b->host);
	if (b->on_activate != NULL) {
		b->on_activate(ctx, b->host, id, b->cb_user);
	}
	if (event != NULL) {
		event->consumed = 1;
	}
}

static sk_ui_node_t ib_make_host(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, sk_ui_item_bind_kind_t kind, const_chr_t id) {
	const sk_ui_api_t* ui = ib_api();
	sk_ui_node_t n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	char auto_id[64];
	if (!sk_ui_node_is_valid(n)) {
		return SK_UI_NODE_INVALID;
	}
	ib_apply_kind(ctx, n, (u32)kind);
	if (id != NULL && id[0] != '\0') {
		(void)ui->node_set_id(ctx, n, id);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(auto_id, sizeof(auto_id), "%s-%u", ib_widget_type((u32)kind), ctx->widget_id_seq);
		(void)ui->node_set_id(ctx, n, auto_id);
	}
	if (ui_item_bind_impl(ctx, n, items, kind) != 0) {
		(void)ui->node_destroy(ctx, n);
		return SK_UI_NODE_INVALID;
	}
	return n;
}

sk_ui_node_t ui_widget_item_view_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, sk_ui_item_bind_kind_t kind, const_chr_t id) {
	return ib_make_host(ctx, parent, items, kind, id);
}

sk_ui_node_t ui_widget_tree_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, const_chr_t id) {
	return ib_make_host(ctx, parent, items, SK_UI_ITEM_BIND_TREE, id);
}

sk_ui_node_t ui_widget_list_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, const_chr_t id) {
	return ib_make_host(ctx, parent, items, SK_UI_ITEM_BIND_LIST, id);
}

#ifdef SK_TESTS

static sk_ui_node_t ib_test_tree(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_item_array_t* arr, const_chr_t id) {
	return ui->widget_tree(ctx, ui->context_root(ctx), arr, id);
}

SK_TEST(ui_item_bind_empty_array) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	sk_ui_item_t items[2];

	TEST_ASSERT_NOT_NULL(ctx);
	arr.items = NULL;
	arr.count = 0u;
	arr.revision = 0u;
	host = ib_test_tree(ui, ctx, &arr, "empty");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(host));
	TEST_ASSERT_EQUAL_UINT(0u, ui->item_bind_row_count(ctx, host));
	TEST_ASSERT_TRUE(ui->item_bind_get_array(ctx, host) == &arr);
	TEST_ASSERT_EQUAL_INT((int)SK_UI_ITEM_BIND_TREE, (int)ui->item_bind_get_kind(ctx, host));

	sk_ui_item_set(&items[0], 1ull, 0ull, "A", 0u);
	sk_ui_item_set(&items[1], 2ull, 1ull, "B", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 2u;
	arr.revision = 1u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, host));
	TEST_ASSERT_EQUAL_UINT(1u, ui->item_bind_row_count(ctx, host));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->item_bind_find(ctx, host, 1ull)));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->item_bind_find(ctx, host, 2ull)));

	arr.items = NULL;
	arr.count = 0u;
	arr.revision = 2u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, host));
	TEST_ASSERT_EQUAL_UINT(0u, ui->item_bind_row_count(ctx, host));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, host));
	ui->context_destroy(ctx);
}

SK_TEST(ui_item_bind_mutation_between_frames) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[4];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	sk_ui_node_t row1;
	sk_ui_node_t row1b;
	sk_ui_prop_value_t pv;

	sk_ui_item_set(&items[0], 10ull, 0ull, "Root", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&items[1], 11ull, 10ull, "Old", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 2u;
	arr.revision = 1u;
	host = ib_test_tree(ui, ctx, &arr, "mut");
	TEST_ASSERT_EQUAL_UINT(2u, ui->item_bind_row_count(ctx, host));
	row1 = ui->item_bind_find(ctx, host, 10ull);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row1));

	items[1].label = "Renamed";
	sk_ui_item_set(&items[2], 12ull, 10ull, "New", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.count = 3u;
	arr.revision = 2u;
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	row1b = ui->item_bind_find(ctx, host, 10ull);
	TEST_ASSERT_TRUE(sk_ui_node_eq(row1, row1b));
	TEST_ASSERT_EQUAL_UINT(3u, ui->item_bind_row_count(ctx, host));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, ui->item_bind_find(ctx, host, 11ull), "text", &pv));
	TEST_ASSERT_EQUAL_STRING("Renamed", pv.data.str_value);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->item_bind_find(ctx, host, 12ull)));
	ui->context_destroy(ctx);
}

SK_TEST(ui_item_bind_insert_remove_mid_tree) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[6];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	sk_ui_node_t child_a;

	sk_ui_item_set(&items[0], 1ull, 0ull, "R", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&items[1], 2ull, 1ull, "A", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[2], 3ull, 1ull, "B", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 3u;
	arr.revision = 0u;
	host = ib_test_tree(ui, ctx, &arr, "mid");
	child_a = ui->item_bind_find(ctx, host, 2ull);
	TEST_ASSERT_EQUAL_UINT(3u, ui->item_bind_row_count(ctx, host));

	/* Insert C between A and B; drop B. */
	sk_ui_item_set(&items[0], 1ull, 0ull, "R", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&items[1], 2ull, 1ull, "A", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[2], 4ull, 1ull, "C", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.count = 3u;
	arr.revision = 1u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, host));
	TEST_ASSERT_TRUE(sk_ui_node_eq(child_a, ui->item_bind_find(ctx, host, 2ull)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->item_bind_find(ctx, host, 4ull)));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->item_bind_find(ctx, host, 3ull)));
	TEST_ASSERT_EQUAL_UINT(3u, ui->item_bind_row_count(ctx, host));

	/* Packed child range: A, Mid, C. */
	sk_ui_item_set(&items[0], 1ull, 0ull, "R", (u32)SK_UI_ITEM_FLAG_OPEN);
	items[0].first_child = 1u;
	items[0].child_count = 3u;
	sk_ui_item_set(&items[1], 2ull, 1ull, "A", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[2], 5ull, 1ull, "Mid", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[3], 4ull, 1ull, "C", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.count = 4u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, host));
	TEST_ASSERT_EQUAL_UINT(4u, ui->item_bind_row_count(ctx, host));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->item_bind_find(ctx, host, 5ull)));
	ui->context_destroy(ctx);
}

SK_TEST(ui_item_bind_reorder) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[3];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	sk_ui_node_t a;
	sk_ui_node_t b;

	sk_ui_item_set(&items[0], 1ull, 0ull, "A", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[1], 2ull, 0ull, "B", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 2u;
	arr.revision = 0u;
	host = ib_test_tree(ui, ctx, &arr, "ord");
	a = ui->item_bind_find(ctx, host, 1ull);
	b = ui->item_bind_find(ctx, host, 2ull);
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, host, 0u), a));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, host, 1u), b));

	sk_ui_item_set(&items[0], 2ull, 0ull, "B", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[1], 1ull, 0ull, "A", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.revision = 1u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, host));
	TEST_ASSERT_TRUE(sk_ui_node_eq(a, ui->item_bind_find(ctx, host, 1ull)));
	TEST_ASSERT_TRUE(sk_ui_node_eq(b, ui->item_bind_find(ctx, host, 2ull)));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, host, 0u), b));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, host, 1u), a));
	ui->context_destroy(ctx);
}

SK_TEST(ui_item_bind_selection_expansion_survive) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[5];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;

	sk_ui_item_set(&items[0], 1ull, 0ull, "Scene", 0u);
	sk_ui_item_set(&items[1], 2ull, 1ull, "Ent", 0u);
	sk_ui_item_set(&items[2], 3ull, 2ull, "Leaf", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 3u;
	arr.revision = 0u;
	host = ib_test_tree(ui, ctx, &arr, "st");
	TEST_ASSERT_EQUAL_UINT(1u, ui->item_bind_row_count(ctx, host));

	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_open(ctx, host, 1ull, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_open(ctx, host, 2ull, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_selected(ctx, host, 3ull, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 2ull));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 3ull));
	TEST_ASSERT_EQUAL_UINT(3u, ui->item_bind_row_count(ctx, host));
	TEST_ASSERT_TRUE((items[0].flags & (u32)SK_UI_ITEM_FLAG_OPEN) != 0u);
	TEST_ASSERT_TRUE((items[2].flags & (u32)SK_UI_ITEM_FLAG_SELECTED) != 0u);

	/* Filter away the leaf, relabel, add a sibling — state must stick. */
	sk_ui_item_set(&items[0], 1ull, 0ull, "Scene2", 0u);
	sk_ui_item_set(&items[1], 2ull, 1ull, "Ent2", 0u);
	sk_ui_item_set(&items[2], 9ull, 1ull, "Sib", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.count = 3u;
	arr.revision = 4u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, host));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 2ull));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 3ull));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->item_bind_find(ctx, host, 3ull)));

	/* Bring the leaf back: still selected, still expanded path. */
	sk_ui_item_set(&items[3], 3ull, 2ull, "Leaf", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.count = 4u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, host));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 3ull));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->item_bind_find(ctx, host, 3ull)));
	ui->context_destroy(ctx);
}

SK_TEST(ui_item_bind_deep_nesting) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[8];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	u32 i;
	sk_ui_node_t leaf;

	for (i = 0u; i < 8u; ++i) {
		u64 id = (u64)i + 1ull;
		u64 parent = i == 0u ? 0ull : (u64)i;
		u32 flags = (i == 7u) ? (u32)SK_UI_ITEM_FLAG_LEAF : (u32)SK_UI_ITEM_FLAG_OPEN;
		sk_ui_item_set(&items[i], id, parent, "n", flags);
	}
	items[0].label = "n0";
	items[1].label = "n1";
	items[2].label = "n2";
	items[3].label = "n3";
	items[4].label = "n4";
	items[5].label = "n5";
	items[6].label = "n6";
	items[7].label = "n7";
	arr.items = items;
	arr.count = 8u;
	arr.revision = 0u;
	host = ib_test_tree(ui, ctx, &arr, "deep");
	TEST_ASSERT_EQUAL_UINT(8u, ui->item_bind_row_count(ctx, host));
	leaf = ui->item_bind_find(ctx, host, 8ull);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(leaf));

	items[7].label = "leaf";
	arr.revision = 1u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, host));
	TEST_ASSERT_TRUE(sk_ui_node_eq(leaf, ui->item_bind_find(ctx, host, 8ull)));
	{
		sk_ui_prop_value_t pv;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, leaf, "text", &pv));
		TEST_ASSERT_EQUAL_STRING("leaf", pv.data.str_value);
	}
	for (i = 1u; i <= 7u; ++i) {
		TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, (u64)i));
	}
	ui->context_destroy(ctx);
}

SK_TEST(ui_item_bind_list_combo_table_share_contract) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[3];
	sk_ui_item_array_t arr;
	sk_ui_node_t tree;
	sk_ui_node_t list;
	sk_ui_node_t combo;
	sk_ui_node_t table;
	sk_ui_node_t root;

	sk_ui_item_set(&items[0], 1ull, 0ull, "P", 0u);
	sk_ui_item_set(&items[1], 2ull, 1ull, "C", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[2], 3ull, 0ull, "Q", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 3u;
	arr.revision = 0u;
	root = ui->context_root(ctx);
	tree = ui->widget_tree(ctx, root, &arr, "t");
	list = ui->widget_list(ctx, root, &arr, "l");
	combo = ui->widget_item_view(ctx, root, &arr, SK_UI_ITEM_BIND_COMBO, "c");
	table = ui->widget_item_view(ctx, root, &arr, SK_UI_ITEM_BIND_TABLE, "tb");

	TEST_ASSERT_EQUAL_UINT(2u, ui->item_bind_row_count(ctx, tree));
	TEST_ASSERT_EQUAL_UINT(3u, ui->item_bind_row_count(ctx, list));
	TEST_ASSERT_EQUAL_UINT(3u, ui->item_bind_row_count(ctx, combo));
	TEST_ASSERT_EQUAL_UINT(3u, ui->item_bind_row_count(ctx, table));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_ITEM_BIND_LIST, (int)ui->item_bind_get_kind(ctx, list));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_ITEM_BIND_COMBO, (int)ui->item_bind_get_kind(ctx, combo));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_ITEM_BIND_TABLE, (int)ui->item_bind_get_kind(ctx, table));
	TEST_ASSERT_TRUE(ui->item_bind_get_array(ctx, list) == &arr);
	TEST_ASSERT_TRUE(ui->item_bind_get_array(ctx, table) == &arr);
	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
