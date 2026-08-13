/**
 * @file test_engine.c
 * @brief Code-driven UI test engine: item registry, frame stepping, input.
 *
 * imgui_test_engine-style tests (APX-259/260):
 * - After each controlled frame, register every live id-bearing node with its
 *   abs rect, interaction state, and slash-separated id path.
 * - Lookup by stable test id or id path.
 * - Deterministic yield (N frames) and run-until with a frame budget and a
 *   clear timeout error message.
 * - Synthetic input (mouse, keys, text, drag) via the same input_dispatch path
 *   as hosts so hover → active → click state transitions match production.
 */

#include "ui.internal.h"

#include "allocator.h"
#include "array.h"
#include "hashmap.h"
#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

typedef SK_ARRAY(sk_ui_test_item_t) ui_test_item_array_t;
typedef SK_HASH_MAP(const_chr_t, u32) ui_test_index_map_t;

struct sk_ui_test_engine_t {
	const sk_allocator_t* allocator;
	sk_ui_harness_t* harness;
	ui_test_item_array_t items;
	ui_test_index_map_t by_id;
	ui_test_index_map_t by_path;
	f32 pointer_x; /**< Last synthetic pointer x (logical). */
	f32 pointer_y; /**< Last synthetic pointer y (logical). */
	char last_error[256];
};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static const sk_ui_api_t* te_api(void) {
	return ui_get_api_table();
}

static sk_logger_t* te_logger(void) {
	static sk_logger_t* log = NULL;
	const sk_logger_api_t* api = ui_logger_api();
	sk_logger_context_t* log_ctx = ui_logger_context();
	if (log == NULL && api != NULL && log_ctx != NULL) {
		log = api->create_logger(log_ctx, "ui-test-engine");
	}
	return log;
}

static f32 te_default_dt(f32 delta_seconds) {
	return delta_seconds > 0.0f ? delta_seconds : (1.0f / 60.0f);
}

static char* te_strdup(const sk_allocator_t* a, const_chr_t src) {
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

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void te_set_error(sk_ui_test_engine_t* engine, const_chr_t fmt, ...) {
	va_list args;
	if (engine == NULL) {
		return;
	}
	va_start(args, fmt);
	(void)vsnprintf(engine->last_error, sizeof(engine->last_error), fmt, args);
	va_end(args);
	if (ui_logger_api() != NULL && te_logger() != NULL) {
		sk_log_message(ui_logger_api(), SK_LOGGER_TYPE_ERROR, te_logger(), engine->last_error);
	}
}

static void te_clear_error(sk_ui_test_engine_t* engine) {
	if (engine != NULL) {
		engine->last_error[0] = '\0';
	}
}

static void te_registry_clear(sk_ui_test_engine_t* engine) {
	const sk_allocator_t* a;
	u32 i;
	if (engine == NULL) {
		return;
	}
	a = engine->allocator;
	for (i = 0u; i < engine->items.count; ++i) {
		sk_ui_test_item_t* it = &engine->items.items[i];
		/* id and id_path are owned heap copies (id may alias id_path when equal). */
		if (it->id_path != NULL) {
			a->free(a->instance, SK_CONST_CAST(void*, it->id_path));
			it->id_path = NULL;
		}
		if (it->id != NULL) {
			/* id is either a separate copy or was the same pointer as id_path.
			 * We always allocate id as its own copy for stable map keys. */
			a->free(a->instance, SK_CONST_CAST(void*, it->id));
			it->id = NULL;
		}
	}
	sk_array_clear(&engine->items);
	sk_hash_map_clear(&engine->by_id);
	sk_hash_map_clear(&engine->by_path);
}

/**
 * Build slash-separated id path from root toward @p node using only ancestors
 * (and self) that have non-empty test ids. Returns a heap string or NULL.
 */
static char* te_build_id_path(const sk_ui_context_t* ctx, sk_ui_node_t node, const sk_allocator_t* a) {
	const sk_ui_api_t* ui = te_api();
	const_chr_t segs[64];
	u32 n = 0u;
	sk_ui_node_t cur = node;
	u32 i;
	size_t total = 0u;
	char* out;
	char* w;

	while (sk_ui_node_is_valid(cur) && n < 64u) {
		const_chr_t id = ui->node_get_id(ctx, cur);
		if (id != NULL && id[0] != '\0') {
			segs[n++] = id;
		}
		cur = ui->node_parent(ctx, cur);
	}
	if (n == 0u) {
		return NULL;
	}
	/* segs[0] is nearest-to-node; reverse to root→leaf order. */
	for (i = 0u; i < n; ++i) {
		total += strlen(segs[i]);
	}
	total += (size_t)(n - 1u); /* slashes */
	out = (char*)a->alloc(a->instance, total + 1u);
	if (out == NULL) {
		return NULL;
	}
	w = out;
	for (i = n; i > 0u; --i) {
		const_chr_t s = segs[i - 1u];
		size_t len = strlen(s);
		memcpy(w, s, len);
		w += len;
		if (i > 1u) {
			*w++ = '/';
		}
	}
	*w = '\0';
	return out;
}

typedef struct te_rebuild_walk_t {
	sk_ui_test_engine_t* engine;
	const sk_ui_context_t* ctx;
	i32 failed;
} te_rebuild_walk_t;

static i32 te_rebuild_visit(sk_ui_context_t* ctx, sk_ui_node_t node, u32 depth, void_ptr_t user) {
	te_rebuild_walk_t* w = (te_rebuild_walk_t*)user;
	const sk_ui_api_t* ui = te_api();
	const_chr_t id;
	sk_ui_test_item_t item;
	sk_ui_rect_t border;
	char* id_copy;
	char* path_copy;
	u32 index;
	u32 st;
	(void)depth;

	if (w->failed != 0) {
		return 1;
	}
	id = ui->node_get_id(ctx, node);
	if (id == NULL || id[0] == '\0') {
		return 0;
	}

	memset(&item, 0, sizeof(item));
	item.node = node;
	id_copy = te_strdup(w->engine->allocator, id);
	path_copy = te_build_id_path(w->ctx, node, w->engine->allocator);
	if (id_copy == NULL || path_copy == NULL) {
		if (id_copy != NULL) {
			w->engine->allocator->free(w->engine->allocator->instance, id_copy);
		}
		if (path_copy != NULL) {
			w->engine->allocator->free(w->engine->allocator->instance, path_copy);
		}
		w->failed = 1;
		return 1;
	}
	item.id = id_copy;
	item.id_path = path_copy;

	if (ui->node_get_abs_rect(ctx, node, &border, NULL) == 0) {
		item.rect = border;
	} else {
		memset(&item.rect, 0, sizeof(item.rect));
	}
	st = ui->node_get_state(ctx, node);
	item.state_flags = st;
	item.hovered = (st & (u32)SK_UI_STATE_HOVER) != 0u ? 1 : 0;
	item.active = (st & (u32)SK_UI_STATE_ACTIVE) != 0u ? 1 : 0;
	item.focused = (st & (u32)SK_UI_STATE_FOCUSED) != 0u ? 1 : 0;
	item.disabled = (st & (u32)SK_UI_STATE_DISABLED) != 0u ? 1 : 0;
	item.visible = ui->node_is_visible(ctx, node);

	index = w->engine->items.count;
	if (sk_array_push(&w->engine->items, item) != 0) {
		w->engine->allocator->free(w->engine->allocator->instance, id_copy);
		w->engine->allocator->free(w->engine->allocator->instance, path_copy);
		w->failed = 1;
		return 1;
	}
	/* Map keys point at the stored item's owned strings (stable until clear). */
	if (sk_hash_map_put(&w->engine->by_id, w->engine->items.items[index].id, index) != 0) {
		w->failed = 1;
		return 1;
	}
	if (sk_hash_map_put(&w->engine->by_path, w->engine->items.items[index].id_path, index) != 0) {
		w->failed = 1;
		return 1;
	}
	return 0;
}

static i32 te_registry_rebuild(sk_ui_test_engine_t* engine) {
	const sk_ui_api_t* ui = te_api();
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	te_rebuild_walk_t walk;

	te_registry_clear(engine);
	ctx = ui->harness_context(engine->harness);
	if (ctx == NULL) {
		return -1;
	}
	root = ui->context_root(ctx);
	if (!sk_ui_node_is_valid(root)) {
		return 0;
	}
	memset(&walk, 0, sizeof(walk));
	walk.engine = engine;
	walk.ctx = ctx;
	walk.failed = 0;
	(void)ui->traverse_preorder(ctx, root, te_rebuild_visit, &walk);
	if (walk.failed != 0) {
		te_registry_clear(engine);
		te_set_error(engine, "test_engine: item registry rebuild failed (OOM)");
		return -1;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Public impl                                                                */
/* -------------------------------------------------------------------------- */

sk_ui_test_engine_t* ui_test_engine_create_impl(const sk_ui_test_engine_desc_t* desc) {
	const sk_ui_api_t* ui = te_api();
	const sk_allocator_t* a;
	sk_ui_test_engine_t* engine;
	sk_ui_harness_desc_t hd;
	sk_ui_test_engine_desc_t d;

	if (desc == NULL) {
		memset(&d, 0, sizeof(d));
	} else {
		d = *desc;
	}
	a = d.allocator != NULL ? d.allocator : sk_allocator_default();
	engine = (sk_ui_test_engine_t*)a->alloc(a->instance, sizeof(sk_ui_test_engine_t));
	if (engine == NULL) {
		return NULL;
	}
	memset(engine, 0, sizeof(*engine));
	engine->allocator = a;
	sk_array_init(&engine->items, a);
	if (sk_hash_map_init(&engine->by_id, a, sk_hash_cstr, sk_equals_cstr) != 0) {
		a->free(a->instance, engine);
		return NULL;
	}
	if (sk_hash_map_init(&engine->by_path, a, sk_hash_cstr, sk_equals_cstr) != 0) {
		sk_hash_map_free(&engine->by_id);
		a->free(a->instance, engine);
		return NULL;
	}

	memset(&hd, 0, sizeof(hd));
	hd.allocator = a;
	hd.width = d.width;
	hd.height = d.height;
	hd.content_scale = d.content_scale;
	hd.soft_render = d.soft_render;
	engine->harness = ui->harness_create(&hd);
	if (engine->harness == NULL) {
		sk_hash_map_free(&engine->by_path);
		sk_hash_map_free(&engine->by_id);
		sk_array_free(&engine->items);
		a->free(a->instance, engine);
		return NULL;
	}
	return engine;
}

void ui_test_engine_destroy_impl(sk_ui_test_engine_t* engine) {
	const sk_ui_api_t* ui;
	const sk_allocator_t* a;
	if (engine == NULL) {
		return;
	}
	ui = te_api();
	a = engine->allocator;
	te_registry_clear(engine);
	if (engine->harness != NULL) {
		ui->harness_destroy(engine->harness);
		engine->harness = NULL;
	}
	sk_hash_map_free(&engine->by_path);
	sk_hash_map_free(&engine->by_id);
	sk_array_free(&engine->items);
	a->free(a->instance, engine);
}

sk_ui_context_t* ui_test_engine_context_impl(sk_ui_test_engine_t* engine) {
	if (engine == NULL || engine->harness == NULL) {
		return NULL;
	}
	return te_api()->harness_context(engine->harness);
}

sk_ui_harness_t* ui_test_engine_harness_impl(sk_ui_test_engine_t* engine) {
	return engine != NULL ? engine->harness : NULL;
}

i32 ui_test_engine_step_impl(sk_ui_test_engine_t* engine, f32 delta_seconds) {
	const sk_ui_api_t* ui = te_api();
	if (engine == NULL || engine->harness == NULL) {
		return -1;
	}
	te_clear_error(engine);
	if (ui->harness_step(engine->harness, delta_seconds) != 0) {
		te_set_error(engine, "test_engine_step: harness_step failed");
		return SK_UI_TEST_ERR_STEP;
	}
	if (te_registry_rebuild(engine) != 0) {
		return SK_UI_TEST_ERR_STEP;
	}
	return SK_UI_TEST_OK;
}

i32 ui_test_engine_yield_frames_impl(sk_ui_test_engine_t* engine, u32 frame_count, f32 delta_seconds) {
	u32 i;
	f32 dt = te_default_dt(delta_seconds);
	if (engine == NULL) {
		return -1;
	}
	for (i = 0u; i < frame_count; ++i) {
		if (ui_test_engine_step_impl(engine, dt) != SK_UI_TEST_OK) {
			return SK_UI_TEST_ERR_STEP;
		}
	}
	return SK_UI_TEST_OK;
}

i32 ui_test_engine_run_until_impl(sk_ui_test_engine_t* engine, sk_ui_test_predicate_fn pred, void_ptr_t user, u32 max_frames, f32 delta_seconds) {
	u32 i;
	f32 dt = te_default_dt(delta_seconds);
	if (engine == NULL || pred == NULL) {
		return -1;
	}
	te_clear_error(engine);
	/* Check before stepping so an already-true condition costs zero frames. */
	if (pred(engine, user) != 0) {
		return SK_UI_TEST_OK;
	}
	for (i = 0u; i < max_frames; ++i) {
		if (ui_test_engine_step_impl(engine, dt) != SK_UI_TEST_OK) {
			return SK_UI_TEST_ERR_STEP;
		}
		if (pred(engine, user) != 0) {
			return SK_UI_TEST_OK;
		}
	}
	te_set_error(engine, "test_engine_run_until timed out after %u frames (dt=%.6f, budget_time=%.6fs)", max_frames, (double)dt, (double)dt * (double)max_frames);
	return SK_UI_TEST_ERR_TIMEOUT;
}

f64 ui_test_engine_time_impl(const sk_ui_test_engine_t* engine) {
	if (engine == NULL || engine->harness == NULL) {
		return 0.0;
	}
	return te_api()->harness_time(engine->harness);
}

u32 ui_test_engine_frame_index_impl(const sk_ui_test_engine_t* engine) {
	if (engine == NULL || engine->harness == NULL) {
		return 0u;
	}
	return te_api()->harness_frame_index(engine->harness);
}

const sk_ui_test_item_t* ui_test_engine_find_by_id_impl(const sk_ui_test_engine_t* engine, const_chr_t test_id) {
	u32 index = 0u;
	if (engine == NULL || test_id == NULL || test_id[0] == '\0') {
		return NULL;
	}
	if (sk_hash_map_get(&SK_CONST_CAST(sk_ui_test_engine_t*, engine)->by_id, test_id, &index) != 0) {
		return NULL;
	}
	if (index >= engine->items.count) {
		return NULL;
	}
	return &engine->items.items[index];
}

const sk_ui_test_item_t* ui_test_engine_find_by_path_impl(const sk_ui_test_engine_t* engine, const_chr_t id_path) {
	u32 index = 0u;
	if (engine == NULL || id_path == NULL || id_path[0] == '\0') {
		return NULL;
	}
	if (sk_hash_map_get(&SK_CONST_CAST(sk_ui_test_engine_t*, engine)->by_path, id_path, &index) == 0) {
		if (index < engine->items.count) {
			return &engine->items.items[index];
		}
		return NULL;
	}
	/* Convenience: path without '/' is treated as a bare test id. */
	if (strchr(id_path, '/') == NULL) {
		return ui_test_engine_find_by_id_impl(engine, id_path);
	}
	return NULL;
}

u32 ui_test_engine_item_count_impl(const sk_ui_test_engine_t* engine) {
	return engine != NULL ? engine->items.count : 0u;
}

const sk_ui_test_item_t* ui_test_engine_item_at_impl(const sk_ui_test_engine_t* engine, u32 index) {
	if (engine == NULL || index >= engine->items.count) {
		return NULL;
	}
	return &engine->items.items[index];
}

const_chr_t ui_test_engine_last_error_impl(const sk_ui_test_engine_t* engine) {
	if (engine == NULL) {
		return "";
	}
	return engine->last_error;
}

/* -------------------------------------------------------------------------- */
/* Synthetic input (always via input_dispatch)                                */
/* -------------------------------------------------------------------------- */

static sk_ui_context_t* te_ctx(sk_ui_test_engine_t* engine) {
	if (engine == NULL || engine->harness == NULL) {
		return NULL;
	}
	return te_api()->harness_context(engine->harness);
}

static i32 te_dispatch(sk_ui_test_engine_t* engine, const sk_ui_input_event_t* event) {
	const sk_ui_api_t* ui = te_api();
	sk_ui_context_t* ctx = te_ctx(engine);
	if (ctx == NULL || event == NULL) {
		te_set_error(engine, "test_engine_input: engine or event is invalid");
		return SK_UI_TEST_ERR_INPUT;
	}
	if (ui->input_dispatch(ctx, event) != 0) {
		te_set_error(engine, "test_engine_input: input_dispatch failed");
		return SK_UI_TEST_ERR_INPUT;
	}
	if (event->kind == SK_UI_INPUT_POINTER_MOVE || event->kind == SK_UI_INPUT_POINTER_BUTTON || event->kind == SK_UI_INPUT_WHEEL) {
		engine->pointer_x = event->x;
		engine->pointer_y = event->y;
	}
	return SK_UI_TEST_OK;
}

static i32 te_resolve_node(sk_ui_test_engine_t* engine, const_chr_t test_id, sk_ui_node_t* out_node) {
	const sk_ui_api_t* ui = te_api();
	sk_ui_context_t* ctx = te_ctx(engine);
	sk_ui_node_t node;
	if (ctx == NULL || test_id == NULL || test_id[0] == '\0' || out_node == NULL) {
		te_set_error(engine, "test_engine: invalid resolve (null engine/id)");
		return SK_UI_TEST_ERR_NOT_FOUND;
	}
	node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, test_id);
	if (!sk_ui_node_is_valid(node) || !ui->node_alive(ctx, node)) {
		te_set_error(engine, "test_engine: item not found: %s", test_id);
		return SK_UI_TEST_ERR_NOT_FOUND;
	}
	*out_node = node;
	return SK_UI_TEST_OK;
}

static i32 te_node_center(sk_ui_test_engine_t* engine, sk_ui_node_t node, f32* out_x, f32* out_y) {
	const sk_ui_api_t* ui = te_api();
	sk_ui_context_t* ctx = te_ctx(engine);
	sk_ui_rect_t border;
	if (ctx == NULL || out_x == NULL || out_y == NULL) {
		return SK_UI_TEST_ERR_INPUT;
	}
	if (ui->node_get_abs_rect(ctx, node, &border, NULL) != 0) {
		te_set_error(engine, "test_engine: node has no layout rect (step first)");
		return SK_UI_TEST_ERR_INPUT;
	}
	if (border.width <= 0.0f || border.height <= 0.0f) {
		te_set_error(engine, "test_engine: node has empty layout rect");
		return SK_UI_TEST_ERR_INPUT;
	}
	*out_x = border.x + border.width * 0.5f;
	*out_y = border.y + border.height * 0.5f;
	return SK_UI_TEST_OK;
}

i32 ui_test_engine_input_impl(sk_ui_test_engine_t* engine, const sk_ui_input_event_t* event) {
	te_clear_error(engine);
	return te_dispatch(engine, event);
}

i32 ui_test_engine_mouse_move_impl(sk_ui_test_engine_t* engine, f32 x, f32 y) {
	sk_ui_input_event_t ev;
	te_clear_error(engine);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = x;
	ev.y = y;
	return te_dispatch(engine, &ev);
}

i32 ui_test_engine_mouse_button_impl(sk_ui_test_engine_t* engine, i32 button, i32 down, u32 mods) {
	sk_ui_input_event_t ev;
	te_clear_error(engine);
	if (engine == NULL) {
		return SK_UI_TEST_ERR_INPUT;
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = engine->pointer_x;
	ev.y = engine->pointer_y;
	ev.button = button;
	ev.down = down != 0 ? 1 : 0;
	ev.mods = mods;
	return te_dispatch(engine, &ev);
}

i32 ui_test_engine_scroll_wheel_impl(sk_ui_test_engine_t* engine, f32 scroll_x, f32 scroll_y, u32 mods) {
	sk_ui_input_event_t ev;
	te_clear_error(engine);
	if (engine == NULL) {
		return SK_UI_TEST_ERR_INPUT;
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_WHEEL;
	ev.x = engine->pointer_x;
	ev.y = engine->pointer_y;
	ev.scroll_x = scroll_x;
	ev.scroll_y = scroll_y;
	ev.mods = mods;
	return te_dispatch(engine, &ev);
}

i32 ui_test_engine_key_impl(sk_ui_test_engine_t* engine, i32 key, i32 down, u32 mods) {
	sk_ui_input_event_t ev;
	te_clear_error(engine);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_KEY;
	ev.key = key;
	ev.down = down != 0 ? 1 : 0;
	ev.mods = mods;
	return te_dispatch(engine, &ev);
}

i32 ui_test_engine_text_impl(sk_ui_test_engine_t* engine, const_chr_t text) {
	sk_ui_input_event_t ev;
	te_clear_error(engine);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_TEXT;
	ev.text = text != NULL ? text : "";
	return te_dispatch(engine, &ev);
}

i32 ui_test_engine_hover_impl(sk_ui_test_engine_t* engine, const_chr_t test_id) {
	sk_ui_node_t node;
	f32 x, y;
	i32 rc;
	te_clear_error(engine);
	rc = te_resolve_node(engine, test_id, &node);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	rc = te_node_center(engine, node, &x, &y);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui_test_engine_mouse_move_impl(engine, x, y);
}

i32 ui_test_engine_click_ex_impl(sk_ui_test_engine_t* engine, const_chr_t test_id, i32 button, u32 mods) {
	sk_ui_node_t node;
	f32 x, y;
	i32 rc;
	te_clear_error(engine);
	rc = te_resolve_node(engine, test_id, &node);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	rc = te_node_center(engine, node, &x, &y);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	rc = ui_test_engine_mouse_move_impl(engine, x, y);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	rc = ui_test_engine_mouse_button_impl(engine, button, 1, mods);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui_test_engine_mouse_button_impl(engine, button, 0, mods);
}

i32 ui_test_engine_click_impl(sk_ui_test_engine_t* engine, const_chr_t test_id) {
	return ui_test_engine_click_ex_impl(engine, test_id, SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_NONE);
}

i32 ui_test_engine_double_click_impl(sk_ui_test_engine_t* engine, const_chr_t test_id) {
	i32 rc;
	te_clear_error(engine);
	rc = ui_test_engine_click_impl(engine, test_id);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui_test_engine_click_impl(engine, test_id);
}

i32 ui_test_engine_press_impl(sk_ui_test_engine_t* engine, const_chr_t test_id, i32 button, u32 mods) {
	sk_ui_node_t node;
	f32 x, y;
	i32 rc;
	te_clear_error(engine);
	rc = te_resolve_node(engine, test_id, &node);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	rc = te_node_center(engine, node, &x, &y);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	rc = ui_test_engine_mouse_move_impl(engine, x, y);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui_test_engine_mouse_button_impl(engine, button, 1, mods);
}

i32 ui_test_engine_release_impl(sk_ui_test_engine_t* engine, i32 button, u32 mods) {
	return ui_test_engine_mouse_button_impl(engine, button, 0, mods);
}

i32 ui_test_engine_drag_impl(sk_ui_test_engine_t* engine, f32 x0, f32 y0, f32 x1, f32 y1, u32 motion_frames, f32 delta_seconds) {
	i32 rc;
	u32 i;
	u32 frames;
	te_clear_error(engine);

	rc = ui_test_engine_mouse_move_impl(engine, x0, y0);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	rc = ui_test_engine_mouse_button_impl(engine, SK_UI_POINTER_BUTTON_LEFT, 1, SK_UI_MOD_NONE);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}

	if (motion_frames == 0u) {
		rc = ui_test_engine_mouse_move_impl(engine, x1, y1);
		if (rc != SK_UI_TEST_OK) {
			return rc;
		}
	} else {
		frames = motion_frames;
		for (i = 1u; i <= frames; ++i) {
			f32 t = (f32)i / (f32)frames;
			f32 x = x0 + (x1 - x0) * t;
			f32 y = y0 + (y1 - y0) * t;
			rc = ui_test_engine_mouse_move_impl(engine, x, y);
			if (rc != SK_UI_TEST_OK) {
				return rc;
			}
			if (delta_seconds > 0.0f) {
				if (ui_test_engine_step_impl(engine, delta_seconds) != SK_UI_TEST_OK) {
					return SK_UI_TEST_ERR_STEP;
				}
			}
		}
	}

	return ui_test_engine_mouse_button_impl(engine, SK_UI_POINTER_BUTTON_LEFT, 0, SK_UI_MOD_NONE);
}

i32 ui_test_engine_type_impl(sk_ui_test_engine_t* engine, const_chr_t test_id, const_chr_t text) {
	const sk_ui_api_t* ui = te_api();
	sk_ui_context_t* ctx;
	sk_ui_node_t node;
	i32 rc;
	te_clear_error(engine);
	rc = te_resolve_node(engine, test_id, &node);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	ctx = te_ctx(engine);
	if (ui->focus_set(ctx, node) != 0) {
		te_set_error(engine, "test_engine_type: focus_set failed for %s", test_id);
		return SK_UI_TEST_ERR_INPUT;
	}
	return ui_test_engine_text_impl(engine, text);
}

i32 ui_test_engine_scroll_impl(sk_ui_test_engine_t* engine, const_chr_t test_id, f32 scroll_x, f32 scroll_y) {
	sk_ui_node_t node;
	f32 x, y;
	i32 rc;
	te_clear_error(engine);
	rc = te_resolve_node(engine, test_id, &node);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	rc = te_node_center(engine, node, &x, &y);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	rc = ui_test_engine_mouse_move_impl(engine, x, y);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui_test_engine_scroll_wheel_impl(engine, scroll_x, scroll_y, SK_UI_MOD_NONE);
}

i32 ui_test_engine_focus_impl(sk_ui_test_engine_t* engine, const_chr_t test_id) {
	const sk_ui_api_t* ui = te_api();
	sk_ui_context_t* ctx;
	sk_ui_node_t node;
	i32 rc;
	te_clear_error(engine);
	rc = te_resolve_node(engine, test_id, &node);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	ctx = te_ctx(engine);
	if (ui->focus_set(ctx, node) != 0) {
		te_set_error(engine, "test_engine_focus: focus_set failed for %s", test_id);
		return SK_UI_TEST_ERR_INPUT;
	}
	return SK_UI_TEST_OK;
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"

static const sk_ui_api_t* te_test_api(void) {
	return ui_get_api_table();
}

static void te_test_set_size(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, f32 w, f32 h) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &p));
}

typedef struct te_wait_ctx_t {
	const_chr_t path;
	u32 min_frame;
} te_wait_ctx_t;

static i32 te_pred_item_visible(sk_ui_test_engine_t* engine, void_ptr_t user) {
	const te_wait_ctx_t* w = (const te_wait_ctx_t*)user;
	const sk_ui_test_item_t* item;
	const sk_ui_api_t* ui = te_test_api();
	if (ui->test_engine_frame_index(engine) < w->min_frame) {
		return 0;
	}
	item = ui->test_engine_find_by_path(engine, w->path);
	return (item != NULL && item->visible != 0) ? 1 : 0;
}

static i32 te_pred_never(sk_ui_test_engine_t* engine, void_ptr_t user) {
	(void)engine;
	(void)user;
	return 0;
}

/**
 * Build a small UI, step frames, find a named button by id path, and read
 * back its rect and hovered/active flags from the item registry.
 */
SK_TEST(ui_te_item_registry_find_button_by_path_rect_state) {
	const sk_ui_api_t* ui = te_test_api();
	sk_ui_test_engine_desc_t desc;
	sk_ui_test_engine_t* engine;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t panel;
	sk_ui_node_t btn;
	const sk_ui_test_item_t* item;
	u32 st;

	memset(&desc, 0, sizeof(desc));
	desc.width = 320.0f;
	desc.height = 200.0f;
	desc.content_scale = 1.0f;
	desc.soft_render = 0;

	engine = ui->test_engine_create(&desc);
	TEST_ASSERT_NOT_NULL(engine);
	ctx = ui->test_engine_context(engine);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_NOT_NULL(ui->test_engine_harness(engine));
	TEST_ASSERT_EQUAL_UINT(0u, ui->test_engine_frame_index(engine));
	TEST_ASSERT_EQUAL_UINT(0u, ui->test_engine_item_count(engine));

	root = ui->context_root(ctx);
	panel = ui->widget_panel(ctx, root, "panel-main");
	btn = ui->widget_button(ctx, panel, "Go", "btn-go");
	te_test_set_size(ui, ctx, btn, 96.0f, 32.0f);

	/* No input injection yet: set interaction flags so the registry reports them. */
	st = (u32)SK_UI_STATE_HOVER | (u32)SK_UI_STATE_ACTIVE;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, btn, st));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_step(engine, 1.0f / 60.0f));
	TEST_ASSERT_EQUAL_UINT(1u, ui->test_engine_frame_index(engine));
	TEST_ASSERT_TRUE(ui->test_engine_item_count(engine) >= 2u);

	item = ui->test_engine_find_by_path(engine, "panel-main/btn-go");
	TEST_ASSERT_NOT_NULL(item);
	TEST_ASSERT_TRUE(sk_ui_node_eq(item->node, btn));
	TEST_ASSERT_EQUAL_STRING("btn-go", item->id);
	TEST_ASSERT_EQUAL_STRING("panel-main/btn-go", item->id_path);
	TEST_ASSERT_TRUE(item->rect.width > 0.0f);
	TEST_ASSERT_TRUE(item->rect.height > 0.0f);
	TEST_ASSERT_TRUE(item->hovered != 0);
	TEST_ASSERT_TRUE(item->active != 0);
	TEST_ASSERT_TRUE((item->state_flags & (u32)SK_UI_STATE_HOVER) != 0u);
	TEST_ASSERT_TRUE((item->state_flags & (u32)SK_UI_STATE_ACTIVE) != 0u);
	TEST_ASSERT_TRUE(item->visible != 0);

	/* Bare id lookup and path-without-slash convenience. */
	TEST_ASSERT_NOT_NULL(ui->test_engine_find_by_id(engine, "btn-go"));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->test_engine_find_by_id(engine, "btn-go")->node, btn));
	TEST_ASSERT_NOT_NULL(ui->test_engine_find_by_path(engine, "btn-go"));
	TEST_ASSERT_NULL(ui->test_engine_find_by_path(engine, "missing/path"));
	TEST_ASSERT_NULL(ui->test_engine_find_by_id(engine, "no-such-id"));

	ui->test_engine_destroy(engine);
}

SK_TEST(ui_te_yield_frames_and_run_until_timeout) {
	const sk_ui_api_t* ui = te_test_api();
	sk_ui_test_engine_desc_t desc;
	sk_ui_test_engine_t* engine;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t btn;
	te_wait_ctx_t wait;
	const_chr_t err;
	f64 t0;
	f64 t1;

	memset(&desc, 0, sizeof(desc));
	desc.width = 200.0f;
	desc.height = 100.0f;
	engine = ui->test_engine_create(&desc);
	TEST_ASSERT_NOT_NULL(engine);
	ctx = ui->test_engine_context(engine);
	root = ui->context_root(ctx);
	btn = ui->widget_button(ctx, root, "Wait", "btn-wait");
	te_test_set_size(ui, ctx, btn, 80.0f, 24.0f);

	/* yield 3 frames advances the stable clock and frame index. */
	t0 = ui->test_engine_time(engine);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_yield_frames(engine, 3u, 1.0f / 60.0f));
	TEST_ASSERT_EQUAL_UINT(3u, ui->test_engine_frame_index(engine));
	t1 = ui->test_engine_time(engine);
	TEST_ASSERT_FLOAT_WITHIN(1e-5, 3.0f / 60.0f, (f32)(t1 - t0));
	TEST_ASSERT_NOT_NULL(ui->test_engine_find_by_id(engine, "btn-wait"));

	/* run_until succeeds once the button is visible after a step. */
	wait.path = "btn-wait";
	wait.min_frame = 0u;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_run_until(engine, te_pred_item_visible, &wait, 4u, 1.0f / 60.0f));

	/* Predicate that never holds → clear timeout error after budget. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_ERR_TIMEOUT, ui->test_engine_run_until(engine, te_pred_never, NULL, 2u, 0.05f));
	err = ui->test_engine_last_error(engine);
	TEST_ASSERT_NOT_NULL(err);
	TEST_ASSERT_TRUE(strstr(err, "timed out") != NULL);
	TEST_ASSERT_TRUE(strstr(err, "2 frames") != NULL);

	ui->test_engine_destroy(engine);
}

SK_TEST(ui_te_run_until_waits_for_min_frame) {
	const sk_ui_api_t* ui = te_test_api();
	sk_ui_test_engine_desc_t desc;
	sk_ui_test_engine_t* engine;
	sk_ui_context_t* ctx;
	te_wait_ctx_t wait;
	u32 start_frame;
	u32 end_frame;

	memset(&desc, 0, sizeof(desc));
	desc.width = 160.0f;
	desc.height = 80.0f;
	engine = ui->test_engine_create(&desc);
	TEST_ASSERT_NOT_NULL(engine);
	ctx = ui->test_engine_context(engine);
	(void)ui->widget_button(ctx, ui->context_root(ctx), "X", "btn-x");

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_step(engine, 1.0f / 60.0f));
	start_frame = ui->test_engine_frame_index(engine);
	TEST_ASSERT_EQUAL_UINT(1u, start_frame);

	wait.path = "btn-x";
	wait.min_frame = start_frame + 3u; /* require three more steps */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_run_until(engine, te_pred_item_visible, &wait, 8u, 1.0f / 60.0f));
	end_frame = ui->test_engine_frame_index(engine);
	TEST_ASSERT_TRUE(end_frame >= wait.min_frame);

	ui->test_engine_destroy(engine);
}

/* ---- APX-260: synthetic input verification -------------------------------- */

static i32 g_te_click_count;
static i32 g_te_click_button;
static i32 g_te_right_down_count;
static i32 g_te_dbl_click_count;

static void te_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)user;
	g_te_click_count += 1;
	g_te_click_button = event->button;
}

static void te_on_pointer_down(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)user;
	if (event->button == SK_UI_POINTER_BUTTON_RIGHT) {
		g_te_right_down_count += 1;
	}
}

static void te_on_dbl_click_counter(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)user;
	(void)event;
	g_te_dbl_click_count += 1;
}

/**
 * Multi-frame pointer path: hover → step → press → step → release → click.
 * Callback fires; registry reports hover then active across frames.
 */
SK_TEST(ui_te_click_button_callback_and_hover_active_frames) {
	const sk_ui_api_t* ui = te_test_api();
	sk_ui_test_engine_desc_t desc;
	sk_ui_test_engine_t* engine;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t btn;
	sk_ui_node_callbacks_t cbs;
	const sk_ui_test_item_t* item;
	u32 st;

	memset(&desc, 0, sizeof(desc));
	desc.width = 240.0f;
	desc.height = 120.0f;
	engine = ui->test_engine_create(&desc);
	TEST_ASSERT_NOT_NULL(engine);
	ctx = ui->test_engine_context(engine);
	root = ui->context_root(ctx);
	btn = ui->widget_button(ctx, root, "Fire", "btn-fire");
	te_test_set_size(ui, ctx, btn, 100.0f, 32.0f);

	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = te_on_click;
	cbs.on_pointer_down = te_on_pointer_down;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, btn, &cbs));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_step(engine, 1.0f / 60.0f));

	/* Hover only — HOVER set, not ACTIVE yet. */
	g_te_click_count = 0;
	g_te_right_down_count = 0;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_hover(engine, "btn-fire"));
	st = ui->node_get_state(ctx, btn);
	TEST_ASSERT_TRUE((st & (u32)SK_UI_STATE_HOVER) != 0u);
	TEST_ASSERT_TRUE((st & (u32)SK_UI_STATE_ACTIVE) == 0u);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_step(engine, 1.0f / 60.0f));
	item = ui->test_engine_find_by_id(engine, "btn-fire");
	TEST_ASSERT_NOT_NULL(item);
	TEST_ASSERT_TRUE(item->hovered != 0);
	TEST_ASSERT_TRUE(item->active == 0);

	/* Press — ACTIVE set. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_press(engine, "btn-fire", SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_NONE));
	st = ui->node_get_state(ctx, btn);
	TEST_ASSERT_TRUE((st & (u32)SK_UI_STATE_ACTIVE) != 0u);
	TEST_ASSERT_EQUAL_INT(0, g_te_click_count);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_step(engine, 1.0f / 60.0f));
	item = ui->test_engine_find_by_id(engine, "btn-fire");
	TEST_ASSERT_NOT_NULL(item);
	TEST_ASSERT_TRUE(item->active != 0);

	/* Release — CLICK fires, ACTIVE clears. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_release(engine, SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(1, g_te_click_count);
	TEST_ASSERT_EQUAL_INT(SK_UI_POINTER_BUTTON_LEFT, g_te_click_button);
	st = ui->node_get_state(ctx, btn);
	TEST_ASSERT_TRUE((st & (u32)SK_UI_STATE_ACTIVE) == 0u);

	/* Convenience one-shot click also fires the callback. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_click(engine, "btn-fire"));
	TEST_ASSERT_EQUAL_INT(2, g_te_click_count);

	/* Right-button press/release: POINTER_DOWN, no SK_UI_EVENT_CLICK. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_click_ex(engine, "btn-fire", SK_UI_POINTER_BUTTON_RIGHT, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(1, g_te_right_down_count);
	TEST_ASSERT_EQUAL_INT(2, g_te_click_count); /* still left-only clicks */

	/* Middle button path (down/up via click_ex). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_click_ex(engine, "btn-fire", SK_UI_POINTER_BUTTON_MIDDLE, SK_UI_MOD_CTRL));

	/* Double-click: two left clicks. */
	g_te_dbl_click_count = 0;
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = te_on_dbl_click_counter;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, btn, &cbs));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_double_click(engine, "btn-fire"));
	TEST_ASSERT_EQUAL_INT(2, g_te_dbl_click_count);

	/* Missing id → NOT_FOUND. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_ERR_NOT_FOUND, ui->test_engine_click(engine, "no-such-btn"));
	TEST_ASSERT_TRUE(strstr(ui->test_engine_last_error(engine), "not found") != NULL);

	ui->test_engine_destroy(engine);
}

/**
 * Drag a slider with intermediate motion frames to a target value.
 */
SK_TEST(ui_te_drag_slider_to_target_value) {
	const sk_ui_api_t* ui = te_test_api();
	sk_ui_test_engine_desc_t desc;
	sk_ui_test_engine_t* engine;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t sl;
	sk_ui_rect_t border;
	f32 x0, y0, x1, y1;
	f32 value;

	memset(&desc, 0, sizeof(desc));
	desc.width = 320.0f;
	desc.height = 80.0f;
	engine = ui->test_engine_create(&desc);
	TEST_ASSERT_NOT_NULL(engine);
	ctx = ui->test_engine_context(engine);
	root = ui->context_root(ctx);
	sl = ui->widget_slider(ctx, root, 0.0f, 100.0f, 0.0f, "sl-main");
	te_test_set_size(ui, ctx, sl, 200.0f, 24.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_step(engine, 1.0f / 60.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sl, &border, NULL));
	TEST_ASSERT_TRUE(border.width > 0.0f);

	/* Start near left (value ~0), drag to 75% of track with motion frames. */
	x0 = border.x + 2.0f;
	y0 = border.y + border.height * 0.5f;
	x1 = border.x + border.width * 0.75f;
	y1 = y0;

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(engine, x0, y0, x1, y1, 6u, 1.0f / 60.0f));
	value = ui->slider_get_value(ctx, sl);
	TEST_ASSERT_FLOAT_WITHIN(3.0f, 75.0f, value);

	/* Zero motion frames: jump-drag still updates value via final move. */
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_value(ctx, sl, 10.0f));
	x1 = border.x + border.width * 0.5f;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(engine, x0, y0, x1, y1, 0u, 0.0f));
	value = ui->slider_get_value(ctx, sl);
	TEST_ASSERT_FLOAT_WITHIN(3.0f, 50.0f, value);

	ui->test_engine_destroy(engine);
}

/**
 * Type into a text field; buffer matches. Also key press with modifiers.
 */
SK_TEST(ui_te_type_text_field_buffer_and_keys) {
	const sk_ui_api_t* ui = te_test_api();
	sk_ui_test_engine_desc_t desc;
	sk_ui_test_engine_t* engine;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t ti;
	sk_ui_node_t sv;
	f32 sx, sy;

	memset(&desc, 0, sizeof(desc));
	desc.width = 400.0f;
	desc.height = 200.0f;
	engine = ui->test_engine_create(&desc);
	TEST_ASSERT_NOT_NULL(engine);
	ctx = ui->test_engine_context(engine);
	root = ui->context_root(ctx);

	ti = ui->widget_text_input(ctx, root, "", "ti-name");
	te_test_set_size(ui, ctx, ti, 180.0f, 28.0f);

	sv = ui->widget_scroll_view(ctx, root, "sv-panel");
	te_test_set_size(ui, ctx, sv, 100.0f, 60.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, sv, 100.0f, 240.0f));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_step(engine, 1.0f / 60.0f));

	/* Focus + type via id-keyed helper. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_type(engine, "ti-name", "Hello"));
	TEST_ASSERT_EQUAL_STRING("Hello", ui->text_input_get_text(ctx, ti));

	/* Append via raw text after focus. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_text(engine, " World"));
	TEST_ASSERT_EQUAL_STRING("Hello World", ui->text_input_get_text(ctx, ti));

	/* Backspace via key press/release with no modifiers. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(engine, SK_UI_KEY_BACKSPACE, 1, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(engine, SK_UI_KEY_BACKSPACE, 0, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_STRING("Hello Worl", ui->text_input_get_text(ctx, ti));

	/* Key with modifiers (SHIFT+TAB focus advance path — must not crash). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(engine, SK_UI_KEY_TAB, 1, SK_UI_MOD_SHIFT));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(engine, SK_UI_KEY_TAB, 0, SK_UI_MOD_SHIFT));

	/* Scroll wheel over scroll_view id. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_scroll(engine, "sv-panel", 0.0f, -2.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_TRUE(sy > 0.0f);

	ui->test_engine_destroy(engine);
}

#endif /* SK_TESTS */
