/**
 * @file input.c
 * @brief Hit testing, pointer capture, focus, and event routing.
 *
 * Coordinates are logical units matching the flex layout solver. Platform
 * hosts and the future UI tester share the same ingress: input_dispatch with
 * sk_ui_input_event_t. Routing expands those into enter/leave/move/down/up/
 * click/wheel/key/text/focus events with capture → target → bubble order.
 */

#include "ui_internal.h"

#include "allocator.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* Geometry helpers                                                           */
/* -------------------------------------------------------------------------- */

static i32 ui_rect_contains(const sk_ui_rect_t* r, f32 x, f32 y) {
	return x >= r->x && y >= r->y && x < (r->x + r->width) && y < (r->y + r->height);
}

static sk_ui_rect_t ui_rect_intersect(const sk_ui_rect_t* a, const sk_ui_rect_t* b) {
	sk_ui_rect_t out;
	f32 x0 = a->x > b->x ? a->x : b->x;
	f32 y0 = a->y > b->y ? a->y : b->y;
	f32 x1 = (a->x + a->width) < (b->x + b->width) ? (a->x + a->width) : (b->x + b->width);
	f32 y1 = (a->y + a->height) < (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);
	out.x = x0;
	out.y = y0;
	out.width = x1 > x0 ? (x1 - x0) : 0.0f;
	out.height = y1 > y0 ? (y1 - y0) : 0.0f;
	return out;
}

static i32 ui_node_is_disabled(const ui_node_slot_t* slot) {
	return (slot->state_flags & (u32)SK_UI_STATE_DISABLED) != 0u ? 1 : 0;
}

/**
 * Absolute border + content rects in root space.
 * Parent content origin is the reference for each child's layout_* rects.
 */
static i32 ui_abs_rects(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	f32 ox = 0.0f;
	f32 oy = 0.0f;
	sk_ui_node_t cur;
	sk_ui_node_t chain[64];
	u32 depth = 0u;
	u32 i;

	if (slot == NULL) {
		return -1;
	}

	/* Collect root → node path (excluding node itself for offset walk). */
	cur = slot->parent;
	while (sk_ui_node_is_valid(cur) && depth < 64u) {
		chain[depth++] = cur;
		{
			const ui_node_slot_t* p = ui_slot(ctx, cur);
			if (p == NULL) {
				break;
			}
			cur = p->parent;
		}
	}

	/* Walk root → parent, accumulating content origins. */
	for (i = depth; i > 0u; --i) {
		const ui_node_slot_t* p = ui_slot(ctx, chain[i - 1u]);
		if (p == NULL) {
			return -1;
		}
		ox += p->layout_content.x;
		oy += p->layout_content.y;
	}

	if (out_border != NULL) {
		out_border->x = ox + slot->layout_border.x;
		out_border->y = oy + slot->layout_border.y;
		out_border->width = slot->layout_border.width;
		out_border->height = slot->layout_border.height;
	}
	if (out_content != NULL) {
		out_content->x = ox + slot->layout_content.x;
		out_content->y = oy + slot->layout_content.y;
		out_content->width = slot->layout_content.width;
		out_content->height = slot->layout_content.height;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Hit testing                                                                */
/* -------------------------------------------------------------------------- */

typedef struct ui_hit_frame_t {
	sk_ui_node_t node;
	f32 origin_x; /**< Absolute content origin of this node (for children). */
	f32 origin_y;
	sk_ui_rect_t clip; /**< Active clip in absolute space. */
	i32 has_clip;
	u32 next_child; /**< Walk children reverse (z-order: last first). */
} ui_hit_frame_t;

static sk_ui_node_t ui_hit_test_walk(const sk_ui_context_t* ctx, f32 x, f32 y) {
	ui_hit_frame_t stack[64];
	u32 sp = 0u;
	const ui_node_slot_t* root_slot;
	sk_ui_node_t best = SK_UI_NODE_INVALID;
	sk_ui_rect_t root_border;
	sk_ui_rect_t root_content;

	if (!sk_ui_node_is_valid(ctx->root)) {
		return SK_UI_NODE_INVALID;
	}
	root_slot = ui_slot(ctx, ctx->root);
	if (root_slot == NULL) {
		return SK_UI_NODE_INVALID;
	}

	if (ui_abs_rects(ctx, ctx->root, &root_border, &root_content) != 0) {
		return SK_UI_NODE_INVALID;
	}

	stack[0].node = ctx->root;
	stack[0].origin_x = root_content.x;
	stack[0].origin_y = root_content.y;
	stack[0].has_clip = 0;
	stack[0].next_child = root_slot->children.count; /* reverse index+1 */
	if (root_slot->clip_children != 0u) {
		stack[0].clip = root_content;
		stack[0].has_clip = 1;
	}
	sp = 1u;

	while (sp > 0u) {
		ui_hit_frame_t* fr = &stack[sp - 1u];
		const ui_node_slot_t* slot = ui_slot(ctx, fr->node);
		if (slot == NULL) {
			--sp;
			continue;
		}

		/* First visit: consider this node as a hit candidate after children. */
		if (fr->next_child == slot->children.count) {
			/* Descend into children first (paint order: last child on top). */
		}

		if (fr->next_child > 0u) {
			sk_ui_node_t child;
			const ui_node_slot_t* child_slot;
			sk_ui_rect_t child_border;
			sk_ui_rect_t child_content;
			f32 cox;
			f32 coy;
			i32 in_clip = 1;

			fr->next_child -= 1u;
			child = slot->children.items[fr->next_child];
			child_slot = ui_slot(ctx, child);
			if (child_slot == NULL) {
				continue;
			}

			/* Child rects are relative to this node's content origin. */
			cox = fr->origin_x;
			coy = fr->origin_y;
			child_border.x = cox + child_slot->layout_border.x;
			child_border.y = coy + child_slot->layout_border.y;
			child_border.width = child_slot->layout_border.width;
			child_border.height = child_slot->layout_border.height;
			child_content.x = cox + child_slot->layout_content.x;
			child_content.y = coy + child_slot->layout_content.y;
			child_content.width = child_slot->layout_content.width;
			child_content.height = child_slot->layout_content.height;

			if (fr->has_clip) {
				/* Point must be inside parent clip to hit anything in this subtree. */
				if (!ui_rect_contains(&fr->clip, x, y)) {
					continue;
				}
				/* Child geometry outside clip is not hittable. */
				{
					sk_ui_rect_t clipped = ui_rect_intersect(&child_border, &fr->clip);
					if (clipped.width <= 0.0f || clipped.height <= 0.0f) {
						continue;
					}
					if (!ui_rect_contains(&clipped, x, y) && child_slot->children.count == 0u) {
						/* Entire border outside point under clip — skip leaf. */
						continue;
					}
					/* For non-leaves we still walk; clip is tightened below. */
					(void)in_clip;
				}
			}

			if (sp < 64u) {
				ui_hit_frame_t child_fr;
				child_fr.node = child;
				child_fr.origin_x = child_content.x;
				child_fr.origin_y = child_content.y;
				child_fr.next_child = child_slot->children.count;
				if (fr->has_clip) {
					if (child_slot->clip_children != 0u) {
						sk_ui_rect_t c = ui_rect_intersect(&fr->clip, &child_content);
						child_fr.clip = c;
						child_fr.has_clip = 1;
					} else {
						child_fr.clip = fr->clip;
						child_fr.has_clip = 1;
					}
				} else if (child_slot->clip_children != 0u) {
					child_fr.clip = child_content;
					child_fr.has_clip = 1;
				} else {
					child_fr.has_clip = 0;
					memset(&child_fr.clip, 0, sizeof(child_fr.clip));
				}
				stack[sp++] = child_fr;
			}
			continue;
		}

		/* No more children: test this node as a target.
		 * origin is absolute content origin; border abs =
		 * origin - layout_content + layout_border. */
		{
			sk_ui_rect_t border;
			i32 can_target;
			border.x = fr->origin_x - slot->layout_content.x + slot->layout_border.x;
			border.y = fr->origin_y - slot->layout_content.y + slot->layout_border.y;
			border.width = slot->layout_border.width;
			border.height = slot->layout_border.height;

			can_target = 1;
			if (slot->pointer_events == (u8)SK_UI_POINTER_EVENTS_NONE) {
				can_target = 0;
			}
			if (ui_node_is_disabled(slot)) {
				can_target = 0;
			}

			if (can_target && ui_rect_contains(&border, x, y)) {
				i32 ok = 1;
				if (fr->has_clip) {
					sk_ui_rect_t clipped = ui_rect_intersect(&border, &fr->clip);
					if (!ui_rect_contains(&clipped, x, y)) {
						ok = 0;
					}
				}
				if (ok) {
					/* Reverse-paint DFS: first finished hittable node is top-most. */
					return fr->node;
				}
			}
		}
		--sp;
	}

	return best;
}

sk_ui_node_t ui_hit_test_impl(const sk_ui_context_t* ctx, f32 x, f32 y) {
	return ui_hit_test_walk(ctx, x, y);
}

i32 ui_node_get_abs_rect_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_rect_t* out_border, sk_ui_rect_t* out_content) {
	return ui_abs_rects(ctx, node, out_border, out_content);
}

/* -------------------------------------------------------------------------- */
/* Interaction flags                                                          */
/* -------------------------------------------------------------------------- */

void ui_input_node_defaults(ui_node_slot_t* slot, sk_ui_node_kind_t kind) {
	memset(&slot->callbacks, 0, sizeof(slot->callbacks));
	slot->clip_children = 0u;
	slot->pointer_events = (u8)SK_UI_POINTER_EVENTS_AUTO;
	slot->focusable = (kind == SK_UI_NODE_KIND_BUTTON) ? 1u : 0u;
}

void ui_input_on_node_destroy(sk_ui_context_t* ctx, sk_ui_node_t node) {
	if (sk_ui_node_eq(ctx->hover, node)) {
		ctx->hover = SK_UI_NODE_INVALID;
	}
	if (sk_ui_node_eq(ctx->active, node)) {
		ctx->active = SK_UI_NODE_INVALID;
	}
	if (sk_ui_node_eq(ctx->focus, node)) {
		ctx->focus = SK_UI_NODE_INVALID;
		ctx->wants_keyboard = 0;
	}
	if (sk_ui_node_eq(ctx->pointer_capture, node)) {
		ctx->pointer_capture = SK_UI_NODE_INVALID;
	}
}

i32 ui_node_set_callbacks_impl(sk_ui_context_t* ctx, sk_ui_node_t node, const sk_ui_node_callbacks_t* callbacks) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (callbacks == NULL) {
		memset(&slot->callbacks, 0, sizeof(slot->callbacks));
	} else {
		slot->callbacks = *callbacks;
	}
	return 0;
}

i32 ui_node_get_callbacks_impl(const sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_node_callbacks_t* out) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	if (out != NULL) {
		*out = slot->callbacks;
	}
	return 0;
}

i32 ui_node_set_clip_children_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 clip) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	slot->clip_children = clip != 0 ? 1u : 0u;
	ui_mark_dirty_up(ctx, node, (u32)(SK_UI_DIRTY_LAYOUT | SK_UI_DIRTY_PAINT));
	return 0;
}

i32 ui_node_get_clip_children_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL && slot->clip_children != 0u ? 1 : 0;
}

i32 ui_node_set_pointer_events_impl(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_pointer_events_t mode) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	slot->pointer_events = (u8)mode;
	return 0;
}

sk_ui_pointer_events_t ui_node_get_pointer_events_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL ? (sk_ui_pointer_events_t)slot->pointer_events : SK_UI_POINTER_EVENTS_AUTO;
}

i32 ui_node_set_focusable_impl(sk_ui_context_t* ctx, sk_ui_node_t node, i32 focusable) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	if (slot == NULL) {
		return -1;
	}
	slot->focusable = focusable != 0 ? 1u : 0u;
	if (slot->focusable == 0u && sk_ui_node_eq(ctx->focus, node)) {
		/* Clear focus without dispatch when becoming non-focusable mid-flight. */
		u32 st = slot->state_flags & ~(u32)SK_UI_STATE_FOCUSED;
		(void)ui_node_set_state_impl(ctx, node, st);
		ctx->focus = SK_UI_NODE_INVALID;
		ctx->wants_keyboard = 0;
	}
	return 0;
}

i32 ui_node_get_focusable_impl(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	return slot != NULL && slot->focusable != 0u ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/* Event dispatch                                                             */
/* -------------------------------------------------------------------------- */

static sk_ui_event_fn ui_typed_handler(const sk_ui_node_callbacks_t* cb, sk_ui_event_type_t type) {
	switch (type) {
	case SK_UI_EVENT_POINTER_ENTER:
		return cb->on_pointer_enter;
	case SK_UI_EVENT_POINTER_LEAVE:
		return cb->on_pointer_leave;
	case SK_UI_EVENT_POINTER_MOVE:
		return cb->on_pointer_move;
	case SK_UI_EVENT_POINTER_DOWN:
		return cb->on_pointer_down;
	case SK_UI_EVENT_POINTER_UP:
		return cb->on_pointer_up;
	case SK_UI_EVENT_CLICK:
		return cb->on_click;
	case SK_UI_EVENT_WHEEL:
		return cb->on_wheel;
	case SK_UI_EVENT_KEY_DOWN:
		return cb->on_key_down;
	case SK_UI_EVENT_KEY_UP:
		return cb->on_key_up;
	case SK_UI_EVENT_TEXT_INPUT:
		return cb->on_text_input;
	case SK_UI_EVENT_FOCUS_IN:
		return cb->on_focus_in;
	case SK_UI_EVENT_FOCUS_OUT:
		return cb->on_focus_out;
	default:
		return NULL;
	}
}

static void ui_invoke_node(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	sk_ui_event_fn typed;
	if (slot == NULL) {
		return;
	}
	event->current = node;
	typed = ui_typed_handler(&slot->callbacks, event->type);
	if (typed != NULL) {
		typed(ctx, node, event, slot->callbacks.user);
		if (event->consumed != 0) {
			return;
		}
	}
	if (slot->callbacks.on_event != NULL) {
		slot->callbacks.on_event(ctx, node, event, slot->callbacks.user);
	}
}

/** Build root → target path inclusive. Returns count. */
static u32 ui_build_path(const sk_ui_context_t* ctx, sk_ui_node_t target, sk_ui_node_t* path, u32 max_path) {
	sk_ui_node_t cur = target;
	sk_ui_node_t rev[64];
	u32 n = 0u;
	u32 i;

	while (sk_ui_node_is_valid(cur) && n < 64u) {
		rev[n++] = cur;
		{
			const ui_node_slot_t* slot = ui_slot(ctx, cur);
			if (slot == NULL) {
				break;
			}
			cur = slot->parent;
		}
	}
	if (n > max_path) {
		n = max_path;
	}
	for (i = 0u; i < n; ++i) {
		path[i] = rev[n - 1u - i];
	}
	return n;
}

/**
 * Dispatch with capture → target → bubble.
 * Enter/leave are target-only (no bubble) for hover stability.
 */
static void ui_dispatch_event(sk_ui_context_t* ctx, sk_ui_event_t* event) {
	sk_ui_node_t path[64];
	u32 path_len;
	u32 i;
	i32 target_only = 0;

	if (!sk_ui_node_is_valid(event->target)) {
		return;
	}

	if (event->type == SK_UI_EVENT_POINTER_ENTER || event->type == SK_UI_EVENT_POINTER_LEAVE || event->type == SK_UI_EVENT_FOCUS_IN || event->type == SK_UI_EVENT_FOCUS_OUT) {
		target_only = 1;
	}

	path_len = ui_build_path(ctx, event->target, path, 64u);
	if (path_len == 0u) {
		return;
	}

	event->consumed = 0;

	if (target_only) {
		event->phase = SK_UI_EVENT_PHASE_TARGET;
		ui_invoke_node(ctx, event->target, event);
		return;
	}

	/* Capture: root → parent of target */
	event->phase = SK_UI_EVENT_PHASE_CAPTURE;
	for (i = 0u; i + 1u < path_len; ++i) {
		ui_invoke_node(ctx, path[i], event);
		if (event->consumed != 0) {
			return;
		}
	}

	/* Target */
	event->phase = SK_UI_EVENT_PHASE_TARGET;
	ui_invoke_node(ctx, event->target, event);
	if (event->consumed != 0) {
		return;
	}

	/* Bubble: parent → root */
	event->phase = SK_UI_EVENT_PHASE_BUBBLE;
	for (i = path_len - 1u; i > 0u; --i) {
		ui_invoke_node(ctx, path[i - 1u], event);
		if (event->consumed != 0) {
			return;
		}
	}
}

static void ui_event_seed(sk_ui_event_t* e, sk_ui_event_type_t type, sk_ui_node_t target, f32 x, f32 y) {
	memset(e, 0, sizeof(*e));
	e->type = type;
	e->target = target;
	e->current = target;
	e->x = x;
	e->y = y;
}

/* -------------------------------------------------------------------------- */
/* State flags helpers                                                        */
/* -------------------------------------------------------------------------- */

static void ui_set_state_bit(sk_ui_context_t* ctx, sk_ui_node_t node, u32 bit, i32 on) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	u32 st;
	if (slot == NULL) {
		return;
	}
	st = slot->state_flags;
	if (on) {
		st |= bit;
	} else {
		st &= ~bit;
	}
	if (st != slot->state_flags) {
		(void)ui_node_set_state_impl(ctx, node, st);
	}
}

/* -------------------------------------------------------------------------- */
/* Hover                                                                      */
/* -------------------------------------------------------------------------- */

static void ui_update_hover(sk_ui_context_t* ctx, sk_ui_node_t new_hover, f32 x, f32 y) {
	sk_ui_event_t ev;

	if (sk_ui_node_eq(ctx->hover, new_hover)) {
		return;
	}

	if (sk_ui_node_is_valid(ctx->hover)) {
		ui_set_state_bit(ctx, ctx->hover, (u32)SK_UI_STATE_HOVER, 0);
		ui_event_seed(&ev, SK_UI_EVENT_POINTER_LEAVE, ctx->hover, x, y);
		ui_dispatch_event(ctx, &ev);
	}

	ctx->hover = new_hover;

	if (sk_ui_node_is_valid(new_hover)) {
		ui_set_state_bit(ctx, new_hover, (u32)SK_UI_STATE_HOVER, 1);
		ui_event_seed(&ev, SK_UI_EVENT_POINTER_ENTER, new_hover, x, y);
		ui_dispatch_event(ctx, &ev);
	}
}

/* -------------------------------------------------------------------------- */
/* Focus                                                                      */
/* -------------------------------------------------------------------------- */

static i32 ui_node_can_focus(const sk_ui_context_t* ctx, sk_ui_node_t node) {
	const ui_node_slot_t* slot = ui_slot(ctx, node);
	if (slot == NULL) {
		return 0;
	}
	if (slot->focusable == 0u) {
		return 0;
	}
	if (ui_node_is_disabled(slot)) {
		return 0;
	}
	return 1;
}

i32 ui_focus_set_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_event_t ev;
	sk_ui_node_t prev;

	if (sk_ui_node_is_valid(node)) {
		if (!ui_node_can_focus(ctx, node)) {
			return -1;
		}
	}

	if (sk_ui_node_eq(ctx->focus, node)) {
		ctx->wants_keyboard = sk_ui_node_is_valid(node) ? 1 : 0;
		return 0;
	}

	prev = ctx->focus;
	if (sk_ui_node_is_valid(prev)) {
		ui_set_state_bit(ctx, prev, (u32)SK_UI_STATE_FOCUSED, 0);
		ui_event_seed(&ev, SK_UI_EVENT_FOCUS_OUT, prev, ctx->pointer_x, ctx->pointer_y);
		ui_dispatch_event(ctx, &ev);
	}

	ctx->focus = node;
	ctx->wants_keyboard = sk_ui_node_is_valid(node) ? 1 : 0;

	if (sk_ui_node_is_valid(node)) {
		ui_set_state_bit(ctx, node, (u32)SK_UI_STATE_FOCUSED, 1);
		ui_event_seed(&ev, SK_UI_EVENT_FOCUS_IN, node, ctx->pointer_x, ctx->pointer_y);
		ui_dispatch_event(ctx, &ev);
	}
	return 0;
}

sk_ui_node_t ui_focus_get_impl(const sk_ui_context_t* ctx) {
	return ctx->focus;
}

/** Collect focusable nodes in tree preorder into out[]. Returns count. */
static u32 ui_collect_focusable(const sk_ui_context_t* ctx, sk_ui_node_t* out, u32 max_out) {
	sk_ui_node_t stack[64];
	u32 sp = 0u;
	u32 count = 0u;

	if (!sk_ui_node_is_valid(ctx->root) || max_out == 0u) {
		return 0u;
	}
	stack[sp++] = ctx->root;

	while (sp > 0u) {
		sk_ui_node_t n = stack[--sp];
		const ui_node_slot_t* slot = ui_slot(ctx, n);
		u32 i;
		if (slot == NULL) {
			continue;
		}
		if (ui_node_can_focus(ctx, n) && count < max_out) {
			out[count++] = n;
		}
		/* Push children reverse so left-to-right preorder pops correctly. */
		for (i = slot->children.count; i > 0u; --i) {
			if (sp < 64u) {
				stack[sp++] = slot->children.items[i - 1u];
			}
		}
	}
	return count;
}

i32 ui_focus_advance_impl(sk_ui_context_t* ctx, i32 reverse) {
	sk_ui_node_t list[128];
	u32 count = ui_collect_focusable(ctx, list, 128u);
	u32 idx = 0u;
	u32 i;
	sk_ui_node_t next;

	if (count == 0u) {
		return 0;
	}

	if (sk_ui_node_is_valid(ctx->focus)) {
		for (i = 0u; i < count; ++i) {
			if (sk_ui_node_eq(list[i], ctx->focus)) {
				idx = i;
				break;
			}
		}
		if (reverse) {
			next = list[idx == 0u ? (count - 1u) : (idx - 1u)];
		} else {
			next = list[(idx + 1u) % count];
		}
	} else {
		next = reverse ? list[count - 1u] : list[0];
	}
	return ui_focus_set_impl(ctx, next);
}

/* -------------------------------------------------------------------------- */
/* Pointer capture                                                            */
/* -------------------------------------------------------------------------- */

sk_ui_node_t ui_pointer_capture_get_impl(const sk_ui_context_t* ctx) {
	return ctx->pointer_capture;
}

i32 ui_pointer_capture_set_impl(sk_ui_context_t* ctx, sk_ui_node_t node) {
	if (sk_ui_node_is_valid(node) && ui_slot(ctx, node) == NULL) {
		return -1;
	}
	ctx->pointer_capture = node;
	return 0;
}

i32 ui_wants_mouse_impl(const sk_ui_context_t* ctx) {
	return ctx->wants_mouse;
}

i32 ui_wants_keyboard_impl(const sk_ui_context_t* ctx) {
	return ctx->wants_keyboard;
}

/* -------------------------------------------------------------------------- */
/* Input dispatch                                                             */
/* -------------------------------------------------------------------------- */

static void ui_handle_pointer_move(sk_ui_context_t* ctx, f32 x, f32 y, u32 mods) {
	sk_ui_node_t hit;
	sk_ui_event_t ev;
	sk_ui_node_t route;

	ctx->pointer_x = x;
	ctx->pointer_y = y;
	hit = ui_hit_test_impl(ctx, x, y);

	/* Hover tracks geometry even during capture. */
	ui_update_hover(ctx, hit, x, y);

	route = sk_ui_node_is_valid(ctx->pointer_capture) ? ctx->pointer_capture : hit;
	ctx->wants_mouse = sk_ui_node_is_valid(route) || sk_ui_node_is_valid(ctx->pointer_capture) ? 1 : 0;

	if (sk_ui_node_is_valid(route)) {
		ui_event_seed(&ev, SK_UI_EVENT_POINTER_MOVE, route, x, y);
		ev.mods = mods;
		ui_dispatch_event(ctx, &ev);
	}
}

static void ui_handle_pointer_button(sk_ui_context_t* ctx, f32 x, f32 y, i32 button, i32 down, u32 mods) {
	sk_ui_node_t hit;
	sk_ui_event_t ev;
	sk_ui_node_t route;
	u32 bit;

	ctx->pointer_x = x;
	ctx->pointer_y = y;
	hit = ui_hit_test_impl(ctx, x, y);
	ui_update_hover(ctx, hit, x, y);

	bit = button >= 0 && button < 31 ? (1u << (u32)button) : 0u;

	if (down) {
		if (bit != 0u) {
			ctx->pointer_buttons |= bit;
		}
		route = hit;
		if (sk_ui_node_is_valid(route)) {
			ctx->pointer_capture = route;
			ctx->active = route;
			ui_set_state_bit(ctx, route, (u32)SK_UI_STATE_ACTIVE, 1);
			/* Focus focusable targets on primary button down. */
			if (button == SK_UI_POINTER_BUTTON_LEFT && ui_node_can_focus(ctx, route)) {
				(void)ui_focus_set_impl(ctx, route);
			}
			ui_event_seed(&ev, SK_UI_EVENT_POINTER_DOWN, route, x, y);
			ev.button = button;
			ev.down = 1;
			ev.mods = mods;
			ui_dispatch_event(ctx, &ev);
		}
		ctx->wants_mouse = sk_ui_node_is_valid(route) ? 1 : 0;
	} else {
		if (bit != 0u) {
			ctx->pointer_buttons &= ~bit;
		}
		route = sk_ui_node_is_valid(ctx->pointer_capture) ? ctx->pointer_capture : hit;
		if (sk_ui_node_is_valid(route)) {
			ui_event_seed(&ev, SK_UI_EVENT_POINTER_UP, route, x, y);
			ev.button = button;
			ev.down = 0;
			ev.mods = mods;
			ui_dispatch_event(ctx, &ev);

			/* Click if released over the same node that was pressed. */
			if (sk_ui_node_eq(ctx->active, route) && sk_ui_node_eq(hit, route) && button == SK_UI_POINTER_BUTTON_LEFT) {
				ui_event_seed(&ev, SK_UI_EVENT_CLICK, route, x, y);
				ev.button = button;
				ev.mods = mods;
				ui_dispatch_event(ctx, &ev);
			}
		}

		if (sk_ui_node_is_valid(ctx->active)) {
			ui_set_state_bit(ctx, ctx->active, (u32)SK_UI_STATE_ACTIVE, 0);
			ctx->active = SK_UI_NODE_INVALID;
		}
		/* Clear capture when all buttons released. */
		if (ctx->pointer_buttons == 0u) {
			ctx->pointer_capture = SK_UI_NODE_INVALID;
		}
		ctx->wants_mouse = sk_ui_node_is_valid(hit) || sk_ui_node_is_valid(ctx->pointer_capture) ? 1 : 0;
	}
}

static void ui_handle_wheel(sk_ui_context_t* ctx, f32 x, f32 y, f32 sx, f32 sy, u32 mods) {
	sk_ui_node_t hit;
	sk_ui_event_t ev;
	sk_ui_node_t route;

	ctx->pointer_x = x;
	ctx->pointer_y = y;
	hit = ui_hit_test_impl(ctx, x, y);
	ui_update_hover(ctx, hit, x, y);
	route = sk_ui_node_is_valid(ctx->pointer_capture) ? ctx->pointer_capture : hit;
	ctx->wants_mouse = sk_ui_node_is_valid(route) ? 1 : 0;

	if (sk_ui_node_is_valid(route)) {
		ui_event_seed(&ev, SK_UI_EVENT_WHEEL, route, x, y);
		ev.scroll_x = sx;
		ev.scroll_y = sy;
		ev.mods = mods;
		ui_dispatch_event(ctx, &ev);
	}
}

static void ui_handle_key(sk_ui_context_t* ctx, i32 key, i32 down, u32 mods, i32 repeat) {
	sk_ui_event_t ev;
	sk_ui_node_t target = ctx->focus;

	/* Tab traversal before delivering KEY_DOWN to the focused node. */
	if (down && key == SK_UI_KEY_TAB) {
		i32 reverse = (mods & (u32)SK_UI_MOD_SHIFT) != 0u ? 1 : 0;
		(void)ui_focus_advance_impl(ctx, reverse);
		/* Still dispatch KEY_DOWN so listeners can observe/consume. */
		target = ctx->focus;
	}

	if (!sk_ui_node_is_valid(target)) {
		ctx->wants_keyboard = 0;
		return;
	}
	ctx->wants_keyboard = 1;

	ui_event_seed(&ev, down ? SK_UI_EVENT_KEY_DOWN : SK_UI_EVENT_KEY_UP, target, ctx->pointer_x, ctx->pointer_y);
	ev.key = key;
	ev.down = down;
	ev.mods = mods;
	ev.repeat = repeat;
	ui_dispatch_event(ctx, &ev);
}

static void ui_handle_text(sk_ui_context_t* ctx, const_chr_t text) {
	sk_ui_event_t ev;
	if (!sk_ui_node_is_valid(ctx->focus) || text == NULL) {
		return;
	}
	ctx->wants_keyboard = 1;
	ui_event_seed(&ev, SK_UI_EVENT_TEXT_INPUT, ctx->focus, ctx->pointer_x, ctx->pointer_y);
	ev.text = text;
	ui_dispatch_event(ctx, &ev);
}

i32 ui_input_dispatch_impl(sk_ui_context_t* ctx, const sk_ui_input_event_t* event) {
	if (event == NULL) {
		return -1;
	}
	switch (event->kind) {
	case SK_UI_INPUT_POINTER_MOVE:
		ui_handle_pointer_move(ctx, event->x, event->y, event->mods);
		return 0;
	case SK_UI_INPUT_POINTER_BUTTON:
		ui_handle_pointer_button(ctx, event->x, event->y, event->button, event->down, event->mods);
		return 0;
	case SK_UI_INPUT_WHEEL:
		ui_handle_wheel(ctx, event->x, event->y, event->scroll_x, event->scroll_y, event->mods);
		return 0;
	case SK_UI_INPUT_KEY:
		ui_handle_key(ctx, event->key, event->down, event->mods, event->repeat);
		return 0;
	case SK_UI_INPUT_TEXT:
		ui_handle_text(ctx, event->text);
		return 0;
	default:
		return -1;
	}
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

static const sk_ui_api_t* input_test_api(void) {
	return ui_get_api_table();
}

static sk_ui_layout_style_t input_box(f32 w, f32 h) {
	sk_ui_layout_style_t s;
	ui_layout_style_init_default(&s);
	s.width = sk_ui_pt(w);
	s.height = sk_ui_pt(h);
	s.flex_grow = 0.0f;
	s.flex_shrink = 0.0f;
	return s;
}

static sk_ui_input_event_t input_move(f32 x, f32 y) {
	sk_ui_input_event_t e;
	memset(&e, 0, sizeof(e));
	e.kind = SK_UI_INPUT_POINTER_MOVE;
	e.x = x;
	e.y = y;
	return e;
}

static sk_ui_input_event_t input_button(f32 x, f32 y, i32 button, i32 down) {
	sk_ui_input_event_t e;
	memset(&e, 0, sizeof(e));
	e.kind = SK_UI_INPUT_POINTER_BUTTON;
	e.x = x;
	e.y = y;
	e.button = button;
	e.down = down;
	return e;
}

static sk_ui_input_event_t input_key(i32 key, i32 down, u32 mods) {
	sk_ui_input_event_t e;
	memset(&e, 0, sizeof(e));
	e.kind = SK_UI_INPUT_KEY;
	e.key = key;
	e.down = down;
	e.mods = mods;
	return e;
}

typedef struct {
	i32 enter;
	i32 leave;
	i32 move;
	i32 down;
	i32 up;
	i32 click;
	i32 wheel;
	i32 key_down;
	i32 key_up;
	i32 text;
	i32 focus_in;
	i32 focus_out;
	i32 generic;
	sk_ui_event_phase_t last_phase;
	sk_ui_event_type_t last_type;
	sk_ui_node_t last_current;
	sk_ui_node_t last_target;
	i32 consume_on_capture;
	i32 consume_on_target;
	i32 phases[16];
	sk_ui_node_t phase_nodes[16];
	u32 phase_count;
} input_log_t;

static void input_log_clear(input_log_t* l) {
	memset(l, 0, sizeof(*l));
}

static void input_on_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	input_log_t* log = (input_log_t*)user;
	(void)ctx;
	(void)node;
	log->generic += 1;
	log->last_phase = event->phase;
	log->last_type = event->type;
	log->last_current = event->current;
	log->last_target = event->target;
	if (log->phase_count < 16u) {
		log->phases[log->phase_count] = (i32)event->phase;
		log->phase_nodes[log->phase_count] = event->current;
		log->phase_count += 1u;
	}
	if (log->consume_on_capture && event->phase == SK_UI_EVENT_PHASE_CAPTURE) {
		event->consumed = 1;
	}
	if (log->consume_on_target && event->phase == SK_UI_EVENT_PHASE_TARGET) {
		event->consumed = 1;
	}
}

static void input_on_enter(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->enter += 1;
}
static void input_on_leave(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->leave += 1;
}
static void input_on_move(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->move += 1;
}
static void input_on_down(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->down += 1;
}
static void input_on_up(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->up += 1;
}
static void input_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->click += 1;
}
static void input_on_wheel(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->wheel += 1;
}
static void input_on_key_down(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->key_down += 1;
}
static void input_on_key_up(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->key_up += 1;
}
static void input_on_text(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->text += 1;
}
static void input_on_focus_in(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->focus_in += 1;
}
static void input_on_focus_out(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	((input_log_t*)user)->focus_out += 1;
}

static sk_ui_node_callbacks_t input_full_cbs(input_log_t* log) {
	sk_ui_node_callbacks_t c;
	memset(&c, 0, sizeof(c));
	c.on_event = input_on_event;
	c.on_pointer_enter = input_on_enter;
	c.on_pointer_leave = input_on_leave;
	c.on_pointer_move = input_on_move;
	c.on_pointer_down = input_on_down;
	c.on_pointer_up = input_on_up;
	c.on_click = input_on_click;
	c.on_wheel = input_on_wheel;
	c.on_key_down = input_on_key_down;
	c.on_key_up = input_on_key_up;
	c.on_text_input = input_on_text;
	c.on_focus_in = input_on_focus_in;
	c.on_focus_out = input_on_focus_out;
	c.user = log;
	return c;
}

/* ---- hit testing: overlapping siblings (later = on top) ---- */

SK_TEST(ui_input_hit_test_overlap_z_order) {
	const sk_ui_api_t* ui = input_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t under;
	sk_ui_node_t over;
	sk_ui_layout_style_t rs;
	sk_ui_layout_style_t su;
	sk_ui_layout_style_t so;

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	under = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	over = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);

	/* Absolute overlap: under at (0,0) 100x100, over at (50,50) 100x100 (later sibling). */
	su = input_box(100.0f, 100.0f);
	su.position = SK_UI_POSITION_ABSOLUTE;
	su.left = sk_ui_pt(0.0f);
	su.top = sk_ui_pt(0.0f);
	so = input_box(100.0f, 100.0f);
	so.position = SK_UI_POSITION_ABSOLUTE;
	so.left = sk_ui_pt(50.0f);
	so.top = sk_ui_pt(50.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, under, &su));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, over, &so));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 200.0f));

	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->hit_test(ctx, 10.0f, 10.0f), under));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->hit_test(ctx, 60.0f, 60.0f), over)); /* overlap → later sibling */
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->hit_test(ctx, 120.0f, 120.0f), over));
	/* Outside both boxes but still on root content (200x200). */
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->hit_test(ctx, 190.0f, 190.0f), root));
	/* Completely outside the layout root. */
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->hit_test(ctx, 250.0f, 250.0f)));

	ui->context_destroy(ctx);
}

/* ---- hit testing: clipped children ---- */

SK_TEST(ui_input_hit_test_clip_children) {
	const sk_ui_api_t* ui = input_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t clipper;
	sk_ui_node_t child;
	sk_ui_layout_style_t rs;
	sk_ui_layout_style_t sc;
	sk_ui_layout_style_t sk;
	sk_ui_rect_t abs_child;

	ui_layout_style_init_default(&rs);
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	clipper = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	child = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, clipper);

	sc = input_box(50.0f, 50.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, clipper, &sc));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_clip_children(ctx, clipper, 1));

	/* Child larger than clipper and shifted so part protrudes. */
	sk = input_box(80.0f, 80.0f);
	sk.position = SK_UI_POSITION_ABSOLUTE;
	sk.left = sk_ui_pt(20.0f);
	sk.top = sk_ui_pt(20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, child, &sk));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 200.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, child, &abs_child, NULL));
	/* Inside both child and clipper content → hit child */
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->hit_test(ctx, 30.0f, 30.0f), child));
	/* Outside clipper (would be on protruding child) → miss child */
	TEST_ASSERT_FALSE(sk_ui_node_eq(ui->hit_test(ctx, 80.0f, 80.0f), child));

	/* Without clip, protruding region would hit child. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_clip_children(ctx, clipper, 0));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->hit_test(ctx, 80.0f, 80.0f), child));

	ui->context_destroy(ctx);
}

/* ---- propagation + consumption ---- */

SK_TEST(ui_input_propagation_and_consume) {
	const sk_ui_api_t* ui = input_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t mid;
	sk_ui_node_t leaf;
	input_log_t log_root;
	input_log_t log_mid;
	input_log_t log_leaf;
	sk_ui_node_callbacks_t cb;
	sk_ui_layout_style_t rs;
	sk_ui_input_event_t ev;

	input_log_clear(&log_root);
	input_log_clear(&log_mid);
	input_log_clear(&log_leaf);

	ui_layout_style_init_default(&rs);
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	mid = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	leaf = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, mid);
	{
		sk_ui_layout_style_t sm = input_box(100.0f, 100.0f);
		sk_ui_layout_style_t sl = input_box(50.0f, 50.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, mid, &sm));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, leaf, &sl));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 200.0f));

	cb = input_full_cbs(&log_root);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, root, &cb));
	cb = input_full_cbs(&log_mid);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, mid, &cb));
	cb = input_full_cbs(&log_leaf);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, leaf, &cb));

	/* Full path on pointer down at leaf. */
	ev = input_button(10.0f, 10.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(log_leaf.down >= 1);
	TEST_ASSERT_TRUE(log_mid.generic >= 1);
	TEST_ASSERT_TRUE(log_root.generic >= 1);
	/* Capture before target: root and mid should see CAPTURE for down. */
	TEST_ASSERT_TRUE(log_root.phase_count >= 1u);
	TEST_ASSERT_EQUAL_INT((int)SK_UI_EVENT_PHASE_CAPTURE, log_root.phases[0]);

	/* Consume on mid capture → leaf must not receive a subsequent event type on next inject.
	 * Re-bind mid to consume on capture for MOVE. */
	input_log_clear(&log_root);
	input_log_clear(&log_mid);
	input_log_clear(&log_leaf);
	log_mid.consume_on_capture = 1;
	cb = input_full_cbs(&log_mid);
	cb.on_event = input_on_event;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, mid, &cb));

	/* Release first so capture clears, then fresh move. */
	ev = input_button(10.0f, 10.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	input_log_clear(&log_root);
	input_log_clear(&log_mid);
	input_log_clear(&log_leaf);
	log_mid.consume_on_capture = 1;

	ev = input_move(12.0f, 12.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	/* mid consumes during capture → leaf never sees move */
	TEST_ASSERT_EQUAL_INT(0, log_leaf.move);
	TEST_ASSERT_TRUE(log_mid.generic >= 1);

	/* Consume at target: bubble ancestors should not see the event. */
	input_log_clear(&log_root);
	input_log_clear(&log_mid);
	input_log_clear(&log_leaf);
	log_leaf.consume_on_target = 1;
	cb = input_full_cbs(&log_leaf);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, leaf, &cb));
	log_mid.consume_on_capture = 0;
	cb = input_full_cbs(&log_mid);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, mid, &cb));

	ev = input_button(15.0f, 15.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(log_leaf.down >= 1);
	/* Root should have seen CAPTURE but not BUBBLE for POINTER_DOWN if leaf consumed at target.
	 * Count bubble phases on root. */
	{
		u32 bi;
		i32 root_bubble = 0;
		for (bi = 0u; bi < log_root.phase_count; ++bi) {
			if (log_root.phases[bi] == SK_UI_EVENT_PHASE_BUBBLE) {
				root_bubble = 1;
			}
		}
		TEST_ASSERT_EQUAL_INT(0, root_bubble);
	}

	ui->context_destroy(ctx);
}

/* ---- pointer capture during drag ---- */

SK_TEST(ui_input_pointer_capture_drag) {
	const sk_ui_api_t* ui = input_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	input_log_t log_a;
	input_log_t log_b;
	sk_ui_node_callbacks_t cb;
	sk_ui_layout_style_t rs;
	sk_ui_input_event_t ev;

	input_log_clear(&log_a);
	input_log_clear(&log_b);

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	{
		sk_ui_layout_style_t sa = input_box(50.0f, 50.0f);
		sk_ui_layout_style_t sb = input_box(50.0f, 50.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &sa));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &sb));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));

	cb = input_full_cbs(&log_a);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, a, &cb));
	cb = input_full_cbs(&log_b);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, b, &cb));

	/* Down on A */
	ev = input_button(10.0f, 10.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->pointer_capture_get(ctx), a));
	TEST_ASSERT_EQUAL_INT(1, log_a.down);
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, a) & (u32)SK_UI_STATE_ACTIVE) != 0u);

	/* Drag over B — moves still route to A (capture) */
	input_log_clear(&log_a);
	input_log_clear(&log_b);
	ev = input_move(70.0f, 10.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(log_a.move >= 1);
	TEST_ASSERT_EQUAL_INT(0, log_b.move);
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->pointer_capture_get(ctx), a));
	/* Hover can move to B */
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, b) & (u32)SK_UI_STATE_HOVER) != 0u);

	/* Up outside A: still POINTER_UP on A, no click (not over A) */
	input_log_clear(&log_a);
	ev = input_button(70.0f, 10.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(log_a.up >= 1);
	TEST_ASSERT_EQUAL_INT(0, log_a.click);
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->pointer_capture_get(ctx)));
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, a) & (u32)SK_UI_STATE_ACTIVE) == 0u);

	/* Full click on A */
	input_log_clear(&log_a);
	ev = input_button(10.0f, 10.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev = input_button(12.0f, 12.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_EQUAL_INT(1, log_a.click);

	ui->context_destroy(ctx);
}

/* ---- focus traversal ---- */

SK_TEST(ui_input_focus_traversal) {
	const sk_ui_api_t* ui = input_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	input_log_t log_a;
	input_log_t log_b;
	input_log_t log_c;
	sk_ui_node_callbacks_t cb;
	sk_ui_layout_style_t rs;
	sk_ui_input_event_t ev;

	input_log_clear(&log_a);
	input_log_clear(&log_b);
	input_log_clear(&log_c);

	ui_layout_style_init_default(&rs);
	rs.flex_direction = SK_UI_FLEX_ROW;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, root);
	c = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root); /* not focusable by default */
	{
		sk_ui_layout_style_t s = input_box(40.0f, 20.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, a, &s));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, b, &s));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, c, &s));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 40.0f));

	TEST_ASSERT_TRUE(ui->node_get_focusable(ctx, a));
	TEST_ASSERT_FALSE(ui->node_get_focusable(ctx, c));

	cb = input_full_cbs(&log_a);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, a, &cb));
	cb = input_full_cbs(&log_b);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, b, &cb));
	cb = input_full_cbs(&log_c);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, c, &cb));

	TEST_ASSERT_EQUAL_INT(0, ui->focus_set(ctx, a));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), a));
	TEST_ASSERT_EQUAL_INT(1, log_a.focus_in);
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, a) & (u32)SK_UI_STATE_FOCUSED) != 0u);
	TEST_ASSERT_TRUE(ui->wants_keyboard(ctx));

	/* Tab → b */
	ev = input_key(SK_UI_KEY_TAB, 1, 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), b));
	TEST_ASSERT_EQUAL_INT(1, log_a.focus_out);
	TEST_ASSERT_EQUAL_INT(1, log_b.focus_in);
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, a) & (u32)SK_UI_STATE_FOCUSED) == 0u);
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, b) & (u32)SK_UI_STATE_FOCUSED) != 0u);

	/* Tab wraps to a (c is not focusable) */
	ev = input_key(SK_UI_KEY_TAB, 1, 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), a));

	/* Shift+Tab from a → b (wrap reverse) */
	ev = input_key(SK_UI_KEY_TAB, 1, (u32)SK_UI_MOD_SHIFT);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), b));

	/* Text input goes to focused node */
	input_log_clear(&log_b);
	{
		sk_ui_input_event_t te;
		memset(&te, 0, sizeof(te));
		te.kind = SK_UI_INPUT_TEXT;
		te.text = "x";
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &te));
	}
	TEST_ASSERT_EQUAL_INT(1, log_b.text);

	/* Clear focus */
	TEST_ASSERT_EQUAL_INT(0, ui->focus_set(ctx, SK_UI_NODE_INVALID));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->focus_get(ctx)));
	TEST_ASSERT_FALSE(ui->wants_keyboard(ctx));

	ui->context_destroy(ctx);
}

/* ---- hover / active state flags for style variants ---- */

SK_TEST(ui_input_hover_active_style_states) {
	const sk_ui_api_t* ui = input_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;
	sk_ui_style_props_t base;
	sk_ui_style_props_t hover_p;
	sk_ui_style_props_t active_p;
	sk_ui_computed_style_t computed;
	sk_ui_layout_style_t rs;
	sk_ui_input_event_t ev;

	ui_layout_style_init_default(&rs);
	rs.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &rs));

	btn = ui->node_create(ctx, SK_UI_NODE_KIND_BUTTON, root);
	{
		sk_ui_layout_style_t s = input_box(80.0f, 30.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, btn, &s));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));

	memset(&base, 0, sizeof(base));
	base.mask = SK_UI_SP_BACKGROUND_COLOR;
	base.background_color = sk_ui_rgba(0.2f, 0.2f, 0.2f, 1.0f);
	memset(&hover_p, 0, sizeof(hover_p));
	hover_p.mask = SK_UI_SP_BACKGROUND_COLOR;
	hover_p.background_color = sk_ui_rgba(0.0f, 1.0f, 0.0f, 1.0f);
	memset(&active_p, 0, sizeof(active_p));
	active_p.mask = SK_UI_SP_BACKGROUND_COLOR;
	active_p.background_color = sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->style_class_register(ctx, "btn", &base));
	TEST_ASSERT_EQUAL_INT(0, ui->style_class_set_variant(ctx, "btn", SK_UI_STATE_HOVER, &hover_p));
	TEST_ASSERT_EQUAL_INT(0, ui->style_class_set_variant(ctx, "btn", SK_UI_STATE_ACTIVE, &active_p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_add_class(ctx, btn, "btn"));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, btn, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, computed.background_color.r);

	/* Hover */
	ev = input_move(10.0f, 10.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, btn) & (u32)SK_UI_STATE_HOVER) != 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, btn, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computed.background_color.r);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, computed.background_color.g);

	/* Active (down) — active variant wins after hover in cascade order */
	ev = input_button(10.0f, 10.0f, SK_UI_POINTER_BUTTON_LEFT, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, btn) & (u32)SK_UI_STATE_ACTIVE) != 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_computed_style(ctx, btn, &computed));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, computed.background_color.r);

	/* Leave → clear hover after up */
	ev = input_button(10.0f, 10.0f, SK_UI_POINTER_BUTTON_LEFT, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	ev = input_move(150.0f, 150.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, btn) & (u32)SK_UI_STATE_HOVER) == 0u);
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, btn) & (u32)SK_UI_STATE_ACTIVE) == 0u);

	/* Wheel over button */
	{
		sk_ui_input_event_t we;
		input_log_t log;
		sk_ui_node_callbacks_t cbs;
		input_log_clear(&log);
		cbs = input_full_cbs(&log);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, btn, &cbs));
		memset(&we, 0, sizeof(we));
		we.kind = SK_UI_INPUT_WHEEL;
		we.x = 10.0f;
		we.y = 10.0f;
		we.scroll_y = -1.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &we));
		TEST_ASSERT_EQUAL_INT(1, log.wheel);
		TEST_ASSERT_TRUE(ui->wants_mouse(ctx));
	}

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
