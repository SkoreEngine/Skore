/**
 * @file drag_drop.c
 * @brief Drag-drop payload API (APX-356; WIDGET_MANIFEST.md §17).
 *
 * BeginDragDropSource / SetDragDropPayload / BeginDragDropTarget /
 * BeginDragDropTargetCustom / AcceptDragDropPayload / GetDragDropPayload.
 * Sources and targets hang off tree rows, property fields, and caller
 * custom rects. The payload is a transient copy that lives until mouse
 * release (empty blob is the SK_ENTITY_PAYLOAD form).
 */

#include "ui.internal.h"

#include "allocator.h"

#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS
#include "test.h"
#endif

enum { UI_DD_SOURCE_MAX = 64, UI_DD_TARGET_MAX = 64, UI_DD_CUSTOM_MAX = 16 };

typedef struct ui_dd_source_t {
	sk_ui_node_t node;
	char type[SK_UI_PAYLOAD_TYPE_MAX + 1];
	u8* data;
	u32 size;
	u32 flags;
	char preview[SK_UI_DRAG_DROP_PREVIEW_MAX];
} ui_dd_source_t;

typedef struct ui_dd_target_t {
	sk_ui_node_t node;
	char type[SK_UI_PAYLOAD_TYPE_MAX + 1];
	u32 flags;
} ui_dd_target_t;

typedef struct ui_dd_custom_t {
	sk_ui_rect_t rect;
	char id[SK_UI_DRAG_DROP_ID_MAX];
	char type[SK_UI_PAYLOAD_TYPE_MAX + 1];
	u32 flags;
	u8 used;
} ui_dd_custom_t;

typedef struct ui_drag_drop_t {
	ui_dd_source_t sources[UI_DD_SOURCE_MAX];
	u32 source_count;
	ui_dd_target_t targets[UI_DD_TARGET_MAX];
	u32 target_count;
	ui_dd_custom_t customs[UI_DD_CUSTOM_MAX];

	i32 pending;
	i32 active;
	sk_ui_node_t pending_source;
	f32 start_x;
	f32 start_y;

	char type[SK_UI_PAYLOAD_TYPE_MAX + 1];
	u8* data;
	u32 size;
	u32 flags;
	char preview[SK_UI_DRAG_DROP_PREVIEW_MAX];
	sk_ui_node_t source;

	sk_ui_node_t hover_target;
	char hover_custom_id[SK_UI_DRAG_DROP_ID_MAX];
	u32 hover_flags;

	i32 delivery;
	i32 accepted;
	i32 expire_armed; /**< Mouse already up; next tick expires after accept. */

	sk_ui_node_t preview_node;
	sk_ui_node_t preview_label;
	sk_ui_node_t highlight_node;
	sk_ui_node_t painted_target;

	sk_ui_payload_t view;
} ui_drag_drop_t;

static const sk_ui_api_t* dd_api(void) {
	return ui_get_api_table();
}

static void dd_copy_type(char* dst, u32 cap, const_chr_t src) {
	u32 i = 0u;
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

static i32 dd_type_eq(const_chr_t a, const_chr_t b) {
	if (a == NULL || b == NULL) {
		return 0;
	}
	return strcmp(a, b) == 0 ? 1 : 0;
}

static i32 dd_rect_contains(const sk_ui_rect_t* r, f32 x, f32 y) {
	if (r == NULL || r->width <= 0.0f || r->height <= 0.0f) {
		return 0;
	}
	return (x >= r->x && y >= r->y && x < (r->x + r->width) && y < (r->y + r->height)) ? 1 : 0;
}

static void dd_free_blob(sk_ui_context_t* ctx, u8** data, u32* size) {
	if (ctx == NULL || data == NULL) {
		return;
	}
	if (*data != NULL) {
		ctx->allocator->free(ctx->allocator->instance, *data);
		*data = NULL;
	}
	if (size != NULL) {
		*size = 0u;
	}
}

static i32 dd_copy_blob(sk_ui_context_t* ctx, u8** dst, u32* dst_size, const void* src, u32 size) {
	dd_free_blob(ctx, dst, dst_size);
	if (size == 0u || src == NULL) {
		return 0;
	}
	*dst = (u8*)ctx->allocator->alloc(ctx->allocator->instance, size);
	if (*dst == NULL) {
		return -1;
	}
	memcpy(*dst, src, (size_t)size);
	*dst_size = size;
	return 0;
}

static ui_dd_source_t* dd_find_source(ui_drag_drop_t* dd, sk_ui_node_t node) {
	u32 i;
	if (dd == NULL || !sk_ui_node_is_valid(node)) {
		return NULL;
	}
	for (i = 0u; i < dd->source_count; ++i) {
		if (sk_ui_node_eq(dd->sources[i].node, node)) {
			return &dd->sources[i];
		}
	}
	return NULL;
}

static ui_dd_target_t* dd_find_target(ui_drag_drop_t* dd, sk_ui_node_t node) {
	u32 i;
	if (dd == NULL || !sk_ui_node_is_valid(node)) {
		return NULL;
	}
	for (i = 0u; i < dd->target_count; ++i) {
		if (sk_ui_node_eq(dd->targets[i].node, node)) {
			return &dd->targets[i];
		}
	}
	return NULL;
}

static ui_dd_custom_t* dd_find_custom(ui_drag_drop_t* dd, const_chr_t id) {
	u32 i;
	if (dd == NULL || id == NULL || id[0] == '\0') {
		return NULL;
	}
	for (i = 0u; i < (u32)UI_DD_CUSTOM_MAX; ++i) {
		if (dd->customs[i].used != 0u && strcmp(dd->customs[i].id, id) == 0) {
			return &dd->customs[i];
		}
	}
	return NULL;
}

static ui_dd_source_t* dd_source_from_hit(sk_ui_context_t* ctx, ui_drag_drop_t* dd, sk_ui_node_t hit) {
	const sk_ui_api_t* ui = dd_api();
	sk_ui_node_t cur = hit;
	while (sk_ui_node_is_valid(cur)) {
		ui_dd_source_t* s = dd_find_source(dd, cur);
		if (s != NULL) {
			return s;
		}
		cur = ui->node_parent(ctx, cur);
	}
	return NULL;
}

static ui_dd_target_t* dd_target_from_hit(sk_ui_context_t* ctx, ui_drag_drop_t* dd, sk_ui_node_t hit) {
	const sk_ui_api_t* ui = dd_api();
	sk_ui_node_t cur = hit;
	while (sk_ui_node_is_valid(cur)) {
		ui_dd_target_t* t = dd_find_target(dd, cur);
		if (t != NULL) {
			return t;
		}
		cur = ui->node_parent(ctx, cur);
	}
	return NULL;
}

static ui_dd_custom_t* dd_custom_at(ui_drag_drop_t* dd, f32 x, f32 y) {
	u32 i;
	ui_dd_custom_t* best = NULL;
	f32 best_area = 0.0f;
	if (dd == NULL) {
		return NULL;
	}
	for (i = 0u; i < (u32)UI_DD_CUSTOM_MAX; ++i) {
		ui_dd_custom_t* c = &dd->customs[i];
		f32 area;
		if (c->used == 0u || dd_rect_contains(&c->rect, x, y) == 0) {
			continue;
		}
		area = c->rect.width * c->rect.height;
		/* Prefer the smallest containing rect (between-row strip over viewport). */
		if (best == NULL || area < best_area) {
			best = c;
			best_area = area;
		}
	}
	return best;
}

static void dd_sync_view(ui_drag_drop_t* dd) {
	if (dd == NULL) {
		return;
	}
	memset(&dd->view, 0, sizeof(dd->view));
	if (dd->active == 0 && dd->delivery == 0) {
		return;
	}
	dd_copy_type(dd->view.type, (u32)sizeof(dd->view.type), dd->type);
	dd->view.data = dd->data;
	dd->view.size = dd->size;
	dd->view.source = dd->source;
	dd->view.preview = (sk_ui_node_is_valid(dd->hover_target) || dd->hover_custom_id[0] != '\0') ? 1 : 0;
	dd->view.delivery = dd->delivery != 0 ? 1 : 0;
}

static void dd_set_hidden(sk_ui_context_t* ctx, sk_ui_node_t node, i32 hidden) {
	const sk_ui_api_t* ui = dd_api();
	if (!sk_ui_node_is_valid(node)) {
		return;
	}
	(void)ui->node_set_prop_i32(ctx, node, "hidden", hidden != 0 ? 1 : 0);
	(void)ui->node_set_pointer_events(ctx, node, SK_UI_POINTER_EVENTS_NONE);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_LAYOUT | (u32)SK_UI_DIRTY_PAINT);
}

static void dd_paint_target_fill(sk_ui_context_t* ctx, sk_ui_node_t node, i32 on) {
	const sk_ui_api_t* ui = dd_api();
	sk_ui_style_props_t p;
	ui_node_slot_t* slot;
	if (!sk_ui_node_is_valid(node) || ui->node_alive(ctx, node) == 0) {
		return;
	}
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH;
	if (on != 0) {
		p.background_color = sk_ui_rgba(0.20f, 0.42f, 0.72f, 1.0f);
		p.border_color = sk_ui_rgba(0.55f, 0.78f, 1.0f, 1.0f);
		p.layout.border.left = 2.0f;
		p.layout.border.top = 2.0f;
		p.layout.border.right = 2.0f;
		p.layout.border.bottom = 2.0f;
		(void)ui->node_add_class(ctx, node, SK_UI_CLASS_DRAG_DROP_TARGET);
	} else {
		p.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
		p.border_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
		p.layout.border.left = 0.0f;
		p.layout.border.top = 0.0f;
		p.layout.border.right = 0.0f;
		p.layout.border.bottom = 0.0f;
		(void)ui->node_remove_class(ctx, node, SK_UI_CLASS_DRAG_DROP_TARGET);
	}
	(void)ui->node_merge_inline_style(ctx, node, &p);
	/* place() runs after style_resolve; write computed so this frame paints. */
	slot = ui_slot_mut(ctx, node);
	if (slot != NULL) {
		slot->computed.background_color = p.background_color;
		slot->computed.border_color = p.border_color;
		slot->layout_style.border = p.layout.border;
	}
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_STYLE | (u32)SK_UI_DIRTY_PAINT);
}

static void dd_clear_highlight(sk_ui_context_t* ctx, ui_drag_drop_t* dd) {
	if (dd == NULL) {
		return;
	}
	if (sk_ui_node_is_valid(dd->painted_target)) {
		dd_paint_target_fill(ctx, dd->painted_target, 0);
	}
	dd->painted_target = SK_UI_NODE_INVALID;
	if (sk_ui_node_is_valid(dd->highlight_node)) {
		dd_set_hidden(ctx, dd->highlight_node, 1);
	}
}

static void dd_apply_highlight(sk_ui_context_t* ctx, ui_drag_drop_t* dd) {
	const sk_ui_api_t* ui = dd_api();
	i32 draw = 1;
	if (dd == NULL || dd->active == 0) {
		dd_clear_highlight(ctx, dd);
		return;
	}
	if ((dd->hover_flags & SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_DRAW_DEFAULT_RECT) != 0u) {
		draw = 0;
	}
	if (draw == 0) {
		dd_clear_highlight(ctx, dd);
		return;
	}
	if (sk_ui_node_is_valid(dd->hover_target) && ui->node_alive(ctx, dd->hover_target) != 0) {
		if (!sk_ui_node_eq(dd->painted_target, dd->hover_target)) {
			dd_clear_highlight(ctx, dd);
			dd_paint_target_fill(ctx, dd->hover_target, 1);
			dd->painted_target = dd->hover_target;
		} else {
			dd_paint_target_fill(ctx, dd->hover_target, 1);
		}
		if (sk_ui_node_is_valid(dd->highlight_node)) {
			dd_set_hidden(ctx, dd->highlight_node, 1);
		}
		return;
	}
	if (dd->hover_custom_id[0] != '\0') {
		ui_dd_custom_t* c = dd_find_custom(dd, dd->hover_custom_id);
		sk_ui_layout_style_t ls;
		if (c == NULL) {
			dd_clear_highlight(ctx, dd);
			return;
		}
		if (!sk_ui_node_is_valid(dd->highlight_node) || ui->node_alive(ctx, dd->highlight_node) == 0) {
			dd->highlight_node = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, ui->context_root(ctx));
			(void)ui->node_add_class(ctx, dd->highlight_node, SK_UI_CLASS_DRAG_DROP_TARGET);
			(void)ui->node_set_prop_str(ctx, dd->highlight_node, "widget", "drag_drop_highlight");
			(void)ui->node_set_id(ctx, dd->highlight_node, "ui-dd-highlight");
			(void)ui->node_set_pointer_events(ctx, dd->highlight_node, SK_UI_POINTER_EVENTS_NONE);
			(void)ui->node_set_focusable(ctx, dd->highlight_node, 0);
			(void)ui->node_set_prop_i32(ctx, dd->highlight_node, "z_index", 440);
		}
		ui_layout_style_init_default(&ls);
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(c->rect.x);
		ls.top = sk_ui_pt(c->rect.y);
		ls.width = sk_ui_pt(c->rect.width);
		ls.height = sk_ui_pt(c->rect.height);
		(void)ui->node_set_layout_style(ctx, dd->highlight_node, &ls);
		dd_set_hidden(ctx, dd->highlight_node, 0);
		if (sk_ui_node_is_valid(dd->painted_target)) {
			(void)ui->node_remove_class(ctx, dd->painted_target, SK_UI_CLASS_DRAG_DROP_TARGET);
			dd->painted_target = SK_UI_NODE_INVALID;
		}
		return;
	}
	dd_clear_highlight(ctx, dd);
}

static void dd_ensure_preview(sk_ui_context_t* ctx, ui_drag_drop_t* dd) {
	const sk_ui_api_t* ui = dd_api();
	sk_ui_layout_style_t ls;
	if (sk_ui_node_is_valid(dd->preview_node) && ui->node_alive(ctx, dd->preview_node) != 0) {
		return;
	}
	dd->preview_node = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, ui->context_root(ctx));
	(void)ui->node_add_class(ctx, dd->preview_node, SK_UI_CLASS_DRAG_DROP_PREVIEW);
	(void)ui->node_add_class(ctx, dd->preview_node, SK_UI_CLASS_TOOLTIP);
	(void)ui->node_set_prop_str(ctx, dd->preview_node, "widget", "drag_drop_preview");
	(void)ui->node_set_id(ctx, dd->preview_node, "ui-dd-preview");
	(void)ui->node_set_pointer_events(ctx, dd->preview_node, SK_UI_POINTER_EVENTS_NONE);
	(void)ui->node_set_focusable(ctx, dd->preview_node, 0);
	(void)ui->node_set_prop_i32(ctx, dd->preview_node, "z_index", 450);
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	ls.left = sk_ui_pt(0.0f);
	ls.top = sk_ui_pt(0.0f);
	ls.min_width = sk_ui_pt(32.0f);
	ls.min_height = sk_ui_pt(20.0f);
	(void)ui->node_set_layout_style(ctx, dd->preview_node, &ls);
	dd->preview_label = ui->widget_text(ctx, dd->preview_node, "", "ui-dd-preview-text");
	(void)ui->node_set_pointer_events(ctx, dd->preview_label, SK_UI_POINTER_EVENTS_NONE);
	dd_set_hidden(ctx, dd->preview_node, 1);
}

static void dd_update_preview(sk_ui_context_t* ctx, ui_drag_drop_t* dd) {
	const sk_ui_api_t* ui = dd_api();
	i32 show;
	if (dd == NULL) {
		return;
	}
	dd_ensure_preview(ctx, dd);
	show = (dd->active != 0 && dd->preview[0] != '\0' && (dd->hover_flags & SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_PREVIEW_TOOLTIP) == 0u) ? 1 : 0;
	if (sk_ui_node_is_valid(dd->preview_label)) {
		(void)ui->label_set_text(ctx, dd->preview_label, dd->preview);
	}
	dd_set_hidden(ctx, dd->preview_node, show != 0 ? 0 : 1);
}

static void dd_clear_session(sk_ui_context_t* ctx, ui_drag_drop_t* dd) {
	if (dd == NULL) {
		return;
	}
	dd_free_blob(ctx, &dd->data, &dd->size);
	dd->active = 0;
	dd->pending = 0;
	dd->pending_source = SK_UI_NODE_INVALID;
	dd->source = SK_UI_NODE_INVALID;
	dd->type[0] = '\0';
	dd->preview[0] = '\0';
	dd->flags = 0u;
	dd->hover_target = SK_UI_NODE_INVALID;
	dd->hover_custom_id[0] = '\0';
	dd->hover_flags = 0u;
	dd->delivery = 0;
	dd->accepted = 0;
	dd->expire_armed = 0;
	dd_clear_highlight(ctx, dd);
	dd_update_preview(ctx, dd);
	dd_sync_view(dd);
}

static i32 dd_activate_from_source(sk_ui_context_t* ctx, ui_drag_drop_t* dd, const ui_dd_source_t* s) {
	const sk_ui_api_t* ui = dd_api();
	const_chr_t text;
	if (s == NULL) {
		return -1;
	}
	dd_free_blob(ctx, &dd->data, &dd->size);
	if (dd_copy_blob(ctx, &dd->data, &dd->size, s->data, s->size) != 0) {
		return -1;
	}
	dd_copy_type(dd->type, (u32)sizeof(dd->type), s->type);
	dd->flags = s->flags;
	dd->source = s->node;
	dd_copy_type(dd->preview, (u32)sizeof(dd->preview), s->preview);
	if (dd->preview[0] == '\0') {
		text = ui->node_get_id(ctx, s->node);
		if (ui->node_get_visible_text != NULL) {
			const_chr_t vis = ui->node_get_visible_text(ctx, s->node);
			if (vis != NULL && vis[0] != '\0') {
				text = vis;
			}
		}
		if (text != NULL && text[0] != '\0') {
			dd_copy_type(dd->preview, (u32)sizeof(dd->preview), text);
		} else {
			dd_copy_type(dd->preview, (u32)sizeof(dd->preview), s->type);
		}
	}
	dd->active = 1;
	dd->pending = 0;
	dd->delivery = 0;
	dd->accepted = 0;
	dd->expire_armed = 0;
	dd->hover_target = SK_UI_NODE_INVALID;
	dd->hover_custom_id[0] = '\0';
	dd->hover_flags = 0u;
	dd_update_preview(ctx, dd);
	dd_sync_view(dd);
	return 0;
}

static void dd_refresh_hover(sk_ui_context_t* ctx, ui_drag_drop_t* dd) {
	const sk_ui_api_t* ui = dd_api();
	sk_ui_node_t hit;
	ui_dd_target_t* t;
	ui_dd_custom_t* c;
	if (dd == NULL || dd->active == 0) {
		return;
	}
	hit = ui->hit_test(ctx, ctx->pointer_x, ctx->pointer_y);
	t = dd_target_from_hit(ctx, dd, hit);
	dd->hover_target = SK_UI_NODE_INVALID;
	dd->hover_custom_id[0] = '\0';
	dd->hover_flags = 0u;
	if (t != NULL && dd_type_eq(t->type, dd->type) != 0) {
		dd->hover_target = t->node;
		dd->hover_flags = t->flags;
	} else {
		c = dd_custom_at(dd, ctx->pointer_x, ctx->pointer_y);
		if (c != NULL && dd_type_eq(c->type, dd->type) != 0) {
			dd_copy_type(dd->hover_custom_id, (u32)sizeof(dd->hover_custom_id), c->id);
			dd->hover_flags = c->flags;
		}
	}
	if ((dd->flags & SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER) == 0u && sk_ui_node_is_valid(dd->source)) {
		/* Without SourceNoDisableHover, keep hover on the source (ImGui). */
		(void)ui->node_set_state(ctx, dd->source, ui->node_get_state(ctx, dd->source) | (u32)SK_UI_STATE_HOVER);
	}
	dd_apply_highlight(ctx, dd);
	dd_update_preview(ctx, dd);
	dd_sync_view(dd);
}

static i32 dd_type_matches(const_chr_t have, const_chr_t want) {
	if (want == NULL || want[0] == '\0') {
		return 0;
	}
	return dd_type_eq(have, want);
}

static const sk_ui_payload_t* dd_try_accept(sk_ui_context_t* ctx, ui_drag_drop_t* dd, const_chr_t type, i32 custom, const_chr_t custom_id) {
	i32 self = 0;
	(void)ctx;
	if (dd == NULL || dd->delivery == 0 || dd->accepted != 0) {
		return NULL;
	}
	if (dd_type_matches(dd->type, type) == 0) {
		return NULL;
	}
	if (custom != 0) {
		if (custom_id == NULL || dd->hover_custom_id[0] == '\0' || strcmp(dd->hover_custom_id, custom_id) != 0) {
			return NULL;
		}
	} else {
		if (!sk_ui_node_is_valid(dd->hover_target)) {
			return NULL;
		}
		if (sk_ui_node_eq(dd->hover_target, dd->source)) {
			self = 1;
		}
	}
	if (self != 0) {
		return NULL;
	}
	dd->accepted = 1;
	dd_sync_view(dd);
	return &dd->view;
}

i32 ui_drag_drop_init(sk_ui_context_t* ctx) {
	ui_drag_drop_t* dd;
	if (ctx == NULL || ctx->allocator == NULL) {
		return -1;
	}
	dd = (ui_drag_drop_t*)ctx->allocator->alloc(ctx->allocator->instance, sizeof(ui_drag_drop_t));
	if (dd == NULL) {
		return -1;
	}
	memset(dd, 0, sizeof(*dd));
	dd->pending_source = SK_UI_NODE_INVALID;
	dd->source = SK_UI_NODE_INVALID;
	dd->hover_target = SK_UI_NODE_INVALID;
	dd->preview_node = SK_UI_NODE_INVALID;
	dd->preview_label = SK_UI_NODE_INVALID;
	dd->highlight_node = SK_UI_NODE_INVALID;
	dd->painted_target = SK_UI_NODE_INVALID;
	ctx->drag_drop = dd;
	return 0;
}

void ui_drag_drop_shutdown(sk_ui_context_t* ctx) {
	ui_drag_drop_t* dd;
	u32 i;
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return;
	}
	dd = ctx->drag_drop;
	for (i = 0u; i < dd->source_count; ++i) {
		dd_free_blob(ctx, &dd->sources[i].data, &dd->sources[i].size);
	}
	dd_free_blob(ctx, &dd->data, &dd->size);
	ctx->allocator->free(ctx->allocator->instance, dd);
	ctx->drag_drop = NULL;
}

void ui_drag_drop_on_node_destroy(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_drag_drop_t* dd;
	u32 i;
	if (ctx == NULL || ctx->drag_drop == NULL || !sk_ui_node_is_valid(node)) {
		return;
	}
	dd = ctx->drag_drop;
	for (i = 0u; i < dd->source_count; ++i) {
		if (sk_ui_node_eq(dd->sources[i].node, node)) {
			dd_free_blob(ctx, &dd->sources[i].data, &dd->sources[i].size);
			if (i + 1u < dd->source_count) {
				dd->sources[i] = dd->sources[dd->source_count - 1u];
			}
			memset(&dd->sources[dd->source_count - 1u], 0, sizeof(dd->sources[0]));
			dd->source_count -= 1u;
			break;
		}
	}
	for (i = 0u; i < dd->target_count; ++i) {
		if (sk_ui_node_eq(dd->targets[i].node, node)) {
			if (i + 1u < dd->target_count) {
				dd->targets[i] = dd->targets[dd->target_count - 1u];
			}
			memset(&dd->targets[dd->target_count - 1u], 0, sizeof(dd->targets[0]));
			dd->target_count -= 1u;
			break;
		}
	}
	if (sk_ui_node_eq(dd->preview_node, node)) {
		dd->preview_node = SK_UI_NODE_INVALID;
		dd->preview_label = SK_UI_NODE_INVALID;
	}
	if (sk_ui_node_eq(dd->preview_label, node)) {
		dd->preview_label = SK_UI_NODE_INVALID;
	}
	if (sk_ui_node_eq(dd->highlight_node, node)) {
		dd->highlight_node = SK_UI_NODE_INVALID;
	}
	if (sk_ui_node_eq(dd->painted_target, node)) {
		dd->painted_target = SK_UI_NODE_INVALID;
	}
	if (sk_ui_node_eq(dd->source, node) || sk_ui_node_eq(dd->pending_source, node)) {
		dd_clear_session(ctx, dd);
	}
	if (sk_ui_node_eq(dd->hover_target, node)) {
		dd->hover_target = SK_UI_NODE_INVALID;
	}
}

i32 ui_drag_drop_blocks_hold_to_open(const sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->drag_drop == NULL || ctx->drag_drop->active == 0) {
		return 0;
	}
	return (ctx->drag_drop->flags & SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS) != 0u ? 1 : 0;
}

void ui_drag_drop_on_pointer(sk_ui_context_t* ctx, i32 button, i32 down) {
	ui_drag_drop_t* dd;
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return;
	}
	dd = ctx->drag_drop;

	/* Expire a delivered-or-cancelled payload on the next tick after release. */
	if (dd->expire_armed != 0 && (ctx->pointer_buttons & (1u << (u32)SK_UI_POINTER_BUTTON_LEFT)) == 0u) {
		if (dd->accepted != 0 || down != 1) {
			dd_clear_session(ctx, dd);
			return;
		}
	}

	if (button != SK_UI_POINTER_BUTTON_LEFT && down != -1) {
		return;
	}

	if (down == 1) {
		ui_dd_source_t* s = dd_source_from_hit(ctx, dd, ctx->hover);
		if (s == NULL && sk_ui_node_is_valid(ctx->active)) {
			s = dd_source_from_hit(ctx, dd, ctx->active);
		}
		dd_clear_session(ctx, dd);
		if (s != NULL) {
			dd->pending = 1;
			dd->pending_source = s->node;
			dd->start_x = ctx->pointer_x;
			dd->start_y = ctx->pointer_y;
		}
		return;
	}

	if (down == 0) {
		if (dd->pending != 0 && dd->active == 0) {
			dd_clear_session(ctx, dd);
			return;
		}
		if (dd->active != 0) {
			dd_refresh_hover(ctx, dd);
			if ((sk_ui_node_is_valid(dd->hover_target) && !sk_ui_node_eq(dd->hover_target, dd->source)) || dd->hover_custom_id[0] != '\0') {
				dd->delivery = 1;
				dd->expire_armed = 1;
				dd_sync_view(dd);
			} else {
				/* Release outside any target, or drop-on-self: cancel. */
				dd_clear_session(ctx, dd);
			}
		}
		return;
	}

	/* Move (down == -1). */
	if (dd->pending != 0 && dd->active == 0) {
		f32 dx = ctx->pointer_x - dd->start_x;
		f32 dy = ctx->pointer_y - dd->start_y;
		if (dx < 0.0f) {
			dx = -dx;
		}
		if (dy < 0.0f) {
			dy = -dy;
		}
		if (dx >= SK_UI_DRAG_DROP_THRESHOLD || dy >= SK_UI_DRAG_DROP_THRESHOLD) {
			ui_dd_source_t* s = dd_find_source(dd, dd->pending_source);
			if (s == NULL) {
				dd_clear_session(ctx, dd);
				return;
			}
			if (dd_activate_from_source(ctx, dd, s) != 0) {
				dd_clear_session(ctx, dd);
				return;
			}
		}
	}
	if (dd->active != 0) {
		dd_refresh_hover(ctx, dd);
	}
}

void ui_drag_drop_place(sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = dd_api();
	ui_drag_drop_t* dd;
	sk_ui_rect_t r;
	sk_ui_layout_style_t ls;
	f32 vw;
	f32 vh;
	f32 x;
	f32 y;
	f32 w;
	f32 h;
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return;
	}
	dd = ctx->drag_drop;
	dd_update_preview(ctx, dd);
	dd_apply_highlight(ctx, dd);
	if (dd->active == 0 || !sk_ui_node_is_valid(dd->preview_node) || ui->node_alive(ctx, dd->preview_node) == 0) {
		return;
	}
	if (ui->node_get_abs_rect(ctx, dd->preview_node, &r, NULL) != 0) {
		return;
	}
	w = r.width > 1.0f ? r.width : 80.0f;
	h = r.height > 1.0f ? r.height : 22.0f;
	vw = ctx->root_width > 1.0f ? ctx->root_width : 640.0f;
	vh = ctx->root_height > 1.0f ? ctx->root_height : 400.0f;
	x = ctx->pointer_x + SK_UI_TOOLTIP_OFFSET_X;
	y = ctx->pointer_y + SK_UI_TOOLTIP_OFFSET_Y;
	if (x + w > vw) {
		x = vw - w;
	}
	if (y + h > vh) {
		y = vh - h;
	}
	if (x < 0.0f) {
		x = 0.0f;
	}
	if (y < 0.0f) {
		y = 0.0f;
	}
	if (ui->node_get_layout_style(ctx, dd->preview_node, &ls) == 0) {
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(x);
		ls.top = sk_ui_pt(y);
		(void)ui->node_set_layout_style(ctx, dd->preview_node, &ls);
	}
	{
		f32 dx = x - r.x;
		f32 dy = y - r.y;
		ui_node_slot_t* slot = ui_slot_mut(ctx, dd->preview_node);
		if (slot != NULL && (dx < -0.5f || dx > 0.5f || dy < -0.5f || dy > 0.5f)) {
			slot->layout_border.x += dx;
			slot->layout_border.y += dy;
			slot->layout_content.x += dx;
			slot->layout_content.y += dy;
		}
	}
}

i32 ui_drag_drop_source_impl(sk_ui_context_t* ctx, sk_ui_node_t item, const_chr_t type, const void* data, u32 size, u32 flags) {
	ui_drag_drop_t* dd;
	ui_dd_source_t* s;
	if (ctx == NULL || ctx->drag_drop == NULL || !sk_ui_node_is_valid(item) || type == NULL || type[0] == '\0') {
		return -1;
	}
	if (size > 0u && data == NULL) {
		return -1;
	}
	if (dd_api()->node_alive(ctx, item) == 0) {
		return -1;
	}
	dd = ctx->drag_drop;
	s = dd_find_source(dd, item);
	if (s == NULL) {
		if (dd->source_count >= (u32)UI_DD_SOURCE_MAX) {
			return -1;
		}
		s = &dd->sources[dd->source_count++];
		memset(s, 0, sizeof(*s));
		s->node = item;
	}
	dd_copy_type(s->type, (u32)sizeof(s->type), type);
	s->flags = flags;
	if (dd_copy_blob(ctx, &s->data, &s->size, data, size) != 0) {
		return -1;
	}
	if (s->preview[0] == '\0') {
		const_chr_t vis = dd_api()->node_get_visible_text(ctx, item);
		if (vis != NULL && vis[0] != '\0') {
			dd_copy_type(s->preview, (u32)sizeof(s->preview), vis);
		}
	}
	return 0;
}

i32 ui_drag_drop_set_preview_impl(sk_ui_context_t* ctx, sk_ui_node_t item, const_chr_t text) {
	ui_dd_source_t* s;
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return -1;
	}
	s = dd_find_source(ctx->drag_drop, item);
	if (s == NULL) {
		return -1;
	}
	dd_copy_type(s->preview, (u32)sizeof(s->preview), text != NULL ? text : "");
	if (ctx->drag_drop->active != 0 && sk_ui_node_eq(ctx->drag_drop->source, item)) {
		dd_copy_type(ctx->drag_drop->preview, (u32)sizeof(ctx->drag_drop->preview), s->preview);
		dd_update_preview(ctx, ctx->drag_drop);
	}
	return 0;
}

i32 ui_drag_drop_target_impl(sk_ui_context_t* ctx, sk_ui_node_t item, const_chr_t type, u32 flags) {
	ui_drag_drop_t* dd;
	ui_dd_target_t* t;
	if (ctx == NULL || ctx->drag_drop == NULL || !sk_ui_node_is_valid(item) || type == NULL || type[0] == '\0') {
		return -1;
	}
	if (dd_api()->node_alive(ctx, item) == 0) {
		return -1;
	}
	dd = ctx->drag_drop;
	t = dd_find_target(dd, item);
	if (t == NULL) {
		if (dd->target_count >= (u32)UI_DD_TARGET_MAX) {
			return -1;
		}
		t = &dd->targets[dd->target_count++];
		memset(t, 0, sizeof(*t));
		t->node = item;
	}
	dd_copy_type(t->type, (u32)sizeof(t->type), type);
	t->flags = flags;
	return 0;
}

i32 ui_drag_drop_target_custom_impl(sk_ui_context_t* ctx, const sk_ui_rect_t* bb, const_chr_t id, const_chr_t type, u32 flags) {
	ui_drag_drop_t* dd;
	ui_dd_custom_t* c;
	u32 i;
	if (ctx == NULL || ctx->drag_drop == NULL || bb == NULL || id == NULL || id[0] == '\0' || type == NULL || type[0] == '\0') {
		return -1;
	}
	dd = ctx->drag_drop;
	c = dd_find_custom(dd, id);
	if (c == NULL) {
		for (i = 0u; i < (u32)UI_DD_CUSTOM_MAX; ++i) {
			if (dd->customs[i].used == 0u) {
				c = &dd->customs[i];
				memset(c, 0, sizeof(*c));
				c->used = 1u;
				break;
			}
		}
	}
	if (c == NULL) {
		return -1;
	}
	c->rect = *bb;
	dd_copy_type(c->id, (u32)sizeof(c->id), id);
	dd_copy_type(c->type, (u32)sizeof(c->type), type);
	c->flags = flags;
	return 0;
}

i32 ui_drag_drop_target_custom_clear_impl(sk_ui_context_t* ctx, const_chr_t id) {
	ui_dd_custom_t* c;
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return -1;
	}
	c = dd_find_custom(ctx->drag_drop, id);
	if (c == NULL) {
		return -1;
	}
	memset(c, 0, sizeof(*c));
	return 0;
}

const sk_ui_payload_t* ui_drag_drop_accept_impl(sk_ui_context_t* ctx, const_chr_t type, u32 flags) {
	(void)flags;
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return NULL;
	}
	return dd_try_accept(ctx, ctx->drag_drop, type, 0, NULL);
}

const sk_ui_payload_t* ui_drag_drop_accept_custom_impl(sk_ui_context_t* ctx, const_chr_t id, const_chr_t type, u32 flags) {
	(void)flags;
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return NULL;
	}
	return dd_try_accept(ctx, ctx->drag_drop, type, 1, id);
}

const sk_ui_payload_t* ui_drag_drop_get_payload_impl(const sk_ui_context_t* ctx) {
	ui_drag_drop_t* dd;
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return NULL;
	}
	dd = SK_CONST_CAST(ui_drag_drop_t*, ctx->drag_drop);
	if (dd->active == 0 && dd->delivery == 0) {
		return NULL;
	}
	dd_sync_view(dd);
	return &dd->view;
}

i32 ui_drag_drop_is_active_impl(const sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return 0;
	}
	return ctx->drag_drop->active != 0 ? 1 : 0;
}

sk_ui_node_t ui_drag_drop_get_source_impl(const sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return SK_UI_NODE_INVALID;
	}
	return ctx->drag_drop->source;
}

sk_ui_node_t ui_drag_drop_get_hovered_target_impl(const sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return SK_UI_NODE_INVALID;
	}
	return ctx->drag_drop->hover_target;
}

const_chr_t ui_drag_drop_get_hovered_custom_id_impl(const sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->drag_drop == NULL || ctx->drag_drop->hover_custom_id[0] == '\0') {
		return NULL;
	}
	return ctx->drag_drop->hover_custom_id;
}

i32 ui_drag_drop_target_hovered_impl(const sk_ui_context_t* ctx, sk_ui_node_t item) {
	if (ctx == NULL || ctx->drag_drop == NULL || !sk_ui_node_is_valid(item)) {
		return 0;
	}
	return sk_ui_node_eq(ctx->drag_drop->hover_target, item) ? 1 : 0;
}

sk_ui_node_t ui_drag_drop_preview_impl(const sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return SK_UI_NODE_INVALID;
	}
	return ctx->drag_drop->preview_node;
}

i32 ui_drag_drop_begin_impl(sk_ui_context_t* ctx, sk_ui_node_t item) {
	ui_dd_source_t* s;
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return -1;
	}
	s = dd_find_source(ctx->drag_drop, item);
	if (s == NULL) {
		return -1;
	}
	return dd_activate_from_source(ctx, ctx->drag_drop, s);
}

i32 ui_drag_drop_cancel_impl(sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->drag_drop == NULL) {
		return -1;
	}
	dd_clear_session(ctx, ctx->drag_drop);
	return 0;
}

#ifdef SK_TESTS

static const sk_ui_api_t* wdd_api(void) {
	return ui_get_api_table();
}

static void wdd_layout(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 w, f32 h) {
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, w, h));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
}

static void wdd_move(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x, f32 y) {
	sk_ui_input_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = x;
	ev.y = y;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

static void wdd_pointer(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x, f32 y, i32 down) {
	sk_ui_input_event_t ev;
	wdd_move(ui, ctx, x, y);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = x;
	ev.y = y;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = down;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

static void wdd_place(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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

static void wdd_center(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t n, f32* x, f32* y) {
	sk_ui_rect_t r;
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, n, &r, NULL));
	*x = r.x + r.width * 0.5f;
	*y = r.y + r.height * 0.5f;
}

/**
 * Typed payload matches Accept; a different type is rejected. Empty
 * SK_ENTITY_PAYLOAD is a valid blob of size 0.
 */
SK_TEST(ui_widget_drag_drop_payload_type_match_reject) {
	const sk_ui_api_t* ui = wdd_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t src;
	sk_ui_node_t dst;
	sk_ui_node_t other;
	const u32 blob = 0xA11CE55u;
	const sk_ui_payload_t* p;
	f32 x0, y0, x1, y1;

	src = ui->widget_button(ctx, root, "hero.skmesh", "dd-src");
	dst = ui->widget_button(ctx, root, "Mesh", "dd-dst");
	other = ui->widget_button(ctx, root, "Other", "dd-other");
	wdd_place(ui, ctx, src, 8.0f, 8.0f, 120.0f, 24.0f);
	wdd_place(ui, ctx, dst, 8.0f, 48.0f, 120.0f, 24.0f);
	wdd_place(ui, ctx, other, 8.0f, 88.0f, 120.0f, 24.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, src, SK_UI_ASSET_PAYLOAD, &blob, (u32)sizeof(blob),
												  SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS | SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_set_preview(ctx, src, "hero.skmesh"));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target(ctx, dst, SK_UI_ASSET_PAYLOAD, SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_DRAW_DEFAULT_RECT));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target(ctx, other, SK_UI_ENTITY_PAYLOAD, 0u));
	wdd_layout(ui, ctx, 320.0f, 200.0f);

	wdd_center(ui, ctx, src, &x0, &y0);
	wdd_center(ui, ctx, dst, &x1, &y1);
	wdd_pointer(ui, ctx, x0, y0, 1);
	wdd_move(ui, ctx, x0 + 10.0f, y0 + 10.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_is_active(ctx));
	p = ui->drag_drop_get_payload(ctx);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_EQUAL_STRING(SK_UI_ASSET_PAYLOAD, p->type);
	TEST_ASSERT_EQUAL_UINT32((u32)sizeof(blob), p->size);
	TEST_ASSERT_NOT_NULL(p->data);
	TEST_ASSERT_EQUAL_UINT32(blob, *(const u32*)p->data);

	wdd_move(ui, ctx, x1, y1);
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_target_hovered(ctx, dst));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target_hovered(ctx, other));
	p = ui->drag_drop_get_payload(ctx);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_EQUAL_INT(1, p->preview);
	wdd_pointer(ui, ctx, x1, y1, 0);
	TEST_ASSERT_NULL(ui->drag_drop_accept(ctx, SK_UI_ENTITY_PAYLOAD, 0u));
	p = ui->drag_drop_accept(ctx, SK_UI_ASSET_PAYLOAD, SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_DRAW_DEFAULT_RECT);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_EQUAL_UINT32(blob, *(const u32*)p->data);
	TEST_ASSERT_NULL(ui->drag_drop_accept(ctx, SK_UI_ASSET_PAYLOAD, 0u)); /* consume-on-read */

	/* Empty entity payload is a valid size-0 blob. */
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, src, SK_UI_ENTITY_PAYLOAD, NULL, 0u, SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target(ctx, dst, SK_UI_ENTITY_PAYLOAD, 0u));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_begin(ctx, src));
	p = ui->drag_drop_get_payload(ctx);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_EQUAL_STRING(SK_UI_ENTITY_PAYLOAD, p->type);
	TEST_ASSERT_EQUAL_UINT32(0u, p->size);
	TEST_ASSERT_NULL(p->data);

	ui->context_destroy(ctx);
}

/**
 * Payload stays live across frames while the mouse is down, then expires
 * after release outside a target.
 */
SK_TEST(ui_widget_drag_drop_payload_lifetime_across_frames) {
	const sk_ui_api_t* ui = wdd_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t src;
	sk_ui_node_t dst;
	const u8 blob[4] = {1u, 2u, 3u, 4u};
	const sk_ui_payload_t* p;
	const void* kept;
	f32 x0, y0, x1, y1;

	src = ui->widget_button(ctx, root, "A", "dd-life-src");
	dst = ui->widget_button(ctx, root, "B", "dd-life-dst");
	wdd_place(ui, ctx, src, 8.0f, 8.0f, 80.0f, 22.0f);
	wdd_place(ui, ctx, dst, 160.0f, 8.0f, 80.0f, 22.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, src, SK_UI_ASSET_PAYLOAD, blob, 4u, SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target(ctx, dst, SK_UI_ASSET_PAYLOAD, 0u));
	wdd_layout(ui, ctx, 320.0f, 120.0f);
	wdd_center(ui, ctx, src, &x0, &y0);
	wdd_center(ui, ctx, dst, &x1, &y1);

	wdd_pointer(ui, ctx, x0, y0, 1);
	wdd_move(ui, ctx, x0 + 12.0f, y0);
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_is_active(ctx));
	p = ui->drag_drop_get_payload(ctx);
	TEST_ASSERT_NOT_NULL(p);
	kept = p->data;
	TEST_ASSERT_NOT_NULL(kept);

	wdd_layout(ui, ctx, 320.0f, 120.0f);
	p = ui->drag_drop_get_payload(ctx);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_TRUE(p->data == kept);
	TEST_ASSERT_EQUAL_UINT32(4u, p->size);
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_is_active(ctx));

	wdd_layout(ui, ctx, 320.0f, 120.0f);
	p = ui->drag_drop_get_payload(ctx);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_TRUE(p->data == kept);

	/* Release far from any target: cancel, payload gone. */
	wdd_pointer(ui, ctx, 300.0f, 100.0f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_is_active(ctx));
	TEST_ASSERT_NULL(ui->drag_drop_get_payload(ctx));
	TEST_ASSERT_NULL(ui->drag_drop_accept(ctx, SK_UI_ASSET_PAYLOAD, 0u));

	ui->context_destroy(ctx);
}

/** Dropping a payload back on its source is rejected. */
SK_TEST(ui_widget_drag_drop_drop_on_self_rejected) {
	const sk_ui_api_t* ui = wdd_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t src;
	const u32 blob = 7u;
	f32 x, y;

	src = ui->widget_button(ctx, root, "Self", "dd-self");
	wdd_place(ui, ctx, src, 16.0f, 16.0f, 100.0f, 24.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, src, SK_UI_ASSET_PAYLOAD, &blob, (u32)sizeof(blob), 0u));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target(ctx, src, SK_UI_ASSET_PAYLOAD, 0u));
	wdd_layout(ui, ctx, 240.0f, 80.0f);
	wdd_center(ui, ctx, src, &x, &y);

	wdd_pointer(ui, ctx, x, y, 1);
	wdd_move(ui, ctx, x + 8.0f, y);
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_is_active(ctx));
	wdd_pointer(ui, ctx, x, y, 0);
	TEST_ASSERT_NULL(ui->drag_drop_accept(ctx, SK_UI_ASSET_PAYLOAD, 0u));
	TEST_ASSERT_NULL(ui->drag_drop_get_payload(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_is_active(ctx));

	ui->context_destroy(ctx);
}

/** Release outside every target cancels; no accept, no leftover payload. */
SK_TEST(ui_widget_drag_drop_cancel_release_outside) {
	const sk_ui_api_t* ui = wdd_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t src;
	sk_ui_node_t dst;
	f32 x, y;

	src = ui->widget_button(ctx, root, "Src", "dd-out-src");
	dst = ui->widget_button(ctx, root, "Dst", "dd-out-dst");
	wdd_place(ui, ctx, src, 8.0f, 8.0f, 70.0f, 22.0f);
	wdd_place(ui, ctx, dst, 8.0f, 80.0f, 70.0f, 22.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, src, SK_UI_ENTITY_PAYLOAD, NULL, 0u, SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_set_preview(ctx, src, "1 entity"));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target(ctx, dst, SK_UI_ENTITY_PAYLOAD, SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_PREVIEW_TOOLTIP));
	wdd_layout(ui, ctx, 280.0f, 160.0f);
	wdd_center(ui, ctx, src, &x, &y);

	wdd_pointer(ui, ctx, x, y, 1);
	wdd_move(ui, ctx, x + 20.0f, y + 4.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_is_active(ctx));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->drag_drop_preview(ctx)));
	wdd_pointer(ui, ctx, 250.0f, 140.0f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_is_active(ctx));
	TEST_ASSERT_NULL(ui->drag_drop_get_payload(ctx));
	TEST_ASSERT_NULL(ui->drag_drop_accept(ctx, SK_UI_ENTITY_PAYLOAD, 0u));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target_hovered(ctx, dst));

	ui->context_destroy(ctx);
}

/** BeginDragDropTargetCustom hits the caller rect + id; misses outside it. */
SK_TEST(ui_widget_drag_drop_custom_rect_hit) {
	const sk_ui_api_t* ui = wdd_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t src;
	sk_ui_rect_t gap;
	sk_ui_rect_t view;
	const sk_ui_payload_t* p;
	f32 x, y;

	src = ui->widget_button(ctx, root, "Cam", "dd-cust-src");
	wdd_place(ui, ctx, src, 8.0f, 8.0f, 80.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, src, SK_UI_ENTITY_PAYLOAD, NULL, 0u, SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS));
	memset(&gap, 0, sizeof(gap));
	gap.x = 8.0f;
	gap.y = 36.0f;
	gap.width = 200.0f;
	gap.height = 8.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target_custom(ctx, &gap, "dd-between", SK_UI_ENTITY_PAYLOAD, SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_DRAW_DEFAULT_RECT));
	memset(&view, 0, sizeof(view));
	view.x = 0.0f;
	view.y = 80.0f;
	view.width = 320.0f;
	view.height = 120.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target_custom(ctx, &view, "dd-viewport", SK_UI_ASSET_PAYLOAD, 0u));
	wdd_layout(ui, ctx, 320.0f, 220.0f);
	wdd_center(ui, ctx, src, &x, &y);

	wdd_pointer(ui, ctx, x, y, 1);
	wdd_move(ui, ctx, 20.0f, 40.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_is_active(ctx));
	TEST_ASSERT_NOT_NULL(ui->drag_drop_get_hovered_custom_id(ctx));
	TEST_ASSERT_EQUAL_STRING("dd-between", ui->drag_drop_get_hovered_custom_id(ctx));
	wdd_pointer(ui, ctx, 20.0f, 40.0f, 0);
	TEST_ASSERT_NULL(ui->drag_drop_accept_custom(ctx, "dd-viewport", SK_UI_ENTITY_PAYLOAD, 0u));
	p = ui->drag_drop_accept_custom(ctx, "dd-between", SK_UI_ENTITY_PAYLOAD, SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_DRAW_DEFAULT_RECT);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_EQUAL_STRING(SK_UI_ENTITY_PAYLOAD, p->type);
	TEST_ASSERT_EQUAL_UINT32(0u, p->size);

	/* Miss: drag into empty space, not the custom strip. */
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_begin(ctx, src));
	wdd_move(ui, ctx, 20.0f, 60.0f);
	TEST_ASSERT_NULL(ui->drag_drop_get_hovered_custom_id(ctx));
	wdd_pointer(ui, ctx, 20.0f, 60.0f, 0);
	TEST_ASSERT_NULL(ui->drag_drop_accept_custom(ctx, "dd-between", SK_UI_ENTITY_PAYLOAD, 0u));

	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target_custom_clear(ctx, "dd-between"));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_begin(ctx, src));
	wdd_move(ui, ctx, 20.0f, 40.0f);
	TEST_ASSERT_NULL(ui->drag_drop_get_hovered_custom_id(ctx));

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
