/**
 * @file render_graph.c
 * @brief sk-render-graph plugin: static fn table + not-implemented stubs.
 *
 * No graph logic yet — this TU exists so the full public API surface is
 * registered, loadable, and testable before pass/resource implementation.
 */

#include "render_graph.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Stubs — not implemented (return null / zero / no-op)                */
/* ------------------------------------------------------------------ */

static i32 render_graph_init_impl(void) {
	return 0;
}

static void render_graph_shutdown_impl(void) {}

static sk_render_graph_t* render_graph_create_impl(sk_render_device_t device) {
	(void)device;
	return NULL;
}

static void render_graph_destroy_impl(sk_render_graph_t* graph) {
	(void)graph;
}

static void render_graph_create_texture_impl(sk_render_graph_t* g, const_chr_t name, const sk_rg_texture_desc_t* desc) {
	(void)g;
	(void)name;
	(void)desc;
}

static void render_graph_create_buffer_impl(sk_render_graph_t* g, const_chr_t name, const sk_rg_buffer_desc_t* desc) {
	(void)g;
	(void)name;
	(void)desc;
}

static void render_graph_create_view_impl(sk_render_graph_t* g, const_chr_t name, const sk_rg_view_desc_t* desc) {
	(void)g;
	(void)name;
	(void)desc;
}

static void render_graph_import_textures_impl(sk_render_graph_t* g, const_chr_t name, const sk_texture_t* textures, u32 count, sk_resource_state_t state) {
	(void)g;
	(void)name;
	(void)textures;
	(void)count;
	(void)state;
}

static void_ptr_t render_graph_create_instance_impl(sk_render_graph_t* g, const_chr_t name, u64 size) {
	(void)g;
	(void)name;
	(void)size;
	return NULL;
}

static void_ptr_t render_graph_get_instance_impl(sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return NULL;
}

static sk_rg_pass_t* render_graph_add_pass_impl(sk_render_graph_t* g, const_chr_t name, sk_rg_pass_type_t type) {
	(void)g;
	(void)name;
	(void)type;
	return NULL;
}

static void render_graph_pass_read_impl(sk_rg_pass_t* p, const_chr_t name) {
	(void)p;
	(void)name;
}

static void render_graph_pass_write_impl(sk_rg_pass_t* p, const_chr_t name) {
	(void)p;
	(void)name;
}

static void render_graph_pass_read_write_impl(sk_rg_pass_t* p, const_chr_t name) {
	(void)p;
	(void)name;
}

static void render_graph_pass_resolve_impl(sk_rg_pass_t* p, const_chr_t name) {
	(void)p;
	(void)name;
}

static void render_graph_pass_stage_impl(sk_rg_pass_t* p, i32 stage) {
	(void)p;
	(void)stage;
}

static void render_graph_pass_set_pipeline_impl(sk_rg_pass_t* p, sk_pipeline_t pipeline) {
	(void)p;
	(void)pipeline;
}

static void render_graph_pass_set_descriptor_set_impl(sk_rg_pass_t* p, u32 set, sk_descriptor_set_t ds) {
	(void)p;
	(void)set;
	(void)ds;
}

static void render_graph_pass_set_record_impl(sk_rg_pass_t* p, sk_rg_record_fn fn, void_ptr_t user) {
	(void)p;
	(void)fn;
	(void)user;
}

static void render_graph_pass_set_resize_impl(sk_rg_pass_t* p, sk_rg_resize_fn fn, void_ptr_t user) {
	(void)p;
	(void)fn;
	(void)user;
}

static void render_graph_pass_set_constants_impl(sk_rg_pass_t* p, u32 size, u32 stage_mask, sk_rg_constants_fn fn, void_ptr_t user) {
	(void)p;
	(void)size;
	(void)stage_mask;
	(void)fn;
	(void)user;
}

static void render_graph_pass_dispatch_impl(sk_rg_pass_t* p, u32 x, u32 y, u32 z) {
	(void)p;
	(void)x;
	(void)y;
	(void)z;
}

static void render_graph_pass_dispatch_indirect_impl(sk_rg_pass_t* p, sk_buffer_t indirect) {
	(void)p;
	(void)indirect;
}

static void render_graph_pass_trace_rays_impl(sk_rg_pass_t* p, u32 w, u32 h, u32 d) {
	(void)p;
	(void)w;
	(void)h;
	(void)d;
}

static sk_texture_t render_graph_get_texture_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_texture_t_zero();
}

static sk_texture_t render_graph_get_prev_texture_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_texture_t_zero();
}

static sk_texture_view_t render_graph_get_texture_view_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_texture_view_t_zero();
}

static sk_buffer_t render_graph_get_buffer_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_buffer_t_zero();
}

static sk_buffer_t render_graph_get_prev_buffer_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_buffer_t_zero();
}

static void render_graph_set_color_output_impl(sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
}

static void render_graph_set_depth_output_impl(sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
}

static void render_graph_set_output_size_impl(sk_render_graph_t* g, sk_rg_extent_t size) {
	(void)g;
	(void)size;
}

static sk_rg_extent_t render_graph_get_output_size_impl(const sk_render_graph_t* g) {
	sk_rg_extent_t zero = {0u, 0u};
	(void)g;
	return zero;
}

static void render_graph_set_current_output_index_impl(sk_render_graph_t* g, u32 index) {
	(void)g;
	(void)index;
}

static void render_graph_begin_impl(sk_render_graph_t* g, void_ptr_t scene) {
	(void)g;
	(void)scene;
}

static void render_graph_execute_impl(sk_render_graph_t* g, sk_command_buffer_t cmd) {
	(void)g;
	(void)cmd;
}

static u32 render_graph_topology_build_count_impl(const sk_render_graph_t* g) {
	(void)g;
	return 0u;
}

/* ------------------------------------------------------------------ */
/* Static function table                                               */
/* ------------------------------------------------------------------ */

static const sk_render_graph_api_t render_graph_api = {
	render_graph_init_impl,
	render_graph_shutdown_impl,
	render_graph_create_impl,
	render_graph_destroy_impl,
	render_graph_create_texture_impl,
	render_graph_create_buffer_impl,
	render_graph_create_view_impl,
	render_graph_import_textures_impl,
	render_graph_create_instance_impl,
	render_graph_get_instance_impl,
	render_graph_add_pass_impl,
	render_graph_pass_read_impl,
	render_graph_pass_write_impl,
	render_graph_pass_read_write_impl,
	render_graph_pass_resolve_impl,
	render_graph_pass_stage_impl,
	render_graph_pass_set_pipeline_impl,
	render_graph_pass_set_descriptor_set_impl,
	render_graph_pass_set_record_impl,
	render_graph_pass_set_resize_impl,
	render_graph_pass_set_constants_impl,
	render_graph_pass_dispatch_impl,
	render_graph_pass_dispatch_indirect_impl,
	render_graph_pass_trace_rays_impl,
	render_graph_get_texture_impl,
	render_graph_get_prev_texture_impl,
	render_graph_get_texture_view_impl,
	render_graph_get_buffer_impl,
	render_graph_get_prev_buffer_impl,
	render_graph_set_color_output_impl,
	render_graph_set_depth_output_impl,
	render_graph_set_output_size_impl,
	render_graph_get_output_size_impl,
	render_graph_set_current_output_index_impl,
	render_graph_begin_impl,
	render_graph_execute_impl,
	render_graph_topology_build_count_impl,
};

void sk_render_graph_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_RENDER_GRAPH_API_TYPE_ID, (const_ptr_t)&render_graph_api);
}

#ifdef SK_TESTS
#include "test.h"

/*
 * Smoke: every approved API sketch entry is wired into the static table.
 * Graph behavior is intentionally unimplemented; non-null pointers prove
 * registration / ABI surface for later tasks.
 */
SK_TEST(render_graph_api_table_is_complete) {
	TEST_ASSERT_NOT_NULL(render_graph_api.init);
	TEST_ASSERT_NOT_NULL(render_graph_api.shutdown);
	TEST_ASSERT_NOT_NULL(render_graph_api.create);
	TEST_ASSERT_NOT_NULL(render_graph_api.destroy);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_texture);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_buffer);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_view);
	TEST_ASSERT_NOT_NULL(render_graph_api.import_textures);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_instance);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_instance);
	TEST_ASSERT_NOT_NULL(render_graph_api.add_pass);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_read);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_write);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_read_write);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_resolve);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_stage);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_pipeline);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_descriptor_set);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_record);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_resize);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_constants);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_dispatch);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_dispatch_indirect);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_trace_rays);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_texture);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_prev_texture);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_texture_view);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_buffer);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_prev_buffer);
	TEST_ASSERT_NOT_NULL(render_graph_api.set_color_output);
	TEST_ASSERT_NOT_NULL(render_graph_api.set_depth_output);
	TEST_ASSERT_NOT_NULL(render_graph_api.set_output_size);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_output_size);
	TEST_ASSERT_NOT_NULL(render_graph_api.set_current_output_index);
	TEST_ASSERT_NOT_NULL(render_graph_api.begin);
	TEST_ASSERT_NOT_NULL(render_graph_api.execute);
	TEST_ASSERT_NOT_NULL(render_graph_api.topology_build_count);
}

SK_TEST(render_graph_api_type_id_nonzero) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_RENDER_GRAPH_API_TYPE_ID, SK_TYPE_ID_ZERO));
}

SK_TEST(render_graph_stub_init_and_create) {
	const i32 rc = render_graph_api.init();
	TEST_ASSERT_EQUAL_INT(0, rc);
	/* Graph logic not implemented yet — create returns null. */
	TEST_ASSERT_NULL(render_graph_api.create(sk_render_device_t_zero()));
	render_graph_api.shutdown();
}

SK_TEST(render_graph_stub_accessors_return_zero) {
	TEST_ASSERT_FALSE(sk_texture_t_is_valid(render_graph_api.get_texture(NULL, "color")));
	TEST_ASSERT_FALSE(sk_buffer_t_is_valid(render_graph_api.get_buffer(NULL, "buf")));
	TEST_ASSERT_EQUAL_UINT32(0u, render_graph_api.topology_build_count(NULL));
	{
		const sk_rg_extent_t extent = render_graph_api.get_output_size(NULL);
		TEST_ASSERT_EQUAL_UINT32(0u, extent.width);
		TEST_ASSERT_EQUAL_UINT32(0u, extent.height);
	}
}
#endif /* SK_TESTS */
