#pragma once

/**
 * @file vulkan_render_device_internal.h
 * @brief Private types for the Vulkan render device plugin (APX-50).
 *
 * Full struct definitions for every backend object live here (a private
 * header, not the public `vulkan_render_device.h`). The public surface only
 * exposes the loader API table; the RHI backend is registered under
 * SK_RENDER_DEVICE_API_TYPE_ID from the plugin entry point.
 */

#include "app.h"
#include "common.h"
#include "allocator.h"
#include "mutex.h"
#include "render_device.h"

#include "volk.h"
#include "vk_mem_alloc.h"

/** Number of in-flight frames resources are deferred before destruction. */
#define SK_VK_FRAMES_IN_FLIGHT 2u

/** Runtime-array (bindless) descriptor capacity, mirrors skore main. */
#define SK_VK_MAX_BINDLESS_RESOURCES 8192u

typedef struct sk_vk_device_t sk_vk_device_t;

/* ------------------------------------------------------------------ */
/* Queue                                                               */
/* ------------------------------------------------------------------ */

/** A Vulkan queue plus the mutex serializing submits on it (main-thread by default). */
typedef struct sk_vk_queue_context_t {
	VkQueue vk_queue;
	sk_mutex_t* mutex;
} sk_vk_queue_context_t;

typedef struct sk_vk_queue_t {
	sk_vk_device_t* device;
	sk_queue_desc_t desc;
	u32 family_index;
	VkFence fence;
	sk_vk_queue_context_t* context;
} sk_vk_queue_t;

/* ------------------------------------------------------------------ */
/* Resources                                                           */
/* ------------------------------------------------------------------ */

typedef struct sk_vk_buffer_t {
	sk_vk_device_t* device;
	VkBuffer buffer;
	VmaAllocation allocation;
	sk_buffer_desc_t desc;
	void* mapped_data;
} sk_vk_buffer_t;

typedef struct sk_vk_memory_t {
	sk_vk_device_t* device;
	VmaAllocation allocation;
	u64 size;
	u32 memory_type_bits;
} sk_vk_memory_t;

typedef struct sk_vk_texture_t {
	sk_vk_device_t* device;
	sk_texture_desc_t desc;
	VkImage image;
	VmaAllocation allocation;
	bool is_depth;
	bool aliased;
	bool owns_image; /* false for swapchain images (swapchain owns the VkImage) */
} sk_vk_texture_t;

typedef struct sk_vk_texture_view_t {
	sk_vk_device_t* device;
	sk_texture_view_desc_t desc;
	VkImageView image_view;
	sk_vk_texture_t* texture;
} sk_vk_texture_view_t;

typedef struct sk_vk_sampler_t {
	sk_vk_device_t* device;
	sk_sampler_desc_t desc;
	VkSampler sampler;
} sk_vk_sampler_t;

typedef struct sk_vk_shader_t {
	sk_vk_device_t* device;
	u32 stage; /* sk_shader_stage_bit_t */
	u32 word_count;
	u32* words; /* owned SPIR-V copy */
} sk_vk_shader_t;

/* ------------------------------------------------------------------ */
/* Render pass / framebuffer / swapchain                               */
/* ------------------------------------------------------------------ */

typedef struct sk_vk_render_pass_t {
	sk_vk_device_t* device;
	sk_render_pass_desc_t desc; /* owned attachment copies */
	VkRenderPass render_pass;
	u32 samples_count;
} sk_vk_render_pass_t;

typedef struct sk_vk_framebuffer_t {
	sk_vk_device_t* device;
	sk_framebuffer_desc_t desc; /* owned attachment handle copies */
	VkFramebuffer framebuffer;
	sk_extent3d_t extent;
	u32 clear_value_count;
	VkClearValue* clear_values;
} sk_vk_framebuffer_t;

typedef struct sk_vk_swapchain_t {
	sk_vk_device_t* device;
	sk_swapchain_desc_t desc;
	VkSurfaceKHR surface;
	void_ptr_t platform_display; /* OS connection the surface depends on (Xlib Display) */
	VkSwapchainKHR swapchain;
	VkExtent2D extent;
	VkFormat format;
	sk_vk_texture_t** textures;
	u32 texture_count;
	u32 image_index;
} sk_vk_swapchain_t;

/* ------------------------------------------------------------------ */
/* Adapter / device                                                    */
/* ------------------------------------------------------------------ */

typedef struct sk_vk_adapter_t {
	sk_vk_device_t* device;
	VkPhysicalDevice physical;
	u32 score;
	u32 graphics_family;
	u32 compute_family;
	u32 transfer_family;
	u32 present_family;

	/* Property chain (pNext linked, mirrors main). */
	VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_props;
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_pipeline_props;
	VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservative_raster_props;
	VkPhysicalDeviceProperties2 device_properties;

	/* Feature chain (pNext linked, mirrors main). */
	VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features;
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features;
	VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features;
	VkPhysicalDeviceShaderDrawParametersFeatures draw_parameters_features;
	VkPhysicalDeviceDescriptorIndexingFeatures indexing_features;
	VkPhysicalDeviceMaintenance4FeaturesKHR maintenance4_features;
	VkPhysicalDeviceMultiviewFeatures multiview_features;
	VkPhysicalDeviceFeatures2 device_features;
} sk_vk_adapter_t;

/* ------------------------------------------------------------------ */
/* Pipeline / descriptor set                                           */
/* ------------------------------------------------------------------ */

typedef struct sk_vk_pipeline_t {
	sk_vk_device_t* device;
	VkPipelineBindPoint bind_point;
	sk_pipeline_desc_t pipeline_desc; /* owned deep copies */
	VkPipeline pipeline;
	VkPipelineLayout pipeline_layout;
	VkDescriptorSetLayout* set_layouts;
	u32 set_layout_count;
} sk_vk_pipeline_t;

typedef struct sk_vk_descriptor_set_t {
	sk_vk_device_t* device;
	sk_descriptor_set_desc_t desc; /* owned binding copies */
	VkDescriptorSet descriptor_set;
	VkDescriptorSetLayout layout;
	VkDescriptorPool dedicated_pool;
} sk_vk_descriptor_set_t;

/* ------------------------------------------------------------------ */
/* Command buffer                                                      */
/* ------------------------------------------------------------------ */

typedef struct sk_vk_command_buffer_t {
	sk_vk_device_t* device;
	VkCommandBuffer command_buffer;
	VkCommandPool command_pool;
	u32 queue_type; /* sk_queue_type_bit_t */
} sk_vk_command_buffer_t;

/* ------------------------------------------------------------------ */
/* Query pool / acceleration structures                                */
/* ------------------------------------------------------------------ */

typedef struct sk_vk_query_pool_t {
	sk_vk_device_t* device;
	sk_query_pool_desc_t desc;
	VkQueryPool query_pool;
} sk_vk_query_pool_t;

typedef struct sk_vk_blas_t {
	sk_vk_device_t* device;
	sk_blas_desc_t desc; /* owned geometry copies */
	VkAccelerationStructureKHR acceleration_structure;
	VkBuffer buffer;
	VmaAllocation allocation;
	VkDeviceAddress device_address;
	VkBuildAccelerationStructureFlagsKHR build_flags;
	bool compacted;
	VkAccelerationStructureGeometryKHR* geometries;
	VkAccelerationStructureBuildRangeInfoKHR* range_infos;
	u32* max_primitive_counts;
	u32 geometry_count;
} sk_vk_blas_t;

typedef struct sk_vk_tlas_t {
	sk_vk_device_t* device;
	sk_tlas_desc_t desc; /* owned instance copies */
	VkAccelerationStructureKHR acceleration_structure;
	VkBuffer buffer;
	VmaAllocation allocation;
	VkDeviceAddress device_address;
	VkBuildAccelerationStructureFlagsKHR build_flags;
	VkBuffer instance_buffer;
	VmaAllocation instance_allocation;
	void* instance_mapped;
	u32 instance_count;
	u32 max_instance_count;
} sk_vk_tlas_t;

/* ------------------------------------------------------------------ */
/* Fence / semaphore                                                   */
/* ------------------------------------------------------------------ */

typedef struct sk_vk_fence_t {
	sk_vk_device_t* device;
	VkFence fence;
} sk_vk_fence_t;

typedef struct sk_vk_semaphore_t {
	sk_vk_device_t* device;
	VkSemaphore semaphore;
} sk_vk_semaphore_t;

/* ------------------------------------------------------------------ */
/* Deferred destruction (resources destroyed while in flight)          */
/* ------------------------------------------------------------------ */

typedef struct sk_vk_destructor_t {
	u64 frame; /* device frame_index at enqueue */
	sk_vk_texture_t* texture;
	sk_vk_texture_view_t* texture_view;
	sk_vk_memory_t* memory;
	sk_vk_shader_t* shader;
	VkBuffer buffer;
	VmaAllocation allocation;
	VkDescriptorSet descriptor_set;
	VkDescriptorSetLayout* set_layouts;
	u32 set_layout_count;
	VkDescriptorPool descriptor_pool;
	VkPipeline pipeline;
	VkPipelineLayout pipeline_layout;
	VkSampler sampler;
	VkRenderPass render_pass;
	VkFramebuffer framebuffer;
	VkQueryPool query_pool;
	VkAccelerationStructureKHR acceleration_structure;
} sk_vk_destructor_t;

/* ------------------------------------------------------------------ */
/* Device                                                              */
/* ------------------------------------------------------------------ */

typedef struct sk_vk_device_t {
	void_ptr_t context;
	const sk_app_api_t* app_api; /* registry lookups (platform window API) */
	const sk_allocator_t* allocator;

	VkInstance instance;
	bool validation_layers_enabled;
	bool debug_utils_extension_present;
	VkDebugUtilsMessengerEXT debug_messenger;
	sk_vk_adapter_t** adapters;
	u32 adapter_count;
	sk_vk_adapter_t* selected_adapter;
	sk_device_properties_t properties;
	sk_device_features_t features;
	bool device_created;

	VkDevice device;
	VmaAllocator vma_allocator;

	sk_mutex_t* descriptor_pool_mutex;
	VkDescriptorPool descriptor_pool;

	sk_vk_queue_context_t* graphics_queue;
	sk_vk_queue_context_t* present_queue;
	sk_vk_queue_context_t* compute_queue;
	sk_vk_queue_context_t* transfer_queue;
	sk_vk_queue_context_t** queue_contexts;
	u32 queue_context_count;

	u64 frame_index;

	sk_vk_destructor_t* destructors;
	u32 destructor_count;
	u32 destructor_capacity;
} sk_vk_device_t;

/* ------------------------------------------------------------------ */
/* Internal helpers shared across the plugin TUs                       */
/* ------------------------------------------------------------------ */

/** Copy a NUL-terminated string through the device allocator (NULL in → NULL out). */
char* sk_vk_dup_string(const sk_vk_device_t* device, const_chr_t src);

/** Allocate + zero `count * size` bytes through the device allocator. */
void_ptr_t sk_vk_alloc(const sk_vk_device_t* device, u64 size);

/** Free a device-allocator pointer. */
void sk_vk_free(const sk_vk_device_t* device, const void* ptr);

/** Deep-copy helpers for descs that own caller arrays. */
void sk_vk_copy_buffer_desc(const sk_vk_device_t* device, const sk_buffer_desc_t* src, sk_buffer_desc_t* dst);
void sk_vk_copy_texture_desc(const sk_vk_device_t* device, const sk_texture_desc_t* src, sk_texture_desc_t* dst);
void sk_vk_copy_texture_view_desc(const sk_vk_device_t* device, const sk_texture_view_desc_t* src, sk_texture_view_desc_t* dst);
void sk_vk_copy_sampler_desc(const sk_vk_device_t* device, const sk_sampler_desc_t* src, sk_sampler_desc_t* dst);
void sk_vk_copy_pipeline_desc(const sk_vk_device_t* device, const sk_pipeline_desc_t* src, sk_pipeline_desc_t* dst);
void sk_vk_free_pipeline_desc(const sk_vk_device_t* device, sk_pipeline_desc_t* desc);
void sk_vk_copy_render_pass_desc(const sk_vk_device_t* device, const sk_render_pass_desc_t* src, sk_render_pass_desc_t* dst);
void sk_vk_free_render_pass_desc(const sk_vk_device_t* device, sk_render_pass_desc_t* desc);
void sk_vk_copy_framebuffer_desc(const sk_vk_device_t* device, const sk_framebuffer_desc_t* src, sk_framebuffer_desc_t* dst);
void sk_vk_free_framebuffer_desc(const sk_vk_device_t* device, sk_framebuffer_desc_t* desc);
void sk_vk_copy_blas_desc(const sk_vk_device_t* device, const sk_blas_desc_t* src, sk_blas_desc_t* dst);
void sk_vk_free_blas_desc(const sk_vk_device_t* device, sk_blas_desc_t* desc);
void sk_vk_copy_tlas_desc(const sk_vk_device_t* device, const sk_tlas_desc_t* src, sk_tlas_desc_t* dst);
void sk_vk_free_tlas_desc(const sk_vk_device_t* device, sk_tlas_desc_t* desc);

/** Deferred destruction engine. */
void sk_vk_enqueue_destructor(sk_vk_device_t* device, const sk_vk_destructor_t* destructor);
void sk_vk_flush_destructors(sk_vk_device_t* device);

/** Create a VkShaderModule from a shader object; caller destroys it. */
VkResult sk_vk_create_shader_module(sk_vk_device_t* device, const sk_vk_shader_t* shader, VkShaderStageFlagBits stage, VkShaderModule* out_module);

/** VkMakeImageCreateInfo from a texture desc (mirrors main). */
void sk_vk_make_image_create_info(const sk_texture_desc_t* desc, VkImageCreateInfo* out);

/** Queue contexts owned by the device (family-index lookup per queue type). */
sk_vk_queue_context_t* sk_vk_device_queue_context(sk_vk_device_t* device, u32 queue_type);

/** Present-family + graphics-family indices of the selected adapter. */
u32 sk_vk_device_graphics_family(sk_vk_device_t* device);
u32 sk_vk_device_present_family(sk_vk_device_t* device);
u32 sk_vk_device_compute_family(sk_vk_device_t* device);
u32 sk_vk_device_transfer_family(sk_vk_device_t* device);

/* ------------------------------------------------------------------ */
/* Command buffer recording (implemented in vulkan_command_buffer.c)   */
/* ------------------------------------------------------------------ */

sk_command_buffer_t sk_vkrd_create_command_buffer(sk_render_device_t dev, const sk_command_buffer_desc_t* desc);
void sk_vkrd_destroy_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd);
i32 sk_vkrd_begin_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_begin_info_t* info);
void sk_vkrd_end_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd);
void sk_vkrd_reset_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd);
void sk_vkrd_execute_commands(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_t* secondary_cmds, u32 secondary_count);

void sk_vkrd_set_viewport(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_viewport, const sk_viewport_t* viewports, u32 viewport_count);
void sk_vkrd_set_scissor(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_scissor, const sk_rect2d_t* scissors, u32 scissor_count);
void sk_vkrd_set_blend_constants(sk_render_device_t dev, sk_command_buffer_t cmd, f32 r, f32 g, f32 b, f32 a);
void sk_vkrd_set_stencil_reference(sk_render_device_t dev, sk_command_buffer_t cmd, u32 reference);
void sk_vkrd_set_depth_bias(sk_render_device_t dev, sk_command_buffer_t cmd, f32 constant_factor, f32 clamp, f32 slope_factor);
void sk_vkrd_set_depth_bounds(sk_render_device_t dev, sk_command_buffer_t cmd, f32 min_depth, f32 max_depth);
void sk_vkrd_bind_pipeline(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline);
void sk_vkrd_bind_descriptor_set(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline, u32 set_index,
								 sk_descriptor_set_t desc_set, const u32* dynamic_offsets, u32 offset_count);
void sk_vkrd_bind_vertex_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_binding, const sk_buffer_t* buffers, const u64* offsets, u32 buffer_count);
void sk_vkrd_bind_index_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, sk_index_type_t index_type);
void sk_vkrd_push_constants(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_t pipeline, u32 shader_stages, u32 offset, u32 size, const void* data);

void sk_vkrd_draw(sk_render_device_t dev, sk_command_buffer_t cmd, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance);
void sk_vkrd_draw_indexed(sk_render_device_t dev, sk_command_buffer_t cmd, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance);
void sk_vkrd_draw_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride);
void sk_vkrd_draw_indexed_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride);
void sk_vkrd_draw_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset, u32 max_draw_count,
								 u32 stride);
void sk_vkrd_draw_indexed_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
										 u32 max_draw_count, u32 stride);
void sk_vkrd_dispatch(sk_render_device_t dev, sk_command_buffer_t cmd, u32 group_count_x, u32 group_count_y, u32 group_count_z);
void sk_vkrd_dispatch_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset);
void sk_vkrd_trace_rays(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_trace_rays_info_t* info);

void sk_vkrd_begin_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_begin_render_pass_info_t* info);
void sk_vkrd_end_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd);
void sk_vkrd_clear_attachments(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_clear_attachment_t* attachments, u32 attachment_count);

void sk_vkrd_copy_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t src, sk_buffer_t dst, u64 size, u64 src_offset, u64 dst_offset);
void sk_vkrd_copy_buffer_to_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info);
void sk_vkrd_copy_texture_to_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info);
void sk_vkrd_copy_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_copy_t* copy_info);
void sk_vkrd_blit_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_blit_t* blit_info);
void sk_vkrd_resolve_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_resolve_t* resolve_info);
void sk_vkrd_update_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, const void* data);
void sk_vkrd_fill_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, u32 data);
void sk_vkrd_clear_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, const sk_clear_values_t* clear, u32 base_mip_level, u32 mip_level_count,
						   u32 base_array_layer, u32 array_layer_count);

void sk_vkrd_resource_barrier_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, sk_resource_state_t old_state, sk_resource_state_t new_state, u32 src_scope,
									 u32 dst_scope);
void sk_vkrd_resource_barrier_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, sk_resource_state_t old_state, sk_resource_state_t new_state,
									  u32 base_mip_level, u32 mip_level_count, u32 base_array_layer, u32 array_layer_count, u32 src_scope, u32 dst_scope);
void sk_vkrd_resource_barrier_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, sk_resource_state_t old_state, sk_resource_state_t new_state,
											  u32 src_scope, u32 dst_scope);
void sk_vkrd_resource_barrier_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, sk_resource_state_t old_state, sk_resource_state_t new_state,
										   u32 src_scope, u32 dst_scope);
void sk_vkrd_memory_barrier(sk_render_device_t dev, sk_command_buffer_t cmd);

void sk_vkrd_begin_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a);
void sk_vkrd_end_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd);
void sk_vkrd_insert_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a);

void sk_vkrd_reset_query_pool(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count);
void sk_vkrd_begin_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
void sk_vkrd_end_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
void sk_vkrd_write_timestamp(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
void sk_vkrd_copy_query_pool_results(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count, sk_buffer_t dst_buffer,
									 u64 dst_offset, u64 stride);

void sk_vkrd_build_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, const sk_blas_desc_t* desc, const sk_as_build_info_t* build_info);
void sk_vkrd_build_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, const sk_tlas_desc_t* desc, const sk_as_build_info_t* build_info);
void sk_vkrd_copy_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t src, sk_blas_t dst, bool compress);
void sk_vkrd_copy_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t src, sk_tlas_t dst, bool compress);
