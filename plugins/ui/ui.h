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
 * lifecycle, layout vs paint dirty flags with ancestor propagation, and
 * tree traversals for later layout/style/input/paint passes.
 *
 * Pure CPU — no rendering, no platform window, no GPU.
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
