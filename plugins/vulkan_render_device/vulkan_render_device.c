/*
 * vulkan_render_device.c — Vulkan render device backend (APX-50).
 *
 * Full port of skore main's VulkanDevice.cpp onto the C RHI surface
 * (sk_render_device_api_t). Subsystems covered with main parity:
 *   - instance/device/queues (volk meta-loader, 1.3 API)
 *   - VMA memory (buffers, textures, aliasing, budgets)
 *   - adapter enumeration + feature/limit queries
 *   - descriptor sets (full info in; resources resolved outside the RHI)
 *   - graphics / compute / ray tracing pipelines (+ bindless indexing)
 *   - render pass (vkCreateRenderPass2KHR) / framebuffer / swapchain
 *   - command recording lives in vulkan_command_buffer.c
 *   - query pools + acceleration structures (BLAS/TLAS) incl. compaction
 *   - deferred destruction (resources freed only at CPU sync points)
 *
 * Device-creation failure returns zero handles / non-zero status codes; the
 * RHI contract (render_device.h) documents the status codes.
 */

#define VMA_ASSERT_LEAK(expr)
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include "vulkan_render_device_internal.h"
#include "vulkan_render_device.h"
#include "vulkan_utils.h"

#include "logger.h"
#include "platform_window.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Plugin entry registers the API tables; file-scope so init() can look up the
 * platform window surface via the app registry. */
static sk_app_context_t* plugin_context = NULL;
static const sk_app_api_t* plugin_app_api = NULL;

static const sk_logger_api_t* vulkan_logger_api(void) {
	return sk_logger_api();
}

/* ------------------------------------------------------------------ */
/* Debug callback                                                      */
/* ------------------------------------------------------------------ */

static VKAPI_ATTR VkBool32 VKAPI_CALL sk_vkrd_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type,
															 const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data) {
	(void)message_type;
	(void)user_data;
	const sk_logger_api_t* api = vulkan_logger_api();
	sk_logger_t* log = api->create_logger("Skore::Vulkan");
	if (log == NULL) {
		return VK_FALSE;
	}
	switch (message_severity) {
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		sk_log_warn(api, log, "%s", callback_data->pMessage != NULL ? callback_data->pMessage : "");
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		sk_log_error(api, log, "%s", callback_data->pMessage != NULL ? callback_data->pMessage : "");
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
		sk_log_info(api, log, "%s", callback_data->pMessage != NULL ? callback_data->pMessage : "");
		break;
	}
	api->destroy_logger(log);
	return VK_FALSE;
}

static void sk_vkrd_log_error(const_chr_t message) {
	const sk_logger_api_t* api = vulkan_logger_api();
	sk_logger_t* log = api->create_logger("Skore::Vulkan");
	if (log == NULL) {
		return;
	}
	sk_log_error(api, log, "%s", message);
	api->destroy_logger(log);
}

/* ------------------------------------------------------------------ */
/* Forward declarations (all RHI table entries)                        */
/* ------------------------------------------------------------------ */

static sk_render_device_t sk_vkrd_init(void_ptr_t context, const sk_device_init_desc_t* desc);
static void sk_vkrd_destroy(sk_render_device_t dev);
static i32 sk_vkrd_wait_idle(sk_render_device_t dev);

static u32 sk_vkrd_get_adapter_count(sk_render_device_t dev);
static sk_adapter_t sk_vkrd_get_adapter(sk_render_device_t dev, u32 index);
static i32 sk_vkrd_select_adapter(sk_render_device_t dev, sk_adapter_t adapter);
static u32 sk_vkrd_get_adapter_score(sk_render_device_t dev, sk_adapter_t adapter);
static const_chr_t sk_vkrd_get_adapter_name(sk_render_device_t dev, sk_adapter_t adapter);
static sk_device_properties_t sk_vkrd_get_properties(sk_render_device_t dev);
static sk_device_features_t sk_vkrd_get_features(sk_render_device_t dev);
static sk_graphics_api_t sk_vkrd_get_api(sk_render_device_t dev);
static u32 sk_vkrd_get_memory_budgets(sk_render_device_t dev, sk_memory_heap_budget_t* out_budgets, u32 max_count);

static sk_buffer_t sk_vkrd_create_buffer(sk_render_device_t dev, const sk_buffer_desc_t* desc);
static void sk_vkrd_destroy_buffer(sk_render_device_t dev, sk_buffer_t buf);
static void* sk_vkrd_buffer_map(sk_render_device_t dev, sk_buffer_t buf);
static void sk_vkrd_buffer_unmap(sk_render_device_t dev, sk_buffer_t buf);
static sk_buffer_desc_t sk_vkrd_get_buffer_desc(sk_render_device_t dev, sk_buffer_t buf);
static void_ptr_t sk_vkrd_get_buffer_mapped_data(sk_render_device_t dev, sk_buffer_t buf);

static sk_texture_t sk_vkrd_create_texture(sk_render_device_t dev, const sk_texture_desc_t* desc);
static void sk_vkrd_destroy_texture(sk_render_device_t dev, sk_texture_t tex);
static sk_texture_view_t sk_vkrd_create_texture_view(sk_render_device_t dev, const sk_texture_view_desc_t* desc);
static void sk_vkrd_destroy_texture_view(sk_render_device_t dev, sk_texture_view_t view);
static sk_sampler_t sk_vkrd_create_sampler(sk_render_device_t dev, const sk_sampler_desc_t* desc);
static void sk_vkrd_destroy_sampler(sk_render_device_t dev, sk_sampler_t sampler);
static sk_texture_desc_t sk_vkrd_get_texture_desc(sk_render_device_t dev, sk_texture_t tex);
static sk_texture_view_desc_t sk_vkrd_get_texture_view_desc(sk_render_device_t dev, sk_texture_view_t view);
static sk_sampler_desc_t sk_vkrd_get_sampler_desc(sk_render_device_t dev, sk_sampler_t sampler);

static sk_memory_t sk_vkrd_create_memory(sk_render_device_t dev, u64 size, u64 alignment, u32 memory_type_bits);
static void sk_vkrd_destroy_memory(sk_render_device_t dev, sk_memory_t mem);
static sk_texture_t sk_vkrd_create_aliased_texture(sk_render_device_t dev, const sk_texture_desc_t* desc, sk_memory_t mem, u64 offset);
static u64 sk_vkrd_get_memory_size(sk_render_device_t dev, sk_memory_t mem);

static sk_shader_t sk_vkrd_create_shader(sk_render_device_t dev, const_chr_t src, u32 src_size, u32 shader_stage);
static void sk_vkrd_destroy_shader(sk_render_device_t dev, sk_shader_t shdr);

static sk_pipeline_t sk_vkrd_create_graphics_pipeline(sk_render_device_t dev, const sk_graphics_pipeline_desc_t* desc);
static sk_pipeline_t sk_vkrd_create_compute_pipeline(sk_render_device_t dev, const sk_compute_pipeline_desc_t* desc);
static sk_pipeline_t sk_vkrd_create_ray_tracing_pipeline(sk_render_device_t dev, const sk_ray_tracing_pipeline_desc_t* desc);
static void sk_vkrd_destroy_pipeline(sk_render_device_t dev, sk_pipeline_t pipeline);
static sk_pipeline_desc_t sk_vkrd_get_pipeline_desc(sk_render_device_t dev, sk_pipeline_t pipeline);
static sk_pipeline_bind_point_t sk_vkrd_get_pipeline_bind_point(sk_render_device_t dev, sk_pipeline_t pipeline);

static sk_descriptor_set_t sk_vkrd_create_descriptor_set(sk_render_device_t dev, const sk_descriptor_set_desc_t* desc);
static void sk_vkrd_update_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set, const sk_descriptor_write_t* writes, u32 write_count);
static void sk_vkrd_destroy_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set);
static sk_descriptor_set_desc_t sk_vkrd_get_descriptor_set_desc(sk_render_device_t dev, sk_descriptor_set_t set);

static sk_render_pass_t sk_vkrd_create_render_pass(sk_render_device_t dev, const sk_render_pass_desc_t* desc);
static void sk_vkrd_destroy_render_pass(sk_render_device_t dev, sk_render_pass_t pass);
static sk_framebuffer_t sk_vkrd_create_framebuffer(sk_render_device_t dev, const sk_framebuffer_desc_t* desc);
static void sk_vkrd_destroy_framebuffer(sk_render_device_t dev, sk_framebuffer_t fb);
static sk_render_pass_desc_t sk_vkrd_get_render_pass_desc(sk_render_device_t dev, sk_render_pass_t pass);
static sk_framebuffer_desc_t sk_vkrd_get_framebuffer_desc(sk_render_device_t dev, sk_framebuffer_t fb);
static sk_extent3d_t sk_vkrd_get_framebuffer_extent(sk_render_device_t dev, sk_framebuffer_t fb);

static sk_swapchain_t sk_vkrd_create_swapchain(sk_render_device_t dev, const sk_swapchain_desc_t* desc);
static void sk_vkrd_destroy_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain);
static i32 sk_vkrd_resize_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain, u32 width, u32 height);
static sk_device_result_t sk_vkrd_acquire_next_image(sk_render_device_t dev, const sk_acquire_info_t* info, u32* out_image_index);
static sk_texture_t sk_vkrd_get_swapchain_image(sk_render_device_t dev, sk_swapchain_t swapchain, u32 image_index);
static sk_extent3d_t sk_vkrd_get_swapchain_extent(sk_render_device_t dev, sk_swapchain_t swapchain);
static sk_pixel_format_t sk_vkrd_get_swapchain_format(sk_render_device_t dev, sk_swapchain_t swapchain);
static u32 sk_vkrd_get_swapchain_image_count(sk_render_device_t dev, sk_swapchain_t swapchain);
static u32 sk_vkrd_get_swapchain_current_image_index(sk_render_device_t dev, sk_swapchain_t swapchain);
static u32 sk_vkrd_get_swapchain_textures(sk_render_device_t dev, sk_swapchain_t swapchain, sk_texture_t* out_textures, u32 max_count);
static sk_device_result_t sk_vkrd_present(sk_render_device_t dev, sk_queue_t queue, const sk_present_info_t* info);

static sk_fence_t sk_vkrd_create_fence(sk_render_device_t dev, const sk_fence_desc_t* desc);
static void sk_vkrd_destroy_fence(sk_render_device_t dev, sk_fence_t fence);
static i32 sk_vkrd_wait_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count, bool wait_all, u64 timeout_ns);
static void sk_vkrd_reset_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count);
static sk_semaphore_t sk_vkrd_create_semaphore(sk_render_device_t dev, const sk_semaphore_desc_t* desc);
static void sk_vkrd_destroy_semaphore(sk_render_device_t dev, sk_semaphore_t semaphore);

static sk_query_pool_t sk_vkrd_create_query_pool(sk_render_device_t dev, const sk_query_pool_desc_t* desc);
static void sk_vkrd_destroy_query_pool(sk_render_device_t dev, sk_query_pool_t pool);
static i32 sk_vkrd_get_query_pool_results(sk_render_device_t dev, sk_query_pool_t pool, u32 first_query, u32 query_count, void* data, u64 data_size, u64 stride, bool wait);

static sk_blas_t sk_vkrd_create_bottom_level_as(sk_render_device_t dev, const sk_blas_desc_t* desc);
static void sk_vkrd_destroy_bottom_level_as(sk_render_device_t dev, sk_blas_t blas);
static sk_tlas_t sk_vkrd_create_top_level_as(sk_render_device_t dev, const sk_tlas_desc_t* desc);
static void sk_vkrd_destroy_top_level_as(sk_render_device_t dev, sk_tlas_t tlas);
static sk_as_build_sizes_t sk_vkrd_get_blas_build_sizes(sk_render_device_t dev, const sk_blas_desc_t* desc);
static sk_as_build_sizes_t sk_vkrd_get_tlas_build_sizes(sk_render_device_t dev, const sk_tlas_desc_t* desc);
static bool sk_vkrd_is_bottom_level_as_compacted(sk_render_device_t dev, sk_blas_t blas);
static u64 sk_vkrd_get_bottom_level_as_compacted_size(sk_render_device_t dev, sk_blas_t blas);
static sk_blas_desc_t sk_vkrd_get_blas_desc(sk_render_device_t dev, sk_blas_t blas);
static sk_tlas_desc_t sk_vkrd_get_tlas_desc(sk_render_device_t dev, sk_tlas_t tlas);
static bool sk_vkrd_update_top_level_as_instances(sk_render_device_t dev, sk_tlas_t tlas, const sk_as_instance_desc_t* instances, u32 instance_count);
static void sk_vkrd_update_top_level_as_instance(sk_render_device_t dev, sk_tlas_t tlas, u32 index, const sk_as_instance_desc_t* instance);
static void sk_vkrd_set_top_level_as_instance_count(sk_render_device_t dev, sk_tlas_t tlas, u32 count);

static sk_queue_t sk_vkrd_create_queue(sk_render_device_t dev, const sk_queue_desc_t* desc);
static void sk_vkrd_destroy_queue(sk_render_device_t dev, sk_queue_t queue);
static void sk_vkrd_submit_command_buffer(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd);
static i32 sk_vkrd_submit(sk_render_device_t dev, sk_queue_t queue, const sk_submit_info_t* info);
static i32 sk_vkrd_submit_and_wait(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd);
static i32 sk_vkrd_queue_wait_idle(sk_render_device_t dev, sk_queue_t queue);

static sk_texture_memory_requirements_t sk_vkrd_get_texture_memory_requirements(sk_render_device_t dev, const sk_texture_desc_t* desc);
static sk_buffer_memory_requirements_t sk_vkrd_get_buffer_memory_requirements(sk_render_device_t dev, const sk_buffer_desc_t* desc);

/* ------------------------------------------------------------------ */
/* API tables                                                          */
/* ------------------------------------------------------------------ */

static const sk_render_device_api_t vulkan_render_device_api = {
	.init = sk_vkrd_init,
	.destroy = sk_vkrd_destroy,
	.wait_idle = sk_vkrd_wait_idle,

	.get_adapter_count = sk_vkrd_get_adapter_count,
	.get_adapter = sk_vkrd_get_adapter,
	.select_adapter = sk_vkrd_select_adapter,
	.get_adapter_score = sk_vkrd_get_adapter_score,
	.get_adapter_name = sk_vkrd_get_adapter_name,
	.get_properties = sk_vkrd_get_properties,
	.get_features = sk_vkrd_get_features,
	.get_api = sk_vkrd_get_api,
	.get_memory_budgets = sk_vkrd_get_memory_budgets,

	.create_buffer = sk_vkrd_create_buffer,
	.destroy_buffer = sk_vkrd_destroy_buffer,
	.buffer_map = sk_vkrd_buffer_map,
	.buffer_unmap = sk_vkrd_buffer_unmap,
	.get_buffer_desc = sk_vkrd_get_buffer_desc,
	.get_buffer_mapped_data = sk_vkrd_get_buffer_mapped_data,

	.create_texture = sk_vkrd_create_texture,
	.destroy_texture = sk_vkrd_destroy_texture,
	.create_texture_view = sk_vkrd_create_texture_view,
	.destroy_texture_view = sk_vkrd_destroy_texture_view,
	.create_sampler = sk_vkrd_create_sampler,
	.destroy_sampler = sk_vkrd_destroy_sampler,
	.get_texture_desc = sk_vkrd_get_texture_desc,
	.get_texture_view_desc = sk_vkrd_get_texture_view_desc,
	.get_sampler_desc = sk_vkrd_get_sampler_desc,

	.create_memory = sk_vkrd_create_memory,
	.destroy_memory = sk_vkrd_destroy_memory,
	.create_aliased_texture = sk_vkrd_create_aliased_texture,
	.get_memory_size = sk_vkrd_get_memory_size,

	.create_shader = sk_vkrd_create_shader,
	.destroy_shader = sk_vkrd_destroy_shader,

	.create_graphics_pipeline = sk_vkrd_create_graphics_pipeline,
	.create_compute_pipeline = sk_vkrd_create_compute_pipeline,
	.create_ray_tracing_pipeline = sk_vkrd_create_ray_tracing_pipeline,
	.destroy_pipeline = sk_vkrd_destroy_pipeline,
	.get_pipeline_desc = sk_vkrd_get_pipeline_desc,
	.get_pipeline_bind_point = sk_vkrd_get_pipeline_bind_point,

	.create_descriptor_set = sk_vkrd_create_descriptor_set,
	.update_descriptor_set = sk_vkrd_update_descriptor_set,
	.destroy_descriptor_set = sk_vkrd_destroy_descriptor_set,
	.get_descriptor_set_desc = sk_vkrd_get_descriptor_set_desc,

	.create_render_pass = sk_vkrd_create_render_pass,
	.destroy_render_pass = sk_vkrd_destroy_render_pass,
	.create_framebuffer = sk_vkrd_create_framebuffer,
	.destroy_framebuffer = sk_vkrd_destroy_framebuffer,
	.get_render_pass_desc = sk_vkrd_get_render_pass_desc,
	.get_framebuffer_desc = sk_vkrd_get_framebuffer_desc,
	.get_framebuffer_extent = sk_vkrd_get_framebuffer_extent,

	.create_swapchain = sk_vkrd_create_swapchain,
	.destroy_swapchain = sk_vkrd_destroy_swapchain,
	.resize_swapchain = sk_vkrd_resize_swapchain,
	.acquire_next_image = sk_vkrd_acquire_next_image,
	.get_swapchain_image = sk_vkrd_get_swapchain_image,
	.get_swapchain_extent = sk_vkrd_get_swapchain_extent,
	.get_swapchain_format = sk_vkrd_get_swapchain_format,
	.get_swapchain_image_count = sk_vkrd_get_swapchain_image_count,
	.get_swapchain_current_image_index = sk_vkrd_get_swapchain_current_image_index,
	.get_swapchain_textures = sk_vkrd_get_swapchain_textures,
	.present = sk_vkrd_present,

	.create_fence = sk_vkrd_create_fence,
	.destroy_fence = sk_vkrd_destroy_fence,
	.wait_fences = sk_vkrd_wait_fences,
	.reset_fences = sk_vkrd_reset_fences,
	.create_semaphore = sk_vkrd_create_semaphore,
	.destroy_semaphore = sk_vkrd_destroy_semaphore,

	.create_command_buffer = sk_vkrd_create_command_buffer,
	.destroy_command_buffer = sk_vkrd_destroy_command_buffer,
	.begin_command_buffer = sk_vkrd_begin_command_buffer,
	.end_command_buffer = sk_vkrd_end_command_buffer,
	.reset_command_buffer = sk_vkrd_reset_command_buffer,
	.execute_commands = sk_vkrd_execute_commands,

	.set_viewport = sk_vkrd_set_viewport,
	.set_scissor = sk_vkrd_set_scissor,
	.set_blend_constants = sk_vkrd_set_blend_constants,
	.set_stencil_reference = sk_vkrd_set_stencil_reference,
	.set_depth_bias = sk_vkrd_set_depth_bias,
	.set_depth_bounds = sk_vkrd_set_depth_bounds,
	.bind_pipeline = sk_vkrd_bind_pipeline,
	.bind_descriptor_set = sk_vkrd_bind_descriptor_set,
	.bind_vertex_buffer = sk_vkrd_bind_vertex_buffer,
	.bind_index_buffer = sk_vkrd_bind_index_buffer,
	.push_constants = sk_vkrd_push_constants,

	.draw = sk_vkrd_draw,
	.draw_indexed = sk_vkrd_draw_indexed,
	.draw_indirect = sk_vkrd_draw_indirect,
	.draw_indexed_indirect = sk_vkrd_draw_indexed_indirect,
	.draw_indirect_count = sk_vkrd_draw_indirect_count,
	.draw_indexed_indirect_count = sk_vkrd_draw_indexed_indirect_count,
	.dispatch = sk_vkrd_dispatch,
	.dispatch_indirect = sk_vkrd_dispatch_indirect,
	.trace_rays = sk_vkrd_trace_rays,

	.begin_render_pass = sk_vkrd_begin_render_pass,
	.end_render_pass = sk_vkrd_end_render_pass,
	.clear_attachments = sk_vkrd_clear_attachments,

	.copy_buffer = sk_vkrd_copy_buffer,
	.copy_buffer_to_texture = sk_vkrd_copy_buffer_to_texture,
	.copy_texture_to_buffer = sk_vkrd_copy_texture_to_buffer,
	.copy_texture = sk_vkrd_copy_texture,
	.blit_texture = sk_vkrd_blit_texture,
	.resolve_texture = sk_vkrd_resolve_texture,
	.update_buffer = sk_vkrd_update_buffer,
	.fill_buffer = sk_vkrd_fill_buffer,
	.clear_texture = sk_vkrd_clear_texture,

	.resource_barrier_buffer = sk_vkrd_resource_barrier_buffer,
	.resource_barrier_texture = sk_vkrd_resource_barrier_texture,
	.resource_barrier_bottom_level_as = sk_vkrd_resource_barrier_bottom_level_as,
	.resource_barrier_top_level_as = sk_vkrd_resource_barrier_top_level_as,
	.memory_barrier = sk_vkrd_memory_barrier,

	.begin_debug_label = sk_vkrd_begin_debug_label,
	.end_debug_label = sk_vkrd_end_debug_label,
	.insert_debug_label = sk_vkrd_insert_debug_label,

	.create_query_pool = sk_vkrd_create_query_pool,
	.destroy_query_pool = sk_vkrd_destroy_query_pool,
	.reset_query_pool = sk_vkrd_reset_query_pool,
	.begin_query = sk_vkrd_begin_query,
	.end_query = sk_vkrd_end_query,
	.write_timestamp = sk_vkrd_write_timestamp,
	.copy_query_pool_results = sk_vkrd_copy_query_pool_results,
	.get_query_pool_results = sk_vkrd_get_query_pool_results,

	.create_bottom_level_as = sk_vkrd_create_bottom_level_as,
	.destroy_bottom_level_as = sk_vkrd_destroy_bottom_level_as,
	.create_top_level_as = sk_vkrd_create_top_level_as,
	.destroy_top_level_as = sk_vkrd_destroy_top_level_as,
	.get_blas_build_sizes = sk_vkrd_get_blas_build_sizes,
	.get_tlas_build_sizes = sk_vkrd_get_tlas_build_sizes,
	.build_bottom_level_as = sk_vkrd_build_bottom_level_as,
	.build_top_level_as = sk_vkrd_build_top_level_as,
	.is_bottom_level_as_compacted = sk_vkrd_is_bottom_level_as_compacted,
	.get_bottom_level_as_compacted_size = sk_vkrd_get_bottom_level_as_compacted_size,
	.get_blas_desc = sk_vkrd_get_blas_desc,
	.get_tlas_desc = sk_vkrd_get_tlas_desc,
	.copy_bottom_level_as = sk_vkrd_copy_bottom_level_as,
	.copy_top_level_as = sk_vkrd_copy_top_level_as,
	.update_top_level_as_instances = sk_vkrd_update_top_level_as_instances,
	.update_top_level_as_instance = sk_vkrd_update_top_level_as_instance,
	.set_top_level_as_instance_count = sk_vkrd_set_top_level_as_instance_count,

	.create_queue = sk_vkrd_create_queue,
	.destroy_queue = sk_vkrd_destroy_queue,
	.submit_command_buffer = sk_vkrd_submit_command_buffer,
	.submit = sk_vkrd_submit,
	.submit_and_wait = sk_vkrd_submit_and_wait,
	.queue_wait_idle = sk_vkrd_queue_wait_idle,

	.get_texture_memory_requirements = sk_vkrd_get_texture_memory_requirements,
	.get_buffer_memory_requirements = sk_vkrd_get_buffer_memory_requirements,
};

/* Loader surface kept from the APX-49 scaffold (volk / VMA wiring proof). */
static i32 sk_vkrd_loader_init(void) {
	return (i32)volkInitialize();
}

static void sk_vkrd_loader_shutdown(void) {
	volkFinalize();
}

static u32 sk_vkrd_loader_volk_version(void) {
	return (u32)VOLK_HEADER_VERSION;
}

static u32 sk_vkrd_loader_vma_allocator_size(void) {
	return (u32)sizeof(VmaAllocator);
}

static const sk_vulkan_render_device_api_t vulkan_loader_api = {
	.init = sk_vkrd_loader_init,
	.shutdown = sk_vkrd_loader_shutdown,
	.volk_version = sk_vkrd_loader_volk_version,
	.vma_allocator_size = sk_vkrd_loader_vma_allocator_size,
};

void sk_vulkan_render_device_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	plugin_context = context;
	plugin_app_api = app_api;
	app_api->set_api(context, SK_RENDER_DEVICE_API_TYPE_ID, &vulkan_render_device_api);
	app_api->set_api(context, SK_VULKAN_RENDER_DEVICE_API_TYPE_ID, &vulkan_loader_api);
}

/* ------------------------------------------------------------------ */
/* Desc deep-copy helpers                                              */
/* ------------------------------------------------------------------ */

char* sk_vk_dup_string(const sk_vk_device_t* device, const_chr_t src) {
	if (src == NULL || src[0] == '\0') {
		return NULL;
	}
	size_t len = strlen(src);
	char* copy = (char*)device->allocator->alloc(device->allocator->instance, len + 1u);
	if (copy == NULL) {
		return NULL;
	}
	memcpy(copy, src, len + 1u);
	return copy;
}

void_ptr_t sk_vk_alloc(const sk_vk_device_t* device, u64 size) {
	return device->allocator->alloc(device->allocator->instance, size);
}

void sk_vk_free(const sk_vk_device_t* device, const void* ptr) {
	device->allocator->free(device->allocator->instance, SK_CONST_CAST(void_ptr_t, ptr));
}

void sk_vk_copy_buffer_desc(const sk_vk_device_t* device, const sk_buffer_desc_t* src, sk_buffer_desc_t* dst) {
	*dst = *src;
	dst->debug_name = sk_vk_dup_string(device, src->debug_name);
}

void sk_vk_copy_texture_desc(const sk_vk_device_t* device, const sk_texture_desc_t* src, sk_texture_desc_t* dst) {
	*dst = *src;
	dst->debug_name = sk_vk_dup_string(device, src->debug_name);
}

void sk_vk_copy_texture_view_desc(const sk_vk_device_t* device, const sk_texture_view_desc_t* src, sk_texture_view_desc_t* dst) {
	*dst = *src;
	dst->debug_name = sk_vk_dup_string(device, src->debug_name);
}

void sk_vk_copy_sampler_desc(const sk_vk_device_t* device, const sk_sampler_desc_t* src, sk_sampler_desc_t* dst) {
	*dst = *src;
	dst->debug_name = sk_vk_dup_string(device, src->debug_name);
}

void sk_vk_copy_pipeline_desc(const sk_vk_device_t* device, const sk_pipeline_desc_t* src, sk_pipeline_desc_t* dst) {
	memset(dst, 0, sizeof(*dst));
	dst->stride = src->stride;
	dst->num_threads = src->num_threads;

	if (src->input_variable_count > 0u) {
		dst->input_variables = (sk_interface_variable_t*)sk_vk_alloc(device, (u64)src->input_variable_count * sizeof(sk_interface_variable_t));
		if (dst->input_variables != NULL) {
			memcpy(dst->input_variables, src->input_variables, (size_t)src->input_variable_count * sizeof(sk_interface_variable_t));
			for (u32 i = 0u; i < src->input_variable_count; ++i) {
				dst->input_variables[i].name = sk_vk_dup_string(device, src->input_variables[i].name);
			}
			dst->input_variable_count = src->input_variable_count;
		}
	}

	if (src->output_variable_count > 0u) {
		dst->output_variables = (sk_interface_variable_t*)sk_vk_alloc(device, (u64)src->output_variable_count * sizeof(sk_interface_variable_t));
		if (dst->output_variables != NULL) {
			memcpy(dst->output_variables, src->output_variables, (size_t)src->output_variable_count * sizeof(sk_interface_variable_t));
			for (u32 i = 0u; i < src->output_variable_count; ++i) {
				dst->output_variables[i].name = sk_vk_dup_string(device, src->output_variables[i].name);
			}
			dst->output_variable_count = src->output_variable_count;
		}
	}

	if (src->descriptor_count > 0u) {
		dst->descriptors = (sk_descriptor_set_layout_t*)sk_vk_alloc(device, (u64)src->descriptor_count * sizeof(sk_descriptor_set_layout_t));
		if (dst->descriptors != NULL) {
			for (u32 i = 0u; i < src->descriptor_count; ++i) {
				const sk_descriptor_set_layout_t* s = &src->descriptors[i];
				sk_descriptor_set_layout_t* d = &dst->descriptors[i];
				d->set = s->set;
				d->debug_name = sk_vk_dup_string(device, s->debug_name);
				if (s->binding_count > 0u) {
					d->bindings = (sk_descriptor_set_layout_binding_t*)sk_vk_alloc(device, (u64)s->binding_count * sizeof(sk_descriptor_set_layout_binding_t));
					if (d->bindings != NULL) {
						memcpy(d->bindings, s->bindings, (size_t)s->binding_count * sizeof(sk_descriptor_set_layout_binding_t));
						for (u32 b = 0u; b < s->binding_count; ++b) {
							d->bindings[b].name = sk_vk_dup_string(device, s->bindings[b].name);
						}
						d->binding_count = s->binding_count;
					}
				}
			}
			dst->descriptor_count = src->descriptor_count;
		}
	}

	if (src->push_constant_count > 0u) {
		dst->push_constants = (sk_push_constant_range_t*)sk_vk_alloc(device, (u64)src->push_constant_count * sizeof(sk_push_constant_range_t));
		if (dst->push_constants != NULL) {
			memcpy(dst->push_constants, src->push_constants, (size_t)src->push_constant_count * sizeof(sk_push_constant_range_t));
			for (u32 i = 0u; i < src->push_constant_count; ++i) {
				dst->push_constants[i].name = sk_vk_dup_string(device, src->push_constants[i].name);
			}
			dst->push_constant_count = src->push_constant_count;
		}
	}
}

void sk_vk_free_pipeline_desc(const sk_vk_device_t* device, sk_pipeline_desc_t* desc) {
	for (u32 i = 0u; i < desc->input_variable_count; ++i) {
		sk_vk_free(device, desc->input_variables[i].name);
	}
	sk_vk_free(device, desc->input_variables);
	for (u32 i = 0u; i < desc->output_variable_count; ++i) {
		sk_vk_free(device, desc->output_variables[i].name);
	}
	sk_vk_free(device, desc->output_variables);
	for (u32 i = 0u; i < desc->descriptor_count; ++i) {
		sk_vk_free(device, desc->descriptors[i].debug_name);
		for (u32 b = 0u; b < desc->descriptors[i].binding_count; ++b) {
			sk_vk_free(device, desc->descriptors[i].bindings[b].name);
		}
		sk_vk_free(device, desc->descriptors[i].bindings);
	}
	sk_vk_free(device, desc->descriptors);
	for (u32 i = 0u; i < desc->push_constant_count; ++i) {
		sk_vk_free(device, desc->push_constants[i].name);
	}
	sk_vk_free(device, desc->push_constants);
	memset(desc, 0, sizeof(*desc));
}

void sk_vk_copy_render_pass_desc(const sk_vk_device_t* device, const sk_render_pass_desc_t* src, sk_render_pass_desc_t* dst) {
	memset(dst, 0, sizeof(*dst));
	dst->debug_name = sk_vk_dup_string(device, src->debug_name);
	if (src->attachment_count > 0u) {
		dst->attachments = (sk_attachment_desc_t*)sk_vk_alloc(device, (u64)src->attachment_count * sizeof(sk_attachment_desc_t));
		if (dst->attachments != NULL) {
			memcpy(dst->attachments, src->attachments, (size_t)src->attachment_count * sizeof(sk_attachment_desc_t));
			dst->attachment_count = src->attachment_count;
		}
	}
	if (src->resolve_attachment_count > 0u) {
		dst->resolve_attachments = (sk_attachment_desc_t*)sk_vk_alloc(device, (u64)src->resolve_attachment_count * sizeof(sk_attachment_desc_t));
		if (dst->resolve_attachments != NULL) {
			memcpy(dst->resolve_attachments, src->resolve_attachments, (size_t)src->resolve_attachment_count * sizeof(sk_attachment_desc_t));
			dst->resolve_attachment_count = src->resolve_attachment_count;
		}
	}
}

void sk_vk_free_render_pass_desc(const sk_vk_device_t* device, sk_render_pass_desc_t* desc) {
	sk_vk_free(device, desc->debug_name);
	sk_vk_free(device, desc->attachments);
	sk_vk_free(device, desc->resolve_attachments);
	memset(desc, 0, sizeof(*desc));
}

void sk_vk_copy_framebuffer_desc(const sk_vk_device_t* device, const sk_framebuffer_desc_t* src, sk_framebuffer_desc_t* dst) {
	memset(dst, 0, sizeof(*dst));
	dst->debug_name = sk_vk_dup_string(device, src->debug_name);
	if (src->attachment_count > 0u) {
		dst->attachments = (sk_texture_view_t*)sk_vk_alloc(device, (u64)src->attachment_count * sizeof(sk_texture_view_t));
		if (dst->attachments != NULL) {
			memcpy(dst->attachments, src->attachments, (size_t)src->attachment_count * sizeof(sk_texture_view_t));
			dst->attachment_count = src->attachment_count;
		}
	}
}

void sk_vk_free_framebuffer_desc(const sk_vk_device_t* device, sk_framebuffer_desc_t* desc) {
	sk_vk_free(device, desc->debug_name);
	sk_vk_free(device, desc->attachments);
	memset(desc, 0, sizeof(*desc));
}

void sk_vk_copy_blas_desc(const sk_vk_device_t* device, const sk_blas_desc_t* src, sk_blas_desc_t* dst) {
	memset(dst, 0, sizeof(*dst));
	dst->debug_name = sk_vk_dup_string(device, src->debug_name);
	dst->build_flags = src->build_flags;
	if (src->geometry_count > 0u) {
		dst->geometries = (sk_geometry_desc_t*)sk_vk_alloc(device, (u64)src->geometry_count * sizeof(sk_geometry_desc_t));
		if (dst->geometries != NULL) {
			memcpy(dst->geometries, src->geometries, (size_t)src->geometry_count * sizeof(sk_geometry_desc_t));
			dst->geometry_count = src->geometry_count;
		}
	}
}

void sk_vk_free_blas_desc(const sk_vk_device_t* device, sk_blas_desc_t* desc) {
	sk_vk_free(device, desc->debug_name);
	sk_vk_free(device, desc->geometries);
	memset(desc, 0, sizeof(*desc));
}

void sk_vk_copy_tlas_desc(const sk_vk_device_t* device, const sk_tlas_desc_t* src, sk_tlas_desc_t* dst) {
	memset(dst, 0, sizeof(*dst));
	dst->debug_name = sk_vk_dup_string(device, src->debug_name);
	dst->build_flags = src->build_flags;
	dst->max_instances = src->max_instances;
	if (src->instance_count > 0u) {
		dst->instances = (sk_as_instance_desc_t*)sk_vk_alloc(device, (u64)src->instance_count * sizeof(sk_as_instance_desc_t));
		if (dst->instances != NULL) {
			memcpy(dst->instances, src->instances, (size_t)src->instance_count * sizeof(sk_as_instance_desc_t));
			dst->instance_count = src->instance_count;
		}
	}
}

void sk_vk_free_tlas_desc(const sk_vk_device_t* device, sk_tlas_desc_t* desc) {
	sk_vk_free(device, desc->debug_name);
	sk_vk_free(device, desc->instances);
	memset(desc, 0, sizeof(*desc));
}

/* ------------------------------------------------------------------ */
/* Deferred destruction engine                                         */
/* ------------------------------------------------------------------ */

void sk_vk_enqueue_destructor(sk_vk_device_t* device, const sk_vk_destructor_t* destructor) {
	if (device->destructor_count == device->destructor_capacity) {
		u32 new_capacity = device->destructor_capacity == 0u ? 16u : device->destructor_capacity * 2u;
		sk_vk_destructor_t* grown = (sk_vk_destructor_t*)device->allocator->realloc(device->allocator->instance, device->destructors,
																					(size_t)new_capacity * sizeof(sk_vk_destructor_t));
		if (grown == NULL) {
			return;
		}
		device->destructors = grown;
		device->destructor_capacity = new_capacity;
	}
	device->destructors[device->destructor_count] = *destructor;
	device->destructors[device->destructor_count].frame = device->frame_index;
	++device->destructor_count;
}

static void sk_vk_flush_one(sk_vk_device_t* device, sk_vk_destructor_t* d) {
	if (d->texture_view != NULL) {
		if (d->texture_view->image_view != VK_NULL_HANDLE) {
			vkDestroyImageView(device->device, d->texture_view->image_view, NULL);
		}
		sk_vk_free(device, d->texture_view->desc.debug_name);
		sk_vk_free(device, d->texture_view);
	}

	if (d->texture != NULL) {
		if (d->texture->allocation != NULL) {
			vmaDestroyImage(device->vma_allocator, d->texture->image, d->texture->allocation);
		} else if (d->texture->aliased) {
			vkDestroyImage(device->device, d->texture->image, NULL);
		}
		sk_vk_free(device, d->texture->desc.debug_name);
		sk_vk_free(device, d->texture);
	}

	if (d->memory != NULL) {
		if (d->memory->allocation != NULL) {
			vmaFreeMemory(device->vma_allocator, d->memory->allocation);
		}
		sk_vk_free(device, d->memory);
	}

	if (d->buffer != VK_NULL_HANDLE && d->allocation != NULL) {
		vmaDestroyBuffer(device->vma_allocator, d->buffer, d->allocation);
	}

	if (d->shader != NULL) {
		sk_vk_free(device, d->shader->words);
		sk_vk_free(device, d->shader);
	}

	if (d->descriptor_set != VK_NULL_HANDLE) {
		if (d->descriptor_pool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(device->device, d->descriptor_pool, NULL);
		} else {
			sk_mutex_lock(device->descriptor_pool_mutex);
			vkFreeDescriptorSets(device->device, device->descriptor_pool, 1, &d->descriptor_set);
			sk_mutex_unlock(device->descriptor_pool_mutex);
		}
	}
	for (u32 s = 0u; s < d->set_layout_count; ++s) {
		vkDestroyDescriptorSetLayout(device->device, d->set_layouts[s], NULL);
	}
	sk_vk_free(device, d->set_layouts);

	if (d->pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(device->device, d->pipeline, NULL);
	}
	if (d->pipeline_layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(device->device, d->pipeline_layout, NULL);
	}
	if (d->sampler != VK_NULL_HANDLE) {
		vkDestroySampler(device->device, d->sampler, NULL);
	}
	if (d->render_pass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(device->device, d->render_pass, NULL);
	}
	if (d->framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(device->device, d->framebuffer, NULL);
	}
	if (d->query_pool != VK_NULL_HANDLE) {
		vkDestroyQueryPool(device->device, d->query_pool, NULL);
	}
	if (d->acceleration_structure != VK_NULL_HANDLE) {
		vkDestroyAccelerationStructureKHR(device->device, d->acceleration_structure, NULL);
	}
}

void sk_vk_flush_destructors(sk_vk_device_t* device) {
	if (device->destructor_count == 0u || !device->device_created) {
		device->destructor_count = 0u;
		return;
	}
	for (u32 i = 0u; i < device->destructor_count; ++i) {
		sk_vk_flush_one(device, &device->destructors[i]);
	}
	device->destructor_count = 0u;
}

/* Advance the frame counter and free resources that are at least two frames old. */
static void sk_vkrd_advance_frame(sk_vk_device_t* device) {
	++device->frame_index;
	if (device->destructor_count == 0u) {
		return;
	}
	u64 cutoff = device->frame_index >= (u64)SK_VK_FRAMES_IN_FLIGHT ? device->frame_index - (u64)SK_VK_FRAMES_IN_FLIGHT : 0u;
	u32 write_index = 0u;
	for (u32 i = 0u; i < device->destructor_count; ++i) {
		if (device->destructors[i].frame <= cutoff) {
			sk_vk_flush_one(device, &device->destructors[i]);
		} else {
			device->destructors[write_index++] = device->destructors[i];
		}
	}
	device->destructor_count = write_index;
}

/* ------------------------------------------------------------------ */
/* Queue / submit internals                                            */
/* ------------------------------------------------------------------ */

static sk_vk_queue_context_t* sk_vk_queue_context_for_type(sk_vk_device_t* device, u32 queue_type) {
	if ((queue_type & (u32)SK_QUEUE_TYPE_COMPUTE) != 0u && (queue_type & (u32)SK_QUEUE_TYPE_GRAPHICS) == 0u) {
		return device->compute_queue;
	}
	if ((queue_type & (u32)SK_QUEUE_TYPE_TRANSFER) != 0u && (queue_type & (u32)SK_QUEUE_TYPE_GRAPHICS) == 0u && (queue_type & (u32)SK_QUEUE_TYPE_COMPUTE) == 0u) {
		return device->transfer_queue;
	}
	return device->graphics_queue;
}

sk_vk_queue_context_t* sk_vk_device_queue_context(sk_vk_device_t* device, u32 queue_type) {
	return sk_vk_queue_context_for_type(device, queue_type);
}

u32 sk_vk_device_graphics_family(sk_vk_device_t* device) {
	return device->selected_adapter->graphics_family;
}

u32 sk_vk_device_present_family(sk_vk_device_t* device) {
	return device->selected_adapter->present_family;
}

u32 sk_vk_device_compute_family(sk_vk_device_t* device) {
	return device->selected_adapter->compute_family;
}

u32 sk_vk_device_transfer_family(sk_vk_device_t* device) {
	return device->selected_adapter->transfer_family;
}

static VkResult sk_vk_queue_submit(sk_vk_queue_t* queue, VkSubmitInfo* submit_info, VkFence fence) {
	sk_mutex_lock(queue->context->mutex);
	VkResult result = vkQueueSubmit(queue->context->vk_queue, 1, submit_info, fence);
	sk_mutex_unlock(queue->context->mutex);
	return result;
}

/* ------------------------------------------------------------------ */
/* MakeImageCreateInfo helper                                          */
/* ------------------------------------------------------------------ */

void sk_vk_make_image_create_info(const sk_texture_desc_t* desc, VkImageCreateInfo* out) {
	VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	info.imageType = desc->extent.depth > 1u ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
	info.extent.width = desc->extent.width;
	info.extent.height = desc->extent.height;
	info.extent.depth = desc->extent.depth;
	info.format = sk_vk_to_vk_format(desc->format);
	info.mipLevels = desc->mip_levels;
	info.arrayLayers = desc->array_layers;
	info.samples = sk_vk_cast_sample_count(desc->sample_count);
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = sk_vk_get_image_usage_flags(desc->usage_flags);
	info.flags = desc->cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	*out = info;
}

VkResult sk_vk_create_shader_module(sk_vk_device_t* device, const sk_vk_shader_t* shader, VkShaderStageFlagBits stage, VkShaderModule* out_module) {
	(void)stage;
	VkShaderModuleCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
	create_info.codeSize = (size_t)shader->word_count * sizeof(u32);
	create_info.pCode = shader->words;
	return vkCreateShaderModule(device->device, &create_info, NULL, out_module);
}

/* ------------------------------------------------------------------ */
/* Adapter rating / creation                                           */
/* ------------------------------------------------------------------ */

static void sk_vkrd_rate_physical_device(sk_vk_device_t* device, sk_vk_adapter_t* adapter) {
	VkPhysicalDeviceProperties* props = &adapter->device_properties.properties;

	u32 score = props->limits.maxImageDimension2D / 1024u;
	if (props->deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
		score += 3000u;
	} else if (props->deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
		score += 500u;
	}

	u32 family_count = 0u;
	vkGetPhysicalDeviceQueueFamilyProperties(adapter->physical, &family_count, NULL);
	VkQueueFamilyProperties* families = (VkQueueFamilyProperties*)device->allocator->alloc(device->allocator->instance, (size_t)family_count * sizeof(VkQueueFamilyProperties));
	if (family_count > 0u && families == NULL) {
		return;
	}
	if (family_count > 0u) {
		vkGetPhysicalDeviceQueueFamilyProperties(adapter->physical, &family_count, families);
	}

	bool has_graphics = false;
	bool has_compute = false;
	bool has_transfer = false;
	bool has_present = false;

	for (u32 i = 0u; i < family_count; ++i) {
		bool has_present_family = sk_vk_platform_get_presentation_support(device->instance, adapter->physical, i);
		bool has_graphics_family = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u;

		if (has_graphics_family && adapter->graphics_family == 0xFFFFFFFFu) {
			adapter->graphics_family = i;
		}
		if (has_present_family && adapter->present_family == 0xFFFFFFFFu) {
			adapter->present_family = i;
		}
		if (has_present_family) {
			has_present = true;
		}
		if (has_graphics_family) {
			has_graphics = true;
		}
		if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u) {
			has_compute = true;
		}
		if ((families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0u) {
			has_transfer = true;
		}
	}

	/* Dedicated async compute: has compute but no graphics. */
	adapter->compute_family = 0xFFFFFFFFu;
	for (u32 i = 0u; i < family_count; ++i) {
		if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0u) {
			adapter->compute_family = i;
			break;
		}
	}
	if (adapter->compute_family == 0xFFFFFFFFu) {
		adapter->compute_family = adapter->graphics_family;
	}

	/* Dedicated transfer: has transfer but no graphics and no compute. */
	adapter->transfer_family = 0xFFFFFFFFu;
	for (u32 i = 0u; i < family_count; ++i) {
		if ((families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0u && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0u &&
			(families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0u) {
			adapter->transfer_family = i;
			break;
		}
	}
	if (adapter->transfer_family == 0xFFFFFFFFu) {
		adapter->transfer_family = adapter->graphics_family;
	}

	if (has_compute) {
		score += 100u;
	}
	if (has_transfer) {
		score += 100u;
	}
	if (!has_graphics || !has_present) {
		score = 0u;
	}

	adapter->score = score;
	device->allocator->free(device->allocator->instance, families);
}

static sk_vk_adapter_t* sk_vkrd_create_adapter(sk_vk_device_t* device, VkPhysicalDevice physical) {
	sk_vk_adapter_t* adapter = (sk_vk_adapter_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_adapter_t));
	if (adapter == NULL) {
		return NULL;
	}
	memset(adapter, 0, sizeof(*adapter));
	adapter->device = device;
	adapter->physical = physical;
	adapter->graphics_family = 0xFFFFFFFFu;
	adapter->compute_family = 0xFFFFFFFFu;
	adapter->transfer_family = 0xFFFFFFFFu;
	adapter->present_family = 0xFFFFFFFFu;

	/* Property chain (pNext linked, mirrors main). */
	adapter->acceleration_structure_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
	adapter->ray_tracing_pipeline_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
	adapter->ray_tracing_pipeline_props.pNext = &adapter->acceleration_structure_props;
	adapter->conservative_raster_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT;
	adapter->conservative_raster_props.pNext = &adapter->ray_tracing_pipeline_props;
	adapter->device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	adapter->device_properties.pNext = &adapter->conservative_raster_props;
	vkGetPhysicalDeviceProperties2(physical, &adapter->device_properties);

	/* Feature chain (pNext linked, mirrors main). */
	adapter->ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
	adapter->acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	adapter->acceleration_structure_features.pNext = &adapter->ray_query_features;
	adapter->ray_tracing_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	adapter->ray_tracing_pipeline_features.pNext = &adapter->acceleration_structure_features;
	adapter->buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	adapter->buffer_device_address_features.pNext = &adapter->ray_tracing_pipeline_features;
	adapter->draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
	adapter->draw_parameters_features.pNext = &adapter->buffer_device_address_features;
	adapter->indexing_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
	adapter->indexing_features.pNext = &adapter->draw_parameters_features;
	adapter->maintenance4_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES_KHR;
	adapter->maintenance4_features.pNext = &adapter->indexing_features;
	adapter->multiview_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
	adapter->multiview_features.pNext = &adapter->maintenance4_features;
	adapter->device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	adapter->device_features.pNext = &adapter->multiview_features;
	vkGetPhysicalDeviceFeatures2(physical, &adapter->device_features);

	sk_vkrd_rate_physical_device(device, adapter);
	return adapter;
}

/* ------------------------------------------------------------------ */
/* Device lifecycle                                                    */
/* ------------------------------------------------------------------ */

static sk_render_device_t sk_vkrd_init(void_ptr_t context, const sk_device_init_desc_t* desc) {
	if (volkInitialize() != VK_SUCCESS) {
		sk_vkrd_log_error("vulkan cannot be initialized");
		return sk_render_device_t_zero();
	}

	(void)desc->enable_debug_layers;

	bool enable_debug = true; //desc != NULL && desc->enable_debug_layers;

	VkApplicationInfo application_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
	application_info.pApplicationName = "Skore Engine";
	application_info.applicationVersion = 0;
	application_info.pEngineName = "Skore Engine";
	application_info.engineVersion = 0;
	application_info.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	create_info.pApplicationInfo = &application_info;

	static const_chr_t validation_layers[1] = {"VK_LAYER_KHRONOS_validation"};
	bool validation_enabled = false;
	VkDebugUtilsMessengerCreateInfoEXT debug_messenger_ci = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};

	if (enable_debug && sk_vk_query_layer_properties(validation_layers, 1u)) {
		validation_enabled = true;
		debug_messenger_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
											 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug_messenger_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
										 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debug_messenger_ci.pfnUserCallback = &sk_vkrd_debug_callback;
		create_info.enabledLayerCount = 1u;
		create_info.ppEnabledLayerNames = validation_layers;
		create_info.pNext = &debug_messenger_ci;
	}

	const_chr_t required_extensions[8];
	u32 required_count = 0u;
	sk_vk_platform_get_required_instance_extensions(required_extensions, 8u, &required_count);

	static const_chr_t debug_utils_extension[1] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
	bool debug_utils_present = enable_debug && sk_vk_query_instance_extensions(debug_utils_extension, 1u);
	if (debug_utils_present) {
		required_extensions[required_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}

#if defined(__APPLE__)
	static const_chr_t portability_extension[1] = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
	if (sk_vk_query_instance_extensions(portability_extension, 1u)) {
		required_extensions[required_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
		create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}
#endif

	if (!sk_vk_query_instance_extensions(required_extensions, required_count)) {
		sk_vkrd_log_error("Required instance extensions not found");
		return sk_render_device_t_zero();
	}

	create_info.enabledExtensionCount = required_count;
	create_info.ppEnabledExtensionNames = required_extensions;

	VkInstance instance = VK_NULL_HANDLE;
	VkResult result = vkCreateInstance(&create_info, NULL, &instance);
	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Error on vkCreateInstance");
		return sk_render_device_t_zero();
	}
	volkLoadInstance(instance);

	sk_vk_device_t* device = (sk_vk_device_t*)sk_allocator_default()->alloc(NULL, sizeof(sk_vk_device_t));
	if (device == NULL) {
		vkDestroyInstance(instance, NULL);
		return sk_render_device_t_zero();
	}
	memset(device, 0, sizeof(*device));
	device->context = context;
	device->app_api = plugin_app_api;
	device->allocator = sk_allocator_default();
	device->instance = instance;
	device->validation_layers_enabled = validation_enabled;
	device->debug_utils_extension_present = debug_utils_present;
	device->descriptor_pool_mutex = sk_mutex_create();

	if (validation_enabled) {
		vkCreateDebugUtilsMessengerEXT(instance, &debug_messenger_ci, NULL, &device->debug_messenger);
	}

	u32 physical_device_count = 0u;
	vkEnumeratePhysicalDevices(instance, &physical_device_count, NULL);
	if (physical_device_count == 0u) {
		sk_vkrd_log_error("No Vulkan physical devices found");
		sk_vkrd_destroy(sk_render_device_t_from_ptr(device));
		return sk_render_device_t_zero();
	}

	VkPhysicalDevice* physical_devices = (VkPhysicalDevice*)device->allocator->alloc(device->allocator->instance, (size_t)physical_device_count * sizeof(VkPhysicalDevice));
	if (physical_devices == NULL) {
		sk_vkrd_destroy(sk_render_device_t_from_ptr(device));
		return sk_render_device_t_zero();
	}
	vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices);

	device->adapters = (sk_vk_adapter_t**)device->allocator->alloc(device->allocator->instance, (size_t)physical_device_count * sizeof(sk_vk_adapter_t*));
	if (device->adapters == NULL) {
		device->allocator->free(device->allocator->instance, physical_devices);
		sk_vkrd_destroy(sk_render_device_t_from_ptr(device));
		return sk_render_device_t_zero();
	}
	device->adapter_count = physical_device_count;
	for (u32 i = 0u; i < physical_device_count; ++i) {
		device->adapters[i] = sk_vkrd_create_adapter(device, physical_devices[i]);
	}
	device->allocator->free(device->allocator->instance, physical_devices);

	return sk_render_device_t_from_ptr(device);
}

static void sk_vkrd_destroy_queue_contexts(sk_vk_device_t* device) {
	for (u32 i = 0u; i < device->queue_context_count; ++i) {
		if (device->queue_contexts[i]->mutex != NULL) {
			sk_mutex_destroy(device->queue_contexts[i]->mutex);
		}
		device->allocator->free(device->allocator->instance, device->queue_contexts[i]);
	}
	device->allocator->free(device->allocator->instance, device->queue_contexts);
	device->queue_contexts = NULL;
	device->queue_context_count = 0u;
	device->graphics_queue = NULL;
	device->present_queue = NULL;
	device->compute_queue = NULL;
	device->transfer_queue = NULL;
}

static void sk_vkrd_destroy(sk_render_device_t dev) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL) {
		return;
	}

	if (device->device_created) {
		vkDeviceWaitIdle(device->device);
		sk_vk_flush_destructors(device);
		vmaDestroyAllocator(device->vma_allocator);
		vkDestroyDescriptorPool(device->device, device->descriptor_pool, NULL);
		sk_vkrd_destroy_queue_contexts(device);
		vkDestroyDevice(device->device, NULL);
		device->device_created = false;
	}

	if (device->validation_layers_enabled && device->debug_messenger != VK_NULL_HANDLE) {
		vkDestroyDebugUtilsMessengerEXT(device->instance, device->debug_messenger, NULL);
	}
	vkDestroyInstance(device->instance, NULL);

	for (u32 i = 0u; i < device->adapter_count; ++i) {
		device->allocator->free(device->allocator->instance, device->adapters[i]);
	}
	device->allocator->free(device->allocator->instance, device->adapters);
	sk_mutex_destroy(device->descriptor_pool_mutex);
	device->allocator->free(device->allocator->instance, device->destructors);
	device->allocator->free(device->allocator->instance, device);
}

static i32 sk_vkrd_wait_idle(sk_render_device_t dev) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created) {
		return 0;
	}
	VkResult result = vkDeviceWaitIdle(device->device);
	sk_vk_flush_destructors(device);
	return result == VK_SUCCESS ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Adapter queries                                                     */
/* ------------------------------------------------------------------ */

static u32 sk_vkrd_get_adapter_count(sk_render_device_t dev) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL) {
		return 0u;
	}
	return device->adapter_count;
}

static sk_adapter_t sk_vkrd_get_adapter(sk_render_device_t dev, u32 index) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || index >= device->adapter_count) {
		return sk_adapter_t_zero();
	}
	return sk_adapter_t_from_ptr(device->adapters[index]);
}

static u32 sk_vkrd_get_adapter_score(sk_render_device_t dev, sk_adapter_t adapter) {
	(void)dev;
	sk_vk_adapter_t* vulkan_adapter = (sk_vk_adapter_t*)sk_adapter_t_to_ptr(adapter);
	if (vulkan_adapter == NULL) {
		return 0u;
	}
	return vulkan_adapter->score;
}

static const_chr_t sk_vkrd_get_adapter_name(sk_render_device_t dev, sk_adapter_t adapter) {
	(void)dev;
	sk_vk_adapter_t* vulkan_adapter = (sk_vk_adapter_t*)sk_adapter_t_to_ptr(adapter);
	if (vulkan_adapter == NULL) {
		return "";
	}
	return vulkan_adapter->device_properties.properties.deviceName;
}

static sk_device_properties_t sk_vkrd_get_properties(sk_render_device_t dev) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL) {
		sk_device_properties_t props = {0};
		return props;
	}
	return device->properties;
}

static sk_device_features_t sk_vkrd_get_features(sk_render_device_t dev) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL) {
		sk_device_features_t features = {0};
		return features;
	}
	return device->features;
}

static sk_graphics_api_t sk_vkrd_get_api(sk_render_device_t dev) {
	(void)dev;
	return SK_GRAPHICS_API_VULKAN;
}

static u32 sk_vkrd_get_memory_budgets(sk_render_device_t dev, sk_memory_heap_budget_t* out_budgets, u32 max_count) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || out_budgets == NULL || max_count == 0u) {
		return 0u;
	}

	VkPhysicalDeviceMemoryProperties mem_props = {0};
	vkGetPhysicalDeviceMemoryProperties(device->selected_adapter->physical, &mem_props);

	VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {0};
	vmaGetHeapBudgets(device->vma_allocator, budgets);

	u32 heap_count = mem_props.memoryHeapCount;
	if (heap_count > max_count) {
		heap_count = max_count;
	}
	for (u32 i = 0u; i < heap_count; ++i) {
		out_budgets[i].usage = budgets[i].usage;
		out_budgets[i].budget = budgets[i].budget;
		out_budgets[i].device_local = (mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u;
	}
	return heap_count;
}

/* ------------------------------------------------------------------ */
/* Adapter selection (logical device + queues + VMA + pools)           */
/* ------------------------------------------------------------------ */

static void sk_vkrd_chain_feature(void* feature, VkPhysicalDeviceFeatures2* device_features2) {
	VkBaseInStructure* base = SK_CONST_CAST(VkBaseInStructure*, feature);
	base->pNext = SK_CONST_CAST(const VkBaseInStructure*, device_features2->pNext);
	device_features2->pNext = feature;
}

static bool sk_vkrd_add_extension(sk_vk_adapter_t* adapter, const_chr_t extension, const_chr_t* names, u32* count, u32 capacity, void* feature,
								  VkPhysicalDeviceFeatures2* device_features2) {
	if (!sk_vk_device_extension_present(adapter->physical, extension)) {
		return false;
	}
	for (u32 i = 0u; i < *count; ++i) {
		if (strcmp(names[i], extension) == 0) {
			return true;
		}
	}
	if (*count >= capacity) {
		return false;
	}
	if (feature != NULL) {
		sk_vkrd_chain_feature(feature, device_features2);
	}
	names[*count] = extension;
	++(*count);
	return true;
}

static sk_vk_queue_context_t* sk_vkrd_make_queue_context(sk_vk_device_t* device, VkQueue queue) {
	sk_vk_queue_context_t* context = (sk_vk_queue_context_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_queue_context_t));
	if (context == NULL) {
		return NULL;
	}
	context->vk_queue = queue;
	context->mutex = sk_mutex_create();

	sk_vk_queue_context_t** grown = (sk_vk_queue_context_t**)device->allocator->realloc(device->allocator->instance, device->queue_contexts,
																						(size_t)(device->queue_context_count + 1u) * sizeof(sk_vk_queue_context_t*));
	if (grown == NULL) {
		sk_mutex_destroy(context->mutex);
		device->allocator->free(device->allocator->instance, context);
		return NULL;
	}
	device->queue_contexts = grown;
	device->queue_contexts[device->queue_context_count++] = context;
	return context;
}

static i32 sk_vkrd_select_adapter(sk_render_device_t dev, sk_adapter_t adapter_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_adapter_t* adapter = (sk_vk_adapter_t*)sk_adapter_t_to_ptr(adapter_handle);
	if (device == NULL || adapter == NULL) {
		return -1;
	}

	if (device->device_created) {
		vkDeviceWaitIdle(device->device);
		sk_vk_flush_destructors(device);
		vmaDestroyAllocator(device->vma_allocator);
		vkDestroyDescriptorPool(device->device, device->descriptor_pool, NULL);
		sk_vkrd_destroy_queue_contexts(device);
		vkDestroyDevice(device->device, NULL);
		device->device_created = false;
	}

	device->selected_adapter = adapter;

	/* --- Limits / properties --- */
	const VkPhysicalDeviceLimits* limits = &adapter->device_properties.properties.limits;
	device->properties.limits.max_texture_size = limits->maxImageDimension2D;
	device->properties.limits.max_texture_3d_size = limits->maxImageDimension3D;
	device->properties.limits.max_cube_map_size = limits->maxImageDimensionCube;
	device->properties.limits.max_viewport_dimensions[0] = limits->maxViewportDimensions[0];
	device->properties.limits.max_viewport_dimensions[1] = limits->maxViewportDimensions[1];
	device->properties.limits.max_compute_work_group_count[0] = limits->maxComputeWorkGroupCount[0];
	device->properties.limits.max_compute_work_group_count[1] = limits->maxComputeWorkGroupCount[1];
	device->properties.limits.max_compute_work_group_count[2] = limits->maxComputeWorkGroupCount[2];
	device->properties.limits.max_compute_work_group_size[0] = limits->maxComputeWorkGroupSize[0];
	device->properties.limits.max_compute_work_group_size[1] = limits->maxComputeWorkGroupSize[1];
	device->properties.limits.max_compute_work_group_size[2] = limits->maxComputeWorkGroupSize[2];
	device->properties.limits.max_compute_invocations = limits->maxComputeWorkGroupInvocations;
	device->properties.limits.max_vertex_input_bindings = limits->maxVertexInputBindings;
	device->properties.limits.max_vertex_input_attributes = limits->maxVertexInputAttributes;
	device->properties.limits.max_attachment_samples = (u32)sk_vk_get_max_usable_sample_count(&adapter->device_properties.properties);
	device->properties.limits.min_memory_map_alignment = limits->minMemoryMapAlignment;
	device->properties.limits.min_uniform_buffer_offset_alignment = limits->minUniformBufferOffsetAlignment;
	device->properties.limits.timestamp_period = limits->timestampPeriod;

	switch (adapter->device_properties.properties.deviceType) {
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
		device->properties.device_type = SK_DEVICE_TYPE_DISCRETE;
		break;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
		device->properties.device_type = SK_DEVICE_TYPE_INTEGRATED;
		break;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
		device->properties.device_type = SK_DEVICE_TYPE_VIRTUAL;
		break;
	case VK_PHYSICAL_DEVICE_TYPE_CPU:
		device->properties.device_type = SK_DEVICE_TYPE_CPU;
		break;
	case VK_PHYSICAL_DEVICE_TYPE_OTHER:
	case VK_PHYSICAL_DEVICE_TYPE_MAX_ENUM:
		device->properties.device_type = SK_DEVICE_TYPE_OTHER;
		break;
	}

	snprintf(device->properties.device_name, sizeof(device->properties.device_name), "%s", adapter->device_properties.properties.deviceName);
	switch (adapter->device_properties.properties.vendorID) {
	case 0x10DEu:
		snprintf(device->properties.vendor_name, sizeof(device->properties.vendor_name), "%s", "NVIDIA Corporation");
		break;
	case 0x1002u:
		snprintf(device->properties.vendor_name, sizeof(device->properties.vendor_name), "%s", "Advanced Micro Devices, Inc.");
		break;
	case 0x8086u:
		snprintf(device->properties.vendor_name, sizeof(device->properties.vendor_name), "%s", "Intel Corporation");
		break;
	case 0x13B5u:
		snprintf(device->properties.vendor_name, sizeof(device->properties.vendor_name), "%s", "ARM Limited");
		break;
	default:
		snprintf(device->properties.vendor_name, sizeof(device->properties.vendor_name), "%s", "Unknown Vendor");
		break;
	}
	snprintf(device->properties.driver_version, sizeof(device->properties.driver_version), "0x%08X", adapter->device_properties.properties.driverVersion);

	/* --- Feature enable structs (chained onto device_features2.pNext) --- */
	VkPhysicalDeviceFeatures2 device_features2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
	device_features2.features.samplerAnisotropy = adapter->device_features.features.samplerAnisotropy;
	device_features2.features.sampleRateShading = adapter->device_features.features.sampleRateShading;
	device_features2.features.depthClamp = VK_TRUE;
	if (adapter->device_features.features.fillModeNonSolid != VK_FALSE) {
		device_features2.features.fillModeNonSolid = VK_TRUE;
	}
	if (adapter->device_features.features.wideLines != VK_FALSE) {
		device_features2.features.wideLines = VK_TRUE;
	}
	if (adapter->device_features.features.fragmentStoresAndAtomics != VK_FALSE) {
		device_features2.features.fragmentStoresAndAtomics = VK_TRUE;
	}
	if (adapter->device_features.features.vertexPipelineStoresAndAtomics != VK_FALSE) {
		device_features2.features.vertexPipelineStoresAndAtomics = VK_TRUE;
	}

	VkPhysicalDeviceMaintenance4FeaturesKHR maintenance4 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES_KHR};
	maintenance4.maintenance4 = VK_TRUE;

	VkPhysicalDeviceDescriptorIndexingFeatures indexing = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
	indexing.descriptorBindingPartiallyBound = adapter->indexing_features.descriptorBindingPartiallyBound;
	indexing.runtimeDescriptorArray = adapter->indexing_features.runtimeDescriptorArray;
	indexing.shaderSampledImageArrayNonUniformIndexing = adapter->indexing_features.shaderSampledImageArrayNonUniformIndexing;
	indexing.shaderStorageBufferArrayNonUniformIndexing = adapter->indexing_features.shaderStorageBufferArrayNonUniformIndexing;
	indexing.shaderUniformBufferArrayNonUniformIndexing = adapter->indexing_features.shaderUniformBufferArrayNonUniformIndexing;
	indexing.descriptorBindingSampledImageUpdateAfterBind = adapter->indexing_features.descriptorBindingSampledImageUpdateAfterBind;
	indexing.descriptorBindingStorageImageUpdateAfterBind = adapter->indexing_features.descriptorBindingStorageImageUpdateAfterBind;
	indexing.descriptorBindingStorageBufferUpdateAfterBind = adapter->indexing_features.descriptorBindingStorageBufferUpdateAfterBind;
	indexing.descriptorBindingUniformBufferUpdateAfterBind = adapter->indexing_features.descriptorBindingUniformBufferUpdateAfterBind;

	bool bindless_texture = indexing.shaderSampledImageArrayNonUniformIndexing != VK_FALSE && indexing.descriptorBindingPartiallyBound != VK_FALSE &&
							indexing.runtimeDescriptorArray != VK_FALSE && indexing.descriptorBindingSampledImageUpdateAfterBind != VK_FALSE &&
							indexing.descriptorBindingStorageImageUpdateAfterBind != VK_FALSE;
	bool bindless_buffer = indexing.shaderStorageBufferArrayNonUniformIndexing != VK_FALSE && indexing.descriptorBindingPartiallyBound != VK_FALSE &&
						   indexing.runtimeDescriptorArray != VK_FALSE && indexing.descriptorBindingStorageBufferUpdateAfterBind != VK_FALSE &&
						   indexing.descriptorBindingStorageImageUpdateAfterBind != VK_FALSE && indexing.descriptorBindingUniformBufferUpdateAfterBind != VK_FALSE;
	bool bindless_sampler = indexing.runtimeDescriptorArray != VK_FALSE && indexing.descriptorBindingPartiallyBound != VK_FALSE;

	device->features.tessellation_shader = adapter->device_features.features.tessellationShader != VK_FALSE;
	device->features.geometry_shader = adapter->device_features.features.geometryShader != VK_FALSE;
	device->features.compute_shader = true;
	device->features.multi_viewport = adapter->device_features.features.multiViewport != VK_FALSE;
	device->features.texture_compression_bc = adapter->device_features.features.textureCompressionBC != VK_FALSE;
	device->features.texture_compression_etc2 = adapter->device_features.features.textureCompressionETC2 != VK_FALSE;
	device->features.texture_compression_astc = adapter->device_features.features.textureCompressionASTC_LDR != VK_FALSE;
	device->features.independent_blend = adapter->device_features.features.independentBlend != VK_FALSE;
	device->features.bindless_texture_supported = bindless_texture;
	device->features.bindless_buffer_supported = bindless_buffer;
	device->features.bindless_sampler_supported = bindless_sampler;
	device->features.multiview_enabled = adapter->multiview_features.multiview != VK_FALSE;

	VkPhysicalDeviceRayQueryFeaturesKHR ray_query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
	ray_query.rayQuery = VK_TRUE;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
	acceleration_structure.accelerationStructure = VK_TRUE;
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
	ray_tracing_pipeline.rayTracingPipeline = VK_TRUE;
	VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
	buffer_device_address.bufferDeviceAddress = VK_TRUE;
	VkPhysicalDeviceShaderDrawParametersFeatures draw_parameters = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES};
	draw_parameters.shaderDrawParameters = adapter->draw_parameters_features.shaderDrawParameters;
	VkPhysicalDeviceMultiviewFeatures multiview = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
	multiview.multiview = VK_TRUE;
	VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentric = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR};
	barycentric.fragmentShaderBarycentric = VK_TRUE;

	/* --- Device extensions (mirrors main's AddIfPresent sequence) --- */
	const_chr_t extensions[32];
	u32 extension_count = 0u;

	if (!sk_vkrd_add_extension(adapter, VK_KHR_SWAPCHAIN_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2)) {
		sk_vkrd_log_error("Device does not support VK_KHR_swapchain");
		return -1;
	}
	if (!sk_vkrd_add_extension(adapter, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2)) {
		sk_vkrd_log_error("Device does not support VK_KHR_create_renderpass2");
		return -1;
	}

	sk_vkrd_add_extension(adapter, VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);

	device->features.resolve_depth = sk_vkrd_add_extension(adapter, VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);

	sk_vkrd_add_extension(adapter, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);
	sk_vkrd_add_extension(adapter, VK_KHR_MAINTENANCE_4_EXTENSION_NAME, extensions, &extension_count, 32u, &maintenance4, &device_features2);

	device->features.buffer_device_address = sk_vkrd_add_extension(adapter, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, extensions, &extension_count, 32u, &buffer_device_address,
																   &device_features2);
	device->features.draw_indirect_count = sk_vkrd_add_extension(adapter, VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);
	device->features.memory_budget = sk_vkrd_add_extension(adapter, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);
	device->features.fragment_shader_barycentric = sk_vkrd_add_extension(adapter, VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME, extensions, &extension_count, 32u,
																		 &barycentric, &device_features2);

	device->features.ray_tracing = sk_vkrd_add_extension(adapter, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2) &&
								   sk_vkrd_add_extension(adapter, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);

	if (device->features.ray_tracing) {
		sk_vkrd_add_extension(adapter, VK_KHR_RAY_QUERY_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);
		sk_vkrd_add_extension(adapter, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);
	}

	sk_vkrd_add_extension(adapter, VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);
	sk_vkrd_add_extension(adapter, VK_KHR_SPIRV_1_4_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);

	if (bindless_texture || bindless_buffer || bindless_sampler) {
		sk_vkrd_chain_feature(&indexing, &device_features2);
	}
	if (device->features.ray_tracing) {
		sk_vkrd_chain_feature(&ray_query, &device_features2);
		sk_vkrd_chain_feature(&acceleration_structure, &device_features2);
		sk_vkrd_chain_feature(&ray_tracing_pipeline, &device_features2);
	}
	if (device->features.multiview_enabled) {
		sk_vkrd_chain_feature(&multiview, &device_features2);
	}
	if (draw_parameters.shaderDrawParameters != VK_FALSE) {
		sk_vkrd_chain_feature(&draw_parameters, &device_features2);
	}
#if defined(__APPLE__)
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif
	sk_vkrd_add_extension(adapter, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME, extensions, &extension_count, 32u, NULL, &device_features2);
#endif

	/* --- Queue create infos (unique families) --- */
	u32 family_values[4];
	u32 family_count = 0u;
	{
		u32 families_to_add[4];
		u32 to_add_count = 0u;
		families_to_add[to_add_count++] = adapter->graphics_family;
		families_to_add[to_add_count++] = adapter->present_family;
		families_to_add[to_add_count++] = adapter->compute_family;
		families_to_add[to_add_count++] = adapter->transfer_family;
		for (u32 i = 0u; i < to_add_count; ++i) {
			bool present = false;
			for (u32 j = 0u; j < family_count; ++j) {
				if (family_values[j] == families_to_add[i]) {
					present = true;
					break;
				}
			}
			if (!present) {
				family_values[family_count++] = families_to_add[i];
			}
		}
	}

	VkDeviceQueueCreateInfo queue_create_infos[4] = {0};
	float queue_priority = 1.0f;
	for (u32 i = 0u; i < family_count; ++i) {
		queue_create_infos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_create_infos[i].queueFamilyIndex = family_values[i];
		queue_create_infos[i].queueCount = 1u;
		queue_create_infos[i].pQueuePriorities = &queue_priority;
	}

	/* --- Create the logical device --- */
	VkDeviceCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	create_info.pNext = &device_features2;
	create_info.pQueueCreateInfos = queue_create_infos;
	create_info.queueCreateInfoCount = family_count;
	create_info.enabledExtensionCount = extension_count;
	create_info.ppEnabledExtensionNames = extensions;

	VkResult result = vkCreateDevice(adapter->physical, &create_info, NULL, &device->device);
	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create logical device");
		return -1;
	}
	volkLoadDevice(device->device);

	/* --- Queues --- */
	{
		VkQueue queue = VK_NULL_HANDLE;
		vkGetDeviceQueue(device->device, adapter->graphics_family, 0u, &queue);
		device->graphics_queue = sk_vkrd_make_queue_context(device, queue);

		if (adapter->present_family == adapter->graphics_family) {
			device->present_queue = device->graphics_queue;
		} else {
			vkGetDeviceQueue(device->device, adapter->present_family, 0u, &queue);
			device->present_queue = sk_vkrd_make_queue_context(device, queue);
		}

		if (adapter->compute_family == adapter->graphics_family) {
			device->compute_queue = device->graphics_queue;
		} else if (adapter->compute_family == adapter->present_family) {
			device->compute_queue = device->present_queue;
		} else {
			vkGetDeviceQueue(device->device, adapter->compute_family, 0u, &queue);
			device->compute_queue = sk_vkrd_make_queue_context(device, queue);
		}

		if (adapter->transfer_family == adapter->graphics_family) {
			device->transfer_queue = device->graphics_queue;
		} else if (adapter->transfer_family == adapter->present_family) {
			device->transfer_queue = device->present_queue;
		} else if (adapter->transfer_family == adapter->compute_family) {
			device->transfer_queue = device->compute_queue;
		} else {
			vkGetDeviceQueue(device->device, adapter->transfer_family, 0u, &queue);
			device->transfer_queue = sk_vkrd_make_queue_context(device, queue);
		}
	}

	/* --- VMA allocator --- */
	VmaVulkanFunctions vma_functions = {0};
	vma_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vma_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo allocator_info = {0};
	allocator_info.physicalDevice = adapter->physical;
	allocator_info.device = device->device;
	allocator_info.instance = device->instance;
	if (device->features.buffer_device_address) {
		allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	}
	if (device->features.memory_budget) {
		allocator_info.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	}
	allocator_info.pVulkanFunctions = &vma_functions;
	if (vmaCreateAllocator(&allocator_info, &device->vma_allocator) != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create VMA allocator");
		vkDestroyDevice(device->device, NULL);
		device->device_created = false;
		return -1;
	}

	/* --- Shared descriptor pool --- */
	VkDescriptorPoolSize pool_sizes[7];
	u32 pool_size_count = 0u;
	pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5000u};
	pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLER, 5000u};
	pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 5000u};
	pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5000u};
	pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5000u};
	pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5000u};
	if (device->features.ray_tracing) {
		pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 64u};
	}

	VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	pool_info.poolSizeCount = pool_size_count;
	pool_info.pPoolSizes = pool_sizes;
	pool_info.maxSets = 5000u;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	vkCreateDescriptorPool(device->device, &pool_info, NULL, &device->descriptor_pool);

	device->properties.features = device->features;
	device->device_created = true;

	return 0;
}

/* ------------------------------------------------------------------ */
/* Buffer                                                              */
/* ------------------------------------------------------------------ */

static sk_buffer_t sk_vkrd_create_buffer(sk_render_device_t dev, const sk_buffer_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_buffer_t_zero();
	}

	VkBufferCreateInfo buffer_create_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	buffer_create_info.size = desc->size;
	buffer_create_info.usage = sk_vk_get_buffer_usage_flags(desc->usage_flags, device->features.buffer_device_address);
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo alloc_info = {0};
	alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
	if (desc->host_visible) {
		alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		if (desc->persistent_mapped) {
			alloc_info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
	}

	VkBuffer vk_buffer = VK_NULL_HANDLE;
	VmaAllocation vma_allocation = NULL;
	VkResult result = vmaCreateBuffer(device->vma_allocator, &buffer_create_info, &alloc_info, &vk_buffer, &vma_allocation, NULL);
	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create buffer");
		return sk_buffer_t_zero();
	}

	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_buffer_t));
	if (buffer == NULL) {
		vmaDestroyBuffer(device->vma_allocator, vk_buffer, vma_allocation);
		return sk_buffer_t_zero();
	}
	memset(buffer, 0, sizeof(*buffer));
	buffer->device = device;
	buffer->buffer = vk_buffer;
	buffer->allocation = vma_allocation;
	sk_vk_copy_buffer_desc(device, desc, &buffer->desc);

	if (desc->host_visible && desc->persistent_mapped) {
		vmaMapMemory(device->vma_allocator, buffer->allocation, &buffer->mapped_data);
	}

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_BUFFER, (u64)vk_buffer, desc->debug_name);
	return sk_buffer_t_from_ptr(buffer);
}

static void sk_vkrd_destroy_buffer(sk_render_device_t dev, sk_buffer_t buf) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buf);
	if (device == NULL || buffer == NULL) {
		return;
	}

	/* Unmap before deferred destroy: VMA requires MapCount == 0 at
	 * vmaDestroyBuffer (debug assert), including persistently mapped
	 * buffers (mapped at create). */
	if (buffer->mapped_data != NULL) {
		vmaUnmapMemory(device->vma_allocator, buffer->allocation);
		buffer->mapped_data = NULL;
	}

	sk_vk_destructor_t destructor = {0};
	destructor.buffer = buffer->buffer;
	destructor.allocation = buffer->allocation;
	sk_vk_enqueue_destructor(device, &destructor);

	sk_vk_free(device, buffer->desc.debug_name);
	sk_vk_free(device, buffer);
}

static void* sk_vkrd_buffer_map(sk_render_device_t dev, sk_buffer_t buf) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buf);
	if (device == NULL || buffer == NULL) {
		return NULL;
	}
	if (buffer->mapped_data != NULL) {
		return buffer->mapped_data;
	}
	if (!buffer->desc.host_visible) {
		return NULL;
	}
	vmaMapMemory(device->vma_allocator, buffer->allocation, &buffer->mapped_data);
	return buffer->mapped_data;
}

static void sk_vkrd_buffer_unmap(sk_render_device_t dev, sk_buffer_t buf) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buf);
	if (device == NULL || buffer == NULL) {
		return;
	}
	if (buffer->mapped_data == NULL || buffer->desc.persistent_mapped) {
		return;
	}
	vmaUnmapMemory(device->vma_allocator, buffer->allocation);
	buffer->mapped_data = NULL;
}

static sk_buffer_desc_t sk_vkrd_get_buffer_desc(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buf);
	if (buffer == NULL) {
		sk_buffer_desc_t desc = {0};
		return desc;
	}
	return buffer->desc;
}

static void_ptr_t sk_vkrd_get_buffer_mapped_data(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buf);
	if (buffer == NULL) {
		return NULL;
	}
	return buffer->mapped_data;
}

/* ------------------------------------------------------------------ */
/* Texture / view / sampler                                            */
/* ------------------------------------------------------------------ */

static sk_texture_t sk_vkrd_create_texture(sk_render_device_t dev, const sk_texture_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_texture_t_zero();
	}

	VkImageCreateInfo image_create_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	sk_vk_make_image_create_info(desc, &image_create_info);

	VmaAllocationCreateInfo alloc_info = {0};
	alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkImage vk_image = VK_NULL_HANDLE;
	VmaAllocation vma_allocation = NULL;
	VkResult result = vmaCreateImage(device->vma_allocator, &image_create_info, &alloc_info, &vk_image, &vma_allocation, NULL);
	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create texture");
		return sk_texture_t_zero();
	}

	sk_vk_texture_t* texture = (sk_vk_texture_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_texture_t));
	if (texture == NULL) {
		vmaDestroyImage(device->vma_allocator, vk_image, vma_allocation);
		return sk_texture_t_zero();
	}
	memset(texture, 0, sizeof(*texture));
	texture->device = device;
	texture->image = vk_image;
	texture->allocation = vma_allocation;
	texture->is_depth = sk_vk_is_depth_format_vk(image_create_info.format);
	texture->owns_image = true;
	sk_vk_copy_texture_desc(device, desc, &texture->desc);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_IMAGE, (u64)vk_image, desc->debug_name);
	return sk_texture_t_from_ptr(texture);
}

static void sk_vkrd_destroy_texture(sk_render_device_t dev, sk_texture_t tex) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_texture_t* texture = (sk_vk_texture_t*)sk_texture_t_to_ptr(tex);
	if (device == NULL || texture == NULL) {
		return;
	}

	if (texture->owns_image) {
		sk_vk_destructor_t destructor = {0};
		destructor.texture = texture;
		sk_vk_enqueue_destructor(device, &destructor);
	} else {
		/* Swapchain image: the VkImage is owned by the swapchain. */
		sk_vk_free(device, texture->desc.debug_name);
		sk_vk_free(device, texture);
	}
}

static sk_texture_view_t sk_vkrd_create_texture_view(sk_render_device_t dev, const sk_texture_view_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_texture_view_t_zero();
	}

	sk_vk_texture_t* texture = (sk_vk_texture_t*)sk_texture_t_to_ptr(desc->texture);
	if (texture == NULL) {
		return sk_texture_view_t_zero();
	}

	VkImageViewCreateInfo image_view_ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	image_view_ci.image = texture->image;
	image_view_ci.viewType = sk_vk_get_image_view_type(desc->type);
	image_view_ci.format = sk_vk_to_vk_format(texture->desc.format);
	image_view_ci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_ci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_ci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_ci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	image_view_ci.subresourceRange.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(texture->desc.format));

	u32 mip_level_count = desc->mip_level_count;
	if (mip_level_count == UINT32_MAX || (desc->base_mip_level + mip_level_count) > texture->desc.mip_levels) {
		mip_level_count = texture->desc.mip_levels - desc->base_mip_level;
	}
	image_view_ci.subresourceRange.baseMipLevel = desc->base_mip_level;
	image_view_ci.subresourceRange.levelCount = mip_level_count;

	u32 array_layer_count = desc->array_layer_count;
	if (array_layer_count == UINT32_MAX || (desc->base_array_layer + array_layer_count) > texture->desc.array_layers) {
		array_layer_count = texture->desc.array_layers - desc->base_array_layer;
	}
	image_view_ci.subresourceRange.baseArrayLayer = desc->base_array_layer;
	image_view_ci.subresourceRange.layerCount = array_layer_count;

	VkImageView image_view = VK_NULL_HANDLE;
	VkResult result = vkCreateImageView(device->device, &image_view_ci, NULL, &image_view);
	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create image view");
		return sk_texture_view_t_zero();
	}

	sk_vk_texture_view_t* texture_view = (sk_vk_texture_view_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_texture_view_t));
	if (texture_view == NULL) {
		vkDestroyImageView(device->device, image_view, NULL);
		return sk_texture_view_t_zero();
	}
	memset(texture_view, 0, sizeof(*texture_view));
	texture_view->device = device;
	texture_view->image_view = image_view;
	texture_view->texture = texture;
	sk_vk_copy_texture_view_desc(device, desc, &texture_view->desc);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_IMAGE_VIEW, (u64)image_view, desc->debug_name);
	return sk_texture_view_t_from_ptr(texture_view);
}

static void sk_vkrd_destroy_texture_view(sk_render_device_t dev, sk_texture_view_t view) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_texture_view_t* texture_view = (sk_vk_texture_view_t*)sk_texture_view_t_to_ptr(view);
	if (device == NULL || texture_view == NULL) {
		return;
	}
	sk_vk_destructor_t destructor = {0};
	destructor.texture_view = texture_view;
	sk_vk_enqueue_destructor(device, &destructor);
}

static sk_sampler_t sk_vkrd_create_sampler(sk_render_device_t dev, const sk_sampler_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_sampler_t_zero();
	}

	VkSamplerCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	create_info.minFilter = desc->min_filter == SK_FILTER_MODE_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	create_info.magFilter = desc->mag_filter == SK_FILTER_MODE_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	create_info.mipmapMode = desc->mipmap_filter == SK_FILTER_MODE_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
	create_info.addressModeU = sk_vk_convert_address_mode(desc->address_mode_u);
	create_info.addressModeV = sk_vk_convert_address_mode(desc->address_mode_v);
	create_info.addressModeW = sk_vk_convert_address_mode(desc->address_mode_w);
	create_info.mipLodBias = desc->mip_lod_bias;
	create_info.anisotropyEnable = desc->anisotropy_enable ? VK_TRUE : VK_FALSE;
	create_info.maxAnisotropy = desc->max_anisotropy;
	create_info.compareEnable = (desc->compare_enable || desc->min_filter == SK_FILTER_MODE_LINEAR || desc->mag_filter == SK_FILTER_MODE_LINEAR) ? VK_TRUE : VK_FALSE;
	create_info.compareOp = sk_vk_convert_compare_op(desc->compare_op);
	create_info.minLod = desc->min_lod;
	create_info.maxLod = desc->max_lod;
	create_info.borderColor = sk_vk_convert_border_color(desc->border_color);
	create_info.unnormalizedCoordinates = VK_FALSE;

	VkSampler vk_sampler = VK_NULL_HANDLE;
	VkResult result = vkCreateSampler(device->device, &create_info, NULL, &vk_sampler);
	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create sampler");
		return sk_sampler_t_zero();
	}

	sk_vk_sampler_t* sampler = (sk_vk_sampler_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_sampler_t));
	if (sampler == NULL) {
		vkDestroySampler(device->device, vk_sampler, NULL);
		return sk_sampler_t_zero();
	}
	memset(sampler, 0, sizeof(*sampler));
	sampler->device = device;
	sampler->sampler = vk_sampler;
	sk_vk_copy_sampler_desc(device, desc, &sampler->desc);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_SAMPLER, (u64)vk_sampler, desc->debug_name);
	return sk_sampler_t_from_ptr(sampler);
}

static void sk_vkrd_destroy_sampler(sk_render_device_t dev, sk_sampler_t sampler) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_sampler_t* vulkan_sampler = (sk_vk_sampler_t*)sk_sampler_t_to_ptr(sampler);
	if (device == NULL || vulkan_sampler == NULL) {
		return;
	}
	sk_vk_destructor_t destructor = {0};
	destructor.sampler = vulkan_sampler->sampler;
	sk_vk_enqueue_destructor(device, &destructor);
	sk_vk_free(device, vulkan_sampler->desc.debug_name);
	sk_vk_free(device, vulkan_sampler);
}

static sk_texture_desc_t sk_vkrd_get_texture_desc(sk_render_device_t dev, sk_texture_t tex) {
	(void)dev;
	sk_vk_texture_t* texture = (sk_vk_texture_t*)sk_texture_t_to_ptr(tex);
	if (texture == NULL) {
		sk_texture_desc_t desc = {0};
		return desc;
	}
	return texture->desc;
}

static sk_texture_view_desc_t sk_vkrd_get_texture_view_desc(sk_render_device_t dev, sk_texture_view_t view) {
	(void)dev;
	sk_vk_texture_view_t* texture_view = (sk_vk_texture_view_t*)sk_texture_view_t_to_ptr(view);
	if (texture_view == NULL) {
		sk_texture_view_desc_t desc = {0};
		return desc;
	}
	return texture_view->desc;
}

static sk_sampler_desc_t sk_vkrd_get_sampler_desc(sk_render_device_t dev, sk_sampler_t sampler) {
	(void)dev;
	sk_vk_sampler_t* vulkan_sampler = (sk_vk_sampler_t*)sk_sampler_t_to_ptr(sampler);
	if (vulkan_sampler == NULL) {
		sk_sampler_desc_t desc = {0};
		return desc;
	}
	return vulkan_sampler->desc;
}

/* ------------------------------------------------------------------ */
/* Memory (aliasing)                                                   */
/* ------------------------------------------------------------------ */

static sk_memory_t sk_vkrd_create_memory(sk_render_device_t dev, u64 size, u64 alignment, u32 memory_type_bits) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created) {
		return sk_memory_t_zero();
	}

	VkMemoryRequirements mem_requirements = {0};
	mem_requirements.size = size;
	mem_requirements.alignment = alignment;
	mem_requirements.memoryTypeBits = memory_type_bits;

	VmaAllocationCreateInfo alloc_info = {0};
	alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	alloc_info.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;

	VmaAllocation vma_allocation = NULL;
	VkResult result = vmaAllocateMemory(device->vma_allocator, &mem_requirements, &alloc_info, &vma_allocation, NULL);
	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create memory");
		return sk_memory_t_zero();
	}

	sk_vk_memory_t* memory = (sk_vk_memory_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_memory_t));
	if (memory == NULL) {
		vmaFreeMemory(device->vma_allocator, vma_allocation);
		return sk_memory_t_zero();
	}
	memset(memory, 0, sizeof(*memory));
	memory->device = device;
	memory->allocation = vma_allocation;
	memory->size = size;
	memory->memory_type_bits = memory_type_bits;
	return sk_memory_t_from_ptr(memory);
}

static void sk_vkrd_destroy_memory(sk_render_device_t dev, sk_memory_t mem) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_memory_t* memory = (sk_vk_memory_t*)sk_memory_t_to_ptr(mem);
	if (device == NULL || memory == NULL) {
		return;
	}
	sk_vk_destructor_t destructor = {0};
	destructor.memory = memory;
	sk_vk_enqueue_destructor(device, &destructor);
}

static sk_texture_t sk_vkrd_create_aliased_texture(sk_render_device_t dev, const sk_texture_desc_t* desc, sk_memory_t mem, u64 offset) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_memory_t* memory = (sk_vk_memory_t*)sk_memory_t_to_ptr(mem);
	if (device == NULL || !device->device_created || desc == NULL || memory == NULL) {
		return sk_texture_t_zero();
	}

	VkImageCreateInfo image_create_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	sk_vk_make_image_create_info(desc, &image_create_info);

	VkImage vk_image = VK_NULL_HANDLE;
	VkResult result = vmaCreateAliasingImage2(device->vma_allocator, memory->allocation, offset, &image_create_info, &vk_image);
	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create aliased texture");
		return sk_texture_t_zero();
	}

	sk_vk_texture_t* texture = (sk_vk_texture_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_texture_t));
	if (texture == NULL) {
		vkDestroyImage(device->device, vk_image, NULL);
		return sk_texture_t_zero();
	}
	memset(texture, 0, sizeof(*texture));
	texture->device = device;
	texture->image = vk_image;
	texture->allocation = NULL;
	texture->is_depth = sk_vk_is_depth_format_vk(image_create_info.format);
	texture->aliased = true;
	texture->owns_image = true;
	sk_vk_copy_texture_desc(device, desc, &texture->desc);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_IMAGE, (u64)vk_image, desc->debug_name);
	return sk_texture_t_from_ptr(texture);
}

static u64 sk_vkrd_get_memory_size(sk_render_device_t dev, sk_memory_t mem) {
	(void)dev;
	sk_vk_memory_t* memory = (sk_vk_memory_t*)sk_memory_t_to_ptr(mem);
	if (memory == NULL) {
		return 0ull;
	}
	return memory->size;
}

/* ------------------------------------------------------------------ */
/* Shader                                                              */
/* ------------------------------------------------------------------ */

static sk_shader_t sk_vkrd_create_shader(sk_render_device_t dev, const_chr_t src, u32 src_size, u32 shader_stage) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || src == NULL || src_size == 0u) {
		return sk_shader_t_zero();
	}

	u32 word_bytes = (src_size + 3u) & ~3u;
	u32 word_count = word_bytes / 4u;
	u32* words = (u32*)device->allocator->alloc(device->allocator->instance, (size_t)word_bytes);
	if (words == NULL) {
		return sk_shader_t_zero();
	}
	memset(words, 0, (size_t)word_bytes);
	memcpy(words, src, (size_t)src_size);

	sk_vk_shader_t* shader = (sk_vk_shader_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_shader_t));
	if (shader == NULL) {
		device->allocator->free(device->allocator->instance, words);
		return sk_shader_t_zero();
	}
	memset(shader, 0, sizeof(*shader));
	shader->device = device;
	shader->stage = shader_stage;
	shader->word_count = word_count;
	shader->words = words;
	return sk_shader_t_from_ptr(shader);
}

static void sk_vkrd_destroy_shader(sk_render_device_t dev, sk_shader_t shdr) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_shader_t* shader = (sk_vk_shader_t*)sk_shader_t_to_ptr(shdr);
	if (device == NULL || shader == NULL) {
		return;
	}
	device->allocator->free(device->allocator->instance, shader->words);
	device->allocator->free(device->allocator->instance, shader);
}

/* ------------------------------------------------------------------ */
/* Pipelines                                                           */
/* ------------------------------------------------------------------ */

static void sk_vkrd_cleanup_pipeline_layout(sk_vk_device_t* device, VkPipelineLayout pipeline_layout, VkDescriptorSetLayout* set_layouts, u32 set_layout_count) {
	if (pipeline_layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(device->device, pipeline_layout, NULL);
	}
	for (u32 i = 0u; i < set_layout_count; ++i) {
		vkDestroyDescriptorSetLayout(device->device, set_layouts[i], NULL);
	}
	device->allocator->free(device->allocator->instance, set_layouts);
}

static u32 sk_vkrd_render_pass_color_attachment_count(const sk_vk_render_pass_t* render_pass) {
	u32 count = 0u;
	for (u32 i = 0u; i < render_pass->desc.attachment_count; ++i) {
		if (!sk_vk_is_depth_format(render_pass->desc.attachments[i].format)) {
			++count;
		}
	}
	return count;
}

static VkBool32 adapter_sample_rate_shading(sk_vk_device_t* device) {
	if (device->selected_adapter == NULL) {
		return VK_FALSE;
	}
	return device->selected_adapter->device_features.features.sampleRateShading;
}

static sk_pipeline_t sk_vkrd_create_graphics_pipeline(sk_render_device_t dev, const sk_graphics_pipeline_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_pipeline_t_zero();
	}
	sk_vk_render_pass_t* render_pass = (sk_vk_render_pass_t*)sk_render_pass_t_to_ptr(desc->render_pass);
	if (render_pass == NULL) {
		return sk_pipeline_t_zero();
	}

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkDescriptorSetLayout* set_layouts = NULL;
	u32 set_layout_count = 0u;
	VkResult layout_result = sk_vk_create_pipeline_layout(device, &desc->pipeline, desc->allow_immediate_set, desc->descriptor_sets_override, desc->descriptor_sets_override_count,
														  &pipeline_layout, &set_layouts, &set_layout_count);
	if (layout_result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create graphics pipeline layout");
		return sk_pipeline_t_zero();
	}

	sk_vk_shader_t* vs = (sk_vk_shader_t*)sk_shader_t_to_ptr(desc->vertex_shader);
	sk_vk_shader_t* fs = (sk_vk_shader_t*)sk_shader_t_to_ptr(desc->fragment_shader);

	VkPipelineShaderStageCreateInfo stages[2] = {0};
	u32 stage_count = 0u;
	VkShaderModule vs_module = VK_NULL_HANDLE;
	VkShaderModule fs_module = VK_NULL_HANDLE;

	if (vs != NULL) {
		if (sk_vk_create_shader_module(device, vs, VK_SHADER_STAGE_VERTEX_BIT, &vs_module) != VK_SUCCESS || vs_module == VK_NULL_HANDLE) {
			sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
			return sk_pipeline_t_zero();
		}
		stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[stage_count].module = vs_module;
		stages[stage_count].pName = "main";
		++stage_count;
	}
	if (fs != NULL) {
		if (sk_vk_create_shader_module(device, fs, VK_SHADER_STAGE_FRAGMENT_BIT, &fs_module) != VK_SUCCESS || fs_module == VK_NULL_HANDLE) {
			if (vs_module != VK_NULL_HANDLE) {
				vkDestroyShaderModule(device->device, vs_module, NULL);
			}
			sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
			return sk_pipeline_t_zero();
		}
		stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[stage_count].module = fs_module;
		stages[stage_count].pName = "main";
		++stage_count;
	}
	if (stage_count == 0u) {
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}

	/* Vertex input */
	u32 stride = desc->vertex_input_stride != UINT32_MAX ? desc->vertex_input_stride : desc->pipeline.stride;

	VkVertexInputBindingDescription binding_description = {0};
	binding_description.binding = 0u;
	binding_description.stride = stride;
	binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription* attribute_descriptions = NULL;
	if (desc->pipeline.input_variable_count > 0u) {
		attribute_descriptions = (VkVertexInputAttributeDescription*)device->allocator->alloc(device->allocator->instance, (size_t)desc->pipeline.input_variable_count *
																															   sizeof(VkVertexInputAttributeDescription));
		if (attribute_descriptions == NULL) {
			if (vs_module != VK_NULL_HANDLE) {
				vkDestroyShaderModule(device->device, vs_module, NULL);
			}
			if (fs_module != VK_NULL_HANDLE) {
				vkDestroyShaderModule(device->device, fs_module, NULL);
			}
			sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
			return sk_pipeline_t_zero();
		}
		for (u32 i = 0u; i < desc->pipeline.input_variable_count; ++i) {
			const sk_interface_variable_t* input = &desc->pipeline.input_variables[i];
			attribute_descriptions[i].location = input->location;
			attribute_descriptions[i].binding = 0u;
			attribute_descriptions[i].format = sk_vk_to_vk_format(input->format);
			attribute_descriptions[i].offset = input->offset;
		}
	}

	VkPipelineVertexInputStateCreateInfo vertex_input_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	if (stride > 0u) {
		vertex_input_info.vertexBindingDescriptionCount = 1u;
		vertex_input_info.pVertexBindingDescriptions = &binding_description;
	}
	if (attribute_descriptions != NULL) {
		vertex_input_info.vertexAttributeDescriptionCount = desc->pipeline.input_variable_count;
		vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions;
	}

	VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
	input_assembly_info.topology = sk_vk_convert_primitive_topology(desc->topology);
	input_assembly_info.primitiveRestartEnable = VK_FALSE;

	VkPipelineRasterizationStateCreateInfo rasterization_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
	rasterization_info.depthClampEnable = desc->rasterizer_state.depth_clamp_enable ? VK_TRUE : VK_FALSE;
	rasterization_info.rasterizerDiscardEnable = desc->rasterizer_state.rasterizer_discard_enable ? VK_TRUE : VK_FALSE;
	rasterization_info.polygonMode = sk_vk_convert_polygon_mode(desc->rasterizer_state.polygon_mode);
	rasterization_info.cullMode = sk_vk_convert_cull_mode(desc->rasterizer_state.cull_mode);
	rasterization_info.frontFace = sk_vk_convert_front_face(desc->rasterizer_state.front_face);
	rasterization_info.depthBiasEnable = desc->rasterizer_state.depth_bias_enable ? VK_TRUE : VK_FALSE;
	rasterization_info.depthBiasConstantFactor = desc->rasterizer_state.depth_bias_constant_factor;
	rasterization_info.depthBiasClamp = desc->rasterizer_state.depth_bias_clamp;
	rasterization_info.depthBiasSlopeFactor = desc->rasterizer_state.depth_bias_slope_factor;
	rasterization_info.lineWidth = desc->rasterizer_state.line_width;

	VkPipelineRasterizationConservativeStateCreateInfoEXT conservative_raster_ci = {0};
	conservative_raster_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
	conservative_raster_ci.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
	conservative_raster_ci.extraPrimitiveOverestimationSize = device->selected_adapter->conservative_raster_props.maxExtraPrimitiveOverestimationSize;
	if (desc->conservative_rasterization_mode == SK_CONSERVATIVE_RASTERIZATION_OVERESTIMATE) {
		conservative_raster_ci.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
		rasterization_info.pNext = &conservative_raster_ci;
	} else if (desc->conservative_rasterization_mode == SK_CONSERVATIVE_RASTERIZATION_UNDERESTIMATE) {
		conservative_raster_ci.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_UNDERESTIMATE_EXT;
		rasterization_info.pNext = &conservative_raster_ci;
	}

	VkPipelineMultisampleStateCreateInfo multisample_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
	if (render_pass->samples_count > 1u) {
		multisample_info.rasterizationSamples = sk_vk_cast_sample_count(render_pass->samples_count);
	} else {
		multisample_info.sampleShadingEnable = adapter_sample_rate_shading(device);
		multisample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisample_info.minSampleShading = 1.0f;
	}
	multisample_info.alphaToCoverageEnable = VK_FALSE;
	multisample_info.alphaToOneEnable = VK_FALSE;

	VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
	depth_stencil_info.depthTestEnable = desc->depth_stencil_state.depth_test_enable ? VK_TRUE : VK_FALSE;
	depth_stencil_info.depthWriteEnable = desc->depth_stencil_state.depth_write_enable ? VK_TRUE : VK_FALSE;
	depth_stencil_info.depthCompareOp = sk_vk_convert_compare_op(desc->depth_stencil_state.depth_compare_op);
	depth_stencil_info.depthBoundsTestEnable = desc->depth_stencil_state.depth_bounds_test_enable ? VK_TRUE : VK_FALSE;
	depth_stencil_info.stencilTestEnable = desc->depth_stencil_state.stencil_test_enable ? VK_TRUE : VK_FALSE;

	depth_stencil_info.front.failOp = sk_vk_convert_stencil_op(desc->depth_stencil_state.front.fail_op);
	depth_stencil_info.front.passOp = sk_vk_convert_stencil_op(desc->depth_stencil_state.front.pass_op);
	depth_stencil_info.front.depthFailOp = sk_vk_convert_stencil_op(desc->depth_stencil_state.front.depth_fail_op);
	depth_stencil_info.front.compareOp = sk_vk_convert_compare_op(desc->depth_stencil_state.front.compare_op);
	depth_stencil_info.front.compareMask = desc->depth_stencil_state.front.compare_mask;
	depth_stencil_info.front.writeMask = desc->depth_stencil_state.front.write_mask;
	depth_stencil_info.front.reference = desc->depth_stencil_state.front.reference;

	depth_stencil_info.back.failOp = sk_vk_convert_stencil_op(desc->depth_stencil_state.back.fail_op);
	depth_stencil_info.back.passOp = sk_vk_convert_stencil_op(desc->depth_stencil_state.back.pass_op);
	depth_stencil_info.back.depthFailOp = sk_vk_convert_stencil_op(desc->depth_stencil_state.back.depth_fail_op);
	depth_stencil_info.back.compareOp = sk_vk_convert_compare_op(desc->depth_stencil_state.back.compare_op);
	depth_stencil_info.back.compareMask = desc->depth_stencil_state.back.compare_mask;
	depth_stencil_info.back.writeMask = desc->depth_stencil_state.back.write_mask;
	depth_stencil_info.back.reference = desc->depth_stencil_state.back.reference;

	depth_stencil_info.minDepthBounds = desc->depth_stencil_state.min_depth_bounds;
	depth_stencil_info.maxDepthBounds = desc->depth_stencil_state.max_depth_bounds;

	/* Color blend (one entry per color attachment; missing entries = disabled blend). */
	u32 color_attachment_count = sk_vkrd_render_pass_color_attachment_count(render_pass);
	u32 blend_attachment_count = color_attachment_count;
	if (desc->blend_state_count > blend_attachment_count) {
		blend_attachment_count = desc->blend_state_count;
	}
	VkPipelineColorBlendAttachmentState* blend_attachments = NULL;
	if (blend_attachment_count > 0u) {
		blend_attachments = (VkPipelineColorBlendAttachmentState*)device->allocator->alloc(device->allocator->instance,
																						   (size_t)blend_attachment_count * sizeof(VkPipelineColorBlendAttachmentState));
		if (blend_attachments == NULL) {
			device->allocator->free(device->allocator->instance, attribute_descriptions);
			if (vs_module != VK_NULL_HANDLE) {
				vkDestroyShaderModule(device->device, vs_module, NULL);
			}
			if (fs_module != VK_NULL_HANDLE) {
				vkDestroyShaderModule(device->device, fs_module, NULL);
			}
			sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
			return sk_pipeline_t_zero();
		}
		for (u32 i = 0u; i < blend_attachment_count; ++i) {
			if (i < desc->blend_state_count) {
				const sk_blend_state_desc_t* blend = &desc->blend_states[i];
				blend_attachments[i].blendEnable = blend->blend_enable ? VK_TRUE : VK_FALSE;
				blend_attachments[i].srcColorBlendFactor = sk_vk_convert_blend_factor(blend->src_color_blend_factor);
				blend_attachments[i].dstColorBlendFactor = sk_vk_convert_blend_factor(blend->dst_color_blend_factor);
				blend_attachments[i].colorBlendOp = sk_vk_convert_blend_op(blend->color_blend_op);
				blend_attachments[i].srcAlphaBlendFactor = sk_vk_convert_blend_factor(blend->src_alpha_blend_factor);
				blend_attachments[i].dstAlphaBlendFactor = sk_vk_convert_blend_factor(blend->dst_alpha_blend_factor);
				blend_attachments[i].alphaBlendOp = sk_vk_convert_blend_op(blend->alpha_blend_op);
				blend_attachments[i].colorWriteMask = 0u;
				if ((blend->color_write_mask & (u32)SK_COLOR_MASK_COMPONENT_RED) != 0u)
					blend_attachments[i].colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
				if ((blend->color_write_mask & (u32)SK_COLOR_MASK_COMPONENT_GREEN) != 0u)
					blend_attachments[i].colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
				if ((blend->color_write_mask & (u32)SK_COLOR_MASK_COMPONENT_BLUE) != 0u)
					blend_attachments[i].colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
				if ((blend->color_write_mask & (u32)SK_COLOR_MASK_COMPONENT_ALPHA) != 0u)
					blend_attachments[i].colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
			} else {
				blend_attachments[i].blendEnable = VK_FALSE;
				blend_attachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			}
		}
	}

	VkPipelineColorBlendStateCreateInfo color_blend_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
	color_blend_info.logicOpEnable = VK_FALSE;
	color_blend_info.logicOp = VK_LOGIC_OP_COPY;
	color_blend_info.attachmentCount = blend_attachment_count;
	color_blend_info.pAttachments = blend_attachments;

	VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic_state_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dynamic_state_info.dynamicStateCount = 2u;
	dynamic_state_info.pDynamicStates = dynamic_states;

	VkPipelineViewportStateCreateInfo viewport_state_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
	viewport_state_info.viewportCount = 1u;
	viewport_state_info.scissorCount = 1u;

	VkGraphicsPipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	pipeline_info.stageCount = stage_count;
	pipeline_info.pStages = stages;
	pipeline_info.pVertexInputState = &vertex_input_info;
	pipeline_info.pInputAssemblyState = &input_assembly_info;
	pipeline_info.pViewportState = &viewport_state_info;
	pipeline_info.pRasterizationState = &rasterization_info;
	pipeline_info.pMultisampleState = &multisample_info;
	pipeline_info.pDepthStencilState = &depth_stencil_info;
	pipeline_info.pColorBlendState = &color_blend_info;
	pipeline_info.pDynamicState = &dynamic_state_info;
	pipeline_info.layout = pipeline_layout;
	pipeline_info.renderPass = render_pass->render_pass;
	pipeline_info.subpass = 0u;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateGraphicsPipelines(device->device, VK_NULL_HANDLE, 1u, &pipeline_info, NULL, &pipeline);

	if (vs_module != VK_NULL_HANDLE) {
		vkDestroyShaderModule(device->device, vs_module, NULL);
	}
	if (fs_module != VK_NULL_HANDLE) {
		vkDestroyShaderModule(device->device, fs_module, NULL);
	}
	device->allocator->free(device->allocator->instance, attribute_descriptions);
	device->allocator->free(device->allocator->instance, blend_attachments);

	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create graphics pipeline");
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}

	sk_vk_pipeline_t* pipeline_obj = (sk_vk_pipeline_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_pipeline_t));
	if (pipeline_obj == NULL) {
		vkDestroyPipeline(device->device, pipeline, NULL);
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}
	memset(pipeline_obj, 0, sizeof(*pipeline_obj));
	pipeline_obj->device = device;
	pipeline_obj->pipeline = pipeline;
	pipeline_obj->pipeline_layout = pipeline_layout;
	pipeline_obj->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
	pipeline_obj->set_layouts = set_layouts;
	pipeline_obj->set_layout_count = set_layout_count;
	sk_vk_copy_pipeline_desc(device, &desc->pipeline, &pipeline_obj->pipeline_desc);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_PIPELINE, (u64)pipeline, desc->debug_name);
	return sk_pipeline_t_from_ptr(pipeline_obj);
}

static sk_pipeline_t sk_vkrd_create_compute_pipeline(sk_render_device_t dev, const sk_compute_pipeline_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_pipeline_t_zero();
	}

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkDescriptorSetLayout* set_layouts = NULL;
	u32 set_layout_count = 0u;
	VkResult layout_result = sk_vk_create_pipeline_layout(device, &desc->pipeline, desc->allow_immediate_set, desc->descriptor_sets_override, desc->descriptor_sets_override_count,
														  &pipeline_layout, &set_layouts, &set_layout_count);
	if (layout_result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create compute pipeline layout");
		return sk_pipeline_t_zero();
	}

	sk_vk_shader_t* shader = (sk_vk_shader_t*)sk_shader_t_to_ptr(desc->compute_shader);
	if (shader == NULL) {
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}

	VkShaderModule shader_module = VK_NULL_HANDLE;
	if (sk_vk_create_shader_module(device, shader, VK_SHADER_STAGE_COMPUTE_BIT, &shader_module) != VK_SUCCESS || shader_module == VK_NULL_HANDLE) {
		sk_vkrd_log_error("Failed to create compute shader module");
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}

	VkPipelineShaderStageCreateInfo shader_stage_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
	shader_stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shader_stage_info.module = shader_module;
	shader_stage_info.pName = "main";

	VkComputePipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	pipeline_info.stage = shader_stage_info;
	pipeline_info.layout = pipeline_layout;
	pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
	pipeline_info.basePipelineIndex = -1;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateComputePipelines(device->device, VK_NULL_HANDLE, 1u, &pipeline_info, NULL, &pipeline);

	vkDestroyShaderModule(device->device, shader_module, NULL);

	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create compute pipeline");
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}

	sk_vk_pipeline_t* pipeline_obj = (sk_vk_pipeline_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_pipeline_t));
	if (pipeline_obj == NULL) {
		vkDestroyPipeline(device->device, pipeline, NULL);
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}
	memset(pipeline_obj, 0, sizeof(*pipeline_obj));
	pipeline_obj->device = device;
	pipeline_obj->pipeline = pipeline;
	pipeline_obj->pipeline_layout = pipeline_layout;
	pipeline_obj->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
	pipeline_obj->set_layouts = set_layouts;
	pipeline_obj->set_layout_count = set_layout_count;
	sk_vk_copy_pipeline_desc(device, &desc->pipeline, &pipeline_obj->pipeline_desc);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_PIPELINE, (u64)pipeline, desc->debug_name);
	return sk_pipeline_t_from_ptr(pipeline_obj);
}

static sk_pipeline_t sk_vkrd_create_ray_tracing_pipeline(sk_render_device_t dev, const sk_ray_tracing_pipeline_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL || !device->features.ray_tracing) {
		return sk_pipeline_t_zero();
	}

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkDescriptorSetLayout* set_layouts = NULL;
	u32 set_layout_count = 0u;
	VkResult layout_result = sk_vk_create_pipeline_layout(device, &desc->pipeline, false, desc->descriptor_sets_override, desc->descriptor_sets_override_count, &pipeline_layout,
														  &set_layouts, &set_layout_count);
	if (layout_result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create ray tracing pipeline layout");
		return sk_pipeline_t_zero();
	}

	sk_vk_shader_t* shader = (sk_vk_shader_t*)sk_shader_t_to_ptr(desc->shader);
	if (shader == NULL) {
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}

	VkShaderModule shader_module = VK_NULL_HANDLE;
	if (sk_vk_create_shader_module(device, shader, VK_SHADER_STAGE_RAYGEN_BIT_KHR, &shader_module) != VK_SUCCESS || shader_module == VK_NULL_HANDLE) {
		sk_vkrd_log_error("Failed to create ray tracing shader module");
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}

	VkPipelineShaderStageCreateInfo stage_create_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
	stage_create_info.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	stage_create_info.module = shader_module;
	stage_create_info.pName = "main";

	VkRayTracingShaderGroupCreateInfoKHR group_info = {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
	group_info.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	group_info.generalShader = 0u;
	group_info.closestHitShader = VK_SHADER_UNUSED_KHR;
	group_info.anyHitShader = VK_SHADER_UNUSED_KHR;
	group_info.intersectionShader = VK_SHADER_UNUSED_KHR;

	VkRayTracingPipelineCreateInfoKHR pipeline_create_info = {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
	pipeline_create_info.stageCount = 1u;
	pipeline_create_info.pStages = &stage_create_info;
	pipeline_create_info.groupCount = 1u;
	pipeline_create_info.pGroups = &group_info;
	pipeline_create_info.maxPipelineRayRecursionDepth = desc->max_recursion_depth;
	pipeline_create_info.layout = pipeline_layout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateRayTracingPipelinesKHR(device->device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1u, &pipeline_create_info, NULL, &pipeline);

	vkDestroyShaderModule(device->device, shader_module, NULL);

	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create ray tracing pipeline");
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}

	sk_vk_pipeline_t* pipeline_obj = (sk_vk_pipeline_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_pipeline_t));
	if (pipeline_obj == NULL) {
		vkDestroyPipeline(device->device, pipeline, NULL);
		sk_vkrd_cleanup_pipeline_layout(device, pipeline_layout, set_layouts, set_layout_count);
		return sk_pipeline_t_zero();
	}
	memset(pipeline_obj, 0, sizeof(*pipeline_obj));
	pipeline_obj->device = device;
	pipeline_obj->pipeline = pipeline;
	pipeline_obj->pipeline_layout = pipeline_layout;
	pipeline_obj->bind_point = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
	pipeline_obj->set_layouts = set_layouts;
	pipeline_obj->set_layout_count = set_layout_count;
	sk_vk_copy_pipeline_desc(device, &desc->pipeline, &pipeline_obj->pipeline_desc);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_PIPELINE, (u64)pipeline, desc->debug_name);
	return sk_pipeline_t_from_ptr(pipeline_obj);
}

static void sk_vkrd_destroy_pipeline(sk_render_device_t dev, sk_pipeline_t pipeline) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_pipeline_t* pipeline_obj = (sk_vk_pipeline_t*)sk_pipeline_t_to_ptr(pipeline);
	if (device == NULL || pipeline_obj == NULL) {
		return;
	}

	sk_vk_destructor_t destructor = {0};
	destructor.pipeline = pipeline_obj->pipeline;
	destructor.pipeline_layout = pipeline_obj->pipeline_layout;
	destructor.set_layouts = pipeline_obj->set_layouts;
	destructor.set_layout_count = pipeline_obj->set_layout_count;
	sk_vk_enqueue_destructor(device, &destructor);

	sk_vk_free_pipeline_desc(device, &pipeline_obj->pipeline_desc);
	sk_vk_free(device, pipeline_obj);
}

static sk_pipeline_desc_t sk_vkrd_get_pipeline_desc(sk_render_device_t dev, sk_pipeline_t pipeline) {
	(void)dev;
	sk_vk_pipeline_t* pipeline_obj = (sk_vk_pipeline_t*)sk_pipeline_t_to_ptr(pipeline);
	if (pipeline_obj == NULL) {
		sk_pipeline_desc_t desc = {0};
		return desc;
	}
	return pipeline_obj->pipeline_desc;
}

static sk_pipeline_bind_point_t sk_vkrd_get_pipeline_bind_point(sk_render_device_t dev, sk_pipeline_t pipeline) {
	(void)dev;
	sk_vk_pipeline_t* pipeline_obj = (sk_vk_pipeline_t*)sk_pipeline_t_to_ptr(pipeline);
	if (pipeline_obj == NULL) {
		return SK_PIPELINE_BIND_POINT_GRAPHICS;
	}
	switch (pipeline_obj->bind_point) {
	case VK_PIPELINE_BIND_POINT_GRAPHICS:
		return SK_PIPELINE_BIND_POINT_GRAPHICS;
	case VK_PIPELINE_BIND_POINT_COMPUTE:
		return SK_PIPELINE_BIND_POINT_COMPUTE;
	case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
		return SK_PIPELINE_BIND_POINT_RAY_TRACING;
	case VK_PIPELINE_BIND_POINT_SUBPASS_SHADING_HUAWEI:
	case VK_PIPELINE_BIND_POINT_MAX_ENUM:
		return SK_PIPELINE_BIND_POINT_GRAPHICS;
	}
	return SK_PIPELINE_BIND_POINT_GRAPHICS;
}

/* ------------------------------------------------------------------ */
/* Descriptor sets                                                     */
/* ------------------------------------------------------------------ */

static sk_descriptor_set_t sk_vkrd_create_descriptor_set(sk_render_device_t dev, const sk_descriptor_set_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_descriptor_set_t_zero();
	}

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	bool has_runtime_array = false;
	VkResult layout_result = sk_vk_create_descriptor_set_layout(device, desc->bindings, desc->binding_count, false, &layout, &has_runtime_array);
	if (layout_result != VK_SUCCESS || layout == VK_NULL_HANDLE) {
		sk_vkrd_log_error("Failed to create descriptor set layout");
		return sk_descriptor_set_t_zero();
	}

	VkDescriptorPool dedicated_pool = VK_NULL_HANDLE;
	if (has_runtime_array) {
		VkDescriptorPoolSize pool_sizes[8];
		u32 pool_size_count = 0u;
		for (u32 i = 0u; i < desc->binding_count; ++i) {
			const sk_descriptor_set_layout_binding_t* binding = &desc->bindings[i];
			if (binding->descriptor_type == SK_DESCRIPTOR_TYPE_NONE) {
				continue;
			}
			VkDescriptorType vk_type = sk_vk_convert_descriptor_type(binding->descriptor_type);
			u32 count = binding->render_type == SK_RENDER_TYPE_RUNTIME_ARRAY ? SK_VK_MAX_BINDLESS_RESOURCES : binding->descriptor_count;
			bool found = false;
			for (u32 s = 0u; s < pool_size_count; ++s) {
				if (pool_sizes[s].type == vk_type) {
					pool_sizes[s].descriptorCount += count;
					found = true;
					break;
				}
			}
			if (!found && pool_size_count < 8u) {
				pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){vk_type, count};
			}
		}

		VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		pool_info.poolSizeCount = pool_size_count;
		pool_info.pPoolSizes = pool_sizes;
		pool_info.maxSets = 1u;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
		vkCreateDescriptorPool(device->device, &pool_info, NULL, &dedicated_pool);
	}

	VkDescriptorSetAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	alloc_info.descriptorPool = dedicated_pool != VK_NULL_HANDLE ? dedicated_pool : device->descriptor_pool;
	alloc_info.descriptorSetCount = 1u;
	alloc_info.pSetLayouts = &layout;

	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	sk_mutex_lock(device->descriptor_pool_mutex);
	VkResult result = vkAllocateDescriptorSets(device->device, &alloc_info, &descriptor_set);
	sk_mutex_unlock(device->descriptor_pool_mutex);

	if (result != VK_SUCCESS) {
		if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
			if (dedicated_pool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device->device, dedicated_pool, NULL);
			}
		}
		vkDestroyDescriptorSetLayout(device->device, layout, NULL);
		sk_vkrd_log_error("Failed to allocate descriptor set");
		return sk_descriptor_set_t_zero();
	}

	sk_vk_descriptor_set_t* descriptor_set_obj = (sk_vk_descriptor_set_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_descriptor_set_t));
	if (descriptor_set_obj == NULL) {
		if (dedicated_pool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(device->device, dedicated_pool, NULL);
		} else {
			sk_mutex_lock(device->descriptor_pool_mutex);
			vkFreeDescriptorSets(device->device, device->descriptor_pool, 1u, &descriptor_set);
			sk_mutex_unlock(device->descriptor_pool_mutex);
		}
		vkDestroyDescriptorSetLayout(device->device, layout, NULL);
		return sk_descriptor_set_t_zero();
	}
	memset(descriptor_set_obj, 0, sizeof(*descriptor_set_obj));
	descriptor_set_obj->device = device;
	descriptor_set_obj->descriptor_set = descriptor_set;
	descriptor_set_obj->layout = layout;
	descriptor_set_obj->dedicated_pool = dedicated_pool;
	descriptor_set_obj->desc.binding_count = desc->binding_count;
	if (desc->binding_count > 0u) {
		descriptor_set_obj->desc.bindings = (sk_descriptor_set_layout_binding_t*)device->allocator->alloc(device->allocator->instance,
																										  (size_t)desc->binding_count * sizeof(sk_descriptor_set_layout_binding_t));
		if (descriptor_set_obj->desc.bindings != NULL) {
			memcpy(descriptor_set_obj->desc.bindings, desc->bindings, (size_t)desc->binding_count * sizeof(sk_descriptor_set_layout_binding_t));
			for (u32 i = 0u; i < desc->binding_count; ++i) {
				descriptor_set_obj->desc.bindings[i].name = sk_vk_dup_string(device, desc->bindings[i].name);
			}
		}
	}
	descriptor_set_obj->desc.debug_name = sk_vk_dup_string(device, desc->debug_name);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_DESCRIPTOR_SET, (u64)descriptor_set, desc->debug_name);
	return sk_descriptor_set_t_from_ptr(descriptor_set_obj);
}

static VkImageLayout sk_vkrd_descriptor_image_layout(const sk_vk_texture_view_t* texture_view, sk_descriptor_type_t type) {
	if (texture_view->texture != NULL && texture_view->texture->is_depth) {
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	}
	if (type == SK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
		return VK_IMAGE_LAYOUT_GENERAL;
	}
	return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

static void sk_vkrd_update_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set_handle, const sk_descriptor_write_t* writes, u32 write_count) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_descriptor_set_t* descriptor_set_obj = (sk_vk_descriptor_set_t*)sk_descriptor_set_t_to_ptr(set_handle);
	if (device == NULL || descriptor_set_obj == NULL || writes == NULL || write_count == 0u) {
		return;
	}

	for (u32 i = 0u; i < write_count; ++i) {
		const sk_descriptor_write_t* write = &writes[i];

		VkWriteDescriptorSet vk_write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		vk_write.dstSet = descriptor_set_obj->descriptor_set;
		vk_write.dstBinding = write->binding;
		vk_write.dstArrayElement = write->array_index;
		vk_write.descriptorCount = 1u;

		VkDescriptorBufferInfo buffer_info = {0};
		VkDescriptorImageInfo image_info = {0};
		VkWriteDescriptorSetAccelerationStructureKHR acceleration_structure_write = {0};
		acceleration_structure_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;

		switch (write->type) {
		case SK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case SK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
			sk_vk_texture_view_t* texture_view = (sk_vk_texture_view_t*)sk_texture_view_t_to_ptr(write->texture_view);
			if (texture_view == NULL) {
				continue;
			}
			image_info.imageView = texture_view->image_view;
			image_info.imageLayout = sk_vkrd_descriptor_image_layout(texture_view, write->type);
			vk_write.descriptorType = sk_vk_convert_descriptor_type(write->type);
			vk_write.pImageInfo = &image_info;
			break;
		}
		case SK_DESCRIPTOR_TYPE_SAMPLER: {
			sk_vk_sampler_t* sampler = (sk_vk_sampler_t*)sk_sampler_t_to_ptr(write->sampler);
			if (sampler == NULL) {
				continue;
			}
			image_info.sampler = sampler->sampler;
			vk_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			vk_write.pImageInfo = &image_info;
			break;
		}
		case SK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case SK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case SK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case SK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
			sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(write->buffer);
			if (buffer == NULL) {
				continue;
			}
			buffer_info.buffer = buffer->buffer;
			buffer_info.offset = write->buffer_offset;
			buffer_info.range = write->buffer_range > 0ull ? write->buffer_range : VK_WHOLE_SIZE;
			vk_write.descriptorType = sk_vk_convert_descriptor_type(write->type);
			vk_write.pBufferInfo = &buffer_info;
			break;
		}
		case SK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE: {
			sk_vk_tlas_t* tlas = (sk_vk_tlas_t*)sk_tlas_t_to_ptr(write->acceleration_structure);
			if (tlas == NULL) {
				continue;
			}
			acceleration_structure_write.accelerationStructureCount = 1u;
			acceleration_structure_write.pAccelerationStructures = &tlas->acceleration_structure;
			vk_write.pNext = &acceleration_structure_write;
			vk_write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			break;
		}
		case SK_DESCRIPTOR_TYPE_NONE:
		case SK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		case SK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
		case SK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			/* Not supported by main's Vulkan update path either. */
			continue;
		}

		vkUpdateDescriptorSets(device->device, 1u, &vk_write, 0u, NULL);
	}
}

static void sk_vkrd_destroy_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_descriptor_set_t* descriptor_set_obj = (sk_vk_descriptor_set_t*)sk_descriptor_set_t_to_ptr(set);
	if (device == NULL || descriptor_set_obj == NULL) {
		return;
	}

	sk_vk_destructor_t destructor = {0};
	destructor.descriptor_set = descriptor_set_obj->descriptor_set;
	destructor.descriptor_pool = descriptor_set_obj->dedicated_pool;
	if (descriptor_set_obj->layout != VK_NULL_HANDLE) {
		destructor.set_layouts = (VkDescriptorSetLayout*)device->allocator->alloc(device->allocator->instance, sizeof(VkDescriptorSetLayout));
		if (destructor.set_layouts != NULL) {
			destructor.set_layouts[0] = descriptor_set_obj->layout;
			destructor.set_layout_count = 1u;
		}
	}
	sk_vk_enqueue_destructor(device, &destructor);

	sk_vk_free(device, descriptor_set_obj->desc.debug_name);
	for (u32 i = 0u; i < descriptor_set_obj->desc.binding_count; ++i) {
		sk_vk_free(device, descriptor_set_obj->desc.bindings[i].name);
	}
	sk_vk_free(device, descriptor_set_obj->desc.bindings);
	sk_vk_free(device, descriptor_set_obj);
}

static sk_descriptor_set_desc_t sk_vkrd_get_descriptor_set_desc(sk_render_device_t dev, sk_descriptor_set_t set) {
	(void)dev;
	sk_vk_descriptor_set_t* descriptor_set_obj = (sk_vk_descriptor_set_t*)sk_descriptor_set_t_to_ptr(set);
	if (descriptor_set_obj == NULL) {
		sk_descriptor_set_desc_t desc = {0};
		return desc;
	}
	return descriptor_set_obj->desc;
}

/* ------------------------------------------------------------------ */
/* Render pass / framebuffer                                           */
/* ------------------------------------------------------------------ */

static sk_render_pass_t sk_vkrd_create_render_pass(sk_render_device_t dev, const sk_render_pass_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_render_pass_t_zero();
	}

	u32 total_attachment_count = desc->attachment_count + desc->resolve_attachment_count;
	VkAttachmentDescription2KHR* attachment_descriptions = NULL;
	VkAttachmentReference2KHR* color_references = NULL;
	VkAttachmentReference2KHR* resolve_references = NULL;
	if (total_attachment_count > 0u) {
		const size_t bytes = (size_t)total_attachment_count * sizeof(VkAttachmentDescription2KHR);
		attachment_descriptions = (VkAttachmentDescription2KHR*)device->allocator->alloc(device->allocator->instance, bytes);
		if (attachment_descriptions != NULL) {
			memset(attachment_descriptions, 0, bytes);
		}
	}
	if (desc->attachment_count > 0u) {
		const size_t bytes = (size_t)desc->attachment_count * sizeof(VkAttachmentReference2KHR);
		color_references = (VkAttachmentReference2KHR*)device->allocator->alloc(device->allocator->instance, bytes);
		if (color_references != NULL) {
			/* pNext must be NULL; non-zero garbage crashes vkCreateRenderPass2KHR. */
			memset(color_references, 0, bytes);
		}
	}
	if (desc->resolve_attachment_count > 0u) {
		const size_t bytes = (size_t)desc->resolve_attachment_count * sizeof(VkAttachmentReference2KHR);
		resolve_references = (VkAttachmentReference2KHR*)device->allocator->alloc(device->allocator->instance, bytes);
		if (resolve_references != NULL) {
			memset(resolve_references, 0, bytes);
		}
	}
	if ((total_attachment_count > 0u && attachment_descriptions == NULL) || (desc->attachment_count > 0u && color_references == NULL) ||
		(desc->resolve_attachment_count > 0u && resolve_references == NULL)) {
		device->allocator->free(device->allocator->instance, attachment_descriptions);
		device->allocator->free(device->allocator->instance, color_references);
		device->allocator->free(device->allocator->instance, resolve_references);
		return sk_render_pass_t_zero();
	}

	VkAttachmentReference2KHR depth_reference = {.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
	VkAttachmentReference2KHR depth_resolve_reference = {.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
	VkSubpassDescriptionDepthStencilResolveKHR depth_resolve_subpass = {.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE_KHR};
	depth_resolve_subpass.depthResolveMode = VK_RESOLVE_MODE_MAX_BIT;
	depth_resolve_subpass.stencilResolveMode = VK_RESOLVE_MODE_MAX_BIT;

	u32 attachment_index = 0u;
	u32 color_count = 0u;
	u32 resolve_color_count = 0u;
	u32 samples_count = 1u;
	bool has_depth = false;
	bool has_depth_resolve = false;

	for (u32 i = 0u; i < desc->attachment_count; ++i) {
		const sk_attachment_desc_t* attachment = &desc->attachments[i];
		VkFormat format = sk_vk_to_vk_format(attachment->format);
		bool is_depth = sk_vk_is_depth_format_vk(format);

		if (attachment->sample_count > samples_count) {
			samples_count = attachment->sample_count;
		}

		VkAttachmentDescription2KHR attachment_description = {.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
		attachment_description.format = format;
		attachment_description.samples = sk_vk_cast_sample_count(attachment->sample_count);
		attachment_description.loadOp = sk_vk_cast_load_op(attachment->load_op);
		attachment_description.storeOp = sk_vk_cast_store_op(attachment->store_op);
		attachment_description.stencilLoadOp = sk_vk_cast_load_op(attachment->stencil_load_op);
		attachment_description.stencilStoreOp = sk_vk_cast_store_op(attachment->stencil_store_op);
		attachment_description.initialLayout = sk_vk_cast_state(attachment->initial_state, VK_IMAGE_LAYOUT_UNDEFINED);
		attachment_description.finalLayout = sk_vk_cast_state(attachment->final_state,
															  is_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		attachment_descriptions[attachment_index] = attachment_description;

		if (!is_depth) {
			color_references[color_count].sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
			color_references[color_count].attachment = attachment_index;
			color_references[color_count].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			++color_count;
		} else {
			depth_reference.attachment = attachment_index;
			depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			has_depth = true;
		}
		++attachment_index;
	}

	for (u32 i = 0u; i < desc->resolve_attachment_count; ++i) {
		const sk_attachment_desc_t* attachment = &desc->resolve_attachments[i];
		VkFormat format = sk_vk_to_vk_format(attachment->format);
		bool is_depth = sk_vk_is_depth_format_vk(format);

		if (attachment->sample_count > samples_count) {
			samples_count = attachment->sample_count;
		}

		VkAttachmentDescription2KHR attachment_description = {.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
		attachment_description.format = format;
		attachment_description.samples = sk_vk_cast_sample_count(attachment->sample_count);
		attachment_description.loadOp = sk_vk_cast_load_op(attachment->load_op);
		attachment_description.storeOp = sk_vk_cast_store_op(attachment->store_op);
		attachment_description.stencilLoadOp = sk_vk_cast_load_op(attachment->stencil_load_op);
		attachment_description.stencilStoreOp = sk_vk_cast_store_op(attachment->stencil_store_op);
		attachment_description.initialLayout = sk_vk_cast_state(attachment->initial_state, VK_IMAGE_LAYOUT_UNDEFINED);
		attachment_description.finalLayout = sk_vk_cast_state(attachment->final_state,
															  is_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		attachment_descriptions[attachment_index] = attachment_description;

		if (!is_depth) {
			resolve_references[resolve_color_count].sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
			resolve_references[resolve_color_count].attachment = attachment_index;
			resolve_references[resolve_color_count].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			++resolve_color_count;
		} else {
			depth_resolve_reference.attachment = attachment_index;
			depth_resolve_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depth_resolve_subpass.pDepthStencilResolveAttachment = &depth_resolve_reference;
			has_depth_resolve = true;
		}
		++attachment_index;
	}

	VkSubpassDescription2KHR subpass = {.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = color_count;
	subpass.pColorAttachments = color_references;
	if (has_depth) {
		subpass.pDepthStencilAttachment = &depth_reference;
	}
	if (resolve_color_count > 0u) {
		subpass.pResolveAttachments = resolve_references;
	}
	if (has_depth_resolve) {
		subpass.pNext = &depth_resolve_subpass;
	}

	VkRenderPassCreateInfo2KHR render_pass_create_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
	render_pass_create_info.attachmentCount = attachment_index;
	render_pass_create_info.pAttachments = attachment_descriptions;
	render_pass_create_info.subpassCount = 1u;
	render_pass_create_info.pSubpasses = &subpass;

	VkRenderPass render_pass = VK_NULL_HANDLE;
	VkResult result = vkCreateRenderPass2KHR(device->device, &render_pass_create_info, NULL, &render_pass);

	device->allocator->free(device->allocator->instance, attachment_descriptions);
	device->allocator->free(device->allocator->instance, color_references);
	device->allocator->free(device->allocator->instance, resolve_references);

	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create render pass");
		return sk_render_pass_t_zero();
	}

	sk_vk_render_pass_t* render_pass_obj = (sk_vk_render_pass_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_render_pass_t));
	if (render_pass_obj == NULL) {
		vkDestroyRenderPass(device->device, render_pass, NULL);
		return sk_render_pass_t_zero();
	}
	memset(render_pass_obj, 0, sizeof(*render_pass_obj));
	render_pass_obj->device = device;
	render_pass_obj->render_pass = render_pass;
	render_pass_obj->samples_count = samples_count;
	sk_vk_copy_render_pass_desc(device, desc, &render_pass_obj->desc);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_RENDER_PASS, (u64)render_pass, desc->debug_name);
	return sk_render_pass_t_from_ptr(render_pass_obj);
}

static void sk_vkrd_destroy_render_pass(sk_render_device_t dev, sk_render_pass_t pass) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_render_pass_t* render_pass = (sk_vk_render_pass_t*)sk_render_pass_t_to_ptr(pass);
	if (device == NULL || render_pass == NULL) {
		return;
	}

	sk_vk_destructor_t destructor = {0};
	destructor.render_pass = render_pass->render_pass;
	sk_vk_enqueue_destructor(device, &destructor);

	sk_vk_free_render_pass_desc(device, &render_pass->desc);
	sk_vk_free(device, render_pass);
}

static sk_framebuffer_t sk_vkrd_create_framebuffer(sk_render_device_t dev, const sk_framebuffer_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_framebuffer_t_zero();
	}
	sk_vk_render_pass_t* render_pass = (sk_vk_render_pass_t*)sk_render_pass_t_to_ptr(desc->render_pass);
	if (render_pass == NULL) {
		return sk_framebuffer_t_zero();
	}

	VkImageView* image_views = NULL;
	if (desc->attachment_count > 0u) {
		image_views = (VkImageView*)device->allocator->alloc(device->allocator->instance, (size_t)desc->attachment_count * sizeof(VkImageView));
	}
	if (desc->attachment_count > 0u && image_views == NULL) {
		return sk_framebuffer_t_zero();
	}

	u32 view_count = 0u;
	sk_extent3d_t extent = {1u, 1u, 1u};
	bool has_extent = false;

	for (u32 i = 0u; i < desc->attachment_count; ++i) {
		sk_vk_texture_view_t* texture_view = (sk_vk_texture_view_t*)sk_texture_view_t_to_ptr(desc->attachments[i]);
		if (texture_view == NULL || texture_view->texture == NULL) {
			continue;
		}
		image_views[view_count++] = texture_view->image_view;

		u32 mip_level = texture_view->desc.base_mip_level;
		extent = texture_view->texture->desc.extent;
		if (mip_level < 32u) {
			extent.width = extent.width >> mip_level;
			extent.height = extent.height >> mip_level;
		}
		if (extent.width < 1u) {
			extent.width = 1u;
		}
		if (extent.height < 1u) {
			extent.height = 1u;
		}
		has_extent = true;
	}
	if (!has_extent) {
		device->allocator->free(device->allocator->instance, image_views);
		return sk_framebuffer_t_zero();
	}

	VkFramebufferCreateInfo framebuffer_create_info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
	framebuffer_create_info.renderPass = render_pass->render_pass;
	framebuffer_create_info.width = extent.width;
	framebuffer_create_info.height = extent.height;
	framebuffer_create_info.layers = 1u;
	framebuffer_create_info.attachmentCount = view_count;
	framebuffer_create_info.pAttachments = image_views;

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VkResult result = vkCreateFramebuffer(device->device, &framebuffer_create_info, NULL, &framebuffer);
	device->allocator->free(device->allocator->instance, image_views);

	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create framebuffer");
		return sk_framebuffer_t_zero();
	}

	sk_vk_framebuffer_t* framebuffer_obj = (sk_vk_framebuffer_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_framebuffer_t));
	if (framebuffer_obj == NULL) {
		vkDestroyFramebuffer(device->device, framebuffer, NULL);
		return sk_framebuffer_t_zero();
	}
	memset(framebuffer_obj, 0, sizeof(*framebuffer_obj));
	framebuffer_obj->device = device;
	framebuffer_obj->framebuffer = framebuffer;
	framebuffer_obj->extent = extent;
	sk_vk_copy_framebuffer_desc(device, desc, &framebuffer_obj->desc);

	framebuffer_obj->clear_value_count = desc->attachment_count;
	if (desc->attachment_count > 0u) {
		framebuffer_obj->clear_values = (VkClearValue*)device->allocator->alloc(device->allocator->instance, (size_t)desc->attachment_count * sizeof(VkClearValue));
	}

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_FRAMEBUFFER, (u64)framebuffer, desc->debug_name);
	return sk_framebuffer_t_from_ptr(framebuffer_obj);
}

static void sk_vkrd_destroy_framebuffer(sk_render_device_t dev, sk_framebuffer_t fb) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_framebuffer_t* framebuffer = (sk_vk_framebuffer_t*)sk_framebuffer_t_to_ptr(fb);
	if (device == NULL || framebuffer == NULL) {
		return;
	}

	sk_vk_destructor_t destructor = {0};
	destructor.framebuffer = framebuffer->framebuffer;
	sk_vk_enqueue_destructor(device, &destructor);

	sk_vk_free_framebuffer_desc(device, &framebuffer->desc);
	sk_vk_free(device, framebuffer->clear_values);
	sk_vk_free(device, framebuffer);
}

static sk_render_pass_desc_t sk_vkrd_get_render_pass_desc(sk_render_device_t dev, sk_render_pass_t pass) {
	(void)dev;
	sk_vk_render_pass_t* render_pass = (sk_vk_render_pass_t*)sk_render_pass_t_to_ptr(pass);
	if (render_pass == NULL) {
		sk_render_pass_desc_t desc = {0};
		return desc;
	}
	return render_pass->desc;
}

static sk_framebuffer_desc_t sk_vkrd_get_framebuffer_desc(sk_render_device_t dev, sk_framebuffer_t fb) {
	(void)dev;
	sk_vk_framebuffer_t* framebuffer = (sk_vk_framebuffer_t*)sk_framebuffer_t_to_ptr(fb);
	if (framebuffer == NULL) {
		sk_framebuffer_desc_t desc = {0};
		return desc;
	}
	return framebuffer->desc;
}

static sk_extent3d_t sk_vkrd_get_framebuffer_extent(sk_render_device_t dev, sk_framebuffer_t fb) {
	(void)dev;
	sk_vk_framebuffer_t* framebuffer = (sk_vk_framebuffer_t*)sk_framebuffer_t_to_ptr(fb);
	if (framebuffer == NULL) {
		sk_extent3d_t extent = {0};
		return extent;
	}
	sk_extent3d_t extent = {framebuffer->extent.width, framebuffer->extent.height, 1u};
	return extent;
}

/* ------------------------------------------------------------------ */
/* Swapchain                                                           */
/* ------------------------------------------------------------------ */

static void sk_vkrd_swapchain_destroy_internal(sk_vk_swapchain_t* swapchain) {
	sk_vk_device_t* device = swapchain->device;
	if (swapchain->swapchain != VK_NULL_HANDLE && device->device != VK_NULL_HANDLE) {
		(void)vkDeviceWaitIdle(device->device);
		sk_vk_flush_destructors(device);
	}
	for (u32 i = 0u; i < swapchain->texture_count; ++i) {
		sk_vk_texture_t* texture = swapchain->textures[i];
		sk_vk_free(device, texture->desc.debug_name);
		sk_vk_free(device, texture);
	}
	device->allocator->free(device->allocator->instance, swapchain->textures);
	swapchain->textures = NULL;
	swapchain->texture_count = 0u;

	if (swapchain->swapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(device->device, swapchain->swapchain, NULL);
		swapchain->swapchain = VK_NULL_HANDLE;
	}
}

static bool sk_vkrd_swapchain_recreate(sk_vk_swapchain_t* swapchain, u32 width, u32 height) {
	sk_vk_device_t* device = swapchain->device;
	sk_vkrd_swapchain_destroy_internal(swapchain);
	swapchain->image_index = 0u;

	sk_vk_swapchain_support_t support;
	sk_vk_query_swapchain_support(device->selected_adapter->physical, swapchain->surface, &support, device->allocator);

	VkSurfaceFormatKHR desired_format = {sk_vk_to_vk_format(swapchain->desc.format == SK_PIXEL_FORMAT_UNKNOWN ? SK_PIXEL_FORMAT_BGRA8_UNORM : swapchain->desc.format),
										 VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
	VkSurfaceFormatKHR surface_format = sk_vk_choose_surface_format(&support, desired_format);
	VkPresentModeKHR present_mode = sk_vk_choose_present_mode(&support, swapchain->desc.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR);

	u32 chosen_width = width;
	u32 chosen_height = height;
	if (chosen_width == 0u || chosen_height == 0u) {
		if (device->app_api != NULL && device->context != NULL) {
			const sk_platform_window_api_t* win_api = (const sk_platform_window_api_t*)device->app_api->get_api(device->context, SK_PLATFORM_WINDOW_API_TYPE_ID);
			if (win_api != NULL) {
				/* Physical framebuffer pixels (HiDPI); not logical client size. */
				sk_extent_t fb_size = win_api->get_framebuffer_size(swapchain->desc.window);
				if (chosen_width == 0u) {
					chosen_width = fb_size.width;
				}
				if (chosen_height == 0u) {
					chosen_height = fb_size.height;
				}
			}
		}
	}

	VkExtent2D extent = sk_vk_choose_extent(&support, chosen_width, chosen_height);
	if (extent.width == 0u || extent.height == 0u) {
		sk_vk_destroy_swapchain_support(&support, device->allocator);
		return false;
	}

	u32 image_count = support.capabilities.minImageCount + 1u;
	if (support.capabilities.maxImageCount > 0u && image_count > support.capabilities.maxImageCount) {
		image_count = support.capabilities.maxImageCount;
	}
	if (swapchain->desc.image_count > 0u && image_count > swapchain->desc.image_count) {
		image_count = swapchain->desc.image_count;
	}

	VkBool32 present_support = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR(device->selected_adapter->physical, device->selected_adapter->present_family, swapchain->surface, &present_support);
	if (present_support == VK_FALSE) {
		sk_vk_destroy_swapchain_support(&support, device->allocator);
		return false;
	}

	VkSwapchainCreateInfoKHR create_info = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
	create_info.surface = swapchain->surface;
	create_info.minImageCount = image_count;
	create_info.imageFormat = surface_format.format;
	create_info.imageColorSpace = surface_format.colorSpace;
	create_info.imageExtent = extent;
	create_info.imageArrayLayers = 1u;
	create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

	u32 graphics_family = device->selected_adapter->graphics_family;
	u32 present_family = device->selected_adapter->present_family;
	u32 queue_family_indices[2] = {graphics_family, present_family};
	if (graphics_family != present_family) {
		create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		create_info.queueFamilyIndexCount = 2u;
		create_info.pQueueFamilyIndices = queue_family_indices;
	} else {
		create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.queueFamilyIndexCount = 0u;
		create_info.pQueueFamilyIndices = NULL;
	}

	VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	VkCompositeAlphaFlagBitsKHR composite_alpha_flags[4] = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
	};
	for (u32 i = 0u; i < 4u; ++i) {
		if ((support.capabilities.supportedCompositeAlpha & (VkCompositeAlphaFlagsKHR)composite_alpha_flags[i]) != 0u) {
			composite_alpha = composite_alpha_flags[i];
			break;
		}
	}

	create_info.preTransform = support.capabilities.currentTransform;
	create_info.compositeAlpha = composite_alpha;
	create_info.presentMode = present_mode;
	create_info.clipped = VK_TRUE;
	create_info.oldSwapchain = VK_NULL_HANDLE;

	VkResult result = vkCreateSwapchainKHR(device->device, &create_info, NULL, &swapchain->swapchain);
	sk_vk_destroy_swapchain_support(&support, device->allocator);
	if (result != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create swapchain");
		return false;
	}

	swapchain->extent = extent;
	swapchain->format = surface_format.format;

	u32 swap_image_count = 0u;
	vkGetSwapchainImagesKHR(device->device, swapchain->swapchain, &swap_image_count, NULL);
	VkImage* images = NULL;
	if (swap_image_count > 0u) {
		images = (VkImage*)device->allocator->alloc(device->allocator->instance, (size_t)swap_image_count * sizeof(VkImage));
	}
	if (swap_image_count > 0u && images == NULL) {
		sk_vkrd_swapchain_destroy_internal(swapchain);
		return false;
	}
	if (swap_image_count > 0u) {
		vkGetSwapchainImagesKHR(device->device, swapchain->swapchain, &swap_image_count, images);
	}

	swapchain->textures = (sk_vk_texture_t**)device->allocator->alloc(device->allocator->instance, (size_t)swap_image_count * sizeof(sk_vk_texture_t*));
	if (swapchain->textures == NULL) {
		device->allocator->free(device->allocator->instance, images);
		sk_vkrd_swapchain_destroy_internal(swapchain);
		return false;
	}
	swapchain->texture_count = swap_image_count;

	sk_pixel_format_t format = sk_vk_to_format(swapchain->format);
	for (u32 i = 0u; i < swap_image_count; ++i) {
		sk_vk_texture_t* texture = (sk_vk_texture_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_texture_t));
		if (texture == NULL) {
			continue;
		}
		memset(texture, 0, sizeof(*texture));
		texture->device = device;
		texture->image = images[i];
		texture->desc.extent = (sk_extent3d_t){extent.width, extent.height, 1u};
		texture->desc.format = format;
		texture->desc.usage_flags = (u32)SK_RESOURCE_USAGE_RENDER_TARGET;
		texture->desc.mip_levels = 1u;
		texture->desc.array_layers = 1u;
		texture->desc.sample_count = 1u;
		swapchain->textures[i] = texture;
	}

	device->allocator->free(device->allocator->instance, images);
	sk_vk_set_object_name(device, VK_OBJECT_TYPE_SWAPCHAIN_KHR, (u64)swapchain->swapchain, swapchain->desc.debug_name);
	return true;
}

static sk_swapchain_t sk_vkrd_create_swapchain(sk_render_device_t dev, const sk_swapchain_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL || device->selected_adapter == NULL) {
		return sk_swapchain_t_zero();
	}

	void_ptr_t native_window = desc->window;
	if (device->app_api != NULL && device->context != NULL) {
		const sk_platform_window_api_t* win_api = (const sk_platform_window_api_t*)device->app_api->get_api(device->context, SK_PLATFORM_WINDOW_API_TYPE_ID);
		if (win_api != NULL) {
			void_ptr_t handle = win_api->get_native_window_handle(desc->window);
			if (handle != NULL) {
				native_window = handle;
			}
		}
	}
	if (native_window == NULL) {
		sk_vkrd_log_error("Swapchain requires a valid window");
		return sk_swapchain_t_zero();
	}

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	void_ptr_t platform_display = NULL;
	if (!sk_vk_platform_create_surface(device, native_window, &surface, &platform_display)) {
		sk_vkrd_log_error("Failed to create window surface");
		return sk_swapchain_t_zero();
	}

	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_swapchain_t));
	if (swapchain == NULL) {
		vkDestroySurfaceKHR(device->instance, surface, NULL);
		sk_vk_platform_destroy_surface_handle(platform_display);
		return sk_swapchain_t_zero();
	}
	memset(swapchain, 0, sizeof(*swapchain));
	swapchain->device = device;
	swapchain->surface = surface;
	swapchain->platform_display = platform_display;
	swapchain->desc = *desc;
	swapchain->desc.debug_name = sk_vk_dup_string(device, desc->debug_name);

	if (!sk_vkrd_swapchain_recreate(swapchain, desc->width, desc->height)) {
		vkDestroySurfaceKHR(device->instance, swapchain->surface, NULL);
		sk_vk_platform_destroy_surface_handle(platform_display);
		sk_vk_free(device, swapchain->desc.debug_name);
		sk_vk_free(device, swapchain);
		return sk_swapchain_t_zero();
	}

	{
		VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		if (vkCreateFence(device->device, &fence_info, NULL, &swapchain->acquire_fence) != VK_SUCCESS) {
			sk_vkrd_swapchain_destroy_internal(swapchain);
			vkDestroySurfaceKHR(device->instance, swapchain->surface, NULL);
			sk_vk_platform_destroy_surface_handle(platform_display);
			sk_vk_free(device, swapchain->desc.debug_name);
			sk_vk_free(device, swapchain);
			return sk_swapchain_t_zero();
		}
		sk_vk_set_object_name(device, VK_OBJECT_TYPE_FENCE, (u64)swapchain->acquire_fence, "swapchain-acquire-fence");
	}

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_SWAPCHAIN_KHR, (u64)swapchain->swapchain, desc->debug_name);
	return sk_swapchain_t_from_ptr(swapchain);
}

static void sk_vkrd_destroy_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	if (device == NULL || swapchain == NULL) {
		return;
	}

	sk_vkrd_swapchain_destroy_internal(swapchain);
	if (swapchain->acquire_fence != VK_NULL_HANDLE) {
		vkDestroyFence(device->device, swapchain->acquire_fence, NULL);
		swapchain->acquire_fence = VK_NULL_HANDLE;
	}
	if (swapchain->surface != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(device->instance, swapchain->surface, NULL);
	}
	sk_vk_platform_destroy_surface_handle(swapchain->platform_display);
	sk_vk_free(device, swapchain->desc.debug_name);
	sk_vk_free(device, swapchain);
}

static i32 sk_vkrd_resize_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain_handle, u32 width, u32 height) {
	(void)dev;
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	if (swapchain == NULL) {
		return -1;
	}
	if (!sk_vkrd_swapchain_recreate(swapchain, width, height)) {
		return -1;
	}
	return 0;
}

static sk_device_result_t sk_vkrd_acquire_next_image(sk_render_device_t dev, const sk_acquire_info_t* info, u32* out_image_index) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || info == NULL) {
		return SK_DEVICE_RESULT_ERROR;
	}
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(info->swapchain);
	if (swapchain == NULL || swapchain->swapchain == VK_NULL_HANDLE) {
		return SK_DEVICE_RESULT_ERROR;
	}

	VkSemaphore semaphore = VK_NULL_HANDLE;
	if (sk_semaphore_t_is_valid(info->signal_semaphore)) {
		sk_vk_semaphore_t* semaphore_obj = (sk_vk_semaphore_t*)sk_semaphore_t_to_ptr(info->signal_semaphore);
		if (semaphore_obj != NULL) {
			semaphore = semaphore_obj->semaphore;
		}
	}
	VkFence fence = VK_NULL_HANDLE;
	if (sk_fence_t_is_valid(info->signal_fence)) {
		sk_vk_fence_t* fence_obj = (sk_vk_fence_t*)sk_fence_t_to_ptr(info->signal_fence);
		if (fence_obj != NULL) {
			fence = fence_obj->fence;
		}
	}

	/* Vulkan forbids both semaphore and fence being VK_NULL_HANDLE. */
	bool used_internal_fence = false;
	if (semaphore == VK_NULL_HANDLE && fence == VK_NULL_HANDLE) {
		fence = swapchain->acquire_fence;
		used_internal_fence = fence != VK_NULL_HANDLE;
	}

	VkResult result = vkAcquireNextImageKHR(device->device, swapchain->swapchain, info->timeout_ns, semaphore, fence, &swapchain->image_index);
	if (out_image_index != NULL) {
		*out_image_index = swapchain->image_index;
	}
	if (used_internal_fence && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)) {
		(void)vkWaitForFences(device->device, 1u, &swapchain->acquire_fence, VK_TRUE, UINT64_MAX);
		(void)vkResetFences(device->device, 1u, &swapchain->acquire_fence);
	}
	/* SUBOPTIMAL still returns a valid image (and signals semaphore/fence). */
	if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
		return SK_DEVICE_RESULT_SUCCESS;
	}
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		return SK_DEVICE_RESULT_SWAPCHAIN_OUT_OF_DATE;
	}
	return SK_DEVICE_RESULT_ERROR;
}

static sk_texture_t sk_vkrd_get_swapchain_image(sk_render_device_t dev, sk_swapchain_t swapchain_handle, u32 image_index) {
	(void)dev;
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	if (swapchain == NULL || image_index >= swapchain->texture_count) {
		return sk_texture_t_zero();
	}
	return sk_texture_t_from_ptr(swapchain->textures[image_index]);
}

static sk_extent3d_t sk_vkrd_get_swapchain_extent(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	(void)dev;
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	if (swapchain == NULL) {
		sk_extent3d_t extent = {0};
		return extent;
	}
	sk_extent3d_t extent = {swapchain->extent.width, swapchain->extent.height, 1u};
	return extent;
}

static sk_pixel_format_t sk_vkrd_get_swapchain_format(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	(void)dev;
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	if (swapchain == NULL) {
		return SK_PIXEL_FORMAT_UNKNOWN;
	}
	return sk_vk_to_format(swapchain->format);
}

static u32 sk_vkrd_get_swapchain_image_count(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	(void)dev;
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	if (swapchain == NULL) {
		return 0u;
	}
	return swapchain->texture_count;
}

static u32 sk_vkrd_get_swapchain_current_image_index(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	(void)dev;
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	if (swapchain == NULL) {
		return 0u;
	}
	return swapchain->image_index;
}

static u32 sk_vkrd_get_swapchain_textures(sk_render_device_t dev, sk_swapchain_t swapchain_handle, sk_texture_t* out_textures, u32 max_count) {
	(void)dev;
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	if (swapchain == NULL || out_textures == NULL) {
		return 0u;
	}
	u32 count = swapchain->texture_count;
	if (count > max_count) {
		count = max_count;
	}
	for (u32 i = 0u; i < count; ++i) {
		out_textures[i] = sk_texture_t_from_ptr(swapchain->textures[i]);
	}
	return count;
}

static sk_device_result_t sk_vkrd_present(sk_render_device_t dev, sk_queue_t queue_handle, const sk_present_info_t* info) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_queue_t* queue = (sk_vk_queue_t*)sk_queue_t_to_ptr(queue_handle);
	if (device == NULL || queue == NULL || info == NULL) {
		return SK_DEVICE_RESULT_ERROR;
	}
	sk_vk_swapchain_t* swapchain = (sk_vk_swapchain_t*)sk_swapchain_t_to_ptr(info->swapchain);
	if (swapchain == NULL || swapchain->swapchain == VK_NULL_HANDLE) {
		return SK_DEVICE_RESULT_ERROR;
	}

	VkSemaphore* wait_semaphores = NULL;
	if (info->wait_semaphore_count > 0u) {
		wait_semaphores = (VkSemaphore*)device->allocator->alloc(device->allocator->instance, (size_t)info->wait_semaphore_count * sizeof(VkSemaphore));
		if (wait_semaphores == NULL) {
			return SK_DEVICE_RESULT_ERROR;
		}
		for (u32 i = 0u; i < info->wait_semaphore_count; ++i) {
			sk_vk_semaphore_t* semaphore = (sk_vk_semaphore_t*)sk_semaphore_t_to_ptr(info->wait_semaphores[i]);
			wait_semaphores[i] = semaphore != NULL ? semaphore->semaphore : VK_NULL_HANDLE;
		}
	}

	VkPresentInfoKHR present_info = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
	present_info.waitSemaphoreCount = info->wait_semaphore_count;
	present_info.pWaitSemaphores = wait_semaphores;
	present_info.swapchainCount = 1u;
	present_info.pSwapchains = &swapchain->swapchain;
	present_info.pImageIndices = &info->image_index;

	sk_mutex_lock(queue->context->mutex);
	VkResult result = vkQueuePresentKHR(queue->context->vk_queue, &present_info);
	sk_mutex_unlock(queue->context->mutex);

	device->allocator->free(device->allocator->instance, wait_semaphores);

	if (result == VK_SUCCESS) {
		return SK_DEVICE_RESULT_SUCCESS;
	}
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		return SK_DEVICE_RESULT_SWAPCHAIN_OUT_OF_DATE;
	}
	return SK_DEVICE_RESULT_ERROR;
}

/* ------------------------------------------------------------------ */
/* Fences / semaphores                                                 */
/* ------------------------------------------------------------------ */

static sk_fence_t sk_vkrd_create_fence(sk_render_device_t dev, const sk_fence_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created) {
		return sk_fence_t_zero();
	}

	VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	if (desc != NULL && desc->signaled) {
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	}

	VkFence fence = VK_NULL_HANDLE;
	if (vkCreateFence(device->device, &fence_info, NULL, &fence) != VK_SUCCESS) {
		return sk_fence_t_zero();
	}

	sk_vk_fence_t* fence_obj = (sk_vk_fence_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_fence_t));
	if (fence_obj == NULL) {
		vkDestroyFence(device->device, fence, NULL);
		return sk_fence_t_zero();
	}
	memset(fence_obj, 0, sizeof(*fence_obj));
	fence_obj->device = device;
	fence_obj->fence = fence;
	return sk_fence_t_from_ptr(fence_obj);
}

static void sk_vkrd_destroy_fence(sk_render_device_t dev, sk_fence_t fence_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_fence_t* fence = (sk_vk_fence_t*)sk_fence_t_to_ptr(fence_handle);
	if (device == NULL || fence == NULL) {
		return;
	}
	vkDestroyFence(device->device, fence->fence, NULL);
	device->allocator->free(device->allocator->instance, fence);
}

static i32 sk_vkrd_wait_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count, bool wait_all, u64 timeout_ns) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || fences == NULL || fence_count == 0u) {
		return -1;
	}

	VkFence* vk_fences = (VkFence*)device->allocator->alloc(device->allocator->instance, (size_t)fence_count * sizeof(VkFence));
	if (vk_fences == NULL) {
		return -1;
	}
	for (u32 i = 0u; i < fence_count; ++i) {
		sk_vk_fence_t* fence = (sk_vk_fence_t*)sk_fence_t_to_ptr(fences[i]);
		vk_fences[i] = fence != NULL ? fence->fence : VK_NULL_HANDLE;
	}

	VkResult result = vkWaitForFences(device->device, fence_count, vk_fences, wait_all ? VK_TRUE : VK_FALSE, timeout_ns);
	device->allocator->free(device->allocator->instance, vk_fences);

	if (result == VK_SUCCESS) {
		sk_vk_flush_destructors(device);
		return 0;
	}
	return 1;
}

static void sk_vkrd_reset_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || fences == NULL || fence_count == 0u) {
		return;
	}

	VkFence* vk_fences = (VkFence*)device->allocator->alloc(device->allocator->instance, (size_t)fence_count * sizeof(VkFence));
	if (vk_fences == NULL) {
		return;
	}
	for (u32 i = 0u; i < fence_count; ++i) {
		sk_vk_fence_t* fence = (sk_vk_fence_t*)sk_fence_t_to_ptr(fences[i]);
		vk_fences[i] = fence != NULL ? fence->fence : VK_NULL_HANDLE;
	}
	vkResetFences(device->device, fence_count, vk_fences);
	device->allocator->free(device->allocator->instance, vk_fences);
}

static sk_semaphore_t sk_vkrd_create_semaphore(sk_render_device_t dev, const sk_semaphore_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created) {
		return sk_semaphore_t_zero();
	}

	VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkSemaphore semaphore = VK_NULL_HANDLE;
	if (vkCreateSemaphore(device->device, &semaphore_info, NULL, &semaphore) != VK_SUCCESS) {
		return sk_semaphore_t_zero();
	}

	sk_vk_semaphore_t* semaphore_obj = (sk_vk_semaphore_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_semaphore_t));
	if (semaphore_obj == NULL) {
		vkDestroySemaphore(device->device, semaphore, NULL);
		return sk_semaphore_t_zero();
	}
	memset(semaphore_obj, 0, sizeof(*semaphore_obj));
	semaphore_obj->device = device;
	semaphore_obj->semaphore = semaphore;
	(void)desc;
	return sk_semaphore_t_from_ptr(semaphore_obj);
}

static void sk_vkrd_destroy_semaphore(sk_render_device_t dev, sk_semaphore_t semaphore_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_semaphore_t* semaphore = (sk_vk_semaphore_t*)sk_semaphore_t_to_ptr(semaphore_handle);
	if (device == NULL || semaphore == NULL) {
		return;
	}
	vkDestroySemaphore(device->device, semaphore->semaphore, NULL);
	device->allocator->free(device->allocator->instance, semaphore);
}

/* ------------------------------------------------------------------ */
/* Query pools                                                         */
/* ------------------------------------------------------------------ */

static VkQueryPipelineStatisticFlags sk_vkrd_pipeline_statistics(u32 flags) {
	VkQueryPipelineStatisticFlags result = 0;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_TESSELLATION_EVAL_SHADER_INVOCATIONS) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;
	if ((flags & (u32)SK_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS) != 0u)
		result |= VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
	return result;
}

static sk_query_pool_t sk_vkrd_create_query_pool(sk_render_device_t dev, const sk_query_pool_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_query_pool_t_zero();
	}

	VkQueryPoolCreateInfo query_pool_info = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
	query_pool_info.queryCount = desc->query_count;

	switch (desc->type) {
	case SK_QUERY_TYPE_TIMESTAMP:
		query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		break;
	case SK_QUERY_TYPE_OCCLUSION:
		query_pool_info.queryType = VK_QUERY_TYPE_OCCLUSION;
		break;
	case SK_QUERY_TYPE_PIPELINE_STATISTICS:
		query_pool_info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
		query_pool_info.pipelineStatistics = sk_vkrd_pipeline_statistics(desc->pipeline_statistics);
		break;
	}

	VkQueryPool query_pool = VK_NULL_HANDLE;
	if (vkCreateQueryPool(device->device, &query_pool_info, NULL, &query_pool) != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create query pool");
		return sk_query_pool_t_zero();
	}

	sk_vk_query_pool_t* query_pool_obj = (sk_vk_query_pool_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_query_pool_t));
	if (query_pool_obj == NULL) {
		vkDestroyQueryPool(device->device, query_pool, NULL);
		return sk_query_pool_t_zero();
	}
	memset(query_pool_obj, 0, sizeof(*query_pool_obj));
	query_pool_obj->device = device;
	query_pool_obj->query_pool = query_pool;
	query_pool_obj->desc = *desc;
	query_pool_obj->desc.debug_name = sk_vk_dup_string(device, desc->debug_name);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_QUERY_POOL, (u64)query_pool, desc->debug_name);
	return sk_query_pool_t_from_ptr(query_pool_obj);
}

static void sk_vkrd_destroy_query_pool(sk_render_device_t dev, sk_query_pool_t pool) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_query_pool_t* query_pool = (sk_vk_query_pool_t*)sk_query_pool_t_to_ptr(pool);
	if (device == NULL || query_pool == NULL) {
		return;
	}

	sk_vk_destructor_t destructor = {0};
	destructor.query_pool = query_pool->query_pool;
	sk_vk_enqueue_destructor(device, &destructor);

	sk_vk_free(device, query_pool->desc.debug_name);
	sk_vk_free(device, query_pool);
}

static i32 sk_vkrd_get_query_pool_results(sk_render_device_t dev, sk_query_pool_t pool, u32 first_query, u32 query_count, void* data, u64 data_size, u64 stride, bool wait) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_query_pool_t* query_pool = (sk_vk_query_pool_t*)sk_query_pool_t_to_ptr(pool);
	if (device == NULL || query_pool == NULL || query_pool->query_pool == VK_NULL_HANDLE || data == NULL) {
		return -1;
	}

	VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
	if (wait) {
		flags |= VK_QUERY_RESULT_WAIT_BIT;
	}
	if (query_pool->desc.allow_partial_results) {
		flags |= VK_QUERY_RESULT_PARTIAL_BIT;
	}
	if (query_pool->desc.return_availability) {
		flags |= VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
	}

	VkResult result = vkGetQueryPoolResults(device->device, query_pool->query_pool, first_query, query_count, data_size, data, stride, flags);
	if (result == VK_SUCCESS || (!wait && result == VK_NOT_READY)) {
		return 0;
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* Acceleration structures                                             */
/* ------------------------------------------------------------------ */

static VkDeviceAddress sk_vkrd_buffer_device_address(sk_vk_device_t* device, sk_buffer_t buffer_handle) {
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buffer_handle);
	if (buffer == NULL) {
		return 0;
	}
	return sk_vk_get_buffer_device_address(device->device, buffer->buffer);
}

static void sk_vkrd_convert_geometries(sk_vk_device_t* device, const sk_blas_desc_t* desc, VkAccelerationStructureGeometryKHR* out_geometries,
									   VkAccelerationStructureBuildRangeInfoKHR* out_range_infos, u32* out_max_primitive_counts) {
	for (u32 i = 0u; i < desc->geometry_count; ++i) {
		const sk_geometry_desc_t* geometry = &desc->geometries[i];
		VkAccelerationStructureGeometryKHR* vk_geometry = &out_geometries[i];
		VkAccelerationStructureBuildRangeInfoKHR* range_info = &out_range_infos[i];
		memset(vk_geometry, 0, sizeof(*vk_geometry));
		memset(range_info, 0, sizeof(*range_info));
		vk_geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;

		if (geometry->type == SK_GEOMETRY_TYPE_TRIANGLES) {
			const sk_geometry_triangles_desc_t* tri = &geometry->triangles;
			vk_geometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			vk_geometry->flags = tri->opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0u;

			VkAccelerationStructureGeometryTrianglesDataKHR* tri_data = &vk_geometry->geometry.triangles;
			tri_data->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			tri_data->vertexFormat = sk_vk_to_vk_format(tri->vertex_format);
			tri_data->vertexStride = tri->vertex_stride;
			tri_data->maxVertex = tri->vertex_count > 0u ? tri->vertex_count - 1u : 0u;

			if (sk_buffer_t_is_valid(tri->vertex_buffer)) {
				tri_data->vertexData.deviceAddress = sk_vkrd_buffer_device_address(device, tri->vertex_buffer) + tri->vertex_offset;
			}

			if (sk_buffer_t_is_valid(tri->index_buffer)) {
				tri_data->indexType = tri->index_type == SK_INDEX_TYPE_UINT16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
				tri_data->indexData.deviceAddress = sk_vkrd_buffer_device_address(device, tri->index_buffer) + tri->index_offset;
				u32 primitive_count = tri->index_count / 3u;
				out_max_primitive_counts[i] = primitive_count;
				range_info->primitiveCount = primitive_count;
			} else {
				tri_data->indexType = VK_INDEX_TYPE_NONE_KHR;
				u32 primitive_count = tri->vertex_count / 3u;
				out_max_primitive_counts[i] = primitive_count;
				range_info->primitiveCount = primitive_count;
			}

			if (sk_buffer_t_is_valid(tri->transform_buffer)) {
				tri_data->transformData.deviceAddress = sk_vkrd_buffer_device_address(device, tri->transform_buffer) + tri->transform_offset;
			}
		} else {
			const sk_geometry_aabbs_desc_t* aabb = &geometry->aabbs;
			vk_geometry->geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
			vk_geometry->flags = aabb->opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0u;

			VkAccelerationStructureGeometryAabbsDataKHR* aabb_data = &vk_geometry->geometry.aabbs;
			aabb_data->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
			aabb_data->stride = aabb->aabb_stride;

			if (sk_buffer_t_is_valid(aabb->aabb_buffer)) {
				aabb_data->data.deviceAddress = sk_vkrd_buffer_device_address(device, aabb->aabb_buffer) + aabb->aabb_offset;
			}

			out_max_primitive_counts[i] = aabb->aabb_count;
			range_info->primitiveCount = aabb->aabb_count;
		}
	}
}

static sk_blas_t sk_vkrd_create_bottom_level_as(sk_render_device_t dev, const sk_blas_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL || !device->features.ray_tracing || desc->geometry_count == 0u) {
		return sk_blas_t_zero();
	}

	VkAccelerationStructureGeometryKHR* geometries = (VkAccelerationStructureGeometryKHR*)device->allocator->alloc(device->allocator->instance,
																												   (size_t)desc->geometry_count *
																													   sizeof(VkAccelerationStructureGeometryKHR));
	VkAccelerationStructureBuildRangeInfoKHR* range_infos = (VkAccelerationStructureBuildRangeInfoKHR*)
																device->allocator->alloc(device->allocator->instance,
																						 (size_t)desc->geometry_count * sizeof(VkAccelerationStructureBuildRangeInfoKHR));
	u32* max_primitive_counts = (u32*)device->allocator->alloc(device->allocator->instance, (size_t)desc->geometry_count * sizeof(u32));
	if (geometries == NULL || range_infos == NULL || max_primitive_counts == NULL) {
		device->allocator->free(device->allocator->instance, geometries);
		device->allocator->free(device->allocator->instance, range_infos);
		device->allocator->free(device->allocator->instance, max_primitive_counts);
		return sk_blas_t_zero();
	}
	sk_vkrd_convert_geometries(device, desc, geometries, range_infos, max_primitive_counts);

	VkBuildAccelerationStructureFlagsKHR build_flags = sk_vk_convert_build_as_flags(desc->build_flags);

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_info.flags = build_flags;
	build_info.geometryCount = desc->geometry_count;
	build_info.pGeometries = geometries;

	VkAccelerationStructureBuildSizesInfoKHR size_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(device->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, max_primitive_counts, &size_info);

	VkBufferCreateInfo buffer_create_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	buffer_create_info.size = size_info.accelerationStructureSize;
	buffer_create_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo alloc_info = {0};
	alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkBuffer as_buffer = VK_NULL_HANDLE;
	VmaAllocation as_allocation = NULL;
	if (vmaCreateBuffer(device->vma_allocator, &buffer_create_info, &alloc_info, &as_buffer, &as_allocation, NULL) != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create BLAS backing buffer");
		device->allocator->free(device->allocator->instance, geometries);
		device->allocator->free(device->allocator->instance, range_infos);
		device->allocator->free(device->allocator->instance, max_primitive_counts);
		return sk_blas_t_zero();
	}

	VkAccelerationStructureCreateInfoKHR create_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
	create_info.buffer = as_buffer;
	create_info.size = size_info.accelerationStructureSize;
	create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	VkAccelerationStructureKHR acceleration_structure = VK_NULL_HANDLE;
	if (vkCreateAccelerationStructureKHR(device->device, &create_info, NULL, &acceleration_structure) != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create BLAS");
		vmaDestroyBuffer(device->vma_allocator, as_buffer, as_allocation);
		device->allocator->free(device->allocator->instance, geometries);
		device->allocator->free(device->allocator->instance, range_infos);
		device->allocator->free(device->allocator->instance, max_primitive_counts);
		return sk_blas_t_zero();
	}

	VkAccelerationStructureDeviceAddressInfoKHR address_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
	address_info.accelerationStructure = acceleration_structure;
	VkDeviceAddress device_address = vkGetAccelerationStructureDeviceAddressKHR(device->device, &address_info);

	sk_vk_blas_t* blas = (sk_vk_blas_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_blas_t));
	if (blas == NULL) {
		vkDestroyAccelerationStructureKHR(device->device, acceleration_structure, NULL);
		vmaDestroyBuffer(device->vma_allocator, as_buffer, as_allocation);
		device->allocator->free(device->allocator->instance, geometries);
		device->allocator->free(device->allocator->instance, range_infos);
		device->allocator->free(device->allocator->instance, max_primitive_counts);
		return sk_blas_t_zero();
	}
	memset(blas, 0, sizeof(*blas));
	blas->device = device;
	blas->acceleration_structure = acceleration_structure;
	blas->buffer = as_buffer;
	blas->allocation = as_allocation;
	blas->device_address = device_address;
	blas->build_flags = build_flags;
	blas->geometries = geometries;
	blas->range_infos = range_infos;
	blas->max_primitive_counts = max_primitive_counts;
	blas->geometry_count = desc->geometry_count;
	sk_vk_copy_blas_desc(device, desc, &blas->desc);

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, (u64)acceleration_structure, desc->debug_name);
	return sk_blas_t_from_ptr(blas);
}

static void sk_vkrd_destroy_bottom_level_as(sk_render_device_t dev, sk_blas_t blas_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_blas_t* blas = (sk_vk_blas_t*)sk_blas_t_to_ptr(blas_handle);
	if (device == NULL || blas == NULL) {
		return;
	}

	sk_vk_destructor_t destructor = {0};
	destructor.acceleration_structure = blas->acceleration_structure;
	destructor.buffer = blas->buffer;
	destructor.allocation = blas->allocation;
	sk_vk_enqueue_destructor(device, &destructor);

	sk_vk_free_blas_desc(device, &blas->desc);
	device->allocator->free(device->allocator->instance, blas->geometries);
	device->allocator->free(device->allocator->instance, blas->range_infos);
	device->allocator->free(device->allocator->instance, blas->max_primitive_counts);
	sk_vk_free(device, blas);
}

static void sk_vkrd_update_top_level_as_instance(sk_render_device_t dev, sk_tlas_t tlas_handle, u32 index, const sk_as_instance_desc_t* instance) {
	(void)dev;
	sk_vk_tlas_t* tlas = (sk_vk_tlas_t*)sk_tlas_t_to_ptr(tlas_handle);
	if (tlas == NULL || tlas->instance_mapped == NULL || instance == NULL || index >= tlas->max_instance_count) {
		return;
	}

	VkAccelerationStructureInstanceKHR* instances = (VkAccelerationStructureInstanceKHR*)tlas->instance_mapped;
	VkAccelerationStructureInstanceKHR* vk_instance = &instances[index];
	memset(vk_instance, 0, sizeof(*vk_instance));

	for (u32 row = 0u; row < 3u; ++row) {
		for (u32 col = 0u; col < 4u; ++col) {
			vk_instance->transform.matrix[row][col] = instance->transform[row * 4u + col];
		}
	}

	vk_instance->instanceCustomIndex = instance->instance_id & 0x00FFFFFFu;
	vk_instance->mask = (uint8_t)(instance->instance_mask & 0xFFu);
	vk_instance->instanceShaderBindingTableRecordOffset = instance->shader_binding_table_record_offset & 0x00FFFFFFu;

	VkGeometryInstanceFlagsKHR flags = 0;
	if (instance->front_counter_clockwise) {
		flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
	}
	if (instance->force_opaque) {
		flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	}
	if (instance->force_non_opaque) {
		flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
	}
	vk_instance->flags = (uint8_t)(flags & 0xFFu);

	if (sk_blas_t_is_valid(instance->bottom_level_as)) {
		sk_vk_blas_t* blas = (sk_vk_blas_t*)sk_blas_t_to_ptr(instance->bottom_level_as);
		if (blas != NULL) {
			vk_instance->accelerationStructureReference = blas->device_address;
		}
	}
}

static bool sk_vkrd_update_top_level_as_instances(sk_render_device_t dev, sk_tlas_t tlas_handle, const sk_as_instance_desc_t* instances, u32 instance_count) {
	sk_vk_tlas_t* tlas = (sk_vk_tlas_t*)sk_tlas_t_to_ptr(tlas_handle);
	if (tlas == NULL || instances == NULL || instance_count > tlas->max_instance_count || tlas->instance_mapped == NULL) {
		return false;
	}

	for (u32 i = 0u; i < instance_count; ++i) {
		sk_vkrd_update_top_level_as_instance(dev, tlas_handle, i, &instances[i]);
	}
	tlas->instance_count = instance_count;
	return true;
}

static void sk_vkrd_set_top_level_as_instance_count(sk_render_device_t dev, sk_tlas_t tlas_handle, u32 count) {
	(void)dev;
	sk_vk_tlas_t* tlas = (sk_vk_tlas_t*)sk_tlas_t_to_ptr(tlas_handle);
	if (tlas == NULL) {
		return;
	}
	tlas->instance_count = count < tlas->max_instance_count ? count : tlas->max_instance_count;
}

static sk_tlas_t sk_vkrd_create_top_level_as(sk_render_device_t dev, const sk_tlas_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL || !device->features.ray_tracing) {
		return sk_tlas_t_zero();
	}

	VkBuildAccelerationStructureFlagsKHR build_flags = sk_vk_convert_build_as_flags(desc->build_flags);
	u32 instance_count = desc->instance_count;
	u32 capacity = desc->max_instances > instance_count ? desc->max_instances : instance_count;

	VkAccelerationStructureGeometryKHR geometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry.geometry.instances.arrayOfPointers = VK_FALSE;

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	build_info.flags = build_flags;
	build_info.geometryCount = 1u;
	build_info.pGeometries = &geometry;

	VkAccelerationStructureBuildSizesInfoKHR size_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(device->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &capacity, &size_info);

	VkBufferCreateInfo buffer_create_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	buffer_create_info.size = size_info.accelerationStructureSize;
	buffer_create_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo alloc_info = {0};
	alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkBuffer as_buffer = VK_NULL_HANDLE;
	VmaAllocation as_allocation = NULL;
	if (vmaCreateBuffer(device->vma_allocator, &buffer_create_info, &alloc_info, &as_buffer, &as_allocation, NULL) != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create TLAS backing buffer");
		return sk_tlas_t_zero();
	}

	VkAccelerationStructureCreateInfoKHR create_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
	create_info.buffer = as_buffer;
	create_info.size = size_info.accelerationStructureSize;
	create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

	VkAccelerationStructureKHR acceleration_structure = VK_NULL_HANDLE;
	if (vkCreateAccelerationStructureKHR(device->device, &create_info, NULL, &acceleration_structure) != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create TLAS");
		vmaDestroyBuffer(device->vma_allocator, as_buffer, as_allocation);
		return sk_tlas_t_zero();
	}

	VkAccelerationStructureDeviceAddressInfoKHR address_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
	address_info.accelerationStructure = acceleration_structure;
	VkDeviceAddress device_address = vkGetAccelerationStructureDeviceAddressKHR(device->device, &address_info);

	VkBufferCreateInfo instance_buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	instance_buffer_info.size = sizeof(VkAccelerationStructureInstanceKHR) * (capacity > 0u ? capacity : 1u);
	instance_buffer_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	instance_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo instance_alloc_info = {0};
	instance_alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	instance_alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer instance_buffer = VK_NULL_HANDLE;
	VmaAllocation instance_allocation = NULL;
	VmaAllocationInfo instance_alloc_result = {0};
	if (vmaCreateBuffer(device->vma_allocator, &instance_buffer_info, &instance_alloc_info, &instance_buffer, &instance_allocation, &instance_alloc_result) != VK_SUCCESS) {
		sk_vkrd_log_error("Failed to create TLAS instance buffer");
		vkDestroyAccelerationStructureKHR(device->device, acceleration_structure, NULL);
		vmaDestroyBuffer(device->vma_allocator, as_buffer, as_allocation);
		return sk_tlas_t_zero();
	}

	sk_vk_tlas_t* tlas = (sk_vk_tlas_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_tlas_t));
	if (tlas == NULL) {
		vmaDestroyBuffer(device->vma_allocator, instance_buffer, instance_allocation);
		vkDestroyAccelerationStructureKHR(device->device, acceleration_structure, NULL);
		vmaDestroyBuffer(device->vma_allocator, as_buffer, as_allocation);
		return sk_tlas_t_zero();
	}
	memset(tlas, 0, sizeof(*tlas));
	tlas->device = device;
	tlas->acceleration_structure = acceleration_structure;
	tlas->buffer = as_buffer;
	tlas->allocation = as_allocation;
	tlas->device_address = device_address;
	tlas->build_flags = build_flags;
	tlas->instance_buffer = instance_buffer;
	tlas->instance_allocation = instance_allocation;
	tlas->instance_mapped = instance_alloc_result.pMappedData;
	tlas->instance_count = instance_count;
	tlas->max_instance_count = capacity > 0u ? capacity : 1u;
	sk_vk_copy_tlas_desc(device, desc, &tlas->desc);

	if (desc->instance_count > 0u) {
		sk_vkrd_update_top_level_as_instances(dev, sk_tlas_t_from_ptr(tlas), desc->instances, desc->instance_count);
	}

	sk_vk_set_object_name(device, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, (u64)acceleration_structure, desc->debug_name);
	return sk_tlas_t_from_ptr(tlas);
}

static void sk_vkrd_destroy_top_level_as(sk_render_device_t dev, sk_tlas_t tlas_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_tlas_t* tlas = (sk_vk_tlas_t*)sk_tlas_t_to_ptr(tlas_handle);
	if (device == NULL || tlas == NULL) {
		return;
	}

	sk_vk_destructor_t destructor = {0};
	destructor.acceleration_structure = tlas->acceleration_structure;
	destructor.buffer = tlas->buffer;
	destructor.allocation = tlas->allocation;
	sk_vk_enqueue_destructor(device, &destructor);

	if (tlas->instance_buffer != VK_NULL_HANDLE) {
		sk_vk_destructor_t instance_destructor = {0};
		instance_destructor.buffer = tlas->instance_buffer;
		instance_destructor.allocation = tlas->instance_allocation;
		sk_vk_enqueue_destructor(device, &instance_destructor);
	}

	sk_vk_free_tlas_desc(device, &tlas->desc);
	sk_vk_free(device, tlas);
}

static sk_as_build_sizes_t sk_vkrd_get_blas_build_sizes(sk_render_device_t dev, const sk_blas_desc_t* desc) {
	sk_as_build_sizes_t sizes = {0, 0, 0};
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL || desc->geometry_count == 0u) {
		return sizes;
	}

	VkAccelerationStructureGeometryKHR* geometries = (VkAccelerationStructureGeometryKHR*)device->allocator->alloc(device->allocator->instance,
																												   (size_t)desc->geometry_count *
																													   sizeof(VkAccelerationStructureGeometryKHR));
	VkAccelerationStructureBuildRangeInfoKHR* range_infos = (VkAccelerationStructureBuildRangeInfoKHR*)
																device->allocator->alloc(device->allocator->instance,
																						 (size_t)desc->geometry_count * sizeof(VkAccelerationStructureBuildRangeInfoKHR));
	u32* max_primitive_counts = (u32*)device->allocator->alloc(device->allocator->instance, (size_t)desc->geometry_count * sizeof(u32));
	if (geometries == NULL || range_infos == NULL || max_primitive_counts == NULL) {
		device->allocator->free(device->allocator->instance, geometries);
		device->allocator->free(device->allocator->instance, range_infos);
		device->allocator->free(device->allocator->instance, max_primitive_counts);
		return sizes;
	}
	sk_vkrd_convert_geometries(device, desc, geometries, range_infos, max_primitive_counts);

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_info.flags = sk_vk_convert_build_as_flags(desc->build_flags);
	build_info.geometryCount = desc->geometry_count;
	build_info.pGeometries = geometries;

	VkAccelerationStructureBuildSizesInfoKHR size_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(device->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, max_primitive_counts, &size_info);

	sizes.acceleration_structure_size = size_info.accelerationStructureSize;
	/* The build-time scratch address is aligned up to
	 * minAccelerationStructureScratchOffsetAlignment (see sk_vkrd_aligned_scratch);
	 * main returns buildScratchSize + alignment so callers size the scratch buffer
	 * large enough. Mirror that. */
	u64 scratch_alignment = device->selected_adapter->acceleration_structure_props.minAccelerationStructureScratchOffsetAlignment;
	sizes.build_scratch_size = size_info.buildScratchSize + scratch_alignment;
	sizes.update_scratch_size = size_info.updateScratchSize + scratch_alignment;

	device->allocator->free(device->allocator->instance, geometries);
	device->allocator->free(device->allocator->instance, range_infos);
	device->allocator->free(device->allocator->instance, max_primitive_counts);
	return sizes;
}

static sk_as_build_sizes_t sk_vkrd_get_tlas_build_sizes(sk_render_device_t dev, const sk_tlas_desc_t* desc) {
	sk_as_build_sizes_t sizes = {0, 0, 0};
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sizes;
	}

	u32 instance_count = desc->instance_count;
	u32 capacity = desc->max_instances > instance_count ? desc->max_instances : instance_count;

	VkAccelerationStructureGeometryKHR geometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry.geometry.instances.arrayOfPointers = VK_FALSE;

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	build_info.flags = sk_vk_convert_build_as_flags(desc->build_flags);
	build_info.geometryCount = 1u;
	build_info.pGeometries = &geometry;

	VkAccelerationStructureBuildSizesInfoKHR size_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(device->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &capacity, &size_info);

	sizes.acceleration_structure_size = size_info.accelerationStructureSize;
	/* Match main's GetAccelerationStructureBuildScratchSize: the build-time
	 * scratch device address is aligned up, so report scratch + alignment. */
	u64 scratch_alignment = device->selected_adapter->acceleration_structure_props.minAccelerationStructureScratchOffsetAlignment;
	sizes.build_scratch_size = size_info.buildScratchSize + scratch_alignment;
	sizes.update_scratch_size = size_info.updateScratchSize + scratch_alignment;
	return sizes;
}

static bool sk_vkrd_is_bottom_level_as_compacted(sk_render_device_t dev, sk_blas_t blas_handle) {
	(void)dev;
	sk_vk_blas_t* blas = (sk_vk_blas_t*)sk_blas_t_to_ptr(blas_handle);
	if (blas == NULL) {
		return false;
	}
	return blas->compacted;
}

static u64 sk_vkrd_get_bottom_level_as_compacted_size(sk_render_device_t dev, sk_blas_t blas_handle) {
	(void)dev;
	(void)blas_handle;
	/* Compacted size query is not tracked by main; returns 0 for parity. */
	return 0ull;
}

static sk_blas_desc_t sk_vkrd_get_blas_desc(sk_render_device_t dev, sk_blas_t blas_handle) {
	(void)dev;
	sk_vk_blas_t* blas = (sk_vk_blas_t*)sk_blas_t_to_ptr(blas_handle);
	if (blas == NULL) {
		sk_blas_desc_t desc = {0};
		return desc;
	}
	return blas->desc;
}

static sk_tlas_desc_t sk_vkrd_get_tlas_desc(sk_render_device_t dev, sk_tlas_t tlas_handle) {
	(void)dev;
	sk_vk_tlas_t* tlas = (sk_vk_tlas_t*)sk_tlas_t_to_ptr(tlas_handle);
	if (tlas == NULL) {
		sk_tlas_desc_t desc = {0};
		return desc;
	}
	return tlas->desc;
}

/* ------------------------------------------------------------------ */
/* Queues / submit                                                     */
/* ------------------------------------------------------------------ */

static u32 sk_vkrd_queue_family(sk_vk_device_t* device, u32 queue_type) {
	if ((queue_type & (u32)SK_QUEUE_TYPE_COMPUTE) != 0u && (queue_type & (u32)SK_QUEUE_TYPE_GRAPHICS) == 0u) {
		return sk_vk_device_compute_family(device);
	}
	if ((queue_type & (u32)SK_QUEUE_TYPE_TRANSFER) != 0u && (queue_type & (u32)SK_QUEUE_TYPE_GRAPHICS) == 0u && (queue_type & (u32)SK_QUEUE_TYPE_COMPUTE) == 0u) {
		return sk_vk_device_transfer_family(device);
	}
	return sk_vk_device_graphics_family(device);
}

static sk_queue_t sk_vkrd_create_queue(sk_render_device_t dev, const sk_queue_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return sk_queue_t_zero();
	}

	sk_vk_queue_t* queue = (sk_vk_queue_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_queue_t));
	if (queue == NULL) {
		return sk_queue_t_zero();
	}
	memset(queue, 0, sizeof(*queue));
	queue->device = device;
	queue->desc = *desc;
	queue->family_index = sk_vkrd_queue_family(device, desc->queue_type);
	queue->context = sk_vk_queue_context_for_type(device, desc->queue_type);

	VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	if (vkCreateFence(device->device, &fence_info, NULL, &queue->fence) != VK_SUCCESS) {
		device->allocator->free(device->allocator->instance, queue);
		return sk_queue_t_zero();
	}
	return sk_queue_t_from_ptr(queue);
}

static void sk_vkrd_destroy_queue(sk_render_device_t dev, sk_queue_t queue_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_queue_t* queue = (sk_vk_queue_t*)sk_queue_t_to_ptr(queue_handle);
	if (device == NULL || queue == NULL) {
		return;
	}
	vkDestroyFence(device->device, queue->fence, NULL);
	device->allocator->free(device->allocator->instance, queue);
}

static void sk_vkrd_submit_command_buffer(sk_render_device_t dev, sk_queue_t queue_handle, sk_command_buffer_t cmd_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_queue_t* queue = (sk_vk_queue_t*)sk_queue_t_to_ptr(queue_handle);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (device == NULL || queue == NULL || command_buffer == NULL) {
		return;
	}

	sk_vkrd_advance_frame(device);

	VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
	submit_info.commandBufferCount = 1u;
	submit_info.pCommandBuffers = &command_buffer->command_buffer;
	sk_vk_queue_submit(queue, &submit_info, VK_NULL_HANDLE);
}

static i32 sk_vkrd_submit(sk_render_device_t dev, sk_queue_t queue_handle, const sk_submit_info_t* info) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_queue_t* queue = (sk_vk_queue_t*)sk_queue_t_to_ptr(queue_handle);
	if (device == NULL || queue == NULL || info == NULL) {
		return -1;
	}

	sk_vkrd_advance_frame(device);

	VkCommandBuffer* command_buffers = NULL;
	if (info->command_buffer_count > 0u) {
		command_buffers = (VkCommandBuffer*)device->allocator->alloc(device->allocator->instance, (size_t)info->command_buffer_count * sizeof(VkCommandBuffer));
		if (command_buffers == NULL) {
			return -1;
		}
		for (u32 i = 0u; i < info->command_buffer_count; ++i) {
			sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(info->command_buffers[i]);
			command_buffers[i] = command_buffer != NULL ? command_buffer->command_buffer : VK_NULL_HANDLE;
		}
	}

	VkSemaphore* wait_semaphores = NULL;
	if (info->wait_semaphore_count > 0u) {
		wait_semaphores = (VkSemaphore*)device->allocator->alloc(device->allocator->instance, (size_t)info->wait_semaphore_count * sizeof(VkSemaphore));
		if (wait_semaphores == NULL) {
			device->allocator->free(device->allocator->instance, command_buffers);
			return -1;
		}
		for (u32 i = 0u; i < info->wait_semaphore_count; ++i) {
			sk_vk_semaphore_t* semaphore = (sk_vk_semaphore_t*)sk_semaphore_t_to_ptr(info->wait_semaphores[i]);
			wait_semaphores[i] = semaphore != NULL ? semaphore->semaphore : VK_NULL_HANDLE;
		}
	}

	VkSemaphore* signal_semaphores = NULL;
	if (info->signal_semaphore_count > 0u) {
		signal_semaphores = (VkSemaphore*)device->allocator->alloc(device->allocator->instance, (size_t)info->signal_semaphore_count * sizeof(VkSemaphore));
		if (signal_semaphores == NULL) {
			device->allocator->free(device->allocator->instance, command_buffers);
			device->allocator->free(device->allocator->instance, wait_semaphores);
			return -1;
		}
		for (u32 i = 0u; i < info->signal_semaphore_count; ++i) {
			sk_vk_semaphore_t* semaphore = (sk_vk_semaphore_t*)sk_semaphore_t_to_ptr(info->signal_semaphores[i]);
			signal_semaphores[i] = semaphore != NULL ? semaphore->semaphore : VK_NULL_HANDLE;
		}
	}

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
	submit_info.waitSemaphoreCount = info->wait_semaphore_count;
	submit_info.pWaitSemaphores = wait_semaphores;
	submit_info.pWaitDstStageMask = &wait_stage;
	submit_info.commandBufferCount = info->command_buffer_count;
	submit_info.pCommandBuffers = command_buffers;
	submit_info.signalSemaphoreCount = info->signal_semaphore_count;
	submit_info.pSignalSemaphores = signal_semaphores;

	VkFence fence = VK_NULL_HANDLE;
	if (sk_fence_t_is_valid(info->signal_fence)) {
		sk_vk_fence_t* fence_obj = (sk_vk_fence_t*)sk_fence_t_to_ptr(info->signal_fence);
		if (fence_obj != NULL) {
			fence = fence_obj->fence;
		}
	}

	VkResult result = sk_vk_queue_submit(queue, &submit_info, fence);

	device->allocator->free(device->allocator->instance, command_buffers);
	device->allocator->free(device->allocator->instance, wait_semaphores);
	device->allocator->free(device->allocator->instance, signal_semaphores);

	return result == VK_SUCCESS ? 0 : -1;
}

static i32 sk_vkrd_submit_and_wait(sk_render_device_t dev, sk_queue_t queue_handle, sk_command_buffer_t cmd_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_queue_t* queue = (sk_vk_queue_t*)sk_queue_t_to_ptr(queue_handle);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (device == NULL || queue == NULL || command_buffer == NULL) {
		return -1;
	}

	sk_vkrd_advance_frame(device);

	VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
	submit_info.commandBufferCount = 1u;
	submit_info.pCommandBuffers = &command_buffer->command_buffer;
	sk_vk_queue_submit(queue, &submit_info, queue->fence);

	vkWaitForFences(device->device, 1u, &queue->fence, VK_TRUE, UINT64_MAX);
	vkResetFences(device->device, 1u, &queue->fence);
	sk_vk_flush_destructors(device);
	return 0;
}

static i32 sk_vkrd_queue_wait_idle(sk_render_device_t dev, sk_queue_t queue_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_queue_t* queue = (sk_vk_queue_t*)sk_queue_t_to_ptr(queue_handle);
	if (device == NULL || queue == NULL) {
		return -1;
	}
	VkResult result = vkQueueWaitIdle(queue->context->vk_queue);
	sk_vk_flush_destructors(device);
	return result == VK_SUCCESS ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Device queries (memory requirements)                                */
/* ------------------------------------------------------------------ */

static sk_texture_memory_requirements_t sk_vkrd_get_texture_memory_requirements(sk_render_device_t dev, const sk_texture_desc_t* desc) {
	sk_texture_memory_requirements_t requirements = {0, 0, 0};
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return requirements;
	}

	VkImageCreateInfo image_create_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	sk_vk_make_image_create_info(desc, &image_create_info);

	VkImage image = VK_NULL_HANDLE;
	if (vkCreateImage(device->device, &image_create_info, NULL, &image) != VK_SUCCESS) {
		return requirements;
	}

	VkMemoryRequirements memory_requirements = {0};
	vkGetImageMemoryRequirements(device->device, image, &memory_requirements);
	vkDestroyImage(device->device, image, NULL);

	requirements.size = memory_requirements.size;
	requirements.alignment = memory_requirements.alignment;
	requirements.memory_type_bits = memory_requirements.memoryTypeBits;
	return requirements;
}

static sk_buffer_memory_requirements_t sk_vkrd_get_buffer_memory_requirements(sk_render_device_t dev, const sk_buffer_desc_t* desc) {
	sk_buffer_memory_requirements_t requirements = {0, 0, 0};
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created || desc == NULL) {
		return requirements;
	}

	VkBufferCreateInfo buffer_create_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	buffer_create_info.size = desc->size;
	buffer_create_info.usage = sk_vk_get_buffer_usage_flags(desc->usage_flags, device->features.buffer_device_address);
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer buffer = VK_NULL_HANDLE;
	if (vkCreateBuffer(device->device, &buffer_create_info, NULL, &buffer) != VK_SUCCESS) {
		return requirements;
	}

	VkMemoryRequirements memory_requirements = {0};
	vkGetBufferMemoryRequirements(device->device, buffer, &memory_requirements);
	vkDestroyBuffer(device->device, buffer, NULL);

	requirements.size = memory_requirements.size;
	requirements.alignment = memory_requirements.alignment;
	requirements.memory_type_bits = memory_requirements.memoryTypeBits;
	return requirements;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS
#include "test.h"

SK_TEST(vkrd_render_device_api_table_is_complete) {
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.init);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.wait_idle);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_adapter_count);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_adapter);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.select_adapter);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_adapter_score);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_adapter_name);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_properties);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_features);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_api);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_memory_budgets);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.buffer_map);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.buffer_unmap);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_buffer_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_buffer_mapped_data);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_texture);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_texture);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_texture_view);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_texture_view);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_sampler);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_sampler);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_texture_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_texture_view_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_sampler_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_memory);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_memory);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_aliased_texture);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_memory_size);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_shader);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_shader);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_graphics_pipeline);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_compute_pipeline);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_ray_tracing_pipeline);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_pipeline);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_pipeline_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_pipeline_bind_point);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_descriptor_set);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.update_descriptor_set);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_descriptor_set);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_descriptor_set_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_render_pass);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_render_pass);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_framebuffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_framebuffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_render_pass_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_framebuffer_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_framebuffer_extent);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_swapchain);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_swapchain);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.resize_swapchain);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.acquire_next_image);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_swapchain_image);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_swapchain_extent);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_swapchain_format);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_swapchain_image_count);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_swapchain_current_image_index);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_swapchain_textures);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.present);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_fence);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_fence);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.wait_fences);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.reset_fences);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_semaphore);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_semaphore);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_command_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_command_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.begin_command_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.end_command_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.reset_command_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.execute_commands);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.set_viewport);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.set_scissor);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.set_blend_constants);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.set_stencil_reference);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.set_depth_bias);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.set_depth_bounds);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.bind_pipeline);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.bind_descriptor_set);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.bind_vertex_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.bind_index_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.push_constants);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.draw);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.draw_indexed);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.draw_indirect);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.draw_indexed_indirect);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.draw_indirect_count);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.draw_indexed_indirect_count);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.dispatch);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.dispatch_indirect);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.trace_rays);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.begin_render_pass);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.end_render_pass);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.clear_attachments);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.copy_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.copy_buffer_to_texture);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.copy_texture_to_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.copy_texture);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.blit_texture);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.resolve_texture);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.update_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.fill_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.clear_texture);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.resource_barrier_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.resource_barrier_texture);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.resource_barrier_bottom_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.resource_barrier_top_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.memory_barrier);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.begin_debug_label);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.end_debug_label);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.insert_debug_label);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_query_pool);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_query_pool);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.reset_query_pool);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.begin_query);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.end_query);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.write_timestamp);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.copy_query_pool_results);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_query_pool_results);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_bottom_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_bottom_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_top_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_top_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_blas_build_sizes);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_tlas_build_sizes);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.build_bottom_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.build_top_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.is_bottom_level_as_compacted);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_bottom_level_as_compacted_size);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_blas_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_tlas_desc);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.copy_bottom_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.copy_top_level_as);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.update_top_level_as_instances);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.update_top_level_as_instance);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.set_top_level_as_instance_count);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.create_queue);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.destroy_queue);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.submit_command_buffer);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.submit);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.submit_and_wait);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.queue_wait_idle);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_texture_memory_requirements);
	TEST_ASSERT_NOT_NULL(vulkan_render_device_api.get_buffer_memory_requirements);
}

SK_TEST(vkrd_loader_api_surface_is_wired) {
	TEST_ASSERT_NOT_NULL(vulkan_loader_api.init);
	TEST_ASSERT_NOT_NULL(vulkan_loader_api.shutdown);
	TEST_ASSERT_NOT_NULL(vulkan_loader_api.volk_version);
	TEST_ASSERT_NOT_NULL(vulkan_loader_api.vma_allocator_size);
	TEST_ASSERT_TRUE(vulkan_loader_api.volk_version() > 0u);
	TEST_ASSERT_TRUE(vulkan_loader_api.vma_allocator_size() > 0u);
}

SK_TEST(vkrd_query_pipeline_statistics_flags) {
	u32 flags = (u32)SK_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS | (u32)SK_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS | (u32)SK_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES;
	VkQueryPipelineStatisticFlags converted = sk_vkrd_pipeline_statistics(flags);
	TEST_ASSERT_TRUE((converted & VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT) != 0u);
	TEST_ASSERT_TRUE((converted & VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT) != 0u);
	TEST_ASSERT_TRUE((converted & VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT) != 0u);
	TEST_ASSERT_TRUE((converted & VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT) == 0u);
}
#endif /* SK_TESTS */
