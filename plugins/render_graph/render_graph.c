#include "render_graph.h"

/* Stub init/shutdown — no graph logic yet (scaffold only). */
static i32 render_graph_init_impl(void) {
	return 0;
}

static void render_graph_shutdown_impl(void) {}

static const sk_render_graph_api_t render_graph_api = {
	render_graph_init_impl,
	render_graph_shutdown_impl,
};

void sk_render_graph_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_RENDER_GRAPH_API_TYPE_ID, (const_ptr_t)&render_graph_api);
}

#ifdef SK_TESTS
#include "test.h"

SK_TEST(render_graph_stub_init) {
	const i32 rc = render_graph_init_impl();
	TEST_ASSERT_EQUAL_INT(0, rc);
	render_graph_shutdown_impl();
}
#endif /* SK_TESTS */
