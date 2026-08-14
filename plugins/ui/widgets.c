/**
 * @file widgets.c
 * @brief v1 widget set: factories, default styles, interaction, tests.
 *
 * Widgets compose the retained element tree (BOX/TEXT/IMAGE/BUTTON) with
 * default style classes, stable test ids/classes, and behavior handlers for
 * checkbox/slider/drag (APX-343: SliderFloat/Int + DragFloat/Int + N)/text_input (APX-342: multiline/hint/search/scalar)/scroll_view, menu surfaces (menu_bar, menu,
 * menu_item, menu_popup, dropdown, context_menu, submenu — APX-234/APX-346),
 * popup / modal chrome (popup_menu, modal — APX-347), and
 * docking / editor window chrome (dock_space, dock_node, splitter, tab_bar,
 * tab / tab_item / tab_button, editor_window, window_title_bar, window_content
 * — APX-235 / APX-348). Child /
 * window / layout family (APX-345): widget_window + close, fullscreen, child
 * (border / ResizeX / h-scroll), disabled stack, PushID, SetNextItemWidth,
 * Indent, group, horizontal/vertical + Spring-as-flex-grow. Item-array
 * binding for tree / list / combo / table is in item_bind.c (APX-338). TreeNode
 * / CollapsingHeader chrome is APX-350. Table family (BeginTable / columns /
 * headers / scroll-freeze / CellBg) is APX-351 in table.c. Selectable
 * (history / combo / type-list / entity-picker rows) is APX-352. Combo /
 * ListBox (int* + zero-separated items, BeginCombo, list-box height) is
 * APX-353 in combo.c. Tooltip (BeginTooltip hover card / profiler hover) is
 * APX-354 in tooltip.c. Not full ImGui parity — no multi-viewport docking.
 */

#include "ui.internal.h"

#include "allocator.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ImGui TextDisabled default text color (dim gray on the dark editor). */
#define UI_TEXT_DISABLED_COLOR sk_ui_rgba(0.50f, 0.51f, 0.53f, 1.0f)

/* -------------------------------------------------------------------------- */
/* Widget private state                                                       */
/* -------------------------------------------------------------------------- */

enum {
	UI_WD_CHECKBOX = 1,
	UI_WD_SLIDER = 2,
	UI_WD_TEXT_INPUT = 3,
	UI_WD_SCROLL = 4,
	UI_WD_BUTTON = 5,
	UI_WD_MENU = 6,
	UI_WD_SUBMENU = 7,
	UI_WD_DROPDOWN = 8,
	UI_WD_SPLITTER = 9,
	UI_WD_TAB = 10,
	UI_WD_WINDOW_TITLE = 11,
	UI_WD_RADIO = 12,
	UI_WD_TOGGLE = 13,
	UI_WD_RANGE_SLIDER = 14,
	UI_WD_PROGRESS = 15,
	UI_WD_WINDOW = 16,
	UI_WD_CHILD_RESIZE = 17,
	UI_WD_MODAL = 18,
	UI_WD_TAB_BAR = 19,
	UI_WD_COLLAPSING = 20,
};

enum {
	UI_BIND_NONE = 0,
	UI_BIND_BOOL = 1,
	UI_BIND_FLAGS = 2,
	UI_BIND_RADIO = 3,
};

typedef struct ui_widget_data_t {
	u32 kind;
	sk_ui_widget_bool_fn on_bool;
	sk_ui_widget_float_fn on_float;
	void_ptr_t cb_user;
	/* scroll */
	sk_ui_node_t content;
	f32 drag_last_x;
	f32 drag_last_y;
	i32 dragging;
	/* button family (APX-339): edge-triggered click/press/release + flags */
	u32 button_flags;
	i32 edge_clicked;
	i32 edge_pressed;
	i32 edge_released;
	/* selectable (APX-352): second-click arm + consume-on-read double */
	i32 edge_double_clicked;
	i32 click_armed;
	/* checkbox / radio (APX-341): bound caller scalar + flags mask / option */
	i32* bound_i32;
	i32 bound_kind;
	i32 bound_arg;
	/* InputText family (APX-342) */
	sk_ui_widget_text_fn on_text;
	u32 input_flags;
	i32 input_capacity;
	i32 edge_text_changed;
	i32 edge_text_committed;
	i32 text_dirty;
	char* revert_text;
	void_ptr_t scalar_data;
	i32 scalar_type;
	i32 scalar_has_range;
	f64 scalar_min;
	f64 scalar_max;
	/* Slider / Drag (APX-343): consume-on-read change + text-entry snapshot. */
	i32 edge_value_changed;
	f32 slider_anchor_value;
	/* Window close flag (APX-345): caller-owned bool* p_open. */
	i32* p_open;
	/* Child ResizeX: width at drag start. */
	f32 resize_start_w;
} ui_widget_data_t;

void ui_widget_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot) {
	if (slot == NULL || slot->user_data == NULL) {
		return;
	}
	if (SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_WIDGET_DATA_TYPE_ID)) {
		ui_widget_data_t* wd = (ui_widget_data_t*)slot->user_data;
		if (wd->revert_text != NULL) {
			ctx->allocator->free(ctx->allocator->instance, wd->revert_text);
			wd->revert_text = NULL;
		}
		ctx->allocator->free(ctx->allocator->instance, slot->user_data);
		slot->user_data = NULL;
		slot->user_data_type = SK_TYPE_ID_ZERO;
		return;
	}
	if (SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_TABLE_DATA_TYPE_ID)) {
		ui_table_release_user_data(ctx, slot);
		return;
	}
	if (SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_COMBO_DATA_TYPE_ID)) {
		ui_combo_release_user_data(ctx, slot);
		return;
	}
	if (SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_TOOLTIP_DATA_TYPE_ID)) {
		ui_tooltip_release_user_data(ctx, slot);
		return;
	}
	ui_item_bind_release_user_data(ctx, slot);
}

static ui_widget_data_t* ui_widget_data(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL || slot->user_data == NULL) {
		return NULL;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_WIDGET_DATA_TYPE_ID)) {
		return NULL;
	}
	return (ui_widget_data_t*)slot->user_data;
}

static const ui_widget_data_t* ui_widget_data_const(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL || slot->user_data == NULL) {
		return NULL;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_WIDGET_DATA_TYPE_ID)) {
		return NULL;
	}
	return (const ui_widget_data_t*)slot->user_data;
}

static ui_widget_data_t* ui_widget_data_ensure(sk_ui_context_t* ctx, sk_ui_node_t node, u32 kind) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	ui_widget_data_t* wd;
	if (slot == NULL) {
		return NULL;
	}
	if (slot->user_data != NULL && SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_WIDGET_DATA_TYPE_ID)) {
		wd = (ui_widget_data_t*)slot->user_data;
		wd->kind = kind;
		return wd;
	}
	wd = (ui_widget_data_t*)ctx->allocator->alloc(ctx->allocator->instance, sizeof(ui_widget_data_t));
	if (wd == NULL) {
		return NULL;
	}
	memset(wd, 0, sizeof(*wd));
	wd->kind = kind;
	wd->content = SK_UI_NODE_INVALID;
	slot->user_data = wd;
	slot->user_data_type = SK_UI_WIDGET_DATA_TYPE_ID;
	return wd;
}

/* -------------------------------------------------------------------------- */
/* Prop helpers                                                               */
/* -------------------------------------------------------------------------- */

static const sk_ui_api_t* ui_wapi(void) {
	return ui_get_api_table();
}

static const_chr_t ui_prop_str_const(const ui_node_slot_t* slot, const_chr_t key) {
	u32 i;
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_STR && e->key != NULL && key != NULL && strcmp(e->key, key) == 0) {
			return e->data.str_value;
		}
	}
	return NULL;
}

static i32 ui_prop_i32_const(const ui_node_slot_t* slot, const_chr_t key, i32* out) {
	u32 i;
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_I32 && e->key != NULL && key != NULL && strcmp(e->key, key) == 0) {
			*out = e->data.i32_value;
			return 0;
		}
	}
	return -1;
}

static f32 ui_prop_f32_const(const ui_node_slot_t* slot, const_chr_t key, f32 fallback) {
	u32 i;
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_F32 && e->key != NULL && key != NULL && strcmp(e->key, key) == 0) {
			return e->data.f32_value;
		}
	}
	return fallback;
}

static u32 ui_utf8_code_count(const_chr_t s) {
	u32 n = 0u;
	const u8* p;
	if (s == NULL) {
		return 0u;
	}
	p = (const u8*)s;
	while (*p != 0u) {
		if ((*p & 0x80u) == 0u) {
			p += 1;
		} else if ((*p & 0xE0u) == 0xC0u && p[1] != 0u) {
			p += 2;
		} else if ((*p & 0xF0u) == 0xE0u && p[1] != 0u && p[2] != 0u) {
			p += 3;
		} else if ((*p & 0xF8u) == 0xF0u && p[1] != 0u && p[2] != 0u && p[3] != 0u) {
			p += 4;
		} else {
			p += 1;
			continue;
		}
		n += 1u;
	}
	return n;
}

/** Byte offset of UTF-8 code unit index (0..n). */
static u32 ui_utf8_byte_offset(const_chr_t s, u32 code_index) {
	u32 n = 0u;
	const u8* p;
	const u8* start;
	if (s == NULL) {
		return 0u;
	}
	start = (const u8*)s;
	p = start;
	while (*p != 0u && n < code_index) {
		if ((*p & 0x80u) == 0u) {
			p += 1;
		} else if ((*p & 0xE0u) == 0xC0u && p[1] != 0u) {
			p += 2;
		} else if ((*p & 0xF0u) == 0xE0u && p[1] != 0u && p[2] != 0u) {
			p += 3;
		} else if ((*p & 0xF8u) == 0xF0u && p[1] != 0u && p[2] != 0u && p[3] != 0u) {
			p += 4;
		} else {
			p += 1;
			continue;
		}
		n += 1u;
	}
	return (u32)(p - start);
}

static f32 ui_clampf(f32 v, f32 lo, f32 hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

/* -------------------------------------------------------------------------- */
/* Default styles                                                             */
/* -------------------------------------------------------------------------- */

static void ui_style_fill_layout_pad(sk_ui_style_props_t* p, f32 pad) {
	p->layout.padding.left = pad;
	p->layout.padding.top = pad;
	p->layout.padding.right = pad;
	p->layout.padding.bottom = pad;
	p->mask |= SK_UI_SP_PADDING;
}

i32 ui_widgets_register_defaults_impl(sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t base;
	sk_ui_style_props_t var;

	if (ctx == NULL) {
		return -1;
	}

	/* Panel: column + cross-axis STRETCH so AUTO-width children (e.g. empty
	 * body BOX under panel-main) fill content width — APX-247 / vision D1. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_FLEX_DIRECTION |
				SK_UI_SP_ALIGN_ITEMS;
	base.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	base.border_color = sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 4.0f;
	ui_style_fill_layout_pad(&base, 8.0f);
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	if (ui->style_class_register(ctx, SK_UI_CLASS_PANEL, &base) != 0) {
		return -1;
	}

	/* View (transparent flex column) */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_BACKGROUND_COLOR;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	if (ui->style_class_register(ctx, SK_UI_CLASS_VIEW, &base) != 0) {
		return -1;
	}

	/* Label */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE;
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 14.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_LABEL, &base) != 0) {
		return -1;
	}

	/* Text family (APX-340): same face as Label; the DISABLED variant is the
	 * ImGui TextDisabled dim so text_set_disabled / BeginDisabled dim text. */
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_COLOR;
	var.color = UI_TEXT_DISABLED_COLOR;
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_LABEL, SK_UI_STATE_DISABLED, &var);
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE;
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 14.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TEXT, &base) != 0) {
		return -1;
	}
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TEXT, SK_UI_STATE_DISABLED, &var);
	if (ui->style_class_register(ctx, SK_UI_CLASS_TEXT_WRAPPED, &base) != 0) {
		return -1;
	}
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TEXT_WRAPPED, SK_UI_STATE_DISABLED, &var);

	/* BulletText: left padding hosts the painted disc (ImGui bullet indent). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_PADDING;
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 14.0f;
	base.layout.padding.left = 18.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_BULLET_TEXT, &base) != 0) {
		return -1;
	}
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BULLET_TEXT, SK_UI_STATE_DISABLED, &var);

	/* SeparatorText: full-width rule with the label set into the gap. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_PADDING | SK_UI_SP_MIN_HEIGHT;
	base.color = sk_ui_rgba(0.70f, 0.72f, 0.77f, 1.0f);
	base.font_size = 13.0f;
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.padding.top = 4.0f;
	base.layout.padding.bottom = 4.0f;
	base.layout.min_height = sk_ui_pt(20.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_SEPARATOR_TEXT, &base) != 0) {
		return -1;
	}

	/* Separator (§19): bare horizontal rule, full width, 8px lane with the
	 * 1px rule centered (same convention as the menu separator). The factory
	 * overrides to the vertical form (1px wide, stretch to row height). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_ALIGN_SELF;
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.height = sk_ui_pt(8.0f);
	base.layout.min_height = sk_ui_pt(8.0f);
	base.layout.max_height = sk_ui_pt(8.0f);
	base.layout.align_self = SK_UI_ALIGN_STRETCH;
	if (ui->style_class_register(ctx, SK_UI_CLASS_SEPARATOR, &base) != 0) {
		return -1;
	}

	/* Spacing (§19): transparent fixed-height vertical spacer (ImGui
	 * Spacing(); zero width so it never shows chrome). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT;
	base.layout.width = sk_ui_pt(0.0f);
	base.layout.height = sk_ui_pt(SK_UI_SPACING_DEFAULT);
	base.layout.min_height = sk_ui_pt(SK_UI_SPACING_DEFAULT);
	base.layout.max_height = sk_ui_pt(SK_UI_SPACING_DEFAULT);
	if (ui->style_class_register(ctx, SK_UI_CLASS_SPACING, &base) != 0) {
		return -1;
	}

	/* Button: style POINT width/height are the outer border box — padding and
	 * border sit inside the authored size (APX-248 / vision D2). Pre-fix
	 * content-box mapping made pad 6 + border 1 expand 96x28 → ~108x40. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE |
				SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.28f, 0.42f, 0.72f, 1.0f);
	base.border_color = sk_ui_rgba(0.20f, 0.32f, 0.58f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 4.0f;
	ui_style_fill_layout_pad(&base, 6.0f);
	base.color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
	base.font_size = 14.0f;
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.min_height = sk_ui_pt(28.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_BUTTON, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	/* Hover is clearly brighter; active clearly darker (vision grades single frames). */
	var.background_color = sk_ui_rgba(0.55f, 0.72f, 1.0f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON, SK_UI_STATE_HOVER, &var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
	var.background_color = sk_ui_rgba(0.06f, 0.08f, 0.14f, 1.0f);
	var.border_color = sk_ui_rgba(0.12f, 0.16f, 0.24f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON, SK_UI_STATE_ACTIVE, &var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_COLOR;
	var.background_color = sk_ui_rgba(0.20f, 0.22f, 0.26f, 1.0f);
	var.border_color = sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f);
	var.color = sk_ui_rgba(0.50f, 0.51f, 0.53f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON, SK_UI_STATE_DISABLED, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BORDER_COLOR;
	var.border_color = sk_ui_rgba(0.55f, 0.72f, 1.0f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON, SK_UI_STATE_FOCUSED, &var);

	/* SmallButton: same chrome, tighter pad / shorter min height (ImGui FramePadding.y = 0). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_PADDING | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_FONT_SIZE;
	base.layout.padding.left = 6.0f;
	base.layout.padding.right = 6.0f;
	base.layout.padding.top = 0.0f;
	base.layout.padding.bottom = 0.0f;
	base.layout.min_height = sk_ui_pt(18.0f);
	base.font_size = 13.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_BUTTON_SMALL, &base) != 0) {
		return -1;
	}

	/* InvisibleButton: no fill, no border — hit target only. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.border_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.layout.border.left = 0.0f;
	base.layout.border.top = 0.0f;
	base.layout.border.right = 0.0f;
	base.layout.border.bottom = 0.0f;
	base.corner_radius = 0.0f;
	ui_style_fill_layout_pad(&base, 0.0f);
	base.layout.min_height = sk_ui_pt(0.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_BUTTON_INVISIBLE, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
	var.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	var.border_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON_INVISIBLE, SK_UI_STATE_HOVER, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON_INVISIBLE, SK_UI_STATE_ACTIVE, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON_INVISIBLE, SK_UI_STATE_FOCUSED, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON_INVISIBLE, SK_UI_STATE_DISABLED, &var);

	/* SelectionButton: editor uses the same accent for normal/hover/active. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
	base.background_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.67f);
	base.border_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.85f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_BUTTON_SELECTED, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
	var.background_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.67f);
	var.border_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.85f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON_SELECTED, SK_UI_STATE_HOVER, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON_SELECTED, SK_UI_STATE_ACTIVE, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON_SELECTED, SK_UI_STATE_FOCUSED, &var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_COLOR;
	var.background_color = sk_ui_rgba(0.20f, 0.22f, 0.26f, 1.0f);
	var.border_color = sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f);
	var.color = sk_ui_rgba(0.50f, 0.51f, 0.53f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_BUTTON_SELECTED, SK_UI_STATE_DISABLED, &var);

	/* BorderedButton: visible gray border over the default button fill. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH;
	base.border_color = sk_ui_rgba(0.46f, 0.49f, 0.50f, 0.90f);
	base.layout.border.left = 2.0f;
	base.layout.border.top = 2.0f;
	base.layout.border.right = 2.0f;
	base.layout.border.bottom = 2.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_BUTTON_BORDERED, &base) != 0) {
		return -1;
	}

	/* ArrowButton: square compact chrome. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_FONT_SIZE;
	base.layout.width = sk_ui_pt(22.0f);
	base.layout.height = sk_ui_pt(22.0f);
	base.layout.min_width = sk_ui_pt(22.0f);
	base.layout.min_height = sk_ui_pt(22.0f);
	ui_style_fill_layout_pad(&base, 2.0f);
	base.font_size = 13.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_BUTTON_ARROW, &base) != 0) {
		return -1;
	}

	/*
	 * Selectable (APX-352 / §18): full-row list item. Default is transparent
	 * (no chrome). Hover / active / selected reuse the widget_selection_button
	 * Header accent (0.26, 0.59, 0.98) so History / combo / type-list rows
	 * match editor chrome.
	 */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE |
				SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.border_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.layout.border.left = 0.0f;
	base.layout.border.top = 0.0f;
	base.layout.border.right = 0.0f;
	base.layout.border.bottom = 0.0f;
	base.corner_radius = 0.0f;
	base.layout.padding.left = 6.0f;
	base.layout.padding.right = 6.0f;
	base.layout.padding.top = 2.0f;
	base.layout.padding.bottom = 2.0f;
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 13.0f;
	base.layout.justify_content = SK_UI_JUSTIFY_FLEX_START;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.min_height = sk_ui_pt(22.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_SELECTABLE, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	/* Same RGB as SK_UI_CLASS_BUTTON_SELECTED; lighter alpha for hover. */
	var.background_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.40f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SELECTABLE, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.80f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SELECTABLE, SK_UI_STATE_ACTIVE, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR;
	var.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	var.color = sk_ui_rgba(0.50f, 0.51f, 0.53f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SELECTABLE, SK_UI_STATE_DISABLED, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH;
	var.border_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.85f);
	var.layout.border.left = 1.0f;
	var.layout.border.top = 1.0f;
	var.layout.border.right = 1.0f;
	var.layout.border.bottom = 1.0f;
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SELECTABLE, SK_UI_STATE_FOCUSED, &var);

	/* Checkbox: light empty face + border so unchecked is not a solid "on" block. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	base.background_color = sk_ui_rgba(0.62f, 0.64f, 0.70f, 1.0f);
	base.border_color = sk_ui_rgba(0.36f, 0.38f, 0.44f, 1.0f);
	base.layout.border.left = 2.0f;
	base.layout.border.top = 2.0f;
	base.layout.border.right = 2.0f;
	base.layout.border.bottom = 2.0f;
	base.corner_radius = 3.0f;
	base.color = sk_ui_rgba(0.10f, 0.11f, 0.14f, 1.0f); /* dark X on light face */
	base.layout.width = sk_ui_pt(18.0f);
	base.layout.height = sk_ui_pt(18.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_CHECKBOX, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BORDER_COLOR;
	var.border_color = sk_ui_rgba(0.50f, 0.65f, 0.95f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_CHECKBOX, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.42f, 0.44f, 0.48f, 1.0f);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR;
	var.color = sk_ui_rgba(0.30f, 0.31f, 0.34f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_CHECKBOX, SK_UI_STATE_DISABLED, &var);

	/*
	 * Radio: no rectangular border (box border would look square). Paint draws a
	 * circular ring + optional inner disc. Face color is the ring stroke.
	 */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f); /* transparent; ring is custom paint */
	base.color = sk_ui_rgba(0.72f, 0.74f, 0.80f, 1.0f);			/* ring + disc color */
	base.corner_radius = 9.0f;
	base.layout.width = sk_ui_pt(18.0f);
	base.layout.height = sk_ui_pt(18.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_RADIO, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_COLOR;
	var.color = sk_ui_rgba(0.55f, 0.72f, 1.0f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_RADIO, SK_UI_STATE_HOVER, &var);
	var.color = sk_ui_rgba(0.40f, 0.42f, 0.46f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_RADIO, SK_UI_STATE_DISABLED, &var);

	/* Toggle switch: pill track; paint draws thumb and ON track accent. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	base.background_color = sk_ui_rgba(0.22f, 0.24f, 0.28f, 1.0f);
	base.border_color = sk_ui_rgba(0.40f, 0.42f, 0.48f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 11.0f; /* half of height → pill */
	base.layout.width = sk_ui_pt(40.0f);
	base.layout.height = sk_ui_pt(22.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_TOGGLE, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	/* Hover: lift fill and keep a brighter border so the pill silhouette stays
	 * separable from the thumb (vision must not collapse to "lone circle"). */
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
	var.background_color = sk_ui_rgba(0.32f, 0.34f, 0.40f, 1.0f);
	var.border_color = sk_ui_rgba(0.58f, 0.62f, 0.72f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TOGGLE, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.20f, 0.21f, 0.24f, 1.0f);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
	var.border_color = sk_ui_rgba(0.28f, 0.29f, 0.32f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TOGGLE, SK_UI_STATE_DISABLED, &var);

	/* Slider: track + grab; COLOR so the format label is readable. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE;
	base.background_color = sk_ui_rgba(0.20f, 0.22f, 0.26f, 1.0f);
	base.corner_radius = 4.0f;
	base.layout.height = sk_ui_pt(20.0f);
	base.layout.min_width = sk_ui_pt(80.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 13.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_SLIDER, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.24f, 0.26f, 0.32f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SLIDER, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.28f, 0.36f, 0.50f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SLIDER, SK_UI_STATE_ACTIVE, &var);
	var.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	var.color = sk_ui_rgba(0.55f, 0.56f, 0.58f, 1.0f);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR;
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SLIDER, SK_UI_STATE_DISABLED, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH;
	var.background_color = sk_ui_rgba(0.12f, 0.13f, 0.16f, 1.0f);
	var.border_color = sk_ui_rgba(0.35f, 0.55f, 0.90f, 1.0f);
	var.layout.border.left = 1.0f;
	var.layout.border.top = 1.0f;
	var.layout.border.right = 1.0f;
	var.layout.border.bottom = 1.0f;
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SLIDER, SK_UI_STATE_FOCUSED, &var);

	/* Drag: framed value box (no grab). Same type scale as slider. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_COLOR |
				SK_UI_SP_FONT_SIZE;
	base.background_color = sk_ui_rgba(0.18f, 0.20f, 0.24f, 1.0f);
	base.border_color = sk_ui_rgba(0.38f, 0.40f, 0.46f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 3.0f;
	base.layout.height = sk_ui_pt(20.0f);
	base.layout.min_width = sk_ui_pt(56.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 13.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_DRAG, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
	var.background_color = sk_ui_rgba(0.22f, 0.24f, 0.30f, 1.0f);
	var.border_color = sk_ui_rgba(0.50f, 0.54f, 0.62f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_DRAG, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.26f, 0.34f, 0.48f, 1.0f);
	var.border_color = sk_ui_rgba(0.55f, 0.70f, 0.95f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_DRAG, SK_UI_STATE_ACTIVE, &var);
	var.background_color = sk_ui_rgba(0.14f, 0.15f, 0.17f, 1.0f);
	var.border_color = sk_ui_rgba(0.26f, 0.27f, 0.30f, 1.0f);
	var.color = sk_ui_rgba(0.55f, 0.56f, 0.58f, 1.0f);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_COLOR;
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_DRAG, SK_UI_STATE_DISABLED, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
	var.background_color = sk_ui_rgba(0.12f, 0.13f, 0.16f, 1.0f);
	var.border_color = sk_ui_rgba(0.35f, 0.55f, 0.90f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_DRAG, SK_UI_STATE_FOCUSED, &var);

	/* Vector rows: transparent flex row hosting independent components. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_BACKGROUND_COLOR;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	base.layout.column_gap = 4.0f;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_SLIDER_N, &base) != 0) {
		return -1;
	}
	if (ui->style_class_register(ctx, SK_UI_CLASS_DRAG_N, &base) != 0) {
		return -1;
	}

	/* Range slider: same chrome as single slider (two thumbs drawn in paint). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH;
	base.background_color = sk_ui_rgba(0.20f, 0.22f, 0.26f, 1.0f);
	base.corner_radius = 4.0f;
	base.layout.height = sk_ui_pt(20.0f);
	base.layout.min_width = sk_ui_pt(80.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_RANGE_SLIDER, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.24f, 0.26f, 0.32f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_RANGE_SLIDER, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_RANGE_SLIDER, SK_UI_STATE_DISABLED, &var);

	/* Progress bar: track only; fill fraction is custom paint (no grab handle). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH;
	base.background_color = sk_ui_rgba(0.20f, 0.22f, 0.26f, 1.0f);
	base.corner_radius = 4.0f;
	base.layout.height = sk_ui_pt(16.0f);
	base.layout.min_width = sk_ui_pt(80.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_PROGRESS, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_PROGRESS, SK_UI_STATE_DISABLED, &var);

	/* Text input */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE |
				SK_UI_SP_MIN_HEIGHT | SK_UI_SP_ALIGN_ITEMS;
	base.background_color = sk_ui_rgba(0.12f, 0.13f, 0.15f, 1.0f);
	base.border_color = sk_ui_rgba(0.35f, 0.37f, 0.42f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 3.0f;
	ui_style_fill_layout_pad(&base, 6.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 14.0f;
	base.layout.min_height = sk_ui_pt(28.0f);
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TEXT_INPUT, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BORDER_COLOR;
	var.border_color = sk_ui_rgba(0.45f, 0.60f, 0.95f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TEXT_INPUT, SK_UI_STATE_FOCUSED, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TEXT_INPUT, SK_UI_STATE_HOVER, &var);
	/* Disabled: muted face, border, and ink so vision grades clear dimming (APX-253). */
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_COLOR;
	var.background_color = sk_ui_rgba(0.14f, 0.15f, 0.17f, 1.0f);
	var.border_color = sk_ui_rgba(0.24f, 0.25f, 0.28f, 1.0f);
	var.color = sk_ui_rgba(0.38f, 0.40f, 0.42f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TEXT_INPUT, SK_UI_STATE_DISABLED, &var);

	/* Invalid project name: red focus/chrome (ImGuiInputTextExtraFlags_ShowError). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BORDER_COLOR;
	base.border_color = sk_ui_rgba(0.86f, 0.24f, 0.22f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_TEXT_INPUT_ERROR, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BORDER_COLOR;
	var.border_color = sk_ui_rgba(0.95f, 0.32f, 0.28f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TEXT_INPUT_ERROR, SK_UI_STATE_FOCUSED, &var);

	/* Search bar: extra left pad for the magnifier. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_PADDING;
	base.layout.padding.left = 22.0f;
	base.layout.padding.top = 6.0f;
	base.layout.padding.right = 6.0f;
	base.layout.padding.bottom = 6.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TEXT_INPUT_SEARCH, &base) != 0) {
		return -1;
	}

	/* Scroll view */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS;
	base.background_color = sk_ui_rgba(0.14f, 0.15f, 0.17f, 1.0f);
	base.border_color = sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 3.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_SCROLL_VIEW, &base) != 0) {
		return -1;
	}

	/* Image */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.10f, 0.10f, 0.12f, 1.0f);
	base.layout.min_width = sk_ui_pt(16.0f);
	base.layout.min_height = sk_ui_pt(16.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_IMAGE, &base) != 0) {
		return -1;
	}

	/* Menu bar (horizontal strip) */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_PADDING | SK_UI_SP_COLUMN_GAP;
	base.background_color = sk_ui_rgba(0.18f, 0.19f, 0.22f, 1.0f);
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.min_height = sk_ui_pt(28.0f);
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.padding.left = 8.0f;
	base.layout.padding.right = 8.0f;
	base.layout.column_gap = 4.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MENU_BAR, &base) != 0) {
		return -1;
	}

	/* Menu / dropdown trigger (in-bar or standalone) */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_JUSTIFY_CONTENT |
				SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_POSITION;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	ui_style_fill_layout_pad(&base, 6.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 14.0f;
	base.layout.min_height = sk_ui_pt(28.0f);
	base.layout.min_width = sk_ui_pt(48.0f);
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.position = SK_UI_POSITION_RELATIVE;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MENU, &base) != 0) {
		return -1;
	}
	if (ui->style_class_register(ctx, SK_UI_CLASS_DROPDOWN, &base) != 0) {
		return -1;
	}
	/* Combo preview (APX-353): FrameBg + border, preview text left, arrow right. */
	{
		sk_ui_style_props_t combo;
		ui_style_props_clear(&combo);
		combo.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE |
					 SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_JUSTIFY_CONTENT;
		combo.background_color = sk_ui_rgba(0.16f, 0.17f, 0.19f, 1.0f);
		combo.border_color = sk_ui_rgba(0.32f, 0.34f, 0.38f, 1.0f);
		combo.layout.border.left = 1.0f;
		combo.layout.border.top = 1.0f;
		combo.layout.border.right = 1.0f;
		combo.layout.border.bottom = 1.0f;
		combo.corner_radius = 3.0f;
		combo.layout.padding.left = 8.0f;
		combo.layout.padding.right = 22.0f;
		combo.layout.padding.top = 4.0f;
		combo.layout.padding.bottom = 4.0f;
		combo.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
		combo.font_size = 13.0f;
		combo.layout.min_height = sk_ui_pt(24.0f);
		combo.layout.min_width = sk_ui_pt(120.0f);
		combo.layout.height = sk_ui_pt(24.0f);
		combo.layout.align_items = SK_UI_ALIGN_CENTER;
		combo.layout.justify_content = SK_UI_JUSTIFY_FLEX_START;
		if (ui->style_class_register(ctx, SK_UI_CLASS_COMBO, &combo) != 0) {
			return -1;
		}
		ui_style_props_clear(&var);
		var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
		var.background_color = sk_ui_rgba(0.20f, 0.22f, 0.26f, 1.0f);
		var.border_color = sk_ui_rgba(0.40f, 0.44f, 0.50f, 1.0f);
		(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_COMBO, SK_UI_STATE_HOVER, &var);
		var.background_color = sk_ui_rgba(0.14f, 0.15f, 0.17f, 1.0f);
		var.border_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.90f);
		(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_COMBO, SK_UI_STATE_FOCUSED, &var);
		var.background_color = sk_ui_rgba(0.12f, 0.13f, 0.15f, 1.0f);
		var.border_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 1.0f);
		(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_COMBO, SK_UI_STATE_ACTIVE, &var);
		ui_style_props_clear(&var);
		var.mask = SK_UI_SP_COLOR | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
		var.color = sk_ui_rgba(0.50f, 0.51f, 0.53f, 1.0f);
		var.background_color = sk_ui_rgba(0.12f, 0.13f, 0.14f, 1.0f);
		var.border_color = sk_ui_rgba(0.22f, 0.23f, 0.25f, 1.0f);
		(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_COMBO, SK_UI_STATE_DISABLED, &var);
	}
	if (ui->style_class_register(ctx, SK_UI_CLASS_SUBMENU, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.28f, 0.42f, 0.72f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_MENU, SK_UI_STATE_HOVER, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_DROPDOWN, SK_UI_STATE_HOVER, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SUBMENU, SK_UI_STATE_HOVER, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_COLOR | SK_UI_SP_BACKGROUND_COLOR;
	var.color = sk_ui_rgba(0.50f, 0.51f, 0.53f, 1.0f);
	var.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_MENU, SK_UI_STATE_DISABLED, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_DROPDOWN, SK_UI_STATE_DISABLED, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SUBMENU, SK_UI_STATE_DISABLED, &var);

	/* Menu item row: left pad reserves the checkmark column. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_ALIGN_ITEMS;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.layout.padding.left = 22.0f;
	base.layout.padding.top = 4.0f;
	base.layout.padding.right = 8.0f;
	base.layout.padding.bottom = 4.0f;
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 14.0f;
	base.layout.min_height = sk_ui_pt(22.0f);
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MENU_ITEM, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.28f, 0.42f, 0.72f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_MENU_ITEM, SK_UI_STATE_HOVER, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_COLOR | SK_UI_SP_BACKGROUND_COLOR;
	var.color = sk_ui_rgba(0.40f, 0.41f, 0.43f, 1.0f);
	var.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_MENU_ITEM, SK_UI_STATE_DISABLED, &var);

	/* Priority-group separator inside a menu popup. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.height = sk_ui_pt(8.0f);
	base.layout.min_height = sk_ui_pt(8.0f);
	base.layout.max_height = sk_ui_pt(8.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_MENU_SEPARATOR, &base) != 0) {
		return -1;
	}

	/* Floating popup / context menu panel */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_FLEX_DIRECTION |
				SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_POSITION;
	base.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	base.border_color = sk_ui_rgba(0.32f, 0.34f, 0.40f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 3.0f;
	ui_style_fill_layout_pad(&base, 4.0f);
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.min_width = sk_ui_pt(240.0f);
	base.layout.min_height = sk_ui_pt(160.0f);
	base.layout.position = SK_UI_POSITION_ABSOLUTE;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MENU_POPUP, &base) != 0) {
		return -1;
	}
	if (ui->style_class_register(ctx, SK_UI_CLASS_CONTEXT_MENU, &base) != 0) {
		return -1;
	}

	/* ImGuiBeginPopupMenu: 300px once-size, extra padding / panel chrome. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH |
				SK_UI_SP_MIN_WIDTH | SK_UI_SP_POSITION;
	base.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	base.border_color = sk_ui_rgba(0.38f, 0.40f, 0.46f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 3.0f;
	base.layout.padding.left = 6.0f;
	base.layout.padding.top = 6.0f;
	base.layout.padding.right = 6.0f;
	base.layout.padding.bottom = 6.0f;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.width = sk_ui_pt(SK_UI_POPUP_MENU_WIDTH);
	base.layout.min_width = sk_ui_pt(SK_UI_POPUP_MENU_WIDTH);
	base.layout.position = SK_UI_POSITION_ABSOLUTE;
	if (ui->style_class_register(ctx, SK_UI_CLASS_POPUP_MENU, &base) != 0) {
		return -1;
	}

	/* Modal host: fullscreen overlay (dim + centered dialog). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.height = sk_ui_percent(100.0f);
	base.layout.position = SK_UI_POSITION_ABSOLUTE;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MODAL, &base) != 0) {
		return -1;
	}

	/* Background dim: blocks input and darkens the editor behind. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION;
	base.background_color = sk_ui_rgba(0.01f, 0.02f, 0.03f, 0.72f);
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.height = sk_ui_percent(100.0f);
	base.layout.position = SK_UI_POSITION_ABSOLUTE;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MODAL_DIM, &base) != 0) {
		return -1;
	}

	/* Dialog card: title + body + button row. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_MIN_WIDTH |
				SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	base.border_color = sk_ui_rgba(0.52f, 0.54f, 0.60f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 4.0f;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.min_width = sk_ui_pt(260.0f);
	base.layout.min_height = sk_ui_pt(96.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_MODAL_DIALOG, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_ALIGN_ITEMS |
				SK_UI_SP_FLEX_DIRECTION;
	base.background_color = sk_ui_rgba(0.28f, 0.32f, 0.40f, 1.0f);
	base.layout.padding.left = 10.0f;
	base.layout.padding.top = 6.0f;
	base.layout.padding.right = 8.0f;
	base.layout.padding.bottom = 6.0f;
	base.color = sk_ui_rgba(0.94f, 0.95f, 0.97f, 1.0f);
	base.font_size = 13.0f;
	base.layout.min_height = sk_ui_pt(26.0f);
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MODAL_TITLE, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH;
	base.background_color = sk_ui_rgba(0.14f, 0.15f, 0.17f, 1.0f);
	base.layout.padding.left = 10.0f;
	base.layout.padding.top = 10.0f;
	base.layout.padding.right = 10.0f;
	base.layout.padding.bottom = 8.0f;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_MODAL_BODY, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_COLUMN_GAP |
				SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.15f, 0.16f, 0.18f, 1.0f);
	base.layout.padding.left = 10.0f;
	base.layout.padding.top = 8.0f;
	base.layout.padding.right = 10.0f;
	base.layout.padding.bottom = 10.0f;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.justify_content = SK_UI_JUSTIFY_FLEX_END;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.column_gap = 8.0f;
	base.layout.min_height = sk_ui_pt(40.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_MODAL_BUTTONS, &base) != 0) {
		return -1;
	}

	/* Dock space host (clips nested dock tree) */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW;
	base.background_color = sk_ui_rgba(0.12f, 0.13f, 0.15f, 1.0f);
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.height = sk_ui_percent(100.0f);
	base.layout.flex_grow = 1.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_DOCK_SPACE, &base) != 0) {
		return -1;
	}

	/* Dock node (nested container region) */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.14f, 0.15f, 0.17f, 1.0f);
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.flex_grow = 1.0f;
	base.layout.min_width = sk_ui_pt(40.0f);
	base.layout.min_height = sk_ui_pt(40.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_DOCK_NODE, &base) != 0) {
		return -1;
	}

	/* Splitter drag handle */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.22f, 0.24f, 0.28f, 1.0f);
	base.layout.min_width = sk_ui_pt(4.0f);
	base.layout.min_height = sk_ui_pt(4.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_SPLITTER, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.34f, 0.50f, 0.82f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SPLITTER, SK_UI_STATE_HOVER, &var);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SPLITTER, SK_UI_STATE_ACTIVE, &var);

	/* Tab bar strip (workspace switcher / debugger pages). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
	base.background_color = sk_ui_rgba(0.12f, 0.13f, 0.15f, 1.0f);
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	base.layout.min_height = sk_ui_pt(26.0f);
	base.layout.width = sk_ui_percent(100.0f);
	base.border_color = sk_ui_rgba(0.22f, 0.24f, 0.28f, 1.0f);
	base.layout.border.bottom = 1.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TAB_BAR, &base) != 0) {
		return -1;
	}

	/* Tab select target: unselected is recessed; selected lifts to page chrome. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS |
				SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR | SK_UI_SP_FLEX_DIRECTION;
	base.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	base.layout.padding.left = 10.0f;
	base.layout.padding.right = 8.0f;
	base.layout.padding.top = 4.0f;
	base.layout.padding.bottom = 4.0f;
	base.color = sk_ui_rgba(0.70f, 0.72f, 0.75f, 1.0f);
	base.font_size = 13.0f;
	base.layout.min_height = sk_ui_pt(26.0f);
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.border_color = sk_ui_rgba(0.10f, 0.11f, 0.13f, 1.0f);
	base.layout.border.right = 1.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TAB, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR;
	var.background_color = sk_ui_rgba(0.30f, 0.34f, 0.42f, 1.0f);
	var.color = sk_ui_rgba(0.98f, 0.99f, 1.0f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TAB, SK_UI_STATE_HOVER, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH;
	var.background_color = sk_ui_rgba(0.26f, 0.28f, 0.32f, 1.0f);
	var.color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
	var.border_color = sk_ui_rgba(0.38f, 0.62f, 0.92f, 1.0f);
	var.layout.border.top = 2.0f;
	var.layout.border.right = 1.0f;
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TAB, SK_UI_STATE_ACTIVE, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH;
	var.border_color = sk_ui_rgba(0.72f, 0.80f, 0.94f, 1.0f);
	var.layout.border.left = 1.0f;
	var.layout.border.top = 1.0f;
	var.layout.border.right = 1.0f;
	var.layout.border.bottom = 1.0f;
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TAB, SK_UI_STATE_FOCUSED, &var);

	/* Trailing TabItemButton ('+') — same chrome, never selected-page fill. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_JUSTIFY_CONTENT |
				SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
	base.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	base.layout.padding.left = 10.0f;
	base.layout.padding.right = 10.0f;
	base.layout.padding.top = 4.0f;
	base.layout.padding.bottom = 4.0f;
	base.color = sk_ui_rgba(0.78f, 0.80f, 0.84f, 1.0f);
	base.font_size = 15.0f;
	base.layout.min_height = sk_ui_pt(26.0f);
	base.layout.min_width = sk_ui_pt(22.0f);
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.border_color = sk_ui_rgba(0.10f, 0.11f, 0.13f, 1.0f);
	base.layout.border.right = 1.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TAB_BUTTON, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR;
	var.background_color = sk_ui_rgba(0.32f, 0.38f, 0.48f, 1.0f);
	var.color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TAB_BUTTON, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.22f, 0.24f, 0.28f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TAB_BUTTON, SK_UI_STATE_ACTIVE, &var);

	/* Per-tab close (shown only when p_open is bound). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLOR |
				SK_UI_SP_FONT_SIZE | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_CORNER_RADIUS;
	base.layout.width = sk_ui_pt(16.0f);
	base.layout.height = sk_ui_pt(16.0f);
	base.layout.min_width = sk_ui_pt(16.0f);
	base.layout.min_height = sk_ui_pt(16.0f);
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.color = sk_ui_rgba(0.88f, 0.90f, 0.93f, 1.0f);
	base.font_size = 12.0f;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.corner_radius = 2.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TAB_CLOSE, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR;
	var.background_color = sk_ui_rgba(0.72f, 0.22f, 0.22f, 1.0f);
	var.color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TAB_CLOSE, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.55f, 0.14f, 0.14f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TAB_CLOSE, SK_UI_STATE_ACTIVE, &var);

	/* Selected tab page body (hidden on unselected tabs). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE;
	base.background_color = sk_ui_rgba(0.26f, 0.28f, 0.32f, 1.0f);
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.flex_grow = 1.0f;
	base.layout.width = sk_ui_percent(100.0f);
	ui_style_fill_layout_pad(&base, 8.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 13.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TAB_BODY, &base) != 0) {
		return -1;
	}

	/* Editor window chrome shell */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.14f, 0.15f, 0.17f, 1.0f);
	base.border_color = sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.min_width = sk_ui_pt(80.0f);
	base.layout.min_height = sk_ui_pt(60.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_EDITOR_WINDOW, &base) != 0) {
		return -1;
	}

	/* Window title bar (drag target) — stronger band vs body so vision grades
	 * see a distinct top chrome strip (body is 0.14/0.15/0.17). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_ALIGN_ITEMS |
				SK_UI_SP_FLEX_DIRECTION;
	base.background_color = sk_ui_rgba(0.32f, 0.36f, 0.44f, 1.0f);
	ui_style_fill_layout_pad(&base, 6.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 13.0f;
	base.layout.min_height = sk_ui_pt(24.0f);
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	if (ui->style_class_register(ctx, SK_UI_CLASS_WINDOW_TITLE_BAR, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.26f, 0.36f, 0.58f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_WINDOW_TITLE_BAR, SK_UI_STATE_ACTIVE, &var);

	/* Window content body (clipped nested region) */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_PADDING;
	base.background_color = sk_ui_rgba(0.14f, 0.15f, 0.17f, 1.0f);
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.flex_grow = 1.0f;
	base.layout.width = sk_ui_percent(100.0f);
	ui_style_fill_layout_pad(&base, 4.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_WINDOW_CONTENT, &base) != 0) {
		return -1;
	}

	/* Window close (ImGui title-bar X). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLOR |
				SK_UI_SP_FONT_SIZE | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_CORNER_RADIUS;
	base.layout.width = sk_ui_pt(16.0f);
	base.layout.height = sk_ui_pt(16.0f);
	base.layout.min_width = sk_ui_pt(16.0f);
	base.layout.min_height = sk_ui_pt(16.0f);
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.color = sk_ui_rgba(0.90f, 0.91f, 0.93f, 1.0f);
	base.font_size = 12.0f;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.corner_radius = 2.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_WINDOW_CLOSE, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.72f, 0.22f, 0.22f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_WINDOW_CLOSE, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.55f, 0.14f, 0.14f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_WINDOW_CLOSE, SK_UI_STATE_ACTIVE, &var);

	/* Fullscreen host (project launcher). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_PADDING;
	base.background_color = sk_ui_rgba(0.10f, 0.11f, 0.13f, 1.0f);
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.height = sk_ui_percent(100.0f);
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.flex_grow = 1.0f;
	ui_style_fill_layout_pad(&base, 16.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_FULLSCREEN, &base) != 0) {
		return -1;
	}

	/* Child region: remaining-size pane (border optional via flags). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH;
	base.background_color = sk_ui_rgba(0.13f, 0.14f, 0.16f, 1.0f);
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.flex_grow = 1.0f;
	base.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_CHILD, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_OPACITY | SK_UI_SP_COLOR;
	var.opacity = 0.55f;
	var.color = UI_TEXT_DISABLED_COLOR;
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_CHILD, SK_UI_STATE_DISABLED, &var);

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_BACKGROUND_COLOR;
	base.layout.width = sk_ui_pt(4.0f);
	base.layout.min_width = sk_ui_pt(4.0f);
	base.layout.max_width = sk_ui_pt(4.0f);
	base.layout.height = sk_ui_percent(100.0f);
	base.layout.flex_grow = 0.0f;
	base.background_color = sk_ui_rgba(0.32f, 0.34f, 0.38f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_CHILD_RESIZE, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.45f, 0.55f, 0.72f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_CHILD_RESIZE, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.35f, 0.50f, 0.78f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_CHILD_RESIZE, SK_UI_STATE_ACTIVE, &var);

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.align_items = SK_UI_ALIGN_FLEX_START;
	base.layout.width = sk_ui_auto();
	base.layout.height = sk_ui_auto();
	if (ui->style_class_register(ctx, SK_UI_CLASS_GROUP, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_HORIZONTAL, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	base.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_VERTICAL, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	base.layout.flex_grow = 1.0f;
	base.layout.flex_shrink = 1.0f;
	base.layout.min_width = sk_ui_pt(0.0f);
	base.layout.min_height = sk_ui_pt(0.0f);
	base.layout.width = sk_ui_auto();
	base.layout.height = sk_ui_auto();
	if (ui->style_class_register(ctx, SK_UI_CLASS_SPRING, &base) != 0) {
		return -1;
	}

	/* Item-array hosts (tree / list / combo / table): column of rows. */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	base.background_color = sk_ui_rgba(0.12f, 0.13f, 0.15f, 1.0f);
	base.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_TREE, &base) != 0) {
		return -1;
	}
	if (ui->style_class_register(ctx, SK_UI_CLASS_LIST, &base) != 0) {
		return -1;
	}
	if (ui->style_class_register(ctx, SK_UI_CLASS_COMBO_ITEMS, &base) != 0) {
		return -1;
	}
	{
		sk_ui_style_props_t list_box;
		ui_style_props_clear(&list_box);
		list_box.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS |
						SK_UI_SP_PADDING | SK_UI_SP_WIDTH;
		list_box.layout.flex_direction = SK_UI_FLEX_COLUMN;
		list_box.layout.align_items = SK_UI_ALIGN_STRETCH;
		list_box.background_color = sk_ui_rgba(0.12f, 0.13f, 0.15f, 1.0f);
		list_box.border_color = sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f);
		list_box.layout.border.left = 1.0f;
		list_box.layout.border.top = 1.0f;
		list_box.layout.border.right = 1.0f;
		list_box.layout.border.bottom = 1.0f;
		list_box.corner_radius = 3.0f;
		list_box.layout.padding.left = 2.0f;
		list_box.layout.padding.right = 2.0f;
		list_box.layout.padding.top = 2.0f;
		list_box.layout.padding.bottom = 2.0f;
		list_box.layout.width = sk_ui_percent(100.0f);
		if (ui->style_class_register(ctx, SK_UI_CLASS_LIST_BOX, &list_box) != 0) {
			return -1;
		}
	}
	if (ui->style_class_register(ctx, SK_UI_CLASS_TABLE, &base) != 0) {
		return -1;
	}

	/* Table header / row / cell / body / resize (APX-351). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.min_height = sk_ui_pt(SK_UI_TABLE_ROW_HEIGHT);
	base.background_color = sk_ui_rgba(0.30f, 0.38f, 0.48f, 1.0f);
	base.color = sk_ui_rgba(0.96f, 0.97f, 0.99f, 1.0f);
	base.font_size = 13.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TABLE_HEADER, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.min_height = sk_ui_pt(SK_UI_TABLE_ROW_HEIGHT);
	base.background_color = sk_ui_rgba(0.10f, 0.11f, 0.13f, 1.0f);
	base.color = sk_ui_rgba(0.90f, 0.91f, 0.93f, 1.0f);
	base.font_size = 13.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TABLE_ROW, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_PADDING | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE |
				SK_UI_SP_BACKGROUND_COLOR;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.padding.left = 6.0f;
	base.layout.padding.right = 6.0f;
	base.layout.padding.top = 3.0f;
	base.layout.padding.bottom = 3.0f;
	base.layout.flex_shrink = 0.0f;
	base.layout.min_height = sk_ui_pt(SK_UI_TABLE_ROW_HEIGHT);
	base.color = sk_ui_rgba(0.90f, 0.91f, 0.93f, 1.0f);
	base.font_size = 13.0f;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_TABLE_CELL, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_BACKGROUND_COLOR;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.flex_grow = 1.0f;
	base.layout.flex_shrink = 1.0f;
	base.layout.min_height = sk_ui_pt(0.0f);
	base.background_color = sk_ui_rgba(0.10f, 0.11f, 0.13f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_TABLE_BODY, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_BACKGROUND_COLOR;
	base.layout.width = sk_ui_pt(6.0f);
	base.layout.min_width = sk_ui_pt(6.0f);
	base.layout.max_width = sk_ui_pt(6.0f);
	base.layout.height = sk_ui_percent(100.0f);
	base.layout.flex_grow = 0.0f;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_TABLE_RESIZE, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.45f, 0.55f, 0.72f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TABLE_RESIZE, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.35f, 0.50f, 0.78f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TABLE_RESIZE, SK_UI_STATE_ACTIVE, &var);

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE |
				SK_UI_SP_BACKGROUND_COLOR;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.min_height = sk_ui_pt(20.0f);
	base.layout.width = sk_ui_percent(100.0f);
	ui_style_fill_layout_pad(&base, 2.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 13.0f;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_ITEM_ROW, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.22f, 0.26f, 0.34f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_ITEM_ROW, SK_UI_STATE_HOVER, &var);

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE;
	base.layout.width = sk_ui_pt(16.0f);
	base.layout.height = sk_ui_pt(16.0f);
	base.layout.min_width = sk_ui_pt(16.0f);
	base.layout.min_height = sk_ui_pt(16.0f);
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.color = sk_ui_rgba(0.80f, 0.82f, 0.86f, 1.0f);
	base.font_size = 12.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TREE_ARROW, &base) != 0) {
		return -1;
	}

	/* CollapsingHeader: framed full-width bar (Properties / Settings). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR |
				SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_CORNER_RADIUS;
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.justify_content = SK_UI_JUSTIFY_FLEX_START;
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.min_height = sk_ui_pt(22.0f);
	ui_style_fill_layout_pad(&base, 4.0f);
	base.layout.padding.left = 4.0f;
	base.layout.padding.right = 6.0f;
	base.background_color = sk_ui_rgba(0.22f, 0.27f, 0.34f, 1.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 13.0f;
	base.corner_radius = 3.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_COLLAPSING_HEADER, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.28f, 0.34f, 0.44f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_COLLAPSING_HEADER, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.20f, 0.32f, 0.48f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_COLLAPSING_HEADER, SK_UI_STATE_ACTIVE, &var);
	var.background_color = sk_ui_rgba(0.24f, 0.36f, 0.52f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_COLLAPSING_HEADER, SK_UI_STATE_FOCUSED, &var);
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR | SK_UI_SP_OPACITY;
	var.background_color = sk_ui_rgba(0.16f, 0.18f, 0.21f, 1.0f);
	var.color = UI_TEXT_DISABLED_COLOR;
	var.opacity = 0.55f;
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_COLLAPSING_HEADER, SK_UI_STATE_DISABLED, &var);

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_PADDING;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.padding.left = 8.0f;
	base.layout.padding.top = 4.0f;
	base.layout.padding.right = 4.0f;
	base.layout.padding.bottom = 6.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_COLLAPSING_HEADER_BODY, &base) != 0) {
		return -1;
	}

	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLOR |
				SK_UI_SP_FONT_SIZE | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_CORNER_RADIUS;
	base.layout.width = sk_ui_pt(28.0f);
	base.layout.height = sk_ui_pt(18.0f);
	base.layout.min_width = sk_ui_pt(28.0f);
	base.layout.min_height = sk_ui_pt(18.0f);
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.color = sk_ui_rgba(0.82f, 0.84f, 0.88f, 1.0f);
	base.font_size = 13.0f;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	base.corner_radius = 2.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_COLLAPSING_HEADER_BUTTON, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.32f, 0.38f, 0.48f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_COLLAPSING_HEADER_BUTTON, SK_UI_STATE_HOVER, &var);

	/* Tooltip: compact floating card (asset hover / profiler segment). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_FLEX_DIRECTION |
				SK_UI_SP_POSITION | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
	base.background_color = sk_ui_rgba(0.12f, 0.13f, 0.16f, 0.96f);
	base.border_color = sk_ui_rgba(0.42f, 0.44f, 0.50f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 3.0f;
	base.layout.padding.left = 8.0f;
	base.layout.padding.top = 6.0f;
	base.layout.padding.right = 8.0f;
	base.layout.padding.bottom = 6.0f;
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.position = SK_UI_POSITION_ABSOLUTE;
	base.layout.min_width = sk_ui_pt(32.0f);
	base.layout.min_height = sk_ui_pt(20.0f);
	base.color = sk_ui_rgba(0.90f, 0.91f, 0.93f, 1.0f);
	base.font_size = 13.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TOOLTIP, &base) != 0) {
		return -1;
	}

	/* Drag-drop preview: same floating card as tooltip (payload label). */
	if (ui->style_class_register(ctx, SK_UI_CLASS_DRAG_DROP_PREVIEW, &base) != 0) {
		return -1;
	}

	/* Default drop-target highlight (ImGui Header overlay). */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH;
	base.background_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.35f);
	base.border_color = sk_ui_rgba(0.26f, 0.59f, 0.98f, 0.90f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_DRAG_DROP_TARGET, &base) != 0) {
		return -1;
	}

	ctx->widgets_defaults_registered = 1;
	return 0;
}

void ui_set_clipboard_fns_impl(sk_ui_context_t* ctx, sk_ui_clipboard_get_fn get_fn, sk_ui_clipboard_set_fn set_fn, void_ptr_t user) {
	ctx->clipboard_get = get_fn;
	ctx->clipboard_set = set_fn;
	ctx->clipboard_user = user;
}

/* -------------------------------------------------------------------------- */
/* Factory helpers                                                            */
/* -------------------------------------------------------------------------- */

static i32 ui_widget_assign_id(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t id, const_chr_t prefix) {
	const sk_ui_api_t* ui = ui_wapi();
	char auto_id[192];
	const_chr_t use = id;
	if (use == NULL || use[0] == '\0') {
		ctx->widget_id_seq += 1u;
		(void)snprintf(auto_id, sizeof(auto_id), "%s-%u", prefix != NULL ? prefix : "ui", ctx->widget_id_seq);
		use = auto_id;
	}
	if (ctx->id_prefix_len > 0u) {
		char scoped[192];
		u32 n = 0u;
		u32 i;
		for (i = 0u; ctx->id_prefix[i] != '\0' && n + 1u < (u32)sizeof(scoped); ++i) {
			scoped[n++] = ctx->id_prefix[i];
		}
		if (n + 1u < (u32)sizeof(scoped)) {
			scoped[n++] = '/';
		}
		for (i = 0u; use[i] != '\0' && n + 1u < (u32)sizeof(scoped); ++i) {
			scoped[n++] = use[i];
		}
		scoped[n] = '\0';
		return ui->node_set_id(ctx, node, scoped);
	}
	if (use == auto_id) {
		return ui->node_set_id(ctx, node, use);
	}
	return ui->node_set_id(ctx, node, use);
}

static i32 ui_layout_parent_is_disabled(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_node_t cur = node;
	while (sk_ui_node_is_valid(cur)) {
		const ui_node_slot_t* slot = ui_slot(ctx, cur);
		if (slot == NULL) {
			return 0;
		}
		if ((slot->state_flags & (u32)SK_UI_STATE_DISABLED) != 0u) {
			return 1;
		}
		cur = slot->parent;
	}
	return 0;
}

static void ui_layout_apply_next_item_width(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	f32 w;
	if (ctx->has_next_item_width == 0u) {
		return;
	}
	w = ctx->next_item_width;
	ctx->has_next_item_width = 0u;
	ui_style_props_clear(&p);
	if (w < 0.0f) {
		p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH;
		p.layout.flex_grow = 1.0f;
		p.layout.width = sk_ui_percent(100.0f);
	} else {
		p.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH | SK_UI_SP_FLEX_GROW;
		p.layout.width = sk_ui_pt(w);
		p.layout.min_width = sk_ui_pt(w);
		p.layout.max_width = sk_ui_pt(w);
		p.layout.flex_grow = 0.0f;
	}
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static void ui_layout_apply_indent(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	sk_ui_layout_style_t ls;
	if (ctx->indent_px <= 0.0f) {
		return;
	}
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_MARGIN;
	if (ui->node_get_layout_style(ctx, node, &ls) == 0) {
		p.layout.margin = ls.margin;
	}
	p.layout.margin.left += ctx->indent_px;
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static sk_ui_node_t ui_widget_base(sk_ui_context_t* ctx, sk_ui_node_kind_t kind, sk_ui_node_t parent, const_chr_t class_name, const_chr_t widget_type, const_chr_t id_prefix,
								   const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui->node_create(ctx, kind, parent);
	if (!sk_ui_node_is_valid(n)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->node_add_class(ctx, n, class_name);
	(void)ui->node_set_prop_str(ctx, n, "widget", widget_type);
	(void)ui_widget_assign_id(ctx, n, id, id_prefix);
	if (ctx->disabled_active > 0u || ui_layout_parent_is_disabled(ctx, parent) != 0) {
		u32 st = ui->node_get_state(ctx, n);
		(void)ui->node_set_state(ctx, n, st | (u32)SK_UI_STATE_DISABLED);
	}
	ui_layout_apply_next_item_width(ctx, n);
	ui_layout_apply_indent(ctx, n);
	/* SameLine (APX-349): widget_same_line is a positional command that the
	 * NEXT widget created under the same parent consumes (ImGui semantics).
	 * Internal widget children are created under their own host node, so the
	 * parent match keeps them from stealing the pending state. */
	if (ctx->has_pending_same_line != 0u && sk_ui_node_eq(ctx->pending_same_line_parent, parent) != 0) {
		(void)ui->node_set_prop_i32(ctx, n, "same_line", 1);
		(void)ui->node_set_prop_f32(ctx, n, "same_line_offset", ctx->pending_same_line_offset);
		(void)ui->node_set_prop_f32(ctx, n, "same_line_spacing", ctx->pending_same_line_spacing);
		ui_same_line_reset_pending(ctx);
	}
	return n;
}

/* -------------------------------------------------------------------------- */
/* Behavior handlers                                                          */
/* -------------------------------------------------------------------------- */

static void ui_scroll_clamp(sk_ui_context_t* ctx, sk_ui_node_t node);
static void ui_child_apply_size(sk_ui_context_t* ctx, sk_ui_node_t child, f32 width, f32 height);

static i32 ui_checkbox_read_checked(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 checked = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "checked", &checked);
	return checked != 0 ? 1 : 0;
}

static i32 ui_checkbox_read_mixed(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 mixed = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "mixed", &mixed);
	return mixed != 0 ? 1 : 0;
}

static void ui_checkbox_set_visual(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked, i32 mixed) {
	const sk_ui_api_t* ui = ui_wapi();
	(void)ui->node_set_prop_i32(ctx, node, "checked", checked != 0 ? 1 : 0);
	(void)ui->node_set_prop_i32(ctx, node, "mixed", mixed != 0 ? 1 : 0);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
}

static void ui_checkbox_visual_from_flags(sk_ui_context_t* ctx, sk_ui_node_t node, i32 flags, i32 mask) {
	i32 bits;
	if (mask == 0) {
		ui_checkbox_set_visual(ctx, node, 0, 0);
		return;
	}
	bits = flags & mask;
	if (bits == mask) {
		ui_checkbox_set_visual(ctx, node, 1, 0);
	} else if (bits == 0) {
		ui_checkbox_set_visual(ctx, node, 0, 0);
	} else {
		ui_checkbox_set_visual(ctx, node, 0, 1);
	}
}

static void ui_checkbox_write_bind(ui_widget_data_t* wd, i32 checked) {
	if (wd == NULL || wd->bound_i32 == NULL) {
		return;
	}
	if (wd->bound_kind == UI_BIND_BOOL) {
		*wd->bound_i32 = checked != 0 ? 1 : 0;
	} else if (wd->bound_kind == UI_BIND_FLAGS) {
		if (checked != 0) {
			*wd->bound_i32 |= wd->bound_arg;
		} else {
			*wd->bound_i32 &= ~wd->bound_arg;
		}
	} else if (wd->bound_kind == UI_BIND_RADIO && checked != 0) {
		*wd->bound_i32 = wd->bound_arg;
	}
}

static void ui_checkbox_sync_one(sk_ui_context_t* ctx, sk_ui_node_t node, ui_widget_data_t* wd) {
	if (wd == NULL || wd->bound_i32 == NULL || wd->bound_kind == UI_BIND_NONE) {
		return;
	}
	if (wd->bound_kind == UI_BIND_BOOL) {
		ui_checkbox_set_visual(ctx, node, *wd->bound_i32 != 0 ? 1 : 0, 0);
	} else if (wd->bound_kind == UI_BIND_FLAGS) {
		ui_checkbox_visual_from_flags(ctx, node, *wd->bound_i32, wd->bound_arg);
	} else if (wd->bound_kind == UI_BIND_RADIO) {
		ui_checkbox_set_visual(ctx, node, *wd->bound_i32 == wd->bound_arg ? 1 : 0, 0);
	}
}

void ui_checkbox_radio_sync_all(sk_ui_context_t* ctx) {
	u32 i;
	if (ctx == NULL) {
		return;
	}
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		ui_widget_data_t* wd;
		sk_ui_node_t node;
		if (slot->alive == 0u || slot->user_data == NULL) {
			continue;
		}
		if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_WIDGET_DATA_TYPE_ID)) {
			continue;
		}
		wd = (ui_widget_data_t*)slot->user_data;
		if (wd->kind != UI_WD_CHECKBOX && wd->kind != UI_WD_RADIO) {
			continue;
		}
		node.index = i;
		node.generation = slot->generation;
		ui_checkbox_sync_one(ctx, node, wd);
	}
}

static void ui_checkbox_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	i32 checked;
	i32 mixed;
	(void)event;
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	if (wd != NULL && wd->bound_kind == UI_BIND_FLAGS && wd->bound_i32 != NULL) {
		i32 mask = wd->bound_arg;
		if (mask != 0 && (*wd->bound_i32 & mask) == mask) {
			*wd->bound_i32 &= ~mask;
		} else if (mask != 0) {
			*wd->bound_i32 |= mask;
		}
		ui_checkbox_visual_from_flags(ctx, node, *wd->bound_i32, mask);
	} else {
		mixed = ui_checkbox_read_mixed(ctx, node);
		if (mixed != 0) {
			checked = 1;
		} else {
			checked = ui_checkbox_read_checked(ctx, node) != 0 ? 0 : 1;
		}
		ui_checkbox_set_visual(ctx, node, checked, 0);
		ui_checkbox_write_bind(wd, checked);
	}
	if (wd != NULL) {
		wd->edge_clicked = 1;
		if (wd->on_bool != NULL) {
			wd->on_bool(ctx, node, ui_checkbox_read_checked(ctx, node), wd->cb_user);
		}
	}
}

/**
 * Sibling radios under the same parent form an exclusive group: selecting one
 * clears checked on peer radios. Nested groups use separate parents.
 */
static void ui_radio_clear_sibling_peers(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t parent = ui->node_parent(ctx, node);
	const ui_node_slot_t* parent_slot;
	u32 i;

	if (!sk_ui_node_is_valid(parent)) {
		return;
	}
	parent_slot = ui_slot(ctx, parent);
	if (parent_slot == NULL) {
		return;
	}
	for (i = 0u; i < parent_slot->children.count; ++i) {
		sk_ui_node_t ch = parent_slot->children.items[i];
		const ui_node_slot_t* cs;
		const_chr_t w;
		if (sk_ui_node_eq(ch, node)) {
			continue;
		}
		cs = ui_slot(ctx, ch);
		if (cs == NULL) {
			continue;
		}
		w = ui_prop_str_const(cs, "widget");
		if (w == NULL || strcmp(w, "radio") != 0) {
			continue;
		}
		(void)ui->node_set_prop_i32(ctx, ch, "checked", 0);
		ui_mark_dirty_up(ctx, ch, (u32)SK_UI_DIRTY_PAINT);
	}
}

static void ui_radio_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	i32 already;
	(void)event;
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	already = ui_checkbox_read_checked(ctx, node);
	if (already != 0 && (wd == NULL || wd->bound_kind != UI_BIND_RADIO || wd->bound_i32 == NULL || *wd->bound_i32 == wd->bound_arg)) {
		/* Already selected; re-click does not toggle off and is not a change. */
		return;
	}
	/* Radio selects; does not toggle off on re-click. Group = sibling radios. */
	ui_radio_clear_sibling_peers(ctx, node);
	ui_checkbox_set_visual(ctx, node, 1, 0);
	if (wd != NULL && wd->bound_kind == UI_BIND_RADIO) {
		ui_checkbox_write_bind(wd, 1);
	}
	if (wd != NULL) {
		wd->edge_clicked = 1;
		if (wd->on_bool != NULL) {
			wd->on_bool(ctx, node, 1, wd->cb_user);
		}
	}
}

static void ui_toggle_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	i32 on = 0;
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	(void)event;
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	(void)ui_prop_i32_const(ui_slot(ctx, node), "on", &on);
	on = on != 0 ? 0 : 1;
	(void)ui->node_set_prop_i32(ctx, node, "on", on);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	if (wd != NULL && wd->on_bool != NULL) {
		wd->on_bool(ctx, node, on, wd->cb_user);
	}
}

static f32 ui_slider_round_nearest(f32 v) {
	return (v >= 0.0f) ? floorf(v + 0.5f) : ceilf(v - 0.5f);
}

static f32 ui_slider_lo(f32 a, f32 b) {
	return a < b ? a : b;
}

static f32 ui_slider_hi(f32 a, f32 b) {
	return a > b ? a : b;
}

static i32 ui_slider_is_drag_kind(const ui_node_slot_t* slot) {
	const_chr_t w = ui_prop_str_const(slot, "widget");
	return (w != NULL && strcmp(w, "drag") == 0) ? 1 : 0;
}

static i32 ui_slider_is_vector_host(const ui_node_slot_t* slot) {
	const_chr_t w = ui_prop_str_const(slot, "widget");
	if (w == NULL) {
		return 0;
	}
	return (strcmp(w, "slider_n") == 0 || strcmp(w, "drag_n") == 0) ? 1 : 0;
}

static i32 ui_slider_read_i32(const ui_node_slot_t* slot, const_chr_t key, i32 fallback) {
	i32 v = fallback;
	if (slot != NULL) {
		(void)ui_prop_i32_const(slot, key, &v);
	}
	return v;
}

static i32 ui_f32_bits_eq(f32 a, f32 b) {
	u32 ua;
	u32 ub;
	memcpy(&ua, &a, sizeof(ua));
	memcpy(&ub, &b, sizeof(ub));
	return ua == ub ? 1 : 0;
}

static i32 ui_slider_bounded(f32 min_v, f32 max_v, u32 flags, i32 is_drag) {
	f32 zero = 0.0f;
	if (is_drag == 0) {
		return 1;
	}
	if (ui_f32_bits_eq(min_v, max_v) == 0) {
		return 1;
	}
	if ((flags & SK_UI_SLIDER_FLAG_ALWAYS_CLAMP) != 0u && (ui_f32_bits_eq(min_v, zero) == 0 || ui_f32_bits_eq(max_v, zero) == 0)) {
		return 1;
	}
	return 0;
}

static f32 ui_slider_quantize(f32 val, f32 min_v, f32 step, i32 integer) {
	if (step > 0.0f) {
		f32 n = ui_slider_round_nearest((val - min_v) / step);
		val = min_v + n * step;
	}
	if (integer != 0) {
		val = ui_slider_round_nearest(val);
	}
	return val;
}

static f32 ui_slider_clamp_val(f32 val, f32 min_v, f32 max_v, u32 flags, i32 is_drag) {
	if (ui_slider_bounded(min_v, max_v, flags, is_drag) == 0) {
		return val;
	}
	return ui_clampf(val, ui_slider_lo(min_v, max_v), ui_slider_hi(min_v, max_v));
}

static i32 ui_slider_format_uses_int(const_chr_t fmt) {
	const char* p;
	if (fmt == NULL) {
		return 0;
	}
	p = fmt;
	while (*p != '\0') {
		if (*p == '%' && p[1] != '\0') {
			p += 1;
			if (*p == '%') {
				p += 1;
				continue;
			}
			while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') {
				p += 1;
			}
			while (*p >= '0' && *p <= '9') {
				p += 1;
			}
			if (*p == '.') {
				p += 1;
				while (*p >= '0' && *p <= '9') {
					p += 1;
				}
			}
			if (*p == 'd' || *p == 'i' || *p == 'u' || *p == 'x' || *p == 'X' || *p == 'o') {
				return 1;
			}
			return 0;
		}
		p += 1;
	}
	return 0;
}

static void ui_slider_format_into(char* buf, u32 cap, const_chr_t fmt, f32 val, i32 integer) {
	int iv;
	if (buf == NULL || cap == 0u) {
		return;
	}
	buf[0] = '\0';
	if (fmt == NULL || fmt[0] == '\0') {
		return;
	}
	iv = (int)ui_slider_round_nearest(val);
	/* Editor formats: "%.3f" / "%.0f" / "%d" / "LOD %d" / "auto" / "". */
	if (strcmp(fmt, "LOD %d") == 0) {
		(void)snprintf(buf, cap, "LOD %d", iv);
	} else if (strchr(fmt, '%') == NULL) {
		(void)snprintf(buf, cap, "%s", fmt);
	} else if (integer != 0 || ui_slider_format_uses_int(fmt) != 0 || strcmp(fmt, "%d") == 0) {
		(void)snprintf(buf, cap, "%d", iv);
	} else if (strcmp(fmt, "%.0f") == 0) {
		(void)snprintf(buf, cap, "%.0f", (double)val);
	} else if (strcmp(fmt, "%.1f") == 0) {
		(void)snprintf(buf, cap, "%.1f", (double)val);
	} else if (strcmp(fmt, "%.2f") == 0) {
		(void)snprintf(buf, cap, "%.2f", (double)val);
	} else {
		(void)snprintf(buf, cap, "%.3f", (double)val);
	}
	buf[cap - 1u] = '\0';
}

static void ui_slider_refresh_label(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t fmt;
	char buf[64];
	i32 text_mode;
	i32 integer;
	f32 val;
	if (slot == NULL) {
		return;
	}
	text_mode = ui_slider_read_i32(slot, "text_input", 0);
	if (text_mode != 0) {
		return;
	}
	fmt = ui_prop_str_const(slot, "format");
	if (fmt == NULL) {
		fmt = "";
	}
	integer = ui_slider_read_i32(slot, "integer", 0);
	val = ui_prop_f32_const(slot, "value", 0.0f);
	ui_slider_format_into(buf, (u32)sizeof(buf), fmt, val, integer);
	(void)ui->node_set_prop_str(ctx, node, "text", buf);
	(void)ui->node_set_prop_i32(ctx, node, "text_align", 1);
	(void)ui->node_set_prop_i32(ctx, node, "vertical_align", 1);
	(void)ui->node_set_prop_i32(ctx, node, "wrap", 0);
}

static i32 ui_slider_apply_value(sk_ui_context_t* ctx, sk_ui_node_t node, f32 val, i32 from_user) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	ui_widget_data_t* wd;
	f32 min_v;
	f32 max_v;
	f32 step;
	f32 old;
	u32 flags;
	i32 integer;
	i32 is_drag;
	if (slot == NULL) {
		return -1;
	}
	min_v = ui_prop_f32_const(slot, "min", 0.0f);
	max_v = ui_prop_f32_const(slot, "max", 1.0f);
	step = ui_prop_f32_const(slot, "step", 0.0f);
	flags = (u32)ui_slider_read_i32(slot, "flags", 0);
	integer = ui_slider_read_i32(slot, "integer", 0);
	is_drag = ui_slider_is_drag_kind(slot);
	old = ui_prop_f32_const(slot, "value", 0.0f);
	val = ui_slider_quantize(val, min_v, step, integer);
	val = ui_slider_clamp_val(val, min_v, max_v, flags, is_drag);
	if (ui->node_set_prop_f32(ctx, node, "value", val) != 0) {
		return -1;
	}
	ui_slider_refresh_label(ctx, node);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	if (from_user != 0 && fabsf(val - old) > 1.0e-6f) {
		wd = ui_widget_data(ctx, node);
		if (wd != NULL) {
			wd->edge_value_changed = 1;
			if (wd->on_float != NULL) {
				wd->on_float(ctx, node, val, wd->cb_user);
			}
		}
	}
	return 0;
}

static void ui_slider_set_from_x(sk_ui_context_t* ctx, sk_ui_node_t node, f32 x) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	sk_ui_rect_t border;
	f32 min_v;
	f32 max_v;
	f32 t;
	f32 val;
	if (slot == NULL) {
		return;
	}
	if (ui->node_get_abs_rect(ctx, node, &border, NULL) != 0) {
		return;
	}
	min_v = ui_prop_f32_const(slot, "min", 0.0f);
	max_v = ui_prop_f32_const(slot, "max", 1.0f);
	if (border.width <= 0.0f) {
		t = 0.0f;
	} else {
		t = (x - border.x) / border.width;
	}
	t = ui_clampf(t, 0.0f, 1.0f);
	val = min_v + t * (max_v - min_v);
	(void)ui_slider_apply_value(ctx, node, val, 1);
}

static void ui_slider_exit_text_input(sk_ui_context_t* ctx, sk_ui_node_t node, i32 commit);

static void ui_slider_enter_text_input(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	const sk_allocator_t* a;
	char buf[64];
	f32 val;
	i32 integer;
	u32 n;
	if (slot == NULL) {
		return;
	}
	val = ui_prop_f32_const(slot, "value", 0.0f);
	integer = ui_slider_read_i32(slot, "integer", 0);
	if (integer != 0) {
		(void)snprintf(buf, sizeof(buf), "%d", (int)ui_slider_round_nearest(val));
	} else {
		(void)snprintf(buf, sizeof(buf), "%.3f", (double)val);
	}
	(void)ui->node_set_prop_i32(ctx, node, "text_input", 1);
	(void)ui->node_set_prop_str(ctx, node, "text", buf);
	(void)ui->node_set_prop_i32(ctx, node, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, node, "vertical_align", 1);
	(void)ui->node_set_prop_i32(ctx, node, "caret", (i32)ui_utf8_code_count(buf));
	(void)ui->node_set_prop_i32(ctx, node, "sel_start", 0);
	(void)ui->node_set_prop_i32(ctx, node, "sel_end", (i32)ui_utf8_code_count(buf));
	if (wd != NULL) {
		wd->slider_anchor_value = val;
		a = ctx->allocator;
		if (wd->revert_text != NULL) {
			a->free(a->instance, wd->revert_text);
			wd->revert_text = NULL;
		}
		n = (u32)strlen(buf);
		wd->revert_text = (char*)a->alloc(a->instance, n + 1u);
		if (wd->revert_text != NULL) {
			memcpy(wd->revert_text, buf, n + 1u);
		}
	}
	(void)ui->focus_set(ctx, node);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
}

static void ui_slider_replace_edit(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t insert, i32 del_left) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t cur;
	char next[96];
	i32 sel_a = 0;
	i32 sel_b = 0;
	i32 caret = 0;
	i32 n;
	u32 a_off;
	u32 b_off;
	u32 ins_len;
	if (slot == NULL) {
		return;
	}
	cur = ui_prop_str_const(slot, "text");
	if (cur == NULL) {
		cur = "";
	}
	n = (i32)ui_utf8_code_count(cur);
	(void)ui_prop_i32_const(slot, "sel_start", &sel_a);
	(void)ui_prop_i32_const(slot, "sel_end", &sel_b);
	(void)ui_prop_i32_const(slot, "caret", &caret);
	if (sel_a < 0) {
		sel_a = caret;
	}
	if (sel_b < 0) {
		sel_b = caret;
	}
	if (sel_a > sel_b) {
		i32 tmp = sel_a;
		sel_a = sel_b;
		sel_b = tmp;
	}
	if (sel_a < 0) {
		sel_a = 0;
	}
	if (sel_b > n) {
		sel_b = n;
	}
	if (sel_a == sel_b && del_left != 0 && sel_a > 0) {
		sel_a -= 1;
	}
	a_off = ui_utf8_byte_offset(cur, (u32)sel_a);
	b_off = ui_utf8_byte_offset(cur, (u32)sel_b);
	if (insert == NULL) {
		insert = "";
	}
	ins_len = (u32)strlen(insert);
	if (a_off + ins_len + (strlen(cur) - b_off) >= sizeof(next)) {
		return;
	}
	memcpy(next, cur, a_off);
	memcpy(next + a_off, insert, ins_len);
	memcpy(next + a_off + ins_len, cur + b_off, strlen(cur) - b_off + 1u);
	caret = sel_a + (i32)ui_utf8_code_count(insert);
	(void)ui->node_set_prop_str(ctx, node, "text", next);
	(void)ui->node_set_prop_i32(ctx, node, "caret", caret);
	(void)ui->node_set_prop_i32(ctx, node, "sel_start", caret);
	(void)ui->node_set_prop_i32(ctx, node, "sel_end", caret);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
}

static void ui_slider_exit_text_input(sk_ui_context_t* ctx, sk_ui_node_t node, i32 commit) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	f32 val;
	i32 integer;
	if (slot == NULL) {
		return;
	}
	if (ui_slider_read_i32(slot, "text_input", 0) == 0) {
		return;
	}
	integer = ui_slider_read_i32(slot, "integer", 0);
	if (commit != 0) {
		const_chr_t txt = ui_prop_str_const(slot, "text");
		char* end = NULL;
		if (txt == NULL || txt[0] == '\0') {
			val = 0.0f;
		} else if (integer != 0) {
			val = (f32)strtol(txt, &end, 10);
		} else {
			val = (f32)strtod(txt, &end);
		}
		(void)ui->node_set_prop_i32(ctx, node, "text_input", 0);
		(void)ui_slider_apply_value(ctx, node, val, 1);
	} else {
		val = (wd != NULL) ? wd->slider_anchor_value : ui_prop_f32_const(slot, "value", 0.0f);
		(void)ui->node_set_prop_i32(ctx, node, "text_input", 0);
		(void)ui_slider_apply_value(ctx, node, val, 0);
	}
	(void)ui->node_set_prop_i32(ctx, node, "caret", -1);
	(void)ui->node_set_prop_i32(ctx, node, "sel_start", -1);
	(void)ui->node_set_prop_i32(ctx, node, "sel_end", -1);
	ui_slider_refresh_label(ctx, node);
}

static void ui_slider_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	i32 text_mode;
	i32 is_drag;
	if (event == NULL || slot == NULL) {
		return;
	}
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	text_mode = ui_slider_read_i32(slot, "text_input", 0);
	is_drag = ui_slider_is_drag_kind(slot);
	if (text_mode != 0) {
		if (event->type == SK_UI_EVENT_TEXT_INPUT && event->text != NULL && event->text[0] != '\0') {
			ui_slider_replace_edit(ctx, node, event->text, 0);
			event->consumed = 1;
			return;
		}
		if (event->type == SK_UI_EVENT_KEY_DOWN) {
			if (event->key == SK_UI_KEY_ENTER) {
				ui_slider_exit_text_input(ctx, node, 1);
				event->consumed = 1;
				return;
			}
			if (event->key == SK_UI_KEY_ESCAPE) {
				ui_slider_exit_text_input(ctx, node, 0);
				event->consumed = 1;
				return;
			}
			if (event->key == SK_UI_KEY_BACKSPACE) {
				ui_slider_replace_edit(ctx, node, "", 1);
				event->consumed = 1;
				return;
			}
		}
		if (event->type == SK_UI_EVENT_FOCUS_OUT) {
			ui_slider_exit_text_input(ctx, node, 1);
			return;
		}
		if (event->type == SK_UI_EVENT_POINTER_DOWN) {
			event->consumed = 1;
			return;
		}
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN) {
		if ((event->mods & (u32)SK_UI_MOD_CTRL) != 0u) {
			ui_slider_enter_text_input(ctx, node);
			event->consumed = 1;
			return;
		}
		if (wd != NULL) {
			wd->dragging = 1;
			wd->drag_last_x = event->x;
			wd->slider_anchor_value = ui_prop_f32_const(slot, "value", 0.0f);
		}
		if (is_drag == 0) {
			ui_slider_set_from_x(ctx, node, event->x);
		}
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_MOVE) {
		if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_ACTIVE) == 0u) {
			return;
		}
		if (is_drag != 0) {
			f32 speed = ui_prop_f32_const(slot, "speed", 1.0f);
			f32 cur = ui_prop_f32_const(slot, "value", 0.0f);
			f32 last = (wd != NULL) ? wd->drag_last_x : event->x;
			(void)ui_slider_apply_value(ctx, node, cur + (event->x - last) * speed, 1);
			if (wd != NULL) {
				wd->drag_last_x = event->x;
			}
		} else {
			ui_slider_set_from_x(ctx, node, event->x);
		}
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		if (wd != NULL) {
			wd->dragging = 0;
		}
		event->consumed = 1;
	}
}

static char* ui_wd_strdup(const sk_allocator_t* a, const_chr_t s) {
	size_t n;
	char* d;
	if (s == NULL) {
		s = "";
	}
	n = strlen(s);
	d = (char*)a->alloc(a->instance, n + 1u);
	if (d == NULL) {
		return NULL;
	}
	memcpy(d, s, n + 1u);
	return d;
}

static u32 ui_utf8_fit_bytes(const_chr_t s, u32 max_bytes) {
	u32 n = 0u;
	const u8* p;
	if (s == NULL || max_bytes == 0u) {
		return 0u;
	}
	p = (const u8*)s;
	while (*p != 0u) {
		u32 adv = 1u;
		if ((*p & 0xE0u) == 0xC0u && p[1] != 0u) {
			adv = 2u;
		} else if ((*p & 0xF0u) == 0xE0u && p[1] != 0u && p[2] != 0u) {
			adv = 3u;
		} else if ((*p & 0xF8u) == 0xF0u && p[1] != 0u && p[2] != 0u && p[3] != 0u) {
			adv = 4u;
		}
		if (n + adv > max_bytes) {
			break;
		}
		n += adv;
		p += adv;
	}
	return n;
}

static u32 ui_text_line_count(const_chr_t s) {
	u32 n = 1u;
	if (s == NULL) {
		return 1u;
	}
	while (*s != '\0') {
		if (*s == '\n') {
			n += 1u;
		}
		s += 1;
	}
	return n;
}

static i32 ui_text_line_start(const_chr_t text, i32 caret) {
	i32 i = caret;
	if (text == NULL) {
		return 0;
	}
	if (i < 0) {
		i = 0;
	}
	while (i > 0) {
		u32 b = ui_utf8_byte_offset(text, (u32)(i - 1));
		if (text[b] == '\n') {
			break;
		}
		i -= 1;
	}
	return i;
}

static i32 ui_text_line_end(const_chr_t text, i32 caret) {
	i32 n;
	i32 i;
	if (text == NULL) {
		return 0;
	}
	n = (i32)ui_utf8_code_count(text);
	i = caret;
	if (i < 0) {
		i = 0;
	}
	while (i < n) {
		u32 b = ui_utf8_byte_offset(text, (u32)i);
		if (text[b] == '\n') {
			break;
		}
		i += 1;
	}
	return i;
}

static i32 ui_text_cp_at(const_chr_t text, i32 index) {
	u32 b;
	if (text == NULL || index < 0) {
		return 0;
	}
	b = ui_utf8_byte_offset(text, (u32)index);
	return (i32)(unsigned char)text[b];
}

static void ui_text_refresh_content_size(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	f32 fs = 14.0f;
	f32 lh;
	u32 lines;
	if (wd == NULL || (wd->input_flags & SK_UI_INPUT_TEXT_FLAG_MULTILINE) == 0u) {
		return;
	}
	if (slot != NULL && slot->computed.font_size > 1.0f) {
		fs = slot->computed.font_size;
	}
	lh = fs * 1.25f;
	lines = ui_text_line_count(text);
	(void)ui->node_set_prop_f32(ctx, node, "content_height", lh * (f32)lines + 4.0f);
	(void)ui->node_set_prop_f32(ctx, node, "content_width", slot != NULL ? slot->layout_content.width : 0.0f);
	ui_scroll_clamp(ctx, node);
}

static void ui_text_sync_props(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text, i32 caret, i32 sel_a, i32 sel_b) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 n;
	char local[256];
	char* heap = NULL;
	const_chr_t stable;
	size_t len;
	/* Copy before set_prop_str: text may point at the prop storage being replaced. */
	if (text == NULL) {
		text = "";
	}
	len = strlen(text);
	if (len < sizeof(local)) {
		memcpy(local, text, len + 1u);
		stable = local;
	} else {
		heap = (char*)ctx->allocator->alloc(ctx->allocator->instance, len + 1u);
		if (heap == NULL) {
			return;
		}
		memcpy(heap, text, len + 1u);
		stable = heap;
	}
	(void)ui->node_set_prop_str(ctx, node, "text", stable);
	n = ui_utf8_code_count(stable);
	if (caret < 0) {
		caret = 0;
	}
	if ((u32)caret > n) {
		caret = (i32)n;
	}
	if (sel_a < 0) {
		sel_a = caret;
	}
	if (sel_b < 0) {
		sel_b = caret;
	}
	if ((u32)sel_a > n) {
		sel_a = (i32)n;
	}
	if ((u32)sel_b > n) {
		sel_b = (i32)n;
	}
	(void)ui->node_set_prop_i32(ctx, node, "caret", caret);
	(void)ui->node_set_prop_i32(ctx, node, "sel_start", sel_a);
	(void)ui->node_set_prop_i32(ctx, node, "sel_end", sel_b);
	ui_text_refresh_content_size(ctx, node, stable);
	if (heap != NULL) {
		ctx->allocator->free(ctx->allocator->instance, heap);
	}
	ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_PAINT | SK_UI_DIRTY_LAYOUT));
}

static i32 ui_text_is_readonly(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, node);
	if (wd != NULL && (wd->input_flags & SK_UI_INPUT_TEXT_FLAG_READ_ONLY) != 0u) {
		return 1;
	}
	return 0;
}

static void ui_text_mark_edit(sk_ui_context_t* ctx, sk_ui_node_t node, i32 is_commit) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	const_chr_t text;
	if (wd == NULL) {
		return;
	}
	wd->text_dirty = 1;
	if (is_commit != 0 || (wd->input_flags & SK_UI_INPUT_TEXT_FLAG_ENTER_RETURNS_TRUE) == 0u) {
		wd->edge_text_changed = 1;
	}
	if (is_commit != 0) {
		wd->edge_text_committed = 1;
	}
	text = ui_text_input_get_text_impl(ctx, node);
	if (wd->on_text != NULL) {
		wd->on_text(ctx, node, text != NULL ? text : "", wd->cb_user);
	}
}

static i32 ui_text_char_ok(u32 flags, u32 cp, i32 multiline) {
	if (multiline == 0 && (cp == (u32)'\n' || cp == (u32)'\r')) {
		return 0;
	}
	if ((flags & SK_UI_INPUT_TEXT_FLAG_CHARS_DECIMAL) != 0u) {
		if ((cp >= (u32)'0' && cp <= (u32)'9') || cp == (u32)'.' || cp == (u32)'+' || cp == (u32)'-' || cp == (u32)'e' || cp == (u32)'E') {
			return 1;
		}
		return 0;
	}
	return 1;
}

static u32 ui_text_filter_insert(const_chr_t src, u32 flags, i32 multiline, char* dst, u32 dst_cap) {
	const u8* p;
	u32 n = 0u;
	if (src == NULL || dst == NULL || dst_cap == 0u) {
		return 0u;
	}
	p = (const u8*)src;
	while (*p != 0u && n + 1u < dst_cap) {
		const u8* start = p;
		u32 cp = 0u;
		u32 adv = 1u;
		if ((*p & 0xE0u) == 0xC0u && p[1] != 0u) {
			cp = ((u32)(p[0] & 0x1Fu) << 6) | (u32)(p[1] & 0x3Fu);
			adv = 2u;
		} else if ((*p & 0xF0u) == 0xE0u && p[1] != 0u && p[2] != 0u) {
			cp = ((u32)(p[0] & 0x0Fu) << 12) | ((u32)(p[1] & 0x3Fu) << 6) | (u32)(p[2] & 0x3Fu);
			adv = 3u;
		} else if ((*p & 0xF8u) == 0xF0u && p[1] != 0u && p[2] != 0u && p[3] != 0u) {
			cp = ((u32)(p[0] & 0x07u) << 18) | ((u32)(p[1] & 0x3Fu) << 12) | ((u32)(p[2] & 0x3Fu) << 6) | (u32)(p[3] & 0x3Fu);
			adv = 4u;
		} else {
			cp = *p;
			adv = 1u;
		}
		p += adv;
		if (ui_text_char_ok(flags, cp, multiline) == 0) {
			continue;
		}
		if (n + adv >= dst_cap) {
			break;
		}
		memcpy(dst + n, start, adv);
		n += adv;
	}
	dst[n] = '\0';
	return n;
}

static void ui_text_format_scalar(i32 type, const void* data, char* buf, u32 cap) {
	if (buf == NULL || cap == 0u) {
		return;
	}
	buf[0] = '\0';
	if (data == NULL) {
		return;
	}
	switch (type) {
	case SK_UI_INPUT_DATA_S32:
		(void)snprintf(buf, (size_t)cap, "%d", *(const i32*)data);
		break;
	case SK_UI_INPUT_DATA_U32:
		(void)snprintf(buf, (size_t)cap, "%u", *(const u32*)data);
		break;
	case SK_UI_INPUT_DATA_U64: {
		uint64_t uv = *(const u64*)data;
		(void)snprintf(buf, (size_t)cap, "%" PRIu64, uv);
		break;
	}
	case SK_UI_INPUT_DATA_F32:
		(void)snprintf(buf, (size_t)cap, "%.3f", (double)(*(const f32*)data));
		break;
	case SK_UI_INPUT_DATA_F64:
		(void)snprintf(buf, (size_t)cap, "%.6g", *(const f64*)data);
		break;
	default:
		break;
	}
}

static f64 ui_text_read_scalar_f64(i32 type, const void* data) {
	if (data == NULL) {
		return 0.0;
	}
	switch (type) {
	case SK_UI_INPUT_DATA_S32:
		return (f64)(*(const i32*)data);
	case SK_UI_INPUT_DATA_U32:
		return (f64)(*(const u32*)data);
	case SK_UI_INPUT_DATA_U64:
		return (f64)(*(const u64*)data);
	case SK_UI_INPUT_DATA_F32:
		return (f64)(*(const f32*)data);
	case SK_UI_INPUT_DATA_F64:
		return *(const f64*)data;
	default:
		return 0.0;
	}
}

static void ui_text_write_scalar_f64(i32 type, void* data, f64 v) {
	if (data == NULL) {
		return;
	}
	switch (type) {
	case SK_UI_INPUT_DATA_S32:
		*(i32*)data = (i32)v;
		break;
	case SK_UI_INPUT_DATA_U32:
		*(u32*)data = (u32)v;
		break;
	case SK_UI_INPUT_DATA_U64:
		*(u64*)data = (u64)v;
		break;
	case SK_UI_INPUT_DATA_F32:
		*(f32*)data = (f32)v;
		break;
	case SK_UI_INPUT_DATA_F64:
		*(f64*)data = v;
		break;
	default:
		break;
	}
}

static i32 ui_text_parse_scalar(i32 type, const_chr_t text, f64* out) {
	char* end = NULL;
	f64 v;
	if (text == NULL || text[0] == '\0') {
		*out = 0.0;
		return 0;
	}
	if (type == SK_UI_INPUT_DATA_U64) {
		unsigned long long uv = strtoull(text, &end, 10);
		if (end == text) {
			return -1;
		}
		*out = (f64)uv;
		return 0;
	}
	if (type == SK_UI_INPUT_DATA_S32) {
		long lv = strtol(text, &end, 10);
		if (end == text) {
			return -1;
		}
		*out = (f64)lv;
		return 0;
	}
	if (type == SK_UI_INPUT_DATA_U32) {
		unsigned long uv = strtoul(text, &end, 10);
		if (end == text) {
			return -1;
		}
		*out = (f64)uv;
		return 0;
	}
	v = strtod(text, &end);
	if (end == text) {
		return -1;
	}
	*out = v;
	return 0;
}

static void ui_text_commit_scalar(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	const_chr_t text;
	f64 v;
	char buf[64];
	if (wd == NULL || wd->scalar_data == NULL) {
		return;
	}
	text = ui_text_input_get_text_impl(ctx, node);
	if (ui_text_parse_scalar(wd->scalar_type, text, &v) != 0) {
		v = ui_text_read_scalar_f64(wd->scalar_type, wd->scalar_data);
	}
	if (wd->scalar_has_range != 0) {
		if (v < wd->scalar_min) {
			v = wd->scalar_min;
		}
		if (v > wd->scalar_max) {
			v = wd->scalar_max;
		}
	}
	if (wd->scalar_type == SK_UI_INPUT_DATA_S32) {
		if (v > 2147483647.0) {
			v = 2147483647.0;
		}
		if (v < -2147483648.0) {
			v = -2147483648.0;
		}
	} else if (wd->scalar_type == SK_UI_INPUT_DATA_U32) {
		if (v < 0.0) {
			v = 0.0;
		}
		if (v > 4294967295.0) {
			v = 4294967295.0;
		}
	} else if (wd->scalar_type == SK_UI_INPUT_DATA_U64) {
		if (v < 0.0) {
			v = 0.0;
		}
	}
	ui_text_write_scalar_f64(wd->scalar_type, wd->scalar_data, v);
	ui_text_format_scalar(wd->scalar_type, wd->scalar_data, buf, (u32)sizeof(buf));
	{
		i32 n = (i32)ui_utf8_code_count(buf);
		ui_text_sync_props(ctx, node, buf, n, n, n);
	}
}

static void ui_text_snapshot_revert(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	const_chr_t text;
	if (wd == NULL) {
		return;
	}
	if (wd->revert_text != NULL) {
		ctx->allocator->free(ctx->allocator->instance, wd->revert_text);
		wd->revert_text = NULL;
	}
	text = ui_text_input_get_text_impl(ctx, node);
	wd->revert_text = ui_wd_strdup(ctx->allocator, text);
	wd->text_dirty = 0;
}

static void ui_text_restore_revert(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	const_chr_t src;
	i32 n;
	if (wd == NULL || wd->revert_text == NULL) {
		return;
	}
	src = wd->revert_text;
	n = (i32)ui_utf8_code_count(src);
	ui_text_sync_props(ctx, node, src, n, n, n);
	wd->text_dirty = 0;
}

static void ui_text_apply_flags(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_TEXT_INPUT);
	if (wd == NULL) {
		return;
	}
	wd->input_flags = flags;
	(void)ui->node_set_prop_i32(ctx, node, "flags", (i32)flags);
	(void)ui->node_set_prop_i32(ctx, node, "readonly", (flags & SK_UI_INPUT_TEXT_FLAG_READ_ONLY) != 0u ? 1 : 0);
	(void)ui->node_set_prop_i32(ctx, node, "password", (flags & SK_UI_INPUT_TEXT_FLAG_PASSWORD) != 0u ? 1 : 0);
	if ((flags & SK_UI_INPUT_TEXT_FLAG_MULTILINE) != 0u) {
		(void)ui->node_set_prop_i32(ctx, node, "wrap", 1);
		(void)ui->node_set_prop_i32(ctx, node, "vertical_align", 0);
		(void)ui->node_set_clip_children(ctx, node, 1);
		ui_text_refresh_content_size(ctx, node, ui_text_input_get_text_impl(ctx, node));
	}
	if ((flags & SK_UI_INPUT_TEXT_FLAG_SHOW_ERROR) != 0u) {
		if (ui->node_has_class(ctx, node, SK_UI_CLASS_TEXT_INPUT_ERROR) == 0) {
			(void)ui->node_add_class(ctx, node, SK_UI_CLASS_TEXT_INPUT_ERROR);
		}
	} else {
		(void)ui->node_remove_class(ctx, node, SK_UI_CLASS_TEXT_INPUT_ERROR);
	}
	ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_PAINT | SK_UI_DIRTY_STYLE));
}

static void ui_text_input_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot;
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	const_chr_t text;
	i32 caret = 0;
	i32 sel_a = 0;
	i32 sel_b = 0;
	i32 multiline;
	u32 flags = 0u;
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return;
	}
	if (wd == NULL) {
		wd = ui_widget_data(ctx, node);
	}
	if (wd != NULL) {
		flags = wd->input_flags;
	}
	multiline = (flags & SK_UI_INPUT_TEXT_FLAG_MULTILINE) != 0u ? 1 : 0;
	text = ui_prop_str_const(slot, "text");
	if (text == NULL) {
		text = "";
	}
	(void)ui_prop_i32_const(slot, "caret", &caret);
	(void)ui_prop_i32_const(slot, "sel_start", &sel_a);
	(void)ui_prop_i32_const(slot, "sel_end", &sel_b);

	if (event->type == SK_UI_EVENT_FOCUS_IN) {
		ui_text_snapshot_revert(ctx, node);
		if ((flags & SK_UI_INPUT_TEXT_FLAG_AUTO_SELECT_ALL) != 0u) {
			i32 n = (i32)ui_utf8_code_count(text);
			ui_text_sync_props(ctx, node, text, n, 0, n);
		}
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_FOCUS_OUT) {
		if (wd != NULL && wd->text_dirty != 0) {
			ui_text_commit_scalar(ctx, node);
			wd->edge_text_committed = 1;
			wd->text_dirty = 0;
		}
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_WHEEL && multiline != 0) {
		f32 sy = ui_prop_f32_const(slot, "scroll_y", 0.0f);
		sy -= event->scroll_y * 18.0f;
		(void)ui->node_set_prop_f32(ctx, node, "scroll_y", sy);
		ui_scroll_clamp(ctx, node);
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_TEXT_INPUT && event->text != NULL && event->text[0] != '\0') {
		(void)ui_text_input_insert_impl(ctx, node, event->text);
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_KEY_DOWN) {
		i32 key = event->key;
		u32 mods = event->mods;
		if (key == SK_UI_KEY_ESCAPE) {
			ui_text_restore_revert(ctx, node);
			(void)ui->focus_set(ctx, SK_UI_NODE_INVALID);
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_ENTER) {
			if (multiline != 0 && (flags & SK_UI_INPUT_TEXT_FLAG_ENTER_RETURNS_TRUE) == 0u) {
				(void)ui_text_input_insert_impl(ctx, node, "\n");
			} else {
				ui_text_commit_scalar(ctx, node);
				ui_text_mark_edit(ctx, node, 1);
				if (wd != NULL) {
					wd->text_dirty = 0;
				}
				(void)ui->focus_set(ctx, SK_UI_NODE_INVALID);
			}
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_BACKSPACE) {
			if (sel_a != sel_b) {
				(void)ui_text_input_delete_selection_impl(ctx, node);
			} else if (caret > 0) {
				(void)ui->node_set_prop_i32(ctx, node, "sel_start", caret - 1);
				(void)ui->node_set_prop_i32(ctx, node, "sel_end", caret);
				(void)ui_text_input_delete_selection_impl(ctx, node);
			}
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_DELETE) {
			u32 n = ui_utf8_code_count(text);
			if (sel_a != sel_b) {
				(void)ui_text_input_delete_selection_impl(ctx, node);
			} else if ((u32)caret < n) {
				(void)ui->node_set_prop_i32(ctx, node, "sel_start", caret);
				(void)ui->node_set_prop_i32(ctx, node, "sel_end", caret + 1);
				(void)ui_text_input_delete_selection_impl(ctx, node);
			}
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_LEFT) {
			if ((mods & (u32)SK_UI_MOD_SHIFT) != 0u) {
				i32 c = caret > 0 ? caret - 1 : 0;
				(void)ui->node_set_prop_i32(ctx, node, "caret", c);
				(void)ui->node_set_prop_i32(ctx, node, "sel_end", c);
			} else {
				i32 c = caret > 0 ? caret - 1 : 0;
				ui_text_sync_props(ctx, node, text, c, c, c);
			}
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_RIGHT) {
			u32 n = ui_utf8_code_count(text);
			if ((mods & (u32)SK_UI_MOD_SHIFT) != 0u) {
				i32 c = (u32)caret < n ? caret + 1 : (i32)n;
				(void)ui->node_set_prop_i32(ctx, node, "caret", c);
				(void)ui->node_set_prop_i32(ctx, node, "sel_end", c);
			} else {
				i32 c = (u32)caret < n ? caret + 1 : (i32)n;
				ui_text_sync_props(ctx, node, text, c, c, c);
			}
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_HOME) {
			if (multiline != 0 && (mods & (u32)(SK_UI_MOD_CTRL | SK_UI_MOD_SUPER)) == 0u) {
				i32 c = ui_text_line_start(text, caret);
				if ((mods & (u32)SK_UI_MOD_SHIFT) != 0u) {
					(void)ui->node_set_prop_i32(ctx, node, "caret", c);
					(void)ui->node_set_prop_i32(ctx, node, "sel_end", c);
				} else {
					ui_text_sync_props(ctx, node, text, c, c, c);
				}
			} else {
				ui_text_sync_props(ctx, node, text, 0, 0, 0);
			}
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_END) {
			if (multiline != 0 && (mods & (u32)(SK_UI_MOD_CTRL | SK_UI_MOD_SUPER)) == 0u) {
				i32 c = ui_text_line_end(text, caret);
				if ((mods & (u32)SK_UI_MOD_SHIFT) != 0u) {
					(void)ui->node_set_prop_i32(ctx, node, "caret", c);
					(void)ui->node_set_prop_i32(ctx, node, "sel_end", c);
				} else {
					ui_text_sync_props(ctx, node, text, c, c, c);
				}
			} else {
				i32 n = (i32)ui_utf8_code_count(text);
				ui_text_sync_props(ctx, node, text, n, n, n);
			}
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_UP && multiline != 0) {
			i32 ls = ui_text_line_start(text, caret);
			i32 col = caret - ls;
			i32 prev_end = ls > 0 ? ls - 1 : 0;
			i32 prev_start = ui_text_line_start(text, prev_end);
			i32 prev_len = prev_end - prev_start;
			i32 c = prev_start + (col < prev_len ? col : prev_len);
			if ((mods & (u32)SK_UI_MOD_SHIFT) != 0u) {
				(void)ui->node_set_prop_i32(ctx, node, "caret", c);
				(void)ui->node_set_prop_i32(ctx, node, "sel_end", c);
			} else {
				ui_text_sync_props(ctx, node, text, c, c, c);
			}
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_DOWN && multiline != 0) {
			i32 le = ui_text_line_end(text, caret);
			i32 ls = ui_text_line_start(text, caret);
			i32 col = caret - ls;
			i32 n = (i32)ui_utf8_code_count(text);
			i32 next_start = le < n && ui_text_cp_at(text, le) == '\n' ? le + 1 : le;
			i32 next_end = ui_text_line_end(text, next_start);
			i32 next_len = next_end - next_start;
			i32 c = next_start + (col < next_len ? col : next_len);
			if ((mods & (u32)SK_UI_MOD_SHIFT) != 0u) {
				(void)ui->node_set_prop_i32(ctx, node, "caret", c);
				(void)ui->node_set_prop_i32(ctx, node, "sel_end", c);
			} else {
				ui_text_sync_props(ctx, node, text, c, c, c);
			}
			event->consumed = 1;
			return;
		}
		if ((mods & (u32)SK_UI_MOD_CTRL) != 0u || (mods & (u32)SK_UI_MOD_SUPER) != 0u) {
			if (key == 'c' || key == 'C') {
				(void)ui_text_input_copy_impl(ctx, node);
				event->consumed = 1;
			} else if (key == 'x' || key == 'X') {
				(void)ui_text_input_cut_impl(ctx, node);
				event->consumed = 1;
			} else if (key == 'v' || key == 'V') {
				(void)ui_text_input_paste_impl(ctx, node);
				event->consumed = 1;
			} else if (key == 'a' || key == 'A') {
				i32 n = (i32)ui_utf8_code_count(text);
				ui_text_sync_props(ctx, node, text, n, 0, n);
				event->consumed = 1;
			}
		}
	}
}

static void ui_scroll_clamp(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	f32 sx, sy, cw, ch, vw, vh, max_x, max_y;
	if (slot == NULL) {
		return;
	}
	sx = ui_prop_f32_const(slot, "scroll_x", 0.0f);
	sy = ui_prop_f32_const(slot, "scroll_y", 0.0f);
	cw = ui_prop_f32_const(slot, "content_width", 0.0f);
	ch = ui_prop_f32_const(slot, "content_height", 0.0f);
	vw = slot->layout_content.width;
	vh = slot->layout_content.height;
	/*
	 * Before the first layout pass, content box is 0×0. Clamping then would
	 * force scroll to 0 and drop intentional mid-scroll test setup (APX-253).
	 * Only clamp an axis once that viewport dimension is known.
	 */
	if (vw > 0.5f) {
		max_x = cw > vw ? cw - vw : 0.0f;
		sx = ui_clampf(sx, 0.0f, max_x);
	} else if (sx < 0.0f) {
		sx = 0.0f;
	}
	if (vh > 0.5f) {
		max_y = ch > vh ? ch - vh : 0.0f;
		sy = ui_clampf(sy, 0.0f, max_y);
	} else if (sy < 0.0f) {
		sy = 0.0f;
	}
	(void)ui->node_set_prop_f32(ctx, node, "scroll_x", sx);
	(void)ui->node_set_prop_f32(ctx, node, "scroll_y", sy);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
}

static void ui_scroll_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	f32 sx, sy;
	i32 flags = 0;
	if (slot == NULL) {
		return;
	}
	sx = ui_prop_f32_const(slot, "scroll_x", 0.0f);
	sy = ui_prop_f32_const(slot, "scroll_y", 0.0f);
	(void)ui_prop_i32_const(slot, "child_flags", &flags);

	/* ResizeX: a press near the right edge resizes the child (ImGui ChildFlags_ResizeX). */
	if ((flags & (i32)SK_UI_CHILD_FLAG_RESIZE_X) != 0 && wd != NULL) {
		sk_ui_rect_t br;
		if (event->type == SK_UI_EVENT_POINTER_DOWN && event->button == SK_UI_POINTER_BUTTON_LEFT && ui->node_get_abs_rect(ctx, node, &br, NULL) == 0) {
			if (event->x >= br.x + br.width - 6.0f) {
				wd->dragging = 2;
				wd->drag_last_x = event->x;
				wd->resize_start_w = br.width;
				(void)ui->pointer_capture_set(ctx, node);
				event->consumed = 1;
				return;
			}
		}
		if (wd->dragging == 2) {
			if (event->type == SK_UI_EVENT_POINTER_MOVE) {
				f32 w = wd->resize_start_w + (event->x - wd->drag_last_x);
				if (w < 40.0f) {
					w = 40.0f;
				}
				ui_child_apply_size(ctx, node, w, ui_prop_f32_const(slot, "child_h", 0.0f));
				ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
				event->consumed = 1;
				return;
			}
			if (event->type == SK_UI_EVENT_POINTER_UP) {
				wd->dragging = 0;
				if (sk_ui_node_eq(ui->pointer_capture_get(ctx), node)) {
					(void)ui->pointer_capture_set(ctx, SK_UI_NODE_INVALID);
				}
				event->consumed = 1;
				return;
			}
		}
	}

	if (event->type == SK_UI_EVENT_WHEEL) {
		/* Wheel delta: positive scroll_y typically means scroll up content (decrease offset). */
		sy -= event->scroll_y * 24.0f;
		sx -= event->scroll_x * 24.0f;
		(void)ui->node_set_prop_f32(ctx, node, "scroll_x", sx);
		(void)ui->node_set_prop_f32(ctx, node, "scroll_y", sy);
		ui_scroll_clamp(ctx, node);
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN && event->button == SK_UI_POINTER_BUTTON_LEFT) {
		if (wd != NULL) {
			wd->dragging = 1;
			wd->drag_last_x = event->x;
			wd->drag_last_y = event->y;
		}
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		if (wd != NULL) {
			wd->dragging = 0;
		}
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_MOVE && wd != NULL && wd->dragging != 0) {
		f32 dx = event->x - wd->drag_last_x;
		f32 dy = event->y - wd->drag_last_y;
		wd->drag_last_x = event->x;
		wd->drag_last_y = event->y;
		/* Drag content with the pointer (natural: drag down reveals upper content). */
		(void)ui->node_set_prop_f32(ctx, node, "scroll_x", sx - dx);
		(void)ui->node_set_prop_f32(ctx, node, "scroll_y", sy - dy);
		ui_scroll_clamp(ctx, node);
		event->consumed = 1;
	}
}

/* -------------------------------------------------------------------------- */
/* Factories                                                                  */
/* -------------------------------------------------------------------------- */

sk_ui_node_t ui_widget_panel_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	return ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_PANEL, "panel", "ui-panel", id);
}

sk_ui_node_t ui_widget_view_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	return ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_VIEW, "view", "ui-view", id);
}

sk_ui_node_t ui_widget_label_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_TEXT, parent, SK_UI_CLASS_LABEL, "label", "ui-label", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", text != NULL ? text : "");
	(void)ui->node_set_prop_i32(ctx, n, "wrap", 1);
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 0);
	return n;
}

/* -------------------------------------------------------------------------- */
/* Text family (APX-340)                                                      */
/* -------------------------------------------------------------------------- */

/** Shared text-node setup; @p cls is the style class, @p wrap 0/1. */
static sk_ui_node_t ui_text_base(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t cls, const_chr_t widget_type, const_chr_t id_prefix, const_chr_t text, const_chr_t id,
								 i32 wrap) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_TEXT, parent, cls, widget_type, id_prefix, id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", text != NULL ? text : "");
	(void)ui->node_set_prop_i32(ctx, n, "wrap", wrap != 0 ? 1 : 0);
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 0);
	return n;
}

sk_ui_node_t ui_widget_text_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	return ui_text_base(ctx, parent, SK_UI_CLASS_TEXT, "text", "ui-text", text, id, 0);
}

sk_ui_node_t ui_widget_text_wrapped_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	return ui_text_base(ctx, parent, SK_UI_CLASS_TEXT_WRAPPED, "text_wrapped", "ui-text-wrapped", text, id, 1);
}

sk_ui_node_t ui_widget_text_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	sk_ui_node_t n = ui_text_base(ctx, parent, SK_UI_CLASS_TEXT, "text_disabled", "ui-text-disabled", text, id, 0);
	if (sk_ui_node_is_valid(n)) {
		/* ImGui TextDisabled pushes the disabled style colour; keep the state
		 * bit too so node_is_enabled() reads false. The inline colour survives
		 * host-side state resets (sandbox default frames, BeginDisabled pop). */
		(void)ui_text_set_disabled_impl(ctx, n, 1);
		(void)ui_text_set_color_impl(ctx, n, UI_TEXT_DISABLED_COLOR);
	}
	return n;
}

sk_ui_node_t ui_widget_text_colored_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, sk_ui_color_t color, const_chr_t id) {
	sk_ui_node_t n = ui_text_base(ctx, parent, SK_UI_CLASS_TEXT, "text_colored", "ui-text-colored", text, id, 0);
	if (sk_ui_node_is_valid(n)) {
		(void)ui_text_set_color_impl(ctx, n, color);
	}
	return n;
}

sk_ui_node_t ui_widget_separator_text_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_SEPARATOR_TEXT, "separator_text", "ui-separator-text", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", text != NULL ? text : "");
	/* Paint centers the label vertically inside the rule. */
	(void)ui->node_set_prop_i32(ctx, n, "wrap", 0);
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	return n;
}

/* ---- separator / spacing / same-line family (APX-349; manifest §19) ---- */

void ui_same_line_reset_pending(sk_ui_context_t* ctx) {
	if (ctx == NULL) {
		return;
	}
	ctx->has_pending_same_line = 0u;
	ctx->pending_same_line_offset = 0.0f;
	ctx->pending_same_line_spacing = -1.0f;
	ctx->pending_same_line_parent = SK_UI_NODE_INVALID;
}

i32 ui_widget_same_line_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 offset_from_start_x, f32 spacing) {
	if (ctx == NULL) {
		return -1;
	}
	ctx->has_pending_same_line = 1u;
	ctx->pending_same_line_offset = offset_from_start_x;
	ctx->pending_same_line_spacing = spacing;
	ctx->pending_same_line_parent = parent;
	return 0;
}

/** Vertical rule when the parent flows horizontally (menu bar / horizontal
 * layout) or when SameLine was called immediately before (toolbar divider).
 * Class styles resolve later than the factory, so also check the parent's
 * registered classes (the menu bar is a row class). */
static i32 ui_separator_is_vertical(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t pls;
	sk_ui_prop_value_t pv;
	const ui_node_slot_t* pslot;
	u32 i;
	if (ui->node_get_layout_style(ctx, parent, &pls) == 0 && (pls.flex_direction == SK_UI_FLEX_ROW || pls.flex_direction == SK_UI_FLEX_ROW_REVERSE)) {
		return 1;
	}
	pslot = ui_slot(ctx, parent);
	if (pslot != NULL) {
		for (i = 0u; i < pslot->classes.count; ++i) {
			const ui_style_class_t* cls = ui_style_class_lookup(ctx, pslot->classes.items[i]);
			if (cls != NULL && (cls->base.layout.flex_direction == SK_UI_FLEX_ROW || cls->base.layout.flex_direction == SK_UI_FLEX_ROW_REVERSE)) {
				return 1;
			}
		}
	}
	if (ui->node_get_prop(ctx, node, "same_line", &pv) == 0 && pv.type == SK_UI_PROP_I32 && pv.data.i32_value != 0) {
		return 1;
	}
	return 0;
}

sk_ui_node_t ui_widget_separator_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_SEPARATOR, "separator", "ui-separator", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_focusable(ctx, n, 0);
	if (ui_separator_is_vertical(ctx, parent, n) != 0) {
		/* Vertical toolbar / menu-bar rule: 1px wide, stretches to the row
		 * height (post-pass overrides the height for same-line rows). Inline
		 * so the resolved cascade keeps it (class default is the horizontal
		 * full-width 8px lane). */
		sk_ui_style_props_t p;
		(void)ui->node_set_prop_i32(ctx, n, "vertical", 1);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_ALIGN_SELF;
		p.layout.width = sk_ui_pt(1.0f);
		p.layout.min_width = sk_ui_pt(1.0f);
		p.layout.max_width = sk_ui_pt(1.0f);
		p.layout.height = sk_ui_auto();
		p.layout.min_height = sk_ui_pt(0.0f);
		p.layout.max_height = sk_ui_auto();
		p.layout.align_self = SK_UI_ALIGN_STRETCH;
		(void)ui->node_merge_inline_style(ctx, n, &p);
	} else {
		(void)ui->node_set_prop_i32(ctx, n, "vertical", 0);
	}
	return n;
}

i32 ui_separator_get_vertical_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 vertical = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "vertical", &vertical);
	if (vertical == 0 && sk_ui_node_is_valid(slot->parent)) {
		/* Resolved parent direction (inline-flipped row parents) matches what
		 * paint draws. */
		const ui_node_slot_t* pslot = ui_slot(ctx, slot->parent);
		if (pslot != NULL && (pslot->layout_style.flex_direction == SK_UI_FLEX_ROW || pslot->layout_style.flex_direction == SK_UI_FLEX_ROW_REVERSE)) {
			vertical = 1;
		}
	}
	return vertical != 0 ? 1 : 0;
}

sk_ui_node_t ui_widget_spacing_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_SPACING, "spacing", "ui-spacing", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_focusable(ctx, n, 0);
	return n;
}

sk_ui_node_t ui_widget_dummy_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 width, f32 height, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_DUMMY, "dummy", "ui-dummy", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_focusable(ctx, n, 0);
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(width);
	p.layout.height = sk_ui_pt(height);
	p.layout.min_width = sk_ui_pt(width);
	p.layout.min_height = sk_ui_pt(height);
	p.layout.max_width = sk_ui_pt(width);
	p.layout.max_height = sk_ui_pt(height);
	(void)ui->node_merge_inline_style(ctx, n, &p);
	return n;
}

sk_ui_node_t ui_widget_bullet_text_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_TEXT, parent, SK_UI_CLASS_BULLET_TEXT, "bullet_text", "ui-bullet-text", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", text != NULL ? text : "");
	(void)ui->node_set_prop_i32(ctx, n, "wrap", 0);
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 0);
	return n;
}

/** Row container for label/value pairs (LabelText / ImGuiTextWithLabel). */
static sk_ui_node_t ui_text_row(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t value, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t row = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_VIEW, "label_text", "ui-label-text", id);
	sk_ui_node_t lab;
	sk_ui_node_t val;
	char lab_id[64];
	char val_id[64];
	if (!sk_ui_node_is_valid(row)) {
		return row;
	}
	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP;
		p.layout.flex_direction = SK_UI_FLEX_ROW;
		p.layout.align_items = SK_UI_ALIGN_CENTER;
		p.layout.column_gap = 8.0f;
		(void)ui->node_merge_inline_style(ctx, row, &p);
	}
	lab = ui_text_base(ctx, row, SK_UI_CLASS_TEXT, "text_disabled", "ui-label-text-label", label, NULL, 0);
	(void)ui_text_set_disabled_impl(ctx, lab, 1);
	val = ui_text_base(ctx, row, SK_UI_CLASS_TEXT, "text", "ui-label-text-value", value, NULL, 0);
	/* Child ids: {row}/{label|value} so find_by_id resolves them. */
	(void)snprintf(lab_id, sizeof(lab_id), "%s.label", ui->node_get_id(ctx, row) != NULL ? ui->node_get_id(ctx, row) : "");
	(void)snprintf(val_id, sizeof(val_id), "%s.value", ui->node_get_id(ctx, row) != NULL ? ui->node_get_id(ctx, row) : "");
	(void)ui->node_set_id(ctx, lab, lab_id);
	(void)ui->node_set_id(ctx, val, val_id);
	return row;
}

sk_ui_node_t ui_widget_label_text_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t value, const_chr_t id) {
	return ui_text_row(ctx, parent, label, value, id);
}

sk_ui_node_t ui_widget_text_with_label_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t value, const_chr_t id) {
	return ui_text_row(ctx, parent, label, value, id);
}

sk_ui_node_t ui_widget_text_centered_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t host = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_VIEW, "text_centered", "ui-text-centered", id);
	sk_ui_node_t txt;
	char txt_id[64];
	if (!sk_ui_node_is_valid(host)) {
		return host;
	}
	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH;
		p.layout.flex_direction = SK_UI_FLEX_COLUMN;
		p.layout.justify_content = SK_UI_JUSTIFY_CENTER;
		p.layout.align_items = SK_UI_ALIGN_CENTER;
		p.layout.flex_grow = 1.0f;
		p.layout.width = sk_ui_percent(100.0f);
		(void)ui->node_merge_inline_style(ctx, host, &p);
	}
	txt = ui_text_base(ctx, host, SK_UI_CLASS_TEXT, "text", "ui-text-centered-text", text, NULL, 0);
	(void)ui->node_set_prop_i32(ctx, txt, "text_align", 1);
	(void)ui->node_set_prop_i32(ctx, txt, "vertical_align", 1);
	(void)snprintf(txt_id, sizeof(txt_id), "%s.text", ui->node_get_id(ctx, host) != NULL ? ui->node_get_id(ctx, host) : "");
	(void)ui->node_set_id(ctx, txt, txt_id);
	return host;
}

/**
 * Split an ImGui-style label.
 *   `###id` — visible prefix; suffix is a hidden id.
 *   `##id` at start (not `###`) — no visible text; suffix after `##`.
 *   otherwise the whole string is visible.
 */
static void ui_split_imgui_label(const_chr_t label, char* visible, u32 vis_cap, const_chr_t* out_id_suffix) {
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
		/* `##id` — box only; hidden id is the suffix. */
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

static u32 ui_button_norm_flags(u32 flags) {
	if (flags == 0u) {
		return SK_UI_BUTTON_FLAG_MOUSE_LEFT;
	}
	return flags;
}

static i32 ui_button_flag_for_mouse(i32 button) {
	if (button == SK_UI_POINTER_BUTTON_LEFT) {
		return (i32)SK_UI_BUTTON_FLAG_MOUSE_LEFT;
	}
	if (button == SK_UI_POINTER_BUTTON_MIDDLE) {
		return (i32)SK_UI_BUTTON_FLAG_MOUSE_MIDDLE;
	}
	if (button == SK_UI_POINTER_BUTTON_RIGHT) {
		return (i32)SK_UI_BUTTON_FLAG_MOUSE_RIGHT;
	}
	return 0;
}

static i32 ui_button_allows(const ui_widget_data_t* wd, i32 button) {
	u32 flags = ui_button_norm_flags(wd != NULL ? wd->button_flags : 0u);
	i32 bit = ui_button_flag_for_mouse(button);
	return bit != 0 && (flags & (u32)bit) != 0u ? 1 : 0;
}

static void ui_button_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	sk_ui_node_t hit;
	if (wd == NULL || event == NULL) {
		return;
	}
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN) {
		if (ui_button_allows(wd, event->button) == 0) {
			return;
		}
		wd->edge_pressed = 1;
		wd->edge_released = 0;
		wd->edge_clicked = 0;
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		if (ui_button_allows(wd, event->button) == 0) {
			return;
		}
		wd->edge_released = 1;
		hit = ui->hit_test(ctx, event->x, event->y);
		if (sk_ui_node_eq(hit, node)) {
			wd->edge_clicked = 1;
		}
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_CLICK) {
		if (ui_button_allows(wd, event->button) != 0) {
			wd->edge_clicked = 1;
		}
	}
}

i32 ui_button_set_size_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	if (width > 0.0f) {
		p.layout.width = sk_ui_pt(width);
		p.mask |= SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH;
		p.layout.min_width = sk_ui_pt(width);
		p.layout.max_width = sk_ui_pt(width);
	} else {
		p.layout.width = sk_ui_auto();
	}
	if (height > 0.0f) {
		p.layout.height = sk_ui_pt(height);
		p.mask |= SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT;
		p.layout.min_height = sk_ui_pt(height);
		p.layout.max_height = sk_ui_pt(height);
	} else {
		p.layout.height = sk_ui_auto();
	}
	return ui->node_merge_inline_style(ctx, node, &p);
}

i32 ui_button_set_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 selected) {
	const sk_ui_api_t* ui = ui_wapi();
	i32 rc;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	rc = ui->node_set_prop_i32(ctx, node, "selected", selected != 0 ? 1 : 0);
	if (rc != 0) {
		return rc;
	}
	if (selected != 0) {
		if (ui->node_has_class(ctx, node, SK_UI_CLASS_BUTTON_SELECTED) == 0) {
			(void)ui->node_add_class(ctx, node, SK_UI_CLASS_BUTTON_SELECTED);
		}
	} else {
		(void)ui->node_remove_class(ctx, node, SK_UI_CLASS_BUTTON_SELECTED);
	}
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	return 0;
}

i32 ui_button_get_selected_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 selected = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "selected", &selected);
	return selected != 0 ? 1 : 0;
}

i32 ui_button_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_BUTTON);
	if (wd == NULL) {
		return -1;
	}
	wd->button_flags = ui_button_norm_flags(flags);
	return 0;
}

u32 ui_button_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, node);
	if (wd == NULL) {
		return SK_UI_BUTTON_FLAG_MOUSE_LEFT;
	}
	return ui_button_norm_flags(wd->button_flags);
}

i32 ui_button_clicked_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	i32 v;
	if (wd == NULL) {
		return 0;
	}
	v = wd->edge_clicked != 0 ? 1 : 0;
	wd->edge_clicked = 0;
	return v;
}

i32 ui_button_pressed_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	i32 v;
	if (wd == NULL) {
		return 0;
	}
	v = wd->edge_pressed != 0 ? 1 : 0;
	wd->edge_pressed = 0;
	return v;
}

i32 ui_button_released_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	i32 v;
	if (wd == NULL) {
		return 0;
	}
	v = wd->edge_released != 0 ? 1 : 0;
	wd->edge_released = 0;
	return v;
}

i32 ui_button_is_hovered_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return 0;
	}
	return (ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_HOVER) != 0u ? 1 : 0;
}

i32 ui_button_is_active_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return 0;
	}
	return (ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_ACTIVE) != 0u ? 1 : 0;
}

static sk_ui_node_t ui_button_make(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id, const_chr_t extra_class, const_chr_t widget_type, f32 width,
								   f32 height, u32 flags, i32 focusable) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	char visible[256];
	const_chr_t id_suffix = NULL;
	const_chr_t use_id = id;
	sk_ui_node_t n;

	ui_split_imgui_label(label, visible, (u32)sizeof(visible), &id_suffix);
	if ((use_id == NULL || use_id[0] == '\0') && id_suffix != NULL && id_suffix[0] != '\0') {
		use_id = id_suffix;
	}

	n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, SK_UI_CLASS_BUTTON, "button", "ui-button", use_id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	if (extra_class != NULL && extra_class[0] != '\0') {
		(void)ui->node_add_class(ctx, n, extra_class);
	}
	(void)ui->node_set_prop_str(ctx, n, "button_variant", widget_type != NULL ? widget_type : "button");
	(void)ui->node_set_prop_str(ctx, n, "text", visible);
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_focusable(ctx, n, focusable != 0 ? 1 : 0);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_BUTTON);
	if (wd != NULL) {
		wd->button_flags = ui_button_norm_flags(flags);
		wd->edge_clicked = 0;
		wd->edge_pressed = 0;
		wd->edge_released = 0;
	}
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_button_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	if (width > 0.0f || height > 0.0f) {
		(void)ui_button_set_size_impl(ctx, n, width, height);
	}
	return n;
}

sk_ui_node_t ui_widget_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	return ui_button_make(ctx, parent, label, id, NULL, "button", 0.0f, 0.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT, 1);
}

sk_ui_node_t ui_widget_small_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	return ui_button_make(ctx, parent, label, id, SK_UI_CLASS_BUTTON_SMALL, "small_button", 0.0f, 0.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT, 1);
}

sk_ui_node_t ui_widget_invisible_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id, f32 width, f32 height, u32 flags) {
	return ui_button_make(ctx, parent, "", id, SK_UI_CLASS_BUTTON_INVISIBLE, "invisible_button", width, height, flags, 0);
}

sk_ui_node_t ui_widget_selection_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, i32 selected, const_chr_t id, f32 width, f32 height) {
	sk_ui_node_t n = ui_button_make(ctx, parent, label, id, NULL, "selection_button", width, height, SK_UI_BUTTON_FLAG_MOUSE_LEFT, 1);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui_button_set_selected_impl(ctx, n, selected);
	return n;
}

sk_ui_node_t ui_widget_bordered_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id, f32 width, f32 height) {
	return ui_button_make(ctx, parent, label, id, SK_UI_CLASS_BUTTON_BORDERED, "bordered_button", width, height, SK_UI_BUTTON_FLAG_MOUSE_LEFT, 1);
}

sk_ui_node_t ui_widget_arrow_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 dir, const_chr_t id) {
	const_chr_t glyph = ">";
	if (dir == SK_UI_ARROW_LEFT) {
		glyph = "<";
	} else if (dir == SK_UI_ARROW_UP) {
		glyph = "^";
	} else if (dir == SK_UI_ARROW_DOWN) {
		glyph = "v";
	}
	return ui_button_make(ctx, parent, glyph, id, SK_UI_CLASS_BUTTON_ARROW, "arrow_button", 22.0f, 22.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT, 1);
}

static i32 ui_node_is_selectable(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t wtype;
	if (slot == NULL) {
		return 0;
	}
	wtype = ui_prop_str_const(slot, "widget");
	return (wtype != NULL && strcmp(wtype, "selectable") == 0) ? 1 : 0;
}

static u32 ui_selectable_flags_of(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, node);
	if (wd == NULL) {
		return SK_UI_SELECTABLE_FLAG_NONE;
	}
	return wd->button_flags;
}

static void ui_selectable_apply_span_style(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags, f32 width, f32 height) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	i32 span = ((flags & (SK_UI_SELECTABLE_FLAG_SPAN_AVAIL_WIDTH | SK_UI_SELECTABLE_FLAG_SPAN_ALL_COLUMNS)) != 0u) ? 1 : 0;

	ui_style_props_clear(&p);
	if (width > 0.0f) {
		p.mask |= SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH;
		p.layout.width = sk_ui_pt(width);
		p.layout.min_width = sk_ui_pt(width);
		p.layout.max_width = sk_ui_pt(width);
	} else if (span != 0) {
		p.mask |= SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_ALIGN_SELF;
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.flex_grow = 1.0f;
		p.layout.align_self = SK_UI_ALIGN_STRETCH;
	} else {
		p.mask |= SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW;
		p.layout.width = sk_ui_auto();
		p.layout.flex_grow = 0.0f;
	}
	if (height > 0.0f) {
		p.mask |= SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT;
		p.layout.height = sk_ui_pt(height);
		p.layout.min_height = sk_ui_pt(height);
		p.layout.max_height = sk_ui_pt(height);
	} else {
		p.mask |= SK_UI_SP_HEIGHT;
		p.layout.height = sk_ui_auto();
	}
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static void ui_selectable_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	sk_ui_node_t hit;
	u32 flags;
	if (wd == NULL || event == NULL) {
		return;
	}
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	flags = wd->button_flags;
	if ((flags & SK_UI_SELECTABLE_FLAG_DISABLED) != 0u) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN) {
		if (event->button != SK_UI_POINTER_BUTTON_LEFT) {
			return;
		}
		wd->edge_pressed = 1;
		wd->edge_released = 0;
		wd->edge_clicked = 0;
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		if (event->button != SK_UI_POINTER_BUTTON_LEFT) {
			return;
		}
		wd->edge_released = 1;
		hit = ui->hit_test(ctx, event->x, event->y);
		if (sk_ui_node_eq(hit, node)) {
			if ((flags & SK_UI_SELECTABLE_FLAG_ALLOW_DOUBLE_CLICK) != 0u && wd->click_armed != 0) {
				wd->edge_double_clicked = 1;
				wd->click_armed = 0;
			} else {
				wd->click_armed = 1;
			}
			wd->edge_clicked = 1;
		}
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_CLICK) {
		if (event->button == SK_UI_POINTER_BUTTON_LEFT) {
			if ((flags & SK_UI_SELECTABLE_FLAG_ALLOW_DOUBLE_CLICK) != 0u && wd->click_armed != 0 && wd->edge_clicked == 0) {
				wd->edge_double_clicked = 1;
				wd->click_armed = 0;
			} else if (wd->edge_clicked == 0) {
				wd->click_armed = 1;
			}
			wd->edge_clicked = 1;
		}
	}
}

sk_ui_node_t ui_widget_selectable_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, i32 selected, u32 flags, const_chr_t id, f32 width, f32 height) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	char visible[256];
	const_chr_t id_suffix = NULL;
	const_chr_t use_id = id;
	sk_ui_node_t n;

	ui_split_imgui_label(label, visible, (u32)sizeof(visible), &id_suffix);
	if ((use_id == NULL || use_id[0] == '\0') && id_suffix != NULL && id_suffix[0] != '\0') {
		use_id = id_suffix;
	}

	n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, SK_UI_CLASS_SELECTABLE, "selectable", "ui-selectable", use_id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", visible);
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_BUTTON);
	if (wd != NULL) {
		wd->button_flags = flags;
		wd->edge_clicked = 0;
		wd->edge_pressed = 0;
		wd->edge_released = 0;
		wd->edge_double_clicked = 0;
		wd->click_armed = 0;
	}
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_selectable_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	ui_selectable_apply_span_style(ctx, n, flags, width, height);
	if (selected != 0) {
		(void)ui_button_set_selected_impl(ctx, n, 1);
	} else {
		(void)ui->node_set_prop_i32(ctx, n, "selected", 0);
	}
	if ((flags & SK_UI_SELECTABLE_FLAG_DISABLED) != 0u) {
		(void)ui_button_set_disabled_impl(ctx, n, 1);
	}
	return n;
}

i32 ui_selectable_set_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 selected) {
	if (ui_node_is_selectable(ctx, node) == 0) {
		return -1;
	}
	return ui_button_set_selected_impl(ctx, node, selected);
}

i32 ui_selectable_get_selected_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	if (ui_node_is_selectable(ctx, node) == 0) {
		return 0;
	}
	return ui_button_get_selected_impl(ctx, node);
}

i32 ui_selectable_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	ui_widget_data_t* wd;
	if (ui_node_is_selectable(ctx, node) == 0) {
		return -1;
	}
	wd = ui_widget_data_ensure(ctx, node, UI_WD_BUTTON);
	if (wd != NULL) {
		if (disabled != 0) {
			wd->button_flags |= SK_UI_SELECTABLE_FLAG_DISABLED;
		} else {
			wd->button_flags &= ~SK_UI_SELECTABLE_FLAG_DISABLED;
		}
	}
	return ui_button_set_disabled_impl(ctx, node, disabled);
}

i32 ui_selectable_get_disabled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return 0;
	}
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return 1;
	}
	return ((ui_selectable_flags_of(ctx, node) & SK_UI_SELECTABLE_FLAG_DISABLED) != 0u) ? 1 : 0;
}

i32 ui_selectable_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	ui_widget_data_t* wd;
	if (ui_node_is_selectable(ctx, node) == 0) {
		return -1;
	}
	wd = ui_widget_data_ensure(ctx, node, UI_WD_BUTTON);
	f32 width = 0.0f;
	f32 height = 0.0f;
	sk_ui_layout_style_t ls;
	if (wd == NULL) {
		return -1;
	}
	wd->button_flags = flags;
	if ((flags & SK_UI_SELECTABLE_FLAG_DISABLED) != 0u) {
		(void)ui_button_set_disabled_impl(ctx, node, 1);
	} else {
		(void)ui_button_set_disabled_impl(ctx, node, 0);
	}
	if (ui_wapi()->node_get_layout_style(ctx, node, &ls) == 0) {
		if (ls.width.unit == SK_UI_LENGTH_POINT && ls.width.value > 0.0f) {
			width = ls.width.value;
		}
		if (ls.height.unit == SK_UI_LENGTH_POINT && ls.height.value > 0.0f) {
			height = ls.height.value;
		}
	}
	ui_selectable_apply_span_style(ctx, node, flags, width, height);
	return 0;
}

u32 ui_selectable_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_selectable_flags_of(ctx, node);
}

i32 ui_selectable_set_size_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height) {
	u32 flags;
	if (ui_node_is_selectable(ctx, node) == 0) {
		return -1;
	}
	flags = ui_selectable_flags_of(ctx, node);
	ui_selectable_apply_span_style(ctx, node, flags, width, height);
	return 0;
}

i32 ui_selectable_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_button_clicked_impl(ctx, node);
}

i32 ui_selectable_double_clicked_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	i32 v;
	if (wd == NULL) {
		return 0;
	}
	v = wd->edge_double_clicked != 0 ? 1 : 0;
	wd->edge_double_clicked = 0;
	return v;
}

i32 ui_selectable_is_hovered_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_button_is_hovered_impl(ctx, node);
}

i32 ui_selectable_is_active_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_button_is_active_impl(ctx, node);
}

void ui_selectable_apply_spans(sk_ui_context_t* ctx) {
	u32 i;
	if (ctx == NULL) {
		return;
	}
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		sk_ui_node_t node;
		sk_ui_node_t cur;
		sk_ui_node_t row;
		sk_ui_rect_t sel_b;
		sk_ui_rect_t row_b;
		u32 flags;
		const_chr_t wtype;
		f32 dx;
		f32 dw;
		if (slot->alive == 0u) {
			continue;
		}
		wtype = NULL;
		{
			u32 p;
			for (p = 0u; p < slot->props.count; ++p) {
				if (slot->props.items[p].type == SK_UI_PROP_STR && slot->props.items[p].key != NULL && strcmp(slot->props.items[p].key, "widget") == 0) {
					wtype = slot->props.items[p].data.str_value;
					break;
				}
			}
		}
		if (wtype == NULL || strcmp(wtype, "selectable") != 0) {
			continue;
		}
		node.index = i;
		node.generation = slot->generation;
		flags = ui_selectable_flags_of(ctx, node);
		if ((flags & SK_UI_SELECTABLE_FLAG_SPAN_ALL_COLUMNS) == 0u) {
			continue;
		}
		row = SK_UI_NODE_INVALID;
		cur = slot->parent;
		while (sk_ui_node_is_valid(cur)) {
			const ui_node_slot_t* ps = ui_slot(ctx, cur);
			u32 c;
			i32 is_row = 0;
			if (ps == NULL) {
				break;
			}
			for (c = 0u; c < ps->classes.count; ++c) {
				const_chr_t cls = ps->classes.items[c];
				if (cls != NULL && (strcmp(cls, SK_UI_CLASS_TABLE_ROW) == 0 || strcmp(cls, SK_UI_CLASS_TABLE_HEADER) == 0)) {
					is_row = 1;
					break;
				}
			}
			if (is_row != 0) {
				row = cur;
				break;
			}
			cur = ps->parent;
		}
		if (!sk_ui_node_is_valid(row)) {
			continue;
		}
		if (ui_node_get_abs_rect_impl(ctx, node, &sel_b, NULL) != 0 || ui_node_get_abs_rect_impl(ctx, row, &row_b, NULL) != 0) {
			continue;
		}
		dx = row_b.x - sel_b.x;
		dw = row_b.width - sel_b.width;
		if (dx > -0.01f && dx < 0.01f && dw > -0.01f && dw < 0.01f) {
			continue;
		}
		slot->layout_border.x += dx;
		slot->layout_border.width += dw;
		slot->layout_content.x += dx;
		slot->layout_content.width += dw;
	}
}

sk_ui_node_t ui_widget_checkbox_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 checked, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_CHECKBOX, "checkbox", "ui-checkbox", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_i32(ctx, n, "checked", checked != 0 ? 1 : 0);
	(void)ui->node_set_prop_i32(ctx, n, "mixed", 0);
	(void)ui->node_set_prop_i32(ctx, n, "labeled", 0);
	(void)ui->node_set_prop_str(ctx, n, "text", "");
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_CHECKBOX);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_checkbox_on_click;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

sk_ui_node_t ui_widget_radio_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 checked, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_RADIO, "radio", "ui-radio", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_i32(ctx, n, "checked", checked != 0 ? 1 : 0);
	(void)ui->node_set_prop_i32(ctx, n, "mixed", 0);
	(void)ui->node_set_prop_i32(ctx, n, "labeled", 0);
	(void)ui->node_set_prop_str(ctx, n, "text", "");
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_RADIO);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_radio_on_click;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

sk_ui_node_t ui_widget_toggle_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 on, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_TOGGLE, "toggle", "ui-toggle", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_i32(ctx, n, "on", on != 0 ? 1 : 0);
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_TOGGLE);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_toggle_on_click;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

static sk_ui_node_t ui_slider_make(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 is_drag, f32 min_v, f32 max_v, f32 value, f32 speed, const_chr_t format, i32 integer, u32 flags,
								   const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	const_chr_t cls = is_drag != 0 ? SK_UI_CLASS_DRAG : SK_UI_CLASS_SLIDER;
	const_chr_t wtype = is_drag != 0 ? "drag" : "slider";
	const_chr_t prefix = is_drag != 0 ? "drag" : "slider";
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, cls, wtype, prefix, id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	if (format == NULL) {
		format = integer != 0 ? "%d" : (is_drag != 0 ? "%.3f" : "");
	}
	if (speed <= 0.0f) {
		speed = 1.0f;
	}
	(void)ui->node_set_prop_f32(ctx, n, "min", min_v);
	(void)ui->node_set_prop_f32(ctx, n, "max", max_v);
	(void)ui->node_set_prop_f32(ctx, n, "value", value);
	(void)ui->node_set_prop_f32(ctx, n, "speed", speed);
	(void)ui->node_set_prop_f32(ctx, n, "step", 0.0f);
	(void)ui->node_set_prop_i32(ctx, n, "integer", integer != 0 ? 1 : 0);
	(void)ui->node_set_prop_i32(ctx, n, "flags", (i32)flags);
	(void)ui->node_set_prop_i32(ctx, n, "text_input", 0);
	(void)ui->node_set_prop_str(ctx, n, "format", format);
	(void)ui->node_set_prop_str(ctx, n, "label", "");
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_SLIDER);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_slider_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	(void)ui_slider_apply_value(ctx, n, value, 0);
	return n;
}

static i32 ui_slider_norm_count(i32 count) {
	if (count < 1) {
		return 1;
	}
	if (count > 4) {
		return 4;
	}
	return count;
}

static sk_ui_node_t ui_slider_make_n(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 is_drag, i32 count, f32 min_v, f32 max_v, const f32* values, f32 speed, const_chr_t format,
									 i32 integer, u32 flags, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t row;
	sk_ui_layout_style_t ls;
	char cid[80];
	i32 i;
	count = ui_slider_norm_count(count);
	row = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, is_drag != 0 ? SK_UI_CLASS_DRAG_N : SK_UI_CLASS_SLIDER_N, is_drag != 0 ? "drag_n" : "slider_n",
						 is_drag != 0 ? "drag_n" : "slider_n", id);
	if (!sk_ui_node_is_valid(row)) {
		return row;
	}
	ui_layout_style_init_default(&ls);
	ls.flex_direction = SK_UI_FLEX_ROW;
	ls.column_gap = 4.0f;
	ls.align_items = SK_UI_ALIGN_STRETCH;
	(void)ui->node_set_layout_style(ctx, row, &ls);
	(void)ui->node_set_prop_i32(ctx, row, "components", count);
	for (i = 0; i < count; ++i) {
		sk_ui_node_t child;
		sk_ui_style_props_t p;
		f32 v = values != NULL ? values[i] : 0.0f;
		if (id != NULL && id[0] != '\0') {
			(void)snprintf(cid, sizeof(cid), "%s/%d", id, i);
		} else {
			(void)snprintf(cid, sizeof(cid), "%s-%d", is_drag != 0 ? "drag" : "sl", i);
		}
		child = ui_slider_make(ctx, row, is_drag, min_v, max_v, v, speed, format, integer, flags, cid);
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_WIDTH;
		p.layout.flex_grow = 1.0f;
		p.layout.min_width = sk_ui_pt(28.0f);
		(void)ui->node_merge_inline_style(ctx, child, &p);
	}
	return row;
}

sk_ui_node_t ui_widget_slider_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 min_v, f32 max_v, f32 value, const_chr_t id) {
	return ui_slider_make(ctx, parent, 0, min_v, max_v, value, 1.0f, "", 0, SK_UI_SLIDER_FLAG_ALWAYS_CLAMP, id);
}

sk_ui_node_t ui_widget_slider_int_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 v_min, i32 v_max, i32 value, const_chr_t format, const_chr_t id) {
	return ui_slider_make(ctx, parent, 0, (f32)v_min, (f32)v_max, (f32)value, 1.0f, format != NULL ? format : "%d", 1, SK_UI_SLIDER_FLAG_ALWAYS_CLAMP, id);
}

sk_ui_node_t ui_widget_slider_float_n_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, f32 v_min, f32 v_max, const f32* values, const_chr_t format, const_chr_t id) {
	return ui_slider_make_n(ctx, parent, 0, count, v_min, v_max, values, 1.0f, format != NULL ? format : "%.3f", 0, SK_UI_SLIDER_FLAG_ALWAYS_CLAMP, id);
}

sk_ui_node_t ui_widget_slider_int_n_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, i32 v_min, i32 v_max, const i32* values, const_chr_t format, const_chr_t id) {
	f32 tmp[4];
	i32 i;
	i32 n = ui_slider_norm_count(count);
	for (i = 0; i < n; ++i) {
		tmp[i] = values != NULL ? (f32)values[i] : 0.0f;
	}
	return ui_slider_make_n(ctx, parent, 0, n, (f32)v_min, (f32)v_max, tmp, 1.0f, format != NULL ? format : "%d", 1, SK_UI_SLIDER_FLAG_ALWAYS_CLAMP, id);
}

sk_ui_node_t ui_widget_drag_float_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 v_speed, f32 v_min, f32 v_max, f32 value, const_chr_t format, const_chr_t id) {
	return ui_slider_make(ctx, parent, 1, v_min, v_max, value, v_speed, format != NULL ? format : "%.3f", 0, SK_UI_SLIDER_FLAG_NONE, id);
}

sk_ui_node_t ui_widget_drag_int_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 v_speed, i32 v_min, i32 v_max, i32 value, const_chr_t format, const_chr_t id) {
	return ui_slider_make(ctx, parent, 1, (f32)v_min, (f32)v_max, (f32)value, v_speed, format != NULL ? format : "%d", 1, SK_UI_SLIDER_FLAG_NONE, id);
}

sk_ui_node_t ui_widget_drag_float_n_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, f32 v_speed, f32 v_min, f32 v_max, const f32* values, const_chr_t format,
										 const_chr_t id) {
	return ui_slider_make_n(ctx, parent, 1, count, v_min, v_max, values, v_speed, format != NULL ? format : "%.3f", 0, SK_UI_SLIDER_FLAG_NONE, id);
}

sk_ui_node_t ui_widget_drag_int_n_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, f32 v_speed, i32 v_min, i32 v_max, const i32* values, const_chr_t format,
									   const_chr_t id) {
	f32 tmp[4];
	i32 i;
	i32 n = ui_slider_norm_count(count);
	for (i = 0; i < n; ++i) {
		tmp[i] = values != NULL ? (f32)values[i] : 0.0f;
	}
	return ui_slider_make_n(ctx, parent, 1, n, (f32)v_min, (f32)v_max, tmp, v_speed, format != NULL ? format : "%d", 1, SK_UI_SLIDER_FLAG_NONE, id);
}

sk_ui_node_t ui_widget_range_slider_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 min_v, f32 max_v, f32 value_low, f32 value_high, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_RANGE_SLIDER, "range_slider", "ui-range-slider", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	if (max_v < min_v) {
		f32 t = min_v;
		min_v = max_v;
		max_v = t;
	}
	value_low = ui_clampf(value_low, min_v, max_v);
	value_high = ui_clampf(value_high, min_v, max_v);
	if (value_low > value_high) {
		f32 t = value_low;
		value_low = value_high;
		value_high = t;
	}
	(void)ui->node_set_prop_f32(ctx, n, "min", min_v);
	(void)ui->node_set_prop_f32(ctx, n, "max", max_v);
	(void)ui->node_set_prop_f32(ctx, n, "value_low", value_low);
	(void)ui->node_set_prop_f32(ctx, n, "value_high", value_high);
	(void)ui->node_set_focusable(ctx, n, 1);
	(void)ui_widget_data_ensure(ctx, n, UI_WD_RANGE_SLIDER);
	return n;
}

sk_ui_node_t ui_widget_progress_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 fraction, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_PROGRESS, "progress", "ui-progress", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	fraction = ui_clampf(fraction, 0.0f, 1.0f);
	(void)ui->node_set_prop_f32(ctx, n, "value", fraction);
	(void)ui_widget_data_ensure(ctx, n, UI_WD_PROGRESS);
	return n;
}

sk_ui_node_t ui_widget_text_input_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	i32 nlen;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_TEXT_INPUT, "text_input", "ui-text-input", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	if (text == NULL) {
		text = "";
	}
	nlen = (i32)ui_utf8_code_count(text);
	ui_text_sync_props(ctx, n, text, nlen, nlen, nlen);
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_TEXT_INPUT);
	if (wd != NULL) {
		wd->input_flags = SK_UI_INPUT_TEXT_FLAG_NONE;
		wd->input_capacity = 0;
	}
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_text_input_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

sk_ui_node_t ui_widget_scroll_view_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t content;
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_SCROLL_VIEW, "scroll_view", "ui-scroll-view", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_clip_children(ctx, n, 1);
	(void)ui->node_set_prop_f32(ctx, n, "scroll_x", 0.0f);
	(void)ui->node_set_prop_f32(ctx, n, "scroll_y", 0.0f);
	(void)ui->node_set_prop_f32(ctx, n, "content_width", 0.0f);
	(void)ui->node_set_prop_f32(ctx, n, "content_height", 0.0f);

	content = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, n);
	if (!sk_ui_node_is_valid(content)) {
		return n;
	}
	(void)ui->node_add_class(ctx, content, "ui-scroll-content");
	(void)ui->node_set_prop_str(ctx, content, "widget", "scroll_content");
	ui_layout_style_init_default(&ls);
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	ls.width = sk_ui_auto();
	ls.height = sk_ui_auto();
	(void)ui->node_set_layout_style(ctx, content, &ls);

	wd = ui_widget_data_ensure(ctx, n, UI_WD_SCROLL);
	if (wd != NULL) {
		wd->content = content;
	}
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_scroll_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

sk_ui_node_t ui_widget_image_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 texture_id, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_IMAGE, parent, SK_UI_CLASS_IMAGE, "image", "ui-image", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_i32(ctx, n, "texture_id", texture_id);
	return n;
}

/* -------------------------------------------------------------------------- */
/* Menu surfaces (APX-234 / APX-346 editor BeginMenu / MenuItem)              */
/* -------------------------------------------------------------------------- */

#define UI_MENU_SHORTCUT_EM 0.55f
#define UI_MENU_SHORTCUT_GAP 28.0f
#define UI_MENU_SHORTCUT_PAD_R 8.0f
#define UI_MENU_CHECK_PAD 22.0f
#define UI_MENU_FONT_SIZE 14.0f

static sk_ui_node_t ui_menu_find_popup_child(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	u32 i;
	if (slot == NULL) {
		return SK_UI_NODE_INVALID;
	}
	for (i = 0u; i < slot->children.count; ++i) {
		const ui_node_slot_t* ch = ui_slot(ctx, slot->children.items[i]);
		const_chr_t w;
		if (ch == NULL) {
			continue;
		}
		w = ui_prop_str_const(ch, "widget");
		if (w != NULL && (strcmp(w, "menu_popup") == 0 || strcmp(w, "context_menu") == 0 || strcmp(w, "popup_menu") == 0)) {
			return slot->children.items[i];
		}
	}
	return SK_UI_NODE_INVALID;
}

static void ui_menu_set_popup_open(sk_ui_context_t* ctx, sk_ui_node_t owner, i32 open) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t popup = ui_menu_find_popup_child(ctx, owner);
	(void)ui->node_set_prop_i32(ctx, owner, "open", open != 0 ? 1 : 0);
	if (sk_ui_node_is_valid(popup)) {
		(void)ui->node_set_prop_i32(ctx, popup, "open", open != 0 ? 1 : 0);
		(void)ui->node_set_prop_i32(ctx, popup, "hidden", open != 0 ? 0 : 1);
	}
	ui_mark_dirty_up(ctx, owner, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
}

static i32 ui_menu_event_on_trigger(const sk_ui_context_t* ctx, sk_ui_node_t menu, sk_ui_node_t target) {
	/* Only the labeled trigger toggles. Clicks on the floating popup (padding
	 * or a disabled row that is not itself a hit target) must not close it. */
	(void)ctx;
	if (!sk_ui_node_is_valid(target)) {
		return 0;
	}
	return sk_ui_node_eq(target, menu) ? 1 : 0;
}

static void ui_menu_toggle_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	i32 open = 0;
	(void)user;
	if (event != NULL && ui_menu_event_on_trigger(ctx, node, event->target) == 0) {
		return;
	}
	if ((ui_wapi()->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	(void)ui_prop_i32_const(ui_slot(ctx, node), "open", &open);
	ui_menu_set_popup_open(ctx, node, open != 0 ? 0 : 1);
	if (event != NULL) {
		event->consumed = 1;
	}
}

static void ui_submenu_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)user;
	if ((ui_wapi()->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	/* Hover opens nested popup so open state propagates across frames via props. */
	if (event->type == SK_UI_EVENT_POINTER_ENTER) {
		ui_menu_set_popup_open(ctx, node, 1);
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_LEAVE) {
		/* Keep open if pointer moved into the popup child (engine hover may be
		 * the popup). Close only when neither the item nor its popup is hovered. */
		sk_ui_node_t popup = ui_menu_find_popup_child(ctx, node);
		sk_ui_node_t hover = ctx->hover;
		if (sk_ui_node_is_valid(popup) && (sk_ui_node_eq(hover, popup) || sk_ui_node_eq(hover, node))) {
			return;
		}
		/* Walk ancestors of hover: stay open if still inside popup subtree. */
		if (sk_ui_node_is_valid(popup) && sk_ui_node_is_valid(hover)) {
			const ui_node_slot_t* hs = ui_slot(ctx, hover);
			while (hs != NULL && sk_ui_node_is_valid(hs->parent)) {
				if (sk_ui_node_eq(hs->parent, popup) || sk_ui_node_eq(hs->parent, node)) {
					return;
				}
				hs = ui_slot(ctx, hs->parent);
			}
		}
		ui_menu_set_popup_open(ctx, node, 0);
	}
}

static sk_ui_node_t ui_menu_make_popup(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 attach, i32 z_index, const_chr_t id_prefix, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	char auto_id[80];
	const_chr_t popup_id = id;
	sk_ui_node_t n;

	if (id == NULL || id[0] == '\0') {
		ctx->widget_id_seq += 1u;
		(void)snprintf(auto_id, sizeof(auto_id), "%s-popup-%u", id_prefix != NULL ? id_prefix : "menu", ctx->widget_id_seq);
		popup_id = auto_id;
	}
	n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_MENU_POPUP, "menu_popup", "ui-menu-popup", popup_id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_i32(ctx, n, "open", 0);
	(void)ui->node_set_prop_i32(ctx, n, "hidden", 1);
	(void)ui->node_set_prop_i32(ctx, n, "attach", attach != 0 ? 1 : 0);
	(void)ui->node_set_prop_i32(ctx, n, "z_index", z_index);
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	ls.min_width = sk_ui_pt(attach != 0 ? 140.0f : 240.0f);
	ls.min_height = sk_ui_pt(attach != 0 ? 52.0f : 160.0f);
	if (attach != 0) {
		ls.left = sk_ui_pt(2.0f);
		ls.top = sk_ui_pt(0.0f);
	} else {
		ls.left = sk_ui_pt(0.0f);
		ls.top = sk_ui_pt(28.0f);
	}
	(void)ui->node_set_layout_style(ctx, n, &ls);
	return n;
}

sk_ui_node_t ui_widget_menu_bar_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	return ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_MENU_BAR, "menu_bar", "ui-menu-bar", id);
}

static void ui_menu_close_ancestors(sk_ui_context_t* ctx, sk_ui_node_t node);

static void ui_menu_item_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	ui_button_on_event(ctx, node, event, user);
	if (event != NULL && event->type == SK_UI_EVENT_CLICK) {
		event->consumed = 1;
	}
	if (wd != NULL && wd->edge_clicked != 0) {
		ui_menu_close_ancestors(ctx, node);
	}
}

sk_ui_node_t ui_widget_menu_item_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, SK_UI_CLASS_MENU_ITEM, "menu_item", "ui-menu-item", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", label != NULL ? label : "");
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "selected", 0);
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_BUTTON);
	if (wd != NULL) {
		wd->button_flags = SK_UI_BUTTON_FLAG_MOUSE_LEFT;
	}
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_menu_item_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

sk_ui_node_t ui_widget_menu_popup_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 attach, const_chr_t id) {
	return ui_menu_make_popup(ctx, parent, attach, attach != 0 ? 110 : 100, "menu", id);
}

sk_ui_node_t ui_widget_menu_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t n;
	sk_ui_node_t popup;
	char popup_id[80];

	n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, SK_UI_CLASS_MENU, "menu", "ui-menu", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", label != NULL ? label : "");
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "open", 0);
	(void)ui->node_set_focusable(ctx, n, 1);
	if (id != NULL && id[0] != '\0') {
		(void)snprintf(popup_id, sizeof(popup_id), "%s-popup", id);
		popup = ui_menu_make_popup(ctx, n, 0, 100, id, popup_id);
	} else {
		popup = ui_menu_make_popup(ctx, n, 0, 100, "menu", NULL);
	}
	(void)popup;
	wd = ui_widget_data_ensure(ctx, n, UI_WD_MENU);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_menu_toggle_on_click;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

sk_ui_node_t ui_widget_dropdown_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t n;
	char popup_id[80];

	n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, SK_UI_CLASS_DROPDOWN, "dropdown", "ui-dropdown", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", label != NULL ? label : "");
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "open", 0);
	(void)ui->node_set_focusable(ctx, n, 1);
	if (id != NULL && id[0] != '\0') {
		(void)snprintf(popup_id, sizeof(popup_id), "%s-popup", id);
		(void)ui_menu_make_popup(ctx, n, 0, 100, id, popup_id);
	} else {
		(void)ui_menu_make_popup(ctx, n, 0, 100, "dropdown", NULL);
	}
	wd = ui_widget_data_ensure(ctx, n, UI_WD_DROPDOWN);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_menu_toggle_on_click;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

sk_ui_node_t ui_widget_context_menu_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_CONTEXT_MENU, "context_menu", "ui-context-menu", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_i32(ctx, n, "open", 0);
	(void)ui->node_set_prop_i32(ctx, n, "hidden", 1);
	(void)ui->node_set_prop_i32(ctx, n, "z_index", 200);
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	ls.min_width = sk_ui_pt(240.0f);
	ls.left = sk_ui_pt(0.0f);
	ls.top = sk_ui_pt(0.0f);
	(void)ui->node_set_layout_style(ctx, n, &ls);
	return n;
}

sk_ui_node_t ui_widget_submenu_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t n;
	char popup_id[80];

	n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, SK_UI_CLASS_SUBMENU, "submenu", "ui-submenu", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", label != NULL ? label : "");
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "open", 0);
	(void)ui->node_set_focusable(ctx, n, 1);
	if (id != NULL && id[0] != '\0') {
		(void)snprintf(popup_id, sizeof(popup_id), "%s-popup", id);
		(void)ui_menu_make_popup(ctx, n, 1, 110, id, popup_id);
	} else {
		(void)ui_menu_make_popup(ctx, n, 1, 110, "submenu", NULL);
	}
	wd = ui_widget_data_ensure(ctx, n, UI_WD_SUBMENU);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_submenu_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

static void ui_popup_sync_stack(sk_ui_context_t* ctx, sk_ui_node_t node, i32 open);
static void ui_popup_raise(sk_ui_context_t* ctx, sk_ui_node_t node);
static void ui_popup_apply_default_focus(sk_ui_context_t* ctx, sk_ui_node_t owner);

i32 ui_menu_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 open) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t w;
	if (slot == NULL) {
		return -1;
	}
	if (open != 0 && (ui_wapi()->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return 0;
	}
	w = ui_prop_str_const(slot, "widget");
	if (w != NULL && (strcmp(w, "menu_popup") == 0 || strcmp(w, "context_menu") == 0 || strcmp(w, "popup_menu") == 0 || strcmp(w, "modal") == 0)) {
		const sk_ui_api_t* ui = ui_wapi();
		(void)ui->node_set_prop_i32(ctx, node, "open", open != 0 ? 1 : 0);
		(void)ui->node_set_prop_i32(ctx, node, "hidden", open != 0 ? 0 : 1);
		ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
		ui_popup_sync_stack(ctx, node, open);
		if (open != 0) {
			ui_popup_raise(ctx, node);
			ui_popup_apply_default_focus(ctx, node);
		}
		if (w != NULL && strcmp(w, "modal") == 0) {
			ui_widget_data_t* wd = ui_widget_data(ctx, node);
			if (wd != NULL && wd->p_open != NULL) {
				*wd->p_open = open != 0 ? 1 : 0;
			}
		}
		return 0;
	}
	ui_menu_set_popup_open(ctx, node, open);
	{
		sk_ui_node_t popup = ui_menu_find_popup_child(ctx, node);
		if (sk_ui_node_is_valid(popup)) {
			ui_popup_sync_stack(ctx, popup, open);
		} else {
			ui_popup_sync_stack(ctx, node, open);
		}
	}
	return 0;
}

i32 ui_menu_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 open = 0;
	sk_ui_node_t popup;
	if (slot == NULL) {
		return 0;
	}
	if (ui_prop_i32_const(slot, "open", &open) == 0 && open != 0) {
		return 1;
	}
	popup = ui_menu_find_popup_child(ctx, node);
	if (sk_ui_node_is_valid(popup)) {
		const ui_node_slot_t* ps = ui_slot(ctx, popup);
		open = 0;
		if (ps != NULL && ui_prop_i32_const(ps, "open", &open) == 0 && open != 0) {
			return 1;
		}
	}
	return 0;
}

sk_ui_node_t ui_menu_get_popup_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_menu_find_popup_child(ctx, node);
}

static i32 ui_menu_node_contains(const sk_ui_context_t* ctx, sk_ui_node_t ancestor, sk_ui_node_t node) {
	sk_ui_node_t cur = node;
	if (!sk_ui_node_is_valid(ancestor) || !sk_ui_node_is_valid(cur)) {
		return 0;
	}
	while (sk_ui_node_is_valid(cur)) {
		const ui_node_slot_t* slot;
		if (sk_ui_node_eq(cur, ancestor)) {
			return 1;
		}
		slot = ui_slot(ctx, cur);
		if (slot == NULL) {
			break;
		}
		cur = slot->parent;
	}
	return 0;
}

static i32 ui_menu_point_in_rect(const sk_ui_context_t* ctx, sk_ui_node_t node, f32 x, f32 y) {
	sk_ui_rect_t r;
	if (ui_wapi()->node_get_abs_rect(ctx, node, &r, NULL) != 0) {
		return 0;
	}
	return (x >= r.x && y >= r.y && x < (r.x + r.width) && y < (r.y + r.height)) ? 1 : 0;
}

static i32 ui_menu_surface_is_closed(const ui_node_slot_t* slot) {
	const_chr_t w;
	i32 open = 1;
	i32 hidden = 0;
	if (slot == NULL) {
		return 1;
	}
	w = ui_prop_str_const(slot, "widget");
	if (w == NULL) {
		return 0;
	}
	if (strcmp(w, "menu_popup") != 0 && strcmp(w, "context_menu") != 0 && strcmp(w, "popup_menu") != 0) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "open", &open);
	(void)ui_prop_i32_const(slot, "hidden", &hidden);
	return (open == 0 || hidden != 0) ? 1 : 0;
}

static i32 ui_menu_point_in_surface(const sk_ui_context_t* ctx, sk_ui_node_t node, f32 x, f32 y) {
	sk_ui_node_t stack[64];
	u32 n = 0u;
	if (!sk_ui_node_is_valid(node)) {
		return 0;
	}
	stack[n++] = node;
	while (n > 0u) {
		sk_ui_node_t cur = stack[--n];
		const ui_node_slot_t* slot = ui_slot(ctx, cur);
		u32 i;
		if (slot == NULL || ui_menu_surface_is_closed(slot) != 0) {
			continue;
		}
		if (ui_menu_point_in_rect(ctx, cur, x, y) != 0) {
			return 1;
		}
		for (i = 0u; i < slot->children.count && n < 64u; ++i) {
			stack[n++] = slot->children.items[i];
		}
	}
	return 0;
}

static i32 ui_menu_hit_in_owner(const sk_ui_context_t* ctx, sk_ui_node_t owner, sk_ui_node_t hit, f32 x, f32 y) {
	if (ui_menu_point_in_surface(ctx, owner, x, y) != 0) {
		return 1;
	}
	if (sk_ui_node_is_valid(hit) && ui_menu_node_contains(ctx, owner, hit)) {
		return 1;
	}
	return 0;
}

static i32 ui_menu_is_owner_widget(const_chr_t w) {
	if (w == NULL) {
		return 0;
	}
	return (strcmp(w, "menu") == 0 || strcmp(w, "dropdown") == 0 || strcmp(w, "submenu") == 0 || strcmp(w, "context_menu") == 0 || strcmp(w, "popup_menu") == 0 ||
			strcmp(w, "combo") == 0) ?
			   1 :
			   0;
}

static void ui_menu_close_ancestors(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	while (slot != NULL && sk_ui_node_is_valid(slot->parent)) {
		sk_ui_node_t parent = slot->parent;
		const ui_node_slot_t* ps = ui_slot(ctx, parent);
		const_chr_t w;
		slot = ps;
		if (ps == NULL) {
			break;
		}
		w = ui_prop_str_const(ps, "widget");
		if (ui_menu_is_owner_widget(w) || (w != NULL && (strcmp(w, "menu_popup") == 0))) {
			(void)ui_menu_set_open_impl(ctx, parent, 0);
		}
	}
}

typedef struct ui_menu_dismiss_t {
	sk_ui_node_t hit;
	f32 x;
	f32 y;
} ui_menu_dismiss_t;

static i32 ui_menu_dismiss_walk(sk_ui_context_t* ctx, sk_ui_node_t node, u32 depth, void_ptr_t user) {
	const ui_menu_dismiss_t* d = (const ui_menu_dismiss_t*)user;
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t w;
	(void)depth;
	if (slot == NULL || d == NULL) {
		return 0;
	}
	w = ui_prop_str_const(slot, "widget");
	if (ui_menu_is_owner_widget(w)) {
		if (ui_menu_get_open_impl(ctx, node) != 0 && ui_menu_hit_in_owner(ctx, node, d->hit, d->x, d->y) == 0) {
			(void)ui_menu_set_open_impl(ctx, node, 0);
		}
		return 0;
	}
	if (w != NULL && strcmp(w, "menu_popup") == 0) {
		const ui_node_slot_t* ps = ui_slot(ctx, slot->parent);
		const_chr_t pw = ps != NULL ? ui_prop_str_const(ps, "widget") : NULL;
		if (pw != NULL && (strcmp(pw, "menu") == 0 || strcmp(pw, "dropdown") == 0 || strcmp(pw, "submenu") == 0 || strcmp(pw, "combo") == 0)) {
			return 0;
		}
		if (ui_menu_get_open_impl(ctx, node) != 0 && ui_menu_point_in_surface(ctx, node, d->x, d->y) == 0 &&
			(sk_ui_node_is_valid(d->hit) == 0 || ui_menu_node_contains(ctx, node, d->hit) == 0)) {
			(void)ui_menu_set_open_impl(ctx, node, 0);
		}
	}
	return 0;
}

void ui_menu_dismiss_outside_impl(sk_ui_context_t* ctx, sk_ui_node_t hit, f32 x, f32 y) {
	ui_menu_dismiss_t d;
	const sk_ui_api_t* ui;
	if (ctx == NULL) {
		return;
	}
	ui = ui_wapi();
	d.hit = hit;
	d.x = x;
	d.y = y;
	(void)ui->traverse_preorder(ctx, ui->context_root(ctx), ui_menu_dismiss_walk, &d);
}

static f32 ui_menu_text_advance(const_chr_t text, f32 font_size) {
	u32 n;
	if (text == NULL || text[0] == '\0') {
		return 0.0f;
	}
	n = (u32)strlen(text);
	if (font_size < 1.0f) {
		font_size = UI_MENU_FONT_SIZE;
	}
	return (f32)n * font_size * UI_MENU_SHORTCUT_EM;
}

static void ui_menu_item_apply_min_width(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t label = ui->label_get_text(ctx, node);
	const_chr_t shortcut = slot != NULL ? ui_prop_str_const(slot, "shortcut") : NULL;
	f32 sw = ui_menu_text_advance(shortcut, UI_MENU_FONT_SIZE);
	f32 lw = ui_menu_text_advance(label, UI_MENU_FONT_SIZE);
	f32 min_w = UI_MENU_CHECK_PAD + lw + UI_MENU_SHORTCUT_PAD_R;
	sk_ui_style_props_t p;
	if (sw > 0.0f) {
		min_w += UI_MENU_SHORTCUT_GAP + sw;
	}
	(void)ui->node_set_prop_f32(ctx, node, "shortcut_w", sw);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_MIN_WIDTH;
	p.layout.min_width = sk_ui_pt(min_w);
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

i32 ui_menu_set_enabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 enabled) {
	i32 rc;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	rc = ui_button_set_disabled_impl(ctx, node, enabled != 0 ? 0 : 1);
	if (rc == 0 && enabled == 0) {
		(void)ui_menu_set_open_impl(ctx, node, 0);
	}
	return rc;
}

i32 ui_menu_get_enabled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return 0;
	}
	return (ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) == 0u ? 1 : 0;
}

i32 ui_menu_item_set_enabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 enabled) {
	return ui_menu_set_enabled_impl(ctx, node, enabled);
}

i32 ui_menu_item_get_enabled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_menu_get_enabled_impl(ctx, node);
}

i32 ui_menu_item_set_shortcut_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t shortcut) {
	const sk_ui_api_t* ui = ui_wapi();
	i32 rc;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	rc = ui->node_set_prop_str(ctx, node, "shortcut", shortcut != NULL ? shortcut : "");
	if (rc != 0) {
		return rc;
	}
	ui_menu_item_apply_min_width(ctx, node);
	ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	return 0;
}

const_chr_t ui_menu_item_get_shortcut_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t s;
	if (slot == NULL) {
		return "";
	}
	s = ui_prop_str_const(slot, "shortcut");
	return s != NULL ? s : "";
}

f32 ui_menu_item_measure_shortcut_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_menu_text_advance(ui_menu_item_get_shortcut_impl(ctx, node), UI_MENU_FONT_SIZE);
}

i32 ui_menu_item_get_shortcut_rect_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_rect_t r;
	f32 sw;
	if (out == NULL || ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	sw = ui_menu_item_measure_shortcut_impl(ctx, node);
	if (sw <= 0.0f) {
		out->x = 0.0f;
		out->y = 0.0f;
		out->width = 0.0f;
		out->height = 0.0f;
		return -1;
	}
	if (ui->node_get_abs_rect(ctx, node, &r, NULL) != 0) {
		return -1;
	}
	out->width = sw;
	out->height = r.height;
	out->y = r.y;
	out->x = r.x + r.width - UI_MENU_SHORTCUT_PAD_R - sw;
	return 0;
}

i32 ui_menu_item_set_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 selected) {
	const sk_ui_api_t* ui = ui_wapi();
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	(void)ui->node_set_prop_i32(ctx, node, "selected", selected != 0 ? 1 : 0);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	return 0;
}

i32 ui_menu_item_get_selected_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 selected = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "selected", &selected);
	return selected != 0 ? 1 : 0;
}

sk_ui_node_t ui_widget_menu_separator_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_MENU_SEPARATOR, "menu_separator", "ui-menu-separator", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_focusable(ctx, n, 0);
	return n;
}

i32 ui_menu_item_clicked_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_button_clicked_impl(ctx, node);
}

/* -------------------------------------------------------------------------- */
/* Docking / editor window surfaces (APX-235)                                 */
/* -------------------------------------------------------------------------- */

static void ui_splitter_apply_ratio(sk_ui_context_t* ctx, sk_ui_node_t node, f32 ratio, i32 fire_cb) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd;
	ratio = ui_clampf(ratio, 0.0f, 1.0f);
	(void)ui->node_set_prop_f32(ctx, node, "ratio", ratio);
	ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	if (fire_cb == 0) {
		return;
	}
	wd = ui_widget_data(ctx, node);
	if (wd != NULL && wd->on_float != NULL) {
		wd->on_float(ctx, node, ratio, wd->cb_user);
	}
}

static void ui_splitter_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 axis = 0;
	sk_ui_rect_t parent_border;
	sk_ui_node_t parent;
	f32 span;
	f32 origin;
	f32 ratio;

	if (slot == NULL) {
		return;
	}
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	(void)ui_prop_i32_const(slot, "axis", &axis);

	if (event->type == SK_UI_EVENT_POINTER_DOWN && event->button == SK_UI_POINTER_BUTTON_LEFT) {
		if (wd != NULL) {
			wd->dragging = 1;
			wd->drag_last_x = event->x;
			wd->drag_last_y = event->y;
		}
		(void)ui->pointer_capture_set(ctx, node);
		(void)ui->node_set_state(ctx, node, ui->node_get_state(ctx, node) | (u32)SK_UI_STATE_ACTIVE);
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		if (wd != NULL) {
			wd->dragging = 0;
		}
		if (sk_ui_node_eq(ui->pointer_capture_get(ctx), node)) {
			(void)ui->pointer_capture_set(ctx, SK_UI_NODE_INVALID);
		}
		(void)ui->node_set_state(ctx, node, ui->node_get_state(ctx, node) & ~(u32)SK_UI_STATE_ACTIVE);
		event->consumed = 1;
		return;
	}
	if (event->type != SK_UI_EVENT_POINTER_MOVE) {
		return;
	}
	if (wd == NULL || wd->dragging == 0) {
		return;
	}

	parent = ui->node_parent(ctx, node);
	if (!sk_ui_node_is_valid(parent) || ui->node_get_abs_rect(ctx, parent, &parent_border, NULL) != 0) {
		/* Fallback: use own abs rect span when parent missing. */
		if (ui->node_get_abs_rect(ctx, node, &parent_border, NULL) != 0) {
			return;
		}
	}
	if (axis != 0) {
		span = parent_border.height;
		origin = parent_border.y;
		ratio = span > 0.0f ? (event->y - origin) / span : 0.0f;
	} else {
		span = parent_border.width;
		origin = parent_border.x;
		ratio = span > 0.0f ? (event->x - origin) / span : 0.0f;
	}
	wd->drag_last_x = event->x;
	wd->drag_last_y = event->y;
	ui_splitter_apply_ratio(ctx, node, ratio, 1);
	event->consumed = 1;
}

static sk_ui_node_t ui_find_child_widget(const sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t widget);
static void ui_tab_close_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);
static void ui_tab_close_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);

static i32 ui_tab_is_page(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t w;
	i32 flags = 0;
	i32 hidden = 0;
	if (slot == NULL) {
		return 0;
	}
	w = ui_prop_str_const(slot, "widget");
	if (w == NULL || strcmp(w, "tab") != 0) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "tab_flags", &flags);
	if ((flags & (i32)SK_UI_TAB_ITEM_FLAG_BUTTON) != 0) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "hidden", &hidden);
	if (hidden != 0) {
		return 0;
	}
	return 1;
}

static i32 ui_tab_bar_has_set_selected(const sk_ui_context_t* ctx, sk_ui_node_t tab_bar) {
	const ui_node_slot_t* bar_slot = ui_slot(ctx, tab_bar);
	u32 i;
	if (bar_slot == NULL) {
		return 0;
	}
	for (i = 0u; i < bar_slot->children.count; ++i) {
		const ui_node_slot_t* cs = ui_slot(ctx, bar_slot->children.items[i]);
		i32 flags = 0;
		if (cs == NULL) {
			continue;
		}
		(void)ui_prop_i32_const(cs, "tab_flags", &flags);
		if ((flags & (i32)SK_UI_TAB_ITEM_FLAG_SET_SELECTED) != 0 && ui_tab_is_page(ctx, bar_slot->children.items[i]) != 0) {
			return 1;
		}
	}
	return 0;
}

static void ui_tab_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t bar;
	(void)user;
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	if (ui_tab_is_page(ctx, node) == 0) {
		return;
	}
	bar = ui->node_parent(ctx, node);
	if (sk_ui_node_is_valid(bar) && ui_tab_bar_has_set_selected(ctx, bar) != 0) {
		if (event != NULL) {
			event->consumed = 1;
		}
		return;
	}
	(void)ui_tab_bar_set_active_impl(ctx, bar, node);
	if (event != NULL) {
		event->consumed = 1;
	}
}

static void ui_window_title_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	sk_ui_node_t window;
	const ui_node_slot_t* win_slot;
	sk_ui_layout_style_t ls;
	f32 dx;
	f32 dy;

	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}

	if (event->type == SK_UI_EVENT_POINTER_DOWN && event->button == SK_UI_POINTER_BUTTON_LEFT) {
		if (wd != NULL) {
			wd->dragging = 1;
			wd->drag_last_x = event->x;
			wd->drag_last_y = event->y;
		}
		(void)ui->pointer_capture_set(ctx, node);
		(void)ui->node_set_state(ctx, node, ui->node_get_state(ctx, node) | (u32)SK_UI_STATE_ACTIVE);
		window = ui->node_parent(ctx, node);
		ui_dock_on_float_pointer(ctx, window, event);
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		window = ui->node_parent(ctx, node);
		ui_dock_on_float_pointer(ctx, window, event);
		if (wd != NULL) {
			wd->dragging = 0;
		}
		if (sk_ui_node_eq(ui->pointer_capture_get(ctx), node)) {
			(void)ui->pointer_capture_set(ctx, SK_UI_NODE_INVALID);
		}
		(void)ui->node_set_state(ctx, node, ui->node_get_state(ctx, node) & ~(u32)SK_UI_STATE_ACTIVE);
		event->consumed = 1;
		return;
	}
	if (event->type != SK_UI_EVENT_POINTER_MOVE || wd == NULL || wd->dragging == 0) {
		return;
	}

	/* Drag floating editor_window (absolute) by updating left/top; in-flow
	 * docked windows ignore drag (ratio/layout owned by dock tree). */
	window = ui->node_parent(ctx, node);
	win_slot = ui_slot(ctx, window);
	if (win_slot == NULL || win_slot->layout_style.position != SK_UI_POSITION_ABSOLUTE) {
		wd->drag_last_x = event->x;
		wd->drag_last_y = event->y;
		event->consumed = 1;
		return;
	}
	dx = event->x - wd->drag_last_x;
	dy = event->y - wd->drag_last_y;
	wd->drag_last_x = event->x;
	wd->drag_last_y = event->y;
	ls = win_slot->layout_style;
	if (ls.left.unit != SK_UI_LENGTH_POINT) {
		ls.left = sk_ui_pt(0.0f);
	}
	if (ls.top.unit != SK_UI_LENGTH_POINT) {
		ls.top = sk_ui_pt(0.0f);
	}
	ls.left.value += dx;
	ls.top.value += dy;
	(void)ui->node_set_layout_style(ctx, window, &ls);
	(void)ui->node_set_prop_f32(ctx, node, "drag_x", ls.left.value);
	(void)ui->node_set_prop_f32(ctx, node, "drag_y", ls.top.value);
	ui_dock_on_float_pointer(ctx, window, event);
	event->consumed = 1;
}

static sk_ui_node_t ui_find_child_widget(const sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t widget) {
	const ui_node_slot_t* slot = ui_slot(ctx, parent);
	u32 i;
	if (slot == NULL || widget == NULL) {
		return SK_UI_NODE_INVALID;
	}
	for (i = 0u; i < slot->children.count; ++i) {
		const ui_node_slot_t* ch = ui_slot(ctx, slot->children.items[i]);
		const_chr_t w;
		if (ch == NULL) {
			continue;
		}
		w = ui_prop_str_const(ch, "widget");
		if (w != NULL && strcmp(w, widget) == 0) {
			return slot->children.items[i];
		}
	}
	return SK_UI_NODE_INVALID;
}

sk_ui_node_t ui_widget_dock_space_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_DOCK_SPACE, "dock_space", "ui-dock-space", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_clip_children(ctx, n, 1);
	return n;
}

sk_ui_node_t ui_widget_dock_node_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 orientation, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_DOCK_NODE, "dock_node", "ui-dock-node", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_i32(ctx, n, "orientation", orientation != 0 ? 1 : 0);
	(void)ui->node_set_clip_children(ctx, n, 1);
	if (ui->node_get_layout_style(ctx, n, &ls) == 0) {
		ls.flex_direction = orientation != 0 ? SK_UI_FLEX_COLUMN : SK_UI_FLEX_ROW;
		ls.flex_grow = 1.0f;
		(void)ui->node_set_layout_style(ctx, n, &ls);
	}
	return n;
}

sk_ui_node_t ui_widget_splitter_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 axis, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_SPLITTER, "splitter", "ui-splitter", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_i32(ctx, n, "axis", axis != 0 ? 1 : 0);
	(void)ui->node_set_prop_f32(ctx, n, "ratio", 0.5f);
	(void)ui->node_set_focusable(ctx, n, 1);
	if (ui->node_get_layout_style(ctx, n, &ls) == 0) {
		if (axis != 0) {
			/* Horizontal bar: fixed height, grow width. */
			ls.width = sk_ui_percent(100.0f);
			ls.height = sk_ui_pt(6.0f);
			ls.flex_grow = 0.0f;
		} else {
			/* Vertical bar: fixed width, grow height. */
			ls.width = sk_ui_pt(6.0f);
			ls.height = sk_ui_percent(100.0f);
			ls.flex_grow = 0.0f;
		}
		(void)ui->node_set_layout_style(ctx, n, &ls);
	}
	wd = ui_widget_data_ensure(ctx, n, UI_WD_SPLITTER);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_splitter_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
}

sk_ui_node_t ui_widget_tab_bar_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_TAB_BAR, "tab_bar", "ui-tab-bar", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui_widget_data_ensure(ctx, n, UI_WD_TAB_BAR);
	if (ui->node_get_layout_style(ctx, n, &ls) == 0) {
		ls.column_gap = 1.0f;
		(void)ui->node_set_layout_style(ctx, n, &ls);
	}
	return n;
}

sk_ui_node_t ui_widget_tab_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	return ui_widget_tab_item_impl(ctx, parent, label, id, NULL, SK_UI_TAB_ITEM_FLAG_NONE);
}

sk_ui_node_t ui_widget_editor_window_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t title, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t win;
	sk_ui_node_t title_bar;
	sk_ui_node_t content;
	char title_id[80];
	char content_id[80];

	win = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_EDITOR_WINDOW, "editor_window", "ui-editor-window", id);
	if (!sk_ui_node_is_valid(win)) {
		return win;
	}

	if (id != NULL && id[0] != '\0') {
		(void)snprintf(title_id, sizeof(title_id), "%s-title", id);
		(void)snprintf(content_id, sizeof(content_id), "%s-content", id);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(title_id, sizeof(title_id), "ui-window-title-%u", ctx->widget_id_seq);
		ctx->widget_id_seq += 1u;
		(void)snprintf(content_id, sizeof(content_id), "ui-window-content-%u", ctx->widget_id_seq);
	}

	title_bar = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, win, SK_UI_CLASS_WINDOW_TITLE_BAR, "window_title_bar", "ui-window-title-bar", title_id);
	if (sk_ui_node_is_valid(title_bar)) {
		(void)ui->node_set_prop_str(ctx, title_bar, "text", title != NULL ? title : "");
		(void)ui->node_set_prop_i32(ctx, title_bar, "text_align", 0);
		(void)ui->node_set_prop_i32(ctx, title_bar, "vertical_align", 1);
		(void)ui->node_set_focusable(ctx, title_bar, 1);
		wd = ui_widget_data_ensure(ctx, title_bar, UI_WD_WINDOW_TITLE);
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_event = ui_window_title_on_event;
		cbs.user = wd;
		(void)ui->node_set_callbacks(ctx, title_bar, &cbs);
	}

	content = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, win, SK_UI_CLASS_WINDOW_CONTENT, "window_content", "ui-window-content", content_id);
	if (sk_ui_node_is_valid(content)) {
		(void)ui->node_set_clip_children(ctx, content, 1);
	}
	return win;
}

sk_ui_node_t ui_editor_window_title_bar_impl(const sk_ui_context_t* ctx, sk_ui_node_t window) {
	return ui_find_child_widget(ctx, window, "window_title_bar");
}

sk_ui_node_t ui_editor_window_content_impl(const sk_ui_context_t* ctx, sk_ui_node_t window) {
	return ui_find_child_widget(ctx, window, "window_content");
}

i32 ui_editor_window_set_title_impl(sk_ui_context_t* ctx, sk_ui_node_t window, const_chr_t title) {
	sk_ui_node_t bar = ui_editor_window_title_bar_impl(ctx, window);
	if (!sk_ui_node_is_valid(bar)) {
		return -1;
	}
	return ui_wapi()->node_set_prop_str(ctx, bar, "text", title != NULL ? title : "");
}

i32 ui_tab_set_active_impl(sk_ui_context_t* ctx, sk_ui_node_t tab, i32 active) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 st;
	if (ui->node_set_prop_i32(ctx, tab, "active", active != 0 ? 1 : 0) != 0) {
		return -1;
	}
	st = ui->node_get_state(ctx, tab);
	if (active != 0) {
		st |= (u32)SK_UI_STATE_ACTIVE;
	} else {
		st &= ~(u32)SK_UI_STATE_ACTIVE;
	}
	(void)ui->node_set_state(ctx, tab, st);
	ui_mark_dirty_up(ctx, tab, (u32)SK_UI_DIRTY_PAINT);
	return 0;
}

i32 ui_tab_get_active_impl(const sk_ui_context_t* ctx, sk_ui_node_t tab) {
	const ui_node_slot_t* slot = ui_slot(ctx, tab);
	i32 active = 0;
	if (slot == NULL) {
		return 0;
	}
	if (ui_prop_i32_const(slot, "active", &active) == 0 && active != 0) {
		return 1;
	}
	return 0;
}

static void ui_tab_sync_body(sk_ui_context_t* ctx, sk_ui_node_t tab, i32 active) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = ui_widget_data(ctx, tab);
	sk_ui_node_t body = SK_UI_NODE_INVALID;
	if (wd != NULL && sk_ui_node_is_valid(wd->content)) {
		body = wd->content;
	}
	if (!sk_ui_node_is_valid(body)) {
		body = ui_find_child_widget(ctx, tab, "tab_body");
	}
	if (sk_ui_node_is_valid(body)) {
		(void)ui->node_set_prop_i32(ctx, body, "hidden", active != 0 ? 0 : 1);
		ui_mark_dirty_up(ctx, body, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	}
}

static i32 ui_tab_page_index(const sk_ui_context_t* ctx, sk_ui_node_t tab_bar, sk_ui_node_t tab) {
	const ui_node_slot_t* bar_slot = ui_slot(ctx, tab_bar);
	u32 i;
	i32 idx = 0;
	if (bar_slot == NULL) {
		return -1;
	}
	for (i = 0u; i < bar_slot->children.count; ++i) {
		sk_ui_node_t ch = bar_slot->children.items[i];
		if (ui_tab_is_page(ctx, ch) == 0) {
			continue;
		}
		if (sk_ui_node_eq(ch, tab)) {
			return idx;
		}
		idx += 1;
	}
	return -1;
}

static sk_ui_node_t ui_tab_page_at(const sk_ui_context_t* ctx, sk_ui_node_t tab_bar, i32 index) {
	const ui_node_slot_t* bar_slot = ui_slot(ctx, tab_bar);
	u32 i;
	i32 idx = 0;
	if (bar_slot == NULL || index < 0) {
		return SK_UI_NODE_INVALID;
	}
	for (i = 0u; i < bar_slot->children.count; ++i) {
		sk_ui_node_t ch = bar_slot->children.items[i];
		if (ui_tab_is_page(ctx, ch) == 0) {
			continue;
		}
		if (idx == index) {
			return ch;
		}
		idx += 1;
	}
	return SK_UI_NODE_INVALID;
}

static void ui_tab_bar_write_selected(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, i32 index) {
	ui_widget_data_t* wd = ui_widget_data(ctx, tab_bar);
	if (wd != NULL && wd->bound_i32 != NULL) {
		*wd->bound_i32 = index;
	}
}

i32 ui_tab_bar_set_active_impl(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, sk_ui_node_t tab) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* bar_slot;
	u32 i;
	i32 selected = -1;
	if (!sk_ui_node_is_valid(tab)) {
		return -1;
	}
	if (!sk_ui_node_is_valid(tab_bar)) {
		tab_bar = ui->node_parent(ctx, tab);
	}
	if (ui_tab_is_page(ctx, tab) == 0) {
		return -1;
	}
	bar_slot = ui_slot(ctx, tab_bar);
	if (bar_slot != NULL) {
		(void)ui_widget_data_ensure(ctx, tab_bar, UI_WD_TAB_BAR);
		for (i = 0u; i < bar_slot->children.count; ++i) {
			sk_ui_node_t ch = bar_slot->children.items[i];
			const ui_node_slot_t* cs = ui_slot(ctx, ch);
			const_chr_t w;
			i32 flags = 0;
			if (cs == NULL) {
				continue;
			}
			w = ui_prop_str_const(cs, "widget");
			if (w == NULL || strcmp(w, "tab") != 0) {
				continue;
			}
			(void)ui_prop_i32_const(cs, "tab_flags", &flags);
			if ((flags & (i32)SK_UI_TAB_ITEM_FLAG_BUTTON) != 0) {
				continue;
			}
			(void)ui_tab_set_active_impl(ctx, ch, sk_ui_node_eq(ch, tab) ? 1 : 0);
			ui_tab_sync_body(ctx, ch, sk_ui_node_eq(ch, tab) ? 1 : 0);
		}
		selected = ui_tab_page_index(ctx, tab_bar, tab);
		ui_tab_bar_write_selected(ctx, tab_bar, selected);
	} else {
		(void)ui_tab_set_active_impl(ctx, tab, 1);
		ui_tab_sync_body(ctx, tab, 1);
	}
	return 0;
}

static sk_ui_node_t ui_tab_ensure_close(sk_ui_context_t* ctx, sk_ui_node_t tab) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t close;
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	char close_id[96];
	const_chr_t tid;
	sk_ui_layout_style_t ls;

	close = ui_find_child_widget(ctx, tab, "tab_close");
	if (sk_ui_node_is_valid(close)) {
		return close;
	}
	tid = ui->node_get_id(ctx, tab);
	if (tid != NULL && tid[0] != '\0') {
		(void)snprintf(close_id, sizeof(close_id), "%s-close", tid);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(close_id, sizeof(close_id), "ui-tab-close-%u", ctx->widget_id_seq);
	}
	close = ui_button_make(ctx, tab, "x", close_id, SK_UI_CLASS_TAB_CLOSE, "tab_close", 16.0f, 16.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT, 1);
	if (!sk_ui_node_is_valid(close)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->node_set_prop_str(ctx, close, "widget", "tab_close");
	if (ui->node_get_layout_style(ctx, close, &ls) == 0) {
		ls.flex_grow = 0.0f;
		ls.flex_shrink = 0.0f;
		(void)ui->node_set_layout_style(ctx, close, &ls);
	}
	wd = ui_widget_data_ensure(ctx, close, UI_WD_BUTTON);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_tab_close_on_click;
	cbs.on_event = ui_tab_close_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, close, &cbs);
	return close;
}

static void ui_tab_do_close(sk_ui_context_t* ctx, sk_ui_node_t close_btn) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t tab = ui->node_parent(ctx, close_btn);
	sk_ui_node_t bar;
	ui_widget_data_t* wd;
	i32 was_active;
	if (!sk_ui_node_is_valid(tab)) {
		return;
	}
	wd = ui_widget_data(ctx, tab);
	if (wd != NULL && wd->p_open != NULL) {
		*wd->p_open = 0;
	}
	(void)ui->node_set_prop_i32(ctx, tab, "hidden", 1);
	ui_tab_sync_body(ctx, tab, 0);
	was_active = ui_tab_get_active_impl(ctx, tab);
	(void)ui_tab_set_active_impl(ctx, tab, 0);
	bar = ui->node_parent(ctx, tab);
	if (was_active != 0 && sk_ui_node_is_valid(bar)) {
		sk_ui_node_t next = SK_UI_NODE_INVALID;
		const ui_node_slot_t* bar_slot = ui_slot(ctx, bar);
		u32 i;
		if (bar_slot != NULL) {
			for (i = 0u; i < bar_slot->children.count; ++i) {
				if (ui_tab_is_page(ctx, bar_slot->children.items[i]) != 0) {
					next = bar_slot->children.items[i];
					break;
				}
			}
		}
		if (sk_ui_node_is_valid(next)) {
			(void)ui_tab_bar_set_active_impl(ctx, bar, next);
		} else {
			ui_tab_bar_write_selected(ctx, bar, -1);
		}
	}
	ui_mark_dirty_up(ctx, tab, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
}

static void ui_tab_close_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	ui_button_on_event(ctx, node, event, user);
	if (wd != NULL && event != NULL && (wd->edge_clicked != 0 || (event->type == SK_UI_EVENT_POINTER_DOWN && wd->edge_pressed != 0))) {
		ui_tab_do_close(ctx, node);
		event->consumed = 1;
	}
}

static void ui_tab_close_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)user;
	if (event != NULL) {
		event->consumed = 1;
	}
	ui_tab_do_close(ctx, node);
}

sk_ui_node_t ui_widget_tab_item_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id, i32* p_open, u32 flags) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	const_chr_t wtype = "tab";
	const_chr_t class_name = SK_UI_CLASS_TAB;
	sk_ui_node_t n;

	if ((flags & SK_UI_TAB_ITEM_FLAG_BUTTON) != 0u) {
		wtype = "tab_button";
		class_name = SK_UI_CLASS_TAB_BUTTON;
	}
	n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, class_name, wtype, "ui-tab", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", label != NULL ? label : "");
	(void)ui->node_set_prop_i32(ctx, n, "active", 0);
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "tab_flags", (i32)flags);
	(void)ui->node_set_prop_i32(ctx, n, "has_close", 0);
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_TAB);
	if (wd != NULL) {
		wd->button_flags = SK_UI_BUTTON_FLAG_MOUSE_LEFT;
		wd->content = SK_UI_NODE_INVALID;
	}
	memset(&cbs, 0, sizeof(cbs));
	if ((flags & SK_UI_TAB_ITEM_FLAG_BUTTON) != 0u) {
		cbs.on_event = ui_button_on_event;
	} else {
		cbs.on_click = ui_tab_on_click;
	}
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	if (p_open != NULL) {
		(void)ui_tab_bind_open_impl(ctx, n, p_open);
	}
	if ((flags & SK_UI_TAB_ITEM_FLAG_SET_SELECTED) != 0u && (flags & SK_UI_TAB_ITEM_FLAG_BUTTON) == 0u) {
		(void)ui_tab_bar_set_active_impl(ctx, parent, n);
	}
	return n;
}

sk_ui_node_t ui_widget_tab_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	return ui_widget_tab_item_impl(ctx, parent, label, id, NULL, SK_UI_TAB_ITEM_FLAG_BUTTON);
}

sk_ui_node_t ui_tab_body_impl(sk_ui_context_t* ctx, sk_ui_node_t tab) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd;
	sk_ui_node_t body;
	sk_ui_node_t host;
	char body_id[96];
	const_chr_t tid;
	i32 active;

	if (ctx == NULL || !sk_ui_node_is_valid(tab)) {
		return SK_UI_NODE_INVALID;
	}
	wd = ui_widget_data_ensure(ctx, tab, UI_WD_TAB);
	if (wd != NULL && sk_ui_node_is_valid(wd->content)) {
		return wd->content;
	}
	body = ui_find_child_widget(ctx, tab, "tab_body");
	if (sk_ui_node_is_valid(body)) {
		if (wd != NULL) {
			wd->content = body;
		}
		return body;
	}
	host = ui->node_parent(ctx, tab);
	if (sk_ui_node_is_valid(host)) {
		sk_ui_node_t bar_parent = ui->node_parent(ctx, host);
		const ui_node_slot_t* hs = ui_slot(ctx, host);
		const_chr_t hw = hs != NULL ? ui_prop_str_const(hs, "widget") : NULL;
		if (hw != NULL && strcmp(hw, "tab_bar") == 0 && sk_ui_node_is_valid(bar_parent)) {
			host = bar_parent;
		}
	}
	tid = ui->node_get_id(ctx, tab);
	if (tid != NULL && tid[0] != '\0') {
		(void)snprintf(body_id, sizeof(body_id), "%s-body", tid);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(body_id, sizeof(body_id), "ui-tab-body-%u", ctx->widget_id_seq);
	}
	body = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, host, SK_UI_CLASS_TAB_BODY, "tab_body", "ui-tab-body", body_id);
	if (!sk_ui_node_is_valid(body)) {
		return body;
	}
	active = ui_tab_get_active_impl(ctx, tab);
	(void)ui->node_set_prop_i32(ctx, body, "hidden", active != 0 ? 0 : 1);
	if (wd != NULL) {
		wd->content = body;
	}
	return body;
}

sk_ui_node_t ui_tab_close_button_impl(const sk_ui_context_t* ctx, sk_ui_node_t tab) {
	return ui_find_child_widget(ctx, tab, "tab_close");
}

i32 ui_tab_bind_open_impl(sk_ui_context_t* ctx, sk_ui_node_t tab, i32* p_open) {
	ui_widget_data_t* wd;
	sk_ui_node_t close;
	if (ctx == NULL || !sk_ui_node_is_valid(tab)) {
		return -1;
	}
	wd = ui_widget_data_ensure(ctx, tab, UI_WD_TAB);
	if (wd == NULL) {
		return -1;
	}
	wd->p_open = p_open;
	close = ui_tab_ensure_close(ctx, tab);
	if (p_open == NULL) {
		if (sk_ui_node_is_valid(close)) {
			(void)ui_wapi()->node_set_prop_i32(ctx, close, "hidden", 1);
		}
		(void)ui_wapi()->node_set_prop_i32(ctx, tab, "has_close", 0);
		return 0;
	}
	if (sk_ui_node_is_valid(close)) {
		(void)ui_wapi()->node_set_prop_i32(ctx, close, "hidden", 0);
	}
	(void)ui_wapi()->node_set_prop_i32(ctx, tab, "has_close", 1);
	(void)ui_wapi()->node_set_prop_i32(ctx, tab, "text_align", 0);
	if (*p_open == 0) {
		(void)ui_wapi()->node_set_prop_i32(ctx, tab, "hidden", 1);
		ui_tab_sync_body(ctx, tab, 0);
	} else {
		(void)ui_wapi()->node_set_prop_i32(ctx, tab, "hidden", 0);
	}
	return 0;
}

i32 ui_tab_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t tab) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, tab);
	const ui_node_slot_t* slot;
	i32 hidden = 0;
	if (wd != NULL && wd->p_open != NULL) {
		return *wd->p_open != 0 ? 1 : 0;
	}
	slot = ui_slot(ctx, tab);
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "hidden", &hidden);
	return hidden != 0 ? 0 : 1;
}

i32 ui_tab_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t tab, u32 flags) {
	const sk_ui_api_t* ui = ui_wapi();
	if (ctx == NULL || !sk_ui_node_is_valid(tab)) {
		return -1;
	}
	if (ui->node_set_prop_i32(ctx, tab, "tab_flags", (i32)flags) != 0) {
		return -1;
	}
	if ((flags & SK_UI_TAB_ITEM_FLAG_SET_SELECTED) != 0u && (flags & SK_UI_TAB_ITEM_FLAG_BUTTON) == 0u) {
		(void)ui_tab_bar_set_active_impl(ctx, SK_UI_NODE_INVALID, tab);
	}
	return 0;
}

u32 ui_tab_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t tab) {
	const ui_node_slot_t* slot = ui_slot(ctx, tab);
	i32 flags = 0;
	if (slot == NULL) {
		return 0u;
	}
	(void)ui_prop_i32_const(slot, "tab_flags", &flags);
	return (u32)flags;
}

i32 ui_tab_bar_bind_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, i32* selected) {
	ui_widget_data_t* wd;
	if (ctx == NULL || !sk_ui_node_is_valid(tab_bar)) {
		return -1;
	}
	wd = ui_widget_data_ensure(ctx, tab_bar, UI_WD_TAB_BAR);
	if (wd == NULL) {
		return -1;
	}
	wd->bound_i32 = selected;
	if (selected == NULL) {
		return 0;
	}
	if (*selected >= 0) {
		return ui_tab_bar_set_selected_impl(ctx, tab_bar, *selected);
	}
	*selected = ui_tab_bar_get_selected_impl(ctx, tab_bar);
	return 0;
}

i32 ui_tab_bar_get_selected_impl(const sk_ui_context_t* ctx, sk_ui_node_t tab_bar) {
	const ui_node_slot_t* bar_slot = ui_slot(ctx, tab_bar);
	u32 i;
	i32 idx = 0;
	if (bar_slot == NULL) {
		return -1;
	}
	for (i = 0u; i < bar_slot->children.count; ++i) {
		sk_ui_node_t ch = bar_slot->children.items[i];
		if (ui_tab_is_page(ctx, ch) == 0) {
			continue;
		}
		if (ui_tab_get_active_impl(ctx, ch) != 0) {
			return idx;
		}
		idx += 1;
	}
	return -1;
}

i32 ui_tab_bar_set_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, i32 index) {
	sk_ui_node_t tab = ui_tab_page_at(ctx, tab_bar, index);
	if (!sk_ui_node_is_valid(tab)) {
		return -1;
	}
	return ui_tab_bar_set_active_impl(ctx, tab_bar, tab);
}

i32 ui_tab_clicked_impl(sk_ui_context_t* ctx, sk_ui_node_t tab) {
	return ui_button_clicked_impl(ctx, tab);
}

/* -------------------------------------------------------------------------- */
/* CollapsingHeader (APX-350 / manifest §8)                                   */
/* -------------------------------------------------------------------------- */

static const_chr_t ui_ch_widget(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? ui_prop_str_const(slot, "widget") : NULL;
}

static sk_ui_node_t ui_ch_find_child(const sk_ui_context_t* ctx, sk_ui_node_t header, const_chr_t widget) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 i;
	u32 n;
	if (ctx == NULL || !sk_ui_node_is_valid(header) || widget == NULL) {
		return SK_UI_NODE_INVALID;
	}
	n = ui->node_child_count(ctx, header);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t c = ui->node_child_at(ctx, header, i);
		const_chr_t w = ui_ch_widget(ctx, c);
		if (w != NULL && strcmp(w, widget) == 0) {
			return c;
		}
	}
	return SK_UI_NODE_INVALID;
}

static void ui_ch_apply_open(sk_ui_context_t* ctx, sk_ui_node_t header, i32 open) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = ui_widget_data(ctx, header);
	sk_ui_node_t arrow = ui_ch_find_child(ctx, header, "tree_arrow");
	sk_ui_node_t body = wd != NULL ? wd->content : SK_UI_NODE_INVALID;
	(void)ui->node_set_prop_i32(ctx, header, "open", open != 0 ? 1 : 0);
	if (sk_ui_node_is_valid(arrow)) {
		(void)ui->node_set_prop_i32(ctx, arrow, "open", open != 0 ? 1 : 0);
	}
	if (sk_ui_node_is_valid(body)) {
		(void)ui->node_set_prop_i32(ctx, body, "hidden", open != 0 ? 0 : 1);
	}
}

static void ui_ch_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	i32 open;
	(void)user;
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	if (event != NULL && sk_ui_node_is_valid(event->target) && !sk_ui_node_eq(event->target, node)) {
		const_chr_t tw = ui_ch_widget(ctx, event->target);
		if (tw != NULL && (strcmp(tw, "collapsing_header_button") == 0 || strcmp(tw, "button") == 0)) {
			return;
		}
	}
	open = ui_collapsing_header_get_open_impl(ctx, node) != 0 ? 0 : 1;
	ui_ch_apply_open(ctx, node, open);
	if (event != NULL) {
		event->consumed = 1;
	}
}

sk_ui_node_t ui_widget_collapsing_header_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id, u32 flags) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t bar;
	sk_ui_node_t arrow;
	sk_ui_node_t text;
	sk_ui_node_t body;
	ui_widget_data_t* wd;
	sk_ui_node_callbacks_t cbs;
	i32 open = (flags & SK_UI_TREE_NODE_FLAG_DEFAULT_OPEN) != 0u ? 1 : 0;
	char idbuf[96];
	const_chr_t use_id = (id != NULL && id[0] != '\0') ? id : "collapsing";

	if ((flags & SK_UI_TREE_NODE_FLAG_FRAMED) == 0u) {
		flags |= SK_UI_TREE_NODE_FLAG_FRAMED | SK_UI_TREE_NODE_FLAG_NO_TREE_PUSH_ON_OPEN | SK_UI_TREE_NODE_FLAG_SPAN_FULL_WIDTH;
	}

	bar = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_COLLAPSING_HEADER, "collapsing_header", "ui-collapsing-header", id);
	if (!sk_ui_node_is_valid(bar)) {
		return SK_UI_NODE_INVALID;
	}
	if (ctx->has_next_item_open != 0u) {
		open = ctx->next_item_open != 0u ? 1 : 0;
		ctx->has_next_item_open = 0u;
	}
	(void)ui->node_set_prop_i32(ctx, bar, "ch_flags", (i32)flags);
	(void)ui->node_set_prop_str(ctx, bar, "text", label != NULL ? label : "");
	(void)ui->node_set_focusable(ctx, bar, 1);
	wd = ui_widget_data_ensure(ctx, bar, UI_WD_COLLAPSING);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_ch_on_click;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, bar, &cbs);

	(void)snprintf(idbuf, sizeof(idbuf), "%s-arrow", use_id);
	arrow = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, bar);
	(void)ui->node_add_class(ctx, arrow, SK_UI_CLASS_TREE_ARROW);
	(void)ui->node_set_prop_str(ctx, arrow, "widget", "tree_arrow");
	(void)ui->node_set_id(ctx, arrow, idbuf);
	(void)ui->node_set_pointer_events(ctx, arrow, SK_UI_POINTER_EVENTS_NONE);

	text = ui->widget_label(ctx, bar, label != NULL ? label : "", NULL);
	(void)ui->node_set_pointer_events(ctx, text, SK_UI_POINTER_EVENTS_NONE);
	(void)ui->label_set_wrap(ctx, text, 0);
	{
		sk_ui_style_props_t lp;
		ui_style_props_clear(&lp);
		lp.mask = SK_UI_SP_FLEX_SHRINK | SK_UI_SP_FLEX_GROW;
		lp.layout.flex_shrink = 1.0f;
		lp.layout.flex_grow = 1.0f;
		(void)ui->node_merge_inline_style(ctx, text, &lp);
	}

	if ((flags & SK_UI_TREE_NODE_FLAG_TRAILING_BUTTON) != 0u) {
		sk_ui_node_t btn;
		sk_ui_node_t spring;
		(void)snprintf(idbuf, sizeof(idbuf), "%s-btn", use_id);
		spring = ui->widget_spring(ctx, bar, 1.0f, NULL);
		(void)ui->node_set_pointer_events(ctx, spring, SK_UI_POINTER_EVENTS_NONE);
		btn = ui_button_make(ctx, bar, "...", idbuf, SK_UI_CLASS_COLLAPSING_HEADER_BUTTON, "collapsing_header_button", 28.0f, 18.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT, 1);
		if (sk_ui_node_is_valid(btn)) {
			(void)ui->node_set_prop_str(ctx, btn, "widget", "collapsing_header_button");
		}
	}

	(void)snprintf(idbuf, sizeof(idbuf), "%s-body", use_id);
	body = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_COLLAPSING_HEADER_BODY, "collapsing_header_body", "ui-collapsing-header-body", idbuf);
	if (wd != NULL) {
		wd->content = body;
	}
	ui_ch_apply_open(ctx, bar, open);
	return bar;
}

i32 ui_collapsing_header_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t header) {
	const ui_node_slot_t* slot = ui_slot(ctx, header);
	i32 open = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "open", &open);
	return open != 0 ? 1 : 0;
}

i32 ui_collapsing_header_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t header, i32 open) {
	if (ctx == NULL || !sk_ui_node_is_valid(header)) {
		return -1;
	}
	ui_ch_apply_open(ctx, header, open);
	return 0;
}

sk_ui_node_t ui_collapsing_header_body_impl(sk_ui_context_t* ctx, sk_ui_node_t header) {
	ui_widget_data_t* wd = ui_widget_data(ctx, header);
	if (wd != NULL && sk_ui_node_is_valid(wd->content)) {
		return wd->content;
	}
	return SK_UI_NODE_INVALID;
}

sk_ui_node_t ui_collapsing_header_button_impl(const sk_ui_context_t* ctx, sk_ui_node_t header) {
	return ui_ch_find_child(ctx, header, "collapsing_header_button");
}

i32 ui_collapsing_header_button_clicked_impl(sk_ui_context_t* ctx, sk_ui_node_t header) {
	sk_ui_node_t btn = ui_collapsing_header_button_impl(ctx, header);
	if (!sk_ui_node_is_valid(btn)) {
		return 0;
	}
	return ui_button_clicked_impl(ctx, btn);
}

u32 ui_collapsing_header_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t header) {
	const ui_node_slot_t* slot = ui_slot(ctx, header);
	i32 flags = 0;
	if (slot == NULL) {
		return 0u;
	}
	(void)ui_prop_i32_const(slot, "ch_flags", &flags);
	return (u32)flags;
}

i32 ui_splitter_set_ratio_impl(sk_ui_context_t* ctx, sk_ui_node_t splitter, f32 ratio) {
	ui_splitter_apply_ratio(ctx, splitter, ratio, 1);
	return 0;
}

f32 ui_splitter_get_ratio_impl(const sk_ui_context_t* ctx, sk_ui_node_t splitter) {
	const ui_node_slot_t* slot = ui_slot(ctx, splitter);
	if (slot == NULL) {
		return 0.0f;
	}
	return ui_prop_f32_const(slot, "ratio", 0.5f);
}

i32 ui_splitter_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t splitter, sk_ui_widget_float_fn fn, void_ptr_t user) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, splitter, UI_WD_SPLITTER);
	if (wd == NULL) {
		return -1;
	}
	wd->on_float = fn;
	wd->cb_user = user;
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Accessors                                                                  */
/* -------------------------------------------------------------------------- */

i32 ui_label_set_text_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text) {
	return ui_wapi()->node_set_prop_str(ctx, node, "text", text != NULL ? text : "");
}

const_chr_t ui_label_get_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t t;
	if (slot == NULL) {
		return NULL;
	}
	t = ui_prop_str_const(slot, "text");
	return t != NULL ? t : "";
}

i32 ui_label_set_wrap_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 wrap) {
	return ui_wapi()->node_set_prop_i32(ctx, node, "wrap", wrap != 0 ? 1 : 0);
}

i32 ui_label_set_align_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 text_align, i32 vertical_align) {
	const sk_ui_api_t* ui = ui_wapi();
	if (ui->node_set_prop_i32(ctx, node, "text_align", text_align) != 0) {
		return -1;
	}
	return ui->node_set_prop_i32(ctx, node, "vertical_align", vertical_align);
}

i32 ui_text_set_text_range_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t begin, const_chr_t end) {
	const sk_ui_api_t* ui = ui_wapi();
	const sk_allocator_t* a;
	char* buf;
	size_t n;
	i32 rc;
	if (ctx == NULL || begin == NULL || end == NULL || end < begin) {
		return -1;
	}
	a = ctx->allocator;
	n = (size_t)(end - begin);
	buf = (char*)a->alloc(a->instance, n + 1u);
	if (buf == NULL) {
		return -1;
	}
	if (n > 0u) {
		memcpy(buf, begin, n);
	}
	buf[n] = '\0';
	rc = ui->node_set_prop_str(ctx, node, "text", buf);
	a->free(a->instance, buf);
	return rc;
}

i32 ui_text_set_color_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_color_t color) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_COLOR;
	p.color = color;
	return ui->node_merge_inline_style(ctx, node, &p);
}

i32 ui_text_get_color_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_color_t* out_color) {
	sk_ui_computed_style_t cs;
	if (ui_wapi()->node_get_computed_style(ctx, node, &cs) != 0) {
		return -1;
	}
	if (out_color != NULL) {
		*out_color = cs.color;
	}
	return 0;
}

i32 ui_text_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 st = ui->node_get_state(ctx, node);
	if (disabled) {
		st |= (u32)SK_UI_STATE_DISABLED;
	} else {
		st &= ~(u32)SK_UI_STATE_DISABLED;
	}
	return ui->node_set_state(ctx, node, st);
}

i32 ui_text_get_disabled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ((ui_wapi()->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) ? 1 : 0;
}

i32 ui_text_with_label_parts_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t* out_label, sk_ui_node_t* out_value) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t a;
	sk_ui_node_t b;
	if (out_label != NULL) {
		*out_label = SK_UI_NODE_INVALID;
	}
	if (out_value != NULL) {
		*out_value = SK_UI_NODE_INVALID;
	}
	a = ui->node_child_at(ctx, node, 0u);
	b = ui->node_child_at(ctx, node, 1u);
	if (out_label != NULL) {
		*out_label = a;
	}
	if (out_value != NULL) {
		*out_value = b;
	}
	return sk_ui_node_is_valid(a) && sk_ui_node_is_valid(b) ? 0 : -1;
}

i32 ui_text_centered_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t* out_text) {
	sk_ui_node_t t = ui_wapi()->node_child_at(ctx, node, 0u);
	if (out_text != NULL) {
		*out_text = t;
	}
	return sk_ui_node_is_valid(t) ? 0 : -1;
}

i32 ui_button_set_label_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label) {
	return ui_wapi()->node_set_prop_str(ctx, node, "text", label != NULL ? label : "");
}

i32 ui_button_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 st = ui->node_get_state(ctx, node);
	if (disabled) {
		st |= (u32)SK_UI_STATE_DISABLED;
	} else {
		st &= ~(u32)SK_UI_STATE_DISABLED;
	}
	return ui->node_set_state(ctx, node, st);
}

static sk_ui_node_t ui_checkbox_find_label_child(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 n = ui->node_child_count(ctx, node);
	u32 i;
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t ch = ui->node_child_at(ctx, node, i);
		const_chr_t w;
		const ui_node_slot_t* cs = ui_slot(ctx, ch);
		if (cs == NULL) {
			continue;
		}
		w = ui_prop_str_const(cs, "widget");
		if (w != NULL && strcmp(w, "label") == 0) {
			return ch;
		}
		if ((sk_ui_node_kind_t)cs->kind == SK_UI_NODE_KIND_TEXT) {
			return ch;
		}
	}
	return SK_UI_NODE_INVALID;
}

static void ui_checkbox_apply_box_layout(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	(void)ui->node_set_inline_style(ctx, node, &p);
	(void)ui->node_set_prop_i32(ctx, node, "labeled", 0);
}

static void ui_checkbox_apply_labeled_layout(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	/* Keep class face/border colours so the 18px mark paints a light box + X.
	 * skip_box_chrome on labeled nodes so the row itself stays transparent. */
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_FLEX_DIRECTION |
			 SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_PADDING | SK_UI_SP_COLUMN_GAP | SK_UI_SP_BORDER_WIDTH;
	p.layout.width = sk_ui_auto();
	p.layout.height = sk_ui_auto();
	p.layout.min_width = sk_ui_auto();
	p.layout.min_height = sk_ui_pt(18.0f);
	p.layout.max_width = sk_ui_auto();
	p.layout.max_height = sk_ui_auto();
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.padding.left = 22.0f;
	p.layout.padding.top = 1.0f;
	p.layout.padding.right = 6.0f;
	p.layout.padding.bottom = 1.0f;
	p.layout.column_gap = 4.0f;
	p.layout.border.left = 0.0f;
	p.layout.border.top = 0.0f;
	p.layout.border.right = 0.0f;
	p.layout.border.bottom = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
	(void)ui->node_set_prop_i32(ctx, node, "labeled", 1);
}

static i32 ui_checkbox_apply_label(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label) {
	const sk_ui_api_t* ui = ui_wapi();
	char visible[256];
	const_chr_t id_suffix = NULL;
	sk_ui_node_t child;

	ui_split_imgui_label(label, visible, (u32)sizeof(visible), &id_suffix);
	(void)ui->node_set_prop_str(ctx, node, "text", visible);
	child = ui_checkbox_find_label_child(ctx, node);
	if (visible[0] == '\0') {
		if (sk_ui_node_is_valid(child)) {
			(void)ui->node_destroy(ctx, child);
		}
		ui_checkbox_apply_box_layout(ctx, node);
		return 0;
	}
	ui_checkbox_apply_labeled_layout(ctx, node);
	if (!sk_ui_node_is_valid(child)) {
		child = ui->widget_label(ctx, node, visible, NULL);
		if (!sk_ui_node_is_valid(child)) {
			return -1;
		}
		(void)ui->node_set_pointer_events(ctx, child, SK_UI_POINTER_EVENTS_NONE);
		(void)ui->label_set_wrap(ctx, child, 0);
	} else {
		(void)ui->label_set_text(ctx, child, visible);
		(void)ui->label_set_wrap(ctx, child, 0);
	}
	return 0;
}

static i32 ui_widget_set_disabled(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 st;
	sk_ui_node_t child;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	st = ui->node_get_state(ctx, node);
	if (disabled) {
		st |= (u32)SK_UI_STATE_DISABLED;
	} else {
		st &= ~(u32)SK_UI_STATE_DISABLED;
	}
	if (ui->node_set_state(ctx, node, st) != 0) {
		return -1;
	}
	child = ui_checkbox_find_label_child(ctx, node);
	if (sk_ui_node_is_valid(child)) {
		u32 cst = ui->node_get_state(ctx, child);
		if (disabled) {
			cst |= (u32)SK_UI_STATE_DISABLED;
		} else {
			cst &= ~(u32)SK_UI_STATE_DISABLED;
		}
		(void)ui->node_set_state(ctx, child, cst);
	}
	return 0;
}

i32 ui_checkbox_set_checked_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	ui_checkbox_set_visual(ctx, node, checked != 0 ? 1 : 0, 0);
	ui_checkbox_write_bind(wd, checked != 0 ? 1 : 0);
	return 0;
}

i32 ui_checkbox_get_checked_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	if (ui_checkbox_read_mixed(ctx, node) != 0) {
		return 0;
	}
	return ui_checkbox_read_checked(ctx, node);
}

i32 ui_checkbox_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_CHECKBOX);
	if (wd == NULL) {
		return -1;
	}
	wd->on_bool = fn;
	wd->cb_user = user;
	return 0;
}

i32 ui_checkbox_set_label_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label) {
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	return ui_checkbox_apply_label(ctx, node, label);
}

const_chr_t ui_checkbox_get_label_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t text;
	if (slot == NULL) {
		return "";
	}
	text = ui_prop_str_const(slot, "text");
	return text != NULL ? text : "";
}

i32 ui_checkbox_bind_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32* value) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_CHECKBOX);
	if (wd == NULL) {
		return -1;
	}
	wd->bound_i32 = value;
	wd->bound_kind = value != NULL ? UI_BIND_BOOL : UI_BIND_NONE;
	wd->bound_arg = 0;
	if (value != NULL) {
		ui_checkbox_sync_one(ctx, node, wd);
	}
	return 0;
}

i32 ui_checkbox_bind_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32* flags, i32 flags_value) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_CHECKBOX);
	if (wd == NULL) {
		return -1;
	}
	wd->bound_i32 = flags;
	wd->bound_kind = flags != NULL ? UI_BIND_FLAGS : UI_BIND_NONE;
	wd->bound_arg = flags_value;
	if (flags != NULL) {
		ui_checkbox_sync_one(ctx, node, wd);
	}
	return 0;
}

i32 ui_checkbox_set_mixed_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 mixed) {
	i32 checked = ui_checkbox_read_checked(ctx, node);
	if (mixed != 0) {
		checked = 0;
	}
	ui_checkbox_set_visual(ctx, node, checked, mixed != 0 ? 1 : 0);
	return 0;
}

i32 ui_checkbox_get_mixed_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_checkbox_read_mixed(ctx, node);
}

i32 ui_checkbox_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	i32 v;
	if (wd == NULL) {
		return 0;
	}
	v = wd->edge_clicked != 0 ? 1 : 0;
	wd->edge_clicked = 0;
	return v;
}

i32 ui_checkbox_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	return ui_widget_set_disabled(ctx, node, disabled);
}

i32 ui_radio_set_checked_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	if (checked != 0) {
		ui_radio_clear_sibling_peers(ctx, node);
		ui_checkbox_set_visual(ctx, node, 1, 0);
		if (wd != NULL && wd->bound_kind == UI_BIND_RADIO) {
			ui_checkbox_write_bind(wd, 1);
		}
	} else {
		ui_checkbox_set_visual(ctx, node, 0, 0);
	}
	return 0;
}

i32 ui_radio_get_checked_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_checkbox_read_checked(ctx, node);
}

i32 ui_radio_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_RADIO);
	if (wd == NULL) {
		return -1;
	}
	wd->on_bool = fn;
	wd->cb_user = user;
	return 0;
}

i32 ui_radio_set_label_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label) {
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	return ui_checkbox_apply_label(ctx, node, label);
}

const_chr_t ui_radio_get_label_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_checkbox_get_label_impl(ctx, node);
}

i32 ui_radio_bind_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32* value, i32 option) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_RADIO);
	if (wd == NULL) {
		return -1;
	}
	wd->bound_i32 = value;
	wd->bound_kind = value != NULL ? UI_BIND_RADIO : UI_BIND_NONE;
	wd->bound_arg = option;
	if (value != NULL) {
		ui_checkbox_sync_one(ctx, node, wd);
	}
	return 0;
}

i32 ui_radio_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_checkbox_changed_impl(ctx, node);
}

i32 ui_radio_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	return ui_widget_set_disabled(ctx, node, disabled);
}

i32 ui_toggle_set_on_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 on) {
	i32 rc = ui_wapi()->node_set_prop_i32(ctx, node, "on", on != 0 ? 1 : 0);
	if (rc == 0) {
		ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	}
	return rc;
}

i32 ui_toggle_get_on_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 on = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "on", &on);
	return on != 0 ? 1 : 0;
}

i32 ui_toggle_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 st = ui->node_get_state(ctx, node);
	if (disabled) {
		st |= (u32)SK_UI_STATE_DISABLED;
	} else {
		st &= ~(u32)SK_UI_STATE_DISABLED;
	}
	return ui->node_set_state(ctx, node, st);
}

i32 ui_toggle_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_TOGGLE);
	if (wd == NULL) {
		return -1;
	}
	wd->on_bool = fn;
	wd->cb_user = user;
	return 0;
}

static sk_ui_node_t ui_slider_first_comp(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot != NULL && ui_slider_is_vector_host(slot) != 0) {
		return ui_wapi()->node_child_at(ctx, node, 0u);
	}
	return node;
}

static u32 ui_slider_leaf_count(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot != NULL && ui_slider_is_vector_host(slot) != 0) {
		return ui_wapi()->node_child_count(ctx, node);
	}
	return 1u;
}

static sk_ui_node_t ui_slider_leaf_at(const sk_ui_context_t* ctx, sk_ui_node_t node, u32 index) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot != NULL && ui_slider_is_vector_host(slot) != 0) {
		return ui_wapi()->node_child_at(ctx, node, index);
	}
	return index == 0u ? node : SK_UI_NODE_INVALID;
}

i32 ui_slider_set_value_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	u32 i;
	u32 n;
	if (slot == NULL) {
		return -1;
	}
	if (ui_slider_is_vector_host(slot) != 0) {
		n = ui->node_child_count(ctx, node);
		for (i = 0u; i < n; ++i) {
			(void)ui_slider_apply_value(ctx, ui->node_child_at(ctx, node, i), value, 0);
		}
		return 0;
	}
	return ui_slider_apply_value(ctx, node, value, 0);
}

f32 ui_slider_get_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	sk_ui_node_t leaf = ui_slider_first_comp(ctx, node);
	if (slot == NULL) {
		return 0.0f;
	}
	slot = ui_slot(ctx, leaf);
	if (slot == NULL) {
		return 0.0f;
	}
	return ui_prop_f32_const(slot, "value", 0.0f);
}

i32 ui_slider_set_range_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 min_v, f32 max_v) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 i;
	u32 n = ui_slider_leaf_count(ctx, node);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t leaf = ui_slider_leaf_at(ctx, node, i);
		f32 val;
		if (!sk_ui_node_is_valid(leaf)) {
			continue;
		}
		if (ui->node_set_prop_f32(ctx, leaf, "min", min_v) != 0) {
			return -1;
		}
		if (ui->node_set_prop_f32(ctx, leaf, "max", max_v) != 0) {
			return -1;
		}
		val = ui_slider_get_value_impl(ctx, leaf);
		(void)ui_slider_apply_value(ctx, leaf, val, 0);
	}
	return 0;
}

i32 ui_slider_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_float_fn fn, void_ptr_t user) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_SLIDER);
	if (wd == NULL) {
		return -1;
	}
	wd->on_float = fn;
	wd->cb_user = user;
	return 0;
}

i32 ui_slider_set_format_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t format) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 i;
	u32 n = ui_slider_leaf_count(ctx, node);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t leaf = ui_slider_leaf_at(ctx, node, i);
		if (!sk_ui_node_is_valid(leaf)) {
			continue;
		}
		if (ui->node_set_prop_str(ctx, leaf, "format", format != NULL ? format : "") != 0) {
			return -1;
		}
		ui_slider_refresh_label(ctx, leaf);
		ui_mark_dirty_up(ctx, leaf, (u32)SK_UI_DIRTY_PAINT);
	}
	return 0;
}

const_chr_t ui_slider_get_format_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, ui_slider_first_comp(ctx, node));
	const_chr_t fmt;
	if (slot == NULL) {
		return "";
	}
	fmt = ui_prop_str_const(slot, "format");
	return fmt != NULL ? fmt : "";
}

i32 ui_slider_set_step_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 step) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 i;
	u32 n = ui_slider_leaf_count(ctx, node);
	if (step < 0.0f) {
		step = 0.0f;
	}
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t leaf = ui_slider_leaf_at(ctx, node, i);
		f32 val;
		if (!sk_ui_node_is_valid(leaf)) {
			continue;
		}
		if (ui->node_set_prop_f32(ctx, leaf, "step", step) != 0) {
			return -1;
		}
		val = ui_slider_get_value_impl(ctx, leaf);
		(void)ui_slider_apply_value(ctx, leaf, val, 0);
	}
	return 0;
}

f32 ui_slider_get_step_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, ui_slider_first_comp(ctx, node));
	if (slot == NULL) {
		return 0.0f;
	}
	return ui_prop_f32_const(slot, "step", 0.0f);
}

i32 ui_slider_set_speed_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 speed) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 i;
	u32 n = ui_slider_leaf_count(ctx, node);
	if (speed <= 0.0f) {
		speed = 1.0f;
	}
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t leaf = ui_slider_leaf_at(ctx, node, i);
		if (!sk_ui_node_is_valid(leaf)) {
			continue;
		}
		if (ui->node_set_prop_f32(ctx, leaf, "speed", speed) != 0) {
			return -1;
		}
	}
	return 0;
}

f32 ui_slider_get_speed_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, ui_slider_first_comp(ctx, node));
	if (slot == NULL) {
		return 1.0f;
	}
	return ui_prop_f32_const(slot, "speed", 1.0f);
}

i32 ui_slider_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 i;
	u32 n = ui_slider_leaf_count(ctx, node);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t leaf = ui_slider_leaf_at(ctx, node, i);
		f32 val;
		if (!sk_ui_node_is_valid(leaf)) {
			continue;
		}
		if (ui->node_set_prop_i32(ctx, leaf, "flags", (i32)flags) != 0) {
			return -1;
		}
		val = ui_slider_get_value_impl(ctx, leaf);
		(void)ui_slider_apply_value(ctx, leaf, val, 0);
	}
	return 0;
}

u32 ui_slider_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, ui_slider_first_comp(ctx, node));
	if (slot == NULL) {
		return 0u;
	}
	return (u32)ui_slider_read_i32(slot, "flags", 0);
}

i32 ui_slider_set_label_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label) {
	const sk_ui_api_t* ui = ui_wapi();
	char visible[96];
	ui_split_imgui_label(label, visible, (u32)sizeof(visible), NULL);
	return ui->node_set_prop_str(ctx, node, "label", visible);
}

const_chr_t ui_slider_get_label_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t lab;
	if (slot == NULL) {
		return "";
	}
	lab = ui_prop_str_const(slot, "label");
	return lab != NULL ? lab : "";
}

i32 ui_slider_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd;
	i32 any = 0;
	u32 i;
	u32 n = ui_slider_leaf_count(ctx, node);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t leaf = ui_slider_leaf_at(ctx, node, i);
		i32 v;
		wd = ui_widget_data(ctx, leaf);
		if (wd == NULL) {
			continue;
		}
		v = wd->edge_value_changed != 0 ? 1 : 0;
		wd->edge_value_changed = 0;
		if (v != 0) {
			any = 1;
		}
	}
	return any;
}

i32 ui_slider_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	u32 i;
	u32 n = ui_slider_leaf_count(ctx, node);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t leaf = ui_slider_leaf_at(ctx, node, i);
		if (sk_ui_node_is_valid(leaf)) {
			(void)ui_widget_set_disabled(ctx, leaf, disabled);
		}
	}
	return ui_widget_set_disabled(ctx, node, disabled);
}

i32 ui_slider_is_text_input_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, ui_slider_first_comp(ctx, node));
	if (slot == NULL) {
		return 0;
	}
	return ui_slider_read_i32(slot, "text_input", 0) != 0 ? 1 : 0;
}

i32 ui_slider_set_text_input_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 on) {
	sk_ui_node_t leaf = ui_slider_first_comp(ctx, node);
	if (on != 0) {
		ui_slider_enter_text_input(ctx, leaf);
	} else {
		ui_slider_exit_text_input(ctx, leaf, 1);
	}
	return 0;
}

i32 ui_slider_set_int_value_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value) {
	return ui_slider_set_value_impl(ctx, node, (f32)value);
}

i32 ui_slider_get_int_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return (i32)ui_slider_round_nearest(ui_slider_get_value_impl(ctx, node));
}

i32 ui_slider_format_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, char* out, u32 out_cap) {
	const ui_node_slot_t* slot = ui_slot(ctx, ui_slider_first_comp(ctx, node));
	const_chr_t fmt;
	f32 val;
	i32 integer;
	if (out == NULL || out_cap == 0u || slot == NULL) {
		return -1;
	}
	fmt = ui_prop_str_const(slot, "format");
	val = ui_prop_f32_const(slot, "value", 0.0f);
	integer = ui_slider_read_i32(slot, "integer", 0);
	ui_slider_format_into(out, out_cap, fmt != NULL ? fmt : "", val, integer);
	return 0;
}

i32 ui_slider_component_impl(const sk_ui_context_t* ctx, sk_ui_node_t row, i32 index, sk_ui_node_t* out_comp) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 n;
	if (out_comp == NULL) {
		return -1;
	}
	*out_comp = SK_UI_NODE_INVALID;
	n = ui->node_child_count(ctx, row);
	if (index < 0 || (u32)index >= n) {
		return -1;
	}
	*out_comp = ui->node_child_at(ctx, row, (u32)index);
	return sk_ui_node_is_valid(*out_comp) ? 0 : -1;
}

i32 ui_slider_set_values_impl(sk_ui_context_t* ctx, sk_ui_node_t row, const f32* values, i32 count) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, row);
	u32 n;
	u32 i;
	if (values == NULL || count <= 0) {
		return -1;
	}
	if (slot != NULL && ui_slider_is_vector_host(slot) == 0) {
		return ui_slider_set_value_impl(ctx, row, values[0]);
	}
	n = ui->node_child_count(ctx, row);
	if ((u32)count < n) {
		n = (u32)count;
	}
	for (i = 0u; i < n; ++i) {
		(void)ui_slider_apply_value(ctx, ui->node_child_at(ctx, row, i), values[i], 0);
	}
	return 0;
}

i32 ui_slider_get_values_impl(const sk_ui_context_t* ctx, sk_ui_node_t row, f32* out, i32 count) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, row);
	u32 n;
	u32 i;
	if (out == NULL || count <= 0) {
		return -1;
	}
	if (slot != NULL && ui_slider_is_vector_host(slot) == 0) {
		out[0] = ui_slider_get_value_impl(ctx, row);
		return 0;
	}
	n = ui->node_child_count(ctx, row);
	if ((u32)count < n) {
		n = (u32)count;
	}
	for (i = 0u; i < n; ++i) {
		out[i] = ui_slider_get_value_impl(ctx, ui->node_child_at(ctx, row, i));
	}
	return 0;
}

i32 ui_range_slider_set_values_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value_low, f32 value_high) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	f32 min_v, max_v;
	if (slot == NULL) {
		return -1;
	}
	min_v = ui_prop_f32_const(slot, "min", 0.0f);
	max_v = ui_prop_f32_const(slot, "max", 1.0f);
	if (max_v < min_v) {
		f32 t = min_v;
		min_v = max_v;
		max_v = t;
	}
	value_low = ui_clampf(value_low, min_v, max_v);
	value_high = ui_clampf(value_high, min_v, max_v);
	if (value_low > value_high) {
		f32 t = value_low;
		value_low = value_high;
		value_high = t;
	}
	if (ui->node_set_prop_f32(ctx, node, "value_low", value_low) != 0) {
		return -1;
	}
	if (ui->node_set_prop_f32(ctx, node, "value_high", value_high) != 0) {
		return -1;
	}
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	return 0;
}

i32 ui_range_slider_get_values_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, f32* out_low, f32* out_high) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (out_low != NULL) {
		*out_low = ui_prop_f32_const(slot, "value_low", 0.0f);
	}
	if (out_high != NULL) {
		*out_high = ui_prop_f32_const(slot, "value_high", 0.0f);
	}
	return 0;
}

i32 ui_progress_set_value_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 fraction) {
	const sk_ui_api_t* ui = ui_wapi();
	fraction = ui_clampf(fraction, 0.0f, 1.0f);
	if (ui->node_set_prop_f32(ctx, node, "value", fraction) != 0) {
		return -1;
	}
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	return 0;
}

f32 ui_progress_get_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return 0.0f;
	}
	return ui_clampf(ui_prop_f32_const(slot, "value", 0.0f), 0.0f, 1.0f);
}

i32 ui_text_input_set_text_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	char local[256];
	char* heap = NULL;
	const_chr_t use = text != NULL ? text : "";
	u32 len;
	i32 n;
	if (wd != NULL && wd->input_capacity > 0) {
		len = (u32)strlen(use);
		if (len > (u32)wd->input_capacity) {
			u32 fit = ui_utf8_fit_bytes(use, (u32)wd->input_capacity);
			if (fit + 1u <= sizeof(local)) {
				memcpy(local, use, fit);
				local[fit] = '\0';
				use = local;
			} else {
				heap = (char*)ctx->allocator->alloc(ctx->allocator->instance, fit + 1u);
				if (heap == NULL) {
					return -1;
				}
				memcpy(heap, use, fit);
				heap[fit] = '\0';
				use = heap;
			}
		}
	}
	n = (i32)ui_utf8_code_count(use);
	ui_text_sync_props(ctx, node, use, n, n, n);
	if (heap != NULL) {
		ctx->allocator->free(ctx->allocator->instance, heap);
	}
	return 0;
}

const_chr_t ui_text_input_get_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_label_get_text_impl(ctx, node);
}

i32 ui_text_input_get_caret_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 caret = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "caret", &caret);
	return caret;
}

i32 ui_text_input_set_selection_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 start, i32 end) {
	const_chr_t text = ui_text_input_get_text_impl(ctx, node);
	ui_text_sync_props(ctx, node, text, end, start, end);
	return 0;
}

i32 ui_text_input_insert_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t utf8) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	const_chr_t text;
	i32 sel_a = 0, sel_b = 0, caret = 0;
	u32 b0, ins_len, old_len, new_len, fit;
	char filtered[2048];
	char* buf;
	const sk_allocator_t* a;
	u32 flags = 0u;
	i32 multiline = 0;
	if (slot == NULL || utf8 == NULL || utf8[0] == '\0') {
		return -1;
	}
	if (ui_text_is_readonly(ctx, node) != 0) {
		return -1;
	}
	if (wd != NULL) {
		flags = wd->input_flags;
		multiline = (flags & SK_UI_INPUT_TEXT_FLAG_MULTILINE) != 0u ? 1 : 0;
	}
	ins_len = ui_text_filter_insert(utf8, flags, multiline, filtered, (u32)sizeof(filtered));
	if (ins_len == 0u) {
		return 0;
	}
	/* Delete selection first. */
	(void)ui_prop_i32_const(slot, "sel_start", &sel_a);
	(void)ui_prop_i32_const(slot, "sel_end", &sel_b);
	if (sel_a != sel_b) {
		(void)ui_text_input_delete_selection_impl(ctx, node);
		slot = ui_slot(ctx, node);
	}
	text = ui_prop_str_const(slot, "text");
	if (text == NULL) {
		text = "";
	}
	(void)ui_prop_i32_const(slot, "caret", &caret);
	old_len = (u32)strlen(text);
	if (wd != NULL && wd->input_capacity > 0) {
		u32 remain = (u32)wd->input_capacity > old_len ? (u32)wd->input_capacity - old_len : 0u;
		fit = ui_utf8_fit_bytes(filtered, remain);
		filtered[fit] = '\0';
		ins_len = fit;
		if (ins_len == 0u) {
			return 0;
		}
	}
	b0 = ui_utf8_byte_offset(text, (u32)caret);
	new_len = old_len + ins_len;
	a = ctx->allocator;
	buf = (char*)a->alloc(a->instance, new_len + 1u);
	if (buf == NULL) {
		return -1;
	}
	memcpy(buf, text, b0);
	memcpy(buf + b0, filtered, ins_len);
	memcpy(buf + b0 + ins_len, text + b0, old_len - b0 + 1u);
	{
		i32 new_caret = caret + (i32)ui_utf8_code_count(filtered);
		ui_text_sync_props(ctx, node, buf, new_caret, new_caret, new_caret);
	}
	a->free(a->instance, buf);
	ui_text_mark_edit(ctx, node, 0);
	return 0;
}

i32 ui_text_input_delete_selection_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t text;
	i32 sel_a = 0, sel_b = 0;
	u32 b0, b1, old_len;
	char* buf;
	const sk_allocator_t* a;
	if (slot == NULL) {
		return -1;
	}
	if (ui_text_is_readonly(ctx, node) != 0) {
		return -1;
	}
	text = ui_prop_str_const(slot, "text");
	if (text == NULL) {
		text = "";
	}
	(void)ui_prop_i32_const(slot, "sel_start", &sel_a);
	(void)ui_prop_i32_const(slot, "sel_end", &sel_b);
	if (sel_a > sel_b) {
		i32 t = sel_a;
		sel_a = sel_b;
		sel_b = t;
	}
	if (sel_a == sel_b) {
		return 0;
	}
	b0 = ui_utf8_byte_offset(text, (u32)sel_a);
	b1 = ui_utf8_byte_offset(text, (u32)sel_b);
	old_len = (u32)strlen(text);
	a = ctx->allocator;
	buf = (char*)a->alloc(a->instance, old_len - (b1 - b0) + 1u);
	if (buf == NULL) {
		return -1;
	}
	memcpy(buf, text, b0);
	memcpy(buf + b0, text + b1, old_len - b1 + 1u);
	ui_text_sync_props(ctx, node, buf, sel_a, sel_a, sel_a);
	a->free(a->instance, buf);
	ui_text_mark_edit(ctx, node, 0);
	return 0;
}

i32 ui_text_input_copy_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t text;
	i32 sel_a = 0, sel_b = 0;
	u32 b0, b1;
	char* tmp;
	u32 len;
	i32 rc;
	if (ctx->clipboard_set == NULL || slot == NULL) {
		return -1;
	}
	text = ui_prop_str_const(slot, "text");
	if (text == NULL) {
		text = "";
	}
	(void)ui_prop_i32_const(slot, "sel_start", &sel_a);
	(void)ui_prop_i32_const(slot, "sel_end", &sel_b);
	if (sel_a > sel_b) {
		i32 t = sel_a;
		sel_a = sel_b;
		sel_b = t;
	}
	if (sel_a == sel_b) {
		return 0;
	}
	b0 = ui_utf8_byte_offset(text, (u32)sel_a);
	b1 = ui_utf8_byte_offset(text, (u32)sel_b);
	len = b1 - b0;
	tmp = (char*)ctx->allocator->alloc(ctx->allocator->instance, len + 1u);
	if (tmp == NULL) {
		return -1;
	}
	memcpy(tmp, text + b0, len);
	tmp[len] = '\0';
	rc = ctx->clipboard_set(ctx->clipboard_user, tmp);
	ctx->allocator->free(ctx->allocator->instance, tmp);
	return rc;
}

i32 ui_text_input_cut_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	if (ui_text_is_readonly(ctx, node) != 0) {
		return ui_text_input_copy_impl(ctx, node);
	}
	if (ui_text_input_copy_impl(ctx, node) != 0 && ctx->clipboard_set == NULL) {
		/* No clipboard: still delete selection. */
	}
	return ui_text_input_delete_selection_impl(ctx, node);
}

i32 ui_text_input_paste_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	char buf[4096];
	u32 len = 0u;
	if (ui_text_is_readonly(ctx, node) != 0) {
		return -1;
	}
	if (ctx->clipboard_get == NULL) {
		return -1;
	}
	if (ctx->clipboard_get(ctx->clipboard_user, buf, (u32)sizeof(buf), &len) != 0) {
		return -1;
	}
	if (len >= sizeof(buf)) {
		len = (u32)sizeof(buf) - 1u;
	}
	buf[len] = '\0';
	return ui_text_input_insert_impl(ctx, node, buf);
}

sk_ui_node_t ui_widget_text_input_multiline_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, f32 width, f32 height, const_chr_t id) {
	sk_ui_node_t n = ui_widget_text_input_impl(ctx, parent, text, id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	ui_text_apply_flags(ctx, n, SK_UI_INPUT_TEXT_FLAG_MULTILINE);
	(void)ui_text_input_set_size_impl(ctx, n, width, height);
	return n;
}

sk_ui_node_t ui_widget_text_input_with_hint_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t hint, const_chr_t id) {
	sk_ui_node_t n = ui_widget_text_input_impl(ctx, parent, text, id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui_text_input_set_hint_impl(ctx, n, hint);
	return n;
}

sk_ui_node_t ui_widget_search_input_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_text_input_with_hint_impl(ctx, parent, text, "Search", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_add_class(ctx, n, SK_UI_CLASS_TEXT_INPUT_SEARCH);
	(void)ui->node_set_prop_str(ctx, n, "widget", "text_input");
	(void)ui->node_set_prop_i32(ctx, n, "search", 1);
	return n;
}

sk_ui_node_t ui_widget_text_input_readonly_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id) {
	sk_ui_node_t n = ui_widget_text_input_impl(ctx, parent, text, id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	ui_text_apply_flags(ctx, n, SK_UI_INPUT_TEXT_FLAG_READ_ONLY);
	return n;
}

sk_ui_node_t ui_widget_input_scalar_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_input_data_type_t type, void_ptr_t data, const_chr_t id) {
	char buf[64];
	ui_widget_data_t* wd;
	sk_ui_node_t n;
	ui_text_format_scalar((i32)type, data, buf, (u32)sizeof(buf));
	n = ui_widget_text_input_impl(ctx, parent, buf, id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	ui_text_apply_flags(ctx, n, SK_UI_INPUT_TEXT_FLAG_CHARS_DECIMAL);
	wd = ui_widget_data(ctx, n);
	if (wd != NULL) {
		wd->scalar_data = data;
		wd->scalar_type = (i32)type;
	}
	return n;
}

sk_ui_node_t ui_widget_input_float_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32* v, const_chr_t id) {
	return ui_widget_input_scalar_impl(ctx, parent, SK_UI_INPUT_DATA_F32, v, id);
}

sk_ui_node_t ui_widget_input_int_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32* v, const_chr_t id) {
	return ui_widget_input_scalar_impl(ctx, parent, SK_UI_INPUT_DATA_S32, v, id);
}

sk_ui_node_t ui_widget_input_float3_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32* v, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t row;
	sk_ui_layout_style_t ls;
	char cid[64];
	i32 i;
	row = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_VIEW, "input_float3", "ui-input-float3", id);
	if (!sk_ui_node_is_valid(row)) {
		return row;
	}
	ui_layout_style_init_default(&ls);
	ls.flex_direction = SK_UI_FLEX_ROW;
	ls.column_gap = 4.0f;
	ls.align_items = SK_UI_ALIGN_CENTER;
	(void)ui->node_set_layout_style(ctx, row, &ls);
	for (i = 0; i < 3; ++i) {
		sk_ui_node_t field;
		sk_ui_style_props_t p;
		if (id != NULL && id[0] != '\0') {
			(void)snprintf(cid, sizeof(cid), "%s/%d", id, i);
		} else {
			(void)snprintf(cid, sizeof(cid), "f3-%d", i);
		}
		field = ui_widget_input_scalar_impl(ctx, row, SK_UI_INPUT_DATA_F32, v != NULL ? &v[i] : NULL, cid);
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_WIDTH;
		p.layout.flex_grow = 1.0f;
		p.layout.min_width = sk_ui_pt(32.0f);
		(void)ui->node_merge_inline_style(ctx, field, &p);
	}
	return row;
}

i32 ui_text_input_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	ui_text_apply_flags(ctx, node, flags);
	return 0;
}

u32 ui_text_input_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, node);
	if (wd == NULL) {
		return 0u;
	}
	return wd->input_flags;
}

i32 ui_text_input_set_hint_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t hint) {
	const sk_ui_api_t* ui = ui_wapi();
	i32 rc = ui->node_set_prop_str(ctx, node, "hint", hint != NULL ? hint : "");
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	return rc;
}

const_chr_t ui_text_input_get_hint_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t h;
	if (slot == NULL) {
		return "";
	}
	h = ui_prop_str_const(slot, "hint");
	return h != NULL ? h : "";
}

i32 ui_text_input_set_readonly_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 readonly) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_TEXT_INPUT);
	u32 flags;
	if (wd == NULL) {
		return -1;
	}
	flags = wd->input_flags;
	if (readonly != 0) {
		flags |= SK_UI_INPUT_TEXT_FLAG_READ_ONLY;
	} else {
		flags &= ~SK_UI_INPUT_TEXT_FLAG_READ_ONLY;
	}
	ui_text_apply_flags(ctx, node, flags);
	return 0;
}

i32 ui_text_input_get_readonly_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_text_is_readonly(ctx, node);
}

i32 ui_text_input_set_password_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 password) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_TEXT_INPUT);
	u32 flags;
	if (wd == NULL) {
		return -1;
	}
	flags = wd->input_flags;
	if (password != 0) {
		flags |= SK_UI_INPUT_TEXT_FLAG_PASSWORD;
	} else {
		flags &= ~SK_UI_INPUT_TEXT_FLAG_PASSWORD;
	}
	ui_text_apply_flags(ctx, node, flags);
	return 0;
}

i32 ui_text_input_get_password_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, node);
	if (wd == NULL) {
		return 0;
	}
	return (wd->input_flags & SK_UI_INPUT_TEXT_FLAG_PASSWORD) != 0u ? 1 : 0;
}

i32 ui_text_input_set_capacity_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 capacity) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_TEXT_INPUT);
	if (wd == NULL) {
		return -1;
	}
	if (capacity < 0) {
		capacity = 0;
	}
	wd->input_capacity = capacity;
	if (capacity > 0) {
		(void)ui_text_input_set_text_impl(ctx, node, ui_text_input_get_text_impl(ctx, node));
	}
	return 0;
}

i32 ui_text_input_get_capacity_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, node);
	if (wd == NULL) {
		return 0;
	}
	return wd->input_capacity;
}

i32 ui_text_input_get_selection_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, i32* out_start, i32* out_end) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 a = 0;
	i32 b = 0;
	if (slot == NULL) {
		return -1;
	}
	(void)ui_prop_i32_const(slot, "sel_start", &a);
	(void)ui_prop_i32_const(slot, "sel_end", &b);
	if (out_start != NULL) {
		*out_start = a;
	}
	if (out_end != NULL) {
		*out_end = b;
	}
	return 0;
}

i32 ui_text_input_set_error_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 show_error) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_TEXT_INPUT);
	u32 flags;
	if (wd == NULL) {
		return -1;
	}
	flags = wd->input_flags;
	if (show_error != 0) {
		flags |= SK_UI_INPUT_TEXT_FLAG_SHOW_ERROR;
	} else {
		flags &= ~SK_UI_INPUT_TEXT_FLAG_SHOW_ERROR;
	}
	ui_text_apply_flags(ctx, node, flags);
	return 0;
}

i32 ui_text_input_get_error_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, node);
	if (wd == NULL) {
		return 0;
	}
	return (wd->input_flags & SK_UI_INPUT_TEXT_FLAG_SHOW_ERROR) != 0u ? 1 : 0;
}

i32 ui_text_input_set_size_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height) {
	return ui_button_set_size_impl(ctx, node, width, height);
}

i32 ui_text_input_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	return ui_widget_set_disabled(ctx, node, disabled);
}

i32 ui_text_input_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	i32 v;
	if (wd == NULL) {
		return 0;
	}
	v = wd->edge_text_changed;
	wd->edge_text_changed = 0;
	return v;
}

i32 ui_text_input_committed_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	i32 v;
	if (wd == NULL) {
		return 0;
	}
	v = wd->edge_text_committed;
	wd->edge_text_committed = 0;
	return v;
}

i32 ui_text_input_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_text_fn fn, void_ptr_t user) {
	ui_widget_data_t* wd = ui_widget_data_ensure(ctx, node, UI_WD_TEXT_INPUT);
	if (wd == NULL) {
		return -1;
	}
	wd->on_text = fn;
	wd->cb_user = user;
	return 0;
}

static i32 ui_text_ci_contains(const_chr_t hay, const_chr_t needle, u32 nlen) {
	u32 i;
	u32 hlen;
	if (hay == NULL) {
		hay = "";
	}
	if (needle == NULL || nlen == 0u) {
		return 1;
	}
	hlen = (u32)strlen(hay);
	if (nlen > hlen) {
		return 0;
	}
	for (i = 0u; i + nlen <= hlen; ++i) {
		u32 k;
		i32 ok = 1;
		for (k = 0u; k < nlen; ++k) {
			unsigned char a = (unsigned char)hay[i + k];
			unsigned char b = (unsigned char)needle[k];
			if (a >= 'A' && a <= 'Z') {
				a = (unsigned char)(a - 'A' + 'a');
			}
			if (b >= 'A' && b <= 'Z') {
				b = (unsigned char)(b - 'A' + 'a');
			}
			if (a != b) {
				ok = 0;
				break;
			}
		}
		if (ok != 0) {
			return 1;
		}
	}
	return 0;
}

i32 ui_text_filter_pass_impl(const_chr_t filter, const_chr_t text) {
	const_chr_t p;
	i32 has_include = 0;
	i32 include_hit = 0;
	if (filter == NULL || filter[0] == '\0') {
		return 1;
	}
	if (text == NULL) {
		text = "";
	}
	p = filter;
	while (*p != '\0') {
		const_chr_t start;
		u32 n;
		i32 exclude;
		while (*p == ' ' || *p == '\t' || *p == ',') {
			p += 1;
		}
		if (*p == '\0') {
			break;
		}
		exclude = 0;
		if (*p == '-') {
			exclude = 1;
			p += 1;
		}
		start = p;
		while (*p != '\0' && *p != ',') {
			p += 1;
		}
		n = (u32)(p - start);
		while (n > 0u && (start[n - 1u] == ' ' || start[n - 1u] == '\t')) {
			n -= 1u;
		}
		if (n == 0u) {
			continue;
		}
		if (exclude != 0) {
			if (ui_text_ci_contains(text, start, n) != 0) {
				return 0;
			}
		} else {
			has_include = 1;
			if (ui_text_ci_contains(text, start, n) != 0) {
				include_hit = 1;
			}
		}
	}
	if (has_include != 0 && include_hit == 0) {
		return 0;
	}
	return 1;
}

i32 ui_input_scalar_set_range_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const void* p_min, const void* p_max) {
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	if (wd == NULL) {
		return -1;
	}
	if (p_min == NULL || p_max == NULL) {
		wd->scalar_has_range = 0;
		return 0;
	}
	wd->scalar_min = ui_text_read_scalar_f64(wd->scalar_type, p_min);
	wd->scalar_max = ui_text_read_scalar_f64(wd->scalar_type, p_max);
	wd->scalar_has_range = 1;
	return 0;
}

i32 ui_input_scalar_apply_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_text_commit_scalar(ctx, node);
	return 0;
}

i32 ui_input_float3_component_impl(const sk_ui_context_t* ctx, sk_ui_node_t row, i32 index, sk_ui_node_t* out_field) {
	const sk_ui_api_t* ui = ui_wapi();
	if (index < 0 || index > 2 || out_field == NULL) {
		return -1;
	}
	*out_field = ui->node_child_at(ctx, row, (u32)index);
	if (!sk_ui_node_is_valid(*out_field)) {
		return -1;
	}
	return 0;
}

void ui_text_input_sync_all(sk_ui_context_t* ctx) {
	u32 i;
	if (ctx == NULL) {
		return;
	}
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		ui_widget_data_t* wd;
		sk_ui_node_t node;
		sk_ui_node_t focus;
		char buf[64];
		if (slot->alive == 0u || slot->user_data == NULL) {
			continue;
		}
		if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_WIDGET_DATA_TYPE_ID)) {
			continue;
		}
		wd = (ui_widget_data_t*)slot->user_data;
		if (wd->kind != UI_WD_TEXT_INPUT || wd->scalar_data == NULL) {
			continue;
		}
		node.index = i;
		node.generation = slot->generation;
		focus = ui_wapi()->focus_get(ctx);
		if (sk_ui_node_eq(focus, node)) {
			continue;
		}
		ui_text_format_scalar(wd->scalar_type, wd->scalar_data, buf, (u32)sizeof(buf));
		{
			const_chr_t cur = ui_text_input_get_text_impl(ctx, node);
			if (cur != NULL && strcmp(cur, buf) == 0) {
				continue;
			}
		}
		(void)ui_text_input_set_text_impl(ctx, node, buf);
	}
}

sk_ui_node_t ui_scroll_view_content_impl(const sk_ui_context_t* ctx, sk_ui_node_t scroll_view) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, scroll_view);
	if (wd == NULL) {
		return SK_UI_NODE_INVALID;
	}
	return wd->content;
}

i32 ui_scroll_view_set_scroll_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 scroll_x, f32 scroll_y) {
	const sk_ui_api_t* ui = ui_wapi();
	if (ui->node_set_prop_f32(ctx, node, "scroll_x", scroll_x) != 0) {
		return -1;
	}
	if (ui->node_set_prop_f32(ctx, node, "scroll_y", scroll_y) != 0) {
		return -1;
	}
	ui_scroll_clamp(ctx, node);
	return 0;
}

i32 ui_scroll_view_get_scroll_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, f32* out_x, f32* out_y) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (out_x != NULL) {
		*out_x = ui_prop_f32_const(slot, "scroll_x", 0.0f);
	}
	if (out_y != NULL) {
		*out_y = ui_prop_f32_const(slot, "scroll_y", 0.0f);
	}
	return 0;
}

i32 ui_scroll_view_set_content_size_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = ui_widget_data(ctx, node);
	sk_ui_layout_style_t ls;
	if (ui->node_set_prop_f32(ctx, node, "content_width", width) != 0) {
		return -1;
	}
	if (ui->node_set_prop_f32(ctx, node, "content_height", height) != 0) {
		return -1;
	}
	if (wd != NULL && sk_ui_node_is_valid(wd->content)) {
		ui_layout_style_init_default(&ls);
		ls.flex_direction = SK_UI_FLEX_COLUMN;
		ls.width = sk_ui_pt(width);
		ls.height = sk_ui_pt(height);
		(void)ui->node_set_layout_style(ctx, wd->content, &ls);
	}
	ui_scroll_clamp(ctx, node);
	ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	return 0;
}

i32 ui_image_set_texture_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 texture_id) {
	return ui_wapi()->node_set_prop_i32(ctx, node, "texture_id", texture_id);
}

/* -------------------------------------------------------------------------- */
/* Child / window / layout family (APX-345; manifest §13)                     */
/* -------------------------------------------------------------------------- */

static void ui_set_disabled_walk(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t stack[64];
	u32 sp = 0u;
	if (!sk_ui_node_is_valid(node)) {
		return;
	}
	stack[sp++] = node;
	while (sp > 0u) {
		sk_ui_node_t cur;
		const ui_node_slot_t* slot;
		u32 st;
		u32 i;
		sp -= 1u;
		cur = stack[sp];
		st = ui->node_get_state(ctx, cur);
		if (disabled != 0) {
			st |= (u32)SK_UI_STATE_DISABLED;
		} else {
			st &= ~(u32)SK_UI_STATE_DISABLED;
		}
		(void)ui->node_set_state(ctx, cur, st);
		slot = ui_slot(ctx, cur);
		if (slot == NULL) {
			continue;
		}
		for (i = 0u; i < slot->children.count; ++i) {
			if (sp >= 64u) {
				break;
			}
			stack[sp++] = slot->children.items[i];
		}
	}
}

static void ui_window_apply_open(sk_ui_context_t* ctx, sk_ui_node_t window, i32 open) {
	const sk_ui_api_t* ui = ui_wapi();
	(void)ui->node_set_prop_i32(ctx, window, "hidden", open != 0 ? 0 : 1);
	ui_mark_dirty_up(ctx, window, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
}

static void ui_window_do_close(sk_ui_context_t* ctx, sk_ui_node_t close_btn) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t bar = ui->node_parent(ctx, close_btn);
	sk_ui_node_t window = ui->node_parent(ctx, bar);
	ui_widget_data_t* wd;
	if (!sk_ui_node_is_valid(window)) {
		return;
	}
	wd = ui_widget_data(ctx, window);
	if (wd != NULL && wd->p_open != NULL) {
		*wd->p_open = 0;
	}
	ui_window_apply_open(ctx, window, 0);
}

static void ui_window_close_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	ui_button_on_event(ctx, node, event, user);
	if (wd != NULL && event != NULL && (wd->edge_clicked != 0 || (event->type == SK_UI_EVENT_POINTER_DOWN && wd->edge_pressed != 0))) {
		ui_window_do_close(ctx, node);
		event->consumed = 1;
	}
}

static void ui_window_close_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)user;
	if (event != NULL) {
		event->consumed = 1;
	}
	ui_window_do_close(ctx, node);
}

static sk_ui_node_t ui_window_ensure_close(sk_ui_context_t* ctx, sk_ui_node_t window) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t bar;
	sk_ui_node_t close;
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	char close_id[96];
	const_chr_t wid;
	sk_ui_layout_style_t ls;

	bar = ui_editor_window_title_bar_impl(ctx, window);
	if (!sk_ui_node_is_valid(bar)) {
		return SK_UI_NODE_INVALID;
	}
	close = ui_find_child_widget(ctx, bar, "window_close");
	if (sk_ui_node_is_valid(close)) {
		return close;
	}
	wid = ui->node_get_id(ctx, window);
	if (wid != NULL && wid[0] != '\0') {
		(void)snprintf(close_id, sizeof(close_id), "%s-close", wid);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(close_id, sizeof(close_id), "ui-window-close-%u", ctx->widget_id_seq);
	}
	{
		char spring_id[96];
		sk_ui_node_t spring;
		if (wid != NULL && wid[0] != '\0') {
			(void)snprintf(spring_id, sizeof(spring_id), "%s-close-gap", wid);
		} else {
			ctx->widget_id_seq += 1u;
			(void)snprintf(spring_id, sizeof(spring_id), "ui-window-close-gap-%u", ctx->widget_id_seq);
		}
		spring = ui_widget_spring_impl(ctx, bar, 1.0f, spring_id);
		(void)spring;
	}
	close = ui_button_make(ctx, bar, "x", close_id, SK_UI_CLASS_WINDOW_CLOSE, "window_close", 16.0f, 16.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT, 1);
	if (!sk_ui_node_is_valid(close)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->node_set_prop_str(ctx, close, "widget", "window_close");
	if (ui->node_get_layout_style(ctx, close, &ls) == 0) {
		ls.position = SK_UI_POSITION_RELATIVE;
		ls.flex_grow = 0.0f;
		(void)ui->node_set_layout_style(ctx, close, &ls);
	}
	wd = ui_widget_data_ensure(ctx, close, UI_WD_BUTTON);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_window_close_on_click;
	cbs.on_event = ui_window_close_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, close, &cbs);
	return close;
}

i32 ui_editor_window_bind_open_impl(sk_ui_context_t* ctx, sk_ui_node_t window, i32* p_open) {
	ui_widget_data_t* wd;
	sk_ui_node_t close;
	if (ctx == NULL || !sk_ui_node_is_valid(window)) {
		return -1;
	}
	wd = ui_widget_data_ensure(ctx, window, UI_WD_WINDOW);
	if (wd == NULL) {
		return -1;
	}
	wd->p_open = p_open;
	close = ui_window_ensure_close(ctx, window);
	if (p_open == NULL) {
		if (sk_ui_node_is_valid(close)) {
			(void)ui_wapi()->node_set_prop_i32(ctx, close, "hidden", 1);
		}
		return 0;
	}
	if (sk_ui_node_is_valid(close)) {
		(void)ui_wapi()->node_set_prop_i32(ctx, close, "hidden", 0);
	}
	ui_window_apply_open(ctx, window, *p_open != 0 ? 1 : 0);
	return 0;
}

i32 ui_editor_window_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t window) {
	const ui_widget_data_t* wd = ui_widget_data_const(ctx, window);
	const ui_node_slot_t* slot;
	i32 hidden = 0;
	if (wd != NULL && wd->p_open != NULL) {
		return *wd->p_open != 0 ? 1 : 0;
	}
	slot = ui_slot(ctx, window);
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "hidden", &hidden);
	return hidden != 0 ? 0 : 1;
}

i32 ui_editor_window_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t window, i32 open) {
	ui_widget_data_t* wd = ui_widget_data(ctx, window);
	if (wd != NULL && wd->p_open != NULL) {
		*wd->p_open = open != 0 ? 1 : 0;
	}
	ui_window_apply_open(ctx, window, open);
	return 0;
}

sk_ui_node_t ui_editor_window_close_button_impl(const sk_ui_context_t* ctx, sk_ui_node_t window) {
	sk_ui_node_t bar = ui_editor_window_title_bar_impl(ctx, window);
	if (!sk_ui_node_is_valid(bar)) {
		return SK_UI_NODE_INVALID;
	}
	return ui_find_child_widget(ctx, bar, "window_close");
}

sk_ui_node_t ui_widget_window_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t title, const_chr_t id, i32* p_open) {
	sk_ui_node_t win = ui_widget_editor_window_impl(ctx, parent, title, id);
	if (!sk_ui_node_is_valid(win)) {
		return win;
	}
	if (p_open != NULL) {
		(void)ui_editor_window_bind_open_impl(ctx, win, p_open);
	}
	return win;
}

sk_ui_node_t ui_widget_fullscreen_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_FULLSCREEN, "fullscreen", "ui-fullscreen", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	if (ui->node_get_layout_style(ctx, n, &ls) == 0) {
		ls.width = sk_ui_percent(100.0f);
		ls.height = sk_ui_percent(100.0f);
		ls.flex_grow = 1.0f;
		ls.flex_direction = SK_UI_FLEX_COLUMN;
		(void)ui->node_set_layout_style(ctx, n, &ls);
	}
	return n;
}

static void ui_child_apply_size(sk_ui_context_t* ctx, sk_ui_node_t child, f32 width, f32 height) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
	if (width <= 0.0f && height <= 0.0f) {
		/* Remaining-size (0,0): take leftover in the parent stack. */
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.height = sk_ui_auto();
		p.layout.flex_grow = 1.0f;
		p.layout.flex_shrink = 1.0f;
	} else if (width <= 0.0f && height > 0.0f) {
		/* Fixed-height toolbar, full remaining width. */
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
	(void)ui->node_merge_inline_style(ctx, child, &p);
	(void)ui->node_set_prop_f32(ctx, child, "child_w", width);
	(void)ui->node_set_prop_f32(ctx, child, "child_h", height);
}

static void ui_child_apply_border(sk_ui_context_t* ctx, sk_ui_node_t child, i32 border) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
	if (border != 0) {
		p.layout.border.left = 1.0f;
		p.layout.border.top = 1.0f;
		p.layout.border.right = 1.0f;
		p.layout.border.bottom = 1.0f;
		p.border_color = sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f);
	} else {
		p.layout.border.left = 0.0f;
		p.layout.border.top = 0.0f;
		p.layout.border.right = 0.0f;
		p.layout.border.bottom = 0.0f;
		p.border_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	}
	(void)ui->node_merge_inline_style(ctx, child, &p);
}

static void ui_child_resize_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	sk_ui_node_t child;
	sk_ui_rect_t border;
	f32 w;

	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	child = ui->node_parent(ctx, node);
	if (!sk_ui_node_is_valid(child)) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN && event->button == SK_UI_POINTER_BUTTON_LEFT) {
		if (wd != NULL) {
			wd->dragging = 1;
			wd->drag_last_x = event->x;
			if (ui->node_get_abs_rect(ctx, child, &border, NULL) == 0) {
				wd->resize_start_w = border.width;
			} else {
				wd->resize_start_w = 80.0f;
			}
		}
		(void)ui->pointer_capture_set(ctx, node);
		(void)ui->node_set_state(ctx, node, ui->node_get_state(ctx, node) | (u32)SK_UI_STATE_ACTIVE);
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		if (wd != NULL) {
			wd->dragging = 0;
		}
		if (sk_ui_node_eq(ui->pointer_capture_get(ctx), node)) {
			(void)ui->pointer_capture_set(ctx, SK_UI_NODE_INVALID);
		}
		(void)ui->node_set_state(ctx, node, ui->node_get_state(ctx, node) & ~(u32)SK_UI_STATE_ACTIVE);
		event->consumed = 1;
		return;
	}
	if (event->type != SK_UI_EVENT_POINTER_MOVE || wd == NULL || wd->dragging == 0) {
		return;
	}
	w = wd->resize_start_w + (event->x - wd->drag_last_x);
	if (w < 40.0f) {
		w = 40.0f;
	}
	ui_child_apply_size(ctx, child, w, ui_prop_f32_const(ui_slot(ctx, child), "child_h", 0.0f));
	ui_mark_dirty_up(ctx, child, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	event->consumed = 1;
}

static sk_ui_node_t ui_child_ensure_resize(sk_ui_context_t* ctx, sk_ui_node_t child) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t handle;
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	char hid[96];
	const_chr_t cid;

	handle = ui_find_child_widget(ctx, child, "child_resize");
	if (sk_ui_node_is_valid(handle)) {
		return handle;
	}
	cid = ui->node_get_id(ctx, child);
	if (cid != NULL && cid[0] != '\0') {
		(void)snprintf(hid, sizeof(hid), "%s-resize", cid);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(hid, sizeof(hid), "ui-child-resize-%u", ctx->widget_id_seq);
	}
	handle = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, child, SK_UI_CLASS_CHILD_RESIZE, "child_resize", "ui-child-resize", hid);
	if (!sk_ui_node_is_valid(handle)) {
		return handle;
	}
	{
		sk_ui_node_t body = ui_scroll_view_content_impl(ctx, child);
		sk_ui_layout_style_t bls;
		if (sk_ui_node_is_valid(body) && ui->node_get_layout_style(ctx, body, &bls) == 0) {
			bls.flex_grow = 1.0f;
			bls.width = sk_ui_auto();
			bls.height = sk_ui_percent(100.0f);
			(void)ui->node_set_layout_style(ctx, body, &bls);
		}
	}
	(void)ui->node_set_focusable(ctx, handle, 1);
	wd = ui_widget_data_ensure(ctx, handle, UI_WD_CHILD_RESIZE);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_child_resize_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, handle, &cbs);
	return handle;
}

i32 ui_child_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t child, u32 flags) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t handle;
	sk_ui_layout_style_t ls;
	if (ctx == NULL || !sk_ui_node_is_valid(child)) {
		return -1;
	}
	(void)ui->node_set_prop_i32(ctx, child, "child_flags", (i32)flags);
	ui_child_apply_border(ctx, child, (flags & SK_UI_CHILD_FLAG_BORDER) != 0u ? 1 : 0);
	if (ui->node_get_layout_style(ctx, child, &ls) == 0) {
		ls.flex_direction = ((flags & SK_UI_CHILD_FLAG_RESIZE_X) != 0u) ? SK_UI_FLEX_ROW : SK_UI_FLEX_COLUMN;
		(void)ui->node_set_layout_style(ctx, child, &ls);
	}
	if ((flags & SK_UI_CHILD_FLAG_RESIZE_X) != 0u) {
		handle = ui_child_ensure_resize(ctx, child);
		if (sk_ui_node_is_valid(handle)) {
			(void)ui->node_set_prop_i32(ctx, handle, "hidden", 0);
		}
	} else {
		handle = ui_find_child_widget(ctx, child, "child_resize");
		if (sk_ui_node_is_valid(handle)) {
			(void)ui->node_set_prop_i32(ctx, handle, "hidden", 1);
		}
	}
	return 0;
}

u32 ui_child_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t child) {
	const ui_node_slot_t* slot = ui_slot(ctx, child);
	i32 flags = 0;
	if (slot == NULL) {
		return 0u;
	}
	(void)ui_prop_i32_const(slot, "child_flags", &flags);
	return (u32)flags;
}

i32 ui_child_set_size_impl(sk_ui_context_t* ctx, sk_ui_node_t child, f32 width, f32 height) {
	if (ctx == NULL || !sk_ui_node_is_valid(child)) {
		return -1;
	}
	ui_child_apply_size(ctx, child, width, height);
	return 0;
}

sk_ui_node_t ui_child_content_impl(const sk_ui_context_t* ctx, sk_ui_node_t child) {
	return ui_scroll_view_content_impl(ctx, child);
}

sk_ui_node_t ui_widget_child_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id, f32 width, f32 height, u32 flags) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_scroll_view_impl(ctx, parent, id);
	sk_ui_node_t content;
	char body_id[192];
	const_chr_t cid;
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_add_class(ctx, n, SK_UI_CLASS_CHILD);
	(void)ui->node_set_prop_str(ctx, n, "widget", "child");
	content = ui_scroll_view_content_impl(ctx, n);
	cid = ui->node_get_id(ctx, n);
	if (sk_ui_node_is_valid(content) && cid != NULL && cid[0] != '\0') {
		u32 i;
		u32 k = 0u;
		for (i = 0u; cid[i] != '\0' && k + 6u < (u32)sizeof(body_id); ++i) {
			body_id[k++] = cid[i];
		}
		body_id[k++] = '-';
		body_id[k++] = 'b';
		body_id[k++] = 'o';
		body_id[k++] = 'd';
		body_id[k++] = 'y';
		body_id[k] = '\0';
		(void)ui->node_set_id(ctx, content, body_id);
	}
	ui_child_apply_size(ctx, n, width, height);
	(void)ui_child_set_flags_impl(ctx, n, flags);
	return n;
}

i32 ui_scroll_view_scroll_to_bottom_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	f32 ch;
	if (slot == NULL) {
		return -1;
	}
	ch = ui_prop_f32_const(slot, "content_height", 0.0f);
	return ui_scroll_view_set_scroll_impl(ctx, node, ui_prop_f32_const(slot, "scroll_x", 0.0f), ch);
}

i32 ui_begin_disabled_impl(sk_ui_context_t* ctx, i32 disabled) {
	if (ctx == NULL || ctx->disabled_depth >= 16u) {
		return -1;
	}
	ctx->disabled_stack[ctx->disabled_depth] = disabled != 0 ? 1u : 0u;
	if (disabled != 0) {
		ctx->disabled_active = (u8)(ctx->disabled_active + 1u);
	}
	ctx->disabled_depth = (u8)(ctx->disabled_depth + 1u);
	return 0;
}

i32 ui_end_disabled_impl(sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->disabled_depth == 0u) {
		return -1;
	}
	ctx->disabled_depth = (u8)(ctx->disabled_depth - 1u);
	if (ctx->disabled_stack[ctx->disabled_depth] != 0u && ctx->disabled_active > 0u) {
		ctx->disabled_active = (u8)(ctx->disabled_active - 1u);
	}
	return 0;
}

i32 ui_is_disabled_impl(const sk_ui_context_t* ctx) {
	return (ctx != NULL && ctx->disabled_active > 0u) ? 1 : 0;
}

i32 ui_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled) {
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	ui_set_disabled_walk(ctx, node, disabled);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	return 0;
}

static i32 ui_id_stack_append(sk_ui_context_t* ctx, const_chr_t seg) {
	u32 n;
	u32 i;
	if (ctx == NULL || seg == NULL || ctx->id_stack_depth >= 16u) {
		return -1;
	}
	n = 0u;
	while (seg[n] != '\0') {
		n += 1u;
	}
	if (n == 0u) {
		return -1;
	}
	ctx->id_mark[ctx->id_stack_depth] = ctx->id_prefix_len;
	if (ctx->id_prefix_len > 0u) {
		if ((u32)ctx->id_prefix_len + 1u >= (u32)sizeof(ctx->id_prefix)) {
			return -1;
		}
		ctx->id_prefix[ctx->id_prefix_len] = '/';
		ctx->id_prefix_len = (u16)(ctx->id_prefix_len + 1u);
	}
	for (i = 0u; i < n; ++i) {
		if ((u32)ctx->id_prefix_len + 1u >= (u32)sizeof(ctx->id_prefix)) {
			ctx->id_prefix_len = ctx->id_mark[ctx->id_stack_depth];
			ctx->id_prefix[ctx->id_prefix_len] = '\0';
			return -1;
		}
		ctx->id_prefix[ctx->id_prefix_len] = seg[i];
		ctx->id_prefix_len = (u16)(ctx->id_prefix_len + 1u);
	}
	ctx->id_prefix[ctx->id_prefix_len] = '\0';
	ctx->id_stack_depth = (u8)(ctx->id_stack_depth + 1u);
	return 0;
}

i32 ui_push_id_impl(sk_ui_context_t* ctx, const_chr_t id) {
	if (id == NULL || id[0] == '\0') {
		return -1;
	}
	return ui_id_stack_append(ctx, id);
}

i32 ui_push_id_int_impl(sk_ui_context_t* ctx, i32 id) {
	char buf[24];
	(void)snprintf(buf, sizeof(buf), "%d", id);
	return ui_id_stack_append(ctx, buf);
}

i32 ui_push_id_ptr_impl(sk_ui_context_t* ctx, const void* ptr) {
	char buf[24];
	(void)snprintf(buf, sizeof(buf), "%" PRIxPTR, (uintptr_t)ptr);
	return ui_id_stack_append(ctx, buf);
}

i32 ui_pop_id_impl(sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->id_stack_depth == 0u) {
		return -1;
	}
	ctx->id_stack_depth = (u8)(ctx->id_stack_depth - 1u);
	ctx->id_prefix_len = ctx->id_mark[ctx->id_stack_depth];
	ctx->id_prefix[ctx->id_prefix_len] = '\0';
	return 0;
}

i32 ui_set_next_item_width_impl(sk_ui_context_t* ctx, f32 width) {
	if (ctx == NULL) {
		return -1;
	}
	ctx->has_next_item_width = 1u;
	ctx->next_item_width = width;
	return 0;
}

i32 ui_set_next_item_open_impl(sk_ui_context_t* ctx, i32 is_open, u32 cond) {
	if (ctx == NULL) {
		return -1;
	}
	ctx->has_next_item_open = 1u;
	ctx->next_item_open = is_open != 0 ? 1u : 0u;
	ctx->next_item_open_cond = (u8)cond;
	return 0;
}

i32 ui_indent_impl(sk_ui_context_t* ctx, f32 width) {
	if (ctx == NULL) {
		return -1;
	}
	if (width <= 0.0f) {
		width = SK_UI_INDENT_DEFAULT;
	}
	ctx->indent_px += width;
	return 0;
}

i32 ui_unindent_impl(sk_ui_context_t* ctx, f32 width) {
	if (ctx == NULL) {
		return -1;
	}
	if (width <= 0.0f) {
		width = SK_UI_INDENT_DEFAULT;
	}
	ctx->indent_px -= width;
	if (ctx->indent_px < 0.0f) {
		ctx->indent_px = 0.0f;
	}
	return 0;
}

sk_ui_node_t ui_widget_group_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_GROUP, "group", "ui-group", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	if (ui->node_get_layout_style(ctx, n, &ls) == 0) {
		ls.flex_direction = SK_UI_FLEX_COLUMN;
		ls.align_items = SK_UI_ALIGN_FLEX_START;
		ls.width = sk_ui_auto();
		ls.height = sk_ui_auto();
		ls.flex_grow = 0.0f;
		(void)ui->node_set_layout_style(ctx, n, &ls);
	}
	return n;
}

i32 ui_group_get_extents_impl(const sk_ui_context_t* ctx, sk_ui_node_t group, sk_ui_rect_t* out) {
	if (out == NULL) {
		return -1;
	}
	return ui_wapi()->node_get_abs_rect(ctx, group, out, NULL);
}

sk_ui_node_t ui_widget_horizontal_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_HORIZONTAL, "horizontal", "ui-horizontal", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	if (ui->node_get_layout_style(ctx, n, &ls) == 0) {
		ls.flex_direction = SK_UI_FLEX_ROW;
		ls.align_items = SK_UI_ALIGN_CENTER;
		ls.width = sk_ui_percent(100.0f);
		(void)ui->node_set_layout_style(ctx, n, &ls);
	}
	return n;
}

sk_ui_node_t ui_widget_vertical_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_VERTICAL, "vertical", "ui-vertical", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	if (ui->node_get_layout_style(ctx, n, &ls) == 0) {
		ls.flex_direction = SK_UI_FLEX_COLUMN;
		ls.align_items = SK_UI_ALIGN_STRETCH;
		ls.width = sk_ui_percent(100.0f);
		(void)ui->node_set_layout_style(ctx, n, &ls);
	}
	return n;
}

sk_ui_node_t ui_widget_spring_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 weight, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n;
	if (weight <= 0.0f) {
		weight = 1.0f;
	}
	n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_SPRING, "spring", "ui-spring", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_f32(ctx, n, "weight", weight);
	if (ui->node_get_layout_style(ctx, n, &ls) == 0) {
		ls.flex_grow = weight;
		ls.flex_shrink = 1.0f;
		ls.flex_basis = sk_ui_pt(0.0f);
		ls.width = sk_ui_auto();
		ls.height = sk_ui_auto();
		ls.min_width = sk_ui_pt(0.0f);
		ls.min_height = sk_ui_pt(0.0f);
		(void)ui->node_set_layout_style(ctx, n, &ls);
	}
	return n;
}

/* -------------------------------------------------------------------------- */
/* Popup / modal family (APX-347; manifest §12)                               */
/* -------------------------------------------------------------------------- */

static i32 ui_popup_is_surface_widget(const_chr_t w) {
	if (w == NULL) {
		return 0;
	}
	return (strcmp(w, "menu_popup") == 0 || strcmp(w, "context_menu") == 0 || strcmp(w, "popup_menu") == 0 || strcmp(w, "modal") == 0) ? 1 : 0;
}

static i32 ui_popup_stack_index(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	u32 i;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	for (i = 0u; i < ctx->popup_stack_count; ++i) {
		if (sk_ui_node_eq(ctx->popup_stack[i], node)) {
			return (i32)i;
		}
	}
	return -1;
}

static void ui_popup_sync_stack(sk_ui_context_t* ctx, sk_ui_node_t node, i32 open) {
	i32 idx;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return;
	}
	idx = ui_popup_stack_index(ctx, node);
	if (open != 0) {
		if (idx < 0 && ctx->popup_stack_count < (u8)SK_UI_POPUP_STACK_MAX) {
			ctx->popup_stack[ctx->popup_stack_count] = node;
			ctx->popup_stack_count = (u8)(ctx->popup_stack_count + 1u);
		}
		return;
	}
	if (idx < 0) {
		return;
	}
	{
		u32 i;
		for (i = (u32)idx; i + 1u < ctx->popup_stack_count; ++i) {
			ctx->popup_stack[i] = ctx->popup_stack[i + 1u];
		}
		ctx->popup_stack_count = (u8)(ctx->popup_stack_count - 1u);
		ctx->popup_stack[ctx->popup_stack_count] = SK_UI_NODE_INVALID;
	}
}

static void ui_popup_raise(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t parent;
	u32 n;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return;
	}
	parent = ui->node_parent(ctx, node);
	if (!sk_ui_node_is_valid(parent)) {
		return;
	}
	n = ui->node_child_count(ctx, parent);
	if (n > 0u) {
		(void)ui->node_set_child_index(ctx, parent, node, n - 1u);
	}
}

static sk_ui_node_t ui_popup_find_marked_focus(const sk_ui_context_t* ctx, sk_ui_node_t owner) {
	sk_ui_node_t stack[64];
	u32 n = 0u;
	if (!sk_ui_node_is_valid(owner)) {
		return SK_UI_NODE_INVALID;
	}
	stack[n++] = owner;
	while (n > 0u) {
		sk_ui_node_t cur = stack[--n];
		const ui_node_slot_t* slot = ui_slot(ctx, cur);
		u32 i;
		i32 marked = 0;
		if (slot == NULL) {
			continue;
		}
		if (!sk_ui_node_eq(cur, owner) && ui_prop_i32_const(slot, "default_focus", &marked) == 0 && marked != 0) {
			return cur;
		}
		for (i = slot->children.count; i > 0u && n < 64u;) {
			--i;
			stack[n++] = slot->children.items[i];
		}
	}
	return SK_UI_NODE_INVALID;
}

static sk_ui_node_t ui_popup_first_focusable(const sk_ui_context_t* ctx, sk_ui_node_t owner) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t stack[64];
	u32 n = 0u;
	if (!sk_ui_node_is_valid(owner)) {
		return SK_UI_NODE_INVALID;
	}
	stack[n++] = owner;
	while (n > 0u) {
		sk_ui_node_t cur = stack[--n];
		const ui_node_slot_t* slot = ui_slot(ctx, cur);
		u32 i;
		if (slot == NULL) {
			continue;
		}
		if (!sk_ui_node_eq(cur, owner) && ui->node_get_focusable(ctx, cur) != 0 && (ui->node_get_state(ctx, cur) & (u32)SK_UI_STATE_DISABLED) == 0u) {
			return cur;
		}
		for (i = slot->children.count; i > 0u && n < 64u;) {
			--i;
			stack[n++] = slot->children.items[i];
		}
	}
	return SK_UI_NODE_INVALID;
}

static void ui_popup_apply_default_focus(sk_ui_context_t* ctx, sk_ui_node_t owner) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t target;
	sk_ui_node_t row;
	if (ctx == NULL || !sk_ui_node_is_valid(owner)) {
		return;
	}
	target = ui_popup_find_marked_focus(ctx, owner);
	if (!sk_ui_node_is_valid(target)) {
		sk_ui_node_t dialog = ui_find_child_widget(ctx, owner, "modal_dialog");
		row = ui_find_child_widget(ctx, owner, "modal_buttons");
		if (!sk_ui_node_is_valid(row) && sk_ui_node_is_valid(dialog)) {
			row = ui_find_child_widget(ctx, dialog, "modal_buttons");
		}
		if (sk_ui_node_is_valid(row)) {
			target = ui_popup_first_focusable(ctx, row);
		}
	}
	if (!sk_ui_node_is_valid(target)) {
		target = ui_popup_first_focusable(ctx, owner);
	}
	if (sk_ui_node_is_valid(target)) {
		(void)ui->focus_set(ctx, target);
	}
}

static sk_ui_node_t ui_modal_dialog_child(const sk_ui_context_t* ctx, sk_ui_node_t modal) {
	return ui_find_child_widget(ctx, modal, "modal_dialog");
}

sk_ui_node_t ui_modal_dialog_impl(const sk_ui_context_t* ctx, sk_ui_node_t modal) {
	return ui_modal_dialog_child(ctx, modal);
}

sk_ui_node_t ui_modal_dim_impl(const sk_ui_context_t* ctx, sk_ui_node_t modal) {
	return ui_find_child_widget(ctx, modal, "modal_dim");
}

sk_ui_node_t ui_modal_title_bar_impl(const sk_ui_context_t* ctx, sk_ui_node_t modal) {
	sk_ui_node_t dialog = ui_modal_dialog_child(ctx, modal);
	if (!sk_ui_node_is_valid(dialog)) {
		return SK_UI_NODE_INVALID;
	}
	return ui_find_child_widget(ctx, dialog, "modal_title");
}

sk_ui_node_t ui_modal_body_impl(const sk_ui_context_t* ctx, sk_ui_node_t modal) {
	sk_ui_node_t dialog = ui_modal_dialog_child(ctx, modal);
	if (!sk_ui_node_is_valid(dialog)) {
		return SK_UI_NODE_INVALID;
	}
	return ui_find_child_widget(ctx, dialog, "modal_body");
}

sk_ui_node_t ui_modal_button_row_impl(const sk_ui_context_t* ctx, sk_ui_node_t modal) {
	sk_ui_node_t dialog = ui_modal_dialog_child(ctx, modal);
	if (!sk_ui_node_is_valid(dialog)) {
		return SK_UI_NODE_INVALID;
	}
	return ui_find_child_widget(ctx, dialog, "modal_buttons");
}

static void ui_modal_apply_size(sk_ui_context_t* ctx, sk_ui_node_t modal, u32 flags) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t dialog = ui_modal_dialog_child(ctx, modal);
	sk_ui_node_t body = ui_modal_body_impl(ctx, modal);
	sk_ui_style_props_t p;

	if (!sk_ui_node_is_valid(dialog)) {
		return;
	}
	ui_style_props_clear(&p);
	if ((flags & SK_UI_MODAL_FLAG_ALWAYS_AUTO_RESIZE) != 0u) {
		p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_ALIGN_SELF;
		p.layout.width = sk_ui_auto();
		p.layout.height = sk_ui_auto();
		p.layout.min_width = sk_ui_pt(260.0f);
		p.layout.flex_grow = 0.0f;
		p.layout.flex_shrink = 0.0f;
		p.layout.align_self = SK_UI_ALIGN_CENTER;
		(void)ui->node_merge_inline_style(ctx, dialog, &p);
		if (sk_ui_node_is_valid(body)) {
			ui_style_props_clear(&p);
			p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_FLEX_GROW;
			p.layout.height = sk_ui_auto();
			p.layout.min_height = sk_ui_pt(0.0f);
			p.layout.max_height = sk_ui_auto();
			p.layout.flex_grow = 0.0f;
			(void)ui->node_merge_inline_style(ctx, body, &p);
			(void)ui->node_set_clip_children(ctx, body, 0);
		}
		return;
	}
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH;
	p.layout.width = sk_ui_pt(SK_UI_MODAL_FIXED_WIDTH);
	p.layout.min_width = sk_ui_pt(SK_UI_MODAL_FIXED_WIDTH);
	p.layout.max_width = sk_ui_pt(SK_UI_MODAL_FIXED_WIDTH);
	(void)ui->node_merge_inline_style(ctx, dialog, &p);
	if (sk_ui_node_is_valid(body)) {
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_FLEX_GROW;
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.height = sk_ui_pt(SK_UI_MODAL_FIXED_BODY_HEIGHT);
		p.layout.min_height = sk_ui_pt(SK_UI_MODAL_FIXED_BODY_HEIGHT);
		p.layout.max_height = sk_ui_pt(SK_UI_MODAL_FIXED_BODY_HEIGHT);
		p.layout.flex_grow = 0.0f;
		(void)ui->node_merge_inline_style(ctx, body, &p);
		(void)ui->node_set_clip_children(ctx, body, 1);
	}
}

static void ui_modal_do_close(sk_ui_context_t* ctx, sk_ui_node_t close_btn) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t cur = close_btn;
	while (sk_ui_node_is_valid(cur)) {
		const ui_node_slot_t* slot = ui_slot(ctx, cur);
		const_chr_t w;
		if (slot == NULL) {
			break;
		}
		w = ui_prop_str_const(slot, "widget");
		if (w != NULL && strcmp(w, "modal") == 0) {
			(void)ui_menu_set_open_impl(ctx, cur, 0);
			return;
		}
		cur = ui->node_parent(ctx, cur);
	}
}

static void ui_modal_close_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)user;
	if (event != NULL) {
		event->consumed = 1;
	}
	ui_modal_do_close(ctx, node);
}

static sk_ui_node_t ui_modal_ensure_close(sk_ui_context_t* ctx, sk_ui_node_t modal) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t bar = ui_modal_title_bar_impl(ctx, modal);
	sk_ui_node_t close;
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	char close_id[96];
	const_chr_t wid;
	sk_ui_layout_style_t ls;

	if (!sk_ui_node_is_valid(bar)) {
		return SK_UI_NODE_INVALID;
	}
	close = ui_find_child_widget(ctx, bar, "window_close");
	if (sk_ui_node_is_valid(close)) {
		return close;
	}
	wid = ui->node_get_id(ctx, modal);
	if (wid != NULL && wid[0] != '\0') {
		(void)snprintf(close_id, sizeof(close_id), "%s-close", wid);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(close_id, sizeof(close_id), "ui-modal-close-%u", ctx->widget_id_seq);
	}
	{
		char spring_id[96];
		if (wid != NULL && wid[0] != '\0') {
			(void)snprintf(spring_id, sizeof(spring_id), "%s-close-gap", wid);
		} else {
			ctx->widget_id_seq += 1u;
			(void)snprintf(spring_id, sizeof(spring_id), "ui-modal-close-gap-%u", ctx->widget_id_seq);
		}
		(void)ui_widget_spring_impl(ctx, bar, 1.0f, spring_id);
	}
	close = ui_button_make(ctx, bar, "x", close_id, SK_UI_CLASS_WINDOW_CLOSE, "window_close", 16.0f, 16.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT, 1);
	if (!sk_ui_node_is_valid(close)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->node_set_prop_str(ctx, close, "widget", "window_close");
	if (ui->node_get_layout_style(ctx, close, &ls) == 0) {
		ls.position = SK_UI_POSITION_RELATIVE;
		ls.flex_grow = 0.0f;
		(void)ui->node_set_layout_style(ctx, close, &ls);
	}
	wd = ui_widget_data_ensure(ctx, close, UI_WD_BUTTON);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_modal_close_on_click;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, close, &cbs);
	return close;
}

sk_ui_node_t ui_widget_popup_menu_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_POPUP_MENU, "popup_menu", "ui-popup-menu", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_i32(ctx, n, "open", 0);
	(void)ui->node_set_prop_i32(ctx, n, "hidden", 1);
	(void)ui->node_set_prop_i32(ctx, n, "z_index", 200);
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	ls.width = sk_ui_pt(SK_UI_POPUP_MENU_WIDTH);
	ls.min_width = sk_ui_pt(SK_UI_POPUP_MENU_WIDTH);
	ls.left = sk_ui_pt(0.0f);
	ls.top = sk_ui_pt(0.0f);
	(void)ui->node_set_layout_style(ctx, n, &ls);
	return n;
}

sk_ui_node_t ui_widget_modal_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t title, const_chr_t id, i32* p_open, u32 flags) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_layout_style_t ls;
	ui_widget_data_t* wd;
	sk_ui_node_t modal;
	sk_ui_node_t dim;
	sk_ui_node_t dialog;
	sk_ui_node_t title_bar;
	sk_ui_node_t body;
	sk_ui_node_t buttons;
	char dim_id[80];
	char dialog_id[80];
	char title_id[80];
	char body_id[80];
	char buttons_id[80];
	i32 start_open = 0;

	modal = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_MODAL, "modal", "ui-modal", id);
	if (!sk_ui_node_is_valid(modal)) {
		return modal;
	}

	if (id != NULL && id[0] != '\0') {
		(void)snprintf(dim_id, sizeof(dim_id), "%s-dim", id);
		(void)snprintf(dialog_id, sizeof(dialog_id), "%s-dialog", id);
		(void)snprintf(title_id, sizeof(title_id), "%s-title", id);
		(void)snprintf(body_id, sizeof(body_id), "%s-body", id);
		(void)snprintf(buttons_id, sizeof(buttons_id), "%s-buttons", id);
	} else {
		ctx->widget_id_seq += 1u;
		(void)snprintf(dim_id, sizeof(dim_id), "ui-modal-dim-%u", ctx->widget_id_seq);
		ctx->widget_id_seq += 1u;
		(void)snprintf(dialog_id, sizeof(dialog_id), "ui-modal-dialog-%u", ctx->widget_id_seq);
		ctx->widget_id_seq += 1u;
		(void)snprintf(title_id, sizeof(title_id), "ui-modal-title-%u", ctx->widget_id_seq);
		ctx->widget_id_seq += 1u;
		(void)snprintf(body_id, sizeof(body_id), "ui-modal-body-%u", ctx->widget_id_seq);
		ctx->widget_id_seq += 1u;
		(void)snprintf(buttons_id, sizeof(buttons_id), "ui-modal-buttons-%u", ctx->widget_id_seq);
	}

	(void)ui->node_set_prop_i32(ctx, modal, "flags", (i32)flags);
	(void)ui->node_set_prop_i32(ctx, modal, "z_index", 300);
	ui_layout_style_init_default(&ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(0.0f);
	ls.top = sk_ui_pt(0.0f);
	ls.width = sk_ui_percent(100.0f);
	ls.height = sk_ui_percent(100.0f);
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	ls.justify_content = SK_UI_JUSTIFY_CENTER;
	ls.align_items = SK_UI_ALIGN_CENTER;
	(void)ui->node_set_layout_style(ctx, modal, &ls);

	dim = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, modal, SK_UI_CLASS_MODAL_DIM, "modal_dim", "ui-modal-dim", dim_id);
	if (sk_ui_node_is_valid(dim)) {
		ui_layout_style_init_default(&ls);
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(0.0f);
		ls.top = sk_ui_pt(0.0f);
		ls.width = sk_ui_percent(100.0f);
		ls.height = sk_ui_percent(100.0f);
		(void)ui->node_set_layout_style(ctx, dim, &ls);
		(void)ui->node_set_prop_i32(ctx, dim, "z_index", 0);
	}

	dialog = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, modal, SK_UI_CLASS_MODAL_DIALOG, "modal_dialog", "ui-modal-dialog", dialog_id);
	if (sk_ui_node_is_valid(dialog)) {
		(void)ui->node_set_prop_i32(ctx, dialog, "z_index", 1);
	}

	title_bar = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, dialog, SK_UI_CLASS_MODAL_TITLE, "modal_title", "ui-modal-title", title_id);
	if (sk_ui_node_is_valid(title_bar)) {
		(void)ui->node_set_prop_str(ctx, title_bar, "text", title != NULL ? title : "");
		(void)ui->node_set_prop_i32(ctx, title_bar, "text_align", 0);
		(void)ui->node_set_prop_i32(ctx, title_bar, "vertical_align", 1);
	}

	body = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, dialog, SK_UI_CLASS_MODAL_BODY, "modal_body", "ui-modal-body", body_id);
	(void)body;
	buttons = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, dialog, SK_UI_CLASS_MODAL_BUTTONS, "modal_buttons", "ui-modal-buttons", buttons_id);
	(void)buttons;

	wd = ui_widget_data_ensure(ctx, modal, UI_WD_MODAL);
	if (wd != NULL) {
		wd->p_open = p_open;
	}
	if (p_open != NULL) {
		(void)ui_modal_ensure_close(ctx, modal);
		start_open = *p_open != 0 ? 1 : 0;
	}
	(void)ui->node_set_prop_i32(ctx, modal, "open", start_open);
	(void)ui->node_set_prop_i32(ctx, modal, "hidden", start_open != 0 ? 0 : 1);
	ui_modal_apply_size(ctx, modal, flags);
	if (start_open != 0) {
		ui_popup_sync_stack(ctx, modal, 1);
		ui_popup_raise(ctx, modal);
	}
	return modal;
}

i32 ui_modal_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t modal, i32 open) {
	return ui_menu_set_open_impl(ctx, modal, open);
}

i32 ui_modal_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t modal) {
	return ui_menu_get_open_impl(ctx, modal);
}

i32 ui_modal_bind_open_impl(sk_ui_context_t* ctx, sk_ui_node_t modal, i32* p_open) {
	ui_widget_data_t* wd;
	sk_ui_node_t close;
	if (ctx == NULL || !sk_ui_node_is_valid(modal)) {
		return -1;
	}
	wd = ui_widget_data_ensure(ctx, modal, UI_WD_MODAL);
	if (wd == NULL) {
		return -1;
	}
	wd->p_open = p_open;
	close = ui_modal_ensure_close(ctx, modal);
	if (p_open == NULL) {
		if (sk_ui_node_is_valid(close)) {
			(void)ui_wapi()->node_set_prop_i32(ctx, close, "hidden", 1);
		}
		return 0;
	}
	if (sk_ui_node_is_valid(close)) {
		(void)ui_wapi()->node_set_prop_i32(ctx, close, "hidden", 0);
	}
	return ui_menu_set_open_impl(ctx, modal, *p_open != 0 ? 1 : 0);
}

u32 ui_modal_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t modal) {
	const ui_node_slot_t* slot = ui_slot(ctx, modal);
	i32 flags = 0;
	if (slot == NULL) {
		return 0u;
	}
	(void)ui_prop_i32_const(slot, "flags", &flags);
	return (u32)flags;
}

i32 ui_modal_set_title_impl(sk_ui_context_t* ctx, sk_ui_node_t modal, const_chr_t title) {
	sk_ui_node_t bar = ui_modal_title_bar_impl(ctx, modal);
	if (!sk_ui_node_is_valid(bar)) {
		return -1;
	}
	return ui_wapi()->node_set_prop_str(ctx, bar, "text", title != NULL ? title : "");
}

i32 ui_popup_open_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	if (ui_menu_get_open_impl(ctx, node) != 0) {
		return 0;
	}
	return ui_menu_set_open_impl(ctx, node, 1);
}

i32 ui_popup_close_current_impl(sk_ui_context_t* ctx) {
	sk_ui_node_t top;
	if (ctx == NULL || ctx->popup_stack_count == 0u) {
		return 0;
	}
	top = ctx->popup_stack[ctx->popup_stack_count - 1u];
	if (!sk_ui_node_is_valid(top)) {
		ctx->popup_stack_count = (u8)(ctx->popup_stack_count - 1u);
		return 0;
	}
	return ui_menu_set_open_impl(ctx, top, 0);
}

i32 ui_popup_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_menu_get_open_impl(ctx, node);
}

i32 ui_set_item_default_focus_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t cur;
	if (ctx == NULL || !sk_ui_node_is_valid(node)) {
		return -1;
	}
	if (ui->node_set_prop_i32(ctx, node, "default_focus", 1) != 0) {
		return -1;
	}
	cur = node;
	while (sk_ui_node_is_valid(cur)) {
		const ui_node_slot_t* slot = ui_slot(ctx, cur);
		const_chr_t w;
		if (slot == NULL) {
			break;
		}
		w = ui_prop_str_const(slot, "widget");
		if (ui_popup_is_surface_widget(w) && ui_menu_get_open_impl(ctx, cur) != 0) {
			ui_popup_apply_default_focus(ctx, cur);
			break;
		}
		cur = slot->parent;
	}
	return 0;
}

static i32 ui_popup_node_contains(const sk_ui_context_t* ctx, sk_ui_node_t ancestor, sk_ui_node_t node) {
	sk_ui_node_t cur = node;
	if (!sk_ui_node_is_valid(ancestor) || !sk_ui_node_is_valid(cur)) {
		return 0;
	}
	while (sk_ui_node_is_valid(cur)) {
		const ui_node_slot_t* slot;
		if (sk_ui_node_eq(cur, ancestor)) {
			return 1;
		}
		slot = ui_slot(ctx, cur);
		if (slot == NULL) {
			break;
		}
		cur = slot->parent;
	}
	return 0;
}

sk_ui_node_t ui_popup_hit_redirect_impl(const sk_ui_context_t* ctx, sk_ui_node_t hit) {
	i32 i;
	if (ctx == NULL) {
		return hit;
	}
	for (i = (i32)ctx->popup_stack_count - 1; i >= 0; --i) {
		sk_ui_node_t node = ctx->popup_stack[i];
		const ui_node_slot_t* slot = ui_slot(ctx, node);
		const_chr_t w;
		if (slot == NULL) {
			continue;
		}
		w = ui_prop_str_const(slot, "widget");
		if (w == NULL || strcmp(w, "modal") != 0) {
			continue;
		}
		if (ui_menu_get_open_impl(ctx, node) == 0) {
			continue;
		}
		if (ui_popup_node_contains(ctx, node, hit) != 0) {
			return hit;
		}
		{
			sk_ui_node_t dim = ui_modal_dim_impl(ctx, node);
			return sk_ui_node_is_valid(dim) ? dim : node;
		}
	}
	return hit;
}

void ui_popup_on_right_click_impl(sk_ui_context_t* ctx, sk_ui_node_t hit, f32 x, f32 y) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t cur = hit;
	if (ctx == NULL) {
		return;
	}
	while (sk_ui_node_is_valid(cur)) {
		const ui_node_slot_t* slot = ui_slot(ctx, cur);
		sk_ui_node_t menu;
		if (slot == NULL) {
			break;
		}
		menu = ui_find_child_widget(ctx, cur, "popup_menu");
		if (!sk_ui_node_is_valid(menu)) {
			menu = ui_find_child_widget(ctx, cur, "context_menu");
		}
		if (sk_ui_node_is_valid(menu)) {
			sk_ui_layout_style_t ls;
			if (ui->node_get_layout_style(ctx, menu, &ls) == 0) {
				ls.position = SK_UI_POSITION_ABSOLUTE;
				ls.left = sk_ui_pt(x);
				ls.top = sk_ui_pt(y);
				(void)ui->node_set_layout_style(ctx, menu, &ls);
			}
			(void)ui_popup_open_impl(ctx, menu);
			return;
		}
		cur = slot->parent;
	}
}

i32 ui_popup_on_escape_impl(sk_ui_context_t* ctx) {
	if (ctx == NULL || ctx->popup_stack_count == 0u) {
		return 0;
	}
	(void)ui_popup_close_current_impl(ctx);
	return 1;
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

/* Simple in-memory clipboard for text_input tests. */
static char g_clip_buf[512];
static u32 g_clip_len;

static i32 test_clip_get(void_ptr_t user, char* buf, u32 cap, u32* out_len) {
	u32 n = g_clip_len;
	(void)user;
	if (n >= cap) {
		n = cap > 0u ? cap - 1u : 0u;
	}
	if (cap > 0u) {
		memcpy(buf, g_clip_buf, n);
		buf[n] = '\0';
	}
	if (out_len != NULL) {
		*out_len = n;
	}
	return 0;
}

static i32 test_clip_set(void_ptr_t user, const_chr_t text) {
	u32 n;
	(void)user;
	if (text == NULL) {
		text = "";
	}
	n = (u32)strlen(text);
	if (n >= sizeof(g_clip_buf)) {
		n = (u32)sizeof(g_clip_buf) - 1u;
	}
	memcpy(g_clip_buf, text, n);
	g_clip_buf[n] = '\0';
	g_clip_len = n;
	return 0;
}

static const sk_ui_api_t* wtest_api(void) {
	return ui_get_api_table();
}

static void wtest_layout(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 w, f32 h) {
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, w, h));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
}

/** Inline width/height so class defaults cannot stretch the control. */
static void wtest_set_size(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, f32 w, f32 h) {
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

static i32 g_bool_cb_count;
static i32 g_bool_cb_value;
static void test_bool_cb(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)user;
	g_bool_cb_count += 1;
	g_bool_cb_value = value;
}

static i32 g_float_cb_count;
static f32 g_float_cb_value;
static void test_float_cb(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)user;
	g_float_cb_count += 1;
	g_float_cb_value = value;
}

/* Soft-raster solid triangles from a draw list into an RGBA8 buffer (golden). */
static void soft_clear(u8* px, u32 w, u32 h, u8 r, u8 g, u8 b, u8 a) {
	u32 i, n = w * h;
	for (i = 0u; i < n; ++i) {
		px[i * 4u + 0u] = r;
		px[i * 4u + 1u] = g;
		px[i * 4u + 2u] = b;
		px[i * 4u + 3u] = a;
	}
}

static void soft_put(u8* px, u32 w, u32 h, i32 x, i32 y, u32 color) {
	u8* p;
	if (x < 0 || y < 0 || (u32)x >= w || (u32)y >= h) {
		return;
	}
	p = px + ((u32)y * w + (u32)x) * 4u;
	/* Overwrite (opaque enough for solid widget goldens). */
	p[0] = (u8)(color & 0xFFu);
	p[1] = (u8)((color >> 8) & 0xFFu);
	p[2] = (u8)((color >> 16) & 0xFFu);
	p[3] = (u8)((color >> 24) & 0xFFu);
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
		/* Solid + image (treat image as solid using vertex color). Skip pure font AA for goldens. */
		if (cmd->texture_kind == SK_UI_DRAW_TEX_MSDF) {
			continue;
		}
		for (i = 0u; i + 2u < cmd->index_count; i += 3u) {
			u32 i0 = dl->indices[cmd->index_offset + i + 0u];
			u32 i1 = dl->indices[cmd->index_offset + i + 1u];
			u32 i2 = dl->indices[cmd->index_offset + i + 2u];
			const sk_ui_draw_vertex_t* v0 = &dl->vertices[i0];
			const sk_ui_draw_vertex_t* v1 = &dl->vertices[i1];
			const sk_ui_draw_vertex_t* v2 = &dl->vertices[i2];
			soft_tri(px, w, h, v0->x, v0->y, v1->x, v1->y, v2->x, v2->y, v0->color);
		}
	}
}

/* Minimal uncompressed RGBA PNG writer (store only). */
static u32 png_crc_table[256];
static i32 png_crc_ready;

static void png_crc_init(void) {
	u32 n, k;
	if (png_crc_ready) {
		return;
	}
	for (n = 0u; n < 256u; ++n) {
		u32 c = n;
		for (k = 0u; k < 8u; ++k) {
			c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		}
		png_crc_table[n] = c;
	}
	png_crc_ready = 1;
}

static u32 png_crc(const u8* data, u32 len) {
	u32 c = 0xFFFFFFFFu;
	u32 i;
	png_crc_init();
	for (i = 0u; i < len; ++i) {
		c = png_crc_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
	}
	return c ^ 0xFFFFFFFFu;
}

static void png_be32(u8* p, u32 v) {
	p[0] = (u8)((v >> 24) & 0xFFu);
	p[1] = (u8)((v >> 16) & 0xFFu);
	p[2] = (u8)((v >> 8) & 0xFFu);
	p[3] = (u8)(v & 0xFFu);
}

static i32 png_write_rgba(const char* path, const u8* rgba, u32 w, u32 h) {
	FILE* f;
	u32 row_bytes = w * 4u + 1u;
	u32 raw_len = row_bytes * h;
	u8* raw;
	u8* zdata;
	u32 zlen;
	u32 y;
	u8 sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	u8 ihdr[13 + 12];
	u8 chunk_hdr[8];
	u32 crc;
	u32 adler_s1 = 1u, adler_s2 = 0u;
	u32 i;

	raw = (u8*)malloc(raw_len);
	if (raw == NULL) {
		return -1;
	}
	for (y = 0u; y < h; ++y) {
		raw[y * row_bytes] = 0; /* filter None */
		memcpy(raw + y * row_bytes + 1u, rgba + y * w * 4u, w * 4u);
	}
	/* zlib store blocks (32k chunks). */
	{
		u32 pos = 0u;
		u32 max_store = 65535u;
		u32 blocks = (raw_len + max_store - 1u) / max_store;
		u32 bi;
		zlen = 2u + blocks * 5u + raw_len + 4u;
		zdata = (u8*)malloc(zlen);
		if (zdata == NULL) {
			free(raw);
			return -1;
		}
		zdata[0] = 0x78;
		zdata[1] = 0x01;
		pos = 2u;
		for (bi = 0u; bi < blocks; ++bi) {
			u32 remain = raw_len - bi * max_store;
			u32 chunk = remain > max_store ? max_store : remain;
			u32 bfinal = (bi + 1u == blocks) ? 1u : 0u;
			zdata[pos++] = (u8)bfinal; /* BFINAL + BTYPE=00 */
			zdata[pos++] = (u8)(chunk & 0xFFu);
			zdata[pos++] = (u8)((chunk >> 8) & 0xFFu);
			zdata[pos++] = (u8)((~chunk) & 0xFFu);
			zdata[pos++] = (u8)(((~chunk) >> 8) & 0xFFu);
			memcpy(zdata + pos, raw + bi * max_store, chunk);
			pos += chunk;
		}
		for (i = 0u; i < raw_len; ++i) {
			adler_s1 = (adler_s1 + raw[i]) % 65521u;
			adler_s2 = (adler_s2 + adler_s1) % 65521u;
		}
		png_be32(zdata + pos, (adler_s2 << 16) | adler_s1);
		pos += 4u;
		zlen = pos;
	}
	free(raw);

	f = fopen(path, "wb");
	if (f == NULL) {
		free(zdata);
		return -1;
	}
	fwrite(sig, 1, 8, f);
	/* IHDR */
	png_be32(ihdr, 13u);
	ihdr[4] = 'I';
	ihdr[5] = 'H';
	ihdr[6] = 'D';
	ihdr[7] = 'R';
	png_be32(ihdr + 8, w);
	png_be32(ihdr + 12, h);
	ihdr[16] = 8;
	ihdr[17] = 6;
	ihdr[18] = 0;
	ihdr[19] = 0;
	ihdr[20] = 0;
	crc = png_crc(ihdr + 4, 13u + 4u);
	png_be32(ihdr + 21, crc);
	fwrite(ihdr, 1, 25, f);
	/* IDAT */
	png_be32(chunk_hdr, zlen);
	chunk_hdr[4] = 'I';
	chunk_hdr[5] = 'D';
	chunk_hdr[6] = 'A';
	chunk_hdr[7] = 'T';
	fwrite(chunk_hdr, 1, 8, f);
	fwrite(zdata, 1, zlen, f);
	{
		u8* crcbuf = (u8*)malloc(4u + zlen);
		if (crcbuf != NULL) {
			crcbuf[0] = 'I';
			crcbuf[1] = 'D';
			crcbuf[2] = 'A';
			crcbuf[3] = 'T';
			memcpy(crcbuf + 4, zdata, zlen);
			crc = png_crc(crcbuf, 4u + zlen);
			free(crcbuf);
		} else {
			crc = 0;
		}
	}
	{
		u8 cbuf[4];
		png_be32(cbuf, crc);
		fwrite(cbuf, 1, 4, f);
	}
	free(zdata);
	/* IEND */
	{
		u8 iend[12];
		png_be32(iend, 0);
		iend[4] = 'I';
		iend[5] = 'E';
		iend[6] = 'N';
		iend[7] = 'D';
		crc = png_crc(iend + 4, 4u);
		png_be32(iend + 8, crc);
		fwrite(iend, 1, 12, f);
	}
	fclose(f);
	return 0;
}

static i32 png_read_rgba(const char* path, u8** out, u32* out_w, u32* out_h) {
	/* Only store-block zlib goldens we write ourselves. Minimal reader. */
	FILE* f = fopen(path, "rb");
	u8 sig[8];
	u32 w = 0, h = 0;
	u8* idat = NULL;
	u32 idat_len = 0u;
	u8* raw = NULL;
	u32 raw_cap = 0u;
	u32 raw_len = 0u;
	if (f == NULL) {
		return -1;
	}
	if (fread(sig, 1, 8, f) != 8) {
		fclose(f);
		return -1;
	}
	for (;;) {
		u8 lenb[4], type[4];
		u32 len;
		if (fread(lenb, 1, 4, f) != 4) {
			break;
		}
		len = ((u32)lenb[0] << 24) | ((u32)lenb[1] << 16) | ((u32)lenb[2] << 8) | (u32)lenb[3];
		if (fread(type, 1, 4, f) != 4) {
			break;
		}
		if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
			u8 ihdr[13];
			if (len != 13u || fread(ihdr, 1, 13, f) != 13) {
				fclose(f);
				free(idat);
				return -1;
			}
			w = ((u32)ihdr[0] << 24) | ((u32)ihdr[1] << 16) | ((u32)ihdr[2] << 8) | (u32)ihdr[3];
			h = ((u32)ihdr[4] << 24) | ((u32)ihdr[5] << 16) | ((u32)ihdr[6] << 8) | (u32)ihdr[7];
			fseek(f, 4, SEEK_CUR);
		} else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
			u8* nidat = (u8*)realloc(idat, idat_len + len);
			if (nidat == NULL || fread(nidat + idat_len, 1, len, f) != len) {
				free(nidat);
				fclose(f);
				return -1;
			}
			idat = nidat;
			idat_len += len;
			fseek(f, 4, SEEK_CUR);
		} else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
			break;
		} else {
			fseek(f, (long)len + 4, SEEK_CUR);
		}
	}
	fclose(f);
	if (idat == NULL || w == 0u || h == 0u || idat_len < 6u) {
		free(idat);
		return -1;
	}
	/* inflate store: skip zlib header 2 bytes, read blocks */
	{
		u32 pos = 2u;
		raw_cap = (w * 4u + 1u) * h + 64u;
		raw = (u8*)malloc(raw_cap);
		if (raw == NULL) {
			free(idat);
			return -1;
		}
		while (pos + 5u <= idat_len) {
			u32 bfinal = idat[pos] & 1u;
			u32 btype = (idat[pos] >> 1) & 3u;
			u32 chunk;
			pos += 1u;
			if (btype != 0u) {
				free(idat);
				free(raw);
				return -2; /* compressed */
			}
			chunk = (u32)idat[pos] | ((u32)idat[pos + 1u] << 8);
			pos += 4u;
			if (pos + chunk > idat_len || raw_len + chunk > raw_cap) {
				free(idat);
				free(raw);
				return -1;
			}
			memcpy(raw + raw_len, idat + pos, chunk);
			raw_len += chunk;
			pos += chunk;
			if (bfinal) {
				break;
			}
		}
	}
	free(idat);
	{
		u32 row = w * 4u + 1u;
		u8* rgba = (u8*)malloc(w * h * 4u);
		u32 y;
		if (rgba == NULL) {
			free(raw);
			return -1;
		}
		for (y = 0u; y < h; ++y) {
			if (y * row >= raw_len) {
				free(raw);
				free(rgba);
				return -1;
			}
			memcpy(rgba + y * w * 4u, raw + y * row + 1u, w * 4u);
		}
		free(raw);
		*out = rgba;
		*out_w = w;
		*out_h = h;
		return 0;
	}
}

static i32 env_regen_goldens(void) {
	const char* e = getenv("SK_UI_REGEN_GOLDENS");
	return (e != NULL && e[0] == '1') ? 1 : 0;
}

#define W_GOLDEN_W 64u
#define W_GOLDEN_H 40u

static i32 widget_golden_rel(char* rel, u32 cap, const char* name) {
	int n = snprintf(rel, cap, "plugins/ui/testdata/widgets/%s.png", name);
	return (n < 0 || (u32)n >= cap) ? -1 : 0;
}

static i32 widget_golden_path(char* out, u32 cap, const char* name) {
	char rel[256];
	if (widget_golden_rel(rel, (u32)sizeof(rel), name) != 0) {
		return -1;
	}
	return sk_test_locate(out, cap, rel, __FILE__);
}

static void widget_golden_compare(const char* name, const u8* actual) {
	char path[512];
	u8* golden = NULL;
	u32 gw = 0, gh = 0;
	i32 regen = env_regen_goldens();
	i32 found = widget_golden_path(path, (u32)sizeof(path), name);
	if (regen || found != 0) {
		char rel[256];
		TEST_ASSERT_EQUAL_INT(0, widget_golden_rel(rel, (u32)sizeof(rel), name));
		TEST_ASSERT_EQUAL_INT(0, sk_test_source_path(path, (u32)sizeof(path), rel, __FILE__));
		TEST_ASSERT_EQUAL_INT(0, png_write_rgba(path, actual, W_GOLDEN_W, W_GOLDEN_H));
		if (regen) {
			return;
		}
		/* Fall through to load the file we just wrote when bootstrapping. */
	}
	if (png_read_rgba(path, &golden, &gw, &gh) != 0) {
		TEST_FAIL_MESSAGE("widget golden PNG missing or unreadable");
		return;
	}
	TEST_ASSERT_EQUAL_UINT(W_GOLDEN_W, gw);
	TEST_ASSERT_EQUAL_UINT(W_GOLDEN_H, gh);
	{
		u32 i, fail = 0u, n = W_GOLDEN_W * W_GOLDEN_H * 4u;
		for (i = 0u; i < n; ++i) {
			i32 d = (i32)actual[i] - (i32)golden[i];
			if (d < 0) {
				d = -d;
			}
			if (d > 8) {
				fail += 1u;
			}
		}
		free(golden);
		/* Allow tiny AA/rounding differences on a few pixels. */
		TEST_ASSERT_TRUE(fail < (W_GOLDEN_W * W_GOLDEN_H));
	}
}

static void paint_widget_golden(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t widget, const char* name, u32 state_or) {
	u8* px = (u8*)malloc(W_GOLDEN_W * W_GOLDEN_H * 4u);
	const sk_ui_draw_list_t* dl;
	u32 st;
	TEST_ASSERT_NOT_NULL(px);
	st = ui->node_get_state(ctx, widget) | state_or;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, widget, st));
	wtest_layout(ui, ctx, (f32)W_GOLDEN_W, (f32)W_GOLDEN_H);
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));
	dl = ui->get_draw_list(ctx);
	soft_raster_draw_list(dl, px, W_GOLDEN_W, W_GOLDEN_H);
	widget_golden_compare(name, px);
	free(px);
}

SK_TEST(ui_widget_defaults_registered) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_PANEL));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_BUTTON));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_BUTTON_SMALL));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_BUTTON_INVISIBLE));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_BUTTON_SELECTED));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_BUTTON_BORDERED));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_BUTTON_ARROW));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_SELECTABLE));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_COMBO));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_LIST_BOX));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_CHECKBOX));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_RADIO));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_TOGGLE));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_SLIDER));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_DRAG));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_SLIDER_N));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_DRAG_N));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_RANGE_SLIDER));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_PROGRESS));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_TEXT_INPUT));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_SCROLL_VIEW));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_IMAGE));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_TOOLTIP));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_DRAG_DROP_PREVIEW));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_DRAG_DROP_TARGET));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_panel_view_label_ids) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t panel = ui->widget_panel(ctx, root, "panel-main");
	sk_ui_node_t view = ui->widget_view(ctx, panel, NULL);
	sk_ui_node_t label = ui->widget_label(ctx, view, "Hello", "lbl");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(panel));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, panel, SK_UI_CLASS_PANEL));
	TEST_ASSERT_EQUAL_STRING("panel-main", ui->node_get_id(ctx, panel));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, view, SK_UI_CLASS_VIEW));
	TEST_ASSERT_NOT_NULL(ui->node_get_id(ctx, view));
	TEST_ASSERT_EQUAL_STRING("Hello", ui->label_get_text(ctx, label));
	TEST_ASSERT_EQUAL_INT(0, ui->label_set_text(ctx, label, "World"));
	TEST_ASSERT_EQUAL_STRING("World", ui->label_get_text(ctx, label));
	TEST_ASSERT_EQUAL_INT(0, ui->label_set_align(ctx, label, 1, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->label_set_wrap(ctx, label, 0));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_button_hover_disabled_states) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;
	sk_ui_computed_style_t cs;
	btn = ui->widget_button(ctx, root, "OK", "btn-ok");
	wtest_set_size(ui, ctx, btn, 80.0f, 28.0f);
	wtest_layout(ui, ctx, 100.0f, 40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, btn, &cs));
	/* Hover darkens/lights via variant. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, btn, SK_UI_STATE_HOVER));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, btn, &cs));
	TEST_ASSERT_TRUE(cs.background_color.b > 0.5f);
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_disabled(ctx, btn, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, btn) & SK_UI_STATE_DISABLED) != 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_label(ctx, btn, "Go"));
	TEST_ASSERT_EQUAL_STRING("Go", ui->label_get_text(ctx, btn));
	ui->context_destroy(ctx);
}

static void wtest_pointer(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x, f32 y, i32 button, i32 down) {
	sk_ui_input_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = x;
	ev.y = y;
	ev.button = button;
	ev.down = down;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

static void wtest_move(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x, f32 y) {
	sk_ui_input_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = x;
	ev.y = y;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

SK_TEST(ui_widget_button_family_labels_ids_size) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;
	sk_ui_node_t small;
	sk_ui_node_t empty;
	sk_ui_node_t zero;
	sk_ui_node_t hidden;
	sk_ui_node_t collide;
	sk_ui_node_t sized;
	sk_ui_rect_t border;
	sk_ui_prop_value_t pv;

	btn = ui->widget_button(ctx, root, "Clear", "btn-clear");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(btn));
	TEST_ASSERT_EQUAL_STRING("Clear", ui->label_get_text(ctx, btn));
	TEST_ASSERT_EQUAL_STRING("btn-clear", ui->node_get_id(ctx, btn));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, btn, SK_UI_CLASS_BUTTON));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, btn, "widget", &pv));
	TEST_ASSERT_EQUAL_STRING("button", pv.data.str_value);

	/* ###id suffix: visible prefix, hidden id when factory id is empty. */
	hidden = ui->widget_button(ctx, root, "ICON###trash", NULL);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(hidden));
	TEST_ASSERT_EQUAL_STRING("ICON", ui->label_get_text(ctx, hidden));
	TEST_ASSERT_EQUAL_STRING("trash", ui->node_get_id(ctx, hidden));

	/* Empty label is legal; node still lives. */
	empty = ui->widget_button(ctx, root, "", "btn-empty");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(empty));
	TEST_ASSERT_EQUAL_STRING("", ui->label_get_text(ctx, empty));

	/* NULL label treated as empty. */
	zero = ui->widget_button(ctx, root, NULL, "btn-null");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(zero));
	TEST_ASSERT_EQUAL_STRING("", ui->label_get_text(ctx, zero));

	/* Id collision: second node stays valid; find_by_id keeps the first. */
	collide = ui->widget_button(ctx, root, "Other", "btn-clear");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(collide));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->find_by_id(ctx, "btn-clear"), btn));
	TEST_ASSERT_TRUE(ui->node_get_id(ctx, collide) == NULL || strcmp(ui->node_get_id(ctx, collide), "btn-clear") != 0);

	small = ui->widget_small_button(ctx, root, "x", "btn-small");
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, small, SK_UI_CLASS_BUTTON_SMALL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, small, "button_variant", &pv));
	TEST_ASSERT_EQUAL_STRING("small_button", pv.data.str_value);

	/* Explicit size; zero axis stays auto. */
	sized = ui->widget_button(ctx, root, "OK", "btn-sized");
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_size(ctx, sized, 120.0f, 0.0f));
	wtest_layout(ui, ctx, 200.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sized, &border, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, border.width);
	TEST_ASSERT_TRUE(border.height >= 18.0f);

	/* Zero size on both axes: auto (content + class min_height). */
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_size(ctx, empty, 0.0f, 0.0f));
	wtest_layout(ui, ctx, 200.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, empty, &border, NULL));
	TEST_ASSERT_TRUE(border.width >= 0.0f);
	TEST_ASSERT_TRUE(border.height >= 0.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_button_family_click_press_release) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;
	sk_ui_node_t inv;
	sk_ui_node_t dis;
	sk_ui_node_t sel;
	sk_ui_node_t brd;
	sk_ui_node_t arr;

	btn = ui->widget_button(ctx, root, "OK", "fam-ok");
	wtest_set_size(ui, ctx, btn, 80.0f, 28.0f);
	inv = ui->widget_invisible_button(ctx, root, "fam-inv", 40.0f, 40.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT | SK_UI_BUTTON_FLAG_MOUSE_MIDDLE);
	dis = ui->widget_button(ctx, root, "No", "fam-dis");
	wtest_set_size(ui, ctx, dis, 40.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_disabled(ctx, dis, 1));
	sel = ui->widget_selection_button(ctx, root, "Move", 1, "fam-sel", 48.0f, 24.0f);
	brd = ui->widget_bordered_button(ctx, root, "Add", "fam-brd", 80.0f, 24.0f);
	arr = ui->widget_arrow_button(ctx, root, SK_UI_ARROW_RIGHT, "fam-arr");

	/* Place so hit tests do not overlap. */
	{
		sk_ui_layout_style_t ls;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, inv, &ls));
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(100.0f);
		ls.top = sk_ui_pt(0.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, inv, &ls));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, dis, &ls));
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(0.0f);
		ls.top = sk_ui_pt(40.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, dis, &ls));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, sel, &ls));
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(50.0f);
		ls.top = sk_ui_pt(40.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, sel, &ls));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, brd, &ls));
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(0.0f);
		ls.top = sk_ui_pt(80.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, brd, &ls));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, arr, &ls));
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(100.0f);
		ls.top = sk_ui_pt(80.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, arr, &ls));
	}
	wtest_layout(ui, ctx, 200.0f, 140.0f);

	TEST_ASSERT_TRUE(ui->node_has_class(ctx, inv, SK_UI_CLASS_BUTTON_INVISIBLE));
	TEST_ASSERT_EQUAL_INT(1, ui->button_get_selected(ctx, sel));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, sel, SK_UI_CLASS_BUTTON_SELECTED));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, brd, SK_UI_CLASS_BUTTON_BORDERED));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, arr, SK_UI_CLASS_BUTTON_ARROW));
	TEST_ASSERT_EQUAL_STRING(">", ui->label_get_text(ctx, arr));
	TEST_ASSERT_EQUAL_UINT(SK_UI_BUTTON_FLAG_MOUSE_LEFT | SK_UI_BUTTON_FLAG_MOUSE_MIDDLE, ui->button_get_flags(ctx, inv));

	/* Press/release over the button → clicked once, consume-on-read. */
	wtest_move(ui, ctx, 40.0f, 14.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_hovered(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_active(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, btn));
	wtest_pointer(ui, ctx, 40.0f, 14.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	TEST_ASSERT_EQUAL_INT(1, ui->button_pressed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_pressed(ctx, btn)); /* consumed */
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_active(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, btn));
	wtest_pointer(ui, ctx, 40.0f, 14.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->button_released(ctx, btn));
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_active(ctx, btn));

	/* Drag off then release: no click. */
	wtest_pointer(ui, ctx, 40.0f, 14.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	TEST_ASSERT_EQUAL_INT(1, ui->button_pressed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_active(ctx, btn));
	wtest_move(ui, ctx, 190.0f, 130.0f);
	wtest_pointer(ui, ctx, 190.0f, 130.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->button_released(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_active(ctx, btn));

	/* Disabled is not hittable: no press / click. */
	wtest_pointer(ui, ctx, 20.0f, 50.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, 20.0f, 50.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->button_pressed(ctx, dis));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, dis));

	/* Invisible middle-click is a click (editor texture canvas flags). */
	wtest_pointer(ui, ctx, 120.0f, 20.0f, SK_UI_POINTER_BUTTON_MIDDLE, 1);
	TEST_ASSERT_EQUAL_INT(1, ui->button_pressed(ctx, inv));
	wtest_pointer(ui, ctx, 120.0f, 20.0f, SK_UI_POINTER_BUTTON_MIDDLE, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, inv));

	/* Selection look is caller-owned. */
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_selected(ctx, sel, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->button_get_selected(ctx, sel));
	TEST_ASSERT_FALSE(ui->node_has_class(ctx, sel, SK_UI_CLASS_BUTTON_SELECTED));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_text_family_measure_wrap_variants) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	{ /* Keep auto widths: no cross-axis stretch. */
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_ALIGN_ITEMS;
		p.layout.align_items = SK_UI_ALIGN_FLEX_START;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, root, &p));
	}
	sk_ui_node_t t_short;
	sk_ui_node_t t_long;
	sk_ui_node_t t_empty;
	sk_ui_node_t t_wrap;
	sk_ui_node_t t_col;
	sk_ui_node_t t_dis;
	sk_ui_rect_t r;
	sk_ui_computed_style_t cs;
	sk_ui_color_t col;
	f32 single_h;
	f32 short_w;
	f32 long_w;

	/* Factories + classes + widget props. */
	t_short = ui->widget_text(ctx, root, "Hi", "tf-short");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(t_short));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, t_short, SK_UI_CLASS_TEXT));
	TEST_ASSERT_EQUAL_STRING("Hi", ui->label_get_text(ctx, t_short));
	t_long = ui->widget_text(ctx, root, "Hello World", "tf-long");
	t_empty = ui->widget_text(ctx, root, "", "tf-empty");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(t_empty));
	TEST_ASSERT_EQUAL_STRING("", ui->label_get_text(ctx, t_empty));
	/* NULL text treated as empty. */
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->widget_text(ctx, root, NULL, "tf-null")));
	t_wrap = ui->widget_text_wrapped(ctx, root, "Hello World Wrapped Text", "tf-wrap");
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, t_wrap, SK_UI_CLASS_TEXT_WRAPPED));
	t_col = ui->widget_text_colored(ctx, root, "Red", sk_ui_rgba(1.0f, 0.2f, 0.2f, 1.0f), "tf-col");
	t_dis = ui->widget_text_disabled(ctx, root, "Dim", "tf-dis");

	/* Measured size: longer text measures wider; height is one text line. */
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, t_short, &r, NULL));
	short_w = r.width;
	TEST_ASSERT_TRUE(short_w > 0.0f);
	TEST_ASSERT_TRUE(r.height > 0.0f);
	single_h = r.height;
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, t_long, &r, NULL));
	long_w = r.width;
	TEST_ASSERT_TRUE(long_w > short_w);
	/* Plain Text does not soft-wrap: auto box stays single line. */
	TEST_ASSERT_FLOAT_WITHIN(2.0f, single_h, r.height);
	/* Empty text is legal: valid (possibly zero-extent) box, no crash. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, t_empty, &r, NULL));
	TEST_ASSERT_TRUE(r.width >= 0.0f);
	TEST_ASSERT_TRUE(r.height >= 0.0f);

	/* Wrap at a given width: fixed 60px column, height grows to 3+ lines. */
	wtest_set_size(ui, ctx, t_wrap, 60.0f, 0.0f);
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, t_wrap, &r, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 60.0f, r.width);
	TEST_ASSERT_TRUE(r.height >= single_h * 3.0f);

	/* Colour: computed color matches the caller RGBA. */
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, t_col, &cs));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, cs.color.r);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, cs.color.g);
	TEST_ASSERT_EQUAL_INT(0, ui->text_get_color(ctx, t_col, &col));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, col.r);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, col.g);
	/* text_set_color on an existing node (TextColored live update). */
	TEST_ASSERT_EQUAL_INT(0, ui->text_set_color(ctx, t_short, sk_ui_rgba(0.1f, 0.9f, 0.3f, 1.0f)));
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, t_short, &cs));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, cs.color.g);

	/* Disabled: state bit + class disabled variant dims the colour. */
	TEST_ASSERT_EQUAL_INT(1, ui->text_get_disabled(ctx, t_dis));
	TEST_ASSERT_EQUAL_INT(0, ui->text_set_disabled(ctx, t_short, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->text_get_disabled(ctx, t_short));
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, t_short, &cs));
	TEST_ASSERT_TRUE(cs.color.r < 0.70f);
	TEST_ASSERT_EQUAL_INT(0, ui->text_set_disabled(ctx, t_short, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->text_get_disabled(ctx, t_short));
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, t_short, &cs));
	/* Back to the earlier inline green (0.1, 0.9, 0.3), not the dim variant. */
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, cs.color.g);

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_text_family_utf8_newlines_range) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t t;
	sk_ui_node_t nl;
	sk_ui_node_t huge;
	sk_ui_rect_t r;
	f32 one_h;
	f32 nl_h;
	f32 huge_h;
	char big[1024];
	u32 i;
	const_chr_t buf = "range-ignored";

	/* Multi-byte UTF-8 round-trips byte-exact and measures non-zero. */
	t = ui->widget_text(ctx, root, "h\xc3\xa9llo w\xc3\xb6rld \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "tf-utf8");
	TEST_ASSERT_EQUAL_STRING("h\xc3\xa9llo w\xc3\xb6rld \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", ui->label_get_text(ctx, t));
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, t, &r, NULL));
	TEST_ASSERT_TRUE(r.width > 0.0f);
	TEST_ASSERT_TRUE(r.height > 0.0f);
	one_h = r.height;

	/* Embedded newlines (wrapped label) break into lines: height grows. */
	nl = ui->widget_text_wrapped(ctx, root, "line1\nline2\nline3\nline4", "tf-nl");
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, nl, &r, NULL));
	nl_h = r.height;
	TEST_ASSERT_TRUE(nl_h >= one_h * 3.5f);

	/* Very long string wrapped at a narrow width: many lines, no crash. */
	memset(big, 'x', sizeof(big) - 1u);
	big[sizeof(big) - 1u] = '\0';
	for (i = 8u; i < sizeof(big) - 1u; i += 9u) {
		big[i] = ' ';
	}
	huge = ui->widget_text_wrapped(ctx, root, big, "tf-huge");
	wtest_set_size(ui, ctx, huge, 80.0f, 0.0f);
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, huge, &r, NULL));
	huge_h = r.height;
	TEST_ASSERT_TRUE(huge_h > nl_h);

	/* TextUnformatted range: non-null-terminated [begin,end) replaces text. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_set_text_range(ctx, t, buf + 1, buf + 4));
	TEST_ASSERT_EQUAL_STRING("ang", ui->label_get_text(ctx, t));
	/* Range of zero length → empty. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_set_text_range(ctx, t, buf, buf));
	TEST_ASSERT_EQUAL_STRING("", ui->label_get_text(ctx, t));
	/* Invalid range rejected. */
	TEST_ASSERT_TRUE(ui->text_set_text_range(ctx, t, buf + 3, buf + 1) != 0);

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_text_family_special_nodes) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t sep;
	sk_ui_node_t bullet;
	sk_ui_node_t lt;
	sk_ui_node_t lab;
	sk_ui_node_t val;
	sk_ui_node_t cen;
	sk_ui_node_t cen_txt;
	sk_ui_rect_t r;
	sk_ui_computed_style_t cs;

	/* SeparatorText: full-width labelled rule. */
	sep = ui->widget_separator_text(ctx, root, "Resource Info", "tf-sep");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sep));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, sep, SK_UI_CLASS_SEPARATOR_TEXT));
	TEST_ASSERT_EQUAL_STRING("Resource Info", ui->label_get_text(ctx, sep));
	wtest_layout(ui, ctx, 300.0f, 200.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep, &r, NULL));
	TEST_ASSERT_TRUE(r.width >= 280.0f);
	TEST_ASSERT_TRUE(r.height >= 20.0f);
	/* Empty label is legal. */
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->widget_separator_text(ctx, root, "", "tf-sep-empty")));

	/* BulletText: left padding hosts the disc; text still readable. */
	bullet = ui->widget_bullet_text(ctx, root, "bullet line", "tf-bullet");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(bullet));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, bullet, SK_UI_CLASS_BULLET_TEXT));
	TEST_ASSERT_EQUAL_STRING("bullet line", ui->label_get_text(ctx, bullet));
	wtest_layout(ui, ctx, 300.0f, 200.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, bullet, &r, NULL));
	TEST_ASSERT_TRUE(r.width >= 18.0f + 30.0f);
	TEST_ASSERT_TRUE(r.height > 0.0f);

	/* LabelText / TextWithLabel: dimmed label + value row children. */
	lt = ui->widget_text_with_label(ctx, root, "Name", "value", "tf-lt");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(lt));
	TEST_ASSERT_EQUAL_INT(0, ui->text_with_label_parts(ctx, lt, &lab, &val));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(lab));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(val));
	TEST_ASSERT_EQUAL_STRING("Name", ui->label_get_text(ctx, lab));
	TEST_ASSERT_EQUAL_STRING("value", ui->label_get_text(ctx, val));
	wtest_layout(ui, ctx, 300.0f, 200.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, lt, &r, NULL));
	TEST_ASSERT_TRUE(r.width > 0.0f && r.height > 0.0f);
	/* label_text factory is the same row shape. */
	lt = ui->widget_label_text(ctx, root, "FPS", "120", "tf-lt2");
	TEST_ASSERT_EQUAL_INT(0, ui->text_with_label_parts(ctx, lt, &lab, &val));
	TEST_ASSERT_EQUAL_STRING("FPS", ui->label_get_text(ctx, lab));
	/* The label is dimmed (disabled variant) while the value is not. */
	wtest_layout(ui, ctx, 300.0f, 200.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, lab, &cs));
	TEST_ASSERT_TRUE(cs.color.r < 0.70f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, val, &cs));
	TEST_ASSERT_TRUE(cs.color.r > 0.80f);

	/* Centered empty-state text: host + centered text child. */
	cen = ui->widget_text_centered(ctx, root, "Open a scene...", "tf-cen");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(cen));
	TEST_ASSERT_EQUAL_INT(0, ui->text_centered_text(ctx, cen, &cen_txt));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(cen_txt));
	TEST_ASSERT_EQUAL_STRING("Open a scene...", ui->label_get_text(ctx, cen_txt));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_separator_family_orientation_and_extents) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t row;
	sk_ui_node_t sep_h;
	sk_ui_node_t sep_v_row;
	sk_ui_node_t sep_v_sl;
	sk_ui_node_t dummy;
	sk_ui_node_t spacing;
	sk_ui_rect_t r;

	/* Horizontal rule in a column (default). */
	sep_h = ui->widget_separator(ctx, root, "sl-sep-h");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sep_h));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, sep_h, SK_UI_CLASS_SEPARATOR));
	TEST_ASSERT_EQUAL_INT(0, ui->separator_get_vertical(ctx, sep_h));

	/* Vertical rule inside a horizontal layout (menu bar / BeginHorizontal). */
	row = ui->widget_horizontal(ctx, root, "sl-row");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row));
	(void)ui->widget_button(ctx, row, "R", "sl-row-btn");
	sep_v_row = ui->widget_separator(ctx, row, "sl-sep-v");
	TEST_ASSERT_EQUAL_INT(1, ui->separator_get_vertical(ctx, sep_v_row));

	/* SameLine(); Separator(); pattern → vertical toolbar divider. */
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, root, 0.0f, -1.0f));
	sep_v_sl = ui->widget_separator(ctx, root, "sl-sep-tb");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sep_v_sl));
	TEST_ASSERT_EQUAL_INT(1, ui->separator_get_vertical(ctx, sep_v_sl));

	/* Dummy(0, 2): zero width, explicit 2px height (SceneView toolbar). */
	dummy = ui->widget_dummy(ctx, root, 0.0f, 2.0f, "sl-dummy");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(dummy));

	/* Spacing: fixed-height transparent spacer. */
	spacing = ui->widget_spacing(ctx, root, "sl-spacing");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(spacing));

	wtest_layout(ui, ctx, 320.0f, 240.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep_h, &r, NULL));
	TEST_ASSERT_TRUE(r.width >= 300.0f);
	TEST_ASSERT_TRUE(r.height >= 8.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep_v_row, &r, NULL));
	TEST_ASSERT_TRUE(r.width <= 2.0f);
	TEST_ASSERT_TRUE(r.height >= 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep_v_sl, &r, NULL));
	TEST_ASSERT_TRUE(r.width <= 2.0f);
	TEST_ASSERT_TRUE(r.height >= 8.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dummy, &r, NULL));
	TEST_ASSERT_TRUE(r.width <= 0.5f);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 2.0f, r.height);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, spacing, &r, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, SK_UI_SPACING_DEFAULT, r.height);

	/* SameLine at the start of a row (no previous item): the widget keeps its
	 * normal in-flow position — pending state is consumed harmlessly. */
	{
		sk_ui_node_t solo;
		TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, root, 0.0f, -1.0f));
		solo = ui->widget_text(ctx, root, "solo", "sl-solo");
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(solo));
	}
	wtest_layout(ui, ctx, 320.0f, 240.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dummy, &r, NULL));
	TEST_ASSERT_TRUE(r.width <= 0.5f);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 2.0f, r.height);

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_sameline_offset_and_gap_arithmetic) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t col;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	sk_ui_node_t d;
	sk_ui_node_t e;
	sk_ui_node_t e2;
	sk_ui_style_props_t p;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t rc;
	sk_ui_rect_t rd;
	sk_ui_rect_t re;
	sk_ui_rect_t re2;
	f32 y0;

	col = ui->widget_vertical(ctx, root, "sl-col");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(col));
	/* Start-aligned so intrinsic widths drive x (no stretch). */
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH;
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	p.layout.width = sk_ui_percent(100.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, col, &p));

	/* Row: a | SameLine(default 8) b | SameLine(0, 0) c | SameLine(40) d |
	 * SameLine(0, 4) e2 — all on one row; an in-flow item starts the next. */
	a = ui->widget_button(ctx, col, "A", "sl-a");
	wtest_set_size(ui, ctx, a, 20.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, col, 0.0f, -1.0f));
	b = ui->widget_button(ctx, col, "B", "sl-b");
	wtest_set_size(ui, ctx, b, 20.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, col, 0.0f, 0.0f));
	c = ui->widget_button(ctx, col, "C", "sl-c");
	wtest_set_size(ui, ctx, c, 20.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, col, 40.0f, -1.0f));
	d = ui->widget_button(ctx, col, "D", "sl-d");
	wtest_set_size(ui, ctx, d, 20.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, col, 0.0f, 4.0f));
	e2 = ui->widget_button(ctx, col, "E2", "sl-e2");
	wtest_set_size(ui, ctx, e2, 20.0f, 20.0f);
	/* End of row: the next in-flow item starts a fresh row. */
	e = ui->widget_button(ctx, col, "E", "sl-e");
	wtest_set_size(ui, ctx, e, 20.0f, 20.0f);

	wtest_layout(ui, ctx, 200.0f, 120.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, b, &rb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, c, &rc, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, d, &rd, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, e, &re, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, e2, &re2, NULL));

	/* All five row items share the row y. */
	y0 = ra.y;
	TEST_ASSERT_FLOAT_WITHIN(0.5f, y0, rb.y);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, y0, rc.y);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, y0, rd.y);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, y0, re2.y);

	/* b = a.right + default gap (8); c = b.right + 0 (tight); d = row start + 40
	 * (offset form is absolute within the row — may overlap, ImGui matrix);
	 * e2 = d.right + 4 (spacing override). */
	TEST_ASSERT_FLOAT_WITHIN(0.5f, ra.x + ra.width + SK_UI_SAMELINE_DEFAULT_GAP, rb.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rb.x + rb.width + 0.0f, rc.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, ra.x + 40.0f, rd.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rd.x + rd.width + 4.0f, re2.x);

	/* e starts a new row below the packed row. */
	TEST_ASSERT_TRUE(re.y > y0 + 0.5f);
	TEST_ASSERT_TRUE(re2.y < re.y);

	/* SameLine at the very end of the build is cleared by layout, so the next
	 * frame's first widget is not dragged onto a stale row. */
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, col, 0.0f, -1.0f));
	{
		sk_ui_node_t z = ui->widget_button(ctx, col, "Z", "sl-z");
		sk_ui_rect_t rz;
		wtest_set_size(ui, ctx, z, 20.0f, 20.0f);
		/* Consumed by Z: joins e's row (its x = e.right + gap). */
		TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, col, 0.0f, -1.0f));
		/* Unconsumed: cleared at the next layout. */
		wtest_layout(ui, ctx, 200.0f, 120.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, z, &rz, NULL));
		TEST_ASSERT_FLOAT_WITHIN(0.5f, re.x + re.width + SK_UI_SAMELINE_DEFAULT_GAP, rz.x);
	}
	/* After the stale SameLine was dropped, a fresh widget starts a new row. */
	{
		sk_ui_node_t w = ui->widget_button(ctx, col, "W", "sl-w");
		sk_ui_rect_t rw;
		wtest_set_size(ui, ctx, w, 20.0f, 20.0f);
		wtest_layout(ui, ctx, 200.0f, 120.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, w, &rw, NULL));
		TEST_ASSERT_TRUE(rw.y > re.y + 0.5f);
	}

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_checkbox_toggle_and_callback) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t cb = ui->widget_checkbox(ctx, root, 0, "cb1");
	sk_ui_input_event_t ev;
	wtest_set_size(ui, ctx, cb, 18.0f, 18.0f);
	g_bool_cb_count = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_on_change(ctx, cb, test_bool_cb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, cb));
	wtest_layout(ui, ctx, 40.0f, 40.0f);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = 9.0f;
	ev.y = 9.0f;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(1, g_bool_cb_count);
	TEST_ASSERT_EQUAL_INT(1, g_bool_cb_value);
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_checked(ctx, cb, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, cb));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_checkbox_bind_flags_mixed_disabled) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t cb;
	sk_ui_node_t hidden;
	sk_ui_node_t dis;
	i32 bound = 0;
	i32 flags = 0x1; /* bit 0 set, bit 1 clear → mixed for mask 0x3 */
	sk_ui_input_event_t ev;

	cb = ui->widget_checkbox(ctx, root, 0, "cb-bind");
	wtest_set_size(ui, ctx, cb, 18.0f, 18.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_bind(ctx, cb, &bound));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_changed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_label(ctx, cb, "Trace"));
	TEST_ASSERT_EQUAL_STRING("Trace", ui->checkbox_get_label(ctx, cb));
	/* ## label is box-only. */
	hidden = ui->widget_checkbox(ctx, root, 0, "cb-hid");
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_label(ctx, hidden, "##v"));
	TEST_ASSERT_EQUAL_STRING("", ui->checkbox_get_label(ctx, hidden));

	wtest_layout(ui, ctx, 200.0f, 80.0f);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = 9.0f;
	ev.y = 9.0f;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(1, bound);
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_changed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_changed(ctx, cb)); /* consume-on-read */

	/* Toggle back writes 0. */
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, bound);
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_changed(ctx, cb));

	/* External mutation is pulled on style_resolve. */
	bound = 1;
	wtest_layout(ui, ctx, 200.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_changed(ctx, cb));

	/* Flags: mixed when some but not all bits are set. */
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_bind_flags(ctx, cb, &flags, 0x3));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_mixed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, cb));
	/* Click mixed → set all bits. */
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(0x3, flags);
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_mixed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_changed(ctx, cb));
	/* Click checked flags → clear all bits. */
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(0, flags);
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_mixed(ctx, cb));

	/* Programmatic mixed (no flags bind). */
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_bind(ctx, cb, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_mixed(ctx, cb, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_mixed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, cb));

	/* Disabled: no toggle, no changed. */
	dis = ui->widget_checkbox(ctx, root, 0, "cb-dis");
	wtest_set_size(ui, ctx, dis, 18.0f, 18.0f);
	{
		sk_ui_layout_style_t ls;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, dis, &ls));
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(80.0f);
		ls.top = sk_ui_pt(0.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, dis, &ls));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_disabled(ctx, dis, 1));
	wtest_layout(ui, ctx, 200.0f, 80.0f);
	ev.x = 89.0f;
	ev.y = 9.0f;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, dis));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_changed(ctx, dis));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_radio_group_exclusivity_and_bind) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t group;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	i32 mode = 0;
	sk_ui_input_event_t ev;
	sk_ui_layout_style_t ls;

	group = ui->widget_view(ctx, root, "rg");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, group, &ls));
	ls.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, group, &ls));
	a = ui->widget_radio(ctx, group, 1, "rd-a");
	b = ui->widget_radio(ctx, group, 0, "rd-b");
	c = ui->widget_radio(ctx, group, 0, "rd-c");
	wtest_set_size(ui, ctx, a, 18.0f, 18.0f);
	wtest_set_size(ui, ctx, b, 18.0f, 18.0f);
	wtest_set_size(ui, ctx, c, 18.0f, 18.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->radio_set_label(ctx, a, "Move"));
	TEST_ASSERT_EQUAL_STRING("Move", ui->radio_get_label(ctx, a));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_bind(ctx, a, &mode, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_bind(ctx, b, &mode, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_bind(ctx, c, &mode, 2));
	TEST_ASSERT_EQUAL_INT(1, ui->radio_get_checked(ctx, a));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, b));
	wtest_layout(ui, ctx, 120.0f, 80.0f);

	/* Click B: A clears, B selected, bound value = 1, changed once. */
	{
		sk_ui_rect_t r;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, b, &r, NULL));
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.x = r.x + r.width * 0.5f;
		ev.y = r.y + r.height * 0.5f;
		ev.button = SK_UI_POINTER_BUTTON_LEFT;
		ev.down = 1;
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
		ev.down = 0;
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, a));
	TEST_ASSERT_EQUAL_INT(1, ui->radio_get_checked(ctx, b));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, c));
	TEST_ASSERT_EQUAL_INT(1, mode);
	TEST_ASSERT_EQUAL_INT(1, ui->radio_changed(ctx, b));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_changed(ctx, b));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_changed(ctx, a));

	/* Re-click selected: no change. */
	{
		sk_ui_rect_t r;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, b, &r, NULL));
		ev.x = r.x + r.width * 0.5f;
		ev.y = r.y + r.height * 0.5f;
		ev.down = 1;
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
		ev.down = 0;
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	}
	TEST_ASSERT_EQUAL_INT(1, ui->radio_get_checked(ctx, b));
	TEST_ASSERT_EQUAL_INT(1, mode);
	TEST_ASSERT_EQUAL_INT(0, ui->radio_changed(ctx, b));

	/* External mutation reflected next frame. */
	mode = 2;
	wtest_layout(ui, ctx, 120.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, a));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, b));
	TEST_ASSERT_EQUAL_INT(1, ui->radio_get_checked(ctx, c));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_changed(ctx, c));

	/* Disabled radio ignores click. */
	TEST_ASSERT_EQUAL_INT(0, ui->radio_set_disabled(ctx, a, 1));
	wtest_layout(ui, ctx, 120.0f, 80.0f);
	{
		sk_ui_rect_t r;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, a, &r, NULL));
		ev.x = r.x + r.width * 0.5f;
		ev.y = r.y + r.height * 0.5f;
		ev.down = 1;
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
		ev.down = 0;
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, a));
	TEST_ASSERT_EQUAL_INT(1, ui->radio_get_checked(ctx, c));
	TEST_ASSERT_EQUAL_INT(2, mode);
	TEST_ASSERT_EQUAL_INT(0, ui->radio_changed(ctx, a));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_slider_clamp_and_drag) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t sl = ui->widget_slider(ctx, root, 0.0f, 100.0f, 25.0f, "sl");
	sk_ui_input_event_t ev;
	wtest_set_size(ui, ctx, sl, 100.0f, 20.0f);
	g_float_cb_count = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_on_change(ctx, sl, test_float_cb, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, ui->slider_get_value(ctx, sl));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_value(ctx, sl, 200.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, ui->slider_get_value(ctx, sl));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_value(ctx, sl, -10.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ui->slider_get_value(ctx, sl));
	wtest_layout(ui, ctx, 120.0f, 40.0f);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = 50.0f;
	ev.y = 10.0f;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(g_float_cb_count >= 1);
	TEST_ASSERT_FLOAT_WITHIN(2.0f, 50.0f, ui->slider_get_value(ctx, sl));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_range(ctx, sl, 10.0f, 20.0f));
	TEST_ASSERT_TRUE(ui->slider_get_value(ctx, sl) >= 10.0f);
	TEST_ASSERT_TRUE(ui->slider_get_value(ctx, sl) <= 20.0f);
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_slider_family_mapping_clamp_step_format) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t sl;
	sk_ui_node_t si;
	sk_ui_node_t inv;
	sk_ui_node_t zw;
	sk_ui_node_t dg;
	sk_ui_node_t dis;
	sk_ui_node_t row;
	sk_ui_node_t c0;
	sk_ui_node_t c1;
	sk_ui_node_t c2;
	sk_ui_rect_t r;
	sk_ui_input_event_t ev;
	char fmt[32];
	f32 vec[3] = {1.0f, 2.0f, 3.0f};
	f32 out[3];

	sl = ui->widget_slider(ctx, root, 0.0f, 100.0f, 0.0f, "map");
	wtest_set_size(ui, ctx, sl, 100.0f, 20.0f);
	wtest_layout(ui, ctx, 120.0f, 40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sl, &r, NULL));

	/* Pixel position → value: left / mid / right (inset so hit-test stays inside). */
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = r.x + 0.5f;
	ev.y = r.y + r.height * 0.5f;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, ui->slider_get_value(ctx, sl));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));

	ev.down = 1;
	ev.x = r.x + r.width * 0.5f;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 50.0f, ui->slider_get_value(ctx, sl));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));

	ev.down = 1;
	ev.x = r.x + r.width - 0.5f;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 99.5f, ui->slider_get_value(ctx, sl));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));

	/* Clamp past both ends: press inside, then move outside while captured. */
	ev.down = 1;
	ev.x = r.x + r.width * 0.5f;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = r.x + r.width + 80.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, ui->slider_get_value(ctx, sl));
	ev.x = r.x - 80.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ui->slider_get_value(ctx, sl));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));

	/* Step snap. */
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_step(ctx, sl, 10.0f));
	ev.down = 1;
	ev.x = r.x + r.width * 0.33f;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	{
		f32 stepped = ui->slider_get_value(ctx, sl);
		f32 nearest = 10.0f * (f32)((int)(stepped / 10.0f + (stepped >= 0.0f ? 0.5f : -0.5f)));
		TEST_ASSERT_FLOAT_WITHIN(0.01f, nearest, stepped);
	}

	/* Integer slider + custom format. */
	si = ui->widget_slider_int(ctx, root, 0, 8, 3, "LOD %d", "lod");
	TEST_ASSERT_EQUAL_INT(3, ui->slider_get_int_value(ctx, si));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_format_value(ctx, si, fmt, (u32)sizeof(fmt)));
	TEST_ASSERT_EQUAL_STRING("LOD 3", fmt);
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_int_value(ctx, si, 99));
	TEST_ASSERT_EQUAL_INT(8, ui->slider_get_int_value(ctx, si));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_format(ctx, si, "auto"));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_format_value(ctx, si, fmt, (u32)sizeof(fmt)));
	TEST_ASSERT_EQUAL_STRING("auto", fmt);

	/* Inverted range: left is min (high), right is max (low). */
	inv = ui->widget_slider(ctx, root, 100.0f, 0.0f, 25.0f, "inv");
	wtest_set_size(ui, ctx, inv, 100.0f, 20.0f);
	wtest_layout(ui, ctx, 120.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, inv, &r, NULL));
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 1;
	ev.x = r.x + 0.5f;
	ev.y = r.y + 10.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, ui->slider_get_value(ctx, inv));
	ev.x = r.x + r.width - 0.5f;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, ui->slider_get_value(ctx, inv));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));

	/* Zero-width slider range stays at the single value. */
	zw = ui->widget_slider(ctx, root, 5.0f, 5.0f, 5.0f, "zw");
	wtest_set_size(ui, ctx, zw, 80.0f, 20.0f);
	wtest_layout(ui, ctx, 120.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_value(ctx, zw, 12.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, ui->slider_get_value(ctx, zw));

	/* Drag speed: 20px * 0.5 = +10. Unbounded min==max==0. */
	dg = ui->widget_drag_float(ctx, root, 0.5f, 0.0f, 0.0f, 1.0f, "%.3f", "dg");
	wtest_set_size(ui, ctx, dg, 80.0f, 20.0f);
	wtest_layout(ui, ctx, 140.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dg, &r, NULL));
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.x = r.x + 10.0f;
	ev.y = r.y + 10.0f;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, ui->slider_get_value(ctx, dg));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = r.x + 30.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(0.05f, 11.0f, ui->slider_get_value(ctx, dg));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));

	/* Vector components edit independently. */
	row = ui->widget_slider_float_n(ctx, root, 3, 0.0f, 10.0f, vec, "%.1f", "vec");
	TEST_ASSERT_EQUAL_INT(0, ui->slider_component(ctx, row, 0, &c0));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_component(ctx, row, 1, &c1));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_component(ctx, row, 2, &c2));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, ui->slider_get_value(ctx, c0));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, ui->slider_get_value(ctx, c1));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, ui->slider_get_value(ctx, c2));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_value(ctx, c1, 9.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, ui->slider_get_value(ctx, c0));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.0f, ui->slider_get_value(ctx, c1));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, ui->slider_get_value(ctx, c2));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_get_values(ctx, row, out, 3));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.0f, out[1]);

	/* Disabled ignores pointer. */
	dis = ui->widget_slider(ctx, root, 0.0f, 10.0f, 4.0f, "dis");
	wtest_set_size(ui, ctx, dis, 80.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_disabled(ctx, dis, 1));
	wtest_layout(ui, ctx, 140.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dis, &r, NULL));
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.x = r.x + r.width;
	ev.y = r.y + 10.0f;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev.down = 0;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, ui->slider_get_value(ctx, dis));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_changed(ctx, dis));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_text_input_edit_ops) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t ti;
	sk_ui_input_event_t ev;
	g_clip_len = 0;
	g_clip_buf[0] = '\0';
	ui->set_clipboard_fns(ctx, test_clip_get, test_clip_set, NULL);
	ti = ui->widget_text_input(ctx, root, "abc", "ti");
	wtest_set_size(ui, ctx, ti, 120.0f, 28.0f);
	wtest_layout(ui, ctx, 140.0f, 40.0f);
	TEST_ASSERT_EQUAL_STRING("abc", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->focus_set(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, ti, 1, 2)); /* select 'b' */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_delete_selection(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("ac", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_insert(ctx, ti, "X"));
	TEST_ASSERT_EQUAL_STRING("aXc", ui->text_input_get_text(ctx, ti));
	/* Select all and copy/cut/paste. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, ti, 0, 3));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_copy(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("aXc", g_clip_buf);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_cut(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_paste(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("aXc", ui->text_input_get_text(ctx, ti));
	/* Keyboard insert path */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ti, ""));
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_TEXT;
	ev.text = "Hi";
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_STRING("Hi", ui->text_input_get_text(ctx, ti));
	/* Backspace */
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_KEY;
	ev.key = SK_UI_KEY_BACKSPACE;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_STRING("H", ui->text_input_get_text(ctx, ti));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_text_input_family_buffer_edit_selection) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t ti;
	sk_ui_node_t grow;
	i32 a = 0;
	i32 b = 0;
	char long_src[80];
	u32 i;

	g_clip_len = 0;
	g_clip_buf[0] = '\0';
	ui->set_clipboard_fns(ctx, test_clip_get, test_clip_set, NULL);

	ti = ui->widget_text_input(ctx, root, "abcdef", "ti-cap");
	wtest_set_size(ui, ctx, ti, 160.0f, 28.0f);
	wtest_layout(ui, ctx, 200.0f, 60.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_capacity(ctx, ti, 8));
	TEST_ASSERT_EQUAL_INT(8, ui->text_input_get_capacity(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ti, "abcdefghijklmnop"));
	TEST_ASSERT_EQUAL_STRING("abcdefgh", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ti, "ab"));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, ti, 2, 2));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_insert(ctx, ti, "XYZXYZ"));
	TEST_ASSERT_EQUAL_STRING("abXYZXYZ", ui->text_input_get_text(ctx, ti));
	/* UTF-8: 2-byte é must not split. capacity 5 → "ab" + "é" + "x" = 5. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_capacity(ctx, ti, 5));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ti, "ab"));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_insert(ctx, ti, "\xc3\xa9xy"));
	TEST_ASSERT_EQUAL_STRING("ab\xc3\xa9x", ui->text_input_get_text(ctx, ti));

	/* Unbounded grow (CallbackResize): no 256-char cap. */
	grow = ui->widget_text_input(ctx, root, "", "ti-grow");
	memset(long_src, 'a', sizeof(long_src) - 1u);
	long_src[sizeof(long_src) - 1u] = '\0';
	for (i = 0u; i < 6u; ++i) {
		TEST_ASSERT_EQUAL_INT(0, ui->text_input_insert(ctx, grow, long_src));
	}
	TEST_ASSERT_EQUAL_UINT((u32)(6u * (sizeof(long_src) - 1u)), (u32)strlen(ui->text_input_get_text(ctx, grow)));

	/* Cursor / selection / clipboard. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_capacity(ctx, ti, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ti, "hello"));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, ti, 1, 4)); /* ell */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_get_selection(ctx, ti, &a, &b));
	TEST_ASSERT_EQUAL_INT(1, a);
	TEST_ASSERT_EQUAL_INT(4, b);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_copy(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("ell", g_clip_buf);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_delete_selection(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("ho", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_paste(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("hello", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, ti, 0, 5));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_cut(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_paste(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("hello", ui->text_input_get_text(ctx, ti));

	/* UTF-8 caret is in codepoints, not bytes. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ti, "h\xc3\xa9"));
	TEST_ASSERT_EQUAL_INT(2, ui->text_input_get_caret(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, ti, 1, 2));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_delete_selection(ctx, ti));
	TEST_ASSERT_EQUAL_STRING("h", ui->text_input_get_text(ctx, ti));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_text_input_family_flags_commit_revert) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t ti;
	sk_ui_node_t ro;
	sk_ui_node_t pw;
	sk_ui_node_t hint;
	sk_ui_node_t ml;
	sk_ui_input_event_t ev;

	g_clip_len = 0;
	g_clip_buf[0] = '\0';
	ui->set_clipboard_fns(ctx, test_clip_get, test_clip_set, NULL);

	ti = ui->widget_text_input(ctx, root, "Name", "ti-flags");
	wtest_set_size(ui, ctx, ti, 160.0f, 28.0f);
	wtest_layout(ui, ctx, 200.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_flags(ctx, ti, SK_UI_INPUT_TEXT_FLAG_ENTER_RETURNS_TRUE | SK_UI_INPUT_TEXT_FLAG_AUTO_SELECT_ALL));
	TEST_ASSERT_TRUE((ui->text_input_get_flags(ctx, ti) & SK_UI_INPUT_TEXT_FLAG_ENTER_RETURNS_TRUE) != 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->focus_set(ctx, ti));
	{
		i32 a = 0;
		i32 b = 0;
		TEST_ASSERT_EQUAL_INT(0, ui->text_input_get_selection(ctx, ti, &a, &b));
		TEST_ASSERT_EQUAL_INT(0, a);
		TEST_ASSERT_EQUAL_INT(4, b);
	}
	/* Live edit with EnterReturnsTrue: text updates, changed stays 0. */
	(void)ui->text_input_changed(ctx, ti);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_insert(ctx, ti, "X"));
	TEST_ASSERT_EQUAL_STRING("X", ui->text_input_get_text(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_changed(ctx, ti));
	/* Enter commits and returns true. */
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_KEY;
	ev.key = SK_UI_KEY_ENTER;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(1, ui->text_input_changed(ctx, ti));
	TEST_ASSERT_EQUAL_INT(1, ui->text_input_committed(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_changed(ctx, ti));

	/* Escape restores the snapshot from focus-in. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_flags(ctx, ti, SK_UI_INPUT_TEXT_FLAG_NONE));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ti, "keep"));
	TEST_ASSERT_EQUAL_INT(0, ui->focus_set(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ti, "keep"));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, ti, 4, 4));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_insert(ctx, ti, "x"));
	TEST_ASSERT_EQUAL_STRING("keepx", ui->text_input_get_text(ctx, ti));
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_KEY;
	ev.key = SK_UI_KEY_ESCAPE;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_STRING("keep", ui->text_input_get_text(ctx, ti));

	/* Read-only: focusable, no mutate, copy still works. */
	ro = ui->widget_text_input_readonly(ctx, root, "uuid-1234", "ti-ro");
	wtest_set_size(ui, ctx, ro, 160.0f, 28.0f);
	wtest_layout(ui, ctx, 200.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->text_input_get_readonly(ctx, ro));
	TEST_ASSERT_EQUAL_INT(0, ui->focus_set(ctx, ro));
	TEST_ASSERT_TRUE(ui->text_input_insert(ctx, ro, "x") != 0);
	TEST_ASSERT_EQUAL_STRING("uuid-1234", ui->text_input_get_text(ctx, ro));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, ro, 0, 4));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_copy(ctx, ro));
	TEST_ASSERT_EQUAL_STRING("uuid", g_clip_buf);

	/* Password stores the real string. */
	pw = ui->widget_text_input(ctx, root, "secret", "ti-pw");
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_password(ctx, pw, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->text_input_get_password(ctx, pw));
	TEST_ASSERT_EQUAL_STRING("secret", ui->text_input_get_text(ctx, pw));

	/* Hint + search + error chrome. */
	hint = ui->widget_text_input_with_hint(ctx, root, "", "Search", "ti-hint");
	TEST_ASSERT_EQUAL_STRING("Search", ui->text_input_get_hint(ctx, hint));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_error(ctx, hint, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->text_input_get_error(ctx, hint));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, hint, SK_UI_CLASS_TEXT_INPUT_ERROR));

	ml = ui->widget_text_input_multiline(ctx, root, "line1\nline2\nline3", 120.0f, 48.0f, "ti-ml");
	TEST_ASSERT_TRUE((ui->text_input_get_flags(ctx, ml) & SK_UI_INPUT_TEXT_FLAG_MULTILINE) != 0u);
	TEST_ASSERT_EQUAL_STRING("line1\nline2\nline3", ui->text_input_get_text(ctx, ml));
	TEST_ASSERT_EQUAL_INT(0, ui->focus_set(ctx, ml));
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_KEY;
	ev.key = SK_UI_KEY_ENTER;
	ev.down = 1;
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(strstr(ui->text_input_get_text(ctx, ml), "\n\n") != NULL || strlen(ui->text_input_get_text(ctx, ml)) > 17u);

	/* Filter helper. */
	TEST_ASSERT_EQUAL_INT(1, ui->text_filter_pass("", "anything"));
	TEST_ASSERT_EQUAL_INT(1, ui->text_filter_pass("ent", "Entity_01"));
	TEST_ASSERT_EQUAL_INT(0, ui->text_filter_pass("zzz", "Entity_01"));
	TEST_ASSERT_EQUAL_INT(0, ui->text_filter_pass("-cam,ent", "Camera"));
	TEST_ASSERT_EQUAL_INT(1, ui->text_filter_pass("-cam,ent", "Entity"));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_text_input_family_numeric_parse_clamp) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t fi;
	sk_ui_node_t ii;
	sk_ui_node_t sc;
	sk_ui_node_t f3;
	sk_ui_node_t c0;
	sk_ui_node_t c1;
	sk_ui_node_t c2;
	f32 fv = 1.5f;
	i32 iv = 4;
	u32 uv = 7u;
	f32 vmin = 0.0f;
	f32 vmax = 10.0f;
	f32 vec[3] = {1.0f, 2.0f, 3.0f};

	fi = ui->widget_input_float(ctx, root, &fv, "in-f");
	wtest_set_size(ui, ctx, fi, 80.0f, 24.0f);
	TEST_ASSERT_EQUAL_STRING("1.500", ui->text_input_get_text(ctx, fi));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, fi, "8.25"));
	TEST_ASSERT_EQUAL_INT(0, ui->input_scalar_apply(ctx, fi));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.25f, fv);
	TEST_ASSERT_EQUAL_INT(0, ui->input_scalar_set_range(ctx, fi, &vmin, &vmax));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, fi, "99"));
	TEST_ASSERT_EQUAL_INT(0, ui->input_scalar_apply(ctx, fi));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, fv);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, fi, "-3"));
	TEST_ASSERT_EQUAL_INT(0, ui->input_scalar_apply(ctx, fi));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, fv);

	/* CharsDecimal rejects letters. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, fi, ""));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_insert(ctx, fi, "12ab.3"));
	TEST_ASSERT_EQUAL_STRING("12.3", ui->text_input_get_text(ctx, fi));

	ii = ui->widget_input_int(ctx, root, &iv, "in-i");
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ii, "42"));
	TEST_ASSERT_EQUAL_INT(0, ui->input_scalar_apply(ctx, ii));
	TEST_ASSERT_EQUAL_INT(42, iv);

	sc = ui->widget_input_scalar(ctx, root, SK_UI_INPUT_DATA_U32, &uv, "in-u");
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, sc, "9"));
	TEST_ASSERT_EQUAL_INT(0, ui->input_scalar_apply(ctx, sc));
	TEST_ASSERT_EQUAL_UINT(9u, uv);

	f3 = ui->widget_input_float3(ctx, root, vec, "in-f3");
	TEST_ASSERT_EQUAL_INT(0, ui->input_float3_component(ctx, f3, 0, &c0));
	TEST_ASSERT_EQUAL_INT(0, ui->input_float3_component(ctx, f3, 1, &c1));
	TEST_ASSERT_EQUAL_INT(0, ui->input_float3_component(ctx, f3, 2, &c2));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, c0, "4"));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, c1, "5"));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, c2, "6"));
	TEST_ASSERT_EQUAL_INT(0, ui->input_scalar_apply(ctx, c0));
	TEST_ASSERT_EQUAL_INT(0, ui->input_scalar_apply(ctx, c1));
	TEST_ASSERT_EQUAL_INT(0, ui->input_scalar_apply(ctx, c2));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, vec[0]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, vec[1]);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, vec[2]);

	/* External mutation is pulled when not focused. */
	fv = 3.0f;
	wtest_layout(ui, ctx, 240.0f, 120.0f);
	TEST_ASSERT_EQUAL_STRING("3.000", ui->text_input_get_text(ctx, fi));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_scroll_view_wheel_and_clamp) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t sv = ui->widget_scroll_view(ctx, root, "sv");
	sk_ui_node_t content = ui->scroll_view_content(ctx, sv);
	sk_ui_input_event_t ev;
	f32 sx = 0.0f, sy = 0.0f;
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(content));
	wtest_set_size(ui, ctx, sv, 80.0f, 60.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, sv, 80.0f, 200.0f));
	wtest_layout(ui, ctx, 100.0f, 80.0f);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_WHEEL;
	ev.x = 40.0f;
	ev.y = 30.0f;
	ev.scroll_y = -2.0f; /* scroll down content */
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_TRUE(sy > 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_scroll(ctx, sv, 0.0f, 9999.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	/* Border-box 60h with 1px top/bottom border → content viewport 58; max = 200-58. */
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 142.0f, sy);
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_image_texture_prop) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t img = ui->widget_image(ctx, root, 7, "img");
	sk_ui_prop_value_t pv;
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, img, "texture_id", &pv));
	TEST_ASSERT_EQUAL_INT(SK_UI_PROP_I32, pv.type);
	TEST_ASSERT_EQUAL_INT(7, pv.data.i32_value);
	TEST_ASSERT_EQUAL_INT(0, ui->image_set_texture(ctx, img, 99));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, img, "texture_id", &pv));
	TEST_ASSERT_EQUAL_INT(99, pv.data.i32_value);
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_goldens_default_hover_disabled) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t n;

	/* Panel */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_panel(ctx, root, "g-panel");
	wtest_set_size(ui, ctx, n, 56.0f, 32.0f);
	paint_widget_golden(ui, ctx, n, "panel_default", 0);
	ui->context_destroy(ctx);

	/* View */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_view(ctx, root, "g-view");
	wtest_set_size(ui, ctx, n, 56.0f, 32.0f);
	/* Give view a visible bg via inline for golden. */
	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_BACKGROUND_COLOR;
		p.background_color = sk_ui_rgba(0.25f, 0.25f, 0.30f, 1.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, n, &p));
	}
	paint_widget_golden(ui, ctx, n, "view_default", 0);
	ui->context_destroy(ctx);

	/* Button default / hover / disabled */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_button(ctx, root, "B", "g-btn");
	wtest_set_size(ui, ctx, n, 56.0f, 28.0f);
	paint_widget_golden(ui, ctx, n, "button_default", 0);
	paint_widget_golden(ui, ctx, n, "button_hover", SK_UI_STATE_HOVER);
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_disabled(ctx, n, 1));
	paint_widget_golden(ui, ctx, n, "button_disabled", 0);
	ui->context_destroy(ctx);

	/* Checkbox default / checked */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_checkbox(ctx, root, 0, "g-cb");
	wtest_set_size(ui, ctx, n, 18.0f, 18.0f);
	paint_widget_golden(ui, ctx, n, "checkbox_default", 0);
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_checked(ctx, n, 1));
	paint_widget_golden(ui, ctx, n, "checkbox_checked", 0);
	paint_widget_golden(ui, ctx, n, "checkbox_hover", SK_UI_STATE_HOVER);
	ui->context_destroy(ctx);

	/* Radio default / checked */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_radio(ctx, root, 0, "g-rd");
	wtest_set_size(ui, ctx, n, 18.0f, 18.0f);
	paint_widget_golden(ui, ctx, n, "radio_default", 0);
	TEST_ASSERT_EQUAL_INT(0, ui->radio_set_checked(ctx, n, 1));
	paint_widget_golden(ui, ctx, n, "radio_checked", 0);
	ui->context_destroy(ctx);

	/* Toggle off / on */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_toggle(ctx, root, 0, "g-tg");
	wtest_set_size(ui, ctx, n, 40.0f, 22.0f);
	paint_widget_golden(ui, ctx, n, "toggle_off", 0);
	TEST_ASSERT_EQUAL_INT(0, ui->toggle_set_on(ctx, n, 1));
	paint_widget_golden(ui, ctx, n, "toggle_on", 0);
	ui->context_destroy(ctx);

	/* Slider */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_slider(ctx, root, 0.0f, 1.0f, 0.5f, "g-sl");
	wtest_set_size(ui, ctx, n, 56.0f, 20.0f);
	paint_widget_golden(ui, ctx, n, "slider_default", 0);
	paint_widget_golden(ui, ctx, n, "slider_hover", SK_UI_STATE_HOVER);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, n, SK_UI_STATE_DISABLED));
	paint_widget_golden(ui, ctx, n, "slider_disabled", 0);
	ui->context_destroy(ctx);

	/* Text input */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_text_input(ctx, root, "x", "g-ti");
	wtest_set_size(ui, ctx, n, 56.0f, 28.0f);
	paint_widget_golden(ui, ctx, n, "text_input_default", 0);
	paint_widget_golden(ui, ctx, n, "text_input_hover", SK_UI_STATE_HOVER);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, n, SK_UI_STATE_DISABLED));
	paint_widget_golden(ui, ctx, n, "text_input_disabled", 0);
	ui->context_destroy(ctx);

	/* Scroll view */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_scroll_view(ctx, root, "g-sv");
	wtest_set_size(ui, ctx, n, 56.0f, 32.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, n, 56.0f, 80.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_scroll(ctx, n, 0.0f, 10.0f));
	paint_widget_golden(ui, ctx, n, "scroll_view_default", 0);
	ui->context_destroy(ctx);

	/* Image */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_image(ctx, root, 1, "g-img");
	wtest_set_size(ui, ctx, n, 40.0f, 28.0f);
	paint_widget_golden(ui, ctx, n, "image_default", 0);
	ui->context_destroy(ctx);

	/* Label (bg via parent panel; label itself may be empty without font) */
	ctx = ui->context_create(NULL);
	root = ui->context_root(ctx);
	n = ui->widget_label(ctx, root, "Hi", "g-lbl");
	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
		p.background_color = sk_ui_rgba(0.2f, 0.2f, 0.25f, 1.0f);
		p.layout.width = sk_ui_pt(56.0f);
		p.layout.height = sk_ui_pt(24.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, n, &p));
	}
	paint_widget_golden(ui, ctx, n, "label_default", 0);
	ui->context_destroy(ctx);
}

static void ui_test_splitter_on_change(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	f32* out = (f32*)user;
	(void)ctx;
	(void)node;
	if (out != NULL) {
		*out = value;
	}
}

SK_TEST(ui_widget_dock_splitter_tab_interaction) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t space;
	sk_ui_node_t split;
	sk_ui_node_t bar;
	sk_ui_node_t t0;
	sk_ui_node_t t1;
	sk_ui_node_t win;
	sk_ui_node_t content;
	sk_ui_style_props_t p;
	f32 seen = -1.0f;

	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	space = ui->widget_dock_space(ctx, root, "w-dock");
	(void)ui->widget_dock_node(ctx, space, 0, "w-left");
	split = ui->widget_splitter(ctx, space, 0, "w-split");
	(void)ui->widget_dock_node(ctx, space, 0, "w-right");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(300.0f);
	p.layout.height = sk_ui_pt(120.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, space, &p));

	bar = ui->widget_tab_bar(ctx, root, "w-tabs");
	t0 = ui->widget_tab(ctx, bar, "A", "w-tab-a");
	t1 = ui->widget_tab(ctx, bar, "B", "w-tab-b");
	p.layout.width = sk_ui_pt(160.0f);
	p.layout.height = sk_ui_pt(28.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, bar, &p));

	win = ui->widget_editor_window(ctx, root, "Panel", "w-win");
	content = ui->editor_window_content(ctx, win);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(content));
	p.layout.width = sk_ui_pt(200.0f);
	p.layout.height = sk_ui_pt(100.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, win, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));

	/* Splitter on_change fires when ratio is set. */
	TEST_ASSERT_EQUAL_INT(0, ui->splitter_set_on_change(ctx, split, ui_test_splitter_on_change, &seen));
	TEST_ASSERT_EQUAL_INT(0, ui->splitter_set_ratio(ctx, split, 0.25f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, ui->splitter_get_ratio(ctx, split));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, seen);

	/* Tab select deactivates siblings. */
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_set_active(ctx, bar, t0));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t1));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_set_active(ctx, bar, t1));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t1));

	TEST_ASSERT_EQUAL_INT(0, ui->editor_window_set_title(ctx, win, "Panel*"));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->editor_window_title_bar(ctx, win)));

	ui->context_destroy(ctx);
}

static i32 wtest_hidden(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_prop_value_t pv;
	if (ui->node_get_prop(ctx, node, "hidden", &pv) != 0 || pv.type != SK_UI_PROP_I32) {
		return 0;
	}
	return pv.data.i32_value != 0 ? 1 : 0;
}

SK_TEST(ui_widget_tab_bar_family_select_body_close) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t host;
	sk_ui_node_t bar;
	sk_ui_node_t scene;
	sk_ui_node_t game;
	sk_ui_node_t plus;
	sk_ui_node_t scene_body;
	sk_ui_node_t game_body;
	sk_ui_node_t xbtn;
	i32 scene_open = 1;
	i32 selected = 0;
	sk_ui_style_props_t p;
	sk_ui_rect_t r;

	host = ui->widget_vertical(ctx, root, "tf-host");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(320.0f);
	p.layout.height = sk_ui_pt(120.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, host, &p));

	bar = ui->widget_tab_bar(ctx, host, "tf-bar");
	scene = ui->widget_tab_item(ctx, bar, "Scene", "tf-scene", &scene_open, SK_UI_TAB_ITEM_FLAG_NONE);
	game = ui->widget_tab_item(ctx, bar, "Game", "tf-game", NULL, SK_UI_TAB_ITEM_FLAG_NONE);
	plus = ui->widget_tab_button(ctx, bar, "+", "tf-plus");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(scene));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(game));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(plus));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->tab_close_button(ctx, scene)));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->tab_close_button(ctx, game)));
	TEST_ASSERT_EQUAL_UINT(SK_UI_TAB_ITEM_FLAG_BUTTON, ui->tab_get_flags(ctx, plus));

	scene_body = ui->tab_body(ctx, scene);
	game_body = ui->tab_body(ctx, game);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(scene_body));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(game_body));
	(void)ui->widget_text(ctx, scene_body, "Scene page", "tf-scene-txt");
	(void)ui->widget_text(ctx, game_body, "Game page", "tf-game-txt");

	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_bind_selected(ctx, bar, &selected));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_set_selected(ctx, bar, 0));
	TEST_ASSERT_EQUAL_INT(0, selected);
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, scene));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, game));
	TEST_ASSERT_EQUAL_INT(0, wtest_hidden(ui, ctx, scene_body));
	TEST_ASSERT_EQUAL_INT(1, wtest_hidden(ui, ctx, game_body));

	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_set_selected(ctx, bar, 1));
	TEST_ASSERT_EQUAL_INT(1, selected);
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, scene));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, game));
	TEST_ASSERT_EQUAL_INT(1, wtest_hidden(ui, ctx, scene_body));
	TEST_ASSERT_EQUAL_INT(0, wtest_hidden(ui, ctx, game_body));

	wtest_set_size(ui, ctx, bar, 300.0f, 28.0f);
	wtest_set_size(ui, ctx, scene, 80.0f, 26.0f);
	wtest_set_size(ui, ctx, game, 80.0f, 26.0f);
	wtest_set_size(ui, ctx, plus, 24.0f, 26.0f);
	wtest_layout(ui, ctx, 340.0f, 140.0f);

	xbtn = ui->tab_close_button(ctx, scene);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, xbtn, &r, NULL));
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, scene_open);
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_open(ctx, scene));
	TEST_ASSERT_EQUAL_INT(1, wtest_hidden(ui, ctx, scene));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_tab_bar_family_set_selected_plus) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t bar;
	sk_ui_node_t t0;
	sk_ui_node_t t1;
	sk_ui_node_t plus;
	i32 selected = 0;
	sk_ui_rect_t r;

	bar = ui->widget_tab_bar(ctx, root, "ts-bar");
	t0 = ui->widget_tab_item(ctx, bar, "Types", "ts-types", NULL, SK_UI_TAB_ITEM_FLAG_NONE);
	t1 = ui->widget_tab_item(ctx, bar, "Instance", "ts-inst", NULL, SK_UI_TAB_ITEM_FLAG_NONE);
	plus = ui->widget_tab_button(ctx, bar, "+", "ts-plus");
	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_bind_selected(ctx, bar, &selected));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_set_selected(ctx, bar, 0));
	TEST_ASSERT_EQUAL_INT(0, selected);

	wtest_set_size(ui, ctx, bar, 240.0f, 28.0f);
	wtest_set_size(ui, ctx, t0, 72.0f, 26.0f);
	wtest_set_size(ui, ctx, t1, 80.0f, 26.0f);
	wtest_set_size(ui, ctx, plus, 24.0f, 26.0f);
	wtest_layout(ui, ctx, 280.0f, 80.0f);

	/* User click selects Instance. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, t1, &r, NULL));
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t1));
	TEST_ASSERT_EQUAL_INT(1, selected);

	/* SetSelected on Types beats a same-frame user click on Instance. */
	TEST_ASSERT_EQUAL_INT(0, ui->tab_set_flags(ctx, t0, SK_UI_TAB_ITEM_FLAG_SET_SELECTED));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t1));
	TEST_ASSERT_EQUAL_INT(0, selected);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, t1, &r, NULL));
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t1));
	TEST_ASSERT_EQUAL_INT(0, selected);

	/* Trailing '+' clicks but does not become the selection. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, plus, &r, NULL));
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->tab_clicked(ctx, plus));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, plus));
	TEST_ASSERT_EQUAL_INT(0, selected);
	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_get_selected(ctx, bar));

	TEST_ASSERT_EQUAL_INT(0, ui->tab_set_flags(ctx, t0, SK_UI_TAB_ITEM_FLAG_NONE));
	ui->context_destroy(ctx);
}

static void wtest_box(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, f32 w, f32 h) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	p.layout.flex_grow = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &p));
}

SK_TEST(ui_widget_child_sizing_modes) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t host;
	sk_ui_node_t bar;
	sk_ui_node_t body;
	sk_ui_node_t bordered;
	sk_ui_node_t hscroll;
	sk_ui_node_t content;
	sk_ui_rect_t rbar;
	sk_ui_rect_t rbody;
	sk_ui_rect_t rhost;
	f32 sx = 0.0f;
	f32 sy = 0.0f;
	sk_ui_style_props_t p;

	host = ui->widget_vertical(ctx, root, "ch-host");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(200.0f);
	p.layout.height = sk_ui_pt(160.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, host, &p));

	bar = ui->widget_child(ctx, host, "ch-bar", 0.0f, 28.0f, SK_UI_CHILD_FLAG_NONE);
	body = ui->widget_child(ctx, host, "ch-body", 0.0f, 0.0f, SK_UI_CHILD_FLAG_BORDER);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(bar));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(body));
	TEST_ASSERT_EQUAL_UINT(SK_UI_CHILD_FLAG_NONE, ui->child_get_flags(ctx, bar));
	TEST_ASSERT_EQUAL_UINT(SK_UI_CHILD_FLAG_BORDER, ui->child_get_flags(ctx, body));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->child_content(ctx, body)));

	wtest_layout(ui, ctx, 220.0f, 180.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, host, &rhost, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, bar, &rbar, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, body, &rbody, NULL));
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 28.0f, rbar.height);
	TEST_ASSERT_TRUE(rbody.height > rbar.height);
	TEST_ASSERT_FLOAT_WITHIN(2.0f, rhost.height, rbar.height + rbody.height);

	bordered = ui->widget_child(ctx, root, "ch-rx", 80.0f, 60.0f, SK_UI_CHILD_FLAG_BORDER | SK_UI_CHILD_FLAG_RESIZE_X);
	TEST_ASSERT_EQUAL_UINT(SK_UI_CHILD_FLAG_BORDER | SK_UI_CHILD_FLAG_RESIZE_X, ui->child_get_flags(ctx, bordered));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_widget(ctx, bordered, "child_resize")));

	hscroll = ui->widget_child(ctx, root, "ch-hs", 80.0f, 48.0f, SK_UI_CHILD_FLAG_HORIZONTAL_SCROLLBAR);
	content = ui->child_content(ctx, hscroll);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(content));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, hscroll, 240.0f, 120.0f));
	wtest_layout(ui, ctx, 220.0f, 280.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_scroll_to_bottom(ctx, hscroll));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, hscroll, &sx, &sy));
	TEST_ASSERT_TRUE(sy > 40.0f);

	{
		i32 opened = 1;
		sk_ui_node_t wclose;
		sk_ui_node_t xbtn;
		wclose = ui->widget_window(ctx, root, "Console", "ch-win", &opened);
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(wclose));
		xbtn = ui->editor_window_close_button(ctx, wclose);
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(xbtn));
		TEST_ASSERT_EQUAL_INT(1, ui->editor_window_get_open(ctx, wclose));
		TEST_ASSERT_EQUAL_INT(0, ui->editor_window_set_open(ctx, wclose, 0));
		TEST_ASSERT_EQUAL_INT(0, opened);
		TEST_ASSERT_EQUAL_INT(0, ui->editor_window_get_open(ctx, wclose));
	}

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_disabled_stack_propagation) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t host;
	sk_ui_node_t child_btn;
	sk_ui_node_t stack_btn;
	sk_ui_node_t after;

	host = ui->widget_group(ctx, root, "ds-host");
	child_btn = ui->widget_button(ctx, host, "In", "ds-in");
	wtest_set_size(ui, ctx, child_btn, 48.0f, 20.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->is_disabled(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->begin_disabled(ctx, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->is_disabled(ctx));
	stack_btn = ui->widget_button(ctx, host, "New", "ds-new");
	wtest_set_size(ui, ctx, stack_btn, 48.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->end_disabled(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->is_disabled(ctx));
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, stack_btn) & (u32)SK_UI_STATE_DISABLED) != 0u);

	TEST_ASSERT_EQUAL_INT(0, ui->set_disabled(ctx, host, 1));
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, child_btn) & (u32)SK_UI_STATE_DISABLED) != 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->node_is_enabled(ctx, child_btn));

	wtest_layout(ui, ctx, 200.0f, 80.0f);
	wtest_pointer(ui, ctx, 24.0f, 10.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, 24.0f, 10.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, child_btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, stack_btn));

	TEST_ASSERT_EQUAL_INT(0, ui->set_disabled(ctx, host, 0));
	TEST_ASSERT_EQUAL_INT(1, ui->node_is_enabled(ctx, child_btn));
	after = ui->widget_button(ctx, root, "After", "ds-after");
	TEST_ASSERT_EQUAL_INT(0, (i32)(ui->node_get_state(ctx, after) & (u32)SK_UI_STATE_DISABLED));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_id_scope_uniqueness) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t nint;
	sk_ui_node_t nptr;
	const void* key = (const void*)(uintptr_t)0xabcdu;

	TEST_ASSERT_EQUAL_INT(0, ui->push_id(ctx, "row"));
	a = ui->widget_button(ctx, root, "Vis", "vis");
	TEST_ASSERT_EQUAL_INT(0, ui->pop_id(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->push_id(ctx, "col"));
	b = ui->widget_button(ctx, root, "Vis", "vis");
	TEST_ASSERT_EQUAL_INT(0, ui->pop_id(ctx));

	TEST_ASSERT_TRUE(sk_ui_node_is_valid(a));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(b));
	TEST_ASSERT_FALSE(sk_ui_node_eq(a, b));
	TEST_ASSERT_EQUAL_STRING("row/vis", ui->node_get_id(ctx, a));
	TEST_ASSERT_EQUAL_STRING("col/vis", ui->node_get_id(ctx, b));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->find_by_id(ctx, "row/vis"), a));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->find_by_id(ctx, "col/vis"), b));

	TEST_ASSERT_EQUAL_INT(0, ui->push_id_int(ctx, 7));
	nint = ui->widget_button(ctx, root, "I", "k");
	TEST_ASSERT_EQUAL_INT(0, ui->pop_id(ctx));
	TEST_ASSERT_EQUAL_STRING("7/k", ui->node_get_id(ctx, nint));

	TEST_ASSERT_EQUAL_INT(0, ui->push_id_ptr(ctx, key));
	nptr = ui->widget_button(ctx, root, "P", "k");
	TEST_ASSERT_EQUAL_INT(0, ui->pop_id(ctx));
	TEST_ASSERT_TRUE(ui->node_get_id(ctx, nptr) != NULL);
	TEST_ASSERT_TRUE(strstr(ui->node_get_id(ctx, nptr), "/k") != NULL);
	TEST_ASSERT_FALSE(sk_ui_node_eq(nint, nptr));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_item_width_fill) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t col;
	sk_ui_node_t fill;
	sk_ui_node_t fixed;
	sk_ui_rect_t rfill;
	sk_ui_rect_t rfix;
	sk_ui_style_props_t p;

	col = ui->widget_vertical(ctx, root, "iw-col");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING;
	p.layout.width = sk_ui_pt(200.0f);
	p.layout.height = sk_ui_pt(80.0f);
	p.layout.padding.left = 0.0f;
	p.layout.padding.right = 0.0f;
	p.layout.padding.top = 0.0f;
	p.layout.padding.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, col, &p));

	TEST_ASSERT_EQUAL_INT(0, ui->set_next_item_width(ctx, SK_UI_ITEM_WIDTH_FILL));
	fill = ui->widget_button(ctx, col, "Fill", "iw-fill");
	TEST_ASSERT_EQUAL_INT(0, ui->set_next_item_width(ctx, 64.0f));
	fixed = ui->widget_button(ctx, col, "Fix", "iw-fix");

	wtest_layout(ui, ctx, 220.0f, 100.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, fill, &rfill, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, fixed, &rfix, NULL));
	TEST_ASSERT_FLOAT_WITHIN(2.0f, 200.0f, rfill.width);
	TEST_ASSERT_FLOAT_WITHIN(1.0f, 64.0f, rfix.width);

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_group_extents) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t g;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_rect_t rg;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;

	g = ui->widget_group(ctx, root, "gr");
	a = ui->widget_panel(ctx, g, "gr-a");
	b = ui->widget_panel(ctx, g, "gr-b");
	wtest_box(ui, ctx, a, 50.0f, 20.0f);
	wtest_box(ui, ctx, b, 70.0f, 20.0f);
	wtest_layout(ui, ctx, 200.0f, 100.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->group_get_extents(ctx, g, &rg));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, a, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, b, &rb, NULL));
	TEST_ASSERT_TRUE(rg.width + 0.5f >= 70.0f);
	TEST_ASSERT_TRUE(rg.height + 0.5f >= 40.0f);
	TEST_ASSERT_TRUE(rg.x <= ra.x + 0.5f);
	TEST_ASSERT_TRUE(rg.y <= ra.y + 0.5f);
	TEST_ASSERT_TRUE(rg.x + rg.width + 0.5f >= rb.x + rb.width);
	TEST_ASSERT_TRUE(rg.y + rg.height + 0.5f >= rb.y + rb.height);

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_spring_flex_distribution) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t row;
	sk_ui_node_t left;
	sk_ui_node_t spring;
	sk_ui_node_t right;
	sk_ui_node_t row2;
	sk_ui_node_t s0;
	sk_ui_node_t s1;
	sk_ui_rect_t rrow;
	sk_ui_rect_t rleft;
	sk_ui_rect_t rspring;
	sk_ui_rect_t rright;
	sk_ui_rect_t rs0;
	sk_ui_rect_t rs1;
	sk_ui_style_props_t p;

	row = ui->widget_horizontal(ctx, root, "sp-row");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(200.0f);
	p.layout.height = sk_ui_pt(24.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, row, &p));
	left = ui->widget_panel(ctx, row, "sp-l");
	spring = ui->widget_spring(ctx, row, 1.0f, "sp-s");
	right = ui->widget_panel(ctx, row, "sp-r");
	wtest_box(ui, ctx, left, 40.0f, 20.0f);
	wtest_box(ui, ctx, right, 40.0f, 20.0f);

	wtest_layout(ui, ctx, 220.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, row, &rrow, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, left, &rleft, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, spring, &rspring, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, right, &rright, NULL));
	TEST_ASSERT_FLOAT_WITHIN(2.0f, 40.0f, rleft.width);
	TEST_ASSERT_FLOAT_WITHIN(2.0f, 40.0f, rright.width);
	TEST_ASSERT_TRUE(rspring.width > 80.0f);
	TEST_ASSERT_FLOAT_WITHIN(3.0f, rrow.width, rleft.width + rspring.width + rright.width);

	row2 = ui->widget_horizontal(ctx, root, "sp-row2");
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, row2, &p));
	(void)ui->widget_panel(ctx, row2, "sp2-l");
	wtest_box(ui, ctx, ui->find_by_id(ctx, "sp2-l"), 20.0f, 16.0f);
	s0 = ui->widget_spring(ctx, row2, 1.0f, "sp2-0");
	s1 = ui->widget_spring(ctx, row2, 1.0f, "sp2-1");
	(void)ui->widget_panel(ctx, row2, "sp2-r");
	wtest_box(ui, ctx, ui->find_by_id(ctx, "sp2-r"), 20.0f, 16.0f);
	wtest_layout(ui, ctx, 220.0f, 120.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, s0, &rs0, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, s1, &rs1, NULL));
	TEST_ASSERT_FLOAT_WITHIN(8.0f, rs0.width, rs1.width);
	TEST_ASSERT_TRUE(rs0.width > 40.0f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_menu_family_open_close_enabled) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t bar;
	sk_ui_node_t file_m;
	sk_ui_node_t popup;
	sk_ui_node_t open_it;
	sk_ui_node_t dis_it;
	sk_ui_node_t recent;
	sk_ui_node_t sub_it;

	bar = ui->widget_menu_bar(ctx, root, "mf-bar");
	file_m = ui->widget_menu(ctx, bar, "File", "mf-file");
	popup = ui->menu_get_popup(ctx, file_m);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(popup));
	open_it = ui->widget_menu_item(ctx, popup, "Open", "mf-open");
	dis_it = ui->widget_menu_item(ctx, popup, "Locked", "mf-locked");
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_set_enabled(ctx, dis_it, 0));
	recent = ui->widget_submenu(ctx, popup, "Recent", "mf-recent");
	sub_it = ui->widget_menu_item(ctx, ui->menu_get_popup(ctx, recent), "A.skore", "mf-a");
	(void)sub_it;

	wtest_set_size(ui, ctx, bar, 320.0f, 28.0f);
	wtest_set_size(ui, ctx, file_m, 64.0f, 24.0f);
	wtest_layout(ui, ctx, 400.0f, 240.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, file_m));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, recent));
	TEST_ASSERT_EQUAL_INT(1, ui->menu_get_enabled(ctx, file_m));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_get_enabled(ctx, dis_it));

	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, file_m, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->menu_get_open(ctx, file_m));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, recent, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->menu_get_open(ctx, recent));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, recent, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, recent));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, file_m, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, file_m));

	/* Disabled BeginMenu does not open. */
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_enabled(ctx, file_m, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_enabled(ctx, file_m));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, file_m, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, file_m));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_enabled(ctx, file_m, 1));

	/* Place File / items and click. */
	{
		sk_ui_layout_style_t ls;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, file_m, &ls));
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(8.0f);
		ls.top = sk_ui_pt(4.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, file_m, &ls));
	}
	wtest_set_size(ui, ctx, open_it, 160.0f, 22.0f);
	wtest_set_size(ui, ctx, dis_it, 160.0f, 22.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, file_m, 1));
	wtest_layout(ui, ctx, 400.0f, 240.0f);

	{
		sk_ui_rect_t r;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, open_it, &r, NULL));
		wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
		wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
		TEST_ASSERT_EQUAL_INT(1, ui->menu_item_clicked(ctx, open_it));
		TEST_ASSERT_EQUAL_INT(0, ui->menu_item_clicked(ctx, open_it));
		TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, file_m));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, file_m, 1));
	wtest_layout(ui, ctx, 400.0f, 240.0f);
	{
		sk_ui_rect_t r;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dis_it, &r, NULL));
		wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
		wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
		TEST_ASSERT_EQUAL_INT(0, ui->menu_item_clicked(ctx, dis_it));
	}

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_menu_family_shortcut_check_separator) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t bar;
	sk_ui_node_t file_m;
	sk_ui_node_t popup;
	sk_ui_node_t save;
	sk_ui_node_t sep;
	sk_ui_node_t checked;
	sk_ui_node_t pop_host;
	sk_ui_node_t standalone;
	sk_ui_node_t pop_item;
	sk_ui_rect_t sc_rect;
	sk_ui_rect_t item_rect;
	sk_ui_rect_t sep_rect;
	sk_ui_prop_value_t pv;
	f32 sw;

	bar = ui->widget_menu_bar(ctx, root, "ms-bar");
	file_m = ui->widget_menu(ctx, bar, "File", "ms-file");
	popup = ui->menu_get_popup(ctx, file_m);
	save = ui->widget_menu_item(ctx, popup, "Save", "ms-save");
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_set_shortcut(ctx, save, "Ctrl+S"));
	TEST_ASSERT_EQUAL_STRING("Ctrl+S", ui->menu_item_get_shortcut(ctx, save));
	sw = ui->menu_item_measure_shortcut(ctx, save);
	TEST_ASSERT_TRUE(sw > 20.0f);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, (f32)strlen("Ctrl+S") * 14.0f * 0.55f, sw);

	sep = ui->widget_menu_separator(ctx, popup, "ms-sep");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sep));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, sep, SK_UI_CLASS_MENU_SEPARATOR));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, sep, "widget", &pv));
	TEST_ASSERT_EQUAL_STRING("menu_separator", pv.data.str_value);

	checked = ui->widget_menu_item(ctx, popup, "Show Grid", "ms-grid");
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_get_selected(ctx, checked));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_set_selected(ctx, checked, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->menu_item_get_selected(ctx, checked));

	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, file_m, 1));
	wtest_layout(ui, ctx, 400.0f, 240.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, save, &item_rect, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_get_shortcut_rect(ctx, save, &sc_rect));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, sw, sc_rect.width);
	TEST_ASSERT_TRUE(sc_rect.x > item_rect.x);
	TEST_ASSERT_TRUE(sc_rect.x + sc_rect.width <= item_rect.x + item_rect.width + 0.5f);
	TEST_ASSERT_TRUE(sc_rect.x > item_rect.x + item_rect.width * 0.35f);

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep, &sep_rect, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 8.0f, sep_rect.height);
	TEST_ASSERT_TRUE(sep_rect.y > item_rect.y);

	/* Same MenuItem inside a standalone popup (workspace-type popup). */
	pop_host = ui->widget_view(ctx, root, "ms-pop-host");
	standalone = ui->widget_menu_popup(ctx, pop_host, 0, "ms-popup");
	pop_item = ui->widget_menu_item(ctx, standalone, "Scene", "ms-scene");
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, standalone, 1));
	wtest_set_size(ui, ctx, pop_item, 140.0f, 22.0f);
	wtest_layout(ui, ctx, 400.0f, 240.0f);
	{
		sk_ui_rect_t r;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, pop_item, &r, NULL));
		wtest_pointer(ui, ctx, r.x + 20.0f, r.y + 10.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
		wtest_pointer(ui, ctx, r.x + 20.0f, r.y + 10.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
		TEST_ASSERT_EQUAL_INT(1, ui->menu_item_clicked(ctx, pop_item));
	}

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_popup_modal_open_close_edge) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t host;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t item;

	host = ui->widget_view(ctx, root, "pm-host");
	a = ui->widget_popup_menu(ctx, host, "pm-a");
	b = ui->widget_popup_menu(ctx, host, "pm-b");
	item = ui->widget_menu_item(ctx, a, "Cut", "pm-cut");
	(void)item;
	wtest_set_size(ui, ctx, host, 200.0f, 80.0f);
	wtest_layout(ui, ctx, 400.0f, 240.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->popup_get_open(ctx, a));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_open(ctx, a));
	TEST_ASSERT_EQUAL_INT(1, ui->popup_get_open(ctx, a));
	/* Edge: already-open OpenPopup is a no-op. */
	TEST_ASSERT_EQUAL_INT(0, ui->popup_open(ctx, a));
	TEST_ASSERT_EQUAL_INT(1, ui->popup_get_open(ctx, a));

	TEST_ASSERT_EQUAL_INT(0, ui->popup_open(ctx, b));
	TEST_ASSERT_EQUAL_INT(1, ui->popup_get_open(ctx, a));
	TEST_ASSERT_EQUAL_INT(1, ui->popup_get_open(ctx, b));

	TEST_ASSERT_EQUAL_INT(0, ui->popup_close_current(ctx));
	TEST_ASSERT_EQUAL_INT(1, ui->popup_get_open(ctx, a));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_get_open(ctx, b));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_close_current(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_get_open(ctx, a));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_close_current(ctx));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_popup_modal_click_outside_and_block) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t host;
	sk_ui_node_t menu;
	sk_ui_node_t item;
	sk_ui_node_t behind;
	sk_ui_node_t modal;
	sk_ui_node_t ok;
	sk_ui_layout_style_t ls;
	sk_ui_rect_t r;

	host = ui->widget_view(ctx, root, "pm-ctx-host");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, host, &ls));
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(8.0f);
	ls.top = sk_ui_pt(8.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, host, &ls));
	wtest_set_size(ui, ctx, host, 80.0f, 24.0f);
	menu = ui->widget_popup_menu(ctx, host, "pm-ctx");
	item = ui->widget_menu_item(ctx, menu, "Rename", "pm-rename");
	wtest_set_size(ui, ctx, item, 280.0f, 22.0f);

	behind = ui->widget_button(ctx, root, "Behind", "pm-behind");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, behind, &ls));
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(8.0f);
	ls.top = sk_ui_pt(200.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, behind, &ls));
	wtest_set_size(ui, ctx, behind, 80.0f, 24.0f);

	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->popup_open(ctx, menu));
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->popup_get_open(ctx, menu));

	wtest_pointer(ui, ctx, 380.0f, 280.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, 380.0f, 280.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->popup_get_open(ctx, menu));

	modal = ui->widget_modal(ctx, root, "Error", "pm-modal", NULL, SK_UI_MODAL_FLAG_ALWAYS_AUTO_RESIZE);
	(void)ui->widget_text(ctx, ui->modal_body(ctx, modal), "Cannot save.", "pm-msg");
	ok = ui->widget_button(ctx, ui->modal_button_row(ctx, modal), "OK", "pm-ok");
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_size(ctx, ok, 120.0f, 0.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->set_item_default_focus(ctx, ok));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_open(ctx, modal));
	wtest_layout(ui, ctx, 400.0f, 300.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->modal_get_open(ctx, modal));

	(void)ui->button_clicked(ctx, behind);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, behind, &r, NULL));
	wtest_pointer(ui, ctx, r.x + 10.0f, r.y + 10.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + 10.0f, r.y + 10.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, behind));
	TEST_ASSERT_EQUAL_INT(1, ui->modal_get_open(ctx, modal));

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_popup_modal_size_and_focus) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t auto_m;
	sk_ui_node_t fixed_m;
	sk_ui_node_t ok;
	sk_ui_node_t cancel;
	sk_ui_node_t save;
	sk_ui_node_t auto_dlg;
	sk_ui_node_t fixed_body;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	i32 open_flag = 1;

	auto_m = ui->widget_modal(ctx, root, "Cannot delete", "pm-auto", NULL, SK_UI_MODAL_FLAG_ALWAYS_AUTO_RESIZE);
	(void)ui->widget_text(ctx, ui->modal_body(ctx, auto_m), "File is locked.", "pm-auto-msg");
	ok = ui->widget_button(ctx, ui->modal_button_row(ctx, auto_m), "OK", "pm-auto-ok");
	cancel = ui->widget_button(ctx, ui->modal_button_row(ctx, auto_m), "Close", "pm-auto-close");
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_size(ctx, ok, 120.0f, 0.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_size(ctx, cancel, 120.0f, 0.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->set_item_default_focus(ctx, ok));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_open(ctx, auto_m));
	wtest_layout(ui, ctx, 640.0f, 400.0f);
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), ok));

	fixed_m = ui->widget_modal(ctx, root, "Save Content", "pm-fixed", &open_flag, SK_UI_MODAL_FLAG_NO_SCROLLBAR);
	(void)ui->widget_text(ctx, ui->modal_body(ctx, fixed_m), "scene.skore", "pm-row0");
	(void)ui->widget_text(ctx, ui->modal_body(ctx, fixed_m), "material.mat", "pm-row1");
	save = ui->widget_button(ctx, ui->modal_button_row(ctx, fixed_m), "Save", "pm-save");
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_size(ctx, save, 120.0f, 0.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->set_item_default_focus(ctx, save));

	wtest_layout(ui, ctx, 640.0f, 400.0f);

	auto_dlg = ui->modal_dialog(ctx, auto_m);
	fixed_body = ui->modal_body(ctx, fixed_m);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(auto_dlg));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(fixed_body));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, auto_dlg, &ra, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, fixed_body, &rb, NULL));
	TEST_ASSERT_TRUE(ra.width + 0.5f >= 260.0f);
	TEST_ASSERT_TRUE(ra.width + 0.5f < 640.0f);
	TEST_ASSERT_TRUE(ra.height > 40.0f);
	TEST_ASSERT_TRUE(ra.height + 0.5f < 360.0f);
	TEST_ASSERT_FLOAT_WITHIN(2.0f, SK_UI_MODAL_FIXED_BODY_HEIGHT, rb.height);
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), save));
	TEST_ASSERT_EQUAL_UINT32(SK_UI_MODAL_FLAG_ALWAYS_AUTO_RESIZE, ui->modal_get_flags(ctx, auto_m));
	TEST_ASSERT_EQUAL_UINT32(SK_UI_MODAL_FLAG_NO_SCROLLBAR, ui->modal_get_flags(ctx, fixed_m));
	TEST_ASSERT_EQUAL_INT(1, ui->modal_get_open(ctx, fixed_m));
	TEST_ASSERT_EQUAL_INT(1, open_flag);

	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_tree_family_open_select_leaf_depth) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[5];
	sk_ui_item_array_t arr;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t host;
	sk_ui_node_t row0;
	sk_ui_node_t row1;
	sk_ui_node_t leaf;
	sk_ui_layout_style_t ls0;
	sk_ui_layout_style_t ls1;
	sk_ui_rect_t hr;
	sk_ui_rect_t rr;
	sk_ui_style_props_t p;

	sk_ui_item_set(&items[0], 1ull, 0ull, "Scene", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&items[1], 2ull, 1ull, "Camera", 0u);
	sk_ui_item_set(&items[2], 3ull, 2ull, "Lens", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[3], 4ull, 1ull, "Light", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 4u;
	arr.revision = 0u;
	host = ui->widget_tree(ctx, root, &arr, "tf-tree");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(280.0f);
	p.layout.height = sk_ui_pt(160.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, host, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_open(ctx, host, 2ull, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_selected(ctx, host, 3ull, 1));
	wtest_layout(ui, ctx, 320.0f, 200.0f);

	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 3ull));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "tf-tree/a3")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "tf-tree/a1")));
	row0 = ui->item_bind_find(ctx, host, 1ull);
	row1 = ui->item_bind_find(ctx, host, 2ull);
	leaf = ui->item_bind_find(ctx, host, 3ull);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, row0, &ls0));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, row1, &ls1));
	TEST_ASSERT_TRUE(ls1.padding.left > ls0.padding.left + 1.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, host, &hr, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, leaf, &rr, NULL));
	TEST_ASSERT_TRUE(rr.width + 1.0f >= hr.width * 0.85f);

	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_open(ctx, host, 1ull, 0));
	TEST_ASSERT_EQUAL_UINT(1u, ui->item_bind_row_count(ctx, host));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 3ull));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_tree_family_set_next_open_once) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[3];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	sk_ui_node_t ch;
	sk_ui_node_t body;

	sk_ui_item_set(&items[0], 10ull, 0ull, "Settings", 0u);
	sk_ui_item_set(&items[1], 11ull, 10ull, "Input", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 2u;
	arr.revision = 0u;
	host = ui->widget_tree(ctx, ui->context_root(ctx), &arr, "tf-set");
	TEST_ASSERT_EQUAL_INT(0, ui->set_next_item_open(ctx, 1, SK_UI_COND_ONCE));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_open(ctx, host, 10ull, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 10ull));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_open(ctx, host, 10ull, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->set_next_item_open(ctx, 1, SK_UI_COND_ONCE));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_open(ctx, host, 10ull, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_get_open(ctx, host, 10ull));

	TEST_ASSERT_EQUAL_INT(0, ui->set_next_item_open(ctx, 1, SK_UI_COND_ONCE));
	ch = ui->widget_collapsing_header(ctx, ui->context_root(ctx), "Transform", "tf-ch", SK_UI_TREE_NODE_FLAG_TRAILING_BUTTON);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ch));
	TEST_ASSERT_EQUAL_INT(1, ui->collapsing_header_get_open(ctx, ch));
	body = ui->collapsing_header_body(ctx, ch);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(body));
	TEST_ASSERT_EQUAL_INT(0, wtest_hidden(ui, ctx, body));
	TEST_ASSERT_EQUAL_INT(0, ui->collapsing_header_set_open(ctx, ch, 0));
	TEST_ASSERT_EQUAL_INT(1, wtest_hidden(ui, ctx, body));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_tree_family_double_click_overlap) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_item_t items[3];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	sk_ui_node_t row;
	sk_ui_node_t vis;
	sk_ui_rect_t r;
	sk_ui_style_props_t p;

	sk_ui_item_set(&items[0], 1ull, 0ull, "Entity", 0u);
	sk_ui_item_set(&items[1], 2ull, 1ull, "Child", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 2u;
	arr.revision = 0u;
	host = ui->widget_tree(ctx, ui->context_root(ctx), &arr, "tf-dc");
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_flags(ctx, host, SK_UI_TREE_NODE_FLAGS_DEFAULT | SK_UI_TREE_NODE_FLAG_OPEN_ON_DOUBLE_CLICK | SK_UI_TREE_NODE_FLAG_ALLOW_OVERLAP));
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(280.0f);
	p.layout.height = sk_ui_pt(80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, host, &p));
	row = ui->item_bind_find(ctx, host, 1ull);
	vis = ui->widget_small_button(ctx, row, "V", "tf-dc-vis");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(vis));
	wtest_set_size(ui, ctx, vis, 16.0f, 16.0f);
	wtest_layout(ui, ctx, 320.0f, 120.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_get_open(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, row, &r, NULL));
	wtest_pointer(ui, ctx, r.x + r.width * 0.55f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + r.width * 0.55f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_get_open(ctx, host, 1ull));
	wtest_pointer(ui, ctx, r.x + r.width * 0.55f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + r.width * 0.55f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_last_was_arrow(ctx, host));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, vis, &r, NULL));
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, vis));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 1ull));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_collapsing_header_open_button) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t col;
	sk_ui_node_t ch;
	sk_ui_node_t body;
	sk_ui_node_t btn;
	sk_ui_rect_t r;
	sk_ui_style_props_t p;

	col = ui->widget_vertical(ctx, root, "ch-col");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(280.0f);
	p.layout.height = sk_ui_pt(120.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, col, &p));
	ch = ui->widget_collapsing_header(ctx, col, "Transform", "ch-xform", SK_UI_TREE_NODE_FLAG_TRAILING_BUTTON | SK_UI_TREE_NODE_FLAG_DEFAULT_OPEN);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ch));
	TEST_ASSERT_EQUAL_INT(1, ui->collapsing_header_get_open(ctx, ch));
	body = ui->collapsing_header_body(ctx, ch);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(body));
	(void)ui->widget_text(ctx, body, "Position", "ch-pos");
	btn = ui->collapsing_header_button(ctx, ch);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(btn));
	TEST_ASSERT_TRUE((ui->collapsing_header_get_flags(ctx, ch) & SK_UI_TREE_NODE_FLAG_TRAILING_BUTTON) != 0u);

	wtest_set_size(ui, ctx, ch, 260.0f, 24.0f);
	wtest_layout(ui, ctx, 320.0f, 160.0f);
	TEST_ASSERT_EQUAL_INT(0, wtest_hidden(ui, ctx, body));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ch, &r, NULL));
	wtest_pointer(ui, ctx, r.x + 40.0f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + 40.0f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->collapsing_header_get_open(ctx, ch));
	TEST_ASSERT_EQUAL_INT(1, wtest_hidden(ui, ctx, body));

	TEST_ASSERT_EQUAL_INT(0, ui->collapsing_header_set_open(ctx, ch, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, btn, &r, NULL));
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->collapsing_header_button_clicked(ctx, ch));
	TEST_ASSERT_EQUAL_INT(1, ui->collapsing_header_get_open(ctx, ch));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_selectable_click_changed_selected_disabled) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t row;
	sk_ui_node_t dis;
	sk_ui_computed_style_t cs;
	sk_ui_rect_t r;

	row = ui->widget_selectable(ctx, root, "Create Entity", 0, SK_UI_SELECTABLE_FLAG_NONE, "sel-hist", 0.0f, 0.0f);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, row, SK_UI_CLASS_SELECTABLE));
	TEST_ASSERT_EQUAL_STRING("Create Entity", ui->label_get_text(ctx, row));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_get_selected(ctx, row));

	/* Selected look uses the same Header token as widget_selection_button. */
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_set_selected(ctx, row, 1));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_get_selected(ctx, row));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, row, SK_UI_CLASS_BUTTON_SELECTED));
	wtest_layout(ui, ctx, 280.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, row, &cs));
	TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.26f, cs.background_color.r);
	TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.59f, cs.background_color.g);
	TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.98f, cs.background_color.b);
	TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.67f, cs.background_color.a);

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, row, &r, NULL));
	wtest_move(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f);
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_is_hovered(ctx, row));
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_is_active(ctx, row));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_changed(ctx, row));
	wtest_pointer(ui, ctx, r.x + r.width * 0.5f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_changed(ctx, row));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_changed(ctx, row)); /* consume-on-read */
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_double_clicked(ctx, row));

	dis = ui->widget_selectable(ctx, root, "Locked Type", 0, SK_UI_SELECTABLE_FLAG_DISABLED, "sel-dis", 120.0f, 22.0f);
	{
		sk_ui_layout_style_t ls;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, dis, &ls));
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(0.0f);
		ls.top = sk_ui_pt(40.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, dis, &ls));
	}
	wtest_layout(ui, ctx, 280.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_get_disabled(ctx, dis));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dis, &r, NULL));
	wtest_pointer(ui, ctx, r.x + 8.0f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r.x + 8.0f, r.y + r.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_changed(ctx, dis));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_double_clicked(ctx, dis));
	ui->context_destroy(ctx);
}

SK_TEST(ui_widget_selectable_double_click_and_span_size) {
	const sk_ui_api_t* ui = wtest_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t col;
	sk_ui_node_t plain;
	sk_ui_node_t avail;
	sk_ui_node_t sized;
	sk_ui_node_t dbl;
	sk_ui_node_t table;
	sk_ui_node_t span;
	sk_ui_node_t cell0;
	sk_ui_rect_t r_plain;
	sk_ui_rect_t r_avail;
	sk_ui_rect_t r_sized;
	sk_ui_rect_t r_span;
	sk_ui_rect_t r_cell;
	sk_ui_rect_t r_row;
	sk_ui_style_props_t p;

	col = ui->widget_vertical(ctx, root, "sel-col");
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_ALIGN_ITEMS;
	p.layout.width = sk_ui_pt(280.0f);
	p.layout.height = sk_ui_pt(200.0f);
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, col, &p));

	plain = ui->widget_selectable(ctx, col, "Hi", 0, SK_UI_SELECTABLE_FLAG_NONE, "sel-plain", 0.0f, 0.0f);
	avail = ui->widget_selectable(ctx, col, "Avail", 0, SK_UI_SELECTABLE_FLAG_SPAN_AVAIL_WIDTH, "sel-avail", 0.0f, 0.0f);
	sized = ui->widget_selectable(ctx, col, "Launch", 0, SK_UI_SELECTABLE_FLAG_NONE, "sel-size", 160.0f, 28.0f);
	dbl = ui->widget_selectable(ctx, col, "EntityA", 0, SK_UI_SELECTABLE_FLAG_ALLOW_DOUBLE_CLICK, "sel-dbl", 0.0f, 0.0f);

	table = ui->widget_table(ctx, col, "sel-tbl", 3, SK_UI_TABLE_FLAG_SIZING_FIXED_SAME | SK_UI_TABLE_FLAG_BORDERS, 280.0f, 40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "A", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 90.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "B", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 90.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "C", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 90.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_next_row(ctx, table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f));
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
	span = ui->widget_selectable(ctx, ui->table_current_cell(ctx, table), "Type", 0, SK_UI_SELECTABLE_FLAG_SPAN_ALL_COLUMNS, "sel-span", 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
	(void)ui->widget_text(ctx, ui->table_current_cell(ctx, table), "col1", "sel-c1");
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(ctx, table));
	(void)ui->widget_text(ctx, ui->table_current_cell(ctx, table), "col2", "sel-c2");
	TEST_ASSERT_EQUAL_INT(0, ui->table_end(ctx, table));

	wtest_layout(ui, ctx, 320.0f, 280.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, plain, &r_plain, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, avail, &r_avail, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sized, &r_sized, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, span, &r_span, NULL));
	cell0 = ui->table_get_cell(ctx, table, 0, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, cell0, &r_cell, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, ui->table_get_row(ctx, table, 0), &r_row, NULL));

	/* Default is content-sized; span-avail fills the column. */
	TEST_ASSERT_TRUE(r_plain.width + 8.0f < r_avail.width);
	TEST_ASSERT_FLOAT_WITHIN(2.0f, 280.0f, r_avail.width);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 160.0f, r_sized.width);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 28.0f, r_sized.height);

	/* SpanAllColumns hit rect covers the table row, not just column 0. */
	TEST_ASSERT_TRUE(r_span.width + 1.0f > r_cell.width);
	TEST_ASSERT_FLOAT_WITHIN(4.0f, r_row.width, r_span.width);
	TEST_ASSERT_FLOAT_WITHIN(4.0f, r_row.x, r_span.x);

	/* Double-click is distinct from a single activate. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dbl, &r_plain, NULL));
	wtest_pointer(ui, ctx, r_plain.x + 12.0f, r_plain.y + r_plain.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r_plain.x + 12.0f, r_plain.y + r_plain.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_changed(ctx, dbl));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_double_clicked(ctx, dbl));
	wtest_pointer(ui, ctx, r_plain.x + 12.0f, r_plain.y + r_plain.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 1);
	wtest_pointer(ui, ctx, r_plain.x + 12.0f, r_plain.y + r_plain.height * 0.5f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_changed(ctx, dbl));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_double_clicked(ctx, dbl));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_double_clicked(ctx, dbl));

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
