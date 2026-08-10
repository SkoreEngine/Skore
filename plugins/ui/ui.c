/**
 * @file ui.c
 * @brief UI module scaffold + in-source unit tests.
 *
 * No UI functionality yet — only the API table registration surface.
 * FreeType and stb_rect_pack are linked at the CMake level for later font
 * atlas work; they are not used in this scaffold.
 */

#include "ui.h"

/* Stub init/shutdown — no UI logic yet (scaffold only). */
static i32 ui_init_impl(void) {
	return 0;
}

static void ui_shutdown_impl(void) {}

static const sk_ui_api_t ui_api = {
	ui_init_impl,
	ui_shutdown_impl,
};

void sk_ui_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_UI_API_TYPE_ID, (const_ptr_t)&ui_api);
}

#ifdef SK_TESTS
#include "test.h"

/* Vendored deps: prove FreeType and stb_rect_pack link and call correctly.
 * No UI font/atlas logic yet — only the scaffold harness. */
#include <ft2build.h>
#include FT_FREETYPE_H
#include "stb_rect_pack.h"

SK_TEST(ui_stub_init) {
	const i32 rc = ui_init_impl();
	TEST_ASSERT_EQUAL_INT(0, rc);
	ui_shutdown_impl();
}

SK_TEST(ui_thirdparty_deps_link) {
	/* FreeType: create and destroy a library instance. */
	FT_Library library = NULL;
	const FT_Error ft_err = FT_Init_FreeType(&library);
	TEST_ASSERT_EQUAL_INT(0, (int)ft_err);
	TEST_ASSERT_NOT_NULL(library);
	FT_Done_FreeType(library);

	/* stb_rect_pack: pack a single rect into a small target. */
	stbrp_context pack_ctx;
	stbrp_node nodes[8];
	stbrp_rect rects[1];
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
#endif /* SK_TESTS */
