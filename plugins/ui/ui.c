/**
 * @file ui.c
 * @brief Retained-mode UI element tree, dirty tracking, and unit tests.
 *
 * Node storage is a dense generation-tagged slot array with a freelist
 * (same handle spirit as ECS entities). Hierarchy uses parent handles and
 * ordered child id arrays. User ids map through a string hash map. Dirty
 * bits (layout / paint / style) OR onto ancestors so clean parents skip
 * whole subtrees in later passes. Flexbox layout lives in layout.c.
 */

#include "ui.h"
#include "ui_internal.h"

#include "allocator.h"

/* -------------------------------------------------------------------------- */
/* String helpers                                                             */
/* -------------------------------------------------------------------------- */

static char* ui_strdup(const sk_allocator_t* a, const_chr_t src) {
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

static i32 ui_cstr_eq(const_chr_t a, const_chr_t b) {
	if (a == b) {
		return 1;
	}
	if (a == NULL || b == NULL) {
		return 0;
	}
	return strcmp(a, b) == 0 ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/* Slot access (shared with layout.c)                                         */
/* -------------------------------------------------------------------------- */

ui_node_slot_t* ui_slot_mut(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot;
	if (node.index == 0u || node.index >= ctx->slots.count) {
		return NULL;
	}
	slot = &ctx->slots.items[node.index];
	if (slot->alive == 0u || slot->generation != node.generation) {
		return NULL;
	}
	return slot;
}

const ui_node_slot_t* ui_slot(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot;
	if (node.index == 0u || node.index >= ctx->slots.count) {
		return NULL;
	}
	slot = &ctx->slots.items[node.index];
	if (slot->alive == 0u || slot->generation != node.generation) {
		return NULL;
	}
	return slot;
}

void ui_mark_dirty_up(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	ui_node_slot_t* slot;
	while (node.index != 0u) {
		slot = ui_slot_mut(ctx, node);
		if (slot == NULL) {
			return;
		}
		slot->dirty = (u16)(slot->dirty | (u16)flags);
		node = slot->parent;
	}
}

/* -------------------------------------------------------------------------- */
/* Property helpers                                                           */
/* -------------------------------------------------------------------------- */

static void ui_prop_free_value(const sk_allocator_t* a, ui_prop_entry_t* e) {
	if (e->type == SK_UI_PROP_STR && e->data.str_value != NULL) {
		a->free(a->instance, e->data.str_value);
		e->data.str_value = NULL;
	}
	e->type = SK_UI_PROP_NONE;
}

static void ui_prop_entry_free(const sk_allocator_t* a, ui_prop_entry_t* e) {
	ui_prop_free_value(a, e);
	if (e->key != NULL) {
		a->free(a->instance, e->key);
		e->key = NULL;
	}
}

static i32 ui_prop_find(const ui_node_slot_t* slot, const_chr_t key) {
	u32 i;
	if (key == NULL) {
		return -1;
	}
	for (i = 0u; i < slot->props.count; ++i) {
		if (ui_cstr_eq(slot->props.items[i].key, key)) {
			return (i32)i;
		}
	}
	return -1;
}

/* -------------------------------------------------------------------------- */
/* Node lifecycle helpers                                                     */
/* -------------------------------------------------------------------------- */

static void ui_slot_init_empty(ui_node_slot_t* slot, const sk_allocator_t* a) {
	memset(slot, 0, sizeof(*slot));
	sk_array_init(&slot->children, a);
	sk_array_init(&slot->classes, a);
	sk_array_init(&slot->props, a);
	slot->user_data_type = SK_TYPE_ID_ZERO;
	ui_layout_style_init_default(&slot->layout_style);
	ui_node_style_init(slot, a);
	ui_input_node_defaults(slot, SK_UI_NODE_KIND_BOX);
}

static void ui_slot_release_contents(sk_ui_context_t* ctx, ui_node_slot_t* slot) {
	u32 i;
	const sk_allocator_t* a = ctx->allocator;

	if (slot->id != NULL) {
		sk_hash_map_remove(&ctx->id_map, (const_chr_t)slot->id);
		a->free(a->instance, slot->id);
		slot->id = NULL;
	}
	for (i = 0u; i < slot->classes.count; ++i) {
		if (slot->classes.items[i] != NULL) {
			a->free(a->instance, slot->classes.items[i]);
		}
	}
	sk_array_free(&slot->classes);
	for (i = 0u; i < slot->props.count; ++i) {
		ui_prop_entry_free(a, &slot->props.items[i]);
	}
	sk_array_free(&slot->props);
	sk_array_free(&slot->children);
	ui_node_style_release(slot, a);
	ui_widget_release_user_data(ctx, slot);
	slot->user_data = NULL;
	slot->user_data_type = SK_TYPE_ID_ZERO;
	slot->dirty = 0u;
	slot->kind = 0u;
	slot->parent = SK_UI_NODE_INVALID;
}

static i32 ui_detach_from_parent(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	ui_node_slot_t* parent_slot;
	u32 i;
	if (slot == NULL) {
		return -1;
	}
	if (!sk_ui_node_is_valid(slot->parent)) {
		return 0;
	}
	parent_slot = ui_slot_mut(ctx, slot->parent);
	if (parent_slot == NULL) {
		slot->parent = SK_UI_NODE_INVALID;
		return -1;
	}
	for (i = 0u; i < parent_slot->children.count; ++i) {
		if (sk_ui_node_eq(parent_slot->children.items[i], node)) {
			/* Preserve sibling order: shift left. */
			u32 j;
			for (j = i; j + 1u < parent_slot->children.count; ++j) {
				parent_slot->children.items[j] = parent_slot->children.items[j + 1u];
			}
			parent_slot->children.count -= 1u;
			break;
		}
	}
	slot->parent = SK_UI_NODE_INVALID;
	return 0;
}

static i32 ui_is_ancestor(const sk_ui_context_t* ctx, sk_ui_node_t ancestor, sk_ui_node_t node) {
	const ui_node_slot_t* slot;
	while (sk_ui_node_is_valid(node)) {
		if (sk_ui_node_eq(node, ancestor)) {
			return 1;
		}
		slot = ui_slot(ctx, node);
		if (slot == NULL) {
			return 0;
		}
		node = slot->parent;
	}
	return 0;
}

static sk_ui_node_t ui_alloc_node(sk_ui_context_t* ctx, sk_ui_node_kind_t kind) {
	u32 index;
	u32 generation;
	ui_node_slot_t* slot;
	sk_ui_node_t handle;

	if (ctx->freelist.count > 0u) {
		index = sk_array_pop(&ctx->freelist);
		slot = &ctx->slots.items[index];
		/* Generation was bumped on free; reuse it for the reincarnated handle. */
		generation = slot->generation;
		if (generation == 0u) {
			generation = 1u;
		}
	} else {
		index = ctx->slots.count;
		if (sk_array_resize(&ctx->slots, index + 1u) != 0) {
			return SK_UI_NODE_INVALID;
		}
		slot = &ctx->slots.items[index];
		generation = 1u;
	}

	/* Fresh arrays/fields; restore generation after memset in init. */
	ui_slot_init_empty(slot, ctx->allocator);
	slot->generation = generation;
	slot->alive = 1u;
	slot->kind = (u8)kind;
	slot->dirty = (u16)SK_UI_DIRTY_ALL;
	slot->parent = SK_UI_NODE_INVALID;
	slot->id = NULL;
	slot->user_data = NULL;
	slot->user_data_type = SK_TYPE_ID_ZERO;
	ui_input_node_defaults(slot, kind);

	handle.index = index;
	handle.generation = generation;
	ctx->live_count += 1u;
	return handle;
}

/* Tree free re-enters for each child; UI depth is bounded. */
// NOLINTBEGIN(misc-no-recursion)
static void ui_free_node_recursive(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	u32 i;
	u32 child_count;
	sk_ui_node_t* children_copy = NULL;
	const sk_allocator_t* a;

	if (slot == NULL) {
		return;
	}

	/* Drop hover/active/focus/capture before the handle becomes stale. */
	ui_input_on_node_destroy(ctx, node);

	/* Snapshot children first — recursive free mutates the tree. */
	child_count = slot->children.count;
	a = ctx->allocator;
	if (child_count > 0u) {
		children_copy = (sk_ui_node_t*)a->alloc(a->instance, sizeof(sk_ui_node_t) * (size_t)child_count);
		if (children_copy != NULL) {
			memcpy(children_copy, slot->children.items, sizeof(sk_ui_node_t) * (size_t)child_count);
			sk_array_clear(&slot->children);
			for (i = 0u; i < child_count; ++i) {
				ui_free_node_recursive(ctx, children_copy[i]);
			}
			a->free(a->instance, children_copy);
		} else {
			/* OOM fallback: pop children until empty. */
			while (slot->children.count > 0u) {
				sk_ui_node_t c = sk_array_pop(&slot->children);
				ui_free_node_recursive(ctx, c);
			}
		}
	}

	/* Re-fetch after children free (slot address is stable in the array). */
	slot = &ctx->slots.items[node.index];
	ui_slot_release_contents(ctx, slot);
	slot->alive = 0u;
	slot->generation += 1u;
	if (slot->generation == 0u) {
		slot->generation = 1u;
	}
	if (ctx->live_count > 0u) {
		ctx->live_count -= 1u;
	}
	(void)sk_array_push(&ctx->freelist, node.index);
}
// NOLINTEND(misc-no-recursion)

/* -------------------------------------------------------------------------- */
/* API implementations                                                        */
/* -------------------------------------------------------------------------- */

static i32 ui_init_impl(void) {
	return 0;
}

static void ui_shutdown_impl(void) {}

static sk_ui_context_t* ui_context_create(const sk_allocator_t* allocator) {
	sk_ui_context_t* ctx;
	const sk_allocator_t* a = allocator != NULL ? allocator : sk_allocator_default();
	sk_ui_node_t root;

	ctx = (sk_ui_context_t*)a->alloc(a->instance, sizeof(sk_ui_context_t));
	if (ctx == NULL) {
		return NULL;
	}
	memset(ctx, 0, sizeof(*ctx));
	ctx->allocator = a;
	ctx->content_scale_x = 1.0f;
	ctx->content_scale_y = 1.0f;
	ctx->hover = SK_UI_NODE_INVALID;
	ctx->active = SK_UI_NODE_INVALID;
	ctx->focus = SK_UI_NODE_INVALID;
	ctx->pointer_capture = SK_UI_NODE_INVALID;
	ctx->pointer_x = 0.0f;
	ctx->pointer_y = 0.0f;
	ctx->pointer_buttons = 0u;
	ctx->wants_mouse = 0;
	ctx->wants_keyboard = 0;
	sk_array_init(&ctx->slots, a);
	sk_array_init(&ctx->freelist, a);
	if (sk_hash_map_init(&ctx->id_map, a, sk_hash_cstr, sk_equals_cstr) != 0) {
		a->free(a->instance, ctx);
		return NULL;
	}
	ui_style_registry_init(ctx);
	ui_draw_list_store_init(&ctx->draw, a);

	/* Slot 0 is never used — keeps index 0 as the invalid sentinel. */
	if (sk_array_resize(&ctx->slots, 1u) != 0) {
		ui_draw_list_store_shutdown(&ctx->draw);
		ui_style_registry_shutdown(ctx);
		sk_hash_map_free(&ctx->id_map);
		sk_array_free(&ctx->freelist);
		sk_array_free(&ctx->slots);
		a->free(a->instance, ctx);
		return NULL;
	}
	memset(&ctx->slots.items[0], 0, sizeof(ctx->slots.items[0]));

	root = ui_alloc_node(ctx, SK_UI_NODE_KIND_BOX);
	if (!sk_ui_node_is_valid(root)) {
		ui_draw_list_store_shutdown(&ctx->draw);
		ui_style_registry_shutdown(ctx);
		sk_hash_map_free(&ctx->id_map);
		sk_array_free(&ctx->freelist);
		sk_array_free(&ctx->slots);
		a->free(a->instance, ctx);
		return NULL;
	}
	ctx->root = root;
	/* Default widget styles so factories work without hand-styling. */
	(void)ui_widgets_register_defaults_impl(ctx);
	return ctx;
}

static void ui_context_destroy(sk_ui_context_t* ctx) {
	u32 i;
	const sk_allocator_t* a;
	if (ctx == NULL) {
		return;
	}
	a = ctx->allocator;

	/* Free every live slot (including root). Walk indices; freelist may grow. */
	for (i = 1u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		if (slot->alive != 0u) {
			sk_ui_node_t h;
			h.index = i;
			h.generation = slot->generation;
			/* Detach children list first so recursive free doesn't double-walk
			 * via parent links — free this node as a local root. */
			slot->parent = SK_UI_NODE_INVALID;
			ui_free_node_recursive(ctx, h);
		}
	}

	/* Any remaining freelist / empty slots: release array storage. */
	for (i = 0u; i < ctx->slots.count; ++i) {
		ui_node_slot_t* slot = &ctx->slots.items[i];
		/* Alive should be 0; arrays already freed in release. */
		if (slot->children.allocator != NULL) {
			sk_array_free(&slot->children);
		}
		if (slot->classes.allocator != NULL) {
			sk_array_free(&slot->classes);
		}
		if (slot->props.allocator != NULL) {
			sk_array_free(&slot->props);
		}
	}

	sk_array_free(&ctx->slots);
	sk_array_free(&ctx->freelist);
	sk_hash_map_free(&ctx->id_map);
	ui_style_registry_shutdown(ctx);
	ui_draw_list_store_shutdown(&ctx->draw);
	a->free(a->instance, ctx);
}

static sk_ui_node_t ui_context_root(const sk_ui_context_t* ctx) {
	return ctx->root;
}

static u32 ui_context_node_count(const sk_ui_context_t* ctx) {
	return ctx->live_count;
}

static sk_ui_node_t ui_node_create_impl(sk_ui_context_t* ctx, sk_ui_node_kind_t kind, sk_ui_node_t parent) {
	sk_ui_node_t node = ui_alloc_node(ctx, kind);
	ui_node_slot_t* parent_slot;
	ui_node_slot_t* child_slot;
	if (!sk_ui_node_is_valid(node)) {
		return SK_UI_NODE_INVALID;
	}
	if (!sk_ui_node_is_valid(parent)) {
		return node;
	}
	parent_slot = ui_slot_mut(ctx, parent);
	if (parent_slot == NULL) {
		return node; /* orphan */
	}
	child_slot = ui_slot_mut(ctx, node);
	if (sk_array_push(&parent_slot->children, node) != 0) {
		return node;
	}
	child_slot->parent = parent;
	ui_mark_dirty_up(ctx, parent, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	return node;
}

static i32 ui_node_destroy(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	sk_ui_node_t parent;
	if (slot == NULL) {
		return -1;
	}
	if (sk_ui_node_eq(node, ctx->root)) {
		return -1;
	}
	parent = slot->parent;
	(void)ui_detach_from_parent(ctx, node);
	if (sk_ui_node_is_valid(parent)) {
		ui_mark_dirty_up(ctx, parent, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	}
	ui_free_node_recursive(ctx, node);
	return 0;
}

static i32 ui_node_alive(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	return ui_slot(ctx, node) != NULL ? 1 : 0;
}

static sk_ui_node_kind_t ui_node_get_kind(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? (sk_ui_node_kind_t)slot->kind : SK_UI_NODE_KIND_BOX;
}

static i32 ui_node_set_kind(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_kind_t kind) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	slot->kind = (u8)kind;
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_ALL);
	return 0;
}

static sk_ui_node_t ui_node_parent(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? slot->parent : SK_UI_NODE_INVALID;
}

static u32 ui_node_child_count(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? slot->children.count : 0u;
}

static sk_ui_node_t ui_node_child_at(const sk_ui_context_t* ctx, sk_ui_node_t node, u32 index) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL || index >= slot->children.count) {
		return SK_UI_NODE_INVALID;
	}
	return slot->children.items[index];
}

static i32 ui_node_child_index(const sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t child) {
	const ui_node_slot_t* slot = ui_slot(ctx, parent);
	u32 i;
	if (slot == NULL) {
		return -1;
	}
	for (i = 0u; i < slot->children.count; ++i) {
		if (sk_ui_node_eq(slot->children.items[i], child)) {
			return (i32)i;
		}
	}
	return -1;
}

static i32 ui_node_insert_child(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t child, u32 index) {
	ui_node_slot_t* parent_slot;
	ui_node_slot_t* child_slot;
	u32 i;
	u32 insert_at;

	if (sk_ui_node_eq(parent, child)) {
		return -1;
	}
	if (sk_ui_node_eq(child, ctx->root)) {
		return -1;
	}
	parent_slot = ui_slot_mut(ctx, parent);
	child_slot = ui_slot_mut(ctx, child);
	if (parent_slot == NULL || child_slot == NULL) {
		return -1;
	}
	/* Refuse cycles: child cannot be an ancestor of parent. */
	if (ui_is_ancestor(ctx, child, parent)) {
		return -1;
	}

	/* Detach from previous parent if any. */
	if (sk_ui_node_is_valid(child_slot->parent)) {
		sk_ui_node_t old_parent = child_slot->parent;
		if (ui_detach_from_parent(ctx, child) != 0) {
			return -1;
		}
		if (!sk_ui_node_eq(old_parent, parent)) {
			ui_mark_dirty_up(ctx, old_parent, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
		}
		/* parent_slot may still be valid (array stable). */
		parent_slot = ui_slot_mut(ctx, parent);
		child_slot = ui_slot_mut(ctx, child);
		if (parent_slot == NULL || child_slot == NULL) {
			return -1;
		}
	}

	insert_at = index;
	if (insert_at > parent_slot->children.count) {
		insert_at = parent_slot->children.count;
	}
	if (sk_array_reserve(&parent_slot->children, parent_slot->children.count + 1u) != 0) {
		return -1;
	}
	for (i = parent_slot->children.count; i > insert_at; --i) {
		parent_slot->children.items[i] = parent_slot->children.items[i - 1u];
	}
	parent_slot->children.items[insert_at] = child;
	parent_slot->children.count += 1u;
	child_slot->parent = parent;
	ui_mark_dirty_up(ctx, parent, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	return 0;
}

static i32 ui_node_remove_child(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t child) {
	ui_node_slot_t* child_slot = ui_slot_mut(ctx, child);
	if (child_slot == NULL) {
		return -1;
	}
	if (!sk_ui_node_eq(child_slot->parent, parent)) {
		return -1;
	}
	if (ui_detach_from_parent(ctx, child) != 0) {
		return -1;
	}
	ui_mark_dirty_up(ctx, parent, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	return 0;
}

static i32 ui_node_set_child_index(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t child, u32 new_index) {
	ui_node_slot_t* parent_slot = ui_slot_mut(ctx, parent);
	i32 cur;
	u32 i;
	u32 target;
	sk_ui_node_t moved;
	if (parent_slot == NULL || ui_slot_mut(ctx, child) == NULL) {
		return -1;
	}
	cur = ui_node_child_index(ctx, parent, child);
	if (cur < 0) {
		return -1;
	}
	if (parent_slot->children.count == 0u) {
		return -1;
	}
	target = new_index;
	if (target >= parent_slot->children.count) {
		target = parent_slot->children.count - 1u;
	}
	if ((u32)cur == target) {
		return 0;
	}
	moved = parent_slot->children.items[cur];
	if ((u32)cur < target) {
		for (i = (u32)cur; i < target; ++i) {
			parent_slot->children.items[i] = parent_slot->children.items[i + 1u];
		}
	} else {
		for (i = (u32)cur; i > target; --i) {
			parent_slot->children.items[i] = parent_slot->children.items[i - 1u];
		}
	}
	parent_slot->children.items[target] = moved;
	ui_mark_dirty_up(ctx, parent, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	return 0;
}

static i32 ui_node_reparent(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_t new_parent, u32 index) {
	return ui_node_insert_child(ctx, new_parent, node, index);
}

/* ---- id / class ---- */

static i32 ui_node_set_id(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t id) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	const sk_allocator_t* a;
	char* copy;
	if (slot == NULL) {
		return -1;
	}
	a = ctx->allocator;

	/* Clear */
	if (id == NULL || id[0] == '\0') {
		if (slot->id != NULL) {
			sk_hash_map_remove(&ctx->id_map, (const_chr_t)slot->id);
			a->free(a->instance, slot->id);
			slot->id = NULL;
		}
		return 0;
	}

	/* Uniqueness */
	{
		sk_ui_node_t existing = SK_UI_NODE_INVALID;
		if (sk_hash_map_get(&ctx->id_map, id, &existing) == 0) {
			if (!sk_ui_node_eq(existing, node)) {
				return -2;
			}
			/* Same node already has this id (pointer may differ). */
			if (slot->id != NULL && ui_cstr_eq(slot->id, id)) {
				return 0;
			}
		}
	}

	copy = ui_strdup(a, id);
	if (copy == NULL) {
		return -1;
	}
	if (slot->id != NULL) {
		sk_hash_map_remove(&ctx->id_map, (const_chr_t)slot->id);
		a->free(a->instance, slot->id);
		slot->id = NULL;
	}
	if (sk_hash_map_put(&ctx->id_map, (const_chr_t)copy, node) != 0) {
		a->free(a->instance, copy);
		return -1;
	}
	slot->id = copy;
	return 0;
}

static const_chr_t ui_node_get_id(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? slot->id : NULL;
}

static sk_ui_node_t ui_find_by_id(const sk_ui_context_t* ctx, const_chr_t id) {
	sk_ui_node_t out = SK_UI_NODE_INVALID;
	const_chr_t key;
	if (id == NULL || id[0] == '\0') {
		return SK_UI_NODE_INVALID;
	}
	key = id;
	/* Hash map get is const-safe for lookup; the typed macro needs a mutable map
	 * pointer only for its staging field — use the untyped const-friendly API. */
	if (sk_hash_map_get_(&ctx->id_map._hm, &key, &out) != 0) {
		return SK_UI_NODE_INVALID;
	}
	if (ui_slot(ctx, out) == NULL) {
		return SK_UI_NODE_INVALID;
	}
	return out;
}

static i32 ui_node_add_class(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t class_name) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	char* copy;
	u32 i;
	if (slot == NULL) {
		return -1;
	}
	if (class_name == NULL || class_name[0] == '\0') {
		return 0;
	}
	for (i = 0u; i < slot->classes.count; ++i) {
		if (ui_cstr_eq(slot->classes.items[i], class_name)) {
			return 0;
		}
	}
	copy = ui_strdup(ctx->allocator, class_name);
	if (copy == NULL) {
		return -1;
	}
	if (sk_array_push(&slot->classes, copy) != 0) {
		ctx->allocator->free(ctx->allocator->instance, copy);
		return -1;
	}
	/* Dirty flags depend on the registered class property mask. */
	ui_mark_dirty_up(ctx, node, ui_style_dirty_for_class_name(ctx, class_name));
	return 0;
}

static i32 ui_node_remove_class(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t class_name) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	u32 i;
	if (slot == NULL) {
		return -1;
	}
	if (class_name == NULL) {
		return 0;
	}
	for (i = 0u; i < slot->classes.count; ++i) {
		if (ui_cstr_eq(slot->classes.items[i], class_name)) {
			u32 dirty = ui_style_dirty_for_class_name(ctx, class_name);
			ctx->allocator->free(ctx->allocator->instance, slot->classes.items[i]);
			/* Preserve order: shift left. */
			{
				u32 j;
				for (j = i; j + 1u < slot->classes.count; ++j) {
					slot->classes.items[j] = slot->classes.items[j + 1u];
				}
			}
			slot->classes.count -= 1u;
			ui_mark_dirty_up(ctx, node, dirty);
			return 0;
		}
	}
	return 0;
}

static i32 ui_node_has_class(const sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t class_name) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	u32 i;
	if (slot == NULL || class_name == NULL) {
		return 0;
	}
	for (i = 0u; i < slot->classes.count; ++i) {
		if (ui_cstr_eq(slot->classes.items[i], class_name)) {
			return 1;
		}
	}
	return 0;
}

static u32 ui_node_class_count(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? slot->classes.count : 0u;
}

static const_chr_t ui_node_class_at(const sk_ui_context_t* ctx, sk_ui_node_t node, u32 index) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL || index >= slot->classes.count) {
		return NULL;
	}
	return slot->classes.items[index];
}

/* ---- properties ---- */

static i32 ui_node_set_prop_entry(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, sk_ui_prop_type_t type, i32 i32_value, f32 f32_value, const_chr_t str_value) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	i32 found;
	ui_prop_entry_t* e;
	char* key_copy;
	char* str_copy = NULL;
	if (slot == NULL || key == NULL || key[0] == '\0') {
		return -1;
	}
	if (type == SK_UI_PROP_STR) {
		str_copy = ui_strdup(ctx->allocator, str_value != NULL ? str_value : "");
		if (str_copy == NULL) {
			return -1;
		}
	}
	found = ui_prop_find(slot, key);
	if (found >= 0) {
		e = &slot->props.items[found];
		ui_prop_free_value(ctx->allocator, e);
		e->type = type;
		if (type == SK_UI_PROP_I32) {
			e->data.i32_value = i32_value;
		} else if (type == SK_UI_PROP_F32) {
			e->data.f32_value = f32_value;
		} else {
			e->data.str_value = str_copy;
		}
		ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
		return 0;
	}
	key_copy = ui_strdup(ctx->allocator, key);
	if (key_copy == NULL) {
		if (str_copy != NULL) {
			ctx->allocator->free(ctx->allocator->instance, str_copy);
		}
		return -1;
	}
	{
		ui_prop_entry_t neu;
		memset(&neu, 0, sizeof(neu));
		neu.key = key_copy;
		neu.type = type;
		if (type == SK_UI_PROP_I32) {
			neu.data.i32_value = i32_value;
		} else if (type == SK_UI_PROP_F32) {
			neu.data.f32_value = f32_value;
		} else {
			neu.data.str_value = str_copy;
		}
		if (sk_array_push(&slot->props, neu) != 0) {
			ui_prop_entry_free(ctx->allocator, &neu);
			return -1;
		}
	}
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	return 0;
}

static i32 ui_node_set_prop_i32(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, i32 value) {
	return ui_node_set_prop_entry(ctx, node, key, SK_UI_PROP_I32, value, 0.0f, NULL);
}

static i32 ui_node_set_prop_f32(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, f32 value) {
	return ui_node_set_prop_entry(ctx, node, key, SK_UI_PROP_F32, 0, value, NULL);
}

static i32 ui_node_set_prop_str(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, const_chr_t value) {
	return ui_node_set_prop_entry(ctx, node, key, SK_UI_PROP_STR, 0, 0.0f, value);
}

static i32 ui_node_get_prop(const sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, sk_ui_prop_value_t* out) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	i32 found;
	const ui_prop_entry_t* e;
	if (slot == NULL) {
		return -1;
	}
	found = ui_prop_find(slot, key);
	if (found < 0) {
		return -1;
	}
	e = &slot->props.items[found];
	if (out != NULL) {
		out->type = e->type;
		if (e->type == SK_UI_PROP_I32) {
			out->data.i32_value = e->data.i32_value;
		} else if (e->type == SK_UI_PROP_F32) {
			out->data.f32_value = e->data.f32_value;
		} else if (e->type == SK_UI_PROP_STR) {
			out->data.str_value = e->data.str_value;
		} else {
			out->data.str_value = NULL;
		}
	}
	return 0;
}

static i32 ui_node_clear_prop(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	i32 found;
	u32 i;
	u32 idx;
	if (slot == NULL) {
		return -1;
	}
	found = ui_prop_find(slot, key);
	if (found < 0) {
		return 0;
	}
	idx = (u32)found;
	ui_prop_entry_free(ctx->allocator, &slot->props.items[idx]);
	for (i = idx; i + 1u < slot->props.count; ++i) {
		slot->props.items[i] = slot->props.items[i + 1u];
	}
	slot->props.count -= 1u;
	ui_mark_dirty_up(ctx, node, (u32)SK_UI_DIRTY_PAINT);
	return 0;
}

static i32 ui_node_set_user_data(sk_ui_context_t* ctx, sk_ui_node_t node, void_ptr_t data, sk_type_id_t type_id) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	slot->user_data = data;
	slot->user_data_type = type_id;
	return 0;
}

static void_ptr_t ui_node_get_user_data(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? slot->user_data : NULL;
}

static sk_type_id_t ui_node_get_user_data_type(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? slot->user_data_type : SK_TYPE_ID_ZERO;
}

/* ---- dirty ---- */

static i32 ui_node_mark_dirty(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	if (ui_slot_mut(ctx, node) == NULL) {
		return -1;
	}
	ui_mark_dirty_up(ctx, node, flags);
	return 0;
}

static u32 ui_node_get_dirty(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? (u32)slot->dirty : 0u;
}

static i32 ui_node_clear_dirty(sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	slot->dirty = (u16)(slot->dirty & (u16)(~flags));
	return 0;
}

static i32 ui_clear_dirty_walk(sk_ui_context_t* ctx, sk_ui_node_t node, u32 depth, void_ptr_t user) {
	u32 flags = *(const u32*)user;
	(void)depth;
	return ui_node_clear_dirty(ctx, node, flags);
}

static i32 ui_traverse_postorder(sk_ui_context_t* ctx, sk_ui_node_t root, sk_ui_traverse_fn fn, void_ptr_t user);

static i32 ui_node_clear_dirty_subtree(sk_ui_context_t* ctx, sk_ui_node_t root, u32 flags) {
	u32 f = flags;
	if (ui_slot_mut(ctx, root) == NULL) {
		return -1;
	}
	return ui_traverse_postorder(ctx, root, ui_clear_dirty_walk, &f);
}

static i32 ui_node_is_dirty(const sk_ui_context_t* ctx, sk_ui_node_t node, u32 flags) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return 0;
	}
	return ((u32)slot->dirty & flags) != 0u ? 1 : 0;
}

/* ---- traversal ---- */

typedef struct ui_walk_frame_t {
	sk_ui_node_t node;
	u32 depth;
	u32 next_child;
	u8 visited; /* post-order: 1 after children expanded */
} ui_walk_frame_t;

static i32 ui_stack_push(ui_walk_frame_t** stack, u32* sp, u32* cap, i32* heap, const sk_allocator_t* a, ui_walk_frame_t frame) {
	if (*sp >= *cap) {
		u32 new_cap = (*cap) * 2u;
		ui_walk_frame_t* neu = (ui_walk_frame_t*)a->alloc(a->instance, sizeof(ui_walk_frame_t) * (size_t)new_cap);
		if (neu == NULL) {
			return -1;
		}
		memcpy(neu, *stack, sizeof(ui_walk_frame_t) * (size_t)(*sp));
		if (*heap != 0) {
			a->free(a->instance, *stack);
		}
		*stack = neu;
		*cap = new_cap;
		*heap = 1;
	}
	(*stack)[*sp] = frame;
	*sp += 1u;
	return 0;
}

static i32 ui_traverse_preorder(sk_ui_context_t* ctx, sk_ui_node_t root, sk_ui_traverse_fn fn, void_ptr_t user) {
	ui_node_slot_t* slot;
	u32 child_count;
	sk_ui_node_t* kids;
	i32 rc;
	u32 cap = 64u;
	ui_walk_frame_t stack_local[64];
	ui_walk_frame_t* stack = stack_local;
	u32 sp = 0u;
	const sk_allocator_t* a = ctx->allocator;
	i32 heap_stack = 0;
	ui_walk_frame_t fr0;

	if (fn == NULL || ui_slot_mut(ctx, root) == NULL) {
		return -1;
	}

	rc = fn(ctx, root, 0u, user);
	if (rc != 0) {
		return rc;
	}
	fr0.node = root;
	fr0.depth = 0u;
	fr0.next_child = 0u;
	fr0.visited = 0u;
	if (ui_stack_push(&stack, &sp, &cap, &heap_stack, a, fr0) != 0) {
		return -1;
	}

	while (sp > 0u) {
		ui_walk_frame_t* fr = &stack[sp - 1u];
		slot = ui_slot_mut(ctx, fr->node);
		if (slot == NULL) {
			--sp;
			continue;
		}
		child_count = slot->children.count;
		kids = slot->children.items;
		if (fr->next_child >= child_count) {
			--sp;
			continue;
		}
		{
			sk_ui_node_t child = kids[fr->next_child];
			u32 child_depth = fr->depth + 1u;
			ui_walk_frame_t child_fr;
			fr->next_child += 1u;
			rc = fn(ctx, child, child_depth, user);
			if (rc != 0) {
				if (heap_stack != 0) {
					a->free(a->instance, stack);
				}
				return rc;
			}
			child_fr.node = child;
			child_fr.depth = child_depth;
			child_fr.next_child = 0u;
			child_fr.visited = 0u;
			if (ui_stack_push(&stack, &sp, &cap, &heap_stack, a, child_fr) != 0) {
				if (heap_stack != 0) {
					a->free(a->instance, stack);
				}
				return -1;
			}
		}
	}

	if (heap_stack != 0) {
		a->free(a->instance, stack);
	}
	return 0;
}

static i32 ui_traverse_postorder(sk_ui_context_t* ctx, sk_ui_node_t root, sk_ui_traverse_fn fn, void_ptr_t user) {
	u32 cap = 64u;
	ui_walk_frame_t stack_local[64];
	ui_walk_frame_t* stack = stack_local;
	u32 sp = 0u;
	const sk_allocator_t* a = ctx->allocator;
	i32 heap_stack = 0;
	ui_walk_frame_t fr0;
	i32 rc;

	if (fn == NULL || ui_slot_mut(ctx, root) == NULL) {
		return -1;
	}

	fr0.node = root;
	fr0.depth = 0u;
	fr0.next_child = 0u;
	fr0.visited = 0u;
	if (ui_stack_push(&stack, &sp, &cap, &heap_stack, a, fr0) != 0) {
		return -1;
	}

	while (sp > 0u) {
		ui_walk_frame_t* fr = &stack[sp - 1u];
		ui_node_slot_t* slot = ui_slot_mut(ctx, fr->node);
		if (slot == NULL) {
			--sp;
			continue;
		}
		if (fr->next_child < slot->children.count) {
			ui_walk_frame_t child_fr;
			sk_ui_node_t child = slot->children.items[fr->next_child];
			fr->next_child += 1u;
			child_fr.node = child;
			child_fr.depth = fr->depth + 1u;
			child_fr.next_child = 0u;
			child_fr.visited = 0u;
			if (ui_stack_push(&stack, &sp, &cap, &heap_stack, a, child_fr) != 0) {
				if (heap_stack != 0) {
					a->free(a->instance, stack);
				}
				return -1;
			}
			continue;
		}
		/* Children done — visit this node. */
		{
			sk_ui_node_t n = fr->node;
			u32 depth = fr->depth;
			--sp;
			rc = fn(ctx, n, depth, user);
			if (rc != 0) {
				if (heap_stack != 0) {
					a->free(a->instance, stack);
				}
				return rc;
			}
		}
	}

	if (heap_stack != 0) {
		a->free(a->instance, stack);
	}
	return 0;
}

static i32 ui_traverse_dirty_preorder(sk_ui_context_t* ctx, sk_ui_node_t root, u32 dirty_mask, sk_ui_traverse_fn fn, void_ptr_t user) {
	u32 cap = 64u;
	ui_walk_frame_t stack_local[64];
	ui_walk_frame_t* stack = stack_local;
	u32 sp = 0u;
	const sk_allocator_t* a = ctx->allocator;
	i32 heap_stack = 0;
	ui_walk_frame_t fr0;
	i32 rc;
	ui_node_slot_t* root_slot;

	if (fn == NULL) {
		return -1;
	}
	root_slot = ui_slot_mut(ctx, root);
	if (root_slot == NULL) {
		return -1;
	}
	if (((u32)root_slot->dirty & dirty_mask) == 0u) {
		return 0;
	}

	rc = fn(ctx, root, 0u, user);
	if (rc != 0) {
		return rc;
	}
	fr0.node = root;
	fr0.depth = 0u;
	fr0.next_child = 0u;
	fr0.visited = 0u;
	if (ui_stack_push(&stack, &sp, &cap, &heap_stack, a, fr0) != 0) {
		return -1;
	}

	while (sp > 0u) {
		ui_walk_frame_t* fr = &stack[sp - 1u];
		ui_node_slot_t* slot = ui_slot_mut(ctx, fr->node);
		if (slot == NULL) {
			--sp;
			continue;
		}
		if (fr->next_child >= slot->children.count) {
			--sp;
			continue;
		}
		{
			sk_ui_node_t child = slot->children.items[fr->next_child];
			ui_node_slot_t* child_slot;
			fr->next_child += 1u;
			child_slot = ui_slot_mut(ctx, child);
			if (child_slot == NULL) {
				continue;
			}
			if (((u32)child_slot->dirty & dirty_mask) == 0u) {
				continue; /* clean ⇒ skip subtree */
			}
			rc = fn(ctx, child, fr->depth + 1u, user);
			if (rc != 0) {
				if (heap_stack != 0) {
					a->free(a->instance, stack);
				}
				return rc;
			}
			{
				ui_walk_frame_t child_fr;
				child_fr.node = child;
				child_fr.depth = fr->depth + 1u;
				child_fr.next_child = 0u;
				child_fr.visited = 0u;
				if (ui_stack_push(&stack, &sp, &cap, &heap_stack, a, child_fr) != 0) {
					if (heap_stack != 0) {
						a->free(a->instance, stack);
					}
					return -1;
				}
			}
		}
	}

	if (heap_stack != 0) {
		a->free(a->instance, stack);
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* API table                                                                  */
/* -------------------------------------------------------------------------- */

static const sk_ui_api_t ui_api = {
	ui_init_impl,
	ui_shutdown_impl,
	ui_context_create,
	ui_context_destroy,
	ui_context_root,
	ui_context_node_count,
	ui_node_create_impl,
	ui_node_destroy,
	ui_node_alive,
	ui_node_get_kind,
	ui_node_set_kind,
	ui_node_parent,
	ui_node_child_count,
	ui_node_child_at,
	ui_node_child_index,
	ui_node_insert_child,
	ui_node_remove_child,
	ui_node_set_child_index,
	ui_node_reparent,
	ui_node_set_id,
	ui_node_get_id,
	ui_find_by_id,
	ui_node_add_class,
	ui_node_remove_class,
	ui_node_has_class,
	ui_node_class_count,
	ui_node_class_at,
	ui_node_set_prop_i32,
	ui_node_set_prop_f32,
	ui_node_set_prop_str,
	ui_node_get_prop,
	ui_node_clear_prop,
	ui_node_set_user_data,
	ui_node_get_user_data,
	ui_node_get_user_data_type,
	ui_node_mark_dirty,
	ui_node_get_dirty,
	ui_node_clear_dirty,
	ui_node_clear_dirty_subtree,
	ui_node_is_dirty,
	ui_traverse_preorder,
	ui_traverse_postorder,
	ui_traverse_dirty_preorder,
	ui_node_set_layout_style_impl,
	ui_node_get_layout_style_impl,
	ui_node_get_layout_rect_impl,
	ui_node_get_layout_rect_scaled_impl,
	ui_set_measure_fn_impl,
	ui_layout_impl,
	ui_layout_apply_scale_impl,
	ui_layout_get_content_scale_impl,
	ui_style_class_register_impl,
	ui_style_class_set_variant_impl,
	ui_style_class_unregister_impl,
	ui_style_class_has_impl,
	ui_node_set_inline_style_impl,
	ui_node_merge_inline_style_impl,
	ui_node_get_inline_style_impl,
	ui_node_set_state_impl,
	ui_node_get_state_impl,
	ui_node_get_computed_style_impl,
	ui_style_resolve_impl,
	ui_hit_test_impl,
	ui_node_get_abs_rect_impl,
	ui_node_set_callbacks_impl,
	ui_node_get_callbacks_impl,
	ui_node_set_clip_children_impl,
	ui_node_get_clip_children_impl,
	ui_node_set_pointer_events_impl,
	ui_node_get_pointer_events_impl,
	ui_node_set_focusable_impl,
	ui_node_get_focusable_impl,
	ui_focus_set_impl,
	ui_focus_get_impl,
	ui_focus_advance_impl,
	ui_input_dispatch_impl,
	ui_pointer_capture_get_impl,
	ui_pointer_capture_set_impl,
	ui_wants_mouse_impl,
	ui_wants_keyboard_impl,
	ui_paint_impl,
	ui_get_draw_list_impl,
	ui_font_system_create_impl,
	ui_font_system_destroy_impl,
	ui_font_load_path_impl,
	ui_font_load_memory_impl,
	ui_font_destroy_impl,
	ui_font_get_metrics_impl,
	ui_font_glyph_index_impl,
	ui_font_get_glyph_impl,
	ui_font_atlas_page_count_impl,
	ui_font_atlas_get_page_impl,
	ui_font_cache_count_impl,
	ui_font_cache_stats_impl,
	ui_renderer_create_impl,
	ui_renderer_destroy_impl,
	ui_renderer_set_render_pass_impl,
	ui_renderer_prepare_impl,
	ui_renderer_encode_impl,
	ui_widgets_register_defaults_impl,
	ui_set_clipboard_fns_impl,
	ui_widget_panel_impl,
	ui_widget_view_impl,
	ui_widget_label_impl,
	ui_widget_button_impl,
	ui_widget_checkbox_impl,
	ui_widget_slider_impl,
	ui_widget_text_input_impl,
	ui_widget_scroll_view_impl,
	ui_widget_image_impl,
	ui_label_set_text_impl,
	ui_label_get_text_impl,
	ui_label_set_wrap_impl,
	ui_label_set_align_impl,
	ui_button_set_label_impl,
	ui_button_set_disabled_impl,
	ui_checkbox_set_checked_impl,
	ui_checkbox_get_checked_impl,
	ui_checkbox_set_on_change_impl,
	ui_slider_set_value_impl,
	ui_slider_get_value_impl,
	ui_slider_set_range_impl,
	ui_slider_set_on_change_impl,
	ui_text_input_set_text_impl,
	ui_text_input_get_text_impl,
	ui_text_input_get_caret_impl,
	ui_text_input_set_selection_impl,
	ui_text_input_insert_impl,
	ui_text_input_delete_selection_impl,
	ui_text_input_copy_impl,
	ui_text_input_cut_impl,
	ui_text_input_paste_impl,
	ui_scroll_view_content_impl,
	ui_scroll_view_set_scroll_impl,
	ui_scroll_view_get_scroll_impl,
	ui_scroll_view_set_content_size_impl,
	ui_image_set_texture_impl,
};

void sk_ui_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_UI_API_TYPE_ID, (const_ptr_t)&ui_api);
}

const sk_ui_api_t* ui_get_api_table(void) {
	return &ui_api;
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

/* Vendored deps: prove FreeType and stb_rect_pack still link.
 * Unity (via test.h) may include <stdnoreturn.h>, which defines
 * `noreturn` as `_Noreturn`. FreeType's ftstdlib.h then includes
 * <stdlib.h>; on Windows UCRT that uses `__declspec(noreturn)`, which
 * breaks under clang-tidy when the macro expands. */
#ifdef noreturn
#undef noreturn
#endif
#include <ft2build.h>
#include FT_FREETYPE_H
#include "stb_rect_pack.h"

static const sk_ui_api_t* ui_test_api(void) {
	return ui_get_api_table();
}

SK_TEST(ui_stub_init) {
	const i32 rc = ui_init_impl();
	TEST_ASSERT_EQUAL_INT(0, rc);
	ui_shutdown_impl();
}

SK_TEST(ui_thirdparty_deps_link) {
	FT_Library library = NULL;
	const FT_Error ft_err = FT_Init_FreeType(&library);
	stbrp_context pack_ctx;
	stbrp_node nodes[8];
	stbrp_rect rects[1];

	TEST_ASSERT_EQUAL_INT(0, (int)ft_err);
	TEST_ASSERT_NOT_NULL(library);
	FT_Done_FreeType(library);

	stbrp_init_target(&pack_ctx, 64, 64, nodes, 8);
	rects[0].id = 0;
	rects[0].w = 16;
	rects[0].h = 16;
	rects[0].x = 0;
	rects[0].y = 0;
	rects[0].was_packed = 0;
	stbrp_pack_rects(&pack_ctx, rects, 1);
	TEST_ASSERT_TRUE(rects[0].was_packed != 0);
}

SK_TEST(ui_tree_hierarchy_mutation) {
	const sk_ui_api_t* ui = ui_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	sk_ui_node_t d;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	TEST_ASSERT_TRUE(ui->node_alive(ctx, root));
	TEST_ASSERT_EQUAL_UINT(1u, ui->context_node_count(ctx));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, root);
	c = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, root);
	TEST_ASSERT_TRUE(ui->node_alive(ctx, a));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, b));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, c));
	TEST_ASSERT_EQUAL_UINT(3u, ui->node_child_count(ctx, root));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, root, 0u), a));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, root, 1u), b));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, root, 2u), c));
	TEST_ASSERT_EQUAL_INT(1, ui->node_child_index(ctx, root, b));

	/* Reorder: move c to front. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_child_index(ctx, root, c, 0u));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, root, 0u), c));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, root, 1u), a));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, root, 2u), b));

	/* Nested insert + reparent. */
	d = ui->node_create(ctx, SK_UI_NODE_KIND_IMAGE, a);
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(ctx, d), a));
	TEST_ASSERT_EQUAL_UINT(1u, ui->node_child_count(ctx, a));
	TEST_ASSERT_EQUAL_INT(0, ui->node_reparent(ctx, d, b, 0u));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(ctx, d), b));
	TEST_ASSERT_EQUAL_UINT(0u, ui->node_child_count(ctx, a));
	TEST_ASSERT_EQUAL_UINT(1u, ui->node_child_count(ctx, b));

	/* Detach without destroy. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_remove_child(ctx, b, d));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->node_parent(ctx, d)));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, d));
	TEST_ASSERT_EQUAL_INT(0, ui->node_insert_child(ctx, root, d, 1u));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, root, 1u), d));

	/* Cannot reparent root; cannot create cycle. */
	TEST_ASSERT_TRUE(ui->node_insert_child(ctx, a, root, 0u) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->node_insert_child(ctx, a, b, 0u)); /* b under a */
	TEST_ASSERT_TRUE(ui->node_insert_child(ctx, b, a, 0u) != 0);	/* cycle */

	ui->context_destroy(ctx);
}

SK_TEST(ui_tree_id_uniqueness_and_lookup) {
	const sk_ui_api_t* ui = ui_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_prop_value_t prop;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);

	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, a, "panel-main"));
	TEST_ASSERT_EQUAL_STRING("panel-main", ui->node_get_id(ctx, a));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->find_by_id(ctx, "panel-main"), a));

	/* Duplicate id rejected. */
	TEST_ASSERT_EQUAL_INT(-2, ui->node_set_id(ctx, b, "panel-main"));
	TEST_ASSERT_NULL(ui->node_get_id(ctx, b));

	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, b, "panel-side"));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->find_by_id(ctx, "panel-side"), b));

	/* Reassign a to a new id frees the old map entry. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, a, "panel-top"));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->find_by_id(ctx, "panel-main")));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->find_by_id(ctx, "panel-top"), a));

	/* Classes */
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, a, "primary"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, a, "elevated"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, a, "primary")); /* idempotent */
	TEST_ASSERT_EQUAL_UINT(2u, ui->node_class_count(ctx, a));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, a, "primary"));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, a, "elevated"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_remove_class(ctx, a, "primary"));
	TEST_ASSERT_FALSE(ui->node_has_class(ctx, a, "primary"));
	TEST_ASSERT_EQUAL_UINT(1u, ui->node_class_count(ctx, a));

	/* Properties */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, a, "tab-index", 3));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_f32(ctx, a, "opacity", 0.5f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, a, "label", "Hello"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, a, "tab-index", &prop));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_PROP_I32, (int)prop.type);
	TEST_ASSERT_EQUAL_INT(3, prop.data.i32_value);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, a, "label", &prop));
	TEST_ASSERT_EQUAL_STRING("Hello", prop.data.str_value);
	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_prop(ctx, a, "opacity"));
	TEST_ASSERT_TRUE(ui->node_get_prop(ctx, a, "opacity", &prop) != 0);

	/* Clear id */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, b, NULL));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->find_by_id(ctx, "panel-side")));

	ui->context_destroy(ctx);
}

SK_TEST(ui_tree_destroy_subtree_no_dangling) {
	const sk_ui_api_t* ui = ui_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t parent;
	sk_ui_node_t child;
	sk_ui_node_t grand;
	sk_ui_node_t sibling;
	sk_ui_node_t stale_parent;
	sk_ui_node_t stale_grand;
	u32 before;
	u32 after;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	parent = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	child = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	grand = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, child);
	sibling = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);

	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, parent, "parent"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_id(ctx, grand, "grand"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, child, "row"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, grand, "text", "x"));

	before = ui->context_node_count(ctx);
	TEST_ASSERT_EQUAL_UINT(5u, before); /* root + parent + child + grand + sibling */

	stale_parent = parent;
	stale_grand = grand;
	TEST_ASSERT_EQUAL_INT(0, ui->node_destroy(ctx, parent));

	/* Subtree gone; handles dangling. */
	TEST_ASSERT_FALSE(ui->node_alive(ctx, stale_parent));
	TEST_ASSERT_FALSE(ui->node_alive(ctx, child));
	TEST_ASSERT_FALSE(ui->node_alive(ctx, stale_grand));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, sibling));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, root));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->find_by_id(ctx, "parent")));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->find_by_id(ctx, "grand")));

	after = ui->context_node_count(ctx);
	TEST_ASSERT_EQUAL_UINT(2u, after); /* root + sibling */
	TEST_ASSERT_EQUAL_UINT(1u, ui->node_child_count(ctx, root));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_child_at(ctx, root, 0u), sibling));

	/* Cannot destroy root. */
	TEST_ASSERT_TRUE(ui->node_destroy(ctx, root) != 0);

	/* Slot recycle: new node reuses index with bumped generation. */
	{
		sk_ui_node_t neu = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
		TEST_ASSERT_TRUE(ui->node_alive(ctx, neu));
		TEST_ASSERT_FALSE(ui->node_alive(ctx, stale_parent));
		if (neu.index == stale_parent.index) {
			TEST_ASSERT_TRUE(neu.generation != stale_parent.generation);
		}
	}

	ui->context_destroy(ctx);
}

typedef struct {
	u32 visit_count;
	u32 dirty_visit_count;
	u32 layout_hits;
	u32 paint_hits;
} ui_walk_stats_t;

static i32 ui_count_visit(sk_ui_context_t* ctx, sk_ui_node_t node, u32 depth, void_ptr_t user) {
	ui_walk_stats_t* s = (ui_walk_stats_t*)user;
	(void)ctx;
	(void)node;
	(void)depth;
	s->visit_count += 1u;
	return 0;
}

static i32 ui_count_dirty(sk_ui_context_t* ctx, sk_ui_node_t node, u32 depth, void_ptr_t user) {
	ui_walk_stats_t* s = (ui_walk_stats_t*)user;
	(void)depth;
	s->dirty_visit_count += 1u;
	if (ui_api.node_is_dirty(ctx, node, (u32)SK_UI_DIRTY_LAYOUT)) {
		s->layout_hits += 1u;
	}
	if (ui_api.node_is_dirty(ctx, node, (u32)SK_UI_DIRTY_PAINT)) {
		s->paint_hits += 1u;
	}
	return 0;
}

SK_TEST(ui_tree_dirty_propagation_and_clear) {
	const sk_ui_api_t* ui = ui_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t mid;
	sk_ui_node_t leaf;
	sk_ui_node_t clean_branch;
	ui_walk_stats_t stats;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	mid = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	leaf = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, mid);
	clean_branch = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);

	/* New nodes start fully dirty; clear everything to a known clean state. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_dirty_subtree(ctx, root, (u32)SK_UI_DIRTY_ALL));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_ALL));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, leaf, (u32)SK_UI_DIRTY_ALL));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, clean_branch, (u32)SK_UI_DIRTY_ALL));

	/* Paint-only on leaf propagates paint (not layout) to ancestors. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_mark_dirty(ctx, leaf, (u32)SK_UI_DIRTY_PAINT));
	TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, leaf, (u32)SK_UI_DIRTY_PAINT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, leaf, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, mid, (u32)SK_UI_DIRTY_PAINT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, mid, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_PAINT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_LAYOUT));
	/* Sibling branch stays clean. */
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, clean_branch, (u32)SK_UI_DIRTY_PAINT));

	/* Layout dirty on leaf propagates layout up. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_mark_dirty(ctx, leaf, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, leaf, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, mid, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, clean_branch, (u32)SK_UI_DIRTY_LAYOUT));

	/* Dirty preorder visits only the dirty spine (root, mid, leaf) for layout. */
	memset(&stats, 0, sizeof(stats));
	TEST_ASSERT_EQUAL_INT(0, ui->traverse_dirty_preorder(ctx, root, (u32)SK_UI_DIRTY_LAYOUT, ui_count_dirty, &stats));
	TEST_ASSERT_EQUAL_UINT(3u, stats.dirty_visit_count);
	TEST_ASSERT_EQUAL_UINT(3u, stats.layout_hits);

	/* Full preorder still sees every node. */
	memset(&stats, 0, sizeof(stats));
	TEST_ASSERT_EQUAL_INT(0, ui->traverse_preorder(ctx, root, ui_count_visit, &stats));
	TEST_ASSERT_EQUAL_UINT(4u, stats.visit_count);

	/* Simulated layout pass clears layout flags on the dirty spine. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_dirty_subtree(ctx, root, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, mid, (u32)SK_UI_DIRTY_LAYOUT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, leaf, (u32)SK_UI_DIRTY_LAYOUT));
	/* Paint remains until paint pass. */
	TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, leaf, (u32)SK_UI_DIRTY_PAINT));
	TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_PAINT));

	TEST_ASSERT_EQUAL_INT(0, ui->node_clear_dirty_subtree(ctx, root, (u32)SK_UI_DIRTY_PAINT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_PAINT));
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, leaf, (u32)SK_UI_DIRTY_PAINT));

	/* Structural mutation dirties ancestors for layout+paint. */
	{
		sk_ui_node_t extra = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, mid);
		TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, mid, (u32)SK_UI_DIRTY_LAYOUT));
		TEST_ASSERT_TRUE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_LAYOUT));
		TEST_ASSERT_TRUE(ui->node_alive(ctx, extra));
	}

	ui->context_destroy(ctx);
}

SK_TEST(ui_tree_traverse_postorder_counts) {
	const sk_ui_api_t* ui = ui_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	ui_walk_stats_t stats;

	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	(void)ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	(void)ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);

	memset(&stats, 0, sizeof(stats));
	TEST_ASSERT_EQUAL_INT(0, ui->traverse_postorder(ctx, root, ui_count_visit, &stats));
	TEST_ASSERT_EQUAL_UINT(3u, stats.visit_count);

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
