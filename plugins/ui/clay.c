/**
 * @file clay.c
 * @brief Clay (vendored immediate-mode layout) engine bridge + smoke path.
 *
 * Bridges thirdparty/clay v0.14 into the engine without touching the existing
 * retained-mode layout code or any UI call sites (APX-213):
 *
 *  - the Clay arena is allocated through the engine allocator (sk_allocator_t)
 *  - Clay_Initialize is fed the current viewport dimensions
 *  - Clay_SetMeasureTextFunction is backed by the engine's font metrics
 *    (sk_ui_font_metrics_t line height + per-glyph advances)
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

/** Headroom above Clay_MinMemorySize() so smoke layouts never hit capacity. */
#define UI_CLAY_ARENA_HEADROOM (4u * 1024u * 1024u)

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
 * Decode one UTF-8 codepoint from @p s (length @p len), advancing @p index.
 * Returns the codepoint, or 0 when the byte at @p index is not a valid
 * lead/continuation byte (index still advances past the invalid byte).
 */
static u32 ui_clay_utf8_next(const char* s, size_t len, size_t* index) {
	u32 cp;
	u32 need;
	u32 k;
	size_t i;
	u8 c0;

	i = *index;
	if (i >= len) {
		return 0u;
	}
	c0 = (u8)s[i];
	if (c0 < 0x80u) {
		*index = i + 1u;
		return (u32)c0;
	}
	if (c0 < 0xc2u || c0 > 0xf4u) {
		*index = i + 1u; /* invalid lead byte; skip one */
		return 0u;
	}
	if (c0 < 0xe0u) {
		cp = (u32)(c0 & 0x1fu);
		need = 1u;
	} else if (c0 < 0xf0u) {
		cp = (u32)(c0 & 0x0fu);
		need = 2u;
	} else {
		cp = (u32)(c0 & 0x07u);
		need = 3u;
	}
	if (i + 1u + need > len) {
		*index = len;
		return 0u;
	}
	for (k = 0u; k < need; ++k) {
		u8 cc = (u8)s[i + 1u + k];
		if ((cc & 0xc0u) != 0x80u) {
			*index = i + 1u + k;
			return 0u;
		}
		cp = (cp << 6) | (u32)(cc & 0x3fu);
	}
	*index = i + 1u + need;
	return cp;
}

/**
 * Clay measure callback. Width is the sum of engine glyph advances at the
 * requested pixel size; height is the engine font line height (fallbacks when
 * no font is bound). Signature is fixed by Clay (config must stay non-const).
 */
// NOLINTNEXTLINE(readability-non-const-parameter)
static Clay_Dimensions ui_clay_measure_text(Clay_StringSlice text, Clay_TextElementConfig* config, void* user_data) {
	const ui_clay_state_t* state = (const ui_clay_state_t*)user_data;
	Clay_Dimensions out;
	sk_ui_font_metrics_t metrics;
	size_t index;
	f32 width;
	f32 height;
	u32 pixel_size;

	/* Clay text configs use physical pixel sizes, like the engine font system. */
	pixel_size = (config != NULL && config->fontSize > 0u) ? (u32)config->fontSize : 16u;

	width = 0.0f;
	height = UI_CLAY_FALLBACK_FONT_SIZE;
	if (state->font != NULL && state->font_system != NULL) {
		if (ui_font_get_metrics_impl(state->font, pixel_size, &metrics) == 0) {
			height = metrics.line_height;
		}
		index = 0u;
		while (index < (size_t)text.length) {
			u32 cp = ui_clay_utf8_next(text.chars, (size_t)text.length, &index);
			u32 glyph_index = ui_font_glyph_index_impl(state->font, cp);
			sk_ui_glyph_t glyph;
			if (glyph_index == 0u) {
				glyph_index = ui_font_glyph_index_impl(state->font, 0x20u); /* space fallback */
			}
			if (ui_font_get_glyph_impl(state->font_system, state->font, pixel_size, glyph_index, &glyph) == 0) {
				width += glyph.advance_x;
			} else {
				width += UI_CLAY_FALLBACK_GLYPH_ADVANCE;
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

	/* Arena comes from the engine allocator, sized for Clay's minimum plus
	 * headroom so simple layouts never trip the capacity error handler. */
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

	ui_clay_state.initialized = 1;
	return 0;
}

void ui_clay_shutdown(void) {
	if (!ui_clay_state.initialized) {
		return;
	}
	if (ui_clay_state.allocator != NULL && ui_clay_state.arena_memory != NULL) {
		ui_clay_state.allocator->free(ui_clay_state.allocator->instance, ui_clay_state.arena_memory);
	}
	if (ui_clay_state.logger != NULL) {
		sk_logger_api()->destroy_logger(ui_clay_state.logger);
	}
	memset(&ui_clay_state, 0, sizeof(ui_clay_state));
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

	/* Arena from the engine allocator; Clay viewport = 800x600. */
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
