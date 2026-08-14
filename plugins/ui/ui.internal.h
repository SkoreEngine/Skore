#pragma once

/**
 * @file ui.internal.h
 * @brief Private UI plugin state shared by the ui plugin translation units.
 *
 * Not part of the public sk-ui surface. Do not include from hosts or other plugins.
 */

#include "ui.h"

#include "array.h"
#include "hashmap.h"
#include "logger.h"

#include <stddef.h>
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
/* Dock model (dock.c)                                                        */
/* -------------------------------------------------------------------------- */

enum { UI_DOCK_KIND_LEAF = 0, UI_DOCK_KIND_SPLIT = 1 };

typedef struct ui_dock_slot_t {
	u32 generation;
	u8 alive;
	u8 kind;	 /**< UI_DOCK_KIND_LEAF or UI_DOCK_KIND_SPLIT. */
	u8 _pad0[2]; /**< Align flags. */
	u32 flags;
	sk_ui_dock_node_t parent;
	sk_ui_dock_split_t axis;
	f32 ratio;
	sk_ui_dock_node_t child[2];
	char* tabs[SK_UI_DOCK_LEAF_TABS_MAX];
	u32 tab_count;
	u32 active_index;
	char* stable_id;
	sk_ui_node_t host;
	sk_ui_node_t splitter;
	sk_ui_node_t tab_bar;
	sk_ui_node_t content;
	sk_ui_rect_t rect;
	sk_ui_rect_t splitter_rect;
} ui_dock_slot_t;

typedef SK_ARRAY(ui_dock_slot_t) ui_dock_slot_array_t;
typedef SK_HASH_MAP(const_chr_t, sk_ui_dock_node_t) ui_dock_window_map_t;
typedef SK_HASH_MAP(const_chr_t, sk_ui_node_t) ui_dock_tab_map_t;

typedef struct ui_dock_pending_t {
	char* window_id; /**< Copied; matches node_set_id / find_by_id. */
	sk_ui_dock_node_t node;
	sk_ui_dock_dir_t dir;
} ui_dock_pending_t;

/** Host window catalog: restore drop / default placement. */
typedef struct ui_dock_window_reg_t {
	char* id;
	char* default_target; /**< Stable dock-node id, or NULL to float. */
	f32 x;
	f32 y;
	f32 w;
	f32 h;
} ui_dock_window_reg_t;

typedef struct ui_dockspace_t {
	char* id;
	u32 flags;
	u8 dirty;
	u8 laid_out;
	u8 _pad0[2]; /**< Align root handle. */
	sk_ui_dock_node_t root;
	sk_ui_node_t host; /* widget_dock_space */
	sk_ui_node_t drop;
	sk_ui_rect_t last_rect;
	ui_dock_pending_t pending[SK_UI_DOCK_PENDING_MAX];
	u32 pending_count;
	u8 _pad1[4]; /**< Align struct to 8. */
} ui_dockspace_t;

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
	/* Layout / id / disabled stacks (APX-345). Always present (not SK_TESTS-only). */
	u8 disabled_depth;
	u8 disabled_active; /**< Count of true frames on disabled_stack. */
	u8 id_stack_depth;
	u8 has_next_item_width;
	u8 disabled_stack[16];
	u16 id_mark[16]; /**< id_prefix length at each push_id. */
	char id_prefix[192];
	u16 id_prefix_len;
	f32 next_item_width;
	f32 indent_px;
	sk_ui_clipboard_get_fn clipboard_get;
	sk_ui_clipboard_set_fn clipboard_set;
	void_ptr_t clipboard_user;
	i32 widgets_defaults_registered;
	ui_node_list_t item_binds; /**< Hosts with a live item-array bind (item_bind.c). */

	/* Dock model (dock.c). Unused when dockspace_count == 0. */
	ui_dock_slot_array_t dock_slots;
	ui_freelist_t dock_freelist;
	ui_dockspace_t dockspaces[SK_UI_DOCKSPACE_MAX];
	u32 dockspace_count;
	sk_ui_dock_node_t dock_current;
	sk_ui_dock_node_t dock_builder_root;
	i32 dock_builder_open;
	i32 dock_applying;
	u8 dock_restoring;		 /**< Non-zero while dock_layout_load_json rebuilds the tree. */
	u8 _dock_restore_pad[3]; /**< Align after dock_restoring. */
	ui_dock_window_map_t dock_window_map;
	ui_dock_tab_map_t dock_tab_nodes;
	ui_dock_window_reg_t dock_regs[SK_UI_DOCK_WINDOW_REG_MAX];
	u32 dock_reg_count;
	u8 _dock_reg_pad[4]; /**< Align after dock_reg_count. */
	sk_ui_dock_tab_fn dock_tab_cb;
	void_ptr_t dock_tab_user;
	u32 dock_leaf_count;
	u32 dock_split_count;
	u32 dock_apply_count;
	/* Live chrome layers (last children of context_root). */
	sk_ui_node_t dock_stash;
	sk_ui_node_t dock_overlay;
	/* Active dock-drag session (tab tear-off or floating title-bar). */
	u8 dock_drag_active;
	u8 dock_drag_torn;
	u8 _dock_drag_pad[2]; /**< Align start coords. */
	f32 dock_drag_start_x;
	f32 dock_drag_start_y;
	sk_ui_node_t dock_drag_tab;
	sk_ui_node_t dock_drag_window;
	sk_ui_dock_node_t dock_drag_hover;
	sk_ui_dock_dir_t dock_drag_dir;
	char dock_drag_window_id[64];
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

sk_ui_font_system_t* ui_font_system_create_impl(const sk_allocator_t* allocator);
void ui_font_system_destroy_impl(sk_ui_font_system_t* system);
sk_ui_font_t* ui_font_load_path_impl(sk_ui_font_system_t* system, const sk_filesystem_api_t* fs, const_chr_t path);
sk_ui_font_t* ui_font_load_memory_impl(sk_ui_font_system_t* system, const u8* data, u32 size);
void ui_font_destroy_impl(sk_ui_font_t* font);
i32 ui_font_get_metrics_impl(const sk_ui_font_t* font, u32 pixel_size, sk_ui_font_metrics_t* out);
u32 ui_font_glyph_index_impl(const sk_ui_font_t* font, u32 codepoint);

/* MSDF atlas (font_msdf.c / font.c) — bake, query, dump; layout scales one atlas. */
typedef struct ui_msdf_atlas_live_t ui_msdf_atlas_live_t;
void ui_msdf_atlas_release(const sk_allocator_t* a, ui_msdf_atlas_live_t* atlas);
i32 ui_msdf_atlas_bake(const sk_allocator_t* a, const u8* ttf_bytes, u32 ttf_size, ui_msdf_atlas_live_t** out_atlas);
i32 ui_msdf_atlas_get_public(const ui_msdf_atlas_live_t* atlas, sk_ui_msdf_atlas_t* out);
i32 ui_msdf_atlas_find_codepoint(const ui_msdf_atlas_live_t* atlas, u32 codepoint, sk_ui_msdf_glyph_t* out);
i32 ui_msdf_atlas_dump(const ui_msdf_atlas_live_t* atlas, const sk_filesystem_api_t* fs, const_chr_t path_prefix);

i32 ui_font_msdf_bake_impl(sk_ui_font_t* font);
i32 ui_font_msdf_get_atlas_impl(const sk_ui_font_t* font, sk_ui_msdf_atlas_t* out);
i32 ui_font_msdf_get_glyph_impl(const sk_ui_font_t* font, u32 codepoint, sk_ui_msdf_glyph_t* out);
i32 ui_font_msdf_dump_impl(sk_ui_font_t* font, const sk_filesystem_api_t* fs, const_chr_t path_prefix);
i32 ui_font_measure_text_impl(sk_ui_font_system_t* system, sk_ui_font_t* font, u32 pixel_size, const_chr_t utf8, f32* out_width, f32* out_height);

/**
 * One shaped codepoint at a requested pixel size. Advances / bearings / plane
 * quads are already scaled. advance_x includes kerning from prev when MSDF.
 */
typedef struct ui_text_layout_glyph_t {
	u32 codepoint;
	u32 glyph_index;
	f32 advance_x;
	f32 kerning_x; /**< Pair adjustment already included in advance_x. */
	f32 bearing_x;
	f32 bearing_y;
	f32 quad_l;
	f32 quad_b;
	f32 quad_r;
	f32 quad_t;
	f32 u0, v0, u1, v1;
	i32 has_quad;	   /**< Sample the atlas (ink). */
	i32 is_fallback;   /**< Missing cmap → .notdef / box. */
	i32 is_whitespace; /**< Advance only (space). */
} ui_text_layout_glyph_t;

i32 ui_text_utf8_next(const u8* s, size_t len, size_t* index, u32* out_cp);
i32 ui_text_layout_metrics(sk_ui_font_t* font, f32 px, sk_ui_font_metrics_t* out);
i32 ui_text_layout_shape(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, u32 prev_cp, u32 cp, ui_text_layout_glyph_t* out);
f32 ui_text_layout_measure_width(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, const u8* begin, const u8* end);
i32 ui_text_layout_measure(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, const_chr_t utf8, f32* out_width, f32* out_height);
i32 ui_text_layout_measure_extent(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, const_chr_t utf8, f32* out_advance, f32* out_min_x, f32* out_max_x, f32* out_height);

/* CPU-side MSDF decode matching the fragment shader (MSDF mode). */
f32 ui_msdf_median3(f32 r, f32 g, f32 b);
f32 ui_msdf_screen_px_range(f32 px_range, f32 atlas_w, f32 atlas_h, f32 fwidth_u, f32 fwidth_v);
f32 ui_msdf_coverage(f32 median, f32 screen_px_range);
f32 ui_msdf_sample_median_nearest(const sk_ui_msdf_atlas_t* atlas, f32 u, f32 v);
f32 ui_msdf_sample_median_bilinear(const sk_ui_msdf_atlas_t* atlas, f32 u, f32 v);

/* font.c accessors for MSDF attachment (opaque font internals). */
const sk_allocator_t* ui_font_allocator(const sk_ui_font_t* font);
const u8* ui_font_file_bytes(const sk_ui_font_t* font);
u32 ui_font_file_size(const sk_ui_font_t* font);
u32 ui_font_id(const sk_ui_font_t* font);
sk_ui_font_t* ui_font_system_find(sk_ui_font_system_t* system, u32 font_id);
u32 ui_font_system_font_count(const sk_ui_font_system_t* system);
sk_ui_font_t* ui_font_system_font_at(sk_ui_font_system_t* system, u32 index);
ui_msdf_atlas_live_t* ui_font_msdf_ptr(const sk_ui_font_t* font);
void ui_font_msdf_set(sk_ui_font_t* font, ui_msdf_atlas_live_t* atlas);

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
i32 ui_test_artifact_png_path_in_impl(const sk_filesystem_api_t* fs, const_chr_t subdir, const_chr_t name, char* out, u32 out_cap);

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
/** Type id for item-array bind user_data (item_bind.c). */
#define SK_UI_ITEM_BIND_DATA_TYPE_ID SK_TYPE_ID("sk.ui_item_bind_data", 0x4cd9667484ce0c69ULL, 0xd02fc4d8299c22b5ULL)

/** Free widget user_data if present (called from slot release). */
void ui_widget_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot);
/** Free item-bind user_data if present (called from widget release). */
void ui_item_bind_release_user_data(sk_ui_context_t* ctx, ui_node_slot_t* slot);
/** Sync every bound item-array host (style_resolve / harness_step). */
void ui_item_bind_sync_all(sk_ui_context_t* ctx);

sk_ui_node_t ui_widget_item_view_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, sk_ui_item_bind_kind_t kind, const_chr_t id);
sk_ui_node_t ui_widget_tree_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, const_chr_t id);
sk_ui_node_t ui_widget_list_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, const_chr_t id);
i32 ui_item_bind_impl(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_array_t* items, sk_ui_item_bind_kind_t kind);
i32 ui_item_bind_set_array_impl(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_array_t* items);
i32 ui_item_bind_sync_impl(sk_ui_context_t* ctx, sk_ui_node_t host);
sk_ui_item_array_t* ui_item_bind_get_array_impl(const sk_ui_context_t* ctx, sk_ui_node_t host);
sk_ui_item_bind_kind_t ui_item_bind_get_kind_impl(const sk_ui_context_t* ctx, sk_ui_node_t host);
sk_ui_node_t ui_item_bind_find_impl(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id);
u32 ui_item_bind_row_count_impl(const sk_ui_context_t* ctx, sk_ui_node_t host);
i32 ui_item_bind_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id);
i32 ui_item_bind_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, i32 open);
i32 ui_item_bind_get_selected_impl(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id);
i32 ui_item_bind_set_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, i32 selected);
u64 ui_item_bind_last_activate_impl(const sk_ui_context_t* ctx, sk_ui_node_t host);
i32 ui_item_bind_last_was_arrow_impl(const sk_ui_context_t* ctx, sk_ui_node_t host);
i32 ui_item_bind_clear_state_impl(sk_ui_context_t* ctx, sk_ui_node_t host);
i32 ui_item_bind_set_on_activate_impl(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_id_fn fn, void_ptr_t user);
i32 ui_item_bind_set_on_toggle_impl(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_id_fn fn, void_ptr_t user);

i32 ui_widgets_register_defaults_impl(sk_ui_context_t* ctx);
void ui_set_clipboard_fns_impl(sk_ui_context_t* ctx, sk_ui_clipboard_get_fn get_fn, sk_ui_clipboard_set_fn set_fn, void_ptr_t user);

sk_ui_node_t ui_widget_panel_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_view_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_label_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);
sk_ui_node_t ui_widget_small_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);
sk_ui_node_t ui_widget_invisible_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id, f32 width, f32 height, u32 flags);
sk_ui_node_t ui_widget_selection_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, i32 selected, const_chr_t id, f32 width, f32 height);
sk_ui_node_t ui_widget_bordered_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id, f32 width, f32 height);
sk_ui_node_t ui_widget_arrow_button_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 dir, const_chr_t id);
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
i32 ui_menu_set_enabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 enabled);
i32 ui_menu_get_enabled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_menu_item_set_enabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 enabled);
i32 ui_menu_item_get_enabled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_menu_item_set_shortcut_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t shortcut);
const_chr_t ui_menu_item_get_shortcut_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
f32 ui_menu_item_measure_shortcut_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_menu_item_get_shortcut_rect_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out);
i32 ui_menu_item_set_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 selected);
i32 ui_menu_item_get_selected_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
sk_ui_node_t ui_widget_menu_separator_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
i32 ui_menu_item_clicked_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
void ui_menu_dismiss_outside_impl(sk_ui_context_t* ctx, sk_ui_node_t hit, f32 x, f32 y);

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
i32 ui_button_set_size_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height);
i32 ui_button_clicked_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_button_pressed_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_button_released_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_button_is_hovered_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_button_is_active_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_button_set_selected_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 selected);
i32 ui_button_get_selected_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_button_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);
u32 ui_button_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);

sk_ui_node_t ui_widget_text_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_text_wrapped_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_text_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_text_colored_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, sk_ui_color_t color, const_chr_t id);
sk_ui_node_t ui_widget_separator_text_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_bullet_text_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_label_text_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t value, const_chr_t id);
sk_ui_node_t ui_widget_text_with_label_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t value, const_chr_t id);
sk_ui_node_t ui_widget_text_centered_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
i32 ui_text_set_text_range_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t begin, const_chr_t end);
i32 ui_text_set_color_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_color_t color);
i32 ui_text_get_color_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_color_t* out_color);
i32 ui_text_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
i32 ui_text_get_disabled_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_with_label_parts_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t* out_label, sk_ui_node_t* out_value);
i32 ui_text_centered_text_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t* out_text);

i32 ui_checkbox_set_checked_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked);
i32 ui_checkbox_get_checked_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_checkbox_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user);
i32 ui_checkbox_set_label_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label);
const_chr_t ui_checkbox_get_label_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_checkbox_bind_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32* value);
i32 ui_checkbox_bind_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32* flags, i32 flags_value);
i32 ui_checkbox_set_mixed_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 mixed);
i32 ui_checkbox_get_mixed_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_checkbox_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_checkbox_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);

i32 ui_radio_set_checked_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked);
i32 ui_radio_get_checked_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_radio_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user);
i32 ui_radio_set_label_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label);
const_chr_t ui_radio_get_label_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_radio_bind_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32* value, i32 option);
i32 ui_radio_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_radio_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);

/** Pull bound checkbox/radio pointers before style/layout (style_resolve). */
void ui_checkbox_radio_sync_all(sk_ui_context_t* ctx);

i32 ui_toggle_set_on_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 on);
i32 ui_toggle_get_on_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_toggle_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
i32 ui_toggle_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user);

i32 ui_slider_set_value_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value);
f32 ui_slider_get_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_set_range_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 min_v, f32 max_v);
i32 ui_slider_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_float_fn fn, void_ptr_t user);

sk_ui_node_t ui_widget_slider_int_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 v_min, i32 v_max, i32 value, const_chr_t format, const_chr_t id);
sk_ui_node_t ui_widget_slider_float_n_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, f32 v_min, f32 v_max, const f32* values, const_chr_t format, const_chr_t id);
sk_ui_node_t ui_widget_slider_int_n_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, i32 v_min, i32 v_max, const i32* values, const_chr_t format, const_chr_t id);
sk_ui_node_t ui_widget_drag_float_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 v_speed, f32 v_min, f32 v_max, f32 value, const_chr_t format, const_chr_t id);
sk_ui_node_t ui_widget_drag_int_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 v_speed, i32 v_min, i32 v_max, i32 value, const_chr_t format, const_chr_t id);
sk_ui_node_t ui_widget_drag_float_n_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, f32 v_speed, f32 v_min, f32 v_max, const f32* values, const_chr_t format,
										 const_chr_t id);
sk_ui_node_t ui_widget_drag_int_n_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, f32 v_speed, i32 v_min, i32 v_max, const i32* values, const_chr_t format,
									   const_chr_t id);
i32 ui_slider_set_format_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t format);
const_chr_t ui_slider_get_format_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_set_step_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 step);
f32 ui_slider_get_step_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_set_speed_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 speed);
f32 ui_slider_get_speed_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);
u32 ui_slider_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_set_label_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label);
const_chr_t ui_slider_get_label_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
i32 ui_slider_is_text_input_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_set_text_input_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 on);
i32 ui_slider_set_int_value_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value);
i32 ui_slider_get_int_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_slider_format_value_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, char* out, u32 out_cap);
i32 ui_slider_component_impl(const sk_ui_context_t* ctx, sk_ui_node_t row, i32 index, sk_ui_node_t* out_comp);
i32 ui_slider_set_values_impl(sk_ui_context_t* ctx, sk_ui_node_t row, const f32* values, i32 count);
i32 ui_slider_get_values_impl(const sk_ui_context_t* ctx, sk_ui_node_t row, f32* out, i32 count);

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

sk_ui_node_t ui_widget_text_input_multiline_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, f32 width, f32 height, const_chr_t id);
sk_ui_node_t ui_widget_text_input_with_hint_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t hint, const_chr_t id);
sk_ui_node_t ui_widget_search_input_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_text_input_readonly_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);
sk_ui_node_t ui_widget_input_scalar_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_input_data_type_t type, void_ptr_t data, const_chr_t id);
sk_ui_node_t ui_widget_input_float_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32* v, const_chr_t id);
sk_ui_node_t ui_widget_input_int_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, i32* v, const_chr_t id);
sk_ui_node_t ui_widget_input_float3_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32* v, const_chr_t id);
i32 ui_text_input_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);
u32 ui_text_input_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_set_hint_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t hint);
const_chr_t ui_text_input_get_hint_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_set_readonly_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 readonly);
i32 ui_text_input_get_readonly_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_set_password_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 password);
i32 ui_text_input_get_password_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_set_capacity_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 capacity);
i32 ui_text_input_get_capacity_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_get_selection_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, i32* out_start, i32* out_end);
i32 ui_text_input_set_error_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 show_error);
i32 ui_text_input_get_error_impl(const sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_set_size_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height);
i32 ui_text_input_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
i32 ui_text_input_changed_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_committed_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_text_input_set_on_change_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_text_fn fn, void_ptr_t user);
i32 ui_text_filter_pass_impl(const_chr_t filter, const_chr_t text);
i32 ui_input_scalar_set_range_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const void* p_min, const void* p_max);
i32 ui_input_scalar_apply_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_input_float3_component_impl(const sk_ui_context_t* ctx, sk_ui_node_t row, i32 index, sk_ui_node_t* out_field);

/** Pull bound InputScalar pointers when the field is not focused (style_resolve). */
void ui_text_input_sync_all(sk_ui_context_t* ctx);

sk_ui_node_t ui_scroll_view_content_impl(const sk_ui_context_t* ctx, sk_ui_node_t scroll_view);
i32 ui_scroll_view_set_scroll_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 scroll_x, f32 scroll_y);
i32 ui_scroll_view_get_scroll_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, f32* out_x, f32* out_y);
i32 ui_scroll_view_set_content_size_impl(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height);

i32 ui_image_set_texture_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 texture_id);

sk_ui_node_t ui_widget_window_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t title, const_chr_t id, i32* p_open);
sk_ui_node_t ui_widget_fullscreen_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_child_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id, f32 width, f32 height, u32 flags);
sk_ui_node_t ui_child_content_impl(const sk_ui_context_t* ctx, sk_ui_node_t child);
i32 ui_child_set_flags_impl(sk_ui_context_t* ctx, sk_ui_node_t child, u32 flags);
u32 ui_child_get_flags_impl(const sk_ui_context_t* ctx, sk_ui_node_t child);
i32 ui_child_set_size_impl(sk_ui_context_t* ctx, sk_ui_node_t child, f32 width, f32 height);
i32 ui_scroll_view_scroll_to_bottom_impl(sk_ui_context_t* ctx, sk_ui_node_t node);
i32 ui_editor_window_bind_open_impl(sk_ui_context_t* ctx, sk_ui_node_t window, i32* p_open);
i32 ui_editor_window_get_open_impl(const sk_ui_context_t* ctx, sk_ui_node_t window);
i32 ui_editor_window_set_open_impl(sk_ui_context_t* ctx, sk_ui_node_t window, i32 open);
sk_ui_node_t ui_editor_window_close_button_impl(const sk_ui_context_t* ctx, sk_ui_node_t window);
i32 ui_begin_disabled_impl(sk_ui_context_t* ctx, i32 disabled);
i32 ui_end_disabled_impl(sk_ui_context_t* ctx);
i32 ui_is_disabled_impl(const sk_ui_context_t* ctx);
i32 ui_set_disabled_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
i32 ui_push_id_impl(sk_ui_context_t* ctx, const_chr_t id);
i32 ui_push_id_int_impl(sk_ui_context_t* ctx, i32 id);
i32 ui_push_id_ptr_impl(sk_ui_context_t* ctx, const void* ptr);
i32 ui_pop_id_impl(sk_ui_context_t* ctx);
i32 ui_set_next_item_width_impl(sk_ui_context_t* ctx, f32 width);
i32 ui_indent_impl(sk_ui_context_t* ctx, f32 width);
i32 ui_unindent_impl(sk_ui_context_t* ctx, f32 width);
sk_ui_node_t ui_widget_group_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
i32 ui_group_get_extents_impl(const sk_ui_context_t* ctx, sk_ui_node_t group, sk_ui_rect_t* out);
sk_ui_node_t ui_widget_horizontal_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_vertical_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
sk_ui_node_t ui_widget_spring_impl(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 weight, const_chr_t id);

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

/* -------------------------------------------------------------------------- */
/* Deterministic docking demo (sample_dock.c)                                 */
/* -------------------------------------------------------------------------- */

i32 ui_sample_dock_demo_register_styles_impl(sk_ui_context_t* ctx);
sk_ui_node_t ui_sample_dock_demo_build_impl(sk_ui_context_t* ctx, sk_ui_node_t parent);
void ui_sample_dock_demo_logical_size_impl(f32* out_width, f32* out_height);

/* -------------------------------------------------------------------------- */
/* Dock model + layout solver (dock.c)                                        */
/* -------------------------------------------------------------------------- */

i32 ui_dock_context_init(sk_ui_context_t* ctx);
void ui_dock_context_shutdown(sk_ui_context_t* ctx);
void ui_dock_on_window_destroy(sk_ui_context_t* ctx, sk_ui_node_t node);
void ui_dock_on_node_set_id(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t id);

sk_ui_dock_node_t ui_dockspace_begin_impl(sk_ui_context_t* ctx, sk_ui_node_t host, const_chr_t id, u32 flags);
i32 ui_dockspace_end_impl(sk_ui_context_t* ctx);
sk_ui_dock_node_t ui_dockspace_create_impl(sk_ui_context_t* ctx, sk_ui_node_t host, const_chr_t id, u32 flags);
i32 ui_dockspace_apply_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace);
sk_ui_dock_node_t ui_dockspace_find_impl(const sk_ui_context_t* ctx, const_chr_t id);
sk_ui_node_t ui_dockspace_host_node_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace);
i32 ui_dockspace_destroy_impl(sk_ui_context_t* ctx, const_chr_t id);

i32 ui_dock_window_to_node_impl(sk_ui_context_t* ctx, const_chr_t window_id, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir);
i32 ui_dock_window_undock_impl(sk_ui_context_t* ctx, const_chr_t window_id);
i32 ui_dock_tab_close_impl(sk_ui_context_t* ctx, const_chr_t window_id);
i32 ui_dock_tab_reorder_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t leaf, u32 from_index, u32 to_index);
i32 ui_dock_tab_set_active_impl(sk_ui_context_t* ctx, const_chr_t window_id);
void ui_dock_set_tab_callback_impl(sk_ui_context_t* ctx, sk_ui_dock_tab_fn fn, void_ptr_t user);

i32 ui_dock_builder_begin_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace);
i32 ui_dock_builder_split_node_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir, f32 ratio, sk_ui_dock_node_t* out_at_dir, sk_ui_dock_node_t* out_opposite);
i32 ui_dock_builder_dock_window_impl(sk_ui_context_t* ctx, const_chr_t window_id, sk_ui_dock_node_t node);
i32 ui_dock_builder_set_node_id_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t node, const_chr_t id);
i32 ui_dock_builder_set_node_flags_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t node, u32 flags);
i32 ui_dock_builder_finish_impl(sk_ui_context_t* ctx);

sk_ui_dock_node_t ui_dock_node_at_point_impl(const sk_ui_context_t* ctx, f32 x, f32 y, sk_ui_dock_dir_t* out_dir);
sk_ui_dock_node_t ui_dock_find_node_for_window_impl(const sk_ui_context_t* ctx, const_chr_t window_id);
i32 ui_dock_leaf_tabs_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t leaf, const_chr_t* out_ids, u32 max_out, u32* out_count, u32* out_active);
i32 ui_dock_node_is_leaf_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);
i32 ui_dock_node_is_split_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);
sk_ui_dock_split_t ui_dock_split_get_axis_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);
f32 ui_dock_split_get_ratio_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);
i32 ui_dock_split_set_ratio_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t node, f32 ratio);
sk_ui_dock_node_t ui_dock_split_child_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node, u32 index);
sk_ui_node_t ui_dock_node_host_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);
i32 ui_dock_window_is_docked_impl(const sk_ui_context_t* ctx, const_chr_t window_id);
i32 ui_dock_window_register_impl(sk_ui_context_t* ctx, const_chr_t window_id, const_chr_t default_target, const sk_ui_rect_t* default_rect);

i32 ui_dock_layout_save_json_impl(const sk_ui_context_t* ctx, const_chr_t dockspace_id, char* out, u32 cap, u32* out_len);
i32 ui_dock_layout_load_json_impl(sk_ui_context_t* ctx, const_chr_t dockspace_id, const_chr_t json, u32 len);

i32 ui_dockspace_layout_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace, const sk_ui_rect_t* space);
i32 ui_dock_node_get_rect_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node, sk_ui_rect_t* out);
i32 ui_dock_split_get_splitter_rect_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node, sk_ui_rect_t* out);

/** Apply dirty AUTO_APPLY dockspaces before Clay (forgotten-end safety net). */
void ui_dock_layout_begin(sk_ui_context_t* ctx);
/** Copy chrome abs rects into the model after Clay writeback. */
void ui_dock_layout_end(sk_ui_context_t* ctx);
/** Floating title-bar pointer hook (move/up) — drop overlay + redock. */
void ui_dock_on_float_pointer(sk_ui_context_t* ctx, sk_ui_node_t window, sk_ui_event_t* event);
/** Continue a torn-off dock drag after chrome recycle clears capture. */
void ui_dock_drag_tick(sk_ui_context_t* ctx, f32 x, f32 y, i32 button_up);
