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
#define SK_UI_CLASS_RADIO "ui-radio"
#define SK_UI_CLASS_TOGGLE "ui-toggle"
#define SK_UI_CLASS_SLIDER "ui-slider"
#define SK_UI_CLASS_RANGE_SLIDER "ui-range-slider"
#define SK_UI_CLASS_PROGRESS "ui-progress"
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
/** Docking / editor window surfaces (APX-235): nodes, splitters, tabs, chrome. */
#define SK_UI_CLASS_DOCK_SPACE "ui-dock-space"
#define SK_UI_CLASS_DOCK_NODE "ui-dock-node"
#define SK_UI_CLASS_SPLITTER "ui-splitter"
#define SK_UI_CLASS_TAB_BAR "ui-tab-bar"
#define SK_UI_CLASS_TAB "ui-tab"
#define SK_UI_CLASS_EDITOR_WINDOW "ui-editor-window"
#define SK_UI_CLASS_WINDOW_TITLE_BAR "ui-window-title-bar"
#define SK_UI_CLASS_WINDOW_CONTENT "ui-window-content"

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
	sk_ui_font_system_t* font_system;	/**< Optional; required for FONT texture cmds. */
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

/** Splitter band thickness along the split main axis (logical points). */
#define SK_UI_DOCK_SPLITTER_PT 6.0f
/** Minimum leftover allocated to each child when the parent span is large enough. */
#define SK_UI_DOCK_NODE_MIN_PT 40.0f
/**
 * On-disk dock layout document version (integer at the JSON root).
 * Load must reject any other value. There is no major.minor.
 */
#define SK_UI_DOCK_LAYOUT_VERSION 1

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
	 * via @p fs (pass sk_filesystem_api() from hosts/tests that link sk-app).
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
	 */
	sk_ui_node_t (*widget_tab_bar)(sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t id);

	/**
	 * Selectable tab (widget=tab). Prop "active" (0/1); click activates and
	 * clears active on sibling tabs under the same tab_bar.
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

	/* ---- persist (JSON writer now; restore is a follow-on. PR 5 may add archive pointers.) ---- */

	/**
	 * Emit pretty JSON for the named dockspace into @p out (null-terminated).
	 * Document root is a single object with integer "version"
	 * (SK_UI_DOCK_LAYOUT_VERSION). Payload: tree structure, split axis/ratio,
	 * per-leaf tab order + active window id, window id strings, and floating
	 * window rects (x/y/w/h/z). @p out_len receives bytes written excluding NUL.
	 * @return 0 on success, non-zero if the dockspace is unknown or @p out is too small.
	 */
	i32 (*dock_layout_save_json)(const sk_ui_context_t* ctx, const_chr_t dockspace_id, char* out, u32 cap, u32* out_len);

	/**
	 * Replace the named dockspace model from JSON. Fails if version !=
	 * SK_UI_DOCK_LAYOUT_VERSION. Missing windows become pending binds. Error if
	 * a builder session is open. Does not destroy editor_window nodes
	 * (teardown reparents first). Restore is not implemented yet (always fails).
	 * @return 0 on success, non-zero on parse / schema error / not implemented.
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
