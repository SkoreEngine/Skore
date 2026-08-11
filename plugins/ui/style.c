/**
 * @file style.c
 * @brief Named style classes, inline overrides, state variants, and resolve.
 *
 * Not CSS: no selectors, no stylesheet cascade beyond the fixed precedence
 *   inline > later class > earlier class > inherited (text) > default
 * Within each class: base → hover → focused → active → disabled.
 * Resolve runs on STYLE-dirty nodes (parent before child for inheritance).
 */

#include "ui_internal.h"

#include "allocator.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* String / color helpers                                                     */
/* -------------------------------------------------------------------------- */

static char* ui_style_strdup(const sk_allocator_t* a, const_chr_t src) {
	size_t len;
	char* dst;
	if (src == NULL) {
		return NULL;
	}
	len = strlen(src);
	dst = (char*)a->alloc(a->instance, len + 1u);
	if (dst == NULL) {
		return NULL;
	}
	memcpy(dst, src, len + 1u);
	return dst;
}

static i32 ui_style_cstr_eq(const_chr_t a, const_chr_t b) {
	if (a == b) {
		return 1;
	}
	if (a == NULL || b == NULL) {
		return 0;
	}
	return strcmp(a, b) == 0 ? 1 : 0;
}

static sk_ui_color_t ui_color_transparent(void) {
	return sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
}

static sk_ui_color_t ui_color_black(void) {
	return sk_ui_rgba(0.0f, 0.0f, 0.0f, 1.0f);
}

static i32 ui_f32_bits_eq(f32 a, f32 b) {
	u32 ia;
	u32 ib;
	memcpy(&ia, &a, sizeof(ia));
	memcpy(&ib, &b, sizeof(ib));
	return ia == ib ? 1 : 0;
}

static i32 ui_color_eq(sk_ui_color_t a, sk_ui_color_t b) {
	return ui_f32_bits_eq(a.r, b.r) && ui_f32_bits_eq(a.g, b.g) && ui_f32_bits_eq(a.b, b.b) && ui_f32_bits_eq(a.a, b.a);
}

/* -------------------------------------------------------------------------- */
/* Prop clear / free / copy                                                   */
/* -------------------------------------------------------------------------- */

void ui_style_props_clear(sk_ui_style_props_t* props) {
	if (props == NULL) {
		return;
	}
	memset(props, 0, sizeof(*props));
	ui_layout_style_init_default(&props->layout);
	props->background_color = ui_color_transparent();
	props->border_color = ui_color_transparent();
	props->corner_radius = 0.0f;
	props->opacity = 1.0f;
	props->font_family = NULL;
	props->font_size = 16.0f;
	props->color = ui_color_black();
	props->mask = 0u;
}

void ui_style_props_free_owned(const sk_allocator_t* a, sk_ui_style_props_t* props) {
	if (props == NULL) {
		return;
	}
	if ((props->mask & SK_UI_SP_FONT_FAMILY) != 0u && props->font_family != NULL) {
		a->free(a->instance, SK_CONST_CAST(void*, props->font_family));
		props->font_family = NULL;
	}
	props->mask = 0u;
}

i32 ui_style_props_copy_owned(const sk_allocator_t* a, sk_ui_style_props_t* dst, const sk_ui_style_props_t* src) {
	char* font_copy = NULL;
	if (dst == NULL) {
		return -1;
	}
	ui_style_props_free_owned(a, dst);
	ui_style_props_clear(dst);
	if (src == NULL || src->mask == 0u) {
		return 0;
	}
	*dst = *src;
	dst->font_family = NULL;
	if ((src->mask & SK_UI_SP_FONT_FAMILY) != 0u) {
		const_chr_t fam = src->font_family != NULL ? src->font_family : "";
		font_copy = ui_style_strdup(a, fam);
		if (font_copy == NULL) {
			ui_style_props_clear(dst);
			return -1;
		}
		dst->font_family = font_copy;
	}
	return 0;
}

/** Merge src fields into dst (src wins for overlapping bits). Owns font in dst. */
static i32 ui_style_props_merge_owned(const sk_allocator_t* a, sk_ui_style_props_t* dst, const sk_ui_style_props_t* src) {
	u64 m;
	if (dst == NULL || src == NULL || src->mask == 0u) {
		return 0;
	}
	m = src->mask;
	if ((m & SK_UI_SP_FLEX_DIRECTION) != 0u) {
		dst->layout.flex_direction = src->layout.flex_direction;
	}
	if ((m & SK_UI_SP_FLEX_WRAP) != 0u) {
		dst->layout.flex_wrap = src->layout.flex_wrap;
	}
	if ((m & SK_UI_SP_JUSTIFY_CONTENT) != 0u) {
		dst->layout.justify_content = src->layout.justify_content;
	}
	if ((m & SK_UI_SP_ALIGN_ITEMS) != 0u) {
		dst->layout.align_items = src->layout.align_items;
	}
	if ((m & SK_UI_SP_ALIGN_SELF) != 0u) {
		dst->layout.align_self = src->layout.align_self;
	}
	if ((m & SK_UI_SP_ALIGN_CONTENT) != 0u) {
		dst->layout.align_content = src->layout.align_content;
	}
	if ((m & SK_UI_SP_FLEX_GROW) != 0u) {
		dst->layout.flex_grow = src->layout.flex_grow;
	}
	if ((m & SK_UI_SP_FLEX_SHRINK) != 0u) {
		dst->layout.flex_shrink = src->layout.flex_shrink;
	}
	if ((m & SK_UI_SP_FLEX_BASIS) != 0u) {
		dst->layout.flex_basis = src->layout.flex_basis;
	}
	if ((m & SK_UI_SP_WIDTH) != 0u) {
		dst->layout.width = src->layout.width;
	}
	if ((m & SK_UI_SP_HEIGHT) != 0u) {
		dst->layout.height = src->layout.height;
	}
	if ((m & SK_UI_SP_MIN_WIDTH) != 0u) {
		dst->layout.min_width = src->layout.min_width;
	}
	if ((m & SK_UI_SP_MIN_HEIGHT) != 0u) {
		dst->layout.min_height = src->layout.min_height;
	}
	if ((m & SK_UI_SP_MAX_WIDTH) != 0u) {
		dst->layout.max_width = src->layout.max_width;
	}
	if ((m & SK_UI_SP_MAX_HEIGHT) != 0u) {
		dst->layout.max_height = src->layout.max_height;
	}
	if ((m & SK_UI_SP_PADDING) != 0u) {
		dst->layout.padding = src->layout.padding;
	}
	if ((m & SK_UI_SP_MARGIN) != 0u) {
		dst->layout.margin = src->layout.margin;
	}
	if ((m & SK_UI_SP_BORDER_WIDTH) != 0u) {
		dst->layout.border = src->layout.border;
	}
	if ((m & SK_UI_SP_ROW_GAP) != 0u) {
		dst->layout.row_gap = src->layout.row_gap;
	}
	if ((m & SK_UI_SP_COLUMN_GAP) != 0u) {
		dst->layout.column_gap = src->layout.column_gap;
	}
	if ((m & SK_UI_SP_POSITION) != 0u) {
		dst->layout.position = src->layout.position;
	}
	if ((m & SK_UI_SP_LEFT) != 0u) {
		dst->layout.left = src->layout.left;
	}
	if ((m & SK_UI_SP_TOP) != 0u) {
		dst->layout.top = src->layout.top;
	}
	if ((m & SK_UI_SP_RIGHT) != 0u) {
		dst->layout.right = src->layout.right;
	}
	if ((m & SK_UI_SP_BOTTOM) != 0u) {
		dst->layout.bottom = src->layout.bottom;
	}
	if ((m & SK_UI_SP_BACKGROUND_COLOR) != 0u) {
		dst->background_color = src->background_color;
	}
	if ((m & SK_UI_SP_BORDER_COLOR) != 0u) {
		dst->border_color = src->border_color;
	}
	if ((m & SK_UI_SP_CORNER_RADIUS) != 0u) {
		dst->corner_radius = src->corner_radius;
	}
	if ((m & SK_UI_SP_OPACITY) != 0u) {
		dst->opacity = src->opacity;
	}
	if ((m & SK_UI_SP_FONT_SIZE) != 0u) {
		dst->font_size = src->font_size;
	}
	if ((m & SK_UI_SP_COLOR) != 0u) {
		dst->color = src->color;
	}
	if ((m & SK_UI_SP_FONT_FAMILY) != 0u) {
		const_chr_t fam = src->font_family != NULL ? src->font_family : "";
		char* copy = ui_style_strdup(a, fam);
		if (copy == NULL) {
			return -1;
		}
		if ((dst->mask & SK_UI_SP_FONT_FAMILY) != 0u && dst->font_family != NULL) {
			a->free(a->instance, SK_CONST_CAST(void*, dst->font_family));
		}
		dst->font_family = copy;
	}
	dst->mask |= m;
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Dirty flags from property mask                                             */
/* -------------------------------------------------------------------------- */

u32 ui_style_dirty_flags_for_mask(u64 mask) {
	u32 d = (u32)SK_UI_DIRTY_STYLE;
	if (mask == 0u) {
		return d;
	}
	if ((mask & SK_UI_SP_LAYOUT_MASK) != 0u) {
		d |= (u32)SK_UI_DIRTY_LAYOUT;
	}
	/* Text size / family affect measure → layout. */
	if ((mask & (SK_UI_SP_FONT_FAMILY | SK_UI_SP_FONT_SIZE)) != 0u) {
		d |= (u32)SK_UI_DIRTY_LAYOUT;
	}
	if ((mask & SK_UI_SP_PAINT_MASK) != 0u) {
		d |= (u32)SK_UI_DIRTY_PAINT;
	}
	if ((mask & (SK_UI_SP_FONT_FAMILY | SK_UI_SP_FONT_SIZE)) != 0u) {
		d |= (u32)SK_UI_DIRTY_PAINT;
	}
	return d;
}

u64 ui_style_class_combined_mask(const ui_style_class_t* cls) {
	if (cls == NULL) {
		return 0u;
	}
	return cls->base.mask | cls->hover.mask | cls->active.mask | cls->focused.mask | cls->disabled.mask;
}

u32 ui_style_dirty_for_class_name(const sk_ui_context_t* ctx, const_chr_t class_name) {
	ui_style_class_t* cls = ui_style_class_lookup(ctx, class_name);
	if (cls == NULL) {
		return (u32)SK_UI_DIRTY_STYLE;
	}
	return ui_style_dirty_flags_for_mask(ui_style_class_combined_mask(cls));
}

/* -------------------------------------------------------------------------- */
/* Computed defaults                                                          */
/* -------------------------------------------------------------------------- */

void ui_computed_style_init_default(sk_ui_computed_style_t* out) {
	memset(out, 0, sizeof(*out));
	ui_layout_style_init_default(&out->layout);
	out->background_color = ui_color_transparent();
	out->border_color = ui_color_transparent();
	out->corner_radius = 0.0f;
	out->opacity = 1.0f;
	out->font_family = "";
	out->font_size = 16.0f;
	out->color = ui_color_black();
}

void ui_node_style_init(ui_node_slot_t* slot, const sk_allocator_t* a) {
	(void)a;
	ui_style_props_clear(&slot->inline_style);
	ui_computed_style_init_default(&slot->computed);
	slot->computed_font_family = NULL;
	slot->state_flags = 0u;
	slot->computed.font_family = "";
}

void ui_node_style_release(ui_node_slot_t* slot, const sk_allocator_t* a) {
	ui_style_props_free_owned(a, &slot->inline_style);
	if (slot->computed_font_family != NULL) {
		a->free(a->instance, slot->computed_font_family);
		slot->computed_font_family = NULL;
	}
	slot->computed.font_family = "";
	slot->state_flags = 0u;
}

/* -------------------------------------------------------------------------- */
/* Apply props → computed                                                     */
/* -------------------------------------------------------------------------- */

static void ui_style_apply_to_computed(sk_ui_computed_style_t* dest, const sk_ui_style_props_t* src) {
	u64 m;
	if (src == NULL || src->mask == 0u) {
		return;
	}
	m = src->mask;
	if ((m & SK_UI_SP_FLEX_DIRECTION) != 0u) {
		dest->layout.flex_direction = src->layout.flex_direction;
	}
	if ((m & SK_UI_SP_FLEX_WRAP) != 0u) {
		dest->layout.flex_wrap = src->layout.flex_wrap;
	}
	if ((m & SK_UI_SP_JUSTIFY_CONTENT) != 0u) {
		dest->layout.justify_content = src->layout.justify_content;
	}
	if ((m & SK_UI_SP_ALIGN_ITEMS) != 0u) {
		dest->layout.align_items = src->layout.align_items;
	}
	if ((m & SK_UI_SP_ALIGN_SELF) != 0u) {
		dest->layout.align_self = src->layout.align_self;
	}
	if ((m & SK_UI_SP_ALIGN_CONTENT) != 0u) {
		dest->layout.align_content = src->layout.align_content;
	}
	if ((m & SK_UI_SP_FLEX_GROW) != 0u) {
		dest->layout.flex_grow = src->layout.flex_grow;
	}
	if ((m & SK_UI_SP_FLEX_SHRINK) != 0u) {
		dest->layout.flex_shrink = src->layout.flex_shrink;
	}
	if ((m & SK_UI_SP_FLEX_BASIS) != 0u) {
		dest->layout.flex_basis = src->layout.flex_basis;
	}
	if ((m & SK_UI_SP_WIDTH) != 0u) {
		dest->layout.width = src->layout.width;
	}
	if ((m & SK_UI_SP_HEIGHT) != 0u) {
		dest->layout.height = src->layout.height;
	}
	if ((m & SK_UI_SP_MIN_WIDTH) != 0u) {
		dest->layout.min_width = src->layout.min_width;
	}
	if ((m & SK_UI_SP_MIN_HEIGHT) != 0u) {
		dest->layout.min_height = src->layout.min_height;
	}
	if ((m & SK_UI_SP_MAX_WIDTH) != 0u) {
		dest->layout.max_width = src->layout.max_width;
	}
	if ((m & SK_UI_SP_MAX_HEIGHT) != 0u) {
		dest->layout.max_height = src->layout.max_height;
	}
	if ((m & SK_UI_SP_PADDING) != 0u) {
		dest->layout.padding = src->layout.padding;
	}
	if ((m & SK_UI_SP_MARGIN) != 0u) {
		dest->layout.margin = src->layout.margin;
	}
	if ((m & SK_UI_SP_BORDER_WIDTH) != 0u) {
		dest->layout.border = src->layout.border;
	}
	if ((m & SK_UI_SP_ROW_GAP) != 0u) {
		dest->layout.row_gap = src->layout.row_gap;
	}
	if ((m & SK_UI_SP_COLUMN_GAP) != 0u) {
		dest->layout.column_gap = src->layout.column_gap;
	}
	if ((m & SK_UI_SP_POSITION) != 0u) {
		dest->layout.position = src->layout.position;
	}
	if ((m & SK_UI_SP_LEFT) != 0u) {
		dest->layout.left = src->layout.left;
	}
	if ((m & SK_UI_SP_TOP) != 0u) {
		dest->layout.top = src->layout.top;
	}
	if ((m & SK_UI_SP_RIGHT) != 0u) {
		dest->layout.right = src->layout.right;
	}
	if ((m & SK_UI_SP_BOTTOM) != 0u) {
		dest->layout.bottom = src->layout.bottom;
	}
	if ((m & SK_UI_SP_BACKGROUND_COLOR) != 0u) {
		dest->background_color = src->background_color;
	}
	if ((m & SK_UI_SP_BORDER_COLOR) != 0u) {
		dest->border_color = src->border_color;
	}
	if ((m & SK_UI_SP_CORNER_RADIUS) != 0u) {
		dest->corner_radius = src->corner_radius;
	}
	if ((m & SK_UI_SP_OPACITY) != 0u) {
		dest->opacity = src->opacity;
	}
	if ((m & SK_UI_SP_FONT_FAMILY) != 0u) {
		dest->font_family = src->font_family != NULL ? src->font_family : "";
	}
	if ((m & SK_UI_SP_FONT_SIZE) != 0u) {
		dest->font_size = src->font_size;
	}
	if ((m & SK_UI_SP_COLOR) != 0u) {
		dest->color = src->color;
	}
}

static void ui_style_apply_class_states(sk_ui_computed_style_t* dest, const ui_style_class_t* cls, u32 state) {
	ui_style_apply_to_computed(dest, &cls->base);
	/* base → hover → focused → active → disabled */
	if ((state & (u32)SK_UI_STATE_HOVER) != 0u) {
		ui_style_apply_to_computed(dest, &cls->hover);
	}
	if ((state & (u32)SK_UI_STATE_FOCUSED) != 0u) {
		ui_style_apply_to_computed(dest, &cls->focused);
	}
	if ((state & (u32)SK_UI_STATE_ACTIVE) != 0u) {
		ui_style_apply_to_computed(dest, &cls->active);
	}
	if ((state & (u32)SK_UI_STATE_DISABLED) != 0u) {
		ui_style_apply_to_computed(dest, &cls->disabled);
	}
}

/* -------------------------------------------------------------------------- */
/* Registry                                                                   */
/* -------------------------------------------------------------------------- */

ui_style_class_t* ui_style_class_lookup(const sk_ui_context_t* ctx, const_chr_t name) {
	u32 index = 0u;
	/* Hash map get stages the key in mutable _key_tmp; cast is intentional. */
	sk_ui_context_t* mut = SK_CONST_CAST(sk_ui_context_t*, ctx);
	if (ctx == NULL || name == NULL || name[0] == '\0') {
		return NULL;
	}
	if (sk_hash_map_get(&mut->style_registry, name, &index) != 0) {
		return NULL;
	}
	if (index >= mut->style_classes.count) {
		return NULL;
	}
	return &mut->style_classes.items[index];
}

static void ui_style_class_release_contents(const sk_allocator_t* a, ui_style_class_t* cls) {
	if (cls == NULL) {
		return;
	}
	ui_style_props_free_owned(a, &cls->base);
	ui_style_props_free_owned(a, &cls->hover);
	ui_style_props_free_owned(a, &cls->active);
	ui_style_props_free_owned(a, &cls->focused);
	ui_style_props_free_owned(a, &cls->disabled);
	if (cls->name != NULL) {
		a->free(a->instance, cls->name);
		cls->name = NULL;
	}
}

void ui_style_registry_init(sk_ui_context_t* ctx) {
	sk_array_init(&ctx->style_classes, ctx->allocator);
	(void)sk_hash_map_init(&ctx->style_registry, ctx->allocator, sk_hash_cstr, sk_equals_cstr);
}

void ui_style_registry_shutdown(sk_ui_context_t* ctx) {
	u32 i;
	const sk_allocator_t* a = ctx->allocator;
	for (i = 0u; i < ctx->style_classes.count; ++i) {
		ui_style_class_release_contents(a, &ctx->style_classes.items[i]);
	}
	sk_array_free(&ctx->style_classes);
	sk_hash_map_free(&ctx->style_registry);
}

/** Mark every live node that lists @p class_name dirty with @p flags. */
static void ui_style_dirty_class_users(sk_ui_context_t* ctx, const_chr_t class_name, u32 flags) {
	u32 i;
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		u32 c;
		if (slot->alive == 0u) {
			continue;
		}
		for (c = 0u; c < slot->classes.count; ++c) {
			if (ui_style_cstr_eq(slot->classes.items[c], class_name)) {
				sk_ui_node_t h;
				h.index = i;
				h.generation = slot->generation;
				ui_mark_dirty_up(ctx, h, flags);
				break;
			}
		}
	}
}

i32 ui_style_class_register_impl(sk_ui_context_t* ctx, const_chr_t name, const sk_ui_style_props_t* base) {
	ui_style_class_t* existing = NULL;
	ui_style_class_t cls;
	const sk_allocator_t* a;
	u64 dirty_mask;
	u32 index;

	if (ctx == NULL || name == NULL || name[0] == '\0') {
		return -1;
	}
	a = ctx->allocator;
	dirty_mask = base != NULL ? base->mask : 0u;

	existing = ui_style_class_lookup(ctx, name);
	if (existing != NULL) {
		dirty_mask |= ui_style_class_combined_mask(existing);
		if (ui_style_props_copy_owned(a, &existing->base, base) != 0) {
			return -1;
		}
		ui_style_dirty_class_users(ctx, name, ui_style_dirty_flags_for_mask(dirty_mask));
		return 0;
	}

	memset(&cls, 0, sizeof(cls));
	ui_style_props_clear(&cls.base);
	ui_style_props_clear(&cls.hover);
	ui_style_props_clear(&cls.active);
	ui_style_props_clear(&cls.focused);
	ui_style_props_clear(&cls.disabled);
	cls.name = ui_style_strdup(a, name);
	if (cls.name == NULL) {
		return -1;
	}
	if (ui_style_props_copy_owned(a, &cls.base, base) != 0) {
		ui_style_class_release_contents(a, &cls);
		return -1;
	}
	index = ctx->style_classes.count;
	if (sk_array_push(&ctx->style_classes, cls) != 0) {
		ui_style_class_release_contents(a, &cls);
		return -1;
	}
	if (sk_hash_map_put(&ctx->style_registry, (const_chr_t)ctx->style_classes.items[index].name, index) != 0) {
		ui_style_class_release_contents(a, &ctx->style_classes.items[index]);
		ctx->style_classes.count -= 1u;
		return -1;
	}
	ui_style_dirty_class_users(ctx, name, ui_style_dirty_flags_for_mask(dirty_mask));
	return 0;
}

i32 ui_style_class_set_variant_impl(sk_ui_context_t* ctx, const_chr_t name, sk_ui_state_flags_t state, const sk_ui_style_props_t* props) {
	ui_style_class_t* cls;
	sk_ui_style_props_t* target;
	u64 dirty_mask;

	cls = ui_style_class_lookup(ctx, name);
	if (cls == NULL) {
		return -1;
	}
	if (state == SK_UI_STATE_HOVER) {
		target = &cls->hover;
	} else if (state == SK_UI_STATE_ACTIVE) {
		target = &cls->active;
	} else if (state == SK_UI_STATE_FOCUSED) {
		target = &cls->focused;
	} else if (state == SK_UI_STATE_DISABLED) {
		target = &cls->disabled;
	} else {
		return -1;
	}
	dirty_mask = (props != NULL ? props->mask : 0u) | target->mask;
	if (ui_style_props_copy_owned(ctx->allocator, target, props) != 0) {
		return -1;
	}
	ui_style_dirty_class_users(ctx, name, ui_style_dirty_flags_for_mask(dirty_mask));
	return 0;
}

i32 ui_style_class_unregister_impl(sk_ui_context_t* ctx, const_chr_t name) {
	ui_style_class_t* cls;
	u64 dirty_mask;
	u32 index = 0u;
	u32 last;

	if (ctx == NULL || name == NULL) {
		return -1;
	}
	if (sk_hash_map_get(&ctx->style_registry, name, &index) != 0) {
		return 0;
	}
	if (index >= ctx->style_classes.count) {
		return 0;
	}
	cls = &ctx->style_classes.items[index];
	dirty_mask = ui_style_class_combined_mask(cls);
	ui_style_dirty_class_users(ctx, name, ui_style_dirty_flags_for_mask(dirty_mask));
	(void)sk_hash_map_remove(&ctx->style_registry, name);
	ui_style_class_release_contents(ctx->allocator, cls);

	/* Swap-remove to keep dense array; re-key moved entry. */
	last = ctx->style_classes.count - 1u;
	if (index != last) {
		ctx->style_classes.items[index] = ctx->style_classes.items[last];
		if (ctx->style_classes.items[index].name != NULL) {
			(void)sk_hash_map_put(&ctx->style_registry, (const_chr_t)ctx->style_classes.items[index].name, index);
		}
	}
	ctx->style_classes.count -= 1u;
	return 0;
}

i32 ui_style_class_has_impl(const sk_ui_context_t* ctx, const_chr_t name) {
	return ui_style_class_lookup(ctx, name) != NULL ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/* Node inline / state / computed                                             */
/* -------------------------------------------------------------------------- */

i32 ui_node_set_inline_style_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_style_props_t* props) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	u64 dirty_mask = 0u;
	if (slot == NULL) {
		return -1;
	}
	dirty_mask = slot->inline_style.mask;
	if (props != NULL) {
		dirty_mask |= props->mask;
	}
	if (ui_style_props_copy_owned(ctx->allocator, &slot->inline_style, props) != 0) {
		return -1;
	}
	ui_mark_dirty_up(ctx, node, ui_style_dirty_flags_for_mask(dirty_mask));
	return 0;
}

i32 ui_node_merge_inline_style_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_style_props_t* props) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (props == NULL || props->mask == 0u) {
		return 0;
	}
	if (ui_style_props_merge_owned(ctx->allocator, &slot->inline_style, props) != 0) {
		return -1;
	}
	ui_mark_dirty_up(ctx, node, ui_style_dirty_flags_for_mask(props->mask));
	return 0;
}

i32 ui_node_get_inline_style_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_style_props_t* out) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (out != NULL) {
		*out = slot->inline_style;
	}
	return 0;
}

i32 ui_node_set_state_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 state_flags) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	u32 old;
	u32 changed;
	u32 i;
	u64 variant_mask = 0u;
	u32 dirty;

	if (slot == NULL) {
		return -1;
	}
	old = slot->state_flags;
	if (old == state_flags) {
		return 0;
	}
	changed = old ^ state_flags;
	slot->state_flags = state_flags;

	/* Collect masks of variants that may apply for bits that flipped. */
	for (i = 0u; i < slot->classes.count; ++i) {
		ui_style_class_t* cls = ui_style_class_lookup(ctx, slot->classes.items[i]);
		if (cls == NULL) {
			continue;
		}
		if ((changed & (u32)SK_UI_STATE_HOVER) != 0u) {
			variant_mask |= cls->hover.mask;
		}
		if ((changed & (u32)SK_UI_STATE_FOCUSED) != 0u) {
			variant_mask |= cls->focused.mask;
		}
		if ((changed & (u32)SK_UI_STATE_ACTIVE) != 0u) {
			variant_mask |= cls->active.mask;
		}
		if ((changed & (u32)SK_UI_STATE_DISABLED) != 0u) {
			variant_mask |= cls->disabled.mask;
		}
	}

	dirty = (u32)SK_UI_DIRTY_STYLE;
	if (variant_mask != 0u) {
		dirty = ui_style_dirty_flags_for_mask(variant_mask);
	}
	ui_mark_dirty_up(ctx, node, dirty);
	return 0;
}

u32 ui_node_get_state_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? slot->state_flags : 0u;
}

i32 ui_node_get_computed_style_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_computed_style_t* out) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (out != NULL) {
		*out = slot->computed;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Resolve                                                                    */
/* -------------------------------------------------------------------------- */

static i32 ui_style_set_computed_font(ui_node_slot_t* slot, const sk_allocator_t* a, const_chr_t family) {
	const_chr_t src = family != NULL ? family : "";
	size_t len = strlen(src);
	char* buf;

	if (slot->computed_font_family != NULL && ui_style_cstr_eq(slot->computed_font_family, src)) {
		slot->computed.font_family = slot->computed_font_family;
		return 0;
	}
	buf = (char*)a->alloc(a->instance, len + 1u);
	if (buf == NULL) {
		return -1;
	}
	memcpy(buf, src, len + 1u);
	if (slot->computed_font_family != NULL) {
		a->free(a->instance, slot->computed_font_family);
	}
	slot->computed_font_family = buf;
	slot->computed.font_family = buf;
	return 0;
}

static i32 ui_style_inheritable_changed(const sk_ui_computed_style_t* a, const sk_ui_computed_style_t* b) {
	if (!ui_f32_bits_eq(a->font_size, b->font_size)) {
		return 1;
	}
	if (!ui_color_eq(a->color, b->color)) {
		return 1;
	}
	if (!ui_style_cstr_eq(a->font_family, b->font_family)) {
		return 1;
	}
	return 0;
}

static i32 ui_style_resolve_node(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	sk_ui_computed_style_t result;
	sk_ui_computed_style_t old;
	const ui_node_slot_t* parent_slot;
	const_chr_t font_src;
	u32 i;
	const sk_allocator_t* a;

	if (slot == NULL) {
		return -1;
	}
	a = ctx->allocator;
	old = slot->computed;

	/* 1. Defaults */
	ui_computed_style_init_default(&result);

	/* 2. Inheritance (text props only) from parent computed */
	parent_slot = sk_ui_node_is_valid(slot->parent) ? ui_slot(ctx, slot->parent) : NULL;
	if (parent_slot != NULL) {
		result.font_family = parent_slot->computed.font_family;
		result.font_size = parent_slot->computed.font_size;
		result.color = parent_slot->computed.color;
	}

	/* 3. Classes earlier → later (later wins) */
	for (i = 0u; i < slot->classes.count; ++i) {
		ui_style_class_t* cls = ui_style_class_lookup(ctx, slot->classes.items[i]);
		if (cls != NULL) {
			ui_style_apply_class_states(&result, cls, slot->state_flags);
		}
	}

	/* 4. Inline overrides (highest) */
	ui_style_apply_to_computed(&result, &slot->inline_style);

	/* Own the font string on the node */
	font_src = result.font_family;
	slot->computed = result;
	if (ui_style_set_computed_font(slot, a, font_src) != 0) {
		return -1;
	}

	/* Feed flexbox solver */
	slot->layout_style = slot->computed.layout;

	/* Inheritance change → children need re-resolve */
	if (ui_style_inheritable_changed(&old, &slot->computed)) {
		for (i = 0u; i < slot->children.count; ++i) {
			ui_mark_dirty_up(ctx, slot->children.items[i], (u32)SK_UI_DIRTY_STYLE);
		}
	}

	/* Clear STYLE only on this node (LAYOUT/PAINT remain for later passes). */
	slot->dirty = (u16)(slot->dirty & (u16) ~(u16)SK_UI_DIRTY_STYLE);
	return 0;
}

i32 ui_style_resolve_impl(sk_ui_context_t* ctx) {
	/*
	 * Pre-order walk. Re-check STYLE after each resolve so children marked
	 * by inheritance updates in the same pass are still processed.
	 * Multiple sweeps until stable (bounded by tree size).
	 */
	u32 pass;
	u32 max_passes;
	if (ctx == NULL) {
		return -1;
	}
	max_passes = ctx->live_count + 2u;
	if (max_passes < 4u) {
		max_passes = 4u;
	}

	for (pass = 0u; pass < max_passes; ++pass) {
		u32 resolved = 0u;
		/* Iterative preorder via explicit stack of node indices. */
		sk_ui_node_t stack_local[64];
		sk_ui_node_t* stack = stack_local;
		u32 sp = 0u;
		u32 cap = 64u;
		i32 heap = 0;
		const sk_allocator_t* a = ctx->allocator;

		stack[sp++] = ctx->root;

		while (sp > 0u) {
			sk_ui_node_t node = stack[--sp];
			ui_node_slot_t* slot = ui_slot_mut(ctx, node);
			u32 c;
			if (slot == NULL) {
				continue;
			}
			if (((u32)slot->dirty & (u32)SK_UI_DIRTY_STYLE) != 0u) {
				if (ui_style_resolve_node(ctx, node) != 0) {
					if (heap != 0) {
						a->free(a->instance, stack);
					}
					return -1;
				}
				resolved += 1u;
			}
			/* Push children in reverse so left-to-right preorder. */
			if (slot->children.count > 0u) {
				u32 need = sp + slot->children.count;
				if (need > cap) {
					u32 ncap = cap * 2u;
					sk_ui_node_t* nstack;
					while (ncap < need) {
						ncap *= 2u;
					}
					nstack = (sk_ui_node_t*)a->alloc(a->instance, sizeof(sk_ui_node_t) * (size_t)ncap);
					if (nstack == NULL) {
						if (heap != 0) {
							a->free(a->instance, stack);
						}
						return -1;
					}
					memcpy(nstack, stack, sizeof(sk_ui_node_t) * (size_t)sp);
					if (heap != 0) {
						a->free(a->instance, stack);
					}
					stack = nstack;
					cap = ncap;
					heap = 1;
				}
				for (c = slot->children.count; c > 0u; --c) {
					stack[sp++] = slot->children.items[c - 1u];
				}
			}
		}

		if (heap != 0) {
			a->free(a->instance, stack);
		}
		if (resolved == 0u) {
			return 0;
		}
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

static const sk_ui_api_t* style_test_api(void) {
	return ui_get_api_table();
}

static sk_ui_style_props_t style_props_bg(sk_ui_color_t c) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_BACKGROUND_COLOR;
	p.background_color = c;
	return p;
}

static sk_ui_style_props_t style_props_width(f32 w) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH;
	p.layout.width = sk_ui_pt(w);
	return p;
}

static sk_ui_style_props_t style_props_font(const_chr_t family, f32 size, sk_ui_color_t color) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FONT_FAMILY | SK_UI_SP_FONT_SIZE | SK_UI_SP_COLOR;
	p.font_family = family;
	p.font_size = size;
	p.color = color;
	return p;
}

SK_TEST(ui_style_precedence_inline_over_classes) {
	const sk_ui_api_t* ui = style_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t node;
	sk_ui_style_props_t early;
	sk_ui_style_props_t late;
	sk_ui_style_props_t inline_p;
	sk_ui_computed_style_t computed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	node = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);

	early = style_props_bg(sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f));
	late = style_props_bg(sk_ui_rgba(0.0f, 1.0f, 0.0f, 1.0f));
	inline_p = style_props_bg(sk_ui_rgba(0.0f, 0.0f, 1.0f, 1.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->style_class_register(ctx, "early", &early));
	TEST_ASSERT_EQUAL_INT(0, ui->style_class_register(ctx, "late", &late));
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, node, "early"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, node, "late"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_inline_style(ctx, node, &inline_p));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, node, &computed));
	/* inline wins over later class wins over earlier class */
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computed.background_color.r);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computed.background_color.g);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, computed.background_color.b);

	/* Without inline: later class wins */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_inline_style(ctx, node, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, node, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computed.background_color.r);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, computed.background_color.g);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computed.background_color.b);

	/* Only early class */
	TEST_ASSERT_EQUAL_INT(0, ui->node_remove_class(ctx, node, "late"));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, node, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, computed.background_color.r);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computed.background_color.g);

	ui->context_destroy(ctx);
}

SK_TEST(ui_style_state_variants_hover_active_focused_disabled) {
	const sk_ui_api_t* ui = style_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t node;
	sk_ui_style_props_t base;
	sk_ui_style_props_t hover;
	sk_ui_style_props_t active;
	sk_ui_style_props_t focused;
	sk_ui_style_props_t disabled;
	sk_ui_computed_style_t computed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	node = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, root);

	base = style_props_bg(sk_ui_rgba(0.1f, 0.1f, 0.1f, 1.0f));
	hover = style_props_bg(sk_ui_rgba(0.2f, 0.0f, 0.0f, 1.0f));
	focused = style_props_bg(sk_ui_rgba(0.0f, 0.2f, 0.0f, 1.0f));
	active = style_props_bg(sk_ui_rgba(0.0f, 0.0f, 0.2f, 1.0f));
	disabled = style_props_bg(sk_ui_rgba(0.5f, 0.5f, 0.5f, 1.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->style_class_register(ctx, "btn", &base));
	TEST_ASSERT_EQUAL_INT(0, ui->style_class_set_variant(ctx, "btn", SK_UI_STATE_HOVER, &hover));
	TEST_ASSERT_EQUAL_INT(0, ui->style_class_set_variant(ctx, "btn", SK_UI_STATE_FOCUSED, &focused));
	TEST_ASSERT_EQUAL_INT(0, ui->style_class_set_variant(ctx, "btn", SK_UI_STATE_ACTIVE, &active));
	TEST_ASSERT_EQUAL_INT(0, ui->style_class_set_variant(ctx, "btn", SK_UI_STATE_DISABLED, &disabled));
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, node, "btn"));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, node, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, computed.background_color.r);

	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, node, (u32)SK_UI_STATE_HOVER));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, node, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, computed.background_color.r);

	/* disabled wins over hover when both set (variant order) */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, node, (u32)(SK_UI_STATE_HOVER | SK_UI_STATE_DISABLED)));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, node, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, computed.background_color.r);

	/* active after focused in cascade order */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, node, (u32)(SK_UI_STATE_FOCUSED | SK_UI_STATE_ACTIVE)));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, node, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computed.background_color.r);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computed.background_color.g);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, computed.background_color.b);

	ui->context_destroy(ctx);
}

SK_TEST(ui_style_text_inheritance) {
	const sk_ui_api_t* ui = style_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t parent;
	sk_ui_node_t child;
	sk_ui_style_props_t parent_font;
	sk_ui_style_props_t child_color;
	sk_ui_computed_style_t computed;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	parent = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	child = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, parent);

	parent_font = style_props_font("DejaVu", 20.0f, sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_inline_style(ctx, parent, &parent_font));

	/* Child has no text props — inherits family/size/color from parent. */
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, child, &computed));
	TEST_ASSERT_EQUAL_STRING("DejaVu", computed.font_family);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, computed.font_size);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, computed.color.r);

	/* Child overrides color only; family/size still inherited. */
	child_color = style_props_bg(sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f)); /* clear helper reuse */
	ui_style_props_clear(&child_color);
	child_color.mask = SK_UI_SP_COLOR;
	child_color.color = sk_ui_rgba(0.0f, 0.0f, 1.0f, 1.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_inline_style(ctx, child, &child_color));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, child, &computed));
	TEST_ASSERT_EQUAL_STRING("DejaVu", computed.font_family);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, computed.font_size);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computed.color.r);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, computed.color.b);

	/* Parent font size change re-dirties child via inheritance. */
	parent_font.font_size = 32.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_inline_style(ctx, parent, &parent_font));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, child, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 32.0f, computed.font_size);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, computed.color.b); /* child color still wins */

	ui->context_destroy(ctx);
}

SK_TEST(ui_style_change_marks_exact_dirty_flags) {
	const sk_ui_api_t* ui = style_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t node;
	sk_ui_style_props_t paint_only;
	sk_ui_style_props_t layout_only;
	sk_ui_style_props_t text_props;
	sk_ui_style_props_t class_paint;
	u32 dirty;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	node = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);

	/* Start clean. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_dirty_subtree(ctx, root, (u32)SK_UI_DIRTY_ALL));

	/* Background-only → STYLE | PAINT, not LAYOUT. */
	paint_only = style_props_bg(sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &paint_only));
	dirty = ui->node_get_dirty(ctx, node);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_STYLE) != 0u);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_PAINT) != 0u);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_LAYOUT) == 0u);

	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_dirty_subtree(ctx, root, (u32)SK_UI_DIRTY_ALL));

	/* Width-only → STYLE | LAYOUT, not PAINT. */
	layout_only = style_props_width(100.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &layout_only));
	dirty = ui->node_get_dirty(ctx, node);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_STYLE) != 0u);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_LAYOUT) != 0u);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_PAINT) == 0u);

	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_dirty_subtree(ctx, root, (u32)SK_UI_DIRTY_ALL));

	/* Font size → STYLE | LAYOUT | PAINT (measure + paint). */
	ui_style_props_clear(&text_props);
	text_props.mask = SK_UI_SP_FONT_SIZE;
	text_props.font_size = 18.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &text_props));
	dirty = ui->node_get_dirty(ctx, node);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_STYLE) != 0u);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_LAYOUT) != 0u);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_PAINT) != 0u);

	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_dirty_subtree(ctx, root, (u32)SK_UI_DIRTY_ALL));

	/* Class with paint-only props: add_class marks STYLE|PAINT only. */
	class_paint = style_props_bg(sk_ui_rgba(0.0f, 1.0f, 0.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->style_class_register(ctx, "panel", &class_paint));
	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_dirty_subtree(ctx, root, (u32)SK_UI_DIRTY_ALL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, node, "panel"));
	dirty = ui->node_get_dirty(ctx, node);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_STYLE) != 0u);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_PAINT) != 0u);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_LAYOUT) == 0u);

	/* style_resolve clears STYLE but leaves LAYOUT/PAINT. */
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	dirty = ui->node_get_dirty(ctx, node);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_STYLE) == 0u);
	TEST_ASSERT_TRUE((dirty & (u32)SK_UI_DIRTY_PAINT) != 0u);

	/* Unregistered class name → STYLE only. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_dirty_subtree(ctx, root, (u32)SK_UI_DIRTY_ALL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, node, "unknown-class"));
	dirty = ui->node_get_dirty(ctx, node);
	TEST_ASSERT_EQUAL_UINT((u32)SK_UI_DIRTY_STYLE, dirty);

	ui->context_destroy(ctx);
}

SK_TEST(ui_style_layout_props_feed_solver) {
	const sk_ui_api_t* ui = style_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t child;
	sk_ui_style_props_t root_row;
	sk_ui_style_props_t child_size;
	sk_ui_rect_t border;
	sk_ui_layout_style_t ls;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	child = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);

	ui_style_props_clear(&root_row);
	root_row.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	root_row.layout.flex_direction = SK_UI_FLEX_ROW;
	root_row.layout.width = sk_ui_pt(200.0f);
	root_row.layout.height = sk_ui_pt(40.0f);

	ui_style_props_clear(&child_size);
	child_size.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	child_size.layout.width = sk_ui_pt(50.0f);
	child_size.layout.height = sk_ui_pt(40.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->node_set_inline_style(ctx, root, &root_row));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_inline_style(ctx, child, &child_size));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, root, &ls));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_FLEX_ROW, (int)ls.flex_direction);

	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 40.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, child, &border, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, border.width);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 40.0f, border.height);

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
