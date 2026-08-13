#pragma once

/**
 * @file ui_internal.h
 * @brief Private UI plugin state shared by the ui plugin translation units.
 *
 * Not part of the public sk-ui surface. Do not include from hosts or other plugins.
 */

#include "ui.h"

#include "array.h"
#include "hashmap.h"
#include "logger.h"

#include <string.h>

/* Host logger captured at sk_ui_init. NULL until the plugin is loaded. */
const sk_logger_api_t* ui_logger_api(void);
sk_logger_context_t* ui_logger_context(void);
void ui_bind_host_logger(const sk_logger_api_t* api, sk_logger_context_t* log_ctx);

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

/** Opaque Clay adapter frame state (clay_adapter.c). NULL until first layout. */
typedef struct ui_clay_frame_t ui_clay_frame_t;

struct sk_ui_context_t {
	const sk_allocator_t* allocator;
	ui_slot_array_t slots; /* index 0 unused; live handles use index >= 1 */
	ui_freelist_t freelist;
	ui_id_map_t id_map;
	ui_style_class_array_t style_classes;
	ui_style_registry_t style_registry;
	sk_ui_node_t root;
	u32 live_count;

	/* Clay adapter (clay_adapter.c): per-context frame state. */
	ui_clay_frame_t* clay_frame;
	f32 scroll_delta_x; /**< Wheel input accumulated since last layout (Clay scroll update). */
	f32 scroll_delta_y;

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

	/* Widget set */
	u32 widget_id_seq; /**< Auto test-id counter for widgets without explicit id. */
	sk_ui_clipboard_get_fn clipboard_get;
	sk_ui_clipboard_set_fn clipboard_set;
	void_ptr_t clipboard_user;
	i32 widgets_defaults_registered;
};

/* -------------------------------------------------------------------------- */
/* Slot access (ui.c)                                                         */
/* -------------------------------------------------------------------------- */

ui_node_slot_t* ui_slot_mut(sk_ui_context_t* ctx, sk_ui_node_t node);
const ui_node_slot_t* ui_slot(const sk_ui_context_t* ctx, sk_ui_node_t node);
void ui_mark_dirty_up(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);

/* -------------------------------------------------------------------------- */
/* Layout style / query API (ui.c; Clay-backed solver in clay_adapter.c)       */
/* -------------------------------------------------------------------------- */

void ui_layout_style_init_default(sk_ui_layout_style_t* style);

i32 ui_node_set_layout_style_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_layout_style_t* style);
i32 ui_node_get_layout_style_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_layout_style_t* out);
i32 ui_node_get_layout_rect_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content);
i32 ui_node_get_layout_rect_scaled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content);

void ui_set_measure_fn_impl(sk_ui_context_t* ctx, sk_ui_measure_fn fn, void_ptr_t user);
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

/* -------------------------------------------------------------------------- */
/* GPU renderer (render.c)                                                    */
/* -------------------------------------------------------------------------- */

sk_ui_renderer_t* ui_renderer_create_impl(const sk_ui_renderer_desc_t* desc);
void ui_renderer_destroy_impl(sk_ui_renderer_t* renderer);
i32 ui_renderer_set_render_pass_impl(sk_ui_renderer_t* renderer, sk_render_pass_t render_pass);
i32 ui_renderer_prepare_impl(sk_ui_renderer_t* renderer, const sk_ui_renderer_prepare_info_t* info);
i32 ui_renderer_encode_impl(sk_ui_renderer_t* renderer, const sk_ui_renderer_encode_info_t* info);

/* -------------------------------------------------------------------------- */
/* Headless capture (capture.c)                                               */
/* -------------------------------------------------------------------------- */

sk_ui_capture_t* ui_capture_create_impl(const sk_ui_capture_desc_t* desc);
void ui_capture_destroy_impl(sk_ui_capture_t* capture);
i32 ui_capture_frame_impl(sk_ui_capture_t* capture, const sk_ui_capture_frame_info_t* info, sk_ui_cpu_image_t* out_image);

/* -------------------------------------------------------------------------- */
/* CPU image PNG write (image_write.c)                                        */
/* -------------------------------------------------------------------------- */

i32 ui_cpu_image_write_png_impl(const sk_ui_cpu_image_t* image, const sk_filesystem_api_t* fs, const_chr_t path);
i32 ui_test_artifact_root_impl(const sk_filesystem_api_t* fs, char* out, u32 out_cap);
i32 ui_test_artifact_png_path_impl(const sk_filesystem_api_t* fs, const_chr_t name, char* out, u32 out_cap);

/* -------------------------------------------------------------------------- */
/* Golden image comparison (image_compare.c)                                  */
/* -------------------------------------------------------------------------- */

i32 ui_cpu_image_compare_impl(const sk_ui_cpu_image_t* actual, const sk_ui_cpu_image_t* expected, u32 channel_tolerance, f32 max_diff_fraction,
							  sk_ui_image_compare_stats_t* out_stats, u8* out_diff_rgba);
i32 ui_cpu_image_compare_golden_impl(const sk_ui_cpu_image_t* actual, const_chr_t golden_path, const sk_ui_image_compare_params_t* params, const sk_filesystem_api_t* fs,
									 sk_ui_image_compare_stats_t* out_stats);

/* -------------------------------------------------------------------------- */
/* Structural image assertions (image_structure.c)                            */
/* -------------------------------------------------------------------------- */

i32 ui_cpu_image_assert_solid_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_color_match_t* color, sk_ui_solid_stats_t* out_stats);
i32 ui_cpu_image_assert_coverage_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_color_match_t* color, f32 min_fraction, f32 max_fraction,
									  sk_ui_coverage_stats_t* out_stats);
i32 ui_cpu_image_find_bbox_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_color_match_t* color, sk_ui_bbox_t* out_bbox);
i32 ui_cpu_image_assert_bbox_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_color_match_t* color, const sk_ui_bbox_expected_t* expected,
								  sk_ui_bbox_assert_stats_t* out_stats);
i32 ui_cpu_image_histogram_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, u8 merge_tolerance, u32 max_entries, sk_ui_color_histogram_t* out_hist);
i32 ui_cpu_image_assert_histogram_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_hist_assert_params_t* params, const sk_ui_hist_expectation_t* expected,
									   u32 expected_count, sk_ui_hist_assert_stats_t* out_stats);
i32 ui_cpu_image_region_hash_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, u64 seed, u64* out_hash);
i32 ui_cpu_image_assert_region_hash_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, u64 expected_hash, u64* out_actual_hash);

/* -------------------------------------------------------------------------- */
/* Widgets (widgets.c)                                                        */
/* -------------------------------------------------------------------------- */

/** Type id for widget-owned user_data (freed on node destroy). */
#define SK_UI_WIDGET_DATA_TYPE_ID SK_TYPE_ID("sk.ui_widget_data", 0xa1b2c3d4e5f60718ULL, 0x918273645a5b6c7dULL)

/** Free widget user_data if present (called from slot release). */
void ui_widget_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot);

i32 ui_widgets_register_defaults_impl(sk_ui_context_t* ctx);
void ui_set_clipboard_fns_impl(sk_ui_context_t* ctx, sk_ui_clipboard_get_fn get_fn, sk_ui_clipboard_set_fn set_fn, void_ptr_t user);

sk_ui_node_t ui_widget_panel_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_view_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_label_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);
sk_ui_node_t ui_widget_checkbox_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 checked, const_chr_t id);
sk_ui_node_t ui_widget_radio_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 checked, const_chr_t id);
sk_ui_node_t ui_widget_toggle_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 on, const_chr_t id);
sk_ui_node_t ui_widget_slider_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 min_v, f32 max_v, f32 value, const_chr_t id);
sk_ui_node_t ui_widget_range_slider_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 min_v, f32 max_v, f32 value_low, f32 value_high, const_chr_t id);
sk_ui_node_t ui_widget_progress_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 fraction, const_chr_t id);
sk_ui_node_t ui_widget_text_input_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_scroll_view_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_image_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 texture_id, const_chr_t id);

sk_ui_node_t ui_widget_menu_bar_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_menu_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);
sk_ui_node_t ui_widget_menu_item_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);
sk_ui_node_t ui_widget_menu_popup_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 attach, const_chr_t id);
sk_ui_node_t ui_widget_dropdown_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);
sk_ui_node_t ui_widget_context_menu_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_submenu_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);
i32 ui_menu_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 open);
i32 ui_menu_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
sk_ui_node_t ui_menu_get_popup_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);

sk_ui_node_t ui_widget_dock_space_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_dock_node_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 orientation, const_chr_t id);
sk_ui_node_t ui_widget_splitter_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 axis, const_chr_t id);
sk_ui_node_t ui_widget_tab_bar_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_tab_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);
sk_ui_node_t ui_widget_editor_window_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t title, const_chr_t id);
sk_ui_node_t ui_editor_window_title_bar_impl(const sk_ui_context_t* ctx, sk_ui_node_t window);
sk_ui_node_t ui_editor_window_content_impl(const sk_ui_context_t* ctx, sk_ui_node_t window);
i32 ui_editor_window_set_title_impl(sk_ui_context_t* ctx, sk_ui_node_t window, const_chr_t title);
i32 ui_tab_set_active_impl(sk_ui_context_t* ctx, sk_ui_node_t tab, i32 active);
i32 ui_tab_get_active_impl(const sk_ui_context_t* ctx, sk_ui_node_t tab);
i32 ui_tab_bar_set_active_impl(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, sk_ui_node_t tab);
i32 ui_splitter_set_ratio_impl(sk_ui_context_t* ctx, sk_ui_node_t splitter, f32 ratio);
f32 ui_splitter_get_ratio_impl(const sk_ui_context_t* ctx, sk_ui_node_t splitter);
i32 ui_splitter_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t splitter, sk_ui_widget_float_fn fn, void_ptr_t user);

i32 ui_label_set_text_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text);
const_chr_t ui_label_get_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_label_set_wrap_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 wrap);
i32 ui_label_set_align_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 text_align, i32 vertical_align);

i32 ui_button_set_label_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label);
i32 ui_button_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);

i32 ui_checkbox_set_checked_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked);
i32 ui_checkbox_get_checked_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_checkbox_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user);

i32 ui_radio_set_checked_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked);
i32 ui_radio_get_checked_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_radio_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user);

i32 ui_toggle_set_on_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 on);
i32 ui_toggle_get_on_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_toggle_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
i32 ui_toggle_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user);

i32 ui_slider_set_value_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value);
f32 ui_slider_get_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_set_range_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 min_v, f32 max_v);
i32 ui_slider_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_float_fn fn, void_ptr_t user);

i32 ui_range_slider_set_values_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value_low, f32 value_high);
i32 ui_range_slider_get_values_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, f32* out_low, f32* out_high);

i32 ui_progress_set_value_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 fraction);
f32 ui_progress_get_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);

i32 ui_text_input_set_text_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text);
const_chr_t ui_text_input_get_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_get_caret_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_set_selection_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 start, i32 end);
i32 ui_text_input_insert_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t utf8);
i32 ui_text_input_delete_selection_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_copy_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_cut_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_paste_impl(sk_ui_context_t* ctx, sk_ui_node_t node);

sk_ui_node_t ui_scroll_view_content_impl(const sk_ui_context_t* ctx, sk_ui_node_t scroll_view);
i32 ui_scroll_view_set_scroll_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 scroll_x, f32 scroll_y);
i32 ui_scroll_view_get_scroll_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, f32* out_x, f32* out_y);
i32 ui_scroll_view_set_content_size_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height);

i32 ui_image_set_texture_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 texture_id);

/* -------------------------------------------------------------------------- */
/* Clay immediate-mode layout bridge (clay.c)                                 */
/* -------------------------------------------------------------------------- */

/**
 * Initialize the Clay context (vendored thirdparty/clay). The arena is
 * allocated through @p allocator (NULL = process default); @p viewport_width /
 * @p viewport_height are the current layout dimensions. @p font_system /
 * @p font (both optional, must be NULL or non-NULL together) back
 * Clay_SetMeasureTextFunction with engine font metrics.
 * @return 0 on success, non-zero on failure or if already initialized.
 */
i32 ui_clay_init(const sk_allocator_t* allocator, f32 viewport_width, f32 viewport_height, sk_ui_font_system_t* font_system, sk_ui_font_t* font);

/**
 * Free the Clay arena and logger. Safe on uninitialized state.
 */
void ui_clay_shutdown(void);

/**
 * Idempotent init used by the Clay adapter (clay_adapter.c): initializes Clay
 * on first call and otherwise keeps the arena; viewport dimensions and the
 * font binding are refreshed from the arguments on every call. Fonts are
 * optional (NULL = keep the current binding, or fallback estimation when no
 * font was ever bound).
 * @return 0 on success, non-zero on failure.
 */
i32 ui_clay_ensure_init(const sk_allocator_t* allocator, f32 viewport_width, f32 viewport_height, sk_ui_font_system_t* font_system, sk_ui_font_t* font);

/**
 * Rebind the fonts used by the Clay measure callback (NULL/NULL clears).
 * Does not re-initialize Clay; affects the next layout pass.
 */
void ui_clay_set_font(sk_ui_font_system_t* font_system, sk_ui_font_t* font);

/**
 * Whether Clay has been initialized (used by the adapter to skip redundant
 * init work and by tests to assert lifecycle ordering).
 */
i32 ui_clay_is_initialized(void);

/* -------------------------------------------------------------------------- */
/* Clay-backed adapter (clay_adapter.c)                                       */
/* -------------------------------------------------------------------------- */

/**
 * Clay-backed layout() implementation (replaces the deleted custom solver in
 * the API table): maps the whole retained tree onto Clay's immediate-mode
 * lifecycle in one pass and writes the resulting boxes back into the slot
 * layout rects so hit-testing, scale application, paint, and queries keep
 * working unchanged.
 */
i32 ui_clay_layout_impl(sk_ui_context_t* ctx, f32 root_width, f32 root_height);

/** Free per-context Clay adapter state (called from context destroy). */
void ui_clay_context_shutdown(sk_ui_context_t* ctx);

/* -------------------------------------------------------------------------- */
/* Automation / harness (automation.c)                                        */
/* -------------------------------------------------------------------------- */

sk_ui_node_t ui_query_by_test_id_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t test_id);
sk_ui_node_t ui_query_by_class_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t class_name);
sk_ui_node_t ui_query_by_widget_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t widget_type);
sk_ui_node_t ui_query_by_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t text);
u32 ui_query_all_by_class_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t class_name, sk_ui_node_t* out, u32 max_out);
u32 ui_query_all_by_widget_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t widget_type, sk_ui_node_t* out, u32 max_out);
u32 ui_query_all_by_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t text, sk_ui_node_t* out, u32 max_out);

i32 ui_node_is_visible_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_node_is_enabled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
const_chr_t ui_node_get_visible_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);

i32 ui_action_click_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_action_type_text_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text);
i32 ui_action_scroll_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 scroll_x, f32 scroll_y);
i32 ui_action_focus_impl(sk_ui_context_t* ctx, sk_ui_node_t node);

sk_ui_harness_t* ui_harness_create_impl(const sk_ui_harness_desc_t* desc);
void ui_harness_destroy_impl(sk_ui_harness_t* harness);
sk_ui_context_t* ui_harness_context_impl(sk_ui_harness_t* harness);
i32 ui_harness_step_impl(sk_ui_harness_t* harness, f32 delta_seconds);
f64 ui_harness_time_impl(const sk_ui_harness_t* harness);
f32 ui_harness_last_delta_impl(const sk_ui_harness_t* harness);
u32 ui_harness_frame_index_impl(const sk_ui_harness_t* harness);
i32 ui_harness_set_size_impl(sk_ui_harness_t* harness, f32 width, f32 height);
i32 ui_harness_set_content_scale_impl(sk_ui_harness_t* harness, f32 scale);
const u8* ui_harness_pixels_impl(const sk_ui_harness_t* harness);
void ui_harness_pixel_size_impl(const sk_ui_harness_t* harness, u32* out_w, u32* out_h);
void ui_harness_set_font_impl(sk_ui_harness_t* harness, sk_ui_font_system_t* system, sk_ui_font_t* font);
const sk_ui_draw_list_t* ui_harness_draw_list_impl(const sk_ui_harness_t* harness);

/* -------------------------------------------------------------------------- */
/* Test engine (test_engine.c) — item registry + frame control                */
/* -------------------------------------------------------------------------- */

sk_ui_test_engine_t* ui_test_engine_create_impl(const sk_ui_test_engine_desc_t* desc);
void ui_test_engine_destroy_impl(sk_ui_test_engine_t* engine);
sk_ui_context_t* ui_test_engine_context_impl(sk_ui_test_engine_t* engine);
sk_ui_harness_t* ui_test_engine_harness_impl(sk_ui_test_engine_t* engine);
i32 ui_test_engine_step_impl(sk_ui_test_engine_t* engine, f32 delta_seconds);
i32 ui_test_engine_yield_frames_impl(sk_ui_test_engine_t* engine, u32 frame_count, f32 delta_seconds);
i32 ui_test_engine_run_until_impl(sk_ui_test_engine_t* engine, sk_ui_test_predicate_fn pred, void_ptr_t user, u32 max_frames, f32 delta_seconds);
f64 ui_test_engine_time_impl(const sk_ui_test_engine_t* engine);
u32 ui_test_engine_frame_index_impl(const sk_ui_test_engine_t* engine);
const sk_ui_test_item_t* ui_test_engine_find_by_id_impl(const sk_ui_test_engine_t* engine, const_chr_t test_id);
const sk_ui_test_item_t* ui_test_engine_find_by_path_impl(const sk_ui_test_engine_t* engine, const_chr_t id_path);
u32 ui_test_engine_item_count_impl(const sk_ui_test_engine_t* engine);
const sk_ui_test_item_t* ui_test_engine_item_at_impl(const sk_ui_test_engine_t* engine, u32 index);
const_chr_t ui_test_engine_last_error_impl(const sk_ui_test_engine_t* engine);

i32 ui_test_engine_input_impl(sk_ui_test_engine_t* engine, const sk_ui_input_event_t* event);
i32 ui_test_engine_mouse_move_impl(sk_ui_test_engine_t* engine, f32 x, f32 y);
i32 ui_test_engine_mouse_button_impl(sk_ui_test_engine_t* engine, i32 button, i32 down, u32 mods);
i32 ui_test_engine_scroll_wheel_impl(sk_ui_test_engine_t* engine, f32 scroll_x, f32 scroll_y, u32 mods);
i32 ui_test_engine_key_impl(sk_ui_test_engine_t* engine, i32 key, i32 down, u32 mods);
i32 ui_test_engine_text_impl(sk_ui_test_engine_t* engine, const_chr_t text);
i32 ui_test_engine_hover_impl(sk_ui_test_engine_t* engine, const_chr_t test_id);
i32 ui_test_engine_click_impl(sk_ui_test_engine_t* engine, const_chr_t test_id);
i32 ui_test_engine_click_ex_impl(sk_ui_test_engine_t* engine, const_chr_t test_id, i32 button, u32 mods);
i32 ui_test_engine_double_click_impl(sk_ui_test_engine_t* engine, const_chr_t test_id);
i32 ui_test_engine_press_impl(sk_ui_test_engine_t* engine, const_chr_t test_id, i32 button, u32 mods);
i32 ui_test_engine_release_impl(sk_ui_test_engine_t* engine, i32 button, u32 mods);
i32 ui_test_engine_drag_impl(sk_ui_test_engine_t* engine, f32 x0, f32 y0, f32 x1, f32 y1, u32 motion_frames, f32 delta_seconds);
i32 ui_test_engine_type_impl(sk_ui_test_engine_t* engine, const_chr_t test_id, const_chr_t text);
i32 ui_test_engine_scroll_impl(sk_ui_test_engine_t* engine, const_chr_t test_id, f32 scroll_x, f32 scroll_y);
i32 ui_test_engine_focus_impl(sk_ui_test_engine_t* engine, const_chr_t test_id);

/* -------------------------------------------------------------------------- */
/* Sample main menu scene (sample_menu.c)                                     */
/* -------------------------------------------------------------------------- */

i32 ui_sample_menu_register_styles_impl(sk_ui_context_t* ctx);
sk_ui_node_t ui_sample_menu_build_impl(sk_ui_context_t* ctx, sk_ui_node_t parent);
void ui_sample_menu_logical_size_impl(f32* out_width, f32* out_height);
