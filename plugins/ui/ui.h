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
 * Pure CPU — no rendering, no platform window, no GPU.
 * Layout runs in logical units; apply_scale maps to physical pixels.
 */

#include "allocator.h"
#include "app.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_ui_api_t in the app registry. */
#define SK_UI_API_TYPE_ID SK_TYPE_ID("sk.ui_api", 0xc9c0d15c0efdbacbULL, 0x2376391989195a63ULL)

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
