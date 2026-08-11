#pragma once

/**
 * @file ui.h
 * @brief Retained-mode UI module API.
 *
 * Implemented by the sk-ui plugin (SHARED, statically linked sk-core).
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
/*  Font / glyph atlas (CPU FreeType + stb_rect_pack; no GPU upload)  */
/* ------------------------------------------------------------------ */

/**
 * Opaque font face (one TTF/OTF). Owned by the font system that created it.
 * Destroy with font_destroy, or when the font system is destroyed.
 */
typedef struct sk_ui_font_t sk_ui_font_t;

/**
 * Opaque font system: FreeType library, loaded faces, R8 atlas pages, glyph cache.
 * CPU-side atlas; GPU upload is owned by sk_ui_renderer_t.
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
 */
typedef struct sk_ui_font_metrics_t {
	f32 ascent;
	f32 descent;
	f32 line_height;
	f32 pixel_size;
} sk_ui_font_metrics_t;

/**
 * Per-glyph metrics and atlas placement after rasterization (physical pixels).
 * UV rect is normalized [0,1] within the atlas page bitmap.
 */
typedef struct sk_ui_glyph_t {
	u32 glyph_index;
	f32 advance_x;	/**< Horizontal advance in pixels. */
	f32 advance_y;	/**< Vertical advance (usually 0 for horizontal layout). */
	f32 bearing_x;	/**< Left side bearing (bitmap left of pen). */
	f32 bearing_y;	/**< Top side bearing (bitmap top above baseline). */
	u32 width;		/**< Bitmap width in pixels (0 for empty/space). */
	u32 height;		/**< Bitmap height in pixels. */
	f32 u0;			/**< Atlas UV left. */
	f32 v0;			/**< Atlas UV top. */
	f32 u1;			/**< Atlas UV right. */
	f32 v1;			/**< Atlas UV bottom. */
	u32 page_index; /**< Index into font_system atlas pages. */
} sk_ui_glyph_t;

/**
 * One CPU-side atlas page (R8 coverage). pixels is owned by the font system;
 * valid until the system is destroyed or that page is grown (generation bumps).
 */
typedef struct sk_ui_atlas_page_t {
	u32 width;
	u32 height;
	u32 generation;	  /**< Increments when the page bitmap is reallocated/grown. */
	const u8* pixels; /**< R8, row-major, pitch == width. NULL if empty. */
} sk_ui_atlas_page_t;

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
 * FONT = R8 atlas page (texture_id = page_index). IMAGE = host texture
 * (texture_id = host-defined id from the image node property).
 */
typedef enum sk_ui_draw_texture_kind_t {
	SK_UI_DRAW_TEX_NONE = 0,
	SK_UI_DRAW_TEX_FONT = 1,
	SK_UI_DRAW_TEX_IMAGE = 2,
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
 * All fields may be NULL / zero when unused.
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

/** Widget bool change (checkbox). */
typedef void (*sk_ui_widget_bool_fn)(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user);
/** Widget float change (slider). */
typedef void (*sk_ui_widget_float_fn)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user);

/** Default style class names (stable automation surface). */
#define SK_UI_CLASS_PANEL "ui-panel"
#define SK_UI_CLASS_VIEW "ui-view"
#define SK_UI_CLASS_LABEL "ui-label"
#define SK_UI_CLASS_BUTTON "ui-button"
#define SK_UI_CLASS_CHECKBOX "ui-checkbox"
#define SK_UI_CLASS_SLIDER "ui-slider"
#define SK_UI_CLASS_TEXT_INPUT "ui-text-input"
#define SK_UI_CLASS_SCROLL_VIEW "ui-scroll-view"
#define SK_UI_CLASS_IMAGE "ui-image"
/** Menu surfaces (APX-234): bar, items, floating popups / dropdowns / context. */
#define SK_UI_CLASS_MENU_BAR "ui-menu-bar"
#define SK_UI_CLASS_MENU "ui-menu"
#define SK_UI_CLASS_MENU_ITEM "ui-menu-item"
#define SK_UI_CLASS_MENU_POPUP "ui-menu-popup"
#define SK_UI_CLASS_DROPDOWN "ui-dropdown"
#define SK_UI_CLASS_CONTEXT_MENU "ui-context-menu"
#define SK_UI_CLASS_SUBMENU "ui-submenu"

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
	sk_ui_font_system_t* font_system;	/**< Optional; required for FONT texture cmds. */
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

	/* ---- font system (FreeType raster + stb_rect_pack atlas, CPU only) ---- */

	/**
	 * Create a font system with an initial atlas page of @p page_width x @p page_height
	 * (R8). Pass 0,0 for default 512x512. Owns FreeType state and glyph cache.
	 * @param allocator Optional; NULL uses the process default.
	 * @return New system, or NULL on failure.
	 */
	sk_ui_font_system_t* (*font_system_create)(const sk_allocator_t* allocator, u32 page_width, u32 page_height);

	/**
	 * Destroy a font system, every font it owns, atlas pages, and glyph cache.
	 * Safe on NULL.
	 */
	void (*font_system_destroy)(sk_ui_font_system_t* system);

	/**
	 * Load a TTF/OTF from @p path using the engine filesystem API (open/read/close).
	 * Bytes are copied into the font; the file is not kept open.
	 * @param system Font system (must not be NULL).
	 * @param fs     Filesystem table (e.g. sk_filesystem_api()). Must not be NULL.
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
	 * Destroy one font face and drop its glyphs from the cache. Atlas pages keep
	 * packed bitmaps (UV holes are acceptable). Safe on NULL.
	 */
	void (*font_destroy)(sk_ui_font_t* font);

	/**
	 * Font metrics at @p pixel_size (from sk_ui_font_pixel_size or equivalent).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*font_get_metrics)(const sk_ui_font_t* font, u32 pixel_size, sk_ui_font_metrics_t* out);

	/**
	 * Map a Unicode codepoint to a glyph index (0 if missing / .notdef).
	 */
	u32 (*font_glyph_index)(const sk_ui_font_t* font, u32 codepoint);

	/**
	 * Get a glyph from the cache or rasterize + pack it.
	 * Cache key: (font, pixel_size, glyph_index). On miss: FreeType render,
	 * stb_rect_pack into the current atlas page; grow the page or add a page
	 * when full. Empty glyphs (space) succeed with width/height 0 and no UV.
	 * @return 0 on success, non-zero on failure (OOM, FreeType error, etc.).
	 */
	i32 (*font_get_glyph)(sk_ui_font_system_t* system, sk_ui_font_t* font, u32 pixel_size, u32 glyph_index, sk_ui_glyph_t* out);

	/** Number of atlas pages currently allocated. */
	u32 (*font_atlas_page_count)(const sk_ui_font_system_t* system);

	/**
	 * Snapshot one atlas page (CPU R8). @p out->pixels is system-owned.
	 * @return 0 on success, non-zero if index is out of range.
	 */
	i32 (*font_atlas_get_page)(const sk_ui_font_system_t* system, u32 page_index, sk_ui_atlas_page_t* out);

	/**
	 * Glyph cache entry count (for tests / diagnostics).
	 */
	u32 (*font_cache_count)(const sk_ui_font_system_t* system);

	/**
	 * Cumulative cache hits and misses since system create (for tests).
	 * Either out pointer may be NULL.
	 */
	void (*font_cache_stats)(const sk_ui_font_system_t* system, u32* out_hits, u32* out_misses);

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

	/* ---- v1 widgets (compose tree + default styles + behavior) ---- */

	/**
	 * Register default style classes for the v1 widget set (panel, view, label,
	 * button, checkbox, slider, text_input, scroll_view, image, menu surfaces)
	 * including hover/active/focused/disabled variants. Idempotent. Called
	 * automatically from context_create; safe to call again after unregistering
	 * a class.
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

	/** Horizontal slider clamped to [min_v, max_v]. */
	sk_ui_node_t (*widget_slider)(sk_ui_context_t* ctx, sk_ui_node_t parent, f32 min_v, f32 max_v, f32 value, const_chr_t id);

	/** Single-line text field with caret/selection editing. */
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

	i32 (*slider_set_value)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value);
	f32 (*slider_get_value)(const sk_ui_context_t* ctx, sk_ui_node_t node);
	i32 (*slider_set_range)(sk_ui_context_t* ctx, sk_ui_node_t node, f32 min_v, f32 max_v);
	i32 (*slider_set_on_change)(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_widget_float_fn fn, void_ptr_t user);

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
} sk_ui_api_t;

/**
 * Register the static sk_ui_api_t on the app context.
 * Called from sk_plugin_entry_point.
 * @param context App context (must not be NULL).
 * @param app_api App module table (must not be NULL).
 */
void sk_ui_init(sk_app_context_t* context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
