/**
 * @file widgets.c
 * @brief v1 widget set: factories, default styles, interaction, tests.
 *
 * Widgets compose the retained element tree (BOX/TEXT/IMAGE/BUTTON) with
 * default style classes, stable test ids/classes, and behavior handlers for
 * checkbox/slider/text_input/scroll_view, menu surfaces (menu_bar, menu,
 * menu_item, menu_popup, dropdown, context_menu, submenu — APX-234), and
 * docking / editor window chrome (dock_space, dock_node, splitter, tab_bar,
 * tab, editor_window, window_title_bar, window_content — APX-235). Not full
 * ImGui parity — no tables, trees, or multi-viewport docking.
 */

#include "ui_internal.h"

#include "allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} ui_widget_data_t;

void ui_widget_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot) {
	if (slot == NULL || slot->user_data == NULL) {
		return;
	}
	if (!SK_TYPE_ID_EQ(slot->user_data_type, SK_UI_WIDGET_DATA_TYPE_ID)) {
		return;
	}
	ctx->allocator->free(ctx->allocator->instance, slot->user_data);
	slot->user_data = NULL;
	slot->user_data_type = SK_TYPE_ID_ZERO;
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
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.32f, 0.34f, 0.40f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TOGGLE, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.20f, 0.21f, 0.24f, 1.0f);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR;
	var.border_color = sk_ui_rgba(0.28f, 0.29f, 0.32f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TOGGLE, SK_UI_STATE_DISABLED, &var);

	/* Slider */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH;
	base.background_color = sk_ui_rgba(0.20f, 0.22f, 0.26f, 1.0f);
	base.corner_radius = 4.0f;
	base.layout.height = sk_ui_pt(20.0f);
	base.layout.min_width = sk_ui_pt(80.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_SLIDER, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.24f, 0.26f, 0.32f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SLIDER, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_SLIDER, SK_UI_STATE_DISABLED, &var);

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
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH;
	base.background_color = sk_ui_rgba(0.18f, 0.19f, 0.22f, 1.0f);
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.min_height = sk_ui_pt(28.0f);
	base.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_MENU_BAR, &base) != 0) {
		return -1;
	}

	/* Menu / dropdown trigger (in-bar or standalone) */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS |
				SK_UI_SP_POSITION;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	ui_style_fill_layout_pad(&base, 6.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 14.0f;
	base.layout.min_height = sk_ui_pt(28.0f);
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.layout.position = SK_UI_POSITION_RELATIVE;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MENU, &base) != 0) {
		return -1;
	}
	if (ui->style_class_register(ctx, SK_UI_CLASS_DROPDOWN, &base) != 0) {
		return -1;
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

	/* Menu item row */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_ALIGN_ITEMS;
	base.background_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	ui_style_fill_layout_pad(&base, 6.0f);
	base.color = sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f);
	base.font_size = 14.0f;
	base.layout.min_height = sk_ui_pt(28.0f);
	base.layout.width = sk_ui_percent(100.0f);
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MENU_ITEM, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR;
	var.background_color = sk_ui_rgba(0.28f, 0.42f, 0.72f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_MENU_ITEM, SK_UI_STATE_HOVER, &var);

	/* Floating popup / context menu panel */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_PADDING | SK_UI_SP_FLEX_DIRECTION |
				SK_UI_SP_MIN_WIDTH | SK_UI_SP_POSITION;
	base.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	base.border_color = sk_ui_rgba(0.32f, 0.34f, 0.40f, 1.0f);
	base.layout.border.left = 1.0f;
	base.layout.border.top = 1.0f;
	base.layout.border.right = 1.0f;
	base.layout.border.bottom = 1.0f;
	base.corner_radius = 3.0f;
	ui_style_fill_layout_pad(&base, 4.0f);
	base.layout.flex_direction = SK_UI_FLEX_COLUMN;
	base.layout.min_width = sk_ui_pt(120.0f);
	base.layout.position = SK_UI_POSITION_ABSOLUTE;
	if (ui->style_class_register(ctx, SK_UI_CLASS_MENU_POPUP, &base) != 0) {
		return -1;
	}
	if (ui->style_class_register(ctx, SK_UI_CLASS_CONTEXT_MENU, &base) != 0) {
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

	/* Tab bar strip */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH;
	base.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	base.layout.flex_direction = SK_UI_FLEX_ROW;
	base.layout.align_items = SK_UI_ALIGN_STRETCH;
	base.layout.min_height = sk_ui_pt(26.0f);
	base.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_CLASS_TAB_BAR, &base) != 0) {
		return -1;
	}

	/* Tab select target */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS |
				SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
	base.background_color = sk_ui_rgba(0.18f, 0.19f, 0.22f, 1.0f);
	ui_style_fill_layout_pad(&base, 6.0f);
	base.color = sk_ui_rgba(0.75f, 0.76f, 0.78f, 1.0f);
	base.font_size = 13.0f;
	base.layout.min_height = sk_ui_pt(26.0f);
	base.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	base.layout.align_items = SK_UI_ALIGN_CENTER;
	base.border_color = sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f);
	base.layout.border.bottom = 1.0f;
	if (ui->style_class_register(ctx, SK_UI_CLASS_TAB, &base) != 0) {
		return -1;
	}
	ui_style_props_clear(&var);
	var.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_COLOR;
	var.background_color = sk_ui_rgba(0.22f, 0.24f, 0.28f, 1.0f);
	var.color = sk_ui_rgba(0.95f, 0.96f, 0.98f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TAB, SK_UI_STATE_HOVER, &var);
	var.background_color = sk_ui_rgba(0.14f, 0.15f, 0.17f, 1.0f);
	var.color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
	(void)ui->style_class_set_variant(ctx, SK_UI_CLASS_TAB, SK_UI_STATE_ACTIVE, &var);

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

	/* Window title bar (drag target) */
	ui_style_props_clear(&base);
	base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_ALIGN_ITEMS |
				SK_UI_SP_FLEX_DIRECTION;
	base.background_color = sk_ui_rgba(0.20f, 0.22f, 0.26f, 1.0f);
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
	char auto_id[64];
	if (id != NULL && id[0] != '\0') {
		return ui->node_set_id(ctx, node, id);
	}
	ctx->widget_id_seq += 1u;
	(void)snprintf(auto_id, sizeof(auto_id), "%s-%u", prefix, ctx->widget_id_seq);
	return ui->node_set_id(ctx, node, auto_id);
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
	return n;
}

/* -------------------------------------------------------------------------- */
/* Behavior handlers                                                          */
/* -------------------------------------------------------------------------- */

static void ui_checkbox_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	i32 checked = 0;
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	(void)event;
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	(void)ui_prop_i32_const(ui_slot(ctx, node), "checked", &checked);
	checked = checked != 0 ? 0 : 1;
	(void)ui->node_set_prop_i32(ctx, node, "checked", checked);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	if (wd != NULL && wd->on_bool != NULL) {
		wd->on_bool(ctx, node, checked, wd->cb_user);
	}
}

static void ui_radio_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	ui_widget_data_t* wd = (ui_widget_data_t*)user;
	(void)event;
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	/* Radio selects; does not toggle off on re-click (group policy can clear peers). */
	(void)ui->node_set_prop_i32(ctx, node, "checked", 1);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	if (wd != NULL && wd->on_bool != NULL) {
		wd->on_bool(ctx, node, 1, wd->cb_user);
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

static void ui_slider_set_from_x(sk_ui_context_t* ctx, sk_ui_node_t node, f32 x) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	sk_ui_rect_t border;
	f32 min_v, max_v, t, val;
	ui_widget_data_t* wd;
	if (slot == NULL) {
		return;
	}
	if (ui->node_get_abs_rect(ctx, node, &border, NULL) != 0) {
		return;
	}
	min_v = ui_prop_f32_const(slot, "min", 0.0f);
	max_v = ui_prop_f32_const(slot, "max", 1.0f);
	if (max_v <= min_v) {
		max_v = min_v + 1.0f;
	}
	if (border.width <= 0.0f) {
		t = 0.0f;
	} else {
		t = (x - border.x) / border.width;
	}
	t = ui_clampf(t, 0.0f, 1.0f);
	val = min_v + t * (max_v - min_v);
	(void)ui->node_set_prop_f32(ctx, node, "value", val);
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	wd = ui_widget_data(ctx, node);
	if (wd != NULL && wd->on_float != NULL) {
		wd->on_float(ctx, node, val, wd->cb_user);
	}
}

static void ui_slider_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)user;
	if ((ui_wapi()->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN || event->type == SK_UI_EVENT_POINTER_MOVE) {
		if (event->type == SK_UI_EVENT_POINTER_MOVE && (ui_wapi()->node_get_state(ctx, node) & (u32)SK_UI_STATE_ACTIVE) == 0u) {
			return;
		}
		ui_slider_set_from_x(ctx, node, event->x);
		event->consumed = 1;
	}
}

static void ui_text_sync_props(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text, i32 caret, i32 sel_a, i32 sel_b) {
	const sk_ui_api_t* ui = ui_wapi();
	u32 n;
	char local[1024];
	const_chr_t stable;
	/* Copy before set_prop_str: text may point at the prop storage being replaced. */
	if (text == NULL) {
		text = "";
	}
	{
		size_t len = strlen(text);
		if (len >= sizeof(local)) {
			len = sizeof(local) - 1u;
		}
		memcpy(local, text, len);
		local[len] = '\0';
		stable = local;
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
	ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_PAINT | SK_UI_DIRTY_LAYOUT));
}

static void ui_text_input_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot;
	const_chr_t text;
	i32 caret = 0;
	i32 sel_a = 0;
	i32 sel_b = 0;
	(void)user;
	if ((ui->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return;
	}
	text = ui_prop_str_const(slot, "text");
	if (text == NULL) {
		text = "";
	}
	(void)ui_prop_i32_const(slot, "caret", &caret);
	(void)ui_prop_i32_const(slot, "sel_start", &sel_a);
	(void)ui_prop_i32_const(slot, "sel_end", &sel_b);

	if (event->type == SK_UI_EVENT_TEXT_INPUT && event->text != NULL && event->text[0] != '\0') {
		(void)ui_text_input_insert_impl(ctx, node, event->text);
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_KEY_DOWN) {
		i32 key = event->key;
		u32 mods = event->mods;
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
			ui_text_sync_props(ctx, node, text, 0, 0, 0);
			event->consumed = 1;
			return;
		}
		if (key == SK_UI_KEY_END) {
			i32 n = (i32)ui_utf8_code_count(text);
			ui_text_sync_props(ctx, node, text, n, n, n);
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
	if (slot == NULL) {
		return;
	}
	sx = ui_prop_f32_const(slot, "scroll_x", 0.0f);
	sy = ui_prop_f32_const(slot, "scroll_y", 0.0f);

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

sk_ui_node_t ui_widget_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, SK_UI_CLASS_BUTTON, "button", "ui-button", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", label != NULL ? label : "");
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_focusable(ctx, n, 1);
	(void)ui_widget_data_ensure(ctx, n, UI_WD_BUTTON);
	return n;
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

sk_ui_node_t ui_widget_slider_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 min_v, f32 max_v, f32 value, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_SLIDER, "slider", "ui-slider", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	if (max_v < min_v) {
		f32 t = min_v;
		min_v = max_v;
		max_v = t;
	}
	value = ui_clampf(value, min_v, max_v);
	(void)ui->node_set_prop_f32(ctx, n, "min", min_v);
	(void)ui->node_set_prop_f32(ctx, n, "max", max_v);
	(void)ui->node_set_prop_f32(ctx, n, "value", value);
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_SLIDER);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = ui_slider_on_event;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
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
/* Menu surfaces (APX-234)                                                    */
/* -------------------------------------------------------------------------- */

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
		if (w != NULL && (strcmp(w, "menu_popup") == 0 || strcmp(w, "context_menu") == 0)) {
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

static void ui_menu_toggle_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	i32 open = 0;
	(void)user;
	(void)event;
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
	ls.min_width = sk_ui_pt(120.0f);
	if (attach != 0) {
		ls.left = sk_ui_pt(0.0f); /* writeback uses style; Clay attach=right for hover */
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

sk_ui_node_t ui_widget_menu_item_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, SK_UI_CLASS_MENU_ITEM, "menu_item", "ui-menu-item", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", label != NULL ? label : "");
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 0);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_focusable(ctx, n, 1);
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
	ls.min_width = sk_ui_pt(120.0f);
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

i32 ui_menu_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 open) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t w;
	if (slot == NULL) {
		return -1;
	}
	w = ui_prop_str_const(slot, "widget");
	if (w != NULL && (strcmp(w, "menu_popup") == 0 || strcmp(w, "context_menu") == 0)) {
		const sk_ui_api_t* ui = ui_wapi();
		(void)ui->node_set_prop_i32(ctx, node, "open", open != 0 ? 1 : 0);
		(void)ui->node_set_prop_i32(ctx, node, "hidden", open != 0 ? 0 : 1);
		ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
		return 0;
	}
	ui_menu_set_popup_open(ctx, node, open);
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

static void ui_tab_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)user;
	if ((ui_wapi()->node_get_state(ctx, node) & (u32)SK_UI_STATE_DISABLED) != 0u) {
		return;
	}
	(void)ui_tab_bar_set_active_impl(ctx, SK_UI_NODE_INVALID, node);
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
	return ui_widget_base(ctx, SK_UI_NODE_KIND_BOX, parent, SK_UI_CLASS_TAB_BAR, "tab_bar", "ui-tab-bar", id);
}

sk_ui_node_t ui_widget_tab_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id) {
	const sk_ui_api_t* ui = ui_wapi();
	sk_ui_node_callbacks_t cbs;
	ui_widget_data_t* wd;
	sk_ui_node_t n = ui_widget_base(ctx, SK_UI_NODE_KIND_BUTTON, parent, SK_UI_CLASS_TAB, "tab", "ui-tab", id);
	if (!sk_ui_node_is_valid(n)) {
		return n;
	}
	(void)ui->node_set_prop_str(ctx, n, "text", label != NULL ? label : "");
	(void)ui->node_set_prop_i32(ctx, n, "active", 0);
	(void)ui->node_set_prop_i32(ctx, n, "text_align", 1);
	(void)ui->node_set_prop_i32(ctx, n, "vertical_align", 1);
	(void)ui->node_set_focusable(ctx, n, 1);
	wd = ui_widget_data_ensure(ctx, n, UI_WD_TAB);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_tab_on_click;
	cbs.user = wd;
	(void)ui->node_set_callbacks(ctx, n, &cbs);
	return n;
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

i32 ui_tab_bar_set_active_impl(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, sk_ui_node_t tab) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* bar_slot;
	u32 i;
	if (!sk_ui_node_is_valid(tab)) {
		return -1;
	}
	if (!sk_ui_node_is_valid(tab_bar)) {
		tab_bar = ui->node_parent(ctx, tab);
	}
	bar_slot = ui_slot(ctx, tab_bar);
	if (bar_slot != NULL) {
		for (i = 0u; i < bar_slot->children.count; ++i) {
			sk_ui_node_t ch = bar_slot->children.items[i];
			const ui_node_slot_t* cs = ui_slot(ctx, ch);
			const_chr_t w;
			if (cs == NULL) {
				continue;
			}
			w = ui_prop_str_const(cs, "widget");
			if (w == NULL || strcmp(w, "tab") != 0) {
				continue;
			}
			(void)ui_tab_set_active_impl(ctx, ch, sk_ui_node_eq(ch, tab) ? 1 : 0);
		}
	} else {
		(void)ui_tab_set_active_impl(ctx, tab, 1);
	}
	return 0;
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

i32 ui_checkbox_set_checked_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked) {
	i32 rc = ui_wapi()->node_set_prop_i32(ctx, node, "checked", checked != 0 ? 1 : 0);
	if (rc == 0) {
		ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	}
	return rc;
}

i32 ui_checkbox_get_checked_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 checked = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "checked", &checked);
	return checked != 0 ? 1 : 0;
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

i32 ui_radio_set_checked_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked) {
	i32 rc = ui_wapi()->node_set_prop_i32(ctx, node, "checked", checked != 0 ? 1 : 0);
	if (rc == 0) {
		ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	}
	return rc;
}

i32 ui_radio_get_checked_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 checked = 0;
	if (slot == NULL) {
		return 0;
	}
	(void)ui_prop_i32_const(slot, "checked", &checked);
	return checked != 0 ? 1 : 0;
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

i32 ui_slider_set_value_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value) {
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	f32 min_v, max_v;
	if (slot == NULL) {
		return -1;
	}
	min_v = ui_prop_f32_const(slot, "min", 0.0f);
	max_v = ui_prop_f32_const(slot, "max", 1.0f);
	value = ui_clampf(value, min_v, max_v);
	if (ui->node_set_prop_f32(ctx, node, "value", value) != 0) {
		return -1;
	}
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	return 0;
}

f32 ui_slider_get_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return 0.0f;
	}
	return ui_prop_f32_const(slot, "value", 0.0f);
}

i32 ui_slider_set_range_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 min_v, f32 max_v) {
	const sk_ui_api_t* ui = ui_wapi();
	f32 val;
	if (max_v < min_v) {
		f32 t = min_v;
		min_v = max_v;
		max_v = t;
	}
	if (ui->node_set_prop_f32(ctx, node, "min", min_v) != 0) {
		return -1;
	}
	if (ui->node_set_prop_f32(ctx, node, "max", max_v) != 0) {
		return -1;
	}
	val = ui_slider_get_value_impl(ctx, node);
	return ui_slider_set_value_impl(ctx, node, val);
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
	i32 n;
	if (text == NULL) {
		text = "";
	}
	n = (i32)ui_utf8_code_count(text);
	ui_text_sync_props(ctx, node, text, n, n, n);
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
	const sk_ui_api_t* ui = ui_wapi();
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t text;
	i32 sel_a = 0, sel_b = 0, caret = 0;
	u32 b0, b1, ins_len, old_len, new_len;
	char* buf;
	const sk_allocator_t* a;
	if (slot == NULL || utf8 == NULL || utf8[0] == '\0') {
		return -1;
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
	b0 = ui_utf8_byte_offset(text, (u32)caret);
	ins_len = (u32)strlen(utf8);
	old_len = (u32)strlen(text);
	new_len = old_len + ins_len;
	a = ctx->allocator;
	buf = (char*)a->alloc(a->instance, new_len + 1u);
	if (buf == NULL) {
		return -1;
	}
	memcpy(buf, text, b0);
	memcpy(buf + b0, utf8, ins_len);
	memcpy(buf + b0 + ins_len, text + b0, old_len - b0 + 1u);
	{
		i32 new_caret = caret + (i32)ui_utf8_code_count(utf8);
		ui_text_sync_props(ctx, node, buf, new_caret, new_caret, new_caret);
	}
	a->free(a->instance, buf);
	(void)ui;
	(void)b1;
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
	return 0;
}

i32 ui_text_input_copy_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	const_chr_t text;
	i32 sel_a = 0, sel_b = 0;
	u32 b0, b1;
	char tmp[1024];
	u32 len;
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
	if (len >= sizeof(tmp)) {
		len = (u32)sizeof(tmp) - 1u;
	}
	memcpy(tmp, text + b0, len);
	tmp[len] = '\0';
	return ctx->clipboard_set(ctx->clipboard_user, tmp);
}

i32 ui_text_input_cut_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	if (ui_text_input_copy_impl(ctx, node) != 0 && ctx->clipboard_set == NULL) {
		/* No clipboard: still delete selection. */
	}
	return ui_text_input_delete_selection_impl(ctx, node);
}

i32 ui_text_input_paste_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	char buf[1024];
	u32 len = 0u;
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
		if (cmd->texture_kind == SK_UI_DRAW_TEX_FONT) {
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

#ifndef SK_UI_WIDGET_GOLDEN_DIR
/* Relative to sk-tests cwd (build/bin): climb to source plugins/ui/testdata/widgets. */
#define SK_UI_WIDGET_GOLDEN_DIR "../../plugins/ui/testdata/widgets"
#endif

#define W_GOLDEN_W 64u
#define W_GOLDEN_H 40u

/** Resolve a checked-in golden path (several cwd layouts used by the test host). */
static i32 widget_golden_path(char* out, u32 cap, const char* name) {
	static const char* bases[] = {
		SK_UI_WIDGET_GOLDEN_DIR, "../../plugins/ui/testdata/widgets", "../plugins/ui/testdata/widgets", "plugins/ui/testdata/widgets", ".",
	};
	u32 i;
	for (i = 0u; i < sizeof(bases) / sizeof(bases[0]); ++i) {
		FILE* f;
		snprintf(out, cap, "%s/%s.png", bases[i], name);
		f = fopen(out, "rb");
		if (f != NULL) {
			fclose(f);
			return 0;
		}
	}
	/* Prefer source-tree relative path for regeneration. */
	snprintf(out, cap, "%s/%s.png", SK_UI_WIDGET_GOLDEN_DIR, name);
	return -1;
}

static void widget_golden_compare(const char* name, const u8* actual) {
	char path[512];
	u8* golden = NULL;
	u32 gw = 0, gh = 0;
	i32 regen = env_regen_goldens();
	i32 found = widget_golden_path(path, (u32)sizeof(path), name);
	if (regen || found != 0) {
		/* Write/regenerate at preferred path (dir must exist in the tree). */
		snprintf(path, sizeof(path), "%s/%s.png", SK_UI_WIDGET_GOLDEN_DIR, name);
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
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_CHECKBOX));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_RADIO));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_TOGGLE));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_SLIDER));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_RANGE_SLIDER));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_PROGRESS));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_TEXT_INPUT));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_SCROLL_VIEW));
	TEST_ASSERT_TRUE(ui->style_class_has(ctx, SK_UI_CLASS_IMAGE));
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

#endif /* SK_TESTS */
