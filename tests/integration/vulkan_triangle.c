/*
 * vulkan_triangle.c — offscreen triangle render integration test (APX-79).
 *
 * Full headless render through the app registry:
 *   sk_app_init → force-load sk-vulkan-render-device → init/select adapter →
 *   sk-dxc-compiler HLSL→SPIR-V → vertex/index/readback buffers → offscreen
 *   RGBA8 texture + view → render pass + framebuffer → graphics pipeline →
 *   command buffer (uploads, barriers, clear, bind, draw_indexed, copy to
 *   readback) → submit + fence wait → map readback → PPM artifact + asserts.
 *
 * No window / swapchain / presentation is involved. DXC runtime is required
 * (vendored under thirdparty/dxc/bin). Skipped (TEST_IGNORE) only when no
 * Vulkan ICD / adapter is present (e.g. CI without a GPU).
 */

#include "app.h"
#include "dxc_compiler.h"
#include "filesystem.h"
#include "path.h"
#include "render_device.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS

#ifndef SK_INTEGRATION_ARTIFACT_DIR
#define SK_INTEGRATION_ARTIFACT_DIR "."
#endif

/* Offscreen color target size (128x128 keeps the readback tiny). */
#define TRI_TEX_WIDTH 128u
#define TRI_TEX_HEIGHT 128u
#define TRI_TEXEL_BYTES 4u

/* Clear color the framebuffer is cleared to before the draw. */
#define TRI_CLEAR_R 0.1f
#define TRI_CLEAR_G 0.2f
#define TRI_CLEAR_B 0.3f

/* Triangle covers NDC from (-1,-1)/(0,1)/(1,-1): center is inside, the
 * top-left corner (pixel (2,2), NDC ≈ (-0.97, 0.97)) is outside. */
#define TRI_PIXEL_INSIDE_X (TRI_TEX_WIDTH / 2u)
#define TRI_PIXEL_INSIDE_Y (TRI_TEX_HEIGHT / 2u)
#define TRI_PIXEL_OUTSIDE_X 2u
#define TRI_PIXEL_OUTSIDE_Y 2u

typedef struct tri_vertex_t {
	f32 position[4];
	f32 color[4];
} tri_vertex_t;

/* Vulkan NDC is y-down (origin top-left), so the y coordinates are negated
 * relative to a y-up convention: the triangle points up, with the red vertex
 * at the bottom-left, green at the top-center, and blue at the bottom-right. */
static const tri_vertex_t tri_vertices[3] = {
	{{-1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
	{{0.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
	{{1.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
};

static const u32 tri_indices[3] = {0u, 1u, 2u};

/* Entry points must be "main": the Vulkan backend's graphics pipeline binds
 * every stage with pName = "main". */
static const_chr_t tri_vs_hlsl = "struct VSOutput {\n"
								 "    float4 position : SV_Position;\n"
								 "    float4 color : COLOR0;\n"
								 "};\n"
								 "VSOutput main(float4 position : POSITION, float4 color : COLOR0) {\n"
								 "    VSOutput output;\n"
								 "    output.position = float4(position.xyz, 1.0f);\n"
								 "    output.color = color;\n"
								 "    return output;\n"
								 "}\n";

static const_chr_t tri_ps_hlsl = "struct PSInput {\n"
								 "    float4 position : SV_Position;\n"
								 "    float4 color : COLOR0;\n"
								 "};\n"
								 "float4 main(PSInput input) : SV_Target {\n"
								 "    return input.color;\n"
								 "}\n";

/* ------------------------------------------------------------------ */
/* Plugin path / API helpers                                           */
/* ------------------------------------------------------------------ */

static i32 tri_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
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

/* Re-enter the Vulkan plugin so the real backend registers last (the engine
 * auto-loads the stub sk-render-device first; set_api replaces). */
static const sk_render_device_api_t* tri_render_device_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-vulkan-render-device.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-vulkan-render-device.dylib";
#else
	const_chr_t plugin_name = "sk-vulkan-render-device.so";
#endif
	if (tri_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_render_device_api_t*)app_api->get_api(ctx, SK_RENDER_DEVICE_API_TYPE_ID);
}

static const sk_dxc_compiler_api_t* tri_dxc_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-dxc-compiler.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-dxc-compiler.dylib";
#else
	const_chr_t plugin_name = "sk-dxc-compiler.so";
#endif
	if (tri_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_dxc_compiler_api_t*)app_api->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/* PPM writer (P6 binary; no third-party image deps)                   */
/* ------------------------------------------------------------------ */

static void tri_write_ppm(const_chr_t dir, const u8* pixels, u32 width, u32 height) {
	char path[SK_FS_PATH_MAX];
	snprintf(path, (u32)sizeof(path), "%s/sk-triangle-render.ppm", dir);

	FILE* file = fopen(path, "wb");
	TEST_ASSERT_NOT_NULL_MESSAGE(file, "failed to open PPM artifact for writing");
	if (file == NULL) {
		return;
	}
	/* P6 stores RGB triplets; the readback buffer is RGBA8, so emit only the
	 * R,G,B bytes of each texel and skip the alpha. */
	fprintf(file, "P6\n%u %u\n255\n", width, height);
	for (u32 y = 0u; y < height; ++y) {
		const u8* row = &pixels[(size_t)y * (size_t)width * TRI_TEXEL_BYTES];
		for (u32 x = 0u; x < width; ++x) {
			const u8* texel = &row[(size_t)x * TRI_TEXEL_BYTES];
			const u8 rgb[3] = {texel[0], texel[1], texel[2]};
			fwrite(rgb, 1u, 3u, file);
		}
	}
	fclose(file);
}

/* ------------------------------------------------------------------ */
/* Adapter selection (same highest-score path as vulkan_device.c)      */
/* ------------------------------------------------------------------ */

static sk_adapter_t tri_select_adapter(const sk_render_device_api_t* api, sk_render_device_t dev, u32* out_count) {
	u32 adapter_count = api->get_adapter_count(dev);
	if (adapter_count == 0u) {
		*out_count = 0u;
		return sk_adapter_t_zero();
	}

	sk_adapter_t best = sk_adapter_t_zero();
	u32 best_score = 0u;
	for (u32 i = 0u; i < adapter_count; ++i) {
		sk_adapter_t candidate = api->get_adapter(dev, i);
		u32 score = api->get_adapter_score(dev, candidate);
		if (score > best_score) {
			best_score = score;
			best = candidate;
		}
	}
	*out_count = adapter_count;
	return best;
}

/* ------------------------------------------------------------------ */
/* Test                                                                */
/* ------------------------------------------------------------------ */

SK_TEST(vulkan_offscreen_triangle_render) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	const sk_render_device_api_t* api = tri_render_device_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "vulkan_render_device plugin must register sk_render_device_api_t");
	if (api == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	const sk_dxc_compiler_api_t* dxc = tri_dxc_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(dxc, "dxc_compiler plugin must register sk_dxc_compiler_api_t");
	if (dxc == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	{
		const i32 dxc_init_rc = dxc->init();
		if (dxc_init_rc != 0) {
			sk_app_shutdown(ctx);
		}
		TEST_ASSERT_EQUAL_INT32_MESSAGE(0, dxc_init_rc, "DXC runtime must load (vendored lib missing or not copied to plugins/)");
	}

	sk_render_device_t dev = api->init(ctx, NULL);
	if (!sk_render_device_t_is_valid(dev)) {
		dxc->shutdown();
		TEST_IGNORE_MESSAGE("no Vulkan ICD available; skipping offscreen triangle render");
		sk_app_shutdown(ctx);
		return;
	}

	u32 adapter_count = 0u;
	sk_adapter_t best = tri_select_adapter(api, dev, &adapter_count);
	if (adapter_count == 0u || !sk_adapter_t_is_valid(best)) {
		api->destroy(dev);
		dxc->shutdown();
		TEST_IGNORE_MESSAGE("no suitable Vulkan adapter; skipping offscreen triangle render");
		sk_app_shutdown(ctx);
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, api->select_adapter(dev, best));

	/* --- Compile HLSL VS/PS to SPIR-V --- */
	u8 vs_spirv[8192];
	u8 ps_spirv[8192];
	u32 vs_size = 0u;
	u32 ps_size = 0u;
	char dxc_log[1024];
	memset(vs_spirv, 0, sizeof(vs_spirv));
	memset(ps_spirv, 0, sizeof(ps_spirv));
	{
		i32 rc = dxc->compile("main", "vs_6_0", tri_vs_hlsl, (u32)strlen(tri_vs_hlsl), vs_spirv, (u32)sizeof(vs_spirv), &vs_size, dxc_log, (u32)sizeof(dxc_log));
		TEST_ASSERT_EQUAL_INT32_MESSAGE(0, rc, dxc_log);
	}
	{
		i32 rc = dxc->compile("main", "ps_6_0", tri_ps_hlsl, (u32)strlen(tri_ps_hlsl), ps_spirv, (u32)sizeof(ps_spirv), &ps_size, dxc_log, (u32)sizeof(dxc_log));
		TEST_ASSERT_EQUAL_INT32_MESSAGE(0, rc, dxc_log);
	}
	TEST_ASSERT_TRUE(vs_size >= 4u);
	TEST_ASSERT_TRUE(ps_size >= 4u);

	/* --- Shaders --- */
	sk_shader_t vs = api->create_shader(dev, (const_chr_t)vs_spirv, vs_size, (u32)SK_SHADER_STAGE_VERTEX);
	sk_shader_t ps = api->create_shader(dev, (const_chr_t)ps_spirv, ps_size, (u32)SK_SHADER_STAGE_PIXEL);
	TEST_ASSERT_TRUE(sk_shader_t_is_valid(vs));
	TEST_ASSERT_TRUE(sk_shader_t_is_valid(ps));
	if (!sk_shader_t_is_valid(vs) || !sk_shader_t_is_valid(ps)) {
		api->destroy(dev);
		dxc->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	/* --- Buffers: vertex/index uploaded via update_buffer; readback host-visible --- */
	sk_buffer_desc_t vb_desc;
	memset(&vb_desc, 0, sizeof(vb_desc));
	vb_desc.size = sizeof(tri_vertices);
	vb_desc.usage_flags = (u32)SK_RESOURCE_USAGE_VERTEX_BUFFER | (u32)SK_RESOURCE_USAGE_COPY_DEST;
	vb_desc.host_visible = false;
	vb_desc.debug_name = "triangle-vertex-buffer";
	sk_buffer_t vb = api->create_buffer(dev, &vb_desc);

	sk_buffer_desc_t ib_desc;
	memset(&ib_desc, 0, sizeof(ib_desc));
	ib_desc.size = sizeof(tri_indices);
	ib_desc.usage_flags = (u32)SK_RESOURCE_USAGE_INDEX_BUFFER | (u32)SK_RESOURCE_USAGE_COPY_DEST;
	ib_desc.host_visible = false;
	ib_desc.debug_name = "triangle-index-buffer";
	sk_buffer_t ib = api->create_buffer(dev, &ib_desc);

	sk_buffer_desc_t rb_desc;
	memset(&rb_desc, 0, sizeof(rb_desc));
	rb_desc.size = (u64)TRI_TEX_WIDTH * TRI_TEX_HEIGHT * TRI_TEXEL_BYTES;
	rb_desc.usage_flags = (u32)SK_RESOURCE_USAGE_COPY_DEST;
	rb_desc.host_visible = true;
	rb_desc.persistent_mapped = false;
	rb_desc.debug_name = "triangle-readback-buffer";
	sk_buffer_t rb = api->create_buffer(dev, &rb_desc);

	TEST_ASSERT_TRUE(sk_buffer_t_is_valid(vb));
	TEST_ASSERT_TRUE(sk_buffer_t_is_valid(ib));
	TEST_ASSERT_TRUE(sk_buffer_t_is_valid(rb));
	if (!sk_buffer_t_is_valid(vb) || !sk_buffer_t_is_valid(ib) || !sk_buffer_t_is_valid(rb)) {
		api->destroy(dev);
		dxc->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	/* --- Offscreen color target (RGBA8, RENDER_TARGET | COPY_SOURCE) --- */
	sk_texture_desc_t tex_desc;
	memset(&tex_desc, 0, sizeof(tex_desc));
	tex_desc.extent.width = TRI_TEX_WIDTH;
	tex_desc.extent.height = TRI_TEX_HEIGHT;
	tex_desc.extent.depth = 1u;
	tex_desc.mip_levels = 1u;
	tex_desc.array_layers = 1u;
	tex_desc.sample_count = 1u;
	tex_desc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	tex_desc.usage_flags = (u32)SK_RESOURCE_USAGE_RENDER_TARGET | (u32)SK_RESOURCE_USAGE_COPY_SOURCE;
	tex_desc.debug_name = "triangle-color-target";
	sk_texture_t color_tex = api->create_texture(dev, &tex_desc);

	sk_texture_view_desc_t view_desc;
	memset(&view_desc, 0, sizeof(view_desc));
	view_desc.texture = color_tex;
	view_desc.type = SK_TEXTURE_VIEW_TYPE_2D;
	view_desc.base_mip_level = 0u;
	view_desc.mip_level_count = 1u;
	view_desc.base_array_layer = 0u;
	view_desc.array_layer_count = 1u;
	view_desc.debug_name = "triangle-color-view";
	sk_texture_view_t color_view = api->create_texture_view(dev, &view_desc);
	TEST_ASSERT_TRUE(sk_texture_t_is_valid(color_tex));
	TEST_ASSERT_TRUE(sk_texture_view_t_is_valid(color_view));
	if (!sk_texture_t_is_valid(color_tex) || !sk_texture_view_t_is_valid(color_view)) {
		api->destroy(dev);
		dxc->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	/* --- Render pass: clear + store, end in TRANSFER_SRC so the readback
	 * copy needs no extra layout transition --- */
	sk_attachment_desc_t attachment;
	memset(&attachment, 0, sizeof(attachment));
	attachment.initial_state = SK_RESOURCE_STATE_UNDEFINED;
	attachment.final_state = SK_RESOURCE_STATE_COPY_SOURCE;
	attachment.load_op = SK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.store_op = SK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencil_load_op = SK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencil_store_op = SK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.sample_count = 1u;
	attachment.format = SK_PIXEL_FORMAT_RGBA8_UNORM;

	sk_render_pass_desc_t pass_desc;
	memset(&pass_desc, 0, sizeof(pass_desc));
	pass_desc.attachments = &attachment;
	pass_desc.attachment_count = 1u;
	pass_desc.debug_name = "triangle-render-pass";
	sk_render_pass_t pass = api->create_render_pass(dev, &pass_desc);

	sk_framebuffer_desc_t fb_desc;
	memset(&fb_desc, 0, sizeof(fb_desc));
	fb_desc.render_pass = pass;
	fb_desc.attachments = &color_view;
	fb_desc.attachment_count = 1u;
	fb_desc.debug_name = "triangle-framebuffer";
	sk_framebuffer_t fb = api->create_framebuffer(dev, &fb_desc);
	TEST_ASSERT_TRUE(sk_render_pass_t_is_valid(pass));
	TEST_ASSERT_TRUE(sk_framebuffer_t_is_valid(fb));
	if (!sk_render_pass_t_is_valid(pass) || !sk_framebuffer_t_is_valid(fb)) {
		api->destroy(dev);
		dxc->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	/* --- Graphics pipeline (triangle list, no depth, one color RT) --- */
	sk_interface_variable_t inputs[2];
	memset(inputs, 0, sizeof(inputs));
	inputs[0].location = 0u;
	inputs[0].offset = 0u;
	inputs[0].name = "POSITION";
	inputs[0].format = SK_PIXEL_FORMAT_RGBA32_FLOAT;
	inputs[0].size = 16u;
	inputs[1].location = 1u;
	inputs[1].offset = 16u;
	inputs[1].name = "COLOR";
	inputs[1].format = SK_PIXEL_FORMAT_RGBA32_FLOAT;
	inputs[1].size = 16u;

	sk_rasterizer_state_desc_t rasterizer;
	memset(&rasterizer, 0, sizeof(rasterizer));
	rasterizer.polygon_mode = SK_POLYGON_MODE_FILL;
	rasterizer.cull_mode = SK_CULL_MODE_NONE;
	rasterizer.front_face = SK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.line_width = 1.0f;

	sk_depth_stencil_state_desc_t depth_stencil;
	memset(&depth_stencil, 0, sizeof(depth_stencil));
	depth_stencil.depth_test_enable = false;
	depth_stencil.depth_write_enable = false;
	depth_stencil.depth_compare_op = SK_COMPARE_OP_ALWAYS;

	sk_blend_state_desc_t blend;
	memset(&blend, 0, sizeof(blend));
	blend.blend_enable = false;
	blend.color_write_mask = (u32)SK_COLOR_MASK_COMPONENT_ALL;

	sk_graphics_pipeline_desc_t pipe_desc;
	memset(&pipe_desc, 0, sizeof(pipe_desc));
	/* U32_MAX makes the backend use pipeline.stride (the vertex stride). */
	pipe_desc.vertex_input_stride = UINT32_MAX;
	pipe_desc.pipeline.input_variables = inputs;
	pipe_desc.pipeline.input_variable_count = 2u;
	pipe_desc.pipeline.stride = (u32)sizeof(tri_vertex_t);
	pipe_desc.vertex_shader = vs;
	pipe_desc.fragment_shader = ps;
	pipe_desc.topology = SK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pipe_desc.rasterizer_state = rasterizer;
	pipe_desc.depth_stencil_state = depth_stencil;
	pipe_desc.blend_states = &blend;
	pipe_desc.blend_state_count = 1u;
	pipe_desc.render_pass = pass;
	pipe_desc.debug_name = "triangle-pipeline";
	sk_pipeline_t pipeline = api->create_graphics_pipeline(dev, &pipe_desc);
	TEST_ASSERT_TRUE(sk_pipeline_t_is_valid(pipeline));
	if (!sk_pipeline_t_is_valid(pipeline)) {
		api->destroy(dev);
		dxc->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	/* --- Command buffer: uploads → barriers → clear → draw → readback --- */
	sk_command_buffer_desc_t cb_desc;
	memset(&cb_desc, 0, sizeof(cb_desc));
	cb_desc.level = SK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cb_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	cb_desc.debug_name = "triangle-command-buffer";
	sk_command_buffer_t cmd = api->create_command_buffer(dev, &cb_desc);
	TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(cmd));
	if (!sk_command_buffer_t_is_valid(cmd)) {
		api->destroy(dev);
		dxc->shutdown();
		sk_app_shutdown(ctx);
		return;
	}

	sk_command_buffer_begin_info_t begin_info;
	memset(&begin_info, 0, sizeof(begin_info));
	begin_info.usage_flags = (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
	TEST_ASSERT_EQUAL_INT(0, api->begin_command_buffer(dev, cmd, &begin_info));

	api->update_buffer(dev, cmd, vb, 0u, sizeof(tri_vertices), tri_vertices);
	api->update_buffer(dev, cmd, ib, 0u, sizeof(tri_indices), tri_indices);
	/* Make the update-buffer writes visible to vertex/index fetches. */
	api->memory_barrier(dev, cmd);

	sk_viewport_t viewport = {0.0f, 0.0f, (f32)TRI_TEX_WIDTH, (f32)TRI_TEX_HEIGHT, 0.0f, 1.0f};
	api->set_viewport(dev, cmd, 0u, &viewport, 1u);
	sk_rect2d_t scissor = {0, 0, TRI_TEX_WIDTH, TRI_TEX_HEIGHT};
	api->set_scissor(dev, cmd, 0u, &scissor, 1u);

	sk_clear_values_t clear;
	memset(&clear, 0, sizeof(clear));
	clear.color.r = TRI_CLEAR_R;
	clear.color.g = TRI_CLEAR_G;
	clear.color.b = TRI_CLEAR_B;
	clear.color.a = 1.0f;

	sk_begin_render_pass_info_t rp_info;
	memset(&rp_info, 0, sizeof(rp_info));
	rp_info.render_pass = pass;
	rp_info.framebuffer = fb;
	rp_info.clear_values = &clear;
	api->begin_render_pass(dev, cmd, &rp_info);

	api->bind_pipeline(dev, cmd, SK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	const sk_buffer_t vbs[1] = {vb};
	const u64 vb_offsets[1] = {0u};
	api->bind_vertex_buffer(dev, cmd, 0u, vbs, vb_offsets, 1u);
	api->bind_index_buffer(dev, cmd, ib, 0u, SK_INDEX_TYPE_UINT32);
	api->draw_indexed(dev, cmd, 3u, 1u, 0u, 0, 0u);
	api->end_render_pass(dev, cmd);

	/* Make the color-attachment writes visible before the readback copy. */
	api->memory_barrier(dev, cmd);

	sk_buffer_texture_copy_t readback;
	memset(&readback, 0, sizeof(readback));
	readback.buffer = rb;
	readback.buffer_offset = 0u;
	readback.texture = color_tex;
	readback.mip_level = 0u;
	readback.array_layer = 0u;
	readback.texture_extent.width = TRI_TEX_WIDTH;
	readback.texture_extent.height = TRI_TEX_HEIGHT;
	readback.texture_extent.depth = 1u;
	api->copy_texture_to_buffer(dev, cmd, &readback);

	api->end_command_buffer(dev, cmd);

	/* --- Submit with fence and wait --- */
	sk_queue_desc_t q_desc;
	memset(&q_desc, 0, sizeof(q_desc));
	q_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	sk_queue_t queue = api->create_queue(dev, &q_desc);

	sk_fence_desc_t fence_desc;
	memset(&fence_desc, 0, sizeof(fence_desc));
	fence_desc.signaled = false;
	fence_desc.debug_name = "triangle-fence";
	sk_fence_t fence = api->create_fence(dev, &fence_desc);
	TEST_ASSERT_TRUE(sk_queue_t_is_valid(queue));
	TEST_ASSERT_TRUE(sk_fence_t_is_valid(fence));

	if (sk_queue_t_is_valid(queue) && sk_fence_t_is_valid(fence)) {
		sk_submit_info_t submit_info;
		memset(&submit_info, 0, sizeof(submit_info));
		submit_info.command_buffers = &cmd;
		submit_info.command_buffer_count = 1u;
		submit_info.signal_fence = fence;
		TEST_ASSERT_EQUAL_INT(0, api->submit(dev, queue, &submit_info));
		TEST_ASSERT_EQUAL_INT(0, api->wait_fences(dev, &fence, 1u, true, UINT64_MAX));
	}

	/* --- Readback, artifact, asserts --- */
	void_ptr_t mapped = api->buffer_map(dev, rb);
	TEST_ASSERT_NOT_NULL_MESSAGE(mapped, "readback buffer must map after fence wait");
	if (mapped != NULL) {
		const u8* pixels = (const u8*)mapped;

		tri_write_ppm(SK_INTEGRATION_ARTIFACT_DIR, pixels, TRI_TEX_WIDTH, TRI_TEX_HEIGHT);

		/* Outside the triangle (top-left): clear color. */
		const u8* outside = &pixels[((size_t)TRI_PIXEL_OUTSIDE_Y * TRI_TEX_WIDTH + TRI_PIXEL_OUTSIDE_X) * TRI_TEXEL_BYTES];
		const u32 clear_r = (u32)(TRI_CLEAR_R * 255.0f);
		const u32 clear_g = (u32)(TRI_CLEAR_G * 255.0f);
		const u32 clear_b = (u32)(TRI_CLEAR_B * 255.0f);
		TEST_ASSERT_UINT_WITHIN(16u, clear_r, outside[0]);
		TEST_ASSERT_UINT_WITHIN(16u, clear_g, outside[1]);
		TEST_ASSERT_UINT_WITHIN(16u, clear_b, outside[2]);

		/* Center of the triangle: interpolated (1/3,1/3,1/3)-ish blend of the
		 * three vertex colors ≈ (64, 128, 64); must not equal the clear color. */
		const u8* inside = &pixels[((size_t)TRI_PIXEL_INSIDE_Y * TRI_TEX_WIDTH + TRI_PIXEL_INSIDE_X) * TRI_TEXEL_BYTES];
		TEST_ASSERT_UINT_WITHIN(24u, 64u, inside[0]);
		TEST_ASSERT_UINT_WITHIN(24u, 128u, inside[1]);
		TEST_ASSERT_UINT_WITHIN(24u, 64u, inside[2]);
	}

	/* --- Cleanup --- */
	api->buffer_unmap(dev, rb);
	if (sk_queue_t_is_valid(queue)) {
		api->destroy_queue(dev, queue);
	}
	if (sk_fence_t_is_valid(fence)) {
		api->destroy_fence(dev, fence);
	}
	api->destroy_command_buffer(dev, cmd);
	api->destroy_pipeline(dev, pipeline);
	api->destroy_framebuffer(dev, fb);
	api->destroy_render_pass(dev, pass);
	api->destroy_texture_view(dev, color_view);
	api->destroy_texture(dev, color_tex);
	api->destroy_buffer(dev, vb);
	api->destroy_buffer(dev, ib);
	api->destroy_buffer(dev, rb);
	api->destroy_shader(dev, vs);
	api->destroy_shader(dev, ps);
	dxc->shutdown();
	api->destroy(dev);
	sk_app_shutdown(ctx);
}

#endif /* SK_TESTS */
