#pragma once

/**
 * @file vulkan_utils.h
 * @brief Vulkan conversion / platform helpers for the vulkan_render_device plugin.
 *
 * Port of skore main's `VulkanUtils.cpp` to the C RHI surface: enum→Vk
 * conversions, swapchain support queries, device-extension queries, object
 * naming, buffer device addresses, and the OS-specific surface helpers that
 * main got from SDL (here built on the native window handle exposed by the
 * sk-platform-window plugin).
 */

#include "common.h"
#include "vulkan_render_device_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Resource usage / format helpers --- */
VkBufferUsageFlags sk_vk_get_buffer_usage_flags(u32 usage, bool support_buffer_device_address);
VkImageUsageFlags sk_vk_get_image_usage_flags(u32 usage);
VkImageAspectFlags sk_vk_get_image_aspect_flags(VkFormat format);
bool sk_vk_is_depth_format_vk(VkFormat format);
bool sk_vk_is_depth_format(sk_pixel_format_t format);
VkImageViewType sk_vk_get_image_view_type(sk_texture_view_type_t view_type);
VkSamplerAddressMode sk_vk_convert_address_mode(sk_texture_address_mode_t mode);
VkCompareOp sk_vk_convert_compare_op(sk_compare_op_t op);
VkBorderColor sk_vk_convert_border_color(sk_border_color_t color);
VkDescriptorType sk_vk_convert_descriptor_type(sk_descriptor_type_t type);
VkImageLayout sk_vk_cast_state(sk_resource_state_t state, VkImageLayout default_undefined);
VkAttachmentLoadOp sk_vk_cast_load_op(sk_attachment_load_op_t op);
VkAttachmentStoreOp sk_vk_cast_store_op(sk_attachment_store_op_t op);
VkShaderStageFlags sk_vk_convert_shader_stage_flags(u32 stages);
VkPrimitiveTopology sk_vk_convert_primitive_topology(sk_primitive_topology_t topology);
VkPolygonMode sk_vk_convert_polygon_mode(sk_polygon_mode_t mode);
VkCullModeFlags sk_vk_convert_cull_mode(sk_cull_mode_t mode);
VkFrontFace sk_vk_convert_front_face(sk_front_face_t front_face);
VkBlendFactor sk_vk_convert_blend_factor(sk_blend_factor_t factor);
VkBlendOp sk_vk_convert_blend_op(sk_blend_op_t op);
VkStencilOp sk_vk_convert_stencil_op(sk_stencil_op_t op);
VkSampleCountFlagBits sk_vk_cast_sample_count(u32 sample_count);
VkSampleCountFlagBits sk_vk_get_max_usable_sample_count(const VkPhysicalDeviceProperties* properties);
VkFormat sk_vk_to_vk_format(sk_pixel_format_t format);
sk_pixel_format_t sk_vk_to_format(VkFormat format);
VkDeviceAddress sk_vk_get_buffer_device_address(VkDevice device, VkBuffer buffer);
VkBuildAccelerationStructureFlagsKHR sk_vk_convert_build_as_flags(u32 flags);
VkAccessFlags sk_vk_get_access_flags_from_state(sk_resource_state_t state);
VkPipelineStageFlags sk_vk_get_pipeline_stage_from_state(sk_resource_state_t state);

/* --- Object naming (debug utils) --- */
void sk_vk_set_object_name(sk_vk_device_t* device, VkObjectType type, u64 handle, const_chr_t name);

/* --- Descriptor / pipeline layout creation --- */
VkResult sk_vk_create_descriptor_set_layout(sk_vk_device_t* device, const sk_descriptor_set_layout_binding_t* bindings, u32 binding_count, bool push_descriptor,
											VkDescriptorSetLayout* out_layout, bool* out_has_runtime_array);
VkResult sk_vk_create_pipeline_layout(sk_vk_device_t* device, const sk_pipeline_desc_t* pipeline_desc, bool push_descriptor, const sk_descriptor_set_override_t* overrides,
									  u32 override_count, VkPipelineLayout* out_layout, VkDescriptorSetLayout** out_set_layouts, u32* out_set_layout_count);

/* --- Swapchain support --- */
typedef struct sk_vk_swapchain_support_t {
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR* formats;
	u32 format_count;
	VkPresentModeKHR* present_modes;
	u32 present_mode_count;
} sk_vk_swapchain_support_t;

void sk_vk_query_swapchain_support(VkPhysicalDevice physical, VkSurfaceKHR surface, sk_vk_swapchain_support_t* out, const sk_allocator_t* allocator);
void sk_vk_destroy_swapchain_support(sk_vk_swapchain_support_t* support, const sk_allocator_t* allocator);
VkSurfaceFormatKHR sk_vk_choose_surface_format(const sk_vk_swapchain_support_t* support, VkSurfaceFormatKHR desired);
VkPresentModeKHR sk_vk_choose_present_mode(const sk_vk_swapchain_support_t* support, VkPresentModeKHR desired);
VkExtent2D sk_vk_choose_extent(const sk_vk_swapchain_support_t* support, u32 width, u32 height);

/* --- Instance extensions / platform surface (SDL/GLFW parity) --- */
bool sk_vk_platform_get_required_instance_extensions(const_chr_t* names, u32 max_names, u32* out_count);
bool sk_vk_platform_get_presentation_support(VkInstance instance, VkPhysicalDevice physical, u32 family_index);
/**
 * Create a VkSurfaceKHR for a native window handle.
 * @param out_display Optional; receives an OS handle the surface depends on
 *        (e.g. the Xlib Display) that the caller must keep open until the
 *        surface is destroyed. NULL when not applicable.
 */
bool sk_vk_platform_create_surface(sk_vk_device_t* device, void_ptr_t window, VkSurfaceKHR* out_surface, void_ptr_t* out_display);
/** Close an OS display/connection handle returned by sk_vk_platform_create_surface. */
void sk_vk_platform_destroy_surface_handle(void_ptr_t display);

/* --- Extension / layer queries --- */
bool sk_vk_query_layer_properties(const_chr_t* required_layers, u32 layer_count);
bool sk_vk_query_instance_extensions(const_chr_t* required_extensions, u32 extension_count);
bool sk_vk_device_extension_present(VkPhysicalDevice physical, const_chr_t extension_name);

#ifdef __cplusplus
}
#endif
