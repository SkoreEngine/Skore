#pragma once

/**
 * @file ui_internal.h
 * @brief Private UI plugin state shared by ui.c and layout.c.
 *
 * Not part of the public sk-ui surface. Do not include from hosts or other plugins.
 */

#include "ui.h"

#include "array.h"
#include "hashmap.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* Containers                                                                 */
/* -------------------------------------------------------------------------- */

typedef SK_ARRAY(sk_ui_node_t) ui_node_list_t;
typedef SK_ARRAY(char*) ui_class_list_t;

typedef struct ui_prop_entry_t {
	char* key;
	sk_ui_prop_type_t type;
	union {
		i32 i32_value;
		f32 f32_value;
		char* str_value;
	} data;
} ui_prop_entry_t;

typedef SK_ARRAY(ui_prop_entry_t) ui_prop_list_t;

/* -------------------------------------------------------------------------- */
/* Style registry                                                             */
/* -------------------------------------------------------------------------- */

/**
 * Owned style property set (font_family heap-copied when SK_UI_SP_FONT_FAMILY).
 * Public sk_ui_style_props_t is the same layout; ownership is internal only.
 */
typedef sk_ui_style_props_t ui_style_props_owned_t;

/** Named style class: base + optional state variants. */
typedef struct ui_style_class_t {
	char* name;
	ui_style_props_owned_t base;
	ui_style_props_owned_t hover;
	ui_style_props_owned_t active;
	ui_style_props_owned_t focused;
	ui_style_props_owned_t disabled;
} ui_style_class_t;

typedef SK_ARRAY(ui_style_class_t) ui_style_class_array_t;
/** name → index into style_classes (u32). Avoids pointer map values for tidy. */
typedef SK_HASH_MAP(const_chr_t, u32) ui_style_registry_t;

/* -------------------------------------------------------------------------- */
/* Node slot                                                                  */
/* -------------------------------------------------------------------------- */

typedef struct ui_node_slot_t {
	u32 generation;
	u8 alive;
	u8 kind;
	u16 dirty;
	u32 state_flags; /**< sk_ui_state_flags_t bits. */
	sk_ui_node_t parent;
	ui_node_list_t children;
	char* id;
	ui_class_list_t classes;
	ui_prop_list_t props;
	void_ptr_t user_data;
	sk_type_id_t user_data_type;

	/* Style: inline overrides + last computed result */
	ui_style_props_owned_t inline_style;
	sk_ui_computed_style_t computed;
	char* computed_font_family; /**< Owned buffer; computed.font_family points here. */

	/* Layout (logical units unless _scaled) */
	sk_ui_layout_style_t layout_style;
	sk_ui_rect_t layout_border;	 /**< Border box relative to parent content origin. */
	sk_ui_rect_t layout_content; /**< Content box relative to parent content origin. */
	sk_ui_rect_t layout_border_scaled;
	sk_ui_rect_t layout_content_scaled;

	/* Interaction */
	u8 focusable;
	u8 clip_children;  /**< Non-zero: clip hit-test (and later paint) to content box. */
	u8 pointer_events; /**< sk_ui_pointer_events_t */
	sk_ui_node_callbacks_t callbacks;
} ui_node_slot_t;

typedef SK_ARRAY(ui_node_slot_t) ui_slot_array_t;
typedef SK_ARRAY(u32) ui_freelist_t;
typedef SK_HASH_MAP(const_chr_t, sk_ui_node_t) ui_id_map_t;

/* -------------------------------------------------------------------------- */
/* Context                                                                    */
/* -------------------------------------------------------------------------- */

typedef SK_ARRAY(sk_ui_draw_vertex_t) ui_draw_vertex_array_t;
typedef SK_ARRAY(u32) ui_draw_index_array_t;
typedef SK_ARRAY(sk_ui_draw_cmd_t) ui_draw_cmd_array_t;

/** Mutable draw-list storage owned by the context (public view is const). */
typedef struct ui_draw_list_store_t {
	ui_draw_vertex_array_t vertices;
	ui_draw_index_array_t indices;
	ui_draw_cmd_array_t commands;
	sk_ui_draw_list_t view; /**< Public snapshot; pointers into the arrays. */
	f32 last_scale_x;
	f32 last_scale_y;
	i32 valid; /**< Non-zero after at least one successful paint rebuild. */
} ui_draw_list_store_t;

struct sk_ui_context_t {
	const sk_allocator_t* allocator;
	ui_slot_array_t slots; /* index 0 unused; live handles use index >= 1 */
	ui_freelist_t freelist;
	ui_id_map_t id_map;
	ui_style_class_array_t style_classes;
	ui_style_registry_t style_registry;
	sk_ui_node_t root;
	u32 live_count;

	sk_ui_measure_fn measure_fn;
	void_ptr_t measure_user;

	f32 root_width;
	f32 root_height;
	f32 content_scale_x;
	f32 content_scale_y;

	/* Input routing state (logical units) */
	sk_ui_node_t hover;
	sk_ui_node_t active; /**< Pointer-down target while button held. */
	sk_ui_node_t focus;
	sk_ui_node_t pointer_capture;
	f32 pointer_x;
	f32 pointer_y;
	u32 pointer_buttons; /**< Bit i set if button i is down. */
	i32 wants_mouse;
	i32 wants_keyboard;

	/* Paint / draw list (CPU) */
	ui_draw_list_store_t draw;
};

/* -------------------------------------------------------------------------- */
/* Slot access (ui.c)                                                         */
/* -------------------------------------------------------------------------- */

ui_node_slot_t* ui_slot_mut(sk_ui_context_t* ctx, sk_ui_node_t node);
const ui_node_slot_t* ui_slot(const sk_ui_context_t* ctx, sk_ui_node_t node);
void ui_mark_dirty_up(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);

/* -------------------------------------------------------------------------- */
/* Layout API implementations (layout.c)                                      */
/* -------------------------------------------------------------------------- */

void ui_layout_style_init_default(sk_ui_layout_style_t* style);

i32 ui_node_set_layout_style_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_layout_style_t* style);
i32 ui_node_get_layout_style_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_layout_style_t* out);
i32 ui_node_get_layout_rect_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content);
i32 ui_node_get_layout_rect_scaled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content);

void ui_set_measure_fn_impl(sk_ui_context_t* ctx, sk_ui_measure_fn fn, void_ptr_t user);
i32 ui_layout_impl(sk_ui_context_t* ctx, f32 root_width, f32 root_height);
i32 ui_layout_apply_scale_impl(sk_ui_context_t* ctx, f32 scale_x, f32 scale_y);
void ui_layout_get_content_scale_impl(const sk_ui_context_t* ctx, f32* out_scale_x, f32* out_scale_y);

/* -------------------------------------------------------------------------- */
/* Style API implementations (style.c)                                        */
/* -------------------------------------------------------------------------- */

void ui_style_props_clear(sk_ui_style_props_t* props);
void ui_style_props_free_owned(const sk_allocator_t* a, sk_ui_style_props_t* props);
i32 ui_style_props_copy_owned(const sk_allocator_t* a, sk_ui_style_props_t* dst, const sk_ui_style_props_t* src);
u32 ui_style_dirty_flags_for_mask(u64 mask);
u64 ui_style_class_combined_mask(const ui_style_class_t* cls);
ui_style_class_t* ui_style_class_lookup(const sk_ui_context_t* ctx, const_chr_t name);

void ui_style_registry_init(sk_ui_context_t* ctx);
void ui_style_registry_shutdown(sk_ui_context_t* ctx);
void ui_computed_style_init_default(sk_ui_computed_style_t* out);
void ui_node_style_init(ui_node_slot_t* slot, const sk_allocator_t* a);
void ui_node_style_release(ui_node_slot_t* slot, const sk_allocator_t* a);

/** Dirty flags for add/remove of a class name (looks up registry). */
u32 ui_style_dirty_for_class_name(const sk_ui_context_t* ctx, const_chr_t class_name);

i32 ui_style_class_register_impl(sk_ui_context_t* ctx, const_chr_t name, const sk_ui_style_props_t* base);
i32 ui_style_class_set_variant_impl(sk_ui_context_t* ctx, const_chr_t name, sk_ui_state_flags_t state, const sk_ui_style_props_t* props);
i32 ui_style_class_unregister_impl(sk_ui_context_t* ctx, const_chr_t name);
i32 ui_style_class_has_impl(const sk_ui_context_t* ctx, const_chr_t name);

i32 ui_node_set_inline_style_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_style_props_t* props);
i32 ui_node_merge_inline_style_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_style_props_t* props);
i32 ui_node_get_inline_style_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_style_props_t* out);
i32 ui_node_set_state_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 state_flags);
u32 ui_node_get_state_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_node_get_computed_style_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_computed_style_t* out);
i32 ui_style_resolve_impl(sk_ui_context_t* ctx);

/* -------------------------------------------------------------------------- */
/* Input / hit-test / focus (input.c)                                         */
/* -------------------------------------------------------------------------- */

/** Clear hover/active/focus/capture if they reference @p node (node is dying). */
void ui_input_on_node_destroy(sk_ui_context_t* ctx, sk_ui_node_t node);

/** Default interaction flags for a newly allocated node of @p kind. */
void ui_input_node_defaults(ui_node_slot_t* slot, sk_ui_node_kind_t kind);

sk_ui_node_t ui_hit_test_impl(const sk_ui_context_t* ctx, f32 x, f32 y);
i32 ui_node_get_abs_rect_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content);
i32 ui_node_set_callbacks_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_node_callbacks_t* callbacks);
i32 ui_node_get_callbacks_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_callbacks_t* out);
i32 ui_node_set_clip_children_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 clip);
i32 ui_node_get_clip_children_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_node_set_pointer_events_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_pointer_events_t mode);
sk_ui_pointer_events_t ui_node_get_pointer_events_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_node_set_focusable_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 focusable);
i32 ui_node_get_focusable_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_focus_set_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
sk_ui_node_t ui_focus_get_impl(const sk_ui_context_t* ctx);
i32 ui_focus_advance_impl(sk_ui_context_t* ctx, i32 reverse);
i32 ui_input_dispatch_impl(sk_ui_context_t* ctx, const sk_ui_input_event_t* event);
sk_ui_node_t ui_pointer_capture_get_impl(const sk_ui_context_t* ctx);
i32 ui_pointer_capture_set_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_wants_mouse_impl(const sk_ui_context_t* ctx);
i32 ui_wants_keyboard_impl(const sk_ui_context_t* ctx);

/** Process-local API table (for in-plugin unit tests). */
const sk_ui_api_t* ui_get_api_table(void);

/* -------------------------------------------------------------------------- */
/* Paint / draw list (paint.c)                                                */
/* -------------------------------------------------------------------------- */

void ui_draw_list_store_init(ui_draw_list_store_t* store, const sk_allocator_t* a);
void ui_draw_list_store_shutdown(ui_draw_list_store_t* store);
i32 ui_paint_impl(sk_ui_context_t* ctx, const sk_ui_paint_params_t* params);
const sk_ui_draw_list_t* ui_get_draw_list_impl(const sk_ui_context_t* ctx);

/* -------------------------------------------------------------------------- */
/* Font system (font.c)                                                       */
/* -------------------------------------------------------------------------- */

sk_ui_font_system_t* ui_font_system_create_impl(const sk_allocator_t* allocator, u32 page_width, u32 page_height);
void ui_font_system_destroy_impl(sk_ui_font_system_t* system);
sk_ui_font_t* ui_font_load_path_impl(sk_ui_font_system_t* system, const sk_filesystem_api_t* fs, const_chr_t path);
sk_ui_font_t* ui_font_load_memory_impl(sk_ui_font_system_t* system, const u8* data, u32 size);
void ui_font_destroy_impl(sk_ui_font_t* font);
i32 ui_font_get_metrics_impl(const sk_ui_font_t* font, u32 pixel_size, sk_ui_font_metrics_t* out);
u32 ui_font_glyph_index_impl(const sk_ui_font_t* font, u32 codepoint);
i32 ui_font_get_glyph_impl(sk_ui_font_system_t* system, sk_ui_font_t* font, u32 pixel_size, u32 glyph_index, sk_ui_glyph_t* out);
u32 ui_font_atlas_page_count_impl(const sk_ui_font_system_t* system);
i32 ui_font_atlas_get_page_impl(const sk_ui_font_system_t* system, u32 page_index, sk_ui_atlas_page_t* out);
u32 ui_font_cache_count_impl(const sk_ui_font_system_t* system);
void ui_font_cache_stats_impl(const sk_ui_font_system_t* system, u32* out_hits, u32* out_misses);
