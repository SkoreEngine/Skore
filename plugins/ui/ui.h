#pragma once

/**
 * @file ui.h
 * @brief Retained-mode UI module API.
 *
 * Implemented by the sk-ui plugin (SHARED, statically linked sk-foundation).
 * The plugin registers a static sk_ui_api_t on the app context; hosts
 * obtain it **only** via the app registry:
 *
 *   const sk_ui_api_t* ui =
 *       (const sk_ui_api_t*)app_api->get_api(ctx, SK_UI_API_TYPE_ID);
 *
 * This surface owns the retained element tree: stable generation handles,
 * parent/child hierarchy, per-node id/class/properties, create/destroy
 * lifecycle, layout vs paint dirty flags with ancestor propagation,
 * tree traversals, and a flexbox layout solver over the tree.
 *
 * Tree/layout/paint are pure CPU. GPU upload and draws live on the optional
 * sk_ui_renderer_t path (render_device + DXC only; no parallel device layer).
 * Layout runs in logical units; apply_scale maps to physical pixels.
 */

#include "allocator.h"
#include "app.h"
#include "common.h"
#include "dxc_compiler.h"
#include "filesystem.h"
#include "render_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_ui_api_t in the app registry. */
#define SK_UI_API_TYPE_ID SK_TYPE_ID("sk.ui_api", 0xc9c0d15c0efdbacbULL, 0x2376391989195a63ULL)

/* ------------------------------------------------------------------ */
/*  Font system (FreeType face load + MSDF atlas bake; no GPU upload)  */
/* ------------------------------------------------------------------ */

/**
 * Opaque font face (one TTF/OTF). Owned by the font system that created it.
 * Destroy with font_destroy, or when the font system is destroyed.
 */
typedef struct sk_ui_font_t sk_ui_font_t;

/**
 * Opaque font system: FreeType library (face loading, cmap, metrics), loaded
 * faces, and optional per-font MSDF atlases (APX-265). CPU-side atlas; GPU
 * upload is owned by sk_ui_renderer_t.
 */
typedef struct sk_ui_font_system_t sk_ui_font_system_t;

/**
 * Opaque GPU renderer for UI draw lists.
 * Owns pipelines, dynamic VB/IB, font atlas GPU textures, and samplers.
 * Uses only sk_render_device_api_t (no parallel device layer).
 */
typedef struct sk_ui_renderer_t sk_ui_renderer_t;

/**
 * Font-level metrics at a specific pixel size (physical pixels).
 * Ascent is typically positive, descent negative (FreeType convention scaled to px).
 * line_height is the recommended baseline-to-baseline distance (positive).
 * Values come from MSDF atlas em metrics × pixel_size (one atlas, any size).
 */
typedef struct sk_ui_font_metrics_t {
	f32 ascent;
	f32 descent;
	f32 line_height;
	f32 pixel_size;
} sk_ui_font_metrics_t;

/**
 * MSDF glyph atlas snapshot (RGB8 multi-channel signed distance field).
 * pixels are font-owned; valid until the font is destroyed or rebaked.
 * Distance range is symmetric px_range (default 2.0). Sampler contract for
 * future GPU upload: linear min/mag, no mipmaps (max_lod 0), clamp-to-edge.
 */
typedef struct sk_ui_msdf_atlas_t {
	u32 width;
	u32 height;
	u32 channels;	  /**< 3 for MSDF RGB8. */
	u32 glyph_count;  /**< Glyphs packed into this atlas (ASCII bake size). */
	u32 generation;	  /**< Bumps on bake/rebake. */
	f32 px_range;	  /**< Pixel distance range used at generation (e.g. 2.0). */
	f32 pack_scale;	  /**< Packer glyph scale (ems → atlas pixels). */
	f32 em_size;	  /**< Font em size in font units. */
	f32 ascender_em;  /**< Ascender in em units (typically > 0). */
	f32 descender_em; /**< Descender in em units (typically < 0). */
	f32 line_height_em;
	const u8* pixels; /**< RGB8, row-major, pitch == width * channels. NULL if not baked. */
} sk_ui_msdf_atlas_t;

/**
 * One glyph from a baked MSDF atlas. Metrics are em-normalized (scale by
 * pixel_size / em_size at layout time). UV rect is normalized [0,1] top-left
 * origin within the MSDF atlas bitmap. Plane bounds are the quad in em space
 * (left, bottom, right, top) relative to the pen on the baseline.
 */
typedef struct sk_ui_msdf_glyph_t {
	u32 codepoint;
	u32 glyph_index;
	f32 advance_em;							/**< Horizontal advance in ems. */
	f32 plane_l, plane_b, plane_r, plane_t; /**< Quad bounds in em space. */
	f32 u0, v0, u1, v1;						/**< Atlas UV (top-left origin). */
	i32 atlas_x, atlas_y, atlas_w, atlas_h; /**< Integer box in atlas pixels. */
	i32 is_whitespace;						/**< Non-zero if no geometry (e.g. space). */
} sk_ui_msdf_glyph_t;

/**
 * Derive physical pixel size from logical font size and content scale.
 * Matches HiDPI rule: round(logical * scale), at least 1 when logical > 0.
 * content_scale is typically from get_window_content_scale / monitor scale (1.0 = 96 DPI).
 */
SK_FINLINE u32 sk_ui_font_pixel_size(f32 logical_font_size, f32 content_scale) {
	f32 s = content_scale;
	f32 px;
	if (s < 0.0f) {
		s = 0.0f;
	}
	px = logical_font_size * s;
	if (px <= 0.0f) {
		return 0u;
	}
	/* nearest: (i32)(x + 0.5) for non-negative */
	{
		const u32 rounded = (u32)(px + 0.5f);
		return rounded < 1u ? 1u : rounded;
	}
}

/* ------------------------------------------------------------------ */
/*  Node handle                                                       */
/* ------------------------------------------------------------------ */

/**
 * Stable node handle: dense slot index + generation counter.
 * index == 0 is reserved and means "invalid"; destroyed slots are recycled
 * with a bumped generation so stale handles fail the generation check.
 */
typedef struct sk_ui_node_t {
	u32 index;
	u32 generation;
} sk_ui_node_t;

/** Invalid node sentinel. */
#define SK_UI_NODE_INVALID ((sk_ui_node_t){0u, 0u})

/** @return Non-zero if @p node has a non-zero index (not the invalid sentinel). */
SK_FINLINE i32 sk_ui_node_is_valid(sk_ui_node_t node) {
	return node.index != 0u;
}

/** @return Non-zero if both handles reference the same slot + generation. */
SK_FINLINE i32 sk_ui_node_eq(sk_ui_node_t a, sk_ui_node_t b) {
	return (a.index == b.index) && (a.generation == b.generation);
}

/* ------------------------------------------------------------------ */
/*  Node kind / dirty flags / property types                          */
/* ------------------------------------------------------------------ */

/**
 * Structural tag for a node. Layout and paint specialize on kind later;
 * the tree treats all kinds uniformly.
 */
typedef enum sk_ui_node_kind_t {
	SK_UI_NODE_KIND_BOX = 0,
	SK_UI_NODE_KIND_TEXT = 1,
	SK_UI_NODE_KIND_IMAGE = 2,
	SK_UI_NODE_KIND_BUTTON = 3,
} sk_ui_node_kind_t;

/**
 * Dirty flags. LAYOUT and PAINT are the minimum required pair so layout and
 * paint passes can skip clean subtrees. STYLE is reserved for cascade work.
 * When a flag is set on a node it is also OR'd onto every ancestor so a
 * clean parent implies a clean subtree for that flag.
 */
typedef enum sk_ui_dirty_flags_t {
	SK_UI_DIRTY_NONE = 0,
	SK_UI_DIRTY_LAYOUT = 1u << 0,
	SK_UI_DIRTY_PAINT = 1u << 1,
	SK_UI_DIRTY_STYLE = 1u << 2,
	SK_UI_DIRTY_ALL = (SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT | SK_UI_DIRTY_STYLE),
} sk_ui_dirty_flags_t;

/**
 * Value type for a named node property.
 * STR values are owned copies managed by the node.
 */
typedef enum sk_ui_prop_type_t {
	SK_UI_PROP_NONE = 0,
	SK_UI_PROP_I32 = 1,
	SK_UI_PROP_F32 = 2,
	SK_UI_PROP_STR = 3,
} sk_ui_prop_type_t;

/** Tagged property value (read via node_get_prop_* helpers). */
typedef struct sk_ui_prop_value_t {
	sk_ui_prop_type_t type;
	union {
		i32 i32_value;
		f32 f32_value;
		const_chr_t str_value; /**< Points at node-owned storage; invalid after destroy/clear. */
	} data;
} sk_ui_prop_value_t;

/* ------------------------------------------------------------------ */
/*  Layout types (logical units)                                      */
/* ------------------------------------------------------------------ */

/** Axis-aligned rectangle (x/y top-left, width/height). */
typedef struct sk_ui_rect_t {
	f32 x;
	f32 y;
	f32 width;
	f32 height;
} sk_ui_rect_t;

/** Size (width/height). */
typedef struct sk_ui_size_t {
	f32 width;
	f32 height;
} sk_ui_size_t;

/** Four edges (padding, margin, border widths). */
typedef struct sk_ui_edges_t {
	f32 left;
	f32 top;
	f32 right;
	f32 bottom;
} sk_ui_edges_t;

/** Length unit for width/height/basis/insets. */
typedef enum sk_ui_length_unit_t {
	SK_UI_LENGTH_AUTO = 0,	  /**< Resolve from content / flex / stretch. */
	SK_UI_LENGTH_POINT = 1,	  /**< Logical pixels. */
	SK_UI_LENGTH_PERCENT = 2, /**< Percent of parent content box on that axis. */
} sk_ui_length_unit_t;

/** Length value: unit + numeric component (percent is 0..100). */
typedef struct sk_ui_length_t {
	sk_ui_length_unit_t unit;
	f32 value;
} sk_ui_length_t;

/** @return Length in logical points. */
SK_FINLINE sk_ui_length_t sk_ui_pt(f32 value) {
	sk_ui_length_t l;
	l.unit = SK_UI_LENGTH_POINT;
	l.value = value;
	return l;
}

/** @return Length as percent of parent content axis. */
SK_FINLINE sk_ui_length_t sk_ui_percent(f32 value) {
	sk_ui_length_t l;
	l.unit = SK_UI_LENGTH_PERCENT;
	l.value = value;
	return l;
}

/** @return Auto length. */
SK_FINLINE sk_ui_length_t sk_ui_auto(void) {
	sk_ui_length_t l;
	l.unit = SK_UI_LENGTH_AUTO;
	l.value = 0.0f;
	return l;
}

typedef enum sk_ui_flex_direction_t {
	SK_UI_FLEX_ROW = 0,
	SK_UI_FLEX_ROW_REVERSE = 1,
	SK_UI_FLEX_COLUMN = 2,
	SK_UI_FLEX_COLUMN_REVERSE = 3,
} sk_ui_flex_direction_t;

typedef enum sk_ui_flex_wrap_t {
	SK_UI_FLEX_NOWRAP = 0,
	SK_UI_FLEX_WRAP = 1,
	SK_UI_FLEX_WRAP_REVERSE = 2,
} sk_ui_flex_wrap_t;

typedef enum sk_ui_justify_t {
	SK_UI_JUSTIFY_FLEX_START = 0,
	SK_UI_JUSTIFY_FLEX_END = 1,
	SK_UI_JUSTIFY_CENTER = 2,
	SK_UI_JUSTIFY_SPACE_BETWEEN = 3,
	SK_UI_JUSTIFY_SPACE_AROUND = 4,
	SK_UI_JUSTIFY_SPACE_EVENLY = 5,
} sk_ui_justify_t;

typedef enum sk_ui_align_t {
	SK_UI_ALIGN_AUTO = 0, /**< align-self only: inherit align-items. */
	SK_UI_ALIGN_FLEX_START = 1,
	SK_UI_ALIGN_FLEX_END = 2,
	SK_UI_ALIGN_CENTER = 3,
	SK_UI_ALIGN_STRETCH = 4,
} sk_ui_align_t;

typedef enum sk_ui_position_t {
	SK_UI_POSITION_RELATIVE = 0, /**< In-flow (default). Establishes containing block. */
	SK_UI_POSITION_ABSOLUTE = 1, /**< Out of flex flow; offsets vs nearest positioned ancestor. */
} sk_ui_position_t;

/**
 * Per-node layout style (inline flex/box model). Independent of CSS cascade;
 * style resolution may fill this later. Defaults match a column flex container.
 */
typedef struct sk_ui_layout_style_t {
	sk_ui_flex_direction_t flex_direction;
	sk_ui_flex_wrap_t flex_wrap;
	sk_ui_justify_t justify_content;
	sk_ui_align_t align_items;
	sk_ui_align_t align_self;
	sk_ui_align_t align_content;

	f32 flex_grow;
	f32 flex_shrink;
	sk_ui_length_t flex_basis;

	sk_ui_length_t width;
	sk_ui_length_t height;
	sk_ui_length_t min_width;
	sk_ui_length_t min_height;
	sk_ui_length_t max_width;
	sk_ui_length_t max_height;

	sk_ui_edges_t padding;
	sk_ui_edges_t margin;
	sk_ui_edges_t border; /**< Border widths (layout only; paint later). */

	f32 row_gap;
	f32 column_gap;

	sk_ui_position_t position;
	sk_ui_length_t left;
	sk_ui_length_t top;
	sk_ui_length_t right;
	sk_ui_length_t bottom;
} sk_ui_layout_style_t;

/* ------------------------------------------------------------------ */
/*  Style system (class registry + resolve; not CSS)                  */
/* ------------------------------------------------------------------ */

/**
 * RGBA color in linear 0..1 components (paint space).
 * Not a CSS color model — plain floats for v1.
 */
typedef struct sk_ui_color_t {
	f32 r;
	f32 g;
	f32 b;
	f32 a;
} sk_ui_color_t;

/** @return RGBA color. */
SK_FINLINE sk_ui_color_t sk_ui_rgba(f32 r, f32 g, f32 b, f32 a) {
	sk_ui_color_t c;
	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

/**
 * Interaction / pseudo state flags on a node.
 * Style classes may define property overrides for each state; applied at
 * resolve time after the class base set (see state order on style_resolve).
 */
typedef enum sk_ui_state_flags_t {
	SK_UI_STATE_NONE = 0,
	SK_UI_STATE_HOVER = 1u << 0,
	SK_UI_STATE_ACTIVE = 1u << 1,
	SK_UI_STATE_FOCUSED = 1u << 2,
	SK_UI_STATE_DISABLED = 1u << 3,
} sk_ui_state_flags_t;

/**
 * Which fields of sk_ui_style_props_t are set (partial property set).
 * Unset fields do not participate in cascade merge. u64 bit constants
 * (not an enum — needs full 64-bit range portably).
 */
/* Layout → flexbox solver */
#define SK_UI_SP_FLEX_DIRECTION ((u64)1u << 0)
#define SK_UI_SP_FLEX_WRAP ((u64)1u << 1)
#define SK_UI_SP_JUSTIFY_CONTENT ((u64)1u << 2)
#define SK_UI_SP_ALIGN_ITEMS ((u64)1u << 3)
#define SK_UI_SP_ALIGN_SELF ((u64)1u << 4)
#define SK_UI_SP_ALIGN_CONTENT ((u64)1u << 5)
#define SK_UI_SP_FLEX_GROW ((u64)1u << 6)
#define SK_UI_SP_FLEX_SHRINK ((u64)1u << 7)
#define SK_UI_SP_FLEX_BASIS ((u64)1u << 8)
#define SK_UI_SP_WIDTH ((u64)1u << 9)
#define SK_UI_SP_HEIGHT ((u64)1u << 10)
#define SK_UI_SP_MIN_WIDTH ((u64)1u << 11)
#define SK_UI_SP_MIN_HEIGHT ((u64)1u << 12)
#define SK_UI_SP_MAX_WIDTH ((u64)1u << 13)
#define SK_UI_SP_MAX_HEIGHT ((u64)1u << 14)
#define SK_UI_SP_PADDING ((u64)1u << 15)
#define SK_UI_SP_MARGIN ((u64)1u << 16)
#define SK_UI_SP_BORDER_WIDTH ((u64)1u << 17)
#define SK_UI_SP_ROW_GAP ((u64)1u << 18)
#define SK_UI_SP_COLUMN_GAP ((u64)1u << 19)
#define SK_UI_SP_POSITION ((u64)1u << 20)
#define SK_UI_SP_LEFT ((u64)1u << 21)
#define SK_UI_SP_TOP ((u64)1u << 22)
#define SK_UI_SP_RIGHT ((u64)1u << 23)
#define SK_UI_SP_BOTTOM ((u64)1u << 24)

/* Box appearance */
#define SK_UI_SP_BACKGROUND_COLOR ((u64)1u << 25)
#define SK_UI_SP_BORDER_COLOR ((u64)1u << 26)
#define SK_UI_SP_CORNER_RADIUS ((u64)1u << 27)
#define SK_UI_SP_OPACITY ((u64)1u << 28)

/* Text (font_family / font_size / color inherit when unset) */
#define SK_UI_SP_FONT_FAMILY ((u64)1u << 29)
#define SK_UI_SP_FONT_SIZE ((u64)1u << 30)
#define SK_UI_SP_COLOR ((u64)1u << 31)

/** All layout property bits (feed the flexbox solver). */
#define SK_UI_SP_LAYOUT_MASK                                                                                                                                                     \
	((u64)(SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_WRAP | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_ALIGN_SELF | SK_UI_SP_ALIGN_CONTENT | SK_UI_SP_FLEX_GROW |  \
		   SK_UI_SP_FLEX_SHRINK | SK_UI_SP_FLEX_BASIS | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT | \
		   SK_UI_SP_PADDING | SK_UI_SP_MARGIN | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_ROW_GAP | SK_UI_SP_COLUMN_GAP | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP |              \
		   SK_UI_SP_RIGHT | SK_UI_SP_BOTTOM))

/** Paint-only appearance bits (no layout geometry). */
#define SK_UI_SP_PAINT_MASK ((u64)(SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_OPACITY | SK_UI_SP_COLOR))

/** Text properties that inherit down the tree when unset on the child. */
#define SK_UI_SP_INHERIT_MASK ((u64)(SK_UI_SP_FONT_FAMILY | SK_UI_SP_FONT_SIZE | SK_UI_SP_COLOR))

/**
 * Partial style property set. Only fields with bits set in @p mask are applied
 * during cascade merge. font_family, when set, is a non-owning pointer into
 * caller storage for temporary values; registry/inline storage own copies.
 */
typedef struct sk_ui_style_props_t {
	u64 mask;
	sk_ui_layout_style_t layout;
	sk_ui_color_t background_color;
	sk_ui_color_t border_color;
	f32 corner_radius;
	f32 opacity;
	const_chr_t font_family; /**< Non-owning when used as API input; owned inside registry/node. */
	f32 font_size;
	sk_ui_color_t color;
} sk_ui_style_props_t;

/**
 * Fully resolved style for one node after cascade + inheritance.
 * layout is also written into the node's layout_style for the flex solver.
 * font_family points at node-owned storage (stable until next resolve/destroy).
 */
typedef struct sk_ui_computed_style_t {
	sk_ui_layout_style_t layout;
	sk_ui_color_t background_color;
	sk_ui_color_t border_color;
	f32 corner_radius;
	f32 opacity;
	const_chr_t font_family;
	f32 font_size;
	sk_ui_color_t color;
} sk_ui_computed_style_t;

/** Measure constraint mode for intrinsic content (text, images). */
typedef enum sk_ui_measure_mode_t {
	SK_UI_MEASURE_UNDEFINED = 0, /**< No constraint on this axis. */
	SK_UI_MEASURE_EXACTLY = 1,	 /**< Size is fixed. */
	SK_UI_MEASURE_AT_MOST = 2,	 /**< Size must be <= available. */
} sk_ui_measure_mode_t;

/** Inputs to the measure callback. */
typedef struct sk_ui_measure_constraint_t {
	f32 width;
	f32 height;
	sk_ui_measure_mode_t width_mode;
	sk_ui_measure_mode_t height_mode;
} sk_ui_measure_constraint_t;

/** Opaque UI document / tree context (one root per context). */
typedef struct sk_ui_context_t sk_ui_context_t;

/**
 * Visitor callback for tree traversal.
 * @param ctx   Context that owns @p node.
 * @param node  Current node (alive).
 * @param depth Depth from the walk root (0 at the walk root).
 * @param user  Opaque user pointer passed to traverse_*.
 * @return 0 to continue, non-zero to abort the walk early.
 */
typedef i32 (*sk_ui_traverse_fn)(sk_ui_context_t* ctx, sk_ui_node_t node, u32 depth, void_ptr_t user);

/**
 * Intrinsic measure callback for text/images and other content the layout
 * engine does not interpret. Called when a node needs an auto size and no
 * definite style size is available.
 *
 * @param ctx          Context.
 * @param node         Node being measured.
 * @param constraints  Available width/height and modes.
 * @param out_size     Must be filled with desired size in logical units.
 * @param user         User pointer from set_measure_fn.
 */
typedef void (*sk_ui_measure_fn)(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_measure_constraint_t* constraints, sk_ui_size_t* out_size, void_ptr_t user);

/* ------------------------------------------------------------------ */
/*  Input / events (synthetic-first routing core)                     */
/* ------------------------------------------------------------------ */

/**
 * Routed event kinds delivered to node callbacks after hit-test / focus
 * resolution. Synthetic platform events (see sk_ui_input_event_t) are a
 * separate ingress shape; routing expands them into these.
 */
typedef enum sk_ui_event_type_t {
	SK_UI_EVENT_POINTER_ENTER = 0,
	SK_UI_EVENT_POINTER_LEAVE = 1,
	SK_UI_EVENT_POINTER_MOVE = 2,
	SK_UI_EVENT_POINTER_DOWN = 3,
	SK_UI_EVENT_POINTER_UP = 4,
	SK_UI_EVENT_CLICK = 5,
	SK_UI_EVENT_WHEEL = 6,
	SK_UI_EVENT_KEY_DOWN = 7,
	SK_UI_EVENT_KEY_UP = 8,
	SK_UI_EVENT_TEXT_INPUT = 9,
	SK_UI_EVENT_FOCUS_IN = 10,
	SK_UI_EVENT_FOCUS_OUT = 11,
} sk_ui_event_type_t;

/**
 * Propagation phase. Capture walks root → target parent, target is the hit
 * node, bubble walks target parent → root. Set event->consumed to stop.
 */
typedef enum sk_ui_event_phase_t {
	SK_UI_EVENT_PHASE_CAPTURE = 0,
	SK_UI_EVENT_PHASE_TARGET = 1,
	SK_UI_EVENT_PHASE_BUBBLE = 2,
} sk_ui_event_phase_t;

/** Pointer button indices for sk_ui_event_t / sk_ui_input_event_t. */
typedef enum sk_ui_pointer_button_t {
	SK_UI_POINTER_BUTTON_LEFT = 0,
	SK_UI_POINTER_BUTTON_RIGHT = 1,
	SK_UI_POINTER_BUTTON_MIDDLE = 2,
} sk_ui_pointer_button_t;

/** Modifier bit flags for keyboard / pointer events. */
typedef enum sk_ui_mod_flags_t {
	SK_UI_MOD_NONE = 0,
	SK_UI_MOD_SHIFT = 1u << 0,
	SK_UI_MOD_CTRL = 1u << 1,
	SK_UI_MOD_ALT = 1u << 2,
	SK_UI_MOD_SUPER = 1u << 3,
} sk_ui_mod_flags_t;

/**
 * Well-known key codes for focus traversal and common controls.
 * Other keys may use any positive host-defined code; TAB is required for
 * focus_advance via key events.
 */
typedef enum sk_ui_key_t {
	SK_UI_KEY_UNKNOWN = 0,
	SK_UI_KEY_TAB = 9,
	SK_UI_KEY_ENTER = 13,
	SK_UI_KEY_ESCAPE = 27,
	SK_UI_KEY_SPACE = 32,
	SK_UI_KEY_BACKSPACE = 8,
	SK_UI_KEY_DELETE = 127,
	SK_UI_KEY_LEFT = 1000,
	SK_UI_KEY_RIGHT = 1001,
	SK_UI_KEY_UP = 1002,
	SK_UI_KEY_DOWN = 1003,
	SK_UI_KEY_HOME = 1004,
	SK_UI_KEY_END = 1005,
} sk_ui_key_t;

/**
 * Whether a node participates as a pointer hit target.
 * NONE: skip this node as a target but still test its children (CSS-like).
 */
typedef enum sk_ui_pointer_events_t {
	SK_UI_POINTER_EVENTS_AUTO = 0,
	SK_UI_POINTER_EVENTS_NONE = 1,
} sk_ui_pointer_events_t;

/**
 * Platform / tester ingress event (raw input). Not the same as the routed
 * sk_ui_event_t delivered to callbacks — input_dispatch expands these into
 * enter/leave/move/down/up/click/key/text/focus sequences.
 *
 * This is the real host and UI-tester API (not a test-only path).
 */
typedef enum sk_ui_input_kind_t {
	SK_UI_INPUT_POINTER_MOVE = 0,
	SK_UI_INPUT_POINTER_BUTTON = 1,
	SK_UI_INPUT_WHEEL = 2,
	SK_UI_INPUT_KEY = 3,
	SK_UI_INPUT_TEXT = 4,
} sk_ui_input_kind_t;

typedef struct sk_ui_input_event_t {
	sk_ui_input_kind_t kind;
	f32 x;			  /**< Logical pointer x (layout space). */
	f32 y;			  /**< Logical pointer y (layout space). */
	f32 scroll_x;	  /**< Wheel: horizontal ticks/pixels. */
	f32 scroll_y;	  /**< Wheel: vertical ticks/pixels. */
	i32 button;		  /**< sk_ui_pointer_button_t for POINTER_BUTTON. */
	i32 down;		  /**< 1 = pressed, 0 = released (button or key). */
	i32 key;		  /**< sk_ui_key_t or host code for KEY. */
	u32 mods;		  /**< sk_ui_mod_flags_t bits. */
	i32 repeat;		  /**< Non-zero if key auto-repeat. */
	const_chr_t text; /**< UTF-8 for TEXT; non-owning, valid for the dispatch call. */
} sk_ui_input_event_t;

/**
 * Routed event seen by node callbacks. Mutate @p consumed to stop further
 * capture/target/bubble delivery for this event.
 */
typedef struct sk_ui_event_t {
	sk_ui_event_type_t type;
	sk_ui_event_phase_t phase;
	sk_ui_node_t target;  /**< Original hit / focus / capture target. */
	sk_ui_node_t current; /**< Node whose callback is running. */
	f32 x;
	f32 y;
	f32 scroll_x;
	f32 scroll_y;
	i32 button;
	i32 key;
	u32 mods;
	i32 down;
	i32 repeat;
	const_chr_t text; /**< Non-owning; valid only during the callback. */
	i32 consumed;	  /**< Set non-zero to stop propagation. */
} sk_ui_event_t;

/**
 * Event callback. May set event->consumed to stop remaining phases/nodes.
 * @param ctx   UI context.
 * @param node  Node that owns this callback (same as event->current).
 * @param event Mutable event (phase/current update between calls).
 * @param user  User pointer from sk_ui_node_callbacks_t.
 */
typedef void (*sk_ui_event_fn)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);

/**
 * Typed + generic callbacks on a node. NULL entries are skipped.
 * on_event is invoked for every event type after the typed handler (if any),
 * still subject to consumption from the typed handler.
 */
typedef struct sk_ui_node_callbacks_t {
	sk_ui_event_fn on_event;
	sk_ui_event_fn on_pointer_enter;
	sk_ui_event_fn on_pointer_leave;
	sk_ui_event_fn on_pointer_move;
	sk_ui_event_fn on_pointer_down;
	sk_ui_event_fn on_pointer_up;
	sk_ui_event_fn on_click;
	sk_ui_event_fn on_wheel;
	sk_ui_event_fn on_key_down;
	sk_ui_event_fn on_key_up;
	sk_ui_event_fn on_text_input;
	sk_ui_event_fn on_focus_in;
	sk_ui_event_fn on_focus_out;
	void_ptr_t user;
} sk_ui_node_callbacks_t;

/* ------------------------------------------------------------------ */
/*  Draw list (paint pass output; CPU only, backend-agnostic)         */
/* ------------------------------------------------------------------ */

/**
 * Vertex for UI mesh batches. Positions are in **physical pixels**
 * (framebuffer / swapchain space after content scale). UV is [0,1] in the
 * bound texture (unused for solid color draws; set to 0). Color is packed
 * RGBA8 with R in the low byte (matches common GPU upload layouts).
 */
typedef struct sk_ui_draw_vertex_t {
	f32 x;
	f32 y;
	f32 u;
	f32 v;
	u32 color; /**< Packed RGBA8, R in bits 0..7. */
} sk_ui_draw_vertex_t;

/** Pack linear 0..1 RGBA floats into a sk_ui_draw_vertex_t color. */
SK_FINLINE u32 sk_ui_pack_color(sk_ui_color_t c) {
	f32 r = c.r < 0.0f ? 0.0f : (c.r > 1.0f ? 1.0f : c.r);
	f32 g = c.g < 0.0f ? 0.0f : (c.g > 1.0f ? 1.0f : c.g);
	f32 b = c.b < 0.0f ? 0.0f : (c.b > 1.0f ? 1.0f : c.b);
	f32 a = c.a < 0.0f ? 0.0f : (c.a > 1.0f ? 1.0f : c.a);
	return ((u32)(r * 255.0f + 0.5f)) | (((u32)(g * 255.0f + 0.5f)) << 8) | (((u32)(b * 255.0f + 0.5f)) << 16) | (((u32)(a * 255.0f + 0.5f)) << 24);
}

/**
 * Draw-list command kinds. MESH references a contiguous index range in the
 * list's index buffer (triangle list). PUSH/POP_CLIP maintain a scissor stack
 * for overflow and scroll containers (tests assert nesting; backends may use
 * either the stack or the clip stored on MESH).
 */
typedef enum sk_ui_draw_cmd_kind_t {
	SK_UI_DRAW_CMD_MESH = 0,
	SK_UI_DRAW_CMD_PUSH_CLIP = 1,
	SK_UI_DRAW_CMD_POP_CLIP = 2,
} sk_ui_draw_cmd_kind_t;

/**
 * Texture binding kind for a MESH command. NONE = solid (vertex color only).
 * IMAGE = host texture (texture_id = host-defined id from the image node
 * property). MSDF = RGB multi-channel SDF atlas (texture_id = font id).
 */
typedef enum sk_ui_draw_texture_kind_t {
	SK_UI_DRAW_TEX_NONE = 0,
	SK_UI_DRAW_TEX_IMAGE = 1,
	SK_UI_DRAW_TEX_MSDF = 2,
} sk_ui_draw_texture_kind_t;

/**
 * One command in the paint stream. For MESH: index_offset/index_count select
 * triangles; texture_kind/texture_id select the bind; clip is the active
 * scissor in physical pixels (intersection of the clip stack). For PUSH_CLIP:
 * clip is the pushed rect (before intersection). POP_CLIP ignores other fields.
 */
typedef struct sk_ui_draw_cmd_t {
	sk_ui_draw_cmd_kind_t kind;
	sk_ui_draw_texture_kind_t texture_kind;
	u32 texture_id;
	u32 index_offset;
	u32 index_count;
	sk_ui_rect_t clip; /**< Physical-pixel scissor; full viewport when unconstrained. */
} sk_ui_draw_cmd_t;

/**
 * Backend-agnostic draw list: contiguous vertex/index arrays plus a command
 * stream. Arrays are owned by the UI context; valid until the next rebuild
 * paint or context destroy. Ready for direct GPU buffer upload (APX-134).
 */
typedef struct sk_ui_draw_list_t {
	const sk_ui_draw_vertex_t* vertices;
	u32 vertex_count;
	const u32* indices;
	u32 index_count;
	const sk_ui_draw_cmd_t* commands;
	u32 command_count;
	/**
	 * Monotonic rebuild counter. Unchanged across paint() calls that reuse
	 * the previous list (tree and content scale clean).
	 */
	u32 generation;
	/** Non-zero if the last paint() reused this list without rebuilding. */
	i32 reused;
} sk_ui_draw_list_t;

/**
 * Optional inputs for the paint walk (fonts for text glyphs).
 * All fields may be NULL / zero when unused. Text always renders through
 * the MSDF pipeline (msdf-atlas-c bake + median/smoothstep decode).
 */
typedef struct sk_ui_paint_params_t {
	sk_ui_font_system_t* font_system; /**< Required to emit text glyph quads. */
	sk_ui_font_t* font;				  /**< Default face for TEXT nodes. */
} sk_ui_paint_params_t;

/* ------------------------------------------------------------------ */
/*  Widget callbacks / clipboard (v1 widget set)                      */
/* ------------------------------------------------------------------ */

/** Clipboard get: fill @p buf with UTF-8, set @p out_len, return 0 on success. */
typedef i32 (*sk_ui_clipboard_get_fn)(void_ptr_t user, char* buf, u32 cap, u32* out_len);
/** Clipboard set: store UTF-8 @p text, return 0 on success. */
typedef i32 (*sk_ui_clipboard_set_fn)(void_ptr_t user, const_chr_t text);

/**
 * Mouse buttons that activate a button (ImGuiButtonFlags_MouseButton*).
 * Default is LEFT. InvisibleButton on the editor texture canvas ORs LEFT|MIDDLE.
 */
#define SK_UI_BUTTON_FLAG_MOUSE_LEFT (1u << 0)
#define SK_UI_BUTTON_FLAG_MOUSE_MIDDLE (1u << 1)
#define SK_UI_BUTTON_FLAG_MOUSE_RIGHT (1u << 2)

/** ArrowButton dir (ImGuiDir): Left / Right / Up / Down. */
#define SK_UI_ARROW_LEFT 0
#define SK_UI_ARROW_RIGHT 1
#define SK_UI_ARROW_UP 2
#define SK_UI_ARROW_DOWN 3

/**
 * InputText flags (ImGuiInputTextFlags + editor extra ShowError).
 * READ_ONLY stays focusable (blue focus rect). ENTER_RETURNS_TRUE makes
 * text_input_changed fire on Enter rather than every live edit.
 */
#define SK_UI_INPUT_TEXT_FLAG_NONE 0u
#define SK_UI_INPUT_TEXT_FLAG_READ_ONLY (1u << 0)
#define SK_UI_INPUT_TEXT_FLAG_PASSWORD (1u << 1)
#define SK_UI_INPUT_TEXT_FLAG_ENTER_RETURNS_TRUE (1u << 2)
#define SK_UI_INPUT_TEXT_FLAG_AUTO_SELECT_ALL (1u << 3)
#define SK_UI_INPUT_TEXT_FLAG_CHARS_DECIMAL (1u << 4)
#define SK_UI_INPUT_TEXT_FLAG_MULTILINE (1u << 5)
#define SK_UI_INPUT_TEXT_FLAG_SHOW_ERROR (1u << 6)

/** InputScalar data types the editor actually binds (FieldRenderers). */
typedef enum sk_ui_input_data_type_t {
	SK_UI_INPUT_DATA_S32 = 0,
	SK_UI_INPUT_DATA_U32 = 1,
	SK_UI_INPUT_DATA_U64 = 2,
	SK_UI_INPUT_DATA_F32 = 3,
	SK_UI_INPUT_DATA_F64 = 4,
} sk_ui_input_data_type_t;

/** Widget bool change (checkbox). */
typedef void (*sk_ui_widget_bool_fn)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user);
/** Widget float change (slider). */
typedef void (*sk_ui_widget_float_fn)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user);
/** Widget text change (InputText live edit / commit). */
typedef void (*sk_ui_widget_text_fn)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text, void_ptr_t user);
/**
 * Item-array interaction (activate a row, toggle expand).
 * @p host is the bound widget; @p item_id is the stable caller id.
 */
typedef void (*sk_ui_item_id_fn)(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, void_ptr_t user);

/** Default style class names (stable automation surface). */
#define SK_UI_CLASS_PANEL "ui-panel"
#define SK_UI_CLASS_VIEW "ui-view"
#define SK_UI_CLASS_LABEL "ui-label"
#define SK_UI_CLASS_TEXT "ui-text"
#define SK_UI_CLASS_TEXT_WRAPPED "ui-text-wrapped"
#define SK_UI_CLASS_SEPARATOR_TEXT "ui-separator-text"
#define SK_UI_CLASS_SEPARATOR "ui-separator"
#define SK_UI_CLASS_SPACING "ui-spacing"
#define SK_UI_CLASS_DUMMY "ui-dummy"
#define SK_UI_CLASS_BULLET_TEXT "ui-bullet-text"
#define SK_UI_CLASS_BUTTON "ui-button"
#define SK_UI_CLASS_BUTTON_SMALL "ui-button-small"
#define SK_UI_CLASS_BUTTON_INVISIBLE "ui-button-invisible"
#define SK_UI_CLASS_BUTTON_SELECTED "ui-button-selected"
#define SK_UI_CLASS_BUTTON_BORDERED "ui-button-bordered"
#define SK_UI_CLASS_BUTTON_ARROW "ui-button-arrow"
#define SK_UI_CLASS_SELECTABLE "ui-selectable"
#define SK_UI_CLASS_CHECKBOX "ui-checkbox"
#define SK_UI_CLASS_RADIO "ui-radio"
#define SK_UI_CLASS_TOGGLE "ui-toggle"
#define SK_UI_CLASS_SLIDER "ui-slider"
#define SK_UI_CLASS_DRAG "ui-drag"
#define SK_UI_CLASS_SLIDER_N "ui-slider-n"
#define SK_UI_CLASS_DRAG_N "ui-drag-n"
#define SK_UI_CLASS_RANGE_SLIDER "ui-range-slider"

/** Slider / Drag flags (ImGuiSliderFlags the editor actually sets). */
#define SK_UI_SLIDER_FLAG_NONE 0u
#define SK_UI_SLIDER_FLAG_ALWAYS_CLAMP (1u << 0)

/**
 * ImGuiSelectableFlags the editor actually sets (manifest §18).
 * Disabled, SpanAllColumns, SpanAvailWidth, AllowDoubleClick.
 */
#define SK_UI_SELECTABLE_FLAG_NONE 0u
#define SK_UI_SELECTABLE_FLAG_DISABLED (1u << 0)
#define SK_UI_SELECTABLE_FLAG_SPAN_ALL_COLUMNS (1u << 1)
#define SK_UI_SELECTABLE_FLAG_SPAN_AVAIL_WIDTH (1u << 2)
#define SK_UI_SELECTABLE_FLAG_ALLOW_DOUBLE_CLICK (1u << 3)

#define SK_UI_CLASS_PROGRESS "ui-progress"
#define SK_UI_CLASS_TEXT_INPUT "ui-text-input"
#define SK_UI_CLASS_TEXT_INPUT_ERROR "ui-text-input-error"
#define SK_UI_CLASS_TEXT_INPUT_SEARCH "ui-text-input-search"
#define SK_UI_CLASS_SCROLL_VIEW "ui-scroll-view"
#define SK_UI_CLASS_IMAGE "ui-image"
/** Menu surfaces (APX-234): bar, items, floating popups / dropdowns / context. */
#define SK_UI_CLASS_MENU_BAR "ui-menu-bar"
#define SK_UI_CLASS_MENU "ui-menu"
#define SK_UI_CLASS_MENU_ITEM "ui-menu-item"
#define SK_UI_CLASS_MENU_SEPARATOR "ui-menu-separator"
#define SK_UI_CLASS_MENU_POPUP "ui-menu-popup"
#define SK_UI_CLASS_DROPDOWN "ui-dropdown"
#define SK_UI_CLASS_CONTEXT_MENU "ui-context-menu"
#define SK_UI_CLASS_SUBMENU "ui-submenu"
/** Popup / modal chrome (APX-347; manifest §12). */
#define SK_UI_CLASS_POPUP_MENU "ui-popup-menu"
#define SK_UI_CLASS_MODAL "ui-modal"
#define SK_UI_CLASS_MODAL_DIM "ui-modal-dim"
#define SK_UI_CLASS_MODAL_DIALOG "ui-modal-dialog"
#define SK_UI_CLASS_MODAL_TITLE "ui-modal-title"
#define SK_UI_CLASS_MODAL_BODY "ui-modal-body"
#define SK_UI_CLASS_MODAL_BUTTONS "ui-modal-buttons"

/** ImGuiBeginPopupMenu SetNextWindowSize(ImVec2{300, 0}, Once). */
#define SK_UI_POPUP_MENU_WIDTH 300.0f
/** Save Content dialog width (fixed child+table body). */
#define SK_UI_MODAL_FIXED_WIDTH 420.0f
/** Save Content table-body height inside a fixed modal. */
#define SK_UI_MODAL_FIXED_BODY_HEIGHT 160.0f

/** BeginPopupModal window flags the editor actually sets. */
#define SK_UI_MODAL_FLAG_NONE 0u
#define SK_UI_MODAL_FLAG_ALWAYS_AUTO_RESIZE (1u << 0)
#define SK_UI_MODAL_FLAG_NO_SCROLLBAR (1u << 1)
/** Docking / editor window surfaces (APX-235): nodes, splitters, tabs, chrome. */
#define SK_UI_CLASS_DOCK_SPACE "ui-dock-space"
#define SK_UI_CLASS_DOCK_NODE "ui-dock-node"
#define SK_UI_CLASS_SPLITTER "ui-splitter"
#define SK_UI_CLASS_TAB_BAR "ui-tab-bar"
#define SK_UI_CLASS_TAB "ui-tab"
#define SK_UI_CLASS_TAB_BUTTON "ui-tab-button"
#define SK_UI_CLASS_TAB_CLOSE "ui-tab-close"
#define SK_UI_CLASS_TAB_BODY "ui-tab-body"

/**
 * BeginTabItem / TabItemButton flags. No tab-bar flags (editor never sets any).
 * SET_SELECTED is ImGuiTabItemFlags_SetSelected: programmatic select wins over
 * a user click on another tab while the flag is set.
 * BUTTON is TabItemButton: clickable chrome that is not a selectable page.
 */
#define SK_UI_TAB_ITEM_FLAG_NONE 0u
#define SK_UI_TAB_ITEM_FLAG_SET_SELECTED (1u << 0)
#define SK_UI_TAB_ITEM_FLAG_BUTTON (1u << 1)
#define SK_UI_CLASS_EDITOR_WINDOW "ui-editor-window"
#define SK_UI_CLASS_WINDOW_TITLE_BAR "ui-window-title-bar"
#define SK_UI_CLASS_WINDOW_CONTENT "ui-window-content"
#define SK_UI_CLASS_WINDOW_CLOSE "ui-window-close"
#define SK_UI_CLASS_FULLSCREEN "ui-fullscreen"
#define SK_UI_CLASS_CHILD "ui-child"
#define SK_UI_CLASS_CHILD_RESIZE "ui-child-resize"
#define SK_UI_CLASS_GROUP "ui-group"
#define SK_UI_CLASS_HORIZONTAL "ui-horizontal"
#define SK_UI_CLASS_VERTICAL "ui-vertical"
#define SK_UI_CLASS_SPRING "ui-spring"

/**
 * BeginChild flags (ImGuiChildFlags + HorizontalScrollbar window flag).
 * BORDER is the legacy `true` / ChildFlags_Borders pane chrome.
 * RESIZE_X is ResourceDebugger's user-resize left pane.
 * HORIZONTAL_SCROLLBAR is Console's ScrollingRegion.
 */
#define SK_UI_CHILD_FLAG_NONE 0u
#define SK_UI_CHILD_FLAG_BORDER (1u << 0)
#define SK_UI_CHILD_FLAG_RESIZE_X (1u << 1)
#define SK_UI_CHILD_FLAG_HORIZONTAL_SCROLLBAR (1u << 2)

/** Default Indent() step in logical px (ImGui-ish). */
#define SK_UI_INDENT_DEFAULT 16.0f

/** ImGui SameLine() default gap (ItemSpacing.x, editor theme scale 1). */
#define SK_UI_SAMELINE_DEFAULT_GAP 8.0f
/** ImGui Spacing() height (ItemSpacing.y, editor theme scale 1). */
#define SK_UI_SPACING_DEFAULT 8.0f

/** SetNextItemWidth(-1): fill leftover main-axis space. */
#define SK_UI_ITEM_WIDTH_FILL (-1.0f)
/** Item-array hosts (APX-338): tree / list / combo / table share one binding. */
#define SK_UI_CLASS_TREE "ui-tree"
#define SK_UI_CLASS_LIST "ui-list"
#define SK_UI_CLASS_COMBO "ui-combo"
#define SK_UI_CLASS_COMBO_ITEMS "ui-combo-items"
#define SK_UI_CLASS_LIST_BOX "ui-list-box"

/** Combo / ListBox row height (selectable min height, manifest §7 / §18). */
#define SK_UI_COMBO_ITEM_HEIGHT 22.0f
/** ImGui default popup / list height in items when the caller passes -1. */
#define SK_UI_COMBO_DEFAULT_HEIGHT_IN_ITEMS 8
/** BeginCombo flags. Editor always uses the default (0). */
#define SK_UI_COMBO_FLAG_NONE 0u
#define SK_UI_CLASS_TABLE "ui-table"
#define SK_UI_CLASS_TABLE_HEADER "ui-table-header"
#define SK_UI_CLASS_TABLE_ROW "ui-table-row"
#define SK_UI_CLASS_TABLE_CELL "ui-table-cell"
#define SK_UI_CLASS_TABLE_BODY "ui-table-body"
#define SK_UI_CLASS_TABLE_RESIZE "ui-table-resize"
#define SK_UI_CLASS_ITEM_ROW "ui-item-row"
#define SK_UI_CLASS_TREE_ARROW "ui-tree-arrow"
#define SK_UI_CLASS_COLLAPSING_HEADER "ui-collapsing-header"
#define SK_UI_CLASS_COLLAPSING_HEADER_BODY "ui-collapsing-header-body"
#define SK_UI_CLASS_COLLAPSING_HEADER_BUTTON "ui-collapsing-header-button"
/** Tooltip surface (APX-354; manifest §20). */
#define SK_UI_CLASS_TOOLTIP "ui-tooltip"
/** ImGui HoverDelayNormal / IsItemHovered(DelayNormal). Project Browser card. */
#define SK_UI_TOOLTIP_DELAY_NORMAL 0.40f
/** Cursor offset so the surface sits next to the pointer, not under it. */
#define SK_UI_TOOLTIP_OFFSET_X 12.0f
#define SK_UI_TOOLTIP_OFFSET_Y 16.0f

/**
 * Drag-drop payload API (APX-356; WIDGET_MANIFEST.md §17).
 * Sources / targets hang off tree rows, property fields, and custom rects.
 * The payload is transient (lives until mouse release); no §21 bind.
 */
#define SK_UI_CLASS_DRAG_DROP_PREVIEW "ui-drag-drop-preview"
#define SK_UI_CLASS_DRAG_DROP_TARGET "ui-drag-drop-target"
/** EditorCommon.hpp payload type strings. */
#define SK_UI_ASSET_PAYLOAD "sk-asset-payload"
#define SK_UI_ENTITY_PAYLOAD "sk-entity-payload"
/** ImGui DataType buffer (32 + NUL). */
#define SK_UI_PAYLOAD_TYPE_MAX 32
/** Preview tooltip / SetDragDropPayload preview text. */
#define SK_UI_DRAG_DROP_PREVIEW_MAX 96
/** BeginDragDropTargetCustom id (between-row / viewport). */
#define SK_UI_DRAG_DROP_ID_MAX 64
/** ImGui drag threshold in logical px before the payload becomes active. */
#define SK_UI_DRAG_DROP_THRESHOLD 6.0f

#define SK_UI_DRAG_DROP_FLAG_NONE 0u
/** Do not hold-to-open other tree nodes while this source is dragged. */
#define SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS (1u << 0)
/** Keep hover on other items while dragging (editor default). */
#define SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER (1u << 1)
/** Do not paint the default target highlight; caller peeks GetDragDropPayload. */
#define SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_DRAW_DEFAULT_RECT (1u << 2)
/** Hide the preview tooltip while this target is hovered. */
#define SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_PREVIEW_TOOLTIP (1u << 3)

/**
 * Active drag payload (ImGuiPayload analog). Owned by the context; valid
 * until mouse release (or the next begin). @p data is NULL when size is 0
 * (SK_ENTITY_PAYLOAD — selection is implicit).
 */
typedef struct sk_ui_payload_t {
	char type[SK_UI_PAYLOAD_TYPE_MAX + 1];
	const void* data;
	u32 size;
	sk_ui_node_t source;
	i32 preview;  /**< Non-zero while a matching target is hovered. */
	i32 delivery; /**< Non-zero on the release frame that hit a target. */
} sk_ui_payload_t;

/**
 * BeginTable flags the editor actually sets (ImGuiTableFlags analog).
 * Sizing bits are mutually exclusive; the last one that is set wins.
 */
#define SK_UI_TABLE_FLAG_NONE 0u
#define SK_UI_TABLE_FLAG_RESIZABLE (1u << 0)
#define SK_UI_TABLE_FLAG_ROW_BG (1u << 1)
#define SK_UI_TABLE_FLAG_BORDERS_OUTER (1u << 2)
#define SK_UI_TABLE_FLAG_BORDERS_INNER_H (1u << 3)
#define SK_UI_TABLE_FLAG_BORDERS_INNER_V (1u << 4)
#define SK_UI_TABLE_FLAG_BORDERS (SK_UI_TABLE_FLAG_BORDERS_OUTER | SK_UI_TABLE_FLAG_BORDERS_INNER_H | SK_UI_TABLE_FLAG_BORDERS_INNER_V)
#define SK_UI_TABLE_FLAG_NO_BORDERS_IN_BODY (1u << 5)
#define SK_UI_TABLE_FLAG_SCROLL_X (1u << 6)
#define SK_UI_TABLE_FLAG_SCROLL_Y (1u << 7)
#define SK_UI_TABLE_FLAG_SIZING_FIXED_FIT (1u << 8)
#define SK_UI_TABLE_FLAG_SIZING_FIXED_SAME (1u << 9)
#define SK_UI_TABLE_FLAG_SIZING_STRETCH_PROP (1u << 10)

/** TableSetupColumn flags (ImGuiTableColumnFlags analog). */
#define SK_UI_TABLE_COLUMN_FLAG_NONE 0u
#define SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH (1u << 0)
#define SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED (1u << 1)
#define SK_UI_TABLE_COLUMN_FLAG_NO_HIDE (1u << 2)
#define SK_UI_TABLE_COLUMN_FLAG_NO_RESIZE (1u << 3)
#define SK_UI_TABLE_COLUMN_FLAG_INDENT_ENABLE (1u << 4)
#define SK_UI_TABLE_COLUMN_FLAG_INDENT_DISABLE (1u << 5)

/** TableNextRow flags. HEADERS marks the header row. */
#define SK_UI_TABLE_ROW_FLAG_NONE 0u
#define SK_UI_TABLE_ROW_FLAG_HEADERS (1u << 0)

/** TableSetBgColor target. CellBg with column_n < 0 fills the whole row. */
typedef enum sk_ui_table_bg_target_t {
	SK_UI_TABLE_BG_NONE = 0,
	SK_UI_TABLE_BG_ROW_BG0 = 1,
	SK_UI_TABLE_BG_ROW_BG1 = 2,
	SK_UI_TABLE_BG_CELL = 3,
} sk_ui_table_bg_target_t;

/** Editor tables are 1–5 columns; keep a small hard cap. */
#define SK_UI_TABLE_MAX_COLUMNS 8
/** Default row height (logical px) when min_row_height is 0. */
#define SK_UI_TABLE_ROW_HEIGHT 22.0f
/** FixedFit fallback when a column has no init width. */
#define SK_UI_TABLE_DEFAULT_COL_WIDTH 80.0f

/** Per-level indent for tree rows (ImGui-ish; Entity Tree / Project Browser). */
#define SK_UI_TREE_INDENT 14.0f
/** Leading pad before the first indent step. */
#define SK_UI_TREE_ROW_PAD_X 4.0f

/**
 * TreeNodeEx / CollapsingHeader flags (editor wrappers OR these on).
 * ImGuiTreeNode always ORs OPEN_ON_ARROW | SPAN_AVAIL_WIDTH | SPAN_FULL_WIDTH |
 * FRAME_PADDING. ImGuiTreeLeaf additionally ORs LEAF | NO_TREE_PUSH_ON_OPEN.
 */
#define SK_UI_TREE_NODE_FLAG_NONE 0u
#define SK_UI_TREE_NODE_FLAG_SELECTED (1u << 0)
#define SK_UI_TREE_NODE_FLAG_FRAMED (1u << 1)
#define SK_UI_TREE_NODE_FLAG_ALLOW_OVERLAP (1u << 2)
#define SK_UI_TREE_NODE_FLAG_NO_TREE_PUSH_ON_OPEN (1u << 3)
#define SK_UI_TREE_NODE_FLAG_DEFAULT_OPEN (1u << 4)
#define SK_UI_TREE_NODE_FLAG_OPEN_ON_DOUBLE_CLICK (1u << 5)
#define SK_UI_TREE_NODE_FLAG_OPEN_ON_ARROW (1u << 6)
#define SK_UI_TREE_NODE_FLAG_LEAF (1u << 7)
#define SK_UI_TREE_NODE_FLAG_SPAN_AVAIL_WIDTH (1u << 8)
#define SK_UI_TREE_NODE_FLAG_SPAN_FULL_WIDTH (1u << 9)
#define SK_UI_TREE_NODE_FLAG_FRAME_PADDING (1u << 10)
/** ImGuiCollapsingHeaderProps: trailing '...' button on the header. */
#define SK_UI_TREE_NODE_FLAG_TRAILING_BUTTON (1u << 11)

/** ImGuiTreeNode default OR-mask. */
#define SK_UI_TREE_NODE_FLAGS_DEFAULT \
	(SK_UI_TREE_NODE_FLAG_OPEN_ON_ARROW | SK_UI_TREE_NODE_FLAG_SPAN_AVAIL_WIDTH | SK_UI_TREE_NODE_FLAG_SPAN_FULL_WIDTH | SK_UI_TREE_NODE_FLAG_FRAME_PADDING)

/** SetNextItemOpen / item_bind_set_open_cond. ONCE = first seed only. */
#define SK_UI_COND_NONE 0u
#define SK_UI_COND_ALWAYS 1u
#define SK_UI_COND_ONCE 2u

/* ------------------------------------------------------------------ */
/*  Retained item-array binding (APX-338)                             */
/* ------------------------------------------------------------------ */

/**
 * Caller-owned, mutable item array bound by pointer to a retained widget.
 *
 * This is the **only** collection-binding contract for hierarchical and
 * flat editor surfaces (TreeNode, ListBox, Combo items, Table rows,
 * content-item grids). Later widget tasks must use these types unchanged.
 *
 * ---------------------------------------------------------------------------
 * Ownership
 * ---------------------------------------------------------------------------
 * - The **caller** allocates and frees `sk_ui_item_array_t` and `items[]`.
 * - The **caller** owns every `label` string. A label pointer must stay
 *   valid until the next `item_bind_sync` after that item is removed or
 *   the pointer is replaced.
 * - The widget stores the **pointer** to the `sk_ui_item_array_t` (not a
 *   copy of the items). It never frees the array, the items, or the labels.
 * - Destroying the host widget frees only widget-owned row nodes and the
 *   id → state maps. The caller array is untouched.
 * - The `sk_ui_item_array_t` object itself must outlive the bind (do not
 *   pass a temporary). Replacing `array->items` / `array->count` in place
 *   is the supported grow/shrink path (realloc of the item buffer).
 *
 * ---------------------------------------------------------------------------
 * What invalidates
 * ---------------------------------------------------------------------------
 * - Freeing or moving the `sk_ui_item_array_t` while a widget still holds
 *   the pointer is invalid. Call `item_bind_set_array(ctx, host, NULL)`
 *   (or destroy the host) first.
 * - `items == NULL` with `count > 0` is treated as empty.
 * - `id == 0` (`SK_UI_ITEM_ID_NONE`) is skipped.
 * - Duplicate ids: the first occurrence in `items[]` wins; later copies
 *   are ignored for hierarchy and row identity.
 *
 * ---------------------------------------------------------------------------
 * What happens when the pointer's contents change between frames
 * ---------------------------------------------------------------------------
 * The host widget is **not** recreated. On each `item_bind_sync` (also
 * invoked automatically from `style_resolve` / `harness_step`):
 * - New ids get a row node.
 * - Removed ids have their row node destroyed.
 * - Surviving ids keep the same row handle (generation-stable).
 * - Labels, flags, parent, and sibling order are applied in place.
 * - TREE: rows whose ancestors are collapsed are not materialized.
 *
 * ---------------------------------------------------------------------------
 * Stable identity (selection / expansion survive mutation)
 * ---------------------------------------------------------------------------
 * `id` is the identity. Open and selected state live in widget-owned maps
 * keyed by `id`, seeded once from `SK_UI_ITEM_FLAG_OPEN` /
 * `SK_UI_ITEM_FLAG_SELECTED`. Insert, remove, reorder, and relabel do not
 * drop that state. Removing an item and adding it back with the same `id`
 * restores open/selected (e.g. a filter). `item_bind_clear_state` wipes maps.
 * After interaction the widget writes OPEN/SELECTED back onto the live
 * item flags so the caller can read them.
 *
 * Hierarchy: `parent_id == 0` is a root. If the parent item has
 * `child_count > 0` and `first_child + child_count` is in range, children
 * are that slice (in slice order). Otherwise children are every item whose
 * `parent_id` matches, in array order. Cycles stop at depth 64.
 *
 * Expand-arrow vs row-activate: click the arrow node (`{host}/a{id}`) to
 * toggle open without changing selection (`item_bind_last_was_arrow` is
 * non-zero). Click the row (`{host}/i{id}`) to select (exclusive) and
 * activate (`last_was_arrow` is 0).
 */

/** Sentinel index: no parent / unused child range. */
#define SK_UI_ITEM_NONE 0xFFFFFFFFu
/** Invalid item id. Live items must use a non-zero id. */
#define SK_UI_ITEM_ID_NONE 0ull

/** Bits for sk_ui_item_t::flags. */
typedef enum sk_ui_item_flag_t {
	SK_UI_ITEM_FLAG_NONE = 0,
	SK_UI_ITEM_FLAG_LEAF = 1u << 0,		/**< No expand arrow (tree). */
	SK_UI_ITEM_FLAG_SELECTED = 1u << 1, /**< Selected row. */
	SK_UI_ITEM_FLAG_OPEN = 1u << 2,		/**< Expanded (tree). */
	SK_UI_ITEM_FLAG_DISABLED = 1u << 3, /**< Ignore activate / toggle. */
	SK_UI_ITEM_FLAG_ERROR = 1u << 4,	/**< Error tint (label colour). */
} sk_ui_item_flag_t;

/**
 * How a bound array is presented. Same items / ids / flags for all kinds;
 * only visibility and expand behaviour differ.
 */
typedef enum sk_ui_item_bind_kind_t {
	SK_UI_ITEM_BIND_TREE = 0,  /**< Hierarchical; expand/collapse by id. */
	SK_UI_ITEM_BIND_LIST = 1,  /**< Flat; every live item is a row. */
	SK_UI_ITEM_BIND_COMBO = 2, /**< Flat item list for a combo popup. */
	SK_UI_ITEM_BIND_TABLE = 3, /**< Flat table rows. */
} sk_ui_item_bind_kind_t;

/**
 * One row / node in a caller-owned item array.
 * Layout is 40 bytes on LP64/LLP64 (no implicit hole).
 */
typedef struct sk_ui_item_t {
	u64 id;			   /**< Stable identity. Unique among live items; not 0. */
	u64 parent_id;	   /**< 0 = root. Must be another item's id or 0. */
	u32 first_child;   /**< Optional packed children; SK_UI_ITEM_NONE if unused. */
	u32 child_count;   /**< 0 = derive children by scanning parent_id. */
	u32 flags;		   /**< sk_ui_item_flag_t bits. */
	u32 icon;		   /**< Optional host icon / texture id; 0 = none. */
	const_chr_t label; /**< Caller-owned UTF-8; NULL treated as "". */
} sk_ui_item_t;

/**
 * Caller-owned array header. The widget stores this pointer and rereads
 * `items` / `count` / `revision` on every sync.
 */
typedef struct sk_ui_item_array_t {
	sk_ui_item_t* items; /**< Caller-owned storage; NULL iff count == 0. */
	u32 count;			 /**< Live item count. */
	u32 revision;		 /**< Optional; bump on mutation (not required). */
} sk_ui_item_array_t;

/** Fill an item (unused child range, icon 0). */
SK_FINLINE void sk_ui_item_set(sk_ui_item_t* item, u64 id, u64 parent_id, const_chr_t label, u32 flags) {
	item->id = id;
	item->parent_id = parent_id;
	item->first_child = SK_UI_ITEM_NONE;
	item->child_count = 0u;
	item->flags = flags;
	item->icon = 0u;
	item->label = label;
}

/* ------------------------------------------------------------------ */
/*  Headless harness (automation / UI tester foundation)              */
/* ------------------------------------------------------------------ */

/**
 * Opaque headless harness: owns a UI context, a deterministic clock, and
 * optionally an offscreen RGBA soft-render buffer for golden comparison.
 * No window, GPU, or wall-clock dependency — tests advance frames with
 * harness_step(delta).
 */
typedef struct sk_ui_harness_t sk_ui_harness_t;

/**
 * Creation parameters for sk_ui_api_t::harness_create.
 * width/height are logical root size; content_scale maps to physical pixels
 * for layout_apply_scale and soft-render buffer size.
 */
typedef struct sk_ui_harness_desc_t {
	const sk_allocator_t* allocator; /**< Optional; NULL = process default. */
	f32 width;						 /**< Logical width (default 800 when <= 0). */
	f32 height;						 /**< Logical height (default 600 when <= 0). */
	f32 content_scale;				 /**< HiDPI scale (default 1 when <= 0). */
	i32 soft_render;				 /**< Non-zero: allocate RGBA8 offscreen buffer. */
} sk_ui_harness_desc_t;

/* ------------------------------------------------------------------ */
/*  Code-driven test engine (item registry + frame control)           */
/* ------------------------------------------------------------------ */

/**
 * Opaque headless test engine (APX-259/260). Owns a harness, a per-frame item
 * registry (stable test ids / id paths → rect + interaction state),
 * deterministic frame stepping, and synthetic input that always enters via
 * input_dispatch (same path as hosts). No OS event loop.
 */
typedef struct sk_ui_test_engine_t sk_ui_test_engine_t;

/**
 * Creation parameters for sk_ui_api_t::test_engine_create.
 * Mirrors sk_ui_harness_desc_t (the engine wraps a harness).
 */
typedef struct sk_ui_test_engine_desc_t {
	const sk_allocator_t* allocator; /**< Optional; NULL = process default. */
	f32 width;						 /**< Logical width (default 800 when <= 0). */
	f32 height;						 /**< Logical height (default 600 when <= 0). */
	f32 content_scale;				 /**< HiDPI scale (default 1 when <= 0). */
	i32 soft_render;				 /**< Non-zero: harness soft-render buffer. */
} sk_ui_test_engine_desc_t;

/**
 * Snapshot of one registered UI item after the last successful engine step.
 * String pointers and the struct itself are owned by the engine and remain
 * valid only until the next step / destroy.
 */
typedef struct sk_ui_test_item_t {
	sk_ui_node_t node;	 /**< Live handle at registration time. */
	const_chr_t id;		 /**< Node test id (never NULL when registered). */
	const_chr_t id_path; /**< Slash-separated ancestor+self ids (e.g. "panel/btn"). */
	sk_ui_rect_t rect;	 /**< Absolute border box (logical units after layout). */
	u32 state_flags;	 /**< SK_UI_STATE_* bits at last step. */
	i32 hovered;		 /**< Non-zero if SK_UI_STATE_HOVER set. */
	i32 active;			 /**< Non-zero if SK_UI_STATE_ACTIVE set. */
	i32 focused;		 /**< Non-zero if SK_UI_STATE_FOCUSED set. */
	i32 disabled;		 /**< Non-zero if SK_UI_STATE_DISABLED set. */
	i32 visible;		 /**< Non-zero if node_is_visible at last step. */
} sk_ui_test_item_t;

/**
 * Predicate for test_engine_run_until: return non-zero when the wait condition
 * holds (stop stepping). Called before the first step and after each step.
 */
typedef i32 (*sk_ui_test_predicate_fn)(sk_ui_test_engine_t* engine, void_ptr_t user);

/** Success. */
#define SK_UI_TEST_OK 0
/** Frame budget exhausted without predicate becoming true. */
#define SK_UI_TEST_ERR_TIMEOUT 1
/** harness_step / pipeline failed during yield or run_until. */
#define SK_UI_TEST_ERR_STEP 2
/** test_id / path did not resolve to a live node. */
#define SK_UI_TEST_ERR_NOT_FOUND 3
/** input_dispatch or focus failed during synthetic injection. */
#define SK_UI_TEST_ERR_INPUT 4

/* ------------------------------------------------------------------ */
/*  GPU renderer (draw list → render_device)                           */
/* ------------------------------------------------------------------ */

/**
 * Creation parameters for the UI GPU renderer.
 * @p device_api / @p device / @p dxc / @p render_pass must be valid.
 * @p render_pass must match the pass used when encoding draws (swapchain or
 * offscreen). Recreate the renderer (or call renderer_set_render_pass) when
 * the target format/pass changes.
 */
typedef struct sk_ui_renderer_desc_t {
	const sk_render_device_api_t* device_api; /**< Non-NULL engine RHI table. */
	sk_render_device_t device;				  /**< Live device (adapter selected). */
	const sk_dxc_compiler_api_t* dxc;		  /**< Non-NULL; used to compile embedded HLSL. */
	sk_render_pass_t render_pass;			  /**< Compatible with the encode target. */
	const sk_allocator_t* allocator;		  /**< Optional; NULL = process default. */
} sk_ui_renderer_desc_t;

/**
 * Host image bindings for SK_UI_DRAW_TEX_IMAGE mesh commands.
 * texture_id on the draw command indexes this array (out of range → white).
 */
typedef struct sk_ui_renderer_images_t {
	const sk_texture_view_t* views; /**< May be NULL when count is 0. */
	u32 count;
} sk_ui_renderer_images_t;

/**
 * Parameters for renderer_prepare (transfer work outside a render pass).
 * Uploads dynamic vertex/index buffers and dirty font atlas pages.
 */
typedef struct sk_ui_renderer_prepare_info_t {
	sk_command_buffer_t cmd;			/**< Recording command buffer (not in a pass). */
	const sk_ui_draw_list_t* draw_list; /**< From paint/get_draw_list; may be empty. */
	sk_ui_font_system_t* font_system;	/**< Optional; required for FONT / MSDF cmds. */
	sk_ui_font_t* font;					/**< Optional default face (MSDF atlas fallback). */
} sk_ui_renderer_prepare_info_t;

/**
 * Parameters for renderer_encode (draw work inside a render pass).
 * Host must have begun a render pass compatible with the create-time pass.
 * Sets full-target viewport and per-mesh scissor; issues batched draw_indexed.
 */
typedef struct sk_ui_renderer_encode_info_t {
	sk_command_buffer_t cmd;			/**< Inside a compatible render pass. */
	const sk_ui_draw_list_t* draw_list; /**< Same list prepared for this frame. */
	u32 target_width;					/**< Framebuffer width in pixels. */
	u32 target_height;					/**< Framebuffer height in pixels. */
	sk_ui_renderer_images_t images;		/**< Optional host image views. */
} sk_ui_renderer_encode_info_t;

/* ------------------------------------------------------------------ */
/*  Headless capture (offscreen render target + CPU readback)          */
/* ------------------------------------------------------------------ */

/**
 * Simple CPU image: row-major RGBA8 with R in the low byte (byte order
 * matches sk_ui_draw_vertex_t::color and the GPU upload layout).
 * The buffer is always tightly packed: row stride = width * channels.
 * Pixels are straight (non-premultiplied) alpha as authored by the UI.
 */
typedef struct sk_ui_cpu_image_t {
	u32 width;
	u32 height;
	u32 channels; /**< Always 4 for UI captures (RGBA8). */
	u8* pixels;	  /**< Tightly packed RGBA8; stride = width * channels. */
} sk_ui_cpu_image_t;

/* ------------------------------------------------------------------ */
/*  Golden image comparison (harness / capture tests)                   */
/* ------------------------------------------------------------------ */

/**
 * Return codes from cpu_image_compare / cpu_image_compare_golden.
 * OK is 0 so callers can use `if (rc != 0) fail`. Non-zero values are
 * stable categories for assertion messages.
 */
#define SK_UI_IMAGE_COMPARE_OK 0			 /**< Within tolerance (or golden blessed). */
#define SK_UI_IMAGE_COMPARE_MISMATCH 1		 /**< Pixel differences exceed thresholds. */
#define SK_UI_IMAGE_COMPARE_SIZE_MISMATCH 2	 /**< Width/height differ; no pixel scan. */
#define SK_UI_IMAGE_COMPARE_MISSING_GOLDEN 3 /**< Golden PNG missing or unreadable. */
#define SK_UI_IMAGE_COMPARE_ERROR (-1)		 /**< Hard failure (I/O, OOM, bad args). */

/**
 * Comparison thresholds and options. Zero-init is strict equality:
 * channel_tolerance=0, max_diff_fraction=0, update_golden=0.
 * Blessing is also enabled when SK_UI_REGEN_GOLDENS is set and not "0"
 * (env never becomes the silent default — must be set deliberately).
 */
typedef struct sk_ui_image_compare_params_t {
	u32 channel_tolerance; /**< Max abs per-channel delta still treated as match. */
	f32 max_diff_fraction; /**< Max fraction of pixels allowed beyond tolerance [0,1]. */
	i32 update_golden;	   /**< Non-zero: overwrite golden with actual (bless). Default 0. */
	const_chr_t name;	   /**< Base name for failure artifacts under the test-artifact root. */
} sk_ui_image_compare_params_t;

/**
 * Actionable summary filled by compare APIs (ASCII-printable fields only).
 * Bounding box is inclusive pixel coords of the differing region when
 * differ_count > 0; all zeros otherwise.
 */
typedef struct sk_ui_image_compare_stats_t {
	u32 actual_width;
	u32 actual_height;
	u32 expected_width;
	u32 expected_height;
	u32 pixel_count;	   /**< actual_width * actual_height when sizes match; else 0. */
	u32 differ_count;	   /**< Pixels exceeding channel_tolerance. */
	u32 max_channel_delta; /**< Max abs channel delta across all compared channels. */
	u32 bbox_min_x;
	u32 bbox_min_y;
	u32 bbox_max_x;
	u32 bbox_max_y;
	i32 size_mismatch; /**< Non-zero when dimensions differ. */
	i32 updated;	   /**< Non-zero when golden was rewritten (bless mode). */
	/* Null-terminated paths of failure artifacts when written; empty otherwise. */
	char actual_path[SK_FS_PATH_MAX];
	char expected_path[SK_FS_PATH_MAX];
	char diff_path[SK_FS_PATH_MAX];
} sk_ui_image_compare_stats_t;

/**
 * Opaque headless GPU capture: owns an offscreen RGBA8 color target (a
 * device-owned texture — no swapchain, window, or surface involved), the
 * standard UI GPU renderer bound to it, a host-visible readback buffer, and
 * the queue / command buffers / fences needed to draw and copy back to CPU.
 */
typedef struct sk_ui_capture_t sk_ui_capture_t;

/**
 * Creation parameters for sk_ui_api_t::capture_create.
 * @p device_api / @p device / @p dxc must be valid; the UI renderer is
 * created internally against the offscreen pass (see renderer_create).
 * @p width / @p height are the fixed viewport size in pixels (>= 1).
 * @p clear_color is the explicit clear color applied on every frame before
 * the draw list is encoded (convert 0..1 floats to RGBA8_UNORM).
 */
typedef struct sk_ui_capture_desc_t {
	const sk_render_device_api_t* device_api; /**< Non-NULL engine RHI table. */
	sk_render_device_t device;				  /**< Live device (adapter selected). */
	const sk_dxc_compiler_api_t* dxc;		  /**< Non-NULL; used to compile embedded HLSL. */
	u32 width;								  /**< Viewport width in pixels. */
	u32 height;								  /**< Viewport height in pixels. */
	sk_clear_values_t clear_color;			  /**< Explicit clear color per frame. */
	const sk_allocator_t* allocator;		  /**< Optional; NULL = process default. */
	const_chr_t debug_name;					  /**< Optional; prefix for resource debug names. */
} sk_ui_capture_desc_t;

/**
 * Per-frame parameters for sk_ui_api_t::capture_frame.
 * Same content as renderer_prepare / renderer_encode inputs; the capture
 * drives both internally (prepare on an upload command buffer, encode inside
 * the offscreen render pass).
 */
typedef struct sk_ui_capture_frame_info_t {
	const sk_ui_draw_list_t* draw_list; /**< From paint/get_draw_list; may be empty. */
	sk_ui_font_system_t* font_system;	/**< Optional; required for FONT / MSDF cmds. */
	sk_ui_font_t* font;					/**< Optional default face (MSDF atlas fallback). */
	sk_ui_renderer_images_t images;		/**< Optional host image views. */
} sk_ui_capture_frame_info_t;

/* ------------------------------------------------------------------ */
/*  Structural image assertions (beyond pixel sampling; APX-230)       */
/* ------------------------------------------------------------------ */

/** Return codes for the structural image assertion APIs. */
#define SK_UI_IMAGE_ASSERT_OK 0		  /**< Assertion passed. */
#define SK_UI_IMAGE_ASSERT_FAIL 1	  /**< Assertion failed; stats + log carry the measured values. */
#define SK_UI_IMAGE_ASSERT_ERROR (-1) /**< Bad arguments / unreadable image. */

/** Max dominant colors a histogram reports / an assertion expects. */
#define SK_UI_COLOR_HIST_MAX_ENTRIES 16

/** Half-open pixel region [x0,x1) x [y0,y1), clamped to the image bounds. */
typedef struct sk_ui_region_t {
	u32 x0;
	u32 y0;
	u32 x1;
	u32 y1;
} sk_ui_region_t;

/**
 * Color match predicate: per-channel absolute tolerance. A pixel matches
 * when |pixel.ch - color.ch| <= tolerance for R, G, B and (when
 * include_alpha != 0) A. Images with 3 channels treat alpha as 255.
 */
typedef struct sk_ui_color_match_t {
	u8 r;
	u8 g;
	u8 b;
	u8 a;
	u8 tolerance;	   /**< Max abs per-channel delta still considered a match. */
	i32 include_alpha; /**< Non-zero: alpha participates in the match. */
} sk_ui_color_match_t;

/** Tight bounding box of matching pixels (inclusive pixel coords). */
typedef struct sk_ui_bbox_t {
	u32 min_x;
	u32 min_y;
	u32 max_x;
	u32 max_y;
	u32 pixel_count; /**< Matching pixels inside the box (<= box area). */
	i32 found;		 /**< Non-zero when at least one pixel matched. */
} sk_ui_bbox_t;

/** Measured result of cpu_image_assert_solid. */
typedef struct sk_ui_solid_stats_t {
	u32 region_pixels;		  /**< Effective region area (clamped to image). */
	u32 nonmatching;		  /**< Pixels outside tolerance. */
	u32 max_channel_delta;	  /**< Largest channel delta vs the expected color. */
	sk_ui_bbox_t bad_bbox;	  /**< Tight bbox of nonmatching pixels (found=0 when clean). */
	f32 nonmatching_fraction; /**< nonmatching / region_pixels (0..1). */
} sk_ui_solid_stats_t;

/** Measured result of cpu_image_assert_coverage. */
typedef struct sk_ui_coverage_stats_t {
	u32 region_pixels;
	u32 matching; /**< Pixels matching the color spec. */
	f32 coverage; /**< matching / region_pixels (0..1). */
} sk_ui_coverage_stats_t;

/** Expected geometry for cpu_image_assert_bbox. */
typedef struct sk_ui_bbox_expected_t {
	u32 min_x;
	u32 min_y;
	u32 max_x;
	u32 max_y;				/**< Inclusive expected bbox. */
	u32 position_tolerance; /**< Allowed shift of the min corner per axis (px). */
	u32 size_tolerance;		/**< Allowed width/height delta (px). */
	u32 min_pixels;			/**< Required matching pixel count (0 = skip; 1 typical). */
	i32 expect_empty;		/**< Non-zero: assert NO pixel matches (geometry ignored). */
} sk_ui_bbox_expected_t;

/** Measured result of cpu_image_assert_bbox. */
typedef struct sk_ui_bbox_assert_stats_t {
	sk_ui_bbox_t actual; /**< Measured bbox of matching pixels. */
	u32 expected_min_x;
	u32 expected_min_y;
	u32 expected_max_x;
	u32 expected_max_y;
	u32 expected_min_pixels;
	i32 expect_empty;
	i32 empty_mismatch;	   /**< Expected empty but pixels were found. */
	i32 missing;		   /**< Expected pixels but none were found. */
	i32 min_pixels_fail;   /**< Found, but below expected->min_pixels. */
	i32 position_mismatch; /**< Min-corner shift beyond position_tolerance. */
	i32 size_mismatch;	   /**< Width/height delta beyond size_tolerance. */
	u32 delta_x;		   /**< |actual.min_x - expected.min_x|. */
	u32 delta_y;		   /**< |actual.min_y - expected.min_y|. */
	u32 delta_w;		   /**< |actual width - expected width|. */
	u32 delta_h;		   /**< |actual height - expected height|. */
} sk_ui_bbox_assert_stats_t;

/** One dominant color with its measured share of the region. */
typedef struct sk_ui_color_hist_entry_t {
	u8 r;
	u8 g;
	u8 b;
	u8 a;
	u32 count;	  /**< Pixels of this (merged) color in the region. */
	f32 fraction; /**< count / total_pixels (0..1). */
} sk_ui_color_hist_entry_t;

/** Measured histogram: dominant colors sorted by count descending. */
typedef struct sk_ui_color_histogram_t {
	u32 entry_count;
	sk_ui_color_hist_entry_t entries[SK_UI_COLOR_HIST_MAX_ENTRIES];
	u32 total_pixels; /**< Pixels the histogram spans (clamped region area). */
} sk_ui_color_histogram_t;

/** Expected dominant color with its allowed fraction range (inclusive). */
typedef struct sk_ui_hist_expectation_t {
	sk_ui_color_match_t color;
	f32 min_fraction;
	f32 max_fraction;
} sk_ui_hist_expectation_t;

/** Tuning for cpu_image_assert_histogram (NULL = defaults). */
typedef struct sk_ui_hist_assert_params_t {
	u32 max_entries;			 /**< Dominant colors to keep (default 16, capped at 16). */
	u8 merge_tolerance;			 /**< Per-channel tolerance when merging measured colors (default 16). */
	f32 max_unexpected_fraction; /**< Max total share of colors matching no expectation (default 0.05). */
} sk_ui_hist_assert_params_t;

/** Measured result of cpu_image_assert_histogram. */
typedef struct sk_ui_hist_assert_stats_t {
	sk_ui_color_histogram_t actual; /**< Measured dominant colors (top-K). */
	u32 expected_count;
	u32 missing_count;		 /**< Expected colors with no measured match. */
	u32 out_of_range_count;	 /**< Expected colors present but fraction outside [min,max]. */
	u32 unexpected_count;	 /**< Measured colors matching no expectation. */
	f32 unexpected_fraction; /**< Total share of unexpected measured colors. */
	/** Per-expected measured share/count (0 when the color was absent). */
	f32 measured_fraction[SK_UI_COLOR_HIST_MAX_ENTRIES];
	u32 measured_count[SK_UI_COLOR_HIST_MAX_ENTRIES];
} sk_ui_hist_assert_stats_t;

/* -------------------------------------------------------------------------- */
/*  Docking (APX-287 spec; model + headless layout in APX-288)                */
/* -------------------------------------------------------------------------- */

/**
 * Stable dock-model handle (not a UI node). Same generation-handle rules as
 * sk_ui_node_t: index 0 is invalid; recycled slots bump generation.
 */
typedef struct sk_ui_dock_node_t {
	u32 index;
	u32 generation;
} sk_ui_dock_node_t;

#define SK_UI_DOCK_NODE_INVALID ((sk_ui_dock_node_t){0u, 0u})

SK_FINLINE i32 sk_ui_dock_node_is_valid(sk_ui_dock_node_t node) {
	return node.index != 0u;
}

SK_FINLINE i32 sk_ui_dock_node_eq(sk_ui_dock_node_t a, sk_ui_dock_node_t b) {
	return (a.index == b.index) && (a.generation == b.generation);
}

/**
 * Split orientation. Numeric values match widget_dock_node "orientation"
 * and widget_splitter "axis" (0 = row / vertical bar, 1 = column / horizontal bar).
 */
typedef enum sk_ui_dock_split_t {
	SK_UI_DOCK_SPLIT_HORIZONTAL = 0, /**< Left | right. */
	SK_UI_DOCK_SPLIT_VERTICAL = 1,	 /**< Top / bottom. */
} sk_ui_dock_split_t;

/**
 * Dock insertion direction (ImGuiDir analogue). CENTER/NONE/TAB all mean
 * "add as a tab on the target leaf".
 */
typedef enum sk_ui_dock_dir_t {
	SK_UI_DOCK_DIR_NONE = 0,
	SK_UI_DOCK_DIR_LEFT = 1,
	SK_UI_DOCK_DIR_RIGHT = 2,
	SK_UI_DOCK_DIR_UP = 3,
	SK_UI_DOCK_DIR_DOWN = 4,
	SK_UI_DOCK_DIR_CENTER = 5,
	SK_UI_DOCK_DIR_TAB = 5, /**< Alias of CENTER. */
} sk_ui_dock_dir_t;

/** dockspace_begin / dockspace_create flags. */
#define SK_UI_DOCKSPACE_NONE 0u
#define SK_UI_DOCKSPACE_KEEP_CENTRAL (1u << 0)	  /**< Empty central leaf stays. */
#define SK_UI_DOCKSPACE_NO_SPLIT (1u << 1)		  /**< Runtime split/dock-to-edge disabled. */
#define SK_UI_DOCKSPACE_NO_UNDOCK (1u << 2)		  /**< Tabs cannot tear off. */
#define SK_UI_DOCKSPACE_PASSTHRU_CENTER (1u << 3) /**< Empty central: pointer-events none. */
#define SK_UI_DOCKSPACE_AUTO_APPLY (1u << 4)	  /**< layout() applies when dirty. */

/** Per-node flags (leaf or split). */
#define SK_UI_DOCK_NODE_NONE 0u
#define SK_UI_DOCK_NODE_CENTRAL (1u << 0)
#define SK_UI_DOCK_NODE_NO_TAB_BAR (1u << 1)
#define SK_UI_DOCK_NODE_NO_SPLIT (1u << 2)
#define SK_UI_DOCK_NODE_NO_UNDOCK (1u << 3)

/** Max named dockspaces per context. */
#define SK_UI_DOCKSPACE_MAX 8u
/** Max tabs per leaf (editor workspaces stay well under this). */
#define SK_UI_DOCK_LEAF_TABS_MAX 32u
/** Max pending window-id binds per dockspace (create-then-dock / load-then-create). */
#define SK_UI_DOCK_PENDING_MAX 64u
/** Max host window ids registered for restore mismatch (defaults + drop). */
#define SK_UI_DOCK_WINDOW_REG_MAX 64u

/** Splitter band thickness along the split main axis (logical points). */
#define SK_UI_DOCK_SPLITTER_PT 6.0f
/** Minimum leftover allocated to each child when the parent span is large enough. */
#define SK_UI_DOCK_NODE_MIN_PT 40.0f
/**
 * On-disk dock layout document version (integer at the JSON root).
 * There is no major.minor. Version policy (see docs/ui-dock-layout-format.md):
 * - version == SK_UI_DOCK_LAYOUT_VERSION: accept
 * - version > SK_UI_DOCK_LAYOUT_VERSION (unknown/newer): reject, log the
 *   encountered version, host falls back to the default layout
 * - version < SK_UI_DOCK_LAYOUT_VERSION (older): reject, no migration; log
 *   the encountered version and fall back to the default layout
 */
#define SK_UI_DOCK_LAYOUT_VERSION 1

/** Max floating windows stored on one dock-layout document. */
#define SK_UI_DOCK_LAYOUT_FLOAT_MAX 32u

/**
 * Node kind in the persisted binary dock tree.
 * Numeric values match the live model (0 leaf, 1 split).
 */
typedef enum sk_ui_dock_layout_kind_t {
	SK_UI_DOCK_LAYOUT_KIND_LEAF = 0,
	SK_UI_DOCK_LAYOUT_KIND_SPLIT = 1,
} sk_ui_dock_layout_kind_t;

/**
 * One floating window on a layout document: stable window identifier plus
 * logical-point rect (x/y/w/h) and paint/hit z.
 */
typedef struct sk_ui_dock_layout_float_t {
	const_chr_t window_id; /**< Host window id (node_set_id / factory id). */
	f32 x;
	f32 y;
	f32 w;
	f32 h;
	i32 z;
	u8 _pad0[4]; /**< Align struct to 8 bytes. */
} sk_ui_dock_layout_float_t;

/**
 * One node in the persisted binary dock tree.
 *
 * Split: `axis` is the orientation (`sk_ui_dock_split_t`), `ratio` is the
 * first-child fraction along the main axis, `child_a` / `child_b` are the
 * two children (left/top and right/bottom).
 *
 * Leaf: `tabs[0..tab_count)` are window identifiers in tab order;
 * `active_index` is the selected tab (0 when the leaf is empty).
 *
 * Runtime dock-node handles are never stored. `id` is an optional stable
 * string label; when unset the writer uses a path such as `root/0/1`.
 */
typedef struct sk_ui_dock_layout_node_t {
	sk_ui_dock_layout_kind_t kind;
	u32 flags;
	const_chr_t id;
	sk_ui_dock_split_t axis;
	f32 ratio;
	struct sk_ui_dock_layout_node_t* child_a;
	struct sk_ui_dock_layout_node_t* child_b;
	const_chr_t tabs[SK_UI_DOCK_LEAF_TABS_MAX];
	u32 tab_count;
	u32 active_index;
} sk_ui_dock_layout_node_t;

/**
 * On-disk dock layout document (JSON object, or archive map `"dock"`).
 * `version` is the integer format version (SK_UI_DOCK_LAYOUT_VERSION).
 */
typedef struct sk_ui_dock_layout_t {
	i32 version;
	u32 flags;
	const_chr_t id;
	sk_ui_dock_layout_node_t* root;
	sk_ui_dock_layout_float_t floating[SK_UI_DOCK_LAYOUT_FLOAT_MAX];
	u32 floating_count;
	u8 _pad0[4]; /**< Align after floating_count. */
} sk_ui_dock_layout_t;

/**
 * 0 if @p version may be applied as-is (equals SK_UI_DOCK_LAYOUT_VERSION).
 * Non-zero: reject the document, log @p version, and keep/restore the
 * default layout. Older versions are not migrated.
 */
SK_FINLINE i32 sk_ui_dock_layout_version_supported(i32 version) {
	return version == (i32)SK_UI_DOCK_LAYOUT_VERSION ? 0 : -1;
}

/**
 * Tab close / undock notification. Host may destroy the editor_window.
 * @param ctx        UI context.
 * @param window_id  Stable window id (node id).
 * @param user       Pointer from dock_set_tab_callback.
 */
typedef void (*sk_ui_dock_tab_fn)(sk_ui_context_t* ctx, const_chr_t window_id, void_ptr_t user);

/* ------------------------------------------------------------------ */
/*  Module API                                                        */
/* ------------------------------------------------------------------ */

/**
 * Global UI module API (one table per process after plugin load).
 * All tree operations go through this table; implementations are static
 * inside the plugin.
 */
typedef struct sk_ui_api_t {
	/**
	 * Initialize the UI module (no-op bookkeeping today).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*init)(void);

	/**
	 * Shutdown the UI module. Safe to call multiple times;
	 * no-op when init never succeeded. Does not destroy live contexts —
	 * callers must context_destroy every context first.
	 */
	void (*shutdown)(void);

	/* ---- context ---- */

	/**
	 * Create a UI context with a single root box node (kind BOX).
	 * The root starts with no id, empty class list, and full dirty flags.
	 * @param allocator Optional allocator; NULL uses the process default.
	 * @return New context, or NULL on allocation failure.
	 */
	sk_ui_context_t* (*context_create)(const sk_allocator_t* allocator);

	/**
	 * Destroy a context and every node it owns (no dangling handles remain
	 * valid). Safe on NULL.
	 * @param ctx Context to destroy.
	 */
	void (*context_destroy)(sk_ui_context_t* ctx);

	/**
	 * Root node of @p ctx (always alive while the context is live).
	 * @param ctx Context (must not be NULL).
	 * @return Root handle.
	 */
	sk_ui_node_t (*context_root)(const sk_ui_context_t* ctx);

	/**
	 * Number of live nodes in @p ctx (including the root).
	 * @param ctx Context (must not be NULL).
	 * @return Live node count.
	 */
	u32 (*context_node_count)(const sk_ui_context_t* ctx);

	/* ---- create / destroy ---- */

	/**
	 * Create a node of @p kind. If @p parent is valid and alive, the node is
	 * appended as its last child and the parent (and ancestors) are marked
	 * layout+paint dirty. If @p parent is SK_UI_NODE_INVALID the node is
	 * unparented (orphan) until inserted.
	 * @param ctx    Context (must not be NULL).
	 * @param kind   Node kind tag.
	 * @param parent Parent handle, or SK_UI_NODE_INVALID for an orphan.
	 * @return New node handle, or SK_UI_NODE_INVALID on failure.
	 */
	sk_ui_node_t (*node_create)(sk_ui_context_t* ctx, sk_ui_node_kind_t kind, sk_ui_node_t parent);

	/**
	 * Destroy @p node and its entire subtree. Removes the node from its
	 * parent (if any). Root cannot be destroyed (returns -1). Stale handles
	 * after success fail node_alive.
	 * @param ctx  Context (must not be NULL).
	 * @param node Node to destroy (must be alive, not root).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*node_destroy)(sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Whether @p node is live in @p ctx (valid index, matching generation).
	 * @return Non-zero when alive.
	 */
	i32 (*node_alive)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Kind tag of an alive node. */
	sk_ui_node_kind_t (*node_get_kind)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Set the kind of an alive node (structural tag only). Marks style+layout+paint dirty.
	 * @return 0 on success, non-zero if @p node is dead.
	 */
	i32 (*node_set_kind)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_kind_t kind);

	/* ---- hierarchy ---- */

	/** Parent of @p node, or SK_UI_NODE_INVALID for root / orphan. */
	sk_ui_node_t (*node_parent)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Number of direct children of an alive node. */
	u32 (*node_child_count)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Child at @p index (0-based). Returns SK_UI_NODE_INVALID if out of range
	 * or @p node is dead.
	 */
	sk_ui_node_t (*node_child_at)(const sk_ui_context_t* ctx, sk_ui_node_t node, u32 index);

	/**
	 * Index of @p child under its parent, or -1 if not a child / dead.
	 */
	i32 (*node_child_index)(const sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t child);

	/**
	 * Insert @p child under @p parent at @p index (0 .. child_count).
	 * If @p child already has a parent it is detached first. Cannot insert
	 * the root, an ancestor of @p parent, or a dead node. Marks layout+paint
	 * dirty on @p parent (and ancestors).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*node_insert_child)(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t child, u32 index);

	/**
	 * Detach @p child from @p parent without destroying it (becomes orphan).
	 * Marks layout+paint dirty on @p parent (and ancestors).
	 * @return 0 on success, non-zero if not a child or dead.
	 */
	i32 (*node_remove_child)(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t child);

	/**
	 * Move @p child to @p new_index among siblings under the same parent.
	 * Marks layout+paint dirty on the parent (and ancestors).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*node_set_child_index)(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t child, u32 new_index);

	/**
	 * Reparent @p node under @p new_parent at @p index (see node_insert_child).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*node_reparent)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t new_parent, u32 index);

	/* ---- user-assignable id (unique, tester query surface) ---- */

	/**
	 * Assign a user id string to @p node. Ids are unique within the context.
	 * Empty string or NULL clears the id. Copies @p id into node storage.
	 * @return 0 on success, -1 if dead, -2 if another live node already has @p id.
	 */
	i32 (*node_set_id)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t id);

	/**
	 * User id of @p node, or NULL if unset / dead.
	 * Points at node-owned storage; do not free.
	 */
	const_chr_t (*node_get_id)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Find the live node with user id @p id.
	 * @return Handle, or SK_UI_NODE_INVALID if not found.
	 */
	sk_ui_node_t (*find_by_id)(const sk_ui_context_t* ctx, const_chr_t id);

	/* ---- class list (tester / style query surface) ---- */

	/**
	 * Append a class name if not already present. Copies @p class_name.
	 * Marks style+layout+paint dirty. Empty / NULL is ignored (returns 0).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*node_add_class)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t class_name);

	/**
	 * Remove a class name if present. Marks style+layout+paint dirty.
	 * @return 0 if removed or was absent, non-zero if node is dead.
	 */
	i32 (*node_remove_class)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t class_name);

	/** Non-zero if @p node has class @p class_name. */
	i32 (*node_has_class)(const sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t class_name);

	/** Number of classes on an alive node. */
	u32 (*node_class_count)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Class name at @p index, or NULL if out of range / dead.
	 * Points at node-owned storage.
	 */
	const_chr_t (*node_class_at)(const sk_ui_context_t* ctx, sk_ui_node_t node, u32 index);

	/* ---- property storage ---- */

	/**
	 * Set an i32 property named @p key (creates or replaces). Marks paint dirty.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*node_set_prop_i32)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, i32 value);

	/**
	 * Set an f32 property named @p key (creates or replaces). Marks paint dirty.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*node_set_prop_f32)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, f32 value);

	/**
	 * Set a string property named @p key (owned copy). Marks paint dirty.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*node_set_prop_str)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, const_chr_t value);

	/**
	 * Look up a property by @p key.
	 * @param out Optional; filled when found and non-NULL.
	 * @return 0 if found, non-zero if missing / dead / bad key.
	 */
	i32 (*node_get_prop)(const sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, sk_ui_prop_value_t* out);

	/**
	 * Remove a property by @p key if present.
	 * @return 0 if removed or was absent, non-zero if node is dead.
	 */
	i32 (*node_clear_prop)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key);

	/**
	 * Optional host binding pointer + type cookie (not owned by UI).
	 * @return 0 on success, non-zero if node is dead.
	 */
	i32 (*node_set_user_data)(sk_ui_context_t* ctx, sk_ui_node_t node, void_ptr_t data, sk_type_id_t type_id);

	/** User data pointer previously set, or NULL. */
	void_ptr_t (*node_get_user_data)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** User data type cookie, or SK_TYPE_ID_ZERO. */
	sk_type_id_t (*node_get_user_data_type)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/* ---- dirty tracking ---- */

	/**
	 * OR @p flags onto @p node and every ancestor (rootward).
	 * @return 0 on success, non-zero if @p node is dead.
	 */
	i32 (*node_mark_dirty)(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);

	/** Dirty flags currently set on an alive node (0 if dead). */
	u32 (*node_get_dirty)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Clear @p flags bits on @p node only (does not walk children).
	 * Used after a simulated or real layout/paint pass on that node.
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_clear_dirty)(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);

	/**
	 * Clear @p flags on @p root and every descendant (post-order walk).
	 * Simulates finishing a layout or paint pass over a subtree.
	 * @return 0 on success, non-zero if @p root is dead.
	 */
	i32 (*node_clear_dirty_subtree)(sk_ui_context_t* ctx, sk_ui_node_t root, u32 flags);

	/**
	 * Non-zero if @p node has any of @p flags set. When ancestor propagation
	 * is used, a zero result means the whole subtree is clean for those flags.
	 */
	i32 (*node_is_dirty)(const sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);

	/* ---- traversal ---- */

	/**
	 * Depth-first pre-order walk starting at @p root (visits root first).
	 * Skips nothing; callers that care about dirty bits check node_is_dirty.
	 * @return 0 if the walk completed, or the non-zero status returned by @p fn.
	 */
	i32 (*traverse_preorder)(sk_ui_context_t* ctx, sk_ui_node_t root, sk_ui_traverse_fn fn, void_ptr_t user);

	/**
	 * Depth-first post-order walk starting at @p root (visits root last).
	 * @return 0 if the walk completed, or the non-zero status returned by @p fn.
	 */
	i32 (*traverse_postorder)(sk_ui_context_t* ctx, sk_ui_node_t root, sk_ui_traverse_fn fn, void_ptr_t user);

	/**
	 * Pre-order walk that only visits nodes with any of @p dirty_mask set.
	 * Because dirty bits propagate to ancestors, a clean node is skipped
	 * entirely (subtree not entered).
	 * @return 0 if the walk completed, or the non-zero status returned by @p fn.
	 */
	i32 (*traverse_dirty_preorder)(sk_ui_context_t* ctx, sk_ui_node_t root, u32 dirty_mask, sk_ui_traverse_fn fn, void_ptr_t user);

	/* ---- layout (flexbox, logical units) ---- */

	/**
	 * Replace the layout style of @p node (copied). Marks layout dirty up the tree.
	 * @return 0 on success, non-zero if @p node is dead.
	 */
	i32 (*node_set_layout_style)(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_layout_style_t* style);

	/**
	 * Copy the layout style of an alive node into @p out.
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_get_layout_style)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_layout_style_t* out);

	/**
	 * Border-box and/or content-box rects in **logical** units, relative to the
	 * parent content origin (root is relative to (0,0) of the root size).
	 * Either out pointer may be NULL. Valid after a successful layout().
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_get_layout_rect)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content);

	/**
	 * Same as node_get_layout_rect but after layout_apply_scale (physical px).
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_get_layout_rect_scaled)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content);

	/**
	 * Install an optional measure callback for intrinsic content. Pass NULL to clear.
	 * The solver never inspects fonts; text/image nodes must supply sizes via this.
	 */
	void (*set_measure_fn)(sk_ui_context_t* ctx, sk_ui_measure_fn fn, void_ptr_t user);

	/**
	 * Run the flexbox layout solver for the whole tree.
	 * Root is sized to @p root_width x @p root_height logical units (typically
	 * the window client size). Clears SK_UI_DIRTY_LAYOUT on all nodes on success.
	 * Does not apply HiDPI scale — call layout_apply_scale separately.
	 * @return 0 on success, non-zero on failure (e.g. OOM during scratch alloc).
	 */
	i32 (*layout)(sk_ui_context_t* ctx, f32 root_width, f32 root_height);

	/**
	 * Multiply all logical layout rects by content scale into scaled outputs.
	 * Layout itself never sees scale; paint/hit-test in physical pixels use
	 * the scaled rects. Stores the scale on the context for queries.
	 * @return 0 on success.
	 */
	i32 (*layout_apply_scale)(sk_ui_context_t* ctx, f32 scale_x, f32 scale_y);

	/**
	 * Last content scale applied via layout_apply_scale (defaults 1,1).
	 * Either out pointer may be NULL.
	 */
	void (*layout_get_content_scale)(const sk_ui_context_t* ctx, f32* out_scale_x, f32* out_scale_y);

	/* ---- style registry + resolve ---- */

	/**
	 * Register or replace a named style class with @p base properties.
	 * Copies @p base (including font_family string). Existing variants are
	 * kept when replacing base only via this call — use style_class_set_variant
	 * for hover/active/focused/disabled. Marks STYLE (+ LAYOUT/PAINT from mask)
	 * on live nodes that list @p name.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*style_class_register)(sk_ui_context_t* ctx, const_chr_t name, const sk_ui_style_props_t* base);

	/**
	 * Set or replace a state variant for a registered class.
	 * @p state must be exactly one of HOVER / ACTIVE / FOCUSED / DISABLED.
	 * Marks dirty on live nodes that use the class (flags from @p props mask).
	 * @return 0 on success, non-zero if class missing / bad state / OOM.
	 */
	i32 (*style_class_set_variant)(sk_ui_context_t* ctx, const_chr_t name, sk_ui_state_flags_t state, const sk_ui_style_props_t* props);

	/**
	 * Remove a style class from the registry. Does not remove class names from
	 * nodes (they simply stop contributing). Marks STYLE dirty on users.
	 * @return 0 if removed or was absent, non-zero on bad args.
	 */
	i32 (*style_class_unregister)(sk_ui_context_t* ctx, const_chr_t name);

	/**
	 * Non-zero if @p name is registered.
	 */
	i32 (*style_class_has)(const sk_ui_context_t* ctx, const_chr_t name);

	/**
	 * Replace the node's inline style override set (highest cascade layer).
	 * Pass NULL or empty mask to clear. Copies strings. Marks dirty from mask.
	 * @return 0 on success, non-zero if node is dead.
	 */
	i32 (*node_set_inline_style)(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_style_props_t* props);

	/**
	 * Merge @p props into the node's inline style (OR mask, overwrite fields).
	 * Marks dirty from the merged-in mask only.
	 * @return 0 on success, non-zero if node is dead.
	 */
	i32 (*node_merge_inline_style)(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_style_props_t* props);

	/**
	 * Copy the current inline style props into @p out (font_family points at
	 * node-owned storage). out may be NULL to query presence only.
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_get_inline_style)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_style_props_t* out);

	/**
	 * Set interaction state flags (replaces previous). Marks STYLE and, when
	 * any applied class has a variant for the changed bits, LAYOUT/PAINT from
	 * those variant masks. Always marks at least STYLE when flags change.
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_set_state)(sk_ui_context_t* ctx, sk_ui_node_t node, u32 state_flags);

	/** Current state flags (0 if dead). */
	u32 (*node_get_state)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Copy the last computed style into @p out. Valid after style_resolve.
	 * New nodes start with defaults after first resolve (or after context create
	 * resolve is not automatic — call style_resolve).
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_get_computed_style)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_computed_style_t* out);

	/**
	 * Resolve styles for all STYLE-dirty nodes (parent-before-child for
	 * inheritance). Precedence per property:
	 *   inline > later class (base then state variants) > earlier class >
	 *   inherited (text only) > default.
	 * State variant order within a class: base → hover → focused → active → disabled.
	 * Writes layout fields into layout_style for the flex solver.
	 * Clears SK_UI_DIRTY_STYLE on resolved nodes; leaves LAYOUT/PAINT as set.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*style_resolve)(sk_ui_context_t* ctx);

	/* ---- input routing / hit testing / focus (synthetic-first) ---- */

	/**
	 * Top-most node under logical point (x,y) after layout.
	 * Later siblings are above earlier ones (paint / z order).
	 * Honors clip-children regions: a point outside an ancestor clip never
	 * hits that ancestor's descendants. pointer-events:none skips a node as
	 * a target but still tests its children. Disabled nodes are not targets.
	 * @return Hit node, or SK_UI_NODE_INVALID if nothing is hit.
	 */
	sk_ui_node_t (*hit_test)(const sk_ui_context_t* ctx, f32 x, f32 y);

	/**
	 * Absolute border-box of @p node in the same logical space as hit_test
	 * (root content origin = layout root). Either out may be NULL.
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_get_abs_rect)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content);

	/**
	 * Install typed + generic event callbacks on @p node (copied by value).
	 * Pass NULL to clear all handlers. user is shared by every callback.
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_set_callbacks)(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_node_callbacks_t* callbacks);

	/**
	 * Copy current callbacks into @p out (may be NULL to only probe aliveness).
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_get_callbacks)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_callbacks_t* out);

	/**
	 * When non-zero, descendants outside this node's content box are not
	 * hit-testable (and later paint will scissor). Marks layout dirty.
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_set_clip_children)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 clip);

	/** Non-zero if @p node clips children (0 if dead). */
	i32 (*node_get_clip_children)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Set pointer-events mode (AUTO participates as a hit target; NONE skips
	 * self but still walks children). @return 0 on success, non-zero if dead.
	 */
	i32 (*node_set_pointer_events)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_pointer_events_t mode);

	/** Current pointer-events mode (AUTO if dead). */
	sk_ui_pointer_events_t (*node_get_pointer_events)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Mark @p node focusable for tab order and focus_set. Buttons default
	 * focusable when created; other kinds default non-focusable.
	 * @return 0 on success, non-zero if dead.
	 */
	i32 (*node_set_focusable)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 focusable);

	/** Non-zero if @p node is focusable (0 if dead). */
	i32 (*node_get_focusable)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Move keyboard focus to @p node (must be focusable and not disabled).
	 * Pass SK_UI_NODE_INVALID to clear focus. Dispatches FOCUS_OUT / FOCUS_IN
	 * and updates SK_UI_STATE_FOCUSED.
	 * @return 0 on success, non-zero if node is dead / not focusable / disabled.
	 */
	i32 (*focus_set)(sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Current focused node, or SK_UI_NODE_INVALID. */
	sk_ui_node_t (*focus_get)(const sk_ui_context_t* ctx);

	/**
	 * Advance focus to the next (or previous if @p reverse) focusable node in
	 * tree preorder tab order, wrapping at the ends. No-op if none focusable.
	 * @return 0 on success (including no-op), non-zero on failure.
	 */
	i32 (*focus_advance)(sk_ui_context_t* ctx, i32 reverse);

	/**
	 * Inject one synthetic (or platform-translated) input event and route it
	 * immediately. This is the canonical ingress for hosts and the future UI
	 * tester — not a test-only hook. Coordinates are logical units matching
	 * layout(). Side effects: hover enter/leave, pointer capture, active/
	 * hover state flags, click synthesis, tab focus traversal, wants_* flags.
	 * @return 0 on success, non-zero on failure (e.g. bad kind).
	 */
	i32 (*input_dispatch)(sk_ui_context_t* ctx, const sk_ui_input_event_t* event);

	/** Node currently capturing the pointer (drag), or SK_UI_NODE_INVALID. */
	sk_ui_node_t (*pointer_capture_get)(const sk_ui_context_t* ctx);

	/**
	 * Force pointer capture to @p node (or clear with SK_UI_NODE_INVALID).
	 * Normally set automatically on pointer down. @return 0 on success.
	 */
	i32 (*pointer_capture_set)(sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Non-zero if the last input_dispatch indicated the UI wants mouse input
	 * (hit a node, has capture, or is mid-drag). Hosts skip gameplay mouse.
	 */
	i32 (*wants_mouse)(const sk_ui_context_t* ctx);

	/**
	 * Non-zero if a focusable node holds focus (UI wants keyboard / text).
	 */
	i32 (*wants_keyboard)(const sk_ui_context_t* ctx);

	/* ---- paint / draw list (CPU only; no GPU) ---- */

	/**
	 * Walk the laid-out, styled tree and rebuild the context draw list when
	 * needed. Geometry is emitted in **physical pixels** (logical × content
	 * scale applied exactly once). No GPU upload or draw calls.
	 *
	 * Rebuild when any node is PAINT-dirty or content scale changed since the
	 * last successful paint; otherwise reuses the previous list in place
	 * (generation unchanged, get_draw_list()->reused == 1).
	 *
	 * Clears SK_UI_DIRTY_PAINT on all nodes after a rebuild. Call after
	 * style_resolve, layout, and layout_apply_scale (or accept default 1× scale).
	 *
	 * @param ctx    Context (must not be NULL).
	 * @param params Optional; NULL uses empty defaults (no text glyphs / fonts).
	 * @return 0 on success, non-zero on failure (e.g. OOM).
	 */
	i32 (*paint)(sk_ui_context_t* ctx, const sk_ui_paint_params_t* params);

	/**
	 * Last draw list produced by paint() for @p ctx.
	 * Pointers remain valid until the next rebuild paint, context_destroy, or
	 * until paint fails mid-rebuild (list may be empty). Never free the arrays.
	 * @return Non-NULL while the context is live (may be empty before first paint).
	 */
	const sk_ui_draw_list_t* (*get_draw_list)(const sk_ui_context_t* ctx);

	/* ---- font system (FreeType face load + MSDF atlas, CPU only) ---- */

	/**
	 * Create a font system. Owns the FreeType library (face loading, cmap
	 * queries, and metrics) and per-font baked MSDF atlases.
	 * @param allocator Optional; NULL uses the process default.
	 * @return New system, or NULL on failure.
	 */
	sk_ui_font_system_t* (*font_system_create)(const sk_allocator_t* allocator);

	/**
	 * Destroy a font system, every font it owns, and their MSDF atlases.
	 * Safe on NULL.
	 */
	void (*font_system_destroy)(sk_ui_font_system_t* system);

	/**
	 * Load a TTF/OTF from @p path using the engine filesystem API (open/read/close).
	 * Bytes are copied into the font; the file is not kept open.
	 * @param system Font system (must not be NULL).
	 * @param fs     Filesystem table (e.g. app_api->filesystem_api(ctx)). Must not be NULL.
	 * @param path   UTF-8 path to a font file.
	 * @return Font face, or NULL if the file cannot be read or FreeType rejects it.
	 */
	sk_ui_font_t* (*font_load_path)(sk_ui_font_system_t* system, const sk_filesystem_api_t* fs, const_chr_t path);

	/**
	 * Load a TTF/OTF from memory. Copies @p data (@p size bytes) into the font.
	 * @return Font face, or NULL on failure.
	 */
	sk_ui_font_t* (*font_load_memory)(sk_ui_font_system_t* system, const u8* data, u32 size);

	/**
	 * Destroy one font face and its baked MSDF atlas (if any). Safe on NULL.
	 */
	void (*font_destroy)(sk_ui_font_t* font);

	/**
	 * Font metrics at @p pixel_size (from sk_ui_font_pixel_size or equivalent).
	 * Scaled from MSDF atlas em metrics (one atlas, any size); FreeType face
	 * metrics back the em baseline values. Widget sizing / wrap / align use
	 * font_measure_text (MSDF atlas metrics).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*font_get_metrics)(const sk_ui_font_t* font, u32 pixel_size, sk_ui_font_metrics_t* out);

	/**
	 * Map a Unicode codepoint to a glyph index (0 if missing / .notdef).
	 */
	u32 (*font_glyph_index)(const sk_ui_font_t* font, u32 codepoint);

	/**
	 * Bake an MSDF glyph atlas for the font (printable ASCII + space) via
	 * msdf-atlas-c. Idempotent when an atlas is already present. Paint uses
	 * this atlas for all text (single scale-independent bake). Owns RGB8 pixels and glyph
	 * metrics on the font; frees prior bake on rebake.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*font_msdf_bake)(sk_ui_font_t* font);

	/**
	 * Snapshot the baked MSDF atlas. @p out->pixels is font-owned.
	 * @return 0 on success, non-zero if not baked or args invalid.
	 */
	i32 (*font_msdf_get_atlas)(const sk_ui_font_t* font, sk_ui_msdf_atlas_t* out);

	/**
	 * Look up a codepoint in the baked MSDF atlas (em-normalized metrics).
	 * @return 0 on success, non-zero if not baked / missing glyph / bad args.
	 */
	i32 (*font_msdf_get_glyph)(const sk_ui_font_t* font, u32 codepoint, sk_ui_msdf_glyph_t* out);

	/**
	 * Debug/dev dump of the baked MSDF atlas: writes @p path_prefix.raw (RGB8
	 * bytes) and @p path_prefix.json (glyph metrics + atlas metadata). Bakes
	 * first if needed. Creates parent directories when @p fs is non-NULL.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*font_msdf_dump)(sk_ui_font_t* font, const sk_filesystem_api_t* fs, const_chr_t path_prefix);

	/**
	 * Measure UTF-8 @p utf8 at @p pixel_size. Width is the typographic advance
	 * (wrap / align / Clay sizing). Height is line_height for a single line.
	 * Uses MSDF atlas em metrics × pixel_size (plus kerning, .notdef fallback).
	 * Missing glyphs contribute a defined .notdef box advance (never skipped).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*font_measure_text)(sk_ui_font_system_t* system, sk_ui_font_t* font, u32 pixel_size, const_chr_t utf8, f32* out_width, f32* out_height);

	/* ---- GPU renderer (draw list → sk_render_device_api_t only) ---- */

	/**
	 * Create a GPU renderer: compile embedded HLSL via DXC, build pipelines
	 * (alpha blend, scissor), allocate dynamic VB/IB, white texture, sampler.
	 * @return Renderer, or NULL on failure (shader compile, pipeline create, OOM).
	 */
	sk_ui_renderer_t* (*renderer_create)(const sk_ui_renderer_desc_t* desc);

	/**
	 * Destroy a renderer and all GPU resources it owns. Safe on NULL.
	 * Does not destroy the device, render pass, or font system.
	 */
	void (*renderer_destroy)(sk_ui_renderer_t* renderer);

	/**
	 * Rebuild graphics pipelines for a new render pass (e.g. swapchain format
	 * change). Invalidates nothing else (buffers/atlas stay valid).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*renderer_set_render_pass)(sk_ui_renderer_t* renderer, sk_render_pass_t render_pass);

	/**
	 * Upload draw-list geometry and dirty font atlas pages.
	 * Must be recorded **outside** a render pass (transfer / update_buffer).
	 * Call once per frame before begin_render_pass when the list may have changed.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*renderer_prepare)(sk_ui_renderer_t* renderer, const sk_ui_renderer_prepare_info_t* info);

	/**
	 * Bind UI pipeline state and issue batched draw_indexed for MESH commands.
	 * Must be recorded **inside** a render pass compatible with create / set_render_pass.
	 * Applies viewport (full target), scissor per mesh, alpha blending, and
	 * texture binds (white / font atlas page / host image).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*renderer_encode)(sk_ui_renderer_t* renderer, const sk_ui_renderer_encode_info_t* info);

	/* ---- headless capture (offscreen target + CPU readback) ---- */

	/**
	 * Create a headless capture: offscreen RGBA8 color target of fixed
	 * caller-specified size, explicit clear color, internal UI renderer,
	 * host-visible readback buffer, queue, and command buffers. No
	 * swapchain/window/surface is required — the target is a device-owned
	 * texture with COPY_SOURCE final state for readback.
	 * @return Capture, or NULL on failure.
	 */
	sk_ui_capture_t* (*capture_create)(const sk_ui_capture_desc_t* desc);

	/** Destroy a capture and every GPU/CPU resource it owns. Safe on NULL. */
	void (*capture_destroy)(sk_ui_capture_t* capture);

	/**
	 * Render @p info.draw_list into the offscreen target (cleared first with
	 * the desc clear color) and read the pixels back into @p out_image.
	 *
	 * Pipeline: renderer_prepare (upload, submitted + waited) →
	 * begin_render_pass (CLEAR) → renderer_encode → end_render_pass →
	 * memory_barrier → copy_texture_to_buffer → submit + wait → buffer_map →
	 * row-wise copy (readback rows are unpacked into tight rows).
	 *
	 * @p out_image is tightly packed row-major RGBA8 (stride = width * 4),
	 * top-left origin. The alpha is straight (NOT premultiplied): draw-list
	 * colors are authored straight and the UI pipeline blends with standard
	 * straight-alpha factors, so the cleared target + draw list compose to
	 * straight alpha. @p out_image->pixels is owned by the capture and stays
	 * valid until the next capture_frame or capture_destroy.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*capture_frame)(sk_ui_capture_t* capture, const sk_ui_capture_frame_info_t* info, sk_ui_cpu_image_t* out_image);

	/* ---- CPU image PNG write (stb_image_write) + test artifact paths ---- */

	/**
	 * Write a tightly packed @p image (typically from capture_frame) as a PNG
	 * at the caller-specified @p path. Creates parent directories as needed
	 * via @p fs (pass app_api->filesystem_api(ctx) from the host app context).
	 * On failure, logs a clear error through the process logger and returns
	 * non-zero. @p image->channels must be 1..4 (UI captures use 4 = RGBA8).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*cpu_image_write_png)(const sk_ui_cpu_image_t* image, const sk_filesystem_api_t* fs, const_chr_t path);

	/**
	 * Resolve the single shared root directory for all test artifacts.
	 * Never returns a path under the source tree by default.
	 *
	 * Resolution order:
	 *   1. SK_TEST_ARTIFACT_DIR environment variable (if non-empty)
	 *   2. Compile-time SK_TEST_ARTIFACT_DIR (build/test-artifacts)
	 *   3. {fs->temp_folder}/skore-test-artifacts
	 *
	 * @p fs is required only for the temp fallback (step 3); may be NULL when
	 * an env or compile-time root is available.
	 * @return 0 on success (null-terminated path in @p out), non-zero on failure.
	 */
	i32 (*test_artifact_root)(const sk_filesystem_api_t* fs, char* out, u32 out_cap);

	/**
	 * Build a deterministic PNG path under the test-artifact root:
	 *   {root}/{sanitized_name}.png
	 * @p name is a test or scene name; path separators and unsafe characters
	 * are replaced so the result is a single file component under the root.
	 * Does not create directories (cpu_image_write_png does).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*test_artifact_png_path)(const sk_filesystem_api_t* fs, const_chr_t name, char* out, u32 out_cap);

	/**
	 * Like test_artifact_png_path but under an optional subdirectory:
	 *   {root}/{subdir}/{sanitized_name}.png
	 * @p subdir NULL/empty behaves exactly like test_artifact_png_path.
	 * Useful for suite-scoped capture trees (e.g. "text-screenshot/msdf")
	 * so related runs land in separate folders. Subdirectory separators
	 * are preserved (not sanitized).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*test_artifact_png_path_in)(const sk_filesystem_api_t* fs, const_chr_t subdir, const_chr_t name, char* out, u32 out_cap);

	/* ---- v1 widgets (compose tree + default styles + behavior) ---- */

	/**
	 * Register default style classes for the v1 widget set (panel, view, label,
	 * button, checkbox, slider, text_input, scroll_view, image, menu surfaces,
	 * docking / editor window chrome) including hover/active/focused/disabled
	 * variants. Idempotent. Called automatically from context_create; safe to
	 * call again after unregistering a class.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*widgets_register_defaults)(sk_ui_context_t* ctx);

	/**
	 * Optional platform clipboard hooks for TextInput cut/copy/paste.
	 * Pass NULL get/set to clear. @p user is forwarded to both callbacks.
	 * get: write UTF-8 into @p buf (capacity @p cap), set @p out_len, return 0.
	 * set: store UTF-8 @p text, return 0 on success.
	 */
	void (*set_clipboard_fns)(sk_ui_context_t* ctx, sk_ui_clipboard_get_fn get_fn, sk_ui_clipboard_set_fn set_fn, void_ptr_t user);

	/** Styled container (BOX + class ui-panel). @p id optional stable test id. */
	sk_ui_node_t (*widget_panel)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/** Lightweight container (BOX + class ui-view). */
	sk_ui_node_t (*widget_view)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/** Text label with optional wrap/align props (TEXT + class ui-label). */
	sk_ui_node_t (*widget_label)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/** Button with hover/active/disabled styles (BUTTON + class ui-button). */
	sk_ui_node_t (*widget_button)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);

	/** Checkbox; @p checked non-zero starts checked (BOX + class ui-checkbox). */
	sk_ui_node_t (*widget_checkbox)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 checked, const_chr_t id);

	/**
	 * Radio button; @p checked non-zero starts selected (BOX + class ui-radio).
	 * Paint draws a filled inner disc when checked; outer ring is circular chrome.
	 */
	sk_ui_node_t (*widget_radio)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 checked, const_chr_t id);

	/**
	 * Toggle switch; @p on non-zero starts ON (BOX + class ui-toggle).
	 * Paint draws a pill track + distinct thumb; ON places the thumb toward the end.
	 */
	sk_ui_node_t (*widget_toggle)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 on, const_chr_t id);

	/** Horizontal slider clamped to [min_v, max_v]. */
	sk_ui_node_t (*widget_slider)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 min_v, f32 max_v, f32 value, const_chr_t id);

	/**
	 * Dual-thumb range slider on [min_v, max_v] with low/high values (widget=range_slider).
	 * Paint draws a track, filled span between thumbs, and two distinct grab handles.
	 */
	sk_ui_node_t (*widget_range_slider)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 min_v, f32 max_v, f32 value_low, f32 value_high, const_chr_t id);

	/**
	 * Progress bar (display only; no grab handle). @p fraction is clamped to [0,1]
	 * (widget=progress). Paint fills left→right by fraction; vision grades 0/partial/full.
	 */
	sk_ui_node_t (*widget_progress)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 fraction, const_chr_t id);

	/** Single-line text field with caret/selection editing (InputText). */
	sk_ui_node_t (*widget_text_input)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/** Clipped scroll container with wheel/drag scrolling and painted scrollbars. */
	sk_ui_node_t (*widget_scroll_view)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/** Image node bound to host texture id (IMAGE + class ui-image). */
	sk_ui_node_t (*widget_image)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 texture_id, const_chr_t id);

	/**
	 * Horizontal menu bar container (row flex, widget=menu_bar). Hosts menu /
	 * menu_item children. Stable Clay id for hover across frames.
	 */
	sk_ui_node_t (*widget_menu_bar)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/**
	 * Top-level menu root: labeled trigger + child menu_popup (closed until
	 * menu_set_open). Click toggles open. widget=menu.
	 */
	sk_ui_node_t (*widget_menu)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);

	/**
	 * Selectable menu row (widget=menu_item). Optional nested menu_popup for
	 * submenus (use widget_submenu or attach a popup and set open on hover).
	 */
	sk_ui_node_t (*widget_menu_item)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);

	/**
	 * Floating overlay popup (widget=menu_popup, absolute + Clay floating).
	 * open prop (0/1) survives frames; closed popups are omitted from Clay.
	 * @p attach 0 = below parent, 1 = to the right (submenu).
	 */
	sk_ui_node_t (*widget_menu_popup)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 attach, const_chr_t id);

	/**
	 * Dropdown: labeled trigger + menu_popup child (widget=dropdown). Same open
	 * model as widget_menu.
	 */
	sk_ui_node_t (*widget_dropdown)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);

	/**
	 * Context menu overlay attached to the tree root via Clay floating
	 * (widget=context_menu). Position with layout left/top (viewport).
	 */
	sk_ui_node_t (*widget_context_menu)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/**
	 * Submenu item with nested menu_popup attached to the right (widget=submenu).
	 * Pointer enter opens; leave closes when pointer exits the item+popup chain.
	 */
	sk_ui_node_t (*widget_submenu)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);

	/**
	 * Open/close a menu, dropdown, submenu, menu_popup, or context_menu.
	 * open != 0 shows the floating popup; state is a retained prop (survives
	 * frames). Returns 0 on success.
	 */
	i32 (*menu_set_open)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 open);
	/** Non-zero if the menu/popup open prop is set. */
	i32 (*menu_get_open)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/**
	 * First child with widget=menu_popup under @p node (menu, dropdown, submenu).
	 * SK_UI_NODE_INVALID if none.
	 */
	sk_ui_node_t (*menu_get_popup)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Root docking host (widget=dock_space). Clips nested dock nodes; hosts
	 * dock_node / splitter trees. Stable Clay id for pointer during drags.
	 */
	sk_ui_node_t (*widget_dock_space)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/**
	 * Dock region (widget=dock_node). Nested container with clip_children.
	 * @p orientation 0 = row children (horizontal split), 1 = column.
	 */
	sk_ui_node_t (*widget_dock_node)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 orientation, const_chr_t id);

	/**
	 * Resize handle between dock regions (widget=splitter). Stable Clay id so
	 * pointer capture / drag state resolves across frames.
	 * @p axis 0 = vertical bar (resize width), 1 = horizontal bar (resize height).
	 * Prop "ratio" (0..1) updated while dragging; optional float on_change.
	 */
	sk_ui_node_t (*widget_splitter)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 axis, const_chr_t id);

	/**
	 * Horizontal tab strip (widget=tab_bar). Hosts tab children; stable Clay id.
	 * Bind the selected page index (tab_bar_bind_selected); do not rebuild.
	 */
	sk_ui_node_t (*widget_tab_bar)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/**
	 * Selectable tab (widget=tab). Prop "active" (0/1); click activates and
	 * clears active on sibling page tabs under the same tab_bar.
	 * Equivalent to widget_tab_item(..., NULL, 0).
	 */
	sk_ui_node_t (*widget_tab)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);

	/**
	 * Editor window chrome (widget=editor_window): column with window_title_bar
	 * (drag target) and window_content (clipped body). Nested containers route
	 * through Clay with stable ids for title drag and content scroll/clip.
	 */
	sk_ui_node_t (*widget_editor_window)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t title, const_chr_t id);

	/** Title bar child under an editor_window (widget=window_title_bar). */
	sk_ui_node_t (*editor_window_title_bar)(const sk_ui_context_t* ctx, sk_ui_node_t window);
	/** Content body child under an editor_window (widget=window_content). */
	sk_ui_node_t (*editor_window_content)(const sk_ui_context_t* ctx, sk_ui_node_t window);
	/** Set title bar text (prop "text" on the title bar). */
	i32 (*editor_window_set_title)(sk_ui_context_t* ctx, sk_ui_node_t window, const_chr_t title);

	/** Set/get tab active prop (0/1). */
	i32 (*tab_set_active)(sk_ui_context_t* ctx, sk_ui_node_t tab, i32 active);
	i32 (*tab_get_active)(const sk_ui_context_t* ctx, sk_ui_node_t tab);
	/**
	 * Activate @p tab under @p tab_bar and clear active on sibling tabs.
	 * Pass SK_UI_NODE_INVALID for tab_bar to use the tab's parent.
	 */
	i32 (*tab_bar_set_active)(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, sk_ui_node_t tab);

	/** Splitter ratio (0..1); clamped. Fires on_change when set programmatically too. */
	i32 (*splitter_set_ratio)(sk_ui_context_t* ctx, sk_ui_node_t splitter, f32 ratio);
	f32 (*splitter_get_ratio)(const sk_ui_context_t* ctx, sk_ui_node_t splitter);
	i32 (*splitter_set_on_change)(sk_ui_context_t* ctx, sk_ui_node_t splitter, sk_ui_widget_float_fn fn, void_ptr_t user);

	/** Set/get label text (prop "text"). */
	i32 (*label_set_text)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text);
	const_chr_t (*label_get_text)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/** wrap: 0=off 1=on. text_align/vertical_align: 0=start 1=center 2=end. */
	i32 (*label_set_wrap)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 wrap);
	i32 (*label_set_align)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 text_align, i32 vertical_align);

	i32 (*button_set_label)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label);
	i32 (*button_set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);

	i32 (*checkbox_set_checked)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked);
	i32 (*checkbox_get_checked)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Fires after toggle; @p user stored for the callback. */
	i32 (*checkbox_set_on_change)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user);

	i32 (*radio_set_checked)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 checked);
	i32 (*radio_get_checked)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Fires when the radio becomes selected. */
	i32 (*radio_set_on_change)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user);

	i32 (*toggle_set_on)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 on);
	i32 (*toggle_get_on)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*toggle_set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
	/** Fires after the switch flips; @p user stored for the callback. */
	i32 (*toggle_set_on_change)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_bool_fn fn, void_ptr_t user);

	i32 (*slider_set_value)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value);
	f32 (*slider_get_value)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*slider_set_range)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 min_v, f32 max_v);
	i32 (*slider_set_on_change)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_float_fn fn, void_ptr_t user);

	/** Range slider low/high (clamped and ordered so low <= high). */
	i32 (*range_slider_set_values)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value_low, f32 value_high);
	i32 (*range_slider_get_values)(const sk_ui_context_t* ctx, sk_ui_node_t node, f32* out_low, f32* out_high);

	/** Progress fraction [0,1]. */
	i32 (*progress_set_value)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 fraction);
	f32 (*progress_get_value)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	i32 (*text_input_set_text)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text);
	const_chr_t (*text_input_get_text)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_get_caret)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_set_selection)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 start, i32 end);
	i32 (*text_input_insert)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t utf8);
	i32 (*text_input_delete_selection)(sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_copy)(sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_cut)(sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_paste)(sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Content root under a ScrollView — parent children here. */
	sk_ui_node_t (*scroll_view_content)(const sk_ui_context_t* ctx, sk_ui_node_t scroll_view);
	i32 (*scroll_view_set_scroll)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 scroll_x, f32 scroll_y);
	i32 (*scroll_view_get_scroll)(const sk_ui_context_t* ctx, sk_ui_node_t node, f32* out_x, f32* out_y);
	i32 (*scroll_view_set_content_size)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height);

	i32 (*image_set_texture)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 texture_id);

	/* ---- retained item-array binding (APX-338; tree / list / combo / table) ---- */

	/**
	 * Host widget bound to @p items (pointer kept). @p kind selects tree / list /
	 * combo / table presentation. Same item struct for every kind.
	 * @p id is the automation test id (auto-generated when NULL/empty).
	 */
	sk_ui_node_t (*widget_item_view)(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, sk_ui_item_bind_kind_t kind, const_chr_t id);
	/** widget_item_view(..., SK_UI_ITEM_BIND_TREE, id). Tree-widget task entry. */
	sk_ui_node_t (*widget_tree)(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, const_chr_t id);
	/** widget_item_view(..., SK_UI_ITEM_BIND_LIST, id). */
	sk_ui_node_t (*widget_list)(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_item_array_t* items, const_chr_t id);

	/**
	 * Bind @p items to an existing host (or rebind). Pass items == NULL to
	 * unbind (row nodes destroyed; open/selected maps kept until clear_state
	 * or host destroy).
	 * @return 0 on success, non-zero on dead host / OOM.
	 */
	i32 (*item_bind)(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_array_t* items, sk_ui_item_bind_kind_t kind);
	/** Replace the stored array pointer (NULL unbinds) and sync. */
	i32 (*item_bind_set_array)(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_array_t* items);
	/**
	 * Diff the caller array by id and update row nodes in place.
	 * Also runs from style_resolve / harness_step.
	 */
	i32 (*item_bind_sync)(sk_ui_context_t* ctx, sk_ui_node_t host);
	/** Stored array pointer, or NULL if unbound. */
	sk_ui_item_array_t* (*item_bind_get_array)(const sk_ui_context_t* ctx, sk_ui_node_t host);
	sk_ui_item_bind_kind_t (*item_bind_get_kind)(const sk_ui_context_t* ctx, sk_ui_node_t host);
	/** Live row node for @p item_id, or SK_UI_NODE_INVALID. */
	sk_ui_node_t (*item_bind_find)(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id);
	/** Number of materialized row nodes (visible rows). */
	u32 (*item_bind_row_count)(const sk_ui_context_t* ctx, sk_ui_node_t host);

	i32 (*item_bind_get_open)(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id);
	i32 (*item_bind_set_open)(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, i32 open);
	i32 (*item_bind_get_selected)(const sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id);
	i32 (*item_bind_set_selected)(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, i32 selected);
	/** Last activated item id, or SK_UI_ITEM_ID_NONE. */
	u64 (*item_bind_last_activate)(const sk_ui_context_t* ctx, sk_ui_node_t host);
	/** Non-zero if the last interaction was the expand arrow (not row activate). */
	i32 (*item_bind_last_was_arrow)(const sk_ui_context_t* ctx, sk_ui_node_t host);
	/** Drop open / selected / seed maps (row nodes stay until next sync). */
	i32 (*item_bind_clear_state)(sk_ui_context_t* ctx, sk_ui_node_t host);
	i32 (*item_bind_set_on_activate)(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_id_fn fn, void_ptr_t user);
	i32 (*item_bind_set_on_toggle)(sk_ui_context_t* ctx, sk_ui_node_t host, sk_ui_item_id_fn fn, void_ptr_t user);
	/**
	 * TreeNodeEx flags for this host (SK_UI_TREE_NODE_FLAG_*). Default is
	 * SK_UI_TREE_NODE_FLAGS_DEFAULT (OpenOnArrow + full-row span + frame pad).
	 */
	i32 (*item_bind_set_flags)(sk_ui_context_t* ctx, sk_ui_node_t host, u32 flags);
	u32 (*item_bind_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t host);
	/**
	 * Set open with ImGuiCond. ONCE writes only if this id has not been
	 * seeded yet (EntityTree SetNextItemOpen(true, Once) on ancestors).
	 */
	i32 (*item_bind_set_open_cond)(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, i32 open, u32 cond);
	/** Open every ancestor of @p item_id with SK_UI_COND_ONCE, then sync. */
	i32 (*item_bind_open_ancestors)(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id);

	/* ---- automation / UI tester contract (query, accessors, actions) ---- */

	/**
	 * Find the first live node under @p scope whose user id (test id) equals
	 * @p test_id. Pass SK_UI_NODE_INVALID for @p scope to search the whole tree
	 * (same as find_by_id when ids are unique). Node ids assigned via
	 * node_set_id / widget factories are the stable test ids.
	 * @return Handle, or SK_UI_NODE_INVALID if not found / not under scope.
	 */
	sk_ui_node_t (*query_by_test_id)(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t test_id);

	/**
	 * First live node under @p scope that has style class @p class_name
	 * (preorder). SK_UI_NODE_INVALID scope = whole tree from root.
	 */
	sk_ui_node_t (*query_by_class)(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t class_name);

	/**
	 * First live node under @p scope with prop "widget" equal to @p widget_type
	 * (e.g. "button", "text_input", "scroll_view").
	 */
	sk_ui_node_t (*query_by_widget)(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t widget_type);

	/**
	 * First live node under @p scope whose visible text (prop "text") equals
	 * @p text exactly. Labels, buttons, and text inputs expose this prop.
	 */
	sk_ui_node_t (*query_by_text)(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t text);

	/**
	 * Collect up to @p max_out matches under @p scope into @p out (preorder).
	 * @return Number of matches written (may be < total if truncated).
	 *         When @p out is NULL or @p max_out is 0, returns the full count.
	 */
	u32 (*query_all_by_class)(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t class_name, sk_ui_node_t* out, u32 max_out);
	u32 (*query_all_by_widget)(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t widget_type, sk_ui_node_t* out, u32 max_out);
	u32 (*query_all_by_text)(const sk_ui_context_t* ctx, sk_ui_node_t scope, const_chr_t text, sk_ui_node_t* out, u32 max_out);

	/**
	 * Non-zero if @p node is considered visible to automation: alive, not
	 * marked hidden (prop "hidden" != 1), computed opacity > 0, and after
	 * layout has a non-empty absolute border box.
	 */
	i32 (*node_is_visible)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Non-zero if @p node is enabled (alive and SK_UI_STATE_DISABLED clear).
	 */
	i32 (*node_is_enabled)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Visible text string of @p node (prop "text"), or "" if unset / dead.
	 * Points at node-owned storage; do not free.
	 */
	const_chr_t (*node_get_visible_text)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Programmatic click: synthesizes pointer move + left button down/up at
	 * the element's absolute center via input_dispatch (same path as hosts).
	 * Requires a prior layout so hit-test geometry is valid.
	 * @return 0 on success, non-zero if dead / no layout rect.
	 */
	i32 (*action_click)(sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Focus @p node then inject UTF-8 text via SK_UI_INPUT_TEXT through
	 * input_dispatch (caret/selection and widget handlers run as for users).
	 * @return 0 on success, non-zero if focus or dispatch fails.
	 */
	i32 (*action_type_text)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text);

	/**
	 * Synthesize a wheel event over @p node's center (scroll_x/scroll_y as
	 * for SK_UI_INPUT_WHEEL). ScrollView and other wheel handlers run normally.
	 * @return 0 on success, non-zero if dead / no layout rect.
	 */
	i32 (*action_scroll)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 scroll_x, f32 scroll_y);

	/**
	 * Set keyboard focus via focus_set (FOCUS_OUT / FOCUS_IN, state flags).
	 * Pass SK_UI_NODE_INVALID to clear. Same entry as host tab/click focus.
	 * @return 0 on success, non-zero if not focusable / disabled / dead.
	 */
	i32 (*action_focus)(sk_ui_context_t* ctx, sk_ui_node_t node);

	/* ---- headless harness (construct / drive / optional soft-render) ---- */

	/**
	 * Create a headless UI harness: context + stable clock + optional RGBA
	 * soft-render buffer for golden comparison. Does not open a window or GPU.
	 * @param desc Non-NULL; width/height are logical root size (defaults 800x600).
	 * @return Harness, or NULL on failure.
	 */
	sk_ui_harness_t* (*harness_create)(const sk_ui_harness_desc_t* desc);

	/** Destroy harness, context, fonts, and pixel buffer. Safe on NULL. */
	void (*harness_destroy)(sk_ui_harness_t* harness);

	/** Owned UI context (valid until harness_destroy). */
	sk_ui_context_t* (*harness_context)(sk_ui_harness_t* harness);

	/**
	 * Advance one frame with @p delta_seconds (must be >= 0). Updates the
	 * stable clock (no wall time), then style_resolve → layout →
	 * layout_apply_scale → paint → optional soft-render. Tests must never
	 * depend on OS clocks — only this delta.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*harness_step)(sk_ui_harness_t* harness, f32 delta_seconds);

	/** Stable harness time in seconds (sum of deltas from create / reset). */
	f64 (*harness_time)(const sk_ui_harness_t* harness);

	/** Last delta passed to harness_step (0 before first step). */
	f32 (*harness_last_delta)(const sk_ui_harness_t* harness);

	/** Frame counter (increments on each successful harness_step). */
	u32 (*harness_frame_index)(const sk_ui_harness_t* harness);

	/**
	 * Change logical root size (and soft-render buffer if enabled).
	 * Marks layout dirty; takes effect on the next harness_step.
	 * @return 0 on success.
	 */
	i32 (*harness_set_size)(sk_ui_harness_t* harness, f32 width, f32 height);

	/** Content scale used by layout_apply_scale / soft-render size. */
	i32 (*harness_set_content_scale)(sk_ui_harness_t* harness, f32 scale);

	/**
	 * Soft-render RGBA8 pixels after the last harness_step (NULL if
	 * soft_render was disabled). Row-major, pitch = width * 4. Physical size
	 * is logical * content_scale (rounded).
	 */
	const u8* (*harness_pixels)(const sk_ui_harness_t* harness);

	/** Soft-render buffer size in pixels (0 if disabled). Either out may be NULL. */
	void (*harness_pixel_size)(const sk_ui_harness_t* harness, u32* out_w, u32* out_h);

	/**
	 * Optional paint font params (font_system + default face). When set, text
	 * glyphs are emitted on harness_step paint. Pass NULL system to clear.
	 * Ownership remains with the caller (destroyed before harness_destroy).
	 */
	void (*harness_set_font)(sk_ui_harness_t* harness, sk_ui_font_system_t* system, sk_ui_font_t* font);

	/** Draw list from the last paint inside harness_step (may be empty). */
	const sk_ui_draw_list_t* (*harness_draw_list)(const sk_ui_harness_t* harness);

	/* ---- sample in-game UI scene (main menu; host / HiDPI consumer) ---- */

	/**
	 * Register style classes used by the sample main menu (menu-screen,
	 * menu-card, menu-title, …). Idempotent. Called automatically by
	 * sample_menu_build; hosts may call earlier to customize.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*sample_menu_register_styles)(sk_ui_context_t* ctx);

	/**
	 * Build the sample main menu under @p parent (or the context root when
	 * @p parent is SK_UI_NODE_INVALID). Uses every v1 widget; layout and
	 * appearance come from style classes only. Stable test ids: menu-screen,
	 * menu-card, menu-title, menu-play, menu-volume, …
	 * @return Root screen node, or SK_UI_NODE_INVALID on failure.
	 */
	sk_ui_node_t (*sample_menu_build)(sk_ui_context_t* ctx, sk_ui_node_t parent);

	/**
	 * Logical size used by sample goldens and as a host default
	 * (320 x 240). Either out pointer may be NULL.
	 */
	void (*sample_menu_logical_size)(f32* out_width, f32* out_height);

	/* ---- Golden image comparison (APX-229) ---- */

	/**
	 * Compare two tightly packed RGBA8 (or 1–4 channel) images in memory.
	 * Mismatched dimensions fail immediately (SIZE_MISMATCH) with no pixel
	 * scan. Otherwise compares per-pixel with @p channel_tolerance and
	 * fails with MISMATCH when the fraction of differing pixels exceeds
	 * @p max_diff_fraction. Optional @p out_diff_rgba (same size as actual)
	 * is filled with a visual diff: dim actual for matches, bright red for
	 * failures. Optional @p out_stats receives counts, max channel delta,
	 * and the bounding box of the differing region.
	 * @return SK_UI_IMAGE_COMPARE_OK / MISMATCH / SIZE_MISMATCH / ERROR.
	 */
	i32 (*cpu_image_compare)(const sk_ui_cpu_image_t* actual, const sk_ui_cpu_image_t* expected, u32 channel_tolerance, f32 max_diff_fraction,
							 sk_ui_image_compare_stats_t* out_stats, u8* out_diff_rgba);

	/**
	 * Compare a captured @p actual image against a committed golden PNG at
	 * @p golden_path. Loads the golden via stb_image. On MISMATCH writes three
	 * artifacts under the test-artifact root ({name}_actual/expected/diff.png)
	 * and logs an ASCII summary (differ count, max channel delta, bbox, paths).
	 * Size mismatches fail immediately with a clear message (actual + expected
	 * still written when possible).
	 *
	 * Bless / update mode (never the default): set params->update_golden != 0
	 * or export SK_UI_REGEN_GOLDENS=1 — writes @p actual over @p golden_path and
	 * returns OK with stats.updated=1. Review the image before committing.
	 *
	 * @p params may be NULL (strict equality, no bless, name "image_compare").
	 * @p fs is used for parent-dir creation when writing artifacts/goldens.
	 * @return SK_UI_IMAGE_COMPARE_* code.
	 */
	i32 (*cpu_image_compare_golden)(const sk_ui_cpu_image_t* actual, const_chr_t golden_path, const sk_ui_image_compare_params_t* params, const sk_filesystem_api_t* fs,
									sk_ui_image_compare_stats_t* out_stats);

	/* ---- structural image assertions beyond pixel sampling (APX-230) ---- */

	/**
	 * Assert that every pixel in @p region matches @p color within tolerance
	 * (uniform fill). Fails when any pixel is outside tolerance; stats carry
	 * the nonmatching count, max channel delta, and the tight bbox of the
	 * offending pixels. Region is clamped to the image; an empty clamped
	 * region is an ERROR (likely a test bug).
	 * @return SK_UI_IMAGE_ASSERT_OK / FAIL / ERROR.
	 */
	i32 (*cpu_image_assert_solid)(const sk_ui_cpu_image_t* image, sk_ui_region_t region, const sk_ui_color_match_t* color, sk_ui_solid_stats_t* out_stats);

	/**
	 * Assert that the fraction of @p region pixels matching @p color lies in
	 * [min_fraction, max_fraction] (inclusive). Catches widgets that shrank,
	 * bled, or disappeared without requiring a golden. Stats carry the
	 * measured matching count and coverage.
	 * @return SK_UI_IMAGE_ASSERT_OK / FAIL / ERROR.
	 */
	i32 (*cpu_image_assert_coverage)(const sk_ui_cpu_image_t* image, sk_ui_region_t region, const sk_ui_color_match_t* color, f32 min_fraction, f32 max_fraction,
									 sk_ui_coverage_stats_t* out_stats);

	/**
	 * Find the tight bounding box of all @p region pixels matching @p color.
	 * Pure extraction (never fails on content); out_bbox->found is 0 and the
	 * box is zeroed when nothing matches.
	 * @return 0 on success, SK_UI_IMAGE_ASSERT_ERROR on bad arguments.
	 */
	i32 (*cpu_image_find_bbox)(const sk_ui_cpu_image_t* image, sk_ui_region_t region, const sk_ui_color_match_t* color, sk_ui_bbox_t* out_bbox);

	/**
	 * Assert the tight bbox of matching pixels matches @p expected within
	 * position/size tolerances. Catches drift and misalignment (a shifted
	 * widget keeps its colors, so pixel sampling misses it). Set
	 * expected->expect_empty to assert the region contains no matching pixel.
	 * Stats carry the measured bbox plus per-axis deltas.
	 * @return SK_UI_IMAGE_ASSERT_OK / FAIL / ERROR.
	 */
	i32 (*cpu_image_assert_bbox)(const sk_ui_cpu_image_t* image, sk_ui_region_t region, const sk_ui_color_match_t* color, const sk_ui_bbox_expected_t* expected,
								 sk_ui_bbox_assert_stats_t* out_stats);

	/**
	 * Measure the dominant colors of @p region: colors within
	 * @p merge_tolerance per channel are merged, then the top @p max_entries
	 * are reported sorted by count descending (ties by color ascending, so
	 * results are deterministic). Cheap introspection for debugging and for
	 * building expectations.
	 * @return 0 on success, SK_UI_IMAGE_ASSERT_ERROR on bad arguments.
	 */
	i32 (*cpu_image_histogram)(const sk_ui_cpu_image_t* image, sk_ui_region_t region, u8 merge_tolerance, u32 max_entries, sk_ui_color_histogram_t* out_hist);

	/**
	 * Assert the dominant colors of @p region match @p expected: every
	 * expected color must be present with measured fraction in
	 * [min_fraction, max_fraction], and the total share of measured colors
	 * matching no expectation must not exceed params->max_unexpected_fraction.
	 * Catches wrong theming (colors appear that should not) and missing
	 * widgets (an expected color disappears). Stats carry the full measured
	 * histogram plus per-expected measured fractions/counts.
	 * @return SK_UI_IMAGE_ASSERT_OK / FAIL / ERROR.
	 */
	i32 (*cpu_image_assert_histogram)(const sk_ui_cpu_image_t* image, sk_ui_region_t region, const sk_ui_hist_assert_params_t* params, const sk_ui_hist_expectation_t* expected,
									  u32 expected_count, sk_ui_hist_assert_stats_t* out_stats);

	/**
	 * Stable FNV-1a 64-bit hash of the @p region bytes (dimensions folded
	 * in, @p seed mixed in). Deterministic across runs and platforms; cheap
	 * enough for per-frame change detection of a region before deciding
	 * whether to run heavier assertions.
	 * @return 0 on success, SK_UI_IMAGE_ASSERT_ERROR on bad arguments.
	 */
	i32 (*cpu_image_region_hash)(const sk_ui_cpu_image_t* image, sk_ui_region_t region, u64 seed, u64* out_hash);

	/**
	 * Assert the region hash equals @p expected_hash (e.g. the value
	 * previously returned by cpu_image_region_hash). Failures log and report
	 * the actual hash, not just a boolean.
	 * @return SK_UI_IMAGE_ASSERT_OK / FAIL / ERROR.
	 */
	i32 (*cpu_image_assert_region_hash)(const sk_ui_cpu_image_t* image, sk_ui_region_t region, u64 expected_hash, u64* out_actual_hash);

	/* ---- code-driven test engine (item registry + frame control; APX-259) ---- */

	/**
	 * Create a headless test engine: harness + empty item registry.
	 * @param desc Optional; NULL uses default 800x600, scale 1, no soft-render.
	 * @return Engine, or NULL on failure.
	 */
	sk_ui_test_engine_t* (*test_engine_create)(const sk_ui_test_engine_desc_t* desc);

	/** Destroy engine, registry, and harness. Safe on NULL. */
	void (*test_engine_destroy)(sk_ui_test_engine_t* engine);

	/** UI context owned by the engine harness (valid until destroy). */
	sk_ui_context_t* (*test_engine_context)(sk_ui_test_engine_t* engine);

	/** Underlying harness (clock, soft-render, font). Valid until destroy. */
	sk_ui_harness_t* (*test_engine_harness)(sk_ui_test_engine_t* engine);

	/**
	 * Advance one frame (harness_step) then rebuild the item registry from
	 * the submitted tree: every live node with a non-empty test id is mapped
	 * by id and by slash-separated id path to its abs rect and state flags.
	 * @return 0 on success, non-zero on pipeline failure.
	 */
	i32 (*test_engine_step)(sk_ui_test_engine_t* engine, f32 delta_seconds);

	/**
	 * Run @p frame_count successful steps. When @p delta_seconds <= 0, uses
	 * 1/60. frame_count 0 is a no-op success.
	 * @return 0 on success, SK_UI_TEST_ERR_STEP if any step fails.
	 */
	i32 (*test_engine_yield_frames)(sk_ui_test_engine_t* engine, u32 frame_count, f32 delta_seconds);

	/**
	 * Step until @p pred returns non-zero or @p max_frames steps are taken.
	 * Evaluates the predicate before the first step and after each step.
	 * On timeout sets last_error to a clear budget message and returns
	 * SK_UI_TEST_ERR_TIMEOUT. Step failure returns SK_UI_TEST_ERR_STEP.
	 * When @p delta_seconds <= 0, uses 1/60.
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_TIMEOUT, or SK_UI_TEST_ERR_STEP.
	 */
	i32 (*test_engine_run_until)(sk_ui_test_engine_t* engine, sk_ui_test_predicate_fn pred, void_ptr_t user, u32 max_frames, f32 delta_seconds);

	/** Stable engine time (sum of step deltas; same as harness_time). */
	f64 (*test_engine_time)(const sk_ui_test_engine_t* engine);

	/** Frame index after the last successful step (0 before first step). */
	u32 (*test_engine_frame_index)(const sk_ui_test_engine_t* engine);

	/**
	 * Look up a registered item by exact test id (from the last successful
	 * step). @return Item snapshot, or NULL if not found / empty registry.
	 */
	const sk_ui_test_item_t* (*test_engine_find_by_id)(const sk_ui_test_engine_t* engine, const_chr_t test_id);

	/**
	 * Look up by slash-separated id path (e.g. "panel-main/btn-go"). When the
	 * path has no '/', falls back to find_by_id for convenience.
	 * @return Item snapshot, or NULL if not found.
	 */
	const sk_ui_test_item_t* (*test_engine_find_by_path)(const sk_ui_test_engine_t* engine, const_chr_t id_path);

	/** Number of items registered after the last successful step. */
	u32 (*test_engine_item_count)(const sk_ui_test_engine_t* engine);

	/**
	 * Item at dense index [0, item_count). NULL if out of range.
	 * Order is preorder of id-bearing nodes under the context root.
	 */
	const sk_ui_test_item_t* (*test_engine_item_at)(const sk_ui_test_engine_t* engine, u32 index);

	/**
	 * Last timeout / step failure message (never NULL; empty when no error).
	 * Valid until the next step that succeeds or destroy.
	 */
	const_chr_t (*test_engine_last_error)(const sk_ui_test_engine_t* engine);

	/* ---- synthetic input (APX-260; always via input_dispatch) ---- */

	/**
	 * Dispatch a raw platform-shaped input event through input_dispatch.
	 * Same ingress as hosts; expands to enter/leave/move/down/up/click/key/text.
	 * @return SK_UI_TEST_OK, or SK_UI_TEST_ERR_INPUT on failure.
	 */
	i32 (*test_engine_input)(sk_ui_test_engine_t* engine, const sk_ui_input_event_t* event);

	/**
	 * Move the pointer to absolute logical coordinates (hit-test + hover).
	 * Does not step a frame; state flags update immediately on the node.
	 * @return SK_UI_TEST_OK or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_mouse_move)(sk_ui_test_engine_t* engine, f32 x, f32 y);

	/**
	 * Press (down != 0) or release (down == 0) a pointer button at the last
	 * mouse position. @p button is sk_ui_pointer_button_t; @p mods is
	 * sk_ui_mod_flags_t. Left-button release over the press target synthesizes
	 * SK_UI_EVENT_CLICK as in production.
	 * @return SK_UI_TEST_OK or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_mouse_button)(sk_ui_test_engine_t* engine, i32 button, i32 down, u32 mods);

	/**
	 * Scroll wheel at the last pointer position (SK_UI_INPUT_WHEEL).
	 * @return SK_UI_TEST_OK or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_scroll_wheel)(sk_ui_test_engine_t* engine, f32 scroll_x, f32 scroll_y, u32 mods);

	/**
	 * Key press (down != 0) or release at the focused node, with modifiers.
	 * @p key is sk_ui_key_t or a host code; @p mods is sk_ui_mod_flags_t.
	 * @return SK_UI_TEST_OK or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_key)(sk_ui_test_engine_t* engine, i32 key, i32 down, u32 mods);

	/**
	 * UTF-8 text entry at the focused node (SK_UI_INPUT_TEXT). Focus first via
	 * test_engine_focus / test_engine_type / a prior click.
	 * @return SK_UI_TEST_OK or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_text)(sk_ui_test_engine_t* engine, const_chr_t text);

	/**
	 * Move the pointer to the absolute center of the live node with @p test_id.
	 * Requires a prior layout (engine step) so hit-test geometry is valid.
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_NOT_FOUND, or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_hover)(sk_ui_test_engine_t* engine, const_chr_t test_id);

	/**
	 * Left-click the item: pointer move to center, button down, button up via
	 * input_dispatch. Does not advance frames — interleave test_engine_step to
	 * observe hover/active style and registry flags between phases, or use
	 * press/release explicitly for multi-frame sequences.
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_NOT_FOUND, or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_click)(sk_ui_test_engine_t* engine, const_chr_t test_id);

	/**
	 * Click with explicit @p button (sk_ui_pointer_button_t) and @p mods.
	 * Only left-button release over the press target yields SK_UI_EVENT_CLICK.
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_NOT_FOUND, or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_click_ex)(sk_ui_test_engine_t* engine, const_chr_t test_id, i32 button, u32 mods);

	/**
	 * Two sequential left-click sequences at the item center (double-click).
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_NOT_FOUND, or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_double_click)(sk_ui_test_engine_t* engine, const_chr_t test_id);

	/**
	 * Pointer button down at the item center without release (for drag / hold).
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_NOT_FOUND, or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_press)(sk_ui_test_engine_t* engine, const_chr_t test_id, i32 button, u32 mods);

	/**
	 * Pointer button up at the last mouse position.
	 * @return SK_UI_TEST_OK or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_release)(sk_ui_test_engine_t* engine, i32 button, u32 mods);

	/**
	 * Drag left button from (x0,y0) to (x1,y1): move, press, intermediate
	 * motion frames (linear samples), release. When @p motion_frames is 0,
	 * performs a single move to the end before release. When
	 * @p delta_seconds > 0, runs test_engine_step after each motion sample so
	 * style/layout/paint see intermediate positions (production multi-frame
	 * drag). When delta_seconds <= 0, only input_dispatch runs between samples.
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_INPUT, or SK_UI_TEST_ERR_STEP.
	 */
	i32 (*test_engine_drag)(sk_ui_test_engine_t* engine, f32 x0, f32 y0, f32 x1, f32 y1, u32 motion_frames, f32 delta_seconds);

	/**
	 * Focus the item by test id then inject UTF-8 text (same as action_type_text).
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_NOT_FOUND, or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_type)(sk_ui_test_engine_t* engine, const_chr_t test_id, const_chr_t text);

	/**
	 * Wheel event over the item center.
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_NOT_FOUND, or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_scroll)(sk_ui_test_engine_t* engine, const_chr_t test_id, f32 scroll_x, f32 scroll_y);

	/**
	 * Set keyboard focus via focus_set (FOCUS_OUT / FOCUS_IN).
	 * @return SK_UI_TEST_OK, SK_UI_TEST_ERR_NOT_FOUND, or SK_UI_TEST_ERR_INPUT.
	 */
	i32 (*test_engine_focus)(sk_ui_test_engine_t* engine, const_chr_t test_id);

	/* ---- docking (retained model + apply; APX-287 / APX-288) ---- */

	/**
	 * Lookup-or-create a named dockspace under @p host (or context_root).
	 * Sets the context "current" dockspace. Idempotent. Does not require a
	 * matching end the same frame; end/apply flush projection.
	 * @return Root dock node, or SK_UI_DOCK_NODE_INVALID on failure (OOM, cap).
	 */
	sk_ui_dock_node_t (*dockspace_begin)(sk_ui_context_t* ctx, sk_ui_node_t host, const_chr_t id, u32 flags);

	/**
	 * Apply **only the current** dockspace if dirty. Safe if begin was never
	 * called (returns 0). Does not apply other named dockspaces.
	 * @return 0 on success, non-zero on apply failure (OOM).
	 */
	i32 (*dockspace_end)(sk_ui_context_t* ctx);

	/**
	 * Explicit constructor (same effect as first dockspace_begin).
	 * @return Root dock node, or SK_UI_DOCK_NODE_INVALID on failure.
	 */
	sk_ui_dock_node_t (*dockspace_create)(sk_ui_context_t* ctx, sk_ui_node_t host, const_chr_t id, u32 flags);

	/**
	 * Project the model for @p dockspace (or current if invalid) onto chrome
	 * widgets. Reparents editor_windows; does not destroy them.
	 * @return 0 on success.
	 */
	i32 (*dockspace_apply)(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace);

	/** Root model node for a named dockspace, or INVALID. */
	sk_ui_dock_node_t (*dockspace_find)(const sk_ui_context_t* ctx, const_chr_t id);

	/** Chrome widget_dock_space node for a dockspace root. */
	sk_ui_node_t (*dockspace_host_node)(const sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace);

	/**
	 * Destroy a named dockspace: reparent every widget=="editor_window"
	 * descendant to stash (then overlay floats stay on the overlay), tear
	 * projected chrome, free the model slot. Same id may begin again.
	 * @return 0 on success, non-zero if id is unknown.
	 */
	i32 (*dockspace_destroy)(sk_ui_context_t* ctx, const_chr_t id);

	/**
	 * Dock @p window_id onto @p node. CENTER/NONE/TAB appends a tab (leaf, or
	 * DFS CENTRAL descendant of a split). LEFT/RIGHT/UP/DOWN split using the
	 * dir→index / model.ratio table. Missing window → pending bind (cap 64).
	 * Runtime NO_SPLIT + edge dir → non-zero. Applies immediately unless a
	 * builder session is open.
	 * @return 0 on success, non-zero if window_id is empty / node dead / no central / flag.
	 */
	i32 (*dock_window_to_node)(sk_ui_context_t* ctx, const_chr_t window_id, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir);

	/**
	 * Tear @p window_id out of the tree as a floating editor_window.
	 * @return 0 on success, non-zero if not docked / unknown id.
	 */
	i32 (*dock_window_undock)(sk_ui_context_t* ctx, const_chr_t window_id);

	/**
	 * Remove the tab. Window is stashed (hidden); host callback fires.
	 * Does not node_destroy the editor_window.
	 * @return 0 on success, non-zero if unknown id.
	 */
	i32 (*dock_tab_close)(sk_ui_context_t* ctx, const_chr_t window_id);

	/** Reorder tabs on a leaf. @return 0 on success. */
	i32 (*dock_tab_reorder)(sk_ui_context_t* ctx, sk_ui_dock_node_t leaf, u32 from_index, u32 to_index);

	/** Activate the tab for @p window_id. @return 0 on success. */
	i32 (*dock_tab_set_active)(sk_ui_context_t* ctx, const_chr_t window_id);

	/** Host hook for close (and optional undock). NULL clears. */
	void (*dock_set_tab_callback)(sk_ui_context_t* ctx, sk_ui_dock_tab_fn fn, void_ptr_t user);

	/* ---- dock builder (ImGui DockBuilder* analogue) ---- */

	/**
	 * Open a builder session on @p dockspace (INVALID = current).
	 * Public mutators other than dock_builder_* mutate the model but do not
	 * apply until finish. Nested begin is a programmer error: **debug assert
	 * only** (no production non-zero).
	 * @return 0 on success.
	 */
	i32 (*dock_builder_begin)(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace);

	/**
	 * Split @p node. @p ratio is the fraction kept by the child toward @p dir
	 * (ImGui size_ratio_for_node_at_dir). Converted to first-child model.ratio
	 * via the dir table (LEFT/UP store r; RIGHT/DOWN store 1-r).
	 * CENTER/NONE/TAB → non-zero. out_* may be NULL. @return 0 on success.
	 */
	i32 (*dock_builder_split_node)(sk_ui_context_t* ctx, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir, f32 ratio, sk_ui_dock_node_t* out_at_dir, sk_ui_dock_node_t* out_opposite);

	/**
	 * Tab-append @p window_id on a **leaf**. If @p node is a split, resolve to
	 * the CENTRAL descendant or return non-zero. No dir parameter — edge
	 * docking is split_node then dock_window on out_at_dir. @return 0 on success.
	 */
	i32 (*dock_builder_dock_window)(sk_ui_context_t* ctx, const_chr_t window_id, sk_ui_dock_node_t node);

	/** Optional stable string id for persist / tests. Copies @p id. */
	i32 (*dock_builder_set_node_id)(sk_ui_context_t* ctx, sk_ui_dock_node_t node, const_chr_t id);

	i32 (*dock_builder_set_node_flags)(sk_ui_context_t* ctx, sk_ui_dock_node_t node, u32 flags);

	/**
	 * Close the session and dockspace_apply. @return 0 on success.
	 */
	i32 (*dock_builder_finish)(sk_ui_context_t* ctx);

	/* ---- queries ---- */

	/**
	 * Leaf (or split host) under logical (x,y), plus suggested drop dir
	 * (CENTER if inside the inner 60%, else nearest edge).
	 * out_dir may be NULL.
	 */
	sk_ui_dock_node_t (*dock_node_at_point)(const sk_ui_context_t* ctx, f32 x, f32 y, sk_ui_dock_dir_t* out_dir);

	/** Leaf that currently owns @p window_id, or INVALID if floating/unknown. */
	sk_ui_dock_node_t (*dock_find_node_for_window)(const sk_ui_context_t* ctx, const_chr_t window_id);

	/**
	 * Copy up to @p max_out window ids from a leaf into @p out_ids (pointers
	 * into model-owned strings; valid until next apply/destroy).
	 * out_count / out_active may be NULL.
	 * @return 0 on success, non-zero if @p leaf is not a live leaf.
	 */
	i32 (*dock_leaf_tabs)(const sk_ui_context_t* ctx, sk_ui_dock_node_t leaf, const_chr_t* out_ids, u32 max_out, u32* out_count, u32* out_active);

	i32 (*dock_node_is_leaf)(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);
	i32 (*dock_node_is_split)(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);
	/**
	 * Split axis. Dead or leaf @p node → SK_UI_DOCK_SPLIT_HORIZONTAL (0).
	 * (i32 booleans match node_alive style.)
	 */
	sk_ui_dock_split_t (*dock_split_get_axis)(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);
	/**
	 * First-child ratio. Dead or leaf @p node → 0.f.
	 */
	f32 (*dock_split_get_ratio)(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);
	/** Write model.ratio (clamped) and apply immediately unless builder open. */
	i32 (*dock_split_set_ratio)(sk_ui_context_t* ctx, sk_ui_dock_node_t node, f32 ratio);
	sk_ui_dock_node_t (*dock_split_child)(const sk_ui_context_t* ctx, sk_ui_dock_node_t node, u32 index);
	sk_ui_node_t (*dock_node_host)(const sk_ui_context_t* ctx, sk_ui_dock_node_t node);

	/** Non-zero if @p window_id is in a leaf (not floating / unknown). */
	i32 (*dock_window_is_docked)(const sk_ui_context_t* ctx, const_chr_t window_id);

	/**
	 * Register a host window id for layout restore reconciliation.
	 * @p default_target is a stable dock-node id (`dock_builder_set_node_id`)
	 * used when the saved layout has no position for this window. NULL or
	 * empty means float at @p default_rect (or 80,60,360,240 when NULL).
	 * Re-registering the same id updates the defaults. Cap
	 * SK_UI_DOCK_WINDOW_REG_MAX.
	 * @return 0 on success, non-zero if @p window_id is empty or the table is full.
	 */
	i32 (*dock_window_register)(sk_ui_context_t* ctx, const_chr_t window_id, const_chr_t default_target, const sk_ui_rect_t* default_rect);

	/* ---- persist (JSON save/restore. PR 5 may add archive pointers.) ---- */

	/**
	 * Emit pretty JSON for the named dockspace into @p out (null-terminated).
	 * Schema: docs/ui-dock-layout-format.md (`sk_ui_dock_layout_t`).
	 * Document root is a single object with integer "version"
	 * (SK_UI_DOCK_LAYOUT_VERSION). Payload: tree structure, split axis/ratio,
	 * per-leaf tab order + active_index, window id strings, and floating
	 * window rects (x/y/w/h/z). @p out_len receives bytes written excluding NUL.
	 * @return 0 on success, non-zero if the dockspace is unknown or @p out is too small.
	 */
	i32 (*dock_layout_save_json)(const sk_ui_context_t* ctx, const_chr_t dockspace_id, char* out, u32 cap, u32* out_len);

	/**
	 * Replace the named dockspace model from JSON. Rejects any version other
	 * than SK_UI_DOCK_LAYOUT_VERSION (unknown/newer and older alike; older
	 * documents are not migrated), logs the encountered version, and leaves
	 * the live tree unchanged so the host can keep the default layout.
	 * Unparseable documents also fail without mutating the live tree.
	 * Serialized window ids that are neither live (`find_by_id`) nor
	 * `dock_window_register`'d are dropped. Emptied leaves and splits
	 * collapse so no empty tab group or empty split remains; leftover
	 * sibling ratios are renormalized. Registered windows missing from the
	 * document fall back to their declared default dock target, or float at
	 * the declared rect when they have no target (or the target collapsed).
	 * Registered-but-not-yet-created ids still become pending binds. Error
	 * if a builder session is open. Does not destroy editor_window nodes
	 * (teardown reparents first).
	 * Rebuilds splits, tab order, the active tab index, and floating window
	 * rects, then applies. Creates the named dockspace if it does not exist
	 * yet (startup restore).
	 * @return 0 on success, non-zero on parse / schema / builder-open error.
	 */
	i32 (*dock_layout_load_json)(sk_ui_context_t* ctx, const_chr_t dockspace_id, const_chr_t json, u32 len);

	/* ---- headless layout solver (APX-288; no renderer / Clay required) ---- */

	/**
	 * Resolve the dock node tree into per-node screen rects for @p space.
	 * Collapses empty nodes first, then writes leaf/split rects and the
	 * 6pt splitter band between siblings. @p dockspace INVALID uses current.
	 * @return 0 on success, non-zero if there is no live dockspace.
	 */
	i32 (*dockspace_layout)(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace, const sk_ui_rect_t* space);

	/**
	 * Last layout rect for @p node (same coordinate space as dockspace_layout).
	 * @return 0 on success, non-zero if @p node is dead.
	 */
	i32 (*dock_node_get_rect)(const sk_ui_context_t* ctx, sk_ui_dock_node_t node, sk_ui_rect_t* out);

	/**
	 * Splitter band rect between the two children of a live split node.
	 * @return 0 on success, non-zero if @p node is not a live split.
	 */
	i32 (*dock_split_get_splitter_rect)(const sk_ui_context_t* ctx, sk_ui_dock_node_t node, sk_ui_rect_t* out);

	/* ---- sample docking demo (fixed layout; APX-292) ---- */

	/**
	 * Register style classes used by the docking demo (fill colors + labels).
	 * Idempotent. Called automatically by sample_dock_demo_build.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*sample_dock_demo_register_styles)(sk_ui_context_t* ctx);

	/**
	 * Build a hardcoded docked workspace under @p parent (or the context root
	 * when @p parent is SK_UI_NODE_INVALID). Always resets the named
	 * "dock-demo" dockspace first — never loads persist JSON / .ini.
	 *
	 * Layout (logical 1280 x 720):
	 *   left leaf          — window "dock-demo-hierarchy"
	 *   right column       — split vertically: "dock-demo-inspector" over
	 *                        "dock-demo-console"
	 *   central leaf       — tabs "dock-demo-scene" then "dock-demo-game"
	 *
	 * Each window content is a distinctly colored, labeled panel.
	 * @return Workspace node, or SK_UI_NODE_INVALID on failure.
	 */
	sk_ui_node_t (*sample_dock_demo_build)(sk_ui_context_t* ctx, sk_ui_node_t parent);

	/**
	 * Logical size used by the docking demo and as the player host default
	 * (1280 x 720). Either out pointer may be NULL.
	 */
	void (*sample_dock_demo_logical_size)(f32* out_width, f32* out_height);

	/* ---- button family (APX-339; editor Button / SmallButton / Invisible /
	 * SelectionButton / BorderedButton / ArrowButton) ---- */

	/**
	 * Compact Button (ImGui SmallButton): auto size, reduced vertical pad.
	 * Label may use a `###id` suffix; @p id wins when non-empty.
	 */
	sk_ui_node_t (*widget_small_button)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);

	/**
	 * Invisible hit target (ImGui InvisibleButton). No chrome. Zero on an
	 * axis is auto. @p flags is a SK_UI_BUTTON_FLAG_MOUSE_* mask (0 = LEFT).
	 */
	sk_ui_node_t (*widget_invisible_button)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id, f32 width, f32 height, u32 flags);

	/**
	 * Toolbar toggle (ImGuiSelectionButton). @p selected paints the selected
	 * look from the caller bool; the widget does not own the mode.
	 * Zero on a size axis is auto.
	 */
	sk_ui_node_t (*widget_selection_button)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, i32 selected, const_chr_t id, f32 width, f32 height);

	/**
	 * Button with a visible gray border (ImGuiBorderedButton).
	 * Zero on a size axis is auto.
	 */
	sk_ui_node_t (*widget_bordered_button)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id, f32 width, f32 height);

	/**
	 * Square arrow chrome (ImGui ArrowButton). @p dir is SK_UI_ARROW_*.
	 * Editor does not call ArrowButton today; factory is here for completeness.
	 */
	sk_ui_node_t (*widget_arrow_button)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 dir, const_chr_t id);

	/**
	 * Explicit size. Zero on an axis means auto (ImGui ImVec2 convention).
	 * @return 0 on success.
	 */
	i32 (*button_set_size)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height);

	/**
	 * Edge-triggered click: 1 once after a press+release over the button
	 * with an allowed mouse button, then clears. Disabled / drag-off → 0.
	 */
	i32 (*button_clicked)(sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Edge-triggered pointer-down on an allowed button; consume-on-read. */
	i32 (*button_pressed)(sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Edge-triggered pointer-up (even if dragged off); consume-on-read. */
	i32 (*button_released)(sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Live SK_UI_STATE_HOVER (does not consume). */
	i32 (*button_is_hovered)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Live SK_UI_STATE_ACTIVE / held (does not consume). */
	i32 (*button_is_active)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Caller-owned selected look (SelectionButton). Does not own the mode. */
	i32 (*button_set_selected)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 selected);
	i32 (*button_get_selected)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Allowed mouse buttons (SK_UI_BUTTON_FLAG_MOUSE_*). 0 treated as LEFT. */
	i32 (*button_set_flags)(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);
	u32 (*button_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/* ---- text family (APX-340; editor Text / TextUnformatted / TextDisabled /
	 * TextColored / TextWrapped / SeparatorText + ImGui wrappers) ---- */

	/**
	 * Plain text (ImGui Text / TextUnformatted). No soft wrap: text clips at
	 * the box width (ImGui Text behaviour). Embedded '\n' still breaks lines.
	 * UTF-8. @p id optional stable test id.
	 */
	sk_ui_node_t (*widget_text)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/** Wrapped text (ImGui TextWrapped): soft-wraps at the box width. */
	sk_ui_node_t (*widget_text_wrapped)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/** Dimmed text (ImGui TextDisabled); Text + SK_UI_STATE_DISABLED dim. */
	sk_ui_node_t (*widget_text_disabled)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/** Text with an explicit linear RGBA colour (ImGui TextColored). */
	sk_ui_node_t (*widget_text_colored)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, sk_ui_color_t color, const_chr_t id);

	/**
	 * Labelled section rule (ImGui SeparatorText): full-width horizontal rule
	 * with the label set into a gap in the rule.
	 */
	sk_ui_node_t (*widget_separator_text)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/**
	 * Bullet + text (ImGui BulletText): small disc in the left padding, then
	 * the text. Editor never calls BulletText (manifest §22.3); factory here
	 * for completeness.
	 */
	sk_ui_node_t (*widget_bullet_text)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/**
	 * "label: value" pair on one row (ImGui LabelText): dimmed label then
	 * value. Editor never calls LabelText (manifest §22.3); factory here for
	 * completeness. Returns the row; children via text_with_label_parts.
	 */
	sk_ui_node_t (*widget_label_text)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t value, const_chr_t id);

	/**
	 * ImGuiTextWithLabel wrapper: dimmed @p label then @p value on one row
	 * (same shape as widget_label_text, editor-facing name).
	 */
	sk_ui_node_t (*widget_text_with_label)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t value, const_chr_t id);

	/**
	 * ImGuiCentralizedText wrapper: @p text centered in the parent region
	 * (EntityTree / Properties empty states). Returns the host; text child
	 * via text_centered_text.
	 */
	sk_ui_node_t (*widget_text_centered)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/** Replace text with a non-null-terminated range [begin, end) (TextUnformatted). */
	i32 (*text_set_text_range)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t begin, const_chr_t end);

	/** Set/get text colour (style COLOR prop; linear RGBA 0..1). */
	i32 (*text_set_color)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_color_t color);
	/** Computed text colour; requires style_resolve to have run. */
	i32 (*text_get_color)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_color_t* out_color);

	/** Set/get disabled (SK_UI_STATE_DISABLED → class disabled variant dims). */
	i32 (*text_set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
	i32 (*text_get_disabled)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Children of a text_with_label / label_text row (out_label, out_value). */
	i32 (*text_with_label_parts)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t* out_label, sk_ui_node_t* out_value);
	/** Text child of a widget_text_centered host. */
	i32 (*text_centered_text)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t* out_text);

	/* ---- checkbox / radio (APX-341; editor Checkbox + flags / radio group) ---- */

	/**
	 * Visible label on the same item as the box (ImGui Checkbox).
	 * Empty / `##id` → box only. `###id` strips the hidden id suffix.
	 */
	i32 (*checkbox_set_label)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label);
	const_chr_t (*checkbox_get_label)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Bind a caller-owned i32* (0/1). Click writes back. External mutation is
	 * pulled on the next style_resolve / harness_step. NULL unbinds.
	 */
	i32 (*checkbox_bind)(sk_ui_context_t* ctx, sk_ui_node_t node, i32* value);

	/**
	 * Bind a flags word + bit mask (ImGui CheckboxFlags). Mixed/indeterminate
	 * when some but not all bits of @p flags_value are set in *@p flags.
	 * Click sets all bits if not fully set, otherwise clears them.
	 */
	i32 (*checkbox_bind_flags)(sk_ui_context_t* ctx, sk_ui_node_t node, i32* flags, i32 flags_value);

	/** Mixed / indeterminate (0/1). Also derived from a flags bind. */
	i32 (*checkbox_set_mixed)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 mixed);
	i32 (*checkbox_get_mixed)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Edge-triggered: 1 once after a value-changing click, then clears.
	 * Programmatic set / external bind mutation do not set this.
	 */
	i32 (*checkbox_changed)(sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*checkbox_set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);

	i32 (*radio_set_label)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label);
	const_chr_t (*radio_get_label)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Bind an int* + this option's value (ImGui RadioButton(label, int*, int)).
	 * Click writes *@p value = @p option and clears sibling radios.
	 * NULL unbinds.
	 */
	i32 (*radio_bind)(sk_ui_context_t* ctx, sk_ui_node_t node, i32* value, i32 option);

	/** Edge-triggered: 1 once after this radio becomes selected by a click. */
	i32 (*radio_changed)(sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*radio_set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);

	/* ---- InputText family (APX-342; editor InputText / Multiline / Search /
	 * ReadOnly / InputFloat / InputFloat3 / InputScalar) ---- */

	/**
	 * Multi-line InputText. Zero on a size axis is auto. Enter inserts a
	 * newline unless ENTER_RETURNS_TRUE is set.
	 */
	sk_ui_node_t (*widget_text_input_multiline)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, f32 width, f32 height, const_chr_t id);

	/** Single-line field with a dimmed placeholder when empty (InputTextWithHint). */
	sk_ui_node_t (*widget_text_input_with_hint)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t hint, const_chr_t id);

	/**
	 * Browser / tree search bar: magnifier + "Search" placeholder.
	 * Same model as widget_text_input.
	 */
	sk_ui_node_t (*widget_search_input)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/** Read-only field (UUID / path / entity id). Still focusable. */
	sk_ui_node_t (*widget_text_input_readonly)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t text, const_chr_t id);

	/** InputScalar: typed numeric text bound to caller storage. */
	sk_ui_node_t (*widget_input_scalar)(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_input_data_type_t type, void_ptr_t data, const_chr_t id);

	/** InputFloat: widget_input_scalar(F32, v). */
	sk_ui_node_t (*widget_input_float)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32* v, const_chr_t id);

	/** InputInt: widget_input_scalar(S32, v). */
	sk_ui_node_t (*widget_input_int)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32* v, const_chr_t id);

	/**
	 * InputFloat3: horizontal row of three F32 scalars bound to v[0..2].
	 * Returns the row; children via input_float3_component.
	 */
	sk_ui_node_t (*widget_input_float3)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32* v, const_chr_t id);

	i32 (*text_input_set_flags)(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);
	u32 (*text_input_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_set_hint)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t hint);
	const_chr_t (*text_input_get_hint)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_set_readonly)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 readonly);
	i32 (*text_input_get_readonly)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_set_password)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 password);
	i32 (*text_input_get_password)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/**
	 * Max UTF-8 bytes excluding the NUL. 0 = unbounded grow (CallbackResize).
	 * Insert / set_text truncate on a codepoint boundary.
	 */
	i32 (*text_input_set_capacity)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 capacity);
	i32 (*text_input_get_capacity)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_get_selection)(const sk_ui_context_t* ctx, sk_ui_node_t node, i32* out_start, i32* out_end);
	i32 (*text_input_set_error)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 show_error);
	i32 (*text_input_get_error)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_set_size)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height);
	i32 (*text_input_set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
	/**
	 * Edge-triggered live-edit return (ImGui InputText). Consume-on-read.
	 * With ENTER_RETURNS_TRUE this is 1 only on Enter, not on each keystroke.
	 */
	i32 (*text_input_changed)(sk_ui_context_t* ctx, sk_ui_node_t node);
	/**
	 * Edge-triggered commit (Enter, or deactivate-after-edit). Consume-on-read.
	 */
	i32 (*text_input_committed)(sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*text_input_set_on_change)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_text_fn fn, void_ptr_t user);

	/**
	 * ImGuiTextFilter::PassFilter: empty filter passes; comma-separated
	 * tokens; a leading '-' excludes; otherwise at least one inclusion
	 * token must match (case-insensitive substring).
	 */
	i32 (*text_filter_pass)(const_chr_t filter, const_chr_t text);

	/** Optional min/max clamp applied when a scalar field commits. NULL = clear. */
	i32 (*input_scalar_set_range)(sk_ui_context_t* ctx, sk_ui_node_t node, const void* p_min, const void* p_max);
	/** Parse the current text into the bound scalar (used by tests / commit). */
	i32 (*input_scalar_apply)(sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Child field of an InputFloat3 row (index 0..2). */
	i32 (*input_float3_component)(const sk_ui_context_t* ctx, sk_ui_node_t row, i32 index, sk_ui_node_t* out_field);

	/* ---- Slider / Drag family (APX-343; editor SliderFloat / SliderInt /
	 * SliderScalar, DragFloat / DragFloat2/3/4 / DragInt) ---- */

	/**
	 * Integer slider (ImGui SliderInt). @p format NULL → "%d". Hidden `##`
	 * labels are supported via slider_set_label. AlwaysClamp by default.
	 */
	sk_ui_node_t (*widget_slider_int)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 v_min, i32 v_max, i32 value, const_chr_t format, const_chr_t id);

	/**
	 * N-component SliderFloat row (1..4). Each child is an independent
	 * slider; edit one without touching the others. @p values may be NULL.
	 */
	sk_ui_node_t (*widget_slider_float_n)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, f32 v_min, f32 v_max, const f32* values, const_chr_t format, const_chr_t id);

	/** N-component SliderInt row (1..4). */
	sk_ui_node_t (*widget_slider_int_n)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, i32 v_min, i32 v_max, const i32* values, const_chr_t format, const_chr_t id);

	/**
	 * Drag scalar (ImGui DragFloat). @p v_speed is value-per-pixel.
	 * min==max==0 is unbounded (material scalars). @p format NULL → "%.3f".
	 */
	sk_ui_node_t (*widget_drag_float)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 v_speed, f32 v_min, f32 v_max, f32 value, const_chr_t format, const_chr_t id);

	/** DragInt. @p format NULL → "%d". */
	sk_ui_node_t (*widget_drag_int)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 v_speed, i32 v_min, i32 v_max, i32 value, const_chr_t format, const_chr_t id);

	/** DragFloat2/3/4: N independent drag children (1..4). */
	sk_ui_node_t (*widget_drag_float_n)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, f32 v_speed, f32 v_min, f32 v_max, const f32* values, const_chr_t format,
										const_chr_t id);

	/** DragInt N-component row (1..4). */
	sk_ui_node_t (*widget_drag_int_n)(sk_ui_context_t* ctx, sk_ui_node_t parent, i32 count, f32 v_speed, i32 v_min, i32 v_max, const i32* values, const_chr_t format,
									  const_chr_t id);

	/** printf format shown on the grab ("%.3f", "%d", "LOD %d", "" = no label). */
	i32 (*slider_set_format)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t format);
	const_chr_t (*slider_get_format)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Snap increment (0 = continuous). Applied after position/speed mapping. */
	i32 (*slider_set_step)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 step);
	f32 (*slider_get_step)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Drag speed (value per pixel). Sliders ignore this. */
	i32 (*slider_set_speed)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 speed);
	f32 (*slider_get_speed)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** SK_UI_SLIDER_FLAG_* mask (AlwaysClamp). */
	i32 (*slider_set_flags)(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);
	u32 (*slider_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Visible label (`##id` hides it). Does not replace the value format. */
	i32 (*slider_set_label)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t label);
	const_chr_t (*slider_get_label)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Edge-triggered: 1 once after a user edit that actually changed the
	 * value (drag or committed text entry), then clears. Programmatic set
	 * does not set this.
	 */
	i32 (*slider_changed)(sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*slider_set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);

	/** Ctrl-click text-entry overlay (1 = editing). */
	i32 (*slider_is_text_input)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*slider_set_text_input)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 on);

	i32 (*slider_set_int_value)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value);
	i32 (*slider_get_int_value)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Apply @p format (or the widget's format) into @p out. */
	i32 (*slider_format_value)(const sk_ui_context_t* ctx, sk_ui_node_t node, char* out, u32 out_cap);

	/** Child of a vector row (index 0..count-1). */
	i32 (*slider_component)(const sk_ui_context_t* ctx, sk_ui_node_t row, i32 index, sk_ui_node_t* out_comp);

	/** Read/write all components of a vector row. @p count is the buffer length. */
	i32 (*slider_set_values)(sk_ui_context_t* ctx, sk_ui_node_t row, const f32* values, i32 count);
	i32 (*slider_get_values)(const sk_ui_context_t* ctx, sk_ui_node_t row, f32* out, i32 count);

	/* ---- child / window / layout family (APX-345; manifest §13) ---- */

	/**
	 * Named dockable window (ImGui Begin + p_open). Same chrome as
	 * widget_editor_window. When @p p_open is non-NULL a close button is
	 * shown; clicking it writes 0 and hides the window.
	 */
	sk_ui_node_t (*widget_window)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t title, const_chr_t id, i32* p_open);

	/**
	 * Fullscreen host (ImGuiBeginFullscreen): fills the parent, no title bar.
	 * Project-launcher overlay.
	 */
	sk_ui_node_t (*widget_fullscreen)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/**
	 * Child region (ImGui BeginChild). @p width/@p height of 0,0 is remaining
	 * size (flex-grow). A positive height with width 0 is a fixed-height
	 * toolbar (full remaining width). @p flags is SK_UI_CHILD_FLAG_*.
	 */
	sk_ui_node_t (*widget_child)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id, f32 width, f32 height, u32 flags);

	/** Scroll content under a child (same as scroll_view_content). */
	sk_ui_node_t (*child_content)(const sk_ui_context_t* ctx, sk_ui_node_t child);
	i32 (*child_set_flags)(sk_ui_context_t* ctx, sk_ui_node_t child, u32 flags);
	u32 (*child_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t child);
	/** Explicit size. Zero on an axis means remaining / auto (ImVec2). */
	i32 (*child_set_size)(sk_ui_context_t* ctx, sk_ui_node_t child, f32 width, f32 height);

	/**
	 * Scroll-to-bottom (ImGui SetScrollHereY(1) / console auto-scroll).
	 * Works on widget_child and widget_scroll_view.
	 */
	i32 (*scroll_view_scroll_to_bottom)(sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Bind / query the close flag (bool* p_open). NULL unbinds (no close). */
	i32 (*editor_window_bind_open)(sk_ui_context_t* ctx, sk_ui_node_t window, i32* p_open);
	i32 (*editor_window_get_open)(const sk_ui_context_t* ctx, sk_ui_node_t window);
	i32 (*editor_window_set_open)(sk_ui_context_t* ctx, sk_ui_node_t window, i32 open);
	/** Close button child, or SK_UI_NODE_INVALID when not closable. */
	sk_ui_node_t (*editor_window_close_button)(const sk_ui_context_t* ctx, sk_ui_node_t window);

	/**
	 * BeginDisabled / EndDisabled stack. Subsequent factory calls inherit
	 * SK_UI_STATE_DISABLED (grey + no input). Nested; End pops one frame.
	 */
	i32 (*begin_disabled)(sk_ui_context_t* ctx, i32 disabled);
	i32 (*end_disabled)(sk_ui_context_t* ctx);
	/** Non-zero if the current disabled stack is active. */
	i32 (*is_disabled)(const sk_ui_context_t* ctx);
	/**
	 * Apply disabled (grey + no input) to @p node and every descendant.
	 * Use on an existing subtree; factories created under begin_disabled
	 * are already marked.
	 */
	i32 (*set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);

	/**
	 * PushID / PopID: prefix subsequently assigned node ids with the stack
	 * (string / int / pointer). Same local id under different scopes is unique.
	 */
	i32 (*push_id)(sk_ui_context_t* ctx, const_chr_t id);
	i32 (*push_id_int)(sk_ui_context_t* ctx, i32 id);
	i32 (*push_id_ptr)(sk_ui_context_t* ctx, const void* ptr);
	i32 (*pop_id)(sk_ui_context_t* ctx);

	/**
	 * SetNextItemWidth. Applied to the next factory-created widget, then
	 * consumed. @p width of SK_UI_ITEM_WIDTH_FILL (-1) is flex-grow fill.
	 * width > 0 is an explicit point width.
	 */
	i32 (*set_next_item_width)(sk_ui_context_t* ctx, f32 width);

	/**
	 * Indent / Unindent. Subsequent factories get extra left margin.
	 * @p width of 0 uses SK_UI_INDENT_DEFAULT.
	 */
	i32 (*indent)(sk_ui_context_t* ctx, f32 width);
	i32 (*unindent)(sk_ui_context_t* ctx, f32 width);

	/**
	 * BeginGroup / EndGroup: shrink-wrap box. Extents after layout are the
	 * group's border box (union of children plus padding).
	 */
	sk_ui_node_t (*widget_group)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
	i32 (*group_get_extents)(const sk_ui_context_t* ctx, sk_ui_node_t group, sk_ui_rect_t* out);

	/**
	 * Horizontal / vertical stack (imgui_stacklayout BeginHorizontal /
	 * BeginVertical). Flex row / column.
	 */
	sk_ui_node_t (*widget_horizontal)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
	sk_ui_node_t (*widget_vertical)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
	/**
	 * Spring: empty spacer with flex_grow = @p weight (default 1). Maps
	 * imgui_stacklayout Spring(weight) onto Clay GROW. Clay GROW is
	 * unweighted, so every live spring shares leftover space equally.
	 */
	sk_ui_node_t (*widget_spring)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 weight, const_chr_t id);

	/* ---- menu family (APX-346; editor BeginMenuBar / BeginMenu / MenuItem) ---- */

	/**
	 * BeginMenu(label, enabled). Disabled menus do not open. Applies to
	 * widget_menu / widget_submenu / widget_dropdown.
	 */
	i32 (*menu_set_enabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 enabled);
	i32 (*menu_get_enabled)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * MenuItem(..., enabled). Disabled items never activate. Same MenuItem
	 * works in a menu_popup / context_menu, not only the bar.
	 */
	i32 (*menu_item_set_enabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 enabled);
	i32 (*menu_item_get_enabled)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * Optional right-aligned shortcut. The editor builds the string
	 * (`Ctrl+S`); the widget only displays it. NULL / empty clears.
	 */
	i32 (*menu_item_set_shortcut)(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t shortcut);
	const_chr_t (*menu_item_get_shortcut)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Logical width used to place the shortcut column (0 if none). */
	f32 (*menu_item_measure_shortcut)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/** After layout: shortcut column in absolute logical units. */
	i32 (*menu_item_get_shortcut_rect)(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out);

	/**
	 * Caller-owned checked mark (MenuItem selected=true). Not the unused
	 * `bool* p_selected` overload.
	 */
	i32 (*menu_item_set_selected)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 selected);
	i32 (*menu_item_get_selected)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Horizontal rule between menu priority groups (ImGui Separator). */
	sk_ui_node_t (*widget_menu_separator)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/**
	 * Edge-triggered item activation: 1 once after a press+release over an
	 * enabled MenuItem, then clears. Disabled → 0.
	 */
	i32 (*menu_item_clicked)(sk_ui_context_t* ctx, sk_ui_node_t node);

	/* ---- popup / modal family (APX-347; manifest §12) ---- */

	/**
	 * Styled 300px context menu (ImGuiBeginPopupMenu). widget=popup_menu.
	 * Closed until popup_open / menu_set_open. Click-outside or
	 * popup_close_current dismisses. Host a right-click by parenting this
	 * under the hit target (or call popup_open).
	 */
	sk_ui_node_t (*widget_popup_menu)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/**
	 * Modal dialog (BeginPopupModal). Title, optional caller-owned @p p_open,
	 * fullscreen dim that blocks input behind. ALWAYS_AUTO_RESIZE shrink-wraps
	 * the dialog; otherwise the body is a fixed child (Save Content /
	 * NoScrollbar). Default-closed until popup_open unless @p p_open is 1.
	 */
	sk_ui_node_t (*widget_modal)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t title, const_chr_t id, i32* p_open, u32 flags);

	/** Title-bar child (widget=modal_title). */
	sk_ui_node_t (*modal_title_bar)(const sk_ui_context_t* ctx, sk_ui_node_t modal);
	/** Body child (widget=modal_body). Auto-size or fixed child+table host. */
	sk_ui_node_t (*modal_body)(const sk_ui_context_t* ctx, sk_ui_node_t modal);
	/** Footer button row (widget=modal_buttons). Caller adds Button children. */
	sk_ui_node_t (*modal_button_row)(const sk_ui_context_t* ctx, sk_ui_node_t modal);
	/** Dialog card child (widget=modal_dialog). */
	sk_ui_node_t (*modal_dialog)(const sk_ui_context_t* ctx, sk_ui_node_t modal);
	/** Dim overlay child (widget=modal_dim). */
	sk_ui_node_t (*modal_dim)(const sk_ui_context_t* ctx, sk_ui_node_t modal);

	i32 (*modal_set_open)(sk_ui_context_t* ctx, sk_ui_node_t modal, i32 open);
	i32 (*modal_get_open)(const sk_ui_context_t* ctx, sk_ui_node_t modal);
	/** Bind / query the close flag (bool* p_open). NULL unbinds (no close X). */
	i32 (*modal_bind_open)(sk_ui_context_t* ctx, sk_ui_node_t modal, i32* p_open);
	u32 (*modal_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t modal);
	i32 (*modal_set_title)(sk_ui_context_t* ctx, sk_ui_node_t modal, const_chr_t title);

	/**
	 * Edge-triggered OpenPopup. Opens @p node (popup_menu / context_menu /
	 * menu_popup / modal, or a menu owner). A second call while already open
	 * is a no-op. Newly opened surfaces push the popup stack.
	 */
	i32 (*popup_open)(sk_ui_context_t* ctx, sk_ui_node_t node);
	/**
	 * CloseCurrentPopup: close the top of the popup stack. Writes 0 through
	 * a bound p_open on modals.
	 */
	i32 (*popup_close_current)(sk_ui_context_t* ctx);
	/** Non-zero if @p node (or its popup child) is open. */
	i32 (*popup_get_open)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * SetItemDefaultFocus. When the owning popup / modal opens, focus moves
	 * to @p node (typically the primary button).
	 */
	i32 (*set_item_default_focus)(sk_ui_context_t* ctx, sk_ui_node_t node);

	/* ---- tab bar family (APX-348; manifest §10) ---- */

	/**
	 * BeginTabItem. Named page tab. Close chrome appears only when @p p_open
	 * is non-NULL; clicking it writes 0. @p flags is SK_UI_TAB_ITEM_FLAG_*.
	 * SET_SELECTED selects this tab and blocks user select while set.
	 */
	sk_ui_node_t (*widget_tab_item)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id, i32* p_open, u32 flags);

	/**
	 * TabItemButton (workspace '+'). Clickable, not a selectable page.
	 * tab_clicked / button_clicked is edge-triggered; selection is unchanged.
	 */
	sk_ui_node_t (*widget_tab_button)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id);

	/** Page body. Hidden unless this tab is the selected page. */
	sk_ui_node_t (*tab_body)(sk_ui_context_t* ctx, sk_ui_node_t tab);
	/** Close button child, or SK_UI_NODE_INVALID when no p_open. */
	sk_ui_node_t (*tab_close_button)(const sk_ui_context_t* ctx, sk_ui_node_t tab);

	/** Bind / query the close flag (bool* p_open). NULL unbinds (no close). */
	i32 (*tab_bind_open)(sk_ui_context_t* ctx, sk_ui_node_t tab, i32* p_open);
	i32 (*tab_get_open)(const sk_ui_context_t* ctx, sk_ui_node_t tab);

	i32 (*tab_set_flags)(sk_ui_context_t* ctx, sk_ui_node_t tab, u32 flags);
	u32 (*tab_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t tab);

	/**
	 * Bind the selected page index (0..n-1 of visible page tabs, -1 if none).
	 * Workspace list is editor-owned; do not rebuild the bar each frame.
	 */
	i32 (*tab_bar_bind_selected)(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, i32* selected);
	i32 (*tab_bar_get_selected)(const sk_ui_context_t* ctx, sk_ui_node_t tab_bar);
	i32 (*tab_bar_set_selected)(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, i32 index);

	/** Edge-triggered click on a TabItemButton ('+'), then clears. */
	i32 (*tab_clicked)(sk_ui_context_t* ctx, sk_ui_node_t tab);

	/* ---- separator / spacing / same-line family (APX-349; manifest §19) ---- */

	/**
	 * ImGui Separator. Horizontal rule; renders VERTICAL when the parent is a
	 * horizontal layout / menu bar, or when SameLine was called immediately
	 * before it (toolbar divider pattern SameLine(); Separator(); SameLine(),
	 * ConsoleWindow.cpp:44-70). @p id optional stable test id.
	 */
	sk_ui_node_t (*widget_separator)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/** Non-zero when @p node renders the vertical (toolbar / menu bar) form. */
	i32 (*separator_get_vertical)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/**
	 * ImGui Spacing: fixed-height vertical spacer (SK_UI_SPACING_DEFAULT).
	 * Transparent; only occupies layout height.
	 */
	sk_ui_node_t (*widget_spacing)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/**
	 * ImGui Dummy: explicit-size box with no chrome. Zero on an axis is legal
	 * (SceneViewWindow.cpp:870 Dummy(0, 2) toolbar spacer).
	 */
	sk_ui_node_t (*widget_dummy)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 width, f32 height, const_chr_t id);

	/**
	 * ImGui SameLine. Positional command: the NEXT widget created under
	 * @p parent joins the previous item's row (offset_from_start_x == 0 →
	 * after the previous item plus @p spacing, default SK_UI_SAMELINE_DEFAULT_GAP;
	 * non-zero → aligned to row start + offset, plus @p spacing when >= 0).
	 * SameLine before the first item is a no-op; an unconsumed SameLine is
	 * cleared at the next layout. @return 0 on success.
	 */
	i32 (*widget_same_line)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 offset_from_start_x, f32 spacing);

	/* ---- tree / collapsing header family (APX-350; manifest §8) ---- */

	/**
	 * SetNextItemOpen. Consumed by the next widget_collapsing_header or the
	 * next item_bind_set_open. SK_UI_COND_ONCE applies only if that id has
	 * not been seeded (ancestors of a selection, settings root).
	 */
	i32 (*set_next_item_open)(sk_ui_context_t* ctx, i32 is_open, u32 cond);

	/**
	 * CollapsingHeader (Properties / Settings sections). Framed full-width
	 * header; no indent push. TRAILING_BUTTON adds ImGuiCollapsingHeaderProps
	 * '...'. Body is hidden while closed. @p flags is SK_UI_TREE_NODE_FLAG_*.
	 */
	sk_ui_node_t (*widget_collapsing_header)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t id, u32 flags);
	i32 (*collapsing_header_get_open)(const sk_ui_context_t* ctx, sk_ui_node_t header);
	i32 (*collapsing_header_set_open)(sk_ui_context_t* ctx, sk_ui_node_t header, i32 open);
	/** Content host under the header (hidden when closed). */
	sk_ui_node_t (*collapsing_header_body)(sk_ui_context_t* ctx, sk_ui_node_t header);
	/** Trailing '...' button, or SK_UI_NODE_INVALID when the flag is off. */
	sk_ui_node_t (*collapsing_header_button)(const sk_ui_context_t* ctx, sk_ui_node_t header);
	/** Edge-triggered click on the trailing button, then clears. */
	i32 (*collapsing_header_button_clicked)(sk_ui_context_t* ctx, sk_ui_node_t header);
	u32 (*collapsing_header_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t header);

	/* ---- table family (APX-351; manifest §9) ---- */

	/**
	 * BeginTable. Retained N-column table. @p columns is 1..SK_UI_TABLE_MAX_COLUMNS.
	 * @p flags is SK_UI_TABLE_FLAG_*. Zero on an outer_size axis is leftover /
	 * auto (Packages / profiler fill leftover height when ScrollY).
	 */
	sk_ui_node_t (*widget_table)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id, i32 columns, u32 flags, f32 outer_width, f32 outer_height);
	/** EndTable: resolve column widths, apply borders / row-bg, finish layout. */
	i32 (*table_end)(sk_ui_context_t* ctx, sk_ui_node_t table);

	/**
	 * TableSetupColumn. Call once per column before the first row. @p flags is
	 * SK_UI_TABLE_COLUMN_FLAG_*. @p init_width_or_weight is a fixed width
	 * (WidthFixed) or stretch weight (WidthStretch); 0 uses the sizing policy.
	 */
	i32 (*table_setup_column)(sk_ui_context_t* ctx, sk_ui_node_t table, const_chr_t label, u32 flags, f32 init_width_or_weight);
	/** TableHeadersRow. Emits a header row from setup-column labels. */
	i32 (*table_headers_row)(sk_ui_context_t* ctx, sk_ui_node_t table);
	/** TableNextRow. Advances the cell cursor to a new row. */
	i32 (*table_next_row)(sk_ui_context_t* ctx, sk_ui_node_t table, u32 row_flags, f32 min_row_height);
	/**
	 * TableNextColumn. Advances to the next cell (wraps to the next row).
	 * @return 1 if the cell is visible, 0 otherwise.
	 */
	i32 (*table_next_column)(sk_ui_context_t* ctx, sk_ui_node_t table);
	/**
	 * TableSetColumnIndex. Jumps to @p column_n on the current row.
	 * @return 1 if the column exists.
	 */
	i32 (*table_set_column_index)(sk_ui_context_t* ctx, sk_ui_node_t table, i32 column_n);
	/** Current cell host (parent widgets into this). */
	sk_ui_node_t (*table_current_cell)(const sk_ui_context_t* ctx, sk_ui_node_t table);
	sk_ui_node_t (*table_get_cell)(const sk_ui_context_t* ctx, sk_ui_node_t table, i32 row, i32 column);
	sk_ui_node_t (*table_get_row)(const sk_ui_context_t* ctx, sk_ui_node_t table, i32 row);
	i32 (*table_get_current_row)(const sk_ui_context_t* ctx, sk_ui_node_t table);
	i32 (*table_get_current_column)(const sk_ui_context_t* ctx, sk_ui_node_t table);

	/** TableGetColumnCount. */
	i32 (*table_get_column_count)(const sk_ui_context_t* ctx, sk_ui_node_t table);
	u32 (*table_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t table);
	i32 (*table_set_flags)(sk_ui_context_t* ctx, sk_ui_node_t table, u32 flags);
	u32 (*table_get_column_flags)(const sk_ui_context_t* ctx, sk_ui_node_t table, i32 column);
	i32 (*table_get_column_width)(const sk_ui_context_t* ctx, sk_ui_node_t table, i32 column, f32* out_width);
	i32 (*table_set_column_width)(sk_ui_context_t* ctx, sk_ui_node_t table, i32 column, f32 width);
	/** Resolve widths for @p avail_width (tests + EndTable). */
	i32 (*table_resolve_column_widths)(sk_ui_context_t* ctx, sk_ui_node_t table, f32 avail_width);

	/**
	 * TableSetBgColor. CellBg + column_n < 0 fills the whole current row
	 * (EntityTree). ROW_BG0 / ROW_BG1 override the alternating stripe.
	 */
	i32 (*table_set_bg_color)(sk_ui_context_t* ctx, sk_ui_node_t table, sk_ui_table_bg_target_t target, sk_ui_color_t color, i32 column_n);

	/** TableSetupScrollFreeze. First @p cols / @p rows stay put when scrolling. */
	i32 (*table_setup_scroll_freeze)(sk_ui_context_t* ctx, sk_ui_node_t table, i32 cols, i32 rows);
	i32 (*table_get_scroll_freeze)(const sk_ui_context_t* ctx, sk_ui_node_t table, i32* out_cols, i32* out_rows);

	/** Scroll body (ScrollX / ScrollY). Invalid when the table does not scroll. */
	sk_ui_node_t (*table_body)(const sk_ui_context_t* ctx, sk_ui_node_t table);
	sk_ui_node_t (*table_header)(const sk_ui_context_t* ctx, sk_ui_node_t table);
	i32 (*table_get_scroll)(const sk_ui_context_t* ctx, sk_ui_node_t table, f32* out_x, f32* out_y);
	i32 (*table_set_scroll)(sk_ui_context_t* ctx, sk_ui_node_t table, f32 scroll_x, f32 scroll_y);

	/**
	 * Bind a caller-owned item array as table rows (§21). Diffs by id; the
	 * table host is not rebuilt. Label goes in column 0.
	 */
	i32 (*table_bind_items)(sk_ui_context_t* ctx, sk_ui_node_t table, sk_ui_item_array_t* items);

	/* ---- selectable family (APX-352; manifest §18) ---- */

	/**
	 * ImGui Selectable: clickable full-row item with a label. @p selected is
	 * a caller-owned look (does not toggle itself). @p flags is
	 * SK_UI_SELECTABLE_FLAG_*. Zero on a size axis is auto. Explicit size is
	 * the project-launcher tile; SpanAvailWidth / SpanAllColumns expand the
	 * hit / highlight rect.
	 */
	sk_ui_node_t (*widget_selectable)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, i32 selected, u32 flags, const_chr_t id, f32 width, f32 height);

	/** Caller-owned selected look. Does not own the mode. */
	i32 (*selectable_set_selected)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 selected);
	i32 (*selectable_get_selected)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Visible but not clickable (flag or explicit). */
	i32 (*selectable_set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 disabled);
	i32 (*selectable_get_disabled)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	i32 (*selectable_set_flags)(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags);
	u32 (*selectable_get_flags)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/** Explicit size. Zero on an axis means auto (ImGui ImVec2 convention). */
	i32 (*selectable_set_size)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 width, f32 height);

	/**
	 * Consume-on-read activate (ImGui Selectable return / "changed").
	 * True once after a press+release over the row. Disabled swallows input.
	 */
	i32 (*selectable_changed)(sk_ui_context_t* ctx, sk_ui_node_t node);
	/**
	 * Consume-on-read double-click. Only set when ALLOW_DOUBLE_CLICK is on
	 * and the second click lands on the same row.
	 */
	i32 (*selectable_double_clicked)(sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*selectable_is_hovered)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*selectable_is_active)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/* ---- combo / list box family (APX-353; manifest §7) ---- */

	/**
	 * Combo(label, int* current_item, items_separated_by_zeros, popup_max_height_in_items).
	 * Zero-separated items: "A\\0B\\0C\\0" (ImGui walk; empty / trailing entries
	 * via combo_set_items_n). Binds @p current_item. Preview is the selected
	 * item, or empty when the index is out of range.
	 */
	sk_ui_node_t (*widget_combo)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, i32* current_item, const_chr_t items_separated_by_zeros,
								 i32 popup_max_height_in_items, const_chr_t id);

	/**
	 * BeginCombo(label, preview_value, flags): preview chrome + custom popup body.
	 * Parent selectables under combo_popup(). Clicking a row closes the popup.
	 */
	sk_ui_node_t (*widget_begin_combo)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, const_chr_t preview_value, u32 flags, const_chr_t id);

	/** Popup body under a combo (menu_popup child). */
	sk_ui_node_t (*combo_popup)(const sk_ui_context_t* ctx, sk_ui_node_t combo);
	i32 (*combo_set_open)(sk_ui_context_t* ctx, sk_ui_node_t combo, i32 open);
	i32 (*combo_get_open)(const sk_ui_context_t* ctx, sk_ui_node_t combo);

	/** Bind / replace the caller int*. NULL unbinds. */
	i32 (*combo_bind)(sk_ui_context_t* ctx, sk_ui_node_t combo, i32* current_item);
	/** Replace zero-separated items (C-string walk until a double-NUL). */
	i32 (*combo_set_items)(sk_ui_context_t* ctx, sk_ui_node_t combo, const_chr_t items_separated_by_zeros);
	/**
	 * Replace items from an explicit blob of @p nbytes. Splits on NUL and
	 * keeps empty and trailing entries (unit-test / packed buffers).
	 */
	i32 (*combo_set_items_n)(sk_ui_context_t* ctx, sk_ui_node_t combo, const_chr_t items, u32 nbytes);
	u32 (*combo_item_count)(const sk_ui_context_t* ctx, sk_ui_node_t combo);
	const_chr_t (*combo_item_text)(const sk_ui_context_t* ctx, sk_ui_node_t combo, i32 index);
	/** Selectable row for a zero-separated item, or SK_UI_NODE_INVALID. */
	sk_ui_node_t (*combo_item_at)(const sk_ui_context_t* ctx, sk_ui_node_t combo, i32 index);

	i32 (*combo_get_selected)(const sk_ui_context_t* ctx, sk_ui_node_t combo);
	i32 (*combo_set_selected)(sk_ui_context_t* ctx, sk_ui_node_t combo, i32 index);
	/** Consume-on-read; true once when the bound index changes from a pick. */
	i32 (*combo_changed)(sk_ui_context_t* ctx, sk_ui_node_t combo);
	i32 (*combo_set_preview)(sk_ui_context_t* ctx, sk_ui_node_t combo, const_chr_t preview);
	const_chr_t (*combo_get_preview)(const sk_ui_context_t* ctx, sk_ui_node_t combo);
	i32 (*combo_set_max_height_in_items)(sk_ui_context_t* ctx, sk_ui_node_t combo, i32 n);
	/** Non-zero when the open popup was flipped above the preview (clipped). */
	i32 (*combo_get_popup_flipped)(const sk_ui_context_t* ctx, sk_ui_node_t combo);
	i32 (*combo_set_disabled)(sk_ui_context_t* ctx, sk_ui_node_t combo, i32 disabled);
	/**
	 * Bind a caller-owned item array as the popup rows (§21 COMBO). Diffs by
	 * id; the combo chrome is not rebuilt.
	 */
	i32 (*combo_bind_items)(sk_ui_context_t* ctx, sk_ui_node_t combo, sk_ui_item_array_t* items);

	/**
	 * BeginListBox(label, size) with height from @p height_in_items when
	 * @p height is 0. Negative item count uses SK_UI_COMBO_DEFAULT_HEIGHT_IN_ITEMS.
	 */
	sk_ui_node_t (*widget_list_box)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t label, f32 width, f32 height, i32 height_in_items, const_chr_t id);
	/** Content host — parent selectables or an item-bind list here. */
	sk_ui_node_t (*list_box_content)(const sk_ui_context_t* ctx, sk_ui_node_t list_box);
	i32 (*list_box_set_height_in_items)(sk_ui_context_t* ctx, sk_ui_node_t list_box, i32 n);
	i32 (*list_box_get_height_in_items)(const sk_ui_context_t* ctx, sk_ui_node_t list_box);
	/** Resolved height in logical px (item-count * SK_UI_COMBO_ITEM_HEIGHT). */
	f32 (*list_box_get_height)(const sk_ui_context_t* ctx, sk_ui_node_t list_box);
	/**
	 * Bind a caller-owned item array as list rows (§21 LIST). Diffs by id;
	 * the list-box chrome is not rebuilt.
	 */
	i32 (*list_box_bind_items)(sk_ui_context_t* ctx, sk_ui_node_t list_box, sk_ui_item_array_t* items);

	/* ---- tooltip family (APX-354; manifest §20) ---- */

	/**
	 * BeginTooltip / EndTooltip. Floating surface shown while the previous
	 * sibling (or tooltip_set_anchor) is hovered. Default delay is
	 * SK_UI_TOOLTIP_DELAY_NORMAL (Project Browser DelayNormal). Parent any
	 * children (text, coloured duration, small table) into the returned node.
	 * The surface follows the cursor, clamps to the viewport, and does not
	 * capture hover or click.
	 */
	sk_ui_node_t (*widget_tooltip)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);
	/** Non-zero while the tooltip is open (after delay, or tooltip_set_visible). */
	i32 (*tooltip_get_visible)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Force open / close (sandbox review). Forced-open skips hover tracking. */
	i32 (*tooltip_set_visible)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 visible);
	/** Hover seconds before show. 0 = next hover tick. Negative is treated as 0. */
	i32 (*tooltip_set_delay)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 seconds);
	f32 (*tooltip_get_delay)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	/** Override the hover item. SK_UI_NODE_INVALID restores previous-sibling. */
	i32 (*tooltip_set_anchor)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t item);
	sk_ui_node_t (*tooltip_get_anchor)(const sk_ui_context_t* ctx, sk_ui_node_t node);

	/* ---- drag-drop payload (APX-356; manifest §17) ---- */

	/**
	 * BeginDragDropSource + SetDragDropPayload on @p item (tree row / field).
	 * @p data is copied. Empty payload: data == NULL && size == 0
	 * (SK_ENTITY_PAYLOAD; selection is implicit).
	 */
	i32 (*drag_drop_source)(sk_ui_context_t* ctx, sk_ui_node_t item, const_chr_t type, const void* data, u32 size, u32 flags);
	/** Preview tooltip text while this source is dragged. */
	i32 (*drag_drop_set_preview)(sk_ui_context_t* ctx, sk_ui_node_t item, const_chr_t text);
	/** BeginDragDropTarget on @p item (previous item / tree row / field). */
	i32 (*drag_drop_target)(sk_ui_context_t* ctx, sk_ui_node_t item, const_chr_t type, u32 flags);
	/**
	 * BeginDragDropTargetCustom: caller-supplied rect + id (Entity Tree
	 * between-row reparent, Scene View full-viewport drop).
	 */
	i32 (*drag_drop_target_custom)(sk_ui_context_t* ctx, const sk_ui_rect_t* bb, const_chr_t id, const_chr_t type, u32 flags);
	i32 (*drag_drop_target_custom_clear)(sk_ui_context_t* ctx, const_chr_t id);
	/**
	 * AcceptDragDropPayload on the hovered node target. Type must match.
	 * Drop-on-self is rejected. Consume-on-read (one delivery).
	 */
	const sk_ui_payload_t* (*drag_drop_accept)(sk_ui_context_t* ctx, const_chr_t type, u32 flags);
	/** Accept on a hovered custom-rect target. */
	const sk_ui_payload_t* (*drag_drop_accept_custom)(sk_ui_context_t* ctx, const_chr_t id, const_chr_t type, u32 flags);
	/** GetDragDropPayload: peek the active payload without accepting. */
	const sk_ui_payload_t* (*drag_drop_get_payload)(const sk_ui_context_t* ctx);
	/** Non-zero while a payload is live (until mouse release). */
	i32 (*drag_drop_is_active)(const sk_ui_context_t* ctx);
	sk_ui_node_t (*drag_drop_get_source)(const sk_ui_context_t* ctx);
	sk_ui_node_t (*drag_drop_get_hovered_target)(const sk_ui_context_t* ctx);
	const_chr_t (*drag_drop_get_hovered_custom_id)(const sk_ui_context_t* ctx);
	/** Peek highlight: @p item is the hovered node target (before accept). */
	i32 (*drag_drop_target_hovered)(const sk_ui_context_t* ctx, sk_ui_node_t item);
	/** Preview overlay node (tooltip text while dragging). */
	sk_ui_node_t (*drag_drop_preview)(const sk_ui_context_t* ctx);
	/** Force-start a drag from an attached source (sandbox / tests). */
	i32 (*drag_drop_begin)(sk_ui_context_t* ctx, sk_ui_node_t item);
	/** Cancel / expire the active payload. */
	i32 (*drag_drop_cancel)(sk_ui_context_t* ctx);
} sk_ui_api_t;

#ifdef __cplusplus
}
#endif
