/**
 * @file clay.c
 * @brief Clay (vendored immediate-mode layout) engine bridge + smoke path.
 *
 * Bridges thirdparty/clay v0.14 into the engine without touching the existing
 * retained-mode layout code or any UI call sites (APX-213):
 *
 *  - the Clay arena is allocated through the engine allocator (sk_allocator_t)
 *  - Clay_Initialize is fed the current viewport dimensions
 *  - Clay_SetMeasureTextFunction is backed by engine text layout
 *    (MSDF atlas em metrics × size when that renderer is on, else FreeType)
 *  - Clay errors route to the engine logger (sk_logger_api_t)
 *
 * The smoke test declares a parent container with two children
 * (CLAY_SIZING_FIXED / CLAY_SIZING_GROW) and asserts the render command
 * array count and bounding boxes.
 */

#include "ui_internal.h"

#include "clay.h"

#include "logger.h"

/* -------------------------------------------------------------------------- */
/* State                                                                      */
/* -------------------------------------------------------------------------- */

/** Headroom above Clay_MinMemorySize() so panel trees + text never hit capacity. */
#define UI_CLAY_ARENA_HEADROOM (32u * 1024u * 1024u)

/** Element budget for retained trees (menus, console, sample panels). */
#define UI_CLAY_MAX_ELEMENTS 16384

/** Fallback text height when no engine font is bound (matches 16 px UI default). */
#define UI_CLAY_FALLBACK_FONT_SIZE 16.0f

/** Fallback per-glyph advance when the engine cannot rasterize a glyph. */
#define UI_CLAY_FALLBACK_GLYPH_ADVANCE 8.0f

typedef struct ui_clay_state_t {
	const sk_allocator_t* allocator; /**< Engine allocator used for the arena. */
	void_ptr_t arena_memory;		 /**< Arena block; owned by this module. */
	size_t arena_capacity;			 /**< Allocated arena capacity in bytes. */
	i32 initialized;				 /**< Non-zero after a successful init. */
	f32 viewport_width;				 /**< Layout dimensions passed to Clay. */
	f32 viewport_height;
	sk_ui_font_system_t* font_system; /**< Non-owning; glyph advances for measure. */
	sk_ui_font_t* font;				  /**< Non-owning; metrics for measure. */
	sk_logger_t* logger;			  /**< "clay" logger; NULL until first init. */
} ui_clay_state_t;

static ui_clay_state_t ui_clay_state;

/* -------------------------------------------------------------------------- */
/* Error handler → engine log                                                 */
/* -------------------------------------------------------------------------- */

static void ui_clay_error_handler(Clay_ErrorData error) {
	char buf[160];
	size_t len;
	sk_logger_t* logger;

	/* Clay_String is not NUL-terminated; copy into a bounded buffer. */
	len = (size_t)error.errorText.length;
	if (error.errorText.chars == NULL || len == 0u) {
		buf[0] = '\0';
	} else {
		if (len >= sizeof(buf)) {
			len = sizeof(buf) - 1u;
		}
		memcpy(buf, error.errorText.chars, len);
		buf[len] = '\0';
	}

	logger = ui_clay_state.logger;
	if (logger != NULL) {
		sk_log_message(sk_logger_api(), SK_LOGGER_TYPE_ERROR, logger, "clay layout error (type %d): %s", (int)error.errorType, buf);
	}
}

/* -------------------------------------------------------------------------- */
/* Measure text → engine font metrics                                         */
/* -------------------------------------------------------------------------- */

/**
 * Clay measure callback. Width is the sum of engine glyph advances at the
 * requested pixel size (MSDF atlas em metrics × size when that renderer is
 * on, else FreeType); height is the engine font line height. Missing glyphs
 * use a defined .notdef advance instead of a space substitute.
 */
// NOLINTNEXTLINE(readability-non-const-parameter)
static Clay_Dimensions ui_clay_measure_text(Clay_StringSlice text, Clay_TextElementConfig* config, void* user_data) {
	const ui_clay_state_t* state = (const ui_clay_state_t*)user_data;
	Clay_Dimensions out;
	sk_ui_font_metrics_t metrics;
	size_t index;
	f32 width;
	f32 height;
	f32 px;
	i32 use_msdf;
	u32 prev;

	/* Clay text configs use logical/physical pixel sizes, like the engine. */
	px = (config != NULL && config->fontSize > 0u) ? (f32)config->fontSize : 16.0f;
	use_msdf = ui_get_text_renderer_impl() == SK_UI_TEXT_RENDERER_MSDF ? 1 : 0;

	width = 0.0f;
	height = UI_CLAY_FALLBACK_FONT_SIZE;
	if (state->font != NULL && state->font_system != NULL) {
		if (ui_text_layout_metrics(state->font, px, use_msdf, &metrics) == 0) {
			height = metrics.line_height;
		}
		index = 0u;
		prev = 0u;
		while (index < (size_t)text.length) {
			u32 cp = 0u;
			ui_text_layout_glyph_t glyph;
			if (!ui_text_utf8_next((const u8*)text.chars, (size_t)text.length, &index, &cp)) {
				prev = 0u;
				continue;
			}
			if (ui_text_layout_shape(state->font_system, state->font, px, prev, cp, use_msdf, &glyph) == 0) {
				width += glyph.advance_x;
				prev = glyph.is_fallback != 0 ? 0u : cp;
			} else {
				width += UI_CLAY_FALLBACK_GLYPH_ADVANCE;
				prev = 0u;
			}
		}
	} else {
		/* No engine font bound: rough monospace estimate so layouts still work. */
		width = (f32)text.length * UI_CLAY_FALLBACK_GLYPH_ADVANCE;
	}
	out.width = width;
	out.height = height;
	return out;
}

/* -------------------------------------------------------------------------- */
/* Init / shutdown                                                            */
/* -------------------------------------------------------------------------- */

i32 ui_clay_init(const sk_allocator_t* allocator, f32 viewport_width, f32 viewport_height, sk_ui_font_system_t* font_system, sk_ui_font_t* font) {
	Clay_Arena arena;
	Clay_Dimensions dims;
	Clay_ErrorHandler handler;
	size_t min_size;
	size_t capacity;
	void_ptr_t memory;

	if (ui_clay_state.initialized) {
		return -1; /* already initialized; ui_clay_shutdown first */
	}
	if (allocator == NULL) {
		allocator = sk_allocator_default();
	}

	/* Raise element budget before MinMemorySize so the arena matches it. */
	Clay_SetMaxElementCount(UI_CLAY_MAX_ELEMENTS);

	/* Arena comes from the engine allocator, sized for Clay's minimum plus
	 * headroom so panel trees + text measurement never trip capacity errors. */
	min_size = (size_t)Clay_MinMemorySize();
	capacity = min_size + UI_CLAY_ARENA_HEADROOM;
	memory = allocator->alloc(allocator->instance, capacity);
	if (memory == NULL) {
		return -1;
	}

	arena = Clay_CreateArenaWithCapacityAndMemory(capacity, memory);
	dims.width = viewport_width;
	dims.height = viewport_height;
	handler.errorHandlerFunction = ui_clay_error_handler;
	handler.userData = NULL;

	ui_clay_state.allocator = allocator;
	ui_clay_state.arena_memory = memory;
	ui_clay_state.arena_capacity = capacity;
	ui_clay_state.viewport_width = viewport_width;
	ui_clay_state.viewport_height = viewport_height;
	ui_clay_state.font_system = font_system;
	ui_clay_state.font = font;
	ui_clay_state.logger = sk_logger_api()->create_logger("clay");

	Clay_Initialize(arena, dims, handler);
	Clay_SetMeasureTextFunction(ui_clay_measure_text, &ui_clay_state);
	/* Do not call Clay_SetMaxElementCount after Initialize: it only bumps the
	 * counter without resizing persistent arrays (would desync capacity). */

	ui_clay_state.initialized = 1;
	return 0;
}

void ui_clay_shutdown(void) {
	if (!ui_clay_state.initialized) {
		return;
	}
	/* Drop Clay's global context before freeing the arena it points into. */
	Clay_SetCurrentContext(NULL);
	if (ui_clay_state.allocator != NULL && ui_clay_state.arena_memory != NULL) {
		ui_clay_state.allocator->free(ui_clay_state.allocator->instance, ui_clay_state.arena_memory);
	}
	if (ui_clay_state.logger != NULL) {
		sk_logger_api()->destroy_logger(ui_clay_state.logger);
	}
	memset(&ui_clay_state, 0, sizeof(ui_clay_state));
}

i32 ui_clay_is_initialized(void) {
	return ui_clay_state.initialized;
}

void ui_clay_set_font(sk_ui_font_system_t* font_system, sk_ui_font_t* font) {
	ui_clay_state.font_system = font_system;
	ui_clay_state.font = font;
}

i32 ui_clay_ensure_init(const sk_allocator_t* allocator, f32 viewport_width, f32 viewport_height, sk_ui_font_system_t* font_system, sk_ui_font_t* font) {
	Clay_Dimensions dims;
	if (!ui_clay_state.initialized) {
		if (ui_clay_init(allocator, viewport_width, viewport_height, font_system, font) != 0) {
			return -1;
		}
		return 0;
	}
	/* Already initialized: refresh viewport + font binding for this frame. */
	ui_clay_state.viewport_width = viewport_width;
	ui_clay_state.viewport_height = viewport_height;
	if (font_system != NULL || font != NULL) {
		ui_clay_state.font_system = font_system;
		ui_clay_state.font = font;
	}
	dims.width = viewport_width;
	dims.height = viewport_height;
	Clay_SetLayoutDimensions(dims);
	return 0;
}

#ifdef SK_TESTS
#include "test.h"
#include "testdata/skore_test_font_ttf.h"

/* -------------------------------------------------------------------------- */
/* Smoke path: parent + two children (GROW / FIXED)                            */
/* -------------------------------------------------------------------------- */

/** Declare one frame: parent fills the viewport; children FIXED then GROW. */
static void ui_clay_smoke_declare(void) {
	/* Defaults used on purpose: zero padding/gap, left-to-right direction,
	 * top-left child alignment — the assertions below depend on them. */
	CLAY({
		.id = CLAY_ID("sk_smoke_root"),
		.layout =
			{
				.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
			},
		.backgroundColor = {30.0f, 40.0f, 50.0f, 255.0f},
	}) {
		CLAY({
			.id = CLAY_ID("sk_smoke_child_fixed"),
			.layout =
				{
					.sizing = {CLAY_SIZING_FIXED(100.0f), CLAY_SIZING_FIXED(50.0f)},
				},
			.backgroundColor = {60.0f, 70.0f, 80.0f, 255.0f},
		}) {}
		CLAY({
			.id = CLAY_ID("sk_smoke_child_grow"),
			.layout =
				{
					.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(50.0f)},
				},
			.backgroundColor = {90.0f, 100.0f, 110.0f, 255.0f},
		}) {}
	}
}

/** Find the render command carrying element id @p id, or NULL. */
static const Clay_RenderCommand* ui_clay_find_command(Clay_RenderCommandArray* cmds, u32 id) {
	int32_t i;
	for (i = 0; i < cmds->length; ++i) {
		Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(cmds, i);
		if (cmd != NULL && cmd->id == id) {
			return cmd;
		}
	}
	return NULL;
}

SK_TEST(ui_clay_smoke_parent_two_children_grow_fixed) {
	const sk_allocator_t* allocator = sk_allocator_default();
	sk_ui_font_system_t* sys;
	sk_ui_font_t* font;
	Clay_RenderCommandArray cmds;
	const Clay_RenderCommand* root_cmd;
	const Clay_RenderCommand* fixed_cmd;
	const Clay_RenderCommand* grow_cmd;

	/* Bind the engine's test font so the measure callback runs against real
	 * engine font metrics (not exercised by this layout, but proves wiring). */
	sys = ui_font_system_create_impl(allocator, 256u, 256u);
	TEST_ASSERT_NOT_NULL(sys);
	font = ui_font_load_memory_impl(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);

	/* Arena from the engine allocator; Clay viewport = 800x600.
	 * Prior tests may have left Clay initialized via the panel adapter. */
	if (ui_clay_is_initialized()) {
		ui_clay_shutdown();
	}
	TEST_ASSERT_EQUAL_INT(0, ui_clay_init(allocator, 800.0f, 600.0f, sys, font));

	Clay_BeginLayout();
	ui_clay_smoke_declare();
	cmds = Clay_EndLayout();

	/* Parent + two children → exactly three rectangle commands. */
	TEST_ASSERT_EQUAL_INT(3, cmds.length);

	root_cmd = ui_clay_find_command(&cmds, Clay_GetElementId(CLAY_STRING("sk_smoke_root")).id);
	fixed_cmd = ui_clay_find_command(&cmds, Clay_GetElementId(CLAY_STRING("sk_smoke_child_fixed")).id);
	grow_cmd = ui_clay_find_command(&cmds, Clay_GetElementId(CLAY_STRING("sk_smoke_child_grow")).id);
	TEST_ASSERT_NOT_NULL(root_cmd);
	TEST_ASSERT_NOT_NULL(fixed_cmd);
	TEST_ASSERT_NOT_NULL(grow_cmd);

	/* Parent fills the viewport. */
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, root_cmd->boundingBox.x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, root_cmd->boundingBox.y);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 800.0f, root_cmd->boundingBox.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 600.0f, root_cmd->boundingBox.height);

	/* First child: FIXED 100x50 at the row start. */
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, fixed_cmd->boundingBox.x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, fixed_cmd->boundingBox.y);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, fixed_cmd->boundingBox.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, fixed_cmd->boundingBox.height);

	/* Second child: GROW consumes the remaining row width (800 - 100). */
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, grow_cmd->boundingBox.x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, grow_cmd->boundingBox.y);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 700.0f, grow_cmd->boundingBox.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, grow_cmd->boundingBox.height);

	ui_clay_shutdown();
	ui_font_system_destroy_impl(sys);
}

#endif /* SK_TESTS */
