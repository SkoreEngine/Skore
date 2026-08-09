/*
 * test_render_device.c — Test-double RHI (Render Hardware Interface) layer.
 *
 * Registers a complete sk_render_device_api_t table under
 * SK_RENDER_DEVICE_API_TYPE_ID with pure stubs. No GPU backend here: the
 * device handle and every resource handle returned are zero (invalid), and
 * status-returning calls succeed. Used by host/tests to exercise the app
 * registry and render device call sites without a real GPU.
 */

#include "app.h"
#include "common.h"
#include "render_device.h"

#include <stddef.h>

/* Registers the static API table (called from plugin_entry_point). */
void sk_test_render_device_init(sk_app_context_t* context, const sk_app_api_t* app_api);

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static sk_render_device_t sk_trd_init(void_ptr_t context, const sk_device_init_desc_t* desc);
static void sk_trd_destroy(sk_render_device_t dev);
static i32 sk_trd_wait_idle(sk_render_device_t dev);

static u32 sk_trd_get_adapter_count(sk_render_device_t dev);
static sk_adapter_t sk_trd_get_adapter(sk_render_device_t dev, u32 index);
static i32 sk_trd_select_adapter(sk_render_device_t dev, sk_adapter_t adapter);
static u32 sk_trd_get_adapter_score(sk_render_device_t dev, sk_adapter_t adapter);
static const_chr_t sk_trd_get_adapter_name(sk_render_device_t dev, sk_adapter_t adapter);
static sk_device_properties_t sk_trd_get_properties(sk_render_device_t dev);
static sk_device_features_t sk_trd_get_features(sk_render_device_t dev);
static sk_graphics_api_t sk_trd_get_api(sk_render_device_t dev);
static u32 sk_trd_get_memory_budgets(sk_render_device_t dev, sk_memory_heap_budget_t* out_budgets, u32 max_count);

static sk_buffer_t sk_trd_create_buffer(sk_render_device_t dev, const sk_buffer_desc_t* desc);
static void sk_trd_destroy_buffer(sk_render_device_t dev, sk_buffer_t buf);
static void* sk_trd_buffer_map(sk_render_device_t dev, sk_buffer_t buf);
static void sk_trd_buffer_unmap(sk_render_device_t dev, sk_buffer_t buf);
static sk_buffer_desc_t sk_trd_get_buffer_desc(sk_render_device_t dev, sk_buffer_t buf);
static void_ptr_t sk_trd_get_buffer_mapped_data(sk_render_device_t dev, sk_buffer_t buf);

static sk_texture_t sk_trd_create_texture(sk_render_device_t dev, const sk_texture_desc_t* desc);
static void sk_trd_destroy_texture(sk_render_device_t dev, sk_texture_t tex);
static sk_texture_view_t sk_trd_create_texture_view(sk_render_device_t dev, const sk_texture_view_desc_t* desc);
static void sk_trd_destroy_texture_view(sk_render_device_t dev, sk_texture_view_t view);
static sk_sampler_t sk_trd_create_sampler(sk_render_device_t dev, const sk_sampler_desc_t* desc);
static void sk_trd_destroy_sampler(sk_render_device_t dev, sk_sampler_t sampler);
static sk_texture_desc_t sk_trd_get_texture_desc(sk_render_device_t dev, sk_texture_t tex);
static sk_texture_view_desc_t sk_trd_get_texture_view_desc(sk_render_device_t dev, sk_texture_view_t view);
static sk_sampler_desc_t sk_trd_get_sampler_desc(sk_render_device_t dev, sk_sampler_t sampler);

static sk_memory_t sk_trd_create_memory(sk_render_device_t dev, u64 size, u64 alignment, u32 memory_type_bits);
static void sk_trd_destroy_memory(sk_render_device_t dev, sk_memory_t mem);
static sk_texture_t sk_trd_create_aliased_texture(sk_render_device_t dev, const sk_texture_desc_t* desc, sk_memory_t mem, u64 offset);
static u64 sk_trd_get_memory_size(sk_render_device_t dev, sk_memory_t mem);

static sk_shader_t sk_trd_create_shader(sk_render_device_t dev, const_chr_t src, u32 src_size, u32 shader_stage);
static void sk_trd_destroy_shader(sk_render_device_t dev, sk_shader_t shdr);

static sk_pipeline_t sk_trd_create_graphics_pipeline(sk_render_device_t dev, const sk_graphics_pipeline_desc_t* desc);
static sk_pipeline_t sk_trd_create_compute_pipeline(sk_render_device_t dev, const sk_compute_pipeline_desc_t* desc);
static sk_pipeline_t sk_trd_create_ray_tracing_pipeline(sk_render_device_t dev, const sk_ray_tracing_pipeline_desc_t* desc);
static void sk_trd_destroy_pipeline(sk_render_device_t dev, sk_pipeline_t pipeline);
static sk_pipeline_desc_t sk_trd_get_pipeline_desc(sk_render_device_t dev, sk_pipeline_t pipeline);
static sk_pipeline_bind_point_t sk_trd_get_pipeline_bind_point(sk_render_device_t dev, sk_pipeline_t pipeline);

static sk_descriptor_set_t sk_trd_create_descriptor_set(sk_render_device_t dev, const sk_descriptor_set_desc_t* desc);
static void sk_trd_update_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set, const sk_descriptor_write_t* writes, u32 write_count);
static void sk_trd_destroy_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set);
static sk_descriptor_set_desc_t sk_trd_get_descriptor_set_desc(sk_render_device_t dev, sk_descriptor_set_t set);

static sk_render_pass_t sk_trd_create_render_pass(sk_render_device_t dev, const sk_render_pass_desc_t* desc);
static void sk_trd_destroy_render_pass(sk_render_device_t dev, sk_render_pass_t pass);
static sk_framebuffer_t sk_trd_create_framebuffer(sk_render_device_t dev, const sk_framebuffer_desc_t* desc);
static void sk_trd_destroy_framebuffer(sk_render_device_t dev, sk_framebuffer_t fb);
static sk_render_pass_desc_t sk_trd_get_render_pass_desc(sk_render_device_t dev, sk_render_pass_t pass);
static sk_framebuffer_desc_t sk_trd_get_framebuffer_desc(sk_render_device_t dev, sk_framebuffer_t fb);
static sk_extent3d_t sk_trd_get_framebuffer_extent(sk_render_device_t dev, sk_framebuffer_t fb);

static sk_swapchain_t sk_trd_create_swapchain(sk_render_device_t dev, const sk_swapchain_desc_t* desc);
static void sk_trd_destroy_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain);
static i32 sk_trd_resize_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain, u32 width, u32 height);
static sk_device_result_t sk_trd_acquire_next_image(sk_render_device_t dev, const sk_acquire_info_t* info, u32* out_image_index);
static sk_texture_t sk_trd_get_swapchain_image(sk_render_device_t dev, sk_swapchain_t swapchain, u32 image_index);
static sk_extent3d_t sk_trd_get_swapchain_extent(sk_render_device_t dev, sk_swapchain_t swapchain);
static sk_pixel_format_t sk_trd_get_swapchain_format(sk_render_device_t dev, sk_swapchain_t swapchain);
static u32 sk_trd_get_swapchain_image_count(sk_render_device_t dev, sk_swapchain_t swapchain);
static u32 sk_trd_get_swapchain_current_image_index(sk_render_device_t dev, sk_swapchain_t swapchain);
static u32 sk_trd_get_swapchain_textures(sk_render_device_t dev, sk_swapchain_t swapchain, sk_texture_t* out_textures, u32 max_count);
static sk_device_result_t sk_trd_present(sk_render_device_t dev, sk_queue_t queue, const sk_present_info_t* info);

static sk_fence_t sk_trd_create_fence(sk_render_device_t dev, const sk_fence_desc_t* desc);
static void sk_trd_destroy_fence(sk_render_device_t dev, sk_fence_t fence);
static i32 sk_trd_wait_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count, bool wait_all, u64 timeout_ns);
static void sk_trd_reset_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count);
static sk_semaphore_t sk_trd_create_semaphore(sk_render_device_t dev, const sk_semaphore_desc_t* desc);
static void sk_trd_destroy_semaphore(sk_render_device_t dev, sk_semaphore_t semaphore);

static sk_command_buffer_t sk_trd_create_command_buffer(sk_render_device_t dev, const sk_command_buffer_desc_t* desc);
static void sk_trd_destroy_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd);
static i32 sk_trd_begin_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_begin_info_t* info);
static void sk_trd_end_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd);
static void sk_trd_reset_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd);
static void sk_trd_execute_commands(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_t* secondary_cmds, u32 secondary_count);

static void sk_trd_set_viewport(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_viewport, const sk_viewport_t* viewports, u32 viewport_count);
static void sk_trd_set_scissor(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_scissor, const sk_rect2d_t* scissors, u32 scissor_count);
static void sk_trd_set_blend_constants(sk_render_device_t dev, sk_command_buffer_t cmd, f32 r, f32 g, f32 b, f32 a);
static void sk_trd_set_stencil_reference(sk_render_device_t dev, sk_command_buffer_t cmd, u32 reference);
static void sk_trd_set_depth_bias(sk_render_device_t dev, sk_command_buffer_t cmd, f32 constant_factor, f32 clamp, f32 slope_factor);
static void sk_trd_set_depth_bounds(sk_render_device_t dev, sk_command_buffer_t cmd, f32 min_depth, f32 max_depth);
static void sk_trd_bind_pipeline(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline);
static void sk_trd_bind_descriptor_set(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline, u32 set_index,
									   sk_descriptor_set_t desc_set, const u32* dynamic_offsets, u32 offset_count);
static void sk_trd_bind_vertex_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_binding, const sk_buffer_t* buffers, const u64* offsets, u32 buffer_count);
static void sk_trd_bind_index_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, sk_index_type_t index_type);
static void sk_trd_push_constants(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_t pipeline, u32 shader_stages, u32 offset, u32 size, const void* data);

static void sk_trd_draw(sk_render_device_t dev, sk_command_buffer_t cmd, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance);
static void sk_trd_draw_indexed(sk_render_device_t dev, sk_command_buffer_t cmd, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance);
static void sk_trd_draw_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride);
static void sk_trd_draw_indexed_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride);
static void sk_trd_draw_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
									   u32 max_draw_count, u32 stride);
static void sk_trd_draw_indexed_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
											   u32 max_draw_count, u32 stride);
static void sk_trd_dispatch(sk_render_device_t dev, sk_command_buffer_t cmd, u32 group_count_x, u32 group_count_y, u32 group_count_z);
static void sk_trd_dispatch_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset);
static void sk_trd_trace_rays(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_trace_rays_info_t* info);

static void sk_trd_begin_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_begin_render_pass_info_t* info);
static void sk_trd_end_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd);
static void sk_trd_clear_attachments(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_clear_attachment_t* attachments, u32 attachment_count);

static void sk_trd_copy_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t src, sk_buffer_t dst, u64 size, u64 src_offset, u64 dst_offset);
static void sk_trd_copy_buffer_to_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info);
static void sk_trd_copy_texture_to_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info);
static void sk_trd_copy_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_copy_t* copy_info);
static void sk_trd_blit_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_blit_t* blit_info);
static void sk_trd_resolve_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_resolve_t* resolve_info);
static void sk_trd_update_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, const void* data);
static void sk_trd_fill_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, u32 data);
static void sk_trd_clear_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, const sk_clear_values_t* clear, u32 base_mip_level, u32 mip_level_count,
								 u32 base_array_layer, u32 array_layer_count);

static void sk_trd_resource_barrier_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, sk_resource_state_t old_state, sk_resource_state_t new_state,
										   u32 src_scope, u32 dst_scope);
static void sk_trd_resource_barrier_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, sk_resource_state_t old_state, sk_resource_state_t new_state,
											u32 base_mip_level, u32 mip_level_count, u32 base_array_layer, u32 array_layer_count, u32 src_scope, u32 dst_scope);
static void sk_trd_resource_barrier_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, sk_resource_state_t old_state, sk_resource_state_t new_state,
													u32 src_scope, u32 dst_scope);
static void sk_trd_resource_barrier_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, sk_resource_state_t old_state, sk_resource_state_t new_state,
												 u32 src_scope, u32 dst_scope);
static void sk_trd_memory_barrier(sk_render_device_t dev, sk_command_buffer_t cmd);

static void sk_trd_begin_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a);
static void sk_trd_end_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd);
static void sk_trd_insert_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a);

static sk_query_pool_t sk_trd_create_query_pool(sk_render_device_t dev, const sk_query_pool_desc_t* desc);
static void sk_trd_destroy_query_pool(sk_render_device_t dev, sk_query_pool_t pool);
static void sk_trd_reset_query_pool(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count);
static void sk_trd_begin_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
static void sk_trd_end_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
static void sk_trd_write_timestamp(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
static void sk_trd_copy_query_pool_results(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count, sk_buffer_t dst_buffer,
										   u64 dst_offset, u64 stride);
static i32 sk_trd_get_query_pool_results(sk_render_device_t dev, sk_query_pool_t pool, u32 first_query, u32 query_count, void* data, u64 data_size, u64 stride, bool wait);

static sk_blas_t sk_trd_create_bottom_level_as(sk_render_device_t dev, const sk_blas_desc_t* desc);
static void sk_trd_destroy_bottom_level_as(sk_render_device_t dev, sk_blas_t blas);
static sk_tlas_t sk_trd_create_top_level_as(sk_render_device_t dev, const sk_tlas_desc_t* desc);
static void sk_trd_destroy_top_level_as(sk_render_device_t dev, sk_tlas_t tlas);
static sk_as_build_sizes_t sk_trd_get_blas_build_sizes(sk_render_device_t dev, const sk_blas_desc_t* desc);
static sk_as_build_sizes_t sk_trd_get_tlas_build_sizes(sk_render_device_t dev, const sk_tlas_desc_t* desc);
static void sk_trd_build_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, const sk_blas_desc_t* desc, const sk_as_build_info_t* build_info);
static void sk_trd_build_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, const sk_tlas_desc_t* desc, const sk_as_build_info_t* build_info);
static bool sk_trd_is_bottom_level_as_compacted(sk_render_device_t dev, sk_blas_t blas);
static u64 sk_trd_get_bottom_level_as_compacted_size(sk_render_device_t dev, sk_blas_t blas);
static sk_blas_desc_t sk_trd_get_blas_desc(sk_render_device_t dev, sk_blas_t blas);
static sk_tlas_desc_t sk_trd_get_tlas_desc(sk_render_device_t dev, sk_tlas_t tlas);
static void sk_trd_copy_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t src, sk_blas_t dst, bool compress);
static void sk_trd_copy_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t src, sk_tlas_t dst, bool compress);
static bool sk_trd_update_top_level_as_instances(sk_render_device_t dev, sk_tlas_t tlas, const sk_as_instance_desc_t* instances, u32 instance_count);
static void sk_trd_update_top_level_as_instance(sk_render_device_t dev, sk_tlas_t tlas, u32 index, const sk_as_instance_desc_t* instance);
static void sk_trd_set_top_level_as_instance_count(sk_render_device_t dev, sk_tlas_t tlas, u32 count);

static sk_queue_t sk_trd_create_queue(sk_render_device_t dev, const sk_queue_desc_t* desc);
static void sk_trd_destroy_queue(sk_render_device_t dev, sk_queue_t queue);
static void sk_trd_submit_command_buffer(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd);
static i32 sk_trd_submit(sk_render_device_t dev, sk_queue_t queue, const sk_submit_info_t* info);
static i32 sk_trd_submit_and_wait(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd);
static i32 sk_trd_queue_wait_idle(sk_render_device_t dev, sk_queue_t queue);

static sk_texture_memory_requirements_t sk_trd_get_texture_memory_requirements(sk_render_device_t dev, const sk_texture_desc_t* desc);
static sk_buffer_memory_requirements_t sk_trd_get_buffer_memory_requirements(sk_render_device_t dev, const sk_buffer_desc_t* desc);

/* ------------------------------------------------------------------ */
/* API table                                                           */
/* ------------------------------------------------------------------ */

static const sk_render_device_api_t test_render_device_api = {
	/* Device lifecycle */
	sk_trd_init,
	sk_trd_destroy,
	sk_trd_wait_idle,

	/* Adapter / device queries */
	sk_trd_get_adapter_count,
	sk_trd_get_adapter,
	sk_trd_select_adapter,
	sk_trd_get_adapter_score,
	sk_trd_get_adapter_name,
	sk_trd_get_properties,
	sk_trd_get_features,
	sk_trd_get_api,
	sk_trd_get_memory_budgets,

	/* Buffer */
	sk_trd_create_buffer,
	sk_trd_destroy_buffer,
	sk_trd_buffer_map,
	sk_trd_buffer_unmap,
	sk_trd_get_buffer_desc,
	sk_trd_get_buffer_mapped_data,

	/* Texture */
	sk_trd_create_texture,
	sk_trd_destroy_texture,
	sk_trd_create_texture_view,
	sk_trd_destroy_texture_view,
	sk_trd_create_sampler,
	sk_trd_destroy_sampler,
	sk_trd_get_texture_desc,
	sk_trd_get_texture_view_desc,
	sk_trd_get_sampler_desc,

	/* Memory */
	sk_trd_create_memory,
	sk_trd_destroy_memory,
	sk_trd_create_aliased_texture,
	sk_trd_get_memory_size,

	/* Shader */
	sk_trd_create_shader,
	sk_trd_destroy_shader,

	/* Pipeline */
	sk_trd_create_graphics_pipeline,
	sk_trd_create_compute_pipeline,
	sk_trd_create_ray_tracing_pipeline,
	sk_trd_destroy_pipeline,
	sk_trd_get_pipeline_desc,
	sk_trd_get_pipeline_bind_point,

	/* Descriptor set */
	sk_trd_create_descriptor_set,
	sk_trd_update_descriptor_set,
	sk_trd_destroy_descriptor_set,
	sk_trd_get_descriptor_set_desc,

	/* Render pass / framebuffer */
	sk_trd_create_render_pass,
	sk_trd_destroy_render_pass,
	sk_trd_create_framebuffer,
	sk_trd_destroy_framebuffer,
	sk_trd_get_render_pass_desc,
	sk_trd_get_framebuffer_desc,
	sk_trd_get_framebuffer_extent,

	/* Swapchain */
	sk_trd_create_swapchain,
	sk_trd_destroy_swapchain,
	sk_trd_resize_swapchain,
	sk_trd_acquire_next_image,
	sk_trd_get_swapchain_image,
	sk_trd_get_swapchain_extent,
	sk_trd_get_swapchain_format,
	sk_trd_get_swapchain_image_count,
	sk_trd_get_swapchain_current_image_index,
	sk_trd_get_swapchain_textures,
	sk_trd_present,

	/* Sync */
	sk_trd_create_fence,
	sk_trd_destroy_fence,
	sk_trd_wait_fences,
	sk_trd_reset_fences,
	sk_trd_create_semaphore,
	sk_trd_destroy_semaphore,

	/* Command buffer lifecycle */
	sk_trd_create_command_buffer,
	sk_trd_destroy_command_buffer,
	sk_trd_begin_command_buffer,
	sk_trd_end_command_buffer,
	sk_trd_reset_command_buffer,
	sk_trd_execute_commands,

	/* State */
	sk_trd_set_viewport,
	sk_trd_set_scissor,
	sk_trd_set_blend_constants,
	sk_trd_set_stencil_reference,
	sk_trd_set_depth_bias,
	sk_trd_set_depth_bounds,
	sk_trd_bind_pipeline,
	sk_trd_bind_descriptor_set,
	sk_trd_bind_vertex_buffer,
	sk_trd_bind_index_buffer,
	sk_trd_push_constants,

	/* Draw / dispatch */
	sk_trd_draw,
	sk_trd_draw_indexed,
	sk_trd_draw_indirect,
	sk_trd_draw_indexed_indirect,
	sk_trd_draw_indirect_count,
	sk_trd_draw_indexed_indirect_count,
	sk_trd_dispatch,
	sk_trd_dispatch_indirect,
	sk_trd_trace_rays,

	/* Render pass cmds */
	sk_trd_begin_render_pass,
	sk_trd_end_render_pass,
	sk_trd_clear_attachments,

	/* Transfer */
	sk_trd_copy_buffer,
	sk_trd_copy_buffer_to_texture,
	sk_trd_copy_texture_to_buffer,
	sk_trd_copy_texture,
	sk_trd_blit_texture,
	sk_trd_resolve_texture,
	sk_trd_update_buffer,
	sk_trd_fill_buffer,
	sk_trd_clear_texture,

	/* Barriers */
	sk_trd_resource_barrier_buffer,
	sk_trd_resource_barrier_texture,
	sk_trd_resource_barrier_bottom_level_as,
	sk_trd_resource_barrier_top_level_as,
	sk_trd_memory_barrier,

	/* Debug labels */
	sk_trd_begin_debug_label,
	sk_trd_end_debug_label,
	sk_trd_insert_debug_label,

	/* Queries */
	sk_trd_create_query_pool,
	sk_trd_destroy_query_pool,
	sk_trd_reset_query_pool,
	sk_trd_begin_query,
	sk_trd_end_query,
	sk_trd_write_timestamp,
	sk_trd_copy_query_pool_results,
	sk_trd_get_query_pool_results,

	/* Acceleration structures */
	sk_trd_create_bottom_level_as,
	sk_trd_destroy_bottom_level_as,
	sk_trd_create_top_level_as,
	sk_trd_destroy_top_level_as,
	sk_trd_get_blas_build_sizes,
	sk_trd_get_tlas_build_sizes,
	sk_trd_build_bottom_level_as,
	sk_trd_build_top_level_as,
	sk_trd_is_bottom_level_as_compacted,
	sk_trd_get_bottom_level_as_compacted_size,
	sk_trd_get_blas_desc,
	sk_trd_get_tlas_desc,
	sk_trd_copy_bottom_level_as,
	sk_trd_copy_top_level_as,
	sk_trd_update_top_level_as_instances,
	sk_trd_update_top_level_as_instance,
	sk_trd_set_top_level_as_instance_count,

	/* Queue */
	sk_trd_create_queue,
	sk_trd_destroy_queue,
	sk_trd_submit_command_buffer,
	sk_trd_submit,
	sk_trd_submit_and_wait,
	sk_trd_queue_wait_idle,

	/* Device query */
	sk_trd_get_texture_memory_requirements,
	sk_trd_get_buffer_memory_requirements,
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void sk_test_render_device_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_RENDER_DEVICE_API_TYPE_ID, &test_render_device_api);
}

/* ------------------------------------------------------------------ */
/* Stub implementations (test double: zero handles, success statuses)  */
/* ------------------------------------------------------------------ */

static sk_render_device_t sk_trd_init(void_ptr_t context, const sk_device_init_desc_t* desc) {
	(void)context;
	(void)desc;
	return sk_render_device_t_zero();
}
static void sk_trd_destroy(sk_render_device_t dev) {
	(void)dev;
}
static i32 sk_trd_wait_idle(sk_render_device_t dev) {
	(void)dev;
	return 0;
}

static u32 sk_trd_get_adapter_count(sk_render_device_t dev) {
	(void)dev;
	return 0u;
}
static sk_adapter_t sk_trd_get_adapter(sk_render_device_t dev, u32 index) {
	(void)dev;
	(void)index;
	return sk_adapter_t_zero();
}
static i32 sk_trd_select_adapter(sk_render_device_t dev, sk_adapter_t adapter) {
	(void)dev;
	(void)adapter;
	return 0;
}
static u32 sk_trd_get_adapter_score(sk_render_device_t dev, sk_adapter_t adapter) {
	(void)dev;
	(void)adapter;
	return 0u;
}
static const_chr_t sk_trd_get_adapter_name(sk_render_device_t dev, sk_adapter_t adapter) {
	(void)dev;
	(void)adapter;
	return "";
}
static sk_device_properties_t sk_trd_get_properties(sk_render_device_t dev) {
	(void)dev;
	sk_device_properties_t props = {0};
	return props;
}
static sk_device_features_t sk_trd_get_features(sk_render_device_t dev) {
	(void)dev;
	sk_device_features_t features = {0};
	return features;
}
static sk_graphics_api_t sk_trd_get_api(sk_render_device_t dev) {
	(void)dev;
	return SK_GRAPHICS_API_NONE;
}
static u32 sk_trd_get_memory_budgets(sk_render_device_t dev, sk_memory_heap_budget_t* out_budgets, u32 max_count) {
	(void)dev;
	(void)out_budgets;
	(void)max_count;
	return 0u;
}

static sk_buffer_t sk_trd_create_buffer(sk_render_device_t dev, const sk_buffer_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_buffer_t_zero();
}
static void sk_trd_destroy_buffer(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	(void)buf;
}
static void* sk_trd_buffer_map(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	(void)buf;
	return NULL;
}
static void sk_trd_buffer_unmap(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	(void)buf;
}
static sk_buffer_desc_t sk_trd_get_buffer_desc(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	(void)buf;
	sk_buffer_desc_t desc = {0};
	return desc;
}
static void_ptr_t sk_trd_get_buffer_mapped_data(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	(void)buf;
	return NULL;
}

static sk_texture_t sk_trd_create_texture(sk_render_device_t dev, const sk_texture_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_texture_t_zero();
}
static void sk_trd_destroy_texture(sk_render_device_t dev, sk_texture_t tex) {
	(void)dev;
	(void)tex;
}
static sk_texture_view_t sk_trd_create_texture_view(sk_render_device_t dev, const sk_texture_view_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_texture_view_t_zero();
}
static void sk_trd_destroy_texture_view(sk_render_device_t dev, sk_texture_view_t view) {
	(void)dev;
	(void)view;
}
static sk_sampler_t sk_trd_create_sampler(sk_render_device_t dev, const sk_sampler_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_sampler_t_zero();
}
static void sk_trd_destroy_sampler(sk_render_device_t dev, sk_sampler_t sampler) {
	(void)dev;
	(void)sampler;
}
static sk_texture_desc_t sk_trd_get_texture_desc(sk_render_device_t dev, sk_texture_t tex) {
	(void)dev;
	(void)tex;
	sk_texture_desc_t desc = {0};
	return desc;
}
static sk_texture_view_desc_t sk_trd_get_texture_view_desc(sk_render_device_t dev, sk_texture_view_t view) {
	(void)dev;
	(void)view;
	sk_texture_view_desc_t desc = {0};
	return desc;
}
static sk_sampler_desc_t sk_trd_get_sampler_desc(sk_render_device_t dev, sk_sampler_t sampler) {
	(void)dev;
	(void)sampler;
	sk_sampler_desc_t desc = {0};
	return desc;
}

static sk_memory_t sk_trd_create_memory(sk_render_device_t dev, u64 size, u64 alignment, u32 memory_type_bits) {
	(void)dev;
	(void)size;
	(void)alignment;
	(void)memory_type_bits;
	return sk_memory_t_zero();
}
static void sk_trd_destroy_memory(sk_render_device_t dev, sk_memory_t mem) {
	(void)dev;
	(void)mem;
}
static sk_texture_t sk_trd_create_aliased_texture(sk_render_device_t dev, const sk_texture_desc_t* desc, sk_memory_t mem, u64 offset) {
	(void)dev;
	(void)desc;
	(void)mem;
	(void)offset;
	return sk_texture_t_zero();
}
static u64 sk_trd_get_memory_size(sk_render_device_t dev, sk_memory_t mem) {
	(void)dev;
	(void)mem;
	return 0ull;
}

static sk_shader_t sk_trd_create_shader(sk_render_device_t dev, const_chr_t src, u32 src_size, u32 shader_stage) {
	(void)dev;
	(void)src;
	(void)src_size;
	(void)shader_stage;
	return sk_shader_t_zero();
}
static void sk_trd_destroy_shader(sk_render_device_t dev, sk_shader_t shdr) {
	(void)dev;
	(void)shdr;
}

static sk_pipeline_t sk_trd_create_graphics_pipeline(sk_render_device_t dev, const sk_graphics_pipeline_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_pipeline_t_zero();
}
static sk_pipeline_t sk_trd_create_compute_pipeline(sk_render_device_t dev, const sk_compute_pipeline_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_pipeline_t_zero();
}
static sk_pipeline_t sk_trd_create_ray_tracing_pipeline(sk_render_device_t dev, const sk_ray_tracing_pipeline_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_pipeline_t_zero();
}
static void sk_trd_destroy_pipeline(sk_render_device_t dev, sk_pipeline_t pipeline) {
	(void)dev;
	(void)pipeline;
}
static sk_pipeline_desc_t sk_trd_get_pipeline_desc(sk_render_device_t dev, sk_pipeline_t pipeline) {
	(void)dev;
	(void)pipeline;
	sk_pipeline_desc_t desc = {0};
	return desc;
}
static sk_pipeline_bind_point_t sk_trd_get_pipeline_bind_point(sk_render_device_t dev, sk_pipeline_t pipeline) {
	(void)dev;
	(void)pipeline;
	return SK_PIPELINE_BIND_POINT_GRAPHICS;
}

static sk_descriptor_set_t sk_trd_create_descriptor_set(sk_render_device_t dev, const sk_descriptor_set_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_descriptor_set_t_zero();
}
static void sk_trd_update_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set, const sk_descriptor_write_t* writes, u32 write_count) {
	(void)dev;
	(void)set;
	(void)writes;
	(void)write_count;
}
static void sk_trd_destroy_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set) {
	(void)dev;
	(void)set;
}
static sk_descriptor_set_desc_t sk_trd_get_descriptor_set_desc(sk_render_device_t dev, sk_descriptor_set_t set) {
	(void)dev;
	(void)set;
	sk_descriptor_set_desc_t desc = {0};
	return desc;
}

static sk_render_pass_t sk_trd_create_render_pass(sk_render_device_t dev, const sk_render_pass_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_render_pass_t_zero();
}
static void sk_trd_destroy_render_pass(sk_render_device_t dev, sk_render_pass_t pass) {
	(void)dev;
	(void)pass;
}
static sk_framebuffer_t sk_trd_create_framebuffer(sk_render_device_t dev, const sk_framebuffer_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_framebuffer_t_zero();
}
static void sk_trd_destroy_framebuffer(sk_render_device_t dev, sk_framebuffer_t fb) {
	(void)dev;
	(void)fb;
}
static sk_render_pass_desc_t sk_trd_get_render_pass_desc(sk_render_device_t dev, sk_render_pass_t pass) {
	(void)dev;
	(void)pass;
	sk_render_pass_desc_t desc = {0};
	return desc;
}
static sk_framebuffer_desc_t sk_trd_get_framebuffer_desc(sk_render_device_t dev, sk_framebuffer_t fb) {
	(void)dev;
	(void)fb;
	sk_framebuffer_desc_t desc = {0};
	return desc;
}
static sk_extent3d_t sk_trd_get_framebuffer_extent(sk_render_device_t dev, sk_framebuffer_t fb) {
	(void)dev;
	(void)fb;
	sk_extent3d_t extent = {0};
	return extent;
}

static sk_swapchain_t sk_trd_create_swapchain(sk_render_device_t dev, const sk_swapchain_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_swapchain_t_zero();
}
static void sk_trd_destroy_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain) {
	(void)dev;
	(void)swapchain;
}
static i32 sk_trd_resize_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain, u32 width, u32 height) {
	(void)dev;
	(void)swapchain;
	(void)width;
	(void)height;
	return 0;
}
static sk_device_result_t sk_trd_acquire_next_image(sk_render_device_t dev, const sk_acquire_info_t* info, u32* out_image_index) {
	(void)dev;
	(void)info;
	if (out_image_index) {
		*out_image_index = 0u;
	}
	return SK_DEVICE_RESULT_ERROR;
}
static sk_texture_t sk_trd_get_swapchain_image(sk_render_device_t dev, sk_swapchain_t swapchain, u32 image_index) {
	(void)dev;
	(void)swapchain;
	(void)image_index;
	return sk_texture_t_zero();
}
static sk_extent3d_t sk_trd_get_swapchain_extent(sk_render_device_t dev, sk_swapchain_t swapchain) {
	(void)dev;
	(void)swapchain;
	sk_extent3d_t extent = {0};
	return extent;
}
static sk_pixel_format_t sk_trd_get_swapchain_format(sk_render_device_t dev, sk_swapchain_t swapchain) {
	(void)dev;
	(void)swapchain;
	return SK_PIXEL_FORMAT_UNKNOWN;
}
static u32 sk_trd_get_swapchain_image_count(sk_render_device_t dev, sk_swapchain_t swapchain) {
	(void)dev;
	(void)swapchain;
	return 0u;
}
static u32 sk_trd_get_swapchain_current_image_index(sk_render_device_t dev, sk_swapchain_t swapchain) {
	(void)dev;
	(void)swapchain;
	return 0u;
}
static u32 sk_trd_get_swapchain_textures(sk_render_device_t dev, sk_swapchain_t swapchain, sk_texture_t* out_textures, u32 max_count) {
	(void)dev;
	(void)swapchain;
	(void)out_textures;
	(void)max_count;
	return 0u;
}
static sk_device_result_t sk_trd_present(sk_render_device_t dev, sk_queue_t queue, const sk_present_info_t* info) {
	(void)dev;
	(void)queue;
	(void)info;
	return SK_DEVICE_RESULT_ERROR;
}

static sk_fence_t sk_trd_create_fence(sk_render_device_t dev, const sk_fence_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_fence_t_zero();
}
static void sk_trd_destroy_fence(sk_render_device_t dev, sk_fence_t fence) {
	(void)dev;
	(void)fence;
}
static i32 sk_trd_wait_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count, bool wait_all, u64 timeout_ns) {
	(void)dev;
	(void)fences;
	(void)fence_count;
	(void)wait_all;
	(void)timeout_ns;
	return 0;
}
static void sk_trd_reset_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count) {
	(void)dev;
	(void)fences;
	(void)fence_count;
}
static sk_semaphore_t sk_trd_create_semaphore(sk_render_device_t dev, const sk_semaphore_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_semaphore_t_zero();
}
static void sk_trd_destroy_semaphore(sk_render_device_t dev, sk_semaphore_t semaphore) {
	(void)dev;
	(void)semaphore;
}

static sk_command_buffer_t sk_trd_create_command_buffer(sk_render_device_t dev, const sk_command_buffer_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_command_buffer_t_zero();
}
static void sk_trd_destroy_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}
static i32 sk_trd_begin_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_begin_info_t* info) {
	(void)dev;
	(void)cmd;
	(void)info;
	return 0;
}
static void sk_trd_end_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}
static void sk_trd_reset_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}
static void sk_trd_execute_commands(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_t* secondary_cmds, u32 secondary_count) {
	(void)dev;
	(void)cmd;
	(void)secondary_cmds;
	(void)secondary_count;
}

static void sk_trd_set_viewport(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_viewport, const sk_viewport_t* viewports, u32 viewport_count) {
	(void)dev;
	(void)cmd;
	(void)first_viewport;
	(void)viewports;
	(void)viewport_count;
}
static void sk_trd_set_scissor(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_scissor, const sk_rect2d_t* scissors, u32 scissor_count) {
	(void)dev;
	(void)cmd;
	(void)first_scissor;
	(void)scissors;
	(void)scissor_count;
}
static void sk_trd_set_blend_constants(sk_render_device_t dev, sk_command_buffer_t cmd, f32 r, f32 g, f32 b, f32 a) {
	(void)dev;
	(void)cmd;
	(void)r;
	(void)g;
	(void)b;
	(void)a;
}
static void sk_trd_set_stencil_reference(sk_render_device_t dev, sk_command_buffer_t cmd, u32 reference) {
	(void)dev;
	(void)cmd;
	(void)reference;
}
static void sk_trd_set_depth_bias(sk_render_device_t dev, sk_command_buffer_t cmd, f32 constant_factor, f32 clamp, f32 slope_factor) {
	(void)dev;
	(void)cmd;
	(void)constant_factor;
	(void)clamp;
	(void)slope_factor;
}
static void sk_trd_set_depth_bounds(sk_render_device_t dev, sk_command_buffer_t cmd, f32 min_depth, f32 max_depth) {
	(void)dev;
	(void)cmd;
	(void)min_depth;
	(void)max_depth;
}
static void sk_trd_bind_pipeline(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline) {
	(void)dev;
	(void)cmd;
	(void)bind_point;
	(void)pipeline;
}
static void sk_trd_bind_descriptor_set(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline, u32 set_index,
									   sk_descriptor_set_t desc_set, const u32* dynamic_offsets, u32 offset_count) {
	(void)dev;
	(void)cmd;
	(void)bind_point;
	(void)pipeline;
	(void)set_index;
	(void)desc_set;
	(void)dynamic_offsets;
	(void)offset_count;
}
static void sk_trd_bind_vertex_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_binding, const sk_buffer_t* buffers, const u64* offsets, u32 buffer_count) {
	(void)dev;
	(void)cmd;
	(void)first_binding;
	(void)buffers;
	(void)offsets;
	(void)buffer_count;
}
static void sk_trd_bind_index_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, sk_index_type_t index_type) {
	(void)dev;
	(void)cmd;
	(void)buf;
	(void)offset;
	(void)index_type;
}
static void sk_trd_push_constants(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_t pipeline, u32 shader_stages, u32 offset, u32 size, const void* data) {
	(void)dev;
	(void)cmd;
	(void)pipeline;
	(void)shader_stages;
	(void)offset;
	(void)size;
	(void)data;
}

static void sk_trd_draw(sk_render_device_t dev, sk_command_buffer_t cmd, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance) {
	(void)dev;
	(void)cmd;
	(void)vertex_count;
	(void)instance_count;
	(void)first_vertex;
	(void)first_instance;
}
static void sk_trd_draw_indexed(sk_render_device_t dev, sk_command_buffer_t cmd, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance) {
	(void)dev;
	(void)cmd;
	(void)index_count;
	(void)instance_count;
	(void)first_index;
	(void)vertex_offset;
	(void)first_instance;
}
static void sk_trd_draw_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride) {
	(void)dev;
	(void)cmd;
	(void)buffer;
	(void)offset;
	(void)draw_count;
	(void)stride;
}
static void sk_trd_draw_indexed_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride) {
	(void)dev;
	(void)cmd;
	(void)buffer;
	(void)offset;
	(void)draw_count;
	(void)stride;
}
static void sk_trd_draw_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
									   u32 max_draw_count, u32 stride) {
	(void)dev;
	(void)cmd;
	(void)buffer;
	(void)offset;
	(void)count_buffer;
	(void)count_offset;
	(void)max_draw_count;
	(void)stride;
}
static void sk_trd_draw_indexed_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
											   u32 max_draw_count, u32 stride) {
	(void)dev;
	(void)cmd;
	(void)buffer;
	(void)offset;
	(void)count_buffer;
	(void)count_offset;
	(void)max_draw_count;
	(void)stride;
}
static void sk_trd_dispatch(sk_render_device_t dev, sk_command_buffer_t cmd, u32 group_count_x, u32 group_count_y, u32 group_count_z) {
	(void)dev;
	(void)cmd;
	(void)group_count_x;
	(void)group_count_y;
	(void)group_count_z;
}
static void sk_trd_dispatch_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset) {
	(void)dev;
	(void)cmd;
	(void)buffer;
	(void)offset;
}
static void sk_trd_trace_rays(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_trace_rays_info_t* info) {
	(void)dev;
	(void)cmd;
	(void)info;
}

static void sk_trd_begin_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_begin_render_pass_info_t* info) {
	(void)dev;
	(void)cmd;
	(void)info;
}
static void sk_trd_end_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}
static void sk_trd_clear_attachments(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_clear_attachment_t* attachments, u32 attachment_count) {
	(void)dev;
	(void)cmd;
	(void)attachments;
	(void)attachment_count;
}

static void sk_trd_copy_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t src, sk_buffer_t dst, u64 size, u64 src_offset, u64 dst_offset) {
	(void)dev;
	(void)cmd;
	(void)src;
	(void)dst;
	(void)size;
	(void)src_offset;
	(void)dst_offset;
}
static void sk_trd_copy_buffer_to_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info) {
	(void)dev;
	(void)cmd;
	(void)copy_info;
}
static void sk_trd_copy_texture_to_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info) {
	(void)dev;
	(void)cmd;
	(void)copy_info;
}
static void sk_trd_copy_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_copy_t* copy_info) {
	(void)dev;
	(void)cmd;
	(void)copy_info;
}
static void sk_trd_blit_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_blit_t* blit_info) {
	(void)dev;
	(void)cmd;
	(void)blit_info;
}
static void sk_trd_resolve_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_resolve_t* resolve_info) {
	(void)dev;
	(void)cmd;
	(void)resolve_info;
}
static void sk_trd_update_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, const void* data) {
	(void)dev;
	(void)cmd;
	(void)buf;
	(void)offset;
	(void)size;
	(void)data;
}
static void sk_trd_fill_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, u32 data) {
	(void)dev;
	(void)cmd;
	(void)buf;
	(void)offset;
	(void)size;
	(void)data;
}
static void sk_trd_clear_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, const sk_clear_values_t* clear, u32 base_mip_level, u32 mip_level_count,
								 u32 base_array_layer, u32 array_layer_count) {
	(void)dev;
	(void)cmd;
	(void)tex;
	(void)clear;
	(void)base_mip_level;
	(void)mip_level_count;
	(void)base_array_layer;
	(void)array_layer_count;
}

static void sk_trd_resource_barrier_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, sk_resource_state_t old_state, sk_resource_state_t new_state,
										   u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)cmd;
	(void)buf;
	(void)old_state;
	(void)new_state;
	(void)src_scope;
	(void)dst_scope;
}
static void sk_trd_resource_barrier_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, sk_resource_state_t old_state, sk_resource_state_t new_state,
											u32 base_mip_level, u32 mip_level_count, u32 base_array_layer, u32 array_layer_count, u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)cmd;
	(void)tex;
	(void)old_state;
	(void)new_state;
	(void)base_mip_level;
	(void)mip_level_count;
	(void)base_array_layer;
	(void)array_layer_count;
	(void)src_scope;
	(void)dst_scope;
}
static void sk_trd_resource_barrier_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, sk_resource_state_t old_state, sk_resource_state_t new_state,
													u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)cmd;
	(void)blas;
	(void)old_state;
	(void)new_state;
	(void)src_scope;
	(void)dst_scope;
}
static void sk_trd_resource_barrier_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, sk_resource_state_t old_state, sk_resource_state_t new_state,
												 u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)cmd;
	(void)tlas;
	(void)old_state;
	(void)new_state;
	(void)src_scope;
	(void)dst_scope;
}
static void sk_trd_memory_barrier(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}

static void sk_trd_begin_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a) {
	(void)dev;
	(void)cmd;
	(void)name;
	(void)r;
	(void)g;
	(void)b;
	(void)a;
}
static void sk_trd_end_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}
static void sk_trd_insert_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a) {
	(void)dev;
	(void)cmd;
	(void)name;
	(void)r;
	(void)g;
	(void)b;
	(void)a;
}

static sk_query_pool_t sk_trd_create_query_pool(sk_render_device_t dev, const sk_query_pool_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_query_pool_t_zero();
}
static void sk_trd_destroy_query_pool(sk_render_device_t dev, sk_query_pool_t pool) {
	(void)dev;
	(void)pool;
}
static void sk_trd_reset_query_pool(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)first_query;
	(void)query_count;
}
static void sk_trd_begin_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)query;
}
static void sk_trd_end_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)query;
}
static void sk_trd_write_timestamp(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)query;
}
static void sk_trd_copy_query_pool_results(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count, sk_buffer_t dst_buffer,
										   u64 dst_offset, u64 stride) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)first_query;
	(void)query_count;
	(void)dst_buffer;
	(void)dst_offset;
	(void)stride;
}
static i32 sk_trd_get_query_pool_results(sk_render_device_t dev, sk_query_pool_t pool, u32 first_query, u32 query_count, void* data, u64 data_size, u64 stride, bool wait) {
	(void)dev;
	(void)pool;
	(void)first_query;
	(void)query_count;
	(void)data;
	(void)data_size;
	(void)stride;
	(void)wait;
	return -1;
}

static sk_blas_t sk_trd_create_bottom_level_as(sk_render_device_t dev, const sk_blas_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_blas_t_zero();
}
static void sk_trd_destroy_bottom_level_as(sk_render_device_t dev, sk_blas_t blas) {
	(void)dev;
	(void)blas;
}
static sk_tlas_t sk_trd_create_top_level_as(sk_render_device_t dev, const sk_tlas_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_tlas_t_zero();
}
static void sk_trd_destroy_top_level_as(sk_render_device_t dev, sk_tlas_t tlas) {
	(void)dev;
	(void)tlas;
}
static sk_as_build_sizes_t sk_trd_get_blas_build_sizes(sk_render_device_t dev, const sk_blas_desc_t* desc) {
	(void)dev;
	(void)desc;
	sk_as_build_sizes_t sizes = {0, 0, 0};
	return sizes;
}
static sk_as_build_sizes_t sk_trd_get_tlas_build_sizes(sk_render_device_t dev, const sk_tlas_desc_t* desc) {
	(void)dev;
	(void)desc;
	sk_as_build_sizes_t sizes = {0, 0, 0};
	return sizes;
}
static void sk_trd_build_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, const sk_blas_desc_t* desc, const sk_as_build_info_t* build_info) {
	(void)dev;
	(void)cmd;
	(void)blas;
	(void)desc;
	(void)build_info;
}
static void sk_trd_build_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, const sk_tlas_desc_t* desc, const sk_as_build_info_t* build_info) {
	(void)dev;
	(void)cmd;
	(void)tlas;
	(void)desc;
	(void)build_info;
}
static bool sk_trd_is_bottom_level_as_compacted(sk_render_device_t dev, sk_blas_t blas) {
	(void)dev;
	(void)blas;
	return false;
}
static u64 sk_trd_get_bottom_level_as_compacted_size(sk_render_device_t dev, sk_blas_t blas) {
	(void)dev;
	(void)blas;
	return 0ull;
}
static sk_blas_desc_t sk_trd_get_blas_desc(sk_render_device_t dev, sk_blas_t blas) {
	(void)dev;
	(void)blas;
	sk_blas_desc_t desc = {0};
	return desc;
}
static sk_tlas_desc_t sk_trd_get_tlas_desc(sk_render_device_t dev, sk_tlas_t tlas) {
	(void)dev;
	(void)tlas;
	sk_tlas_desc_t desc = {0};
	return desc;
}
static void sk_trd_copy_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t src, sk_blas_t dst, bool compress) {
	(void)dev;
	(void)cmd;
	(void)src;
	(void)dst;
	(void)compress;
}
static void sk_trd_copy_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t src, sk_tlas_t dst, bool compress) {
	(void)dev;
	(void)cmd;
	(void)src;
	(void)dst;
	(void)compress;
}
static bool sk_trd_update_top_level_as_instances(sk_render_device_t dev, sk_tlas_t tlas, const sk_as_instance_desc_t* instances, u32 instance_count) {
	(void)dev;
	(void)tlas;
	(void)instances;
	(void)instance_count;
	return false;
}
static void sk_trd_update_top_level_as_instance(sk_render_device_t dev, sk_tlas_t tlas, u32 index, const sk_as_instance_desc_t* instance) {
	(void)dev;
	(void)tlas;
	(void)index;
	(void)instance;
}
static void sk_trd_set_top_level_as_instance_count(sk_render_device_t dev, sk_tlas_t tlas, u32 count) {
	(void)dev;
	(void)tlas;
	(void)count;
}

static sk_queue_t sk_trd_create_queue(sk_render_device_t dev, const sk_queue_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_queue_t_zero();
}
static void sk_trd_destroy_queue(sk_render_device_t dev, sk_queue_t queue) {
	(void)dev;
	(void)queue;
}
static void sk_trd_submit_command_buffer(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd) {
	(void)dev;
	(void)queue;
	(void)cmd;
}
static i32 sk_trd_submit(sk_render_device_t dev, sk_queue_t queue, const sk_submit_info_t* info) {
	(void)dev;
	(void)queue;
	(void)info;
	return 0;
}
static i32 sk_trd_submit_and_wait(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd) {
	(void)dev;
	(void)queue;
	(void)cmd;
	return 0;
}
static i32 sk_trd_queue_wait_idle(sk_render_device_t dev, sk_queue_t queue) {
	(void)dev;
	(void)queue;
	return 0;
}

static sk_texture_memory_requirements_t sk_trd_get_texture_memory_requirements(sk_render_device_t dev, const sk_texture_desc_t* desc) {
	(void)dev;
	(void)desc;
	sk_texture_memory_requirements_t r = {0, 0, 0};
	return r;
}
static sk_buffer_memory_requirements_t sk_trd_get_buffer_memory_requirements(sk_render_device_t dev, const sk_buffer_desc_t* desc) {
	(void)dev;
	(void)desc;
	sk_buffer_memory_requirements_t r = {0, 0, 0};
	return r;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS
#include "test.h"

SK_TEST(test_render_device_api_table_is_complete) {
	/* Device */
	TEST_ASSERT_NOT_NULL(test_render_device_api.init);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy);
	TEST_ASSERT_NOT_NULL(test_render_device_api.wait_idle);

	/* Adapter / device queries */
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_adapter_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_adapter);
	TEST_ASSERT_NOT_NULL(test_render_device_api.select_adapter);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_adapter_score);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_adapter_name);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_properties);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_features);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_api);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_memory_budgets);

	/* Buffer / texture / sampler / memory / shader / pipeline */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.buffer_map);
	TEST_ASSERT_NOT_NULL(test_render_device_api.buffer_unmap);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_buffer_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_buffer_mapped_data);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_texture_view);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_texture_view);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_sampler);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_sampler);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_texture_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_texture_view_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_sampler_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_memory);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_memory);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_aliased_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_memory_size);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_shader);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_shader);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_graphics_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_compute_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_ray_tracing_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_pipeline_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_pipeline_bind_point);

	/* Descriptors */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_descriptor_set);
	TEST_ASSERT_NOT_NULL(test_render_device_api.update_descriptor_set);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_descriptor_set);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_descriptor_set_desc);

	/* Pass / FB / swapchain / sync */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_render_pass);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_render_pass);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_framebuffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_framebuffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_render_pass_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_framebuffer_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_framebuffer_extent);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_swapchain);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_swapchain);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resize_swapchain);
	TEST_ASSERT_NOT_NULL(test_render_device_api.acquire_next_image);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_image);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_extent);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_format);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_image_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_current_image_index);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_textures);
	TEST_ASSERT_NOT_NULL(test_render_device_api.present);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_fence);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_fence);
	TEST_ASSERT_NOT_NULL(test_render_device_api.wait_fences);
	TEST_ASSERT_NOT_NULL(test_render_device_api.reset_fences);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_semaphore);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_semaphore);

	/* Cmd lifecycle / state / draw / transfer / barrier / debug */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.begin_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.end_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.reset_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.execute_commands);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_viewport);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_scissor);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_blend_constants);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_stencil_reference);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_depth_bias);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_depth_bounds);
	TEST_ASSERT_NOT_NULL(test_render_device_api.bind_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.bind_descriptor_set);
	TEST_ASSERT_NOT_NULL(test_render_device_api.bind_vertex_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.bind_index_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.push_constants);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indexed);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indirect);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indexed_indirect);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indirect_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indexed_indirect_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.dispatch);
	TEST_ASSERT_NOT_NULL(test_render_device_api.dispatch_indirect);
	TEST_ASSERT_NOT_NULL(test_render_device_api.trace_rays);
	TEST_ASSERT_NOT_NULL(test_render_device_api.begin_render_pass);
	TEST_ASSERT_NOT_NULL(test_render_device_api.end_render_pass);
	TEST_ASSERT_NOT_NULL(test_render_device_api.clear_attachments);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_buffer_to_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_texture_to_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.blit_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resolve_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.update_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.fill_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.clear_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resource_barrier_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resource_barrier_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resource_barrier_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resource_barrier_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.memory_barrier);
	TEST_ASSERT_NOT_NULL(test_render_device_api.begin_debug_label);
	TEST_ASSERT_NOT_NULL(test_render_device_api.end_debug_label);
	TEST_ASSERT_NOT_NULL(test_render_device_api.insert_debug_label);

	/* Queries / AS / queue / memreq */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_query_pool);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_query_pool);
	TEST_ASSERT_NOT_NULL(test_render_device_api.reset_query_pool);
	TEST_ASSERT_NOT_NULL(test_render_device_api.begin_query);
	TEST_ASSERT_NOT_NULL(test_render_device_api.end_query);
	TEST_ASSERT_NOT_NULL(test_render_device_api.write_timestamp);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_query_pool_results);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_query_pool_results);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_blas_build_sizes);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_tlas_build_sizes);
	TEST_ASSERT_NOT_NULL(test_render_device_api.build_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.build_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.is_bottom_level_as_compacted);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_bottom_level_as_compacted_size);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_blas_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_tlas_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.update_top_level_as_instances);
	TEST_ASSERT_NOT_NULL(test_render_device_api.update_top_level_as_instance);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_top_level_as_instance_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_queue);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_queue);
	TEST_ASSERT_NOT_NULL(test_render_device_api.submit_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.submit);
	TEST_ASSERT_NOT_NULL(test_render_device_api.submit_and_wait);
	TEST_ASSERT_NOT_NULL(test_render_device_api.queue_wait_idle);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_texture_memory_requirements);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_buffer_memory_requirements);
}

SK_TEST(test_render_device_stub_registers_and_returns_zero) {
	sk_device_init_desc_t desc = {0};
	desc.enable_debug_layers = true;
	sk_render_device_t dev = test_render_device_api.init(NULL, &desc);
	TEST_ASSERT_FALSE(sk_render_device_t_is_valid(dev));
	TEST_ASSERT_EQUAL_INT(0, test_render_device_api.wait_idle(dev));
	TEST_ASSERT_EQUAL_INT(SK_GRAPHICS_API_NONE, (int)test_render_device_api.get_api(dev));
}

#endif /* SK_TESTS */
