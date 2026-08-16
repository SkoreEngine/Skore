/**
 * @file content_item.c
 * @brief Content-item thumbnail grid (APX-358; WIDGET_MANIFEST.md §16 grid half).
 *
 * ImGuiBeginContentTable / ImGuiContentItem / ImGuiEndContentTable. Binds a
 * caller-owned sk_ui_item_array_t via SK_UI_ITEM_BIND_LIST so folder + filter
 * + zoom rebuilds do not churn one retained node per asset per frame.
 * Folders stay on the Tree family; this grid is flat.
 */

#include "ui.internal.h"

#include "allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SK_TESTS
#include "test.h"
#endif

#define CG_RENAME_CAP 160
#define CG_LABEL_H 18.0f

typedef struct ui_content_grid_data_t {
	sk_ui_node_t host;
	sk_ui_node_t meta;
	f32 thumbnail_scale;
	f32 avail_width;
	u32 column_count;
	u64 rename_id;
	char rename_buf[CG_RENAME_CAP];
	char new_name[CG_RENAME_CAP];
	i32 escape_rename;
	i32 rename_armed;
	u64 last_click_id;
	u64 last_enter_id;
	u64 last_right_id;
	u64 last_press_id;
	u64 edge_id;
	i32 edge_clicked;
	i32 edge_released;
	i32 edge_enter;
	i32 edge_right;
	i32 edge_rename_finish;
} ui_content_grid_data_t;

static const sk_ui_api_t* cg_api(void) {
	return ui_get_api_table();
}

static const_chr_t cg_prop_widget(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_prop_value_t pv;
	if (ui->node_get_prop(ctx, node, "widget", &pv) != 0 || pv.type != SK_UI_PROP_STR) {
		return NULL;
	}
	return pv.data.str_value;
}

static sk_ui_node_t cg_child_widget(const sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t widget) {
	const sk_ui_api_t* ui = cg_api();
	u32 i;
	u32 n;
	if (!sk_ui_node_is_valid(parent) || widget == NULL) {
		return SK_UI_NODE_INVALID;
	}
	n = ui->node_child_count(ctx, parent);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t c = ui->node_child_at(ctx, parent, i);
		const_chr_t w = cg_prop_widget(ctx, c);
		if (w != NULL && strcmp(w, widget) == 0) {
			return c;
		}
	}
	return SK_UI_NODE_INVALID;
}

static ui_content_grid_data_t* cg_data(sk_ui_context_t* ctx, sk_ui_node_t host) {
	const sk_ui_api_t* ui;
	sk_ui_node_t meta;
	ui_node_slot_t* slot;
	if (ctx == NULL || !sk_ui_node_is_valid(host)) {
		return NULL;
	}
	ui = cg_api();
	meta = cg_child_widget(ctx, host, "content_grid_meta");
	if (!sk_ui_node_is_valid(meta)) {
		return NULL;
	}
	slot = ui_slot_mut(ctx, meta);
	(void)ui;
	if (slot == NULL || slot->user_data == NULL) {
		return NULL;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_CONTENT_GRID_DATA_TYPE_ID)) {
		return NULL;
	}
	return (ui_content_grid_data_t*)slot->user_data;
}

static const ui_content_grid_data_t* cg_data_const(const sk_ui_context_t* ctx, sk_ui_node_t host) {
	return cg_data(SK_CONST_CAST(sk_ui_context_t*, ctx), host);
}

static void cg_unreg(sk_ui_context_t* ctx, sk_ui_node_t host) {
	u32 i;
	for (i = 0u; i < ctx->content_grids.count; ++i) {
		if (sk_ui_node_eq(ctx->content_grids.items[i], host)) {
			u32 j;
			for (j = i; j + 1u < ctx->content_grids.count; ++j) {
				ctx->content_grids.items[j] = ctx->content_grids.items[j + 1u];
			}
			ctx->content_grids.count -= 1u;
			return;
		}
	}
}

void ui_content_grid_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot) {
	ui_content_grid_data_t* g;
	if (slot == NULL || slot->user_data == NULL) {
		return;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_CONTENT_GRID_DATA_TYPE_ID)) {
		return;
	}
	g = (ui_content_grid_data_t*)slot->user_data;
	cg_unreg(ctx, g->host);
	ctx->allocator->free(ctx->allocator->instance, g);
	slot->user_data = NULL;
	slot->user_data_type = SK_TYPE_ID_ZERO;
}

void ui_content_grid_release_host(sk_ui_context_t* ctx, sk_ui_node_t host) {
	if (ctx == NULL) {
		return;
	}
	cg_unreg(ctx, host);
}

void ui_content_grid_shutdown(sk_ui_context_t* ctx) {
	if (ctx == NULL) {
		return;
	}
	ctx->content_grids.count = 0u;
}

static f32 cg_avail_width(const sk_ui_context_t* ctx, const ui_content_grid_data_t* g) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_rect_t border;
	sk_ui_layout_style_t ls;
	if (g == NULL) {
		return 0.0f;
	}
	if (ui->node_get_abs_rect(ctx, g->host, &border, NULL) == 0 && border.width > 1.0f) {
		return border.width;
	}
	if (g->avail_width > 1.0f) {
		return g->avail_width;
	}
	if (ui->node_get_layout_style(ctx, g->host, &ls) == 0 && ls.width.unit == SK_UI_LENGTH_POINT && ls.width.value > 1.0f) {
		return ls.width.value;
	}
	return g->avail_width;
}

static u32 cg_compute_columns(const sk_ui_context_t* ctx, const ui_content_grid_data_t* g) {
	f32 avail;
	if (g == NULL) {
		return 1u;
	}
	avail = cg_avail_width(ctx, g);
	if (avail <= 0.0f) {
		avail = g->avail_width;
	}
	return (u32)sk_ui_content_grid_columns_for(avail, g->thumbnail_scale);
}

static const sk_ui_item_t* cg_item(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 id) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_item_array_t* arr = ui->item_bind_get_array(ctx, host);
	u32 i;
	if (arr == NULL || arr->items == NULL || id == SK_UI_ITEM_ID_NONE) {
		return NULL;
	}
	for (i = 0u; i < arr->count; ++i) {
		if (arr->items[i].id == id) {
			return &arr->items[i];
		}
	}
	return NULL;
}

static void cg_on_row_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);
static void cg_on_host_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);

static void cg_wire_row(sk_ui_context_t* ctx, sk_ui_node_t row) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_node_callbacks_t cbs;
	if (ui->node_get_callbacks(ctx, row, &cbs) != 0) {
		return;
	}
	if (cbs.on_event == cg_on_row_event) {
		return;
	}
	cbs.on_event = cg_on_row_event;
	(void)ui->node_set_callbacks(ctx, row, &cbs);
}

static void cg_set_hidden(sk_ui_context_t* ctx, sk_ui_node_t node, i32 hidden) {
	const sk_ui_api_t* ui = cg_api();
	if (sk_ui_node_is_valid(node)) {
		(void)ui->node_set_prop_i32(ctx, node, "hidden", hidden != 0 ? 1 : 0);
	}
}

static void cg_size_box(sk_ui_context_t* ctx, sk_ui_node_t node, f32 w, f32 h) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	p.layout.flex_grow = 0.0f;
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static sk_ui_node_t cg_ensure_named(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t widget, const_chr_t class_name, const_chr_t id, i32 image) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_node_t n = cg_child_widget(ctx, parent, widget);
	if (sk_ui_node_is_valid(n)) {
		return n;
	}
	n = ui->node_create(ctx, image != 0 ? SK_UI_NODE_KIND_IMAGE : SK_UI_NODE_KIND_BOX, parent);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_add_class(ctx, n, class_name);
	(void)ui->node_set_prop_str(ctx, n, "widget", widget);
	if (id != NULL && id[0] != '\0') {
		(void)ui->node_set_id(ctx, n, id);
	}
	(void)ui->node_set_pointer_events(ctx, n, SK_UI_POINTER_EVENTS_NONE);
	return n;
}

static void cg_decorate_cell(sk_ui_context_t* ctx, ui_content_grid_data_t* g, const sk_ui_item_t* it, sk_ui_node_t row) {
	const sk_ui_api_t* ui = cg_api();
	const_chr_t host_id;
	const_chr_t text;
	char idbuf[96];
	f32 thumb;
	f32 pad;
	f32 inner;
	i32 selected;
	i32 error;
	i32 renaming;
	i32 use_tex;
	sk_ui_node_t label;
	sk_ui_node_t thumb_n;
	sk_ui_node_t icon_n;
	sk_ui_node_t err_n;
	sk_ui_node_t rename;
	sk_ui_style_props_t p;

	if (it == NULL || !sk_ui_node_is_valid(row)) {
		return;
	}
	host_id = ui->node_get_id(ctx, g->host);
	text = it->label != NULL ? it->label : "";
	thumb = sk_ui_content_thumb_size(g->thumbnail_scale);
	pad = thumb * 0.08f;
	inner = thumb - pad * 4.0f;
	if (inner < 8.0f) {
		inner = 8.0f;
	}
	selected = ui->item_bind_get_selected(ctx, g->host, it->id);
	error = (it->flags & (u32)SK_UI_ITEM_FLAG_ERROR) != 0u ? 1 : 0;
	renaming = (g->rename_id == it->id) ? 1 : 0;
	use_tex = it->icon != 0u ? 1 : 0;

	(void)ui->node_add_class(ctx, row, SK_UI_CLASS_CONTENT_ITEM);
	(void)ui->node_set_prop_str(ctx, row, "widget", "content_item");
	(void)ui->node_set_prop_i32(ctx, row, "selected", selected);
	(void)ui->node_set_prop_i32(ctx, row, "show_error", error);
	(void)ui->node_set_prop_i32(ctx, row, "use_texture", use_tex);
	(void)ui->node_set_prop_i32(ctx, row, "icon", (i32)it->icon);

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT |
			 SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_GROW |
			 SK_UI_SP_FLEX_SHRINK;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.justify_content = SK_UI_JUSTIFY_FLEX_START;
	p.layout.width = sk_ui_pt(thumb);
	p.layout.height = sk_ui_pt(thumb + CG_LABEL_H);
	p.layout.min_width = sk_ui_pt(thumb);
	p.layout.min_height = sk_ui_pt(thumb + CG_LABEL_H);
	p.layout.max_width = sk_ui_pt(thumb);
	p.layout.max_height = sk_ui_pt(thumb + CG_LABEL_H);
	p.layout.flex_grow = 0.0f;
	p.layout.flex_shrink = 0.0f;
	p.layout.padding.left = pad;
	p.layout.padding.top = pad;
	p.layout.padding.right = pad;
	p.layout.padding.bottom = 2.0f;
	if (selected != 0) {
		p.layout.border.left = 2.0f;
		p.layout.border.top = 2.0f;
		p.layout.border.right = 2.0f;
		p.layout.border.bottom = 2.0f;
		p.border_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 1.0f);
	} else {
		p.layout.border.left = 0.0f;
		p.layout.border.top = 0.0f;
		p.layout.border.right = 0.0f;
		p.layout.border.bottom = 0.0f;
		p.border_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	}
	if ((ui->node_get_state(ctx, row) & (u32)SK_UI_STATE_HOVER) != 0u && selected == 0) {
		p.background_color = sk_ui_rgba(40.0f / 255.0f, 41.0f / 255.0f, 43.0f / 255.0f, 1.0f);
	} else {
		p.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	}
	(void)ui->node_merge_inline_style(ctx, row, &p);
	(void)ui->node_set_clip_children(ctx, row, 1);

	(void)snprintf(idbuf, sizeof(idbuf), "%s/t%llu", host_id != NULL ? host_id : "cg", it->id);
	thumb_n = cg_ensure_named(ctx, row, "image", SK_UI_CLASS_CONTENT_THUMB, idbuf, 1);
	(void)snprintf(idbuf, sizeof(idbuf), "%s/c%llu", host_id != NULL ? host_id : "cg", it->id);
	icon_n = cg_ensure_named(ctx, row, "content_icon", SK_UI_CLASS_CONTENT_ICON, idbuf, 0);
	(void)snprintf(idbuf, sizeof(idbuf), "%s/e%llu", host_id != NULL ? host_id : "cg", it->id);
	err_n = cg_ensure_named(ctx, row, "content_error", SK_UI_CLASS_CONTENT_ERROR, idbuf, 0);

	if (use_tex != 0) {
		sk_ui_style_props_t tp;
		(void)ui->node_set_prop_i32(ctx, thumb_n, "texture_id", (i32)it->icon);
		ui_style_props_clear(&tp);
		tp.mask = SK_UI_SP_BACKGROUND_COLOR;
		tp.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
		(void)ui->node_merge_inline_style(ctx, thumb_n, &tp);
		cg_size_box(ctx, thumb_n, inner, inner);
		cg_set_hidden(ctx, thumb_n, 0);
		cg_set_hidden(ctx, icon_n, 1);
	} else {
		char glyph[8];
		/* Avoid int→char narrowing (clang-tidy cppcoreguidelines-narrowing-conversions). */
		glyph[0] = text[0];
		if (glyph[0] == '\0') {
			glyph[0] = "#"[0];
		}
		glyph[1] = '\0';
		(void)ui->node_set_prop_str(ctx, icon_n, "text", glyph);
		{
			sk_ui_node_t glyph_lbl = cg_child_widget(ctx, icon_n, "label");
			if (!sk_ui_node_is_valid(glyph_lbl)) {
				glyph_lbl = ui->widget_label(ctx, icon_n, glyph, NULL);
				(void)ui->node_set_pointer_events(ctx, glyph_lbl, SK_UI_POINTER_EVENTS_NONE);
			} else {
				(void)ui->label_set_text(ctx, glyph_lbl, glyph);
			}
		}
		cg_size_box(ctx, icon_n, inner, inner);
		cg_set_hidden(ctx, icon_n, 0);
		cg_set_hidden(ctx, thumb_n, 1);
	}

	(void)ui->node_set_prop_str(ctx, err_n, "text", "!");
	{
		sk_ui_node_t mark = cg_child_widget(ctx, err_n, "label");
		if (!sk_ui_node_is_valid(mark)) {
			mark = ui->widget_label(ctx, err_n, "!", NULL);
			(void)ui->node_set_pointer_events(ctx, mark, SK_UI_POINTER_EVENTS_NONE);
		}
		if (error != 0) {
			sk_ui_style_props_t ep;
			ui_style_props_clear(&ep);
			ep.mask = SK_UI_SP_COLOR;
			ep.color = sk_ui_rgba(202.0f / 255.0f, 98.0f / 255.0f, 87.0f / 255.0f, 1.0f);
			(void)ui->node_merge_inline_style(ctx, mark, &ep);
		}
	}
	cg_set_hidden(ctx, err_n, error != 0 ? 0 : 1);
	{
		sk_ui_style_props_t ep;
		ui_style_props_clear(&ep);
		ep.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
		ep.layout.width = sk_ui_pt(14.0f);
		ep.layout.height = sk_ui_pt(14.0f);
		ep.layout.min_width = sk_ui_pt(14.0f);
		ep.layout.min_height = sk_ui_pt(14.0f);
		(void)ui->node_merge_inline_style(ctx, err_n, &ep);
	}

	label = cg_child_widget(ctx, row, "label");
	if (!sk_ui_node_is_valid(label)) {
		label = ui->widget_label(ctx, row, text, NULL);
		(void)ui->node_set_pointer_events(ctx, label, SK_UI_POINTER_EVENTS_NONE);
	} else {
		(void)ui->label_set_text(ctx, label, text);
	}
	(void)ui->label_set_wrap(ctx, label, 0);
	(void)ui->node_set_clip_children(ctx, label, 1);
	{
		sk_ui_style_props_t lp;
		ui_style_props_clear(&lp);
		lp.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_FONT_SIZE;
		lp.layout.width = sk_ui_pt(thumb - pad * 2.0f);
		lp.layout.max_width = sk_ui_pt(thumb - pad * 2.0f);
		lp.layout.height = sk_ui_pt(CG_LABEL_H);
		lp.layout.flex_grow = 0.0f;
		lp.font_size = g->thumbnail_scale < 0.75f ? 10.0f : 12.0f;
		(void)ui->node_merge_inline_style(ctx, label, &lp);
	}
	if (error != 0) {
		sk_ui_style_props_t lp;
		ui_style_props_clear(&lp);
		lp.mask = SK_UI_SP_COLOR;
		lp.color = sk_ui_rgba(0.92f, 0.38f, 0.34f, 1.0f);
		(void)ui->node_merge_inline_style(ctx, label, &lp);
	}

	rename = cg_child_widget(ctx, row, "text_input");
	if (renaming != 0) {
		if (!sk_ui_node_is_valid(rename)) {
			(void)snprintf(idbuf, sizeof(idbuf), "%s/r%llu", host_id != NULL ? host_id : "cg", it->id);
			rename = ui->widget_text_input(ctx, row, g->rename_buf, idbuf);
		} else {
			(void)ui->text_input_set_text(ctx, rename, g->rename_buf);
		}
		{
			sk_ui_style_props_t rp;
			ui_style_props_clear(&rp);
			rp.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH;
			rp.layout.width = sk_ui_pt(thumb - pad * 2.0f);
			rp.layout.height = sk_ui_pt(CG_LABEL_H);
			rp.background_color = sk_ui_rgba(52.0f / 255.0f, 53.0f / 255.0f, 55.0f / 255.0f, 1.0f);
			rp.border_color = sk_ui_rgba(0.70f, 0.74f, 0.80f, 1.0f);
			rp.layout.border.left = 1.0f;
			rp.layout.border.top = 1.0f;
			rp.layout.border.right = 1.0f;
			rp.layout.border.bottom = 1.0f;
			(void)ui->node_merge_inline_style(ctx, rename, &rp);
		}
		cg_set_hidden(ctx, rename, 0);
		cg_set_hidden(ctx, label, 1);
		if (g->rename_armed == 0) {
			(void)ui->focus_set(ctx, rename);
			g->rename_armed = 1;
		}
	} else {
		cg_set_hidden(ctx, rename, 1);
		cg_set_hidden(ctx, label, 0);
	}

	cg_wire_row(ctx, row);
	(void)ui->node_set_child_index(ctx, row, use_tex != 0 ? thumb_n : icon_n, 0u);
	if (error != 0) {
		(void)ui->node_set_child_index(ctx, row, err_n, 1u);
	}
}

static sk_ui_node_t cg_ensure_line(sk_ui_context_t* ctx, ui_content_grid_data_t* g, u32 row_i, f32 thumb) {
	const sk_ui_api_t* ui = cg_api();
	char idbuf[96];
	const_chr_t host_id = ui->node_get_id(ctx, g->host);
	sk_ui_node_t line;
	sk_ui_style_props_t p;
	(void)snprintf(idbuf, sizeof(idbuf), "%s/row%u", host_id != NULL ? host_id : "cg", row_i);
	line = ui->find_by_id(ctx, idbuf);
	if (!sk_ui_node_is_valid(line)) {
		line = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, g->host);
		(void)ui->node_set_id(ctx, line, idbuf);
		(void)ui->node_set_prop_str(ctx, line, "widget", "content_grid_row");
		(void)ui->node_set_prop_i32(ctx, line, "row", (i32)row_i);
	}
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	p.layout.height = sk_ui_pt(thumb + CG_LABEL_H);
	p.layout.min_height = sk_ui_pt(thumb + CG_LABEL_H);
	p.layout.width = sk_ui_percent(100.0f);
	(void)ui->node_merge_inline_style(ctx, line, &p);
	return line;
}

static void cg_style_host(sk_ui_context_t* ctx, ui_content_grid_data_t* g) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_style_props_t p;
	(void)ui->node_add_class(ctx, g->host, SK_UI_CLASS_CONTENT_GRID);
	(void)ui->node_set_prop_str(ctx, g->host, "widget", "content_grid");
	(void)ui->node_set_prop_f32(ctx, g->host, "thumbnail_scale", g->thumbnail_scale);
	(void)ui->node_set_prop_i32(ctx, g->host, "columns", (i32)g->column_count);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.align_items = SK_UI_ALIGN_STRETCH;
	(void)ui->node_merge_inline_style(ctx, g->host, &p);
}

static void cg_apply_rename(sk_ui_context_t* ctx, ui_content_grid_data_t* g, i32 commit) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_node_t input;
	const_chr_t live;
	if (g == NULL || g->rename_id == SK_UI_ITEM_ID_NONE) {
		return;
	}
	if (commit != 0) {
		input = ui_content_grid_rename_input_impl(ctx, g->host, g->rename_id);
		live = sk_ui_node_is_valid(input) ? ui->text_input_get_text(ctx, input) : g->rename_buf;
		if (live == NULL) {
			live = g->rename_buf;
		}
		(void)snprintf(g->new_name, sizeof(g->new_name), "%s", live);
		(void)snprintf(g->rename_buf, sizeof(g->rename_buf), "%s", g->new_name);
		g->edge_id = g->rename_id;
		g->edge_rename_finish = 1;
	} else {
		g->edge_rename_finish = 0;
		g->new_name[0] = '\0';
	}
	g->rename_id = SK_UI_ITEM_ID_NONE;
	g->rename_armed = 0;
	g->escape_rename = 0;
}

static void cg_poll_rename(sk_ui_context_t* ctx, ui_content_grid_data_t* g) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_node_t input;
	sk_ui_node_t focus;
	if (g->rename_id == SK_UI_ITEM_ID_NONE || g->rename_armed == 0) {
		return;
	}
	input = ui_content_grid_rename_input_impl(ctx, g->host, g->rename_id);
	if (!sk_ui_node_is_valid(input)) {
		return;
	}
	focus = ui->focus_get(ctx);
	if (!sk_ui_node_is_valid(focus)) {
		return;
	}
	if (sk_ui_node_eq(focus, input) || sk_ui_node_eq(ui->node_parent(ctx, focus), input)) {
		const_chr_t live = ui->text_input_get_text(ctx, input);
		if (live != NULL) {
			(void)snprintf(g->rename_buf, sizeof(g->rename_buf), "%s", live);
		}
		return;
	}
	cg_apply_rename(ctx, g, g->escape_rename != 0 ? 0 : 1);
}

static void cg_sync_one(sk_ui_context_t* ctx, ui_content_grid_data_t* g) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_item_array_t* arr;
	u32 i;
	sk_ui_node_t meta;
	if (g == NULL) {
		return;
	}
	g->column_count = cg_compute_columns(ctx, g);
	cg_style_host(ctx, g);
	cg_poll_rename(ctx, g);
	arr = ui->item_bind_get_array(ctx, g->host);
	if (arr != NULL && arr->items != NULL) {
		f32 thumb = sk_ui_content_thumb_size(g->thumbnail_scale);
		u32 cols = g->column_count > 0u ? g->column_count : 1u;
		u32 used_rows = arr->count == 0u ? 0u : (arr->count + cols - 1u) / cols;
		u32 ci;
		u32 cn;
		for (i = 0u; i < arr->count; ++i) {
			sk_ui_node_t row = ui->item_bind_find(ctx, g->host, arr->items[i].id);
			sk_ui_node_t line;
			if (!sk_ui_node_is_valid(row)) {
				continue;
			}
			cg_decorate_cell(ctx, g, &arr->items[i], row);
			line = cg_ensure_line(ctx, g, i / cols, thumb);
			if (sk_ui_node_is_valid(line) && !sk_ui_node_eq(ui->node_parent(ctx, row), line)) {
				(void)ui->node_reparent(ctx, row, line, i % cols);
			} else if (sk_ui_node_is_valid(line)) {
				(void)ui->node_set_child_index(ctx, line, row, i % cols);
			}
		}
		cn = ui->node_child_count(ctx, g->host);
		for (ci = 0u; ci < cn; ++ci) {
			sk_ui_node_t ch = ui->node_child_at(ctx, g->host, ci);
			sk_ui_prop_value_t pv;
			const_chr_t w = cg_prop_widget(ctx, ch);
			if (w == NULL || strcmp(w, "content_grid_row") != 0) {
				continue;
			}
			if (ui->node_get_prop(ctx, ch, "row", &pv) == 0 && pv.type == SK_UI_PROP_I32 && pv.data.i32_value >= (i32)used_rows) {
				cg_set_hidden(ctx, ch, 1);
				cg_size_box(ctx, ch, 0.0f, 0.0f);
			} else {
				cg_set_hidden(ctx, ch, 0);
			}
		}
	}
	meta = g->meta;
	if (sk_ui_node_is_valid(meta) && ui->node_alive(ctx, meta)) {
		u32 n = ui->node_child_count(ctx, g->host);
		if (n > 0u) {
			(void)ui->node_set_child_index(ctx, g->host, meta, n - 1u);
		}
	}
}

void ui_content_grid_sync_all(sk_ui_context_t* ctx) {
	u32 i = 0u;
	if (ctx == NULL) {
		return;
	}
	while (i < ctx->content_grids.count) {
		sk_ui_node_t host = ctx->content_grids.items[i];
		ui_content_grid_data_t* g = cg_data(ctx, host);
		if (g == NULL) {
			cg_unreg(ctx, host);
			continue;
		}
		cg_sync_one(ctx, g);
		++i;
	}
}

static void cg_mark_edge(ui_content_grid_data_t* g, u64 id, i32 clicked, i32 released, i32 enter, i32 right) {
	g->edge_id = id;
	if (clicked != 0) {
		g->edge_clicked = 1;
	}
	if (released != 0) {
		g->edge_released = 1;
	}
	if (enter != 0) {
		g->edge_enter = 1;
		g->last_enter_id = id;
	}
	if (right != 0) {
		g->edge_right = 1;
		g->last_right_id = id;
	}
}

static u64 cg_parse_item_id(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_prop_value_t pv;
	if (ui->node_get_prop(ctx, node, "item_id", &pv) != 0 || pv.type != SK_UI_PROP_STR || pv.data.str_value == NULL) {
		return SK_UI_ITEM_ID_NONE;
	}
	return strtoull(pv.data.str_value, NULL, 10);
}

static void cg_on_row_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = cg_api();
	ui_content_grid_data_t* g;
	u64 id;
	(void)user;
	if (event == NULL || (event->phase != SK_UI_EVENT_PHASE_TARGET && event->phase != SK_UI_EVENT_PHASE_BUBBLE)) {
		return;
	}
	{
		sk_ui_node_t walk = ui->node_parent(ctx, node);
		g = NULL;
		while (sk_ui_node_is_valid(walk)) {
			g = cg_data(ctx, walk);
			if (g != NULL) {
				break;
			}
			walk = ui->node_parent(ctx, walk);
		}
	}
	if (g == NULL) {
		return;
	}
	id = cg_parse_item_id(ctx, node);
	if (id == SK_UI_ITEM_ID_NONE) {
		return;
	}
	if (g->rename_id == id && event->type != SK_UI_EVENT_KEY_DOWN) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN) {
		if (event->button == SK_UI_POINTER_BUTTON_RIGHT || event->button == SK_UI_POINTER_BUTTON_LEFT) {
			g->last_press_id = id;
			cg_mark_edge(g, id, 1, 0, 0, event->button == SK_UI_POINTER_BUTTON_RIGHT ? 1 : 0);
		}
		/* item_bind on_click consumes CLICK; detect double-click on down. */
		if (event->button == SK_UI_POINTER_BUTTON_LEFT && g->rename_id != id) {
			i32 is_double = (g->last_click_id == id) ? 1 : 0;
			g->last_click_id = is_double != 0 ? SK_UI_ITEM_ID_NONE : id;
			if (is_double != 0) {
				cg_mark_edge(g, id, 0, 0, 1, 0);
			}
		}
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		if ((event->button == SK_UI_POINTER_BUTTON_RIGHT || event->button == SK_UI_POINTER_BUTTON_LEFT) && g->last_press_id == id) {
			cg_mark_edge(g, id, 0, 1, 0, 0);
			g->last_press_id = SK_UI_ITEM_ID_NONE;
		}
		return;
	}
	if (event->type == SK_UI_EVENT_KEY_DOWN && event->down != 0) {
		if (event->key == SK_UI_KEY_ESCAPE && g->rename_id == id) {
			g->escape_rename = 1;
			cg_apply_rename(ctx, g, 0);
			cg_sync_one(ctx, g);
			event->consumed = 1;
			return;
		}
		if (event->key == SK_UI_KEY_ENTER && g->rename_id == id) {
			cg_apply_rename(ctx, g, 1);
			cg_sync_one(ctx, g);
			event->consumed = 1;
			return;
		}
		if (event->key == SK_UI_KEY_ENTER && g->rename_id == SK_UI_ITEM_ID_NONE && ui->item_bind_get_selected(ctx, g->host, id) != 0) {
			cg_mark_edge(g, id, 0, 0, 1, 0);
			event->consumed = 1;
		}
	}
}

static void cg_on_host_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = cg_api();
	ui_content_grid_data_t* g = (ui_content_grid_data_t*)user;
	sk_ui_item_array_t* arr;
	u32 i;
	(void)node;
	if (g == NULL || event == NULL || event->type != SK_UI_EVENT_KEY_DOWN || event->down == 0) {
		return;
	}
	if (g->rename_id != SK_UI_ITEM_ID_NONE) {
		if (event->key == SK_UI_KEY_ESCAPE) {
			cg_apply_rename(ctx, g, 0);
			cg_sync_one(ctx, g);
			event->consumed = 1;
		} else if (event->key == SK_UI_KEY_ENTER) {
			cg_apply_rename(ctx, g, 1);
			cg_sync_one(ctx, g);
			event->consumed = 1;
		}
		return;
	}
	if (event->key != SK_UI_KEY_ENTER) {
		return;
	}
	arr = ui->item_bind_get_array(ctx, g->host);
	if (arr == NULL || arr->items == NULL) {
		return;
	}
	for (i = 0u; i < arr->count; ++i) {
		if (ui->item_bind_get_selected(ctx, g->host, arr->items[i].id) != 0) {
			cg_mark_edge(g, arr->items[i].id, 0, 0, 1, 0);
			event->consumed = 1;
			return;
		}
	}
}

static ui_content_grid_data_t* cg_attach(sk_ui_context_t* ctx, sk_ui_node_t host, f32 scale) {
	const sk_ui_api_t* ui = cg_api();
	ui_content_grid_data_t* g = cg_data(ctx, host);
	ui_node_slot_t* slot;
	sk_ui_node_t meta;
	if (g != NULL) {
		g->thumbnail_scale = scale > 0.0f ? scale : 1.0f;
		return g;
	}
	meta = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, host);
	if (!sk_ui_node_is_valid(meta)) {
		return NULL;
	}
	(void)ui->node_set_prop_str(ctx, meta, "widget", "content_grid_meta");
	(void)ui->node_set_prop_i32(ctx, meta, "hidden", 1);
	(void)ui->node_set_pointer_events(ctx, meta, SK_UI_POINTER_EVENTS_NONE);
	cg_size_box(ctx, meta, 0.0f, 0.0f);
	g = (ui_content_grid_data_t*)ctx->allocator->alloc(ctx->allocator->instance, sizeof(ui_content_grid_data_t));
	if (g == NULL) {
		(void)ui->node_destroy(ctx, meta);
		return NULL;
	}
	memset(g, 0, sizeof(*g));
	g->host = host;
	g->meta = meta;
	g->thumbnail_scale = scale > 0.0f ? scale : 1.0f;
	g->rename_id = SK_UI_ITEM_ID_NONE;
	g->last_click_id = SK_UI_ITEM_ID_NONE;
	g->last_enter_id = SK_UI_ITEM_ID_NONE;
	g->last_right_id = SK_UI_ITEM_ID_NONE;
	g->last_press_id = SK_UI_ITEM_ID_NONE;
	g->edge_id = SK_UI_ITEM_ID_NONE;
	slot = ui_slot_mut(ctx, meta);
	if (slot == NULL) {
		ctx->allocator->free(ctx->allocator->instance, g);
		(void)ui->node_destroy(ctx, meta);
		return NULL;
	}
	slot->user_data = g;
	slot->user_data_type = SK_UI_CONTENT_GRID_DATA_TYPE_ID;
	{
		sk_ui_node_callbacks_t hcbs;
		memset(&hcbs, 0, sizeof(hcbs));
		hcbs.on_event = cg_on_host_event;
		hcbs.user = g;
		(void)ui->node_set_callbacks(ctx, host, &hcbs);
		(void)ui->node_set_focusable(ctx, host, 1);
	}
	(void)sk_array_push(&ctx->content_grids, host);
	return g;
}

sk_ui_node_t ui_widget_content_grid_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, f32 thumbnail_scale, const_chr_t id) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_node_t host;
	ui_content_grid_data_t* g;
	host = ui->widget_list(ctx, parent, items, id);
	if (!sk_ui_node_is_valid(host)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->item_bind_set_flags(ctx, host, 0u);
	g = cg_attach(ctx, host, thumbnail_scale);
	if (g == NULL) {
		(void)ui->node_destroy(ctx, host);
		return SK_UI_NODE_INVALID;
	}
	cg_sync_one(ctx, g);
	return host;
}

i32 ui_content_grid_set_scale_impl(sk_ui_context_t* ctx, sk_ui_node_t grid, f32 thumbnail_scale) {
	ui_content_grid_data_t* g = cg_data(ctx, grid);
	if (g == NULL) {
		return -1;
	}
	g->thumbnail_scale = thumbnail_scale > 0.0f ? thumbnail_scale : 1.0f;
	cg_sync_one(ctx, g);
	return 0;
}

f32 ui_content_grid_get_scale_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid) {
	const ui_content_grid_data_t* g = cg_data_const(ctx, grid);
	return g != NULL ? g->thumbnail_scale : 0.0f;
}

i32 ui_content_grid_set_available_width_impl(sk_ui_context_t* ctx, sk_ui_node_t grid, f32 width) {
	const sk_ui_api_t* ui = cg_api();
	ui_content_grid_data_t* g = cg_data(ctx, grid);
	sk_ui_style_props_t p;
	if (g == NULL) {
		return -1;
	}
	g->avail_width = width;
	if (width > 0.0f) {
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_WIDTH;
		p.layout.width = sk_ui_pt(width);
		(void)ui->node_merge_inline_style(ctx, grid, &p);
	}
	g->column_count = cg_compute_columns(ctx, g);
	(void)ui->node_set_prop_i32(ctx, grid, "columns", (i32)g->column_count);
	return 0;
}

i32 ui_content_grid_get_column_count_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid) {
	const ui_content_grid_data_t* g = cg_data_const(ctx, grid);
	if (g == NULL) {
		return 0;
	}
	return (i32)cg_compute_columns(ctx, g);
}

f32 ui_content_grid_get_thumb_size_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid) {
	const ui_content_grid_data_t* g = cg_data_const(ctx, grid);
	if (g == NULL) {
		return 0.0f;
	}
	return sk_ui_content_thumb_size(g->thumbnail_scale);
}

i32 ui_content_grid_begin_rename_impl(sk_ui_context_t* ctx, sk_ui_node_t grid, u64 item_id) {
	ui_content_grid_data_t* g = cg_data(ctx, grid);
	const sk_ui_item_t* it;
	if (g == NULL || item_id == SK_UI_ITEM_ID_NONE) {
		return -1;
	}
	it = cg_item(ctx, grid, item_id);
	if (it == NULL) {
		return -1;
	}
	g->rename_id = item_id;
	g->escape_rename = 0;
	g->rename_armed = 0;
	g->edge_rename_finish = 0;
	(void)snprintf(g->rename_buf, sizeof(g->rename_buf), "%s", it->label != NULL ? it->label : "");
	g->new_name[0] = '\0';
	cg_sync_one(ctx, g);
	return 0;
}

i32 ui_content_grid_commit_rename_impl(sk_ui_context_t* ctx, sk_ui_node_t grid) {
	ui_content_grid_data_t* g = cg_data(ctx, grid);
	if (g == NULL || g->rename_id == SK_UI_ITEM_ID_NONE) {
		return -1;
	}
	cg_apply_rename(ctx, g, 1);
	cg_sync_one(ctx, g);
	return 0;
}

i32 ui_content_grid_cancel_rename_impl(sk_ui_context_t* ctx, sk_ui_node_t grid) {
	ui_content_grid_data_t* g = cg_data(ctx, grid);
	if (g == NULL || g->rename_id == SK_UI_ITEM_ID_NONE) {
		return -1;
	}
	cg_apply_rename(ctx, g, 0);
	cg_sync_one(ctx, g);
	return 0;
}

u64 ui_content_grid_rename_id_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid) {
	const ui_content_grid_data_t* g = cg_data_const(ctx, grid);
	return g != NULL ? g->rename_id : SK_UI_ITEM_ID_NONE;
}

i32 ui_content_grid_item_state_impl(sk_ui_context_t* ctx, sk_ui_node_t grid, u64 item_id, sk_ui_content_item_state_t* out) {
	const sk_ui_api_t* ui = cg_api();
	ui_content_grid_data_t* g = cg_data(ctx, grid);
	sk_ui_node_t row;
	if (g == NULL || out == NULL || item_id == SK_UI_ITEM_ID_NONE) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	row = ui->item_bind_find(ctx, grid, item_id);
	if (sk_ui_node_is_valid(row)) {
		u32 st = ui->node_get_state(ctx, row);
		out->hovered = (st & (u32)SK_UI_STATE_HOVER) != 0u ? 1 : 0;
		(void)ui->node_get_abs_rect(ctx, row, &out->rect, NULL);
	}
	if (g->edge_id == item_id) {
		out->clicked = g->edge_clicked;
		out->released = g->edge_released;
		out->enter = g->edge_enter;
		out->right_clicked = g->edge_right;
		out->rename_finish = g->edge_rename_finish;
		out->new_name = g->edge_rename_finish != 0 ? g->new_name : "";
		g->edge_clicked = 0;
		g->edge_released = 0;
		g->edge_enter = 0;
		g->edge_right = 0;
		g->edge_rename_finish = 0;
		g->edge_id = SK_UI_ITEM_ID_NONE;
	} else {
		out->new_name = "";
	}
	return 0;
}

u64 ui_content_grid_last_enter_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid) {
	const ui_content_grid_data_t* g = cg_data_const(ctx, grid);
	return g != NULL ? g->last_enter_id : SK_UI_ITEM_ID_NONE;
}

u64 ui_content_grid_last_right_click_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid) {
	const ui_content_grid_data_t* g = cg_data_const(ctx, grid);
	return g != NULL ? g->last_right_id : SK_UI_ITEM_ID_NONE;
}

sk_ui_node_t ui_content_grid_thumb_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid, u64 item_id) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_node_t row = ui->item_bind_find(ctx, grid, item_id);
	sk_ui_node_t n = cg_child_widget(ctx, row, "image");
	sk_ui_prop_value_t pv;
	if (!sk_ui_node_is_valid(n)) {
		return SK_UI_NODE_INVALID;
	}
	if (ui->node_get_prop(ctx, n, "hidden", &pv) == 0 && pv.type == SK_UI_PROP_I32 && pv.data.i32_value != 0) {
		return SK_UI_NODE_INVALID;
	}
	return n;
}

sk_ui_node_t ui_content_grid_icon_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid, u64 item_id) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_node_t row = ui->item_bind_find(ctx, grid, item_id);
	sk_ui_node_t n = cg_child_widget(ctx, row, "content_icon");
	sk_ui_prop_value_t pv;
	if (!sk_ui_node_is_valid(n)) {
		return SK_UI_NODE_INVALID;
	}
	if (ui->node_get_prop(ctx, n, "hidden", &pv) == 0 && pv.type == SK_UI_PROP_I32 && pv.data.i32_value != 0) {
		return SK_UI_NODE_INVALID;
	}
	return n;
}

sk_ui_node_t ui_content_grid_error_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid, u64 item_id) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_node_t row = ui->item_bind_find(ctx, grid, item_id);
	sk_ui_node_t n = cg_child_widget(ctx, row, "content_error");
	sk_ui_prop_value_t pv;
	if (!sk_ui_node_is_valid(n)) {
		return SK_UI_NODE_INVALID;
	}
	if (ui->node_get_prop(ctx, n, "hidden", &pv) == 0 && pv.type == SK_UI_PROP_I32 && pv.data.i32_value != 0) {
		return SK_UI_NODE_INVALID;
	}
	return n;
}

sk_ui_node_t ui_content_grid_rename_input_impl(const sk_ui_context_t* ctx, sk_ui_node_t grid, u64 item_id) {
	const sk_ui_api_t* ui = cg_api();
	sk_ui_node_t row = ui->item_bind_find(ctx, grid, item_id);
	sk_ui_node_t n = cg_child_widget(ctx, row, "text_input");
	sk_ui_prop_value_t pv;
	if (!sk_ui_node_is_valid(n)) {
		return SK_UI_NODE_INVALID;
	}
	if (ui->node_get_prop(ctx, n, "hidden", &pv) == 0 && pv.type == SK_UI_PROP_I32 && pv.data.i32_value != 0) {
		return SK_UI_NODE_INVALID;
	}
	return n;
}

#ifdef SK_TESTS

static const sk_ui_api_t* wcg_api(void) {
	return ui_get_api_table();
}

static void wcg_fill(sk_ui_item_t* items, sk_ui_item_array_t* arr, u32 count) {
	static const char* names[] = {"Mesh.skmesh", "Hero.skent", "Broken.skmat", "Folder", "Logo.sktex", "Light.skent"};
	u32 i;
	for (i = 0u; i < count && i < 6u; ++i) {
		u32 flags = (u32)SK_UI_ITEM_FLAG_LEAF;
		if (i == 2u) {
			flags |= (u32)SK_UI_ITEM_FLAG_ERROR;
		}
		if (i == 1u) {
			flags |= (u32)SK_UI_ITEM_FLAG_SELECTED;
		}
		sk_ui_item_set(&items[i], (u64)i + 1ull, 0ull, names[i], flags);
		items[i].icon = (i == 0u || i == 4u) ? 10u + i : 0u;
	}
	arr->items = items;
	arr->count = count;
	arr->revision = 1u;
}

static void wcg_layout(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 w, f32 h) {
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, w, h));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
}

SK_TEST(ui_content_grid_columns_vs_width_zoom) {
	const sk_ui_api_t* ui = wcg_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[6];
	sk_ui_item_array_t arr;
	sk_ui_node_t grid;

	TEST_ASSERT_EQUAL_INT(3, sk_ui_content_grid_columns_for(336.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(1, sk_ui_content_grid_columns_for(100.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(6, sk_ui_content_grid_columns_for(336.0f, 0.5f));
	TEST_ASSERT_EQUAL_INT(1, sk_ui_content_grid_columns_for(0.0f, 1.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 112.0f, sk_ui_content_thumb_size(1.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 56.0f, sk_ui_content_thumb_size(0.5f));

	TEST_ASSERT_NOT_NULL(ctx);
	wcg_fill(items, &arr, 6u);
	grid = ui->widget_content_grid(ctx, ui->context_root(ctx), &arr, 1.0f, "cg-cols");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(grid));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_ITEM_BIND_LIST, (int)ui->item_bind_get_kind(ctx, grid));
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_set_available_width(ctx, grid, 336.0f));
	TEST_ASSERT_EQUAL_INT(3, ui->content_grid_get_column_count(ctx, grid));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 112.0f, ui->content_grid_get_thumb_size(ctx, grid));
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_set_scale(ctx, grid, 0.5f));
	TEST_ASSERT_EQUAL_INT(6, ui->content_grid_get_column_count(ctx, grid));
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_set_available_width(ctx, grid, 200.0f));
	TEST_ASSERT_EQUAL_INT(3, ui->content_grid_get_column_count(ctx, grid));

	ui->context_destroy(ctx);
}

SK_TEST(ui_content_grid_selection_rename_icon_error_identity) {
	const sk_ui_api_t* ui = wcg_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[6];
	sk_ui_item_array_t arr;
	sk_ui_node_t grid;
	sk_ui_node_t row;
	sk_ui_node_t row_after;
	sk_ui_content_item_state_t st;

	TEST_ASSERT_NOT_NULL(ctx);
	wcg_fill(items, &arr, 5u);
	grid = ui->widget_content_grid(ctx, ui->context_root(ctx), &arr, 1.0f, "cg-id");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(grid));
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_set_available_width(ctx, grid, 448.0f));
	wcg_layout(ui, ctx, 480.0f, 360.0f);

	TEST_ASSERT_EQUAL_UINT(5u, ui->item_bind_row_count(ctx, grid));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, grid, 2ull));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->content_grid_thumb(ctx, grid, 1ull)));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->content_grid_icon(ctx, grid, 1ull)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->content_grid_icon(ctx, grid, 2ull)));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->content_grid_thumb(ctx, grid, 2ull)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->content_grid_error(ctx, grid, 3ull)));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->content_grid_error(ctx, grid, 1ull)));

	{
		sk_ui_input_event_t keyev;
		memset(&keyev, 0, sizeof(keyev));
		TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_selected(ctx, grid, 2ull, 1));
		TEST_ASSERT_EQUAL_INT(0, ui->focus_set(ctx, grid));
		keyev.kind = SK_UI_INPUT_KEY;
		keyev.key = SK_UI_KEY_ENTER;
		keyev.down = 1;
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &keyev));
		TEST_ASSERT_TRUE(ui->content_grid_last_enter(ctx, grid) == 2ull);
	}

	row = ui->item_bind_find(ctx, grid, 2ull);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row));

	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_begin_rename(ctx, grid, 2ull));
	TEST_ASSERT_TRUE(ui->content_grid_rename_id(ctx, grid) == 2ull);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->content_grid_rename_input(ctx, grid, 2ull)));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ui->content_grid_rename_input(ctx, grid, 2ull), "Hero"));
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_commit_rename(ctx, grid));
	TEST_ASSERT_TRUE(ui->content_grid_rename_id(ctx, grid) == SK_UI_ITEM_ID_NONE);
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_item_state(ctx, grid, 2ull, &st));
	TEST_ASSERT_EQUAL_INT(1, st.rename_finish);
	TEST_ASSERT_EQUAL_STRING("Hero", st.new_name);
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_item_state(ctx, grid, 2ull, &st));
	TEST_ASSERT_EQUAL_INT(0, st.rename_finish);

	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_begin_rename(ctx, grid, 4ull));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ui->content_grid_rename_input(ctx, grid, 4ull), "Nope"));
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_cancel_rename(ctx, grid));
	TEST_ASSERT_TRUE(ui->content_grid_rename_id(ctx, grid) == SK_UI_ITEM_ID_NONE);
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_item_state(ctx, grid, 4ull, &st));
	TEST_ASSERT_EQUAL_INT(0, st.rename_finish);

	sk_ui_item_set(&items[0], 1ull, 0ull, "MeshB.skmesh", (u32)SK_UI_ITEM_FLAG_LEAF);
	items[0].icon = 10u;
	sk_ui_item_set(&items[1], 2ull, 0ull, "Hero.skent", (u32)SK_UI_ITEM_FLAG_LEAF | (u32)SK_UI_ITEM_FLAG_SELECTED);
	sk_ui_item_set(&items[2], 5ull, 0ull, "Logo.sktex", (u32)SK_UI_ITEM_FLAG_LEAF);
	items[2].icon = 14u;
	arr.count = 3u;
	arr.revision = 2u;
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_sync(ctx, grid));
	wcg_layout(ui, ctx, 480.0f, 360.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, grid, 2ull));
	row_after = ui->item_bind_find(ctx, grid, 2ull);
	TEST_ASSERT_TRUE(sk_ui_node_eq(row, row_after));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->item_bind_find(ctx, grid, 3ull)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->item_bind_find(ctx, grid, 5ull)));

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
