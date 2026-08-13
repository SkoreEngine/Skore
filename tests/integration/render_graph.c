/*
 * render_graph.c — host call-site integration tests (APX-156).
 *
 * Ports the C++ main consumer patterns from the migration audit onto the
 * render_graph plugin fn table. Hosts never include C++ RenderGraph types;
 * they only:
 *   sk_app_init → load plugins → get_api(SK_RENDER_GRAPH_API_TYPE_ID) →
 *   sk_render_pipeline_context_create / execute / destroy.
 *
 * RHI is the CPU-mock sk-test-render-device (no Vulkan ICD required), so
 * these always run in CI. Re-load test-render-device last so it wins
 * SK_RENDER_DEVICE_API_TYPE_ID (same pattern as test_render_device.c).
 */

#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "render_device.h"
#include "render_graph.h"
#include "render_pipeline.h"
#include "test.h"

#include <string.h>

#ifdef SK_TESTS

static i32 rg_integration_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	char base[SK_FS_PATH_MAX];
	char plugins[SK_FS_PATH_MAX];

	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}
	i32 n = sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins, (u32)sizeof(plugins));
	if (n < 0) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(plugins), sk_str_view_cstr(plugin_filename), out, out_cap);
	return (n < 0) ? -1 : 0;
}

static void rg_integration_load_plugin(sk_app_context_t* ctx, const_chr_t plugin_filename, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
	if (rg_integration_plugin_path(plugin_filename, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
}

/*
 * Ensure both plugin tables are registered: render_graph (auto-load or
 * explicit) and test_render_device last for the RHI type id.
 */
static i32 rg_integration_acquire_apis(sk_app_context_t* ctx, const sk_render_graph_api_t** out_rg, const sk_render_device_api_t** out_rhi, const sk_app_api_t* app_api) {
#if defined(_WIN32)
	const_chr_t rg_name = "sk-render-graph.dll";
	const_chr_t trd_name = "sk-test-render-device.dll";
#elif defined(__APPLE__)
	const_chr_t rg_name = "sk-render-graph.dylib";
	const_chr_t trd_name = "sk-test-render-device.dylib";
#else
	const_chr_t rg_name = "sk-render-graph.so";
	const_chr_t trd_name = "sk-test-render-device.so";
#endif
	rg_integration_load_plugin(ctx, rg_name, app_api);
	rg_integration_load_plugin(ctx, trd_name, app_api);

	*out_rg = sk_render_graph_api_from_app(ctx, app_api);
	*out_rhi = (const sk_render_device_api_t*)app_api->get_api(ctx, SK_RENDER_DEVICE_API_TYPE_ID);
	if (*out_rg == NULL || *out_rhi == NULL) {
		return -1;
	}
	return 0;
}

/* ---- builders mirroring audit call sites ---- */

typedef struct rg_host_build_user_t {
	i32 record_count;
	u32 output_w;
	u32 output_h;
} rg_host_build_user_t;

static void rg_host_record_count(sk_rg_pass_t* pass, void_ptr_t scene, sk_command_buffer_t cmd, void_ptr_t user) {
	rg_host_build_user_t* u = (rg_host_build_user_t*)user;
	(void)pass;
	(void)scene;
	(void)cmd;
	if (u != NULL) {
		u->record_count += 1;
	}
}

/*
 * Default pipeline-style frame: color output + lighting → composite chain.
 * Mirrors Runtime RenderPipelineContext::Execute + a minimal pass list.
 */
static void rg_host_build_default_pipeline(const sk_render_graph_api_t* api, sk_render_graph_t* graph, void_ptr_t user) {
	rg_host_build_user_t* u = (rg_host_build_user_t*)user;
	sk_rg_texture_desc_t tex;
	sk_rg_extent_t extent;
	sk_rg_pass_t* lighting;
	sk_rg_pass_t* composite;

	memset(&tex, 0, sizeof(tex));
	tex.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	tex.extent.width = u->output_w;
	tex.extent.height = u->output_h;
	tex.extent.depth = 1u;
	tex.scale_x = 1.0f;
	tex.scale_y = 1.0f;
	tex.mip_levels = 1u;
	tex.array_layers = 1u;
	tex.samples = 1u;

	extent.width = u->output_w;
	extent.height = u->output_h;
	api->set_output_size(graph, extent);
	api->create_texture(graph, "Color", &tex);
	api->set_color_output(graph, "Color");

	lighting = api->add_pass(graph, "Lighting", SK_RG_PASS_COMPUTE);
	composite = api->add_pass(graph, "Composite", SK_RG_PASS_COMPUTE);
	api->pass_write(lighting, "Color");
	api->pass_read(composite, "Color");
	api->pass_write(composite, "Color");
	api->pass_set_side_effects(composite, 1);
	api->pass_set_record(lighting, rg_host_record_count, u);
	api->pass_set_record(composite, rg_host_record_count, u);
}

/*
 * Standalone PreviewGenerator-style graph: local Begin/Build/Execute without
 * a long-lived pipeline object (audit §2.2 PreviewGenerator).
 */
static void rg_host_build_preview(const sk_render_graph_api_t* api, sk_render_graph_t* graph, void_ptr_t user) {
	rg_host_build_user_t* u = (rg_host_build_user_t*)user;
	sk_rg_texture_desc_t tex;
	sk_rg_extent_t extent;
	sk_rg_pass_t* preview;

	memset(&tex, 0, sizeof(tex));
	tex.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	tex.extent.width = u->output_w;
	tex.extent.height = u->output_h;
	tex.extent.depth = 1u;
	tex.scale_x = 1.0f;
	tex.scale_y = 1.0f;
	tex.mip_levels = 1u;
	tex.array_layers = 1u;
	tex.samples = 1u;

	extent.width = u->output_w;
	extent.height = u->output_h;
	api->set_output_size(graph, extent);
	api->create_texture(graph, "PreviewColor", &tex);
	api->set_color_output(graph, "PreviewColor");

	preview = api->add_pass(graph, "PreviewForward", SK_RG_PASS_GRAPHICS);
	api->pass_write(preview, "PreviewColor");
	api->pass_set_side_effects(preview, 1);
	api->pass_set_record(preview, rg_host_record_count, u);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_host_acquires_api_via_registry) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	const sk_render_graph_api_t* rg = NULL;
	const sk_render_device_api_t* rhi = NULL;

	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, rg_integration_acquire_apis(ctx, &rg, &rhi, boot.api));
	TEST_ASSERT_NOT_NULL(rg);
	TEST_ASSERT_NOT_NULL(rhi);
	TEST_ASSERT_NOT_NULL(rg->create);
	TEST_ASSERT_NOT_NULL(rg->begin);
	TEST_ASSERT_NOT_NULL(rg->execute);
	TEST_ASSERT_NOT_NULL(rg->destroy);

	sk_app_shutdown(ctx);
}

SK_TEST(render_graph_host_pipeline_context_frame) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	const sk_render_graph_api_t* rg = NULL;
	const sk_render_device_api_t* rhi = NULL;
	sk_render_device_t dev;
	sk_command_buffer_t cmd;
	sk_render_pipeline_context_t pipeline;
	rg_host_build_user_t user;
	u32 frame;

	TEST_ASSERT_NOT_NULL(ctx);
	if (ctx == NULL) {
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, rg_integration_acquire_apis(ctx, &rg, &rhi, boot.api));
	if (rg == NULL || rhi == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, rg->init());
	dev = rhi->init(ctx, NULL);
	TEST_ASSERT_TRUE(sk_render_device_t_is_valid(dev));
	if (!sk_render_device_t_is_valid(dev)) {
		rg->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	memset(&user, 0, sizeof(user));
	user.output_w = 64u;
	user.output_h = 64u;

	/* Port of RenderPipelineContext: create once, execute every frame. */
	TEST_ASSERT_EQUAL_INT(0, sk_render_pipeline_context_create(&pipeline, rg, dev, rg_host_build_default_pipeline, &user));
	TEST_ASSERT_NOT_NULL(pipeline.graph);

	cmd = rhi->create_command_buffer(dev, NULL);
	TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(cmd));

	for (frame = 0u; frame < 4u; ++frame) {
		TEST_ASSERT_EQUAL_INT(0, rhi->begin_command_buffer(dev, cmd, NULL));
		/* Player: SetCurrentOutputIndex before Execute when swapchain is live. */
		rg->set_current_output_index(pipeline.graph, frame % 2u);
		sk_render_pipeline_context_execute(&pipeline, cmd, NULL);
		rhi->end_command_buffer(dev, cmd);

		TEST_ASSERT_TRUE(sk_texture_t_is_valid(rg->get_texture(pipeline.graph, "Color")));
		TEST_ASSERT_TRUE(rg->get_compiled_pass_count(pipeline.graph) >= 1u);
	}

	TEST_ASSERT_EQUAL_INT(8, user.record_count); /* 2 passes × 4 frames */

	rhi->destroy_command_buffer(dev, cmd);
	sk_render_pipeline_context_destroy(&pipeline);
	rhi->destroy(dev);
	rg->shutdown();
	sk_app_shutdown(ctx);
}

SK_TEST(render_graph_host_standalone_preview_path) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	const sk_render_graph_api_t* rg = NULL;
	const sk_render_device_api_t* rhi = NULL;
	sk_render_device_t dev;
	sk_command_buffer_t cmd;
	sk_render_graph_t* graph;
	rg_host_build_user_t user;

	TEST_ASSERT_NOT_NULL(ctx);
	if (ctx == NULL) {
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, rg_integration_acquire_apis(ctx, &rg, &rhi, boot.api));
	if (rg == NULL || rhi == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, rg->init());
	dev = rhi->init(ctx, NULL);
	TEST_ASSERT_TRUE(sk_render_device_t_is_valid(dev));
	if (!sk_render_device_t_is_valid(dev)) {
		rg->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	/*
	 * PreviewGenerator: owns a graph member, does not use RenderPipelineContext.
	 * begin → local BuildRenderGraph → execute.
	 */
	graph = rg->create(dev);
	TEST_ASSERT_NOT_NULL(graph);

	memset(&user, 0, sizeof(user));
	user.output_w = 32u;
	user.output_h = 32u;

	cmd = rhi->create_command_buffer(dev, NULL);
	TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(cmd));
	TEST_ASSERT_EQUAL_INT(0, rhi->begin_command_buffer(dev, cmd, NULL));

	rg->begin(graph, NULL);
	rg_host_build_preview(rg, graph, &user);
	rg->execute(graph, cmd);

	TEST_ASSERT_EQUAL_INT(1, user.record_count);
	TEST_ASSERT_TRUE(sk_texture_t_is_valid(rg->get_texture(graph, "PreviewColor")));
	TEST_ASSERT_EQUAL_UINT32(1u, rg->get_compiled_pass_count(graph));

	rhi->end_command_buffer(dev, cmd);
	rhi->destroy_command_buffer(dev, cmd);
	rg->destroy(graph);
	rhi->destroy(dev);
	rg->shutdown();
	sk_app_shutdown(ctx);
}

SK_TEST(render_graph_host_import_and_output_index) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	const sk_render_graph_api_t* rg = NULL;
	const sk_render_device_api_t* rhi = NULL;
	sk_render_device_t dev;
	sk_command_buffer_t cmd;
	sk_render_pipeline_context_t pipeline;
	rg_host_build_user_t user;
	sk_texture_t imported[2];
	sk_texture_desc_t tdesc;
	sk_rg_extent_t extent;

	TEST_ASSERT_NOT_NULL(ctx);
	if (ctx == NULL) {
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, rg_integration_acquire_apis(ctx, &rg, &rhi, boot.api));
	if (rg == NULL || rhi == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, rg->init());
	dev = rhi->init(ctx, NULL);
	TEST_ASSERT_TRUE(sk_render_device_t_is_valid(dev));
	if (!sk_render_device_t_is_valid(dev)) {
		rg->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	/* Player CreatePlayerSwapchain: import swapchain images as color output. */
	memset(&tdesc, 0, sizeof(tdesc));
	tdesc.extent.width = 128u;
	tdesc.extent.height = 72u;
	tdesc.extent.depth = 1u;
	tdesc.mip_levels = 1u;
	tdesc.array_layers = 1u;
	tdesc.sample_count = 1u;
	tdesc.format = SK_PIXEL_FORMAT_BGRA8_UNORM;
	tdesc.usage_flags = (u32)SK_RESOURCE_USAGE_RENDER_TARGET | (u32)SK_RESOURCE_USAGE_COPY_SOURCE;
	tdesc.debug_name = "swapchain-0";
	imported[0] = rhi->create_texture(dev, &tdesc);
	tdesc.debug_name = "swapchain-1";
	imported[1] = rhi->create_texture(dev, &tdesc);
	TEST_ASSERT_TRUE(sk_texture_t_is_valid(imported[0]));
	TEST_ASSERT_TRUE(sk_texture_t_is_valid(imported[1]));

	memset(&user, 0, sizeof(user));
	user.output_w = 128u;
	user.output_h = 72u;

	TEST_ASSERT_EQUAL_INT(0, sk_render_pipeline_context_create(&pipeline, rg, dev, rg_host_build_default_pipeline, &user));

	extent.width = 128u;
	extent.height = 72u;
	rg->set_output_size(pipeline.graph, extent);
	/* Import under a distinct name; builder still declares "Color" for the chain. */
	rg->import_textures(pipeline.graph, "Swapchain", imported, 2u, SK_RESOURCE_STATE_PRESENT);

	cmd = rhi->create_command_buffer(dev, NULL);
	TEST_ASSERT_EQUAL_INT(0, rhi->begin_command_buffer(dev, cmd, NULL));
	rg->set_current_output_index(pipeline.graph, 1u);
	sk_render_pipeline_context_execute(&pipeline, cmd, NULL);
	rhi->end_command_buffer(dev, cmd);

	TEST_ASSERT_TRUE(user.record_count >= 2);
	TEST_ASSERT_TRUE(sk_texture_t_is_valid(rg->get_texture(pipeline.graph, "Color")));

	rhi->destroy_command_buffer(dev, cmd);
	sk_render_pipeline_context_destroy(&pipeline);
	rhi->destroy_texture(dev, imported[0]);
	rhi->destroy_texture(dev, imported[1]);
	rhi->destroy(dev);
	rg->shutdown();
	sk_app_shutdown(ctx);
}

/*
 * Real host frame loop (APX-157): same path as player/pipeline context —
 * create once, then begin → build → execute for many frames. After warm-up,
 * graph heap_alloc_count and growth_events stay flat (allocation-free frame
 * path outside the in-plugin unit tests).
 */
SK_TEST(render_graph_host_pipeline_zero_heap_steady_state) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	const sk_render_graph_api_t* rg = NULL;
	const sk_render_device_api_t* rhi = NULL;
	sk_render_device_t dev;
	sk_command_buffer_t cmd;
	sk_render_pipeline_context_t pipeline;
	sk_rg_memory_config_t mem_cfg;
	sk_rg_memory_stats_t stats;
	rg_host_build_user_t user;
	u32 heap_after_warm;
	u32 growth_after_warm;
	u32 frame;
	const u32 warm_frames = 2u;
	const u32 steady_frames = 8u;

	TEST_ASSERT_NOT_NULL(ctx);
	if (ctx == NULL) {
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, rg_integration_acquire_apis(ctx, &rg, &rhi, boot.api));
	if (rg == NULL || rhi == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, rg->init());
	dev = rhi->init(ctx, NULL);
	TEST_ASSERT_TRUE(sk_render_device_t_is_valid(dev));
	if (!sk_render_device_t_is_valid(dev)) {
		rg->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	memset(&user, 0, sizeof(user));
	user.output_w = 96u;
	user.output_h = 54u;

	/* Capacities sized for the default host pipeline (and headroom). */
	memset(&mem_cfg, 0, sizeof(mem_cfg));
	mem_cfg.frame_arena_bytes = 64ull * 1024ull;
	mem_cfg.pass_capacity = 32u;
	mem_cfg.resource_capacity = 64u;
	mem_cfg.edge_capacity = 128u;
	mem_cfg.barrier_capacity = 128u;

	TEST_ASSERT_EQUAL_INT(0, sk_render_pipeline_context_create_with_config(&pipeline, rg, dev, &mem_cfg, rg_host_build_default_pipeline, &user));
	TEST_ASSERT_NOT_NULL(pipeline.graph);

	cmd = rhi->create_command_buffer(dev, NULL);
	TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(cmd));

	for (frame = 0u; frame < warm_frames; ++frame) {
		TEST_ASSERT_EQUAL_INT(0, rhi->begin_command_buffer(dev, cmd, NULL));
		rg->set_current_output_index(pipeline.graph, frame % 2u);
		sk_render_pipeline_context_execute(&pipeline, cmd, NULL);
		rhi->end_command_buffer(dev, cmd);
		TEST_ASSERT_EQUAL_INT(SK_RG_OK, rg->get_last_error(pipeline.graph));
	}

	rg->get_memory_stats(pipeline.graph, &stats);
	heap_after_warm = stats.heap_alloc_count;
	growth_after_warm = stats.growth_events;

	for (frame = 0u; frame < steady_frames; ++frame) {
		TEST_ASSERT_EQUAL_INT(0, rhi->begin_command_buffer(dev, cmd, NULL));
		rg->set_current_output_index(pipeline.graph, frame % 2u);
		sk_render_pipeline_context_execute(&pipeline, cmd, NULL);
		rhi->end_command_buffer(dev, cmd);

		rg->get_memory_stats(pipeline.graph, &stats);
		TEST_ASSERT_EQUAL_UINT32(heap_after_warm, stats.heap_alloc_count);
		TEST_ASSERT_EQUAL_UINT32(growth_after_warm, stats.growth_events);
		TEST_ASSERT_EQUAL_INT(0, stats.in_frame); /* execute ends the frame */
		TEST_ASSERT_EQUAL_INT(SK_RG_OK, rg->get_last_error(pipeline.graph));
		TEST_ASSERT_TRUE(rg->get_compiled_pass_count(pipeline.graph) >= 1u);
	}

	/* 2 warm + 8 steady frames, 2 record callbacks each */
	TEST_ASSERT_EQUAL_INT((i32)((warm_frames + steady_frames) * 2u), user.record_count);

	rhi->destroy_command_buffer(dev, cmd);
	sk_render_pipeline_context_destroy(&pipeline);
	rhi->destroy(dev);
	rg->shutdown();
	sk_app_shutdown(ctx);
}

#endif /* SK_TESTS */
