/**
 * @file table.c
 * @brief Table family (APX-351; WIDGET_MANIFEST.md §9).
 *
 * BeginTable / EndTable, TableSetupColumn, headers, next-row / next-column /
 * set-column-index, sizing policies, Resizable, RowBg, border variants,
 * ScrollX/ScrollY + freeze, TableSetBgColor, column-count query, and
 * caller-owned item-array rows (§21).
 */

#include "ui.internal.h"

#include "allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SK_TESTS
#include "test.h"
#endif

typedef SK_HASH_MAP(u64, u32) ui_table_id_map_t;

typedef struct ui_table_column_t {
	char label[48];
	u32 flags;
	f32 init_width;
	f32 width;
	f32 weight;
} ui_table_column_t;

typedef struct ui_table_row_t {
	sk_ui_node_t node;
	sk_ui_node_t hscroll;
	sk_ui_node_t cells[SK_UI_TABLE_MAX_COLUMNS];
	u64 item_id;
	u32 flags;
	f32 min_height;
	i32 is_header;
	i32 bg_set;
	i32 cell_bg_set[SK_UI_TABLE_MAX_COLUMNS];
	sk_ui_color_t row_bg;
	sk_ui_color_t cell_bg[SK_UI_TABLE_MAX_COLUMNS];
} ui_table_row_t;

typedef struct ui_table_data_t {
	u32 column_count;
	u32 setup_count;
	u32 flags;
	f32 outer_w;
	f32 outer_h;
	i32 freeze_cols;
	i32 freeze_rows;
	i32 cursor_row;
	i32 cursor_col;
	i32 has_header;
	u32 row_count;
	u32 row_cap;
	i32 drag_col;
	f32 drag_start_x;
	f32 drag_start_w;
	ui_table_column_t columns[SK_UI_TABLE_MAX_COLUMNS];
	ui_table_row_t* rows;
	sk_ui_node_t table;
	sk_ui_node_t header;
	sk_ui_node_t body;
	sk_ui_node_t body_content;
	sk_ui_item_array_t* items;
	u32 last_item_count;
	u32 last_revision;
	ui_table_id_map_t id_to_row;
} ui_table_data_t;

static const sk_ui_color_t k_table_header_bg = {0.30f, 0.38f, 0.48f, 1.0f};
static const sk_ui_color_t k_table_row_even = {0.10f, 0.11f, 0.13f, 1.0f};
static const sk_ui_color_t k_table_row_odd = {0.16f, 0.18f, 0.21f, 1.0f};
static const sk_ui_color_t k_table_border = {0.28f, 0.30f, 0.34f, 1.0f};

static const sk_ui_api_t* tbl_api(void) {
	return ui_get_api_table();
}

static ui_table_data_t* tbl_data(sk_ui_context_t* ctx, sk_ui_node_t table) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, table);
	if (slot == NULL || slot->user_data == NULL) {
		return NULL;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_TABLE_DATA_TYPE_ID)) {
		return NULL;
	}
	return (ui_table_data_t*)slot->user_data;
}

static const ui_table_data_t* tbl_data_const(const sk_ui_context_t* ctx, sk_ui_node_t table) {
	return tbl_data(SK_CONST_CAST(sk_ui_context_t*, ctx), table);
}

i32 ui_table_is_table(const sk_ui_context_t* ctx, sk_ui_node_t host) {
	return tbl_data_const(ctx, host) != NULL ? 1 : 0;
}

static void tbl_unreg(sk_ui_context_t* ctx, sk_ui_node_t table) {
	u32 i;
	for (i = 0u; i < ctx->tables.count; ++i) {
		if (sk_ui_node_eq(ctx->tables.items[i], table)) {
			u32 j;
			for (j = i; j + 1u < ctx->tables.count; ++j) {
				ctx->tables.items[j] = ctx->tables.items[j + 1u];
			}
			ctx->tables.count -= 1u;
			return;
		}
	}
}

void ui_table_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot) {
	ui_table_data_t* t;
	if (slot == NULL || slot->user_data == NULL) {
		return;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_TABLE_DATA_TYPE_ID)) {
		return;
	}
	t = (ui_table_data_t*)slot->user_data;
	tbl_unreg(ctx, t->table);
	sk_hash_map_free(&t->id_to_row);
	if (t->rows != NULL) {
		ctx->allocator->free(ctx->allocator->instance, t->rows);
		t->rows = NULL;
	}
	ctx->allocator->free(ctx->allocator->instance, t);
	slot->user_data = NULL;
	slot->user_data_type = SK_TYPE_ID_ZERO;
}

static u32 tbl_sizing_mask(u32 flags) {
	return flags & (SK_UI_TABLE_FLAG_SIZING_FIXED_FIT | SK_UI_TABLE_FLAG_SIZING_FIXED_SAME | SK_UI_TABLE_FLAG_SIZING_STRETCH_PROP);
}

static i32 tbl_col_is_stretch(const ui_table_data_t* t, u32 col) {
	u32 cf;
	u32 sizing;
	if (col >= t->column_count) {
		return 0;
	}
	cf = t->columns[col].flags;
	if ((cf & SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH) != 0u) {
		return 1;
	}
	if ((cf & SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED) != 0u) {
		return 0;
	}
	sizing = tbl_sizing_mask(t->flags);
	return sizing == SK_UI_TABLE_FLAG_SIZING_STRETCH_PROP ? 1 : 0;
}

static void tbl_copy_label(char* dst, u32 cap, const_chr_t src) {
	u32 i;
	if (dst == NULL || cap == 0u) {
		return;
	}
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}
	for (i = 0u; i + 1u < cap && src[i] != '\0'; ++i) {
		dst[i] = src[i];
	}
	dst[i] = '\0';
}

static void tbl_apply_outer_size(sk_ui_context_t* ctx, sk_ui_node_t table, f32 width, f32 height) {
	const sk_ui_api_t* ui = tbl_api();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.align_items = SK_UI_ALIGN_STRETCH;
	if (width <= 0.0f && height <= 0.0f) {
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.height = sk_ui_auto();
		p.layout.flex_grow = 1.0f;
		p.layout.flex_shrink = 1.0f;
	} else if (width <= 0.0f && height > 0.0f) {
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.height = sk_ui_pt(height);
		p.layout.min_height = sk_ui_pt(height);
		p.layout.max_height = sk_ui_pt(height);
		p.mask |= SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT;
		p.layout.flex_grow = 0.0f;
		p.layout.flex_shrink = 0.0f;
	} else if (width > 0.0f && height <= 0.0f) {
		p.layout.width = sk_ui_pt(width);
		p.layout.min_width = sk_ui_pt(width);
		p.layout.max_width = sk_ui_pt(width);
		p.mask |= SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH;
		p.layout.height = sk_ui_auto();
		p.layout.flex_grow = 1.0f;
		p.layout.flex_shrink = 1.0f;
	} else {
		p.layout.width = sk_ui_pt(width);
		p.layout.height = sk_ui_pt(height);
		p.layout.min_width = sk_ui_pt(width);
		p.layout.min_height = sk_ui_pt(height);
		p.layout.max_width = sk_ui_pt(width);
		p.layout.max_height = sk_ui_pt(height);
		p.mask |= SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
		p.layout.flex_grow = 0.0f;
		p.layout.flex_shrink = 0.0f;
	}
	(void)ui->node_merge_inline_style(ctx, table, &p);
}

static void tbl_apply_borders(sk_ui_context_t* ctx, ui_table_data_t* t) {
	const sk_ui_api_t* ui = tbl_api();
	sk_ui_style_props_t p;
	u32 i;
	i32 outer = (t->flags & SK_UI_TABLE_FLAG_BORDERS_OUTER) != 0u ? 1 : 0;
	i32 inner_h = (t->flags & SK_UI_TABLE_FLAG_BORDERS_INNER_H) != 0u ? 1 : 0;
	i32 inner_v = (t->flags & SK_UI_TABLE_FLAG_BORDERS_INNER_V) != 0u ? 1 : 0;
	i32 no_body = (t->flags & SK_UI_TABLE_FLAG_NO_BORDERS_IN_BODY) != 0u ? 1 : 0;

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
	p.border_color = k_table_border;
	if (outer != 0) {
		p.layout.border.left = 1.0f;
		p.layout.border.top = 1.0f;
		p.layout.border.right = 1.0f;
		p.layout.border.bottom = 1.0f;
	}
	(void)ui->node_merge_inline_style(ctx, t->table, &p);
	(void)ui->node_set_prop_i32(ctx, t->table, "table_flags", (i32)t->flags);

	for (i = 0u; i < t->row_count; ++i) {
		ui_table_row_t* row = &t->rows[i];
		u32 c;
		i32 row_inner_h;
		i32 row_inner_v;
		if (!sk_ui_node_is_valid(row->node)) {
			continue;
		}
		row_inner_h = inner_h;
		row_inner_v = inner_v;
		if (no_body != 0 && row->is_header == 0) {
			row_inner_h = 0;
			row_inner_v = 0;
		}
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
		p.border_color = k_table_border;
		if (row_inner_h != 0 || (row->is_header != 0 && inner_h != 0)) {
			p.layout.border.bottom = 1.0f;
		}
		(void)ui->node_merge_inline_style(ctx, row->node, &p);
		(void)ui->node_set_prop_i32(ctx, row->node, "table_inner_h", row_inner_h);
		(void)ui->node_set_prop_i32(ctx, row->node, "table_inner_v", row_inner_v);
		(void)ui->node_set_prop_i32(ctx, row->node, "table_no_body_border", no_body);
		for (c = 0u; c < t->column_count; ++c) {
			if (!sk_ui_node_is_valid(row->cells[c])) {
				continue;
			}
			ui_style_props_clear(&p);
			p.mask = SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
			p.border_color = k_table_border;
			if (row_inner_v != 0 && c + 1u < t->column_count) {
				p.layout.border.right = 1.0f;
			}
			(void)ui->node_merge_inline_style(ctx, row->cells[c], &p);
		}
	}
}

static void tbl_apply_row_bg(sk_ui_context_t* ctx, ui_table_data_t* t, ui_table_row_t* row, u32 body_index) {
	const sk_ui_api_t* ui = tbl_api();
	sk_ui_style_props_t p;
	sk_ui_color_t bg;
	u32 c;
	if (row->is_header != 0) {
		bg = k_table_header_bg;
	} else if (row->bg_set != 0) {
		bg = row->row_bg;
	} else if ((t->flags & SK_UI_TABLE_FLAG_ROW_BG) != 0u) {
		bg = (body_index & 1u) != 0u ? k_table_row_odd : k_table_row_even;
	} else {
		bg = k_table_row_even;
	}
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_BACKGROUND_COLOR;
	p.background_color = bg;
	(void)ui->node_merge_inline_style(ctx, row->node, &p);
	for (c = 0u; c < t->column_count; ++c) {
		sk_ui_color_t cell_bg = bg;
		if (row->cell_bg_set[c] != 0) {
			cell_bg = row->cell_bg[c];
		}
		if (!sk_ui_node_is_valid(row->cells[c])) {
			continue;
		}
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_BACKGROUND_COLOR;
		p.background_color = cell_bg;
		(void)ui->node_merge_inline_style(ctx, row->cells[c], &p);
	}
}

static void tbl_apply_all_row_bg(sk_ui_context_t* ctx, ui_table_data_t* t) {
	u32 i;
	u32 body_i = 0u;
	for (i = 0u; i < t->row_count; ++i) {
		if (t->rows[i].is_header != 0) {
			tbl_apply_row_bg(ctx, t, &t->rows[i], 0u);
		} else {
			tbl_apply_row_bg(ctx, t, &t->rows[i], body_i);
			body_i += 1u;
		}
	}
}

static void tbl_apply_cell_widths(sk_ui_context_t* ctx, ui_table_data_t* t) {
	const sk_ui_api_t* ui = tbl_api();
	u32 r;
	u32 c;
	for (r = 0u; r < t->row_count; ++r) {
		ui_table_row_t* row = &t->rows[r];
		for (c = 0u; c < t->column_count; ++c) {
			sk_ui_style_props_t p;
			f32 w = t->columns[c].width;
			i32 indent;
			if (!sk_ui_node_is_valid(row->cells[c])) {
				continue;
			}
			if (w < 8.0f) {
				w = 8.0f;
			}
			ui_style_props_clear(&p);
			p.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
			p.layout.width = sk_ui_pt(w);
			p.layout.min_width = sk_ui_pt(w);
			p.layout.max_width = sk_ui_pt(w);
			p.layout.flex_grow = 0.0f;
			p.layout.flex_shrink = 0.0f;
			indent = 0;
			if ((t->columns[c].flags & SK_UI_TABLE_COLUMN_FLAG_INDENT_ENABLE) != 0u || ((t->columns[c].flags & SK_UI_TABLE_COLUMN_FLAG_INDENT_DISABLE) == 0u && c == 0u)) {
				indent = 1;
			}
			if (indent != 0 && row->is_header == 0) {
				p.mask |= SK_UI_SP_PADDING;
				p.layout.padding.left = 6.0f + SK_UI_TREE_INDENT;
				p.layout.padding.right = 6.0f;
				p.layout.padding.top = 3.0f;
				p.layout.padding.bottom = 3.0f;
			}
			(void)ui->node_merge_inline_style(ctx, row->cells[c], &p);
			(void)ui->node_set_prop_i32(ctx, row->cells[c], "indent", indent);
		}
	}
}

static void tbl_fill_default_columns(ui_table_data_t* t) {
	u32 i;
	for (i = t->setup_count; i < t->column_count; ++i) {
		t->columns[i].label[0] = '\0';
		t->columns[i].flags = SK_UI_TABLE_COLUMN_FLAG_NONE;
		t->columns[i].init_width = 0.0f;
		t->columns[i].width = SK_UI_TABLE_DEFAULT_COL_WIDTH;
		t->columns[i].weight = 1.0f;
	}
	t->setup_count = t->column_count;
}

static f32 tbl_col_fit_width(const ui_table_data_t* t, u32 col) {
	f32 w = t->columns[col].init_width;
	if (w > 0.0f && !tbl_col_is_stretch(t, col)) {
		return w;
	}
	if (w > 0.0f && tbl_col_is_stretch(t, col)) {
		return SK_UI_TABLE_DEFAULT_COL_WIDTH;
	}
	return SK_UI_TABLE_DEFAULT_COL_WIDTH;
}

i32 ui_table_resolve_column_widths_impl(sk_ui_context_t* ctx, sk_ui_node_t table, f32 avail_width) {
	ui_table_data_t* t = tbl_data(ctx, table);
	u32 i;
	u32 sizing;
	f32 fixed_sum = 0.0f;
	f32 weight_sum = 0.0f;
	f32 leftover;
	u32 stretch_n = 0u;
	f32 max_fit = 0.0f;
	if (t == NULL || t->column_count == 0u) {
		return -1;
	}
	tbl_fill_default_columns(t);
	sizing = tbl_sizing_mask(t->flags);
	if (avail_width < 0.0f) {
		avail_width = 0.0f;
	}

	if (sizing == SK_UI_TABLE_FLAG_SIZING_FIXED_SAME) {
		for (i = 0u; i < t->column_count; ++i) {
			f32 fit = tbl_col_fit_width(t, i);
			if (fit > max_fit) {
				max_fit = fit;
			}
		}
		if (max_fit < 8.0f) {
			max_fit = SK_UI_TABLE_DEFAULT_COL_WIDTH;
		}
		if (avail_width > 0.0f && avail_width > max_fit * (f32)t->column_count) {
			max_fit = avail_width / (f32)t->column_count;
		}
		for (i = 0u; i < t->column_count; ++i) {
			t->columns[i].width = max_fit;
			t->columns[i].weight = 1.0f;
		}
		tbl_apply_cell_widths(ctx, t);
		return 0;
	}

	for (i = 0u; i < t->column_count; ++i) {
		if (tbl_col_is_stretch(t, i)) {
			f32 w = t->columns[i].init_width;
			t->columns[i].weight = w > 0.0f ? w : 1.0f;
			weight_sum += t->columns[i].weight;
			stretch_n += 1u;
		} else {
			t->columns[i].width = tbl_col_fit_width(t, i);
			t->columns[i].weight = 0.0f;
			fixed_sum += t->columns[i].width;
		}
	}

	if (stretch_n == 0u) {
		tbl_apply_cell_widths(ctx, t);
		return 0;
	}
	leftover = avail_width > fixed_sum ? avail_width - fixed_sum : 0.0f;
	if (leftover < 1.0f) {
		leftover = SK_UI_TABLE_DEFAULT_COL_WIDTH * (f32)stretch_n;
	}
	if (weight_sum <= 0.0f) {
		weight_sum = (f32)stretch_n;
	}
	for (i = 0u; i < t->column_count; ++i) {
		if (tbl_col_is_stretch(t, i)) {
			t->columns[i].width = leftover * (t->columns[i].weight / weight_sum);
			if (t->columns[i].width < 8.0f) {
				t->columns[i].width = 8.0f;
			}
		}
	}
	tbl_apply_cell_widths(ctx, t);
	return 0;
}

static f32 tbl_avail_width(const ui_table_data_t* t) {
	if (t->outer_w > 0.0f) {
		return t->outer_w;
	}
	return SK_UI_TABLE_DEFAULT_COL_WIDTH * (f32)t->column_count;
}

static void tbl_resolve(sk_ui_context_t* ctx, ui_table_data_t* t) {
	(void)ui_table_resolve_column_widths_impl(ctx, t->table, tbl_avail_width(t));
	tbl_apply_borders(ctx, t);
	tbl_apply_all_row_bg(ctx, t);
}

static i32 tbl_grow_rows(sk_ui_context_t* ctx, ui_table_data_t* t, u32 need) {
	ui_table_row_t* next;
	u32 cap = t->row_cap;
	if (need <= cap) {
		return 0;
	}
	if (cap == 0u) {
		cap = 8u;
	}
	while (cap < need) {
		cap *= 2u;
	}
	next = (ui_table_row_t*)ctx->allocator->alloc(ctx->allocator->instance, sizeof(ui_table_row_t) * cap);
	if (next == NULL) {
		return -1;
	}
	memset(next, 0, sizeof(ui_table_row_t) * cap);
	if (t->rows != NULL && t->row_count > 0u) {
		memcpy(next, t->rows, sizeof(ui_table_row_t) * t->row_count);
		ctx->allocator->free(ctx->allocator->instance, t->rows);
	}
	t->rows = next;
	t->row_cap = cap;
	return 0;
}

static void tbl_resize_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);

static sk_ui_node_t tbl_make_cell(sk_ui_context_t* ctx, ui_table_data_t* t, sk_ui_node_t parent, u32 col, i32 is_header) {
	const sk_ui_api_t* ui = tbl_api();
	sk_ui_node_t cell;
	char idbuf[96];
	const_chr_t tid;
	cell = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	if (!sk_ui_node_is_valid(cell)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->node_add_class(ctx, cell, SK_UI_CLASS_TABLE_CELL);
	(void)ui->node_set_prop_str(ctx, cell, "widget", is_header != 0 ? "table_header_cell" : "table_cell");
	(void)ui->node_set_prop_i32(ctx, cell, "column", (i32)col);
	if (is_header != 0) {
		(void)ui->node_set_focusable(ctx, cell, 1);
	}
	tid = ui->node_get_id(ctx, t->table);
	(void)snprintf(idbuf, sizeof(idbuf), "%s/c%u-%u", tid != NULL ? tid : "table", t->row_count, col);
	(void)ui->node_set_id(ctx, cell, idbuf);
	if (is_header != 0 && (t->flags & SK_UI_TABLE_FLAG_RESIZABLE) != 0u && (t->columns[col].flags & SK_UI_TABLE_COLUMN_FLAG_NO_RESIZE) == 0u && col + 1u < t->column_count) {
		sk_ui_node_t handle;
		sk_ui_node_callbacks_t cbs;
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_event = tbl_resize_on_event;
		cbs.user = t;
		(void)ui->node_set_callbacks(ctx, cell, &cbs);
		handle = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, cell);
		if (sk_ui_node_is_valid(handle)) {
			(void)ui->node_add_class(ctx, handle, SK_UI_CLASS_TABLE_RESIZE);
			(void)ui->node_set_prop_str(ctx, handle, "widget", "table_resize");
			(void)ui->node_set_prop_i32(ctx, handle, "column", (i32)col);
			(void)snprintf(idbuf, sizeof(idbuf), "%s/rz%u", tid != NULL ? tid : "table", col);
			(void)ui->node_set_id(ctx, handle, idbuf);
			(void)ui->node_set_focusable(ctx, handle, 1);
			memset(&cbs, 0, sizeof(cbs));
			cbs.on_event = tbl_resize_on_event;
			cbs.user = t;
			(void)ui->node_set_callbacks(ctx, handle, &cbs);
		}
	}
	return cell;
}

static void tbl_fill_cell_label(sk_ui_context_t* ctx, sk_ui_node_t cell, const_chr_t text) {
	const sk_ui_api_t* ui = tbl_api();
	sk_ui_node_t label;
	u32 i;
	u32 n;
	if (!sk_ui_node_is_valid(cell)) {
		return;
	}
	if (text == NULL) {
		text = "";
	}
	n = ui->node_child_count(ctx, cell);
	label = SK_UI_NODE_INVALID;
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t c = ui->node_child_at(ctx, cell, i);
		sk_ui_prop_value_t pv;
		if (ui->node_get_prop(ctx, c, "widget", &pv) == 0 && pv.type == SK_UI_PROP_STR && pv.data.str_value != NULL &&
			(strcmp(pv.data.str_value, "label") == 0 || strcmp(pv.data.str_value, "text") == 0)) {
			label = c;
			break;
		}
	}
	if (!sk_ui_node_is_valid(label)) {
		char lid[96];
		const_chr_t cid = ui->node_get_id(ctx, cell);
		(void)snprintf(lid, sizeof(lid), "%s/lbl", cid != NULL ? cid : "cell");
		label = ui->widget_text(ctx, cell, text, lid);
		if (sk_ui_node_is_valid(label)) {
			sk_ui_style_props_t lp;
			(void)ui->node_set_pointer_events(ctx, label, SK_UI_POINTER_EVENTS_NONE);
			ui_style_props_clear(&lp);
			lp.mask = SK_UI_SP_FLEX_SHRINK | SK_UI_SP_FLEX_GROW;
			lp.layout.flex_shrink = 0.0f;
			lp.layout.flex_grow = 1.0f;
			(void)ui->node_merge_inline_style(ctx, label, &lp);
		}
	} else {
		(void)ui->label_set_text(ctx, label, text);
	}
}

static sk_ui_node_t tbl_row_parent(ui_table_data_t* t, i32 is_header) {
	if (is_header != 0) {
		return t->table;
	}
	if (sk_ui_node_is_valid(t->body_content)) {
		return t->body_content;
	}
	if (sk_ui_node_is_valid(t->body)) {
		return t->body;
	}
	return t->table;
}

static i32 tbl_add_row(sk_ui_context_t* ctx, ui_table_data_t* t, u32 flags, f32 min_height, u64 item_id, i32 is_header) {
	const sk_ui_api_t* ui = tbl_api();
	ui_table_row_t* row;
	sk_ui_node_t parent;
	sk_ui_style_props_t p;
	char idbuf[96];
	const_chr_t tid;
	u32 c;
	u32 freeze;
	if (tbl_grow_rows(ctx, t, t->row_count + 1u) != 0) {
		return -1;
	}
	tbl_fill_default_columns(t);
	row = &t->rows[t->row_count];
	memset(row, 0, sizeof(*row));
	row->item_id = item_id;
	row->flags = flags;
	row->min_height = min_height > 0.0f ? min_height : SK_UI_TABLE_ROW_HEIGHT;
	row->is_header = is_header;
	row->hscroll = SK_UI_NODE_INVALID;
	parent = tbl_row_parent(t, is_header);
	row->node = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	if (!sk_ui_node_is_valid(row->node)) {
		return -1;
	}
	(void)ui->node_add_class(ctx, row->node, is_header != 0 ? SK_UI_CLASS_TABLE_HEADER : SK_UI_CLASS_TABLE_ROW);
	(void)ui->node_set_prop_str(ctx, row->node, "widget", is_header != 0 ? "table_header" : "table_row");
	tid = ui->node_get_id(ctx, t->table);
	(void)snprintf(idbuf, sizeof(idbuf), "%s/r%u", tid != NULL ? tid : "table", t->row_count);
	(void)ui->node_set_id(ctx, row->node, idbuf);
	if (item_id != SK_UI_ITEM_ID_NONE) {
		(void)snprintf(idbuf, sizeof(idbuf), "%llu", item_id);
		(void)ui->node_set_prop_str(ctx, row->node, "item_id", idbuf);
		(void)sk_hash_map_put(&t->id_to_row, item_id, t->row_count);
	}
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_MIN_HEIGHT;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_STRETCH;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.min_height = sk_ui_pt(row->min_height);
	(void)ui->node_merge_inline_style(ctx, row->node, &p);

	freeze = t->freeze_cols > 0 ? (u32)t->freeze_cols : 0u;
	if (freeze > t->column_count) {
		freeze = t->column_count;
	}
	for (c = 0u; c < freeze && c < t->column_count; ++c) {
		row->cells[c] = tbl_make_cell(ctx, t, row->node, c, is_header);
		if (is_header != 0) {
			tbl_fill_cell_label(ctx, row->cells[c], t->columns[c].label);
		}
	}
	if (freeze > 0u && (t->flags & SK_UI_TABLE_FLAG_SCROLL_X) != 0u && freeze < t->column_count) {
		sk_ui_node_t hs;
		hs = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, row->node);
		row->hscroll = hs;
		if (sk_ui_node_is_valid(hs)) {
			(void)ui->node_add_class(ctx, hs, SK_UI_CLASS_TABLE_BODY);
			(void)ui->node_set_prop_str(ctx, hs, "widget", "table_hscroll");
			(void)snprintf(idbuf, sizeof(idbuf), "%s/hs%u", tid != NULL ? tid : "table", t->row_count);
			(void)ui->node_set_id(ctx, hs, idbuf);
			(void)ui->node_set_clip_children(ctx, hs, 1);
			(void)ui->node_set_prop_f32(ctx, hs, "scroll_x", 0.0f);
			(void)ui->node_set_prop_f32(ctx, hs, "scroll_y", 0.0f);
			ui_style_props_clear(&p);
			p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_WIDTH | SK_UI_SP_HEIGHT;
			p.layout.flex_direction = SK_UI_FLEX_ROW;
			p.layout.align_items = SK_UI_ALIGN_STRETCH;
			p.layout.flex_grow = 1.0f;
			p.layout.min_width = sk_ui_pt(0.0f);
			p.layout.height = sk_ui_percent(100.0f);
			(void)ui->node_merge_inline_style(ctx, hs, &p);
		}
	}
	for (c = freeze; c < t->column_count; ++c) {
		sk_ui_node_t cell_parent = row->node;
		if (sk_ui_node_is_valid(row->hscroll)) {
			cell_parent = row->hscroll;
		}
		row->cells[c] = tbl_make_cell(ctx, t, cell_parent, c, is_header);
		if (is_header != 0) {
			tbl_fill_cell_label(ctx, row->cells[c], t->columns[c].label);
		}
	}
	if (is_header != 0) {
		t->header = row->node;
		t->has_header = 1;
		if (sk_ui_node_is_valid(t->body)) {
			(void)ui->node_set_child_index(ctx, t->table, row->node, 0u);
		}
	}
	t->row_count += 1u;
	return (i32)t->row_count - 1;
}

static void tbl_resize_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = tbl_api();
	ui_table_data_t* t = (ui_table_data_t*)user;
	const ui_node_slot_t* slot;
	i32 col = 0;
	if (t == NULL || event == NULL) {
		return;
	}
	slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return;
	}
	{
		u32 i;
		for (i = 0u; i < slot->props.count; ++i) {
			if (slot->props.items[i].type == SK_UI_PROP_I32 && slot->props.items[i].key != NULL && strcmp(slot->props.items[i].key, "column") == 0) {
				col = slot->props.items[i].data.i32_value;
				break;
			}
		}
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN && event->button == SK_UI_POINTER_BUTTON_LEFT) {
		sk_ui_rect_t br;
		const_chr_t wtype = NULL;
		{
			u32 i;
			for (i = 0u; i < slot->props.count; ++i) {
				if (slot->props.items[i].type == SK_UI_PROP_STR && slot->props.items[i].key != NULL && strcmp(slot->props.items[i].key, "widget") == 0) {
					wtype = slot->props.items[i].data.str_value;
					break;
				}
			}
		}
		if (wtype != NULL && strcmp(wtype, "table_header_cell") == 0) {
			if (ui->node_get_abs_rect(ctx, node, &br, NULL) != 0 || event->x < br.x + br.width - 8.0f) {
				return;
			}
		}
		t->drag_col = col;
		t->drag_start_x = event->x;
		t->drag_start_w = (col >= 0 && (u32)col < t->column_count) ? t->columns[col].width : SK_UI_TABLE_DEFAULT_COL_WIDTH;
		(void)ui->pointer_capture_set(ctx, node);
		event->consumed = 1;
		return;
	}
	if (t->drag_col < 0) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_MOVE) {
		f32 w = t->drag_start_w + (event->x - t->drag_start_x);
		if (w < 16.0f) {
			w = 16.0f;
		}
		(void)ui_table_set_column_width_impl(ctx, t->table, t->drag_col, w);
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		t->drag_col = -1;
		if (sk_ui_node_eq(ui->pointer_capture_get(ctx), node)) {
			(void)ui->pointer_capture_set(ctx, SK_UI_NODE_INVALID);
		}
		event->consumed = 1;
	}
}

static void tbl_ensure_body(sk_ui_context_t* ctx, ui_table_data_t* t) {
	const sk_ui_api_t* ui = tbl_api();
	char idbuf[96];
	const_chr_t tid;
	i32 want_scroll = ((t->flags & (SK_UI_TABLE_FLAG_SCROLL_X | SK_UI_TABLE_FLAG_SCROLL_Y)) != 0u) ? 1 : 0;
	if (sk_ui_node_is_valid(t->body)) {
		return;
	}
	tid = ui->node_get_id(ctx, t->table);
	if (want_scroll != 0) {
		u32 child_flags = SK_UI_CHILD_FLAG_NONE;
		if ((t->flags & SK_UI_TABLE_FLAG_SCROLL_X) != 0u) {
			child_flags |= SK_UI_CHILD_FLAG_HORIZONTAL_SCROLLBAR;
		}
		(void)snprintf(idbuf, sizeof(idbuf), "%s/body", tid != NULL ? tid : "table");
		t->body = ui->widget_child(ctx, t->table, idbuf, 0.0f, 0.0f, child_flags);
		(void)ui->node_add_class(ctx, t->body, SK_UI_CLASS_TABLE_BODY);
		(void)ui->node_set_prop_str(ctx, t->body, "widget", "table_body");
		t->body_content = ui->child_content(ctx, t->body);
	} else {
		t->body = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, t->table);
		(void)ui->node_add_class(ctx, t->body, SK_UI_CLASS_TABLE_BODY);
		(void)ui->node_set_prop_str(ctx, t->body, "widget", "table_body");
		(void)snprintf(idbuf, sizeof(idbuf), "%s/body", tid != NULL ? tid : "table");
		(void)ui->node_set_id(ctx, t->body, idbuf);
		t->body_content = t->body;
	}
}

static void tbl_copy_hscroll(sk_ui_context_t* ctx, ui_table_data_t* t, f32 sx) {
	const sk_ui_api_t* ui = tbl_api();
	u32 i;
	for (i = 0u; i < t->row_count; ++i) {
		if (sk_ui_node_is_valid(t->rows[i].hscroll)) {
			(void)ui->node_set_prop_f32(ctx, t->rows[i].hscroll, "scroll_x", sx);
		}
	}
}

static i32 tbl_find_row_by_item(ui_table_data_t* t, u64 id, u32* out) {
	u32 idx;
	if (id == SK_UI_ITEM_ID_NONE) {
		return 0;
	}
	if (sk_hash_map_get(&t->id_to_row, id, &idx) == 0 && idx < t->row_count) {
		*out = idx;
		return 1;
	}
	return 0;
}

static void tbl_destroy_row(sk_ui_context_t* ctx, ui_table_data_t* t, u32 idx) {
	const sk_ui_api_t* ui = tbl_api();
	ui_table_row_t* row;
	if (idx >= t->row_count) {
		return;
	}
	row = &t->rows[idx];
	if (row->item_id != SK_UI_ITEM_ID_NONE) {
		(void)sk_hash_map_remove(&t->id_to_row, row->item_id);
	}
	if (sk_ui_node_is_valid(row->node) && ui->node_alive(ctx, row->node)) {
		(void)ui->node_destroy(ctx, row->node);
	}
	if (idx + 1u < t->row_count) {
		memmove(&t->rows[idx], &t->rows[idx + 1u], sizeof(ui_table_row_t) * (t->row_count - idx - 1u));
	}
	t->row_count -= 1u;
	{
		u32 i;
		sk_hash_map_clear(&t->id_to_row);
		for (i = 0u; i < t->row_count; ++i) {
			if (t->rows[i].item_id != SK_UI_ITEM_ID_NONE) {
				(void)sk_hash_map_put(&t->id_to_row, t->rows[i].item_id, i);
			}
		}
	}
}

static i32 tbl_sync_items(sk_ui_context_t* ctx, ui_table_data_t* t) {
	const sk_ui_api_t* ui = tbl_api();
	u32 i;
	u32 keep_from;
	if (t->items == NULL) {
		return 0;
	}
	keep_from = t->has_header != 0 ? 1u : 0u;
	if (t->items->items == NULL || t->items->count == 0u) {
		while (t->row_count > keep_from) {
			tbl_destroy_row(ctx, t, t->row_count - 1u);
		}
		t->last_item_count = 0u;
		t->last_revision = t->items->revision;
		return 0;
	}
	/* Drop data rows whose ids disappeared. Walk from the end. */
	i = t->row_count;
	while (i > keep_from) {
		u32 idx = i - 1u;
		u64 id = t->rows[idx].item_id;
		i32 found = 0;
		u32 k;
		if (t->rows[idx].is_header != 0) {
			i = idx;
			continue;
		}
		for (k = 0u; k < t->items->count; ++k) {
			if (t->items->items[k].id == id && id != SK_UI_ITEM_ID_NONE) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			tbl_destroy_row(ctx, t, idx);
		}
		i = idx;
	}
	for (i = 0u; i < t->items->count; ++i) {
		const sk_ui_item_t* it = &t->items->items[i];
		u32 existing = 0u;
		i32 row_i;
		if (it->id == SK_UI_ITEM_ID_NONE) {
			continue;
		}
		if (tbl_find_row_by_item(t, it->id, &existing) != 0) {
			row_i = (i32)existing;
			tbl_fill_cell_label(ctx, t->rows[existing].cells[0], it->label != NULL ? it->label : "");
		} else {
			row_i = tbl_add_row(ctx, t, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f, it->id, 0);
			if (row_i < 0) {
				return -1;
			}
			tbl_fill_cell_label(ctx, t->rows[row_i].cells[0], it->label != NULL ? it->label : "");
		}
		{
			u32 want = keep_from + i;
			if ((u32)row_i != want && want < t->row_count && sk_ui_node_is_valid(t->rows[row_i].node)) {
				(void)ui->node_set_child_index(ctx, tbl_row_parent(t, 0), t->rows[row_i].node, want - keep_from);
			}
		}
	}
	t->last_item_count = t->items->count;
	t->last_revision = t->items->revision;
	t->cursor_row = (i32)t->row_count - 1;
	t->cursor_col = -1;
	tbl_resolve(ctx, t);
	return 0;
}

void ui_table_sync_all(sk_ui_context_t* ctx) {
	u32 i = 0u;
	while (i < ctx->tables.count) {
		sk_ui_node_t n = ctx->tables.items[i];
		ui_table_data_t* t = tbl_data(ctx, n);
		if (t == NULL) {
			tbl_unreg(ctx, n);
			continue;
		}
		(void)tbl_sync_items(ctx, t);
		++i;
	}
}

i32 ui_table_bind_items_impl(sk_ui_context_t* ctx, sk_ui_node_t table, sk_ui_item_array_t* items) {
	ui_table_data_t* t = tbl_data(ctx, table);
	if (t == NULL) {
		return -1;
	}
	t->items = items;
	return tbl_sync_items(ctx, t);
}

i32 ui_table_bind_if_table(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_array_t* items) {
	if (ui_table_is_table(ctx, host) == 0) {
		return -1;
	}
	return ui_table_bind_items_impl(ctx, host, items);
}

sk_ui_node_t ui_widget_table_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id, i32 columns, u32 flags, f32 outer_width, f32 outer_height) {
	const sk_ui_api_t* ui = tbl_api();
	sk_ui_node_t n;
	ui_table_data_t* t;
	char auto_id[64];
	if (ctx == NULL || columns < 1 || columns > (i32)SK_UI_TABLE_MAX_COLUMNS) {
		return SK_UI_NODE_INVALID;
	}
	n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	if (!sk_ui_node_is_valid(n)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->node_add_class(ctx, n, SK_UI_CLASS_TABLE);
	(void)ui->node_set_prop_str(ctx, n, "widget", "table");
	if (id != NULL && id[0] != '\0') {
		(void)ui->node_set_id(ctx, n, id);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(auto_id, sizeof(auto_id), "table-%u", ctx->widget_id_seq);
		(void)ui->node_set_id(ctx, n, auto_id);
	}
	t = (ui_table_data_t*)ctx->allocator->alloc(ctx->allocator->instance, sizeof(ui_table_data_t));
	if (t == NULL) {
		(void)ui->node_destroy(ctx, n);
		return SK_UI_NODE_INVALID;
	}
	memset(t, 0, sizeof(*t));
	t->column_count = (u32)columns;
	t->flags = flags;
	t->outer_w = outer_width;
	t->outer_h = outer_height;
	t->cursor_row = -1;
	t->cursor_col = -1;
	t->drag_col = -1;
	t->table = n;
	t->header = SK_UI_NODE_INVALID;
	t->body = SK_UI_NODE_INVALID;
	t->body_content = SK_UI_NODE_INVALID;
	sk_hash_map_init(&t->id_to_row, ctx->allocator, NULL, NULL);
	{
		ui_node_slot_t* slot = ui_slot_mut(ctx, n);
		if (slot == NULL) {
			sk_hash_map_free(&t->id_to_row);
			ctx->allocator->free(ctx->allocator->instance, t);
			(void)ui->node_destroy(ctx, n);
			return SK_UI_NODE_INVALID;
		}
		slot->user_data = t;
		slot->user_data_type = SK_UI_TABLE_DATA_TYPE_ID;
	}
	(void)sk_array_push(&ctx->tables, n);
	tbl_apply_outer_size(ctx, n, outer_width, outer_height);
	tbl_ensure_body(ctx, t);
	(void)ui->node_set_prop_i32(ctx, n, "columns", columns);
	(void)ui->node_set_prop_i32(ctx, n, "table_flags", (i32)flags);
	return n;
}

i32 ui_table_end_impl(sk_ui_context_t* ctx, sk_ui_node_t table) {
	ui_table_data_t* t = tbl_data(ctx, table);
	if (t == NULL) {
		return -1;
	}
	tbl_fill_default_columns(t);
	tbl_resolve(ctx, t);
	if (sk_ui_node_is_valid(t->body) && (t->flags & (SK_UI_TABLE_FLAG_SCROLL_X | SK_UI_TABLE_FLAG_SCROLL_Y)) != 0u) {
		f32 cw = 0.0f;
		f32 ch = 0.0f;
		u32 i;
		for (i = 0u; i < t->column_count; ++i) {
			cw += t->columns[i].width;
		}
		ch = (f32)t->row_count * SK_UI_TABLE_ROW_HEIGHT;
		(void)tbl_api()->scroll_view_set_content_size(ctx, t->body, cw, ch);
	}
	return 0;
}

i32 ui_table_setup_column_impl(sk_ui_context_t* ctx, sk_ui_node_t table, const_chr_t label, u32 flags, f32 init_width_or_weight) {
	ui_table_data_t* t = tbl_data(ctx, table);
	ui_table_column_t* col;
	if (t == NULL || t->setup_count >= t->column_count) {
		return -1;
	}
	col = &t->columns[t->setup_count];
	tbl_copy_label(col->label, (u32)sizeof(col->label), label);
	col->flags = flags;
	col->init_width = init_width_or_weight;
	col->width = init_width_or_weight > 0.0f && (flags & SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH) == 0u ? init_width_or_weight : SK_UI_TABLE_DEFAULT_COL_WIDTH;
	col->weight = init_width_or_weight > 0.0f ? init_width_or_weight : 1.0f;
	t->setup_count += 1u;
	return 0;
}

i32 ui_table_headers_row_impl(sk_ui_context_t* ctx, sk_ui_node_t table) {
	ui_table_data_t* t = tbl_data(ctx, table);
	i32 row;
	if (t == NULL) {
		return -1;
	}
	if (t->has_header != 0) {
		t->cursor_row = 0;
		t->cursor_col = -1;
		return 0;
	}
	row = tbl_add_row(ctx, t, SK_UI_TABLE_ROW_FLAG_HEADERS, SK_UI_TABLE_ROW_HEIGHT, SK_UI_ITEM_ID_NONE, 1);
	if (row < 0) {
		return -1;
	}
	t->cursor_row = row;
	t->cursor_col = -1;
	tbl_resolve(ctx, t);
	return 0;
}

i32 ui_table_next_row_impl(sk_ui_context_t* ctx, sk_ui_node_t table, u32 row_flags, f32 min_row_height) {
	ui_table_data_t* t = tbl_data(ctx, table);
	i32 is_header;
	i32 row;
	if (t == NULL) {
		return -1;
	}
	is_header = ((row_flags & SK_UI_TABLE_ROW_FLAG_HEADERS) != 0u) ? 1 : 0;
	if (is_header != 0 && t->has_header != 0) {
		t->cursor_row = 0;
		t->cursor_col = -1;
		return 0;
	}
	row = tbl_add_row(ctx, t, row_flags, min_row_height, SK_UI_ITEM_ID_NONE, is_header);
	if (row < 0) {
		return -1;
	}
	t->cursor_row = row;
	t->cursor_col = -1;
	return 0;
}

i32 ui_table_next_column_impl(sk_ui_context_t* ctx, sk_ui_node_t table) {
	ui_table_data_t* t = tbl_data(ctx, table);
	if (t == NULL) {
		return 0;
	}
	if (t->cursor_row < 0) {
		if (ui_table_next_row_impl(ctx, table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f) != 0) {
			return 0;
		}
	}
	t->cursor_col += 1;
	if (t->cursor_col >= (i32)t->column_count) {
		if (ui_table_next_row_impl(ctx, table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f) != 0) {
			return 0;
		}
		t->cursor_col = 0;
	}
	return 1;
}

i32 ui_table_set_column_index_impl(sk_ui_context_t* ctx, sk_ui_node_t table, i32 column_n) {
	ui_table_data_t* t = tbl_data(ctx, table);
	if (t == NULL || column_n < 0 || (u32)column_n >= t->column_count) {
		return 0;
	}
	if (t->cursor_row < 0) {
		if (ui_table_next_row_impl(ctx, table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f) != 0) {
			return 0;
		}
	}
	t->cursor_col = column_n;
	return 1;
}

sk_ui_node_t ui_table_current_cell_impl(const sk_ui_context_t* ctx, sk_ui_node_t table) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	if (t == NULL || t->cursor_row < 0 || t->cursor_col < 0) {
		return SK_UI_NODE_INVALID;
	}
	if ((u32)t->cursor_row >= t->row_count || (u32)t->cursor_col >= t->column_count) {
		return SK_UI_NODE_INVALID;
	}
	return t->rows[t->cursor_row].cells[t->cursor_col];
}

sk_ui_node_t ui_table_get_cell_impl(const sk_ui_context_t* ctx, sk_ui_node_t table, i32 row, i32 column) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	if (t == NULL || row < 0 || column < 0) {
		return SK_UI_NODE_INVALID;
	}
	if ((u32)row >= t->row_count || (u32)column >= t->column_count) {
		return SK_UI_NODE_INVALID;
	}
	return t->rows[row].cells[column];
}

sk_ui_node_t ui_table_get_row_impl(const sk_ui_context_t* ctx, sk_ui_node_t table, i32 row) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	if (t == NULL || row < 0 || (u32)row >= t->row_count) {
		return SK_UI_NODE_INVALID;
	}
	return t->rows[row].node;
}

i32 ui_table_get_current_row_impl(const sk_ui_context_t* ctx, sk_ui_node_t table) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	return t != NULL ? t->cursor_row : -1;
}

i32 ui_table_get_current_column_impl(const sk_ui_context_t* ctx, sk_ui_node_t table) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	return t != NULL ? t->cursor_col : -1;
}

i32 ui_table_get_column_count_impl(const sk_ui_context_t* ctx, sk_ui_node_t table) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	return t != NULL ? (i32)t->column_count : 0;
}

u32 ui_table_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t table) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	return t != NULL ? t->flags : 0u;
}

i32 ui_table_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t table, u32 flags) {
	ui_table_data_t* t = tbl_data(ctx, table);
	if (t == NULL) {
		return -1;
	}
	t->flags = flags;
	(void)tbl_api()->node_set_prop_i32(ctx, table, "table_flags", (i32)flags);
	tbl_resolve(ctx, t);
	return 0;
}

u32 ui_table_get_column_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t table, i32 column) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	if (t == NULL || column < 0 || (u32)column >= t->column_count) {
		return 0u;
	}
	return t->columns[column].flags;
}

i32 ui_table_get_column_width_impl(const sk_ui_context_t* ctx, sk_ui_node_t table, i32 column, f32* out_width) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	if (t == NULL || column < 0 || (u32)column >= t->column_count || out_width == NULL) {
		return -1;
	}
	*out_width = t->columns[column].width;
	return 0;
}

i32 ui_table_set_column_width_impl(sk_ui_context_t* ctx, sk_ui_node_t table, i32 column, f32 width) {
	ui_table_data_t* t = tbl_data(ctx, table);
	if (t == NULL || column < 0 || (u32)column >= t->column_count) {
		return -1;
	}
	if (width < 8.0f) {
		width = 8.0f;
	}
	t->columns[column].init_width = width;
	t->columns[column].width = width;
	t->columns[column].flags |= SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED;
	t->columns[column].flags &= ~SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH;
	tbl_resolve(ctx, t);
	return 0;
}

i32 ui_table_set_bg_color_impl(sk_ui_context_t* ctx, sk_ui_node_t table, sk_ui_table_bg_target_t target, sk_ui_color_t color, i32 column_n) {
	ui_table_data_t* t = tbl_data(ctx, table);
	ui_table_row_t* row;
	u32 c;
	if (t == NULL || t->cursor_row < 0 || (u32)t->cursor_row >= t->row_count) {
		return -1;
	}
	row = &t->rows[t->cursor_row];
	if (target == SK_UI_TABLE_BG_CELL && column_n >= 0) {
		if ((u32)column_n >= t->column_count) {
			return -1;
		}
		row->cell_bg_set[column_n] = 1;
		row->cell_bg[column_n] = color;
	} else if (target == SK_UI_TABLE_BG_CELL || target == SK_UI_TABLE_BG_ROW_BG0 || target == SK_UI_TABLE_BG_ROW_BG1) {
		row->bg_set = 1;
		row->row_bg = color;
		for (c = 0u; c < t->column_count; ++c) {
			row->cell_bg_set[c] = 1;
			row->cell_bg[c] = color;
		}
	} else {
		return -1;
	}
	tbl_apply_row_bg(ctx, t, row, (u32)t->cursor_row);
	return 0;
}

i32 ui_table_setup_scroll_freeze_impl(sk_ui_context_t* ctx, sk_ui_node_t table, i32 cols, i32 rows) {
	ui_table_data_t* t = tbl_data(ctx, table);
	if (t == NULL) {
		return -1;
	}
	if (cols < 0) {
		cols = 0;
	}
	if (rows < 0) {
		rows = 0;
	}
	if (cols > (i32)t->column_count) {
		cols = (i32)t->column_count;
	}
	t->freeze_cols = cols;
	t->freeze_rows = rows;
	(void)tbl_api()->node_set_prop_i32(ctx, table, "freeze_cols", cols);
	(void)tbl_api()->node_set_prop_i32(ctx, table, "freeze_rows", rows);
	return 0;
}

i32 ui_table_get_scroll_freeze_impl(const sk_ui_context_t* ctx, sk_ui_node_t table, i32* out_cols, i32* out_rows) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	if (t == NULL) {
		return -1;
	}
	if (out_cols != NULL) {
		*out_cols = t->freeze_cols;
	}
	if (out_rows != NULL) {
		*out_rows = t->freeze_rows;
	}
	return 0;
}

sk_ui_node_t ui_table_body_impl(const sk_ui_context_t* ctx, sk_ui_node_t table) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	return t != NULL ? t->body : SK_UI_NODE_INVALID;
}

sk_ui_node_t ui_table_header_impl(const sk_ui_context_t* ctx, sk_ui_node_t table) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	return t != NULL ? t->header : SK_UI_NODE_INVALID;
}

i32 ui_table_get_scroll_impl(const sk_ui_context_t* ctx, sk_ui_node_t table, f32* out_x, f32* out_y) {
	const ui_table_data_t* t = tbl_data_const(ctx, table);
	if (t == NULL || !sk_ui_node_is_valid(t->body)) {
		return -1;
	}
	return tbl_api()->scroll_view_get_scroll(ctx, t->body, out_x, out_y);
}

i32 ui_table_set_scroll_impl(sk_ui_context_t* ctx, sk_ui_node_t table, f32 scroll_x, f32 scroll_y) {
	ui_table_data_t* t = tbl_data(ctx, table);
	i32 rc;
	if (t == NULL || !sk_ui_node_is_valid(t->body)) {
		return -1;
	}
	rc = tbl_api()->scroll_view_set_scroll(ctx, t->body, scroll_x, scroll_y);
	tbl_copy_hscroll(ctx, t, scroll_x);
	return rc;
}

#ifdef SK_TESTS

static void tbl_test_fill_row(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t table, const_chr_t a, const_chr_t b, const_chr_t c) {
	TEST_ASSERT_EQUAL_INT(0, ui->table_next_row(ctx, table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f));
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
	(void)ui->widget_text(ctx, ui->table_current_cell(ctx, table), a, NULL);
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
	(void)ui->widget_text(ctx, ui->table_current_cell(ctx, table), b, NULL);
	if (c != NULL) {
		TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
		(void)ui->widget_text(ctx, ui->table_current_cell(ctx, table), c, NULL);
	}
}

SK_TEST(ui_widget_table_column_width_resolution) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t fit;
	sk_ui_node_t same;
	sk_ui_node_t stretch;
	f32 w0 = 0.0f;
	f32 w1 = 0.0f;
	f32 w2 = 0.0f;

	fit = ui->widget_table(ctx, root, "tw-fit", 3, SK_UI_TABLE_FLAG_SIZING_FIXED_FIT, 400.0f, 80.0f);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(fit));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, fit, "A", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 80.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, fit, "B", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 120.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, fit, "C", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 0.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_resolve_column_widths(ctx, fit, 400.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, fit, 0, &w0));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, fit, 1, &w1));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, fit, 2, &w2));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 80.0f, w0);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, w1);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, SK_UI_TABLE_DEFAULT_COL_WIDTH, w2);

	same = ui->widget_table(ctx, root, "tw-same", 3, SK_UI_TABLE_FLAG_SIZING_FIXED_SAME, 300.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, same, "A", SK_UI_TABLE_COLUMN_FLAG_NONE, 80.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, same, "B", SK_UI_TABLE_COLUMN_FLAG_NONE, 120.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, same, "C", SK_UI_TABLE_COLUMN_FLAG_NONE, 40.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_resolve_column_widths(ctx, same, 300.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, same, 0, &w0));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, same, 1, &w1));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, same, 2, &w2));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, w0);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, w1);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, w2);

	stretch = ui->widget_table(ctx, root, "tw-st", 3, SK_UI_TABLE_FLAG_SIZING_STRETCH_PROP, 400.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, stretch, "A", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, stretch, "B", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 2.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, stretch, "C", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_resolve_column_widths(ctx, stretch, 400.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, stretch, 0, &w0));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, stretch, 1, &w1));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, stretch, 2, &w2));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, w0);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 200.0f, w1);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, w2);
	TEST_ASSERT_EQUAL_INT(3, ui->table_get_column_count(ctx, stretch));
	TEST_ASSERT_EQUAL_UINT(SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, ui->table_get_column_flags(ctx, stretch, 1));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_table_cell_navigation) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t table;
	sk_ui_node_t c00;
	sk_ui_node_t c01;
	sk_ui_node_t c10;

	table = ui->widget_table(ctx, root, "tn", 2, SK_UI_TABLE_FLAG_NONE, 200.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "L", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "R", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_headers_row(ctx, table));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_current_row(ctx, table));
	TEST_ASSERT_EQUAL_INT(-1, ui->table_get_current_column(ctx, table));

	TEST_ASSERT_EQUAL_INT(0, ui->table_next_row(ctx, table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f));
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
	TEST_ASSERT_EQUAL_INT(1, ui->table_get_current_row(ctx, table));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_current_column(ctx, table));
	c00 = ui->table_current_cell(ctx, table);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(c00));
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
	TEST_ASSERT_EQUAL_INT(1, ui->table_get_current_column(ctx, table));
	c01 = ui->table_current_cell(ctx, table);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(c01));
	TEST_ASSERT_FALSE(sk_ui_node_eq(c00, c01));

	/* Wrap to the next row. */
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
	TEST_ASSERT_EQUAL_INT(2, ui->table_get_current_row(ctx, table));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_current_column(ctx, table));
	c10 = ui->table_current_cell(ctx, table);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(c10));
	TEST_ASSERT_TRUE(sk_ui_node_eq(c10, ui->table_get_cell(ctx, table, 2, 0)));
	TEST_ASSERT_EQUAL_INT(1, ui->table_set_column_index(ctx, table, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->table_get_current_column(ctx, table));
	TEST_ASSERT_EQUAL_INT(0, ui->table_set_column_index(ctx, table, 9));
	TEST_ASSERT_EQUAL_INT(2, ui->table_get_column_count(ctx, table));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->table_get_row(ctx, table, 0)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->table_header(ctx, table)));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_table_border_bg_flags) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t table;
	sk_ui_node_t row1;
	sk_ui_computed_style_t cs;
	u32 flags = SK_UI_TABLE_FLAG_ROW_BG | SK_UI_TABLE_FLAG_BORDERS | SK_UI_TABLE_FLAG_NO_BORDERS_IN_BODY | SK_UI_TABLE_FLAG_RESIZABLE;

	table = ui->widget_table(ctx, root, "tb-bg", 2, flags, 240.0f, 90.0f);
	TEST_ASSERT_EQUAL_UINT(flags, ui->table_get_flags(ctx, table));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Name", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH | SK_UI_TABLE_COLUMN_FLAG_NO_HIDE, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Value", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED | SK_UI_TABLE_COLUMN_FLAG_NO_RESIZE, 80.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_headers_row(ctx, table));
	tbl_test_fill_row(ui, ctx, table, "alpha", "1", NULL);
	tbl_test_fill_row(ui, ctx, table, "beta", "2", NULL);
	TEST_ASSERT_EQUAL_INT(0, ui->table_end(ctx, table));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));

	TEST_ASSERT_TRUE((ui->table_get_column_flags(ctx, table, 0) & SK_UI_TABLE_COLUMN_FLAG_NO_HIDE) != 0u);
	TEST_ASSERT_TRUE((ui->table_get_column_flags(ctx, table, 1) & SK_UI_TABLE_COLUMN_FLAG_NO_RESIZE) != 0u);
	row1 = ui->table_get_row(ctx, table, 2);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row1));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, row1, &cs));
	TEST_ASSERT_TRUE(cs.background_color.a > 0.5f);
	TEST_ASSERT_TRUE(cs.background_color.r + cs.background_color.g + cs.background_color.b > 0.3f);
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_table_scroll_region) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t table;
	sk_ui_node_t body;
	sk_ui_node_t header;
	f32 sx = 0.0f;
	f32 sy = 0.0f;
	i32 fc = 0;
	i32 fr = 0;
	u32 i;
	sk_ui_style_props_t p;

	table = ui->widget_table(ctx, root, "tb-sc", 3, SK_UI_TABLE_FLAG_SCROLL_Y | SK_UI_TABLE_FLAG_SCROLL_X | SK_UI_TABLE_FLAG_BORDERS | SK_UI_TABLE_FLAG_ROW_BG, 0.0f, 0.0f);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(220.0f);
	p.layout.height = sk_ui_pt(120.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, table, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "A", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 80.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "B", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 80.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "C", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 80.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_scroll_freeze(ctx, table, 1, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_scroll_freeze(ctx, table, &fc, &fr));
	TEST_ASSERT_EQUAL_INT(1, fc);
	TEST_ASSERT_EQUAL_INT(1, fr);
	TEST_ASSERT_EQUAL_INT(0, ui->table_headers_row(ctx, table));
	for (i = 0u; i < 12u; ++i) {
		char a[16];
		char b[16];
		(void)snprintf(a, sizeof(a), "r%u", i);
		(void)snprintf(b, sizeof(b), "%u", i * 3u);
		tbl_test_fill_row(ui, ctx, table, a, b, "x");
	}
	TEST_ASSERT_EQUAL_INT(0, ui->table_end(ctx, table));
	body = ui->table_body(ctx, table);
	header = ui->table_header(ctx, table);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(body));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(header));
	TEST_ASSERT_FALSE(sk_ui_node_eq(header, body));
	TEST_ASSERT_EQUAL_INT(0, ui->table_set_scroll(ctx, table, 12.0f, 40.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_scroll(ctx, table, &sx, &sy));
	TEST_ASSERT_TRUE(sy > 1.0f);
	TEST_ASSERT_TRUE(sx > 1.0f);
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_table_row_bg_override) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t table;
	sk_ui_node_t cell;
	sk_ui_computed_style_t cs;
	sk_ui_color_t mark = sk_ui_rgba(0.20f, 0.45f, 0.80f, 1.0f);

	table = ui->widget_table(ctx, root, "tb-ov", 3, SK_UI_TABLE_FLAG_ROW_BG | SK_UI_TABLE_FLAG_BORDERS, 260.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Name", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH | SK_UI_TABLE_COLUMN_FLAG_INDENT_ENABLE, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Vis", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED | SK_UI_TABLE_COLUMN_FLAG_INDENT_DISABLE, 28.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Lock", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED | SK_UI_TABLE_COLUMN_FLAG_INDENT_DISABLE, 28.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_next_row(ctx, table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f));
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
	(void)ui->widget_text(ctx, ui->table_current_cell(ctx, table), "Entity", NULL);
	TEST_ASSERT_EQUAL_INT(0, ui->table_set_bg_color(ctx, table, SK_UI_TABLE_BG_CELL, mark, -1));
	TEST_ASSERT_EQUAL_INT(0, ui->table_end(ctx, table));
	cell = ui->table_get_cell(ctx, table, 0, 0);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(cell));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, cell, &cs));
	TEST_ASSERT_FLOAT_WITHIN(0.08f, mark.r, cs.background_color.r);
	TEST_ASSERT_FLOAT_WITHIN(0.08f, mark.b, cs.background_color.b);
	cell = ui->table_get_cell(ctx, table, 0, 2);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, cell, &cs));
	TEST_ASSERT_FLOAT_WITHIN(0.08f, mark.r, cs.background_color.r);
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_table_item_bind_no_rebuild) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t table;
	sk_ui_item_t items[4];
	sk_ui_item_array_t arr;
	sk_ui_node_t row_a;
	sk_ui_node_t row_a2;

	table = ui->widget_table(ctx, root, "tb-ib", 2, SK_UI_TABLE_FLAG_BORDERS | SK_UI_TABLE_FLAG_ROW_BG, 240.0f, 120.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Pkg", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Ver", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 60.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_headers_row(ctx, table));
	sk_ui_item_set(&items[0], 10ull, 0ull, "core", 0u);
	sk_ui_item_set(&items[1], 11ull, 0ull, "ui", 0u);
	arr.items = items;
	arr.count = 2u;
	arr.revision = 1u;
	TEST_ASSERT_EQUAL_INT(0, ui->table_bind_items(ctx, table, &arr));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind(ctx, table, &arr, SK_UI_ITEM_BIND_TABLE));
	row_a = ui->table_get_row(ctx, table, 1);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row_a));

	items[0].label = "core-renamed";
	sk_ui_item_set(&items[2], 12ull, 0ull, "jolt", 0u);
	arr.count = 3u;
	arr.revision = 2u;
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	row_a2 = ui->table_get_row(ctx, table, 1);
	TEST_ASSERT_TRUE(sk_ui_node_eq(row_a, row_a2));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->table_get_row(ctx, table, 3)));
	TEST_ASSERT_EQUAL_INT(2, ui->table_get_column_count(ctx, table));
	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
