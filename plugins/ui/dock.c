/**
 * @file dock.c
 * @brief Retained dock node tree, chrome projection, and live drag/drop.
 *
 * Binary split/leaf model owned by the UI context (APX-287). Layout resolves
 * a dockspace rect into per-node screen boxes plus 6pt splitter bands
 * (APX-288). Apply projects the model onto dock/tab/splitter chrome, and
 * pointer handlers implement tab tear-off, drop overlays, and floating
 * redock (APX-289). dock_layout_save_json walks the live node tree into
 * sk_ui_dock_layout_t and emits the versioned JSON document (APX-316);
 * dock_layout_load_json parses that same schema and rebuilds the live tree
 * at startup (APX-317). Restore then reconciles window-id mismatches (APX-318):
 * unknown ids are dropped and empty splits/tab groups collapse with leftover
 * sibling ratios renormalized; registered ids missing from the document dock
 * at their default target or float at their default rect.
 */

#include "ui.internal.h"

#include "allocator.h"
#include "logger.h"
#include "serialization.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define UI_DOCK_WALK_MAX 128u
#define UI_DOCK_EDGE_RATIO 0.25f
#define UI_DOCK_INNER_FRAC 0.6f
#define UI_DOCK_TEAR_PT 8.0f
#define UI_DOCK_OUTER_BAND_PT 16.0f
#define UI_DOCK_FLOAT_W 360.0f
#define UI_DOCK_FLOAT_H 240.0f
#define UI_DOCK_FLOAT_Z 50
#define UI_DOCK_DROP_Z 300
#define UI_DOCK_ZONE_PT 28.0f
#define UI_DOCK_APPEND 0xFFFFFFFFu

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
	static sk_logger_t* log = NULL;
	static sk_logger_context_t* log_ctx = NULL;
	const sk_logger_api_t* api = ui_logger_api();
	sk_logger_context_t* ctx = ui_logger_context();
	if (api != NULL && ctx != NULL && (log == NULL || log_ctx != ctx)) {
		/* The host logger context is destroyed at app teardown, so a logger
		 * cached from an earlier boot is dead. Never touch the old context;
		 * just bind a fresh logger to the current one (integration tests boot
		 * the app repeatedly in one process). */
		log_ctx = ctx;
		log = api->create_logger(ctx, "ui.dock");
	}
	return log;
}

static i32 ui_dock_window_known(const sk_ui_context_t* ctx, const_chr_t window_id);
static void ui_dock_place_unplaced_registered(sk_ui_context_t* ctx, ui_dockspace_t* space);
static void ui_dock_layout_reconcile_mismatches(sk_ui_context_t* ctx, ui_dockspace_t* space);

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

static void ui_dock_salvage_windows(sk_ui_context_t* ctx, sk_ui_node_t root, sk_ui_node_t dest);

static i32 ui_dock_node_protected(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	if (!sk_ui_node_is_valid(node)) {
		return 0;
	}
	if (sk_ui_node_eq(ctx->pointer_capture, node) || sk_ui_node_eq(ctx->focus, node)) {
		return 1;
	}
	return 0;
}

static void ui_dock_destroy_slot_chrome(sk_ui_context_t* ctx, ui_dock_slot_t* slot) {
	const sk_ui_api_t* ui = ui_get_api_table();
	if (slot == NULL) {
		return;
	}
	if (sk_ui_node_is_valid(slot->host) && ui_slot(ctx, slot->host) != NULL) {
		ui_dock_salvage_windows(ctx, slot->host, ctx->dock_stash);
		if (ui_dock_node_protected(ctx, slot->host) == 0) {
			(void)ui->node_destroy(ctx, slot->host);
		}
	}
	slot->host = SK_UI_NODE_INVALID;
	slot->splitter = SK_UI_NODE_INVALID;
	slot->tab_bar = SK_UI_NODE_INVALID;
	slot->content = SK_UI_NODE_INVALID;
}

static void ui_dock_recycle(sk_ui_context_t* ctx, sk_ui_dock_node_t node) {
	ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, node);
	if (slot == NULL) {
		return;
	}
	ui_dock_destroy_slot_chrome(ctx, slot);
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
				/* Surviving sibling inherits the split's full leftover span. */
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
		sk_log_warn(ui_logger_api(), ui_dock_logger(), "dock pending bind overflow (cap %u)", SK_UI_DOCK_PENDING_MAX);
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
	for (i = 0u; i < ctx->dock_reg_count; ++i) {
		if (ctx->dock_regs[i].id != NULL) {
			a->free(a->instance, ctx->dock_regs[i].id);
			ctx->dock_regs[i].id = NULL;
		}
		if (ctx->dock_regs[i].default_target != NULL) {
			a->free(a->instance, ctx->dock_regs[i].default_target);
			ctx->dock_regs[i].default_target = NULL;
		}
	}
	ctx->dock_reg_count = 0u;
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
		if (ctx->dock_restoring == 0u) {
			ui_dock_place_unplaced_registered(ctx, &ctx->dockspaces[s]);
		}
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
		sk_log_debug(ui_logger_api(), ui_dock_logger(), "dockspace reuse '%s'", id);
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
	sk_log_debug(ui_logger_api(), ui_dock_logger(), "dockspace create '%s'", id);
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

static const_chr_t ui_dock_prop_str(const sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key) {
	sk_ui_prop_value_t prop;
	const sk_ui_api_t* ui = ui_get_api_table();
	if (ui->node_get_prop(ctx, node, key, &prop) != 0 || prop.type != SK_UI_PROP_STR) {
		return NULL;
	}
	return prop.data.str_value;
}

static i32 ui_dock_prop_i32(const sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t key, i32 fallback) {
	sk_ui_prop_value_t prop;
	const sk_ui_api_t* ui = ui_get_api_table();
	if (ui->node_get_prop(ctx, node, key, &prop) != 0 || prop.type != SK_UI_PROP_I32) {
		return fallback;
	}
	return prop.data.i32_value;
}

static i32 ui_dock_is_widget(const sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t widget) {
	const_chr_t w = ui_dock_prop_str(ctx, node, "widget");
	return (w != NULL && widget != NULL && strcmp(w, widget) == 0) ? 1 : 0;
}

static void ui_dock_walk_set_input(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_pointer_events_t pe, i32 focusable) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_node_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	if (!sk_ui_node_is_valid(node)) {
		return;
	}
	stack[sp++] = node;
	while (sp > 0u) {
		sk_ui_node_t cur = stack[--sp];
		const ui_node_slot_t* slot;
		u32 i;
		(void)ui->node_set_pointer_events(ctx, cur, pe);
		(void)ui->node_set_focusable(ctx, cur, focusable);
		slot = ui_slot(ctx, cur);
		if (slot == NULL) {
			continue;
		}
		for (i = 0u; i < slot->children.count && sp < UI_DOCK_WALK_MAX; ++i) {
			stack[sp++] = slot->children.items[i];
		}
	}
}

static void ui_dock_set_hidden(sk_ui_context_t* ctx, sk_ui_node_t node, i32 hidden) {
	const sk_ui_api_t* ui = ui_get_api_table();
	if (!sk_ui_node_is_valid(node) || ui_slot(ctx, node) == NULL) {
		return;
	}
	(void)ui->node_set_prop_i32(ctx, node, "hidden", hidden != 0 ? 1 : 0);
}

static void ui_dock_collect_editor_windows(const sk_ui_context_t* ctx, sk_ui_node_t root, sk_ui_node_t* out, u32 cap, u32* out_n) {
	sk_ui_node_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	u32 n = 0u;
	if (!sk_ui_node_is_valid(root)) {
		*out_n = 0u;
		return;
	}
	stack[sp++] = root;
	while (sp > 0u) {
		sk_ui_node_t cur = stack[--sp];
		const ui_node_slot_t* slot = ui_slot(ctx, cur);
		u32 i;
		if (slot == NULL) {
			continue;
		}
		if (ui_dock_is_widget(ctx, cur, "editor_window") != 0) {
			if (n < cap) {
				out[n] = cur;
			}
			n += 1u;
			continue;
		}
		for (i = 0u; i < slot->children.count && sp < UI_DOCK_WALK_MAX; ++i) {
			stack[sp++] = slot->children.items[i];
		}
	}
	*out_n = n;
}

static void ui_dock_salvage_windows(sk_ui_context_t* ctx, sk_ui_node_t root, sk_ui_node_t dest) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_node_t found[64];
	u32 n = 0u;
	u32 i;
	if (!sk_ui_node_is_valid(dest) || ui_slot(ctx, dest) == NULL) {
		return;
	}
	ui_dock_collect_editor_windows(ctx, root, found, 64u, &n);
	if (n > 64u) {
		n = 64u;
	}
	for (i = 0u; i < n; ++i) {
		if (!sk_ui_node_eq(ui->node_parent(ctx, found[i]), dest)) {
			(void)ui->node_reparent(ctx, found[i], dest, UI_DOCK_APPEND);
		}
	}
}

static i32 ui_dock_slot_abs_rect(const sk_ui_context_t* ctx, const ui_dock_slot_t* slot, sk_ui_rect_t* out) {
	if (slot == NULL || out == NULL) {
		return -1;
	}
	if (sk_ui_node_is_valid(slot->host) && ui_node_get_abs_rect_impl(ctx, slot->host, out, NULL) == 0 && out->width > 0.5f && out->height > 0.5f) {
		return 0;
	}
	*out = slot->rect;
	return (out->width > 0.0f && out->height > 0.0f) ? 0 : -1;
}

static i32 ui_dock_space_abs_rect(const sk_ui_context_t* ctx, const ui_dockspace_t* space, sk_ui_rect_t* out) {
	if (space == NULL || out == NULL) {
		return -1;
	}
	if (sk_ui_node_is_valid(space->host) && ui_node_get_abs_rect_impl(ctx, space->host, out, NULL) == 0 && out->width > 0.5f && out->height > 0.5f) {
		return 0;
	}
	if (space->laid_out != 0u) {
		*out = space->last_rect;
		return 0;
	}
	return -1;
}

static sk_ui_rect_t ui_dock_dir_preview_rect(const sk_ui_rect_t* target, sk_ui_dock_dir_t dir) {
	sk_ui_rect_t r = *target;
	const f32 ratio = UI_DOCK_EDGE_RATIO;
	if (dir == SK_UI_DOCK_DIR_LEFT) {
		r.width *= ratio;
	} else if (dir == SK_UI_DOCK_DIR_RIGHT) {
		r.x += r.width * (1.0f - ratio);
		r.width *= ratio;
	} else if (dir == SK_UI_DOCK_DIR_UP) {
		r.height *= ratio;
	} else if (dir == SK_UI_DOCK_DIR_DOWN) {
		r.y += r.height * (1.0f - ratio);
		r.height *= ratio;
	}
	return r;
}

static void ui_dock_merge_layout(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_style_props_t* props) {
	const sk_ui_api_t* ui = ui_get_api_table();
	if (!sk_ui_node_is_valid(node) || props == NULL) {
		return;
	}
	(void)ui->node_merge_inline_style(ctx, node, props);
	{
		sk_ui_layout_style_t ls;
		if (ui->node_get_layout_style(ctx, node, &ls) == 0) {
			if ((props->mask & SK_UI_SP_FLEX_DIRECTION) != 0u) {
				ls.flex_direction = props->layout.flex_direction;
			}
			if ((props->mask & SK_UI_SP_FLEX_GROW) != 0u) {
				ls.flex_grow = props->layout.flex_grow;
			}
			if ((props->mask & SK_UI_SP_FLEX_SHRINK) != 0u) {
				ls.flex_shrink = props->layout.flex_shrink;
			}
			if ((props->mask & SK_UI_SP_WIDTH) != 0u) {
				ls.width = props->layout.width;
			}
			if ((props->mask & SK_UI_SP_HEIGHT) != 0u) {
				ls.height = props->layout.height;
			}
			if ((props->mask & SK_UI_SP_MIN_WIDTH) != 0u) {
				ls.min_width = props->layout.min_width;
			}
			if ((props->mask & SK_UI_SP_MIN_HEIGHT) != 0u) {
				ls.min_height = props->layout.min_height;
			}
			if ((props->mask & SK_UI_SP_POSITION) != 0u) {
				ls.position = props->layout.position;
			}
			if ((props->mask & SK_UI_SP_LEFT) != 0u) {
				ls.left = props->layout.left;
			}
			if ((props->mask & SK_UI_SP_TOP) != 0u) {
				ls.top = props->layout.top;
			}
			(void)ui->node_set_layout_style(ctx, node, &ls);
		}
	}
}

static void ui_dock_set_abs_box(sk_ui_context_t* ctx, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(x);
	p.layout.top = sk_ui_pt(y);
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.flex_grow = 0.0f;
	p.layout.flex_shrink = 0.0f;
	ui_dock_merge_layout(ctx, node, &p);
}

static void ui_dock_set_fill_flex(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_flex_direction_t dir, f32 grow) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_POSITION | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
	p.layout.position = SK_UI_POSITION_RELATIVE;
	p.layout.flex_direction = dir;
	p.layout.flex_grow = grow;
	p.layout.flex_shrink = 1.0f;
	ui_dock_merge_layout(ctx, node, &p);
}

static void ui_dock_title_hidden(sk_ui_context_t* ctx, sk_ui_node_t window, i32 hidden) {
	sk_ui_node_t bar = ui_editor_window_title_bar_impl(ctx, window);
	ui_dock_set_hidden(ctx, bar, hidden);
}

static void ui_dock_style_docked(sk_ui_context_t* ctx, sk_ui_node_t window) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.position = SK_UI_POSITION_RELATIVE;
	p.layout.left = sk_ui_auto();
	p.layout.top = sk_ui_auto();
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	ui_dock_merge_layout(ctx, window, &p);
	(void)ui->node_set_prop_i32(ctx, window, "z_index", 0);
	ui_dock_set_hidden(ctx, window, 0);
	ui_dock_title_hidden(ctx, window, 1);
	ui_dock_walk_set_input(ctx, window, SK_UI_POINTER_EVENTS_AUTO, 1);
}

static void ui_dock_style_float(sk_ui_context_t* ctx, sk_ui_node_t window, f32 x, f32 y, f32 w, f32 h, i32 z) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(x);
	p.layout.top = sk_ui_pt(y);
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.flex_grow = 0.0f;
	p.layout.flex_shrink = 0.0f;
	ui_dock_merge_layout(ctx, window, &p);
	(void)ui->node_set_prop_i32(ctx, window, "z_index", z);
	ui_dock_set_hidden(ctx, window, 0);
	ui_dock_title_hidden(ctx, window, 0);
	ui_dock_walk_set_input(ctx, window, SK_UI_POINTER_EVENTS_AUTO, 1);
}

static void ui_dock_style_stash(sk_ui_context_t* ctx, sk_ui_node_t window) {
	const sk_ui_api_t* ui = ui_get_api_table();
	ui_dock_set_hidden(ctx, window, 1);
	ui_dock_walk_set_input(ctx, window, SK_UI_POINTER_EVENTS_NONE, 0);
	if (sk_ui_node_is_valid(ctx->focus)) {
		sk_ui_node_t cur = ctx->focus;
		while (sk_ui_node_is_valid(cur)) {
			if (sk_ui_node_eq(cur, window)) {
				(void)ui->focus_set(ctx, SK_UI_NODE_INVALID);
				break;
			}
			cur = ui->node_parent(ctx, cur);
		}
	}
}

static u32 ui_dock_float_count(const sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = ui_get_api_table();
	const ui_node_slot_t* slot;
	u32 i;
	u32 n = 0u;
	if (!sk_ui_node_is_valid(ctx->dock_overlay)) {
		return 0u;
	}
	slot = ui_slot(ctx, ctx->dock_overlay);
	if (slot == NULL) {
		return 0u;
	}
	for (i = 0u; i < slot->children.count; ++i) {
		if (ui_dock_is_widget(ctx, slot->children.items[i], "editor_window") != 0) {
			n += 1u;
		}
	}
	(void)ui;
	return n;
}

static void ui_dock_raise_float(sk_ui_context_t* ctx, sk_ui_node_t window) {
	const sk_ui_api_t* ui = ui_get_api_table();
	u32 count;
	if (!sk_ui_node_is_valid(ctx->dock_overlay) || !sk_ui_node_is_valid(window)) {
		return;
	}
	if (!sk_ui_node_eq(ui->node_parent(ctx, window), ctx->dock_overlay)) {
		(void)ui->node_reparent(ctx, window, ctx->dock_overlay, UI_DOCK_APPEND);
	} else {
		count = ui->node_child_count(ctx, ctx->dock_overlay);
		if (count > 0u) {
			(void)ui->node_set_child_index(ctx, ctx->dock_overlay, window, count - 1u);
		}
	}
	(void)ui->node_set_prop_i32(ctx, window, "z_index", UI_DOCK_FLOAT_Z + (i32)ui_dock_float_count(ctx));
}

static sk_ui_node_t ui_dock_ensure_layer(sk_ui_context_t* ctx, sk_ui_node_t existing, const_chr_t id, i32 stash) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_node_t n = existing;
	if (!sk_ui_node_is_valid(n) || ui_slot(ctx, n) == NULL) {
		n = ui_widget_view_impl(ctx, ctx->root, id);
		if (!sk_ui_node_is_valid(n)) {
			return SK_UI_NODE_INVALID;
		}
	} else if (!sk_ui_node_eq(ui->node_parent(ctx, n), ctx->root)) {
		(void)ui->node_reparent(ctx, n, ctx->root, UI_DOCK_APPEND);
	}
	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_POSITION | SK_UI_SP_FLEX_GROW;
		p.layout.position = SK_UI_POSITION_ABSOLUTE;
		p.layout.flex_grow = 0.0f;
		if (stash != 0) {
			p.mask |= SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_LEFT | SK_UI_SP_TOP;
			p.layout.width = sk_ui_pt(0.0f);
			p.layout.height = sk_ui_pt(0.0f);
			p.layout.left = sk_ui_pt(0.0f);
			p.layout.top = sk_ui_pt(0.0f);
		}
		ui_dock_merge_layout(ctx, n, &p);
	}
	if (stash != 0) {
		(void)ui->node_set_clip_children(ctx, n, 1);
		(void)ui->node_set_pointer_events(ctx, n, SK_UI_POINTER_EVENTS_NONE);
		ui_dock_set_hidden(ctx, n, 1);
	} else {
		(void)ui->node_set_pointer_events(ctx, n, SK_UI_POINTER_EVENTS_AUTO);
		ui_dock_set_hidden(ctx, n, 0);
	}
	return n;
}

static void ui_dock_ensure_root_layers(sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = ui_get_api_table();
	i32 created_stash = (!sk_ui_node_is_valid(ctx->dock_stash) || ui_slot(ctx, ctx->dock_stash) == NULL) ? 1 : 0;
	i32 created_overlay = (!sk_ui_node_is_valid(ctx->dock_overlay) || ui_slot(ctx, ctx->dock_overlay) == NULL) ? 1 : 0;
	ctx->dock_stash = ui_dock_ensure_layer(ctx, ctx->dock_stash, "ui-dock-stash", 1);
	ctx->dock_overlay = ui_dock_ensure_layer(ctx, ctx->dock_overlay, "ui-dock-overlay", 0);
	if ((created_stash != 0 || created_overlay != 0) && sk_ui_node_is_valid(ctx->dock_stash) && sk_ui_node_is_valid(ctx->dock_overlay)) {
		u32 n = ui->node_child_count(ctx, ctx->root);
		if (n > 0u) {
			(void)ui->node_set_child_index(ctx, ctx->root, ctx->dock_stash, n - 1u);
			n = ui->node_child_count(ctx, ctx->root);
			(void)ui->node_set_child_index(ctx, ctx->root, ctx->dock_overlay, n - 1u);
		}
	}
}

static sk_ui_node_t ui_dock_ensure_named(sk_ui_context_t* ctx, sk_ui_node_t parent, sk_ui_node_t existing, const_chr_t id, i32 kind) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_node_t n = existing;
	if (sk_ui_node_is_valid(n) && ui_slot(ctx, n) != NULL) {
		if (!sk_ui_node_eq(ui->node_parent(ctx, n), parent)) {
			(void)ui->node_reparent(ctx, n, parent, UI_DOCK_APPEND);
		}
		return n;
	}
	if (kind == 0) {
		n = ui_widget_dock_node_impl(ctx, parent, 0, id);
	} else if (kind == 1) {
		n = ui_widget_splitter_impl(ctx, parent, 0, id);
	} else if (kind == 2) {
		n = ui_widget_tab_bar_impl(ctx, parent, id);
	} else {
		n = ui_widget_view_impl(ctx, parent, id);
	}
	return n;
}

static void ui_dock_tab_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);
static void ui_dock_tab_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user);
static void ui_dock_splitter_changed(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user);
static void ui_dock_refresh_drop(sk_ui_context_t* ctx, ui_dockspace_t* space);
static void ui_dock_hit_drop(const sk_ui_context_t* ctx, f32 x, f32 y, sk_ui_dock_node_t* out_node, sk_ui_dock_dir_t* out_dir);

static sk_ui_node_t ui_dock_ensure_tab(sk_ui_context_t* ctx, sk_ui_node_t tab_bar, const_chr_t window_id, const_chr_t label) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_node_t tab = SK_UI_NODE_INVALID;
	sk_ui_node_callbacks_t cbs;
	char tab_id[96];
	if (window_id == NULL || window_id[0] == '\0') {
		return SK_UI_NODE_INVALID;
	}
	if (sk_hash_map_get(&ctx->dock_tab_nodes, window_id, &tab) != 0 || ui_slot(ctx, tab) == NULL) {
		(void)snprintf(tab_id, sizeof(tab_id), "%s-dock-tab", window_id);
		tab = ui_widget_tab_impl(ctx, tab_bar, label != NULL ? label : window_id, tab_id);
		if (sk_ui_node_is_valid(tab)) {
			(void)sk_hash_map_put(&ctx->dock_tab_nodes, window_id, tab);
		}
	} else if (!sk_ui_node_eq(ui->node_parent(ctx, tab), tab_bar)) {
		(void)ui->node_reparent(ctx, tab, tab_bar, UI_DOCK_APPEND);
	}
	if (!sk_ui_node_is_valid(tab)) {
		return tab;
	}
	(void)ui->node_set_prop_str(ctx, tab, "text", label != NULL ? label : window_id);
	(void)ui->node_set_prop_str(ctx, tab, "dock_window_id", window_id);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ui_dock_tab_on_click;
	cbs.on_event = ui_dock_tab_on_event;
	(void)ui->node_set_callbacks(ctx, tab, &cbs);
	ui_dock_set_hidden(ctx, tab, 0);
	return tab;
}

static const_chr_t ui_dock_window_title(const sk_ui_context_t* ctx, sk_ui_node_t window, const_chr_t fallback) {
	sk_ui_node_t bar = ui_editor_window_title_bar_impl(ctx, window);
	const_chr_t text = ui_dock_prop_str(ctx, bar, "text");
	if (text != NULL && text[0] != '\0') {
		return text;
	}
	return fallback;
}

static void ui_dock_bind_splitter(sk_ui_context_t* ctx, sk_ui_node_t splitter, sk_ui_dock_node_t split, i32 axis) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_style_props_t p;
	(void)ui->node_set_prop_i32(ctx, splitter, "axis", axis);
	(void)ui->node_set_prop_i32(ctx, splitter, "dock_index", (i32)split.index);
	(void)ui->node_set_prop_i32(ctx, splitter, "dock_generation", (i32)split.generation);
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
	if (axis != 0) {
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.height = sk_ui_pt(SK_UI_DOCK_SPLITTER_PT);
	} else {
		p.layout.width = sk_ui_pt(SK_UI_DOCK_SPLITTER_PT);
		p.layout.height = sk_ui_percent(100.0f);
	}
	p.layout.flex_grow = 0.0f;
	p.layout.flex_shrink = 0.0f;
	ui_dock_merge_layout(ctx, splitter, &p);
	if (ui_dock_prop_i32(ctx, splitter, "dock_bound", 0) == 0) {
		(void)ui_splitter_set_on_change_impl(ctx, splitter, ui_dock_splitter_changed, NULL);
		(void)ui->node_set_prop_i32(ctx, splitter, "dock_bound", 1);
	}
}

static sk_ui_node_t ui_dock_ensure_drop(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_node_t drop = space->drop;
	sk_ui_node_t preview;
	char id[80];
	sk_ui_style_props_t style;
	u32 z;
	if (!sk_ui_node_is_valid(space->host)) {
		return SK_UI_NODE_INVALID;
	}
	(void)snprintf(id, sizeof(id), "%s-drop", space->id != NULL ? space->id : "dock");
	if (!sk_ui_node_is_valid(drop) || ui_slot(ctx, drop) == NULL) {
		drop = ui_widget_view_impl(ctx, space->host, id);
		space->drop = drop;
	} else if (!sk_ui_node_eq(ui->node_parent(ctx, drop), space->host)) {
		(void)ui->node_reparent(ctx, drop, space->host, UI_DOCK_APPEND);
	}
	if (!sk_ui_node_is_valid(drop)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->node_set_prop_str(ctx, drop, "widget", "dock_drop");
	(void)ui->node_set_prop_i32(ctx, drop, "z_index", UI_DOCK_DROP_Z);
	{
		sk_ui_style_props_t fill;
		ui_style_props_clear(&fill);
		fill.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
		fill.layout.position = SK_UI_POSITION_ABSOLUTE;
		fill.layout.left = sk_ui_pt(0.0f);
		fill.layout.top = sk_ui_pt(0.0f);
		fill.layout.width = sk_ui_percent(100.0f);
		fill.layout.height = sk_ui_percent(100.0f);
		fill.layout.flex_grow = 0.0f;
		fill.layout.flex_shrink = 0.0f;
		ui_dock_merge_layout(ctx, drop, &fill);
	}
	(void)snprintf(id, sizeof(id), "%s-drop-preview", space->id != NULL ? space->id : "dock");
	preview = ui->find_by_id(ctx, id);
	if (!sk_ui_node_is_valid(preview)) {
		preview = ui_widget_view_impl(ctx, drop, id);
	}
	if (sk_ui_node_is_valid(preview)) {
		memset(&style, 0, sizeof(style));
		style.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS;
		style.background_color = sk_ui_rgba(0.25f, 0.55f, 0.95f, 0.35f);
		style.border_color = sk_ui_rgba(0.40f, 0.70f, 1.00f, 0.85f);
		style.layout.border.left = 2.0f;
		style.layout.border.top = 2.0f;
		style.layout.border.right = 2.0f;
		style.layout.border.bottom = 2.0f;
		style.corner_radius = 3.0f;
		(void)ui->node_merge_inline_style(ctx, preview, &style);
		(void)ui->node_set_prop_str(ctx, preview, "widget", "dock_drop_preview");
	}
	for (z = 0u; z < 9u; ++z) {
		static const char* const k_zone[9] = {"center", "left", "right", "up", "down", "outer-left", "outer-right", "outer-up", "outer-down"};
		sk_ui_node_t zone;
		(void)snprintf(id, sizeof(id), "%s-zone-%s", space->id != NULL ? space->id : "dock", k_zone[z]);
		zone = ui->find_by_id(ctx, id);
		if (!sk_ui_node_is_valid(zone)) {
			zone = ui_widget_view_impl(ctx, drop, id);
		}
		if (sk_ui_node_is_valid(zone)) {
			memset(&style, 0, sizeof(style));
			style.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS;
			style.background_color = sk_ui_rgba(0.20f, 0.40f, 0.80f, 0.45f);
			style.border_color = sk_ui_rgba(0.85f, 0.92f, 1.00f, 0.90f);
			style.layout.border.left = 1.0f;
			style.layout.border.top = 1.0f;
			style.layout.border.right = 1.0f;
			style.layout.border.bottom = 1.0f;
			style.corner_radius = 2.0f;
			(void)ui->node_merge_inline_style(ctx, zone, &style);
			(void)ui->node_set_prop_str(ctx, zone, "widget", "dock_drop_zone");
			ui_dock_set_hidden(ctx, zone, 1);
		}
	}
	{
		u32 count = ui->node_child_count(ctx, space->host);
		if (count > 0u) {
			(void)ui->node_set_child_index(ctx, space->host, drop, count - 1u);
		}
	}
	return drop;
}

static void ui_dock_place_zone(sk_ui_context_t* ctx, ui_dockspace_t* space, const_chr_t suffix, const sk_ui_rect_t* host, f32 x, f32 y, f32 w, f32 h, i32 show) {
	const sk_ui_api_t* ui = ui_get_api_table();
	char id[80];
	sk_ui_node_t zone;
	(void)snprintf(id, sizeof(id), "%s-zone-%s", space->id != NULL ? space->id : "dock", suffix);
	zone = ui->find_by_id(ctx, id);
	if (!sk_ui_node_is_valid(zone)) {
		return;
	}
	if (show == 0) {
		ui_dock_set_hidden(ctx, zone, 1);
		return;
	}
	ui_dock_set_hidden(ctx, zone, 0);
	ui_dock_set_abs_box(ctx, zone, x - host->x, y - host->y, w, h);
}

static void ui_dock_refresh_drop(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_node_t drop;
	sk_ui_node_t preview;
	sk_ui_rect_t host_r;
	sk_ui_rect_t target_r;
	sk_ui_rect_t preview_r;
	char id[80];
	i32 live;
	i32 i;
	static const char* const k_zone[9] = {"center", "left", "right", "up", "down", "outer-left", "outer-right", "outer-up", "outer-down"};

	drop = ui_dock_ensure_drop(ctx, space);
	if (!sk_ui_node_is_valid(drop)) {
		return;
	}
	live = (ctx->dock_drag_active != 0u && ctx->dock_drag_torn != 0u) ? 1 : 0;
	ui_dock_set_hidden(ctx, drop, live == 0 ? 1 : 0);
	(void)ui->node_set_pointer_events(ctx, drop, live != 0 ? SK_UI_POINTER_EVENTS_AUTO : SK_UI_POINTER_EVENTS_NONE);
	(void)snprintf(id, sizeof(id), "%s-drop-preview", space->id != NULL ? space->id : "dock");
	preview = ui->find_by_id(ctx, id);
	if (live == 0 || ui_dock_space_abs_rect(ctx, space, &host_r) != 0) {
		for (i = 0; i < 9; ++i) {
			ui_dock_place_zone(ctx, space, k_zone[i], &host_r, 0.0f, 0.0f, 0.0f, 0.0f, 0);
		}
		if (sk_ui_node_is_valid(preview)) {
			ui_dock_set_hidden(ctx, preview, 1);
		}
		return;
	}
	if (sk_ui_dock_node_is_valid(ctx->dock_drag_hover) && ui_dock_slot_abs_rect(ctx, ui_dock_slot(ctx, ctx->dock_drag_hover), &target_r) == 0) {
		preview_r = ui_dock_dir_preview_rect(&target_r, ctx->dock_drag_dir);
		if (sk_ui_node_is_valid(preview)) {
			ui_dock_set_hidden(ctx, preview, 0);
			ui_dock_set_abs_box(ctx, preview, preview_r.x - host_r.x, preview_r.y - host_r.y, preview_r.width, preview_r.height);
		}
	} else if (sk_ui_node_is_valid(preview)) {
		ui_dock_set_hidden(ctx, preview, 1);
	}

	{
		const ui_dock_slot_t* hover = ui_dock_slot(ctx, ctx->dock_drag_hover);
		sk_ui_rect_t leaf_r;
		f32 zx;
		f32 zy;
		f32 zs = UI_DOCK_ZONE_PT;
		i32 show_inner = 0;
		if (hover != NULL && ui_dock_slot_abs_rect(ctx, hover, &leaf_r) == 0) {
			show_inner = 1;
			zx = leaf_r.x + (leaf_r.width - zs) * 0.5f;
			zy = leaf_r.y + (leaf_r.height - zs) * 0.5f;
			ui_dock_place_zone(ctx, space, "center", &host_r, zx, zy, zs, zs, show_inner);
			ui_dock_place_zone(ctx, space, "left", &host_r, leaf_r.x + 4.0f, zy, zs, zs, show_inner);
			ui_dock_place_zone(ctx, space, "right", &host_r, leaf_r.x + leaf_r.width - zs - 4.0f, zy, zs, zs, show_inner);
			ui_dock_place_zone(ctx, space, "up", &host_r, zx, leaf_r.y + 4.0f, zs, zs, show_inner);
			ui_dock_place_zone(ctx, space, "down", &host_r, zx, leaf_r.y + leaf_r.height - zs - 4.0f, zs, zs, show_inner);
		} else {
			ui_dock_place_zone(ctx, space, "center", &host_r, 0, 0, 0, 0, 0);
			ui_dock_place_zone(ctx, space, "left", &host_r, 0, 0, 0, 0, 0);
			ui_dock_place_zone(ctx, space, "right", &host_r, 0, 0, 0, 0, 0);
			ui_dock_place_zone(ctx, space, "up", &host_r, 0, 0, 0, 0, 0);
			ui_dock_place_zone(ctx, space, "down", &host_r, 0, 0, 0, 0, 0);
		}
		ui_dock_place_zone(ctx, space, "outer-left", &host_r, host_r.x, host_r.y + host_r.height * 0.5f - zs * 0.5f, UI_DOCK_OUTER_BAND_PT, zs, 1);
		ui_dock_place_zone(ctx, space, "outer-right", &host_r, host_r.x + host_r.width - UI_DOCK_OUTER_BAND_PT, host_r.y + host_r.height * 0.5f - zs * 0.5f, UI_DOCK_OUTER_BAND_PT,
						   zs, 1);
		ui_dock_place_zone(ctx, space, "outer-up", &host_r, host_r.x + host_r.width * 0.5f - zs * 0.5f, host_r.y, zs, UI_DOCK_OUTER_BAND_PT, 1);
		ui_dock_place_zone(ctx, space, "outer-down", &host_r, host_r.x + host_r.width * 0.5f - zs * 0.5f, host_r.y + host_r.height - UI_DOCK_OUTER_BAND_PT, zs,
						   UI_DOCK_OUTER_BAND_PT, 1);
	}
}

static void ui_dock_hit_drop(const sk_ui_context_t* ctx, f32 x, f32 y, sk_ui_dock_node_t* out_node, sk_ui_dock_dir_t* out_dir) {
	u32 s;
	sk_ui_dock_node_t node = SK_UI_DOCK_NODE_INVALID;
	sk_ui_dock_dir_t dir = SK_UI_DOCK_DIR_NONE;
	for (s = 0u; s < ctx->dockspace_count; ++s) {
		const ui_dockspace_t* space = &ctx->dockspaces[s];
		sk_ui_rect_t space_r;
		sk_ui_dock_node_t hit;
		sk_ui_dock_dir_t sug = SK_UI_DOCK_DIR_NONE;
		if (ui_dock_space_abs_rect(ctx, space, &space_r) != 0 || !ui_dock_rect_contains(&space_r, x, y)) {
			continue;
		}
		if (x < space_r.x + UI_DOCK_OUTER_BAND_PT) {
			node = space->root;
			dir = SK_UI_DOCK_DIR_LEFT;
			break;
		}
		if (x > space_r.x + space_r.width - UI_DOCK_OUTER_BAND_PT) {
			node = space->root;
			dir = SK_UI_DOCK_DIR_RIGHT;
			break;
		}
		if (y < space_r.y + UI_DOCK_OUTER_BAND_PT) {
			node = space->root;
			dir = SK_UI_DOCK_DIR_UP;
			break;
		}
		if (y > space_r.y + space_r.height - UI_DOCK_OUTER_BAND_PT) {
			node = space->root;
			dir = SK_UI_DOCK_DIR_DOWN;
			break;
		}
		hit = ui_dock_node_at_point_impl(ctx, x, y, &sug);
		if (sk_ui_dock_node_is_valid(hit)) {
			node = hit;
			dir = sug;
			break;
		}
		node = space->root;
		dir = SK_UI_DOCK_DIR_CENTER;
		break;
	}
	if (out_node != NULL) {
		*out_node = node;
	}
	if (out_dir != NULL) {
		*out_dir = dir;
	}
}

static void ui_dock_update_drag_hover(sk_ui_context_t* ctx, f32 x, f32 y) {
	u32 s;
	ui_dock_hit_drop(ctx, x, y, &ctx->dock_drag_hover, &ctx->dock_drag_dir);
	for (s = 0u; s < ctx->dockspace_count; ++s) {
		ui_dock_refresh_drop(ctx, &ctx->dockspaces[s]);
	}
}

static void ui_dock_clear_drag(sk_ui_context_t* ctx) {
	u32 s;
	ctx->dock_drag_active = 0u;
	ctx->dock_drag_torn = 0u;
	ctx->dock_drag_tab = SK_UI_NODE_INVALID;
	ctx->dock_drag_window = SK_UI_NODE_INVALID;
	ctx->dock_drag_hover = SK_UI_DOCK_NODE_INVALID;
	ctx->dock_drag_dir = SK_UI_DOCK_DIR_NONE;
	ctx->dock_drag_window_id[0] = '\0';
	for (s = 0u; s < ctx->dockspace_count; ++s) {
		ui_dock_refresh_drop(ctx, &ctx->dockspaces[s]);
	}
}

static void ui_dock_copy_id(char* dst, u32 cap, const_chr_t src) {
	size_t n;
	if (dst == NULL || cap == 0u) {
		return;
	}
	dst[0] = '\0';
	if (src == NULL) {
		return;
	}
	n = strlen(src);
	if (n >= (size_t)cap) {
		n = (size_t)cap - 1u;
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static void ui_dock_move_float_to_pointer(sk_ui_context_t* ctx, sk_ui_node_t window, f32 x, f32 y) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_layout_style_t ls;
	sk_ui_rect_t br;
	f32 w = UI_DOCK_FLOAT_W;
	f32 h = UI_DOCK_FLOAT_H;
	if (ui->node_get_abs_rect(ctx, window, &br, NULL) == 0 && br.width > 1.0f && br.height > 1.0f) {
		w = br.width;
		h = br.height;
	} else if (ui->node_get_layout_style(ctx, window, &ls) == 0) {
		if (ls.width.unit == SK_UI_LENGTH_POINT && ls.width.value > 1.0f) {
			w = ls.width.value;
		}
		if (ls.height.unit == SK_UI_LENGTH_POINT && ls.height.value > 1.0f) {
			h = ls.height.value;
		}
	}
	ui_dock_style_float(ctx, window, x - 40.0f, y - 12.0f, w, h, UI_DOCK_FLOAT_Z + (i32)ui_dock_float_count(ctx));
}

static i32 ui_dock_commit_drop(sk_ui_context_t* ctx) {
	sk_ui_dock_node_t node = ctx->dock_drag_hover;
	sk_ui_dock_dir_t dir = ctx->dock_drag_dir;
	const_chr_t id = ctx->dock_drag_window_id;
	if (id[0] == '\0' || !sk_ui_dock_node_is_valid(node) || dir == SK_UI_DOCK_DIR_NONE) {
		return -1;
	}
	return ui_dock_window_to_node_impl(ctx, id, node, dir);
}

// NOLINTBEGIN(misc-no-recursion)
static i32 ui_dock_apply_node(sk_ui_context_t* ctx, ui_dockspace_t* space, sk_ui_dock_node_t node, sk_ui_node_t parent) {
	const sk_ui_api_t* ui = ui_get_api_table();
	ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, node);
	char id[80];
	sk_ui_node_t host;
	if (slot == NULL || !sk_ui_node_is_valid(parent)) {
		return -1;
	}
	(void)snprintf(id, sizeof(id), "%s/%u", space->id != NULL ? space->id : "dock", node.index);
	host = ui_dock_ensure_named(ctx, parent, slot->host, id, 0);
	if (!sk_ui_node_is_valid(host)) {
		return -1;
	}
	slot->host = host;
	if (slot->kind == UI_DOCK_KIND_SPLIT_U8) {
		char sid[80];
		sk_ui_node_t split_bar;
		sk_ui_node_t c0;
		sk_ui_node_t c1;
		i32 axis = slot->axis == SK_UI_DOCK_SPLIT_VERTICAL ? 1 : 0;
		(void)ui->node_set_prop_i32(ctx, host, "orientation", axis);
		ui_dock_set_fill_flex(ctx, host, axis != 0 ? SK_UI_FLEX_COLUMN : SK_UI_FLEX_ROW, 1.0f);
		(void)snprintf(sid, sizeof(sid), "%s/%u-split", space->id != NULL ? space->id : "dock", node.index);
		split_bar = ui_dock_ensure_named(ctx, host, slot->splitter, sid, 1);
		slot->splitter = split_bar;
		if (sk_ui_node_is_valid(split_bar)) {
			ui_dock_bind_splitter(ctx, split_bar, node, axis);
			(void)ui->node_set_prop_f32(ctx, split_bar, "ratio", slot->ratio);
		}
		if (ui_dock_apply_node(ctx, space, slot->child[0], host) != 0) {
			return -1;
		}
		if (ui_dock_apply_node(ctx, space, slot->child[1], host) != 0) {
			return -1;
		}
		c0 = ui_dock_node_host_impl(ctx, slot->child[0]);
		c1 = ui_dock_node_host_impl(ctx, slot->child[1]);
		if (sk_ui_node_is_valid(c0)) {
			sk_ui_style_props_t p;
			ui_dock_set_fill_flex(ctx, c0, SK_UI_FLEX_COLUMN, slot->ratio);
			ui_style_props_clear(&p);
			if (axis == 0) {
				p.mask = SK_UI_SP_HEIGHT;
				p.layout.height = sk_ui_percent(100.0f);
			} else {
				p.mask = SK_UI_SP_WIDTH;
				p.layout.width = sk_ui_percent(100.0f);
			}
			ui_dock_merge_layout(ctx, c0, &p);
			(void)ui->node_set_child_index(ctx, host, c0, 0u);
		}
		if (sk_ui_node_is_valid(split_bar)) {
			(void)ui->node_set_child_index(ctx, host, split_bar, 1u);
		}
		if (sk_ui_node_is_valid(c1)) {
			sk_ui_style_props_t p;
			ui_dock_set_fill_flex(ctx, c1, SK_UI_FLEX_COLUMN, 1.0f - slot->ratio);
			ui_style_props_clear(&p);
			if (axis == 0) {
				p.mask = SK_UI_SP_HEIGHT;
				p.layout.height = sk_ui_percent(100.0f);
			} else {
				p.mask = SK_UI_SP_WIDTH;
				p.layout.width = sk_ui_percent(100.0f);
			}
			ui_dock_merge_layout(ctx, c1, &p);
			(void)ui->node_set_child_index(ctx, host, c1, 2u);
		}
		return 0;
	}

	{
		char tid[80];
		char cid[80];
		sk_ui_node_t bar;
		sk_ui_node_t content;
		u32 i;
		i32 show_tabs = ((slot->flags & SK_UI_DOCK_NODE_NO_TAB_BAR) == 0u && slot->tab_count > 0u) ? 1 : 0;
		ui_dock_set_fill_flex(ctx, host, SK_UI_FLEX_COLUMN, 1.0f);
		(void)ui->node_set_prop_i32(ctx, host, "orientation", 1);
		(void)snprintf(tid, sizeof(tid), "%s/%u-tabs", space->id != NULL ? space->id : "dock", node.index);
		bar = ui_dock_ensure_named(ctx, host, slot->tab_bar, tid, 2);
		slot->tab_bar = bar;
		if (sk_ui_node_is_valid(bar)) {
			sk_ui_style_props_t p;
			ui_style_props_clear(&p);
			p.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_HEIGHT;
			p.layout.width = sk_ui_percent(100.0f);
			p.layout.flex_grow = 0.0f;
			p.layout.min_height = sk_ui_pt(26.0f);
			ui_dock_merge_layout(ctx, bar, &p);
			ui_dock_set_hidden(ctx, bar, show_tabs == 0 ? 1 : 0);
		}
		(void)snprintf(cid, sizeof(cid), "%s/%u-content", space->id != NULL ? space->id : "dock", node.index);
		content = ui_dock_ensure_named(ctx, host, slot->content, cid, 3);
		slot->content = content;
		if (sk_ui_node_is_valid(content)) {
			sk_ui_style_props_t p;
			(void)ui->node_set_clip_children(ctx, content, 1);
			ui_style_props_clear(&p);
			p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH;
			p.layout.flex_direction = SK_UI_FLEX_COLUMN;
			p.layout.flex_grow = 1.0f;
			p.layout.width = sk_ui_percent(100.0f);
			ui_dock_merge_layout(ctx, content, &p);
		}
		if ((space->flags & SK_UI_DOCKSPACE_PASSTHRU_CENTER) != 0u && (slot->flags & SK_UI_DOCK_NODE_CENTRAL) != 0u && slot->tab_count == 0u) {
			(void)ui->node_set_pointer_events(ctx, host, SK_UI_POINTER_EVENTS_NONE);
		} else {
			(void)ui->node_set_pointer_events(ctx, host, SK_UI_POINTER_EVENTS_AUTO);
		}
		if (sk_ui_node_is_valid(bar) && show_tabs != 0) {
			sk_ui_node_t active_tab = SK_UI_NODE_INVALID;
			for (i = 0u; i < slot->tab_count; ++i) {
				sk_ui_node_t win = ui->find_by_id(ctx, slot->tabs[i]);
				const_chr_t label = ui_dock_window_title(ctx, win, slot->tabs[i]);
				sk_ui_node_t tab = ui_dock_ensure_tab(ctx, bar, slot->tabs[i], label);
				if (sk_ui_node_is_valid(tab)) {
					(void)ui->node_set_child_index(ctx, bar, tab, i);
					if (i == slot->active_index) {
						active_tab = tab;
					}
				}
			}
			if (sk_ui_node_is_valid(active_tab)) {
				(void)ui_tab_bar_set_active_impl(ctx, bar, active_tab);
			}
			(void)ui->node_set_child_index(ctx, host, bar, 0u);
		}
		if (sk_ui_node_is_valid(content)) {
			(void)ui->node_set_child_index(ctx, host, content, sk_ui_node_is_valid(bar) ? 1u : 0u);
		}
		for (i = 0u; i < slot->tab_count; ++i) {
			sk_ui_node_t win = ui->find_by_id(ctx, slot->tabs[i]);
			if (!sk_ui_node_is_valid(win)) {
				continue;
			}
			if (i == slot->active_index && sk_ui_node_is_valid(content)) {
				if (!sk_ui_node_eq(ui->node_parent(ctx, win), content)) {
					(void)ui->node_reparent(ctx, win, content, UI_DOCK_APPEND);
				}
				ui_dock_style_docked(ctx, win);
			} else if (sk_ui_node_is_valid(ctx->dock_stash)) {
				if (!sk_ui_node_eq(ui->node_parent(ctx, win), ctx->dock_stash)) {
					(void)ui->node_reparent(ctx, win, ctx->dock_stash, UI_DOCK_APPEND);
				}
				ui_dock_style_stash(ctx, win);
			}
		}
	}
	return 0;
}
// NOLINTEND(misc-no-recursion)

static void ui_dock_hide_unused_tabs(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_dock_node_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	if (!sk_ui_dock_node_is_valid(space->root)) {
		return;
	}
	stack[sp++] = space->root;
	while (sp > 0u) {
		sk_ui_dock_node_t cur = stack[--sp];
		const ui_dock_slot_t* slot = ui_dock_slot(ctx, cur);
		u32 i;
		if (slot == NULL) {
			continue;
		}
		if (slot->kind == UI_DOCK_KIND_SPLIT_U8) {
			if (sk_ui_dock_node_is_valid(slot->child[1]) && sp < UI_DOCK_WALK_MAX) {
				stack[sp++] = slot->child[1];
			}
			if (sk_ui_dock_node_is_valid(slot->child[0]) && sp < UI_DOCK_WALK_MAX) {
				stack[sp++] = slot->child[0];
			}
			continue;
		}
		if (!sk_ui_node_is_valid(slot->tab_bar)) {
			continue;
		}
		{
			const ui_node_slot_t* bar = ui_slot(ctx, slot->tab_bar);
			if (bar == NULL) {
				continue;
			}
			for (i = 0u; i < bar->children.count; ++i) {
				sk_ui_node_t tab = bar->children.items[i];
				const_chr_t wid = ui_dock_prop_str(ctx, tab, "dock_window_id");
				i32 keep = 0;
				u32 t;
				if (wid == NULL) {
					continue;
				}
				for (t = 0u; t < slot->tab_count; ++t) {
					if (ui_dock_cstr_eq(slot->tabs[t], wid)) {
						keep = 1;
						break;
					}
				}
				if (keep == 0) {
					if (ui_dock_node_protected(ctx, tab) != 0 || sk_ui_node_eq(ctx->dock_drag_tab, tab)) {
						ui_dock_set_hidden(ctx, tab, 1);
					} else {
						(void)sk_hash_map_remove(&ctx->dock_tab_nodes, wid);
						(void)ui->node_destroy(ctx, tab);
						bar = ui_slot(ctx, slot->tab_bar);
						if (bar == NULL) {
							break;
						}
						i = (i == 0u) ? 0u : i - 1u;
					}
				}
			}
		}
	}
}

i32 ui_dockspace_apply_impl(sk_ui_context_t* ctx, sk_ui_dock_node_t dockspace) {
	ui_dockspace_t* space = ui_dock_space_for_node(ctx, dockspace);
	if (space == NULL) {
		return 0;
	}
	ctx->dock_applying = 1;
	ui_dock_ensure_root_layers(ctx);
	if (sk_ui_node_is_valid(space->host)) {
		ui_dock_salvage_windows(ctx, space->host, ctx->dock_stash);
	}
	ui_dock_resolve_pending(ctx, space);
	ui_dock_collapse_space(ctx, space);
	if (sk_ui_node_is_valid(space->host) && sk_ui_dock_node_is_valid(space->root)) {
		if (ui_dock_apply_node(ctx, space, space->root, space->host) != 0) {
			ctx->dock_applying = 0;
			return -1;
		}
		{
			sk_ui_node_t root_host = ui_dock_node_host_impl(ctx, space->root);
			if (sk_ui_node_is_valid(root_host)) {
				(void)ui_get_api_table()->node_set_child_index(ctx, space->host, root_host, 0u);
			}
		}
	}
	ui_dock_hide_unused_tabs(ctx, space);
	(void)ui_dock_ensure_drop(ctx, space);
	ui_dock_refresh_drop(ctx, space);
	if (space->laid_out != 0u) {
		ui_dock_layout_tree(ctx, space->root, &space->last_rect);
	} else if (sk_ui_node_is_valid(space->host)) {
		sk_ui_rect_t hr;
		if (ui_node_get_abs_rect_impl(ctx, space->host, &hr, NULL) == 0 && hr.width > 0.5f && hr.height > 0.5f) {
			space->last_rect = hr;
			space->laid_out = 1u;
			ui_dock_layout_tree(ctx, space->root, &space->last_rect);
		}
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
	ui_dock_ensure_root_layers(ctx);
	if (sk_ui_node_is_valid(space->host)) {
		ui_dock_salvage_windows(ctx, space->host, ctx->dock_stash);
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
	{
		const sk_ui_api_t* ui = ui_get_api_table();
		sk_ui_node_t win = ui->find_by_id(ctx, window_id);
		sk_ui_rect_t src;
		f32 x = ctx->pointer_x;
		f32 y = ctx->pointer_y;
		f32 w = UI_DOCK_FLOAT_W;
		f32 h = UI_DOCK_FLOAT_H;
		if (ui_dock_slot_abs_rect(ctx, slot, &src) == 0) {
			if (!(ctx->dock_drag_active != 0u)) {
				x = src.x + 24.0f;
				y = src.y + 24.0f;
			}
			if (src.width > 40.0f) {
				w = src.width;
			}
			if (src.height > 40.0f) {
				h = src.height;
			}
		}
		if (sk_ui_node_is_valid(win) && ui->node_get_abs_rect(ctx, win, &src, NULL) == 0 && src.width > 1.0f) {
			w = src.width;
			h = src.height;
		}
		if (ui_dock_remove_window(ctx, window_id) != 0) {
			return -1;
		}
		ui_dock_ensure_root_layers(ctx);
		if (sk_ui_node_is_valid(win) && sk_ui_node_is_valid(ctx->dock_overlay)) {
			(void)ui->node_reparent(ctx, win, ctx->dock_overlay, UI_DOCK_APPEND);
			ui_dock_style_float(ctx, win, x, y, w, h, UI_DOCK_FLOAT_Z + (i32)ui_dock_float_count(ctx));
			ui_dock_raise_float(ctx, win);
		}
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
	{
		const sk_ui_api_t* ui = ui_get_api_table();
		sk_ui_node_t win = ui->find_by_id(ctx, window_id);
		if (ui_dock_remove_window(ctx, window_id) != 0) {
			return -1;
		}
		ui_dock_ensure_root_layers(ctx);
		if (sk_ui_node_is_valid(win) && sk_ui_node_is_valid(ctx->dock_stash)) {
			(void)ui->node_reparent(ctx, win, ctx->dock_stash, UI_DOCK_APPEND);
			ui_dock_style_stash(ctx, win);
		}
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

static void ui_dock_tab_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const_chr_t id = ui_dock_prop_str(ctx, node, "dock_window_id");
	(void)user;
	if (id != NULL && id[0] != '\0' && ctx->dock_drag_torn == 0u) {
		(void)ui_dock_tab_set_active_impl(ctx, id);
	}
	if (event != NULL) {
		event->consumed = 1;
	}
}

static void ui_dock_tab_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	const sk_ui_api_t* ui = ui_get_api_table();
	const_chr_t id = ui_dock_prop_str(ctx, node, "dock_window_id");
	sk_ui_node_t bar;
	sk_ui_rect_t bar_r;
	f32 dx;
	f32 dy;
	(void)user;
	if (event == NULL || id == NULL || id[0] == '\0') {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN && event->button == SK_UI_POINTER_BUTTON_LEFT) {
		ctx->dock_drag_active = 1u;
		ctx->dock_drag_torn = 0u;
		ctx->dock_drag_tab = node;
		ctx->dock_drag_start_x = event->x;
		ctx->dock_drag_start_y = event->y;
		ui_dock_copy_id(ctx->dock_drag_window_id, (u32)sizeof(ctx->dock_drag_window_id), id);
		ctx->dock_drag_window = ui->find_by_id(ctx, id);
		(void)ui->pointer_capture_set(ctx, node);
		event->consumed = 1;
		return;
	}
	if (ctx->dock_drag_active == 0u || !sk_ui_node_eq(ctx->dock_drag_tab, node)) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_MOVE) {
		dx = event->x - ctx->dock_drag_start_x;
		dy = event->y - ctx->dock_drag_start_y;
		bar = ui->node_parent(ctx, node);
		if (ctx->dock_drag_torn == 0u) {
			i32 in_bar = 0;
			if (sk_ui_node_is_valid(bar) && ui->node_get_abs_rect(ctx, bar, &bar_r, NULL) == 0) {
				bar_r.y -= UI_DOCK_TEAR_PT;
				bar_r.height += UI_DOCK_TEAR_PT * 2.0f;
				in_bar = ui_dock_rect_contains(&bar_r, event->x, event->y);
			}
			if (in_bar != 0) {
				const ui_node_slot_t* bslot = ui_slot(ctx, bar);
				u32 i;
				u32 from = 0u;
				u32 to = 0u;
				sk_ui_dock_node_t leaf = ui_dock_find_node_for_window_impl(ctx, id);
				if (bslot != NULL && sk_ui_dock_node_is_valid(leaf)) {
					for (i = 0u; i < bslot->children.count; ++i) {
						if (sk_ui_node_eq(bslot->children.items[i], node)) {
							from = i;
						}
					}
					to = from;
					for (i = 0u; i < bslot->children.count; ++i) {
						sk_ui_rect_t tr;
						if (ui->node_get_abs_rect(ctx, bslot->children.items[i], &tr, NULL) != 0) {
							continue;
						}
						if (event->x >= tr.x && event->x < tr.x + tr.width) {
							to = i;
						}
					}
					if (to != from) {
						(void)ui_dock_tab_reorder_impl(ctx, leaf, from, to);
					}
				}
			} else if ((dx * dx + dy * dy) >= (UI_DOCK_TEAR_PT * UI_DOCK_TEAR_PT)) {
				sk_ui_node_t win = ui->find_by_id(ctx, id);
				if (ui_dock_window_undock_impl(ctx, id) == 0) {
					ctx->dock_drag_torn = 1u;
					ctx->dock_drag_window = sk_ui_node_is_valid(win) ? win : ui->find_by_id(ctx, id);
					if (sk_ui_node_is_valid(ctx->dock_drag_window)) {
						ui_dock_raise_float(ctx, ctx->dock_drag_window);
						ui_dock_move_float_to_pointer(ctx, ctx->dock_drag_window, event->x, event->y);
					}
					ui_dock_update_drag_hover(ctx, event->x, event->y);
				}
			}
		} else if (sk_ui_node_is_valid(ctx->dock_drag_window)) {
			ui_dock_move_float_to_pointer(ctx, ctx->dock_drag_window, event->x, event->y);
			ui_dock_update_drag_hover(ctx, event->x, event->y);
		}
		event->consumed = 1;
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		if (ctx->dock_drag_torn != 0u) {
			ui_dock_update_drag_hover(ctx, event->x, event->y);
			(void)ui_dock_commit_drop(ctx);
		}
		if (sk_ui_node_eq(ui->pointer_capture_get(ctx), node)) {
			(void)ui->pointer_capture_set(ctx, SK_UI_NODE_INVALID);
		}
		ui_dock_clear_drag(ctx);
		event->consumed = 1;
	}
}

static void ui_dock_splitter_changed(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	sk_ui_dock_node_t split;
	(void)user;
	split.index = (u32)ui_dock_prop_i32(ctx, node, "dock_index", 0);
	split.generation = (u32)ui_dock_prop_i32(ctx, node, "dock_generation", 0);
	if (sk_ui_dock_node_is_valid(split)) {
		(void)ui_dock_split_set_ratio_impl(ctx, split, value);
	}
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
		sk_ui_rect_t space_r;
		if (ui_dock_space_abs_rect(ctx, space, &space_r) != 0 || !ui_dock_rect_contains(&space_r, x, y)) {
			continue;
		}
		stack[sp++] = space->root;
		while (sp > 0u) {
			sk_ui_dock_node_t cur = stack[--sp];
			const ui_dock_slot_t* slot = ui_dock_slot(ctx, cur);
			sk_ui_rect_t box;
			if (slot == NULL || ui_dock_slot_abs_rect(ctx, slot, &box) != 0 || !ui_dock_rect_contains(&box, x, y)) {
				continue;
			}
			if (slot->kind == UI_DOCK_KIND_LEAF_U8) {
				if (out_dir != NULL) {
					*out_dir = ui_dock_suggest_dir(&box, x, y);
				}
				return cur;
			}
			if (ui_dock_rect_contains(&slot->splitter_rect, x, y)) {
				if (out_dir != NULL) {
					*out_dir = ui_dock_suggest_dir(&box, x, y);
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

static ui_dock_window_reg_t* ui_dock_reg_find(sk_ui_context_t* ctx, const_chr_t window_id) {
	u32 i;
	if (window_id == NULL || window_id[0] == '\0') {
		return NULL;
	}
	for (i = 0u; i < ctx->dock_reg_count; ++i) {
		if (ui_dock_cstr_eq(ctx->dock_regs[i].id, window_id)) {
			return &ctx->dock_regs[i];
		}
	}
	return NULL;
}

static const ui_dock_window_reg_t* ui_dock_reg_find_const(const sk_ui_context_t* ctx, const_chr_t window_id) {
	u32 i;
	if (window_id == NULL || window_id[0] == '\0') {
		return NULL;
	}
	for (i = 0u; i < ctx->dock_reg_count; ++i) {
		if (ui_dock_cstr_eq(ctx->dock_regs[i].id, window_id)) {
			return &ctx->dock_regs[i];
		}
	}
	return NULL;
}

static i32 ui_dock_window_known(const sk_ui_context_t* ctx, const_chr_t window_id) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_node_t win;
	if (window_id == NULL || window_id[0] == '\0') {
		return 0;
	}
	if (ui_dock_reg_find_const(ctx, window_id) != NULL) {
		return 1;
	}
	win = ui->find_by_id(ctx, window_id);
	return sk_ui_node_is_valid(win) ? 1 : 0;
}

static sk_ui_dock_node_t ui_dock_find_stable(const sk_ui_context_t* ctx, sk_ui_dock_node_t root, const_chr_t id) {
	sk_ui_dock_node_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	if (id == NULL || id[0] == '\0' || !sk_ui_dock_node_is_valid(root)) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	stack[sp++] = root;
	while (sp > 0u) {
		sk_ui_dock_node_t cur = stack[--sp];
		const ui_dock_slot_t* slot = ui_dock_slot(ctx, cur);
		if (slot == NULL) {
			continue;
		}
		if (ui_dock_cstr_eq(slot->stable_id, id)) {
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

static i32 ui_dock_window_is_placed(const sk_ui_context_t* ctx, const_chr_t window_id) {
	const sk_ui_api_t* ui;
	sk_ui_node_t win;
	sk_ui_dock_node_t leaf = SK_UI_DOCK_NODE_INVALID;
	if (window_id == NULL || window_id[0] == '\0') {
		return 0;
	}
	if (sk_hash_map_get(&SK_CONST_CAST(sk_ui_context_t*, ctx)->dock_window_map, window_id, &leaf) == 0 && sk_ui_dock_node_is_valid(leaf)) {
		return 1;
	}
	ui = ui_get_api_table();
	win = ui->find_by_id(ctx, window_id);
	if (!sk_ui_node_is_valid(win) || !sk_ui_node_is_valid(ctx->dock_overlay)) {
		return 0;
	}
	return sk_ui_node_eq(ui->node_parent(ctx, win), ctx->dock_overlay) ? 1 : 0;
}

static void ui_dock_float_registered(sk_ui_context_t* ctx, const ui_dock_window_reg_t* reg) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_node_t win;
	if (reg == NULL || reg->id == NULL) {
		return;
	}
	win = ui->find_by_id(ctx, reg->id);
	if (!sk_ui_node_is_valid(win)) {
		return;
	}
	(void)ui_dock_remove_window(ctx, reg->id);
	ui_dock_ensure_root_layers(ctx);
	if (!sk_ui_node_is_valid(ctx->dock_overlay)) {
		return;
	}
	(void)ui->node_reparent(ctx, win, ctx->dock_overlay, UI_DOCK_APPEND);
	ui_dock_style_float(ctx, win, reg->x, reg->y, reg->w, reg->h, UI_DOCK_FLOAT_Z);
}

static void ui_dock_place_one_default(sk_ui_context_t* ctx, ui_dockspace_t* space, const ui_dock_window_reg_t* reg) {
	sk_ui_dock_node_t target;
	if (reg == NULL || space == NULL || ui_dock_window_is_placed(ctx, reg->id) != 0) {
		return;
	}
	if (reg->default_target != NULL && reg->default_target[0] != '\0') {
		target = ui_dock_find_stable(ctx, space->root, reg->default_target);
		if (!sk_ui_dock_node_is_valid(target) && ctx->dockspace_count > 0u) {
			u32 s;
			for (s = 0u; s < ctx->dockspace_count && !sk_ui_dock_node_is_valid(target); ++s) {
				target = ui_dock_find_stable(ctx, ctx->dockspaces[s].root, reg->default_target);
			}
		}
		if (!sk_ui_dock_node_is_valid(target)) {
			/* Also accept a live window id: dock beside that window. */
			target = ui_dock_find_node_for_window_impl(ctx, reg->default_target);
		}
		if (sk_ui_dock_node_is_valid(target) && ui_dock_bind_window(ctx, reg->id, target, SK_UI_DOCK_DIR_CENTER, 1) == 0) {
			ui_dock_mark_dirty(space);
			return;
		}
	}
	ui_dock_float_registered(ctx, reg);
	ui_dock_mark_dirty(space);
}

static void ui_dock_place_unplaced_registered(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	u32 i;
	if (space == NULL || ctx->dock_builder_open != 0 || ctx->dock_restoring != 0u) {
		return;
	}
	for (i = 0u; i < ctx->dock_reg_count; ++i) {
		ui_dock_place_one_default(ctx, space, &ctx->dock_regs[i]);
	}
}

/* Reclamp remaining split ratios after an empty sibling is removed so the
 * leftover span is a valid first-child fraction of whatever still exists. */
static void ui_dock_renormalize_split_ratios(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	sk_ui_dock_node_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	if (space == NULL || !sk_ui_dock_node_is_valid(space->root)) {
		return;
	}
	stack[sp++] = space->root;
	while (sp > 0u) {
		sk_ui_dock_node_t cur = stack[--sp];
		ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, cur);
		f32 span;
		if (slot == NULL || slot->kind != UI_DOCK_KIND_SPLIT_U8) {
			continue;
		}
		span = (slot->axis == SK_UI_DOCK_SPLIT_HORIZONTAL) ? slot->rect.width : slot->rect.height;
		slot->ratio = ui_dock_clamp_ratio(slot->ratio, span);
		if (sk_ui_dock_node_is_valid(slot->child[1]) && sp < UI_DOCK_WALK_MAX) {
			stack[sp++] = slot->child[1];
		}
		if (sk_ui_dock_node_is_valid(slot->child[0]) && sp < UI_DOCK_WALK_MAX) {
			stack[sp++] = slot->child[0];
		}
	}
}

/* APX-318: drop emptied structure, then place registered windows the document
 * did not mention. Collapse first so a default target that vanished floats. */
static void ui_dock_layout_reconcile_mismatches(sk_ui_context_t* ctx, ui_dockspace_t* space) {
	if (space == NULL) {
		return;
	}
	ui_dock_collapse_space(ctx, space);
	ui_dock_renormalize_split_ratios(ctx, space);
	ui_dock_place_unplaced_registered(ctx, space);
}

i32 ui_dock_window_register_impl(sk_ui_context_t* ctx, const_chr_t window_id, const_chr_t default_target, const sk_ui_rect_t* default_rect) {
	ui_dock_window_reg_t* reg;
	char* id_copy;
	char* target_copy = NULL;
	u32 s;
	if (window_id == NULL || window_id[0] == '\0') {
		return -1;
	}
	reg = ui_dock_reg_find(ctx, window_id);
	if (reg == NULL) {
		if (ctx->dock_reg_count >= SK_UI_DOCK_WINDOW_REG_MAX) {
			sk_log_warn(ui_logger_api(), ui_dock_logger(), "dock window register overflow (cap %u)", (u32)SK_UI_DOCK_WINDOW_REG_MAX);
			return -1;
		}
		id_copy = ui_dock_strdup(ctx->allocator, window_id);
		if (id_copy == NULL) {
			return -1;
		}
		reg = &ctx->dock_regs[ctx->dock_reg_count];
		memset(reg, 0, sizeof(*reg));
		reg->id = id_copy;
		ctx->dock_reg_count += 1u;
	}
	if (default_target != NULL && default_target[0] != '\0') {
		target_copy = ui_dock_strdup(ctx->allocator, default_target);
		if (target_copy == NULL) {
			return -1;
		}
	}
	if (reg->default_target != NULL) {
		ctx->allocator->free(ctx->allocator->instance, reg->default_target);
		reg->default_target = NULL;
	}
	reg->default_target = target_copy;
	if (default_rect != NULL) {
		reg->x = default_rect->x;
		reg->y = default_rect->y;
		reg->w = default_rect->width;
		reg->h = default_rect->height;
	} else {
		reg->x = 80.0f;
		reg->y = 60.0f;
		reg->w = UI_DOCK_FLOAT_W;
		reg->h = UI_DOCK_FLOAT_H;
	}
	for (s = 0u; s < ctx->dockspace_count; ++s) {
		ui_dock_place_unplaced_registered(ctx, &ctx->dockspaces[s]);
		if (ctx->dockspaces[s].dirty != 0u) {
			ui_dock_maybe_apply(ctx, &ctx->dockspaces[s]);
		}
	}
	return 0;
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

/* -------------------------------------------------------------------------- */
/* Persist format v1 (JSON via sk_json_archive_writer / reader)               */
/* Schema: docs/ui-dock-layout-format.md + sk_ui_dock_layout_* in ui.h        */
/*                                                                            */
/* Version policy: accept only SK_UI_DOCK_LAYOUT_VERSION. Newer and older     */
/* documents are rejected (no migration); load leaves the live tree so the    */
/* host keeps the default layout. Unparseable JSON is the same fallback.      */
/* Restore drops serialized window ids that are neither live nor registered   */
/* (APX-318). Emptied leaves/splits collapse; leftover sibling ratios are     */
/* renormalized. Registered windows missing from the document use their       */
/* default dock target or float rect.                                         */
/*                                                                            */
/* Root object:                                                               */
/*   version: integer (SK_UI_DOCK_LAYOUT_VERSION)                             */
/*   id:      dockspace string id                                             */
/*   flags:   dockspace flags                                                 */
/*   root:    node object                                                     */
/*   floating: [ { id, x, y, w, h, z }, ... ]                                 */
/* Node object:                                                               */
/*   kind: "leaf" | "split"                                                   */
/*   id:   builder stable id, else path "root" / "root/0/1"                   */
/*   flags: node flags                                                        */
/*   leaf:  tabs: [window ids], active_index, optional active window id       */
/*   split: axis (0 horiz / 1 vert), ratio (first-child), a, b                */
/* -------------------------------------------------------------------------- */

#define UI_DOCK_LAYOUT_PATH_MAX 128u
#define UI_DOCK_LAYOUT_FLOAT_MAX SK_UI_DOCK_LAYOUT_FLOAT_MAX
#define UI_DOCK_LAYOUT_DEPTH_MAX 32u

static void ui_dock_layout_path_child(const char* parent, u32 index, char* out, u32 cap) {
	char suffix[16];
	size_t i = 0u;
	size_t j = 0u;
	if (parent == NULL || parent[0] == '\0') {
		parent = "root";
	}
	/* Do not snprintf from `stack[sp-1].path` into `stack[sp].path`: GCC
	 * -Wrestrict treats both as one object. Format the index separately. */
	(void)snprintf(suffix, sizeof(suffix), "/%u", index);
	while (i + 1u < (size_t)cap && parent[i] != '\0') {
		out[i] = parent[i];
		i += 1u;
	}
	while (i + 1u < (size_t)cap && suffix[j] != '\0') {
		out[i] = suffix[j];
		i += 1u;
		j += 1u;
	}
	if (cap > 0u) {
		out[i] = '\0';
	}
}

static i32 ui_dock_sv_eq(sk_str_view_t view, const_chr_t s) {
	sk_str_view_t other = sk_str_view_cstr(s);
	if (view.size != other.size) {
		return 0;
	}
	if (view.size == 0u) {
		return 1;
	}
	return memcmp(view.data, other.data, view.size) == 0 ? 1 : 0;
}

static char* ui_dock_sv_dup(const sk_allocator_t* a, sk_str_view_t view) {
	char* dst;
	if (a == NULL) {
		return NULL;
	}
	dst = (char*)a->alloc(a->instance, (size_t)view.size + 1u);
	if (dst == NULL) {
		return NULL;
	}
	if (view.size > 0u && view.data != NULL) {
		memcpy(dst, view.data, view.size);
	}
	dst[view.size] = '\0';
	return dst;
}

static f32 ui_dock_layout_len_pt(sk_ui_length_t len, f32 fallback) {
	if (len.unit == SK_UI_LENGTH_POINT) {
		return len.value;
	}
	return fallback;
}

static void ui_dock_layout_schema_free_str(const sk_allocator_t* a, const_chr_t s) {
	if (a != NULL && s != NULL) {
		a->free(a->instance, SK_CONST_CAST(void_ptr_t, s));
	}
}

static void ui_dock_layout_schema_node_release(const sk_allocator_t* a, sk_ui_dock_layout_node_t* node) {
	u32 i;
	if (node == NULL || a == NULL) {
		return;
	}
	ui_dock_layout_schema_free_str(a, node->id);
	for (i = 0u; i < node->tab_count; ++i) {
		ui_dock_layout_schema_free_str(a, node->tabs[i]);
	}
	a->free(a->instance, node);
}

static void ui_dock_layout_schema_node_free(const sk_allocator_t* a, sk_ui_dock_layout_node_t* node) {
	sk_ui_dock_layout_node_t* stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	if (node == NULL || a == NULL) {
		return;
	}
	stack[sp++] = node;
	while (sp > 0u) {
		sk_ui_dock_layout_node_t* cur = stack[--sp];
		if (cur->child_a != NULL && sp < UI_DOCK_WALK_MAX) {
			stack[sp++] = cur->child_a;
		}
		if (cur->child_b != NULL && sp < UI_DOCK_WALK_MAX) {
			stack[sp++] = cur->child_b;
		}
		cur->child_a = NULL;
		cur->child_b = NULL;
		ui_dock_layout_schema_node_release(a, cur);
	}
}

static void ui_dock_layout_schema_doc_free(const sk_allocator_t* a, sk_ui_dock_layout_t* doc) {
	u32 i;
	if (doc == NULL || a == NULL) {
		return;
	}
	ui_dock_layout_schema_free_str(a, doc->id);
	doc->id = NULL;
	ui_dock_layout_schema_node_free(a, doc->root);
	doc->root = NULL;
	for (i = 0u; i < doc->floating_count; ++i) {
		ui_dock_layout_schema_free_str(a, doc->floating[i].window_id);
		doc->floating[i].window_id = NULL;
	}
	doc->floating_count = 0u;
}

static sk_ui_dock_layout_node_t* ui_dock_layout_schema_alloc_node(const sk_allocator_t* a) {
	sk_ui_dock_layout_node_t* node;
	if (a == NULL) {
		return NULL;
	}
	node = (sk_ui_dock_layout_node_t*)a->alloc(a->instance, sizeof(*node));
	if (node == NULL) {
		return NULL;
	}
	memset(node, 0, sizeof(*node));
	return node;
}

static i32 ui_dock_layout_schema_fill_from_slot(const ui_dock_slot_t* slot, const char* persist_id, const sk_allocator_t* a, sk_ui_dock_layout_node_t* node) {
	u32 i;
	if (node == NULL || a == NULL) {
		return -1;
	}
	node->id = ui_dock_strdup(a, persist_id != NULL ? persist_id : "root");
	if (node->id == NULL) {
		return -1;
	}
	if (slot == NULL) {
		node->kind = SK_UI_DOCK_LAYOUT_KIND_LEAF;
		return 0;
	}
	node->flags = slot->flags;
	if (slot->kind == UI_DOCK_KIND_SPLIT_U8) {
		node->kind = SK_UI_DOCK_LAYOUT_KIND_SPLIT;
		node->axis = slot->axis;
		node->ratio = slot->ratio;
		return 0;
	}
	node->kind = SK_UI_DOCK_LAYOUT_KIND_LEAF;
	node->tab_count = slot->tab_count;
	if (node->tab_count > SK_UI_DOCK_LEAF_TABS_MAX) {
		node->tab_count = SK_UI_DOCK_LEAF_TABS_MAX;
	}
	for (i = 0u; i < node->tab_count; ++i) {
		if (slot->tabs[i] != NULL) {
			node->tabs[i] = ui_dock_strdup(a, slot->tabs[i]);
			if (node->tabs[i] == NULL) {
				return -1;
			}
		}
	}
	node->active_index = slot->active_index;
	if (node->tab_count == 0u || node->active_index >= node->tab_count) {
		node->active_index = 0u;
	}
	return 0;
}

static i32 ui_dock_layout_capture_tree(const sk_ui_context_t* ctx, sk_ui_dock_node_t live_root, const sk_allocator_t* a, sk_ui_dock_layout_node_t** out) {
	typedef struct ui_dock_layout_cap_frame_t {
		sk_ui_dock_node_t live;
		sk_ui_dock_layout_node_t* dst;
		char path[UI_DOCK_LAYOUT_PATH_MAX];
		u8 next_child;
		u8 _pad[3];
	} ui_dock_layout_cap_frame_t;
	ui_dock_layout_cap_frame_t stack[UI_DOCK_LAYOUT_DEPTH_MAX];
	u32 sp = 0u;
	sk_ui_dock_layout_node_t* root;

	if (out == NULL || a == NULL) {
		return -1;
	}
	*out = NULL;
	root = ui_dock_layout_schema_alloc_node(a);
	if (root == NULL) {
		return -1;
	}
	stack[0].live = live_root;
	stack[0].dst = root;
	(void)snprintf(stack[0].path, sizeof(stack[0].path), "root");
	stack[0].next_child = 0u;
	sp = 1u;

	while (sp > 0u) {
		ui_dock_layout_cap_frame_t* fr = &stack[sp - 1u];
		const ui_dock_slot_t* slot = ui_dock_slot(ctx, fr->live);
		const char* persist_id;
		u32 idx;

		if (fr->dst->id == NULL) {
			if (slot != NULL && slot->stable_id != NULL && slot->stable_id[0] != '\0') {
				persist_id = slot->stable_id;
				(void)snprintf(fr->path, sizeof(fr->path), "%s", persist_id);
			} else {
				persist_id = fr->path;
			}
			if (ui_dock_layout_schema_fill_from_slot(slot, persist_id, a, fr->dst) != 0) {
				ui_dock_layout_schema_node_free(a, root);
				return -1;
			}
		}
		if (fr->dst->kind != SK_UI_DOCK_LAYOUT_KIND_SPLIT || fr->next_child >= 2u) {
			sp -= 1u;
			continue;
		}
		idx = fr->next_child;
		fr->next_child = (u8)(idx + 1u);
		if (slot != NULL && sk_ui_dock_node_is_valid(slot->child[idx])) {
			sk_ui_dock_layout_node_t* child;
			if (sp >= UI_DOCK_LAYOUT_DEPTH_MAX) {
				ui_dock_layout_schema_node_free(a, root);
				return -1;
			}
			child = ui_dock_layout_schema_alloc_node(a);
			if (child == NULL) {
				ui_dock_layout_schema_node_free(a, root);
				return -1;
			}
			if (idx == 0u) {
				fr->dst->child_a = child;
			} else {
				fr->dst->child_b = child;
			}
			stack[sp].live = slot->child[idx];
			stack[sp].dst = child;
			ui_dock_layout_path_child(fr->path, idx, stack[sp].path, (u32)sizeof(stack[sp].path));
			stack[sp].next_child = 0u;
			sp += 1u;
		}
	}
	*out = root;
	return 0;
}

static i32 ui_dock_layout_capture_floating(const sk_ui_context_t* ctx, const sk_allocator_t* a, sk_ui_dock_layout_t* doc) {
	const sk_ui_api_t* ui = ui_get_api_table();
	u32 i;
	u32 n;
	if (ctx == NULL || doc == NULL) {
		return -1;
	}
	if (!sk_ui_node_is_valid(ctx->dock_overlay)) {
		return 0;
	}
	n = ui->node_child_count(ctx, ctx->dock_overlay);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t win = ui->node_child_at(ctx, ctx->dock_overlay, i);
		const_chr_t id;
		sk_ui_layout_style_t st;
		sk_ui_dock_layout_float_t* fl;
		if (ui_dock_is_widget(ctx, win, "editor_window") == 0) {
			continue;
		}
		id = ui->node_get_id(ctx, win);
		if (id == NULL || id[0] == '\0') {
			continue;
		}
		if (doc->floating_count >= SK_UI_DOCK_LAYOUT_FLOAT_MAX) {
			break;
		}
		fl = &doc->floating[doc->floating_count];
		memset(fl, 0, sizeof(*fl));
		fl->window_id = ui_dock_strdup(a, id);
		if (fl->window_id == NULL) {
			return -1;
		}
		memset(&st, 0, sizeof(st));
		(void)ui->node_get_layout_style(ctx, win, &st);
		fl->x = ui_dock_layout_len_pt(st.left, 0.0f);
		fl->y = ui_dock_layout_len_pt(st.top, 0.0f);
		fl->w = ui_dock_layout_len_pt(st.width, UI_DOCK_FLOAT_W);
		fl->h = ui_dock_layout_len_pt(st.height, UI_DOCK_FLOAT_H);
		fl->z = ui_dock_prop_i32(ctx, win, "z_index", UI_DOCK_FLOAT_Z);
		doc->floating_count += 1u;
	}
	return 0;
}

static i32 ui_dock_layout_capture(const sk_ui_context_t* ctx, const ui_dockspace_t* space, const_chr_t dockspace_id, const sk_allocator_t* a, sk_ui_dock_layout_t* out) {
	const_chr_t id;
	if (ctx == NULL || space == NULL || out == NULL || a == NULL) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	out->version = (i32)SK_UI_DOCK_LAYOUT_VERSION;
	out->flags = space->flags;
	id = (space->id != NULL && space->id[0] != '\0') ? space->id : dockspace_id;
	out->id = ui_dock_strdup(a, id != NULL ? id : "");
	if (out->id == NULL) {
		return -1;
	}
	if (ui_dock_layout_capture_tree(ctx, space->root, a, &out->root) != 0 || out->root == NULL) {
		ui_dock_layout_schema_doc_free(a, out);
		return -1;
	}
	if (ui_dock_layout_capture_floating(ctx, a, out) != 0) {
		ui_dock_layout_schema_doc_free(a, out);
		return -1;
	}
	return 0;
}

static void ui_dock_layout_emit_leaf(sk_archive_writer_t* w, const sk_ui_dock_layout_node_t* node) {
	u32 i;
	const char* persist_id = (node != NULL && node->id != NULL && node->id[0] != '\0') ? node->id : "root";
	w->write_string(w->instance, sk_str_view_cstr("kind"), sk_str_view_cstr("leaf"));
	w->write_string(w->instance, sk_str_view_cstr("id"), sk_str_view_cstr(persist_id));
	w->write_uint(w->instance, sk_str_view_cstr("flags"), node != NULL ? (u64)node->flags : 0u);
	w->begin_seq_named(w->instance, sk_str_view_cstr("tabs"));
	if (node != NULL) {
		for (i = 0u; i < node->tab_count; ++i) {
			if (node->tabs[i] != NULL) {
				w->add_string(w->instance, sk_str_view_cstr(node->tabs[i]));
			}
		}
	}
	w->end_seq(w->instance);
	w->write_uint(w->instance, sk_str_view_cstr("active_index"), node != NULL ? (u64)node->active_index : 0u);
	if (node != NULL && node->tab_count > 0u && node->active_index < node->tab_count && node->tabs[node->active_index] != NULL) {
		w->write_string(w->instance, sk_str_view_cstr("active"), sk_str_view_cstr(node->tabs[node->active_index]));
	}
}

static void ui_dock_layout_emit_node(sk_archive_writer_t* w, const sk_ui_dock_layout_node_t* node) {
	typedef struct ui_dock_layout_emit_frame_t {
		const sk_ui_dock_layout_node_t* node;
		u8 phase; /**< 0 = emit, 1 = close a, 2 = close b. */
		u8 _pad[3];
	} ui_dock_layout_emit_frame_t;
	ui_dock_layout_emit_frame_t stack[UI_DOCK_LAYOUT_DEPTH_MAX];
	u32 sp = 0u;
	const char* persist_id;

	stack[sp].node = node;
	stack[sp].phase = 0u;
	sp += 1u;

	while (sp > 0u) {
		ui_dock_layout_emit_frame_t* fr = &stack[sp - 1u];
		const sk_ui_dock_layout_node_t* cur = fr->node;

		if (fr->phase == 1u) {
			w->end_map(w->instance);
			if (cur != NULL && cur->child_b != NULL && sp < UI_DOCK_LAYOUT_DEPTH_MAX) {
				w->begin_map_named(w->instance, sk_str_view_cstr("b"));
				fr->phase = 2u;
				stack[sp].node = cur->child_b;
				stack[sp].phase = 0u;
				sp += 1u;
			} else {
				sp -= 1u;
			}
			continue;
		}
		if (fr->phase == 2u) {
			w->end_map(w->instance);
			sp -= 1u;
			continue;
		}

		if (cur == NULL || cur->kind != SK_UI_DOCK_LAYOUT_KIND_SPLIT) {
			ui_dock_layout_emit_leaf(w, cur);
			sp -= 1u;
			continue;
		}

		persist_id = (cur->id != NULL && cur->id[0] != '\0') ? cur->id : "root";
		w->write_string(w->instance, sk_str_view_cstr("kind"), sk_str_view_cstr("split"));
		w->write_int(w->instance, sk_str_view_cstr("axis"), (i64)cur->axis);
		w->write_float(w->instance, sk_str_view_cstr("ratio"), (f64)cur->ratio);
		w->write_string(w->instance, sk_str_view_cstr("id"), sk_str_view_cstr(persist_id));
		w->write_uint(w->instance, sk_str_view_cstr("flags"), (u64)cur->flags);

		if (cur->child_a != NULL && sp < UI_DOCK_LAYOUT_DEPTH_MAX) {
			w->begin_map_named(w->instance, sk_str_view_cstr("a"));
			fr->phase = 1u;
			stack[sp].node = cur->child_a;
			stack[sp].phase = 0u;
			sp += 1u;
			continue;
		}
		if (cur->child_b != NULL && sp < UI_DOCK_LAYOUT_DEPTH_MAX) {
			w->begin_map_named(w->instance, sk_str_view_cstr("b"));
			fr->phase = 2u;
			stack[sp].node = cur->child_b;
			stack[sp].phase = 0u;
			sp += 1u;
			continue;
		}
		sp -= 1u;
	}
}

static void ui_dock_layout_emit_doc(sk_archive_writer_t* w, const sk_ui_dock_layout_t* doc) {
	u32 i;
	w->write_int(w->instance, sk_str_view_cstr("version"), (i64)(doc != NULL ? doc->version : (i32)SK_UI_DOCK_LAYOUT_VERSION));
	w->write_string(w->instance, sk_str_view_cstr("id"), sk_str_view_cstr(doc != NULL && doc->id != NULL ? doc->id : ""));
	w->write_uint(w->instance, sk_str_view_cstr("flags"), doc != NULL ? (u64)doc->flags : 0u);
	w->begin_map_named(w->instance, sk_str_view_cstr("root"));
	ui_dock_layout_emit_node(w, doc != NULL ? doc->root : NULL);
	w->end_map(w->instance);
	w->begin_seq_named(w->instance, sk_str_view_cstr("floating"));
	if (doc != NULL) {
		for (i = 0u; i < doc->floating_count; ++i) {
			const sk_ui_dock_layout_float_t* fl = &doc->floating[i];
			w->begin_map(w->instance);
			w->write_string(w->instance, sk_str_view_cstr("id"), sk_str_view_cstr(fl->window_id != NULL ? fl->window_id : ""));
			w->write_float(w->instance, sk_str_view_cstr("x"), (f64)fl->x);
			w->write_float(w->instance, sk_str_view_cstr("y"), (f64)fl->y);
			w->write_float(w->instance, sk_str_view_cstr("w"), (f64)fl->w);
			w->write_float(w->instance, sk_str_view_cstr("h"), (f64)fl->h);
			w->write_int(w->instance, sk_str_view_cstr("z"), (i64)fl->z);
			w->end_map(w->instance);
		}
	}
	w->end_seq(w->instance);
}

static i32 ui_dock_layout_resolve_active(sk_ui_dock_layout_node_t* node, sk_str_view_t active) {
	u32 i;
	i32 found = -1;
	if (node == NULL || node->tab_count == 0u) {
		if (node != NULL) {
			node->active_index = 0u;
		}
		return 0;
	}
	if (active.size > 0u) {
		for (i = 0u; i < node->tab_count; ++i) {
			if (node->tabs[i] != NULL && ui_dock_sv_eq(active, node->tabs[i]) != 0) {
				found = (i32)i;
				break;
			}
		}
	}
	/* Prefer active_index when it names a live tab. Missing JSON integers
	 * read as 0, so an absent index plus a later `active` id falls back. */
	if (node->active_index >= node->tab_count) {
		node->active_index = (found >= 0) ? (u32)found : (node->tab_count - 1u);
	} else if (found >= 0 && node->active_index == 0u && (u32)found != 0u) {
		node->active_index = (u32)found;
	}
	return 0;
}

static i32 ui_dock_layout_fill_node(sk_archive_reader_t* r, const sk_allocator_t* a, sk_ui_dock_layout_node_t* node) {
	sk_str_view_t kind;
	sk_str_view_t id;
	sk_str_view_t active;

	kind = r->read_string(r->instance, sk_str_view_cstr("kind"));
	id = r->read_string(r->instance, sk_str_view_cstr("id"));
	node->id = ui_dock_sv_dup(a, id);
	if (node->id == NULL) {
		return -1;
	}
	node->flags = (u32)r->read_uint(r->instance, sk_str_view_cstr("flags"));

	if (ui_dock_sv_eq(kind, "split")) {
		node->kind = SK_UI_DOCK_LAYOUT_KIND_SPLIT;
		node->axis = (sk_ui_dock_split_t)r->read_int(r->instance, sk_str_view_cstr("axis"));
		node->ratio = (f32)r->read_float(r->instance, sk_str_view_cstr("ratio"));
		return 0;
	}
	if (!ui_dock_sv_eq(kind, "leaf")) {
		return -1;
	}
	node->kind = SK_UI_DOCK_LAYOUT_KIND_LEAF;
	if (r->begin_seq_named(r->instance, sk_str_view_cstr("tabs")) != 0) {
		while (r->next_seq_entry(r->instance) != 0) {
			sk_str_view_t tab = r->get_string(r->instance);
			if (node->tab_count >= SK_UI_DOCK_LEAF_TABS_MAX) {
				return -1;
			}
			node->tabs[node->tab_count] = ui_dock_sv_dup(a, tab);
			if (node->tabs[node->tab_count] == NULL) {
				return -1;
			}
			node->tab_count += 1u;
		}
		r->end_seq(r->instance);
	}
	node->active_index = (u32)r->read_uint(r->instance, sk_str_view_cstr("active_index"));
	active = r->read_string(r->instance, sk_str_view_cstr("active"));
	return ui_dock_layout_resolve_active(node, active);
}

static sk_ui_dock_layout_node_t* ui_dock_layout_alloc_filled(sk_archive_reader_t* r, const sk_allocator_t* a) {
	sk_ui_dock_layout_node_t* node = ui_dock_layout_schema_alloc_node(a);
	if (node == NULL) {
		return NULL;
	}
	if (ui_dock_layout_fill_node(r, a, node) != 0) {
		ui_dock_layout_schema_node_free(a, node);
		return NULL;
	}
	return node;
}

static i32 ui_dock_layout_read_node(sk_archive_reader_t* r, const sk_allocator_t* a, sk_ui_dock_layout_node_t** out, u32 depth) {
	typedef struct ui_dock_layout_read_frame_t {
		sk_ui_dock_layout_node_t* node;
		u8 step;   /**< 0 = try a, 1 = try b, 2 = done. */
		u8 opened; /**< Non-zero if this node's map was opened by its parent. */
		u8 _pad[2];
	} ui_dock_layout_read_frame_t;
	ui_dock_layout_read_frame_t stack[UI_DOCK_LAYOUT_DEPTH_MAX];
	u32 sp = 0u;
	sk_ui_dock_layout_node_t* root;

	(void)depth;
	if (out == NULL) {
		return -1;
	}
	*out = NULL;
	root = ui_dock_layout_alloc_filled(r, a);
	if (root == NULL) {
		return -1;
	}
	*out = root;
	if (root->kind != SK_UI_DOCK_LAYOUT_KIND_SPLIT) {
		return 0;
	}
	stack[sp].node = root;
	stack[sp].step = 0u;
	stack[sp].opened = 0u;
	sp += 1u;

	while (sp > 0u) {
		ui_dock_layout_read_frame_t* fr = &stack[sp - 1u];
		if (fr->step == 0u) {
			fr->step = 1u;
			if (r->begin_map_named(r->instance, sk_str_view_cstr("a")) != 0) {
				sk_ui_dock_layout_node_t* child = ui_dock_layout_alloc_filled(r, a);
				if (child == NULL) {
					return -1;
				}
				fr->node->child_a = child;
				if (child->kind == SK_UI_DOCK_LAYOUT_KIND_SPLIT) {
					if (sp >= UI_DOCK_LAYOUT_DEPTH_MAX) {
						return -1;
					}
					stack[sp].node = child;
					stack[sp].step = 0u;
					stack[sp].opened = 1u;
					sp += 1u;
				} else {
					r->end_map(r->instance);
				}
			}
			continue;
		}
		if (fr->step == 1u) {
			fr->step = 2u;
			if (r->begin_map_named(r->instance, sk_str_view_cstr("b")) != 0) {
				sk_ui_dock_layout_node_t* child = ui_dock_layout_alloc_filled(r, a);
				if (child == NULL) {
					return -1;
				}
				fr->node->child_b = child;
				if (child->kind == SK_UI_DOCK_LAYOUT_KIND_SPLIT) {
					if (sp >= UI_DOCK_LAYOUT_DEPTH_MAX) {
						return -1;
					}
					stack[sp].node = child;
					stack[sp].step = 0u;
					stack[sp].opened = 1u;
					sp += 1u;
				} else {
					r->end_map(r->instance);
				}
			}
			continue;
		}
		if (fr->opened != 0u) {
			r->end_map(r->instance);
		}
		sp -= 1u;
	}
	return 0;
}

static i32 ui_dock_layout_parse_json(const sk_allocator_t* a, const_chr_t json, u32 len, sk_ui_dock_layout_t* out) {
	sk_archive_reader_t reader;
	sk_str_view_t view;
	i32 rc;

	if (out == NULL || json == NULL) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	view = sk_str_view_make(json, len);
	if (sk_json_archive_reader_init(&reader, view, a) != 0) {
		return -1;
	}

	out->version = (i32)reader.read_int(reader.instance, sk_str_view_cstr("version"));
	out->flags = (u32)reader.read_uint(reader.instance, sk_str_view_cstr("flags"));
	out->id = ui_dock_sv_dup(a, reader.read_string(reader.instance, sk_str_view_cstr("id")));
	if (out->id == NULL) {
		sk_archive_reader_destroy(&reader);
		return -1;
	}

	if (reader.begin_map_named(reader.instance, sk_str_view_cstr("root")) == 0) {
		sk_archive_reader_destroy(&reader);
		ui_dock_layout_schema_doc_free(a, out);
		return -1;
	}
	if (ui_dock_layout_read_node(&reader, a, &out->root, 0u) != 0 || out->root == NULL) {
		sk_archive_reader_destroy(&reader);
		ui_dock_layout_schema_doc_free(a, out);
		return -1;
	}
	reader.end_map(reader.instance);

	if (reader.begin_seq_named(reader.instance, sk_str_view_cstr("floating")) != 0) {
		while (reader.next_seq_entry(reader.instance) != 0) {
			sk_ui_dock_layout_float_t* fl;
			if (out->floating_count >= UI_DOCK_LAYOUT_FLOAT_MAX) {
				sk_archive_reader_destroy(&reader);
				ui_dock_layout_schema_doc_free(a, out);
				return -1;
			}
			fl = &out->floating[out->floating_count];
			memset(fl, 0, sizeof(*fl));
			reader.begin_map(reader.instance);
			fl->window_id = ui_dock_sv_dup(a, reader.read_string(reader.instance, sk_str_view_cstr("id")));
			fl->x = (f32)reader.read_float(reader.instance, sk_str_view_cstr("x"));
			fl->y = (f32)reader.read_float(reader.instance, sk_str_view_cstr("y"));
			fl->w = (f32)reader.read_float(reader.instance, sk_str_view_cstr("w"));
			fl->h = (f32)reader.read_float(reader.instance, sk_str_view_cstr("h"));
			fl->z = (i32)reader.read_int(reader.instance, sk_str_view_cstr("z"));
			reader.end_map(reader.instance);
			if (fl->window_id == NULL) {
				sk_archive_reader_destroy(&reader);
				ui_dock_layout_schema_doc_free(a, out);
				return -1;
			}
			out->floating_count += 1u;
		}
		reader.end_seq(reader.instance);
	}

	rc = (out->root != NULL) ? 0 : -1;
	sk_archive_reader_destroy(&reader);
	if (rc != 0) {
		ui_dock_layout_schema_doc_free(a, out);
	}
	return rc;
}

i32 ui_dock_layout_save_json_impl(const sk_ui_context_t* ctx, const_chr_t dockspace_id, char* out, u32 cap, u32* out_len) {
	const ui_dockspace_t* space;
	sk_archive_writer_t writer;
	sk_ui_dock_layout_t doc;
	sk_str_view_t text;
	i32 rc;

	if (out_len != NULL) {
		*out_len = 0u;
	}
	if (out != NULL && cap > 0u) {
		out[0] = '\0';
	}
	if (ctx == NULL || dockspace_id == NULL || dockspace_id[0] == '\0' || out == NULL || cap == 0u) {
		return -1;
	}
	space = ui_dock_space_by_id_const(ctx, dockspace_id);
	if (space == NULL || !sk_ui_dock_node_is_valid(space->root)) {
		return -1;
	}
	if (ui_dock_layout_capture(ctx, space, dockspace_id, ctx->allocator, &doc) != 0) {
		return -1;
	}
	doc.version = (i32)SK_UI_DOCK_LAYOUT_VERSION;
	if (sk_json_archive_writer_init(&writer, ctx->allocator) != 0) {
		ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
		return -1;
	}

	ui_dock_layout_emit_doc(&writer, &doc);

	text = sk_json_archive_writer_emit_as_string(&writer);
	if (text.data == NULL || text.size + 1u > cap) {
		sk_archive_writer_destroy(&writer);
		ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
		return -1;
	}
	memcpy(out, text.data, text.size);
	out[text.size] = '\0';
	if (out_len != NULL) {
		*out_len = text.size;
	}
	rc = 0;
	sk_archive_writer_destroy(&writer);
	ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
	return rc;
}

static i32 ui_dock_layout_fill_live(sk_ui_context_t* ctx, ui_dockspace_t* space, sk_ui_dock_node_t live, const sk_ui_dock_layout_node_t* src) {
	const sk_ui_api_t* ui = ui_get_api_table();
	ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, live);
	u32 i;
	if (slot == NULL || src == NULL) {
		return -1;
	}
	slot->flags = src->flags;
	if (src->id != NULL && src->id[0] != '\0') {
		if (ui_dock_builder_set_node_id_impl(ctx, live, src->id) != 0) {
			return -1;
		}
	}
	if (src->kind == SK_UI_DOCK_LAYOUT_KIND_SPLIT) {
		slot->axis = src->axis;
		slot->ratio = src->ratio;
		return 0;
	}
	for (i = 0u; i < src->tab_count; ++i) {
		sk_ui_node_t win;
		if (src->tabs[i] == NULL || src->tabs[i][0] == '\0') {
			return -1;
		}
		if (ui_dock_window_known(ctx, src->tabs[i]) == 0) {
			sk_log_warn(ui_logger_api(), ui_dock_logger(), "dock layout drop unregistered window '%s'", src->tabs[i]);
			continue;
		}
		if (ui_dock_append_tab(ctx, live, src->tabs[i]) != 0) {
			return -1;
		}
		win = ui->find_by_id(ctx, src->tabs[i]);
		if (!sk_ui_node_is_valid(win) && ui_dock_queue_pending(ctx, space, src->tabs[i], live, SK_UI_DOCK_DIR_CENTER) != 0) {
			return -1;
		}
		if (src->tab_count > 0u && src->active_index == i) {
			slot->active_index = slot->tab_count - 1u;
		}
	}
	if (slot->tab_count == 0u) {
		slot->active_index = 0u;
	} else if (slot->active_index >= slot->tab_count) {
		slot->active_index = slot->tab_count - 1u;
	}
	return 0;
}

static sk_ui_dock_node_t ui_dock_layout_build_tree(sk_ui_context_t* ctx, ui_dockspace_t* space, const sk_ui_dock_layout_node_t* root_src) {
	typedef struct ui_dock_layout_build_frame_t {
		const sk_ui_dock_layout_node_t* src;
		sk_ui_dock_node_t live;
		u8 step; /**< 0 = child a, 1 = child b, 2 = done. */
		u8 _pad[3];
	} ui_dock_layout_build_frame_t;
	ui_dock_layout_build_frame_t stack[UI_DOCK_LAYOUT_DEPTH_MAX];
	u32 sp = 0u;
	sk_ui_dock_node_t root;

	if (root_src == NULL) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	root = ui_dock_alloc(ctx, (u8)root_src->kind);
	if (!sk_ui_dock_node_is_valid(root)) {
		return SK_UI_DOCK_NODE_INVALID;
	}
	if (ui_dock_layout_fill_live(ctx, space, root, root_src) != 0) {
		ui_dock_free_tree(ctx, root);
		return SK_UI_DOCK_NODE_INVALID;
	}
	if (root_src->kind != SK_UI_DOCK_LAYOUT_KIND_SPLIT) {
		return root;
	}
	stack[sp].src = root_src;
	stack[sp].live = root;
	stack[sp].step = 0u;
	sp += 1u;
	while (sp > 0u) {
		ui_dock_layout_build_frame_t* fr = &stack[sp - 1u];
		ui_dock_slot_t* parent;
		const sk_ui_dock_layout_node_t* child_src;
		sk_ui_dock_node_t child;
		u32 child_index;
		if (fr->step >= 2u) {
			sp -= 1u;
			continue;
		}
		child_index = fr->step;
		fr->step += 1u;
		child_src = (child_index == 0u) ? fr->src->child_a : fr->src->child_b;
		if (child_src == NULL) {
			ui_dock_free_tree(ctx, root);
			return SK_UI_DOCK_NODE_INVALID;
		}
		child = ui_dock_alloc(ctx, (u8)child_src->kind);
		if (!sk_ui_dock_node_is_valid(child)) {
			ui_dock_free_tree(ctx, root);
			return SK_UI_DOCK_NODE_INVALID;
		}
		parent = ui_dock_slot_mut(ctx, fr->live);
		if (parent == NULL) {
			ui_dock_recycle(ctx, child);
			ui_dock_free_tree(ctx, root);
			return SK_UI_DOCK_NODE_INVALID;
		}
		parent->child[child_index] = child;
		{
			ui_dock_slot_t* cslot = ui_dock_slot_mut(ctx, child);
			if (cslot == NULL) {
				ui_dock_recycle(ctx, child);
				ui_dock_free_tree(ctx, root);
				return SK_UI_DOCK_NODE_INVALID;
			}
			cslot->parent = fr->live;
		}
		if (ui_dock_layout_fill_live(ctx, space, child, child_src) != 0) {
			ui_dock_free_tree(ctx, root);
			return SK_UI_DOCK_NODE_INVALID;
		}
		if (child_src->kind == SK_UI_DOCK_LAYOUT_KIND_SPLIT) {
			if (sp >= UI_DOCK_LAYOUT_DEPTH_MAX) {
				ui_dock_free_tree(ctx, root);
				return SK_UI_DOCK_NODE_INVALID;
			}
			stack[sp].src = child_src;
			stack[sp].live = child;
			stack[sp].step = 0u;
			sp += 1u;
		}
	}
	return root;
}

static i32 ui_dock_layout_place_floats(sk_ui_context_t* ctx, const sk_ui_dock_layout_t* doc) {
	const sk_ui_api_t* ui = ui_get_api_table();
	u32 i;
	ui_dock_ensure_root_layers(ctx);
	if (doc == NULL || !sk_ui_node_is_valid(ctx->dock_overlay)) {
		return 0;
	}
	for (i = 0u; i < doc->floating_count; ++i) {
		const sk_ui_dock_layout_float_t* fl = &doc->floating[i];
		sk_ui_node_t win;
		if (fl->window_id == NULL || fl->window_id[0] == '\0') {
			return -1;
		}
		if (ui_dock_window_known(ctx, fl->window_id) == 0) {
			sk_log_warn(ui_logger_api(), ui_dock_logger(), "dock layout drop unregistered floating window '%s'", fl->window_id);
			continue;
		}
		(void)ui_dock_remove_window(ctx, fl->window_id);
		win = ui->find_by_id(ctx, fl->window_id);
		if (!sk_ui_node_is_valid(win)) {
			continue;
		}
		(void)ui->node_reparent(ctx, win, ctx->dock_overlay, UI_DOCK_APPEND);
		ui_dock_style_float(ctx, win, fl->x, fl->y, fl->w, fl->h, fl->z);
	}
	return 0;
}

i32 ui_dock_layout_load_json_impl(sk_ui_context_t* ctx, const_chr_t dockspace_id, const_chr_t json, u32 len) {
	sk_ui_dock_layout_t doc;
	ui_dockspace_t* space;
	sk_ui_dock_node_t old_root;
	sk_ui_dock_node_t new_root;
	i32 was_current;

	if (ctx->dock_builder_open != 0) {
		return -1;
	}
	if (json == NULL || dockspace_id == NULL || dockspace_id[0] == '\0') {
		return -1;
	}
	if (ui_dock_layout_parse_json(ctx->allocator, json, len, &doc) != 0) {
		return -1;
	}
	if (sk_ui_dock_layout_version_supported(doc.version) != 0) {
		sk_log_warn(ui_logger_api(), ui_dock_logger(), "dock layout version %d unsupported (want %d); reject and keep default layout", doc.version, (i32)SK_UI_DOCK_LAYOUT_VERSION);
		ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
		return -1;
	}
	if (doc.root == NULL) {
		ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
		return -1;
	}

	ctx->dock_restoring = 1u;
	space = ui_dock_space_by_id(ctx, dockspace_id);
	if (space == NULL) {
		if (!sk_ui_dock_node_is_valid(ui_dockspace_begin_impl(ctx, SK_UI_NODE_INVALID, dockspace_id, doc.flags))) {
			ctx->dock_restoring = 0u;
			ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
			return -1;
		}
		space = ui_dock_space_by_id(ctx, dockspace_id);
		if (space == NULL) {
			ctx->dock_restoring = 0u;
			ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
			return -1;
		}
	}
	space->flags = doc.flags;
	ui_dock_ensure_root_layers(ctx);
	if (sk_ui_node_is_valid(space->host)) {
		ui_dock_salvage_windows(ctx, space->host, ctx->dock_stash);
	}
	old_root = space->root;
	was_current = (sk_ui_dock_node_eq(ctx->dock_current, old_root) || sk_ui_dock_node_eq(ui_dock_root_of(ctx, ctx->dock_current), old_root)) ? 1 : 0;
	/* Drop the previous model first so tab-id map entries do not collide. */
	space->root = SK_UI_DOCK_NODE_INVALID;
	if (was_current != 0) {
		ctx->dock_current = SK_UI_DOCK_NODE_INVALID;
	}
	if (sk_ui_dock_node_is_valid(old_root)) {
		ui_dock_free_tree(ctx, old_root);
	}
	ui_dock_space_clear_pending(ctx, space);

	new_root = ui_dock_layout_build_tree(ctx, space, doc.root);
	if (!sk_ui_dock_node_is_valid(new_root)) {
		ctx->dock_restoring = 0u;
		ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
		return -1;
	}
	space->root = new_root;
	if (was_current != 0) {
		ctx->dock_current = new_root;
	}

	if (ui_dock_layout_place_floats(ctx, &doc) != 0) {
		ctx->dock_restoring = 0u;
		ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
		return -1;
	}
	ctx->dock_restoring = 0u;
	ui_dock_layout_reconcile_mismatches(ctx, space);
	ui_dock_mark_dirty(space);
	ui_dock_layout_schema_doc_free(ctx->allocator, &doc);
	return ui_dockspace_apply_impl(ctx, space->root);
}

void ui_dock_layout_begin(sk_ui_context_t* ctx) {
	u32 i;
	if (ctx->dockspace_count == 0u || ctx->dock_builder_open != 0 || ctx->dock_applying != 0) {
		return;
	}
	for (i = 0u; i < ctx->dockspace_count; ++i) {
		ui_dockspace_t* space = &ctx->dockspaces[i];
		if (space->dirty != 0u && (space->flags & SK_UI_DOCKSPACE_AUTO_APPLY) != 0u) {
			(void)ui_dockspace_apply_impl(ctx, space->root);
		}
	}
}

void ui_dock_layout_end(sk_ui_context_t* ctx) {
	u32 i;
	for (i = 0u; i < ctx->dockspace_count; ++i) {
		ui_dockspace_t* space = &ctx->dockspaces[i];
		sk_ui_rect_t hr;
		sk_ui_dock_node_t stack[UI_DOCK_WALK_MAX];
		u32 sp = 0u;
		if (ui_dock_space_abs_rect(ctx, space, &hr) == 0) {
			space->last_rect = hr;
			space->laid_out = 1u;
			ui_dock_layout_tree(ctx, space->root, &hr);
		}
		if (!sk_ui_dock_node_is_valid(space->root)) {
			continue;
		}
		stack[sp++] = space->root;
		while (sp > 0u) {
			sk_ui_dock_node_t cur = stack[--sp];
			ui_dock_slot_t* slot = ui_dock_slot_mut(ctx, cur);
			sk_ui_rect_t box;
			if (slot == NULL) {
				continue;
			}
			if (sk_ui_node_is_valid(slot->host) && ui_node_get_abs_rect_impl(ctx, slot->host, &box, NULL) == 0) {
				slot->rect = box;
			}
			if (slot->kind == UI_DOCK_KIND_SPLIT_U8) {
				if (sk_ui_node_is_valid(slot->splitter) && ui_node_get_abs_rect_impl(ctx, slot->splitter, &box, NULL) == 0) {
					slot->splitter_rect = box;
				}
				if (sk_ui_dock_node_is_valid(slot->child[1]) && sp < UI_DOCK_WALK_MAX) {
					stack[sp++] = slot->child[1];
				}
				if (sk_ui_dock_node_is_valid(slot->child[0]) && sp < UI_DOCK_WALK_MAX) {
					stack[sp++] = slot->child[0];
				}
			}
		}
		if (ctx->dock_drag_active != 0u) {
			ui_dock_refresh_drop(ctx, space);
		}
	}
}

void ui_dock_on_float_pointer(sk_ui_context_t* ctx, sk_ui_node_t window, sk_ui_event_t* event) {
	const sk_ui_api_t* ui = ui_get_api_table();
	const ui_node_slot_t* slot;
	const_chr_t id;
	if (ctx->dockspace_count == 0u || event == NULL || !sk_ui_node_is_valid(window)) {
		return;
	}
	slot = ui_slot(ctx, window);
	if (slot == NULL || slot->layout_style.position != SK_UI_POSITION_ABSOLUTE) {
		return;
	}
	if (ui_dock_is_widget(ctx, window, "editor_window") == 0) {
		return;
	}
	id = slot->id;
	if (id == NULL || id[0] == '\0') {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN) {
		ctx->dock_drag_active = 1u;
		ctx->dock_drag_torn = 1u;
		ctx->dock_drag_window = window;
		ctx->dock_drag_tab = SK_UI_NODE_INVALID;
		ctx->dock_drag_start_x = event->x;
		ctx->dock_drag_start_y = event->y;
		ui_dock_copy_id(ctx->dock_drag_window_id, (u32)sizeof(ctx->dock_drag_window_id), id);
		ui_dock_raise_float(ctx, window);
		ui_dock_update_drag_hover(ctx, event->x, event->y);
		return;
	}
	if (ctx->dock_drag_active == 0u || !sk_ui_node_eq(ctx->dock_drag_window, window)) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_MOVE) {
		ui_dock_update_drag_hover(ctx, event->x, event->y);
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_UP) {
		f32 mdx = event->x - ctx->dock_drag_start_x;
		f32 mdy = event->y - ctx->dock_drag_start_y;
		ui_dock_update_drag_hover(ctx, event->x, event->y);
		if ((mdx * mdx + mdy * mdy) >= (UI_DOCK_TEAR_PT * UI_DOCK_TEAR_PT)) {
			(void)ui_dock_commit_drop(ctx);
		}
		ui_dock_clear_drag(ctx);
		(void)ui;
	}
}

void ui_dock_drag_tick(sk_ui_context_t* ctx, f32 x, f32 y, i32 button_up) {
	if (ctx->dock_drag_active == 0u || ctx->dock_drag_torn == 0u) {
		return;
	}
	if (sk_ui_node_is_valid(ctx->dock_drag_window)) {
		ui_dock_move_float_to_pointer(ctx, ctx->dock_drag_window, x, y);
	}
	ui_dock_update_drag_hover(ctx, x, y);
	if (button_up != 0) {
		f32 mdx = x - ctx->dock_drag_start_x;
		f32 mdy = y - ctx->dock_drag_start_y;
		if ((mdx * mdx + mdy * mdy) >= (UI_DOCK_TEAR_PT * UI_DOCK_TEAR_PT)) {
			(void)ui_dock_commit_drop(ctx);
		}
		ui_dock_clear_drag(ctx);
	}
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

/* Unity (via test.h) may include <stdnoreturn.h>, which defines `noreturn`
 * as `_Noreturn`. The Windows UCRT <stdlib.h> then uses `__declspec(noreturn)`,
 * which breaks under clang-tidy when the macro expands (same as core/ui.c). */
#ifdef noreturn
#undef noreturn
#endif

#include <stdio.h>
#include <stdlib.h>

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

static void ui_dock_test_ptr(sk_ui_input_event_t* ev, sk_ui_input_kind_t kind, f32 x, f32 y, i32 down) {
	memset(ev, 0, sizeof(*ev));
	ev->kind = kind;
	ev->x = x;
	ev->y = y;
	ev->button = SK_UI_POINTER_BUTTON_LEFT;
	ev->down = down;
}

static void ui_dock_test_layout(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 w, f32 h) {
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, w, h));
}

SK_TEST(ui_dock_apply_projects_tabs_and_click_activate) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_node_t scene;
	sk_ui_node_t game;
	sk_ui_node_t tab_scene;
	sk_ui_node_t tab_game;
	sk_ui_rect_t tr;
	sk_ui_input_event_t ev;
	sk_ui_node_t stash;
	TEST_ASSERT_NOT_NULL(ctx);

	scene = ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	game = ui->widget_editor_window(ctx, ui->context_root(ctx), "Game", "game");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "live", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "game", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	ui_dock_test_layout(ui, ctx, 800.0f, 500.0f);

	tab_scene = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "scene-dock-tab");
	tab_game = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "game-dock-tab");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tab_scene));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tab_game));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, tab_scene));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, tab_game));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, scene));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, game));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tab_scene, &tr, NULL));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, tr.x + tr.width * 0.5f, tr.y + tr.height * 0.5f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, tr.x + tr.width * 0.5f, tr.y + tr.height * 0.5f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, tr.x + tr.width * 0.5f, tr.y + tr.height * 0.5f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_layout(ui, ctx, 800.0f, 500.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, tab_scene));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, tab_game));

	stash = ui->find_by_id(ctx, "ui-dock-stash");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(stash));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(ctx, game), stash));
	TEST_ASSERT_FALSE(sk_ui_node_eq(ui->node_parent(ctx, scene), stash));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_splitter_drag_resizes_siblings) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t split;
	sk_ui_node_t splitter;
	sk_ui_rect_t sr;
	sk_ui_input_event_t ev;
	f32 before;
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "H", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "S", "scene");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "split-live", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &left, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	ui_dock_test_layout(ui, ctx, 800.0f, 500.0f);

	split = ui->dockspace_find(ctx, "split-live");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, split));
	splitter = ui->dock_node_host(ctx, split);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(splitter));
	{
		const ui_dock_slot_t* slot = ui_dock_slot(ctx, split);
		TEST_ASSERT_NOT_NULL(slot);
		splitter = slot->splitter;
	}
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(splitter));
	before = ui->dock_split_get_ratio(ctx, split);
	TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.25f, before);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, splitter, &sr, NULL));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, sr.x + sr.width * 0.5f, sr.y + sr.height * 0.5f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, sr.x + sr.width * 0.5f, sr.y + sr.height * 0.5f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, sr.x + 120.0f, sr.y + sr.height * 0.5f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, sr.x + 120.0f, sr.y + sr.height * 0.5f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_layout(ui, ctx, 800.0f, 500.0f);
	TEST_ASSERT_TRUE(ui->dock_split_get_ratio(ctx, split) > before + 0.02f);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_tab_tear_off_drop_and_float_hit) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_node_t hier;
	sk_ui_node_t scene;
	sk_ui_node_t tab;
	sk_ui_node_t drop;
	sk_ui_node_t overlay;
	sk_ui_rect_t tr;
	sk_ui_rect_t lr;
	sk_ui_input_event_t ev;
	sk_ui_node_t hit;
	TEST_ASSERT_NOT_NULL(ctx);

	hier = ui->widget_editor_window(ctx, ui->context_root(ctx), "H", "hierarchy");
	scene = ui->widget_editor_window(ctx, ui->context_root(ctx), "S", "scene");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "dnd", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.35f, &left, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	ui_dock_test_layout(ui, ctx, 800.0f, 500.0f);

	tab = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "hierarchy-dock-tab");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tab));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tab, &tr, NULL));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, tr.x + 8.0f, tr.y + 8.0f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, tr.x + 8.0f, tr.y + 8.0f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, tr.x + 8.0f, tr.y + 80.0f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_layout(ui, ctx, 800.0f, 500.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "hierarchy"));
	overlay = ui->find_by_id(ctx, "ui-dock-overlay");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(overlay));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(ctx, hier), overlay));
	drop = ui->find_by_id(ctx, "dnd-drop");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(drop));
	TEST_ASSERT_EQUAL_INT(0, ui_dock_prop_i32(ctx, drop, "hidden", 1));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, rest, &lr));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, lr.x + lr.width * 0.5f, lr.y + lr.height * 0.5f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_layout(ui, ctx, 800.0f, 500.0f);
	TEST_ASSERT_EQUAL_INT(0, ui_dock_prop_i32(ctx, ui->find_by_id(ctx, "dnd-drop-preview"), "hidden", 1));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, lr.x + lr.width * 0.5f, lr.y + lr.height * 0.5f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_layout(ui, ctx, 800.0f, 500.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "hierarchy"));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "hierarchy"), rest));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "scene"));
	ui_dock_test_layout(ui, ctx, 800.0f, 500.0f);
	{
		sk_ui_rect_t fr;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, scene, &fr, NULL));
		hit = ui->hit_test(ctx, fr.x + fr.width * 0.5f, fr.y + 8.0f);
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(hit));
		/* Float title / window is above the docked tree. */
		{
			sk_ui_node_t cur = hit;
			i32 found = 0;
			while (sk_ui_node_is_valid(cur)) {
				if (sk_ui_node_eq(cur, scene)) {
					found = 1;
					break;
				}
				cur = ui->node_parent(ctx, cur);
			}
			TEST_ASSERT_EQUAL_INT(1, found);
		}
	}
	TEST_ASSERT_TRUE(ui->node_alive(ctx, hier));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, scene));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_tab_drag_reorder_and_title_redock) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_node_t tab_a;
	sk_ui_node_t tab_b;
	sk_ui_node_t title;
	sk_ui_rect_t a;
	sk_ui_rect_t b;
	sk_ui_input_event_t ev;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 0u;
	sk_ui_node_t win_b;
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "A", "win-a");
	win_b = ui->widget_editor_window(ctx, ui->context_root(ctx), "B", "win-b");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "reorder", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "win-a", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "win-b", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	ui_dock_test_layout(ui, ctx, 640.0f, 400.0f);

	tab_a = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "win-a-dock-tab");
	tab_b = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "win-b-dock-tab");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tab_a, &a, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tab_b, &b, NULL));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, a.x + 4.0f, a.y + 8.0f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, a.x + 4.0f, a.y + 8.0f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, b.x + b.width * 0.5f, b.y + 8.0f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, b.x + b.width * 0.5f, b.y + 8.0f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("win-b", ids[0]);
	TEST_ASSERT_EQUAL_STRING("win-a", ids[1]);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "win-b"));
	ui_dock_test_layout(ui, ctx, 640.0f, 400.0f);
	title = ui->editor_window_title_bar(ctx, win_b);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(title));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, title, &a, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, root, &b));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, a.x + 10.0f, a.y + 6.0f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, a.x + 10.0f, a.y + 6.0f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, b.x + b.width * 0.5f, b.y + b.height * 0.5f, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, b.x + b.width * 0.5f, b.y + b.height * 0.5f, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ui_dock_test_layout(ui, ctx, 640.0f, 400.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "win-b"));

	ui->context_destroy(ctx);
}

static i32 ui_dock_layout_golden_rel(char* rel, u32 cap, const char* name) {
	int n = snprintf(rel, cap, "plugins/ui/testdata/dock/%s.json", name);
	return (n < 0 || (u32)n >= cap) ? -1 : 0;
}

static i32 ui_dock_layout_golden_path(char* out, u32 cap, const char* name) {
	char rel[256];
	if (ui_dock_layout_golden_rel(rel, (u32)sizeof(rel), name) != 0) {
		return -1;
	}
	return sk_test_locate(out, cap, rel, __FILE__);
}

static i32 ui_dock_layout_env_regen(void) {
	const char* e = getenv("SK_UI_DOCK_REGEN_GOLDENS");
	return (e != NULL && e[0] == '1') ? 1 : 0;
}

static void ui_dock_layout_assert_golden(const char* name, const char* json, u32 len) {
	char path[512];
	char* golden = NULL;
	long file_size = 0;
	u32 glen = 0u;
	u32 i;
	i32 regen = ui_dock_layout_env_regen();
	i32 found = ui_dock_layout_golden_path(path, (u32)sizeof(path), name);
	FILE* f;
	char rel[256];

	if (regen != 0 || found != 0) {
		TEST_ASSERT_EQUAL_INT(0, ui_dock_layout_golden_rel(rel, (u32)sizeof(rel), name));
		TEST_ASSERT_EQUAL_INT(0, sk_test_source_path(path, (u32)sizeof(path), rel, __FILE__));
		f = fopen(path, "wb");
		TEST_ASSERT_NOT_NULL_MESSAGE(f, "could not write dock layout golden");
		TEST_ASSERT_EQUAL_UINT((unsigned)len, (unsigned)fwrite(json, 1u, len, f));
		fclose(f);
		if (regen != 0) {
			return;
		}
	}

	f = fopen(path, "rb");
	TEST_ASSERT_NOT_NULL_MESSAGE(f, "dock layout golden missing");
	TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_END));
	file_size = ftell(f);
	TEST_ASSERT_TRUE(file_size >= 0);
	TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_SET));
	glen = (u32)file_size;
	golden = (char*)malloc((size_t)glen + 1u);
	TEST_ASSERT_NOT_NULL(golden);
	TEST_ASSERT_EQUAL_UINT((unsigned)glen, (unsigned)fread(golden, 1u, glen, f));
	fclose(f);
	golden[glen] = '\0';
	/* Normalize CRLF fixtures to the writer's LF output. */
	{
		u32 w = 0u;
		for (i = 0u; i < glen; ++i) {
			if (golden[i] == '\r') {
				continue;
			}
			golden[w++] = golden[i];
		}
		glen = w;
		golden[glen] = '\0';
	}
	TEST_ASSERT_EQUAL_UINT(len, glen);
	TEST_ASSERT_EQUAL_MEMORY(json, golden, len);
	free(golden);
}

static void ui_dock_test_set_float_rect(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t win, f32 x, f32 y, f32 w, f32 h, i32 z) {
	sk_ui_layout_style_t st;
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, win, &st));
	st.position = SK_UI_POSITION_ABSOLUTE;
	st.left = sk_ui_pt(x);
	st.top = sk_ui_pt(y);
	st.width = sk_ui_pt(w);
	st.height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, win, &st));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, win, "z_index", z));
}

static void ui_dock_assert_live_matches_doc(const sk_ui_api_t* ui, const sk_ui_context_t* ctx, sk_ui_dock_node_t live, const sk_ui_dock_layout_node_t* doc) {
	typedef struct ui_dock_layout_match_frame_t {
		sk_ui_dock_node_t live;
		const sk_ui_dock_layout_node_t* doc;
	} ui_dock_layout_match_frame_t;
	ui_dock_layout_match_frame_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	stack[sp].live = live;
	stack[sp].doc = doc;
	sp += 1u;
	while (sp > 0u) {
		ui_dock_layout_match_frame_t fr = stack[--sp];
		const ui_dock_slot_t* slot = ui_dock_slot(ctx, fr.live);
		TEST_ASSERT_NOT_NULL(fr.doc);
		TEST_ASSERT_NOT_NULL(slot);
		TEST_ASSERT_EQUAL_UINT(slot->flags, fr.doc->flags);
		if (ui->dock_node_is_split(ctx, fr.live)) {
			TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_LAYOUT_KIND_SPLIT, (unsigned)fr.doc->kind);
			TEST_ASSERT_EQUAL_INT((int)ui->dock_split_get_axis(ctx, fr.live), (int)fr.doc->axis);
			TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui->dock_split_get_ratio(ctx, fr.live), fr.doc->ratio);
			if (sp + 2u > UI_DOCK_WALK_MAX) {
				TEST_FAIL_MESSAGE("dock layout match stack overflow");
			}
			stack[sp].live = ui->dock_split_child(ctx, fr.live, 1u);
			stack[sp].doc = fr.doc->child_b;
			sp += 1u;
			stack[sp].live = ui->dock_split_child(ctx, fr.live, 0u);
			stack[sp].doc = fr.doc->child_a;
			sp += 1u;
			continue;
		}
		{
			const_chr_t ids[SK_UI_DOCK_LEAF_TABS_MAX];
			u32 count = 0u;
			u32 active = 0u;
			u32 i;
			TEST_ASSERT_EQUAL_INT(1, ui->dock_node_is_leaf(ctx, fr.live));
			TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_LAYOUT_KIND_LEAF, (unsigned)fr.doc->kind);
			TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, fr.live, ids, SK_UI_DOCK_LEAF_TABS_MAX, &count, &active));
			TEST_ASSERT_EQUAL_UINT(count, fr.doc->tab_count);
			for (i = 0u; i < count; ++i) {
				TEST_ASSERT_EQUAL_STRING(ids[i], fr.doc->tabs[i]);
			}
			if (count > 0u) {
				TEST_ASSERT_EQUAL_UINT(active, fr.doc->active_index);
				TEST_ASSERT_NOT_NULL(fr.doc->tabs[active]);
				TEST_ASSERT_EQUAL_STRING(ids[active], fr.doc->tabs[active]);
			}
		}
	}
}

static void ui_dock_assert_floats_match(const sk_ui_api_t* ui, const sk_ui_context_t* ctx, const sk_ui_dock_layout_t* doc) {
	u32 i;
	u32 n;
	u32 seen = 0u;
	if (!sk_ui_node_is_valid(ctx->dock_overlay)) {
		TEST_ASSERT_EQUAL_UINT(0u, doc->floating_count);
		return;
	}
	n = ui->node_child_count(ctx, ctx->dock_overlay);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t win = ui->node_child_at(ctx, ctx->dock_overlay, i);
		const_chr_t id;
		sk_ui_layout_style_t st;
		i32 z;
		const sk_ui_dock_layout_float_t* fl;
		if (ui_dock_is_widget(ctx, win, "editor_window") == 0) {
			continue;
		}
		id = ui->node_get_id(ctx, win);
		if (id == NULL || id[0] == '\0') {
			continue;
		}
		TEST_ASSERT_TRUE(seen < doc->floating_count);
		fl = &doc->floating[seen];
		TEST_ASSERT_EQUAL_STRING(id, fl->window_id);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, win, &st));
		TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.left, 0.0f), fl->x);
		TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.top, 0.0f), fl->y);
		TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.width, UI_DOCK_FLOAT_W), fl->w);
		TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.height, UI_DOCK_FLOAT_H), fl->h);
		z = ui_dock_prop_i32(ctx, win, "z_index", UI_DOCK_FLOAT_Z);
		TEST_ASSERT_EQUAL_INT(z, fl->z);
		seen += 1u;
	}
	TEST_ASSERT_EQUAL_UINT(seen, doc->floating_count);
}

SK_TEST(ui_dock_layout_save_single_leaf_golden) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	char json[4096];
	u32 len = 0u;
	sk_ui_dock_layout_t doc;
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "leaf-space", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, root, "root"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_save_json(ctx, "leaf-space", json, (u32)sizeof(json), &len));
	TEST_ASSERT_TRUE(len > 0u);
	TEST_ASSERT_TRUE(strstr(json, "\"version\"") != NULL);
	ui_dock_layout_assert_golden("v1_single_leaf", json, len);

	memset(&doc, 0, sizeof(doc));
	TEST_ASSERT_EQUAL_INT(0, ui_dock_layout_parse_json(ctx->allocator, json, len, &doc));
	TEST_ASSERT_EQUAL_INT(SK_UI_DOCK_LAYOUT_VERSION, doc.version);
	TEST_ASSERT_EQUAL_STRING("leaf-space", doc.id);
	ui_dock_assert_live_matches_doc(ui, ctx, ui->dockspace_find(ctx, "leaf-space"), doc.root);
	ui_dock_assert_floats_match(ui, ctx, &doc);
	ui_dock_layout_schema_doc_free(ctx->allocator, &doc);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_save_workspace_golden_and_roundtrip) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t bottom;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t split;
	sk_ui_node_t profiler;
	sk_ui_node_t inspector;
	char json[8192];
	u32 len = 0u;
	sk_ui_dock_layout_t doc;
	const_chr_t tabs[4];
	u32 tab_count = 0u;
	u32 active = 0u;
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Game", "game");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Console", "console");
	profiler = ui->widget_editor_window(ctx, ui->context_root(ctx), "Profiler", "profiler");
	inspector = ui->widget_editor_window(ctx, ui->context_root(ctx), "Inspector", "inspector");

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "editor-main", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &left, &rest));
	split = ui->dockspace_find(ctx, "editor-main");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, split, "root"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, left, "left"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, rest, SK_UI_DOCK_DIR_DOWN, 0.25f, &bottom, &center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, center, "central"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, bottom, "bottom"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "game", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "profiler", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "inspector", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_set_active(ctx, "game"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, center, tabs, 4u, &tab_count, &active));
	TEST_ASSERT_TRUE(tab_count >= 2u);
	TEST_ASSERT_TRUE(active != 0u);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "profiler"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "inspector"));
	ui_dock_test_set_float_rect(ui, ctx, profiler, 80.0f, 60.0f, 360.0f, 240.0f, 50);
	ui_dock_test_set_float_rect(ui, ctx, inspector, 120.0f, 90.0f, 320.0f, 200.0f, 51);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_save_json(ctx, "editor-main", json, (u32)sizeof(json), &len));
	TEST_ASSERT_TRUE(len > 0u);
	TEST_ASSERT_TRUE(strstr(json, "\"version\"") != NULL);
	TEST_ASSERT_TRUE(strstr(json, "\"floating\"") != NULL);
	TEST_ASSERT_TRUE(strstr(json, "profiler") != NULL);
	ui_dock_layout_assert_golden("v1_workspace", json, len);

	memset(&doc, 0, sizeof(doc));
	TEST_ASSERT_EQUAL_INT(0, ui_dock_layout_parse_json(ctx->allocator, json, len, &doc));
	TEST_ASSERT_EQUAL_INT(SK_UI_DOCK_LAYOUT_VERSION, doc.version);
	TEST_ASSERT_EQUAL_STRING("editor-main", doc.id);
	TEST_ASSERT_EQUAL_UINT(SK_UI_DOCKSPACE_KEEP_CENTRAL, doc.flags);
	TEST_ASSERT_NOT_NULL(doc.root);
	TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_LAYOUT_KIND_SPLIT, (unsigned)doc.root->kind);
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_HORIZONTAL, (int)doc.root->axis);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, doc.root->ratio);
	TEST_ASSERT_NOT_NULL(doc.root->child_b);
	TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_LAYOUT_KIND_SPLIT, (unsigned)doc.root->child_b->kind);
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_VERTICAL, (int)doc.root->child_b->axis);
	ui_dock_assert_live_matches_doc(ui, ctx, ui->dockspace_find(ctx, "editor-main"), doc.root);
	ui_dock_assert_floats_match(ui, ctx, &doc);
	TEST_ASSERT_EQUAL_UINT(2u, doc.floating_count);
	ui_dock_layout_schema_doc_free(ctx->allocator, &doc);

	TEST_ASSERT_TRUE(ui->dock_layout_save_json(ctx, "missing-space", json, (u32)sizeof(json), &len) != 0);
	TEST_ASSERT_TRUE(ui->dock_layout_save_json(ctx, "editor-main", json, 8u, &len) != 0);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_serialize_nested_tabs_and_float) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t bottom;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t split;
	sk_ui_node_t floater;
	char json[8192];
	u32 len = 0u;
	sk_ui_dock_layout_t doc;
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Alpha", "alpha");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Beta", "beta");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Console", "console");
	floater = ui->widget_editor_window(ctx, ui->context_root(ctx), "Float", "float-win");

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "iso-space", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.3f, &left, &rest));
	split = ui->dockspace_find(ctx, "iso-space");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, split, "root"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, left, "tools"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, rest, SK_UI_DOCK_DIR_DOWN, 0.4f, &bottom, &center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, center, "stage"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, bottom, "log"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "alpha", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "beta", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "float-win", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_set_active(ctx, "beta"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "float-win"));
	ui_dock_test_set_float_rect(ui, ctx, floater, 40.0f, 50.0f, 280.0f, 160.0f, 60);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_save_json(ctx, "iso-space", json, (u32)sizeof(json), &len));
	TEST_ASSERT_TRUE(len > 0u);
	TEST_ASSERT_NOT_NULL(strstr(json, "\"version\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"id\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"flags\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"root\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"kind\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"axis\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"ratio\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"tabs\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"active_index\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"active\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"floating\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"x\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"y\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"w\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"h\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"z\""));

	memset(&doc, 0, sizeof(doc));
	TEST_ASSERT_EQUAL_INT(0, ui_dock_layout_parse_json(ctx->allocator, json, len, &doc));
	TEST_ASSERT_EQUAL_INT(SK_UI_DOCK_LAYOUT_VERSION, doc.version);
	TEST_ASSERT_EQUAL_INT(0, sk_ui_dock_layout_version_supported(doc.version));
	TEST_ASSERT_EQUAL_STRING("iso-space", doc.id);
	TEST_ASSERT_EQUAL_UINT(SK_UI_DOCKSPACE_KEEP_CENTRAL, doc.flags);
	TEST_ASSERT_NOT_NULL(doc.root);
	TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_LAYOUT_KIND_SPLIT, (unsigned)doc.root->kind);
	TEST_ASSERT_EQUAL_STRING("root", doc.root->id);
	TEST_ASSERT_EQUAL_UINT(0u, doc.root->flags);
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_HORIZONTAL, (int)doc.root->axis);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.3f, doc.root->ratio);
	TEST_ASSERT_NOT_NULL(doc.root->child_a);
	TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_LAYOUT_KIND_LEAF, (unsigned)doc.root->child_a->kind);
	TEST_ASSERT_EQUAL_STRING("tools", doc.root->child_a->id);
	TEST_ASSERT_EQUAL_UINT(0u, doc.root->child_a->flags);
	TEST_ASSERT_EQUAL_UINT(2u, doc.root->child_a->tab_count);
	TEST_ASSERT_EQUAL_STRING("alpha", doc.root->child_a->tabs[0]);
	TEST_ASSERT_EQUAL_STRING("beta", doc.root->child_a->tabs[1]);
	TEST_ASSERT_EQUAL_UINT(1u, doc.root->child_a->active_index);
	TEST_ASSERT_EQUAL_STRING("beta", doc.root->child_a->tabs[doc.root->child_a->active_index]);
	TEST_ASSERT_NOT_NULL(doc.root->child_b);
	TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_LAYOUT_KIND_SPLIT, (unsigned)doc.root->child_b->kind);
	TEST_ASSERT_EQUAL_STRING("root/1", doc.root->child_b->id);
	TEST_ASSERT_EQUAL_UINT(0u, doc.root->child_b->flags);
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_VERTICAL, (int)doc.root->child_b->axis);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6f, doc.root->child_b->ratio);
	TEST_ASSERT_NOT_NULL(doc.root->child_b->child_a);
	TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_LAYOUT_KIND_LEAF, (unsigned)doc.root->child_b->child_a->kind);
	TEST_ASSERT_EQUAL_STRING("stage", doc.root->child_b->child_a->id);
	TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_NODE_CENTRAL, (unsigned)doc.root->child_b->child_a->flags);
	TEST_ASSERT_EQUAL_UINT(1u, doc.root->child_b->child_a->tab_count);
	TEST_ASSERT_EQUAL_STRING("scene", doc.root->child_b->child_a->tabs[0]);
	TEST_ASSERT_EQUAL_UINT(0u, doc.root->child_b->child_a->active_index);
	TEST_ASSERT_EQUAL_STRING("scene", doc.root->child_b->child_a->tabs[doc.root->child_b->child_a->active_index]);
	TEST_ASSERT_NOT_NULL(doc.root->child_b->child_b);
	TEST_ASSERT_EQUAL_UINT((unsigned)SK_UI_DOCK_LAYOUT_KIND_LEAF, (unsigned)doc.root->child_b->child_b->kind);
	TEST_ASSERT_EQUAL_STRING("log", doc.root->child_b->child_b->id);
	TEST_ASSERT_EQUAL_UINT(0u, doc.root->child_b->child_b->flags);
	TEST_ASSERT_EQUAL_UINT(1u, doc.root->child_b->child_b->tab_count);
	TEST_ASSERT_EQUAL_STRING("console", doc.root->child_b->child_b->tabs[0]);
	TEST_ASSERT_EQUAL_UINT(0u, doc.root->child_b->child_b->active_index);
	TEST_ASSERT_EQUAL_STRING("console", doc.root->child_b->child_b->tabs[doc.root->child_b->child_b->active_index]);
	TEST_ASSERT_EQUAL_UINT(1u, doc.floating_count);
	TEST_ASSERT_EQUAL_STRING("float-win", doc.floating[0].window_id);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 40.0f, doc.floating[0].x);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 50.0f, doc.floating[0].y);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 280.0f, doc.floating[0].w);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 160.0f, doc.floating[0].h);
	TEST_ASSERT_EQUAL_INT(60, doc.floating[0].z);
	ui_dock_layout_schema_doc_free(ctx->allocator, &doc);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_version_policy) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* newer = "{\n"
						"    \"version\": 99,\n"
						"    \"id\": \"leaf-space\",\n"
						"    \"flags\": 0,\n"
						"    \"root\": { \"kind\": \"leaf\", \"id\": \"root\", \"flags\": 0, \"tabs\": [\"other\"], \"active_index\": 0 },\n"
						"    \"floating\": []\n"
						"}";
	const char* older = "{\n"
						"    \"version\": 0,\n"
						"    \"id\": \"leaf-space\",\n"
						"    \"flags\": 0,\n"
						"    \"root\": { \"kind\": \"leaf\", \"id\": \"root\", \"flags\": 0, \"tabs\": [\"other\"], \"active_index\": 0 },\n"
						"    \"floating\": []\n"
						"}";

	TEST_ASSERT_EQUAL_INT(0, sk_ui_dock_layout_version_supported(SK_UI_DOCK_LAYOUT_VERSION));
	TEST_ASSERT_EQUAL_INT(0, sk_ui_dock_layout_version_supported(1));
	TEST_ASSERT_TRUE(sk_ui_dock_layout_version_supported(0) != 0);
	TEST_ASSERT_TRUE(sk_ui_dock_layout_version_supported(99) != 0);
	TEST_ASSERT_TRUE(sk_ui_dock_layout_version_supported(-1) != 0);

	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "leaf-space", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", newer, (u32)strlen(newer)) != 0);
	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", older, (u32)strlen(older)) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, ui->dockspace_find(ctx, "leaf-space"), ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", ids[0]);
	TEST_ASSERT_EQUAL_UINT(0u, active);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_parse_version_gate) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_ui_dock_layout_t doc;
	const char* bad = "{\n"
					  "    \"version\": 99,\n"
					  "    \"id\": \"x\",\n"
					  "    \"flags\": 0,\n"
					  "    \"root\": {\n"
					  "        \"kind\": \"leaf\",\n"
					  "        \"id\": \"root\",\n"
					  "        \"flags\": 0,\n"
					  "        \"tabs\": []\n"
					  "    },\n"
					  "    \"floating\": []\n"
					  "}";
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	memset(&doc, 0, sizeof(doc));
	TEST_ASSERT_EQUAL_INT(0, ui_dock_layout_parse_json(a, bad, (u32)strlen(bad), &doc));
	TEST_ASSERT_EQUAL_INT(99, doc.version);
	TEST_ASSERT_TRUE(doc.version != SK_UI_DOCK_LAYOUT_VERSION);
	ui_dock_layout_schema_doc_free(a, &doc);

	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "x", bad, (u32)strlen(bad)) != 0);
	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "x", "{}", 2u) != 0);

	ui->context_destroy(ctx);
}

static void ui_dock_test_make_workspace_windows(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t* out_profiler, sk_ui_node_t* out_inspector) {
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Game", "game");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Console", "console");
	{
		sk_ui_node_t profiler = ui->widget_editor_window(ctx, ui->context_root(ctx), "Profiler", "profiler");
		sk_ui_node_t inspector = ui->widget_editor_window(ctx, ui->context_root(ctx), "Inspector", "inspector");
		if (out_profiler != NULL) {
			*out_profiler = profiler;
		}
		if (out_inspector != NULL) {
			*out_inspector = inspector;
		}
	}
}

static void ui_dock_test_build_workspace(const sk_ui_api_t* ui, sk_ui_context_t* ctx) {
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t bottom;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t split;
	sk_ui_node_t profiler;
	sk_ui_node_t inspector;

	ui_dock_test_make_workspace_windows(ui, ctx, &profiler, &inspector);
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "editor-main", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &left, &rest));
	split = ui->dockspace_find(ctx, "editor-main");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, split, "root"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, left, "left"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, rest, SK_UI_DOCK_DIR_DOWN, 0.25f, &bottom, &center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, center, "central"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, bottom, "bottom"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "game", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "profiler", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "inspector", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_set_active(ctx, "game"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "profiler"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "inspector"));
	ui_dock_test_set_float_rect(ui, ctx, profiler, 80.0f, 60.0f, 360.0f, 240.0f, 50);
	ui_dock_test_set_float_rect(ui, ctx, inspector, 120.0f, 90.0f, 320.0f, 200.0f, 51);
}

static void ui_dock_assert_trees_equal(const sk_ui_api_t* ui, const sk_ui_context_t* a, sk_ui_dock_node_t na, const sk_ui_context_t* b, sk_ui_dock_node_t nb) {
	typedef struct ui_dock_eq_frame_t {
		sk_ui_dock_node_t a;
		sk_ui_dock_node_t b;
	} ui_dock_eq_frame_t;
	ui_dock_eq_frame_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	stack[sp].a = na;
	stack[sp].b = nb;
	sp += 1u;
	while (sp > 0u) {
		ui_dock_eq_frame_t fr = stack[--sp];
		i32 a_split = ui->dock_node_is_split(a, fr.a);
		TEST_ASSERT_EQUAL_INT(a_split, ui->dock_node_is_split(b, fr.b));
		if (a_split != 0) {
			TEST_ASSERT_EQUAL_INT((int)ui->dock_split_get_axis(a, fr.a), (int)ui->dock_split_get_axis(b, fr.b));
			TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui->dock_split_get_ratio(a, fr.a), ui->dock_split_get_ratio(b, fr.b));
			if (sp + 2u > UI_DOCK_WALK_MAX) {
				TEST_FAIL_MESSAGE("dock tree compare stack overflow");
			}
			stack[sp].a = ui->dock_split_child(a, fr.a, 1u);
			stack[sp].b = ui->dock_split_child(b, fr.b, 1u);
			sp += 1u;
			stack[sp].a = ui->dock_split_child(a, fr.a, 0u);
			stack[sp].b = ui->dock_split_child(b, fr.b, 0u);
			sp += 1u;
			continue;
		}
		{
			const_chr_t a_ids[SK_UI_DOCK_LEAF_TABS_MAX];
			const_chr_t b_ids[SK_UI_DOCK_LEAF_TABS_MAX];
			u32 a_count = 0u;
			u32 b_count = 0u;
			u32 a_active = 0u;
			u32 b_active = 0u;
			u32 i;
			TEST_ASSERT_EQUAL_INT(1, ui->dock_node_is_leaf(a, fr.a));
			TEST_ASSERT_EQUAL_INT(1, ui->dock_node_is_leaf(b, fr.b));
			TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(a, fr.a, a_ids, SK_UI_DOCK_LEAF_TABS_MAX, &a_count, &a_active));
			TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(b, fr.b, b_ids, SK_UI_DOCK_LEAF_TABS_MAX, &b_count, &b_active));
			TEST_ASSERT_EQUAL_UINT(a_count, b_count);
			TEST_ASSERT_EQUAL_UINT(a_active, b_active);
			for (i = 0u; i < a_count; ++i) {
				TEST_ASSERT_EQUAL_STRING(a_ids[i], b_ids[i]);
			}
		}
	}
}

static void ui_dock_assert_geometry_equal(const sk_ui_api_t* ui, sk_ui_context_t* a, sk_ui_dock_node_t na, sk_ui_context_t* b, sk_ui_dock_node_t nb, const sk_ui_rect_t* space) {
	typedef struct ui_dock_geo_frame_t {
		sk_ui_dock_node_t a;
		sk_ui_dock_node_t b;
	} ui_dock_geo_frame_t;
	ui_dock_geo_frame_t stack[UI_DOCK_WALK_MAX];
	u32 sp = 0u;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(a, na, space));
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(b, nb, space));
	stack[sp].a = na;
	stack[sp].b = nb;
	sp += 1u;
	while (sp > 0u) {
		ui_dock_geo_frame_t fr = stack[--sp];
		sk_ui_rect_t ra;
		sk_ui_rect_t rb;
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(a, fr.a, &ra));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(b, fr.b, &rb));
		TEST_ASSERT_FLOAT_WITHIN(0.01f, ra.x, rb.x);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, ra.y, rb.y);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, ra.width, rb.width);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, ra.height, rb.height);
		if (ui->dock_node_is_split(a, fr.a) != 0) {
			sk_ui_rect_t sa;
			sk_ui_rect_t sb;
			TEST_ASSERT_EQUAL_INT(0, ui->dock_split_get_splitter_rect(a, fr.a, &sa));
			TEST_ASSERT_EQUAL_INT(0, ui->dock_split_get_splitter_rect(b, fr.b, &sb));
			TEST_ASSERT_FLOAT_WITHIN(0.01f, sa.x, sb.x);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, sa.y, sb.y);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, sa.width, sb.width);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, sa.height, sb.height);
			if (sp + 2u > UI_DOCK_WALK_MAX) {
				TEST_FAIL_MESSAGE("dock geometry compare stack overflow");
			}
			stack[sp].a = ui->dock_split_child(a, fr.a, 1u);
			stack[sp].b = ui->dock_split_child(b, fr.b, 1u);
			sp += 1u;
			stack[sp].a = ui->dock_split_child(a, fr.a, 0u);
			stack[sp].b = ui->dock_split_child(b, fr.b, 0u);
			sp += 1u;
		}
	}
}

static i32 ui_dock_layout_read_golden(const char* name, char* out, u32 cap, u32* out_len) {
	char path[512];
	FILE* f;
	long file_size;
	u32 n;
	u32 w;
	u32 i;
	if (ui_dock_layout_golden_path(path, (u32)sizeof(path), name) != 0) {
		return -1;
	}
	f = fopen(path, "rb");
	if (f == NULL) {
		return -1;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	file_size = ftell(f);
	if (file_size < 0 || (u32)file_size + 1u > cap) {
		fclose(f);
		return -1;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	n = (u32)file_size;
	if (fread(out, 1u, n, f) != n) {
		fclose(f);
		return -1;
	}
	fclose(f);
	w = 0u;
	for (i = 0u; i < n; ++i) {
		if (out[i] == '\r') {
			continue;
		}
		out[w++] = out[i];
	}
	out[w] = '\0';
	if (out_len != NULL) {
		*out_len = w;
	}
	return 0;
}

SK_TEST(ui_dock_layout_save_then_restore_structurally_equal) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* src = ui->context_create(NULL);
	sk_ui_context_t* dst = ui->context_create(NULL);
	char json[8192];
	u32 len = 0u;
	sk_ui_dock_layout_t doc;
	sk_ui_node_t hier;
	sk_ui_node_t scene;
	TEST_ASSERT_NOT_NULL(src);
	TEST_ASSERT_NOT_NULL(dst);

	ui_dock_test_build_workspace(ui, src);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_save_json(src, "editor-main", json, (u32)sizeof(json), &len));
	TEST_ASSERT_TRUE(len > 0u);

	ui_dock_test_make_workspace_windows(ui, dst, NULL, NULL);
	hier = ui->find_by_id(dst, "hierarchy");
	scene = ui->find_by_id(dst, "scene");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(hier));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(scene));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(dst, "editor-main", json, len));
	TEST_ASSERT_TRUE(ui->node_alive(dst, hier));
	TEST_ASSERT_TRUE(ui->node_alive(dst, scene));

	{
		sk_ui_rect_t space;
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 1280.0f;
		space.height = 720.0f;
		ui_dock_assert_trees_equal(ui, src, ui->dockspace_find(src, "editor-main"), dst, ui->dockspace_find(dst, "editor-main"));
		ui_dock_assert_geometry_equal(ui, src, ui->dockspace_find(src, "editor-main"), dst, ui->dockspace_find(dst, "editor-main"), &space);
	}
	memset(&doc, 0, sizeof(doc));
	TEST_ASSERT_EQUAL_INT(0, ui_dock_layout_parse_json(dst->allocator, json, len, &doc));
	ui_dock_assert_live_matches_doc(ui, dst, ui->dockspace_find(dst, "editor-main"), doc.root);
	ui_dock_assert_floats_match(ui, dst, &doc);
	ui_dock_layout_schema_doc_free(dst->allocator, &doc);

	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(dst, "hierarchy"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(dst, "scene"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(dst, "profiler"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(dst, "inspector"));

	ui->context_destroy(src);
	ui->context_destroy(dst);
}

SK_TEST(ui_dock_layout_restore_golden_workspace) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	char json[8192];
	u32 len = 0u;
	char saved[8192];
	u32 saved_len = 0u;
	sk_ui_dock_layout_t doc;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t inner;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t bottom;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	TEST_ASSERT_NOT_NULL(ctx);

	TEST_ASSERT_EQUAL_INT(0, ui_dock_layout_read_golden("v1_workspace", json, (u32)sizeof(json), &len));
	ui_dock_test_make_workspace_windows(ui, ctx, NULL, NULL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "editor-main", json, len));

	root = ui->dockspace_find(ctx, "editor-main");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, root));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_HORIZONTAL, (int)ui->dock_split_get_axis(ctx, root));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, ui->dock_split_get_ratio(ctx, root));
	left = ui->dock_split_child(ctx, root, 0u);
	inner = ui->dock_split_child(ctx, root, 1u);
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, left));
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, inner));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_VERTICAL, (int)ui->dock_split_get_axis(ctx, inner));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.75f, ui->dock_split_get_ratio(ctx, inner));
	center = ui->dock_split_child(ctx, inner, 0u);
	bottom = ui->dock_split_child(ctx, inner, 1u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, left, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("hierarchy", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, center, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("scene", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("game", tabs[1]);
	TEST_ASSERT_EQUAL_UINT(1u, active);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, bottom, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", tabs[0]);

	memset(&doc, 0, sizeof(doc));
	TEST_ASSERT_EQUAL_INT(0, ui_dock_layout_parse_json(ctx->allocator, json, len, &doc));
	ui_dock_assert_live_matches_doc(ui, ctx, root, doc.root);
	ui_dock_assert_floats_match(ui, ctx, &doc);
	ui_dock_layout_schema_doc_free(ctx->allocator, &doc);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_save_json(ctx, "editor-main", saved, (u32)sizeof(saved), &saved_len));
	ui_dock_layout_assert_golden("v1_workspace", saved, saved_len);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_same_context_keeps_windows) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	char json[8192];
	u32 len = 0u;
	sk_ui_node_t hier;
	sk_ui_node_t scene;
	sk_ui_node_t profiler;
	TEST_ASSERT_NOT_NULL(ctx);

	ui_dock_test_build_workspace(ui, ctx);
	hier = ui->find_by_id(ctx, "hierarchy");
	scene = ui->find_by_id(ctx, "scene");
	profiler = ui->find_by_id(ctx, "profiler");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_save_json(ctx, "editor-main", json, (u32)sizeof(json), &len));

	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_destroy(ctx, "editor-main"));
	TEST_ASSERT_FALSE(sk_ui_dock_node_is_valid(ui->dockspace_find(ctx, "editor-main")));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "editor-main", json, len));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, hier));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, scene));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, profiler));
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, ui->dockspace_find(ctx, "editor-main")));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "hierarchy"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "profiler"));

	ui->context_destroy(ctx);
}

static void ui_dock_test_assert_no_empty_groups(const sk_ui_api_t* ui, const sk_ui_context_t* ctx, sk_ui_dock_node_t root) {
	typedef struct ui_dock_empty_frame_t {
		sk_ui_dock_node_t node;
	} ui_dock_empty_frame_t;
	ui_dock_empty_frame_t stack[128];
	u32 sp = 0u;
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(root));
	stack[sp++].node = root;
	while (sp > 0u) {
		sk_ui_dock_node_t cur = stack[--sp].node;
		if (ui->dock_node_is_split(ctx, cur) != 0) {
			sk_ui_dock_node_t a = ui->dock_split_child(ctx, cur, 0u);
			sk_ui_dock_node_t b = ui->dock_split_child(ctx, cur, 1u);
			TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(a));
			TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(b));
			if (sp >= 128u) {
				TEST_FAIL_MESSAGE("dock empty-group walk overflow");
				return;
			}
			stack[sp++].node = b;
			if (sp >= 128u) {
				TEST_FAIL_MESSAGE("dock empty-group walk overflow");
				return;
			}
			stack[sp++].node = a;
			continue;
		}
		{
			const_chr_t tabs[SK_UI_DOCK_LEAF_TABS_MAX];
			u32 count = 0u;
			u32 active = 0u;
			TEST_ASSERT_EQUAL_INT(1, ui->dock_node_is_leaf(ctx, cur));
			TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, cur, tabs, SK_UI_DOCK_LEAF_TABS_MAX, &count, &active));
			TEST_ASSERT_TRUE(count > 0u);
		}
	}
}

static void ui_dock_test_assert_fills_space(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_dock_node_t node, const sk_ui_rect_t* space) {
	sk_ui_rect_t box;
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, node, &box));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, space->x, box.x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, space->y, box.y);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, space->width, box.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, space->height, box.height);
}

SK_TEST(ui_dock_layout_restore_drop_collapses_split) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t a;
	sk_ui_dock_node_t b;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"mismatch\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.3,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"gone-side\", \"flags\": 0, \"tabs\": [\"gone\"], \"active_index\": 0 },\n"
					   "        \"b\": {\n"
					   "            \"kind\": \"split\",\n"
					   "            \"axis\": 1,\n"
					   "            \"ratio\": 0.6,\n"
					   "            \"id\": \"inner\",\n"
					   "            \"flags\": 0,\n"
					   "            \"a\": { \"kind\": \"leaf\", \"id\": \"top\", \"flags\": 0, \"tabs\": [\"keep-a\"], \"active_index\": 0 },\n"
					   "            \"b\": { \"kind\": \"leaf\", \"id\": \"bot\", \"flags\": 0, \"tabs\": [\"keep-b\"], \"active_index\": 0 }\n"
					   "        }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "A", "keep-a");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "B", "keep-b");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "mismatch", json, (u32)strlen(json)));

	root = ui->dockspace_find(ctx, "mismatch");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, root));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_VERTICAL, (int)ui->dock_split_get_axis(ctx, root));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6f, ui->dock_split_get_ratio(ctx, root));
	a = ui->dock_split_child(ctx, root, 0u);
	b = ui->dock_split_child(ctx, root, 1u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, a, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("keep-a", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, b, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("keep-b", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "gone"));
	ui_dock_test_assert_no_empty_groups(ui, ctx, root);
	{
		sk_ui_rect_t space;
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 800.0f;
		space.height = 600.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, root, &space));
		ui_dock_test_assert_fills_space(ui, ctx, root, &space);
	}

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_drop_cascades_nested) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"cascade\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.25,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": {\n"
					   "            \"kind\": \"split\",\n"
					   "            \"axis\": 1,\n"
					   "            \"ratio\": 0.4,\n"
					   "            \"id\": \"dead\",\n"
					   "            \"flags\": 0,\n"
					   "            \"a\": { \"kind\": \"leaf\", \"id\": \"gone-a\", \"flags\": 0, \"tabs\": [\"gone-1\"], \"active_index\": 0 },\n"
					   "            \"b\": { \"kind\": \"leaf\", \"id\": \"gone-b\", \"flags\": 0, \"tabs\": [\"gone-2\"], \"active_index\": 0 }\n"
					   "        },\n"
					   "        \"b\": { \"kind\": \"leaf\", \"id\": \"keep\", \"flags\": 0, \"tabs\": [\"keep\"], \"active_index\": 0 }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Keep", "keep");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "cascade", json, (u32)strlen(json)));

	root = ui->dockspace_find(ctx, "cascade");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("keep", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "gone-1"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "gone-2"));
	ui_dock_test_assert_no_empty_groups(ui, ctx, root);
	{
		sk_ui_rect_t space;
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 800.0f;
		space.height = 600.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, root, &space));
		ui_dock_test_assert_fills_space(ui, ctx, root, &space);
	}

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_unsaved_default_dock) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"unsaved\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.25,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"left\", \"flags\": 0, \"tabs\": [\"hierarchy\"], \"active_index\": 0 },\n"
					   "        \"b\": { \"kind\": \"leaf\", \"id\": \"center\", \"flags\": 1, \"tabs\": [\"scene\"], \"active_index\": 0 }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Inspector", "inspector");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "inspector", "left", NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "unsaved", json, (u32)strlen(json)));

	root = ui->dockspace_find(ctx, "unsaved");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, root));
	left = ui->dock_split_child(ctx, root, 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, left, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("hierarchy", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("inspector", tabs[1]);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "inspector"));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "inspector"), left));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_unsaved_float) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t profiler;
	sk_ui_rect_t def;
	sk_ui_layout_style_t st;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"unsaved-float\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": { \"kind\": \"leaf\", \"id\": \"root\", \"flags\": 0, \"tabs\": [\"hierarchy\"], \"active_index\": 0 },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);

	def.x = 88.0f;
	def.y = 66.0f;
	def.width = 320.0f;
	def.height = 200.0f;
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	profiler = ui->widget_editor_window(ctx, ui->context_root(ctx), "Profiler", "profiler");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "profiler", NULL, &def));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "unsaved-float", json, (u32)strlen(json)));

	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "profiler"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "hierarchy"));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(ctx, profiler), ctx->dock_overlay));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, profiler, &st));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_POSITION_ABSOLUTE, (int)st.position);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.left, 0.0f), def.x);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.top, 0.0f), def.y);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.width, UI_DOCK_FLOAT_W), def.width);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.height, UI_DOCK_FLOAT_H), def.height);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_removed_window_keeps_sibling_tabs) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"removed-tab\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"leaf\",\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"tabs\": [\"keep\", \"gone\", \"also\"],\n"
					   "        \"active_index\": 1\n"
					   "    },\n"
					   "    \"floating\": [ { \"id\": \"ghost\", \"x\": 10, \"y\": 10, \"w\": 100, \"h\": 80, \"z\": 50 } ]\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Keep", "keep");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Also", "also");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "keep", NULL, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "also", NULL, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "removed-tab", json, (u32)strlen(json)));

	root = ui->dockspace_find(ctx, "removed-tab");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("keep", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("also", tabs[1]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "gone"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "ghost"));
	ui_dock_test_assert_no_empty_groups(ui, ctx, root);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_new_window_default_target_or_float) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_node_t profiler;
	sk_ui_node_t orphan;
	sk_ui_layout_style_t st;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"new-win\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.3,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"gone-side\", \"flags\": 0, \"tabs\": [\"gone\"], \"active_index\": 0 },\n"
					   "        \"b\": { \"kind\": \"leaf\", \"id\": \"left\", \"flags\": 0, \"tabs\": [\"hierarchy\"], \"active_index\": 0 }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Inspector", "inspector");
	profiler = ui->widget_editor_window(ctx, ui->context_root(ctx), "Profiler", "profiler");
	orphan = ui->widget_editor_window(ctx, ui->context_root(ctx), "Orphan", "orphan");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "inspector", "left", NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "profiler", NULL, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "orphan", "gone-side", NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "new-win", json, (u32)strlen(json)));

	root = ui->dockspace_find(ctx, "new-win");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("hierarchy", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("inspector", tabs[1]);
	left = ui->dock_find_node_for_window(ctx, "inspector");
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(left, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "profiler"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "orphan"));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(ctx, profiler), ctx->dock_overlay));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(ctx, orphan), ctx->dock_overlay));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, profiler, &st));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.left, 0.0f), 80.0f);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.top, 0.0f), 60.0f);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.width, UI_DOCK_FLOAT_W), UI_DOCK_FLOAT_W);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui_dock_layout_len_pt(st.height, UI_DOCK_FLOAT_H), UI_DOCK_FLOAT_H);
	ui_dock_test_assert_no_empty_groups(ui, ctx, root);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_drop_empty_branch_renormalizes) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t a;
	sk_ui_dock_node_t b;
	sk_ui_rect_t space;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const f32 leftover = 800.0f - SK_UI_DOCK_SPLITTER_PT;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"empty-branch\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.25,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"keep-a\", \"flags\": 0, \"tabs\": [\"keep-a\"], \"active_index\": 0 },\n"
					   "        \"b\": {\n"
					   "            \"kind\": \"split\",\n"
					   "            \"axis\": 0,\n"
					   "            \"ratio\": 0.4,\n"
					   "            \"id\": \"dead\",\n"
					   "            \"flags\": 0,\n"
					   "            \"a\": { \"kind\": \"leaf\", \"id\": \"gone\", \"flags\": 0, \"tabs\": [\"gone\"], \"active_index\": 0 },\n"
					   "            \"b\": { \"kind\": \"leaf\", \"id\": \"keep-b\", \"flags\": 0, \"tabs\": [\"keep-b\"], \"active_index\": 0 }\n"
					   "        }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "A", "keep-a");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "B", "keep-b");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "empty-branch", json, (u32)strlen(json)));

	root = ui->dockspace_find(ctx, "empty-branch");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, root));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_HORIZONTAL, (int)ui->dock_split_get_axis(ctx, root));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, ui->dock_split_get_ratio(ctx, root));
	a = ui->dock_split_child(ctx, root, 0u);
	b = ui->dock_split_child(ctx, root, 1u);
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, a));
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, b));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, a, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_STRING("keep-a", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, b, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_STRING("keep-b", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "gone"));
	ui_dock_test_assert_no_empty_groups(ui, ctx, root);

	space.x = 0.0f;
	space.y = 0.0f;
	space.width = 800.0f;
	space.height = 600.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, root, &space));
	ui_dock_test_assert_fills_space(ui, ctx, root, &space);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, a, &ra));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, b, &rb));
	TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.25f, ra.width);
	TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.75f, rb.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 600.0f, ra.height);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 600.0f, rb.height);

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_future_version_fallback) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* newer = "{\n"
						"    \"version\": 99,\n"
						"    \"id\": \"leaf-space\",\n"
						"    \"flags\": 0,\n"
						"    \"root\": { \"kind\": \"leaf\", \"id\": \"root\", \"flags\": 0, \"tabs\": [\"other\"], \"active_index\": 0 },\n"
						"    \"floating\": []\n"
						"}";

	TEST_ASSERT_TRUE(sk_ui_dock_layout_version_supported(99) != 0);

	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "leaf-space", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", newer, (u32)strlen(newer)) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, ui->dockspace_find(ctx, "leaf-space"), ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", ids[0]);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "console"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "other"));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_corrupt_fallback) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* corrupt = "{ not json at all [[[";
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "leaf-space", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", corrupt, (u32)strlen(corrupt)) != 0);
	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", "", 0u) != 0);
	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", "{", 1u) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, ui->dockspace_find(ctx, "leaf-space"), ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", ids[0]);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "console"));

	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_active_without_index) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"tabs\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"leaf\",\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"tabs\": [\"scene\", \"game\"],\n"
					   "        \"active\": \"game\"\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Game", "game");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "tabs", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "tabs");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("scene", ids[0]);
	TEST_ASSERT_EQUAL_STRING("game", ids[1]);
	TEST_ASSERT_EQUAL_UINT(1u, active);
	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_missing_version_rejected) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* json = "{\n"
					   "    \"id\": \"leaf-space\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": { \"kind\": \"leaf\", \"id\": \"root\", \"flags\": 0, \"tabs\": [\"other\"], \"active_index\": 0 },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "leaf-space", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", json, (u32)strlen(json)) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, ui->dockspace_find(ctx, "leaf-space"), ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", ids[0]);
	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_creates_missing_dockspace) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"startup\",\n"
					   "    \"flags\": 1,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"leaf\",\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 1,\n"
					   "        \"tabs\": [\"console\"],\n"
					   "        \"active_index\": 0\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Console", "console");
	TEST_ASSERT_FALSE(sk_ui_dock_node_is_valid(ui->dockspace_find(ctx, "startup")));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "startup", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "startup");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", ids[0]);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "console"));
	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_load_then_create) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_node_t later;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"late\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"leaf\",\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"tabs\": [\"later\"],\n"
					   "        \"active_index\": 0\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "later", NULL, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "late", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "late");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("later", ids[0]);
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->find_by_id(ctx, "later")));
	later = ui->widget_editor_window(ctx, ui->context_root(ctx), "Later", "later");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(later));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "later"));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "later"), root));
	TEST_ASSERT_TRUE(ui->node_alive(ctx, later));
	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_unsaved_after_create) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_node_t inspector;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"late-unsaved\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.25,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"left\", \"flags\": 0, \"tabs\": [\"hierarchy\"], \"active_index\": 0 },\n"
					   "        \"b\": { \"kind\": \"leaf\", \"id\": \"center\", \"flags\": 1, \"tabs\": [\"scene\"], \"active_index\": 0 }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "inspector", "left", NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "late-unsaved", json, (u32)strlen(json)));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->find_by_id(ctx, "inspector")));
	inspector = ui->widget_editor_window(ctx, ui->context_root(ctx), "Inspector", "inspector");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(inspector));
	root = ui->dockspace_find(ctx, "late-unsaved");
	left = ui->dock_split_child(ctx, root, 0u);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "inspector"));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "inspector"), left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, left, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("hierarchy", ids[0]);
	TEST_ASSERT_EQUAL_STRING("inspector", ids[1]);
	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_builder_open_rejected) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"leaf-space\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": { \"kind\": \"leaf\", \"id\": \"root\", \"flags\": 0, \"tabs\": [\"other\"], \"active_index\": 0 },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "leaf-space", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", root));
	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", json, (u32)strlen(json)) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, ui->dockspace_find(ctx, "leaf-space"), ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", ids[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "other"));
	ui->context_destroy(ctx);
}

SK_TEST(ui_dock_layout_restore_keep_central_empty_after_drop) {
	const sk_ui_api_t* ui = ui_dock_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"keep-central\",\n"
					   "    \"flags\": 1,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.3,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"left\", \"flags\": 0, \"tabs\": [\"hierarchy\"], \"active_index\": 0 },\n"
					   "        \"b\": { \"kind\": \"leaf\", \"id\": \"central\", \"flags\": 1, \"tabs\": [\"gone\"], \"active_index\": 0 }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "keep-central", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "keep-central");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, root));
	left = ui->dock_split_child(ctx, root, 0u);
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, left, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("hierarchy", ids[0]);
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, ui->dock_split_child(ctx, root, 1u)));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, ui->dock_split_child(ctx, root, 1u), ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(0u, count);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "gone"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "hierarchy"));
	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
