/**
 * @file dock.c
 * @brief Retained dock node tree, public dockspace API, and headless layout.
 *
 * Binary split/leaf model owned by the UI context (APX-287). Layout resolves
 * a dockspace rect into per-node screen boxes plus 6pt splitter bands without
 * Clay or a renderer (APX-288). Chrome projection is a later apply step.
 */

#include "ui_internal.h"

#include "allocator.h"
#include "logger.h"

#include <assert.h>
#include <string.h>

#define UI_DOCK_WALK_MAX 128u
#define UI_DOCK_EDGE_RATIO 0.25f
#define UI_DOCK_INNER_FRAC 0.6f

enum { UI_DOCK_KIND_LEAF_U8 = (u8)UI_DOCK_KIND_LEAF, UI_DOCK_KIND_SPLIT_U8 = (u8)UI_DOCK_KIND_SPLIT };

/* -------------------------------------------------------------------------- */
/* Slot access                                                                */
/* -------------------------------------------------------------------------- */

static ui_dock_slot_t* ui_dock_slot_mut(sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	ui_dock_slot_t* slot;
	if (node.index == 0u || node.index >= ctx->dock_slots.count) {
		return NULL;
	}
	slot = &ctx->dock_slots.items[node.index];
	if (slot->alive == 0u || slot->generation != node.generation) {
		return NULL;
	}
	return slot;
}

static const ui_dock_slot_t* ui_dock_slot(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	const ui_dock_slot_t* slot;
	if (node.index == 0u || node.index >= ctx->dock_slots.count) {
		return NULL;
	}
	slot = &ctx->dock_slots.items[node.index];
	if (slot->alive == 0u || slot->generation != node.generation) {
		return NULL;
	}
	return slot;
}

static sk_ui_dock_node_t ui_dock_handle(const ui_dock_slot_t* slot, u32 index) {
	sk_ui_dock_node_t h;
	h.index = index;
	h.generation = slot->generation;
	return h;
}

/* -------------------------------------------------------------------------- */
/* Strings / math                                                             */
/* -------------------------------------------------------------------------- */

static char* ui_dock_strdup(const sk_allocator_t* a, const_chr_t src) {
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

static i32 ui_dock_cstr_eq(const_chr_t a, const_chr_t b) {
	if (a == b) {
		return 1;
	}
	if (a == NULL || b == NULL) {
		return 0;
	}
	return strcmp(a, b) == 0 ? 1 : 0;
}

static f32 ui_dock_clampf(f32 v, f32 lo, f32 hi) {
	if (!(v >= lo)) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static i32 ui_dock_dir_is_edge(sk_ui_dock_dir_t dir) {
	return dir == SK_UI_DOCK_DIR_LEFT || dir == SK_UI_DOCK_DIR_RIGHT || dir == SK_UI_DOCK_DIR_UP || dir == SK_UI_DOCK_DIR_DOWN;
}

static f32 ui_dock_clamp_ratio(f32 ratio, f32 main_span) {
	f32 r = ui_dock_clampf(ratio, 0.0f, 1.0f);
	const f32 min_parent = SK_UI_DOCK_NODE_MIN_PT + SK_UI_DOCK_SPLITTER_PT + SK_UI_DOCK_NODE_MIN_PT;
	if (main_span >= min_parent) {
		const f32 leftover = main_span - SK_UI_DOCK_SPLITTER_PT;
		const f32 min_r = SK_UI_DOCK_NODE_MIN_PT / leftover;
		r = ui_dock_clampf(r, min_r, 1.0f - min_r);
	}
	return r;
}

static i32 ui_dock_rect_contains(const sk_ui_rect_t* r, f32 x, f32 y) {
	return (x >= r->x) && (y >= r->y) && (x < (r->x + r->width)) && (y < (r->y + r->height));
}

static sk_logger_t* ui_dock_logger(void) {
	static sk_logger_t* log;
	if (log == NULL) {
		log = sk_logger_api()->create_logger("ui.dock");
	}
	return log;
}

/* -------------------------------------------------------------------------- */
/* Alloc / recycle                                                            */
/* -------------------------------------------------------------------------- */

static void ui_dock_slot_clear_contents(sk_ui_context_t* ctx, ui_dock_slot_t* slot) {
	const sk_allocator_t* a = ctx->allocator;
	u32 i;
	for (i = 0u; i < slot->tab_count; ++i) {
		if (slot->tabs[i] != NULL) {
			(void)sk_hash_map_remove(&ctx->dock_window_map, (const_chr_t)slot->tabs[i]);
			(void)sk_hash_map_remove(&ctx->dock_tab_nodes, (const_chr_t)slot->tabs[i]);
			a->free(a->instance, slot->tabs[i]);
			slot->tabs[i] = NULL;
		}
	}
	slot->tab_count = 0u;
	slot->active_index = 0u;
	if (slot->stable_id != NULL) {
		a->free(a->instance, slot->stable_id);
		slot->stable_id = NULL;
	}
	slot->host = SK_UI_NODE_INVALID;
	slot->splitter = SK_UI_NODE_INVALID;
	slot->tab_bar = SK_UI_NODE_INVALID;
	slot->content = SK_UI_NODE_INVALID;
	slot->child[0] = SK_UI_DOCK_NODE_INVALID;
	slot->child[1] = SK_UI_DOCK_NODE_INVALID;
	slot->parent = SK_UI_DOCK_NODE_INVALID;
	memset(&slot->rect, 0, sizeof(slot->rect));
	memset(&slot->splitter_rect, 0, sizeof(slot->splitter_rect));
}

static void ui_dock_recycle(sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, node);
	if (slot == NULL) {
		return;
	}
	if (slot->kind == UI_DOCK_KIND_LEAF_U8) {
		if (ctx->dock_leaf_count > 0u) {
			ctx->dock_leaf_count -= 1u;
		}
	} else if (ctx->dock_split_count > 0u) {
		ctx->dock_split_count -= 1u;
	}
	ui_dock_slot_clear_contents(ctx, slot);
	slot->alive = 0u;
	slot->kind = 0u;
	slot->flags = 0u;
	slot->axis = SK_UI_DOCK_SPLIT_HORIZONTAL;
	slot->ratio = 0.0f;
	slot->generation += 1u;
	if (slot->generation == 0u) {
		slot->generation = 1u;
	}
	(void)sk_array_push(&ctx->dock_freelist, node.index);
}

static sk_ui_dock_node_t ui_dock_alloc(sk_ui_context_t* ctx, u8 kind) {
	u32 index;
	u32 generation;
	ui_dock_slot_t* slot;
	sk_ui_dock_node_t handle;

	if (ctx->dock_freelist.count > 0u) {
		index = sk_array_pop(&ctx->dock_freelist);
		slot = &ctx->dock_slots.items[index];
		generation = slot->generation;
		if (generation == 0u) {
			generation = 1u;
		}
	} else {
		index = ctx->dock_slots.count;
		if (sk_array_resize(&ctx->dock_slots, index + 1u) != 0) {
			return SK_UI_DOCK_NODE_INVALID;
		}
		slot = &ctx->dock_slots.items[index];
		generation = 1u;
	}

	memset(slot, 0, sizeof(*slot));
	slot->generation = generation;
	slot->alive = 1u;
	slot->kind = kind;
	slot->parent = SK_UI_DOCK_NODE_INVALID;
	slot->child[0] = SK_UI_DOCK_NODE_INVALID;
	slot->child[1] = SK_UI_DOCK_NODE_INVALID;
	slot->host = SK_UI_NODE_INVALID;
	slot->splitter = SK_UI_NODE_INVALID;
	slot->tab_bar = SK_UI_NODE_INVALID;
	slot->content = SK_UI_NODE_INVALID;
	slot->ratio = 0.5f;
	handle.index = index;
	handle.generation = generation;
	if (kind == UI_DOCK_KIND_LEAF_U8) {
		ctx->dock_leaf_count += 1u;
	} else {
		ctx->dock_split_count += 1u;
	}
	return handle;
}

static void ui_dock_free_tree(sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	sk_ui_dock_node_t stack[UI_DOCK_WALK_MAX];
	sk_ui_dock_node_t list[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	u32 n = 0u;
	u32 i;
	if (!sk_ui_dock_node_is_valid(node)) {
		return;
	}
	stack[sp++] = node;
	while (sp > 0u && n < UI_DOCK_WALK_MAX) {
		sk_ui_dock_node_t cur = stack[--sp];
		const ui_dock_slot_t* slot = ui_dock_slot(ctx, cur);
		list[n++] = cur;
		if (slot == NULL || slot->kind != UI_DOCK_KIND_SPLIT_U8) {
			continue;
		}
		if (sk_ui_dock_node_is_valid(slot->child[1]) && sp < UI_DOCK_WALK_MAX) {
			stack[sp++] = slot->child[1];
		}
		if (sk_ui_dock_node_is_valid(slot->child[0]) && sp < UI_DOCK_WALK_MAX) {
			stack[sp++] = slot->child[0];
		}
	}
	for (i = 0u; i < n; ++i) {
		ui_dock_recycle(ctx, list[i]);
	}
}

/* -------------------------------------------------------------------------- */
/* Dockspace lookup                                                           */
/* -------------------------------------------------------------------------- */

static ui_dockspace_t* ui_dock_space_by_id(sk_ui_context_t* ctx, const_chr_t id) {
	u32 i;
	if (id == NULL || id[0] == '\0') {
		return NULL;
	}
	for (i = 0u; i < ctx->dockspace_count; ++i) {
		if (ui_dock_cstr_eq(ctx->dockspaces[i].id, id)) {
			return &ctx->dockspaces[i];
		}
	}
	return NULL;
}

static const ui_dockspace_t* ui_dock_space_by_id_const(const sk_ui_context_t* ctx, const_chr_t id) {
	u32 i;
	if (id == NULL || id[0] == '\0') {
		return NULL;
	}
	for (i = 0u; i < ctx->dockspace_count; ++i) {
		if (ui_dock_cstr_eq(ctx->dockspaces[i].id, id)) {
			return &ctx->dockspaces[i];
		}
	}
	return NULL;
}

static sk_ui_dock_node_t ui_dock_root_of(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	u32 guard = 0u;
	while (slot != NULL && sk_ui_dock_node_is_valid(slot->parent) && guard < UI_DOCK_WALK_MAX) {
		node = slot->parent;
		slot = ui_dock_slot(ctx, node);
		guard += 1u;
	}
	return node;
}

static ui_dockspace_t* ui_dock_space_for_node(sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	sk_ui_dock_node_t root;
	u32 i;
	if (!sk_ui_dock_node_is_valid(node)) {
		if (sk_ui_dock_node_is_valid(ctx->dock_current)) {
			node = ctx->dock_current;
		} else {
			return NULL;
		}
	}
	root = ui_dock_root_of(ctx, node);
	for (i = 0u; i < ctx->dockspace_count; ++i) {
		if (sk_ui_dock_node_eq(ctx->dockspaces[i].root, root) || sk_ui_dock_node_eq(ctx->dockspaces[i].root, node)) {
			return &ctx->dockspaces[i];
		}
	}
	return NULL;
}

static const ui_dockspace_t* ui_dock_space_for_node_const(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	sk_ui_dock_node_t root;
	u32 i;
	if (!sk_ui_dock_node_is_valid(node)) {
		if (sk_ui_dock_node_is_valid(ctx->dock_current)) {
			node = ctx->dock_current;
		} else {
			return NULL;
		}
	}
	root = ui_dock_root_of(ctx, node);
	for (i = 0u; i < ctx->dockspace_count; ++i) {
		if (sk_ui_dock_node_eq(ctx->dockspaces[i].root, root) || sk_ui_dock_node_eq(ctx->dockspaces[i].root, node)) {
			return &ctx->dockspaces[i];
		}
	}
	return NULL;
}

static void ui_dock_mark_dirty(ui_dockspace_t* space) {
	if (space != NULL) {
		space->dirty = 1u;
	}
}

static sk_ui_dock_node_t ui_dock_walk_central(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	sk_ui_dock_node_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	if (!sk_ui_dock_node_is_valid(node)) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	stack[sp++] = node;
	while (sp > 0u) {
		sk_ui_dock_node_t cur = stack[--sp];
		const ui_dock_slot_t* slot = ui_dock_slot(ctx, cur);
		if (slot == NULL) {
			continue;
		}
		if ((slot->flags & SK_UI_DOCK_NODE_CENTRAL) != 0u) {
			return cur;
		}
		if (slot->kind == UI_DOCK_KIND_SPLIT_U8) {
			if (sk_ui_dock_node_is_valid(slot->child[1]) && sp < UI_DOCK_WALK_MAX) {
				stack[sp++] = slot->child[1];
			}
			if (sk_ui_dock_node_is_valid(slot->child[0]) && sp < UI_DOCK_WALK_MAX) {
				stack[sp++] = slot->child[0];
			}
		}
	}
	return SK_UI_DOCK_NODE_INVALID;
}

static sk_ui_dock_node_t ui_dock_resolve_leaf(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	if (slot == NULL) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	if (slot->kind == UI_DOCK_KIND_LEAF_U8) {
		return node;
	}
	return ui_dock_walk_central(ctx, node);
}

/* -------------------------------------------------------------------------- */
/* Tabs                                                                       */
/* -------------------------------------------------------------------------- */

static i32 ui_dock_leaf_index_of(const ui_dock_slot_t* slot, const_chr_t window_id) {
	u32 i;
	for (i = 0u; i < slot->tab_count; ++i) {
		if (ui_dock_cstr_eq(slot->tabs[i], window_id)) {
			return (i32)i;
		}
	}
	return -1;
}

static void ui_dock_leaf_adjust_active(ui_dock_slot_t* slot) {
	if (slot->tab_count == 0u) {
		slot->active_index = 0u;
		return;
	}
	if (slot->active_index >= slot->tab_count) {
		slot->active_index = slot->tab_count - 1u;
	}
}

static i32 ui_dock_remove_window(sk_ui_context_t* ctx, const_chr_t window_id) {
	sk_ui_dock_node_t leaf = SK_UI_DOCK_NODE_INVALID;
	ui_dock_slot_t* slot;
	i32 idx;
	u32 i;
	const sk_allocator_t* a;
	if (window_id == NULL || window_id[0] == '\0') {
		return -1;
	}
	if (sk_hash_map_get(&ctx->dock_window_map, window_id, &leaf) != 0) {
		return -1;
	}
	slot = ui_dock_slot_mut(ctx, leaf);
	if (slot == NULL) {
		(void)sk_hash_map_remove(&ctx->dock_window_map, window_id);
		return -1;
	}
	idx = ui_dock_leaf_index_of(slot, window_id);
	if (idx < 0) {
		(void)sk_hash_map_remove(&ctx->dock_window_map, window_id);
		return -1;
	}
	a = ctx->allocator;
	(void)sk_hash_map_remove(&ctx->dock_window_map, window_id);
	(void)sk_hash_map_remove(&ctx->dock_tab_nodes, window_id);
	a->free(a->instance, slot->tabs[idx]);
	for (i = (u32)idx; i + 1u < slot->tab_count; ++i) {
		slot->tabs[i] = slot->tabs[i + 1u];
	}
	slot->tabs[slot->tab_count - 1u] = NULL;
	slot->tab_count -= 1u;
	if (slot->active_index == (u32)idx) {
		ui_dock_leaf_adjust_active(slot);
	} else if (slot->active_index > (u32)idx) {
		slot->active_index -= 1u;
	}
	return 0;
}

static i32 ui_dock_append_tab(sk_ui_context_t* ctx, sk_ui_dock_node_t leaf, const_chr_t window_id) {
	ui_dock_slot_t* slot;
	char* copy;
	i32 existing;
	sk_ui_dock_node_t other = SK_UI_DOCK_NODE_INVALID;
	if (window_id == NULL || window_id[0] == '\0') {
		return -1;
	}
	slot = ui_dock_slot_mut(ctx, leaf);
	if (slot == NULL || slot->kind != UI_DOCK_KIND_LEAF_U8) {
		return -1;
	}
	existing = ui_dock_leaf_index_of(slot, window_id);
	if (existing >= 0) {
		slot->active_index = (u32)existing;
		return 0;
	}
	if (sk_hash_map_get(&ctx->dock_window_map, window_id, &other) == 0) {
		if (ui_dock_remove_window(ctx, window_id) != 0) {
			return -1;
		}
		slot = ui_dock_slot_mut(ctx, leaf);
		if (slot == NULL) {
			return -1;
		}
	}
	if (slot->tab_count >= SK_UI_DOCK_LEAF_TABS_MAX) {
		return -1;
	}
	copy = ui_dock_strdup(ctx->allocator, window_id);
	if (copy == NULL) {
		return -1;
	}
	slot->tabs[slot->tab_count] = copy;
	slot->active_index = slot->tab_count;
	slot->tab_count += 1u;
	if (sk_hash_map_put(&ctx->dock_window_map, (const_chr_t)copy, leaf) != 0) {
		ctx->allocator->free(ctx->allocator->instance, copy);
		slot->tab_count -= 1u;
		slot->tabs[slot->tab_count] = NULL;
		ui_dock_leaf_adjust_active(slot);
		return -1;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Split / collapse                                                           */
/* -------------------------------------------------------------------------- */

static i32 ui_dock_replace_child(sk_ui_context_t* ctx, ui_dockspace_t* space, sk_ui_dock_node_t parent, sk_ui_dock_node_t old_child, sk_ui_dock_node_t new_child) {
	ui_dock_slot_t* pslot;
	ui_dock_slot_t* nslot;
	if (!sk_ui_dock_node_is_valid(parent)) {
		space->root = new_child;
		nslot = ui_dock_slot_mut(ctx, new_child);
		if (nslot != NULL) {
			nslot->parent = SK_UI_DOCK_NODE_INVALID;
		}
		if (sk_ui_dock_node_eq(ctx->dock_current, old_child)) {
			ctx->dock_current = new_child;
		}
		if (sk_ui_dock_node_eq(ctx->dock_builder_root, old_child)) {
			ctx->dock_builder_root = new_child;
		}
		return 0;
	}
	pslot = ui_dock_slot_mut(ctx, parent);
	if (pslot == NULL) {
		return -1;
	}
	if (sk_ui_dock_node_eq(pslot->child[0], old_child)) {
		pslot->child[0] = new_child;
	} else if (sk_ui_dock_node_eq(pslot->child[1], old_child)) {
		pslot->child[1] = new_child;
	} else {
		return -1;
	}
	nslot = ui_dock_slot_mut(ctx, new_child);
	if (nslot != NULL) {
		nslot->parent = parent;
	}
	return 0;
}

static i32 ui_dock_split_node(sk_ui_context_t* ctx, ui_dockspace_t* space, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir, f32 ratio, sk_ui_dock_node_t* out_at_dir,
							  sk_ui_dock_node_t* out_opposite) {
	ui_dock_slot_t* slot;
	ui_dock_slot_t* split_slot;
	ui_dock_slot_t* leaf_slot;
	sk_ui_dock_node_t split_h;
	sk_ui_dock_node_t leaf_h;
	sk_ui_dock_node_t parent;
	u32 at_dir;
	f32 r;
	f32 main_span;

	slot = ui_dock_slot_mut(ctx, node);
	if (slot == NULL || space == NULL) {
		return -1;
	}
	if (!ui_dock_dir_is_edge(dir)) {
		return -1;
	}

	split_h = ui_dock_alloc(ctx, UI_DOCK_KIND_SPLIT_U8);
	leaf_h = ui_dock_alloc(ctx, UI_DOCK_KIND_LEAF_U8);
	if (!sk_ui_dock_node_is_valid(split_h) || !sk_ui_dock_node_is_valid(leaf_h)) {
		if (sk_ui_dock_node_is_valid(split_h)) {
			ui_dock_recycle(ctx, split_h);
		}
		if (sk_ui_dock_node_is_valid(leaf_h)) {
			ui_dock_recycle(ctx, leaf_h);
		}
		return -1;
	}

	slot = ui_dock_slot_mut(ctx, node);
	split_slot = ui_dock_slot_mut(ctx, split_h);
	leaf_slot = ui_dock_slot_mut(ctx, leaf_h);
	parent = slot->parent;
	at_dir = (dir == SK_UI_DOCK_DIR_LEFT || dir == SK_UI_DOCK_DIR_UP) ? 0u : 1u;
	main_span = (dir == SK_UI_DOCK_DIR_LEFT || dir == SK_UI_DOCK_DIR_RIGHT) ? slot->rect.width : slot->rect.height;
	r = ui_dock_clamp_ratio(ratio, main_span);

	split_slot->axis = (dir == SK_UI_DOCK_DIR_LEFT || dir == SK_UI_DOCK_DIR_RIGHT) ? SK_UI_DOCK_SPLIT_HORIZONTAL : SK_UI_DOCK_SPLIT_VERTICAL;
	split_slot->ratio = (at_dir == 0u) ? r : (1.0f - r);
	split_slot->flags = 0u;
	split_slot->parent = parent;
	split_slot->child[at_dir] = leaf_h;
	split_slot->child[1u - at_dir] = node;

	leaf_slot->parent = split_h;
	slot->parent = split_h;

	if (ui_dock_replace_child(ctx, space, parent, node, split_h) != 0) {
		slot->parent = parent;
		ui_dock_recycle(ctx, leaf_h);
		ui_dock_recycle(ctx, split_h);
		return -1;
	}

	if (out_at_dir != NULL) {
		*out_at_dir = leaf_h;
	}
	if (out_opposite != NULL) {
		*out_opposite = node;
	}
	ui_dock_mark_dirty(space);
	return 0;
}

static i32 ui_dock_leaf_collapsible(const ui_dock_slot_t* slot, u32 space_flags) {
	if (slot == NULL || slot->kind != UI_DOCK_KIND_LEAF_U8) {
		return 0;
	}
	if (slot->tab_count > 0u) {
		return 0;
	}
	if ((slot->flags & SK_UI_DOCK_NODE_CENTRAL) != 0u && (space_flags & SK_UI_DOCKSPACE_KEEP_CENTRAL) != 0u) {
		return 0;
	}
	return 1;
}

static void ui_dock_collapse_space(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	typedef struct ui_dock_post_t {
		sk_ui_dock_node_t node;
		u8 visit;
		u8 _pad[3];
	} ui_dock_post_t;
	ui_dock_post_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	if (space == NULL || !sk_ui_dock_node_is_valid(space->root)) {
		return;
	}
	stack[sp].node = space->root;
	stack[sp].visit = 0u;
	stack[sp]._pad[0] = 0u;
	stack[sp]._pad[1] = 0u;
	stack[sp]._pad[2] = 0u;
	sp += 1u;
	while (sp > 0u) {
		ui_dock_post_t* top = &stack[sp - 1u];
		ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, top->node);
		if (slot == NULL) {
			sp -= 1u;
			continue;
		}
		if (top->visit == 0u && slot->kind == UI_DOCK_KIND_SPLIT_U8) {
			top->visit = 1u;
			if (sk_ui_dock_node_is_valid(slot->child[1]) && sp < UI_DOCK_WALK_MAX) {
				stack[sp].node = slot->child[1];
				stack[sp].visit = 0u;
				stack[sp]._pad[0] = 0u;
				stack[sp]._pad[1] = 0u;
				stack[sp]._pad[2] = 0u;
				sp += 1u;
			}
			if (sk_ui_dock_node_is_valid(slot->child[0]) && sp < UI_DOCK_WALK_MAX) {
				stack[sp].node = slot->child[0];
				stack[sp].visit = 0u;
				stack[sp]._pad[0] = 0u;
				stack[sp]._pad[1] = 0u;
				stack[sp]._pad[2] = 0u;
				sp += 1u;
			}
			continue;
		}
		if (slot->kind == UI_DOCK_KIND_SPLIT_U8) {
			const ui_dock_slot_t* s0 = ui_dock_slot(ctx, slot->child[0]);
			const ui_dock_slot_t* s1 = ui_dock_slot(ctx, slot->child[1]);
			const i32 e0 = ui_dock_leaf_collapsible(s0, space->flags);
			const i32 e1 = ui_dock_leaf_collapsible(s1, space->flags);
			sk_ui_dock_node_t keep = SK_UI_DOCK_NODE_INVALID;
			sk_ui_dock_node_t drop = SK_UI_DOCK_NODE_INVALID;
			sk_ui_dock_node_t split_h = top->node;
			sk_ui_dock_node_t parent = slot->parent;
			if (e0 != 0 && e1 == 0) {
				keep = slot->child[1];
				drop = slot->child[0];
			} else if (e1 != 0 && e0 == 0) {
				keep = slot->child[0];
				drop = slot->child[1];
			} else if (e0 != 0 && e1 != 0) {
				if (s0 != NULL && (s0->flags & SK_UI_DOCK_NODE_CENTRAL) != 0u) {
					keep = slot->child[0];
					drop = slot->child[1];
				} else {
					keep = slot->child[1];
					drop = slot->child[0];
				}
			}
			if (sk_ui_dock_node_is_valid(keep)) {
				(void)ui_dock_replace_child(ctx, space, parent, split_h, keep);
				ui_dock_recycle(ctx, drop);
				ui_dock_recycle(ctx, split_h);
				ui_dock_mark_dirty(space);
			}
		}
		sp -= 1u;
	}
}

/* -------------------------------------------------------------------------- */
/* Layout                                                                     */
/* -------------------------------------------------------------------------- */

static void ui_dock_assign_split_rects(ui_dock_slot_t* slot, const sk_ui_rect_t* parent) {
	f32 leftover;
	f32 size0;
	f32 bar;
	f32 ratio;
	sk_ui_rect_t spl;

	slot->rect = *parent;
	if (slot->axis == SK_UI_DOCK_SPLIT_HORIZONTAL) {
		bar = (parent->width < SK_UI_DOCK_SPLITTER_PT) ? parent->width : SK_UI_DOCK_SPLITTER_PT;
		leftover = parent->width - bar;
		if (leftover < 0.0f) {
			leftover = 0.0f;
		}
		ratio = ui_dock_clamp_ratio(slot->ratio, parent->width);
		slot->ratio = ratio;
		size0 = leftover * ratio;
		spl.x = parent->x + size0;
		spl.y = parent->y;
		spl.width = bar;
		spl.height = parent->height;
	} else {
		bar = (parent->height < SK_UI_DOCK_SPLITTER_PT) ? parent->height : SK_UI_DOCK_SPLITTER_PT;
		leftover = parent->height - bar;
		if (leftover < 0.0f) {
			leftover = 0.0f;
		}
		ratio = ui_dock_clamp_ratio(slot->ratio, parent->height);
		slot->ratio = ratio;
		size0 = leftover * ratio;
		spl.x = parent->x;
		spl.y = parent->y + size0;
		spl.width = parent->width;
		spl.height = bar;
	}
	slot->splitter_rect = spl;
}

static void ui_dock_split_child_rects(const ui_dock_slot_t* slot, sk_ui_rect_t* out0, sk_ui_rect_t* out1) {
	const sk_ui_rect_t* p = &slot->rect;
	const sk_ui_rect_t* s = &slot->splitter_rect;
	if (slot->axis == SK_UI_DOCK_SPLIT_HORIZONTAL) {
		out0->x = p->x;
		out0->y = p->y;
		out0->width = s->x - p->x;
		out0->height = p->height;
		out1->x = s->x + s->width;
		out1->y = p->y;
		out1->width = (p->x + p->width) - out1->x;
		out1->height = p->height;
	} else {
		out0->x = p->x;
		out0->y = p->y;
		out0->width = p->width;
		out0->height = s->y - p->y;
		out1->x = p->x;
		out1->y = s->y + s->height;
		out1->width = p->width;
		out1->height = (p->y + p->height) - out1->y;
	}
}

static void ui_dock_layout_tree(sk_ui_context_t* ctx, sk_ui_dock_node_t root, const sk_ui_rect_t* space) {
	typedef struct ui_dock_lay_t {
		sk_ui_dock_node_t node;
		sk_ui_rect_t rect;
	} ui_dock_lay_t;
	ui_dock_lay_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	stack[sp].node = root;
	stack[sp].rect = *space;
	sp += 1u;
	while (sp > 0u) {
		ui_dock_lay_t cur = stack[--sp];
		ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, cur.node);
		if (slot == NULL) {
			continue;
		}
		if (slot->kind == UI_DOCK_KIND_LEAF_U8) {
			slot->rect = cur.rect;
			memset(&slot->splitter_rect, 0, sizeof(slot->splitter_rect));
			continue;
		}
		ui_dock_assign_split_rects(slot, &cur.rect);
		if (sp + 2u <= UI_DOCK_WALK_MAX) {
			sk_ui_rect_t r0;
			sk_ui_rect_t r1;
			ui_dock_split_child_rects(slot, &r0, &r1);
			stack[sp].node = slot->child[1];
			stack[sp].rect = r1;
			sp += 1u;
			stack[sp].node = slot->child[0];
			stack[sp].rect = r0;
			sp += 1u;
		}
	}
}

static sk_ui_dock_dir_t ui_dock_suggest_dir(const sk_ui_rect_t* r, f32 x, f32 y) {
	f32 fx;
	f32 fy;
	f32 dl;
	f32 dr;
	f32 du;
	f32 dd;
	f32 best;
	const f32 margin = (1.0f - UI_DOCK_INNER_FRAC) * 0.5f;
	sk_ui_dock_dir_t dir;
	if (r->width <= 0.0f || r->height <= 0.0f) {
		return SK_UI_DOCK_DIR_CENTER;
	}
	fx = (x - r->x) / r->width;
	fy = (y - r->y) / r->height;
	if (fx >= margin && fx <= (1.0f - margin) && fy >= margin && fy <= (1.0f - margin)) {
		return SK_UI_DOCK_DIR_CENTER;
	}
	dl = fx;
	dr = 1.0f - fx;
	du = fy;
	dd = 1.0f - fy;
	best = dl;
	dir = SK_UI_DOCK_DIR_LEFT;
	if (dr < best) {
		best = dr;
		dir = SK_UI_DOCK_DIR_RIGHT;
	}
	if (du < best) {
		best = du;
		dir = SK_UI_DOCK_DIR_UP;
	}
	if (dd < best) {
		dir = SK_UI_DOCK_DIR_DOWN;
	}
	return dir;
}

/* -------------------------------------------------------------------------- */
/* Pending / apply                                                            */
/* -------------------------------------------------------------------------- */

static void ui_dock_space_clear_pending(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	u32 i;
	const sk_allocator_t* a = ctx->allocator;
	for (i = 0u; i < space->pending_count; ++i) {
		if (space->pending[i].window_id != NULL) {
			a->free(a->instance, space->pending[i].window_id);
			space->pending[i].window_id = NULL;
		}
	}
	space->pending_count = 0u;
}

static i32 ui_dock_queue_pending(sk_ui_context_t* ctx, ui_dockspace_t* space, const_chr_t window_id, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir) {
	char* copy;
	u32 i;
	if (space->pending_count >= SK_UI_DOCK_PENDING_MAX) {
		sk_log_warn(sk_logger_api(), ui_dock_logger(), "dock pending bind overflow (cap %u)", SK_UI_DOCK_PENDING_MAX);
		return -1;
	}
	for (i = 0u; i < space->pending_count; ++i) {
		if (ui_dock_cstr_eq(space->pending[i].window_id, window_id)) {
			space->pending[i].node = node;
			space->pending[i].dir = dir;
			return 0;
		}
	}
	copy = ui_dock_strdup(ctx->allocator, window_id);
	if (copy == NULL) {
		return -1;
	}
	space->pending[space->pending_count].window_id = copy;
	space->pending[space->pending_count].node = node;
	space->pending[space->pending_count].dir = dir;
	space->pending_count += 1u;
	return 0;
}

static i32 ui_dock_bind_window(sk_ui_context_t* ctx, const_chr_t window_id, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir, i32 allow_pending);

static void ui_dock_resolve_pending(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	const sk_ui_api_t* ui = ui_get_api_table();
	u32 i = 0u;
	while (i < space->pending_count) {
		const_chr_t id = space->pending[i].window_id;
		sk_ui_dock_node_t node = space->pending[i].node;
		sk_ui_dock_dir_t dir = space->pending[i].dir;
		sk_ui_node_t win;
		if (id == NULL) {
			u32 j;
			for (j = i; j + 1u < space->pending_count; ++j) {
				space->pending[j] = space->pending[j + 1u];
			}
			space->pending_count -= 1u;
			continue;
		}
		win = ui->find_by_id(ctx, id);
		if (!sk_ui_node_is_valid(win) || ui_dock_slot(ctx, node) == NULL) {
			i += 1u;
			continue;
		}
		{
			char* owned = space->pending[i].window_id;
			u32 j;
			for (j = i; j + 1u < space->pending_count; ++j) {
				space->pending[j] = space->pending[j + 1u];
			}
			space->pending_count -= 1u;
			(void)ui_dock_bind_window(ctx, owned, node, dir, 0);
			ctx->allocator->free(ctx->allocator->instance, owned);
		}
	}
}

static void ui_dock_maybe_apply(sk_ui_context_t* ctx, ui_dockspace_t* space);

static i32 ui_dock_bind_window(sk_ui_context_t* ctx, const_chr_t window_id, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir, i32 allow_pending) {
	ui_dockspace_t* space;
	const ui_dock_slot_t* slot;
	sk_ui_dock_node_t target;
	if (window_id == NULL || window_id[0] == '\0') {
		return -1;
	}
	slot = ui_dock_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	space = ui_dock_space_for_node(ctx, node);
	if (space == NULL) {
		return -1;
	}
	if (allow_pending != 0) {
		const sk_ui_api_t* ui = ui_get_api_table();
		sk_ui_node_t win = ui->find_by_id(ctx, window_id);
		if (!sk_ui_node_is_valid(win)) {
			return ui_dock_queue_pending(ctx, space, window_id, node, dir);
		}
	}
	if (ui_dock_dir_is_edge(dir)) {
		sk_ui_dock_node_t at_dir = SK_UI_DOCK_NODE_INVALID;
		(void)ui_dock_remove_window(ctx, window_id);
		if (ui_dock_split_node(ctx, space, node, dir, UI_DOCK_EDGE_RATIO, &at_dir, NULL) != 0) {
			return -1;
		}
		if (ui_dock_append_tab(ctx, at_dir, window_id) != 0) {
			return -1;
		}
		ui_dock_mark_dirty(space);
		return 0;
	}
	target = ui_dock_resolve_leaf(ctx, node);
	if (!sk_ui_dock_node_is_valid(target)) {
		return -1;
	}
	if (ui_dock_append_tab(ctx, target, window_id) != 0) {
		return -1;
	}
	ui_dock_mark_dirty(space);
	return 0;
}

static void ui_dock_maybe_apply(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	sk_ui_dock_node_t root;
	if (space == NULL || ctx->dock_builder_open != 0 || ctx->dock_applying != 0) {
		return;
	}
	root = space->root;
	(void)ui_dockspace_apply_impl(ctx, root);
}

/* -------------------------------------------------------------------------- */
/* Context lifecycle                                                          */
/* -------------------------------------------------------------------------- */

i32 ui_dock_context_init(sk_ui_context_t* ctx) {
	sk_array_init(&ctx->dock_slots, ctx->allocator);
	sk_array_init(&ctx->dock_freelist, ctx->allocator);
	if (sk_hash_map_init(&ctx->dock_window_map, ctx->allocator, sk_hash_cstr, sk_equals_cstr) != 0) {
		sk_array_free(&ctx->dock_freelist);
		sk_array_free(&ctx->dock_slots);
		return -1;
	}
	if (sk_hash_map_init(&ctx->dock_tab_nodes, ctx->allocator, sk_hash_cstr, sk_equals_cstr) != 0) {
		sk_hash_map_free(&ctx->dock_window_map);
		sk_array_free(&ctx->dock_freelist);
		sk_array_free(&ctx->dock_slots);
		return -1;
	}
	if (sk_array_resize(&ctx->dock_slots, 1u) != 0) {
		sk_hash_map_free(&ctx->dock_tab_nodes);
		sk_hash_map_free(&ctx->dock_window_map);
		sk_array_free(&ctx->dock_freelist);
		sk_array_free(&ctx->dock_slots);
		return -1;
	}
	memset(&ctx->dock_slots.items[0], 0, sizeof(ctx->dock_slots.items[0]));
	ctx->dock_current = SK_UI_DOCK_NODE_INVALID;
	ctx->dock_builder_root = SK_UI_DOCK_NODE_INVALID;
	return 0;
}

void ui_dock_context_shutdown(sk_ui_context_t* ctx) {
	u32 i;
	const sk_allocator_t* a = ctx->allocator;
	for (i = 0u; i < ctx->dockspace_count; ++i) {
		ui_dock_space_clear_pending(ctx, &ctx->dockspaces[i]);
		if (ctx->dockspaces[i].id != NULL) {
			a->free(a->instance, ctx->dockspaces[i].id);
			ctx->dockspaces[i].id = NULL;
		}
	}
	ctx->dockspace_count = 0u;
	for (i = 1u; i < ctx->dock_slots.count; ++i) {
		ui_dock_slot_t* slot = &ctx->dock_slots.items[i];
		if (slot->alive != 0u) {
			sk_ui_dock_node_t h = ui_dock_handle(slot, i);
			ui_dock_recycle(ctx, h);
		}
	}
	sk_hash_map_free(&ctx->dock_tab_nodes);
	sk_hash_map_free(&ctx->dock_window_map);
	sk_array_free(&ctx->dock_freelist);
	sk_array_free(&ctx->dock_slots);
	ctx->dock_current = SK_UI_DOCK_NODE_INVALID;
	ctx->dock_builder_root = SK_UI_DOCK_NODE_INVALID;
	ctx->dock_builder_open = 0;
	ctx->dock_tab_cb = NULL;
	ctx->dock_tab_user = NULL;
}

void ui_dock_on_window_destroy(sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot;
	const_chr_t id;
	u32 i;
	if (ctx->dockspace_count == 0u) {
		return;
	}
	slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return;
	}
	id = slot->id;
	if (id != NULL && id[0] != '\0') {
		sk_ui_dock_node_t leaf = SK_UI_DOCK_NODE_INVALID;
		if (sk_hash_map_get(&ctx->dock_window_map, id, &leaf) == 0) {
			ui_dockspace_t* space = ui_dock_space_for_node(ctx, leaf);
			(void)ui_dock_remove_window(ctx, id);
			if (space != NULL) {
				ui_dock_mark_dirty(space);
				ui_dock_maybe_apply(ctx, space);
			}
		}
	}
	for (i = 0u; i < ctx->dockspace_count; ++i) {
		if (sk_ui_node_eq(ctx->dockspaces[i].host, node)) {
			ctx->dockspaces[i].host = SK_UI_NODE_INVALID;
		}
	}
}

void ui_dock_on_node_set_id(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t id) {
	u32 s;
	(void)node;
	if (id == NULL || id[0] == '\0' || ctx->dockspace_count == 0u) {
		return;
	}
	for (s = 0u; s < ctx->dockspace_count; ++s) {
		ui_dock_resolve_pending(ctx, &ctx->dockspaces[s]);
		if (ctx->dockspaces[s].dirty != 0u) {
			ui_dock_maybe_apply(ctx, &ctx->dockspaces[s]);
		}
	}
}

/* -------------------------------------------------------------------------- */
/* Public: dockspace                                                          */
/* -------------------------------------------------------------------------- */

static i32 ui_dock_host_mismatch(const sk_ui_context_t* ctx, const ui_dockspace_t* space, sk_ui_node_t host) {
	const ui_node_slot_t* hslot;
	if (!sk_ui_node_is_valid(host)) {
		return 0;
	}
	if (sk_ui_node_eq(host, space->host)) {
		return 0;
	}
	hslot = ui_slot(ctx, space->host);
	if (hslot != NULL && sk_ui_node_eq(host, hslot->parent)) {
		return 0;
	}
	return 1;
}

sk_ui_dock_node_t ui_dockspace_begin_impl(sk_ui_context_t* ctx, sk_ui_node_t host, const_chr_t id, u32 flags) {
	ui_dockspace_t* space;
	sk_ui_dock_node_t root;
	sk_ui_node_t parent;
	sk_ui_node_t chrome;
	if (id == NULL || id[0] == '\0') {
		return SK_UI_DOCK_NODE_INVALID;
	}
	space = ui_dock_space_by_id(ctx, id);
	if (space != NULL) {
		if (ui_dock_host_mismatch(ctx, space, host) != 0) {
			return SK_UI_DOCK_NODE_INVALID;
		}
		space->flags = flags;
		ctx->dock_current = space->root;
		sk_log_debug(sk_logger_api(), ui_dock_logger(), "dockspace reuse '%s'", id);
		return space->root;
	}
	if (ctx->dockspace_count >= SK_UI_DOCKSPACE_MAX) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	root = ui_dock_alloc(ctx, UI_DOCK_KIND_LEAF_U8);
	if (!sk_ui_dock_node_is_valid(root)) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	{
		ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, root);
		if (slot == NULL) {
			ui_dock_recycle(ctx, root);
			return SK_UI_DOCK_NODE_INVALID;
		}
		slot->flags = SK_UI_DOCK_NODE_CENTRAL;
	}
	space = &ctx->dockspaces[ctx->dockspace_count];
	memset(space, 0, sizeof(*space));
	space->id = ui_dock_strdup(ctx->allocator, id);
	if (space->id == NULL) {
		ui_dock_recycle(ctx, root);
		return SK_UI_DOCK_NODE_INVALID;
	}
	space->flags = flags;
	space->root = root;
	space->host = SK_UI_NODE_INVALID;
	space->drop = SK_UI_NODE_INVALID;
	space->dirty = 1u;
	parent = sk_ui_node_is_valid(host) ? host : ctx->root;
	chrome = ui_widget_dock_space_impl(ctx, parent, id);
	if (!sk_ui_node_is_valid(chrome)) {
		ctx->allocator->free(ctx->allocator->instance, space->id);
		space->id = NULL;
		ui_dock_recycle(ctx, root);
		return SK_UI_DOCK_NODE_INVALID;
	}
	space->host = chrome;
	ctx->dockspace_count += 1u;
	ctx->dock_current = root;
	sk_log_debug(sk_logger_api(), ui_dock_logger(), "dockspace create '%s'", id);
	return root;
}

sk_ui_dock_node_t ui_dockspace_create_impl(sk_ui_context_t* ctx, sk_ui_node_t host, const_chr_t id, u32 flags) {
	return ui_dockspace_begin_impl(ctx, host, id, flags);
}

sk_ui_dock_node_t ui_dockspace_find_impl(const sk_ui_context_t* ctx, const_chr_t id) {
	const ui_dockspace_t* space = ui_dock_space_by_id_const(ctx, id);
	return space != NULL ? space->root : SK_UI_DOCK_NODE_INVALID;
}

sk_ui_node_t ui_dockspace_host_node_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace) {
	const ui_dockspace_t* space = ui_dock_space_for_node_const(ctx, dockspace);
	return space != NULL ? space->host : SK_UI_NODE_INVALID;
}

i32 ui_dockspace_apply_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace) {
	ui_dockspace_t* space = ui_dock_space_for_node(ctx, dockspace);
	if (space == NULL) {
		return 0;
	}
	ctx->dock_applying = 1;
	ui_dock_resolve_pending(ctx, space);
	ui_dock_collapse_space(ctx, space);
	if (space->laid_out != 0u) {
		ui_dock_layout_tree(ctx, space->root, &space->last_rect);
	}
	space->dirty = 0u;
	ctx->dock_apply_count += 1u;
	ctx->dock_applying = 0;
	return 0;
}

i32 ui_dockspace_end_impl(sk_ui_context_t* ctx) {
	ui_dockspace_t* space;
	if (!sk_ui_dock_node_is_valid(ctx->dock_current)) {
		return 0;
	}
	space = ui_dock_space_for_node(ctx, ctx->dock_current);
	if (space == NULL || space->dirty == 0u) {
		return 0;
	}
	return ui_dockspace_apply_impl(ctx, ctx->dock_current);
}

i32 ui_dockspace_destroy_impl(sk_ui_context_t* ctx, const_chr_t id) {
	ui_dockspace_t* space;
	sk_ui_node_t host;
	u32 index;
	u32 i;
	const sk_ui_api_t* ui;
	if (id == NULL || id[0] == '\0') {
		return -1;
	}
	space = ui_dock_space_by_id(ctx, id);
	if (space == NULL) {
		return -1;
	}
	index = 0u;
	for (i = 0u; i < ctx->dockspace_count; ++i) {
		if (&ctx->dockspaces[i] == space) {
			index = i;
			break;
		}
	}
	if (sk_ui_dock_node_eq(ctx->dock_current, space->root) || sk_ui_dock_node_eq(ui_dock_root_of(ctx, ctx->dock_current), space->root)) {
		ctx->dock_current = SK_UI_DOCK_NODE_INVALID;
	}
	if (ctx->dock_builder_open != 0 && (sk_ui_dock_node_eq(ctx->dock_builder_root, space->root) || sk_ui_dock_node_eq(ui_dock_root_of(ctx, ctx->dock_builder_root), space->root))) {
		ctx->dock_builder_open = 0;
		ctx->dock_builder_root = SK_UI_DOCK_NODE_INVALID;
	}
	ui_dock_free_tree(ctx, space->root);
	ui_dock_space_clear_pending(ctx, space);
	if (space->id != NULL) {
		ctx->allocator->free(ctx->allocator->instance, space->id);
		space->id = NULL;
	}
	host = space->host;
	space->host = SK_UI_NODE_INVALID;
	for (i = index; i + 1u < ctx->dockspace_count; ++i) {
		ctx->dockspaces[i] = ctx->dockspaces[i + 1u];
	}
	ctx->dockspace_count -= 1u;
	memset(&ctx->dockspaces[ctx->dockspace_count], 0, sizeof(ctx->dockspaces[0]));
	if (sk_ui_node_is_valid(host) && ui_slot(ctx, host) != NULL) {
		ui = ui_get_api_table();
		(void)ui->node_destroy(ctx, host);
	}
	return 0;
}

i32 ui_dockspace_layout_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace, const sk_ui_rect_t* space_rect) {
	ui_dockspace_t* space;
	if (space_rect == NULL) {
		return -1;
	}
	space = ui_dock_space_for_node(ctx, dockspace);
	if (space == NULL) {
		return -1;
	}
	ui_dock_resolve_pending(ctx, space);
	ui_dock_collapse_space(ctx, space);
	space->last_rect = *space_rect;
	space->laid_out = 1u;
	ui_dock_layout_tree(ctx, space->root, space_rect);
	space->dirty = 0u;
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Public: runtime dock / tabs                                                */
/* -------------------------------------------------------------------------- */

i32 ui_dock_window_to_node_impl(sk_ui_context_t* ctx, const_chr_t window_id, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir) {
	ui_dockspace_t* space;
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	if (window_id == NULL || window_id[0] == '\0' || slot == NULL) {
		return -1;
	}
	space = ui_dock_space_for_node(ctx, node);
	if (space == NULL) {
		return -1;
	}
	if (ui_dock_dir_is_edge(dir)) {
		if (((space->flags & SK_UI_DOCKSPACE_NO_SPLIT) != 0u || (slot->flags & SK_UI_DOCK_NODE_NO_SPLIT) != 0u) && ctx->dock_builder_open == 0) {
			return -1;
		}
	}
	if (ui_dock_bind_window(ctx, window_id, node, dir, 1) != 0) {
		return -1;
	}
	ui_dock_maybe_apply(ctx, space);
	return 0;
}

i32 ui_dock_window_undock_impl(sk_ui_context_t* ctx, const_chr_t window_id) {
	sk_ui_dock_node_t leaf;
	ui_dockspace_t* space;
	const ui_dock_slot_t* slot;
	if (window_id == NULL || window_id[0] == '\0') {
		return -1;
	}
	if (sk_hash_map_get(&ctx->dock_window_map, window_id, &leaf) != 0) {
		return -1;
	}
	slot = ui_dock_slot(ctx, leaf);
	space = ui_dock_space_for_node(ctx, leaf);
	if (space == NULL) {
		return -1;
	}
	if (((space->flags & SK_UI_DOCKSPACE_NO_UNDOCK) != 0u || (slot != NULL && (slot->flags & SK_UI_DOCK_NODE_NO_UNDOCK) != 0u)) && ctx->dock_builder_open == 0) {
		return -1;
	}
	if (ui_dock_remove_window(ctx, window_id) != 0) {
		return -1;
	}
	ui_dock_mark_dirty(space);
	ui_dock_maybe_apply(ctx, space);
	return 0;
}

i32 ui_dock_tab_close_impl(sk_ui_context_t* ctx, const_chr_t window_id) {
	sk_ui_dock_node_t leaf;
	ui_dockspace_t* space;
	sk_ui_dock_tab_fn cb;
	void_ptr_t user;
	if (window_id == NULL || window_id[0] == '\0') {
		return -1;
	}
	if (sk_hash_map_get(&ctx->dock_window_map, window_id, &leaf) != 0) {
		return -1;
	}
	space = ui_dock_space_for_node(ctx, leaf);
	if (ui_dock_remove_window(ctx, window_id) != 0) {
		return -1;
	}
	cb = ctx->dock_tab_cb;
	user = ctx->dock_tab_user;
	if (cb != NULL) {
		cb(ctx, window_id, user);
	}
	if (space != NULL) {
		ui_dock_mark_dirty(space);
		ui_dock_maybe_apply(ctx, space);
	}
	return 0;
}

i32 ui_dock_tab_reorder_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t leaf, u32 from_index, u32 to_index) {
	ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, leaf);
	char* moving;
	u32 i;
	ui_dockspace_t* space;
	if (slot == NULL || slot->kind != UI_DOCK_KIND_LEAF_U8) {
		return -1;
	}
	if (from_index >= slot->tab_count || to_index >= slot->tab_count) {
		return -1;
	}
	if (from_index == to_index) {
		return 0;
	}
	moving = slot->tabs[from_index];
	if (from_index < to_index) {
		for (i = from_index; i < to_index; ++i) {
			slot->tabs[i] = slot->tabs[i + 1u];
		}
	} else {
		for (i = from_index; i > to_index; --i) {
			slot->tabs[i] = slot->tabs[i - 1u];
		}
	}
	slot->tabs[to_index] = moving;
	if (slot->active_index == from_index) {
		slot->active_index = to_index;
	} else if (from_index < slot->active_index && to_index >= slot->active_index) {
		slot->active_index -= 1u;
	} else if (from_index > slot->active_index && to_index <= slot->active_index) {
		slot->active_index += 1u;
	}
	space = ui_dock_space_for_node(ctx, leaf);
	ui_dock_mark_dirty(space);
	ui_dock_maybe_apply(ctx, space);
	return 0;
}

i32 ui_dock_tab_set_active_impl(sk_ui_context_t* ctx, const_chr_t window_id) {
	sk_ui_dock_node_t leaf;
	ui_dock_slot_t* slot;
	i32 idx;
	ui_dockspace_t* space;
	if (window_id == NULL || window_id[0] == '\0') {
		return -1;
	}
	if (sk_hash_map_get(&ctx->dock_window_map, window_id, &leaf) != 0) {
		return -1;
	}
	slot = ui_dock_slot_mut(ctx, leaf);
	if (slot == NULL) {
		return -1;
	}
	idx = ui_dock_leaf_index_of(slot, window_id);
	if (idx < 0) {
		return -1;
	}
	slot->active_index = (u32)idx;
	space = ui_dock_space_for_node(ctx, leaf);
	ui_dock_mark_dirty(space);
	ui_dock_maybe_apply(ctx, space);
	return 0;
}

void ui_dock_set_tab_callback_impl(sk_ui_context_t* ctx, sk_ui_dock_tab_fn fn, void_ptr_t user) {
	ctx->dock_tab_cb = fn;
	ctx->dock_tab_user = user;
}

/* -------------------------------------------------------------------------- */
/* Public: builder                                                            */
/* -------------------------------------------------------------------------- */

i32 ui_dock_builder_begin_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace) {
	ui_dockspace_t* space;
	if (ctx->dock_builder_open != 0) {
#ifndef NDEBUG
		assert(ctx->dock_builder_open == 0 && "nested dock_builder_begin");
#endif
		return 0;
	}
	space = ui_dock_space_for_node(ctx, dockspace);
	if (space == NULL) {
		return -1;
	}
	ctx->dock_builder_open = 1;
	ctx->dock_builder_root = space->root;
	return 0;
}

i32 ui_dock_builder_split_node_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t node, sk_ui_dock_dir_t dir, f32 ratio, sk_ui_dock_node_t* out_at_dir, sk_ui_dock_node_t* out_opposite) {
	ui_dockspace_t* space = ui_dock_space_for_node(ctx, node);
	if (space == NULL || ctx->dock_builder_open == 0) {
		return -1;
	}
	return ui_dock_split_node(ctx, space, node, dir, ratio, out_at_dir, out_opposite);
}

i32 ui_dock_builder_dock_window_impl(sk_ui_context_t* ctx, const_chr_t window_id, sk_ui_dock_node_t node) {
	ui_dockspace_t* space;
	sk_ui_dock_node_t leaf;
	if (window_id == NULL || window_id[0] == '\0' || ctx->dock_builder_open == 0) {
		return -1;
	}
	space = ui_dock_space_for_node(ctx, node);
	if (space == NULL) {
		return -1;
	}
	leaf = ui_dock_resolve_leaf(ctx, node);
	if (!sk_ui_dock_node_is_valid(leaf)) {
		return -1;
	}
	if (ui_dock_append_tab(ctx, leaf, window_id) != 0) {
		return -1;
	}
	ui_dock_mark_dirty(space);
	return 0;
}

i32 ui_dock_builder_set_node_id_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t node, const_chr_t id) {
	ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, node);
	char* copy;
	if (slot == NULL) {
		return -1;
	}
	if (slot->stable_id != NULL) {
		ctx->allocator->free(ctx->allocator->instance, slot->stable_id);
		slot->stable_id = NULL;
	}
	if (id == NULL || id[0] == '\0') {
		return 0;
	}
	copy = ui_dock_strdup(ctx->allocator, id);
	if (copy == NULL) {
		return -1;
	}
	slot->stable_id = copy;
	return 0;
}

i32 ui_dock_builder_set_node_flags_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t node, u32 flags) {
	ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	slot->flags = flags;
	return 0;
}

i32 ui_dock_builder_finish_impl(sk_ui_context_t* ctx) {
	sk_ui_dock_node_t root = ctx->dock_builder_root;
	ctx->dock_builder_open = 0;
	ctx->dock_builder_root = SK_UI_DOCK_NODE_INVALID;
	if (!sk_ui_dock_node_is_valid(root)) {
		root = ctx->dock_current;
	}
	return ui_dockspace_apply_impl(ctx, root);
}

/* -------------------------------------------------------------------------- */
/* Public: queries                                                            */
/* -------------------------------------------------------------------------- */

sk_ui_dock_node_t ui_dock_node_at_point_impl(const sk_ui_context_t* ctx, f32 x, f32 y, sk_ui_dock_dir_t* out_dir) {
	u32 s;
	for (s = 0u; s < ctx->dockspace_count; ++s) {
		const ui_dockspace_t* space = &ctx->dockspaces[s];
		sk_ui_dock_node_t stack[UI_DOCK_WALK_MAX];
		u32 sp = 0u;
		if (space->laid_out == 0u || !ui_dock_rect_contains(&space->last_rect, x, y)) {
			continue;
		}
		stack[sp++] = space->root;
		while (sp > 0u) {
			sk_ui_dock_node_t cur = stack[--sp];
			const ui_dock_slot_t* slot = ui_dock_slot(ctx, cur);
			if (slot == NULL || !ui_dock_rect_contains(&slot->rect, x, y)) {
				continue;
			}
			if (slot->kind == UI_DOCK_KIND_LEAF_U8) {
				if (out_dir != NULL) {
					*out_dir = ui_dock_suggest_dir(&slot->rect, x, y);
				}
				return cur;
			}
			if (ui_dock_rect_contains(&slot->splitter_rect, x, y)) {
				if (out_dir != NULL) {
					*out_dir = ui_dock_suggest_dir(&slot->rect, x, y);
				}
				return cur;
			}
			if (sk_ui_dock_node_is_valid(slot->child[1]) && sp < UI_DOCK_WALK_MAX) {
				stack[sp++] = slot->child[1];
			}
			if (sk_ui_dock_node_is_valid(slot->child[0]) && sp < UI_DOCK_WALK_MAX) {
				stack[sp++] = slot->child[0];
			}
		}
	}
	if (out_dir != NULL) {
		*out_dir = SK_UI_DOCK_DIR_NONE;
	}
	return SK_UI_DOCK_NODE_INVALID;
}

sk_ui_dock_node_t ui_dock_find_node_for_window_impl(const sk_ui_context_t* ctx, const_chr_t window_id) {
	sk_ui_dock_node_t leaf = SK_UI_DOCK_NODE_INVALID;
	if (window_id == NULL || window_id[0] == '\0') {
		return SK_UI_DOCK_NODE_INVALID;
	}
	if (sk_hash_map_get(&SK_CONST_CAST(sk_ui_context_t*, ctx)->dock_window_map, window_id, &leaf) != 0) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	if (ui_dock_slot(ctx, leaf) == NULL) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	return leaf;
}

i32 ui_dock_leaf_tabs_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t leaf, const_chr_t* out_ids, u32 max_out, u32* out_count, u32* out_active) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, leaf);
	u32 n;
	u32 i;
	if (slot == NULL || slot->kind != UI_DOCK_KIND_LEAF_U8) {
		return -1;
	}
	n = slot->tab_count;
	if (n > max_out) {
		n = max_out;
	}
	if (out_ids != NULL) {
		for (i = 0u; i < n; ++i) {
			out_ids[i] = slot->tabs[i];
		}
	}
	if (out_count != NULL) {
		*out_count = slot->tab_count;
	}
	if (out_active != NULL) {
		*out_active = slot->active_index;
	}
	return 0;
}

i32 ui_dock_node_is_leaf_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	return (slot != NULL && slot->kind == UI_DOCK_KIND_LEAF_U8) ? 1 : 0;
}

i32 ui_dock_node_is_split_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	return (slot != NULL && slot->kind == UI_DOCK_KIND_SPLIT_U8) ? 1 : 0;
}

sk_ui_dock_split_t ui_dock_split_get_axis_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	if (slot == NULL || slot->kind != UI_DOCK_KIND_SPLIT_U8) {
		return SK_UI_DOCK_SPLIT_HORIZONTAL;
	}
	return slot->axis;
}

f32 ui_dock_split_get_ratio_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	if (slot == NULL || slot->kind != UI_DOCK_KIND_SPLIT_U8) {
		return 0.0f;
	}
	return slot->ratio;
}

i32 ui_dock_split_set_ratio_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t node, f32 ratio) {
	ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, node);
	ui_dockspace_t* space;
	f32 span;
	if (slot == NULL || slot->kind != UI_DOCK_KIND_SPLIT_U8) {
		return -1;
	}
	span = (slot->axis == SK_UI_DOCK_SPLIT_HORIZONTAL) ? slot->rect.width : slot->rect.height;
	slot->ratio = ui_dock_clamp_ratio(ratio, span);
	space = ui_dock_space_for_node(ctx, node);
	ui_dock_mark_dirty(space);
	ui_dock_maybe_apply(ctx, space);
	return 0;
}

sk_ui_dock_node_t ui_dock_split_child_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node, u32 index) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	if (slot == NULL || slot->kind != UI_DOCK_KIND_SPLIT_U8 || index > 1u) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	return slot->child[index];
}

sk_ui_node_t ui_dock_node_host_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	return slot != NULL ? slot->host : SK_UI_NODE_INVALID;
}

i32 ui_dock_window_is_docked_impl(const sk_ui_context_t* ctx, const_chr_t window_id) {
	return sk_ui_dock_node_is_valid(ui_dock_find_node_for_window_impl(ctx, window_id));
}

i32 ui_dock_node_get_rect_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node, sk_ui_rect_t* out) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (out != NULL) {
		*out = slot->rect;
	}
	return 0;
}

i32 ui_dock_split_get_splitter_rect_impl(const sk_ui_context_t* ctx, sk_ui_dock_node_t node, sk_ui_rect_t* out) {
	const ui_dock_slot_t* slot = ui_dock_slot(ctx, node);
	if (slot == NULL || slot->kind != UI_DOCK_KIND_SPLIT_U8) {
		return -1;
	}
	if (out != NULL) {
		*out = slot->splitter_rect;
	}
	return 0;
}

i32 ui_dock_layout_save_json_impl(const sk_ui_context_t* ctx, const_chr_t dockspace_id, char* out, u32 cap, u32* out_len) {
	(void)ctx;
	(void)dockspace_id;
	if (out != NULL && cap > 0u) {
		out[0] = '\0';
	}
	if (out_len != NULL) {
		*out_len = 0u;
	}
	return -1;
}

i32 ui_dock_layout_load_json_impl(sk_ui_context_t* ctx, const_chr_t dockspace_id, const_chr_t json, u32 len) {
	(void)dockspace_id;
	(void)json;
	(void)len;
	if (ctx->dock_builder_open != 0) {
		return -1;
	}
	return -1;
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

#include <stdio.h>

static const sk_ui_api_t* ui_dock_test_api(void) {
	return ui_get_api_table();
}

static char ui_dock_test_close_id[64];

static void ui_dock_test_on_close(sk_ui_context_t* ctx, const_chr_t window_id, void_ptr_t user) {
	u32* count = (u32*)user;
	(void)ctx;
	if (count != NULL) {
		*count += 1u;
	}
	if (window_id != NULL) {
		size_t n = strlen(window_id);
		if (n >= sizeof(ui_dock_test_close_id)) {
			n = sizeof(ui_dock_test_close_id) - 1u;
		}
		memcpy(ui_dock_test_close_id, window_id, n);
		ui_dock_test_close_id[n] = '\0';
	}
}

SK_TEST(ui_dock_begin_create_find_central) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t found;
	sk_ui_node_t host;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "editor-main", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(root));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_is_split(ctx, root));
	found = ui->dockspace_find(ctx, "editor-main");
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(found, root));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dockspace_create(ctx, SK_UI_NODE_INVALID, "editor-main", SK_UI_DOCKSPACE_KEEP_CENTRAL), root));
	host = ui->dockspace_host_node(ctx, root);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(host));
	TEST_ASSERT_TRUE(ui->node_has_class(ctx, host, SK_UI_CLASS_DOCK_SPACE));
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_end(ctx));

	TEST_ASSERT_FALSE(sk_ui_dock_node_is_valid(ui->dockspace_find(ctx, "missing")));
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_destroy(ctx, "editor-main"));
	TEST_ASSERT_FALSE(sk_ui_dock_node_is_valid(ui->dockspace_find(ctx, "editor-main")));
	TEST_ASSERT_TRUE(ui->dockspace_destroy(ctx, "editor-main") != 0);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_builder_split_dir_ratio_table) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t at_dir;
	sk_ui_dock_node_t opposite;
	sk_ui_dock_node_t split;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "ds", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &at_dir, &opposite));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(opposite, root));
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, at_dir));
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, opposite));
	split = ui->dockspace_find(ctx, "ds");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, split));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_HORIZONTAL, (int)ui->dock_split_get_axis(ctx, split));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, ui->dock_split_get_ratio(ctx, split));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_split_child(ctx, split, 0u), at_dir));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_split_child(ctx, split, 1u), opposite));
	TEST_ASSERT_TRUE(ui->dock_builder_split_node(ctx, opposite, SK_UI_DOCK_DIR_CENTER, 0.5f, NULL, NULL) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	/* RIGHT stores 1-r; DOWN is vertical. */
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_destroy(ctx, "ds"));
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "ds2", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_RIGHT, 0.25f, &at_dir, &opposite));
	split = ui->dockspace_find(ctx, "ds2");
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.75f, ui->dock_split_get_ratio(ctx, split));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_split_child(ctx, split, 1u), at_dir));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, opposite, SK_UI_DOCK_DIR_DOWN, 0.28f, &at_dir, &opposite));
	{
		sk_ui_dock_node_t inner = ui->dock_split_child(ctx, ui->dockspace_find(ctx, "ds2"), 0u);
		TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, inner));
		TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_VERTICAL, (int)ui->dock_split_get_axis(ctx, inner));
		TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.72f, ui->dock_split_get_ratio(ctx, inner));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_collapse_empty_keep_central) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t bottom;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t after;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "keep", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_DOWN, 0.28f, &bottom, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, ui->dockspace_find(ctx, "keep")));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "console"));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_close(ctx, "console"));
	after = ui->dockspace_find(ctx, "keep");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, after));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(after, rest));
	TEST_ASSERT_FALSE(ui->dock_node_is_split(ctx, bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "console"));

	/* Without KEEP_CENTRAL an empty side still collapses; last leaf remains. */
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_destroy(ctx, "keep"));
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "nokeep", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &bottom, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "a", bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "b", rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_close(ctx, "b"));
	after = ui->dockspace_find(ctx, "nokeep");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, after));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(after, bottom));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_generation_recycle) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t at_dir;
	sk_ui_dock_node_t opposite;
	sk_ui_dock_node_t split;
	sk_ui_dock_node_t stale;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "gen", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.5f, &at_dir, &opposite));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "only", opposite));
	split = ui->dockspace_find(ctx, "gen");
	stale = split;
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_close(ctx, "only"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_is_split(ctx, stale));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_is_leaf(ctx, stale));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, ui->dock_split_get_ratio(ctx, stale));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, ui->dockspace_find(ctx, "gen")));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, ui->dockspace_find(ctx, "gen"), SK_UI_DOCK_DIR_UP, 0.5f, &at_dir, &opposite));
	split = ui->dockspace_find(ctx, "gen");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, split));
	TEST_ASSERT_FALSE(sk_ui_dock_node_eq(split, stale));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_pending_bind) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_node_t win;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "pend", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, "later", root, SK_UI_DOCK_DIR_CENTER));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "later"));
	win = ui->widget_editor_window(ctx, ui->context_root(ctx), "Later", "later");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(win));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "later"));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "later"), root));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_two_pane_ratio) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t right;
	sk_ui_dock_node_t split;
	sk_ui_rect_t space;
	sk_ui_rect_t rl;
	sk_ui_rect_t rr;
	sk_ui_rect_t rs;
	const f32 leftover = 400.0f - SK_UI_DOCK_SPLITTER_PT;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "lay", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &left, &right));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "left", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "right", right));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	split = ui->dockspace_find(ctx, "lay");
	space.x = 10.0f;
	space.y = 20.0f;
	space.width = 400.0f;
	space.height = 200.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, split, &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, left, &rl));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, right, &rr));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_split_get_splitter_rect(ctx, split, &rs));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, rl.x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, leftover * 0.25f, rl.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, rl.height);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, SK_UI_DOCK_SPLITTER_PT, rs.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, leftover * 0.75f, rr.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, rl.x + rl.width + rs.width, rr.x);
	TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.25f, rl.width);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_min_size_and_splitter) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t a;
	sk_ui_dock_node_t b;
	sk_ui_dock_node_t split;
	sk_ui_rect_t space;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "min", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.01f, &a, &b));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "a", a));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "b", b));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	split = ui->dockspace_find(ctx, "min");
	space.x = 0.0f;
	space.y = 0.0f;
	space.width = 200.0f;
	space.height = 100.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, split, &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, a, &ra));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, b, &rb));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, SK_UI_DOCK_NODE_MIN_PT, ra.width);
	TEST_ASSERT_TRUE(rb.width + 0.01f >= SK_UI_DOCK_NODE_MIN_PT);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f - SK_UI_DOCK_SPLITTER_PT, ra.width + rb.width);

	/* Parent smaller than 86pt: ratio only clamped to [0,1]. */
	space.width = 50.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->dock_split_set_ratio(ctx, split, 0.25f));
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, split, &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, a, &ra));
	TEST_ASSERT_FLOAT_WITHIN(0.05f, (50.0f - SK_UI_DOCK_SPLITTER_PT) * 0.25f, ra.width);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_nested_and_at_point) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t bottom;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t split;
	sk_ui_dock_dir_t dir;
	sk_ui_rect_t space;
	sk_ui_rect_t rc;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "nest", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &left, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, rest, SK_UI_DOCK_DIR_DOWN, 0.28f, &bottom, &center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	split = ui->dockspace_find(ctx, "nest");
	space.x = 0.0f;
	space.y = 0.0f;
	space.width = 800.0f;
	space.height = 600.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, split, &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, center, &rc));

	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_node_at_point(ctx, rc.x + rc.width * 0.5f, rc.y + rc.height * 0.5f, &dir), center));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_DIR_CENTER, (int)dir);
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_node_at_point(ctx, rc.x + 1.0f, rc.y + rc.height * 0.5f, &dir), center));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_DIR_LEFT, (int)dir);
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "console"), bottom));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_tabs_reorder_active_close) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	u32 seen = 0u;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "tabs", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "game", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "profiler", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(3u, count);
	TEST_ASSERT_EQUAL_UINT(2u, active);
	TEST_ASSERT_EQUAL_STRING("scene", ids[0]);
	TEST_ASSERT_EQUAL_STRING("profiler", ids[2]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_set_active(ctx, "scene"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(0u, active);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_reorder(ctx, root, 0u, 2u));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_STRING("game", ids[0]);
	TEST_ASSERT_EQUAL_STRING("scene", ids[2]);
	TEST_ASSERT_EQUAL_UINT(2u, active);

	ui_dock_test_close_id[0] = '\0';
	ui->dock_set_tab_callback(ctx, ui_dock_test_on_close, &seen);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_close(ctx, "game"));
	TEST_ASSERT_EQUAL_UINT(1u, seen);
	TEST_ASSERT_EQUAL_STRING("game", ui_dock_test_close_id);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_window_to_node_edge_and_undock) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t split;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "rt", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "H", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "C", "console");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, "hierarchy", root, SK_UI_DOCK_DIR_CENTER));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, "console", root, SK_UI_DOCK_DIR_LEFT));
	split = ui->dockspace_find(ctx, "rt");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, split));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, ui->dock_split_get_ratio(ctx, split));
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, ui->dock_find_node_for_window(ctx, "console")));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "hierarchy"), root));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "console"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "console"));
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, ui->dockspace_find(ctx, "rt")));

	/* NO_SPLIT blocks runtime edge dock. */
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "nosplit", SK_UI_DOCKSPACE_NO_SPLIT);
	TEST_ASSERT_TRUE(ui->dock_window_to_node(ctx, "hierarchy", root, SK_UI_DOCK_DIR_RIGHT) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, "hierarchy", root, SK_UI_DOCK_DIR_CENTER));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_builder_defers_apply) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t side;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t split;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "def", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.3f, &side, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "a", side));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	split = ui->dockspace_find(ctx, "def");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, split));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, split));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_close(ctx, "a"));
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, ui->dockspace_find(ctx, "def")));
	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "def", "{}", 2u) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, ui->dockspace_find(ctx, "def")));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dockspace_cap_and_rebind_host) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root;
	sk_ui_node_t a;
	sk_ui_node_t b;
	char name[16];
	u32 i;
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);
	a = ui->widget_view(ctx, root, "ws-a");
	b = ui->widget_view(ctx, root, "ws-b");

	for (i = 0u; i < SK_UI_DOCKSPACE_MAX; ++i) {
		(void)snprintf(name, sizeof(name), "cap-%u", i);
		TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(ui->dockspace_begin(ctx, a, name, 0u)));
	}
	TEST_ASSERT_FALSE(sk_ui_dock_node_is_valid(ui->dockspace_begin(ctx, a, "cap-extra", 0u)));
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_destroy(ctx, "cap-0"));
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(ui->dockspace_begin(ctx, a, "cap-extra", 0u)));
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_destroy(ctx, "cap-extra"));

	{
		sk_ui_dock_node_t ds = ui->dockspace_begin(ctx, a, "rebind", 0u);
		TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(ds));
		TEST_ASSERT_FALSE(sk_ui_dock_node_is_valid(ui->dockspace_begin(ctx, b, "rebind", 0u)));
		TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "rebind", 0u), ds));
		TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dockspace_begin(ctx, a, "rebind", 0u), ds));
	}

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
