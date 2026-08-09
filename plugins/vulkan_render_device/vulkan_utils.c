/*
 * vulkan_utils.c — conversion / platform helpers for the Vulkan render device.
 *
 * Port of skore main's VulkanUtils.cpp onto the C RHI enums. OS-specific
 * surface helpers replace main's SDL dependency: the plugin builds the
 * VkSurfaceKHR from the native window handle exposed by sk-platform-window.
 */

/* Platform surface macros must be defined before any Vulkan header is pulled
 * in (via vulkan_utils.h → internal header → volk.h) so the matching
 * VK_KHR_*_surface structs/prototypes are declared. */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#include <objc/message.h>
#include <objc/runtime.h>
#else
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#endif

#include "vulkan_utils.h"

#include "platform_window.h"

#include <stddef.h>
#include <string.h>

#if defined(__APPLE__)
/* NSWindow → contentView → (CAMetal)Layer via the ObjC runtime (no .m TU).
 * objc_msgSend is declared without a prototype in <objc/message.h>, so calls
 * are dispatched through typed function pointers to match the target method
 * signatures (also the ABI-safe way to pass non-object return types). */
static void* sk_vk_apple_view_from_window(void* ns_window) {
	if (ns_window == NULL) {
		return NULL;
	}
	typedef id (*sk_objc_msg_send_0_fn)(id, SEL);
	typedef void (*sk_objc_msg_send_bool_fn)(id, SEL, BOOL);
	typedef void (*sk_objc_msg_send_id_fn)(id, SEL, id);

	sk_objc_msg_send_0_fn msg_send_0 = (sk_objc_msg_send_0_fn)(uintptr_t)objc_msgSend;
	sk_objc_msg_send_bool_fn msg_send_bool = (sk_objc_msg_send_bool_fn)(uintptr_t)objc_msgSend;
	sk_objc_msg_send_id_fn msg_send_id = (sk_objc_msg_send_id_fn)(uintptr_t)objc_msgSend;

	id window = (id)ns_window;
	id view = msg_send_0(window, sel_registerName("contentView"));
	if (view == nil) {
		return NULL;
	}
	id layer = msg_send_0(view, sel_registerName("layer"));
	if (layer == nil) {
		Class metal_layer_class = objc_getClass("CAMetalLayer");
		if (metal_layer_class == nil) {
			return NULL;
		}
		id new_layer = msg_send_0((id)metal_layer_class, sel_registerName("alloc"));
		new_layer = msg_send_0(new_layer, sel_registerName("init"));
		msg_send_bool(view, sel_registerName("setWantsLayer:"), 1);
		msg_send_id(view, sel_registerName("setLayer:"), new_layer);
		layer = new_layer;
	}
	return (void*)layer;
}
#endif

/* ------------------------------------------------------------------ */
/* Resource usage / format conversions                                 */
/* ------------------------------------------------------------------ */

VkBufferUsageFlags sk_vk_get_buffer_usage_flags(u32 usage, bool support_buffer_device_address) {
	VkBufferUsageFlags usage_flags = 0;

	if ((usage & (u32)SK_RESOURCE_USAGE_VERTEX_BUFFER) != 0u)
		usage_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_INDEX_BUFFER) != 0u)
		usage_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_CONSTANT_BUFFER) != 0u)
		usage_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_INDIRECT_BUFFER) != 0u)
		usage_flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_SHADER_RESOURCE) != 0u)
		usage_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_UNORDERED_ACCESS) != 0u)
		usage_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_COPY_DEST) != 0u)
		usage_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_COPY_SOURCE) != 0u)
		usage_flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_ACCELERATION_STRUCTURE) != 0u)
		usage_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	if (support_buffer_device_address)
		usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	return usage_flags;
}

VkImageUsageFlags sk_vk_get_image_usage_flags(u32 usage) {
	VkImageUsageFlags usage_flags = 0;

	if ((usage & (u32)SK_RESOURCE_USAGE_SHADER_RESOURCE) != 0u)
		usage_flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_RENDER_TARGET) != 0u)
		usage_flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_DEPTH_STENCIL) != 0u)
		usage_flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_UNORDERED_ACCESS) != 0u)
		usage_flags |= VK_IMAGE_USAGE_STORAGE_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_COPY_DEST) != 0u)
		usage_flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if ((usage & (u32)SK_RESOURCE_USAGE_COPY_SOURCE) != 0u)
		usage_flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	return usage_flags;
}

VkImageAspectFlags sk_vk_get_image_aspect_flags(VkFormat format) {
	if (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
		VkImageAspectFlags aspect_flags = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
			aspect_flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		return aspect_flags;
	}
	return VK_IMAGE_ASPECT_COLOR_BIT;
}

bool sk_vk_is_depth_format_vk(VkFormat format) {
	return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

bool sk_vk_is_depth_format(sk_pixel_format_t format) {
	return format == SK_PIXEL_FORMAT_D16_UNORM || format == SK_PIXEL_FORMAT_D24_UNORM_S8_UINT || format == SK_PIXEL_FORMAT_D32_FLOAT || format == SK_PIXEL_FORMAT_D32_FLOAT_S8_UINT;
}

VkImageViewType sk_vk_get_image_view_type(sk_texture_view_type_t view_type) {
	switch (view_type) {
	case SK_TEXTURE_VIEW_TYPE_1D:
		return VK_IMAGE_VIEW_TYPE_1D;
	case SK_TEXTURE_VIEW_TYPE_1D_ARRAY:
		return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
	case SK_TEXTURE_VIEW_TYPE_2D:
		return VK_IMAGE_VIEW_TYPE_2D;
	case SK_TEXTURE_VIEW_TYPE_2D_ARRAY:
		return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	case SK_TEXTURE_VIEW_TYPE_3D:
		return VK_IMAGE_VIEW_TYPE_3D;
	case SK_TEXTURE_VIEW_TYPE_CUBE:
		return VK_IMAGE_VIEW_TYPE_CUBE;
	case SK_TEXTURE_VIEW_TYPE_CUBE_ARRAY:
		return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
	case SK_TEXTURE_VIEW_TYPE_UNDEFINED:
		return VK_IMAGE_VIEW_TYPE_2D;
	}
	return VK_IMAGE_VIEW_TYPE_2D;
}

VkSamplerAddressMode sk_vk_convert_address_mode(sk_texture_address_mode_t mode) {
	switch (mode) {
	case SK_TEXTURE_ADDRESS_REPEAT:
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	case SK_TEXTURE_ADDRESS_MIRRORED_REPEAT:
		return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case SK_TEXTURE_ADDRESS_CLAMP_TO_EDGE:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case SK_TEXTURE_ADDRESS_CLAMP_TO_BORDER:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	case SK_TEXTURE_ADDRESS_MIRROR_CLAMP_TO_EDGE:
		return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
	}
	return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkCompareOp sk_vk_convert_compare_op(sk_compare_op_t op) {
	switch (op) {
	case SK_COMPARE_OP_NEVER:
		return VK_COMPARE_OP_NEVER;
	case SK_COMPARE_OP_LESS:
		return VK_COMPARE_OP_LESS;
	case SK_COMPARE_OP_EQUAL:
		return VK_COMPARE_OP_EQUAL;
	case SK_COMPARE_OP_LESS_EQUAL:
		return VK_COMPARE_OP_LESS_OR_EQUAL;
	case SK_COMPARE_OP_GREATER:
		return VK_COMPARE_OP_GREATER;
	case SK_COMPARE_OP_NOT_EQUAL:
		return VK_COMPARE_OP_NOT_EQUAL;
	case SK_COMPARE_OP_GREATER_EQUAL:
		return VK_COMPARE_OP_GREATER_OR_EQUAL;
	case SK_COMPARE_OP_ALWAYS:
		return VK_COMPARE_OP_ALWAYS;
	}
	return VK_COMPARE_OP_NEVER;
}

VkBorderColor sk_vk_convert_border_color(sk_border_color_t color) {
	switch (color) {
	case SK_BORDER_COLOR_TRANSPARENT_BLACK:
		return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	case SK_BORDER_COLOR_OPAQUE_BLACK:
		return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	case SK_BORDER_COLOR_OPAQUE_WHITE:
		return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	}
	return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
}

VkDescriptorType sk_vk_convert_descriptor_type(sk_descriptor_type_t type) {
	switch (type) {
	case SK_DESCRIPTOR_TYPE_SAMPLER:
		return VK_DESCRIPTOR_TYPE_SAMPLER;
	case SK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	case SK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	case SK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
	case SK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
		return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
	case SK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	case SK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	case SK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	case SK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	case SK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
		return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	case SK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
		return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	case SK_DESCRIPTOR_TYPE_NONE:
		break;
	}
	return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

VkImageLayout sk_vk_cast_state(sk_resource_state_t state, VkImageLayout default_undefined) {
	switch (state) {
	case SK_RESOURCE_STATE_UNDEFINED:
		return default_undefined;
	case SK_RESOURCE_STATE_GENERAL:
		return VK_IMAGE_LAYOUT_GENERAL;
	case SK_RESOURCE_STATE_COLOR_ATTACHMENT:
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	case SK_RESOURCE_STATE_DEPTH_STENCIL_ATTACH:
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	case SK_RESOURCE_STATE_DEPTH_STENCIL_READ:
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	case SK_RESOURCE_STATE_SHADER_READ:
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	case SK_RESOURCE_STATE_COPY_DEST:
		return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	case SK_RESOURCE_STATE_COPY_SOURCE:
		return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	case SK_RESOURCE_STATE_PRESENT:
		return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	case SK_RESOURCE_STATE_INDIRECT_ARGUMENT:
	case SK_RESOURCE_STATE_UNORDERED_ACCESS:
	case SK_RESOURCE_STATE_ACCELERATION_STRUCTURE:
		return VK_IMAGE_LAYOUT_GENERAL;
	}
	return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkAttachmentLoadOp sk_vk_cast_load_op(sk_attachment_load_op_t op) {
	switch (op) {
	case SK_ATTACHMENT_LOAD_OP_LOAD:
		return VK_ATTACHMENT_LOAD_OP_LOAD;
	case SK_ATTACHMENT_LOAD_OP_CLEAR:
		return VK_ATTACHMENT_LOAD_OP_CLEAR;
	case SK_ATTACHMENT_LOAD_OP_DONT_CARE:
		return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}
	return VK_ATTACHMENT_LOAD_OP_LOAD;
}

VkAttachmentStoreOp sk_vk_cast_store_op(sk_attachment_store_op_t op) {
	switch (op) {
	case SK_ATTACHMENT_STORE_OP_STORE:
		return VK_ATTACHMENT_STORE_OP_STORE;
	case SK_ATTACHMENT_STORE_OP_DONT_CARE:
		return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	return VK_ATTACHMENT_STORE_OP_STORE;
}

VkShaderStageFlags sk_vk_convert_shader_stage_flags(u32 stages) {
	VkShaderStageFlags stage_flags = 0;

	if ((stages & (u32)SK_SHADER_STAGE_VERTEX) != 0u)
		stage_flags |= VK_SHADER_STAGE_VERTEX_BIT;
	if ((stages & (u32)SK_SHADER_STAGE_HULL) != 0u)
		stage_flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
	if ((stages & (u32)SK_SHADER_STAGE_DOMAIN) != 0u)
		stage_flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
	if ((stages & (u32)SK_SHADER_STAGE_GEOMETRY) != 0u)
		stage_flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
	if ((stages & (u32)SK_SHADER_STAGE_PIXEL) != 0u)
		stage_flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
	if ((stages & (u32)SK_SHADER_STAGE_COMPUTE) != 0u)
		stage_flags |= VK_SHADER_STAGE_COMPUTE_BIT;
	if ((stages & (u32)SK_SHADER_STAGE_AMPLIFICATION) != 0u)
		stage_flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
	if ((stages & (u32)SK_SHADER_STAGE_MESH) != 0u)
		stage_flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
	if ((stages & (u32)SK_SHADER_STAGE_RAYGEN) != 0u)
		stage_flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	if ((stages & (u32)SK_SHADER_STAGE_ANY_HIT) != 0u)
		stage_flags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	if ((stages & (u32)SK_SHADER_STAGE_CLOSEST_HIT) != 0u)
		stage_flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	if ((stages & (u32)SK_SHADER_STAGE_MISS) != 0u)
		stage_flags |= VK_SHADER_STAGE_MISS_BIT_KHR;
	if ((stages & (u32)SK_SHADER_STAGE_INTERSECTION) != 0u)
		stage_flags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
	if ((stages & (u32)SK_SHADER_STAGE_CALLABLE) != 0u)
		stage_flags |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
	if ((stages & (u32)SK_SHADER_STAGE_ALL) != 0u)
		stage_flags |= VK_SHADER_STAGE_ALL;

	return stage_flags;
}

VkPrimitiveTopology sk_vk_convert_primitive_topology(sk_primitive_topology_t topology) {
	switch (topology) {
	case SK_PRIMITIVE_TOPOLOGY_POINT_LIST:
		return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	case SK_PRIMITIVE_TOPOLOGY_LINE_LIST:
		return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	case SK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
		return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
	case SK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	case SK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	}
	return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkPolygonMode sk_vk_convert_polygon_mode(sk_polygon_mode_t mode) {
	switch (mode) {
	case SK_POLYGON_MODE_FILL:
		return VK_POLYGON_MODE_FILL;
	case SK_POLYGON_MODE_LINE:
		return VK_POLYGON_MODE_LINE;
	case SK_POLYGON_MODE_POINT:
		return VK_POLYGON_MODE_POINT;
	}
	return VK_POLYGON_MODE_FILL;
}

VkCullModeFlags sk_vk_convert_cull_mode(sk_cull_mode_t mode) {
	switch (mode) {
	case SK_CULL_MODE_NONE:
		return VK_CULL_MODE_NONE;
	case SK_CULL_MODE_FRONT:
		return VK_CULL_MODE_FRONT_BIT;
	case SK_CULL_MODE_BACK:
		return VK_CULL_MODE_BACK_BIT;
	}
	return VK_CULL_MODE_BACK_BIT;
}

VkFrontFace sk_vk_convert_front_face(sk_front_face_t front_face) {
	return (front_face == SK_FRONT_FACE_CLOCKWISE) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

VkBlendFactor sk_vk_convert_blend_factor(sk_blend_factor_t factor) {
	switch (factor) {
	case SK_BLEND_FACTOR_ZERO:
		return VK_BLEND_FACTOR_ZERO;
	case SK_BLEND_FACTOR_ONE:
		return VK_BLEND_FACTOR_ONE;
	case SK_BLEND_FACTOR_SRC_COLOR:
		return VK_BLEND_FACTOR_SRC_COLOR;
	case SK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
		return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case SK_BLEND_FACTOR_DST_COLOR:
		return VK_BLEND_FACTOR_DST_COLOR;
	case SK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
		return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case SK_BLEND_FACTOR_SRC_ALPHA:
		return VK_BLEND_FACTOR_SRC_ALPHA;
	case SK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case SK_BLEND_FACTOR_DST_ALPHA:
		return VK_BLEND_FACTOR_DST_ALPHA;
	case SK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case SK_BLEND_FACTOR_CONSTANT_COLOR:
		return VK_BLEND_FACTOR_CONSTANT_COLOR;
	case SK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
		return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
	case SK_BLEND_FACTOR_CONSTANT_ALPHA:
		return VK_BLEND_FACTOR_CONSTANT_ALPHA;
	case SK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
	case SK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
		return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
	}
	return VK_BLEND_FACTOR_ONE;
}

VkBlendOp sk_vk_convert_blend_op(sk_blend_op_t op) {
	switch (op) {
	case SK_BLEND_OP_ADD:
		return VK_BLEND_OP_ADD;
	case SK_BLEND_OP_SUBTRACT:
		return VK_BLEND_OP_SUBTRACT;
	case SK_BLEND_OP_REVERSE_SUBTRACT:
		return VK_BLEND_OP_REVERSE_SUBTRACT;
	case SK_BLEND_OP_MIN:
		return VK_BLEND_OP_MIN;
	case SK_BLEND_OP_MAX:
		return VK_BLEND_OP_MAX;
	}
	return VK_BLEND_OP_ADD;
}

VkStencilOp sk_vk_convert_stencil_op(sk_stencil_op_t op) {
	switch (op) {
	case SK_STENCIL_OP_KEEP:
		return VK_STENCIL_OP_KEEP;
	case SK_STENCIL_OP_ZERO:
		return VK_STENCIL_OP_ZERO;
	case SK_STENCIL_OP_REPLACE:
		return VK_STENCIL_OP_REPLACE;
	case SK_STENCIL_OP_INCREMENT_CLAMP:
		return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	case SK_STENCIL_OP_DECREMENT_CLAMP:
		return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
	case SK_STENCIL_OP_INVERT:
		return VK_STENCIL_OP_INVERT;
	case SK_STENCIL_OP_INCREMENT_WRAP:
		return VK_STENCIL_OP_INCREMENT_AND_WRAP;
	case SK_STENCIL_OP_DECREMENT_WRAP:
		return VK_STENCIL_OP_DECREMENT_AND_WRAP;
	}
	return VK_STENCIL_OP_KEEP;
}

VkSampleCountFlagBits sk_vk_cast_sample_count(u32 sample_count) {
	switch (sample_count) {
	case 1u:
		return VK_SAMPLE_COUNT_1_BIT;
	case 2u:
		return VK_SAMPLE_COUNT_2_BIT;
	case 4u:
		return VK_SAMPLE_COUNT_4_BIT;
	case 8u:
		return VK_SAMPLE_COUNT_8_BIT;
	case 16u:
		return VK_SAMPLE_COUNT_16_BIT;
	case 32u:
		return VK_SAMPLE_COUNT_32_BIT;
	case 64u:
		return VK_SAMPLE_COUNT_64_BIT;
	default:
		return VK_SAMPLE_COUNT_1_BIT;
	}
}

VkSampleCountFlagBits sk_vk_get_max_usable_sample_count(const VkPhysicalDeviceProperties* properties) {
	VkSampleCountFlags counts = properties->limits.framebufferColorSampleCounts & properties->limits.framebufferDepthSampleCounts;

	if ((counts & VK_SAMPLE_COUNT_64_BIT) != 0u) {
		return VK_SAMPLE_COUNT_64_BIT;
	}
	if ((counts & VK_SAMPLE_COUNT_32_BIT) != 0u) {
		return VK_SAMPLE_COUNT_32_BIT;
	}
	if ((counts & VK_SAMPLE_COUNT_16_BIT) != 0u) {
		return VK_SAMPLE_COUNT_16_BIT;
	}
	if ((counts & VK_SAMPLE_COUNT_8_BIT) != 0u) {
		return VK_SAMPLE_COUNT_8_BIT;
	}
	if ((counts & VK_SAMPLE_COUNT_4_BIT) != 0u) {
		return VK_SAMPLE_COUNT_4_BIT;
	}
	if ((counts & VK_SAMPLE_COUNT_2_BIT) != 0u) {
		return VK_SAMPLE_COUNT_2_BIT;
	}
	return VK_SAMPLE_COUNT_1_BIT;
}

VkFormat sk_vk_to_vk_format(sk_pixel_format_t format) {
	switch (format) {
	case SK_PIXEL_FORMAT_UNKNOWN:
		return VK_FORMAT_UNDEFINED;

	case SK_PIXEL_FORMAT_R8_UNORM:
		return VK_FORMAT_R8_UNORM;
	case SK_PIXEL_FORMAT_R8_SNORM:
		return VK_FORMAT_R8_SNORM;
	case SK_PIXEL_FORMAT_R8_UINT:
		return VK_FORMAT_R8_UINT;
	case SK_PIXEL_FORMAT_R8_SINT:
		return VK_FORMAT_R8_SINT;
	case SK_PIXEL_FORMAT_R8_SRGB:
		return VK_FORMAT_R8_SRGB;

	case SK_PIXEL_FORMAT_R16_UNORM:
		return VK_FORMAT_R16_UNORM;
	case SK_PIXEL_FORMAT_R16_SNORM:
		return VK_FORMAT_R16_SNORM;
	case SK_PIXEL_FORMAT_R16_UINT:
		return VK_FORMAT_R16_UINT;
	case SK_PIXEL_FORMAT_R16_SINT:
		return VK_FORMAT_R16_SINT;
	case SK_PIXEL_FORMAT_R16_FLOAT:
		return VK_FORMAT_R16_SFLOAT;
	case SK_PIXEL_FORMAT_RG8_UNORM:
		return VK_FORMAT_R8G8_UNORM;
	case SK_PIXEL_FORMAT_RG8_SNORM:
		return VK_FORMAT_R8G8_SNORM;
	case SK_PIXEL_FORMAT_RG8_UINT:
		return VK_FORMAT_R8G8_UINT;
	case SK_PIXEL_FORMAT_RG8_SINT:
		return VK_FORMAT_R8G8_SINT;
	case SK_PIXEL_FORMAT_RG8_SRGB:
		return VK_FORMAT_R8G8_SRGB;
	case SK_PIXEL_FORMAT_RGB16_UNORM:
		return VK_FORMAT_R16G16B16_UNORM;
	case SK_PIXEL_FORMAT_RGB16_SNORM:
		return VK_FORMAT_R16G16B16_SNORM;
	case SK_PIXEL_FORMAT_RGB16_UINT:
		return VK_FORMAT_R16G16B16_UINT;
	case SK_PIXEL_FORMAT_RGB16_SINT:
		return VK_FORMAT_R16G16B16_SINT;
	case SK_PIXEL_FORMAT_RGB16_FLOAT:
		return VK_FORMAT_R16G16B16_SFLOAT;

	case SK_PIXEL_FORMAT_R32_UINT:
		return VK_FORMAT_R32_UINT;
	case SK_PIXEL_FORMAT_R32_SINT:
		return VK_FORMAT_R32_SINT;
	case SK_PIXEL_FORMAT_R32_FLOAT:
		return VK_FORMAT_R32_SFLOAT;
	case SK_PIXEL_FORMAT_RG16_UNORM:
		return VK_FORMAT_R16G16_UNORM;
	case SK_PIXEL_FORMAT_RG16_SNORM:
		return VK_FORMAT_R16G16_SNORM;
	case SK_PIXEL_FORMAT_RG16_UINT:
		return VK_FORMAT_R16G16_UINT;
	case SK_PIXEL_FORMAT_RG16_SINT:
		return VK_FORMAT_R16G16_SINT;
	case SK_PIXEL_FORMAT_RG16_FLOAT:
		return VK_FORMAT_R16G16_SFLOAT;
	case SK_PIXEL_FORMAT_RGBA8_UNORM:
		return VK_FORMAT_R8G8B8A8_UNORM;
	case SK_PIXEL_FORMAT_RGBA8_SNORM:
		return VK_FORMAT_R8G8B8A8_SNORM;
	case SK_PIXEL_FORMAT_RGBA8_UINT:
		return VK_FORMAT_R8G8B8A8_UINT;
	case SK_PIXEL_FORMAT_RGBA8_SINT:
		return VK_FORMAT_R8G8B8A8_SINT;
	case SK_PIXEL_FORMAT_RGBA8_SRGB:
		return VK_FORMAT_R8G8B8A8_SRGB;
	case SK_PIXEL_FORMAT_BGRA8_UNORM:
		return VK_FORMAT_B8G8R8A8_UNORM;
	case SK_PIXEL_FORMAT_BGRA8_SNORM:
		return VK_FORMAT_B8G8R8A8_SNORM;
	case SK_PIXEL_FORMAT_BGRA8_UINT:
		return VK_FORMAT_B8G8R8A8_UINT;
	case SK_PIXEL_FORMAT_BGRA8_SINT:
		return VK_FORMAT_B8G8R8A8_SINT;
	case SK_PIXEL_FORMAT_BGRA8_SRGB:
		return VK_FORMAT_B8G8R8A8_SRGB;
	case SK_PIXEL_FORMAT_RGB10A2_UNORM:
		return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	case SK_PIXEL_FORMAT_RGB10A2_UINT:
		return VK_FORMAT_A2B10G10R10_UINT_PACK32;
	case SK_PIXEL_FORMAT_RG11B10_FLOAT:
		return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	case SK_PIXEL_FORMAT_RGB9E5_FLOAT:
		return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;

	case SK_PIXEL_FORMAT_RG32_UINT:
		return VK_FORMAT_R32G32_UINT;
	case SK_PIXEL_FORMAT_RG32_SINT:
		return VK_FORMAT_R32G32_SINT;
	case SK_PIXEL_FORMAT_RG32_FLOAT:
		return VK_FORMAT_R32G32_SFLOAT;
	case SK_PIXEL_FORMAT_RGBA16_UNORM:
		return VK_FORMAT_R16G16B16A16_UNORM;
	case SK_PIXEL_FORMAT_RGBA16_SNORM:
		return VK_FORMAT_R16G16B16A16_SNORM;
	case SK_PIXEL_FORMAT_RGBA16_UINT:
		return VK_FORMAT_R16G16B16A16_UINT;
	case SK_PIXEL_FORMAT_RGBA16_SINT:
		return VK_FORMAT_R16G16B16A16_SINT;
	case SK_PIXEL_FORMAT_RGBA16_FLOAT:
		return VK_FORMAT_R16G16B16A16_SFLOAT;

	case SK_PIXEL_FORMAT_RGB32_UINT:
		return VK_FORMAT_R32G32B32_UINT;
	case SK_PIXEL_FORMAT_RGB32_SINT:
		return VK_FORMAT_R32G32B32_SINT;
	case SK_PIXEL_FORMAT_RGB32_FLOAT:
		return VK_FORMAT_R32G32B32_SFLOAT;

	case SK_PIXEL_FORMAT_RGBA32_UINT:
		return VK_FORMAT_R32G32B32A32_UINT;
	case SK_PIXEL_FORMAT_RGBA32_SINT:
		return VK_FORMAT_R32G32B32A32_SINT;
	case SK_PIXEL_FORMAT_RGBA32_FLOAT:
		return VK_FORMAT_R32G32B32A32_SFLOAT;

	case SK_PIXEL_FORMAT_D16_UNORM:
		return VK_FORMAT_D16_UNORM;
	case SK_PIXEL_FORMAT_D24_UNORM_S8_UINT:
		return VK_FORMAT_D24_UNORM_S8_UINT;
	case SK_PIXEL_FORMAT_D32_FLOAT:
		return VK_FORMAT_D32_SFLOAT;
	case SK_PIXEL_FORMAT_D32_FLOAT_S8_UINT:
		return VK_FORMAT_D32_SFLOAT_S8_UINT;

	case SK_PIXEL_FORMAT_BC1_UNORM:
		return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_BC1_SRGB:
		return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_BC2_UNORM:
		return VK_FORMAT_BC2_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_BC2_SRGB:
		return VK_FORMAT_BC2_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_BC3_UNORM:
		return VK_FORMAT_BC3_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_BC3_SRGB:
		return VK_FORMAT_BC3_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_BC4_UNORM:
		return VK_FORMAT_BC4_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_BC4_SNORM:
		return VK_FORMAT_BC4_SNORM_BLOCK;
	case SK_PIXEL_FORMAT_BC5_UNORM:
		return VK_FORMAT_BC5_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_BC5_SNORM:
		return VK_FORMAT_BC5_SNORM_BLOCK;
	case SK_PIXEL_FORMAT_BC6H_UF16:
		return VK_FORMAT_BC6H_UFLOAT_BLOCK;
	case SK_PIXEL_FORMAT_BC6H_SF16:
		return VK_FORMAT_BC6H_SFLOAT_BLOCK;
	case SK_PIXEL_FORMAT_BC7_UNORM:
		return VK_FORMAT_BC7_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_BC7_SRGB:
		return VK_FORMAT_BC7_SRGB_BLOCK;

	case SK_PIXEL_FORMAT_ETC1_UNORM:
	case SK_PIXEL_FORMAT_ETC2_UNORM:
		return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ETC2_SRGB:
		return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ETC2A_UNORM:
		return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ETC2A_SRGB:
		return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;

	case SK_PIXEL_FORMAT_ASTC_4x4_UNORM:
		return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_4x4_SRGB:
		return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_5x4_UNORM:
		return VK_FORMAT_ASTC_5x4_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_5x4_SRGB:
		return VK_FORMAT_ASTC_5x4_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_5x5_UNORM:
		return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_5x5_SRGB:
		return VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_6x5_UNORM:
		return VK_FORMAT_ASTC_6x5_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_6x5_SRGB:
		return VK_FORMAT_ASTC_6x5_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_6x6_UNORM:
		return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_6x6_SRGB:
		return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_8x5_UNORM:
		return VK_FORMAT_ASTC_8x5_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_8x5_SRGB:
		return VK_FORMAT_ASTC_8x5_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_8x6_UNORM:
		return VK_FORMAT_ASTC_8x6_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_8x6_SRGB:
		return VK_FORMAT_ASTC_8x6_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_8x8_UNORM:
		return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_8x8_SRGB:
		return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_10x5_UNORM:
		return VK_FORMAT_ASTC_10x5_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_10x5_SRGB:
		return VK_FORMAT_ASTC_10x5_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_10x6_UNORM:
		return VK_FORMAT_ASTC_10x6_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_10x6_SRGB:
		return VK_FORMAT_ASTC_10x6_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_10x8_UNORM:
		return VK_FORMAT_ASTC_10x8_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_10x8_SRGB:
		return VK_FORMAT_ASTC_10x8_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_10x10_UNORM:
		return VK_FORMAT_ASTC_10x10_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_10x10_SRGB:
		return VK_FORMAT_ASTC_10x10_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_12x10_UNORM:
		return VK_FORMAT_ASTC_12x10_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_12x10_SRGB:
		return VK_FORMAT_ASTC_12x10_SRGB_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_12x12_UNORM:
		return VK_FORMAT_ASTC_12x12_UNORM_BLOCK;
	case SK_PIXEL_FORMAT_ASTC_12x12_SRGB:
		return VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
	}
	return VK_FORMAT_UNDEFINED;
}

sk_pixel_format_t sk_vk_to_format(VkFormat format) {
	switch ((u32)format) {
	case VK_FORMAT_UNDEFINED:
		return SK_PIXEL_FORMAT_UNKNOWN;

	case VK_FORMAT_R8_UNORM:
		return SK_PIXEL_FORMAT_R8_UNORM;
	case VK_FORMAT_R8_SNORM:
		return SK_PIXEL_FORMAT_R8_SNORM;
	case VK_FORMAT_R8_UINT:
		return SK_PIXEL_FORMAT_R8_UINT;
	case VK_FORMAT_R8_SINT:
		return SK_PIXEL_FORMAT_R8_SINT;
	case VK_FORMAT_R8_SRGB:
		return SK_PIXEL_FORMAT_R8_SRGB;

	case VK_FORMAT_R16_UNORM:
		return SK_PIXEL_FORMAT_R16_UNORM;
	case VK_FORMAT_R16_SNORM:
		return SK_PIXEL_FORMAT_R16_SNORM;
	case VK_FORMAT_R16_UINT:
		return SK_PIXEL_FORMAT_R16_UINT;
	case VK_FORMAT_R16_SINT:
		return SK_PIXEL_FORMAT_R16_SINT;
	case VK_FORMAT_R16_SFLOAT:
		return SK_PIXEL_FORMAT_R16_FLOAT;
	case VK_FORMAT_R8G8_UNORM:
		return SK_PIXEL_FORMAT_RG8_UNORM;
	case VK_FORMAT_R8G8_SNORM:
		return SK_PIXEL_FORMAT_RG8_SNORM;
	case VK_FORMAT_R8G8_UINT:
		return SK_PIXEL_FORMAT_RG8_UINT;
	case VK_FORMAT_R8G8_SINT:
		return SK_PIXEL_FORMAT_RG8_SINT;
	case VK_FORMAT_R8G8_SRGB:
		return SK_PIXEL_FORMAT_RG8_SRGB;
	case VK_FORMAT_R16G16B16_UNORM:
		return SK_PIXEL_FORMAT_RGB16_UNORM;
	case VK_FORMAT_R16G16B16_SNORM:
		return SK_PIXEL_FORMAT_RGB16_SNORM;
	case VK_FORMAT_R16G16B16_UINT:
		return SK_PIXEL_FORMAT_RGB16_UINT;
	case VK_FORMAT_R16G16B16_SINT:
		return SK_PIXEL_FORMAT_RGB16_SINT;
	case VK_FORMAT_R16G16B16_SFLOAT:
		return SK_PIXEL_FORMAT_RGB16_FLOAT;

	case VK_FORMAT_R32_UINT:
		return SK_PIXEL_FORMAT_R32_UINT;
	case VK_FORMAT_R32_SINT:
		return SK_PIXEL_FORMAT_R32_SINT;
	case VK_FORMAT_R32_SFLOAT:
		return SK_PIXEL_FORMAT_R32_FLOAT;
	case VK_FORMAT_R16G16_UNORM:
		return SK_PIXEL_FORMAT_RG16_UNORM;
	case VK_FORMAT_R16G16_SNORM:
		return SK_PIXEL_FORMAT_RG16_SNORM;
	case VK_FORMAT_R16G16_UINT:
		return SK_PIXEL_FORMAT_RG16_UINT;
	case VK_FORMAT_R16G16_SINT:
		return SK_PIXEL_FORMAT_RG16_SINT;
	case VK_FORMAT_R16G16_SFLOAT:
		return SK_PIXEL_FORMAT_RG16_FLOAT;
	case VK_FORMAT_R8G8B8A8_UNORM:
		return SK_PIXEL_FORMAT_RGBA8_UNORM;
	case VK_FORMAT_R8G8B8A8_SNORM:
		return SK_PIXEL_FORMAT_RGBA8_SNORM;
	case VK_FORMAT_R8G8B8A8_UINT:
		return SK_PIXEL_FORMAT_RGBA8_UINT;
	case VK_FORMAT_R8G8B8A8_SINT:
		return SK_PIXEL_FORMAT_RGBA8_SINT;
	case VK_FORMAT_R8G8B8A8_SRGB:
		return SK_PIXEL_FORMAT_RGBA8_SRGB;
	case VK_FORMAT_B8G8R8A8_UNORM:
		return SK_PIXEL_FORMAT_BGRA8_UNORM;
	case VK_FORMAT_B8G8R8A8_SNORM:
		return SK_PIXEL_FORMAT_BGRA8_SNORM;
	case VK_FORMAT_B8G8R8A8_UINT:
		return SK_PIXEL_FORMAT_BGRA8_UINT;
	case VK_FORMAT_B8G8R8A8_SINT:
		return SK_PIXEL_FORMAT_BGRA8_SINT;
	case VK_FORMAT_B8G8R8A8_SRGB:
		return SK_PIXEL_FORMAT_BGRA8_SRGB;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return SK_PIXEL_FORMAT_RGB10A2_UNORM;
	case VK_FORMAT_A2B10G10R10_UINT_PACK32:
		return SK_PIXEL_FORMAT_RGB10A2_UINT;
	case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
		return SK_PIXEL_FORMAT_RG11B10_FLOAT;
	case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
		return SK_PIXEL_FORMAT_RGB9E5_FLOAT;

	case VK_FORMAT_R32G32_UINT:
		return SK_PIXEL_FORMAT_RG32_UINT;
	case VK_FORMAT_R32G32_SINT:
		return SK_PIXEL_FORMAT_RG32_SINT;
	case VK_FORMAT_R32G32_SFLOAT:
		return SK_PIXEL_FORMAT_RG32_FLOAT;
	case VK_FORMAT_R16G16B16A16_UNORM:
		return SK_PIXEL_FORMAT_RGBA16_UNORM;
	case VK_FORMAT_R16G16B16A16_SNORM:
		return SK_PIXEL_FORMAT_RGBA16_SNORM;
	case VK_FORMAT_R16G16B16A16_UINT:
		return SK_PIXEL_FORMAT_RGBA16_UINT;
	case VK_FORMAT_R16G16B16A16_SINT:
		return SK_PIXEL_FORMAT_RGBA16_SINT;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return SK_PIXEL_FORMAT_RGBA16_FLOAT;

	case VK_FORMAT_R32G32B32_UINT:
		return SK_PIXEL_FORMAT_RGB32_UINT;
	case VK_FORMAT_R32G32B32_SINT:
		return SK_PIXEL_FORMAT_RGB32_SINT;
	case VK_FORMAT_R32G32B32_SFLOAT:
		return SK_PIXEL_FORMAT_RGB32_FLOAT;

	case VK_FORMAT_R32G32B32A32_UINT:
		return SK_PIXEL_FORMAT_RGBA32_UINT;
	case VK_FORMAT_R32G32B32A32_SINT:
		return SK_PIXEL_FORMAT_RGBA32_SINT;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return SK_PIXEL_FORMAT_RGBA32_FLOAT;

	case VK_FORMAT_D16_UNORM:
		return SK_PIXEL_FORMAT_D16_UNORM;
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return SK_PIXEL_FORMAT_D24_UNORM_S8_UINT;
	case VK_FORMAT_D32_SFLOAT:
		return SK_PIXEL_FORMAT_D32_FLOAT;
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return SK_PIXEL_FORMAT_D32_FLOAT_S8_UINT;

	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_BC1_UNORM;
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_BC1_SRGB;
	case VK_FORMAT_BC2_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_BC2_UNORM;
	case VK_FORMAT_BC2_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_BC2_SRGB;
	case VK_FORMAT_BC3_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_BC3_UNORM;
	case VK_FORMAT_BC3_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_BC3_SRGB;
	case VK_FORMAT_BC4_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_BC4_UNORM;
	case VK_FORMAT_BC4_SNORM_BLOCK:
		return SK_PIXEL_FORMAT_BC4_SNORM;
	case VK_FORMAT_BC5_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_BC5_UNORM;
	case VK_FORMAT_BC5_SNORM_BLOCK:
		return SK_PIXEL_FORMAT_BC5_SNORM;
	case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		return SK_PIXEL_FORMAT_BC6H_UF16;
	case VK_FORMAT_BC6H_SFLOAT_BLOCK:
		return SK_PIXEL_FORMAT_BC6H_SF16;
	case VK_FORMAT_BC7_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_BC7_UNORM;
	case VK_FORMAT_BC7_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_BC7_SRGB;

	case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ETC2_UNORM;
	case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ETC2_SRGB;
	case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ETC2A_UNORM;
	case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ETC2A_SRGB;

	case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_4x4_UNORM;
	case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_4x4_SRGB;
	case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_5x4_UNORM;
	case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_5x4_SRGB;
	case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_5x5_UNORM;
	case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_5x5_SRGB;
	case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_6x5_UNORM;
	case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_6x5_SRGB;
	case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_6x6_UNORM;
	case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_6x6_SRGB;
	case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_8x5_UNORM;
	case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_8x5_SRGB;
	case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_8x6_UNORM;
	case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_8x6_SRGB;
	case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_8x8_UNORM;
	case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_8x8_SRGB;
	case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_10x5_UNORM;
	case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_10x5_SRGB;
	case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_10x6_UNORM;
	case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_10x6_SRGB;
	case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_10x8_UNORM;
	case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_10x8_SRGB;
	case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_10x10_UNORM;
	case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_10x10_SRGB;
	case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_12x10_UNORM;
	case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_12x10_SRGB;
	case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_12x12_UNORM;
	case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
		return SK_PIXEL_FORMAT_ASTC_12x12_SRGB;
	default:
		return SK_PIXEL_FORMAT_UNKNOWN;
	}
	return SK_PIXEL_FORMAT_UNKNOWN;
}

VkDeviceAddress sk_vk_get_buffer_device_address(VkDevice device, VkBuffer buffer) {
	VkBufferDeviceAddressInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
	info.buffer = buffer;
	return vkGetBufferDeviceAddress(device, &info);
}

VkBuildAccelerationStructureFlagsKHR sk_vk_convert_build_as_flags(u32 flags) {
	VkBuildAccelerationStructureFlagsKHR result = 0;

	if ((flags & (u32)SK_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE) != 0u)
		result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	if ((flags & (u32)SK_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION) != 0u)
		result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
	if ((flags & (u32)SK_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE) != 0u)
		result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	if ((flags & (u32)SK_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD) != 0u)
		result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	if ((flags & (u32)SK_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY) != 0u)
		result |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR;

	return result;
}

VkAccessFlags sk_vk_get_access_flags_from_state(sk_resource_state_t state) {
	switch (state) {
	case SK_RESOURCE_STATE_UNDEFINED:
		return 0;
	case SK_RESOURCE_STATE_GENERAL:
		return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	case SK_RESOURCE_STATE_COLOR_ATTACHMENT:
		return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	case SK_RESOURCE_STATE_DEPTH_STENCIL_ATTACH:
		return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	case SK_RESOURCE_STATE_DEPTH_STENCIL_READ:
		return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
	case SK_RESOURCE_STATE_SHADER_READ:
		return VK_ACCESS_SHADER_READ_BIT;
	case SK_RESOURCE_STATE_COPY_DEST:
		return VK_ACCESS_TRANSFER_WRITE_BIT;
	case SK_RESOURCE_STATE_COPY_SOURCE:
		return VK_ACCESS_TRANSFER_READ_BIT;
	case SK_RESOURCE_STATE_INDIRECT_ARGUMENT:
		return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	case SK_RESOURCE_STATE_PRESENT:
		return 0;
	case SK_RESOURCE_STATE_UNORDERED_ACCESS:
		return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	case SK_RESOURCE_STATE_ACCELERATION_STRUCTURE:
		return VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	}
	return 0;
}

VkPipelineStageFlags sk_vk_get_pipeline_stage_from_state(sk_resource_state_t state) {
	switch (state) {
	case SK_RESOURCE_STATE_UNDEFINED:
		return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	case SK_RESOURCE_STATE_GENERAL:
		return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	case SK_RESOURCE_STATE_COLOR_ATTACHMENT:
		return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	case SK_RESOURCE_STATE_DEPTH_STENCIL_ATTACH:
		return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	case SK_RESOURCE_STATE_DEPTH_STENCIL_READ:
		return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
	case SK_RESOURCE_STATE_SHADER_READ:
		return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
	case SK_RESOURCE_STATE_COPY_DEST:
	case SK_RESOURCE_STATE_COPY_SOURCE:
		return VK_PIPELINE_STAGE_TRANSFER_BIT;
	case SK_RESOURCE_STATE_INDIRECT_ARGUMENT:
		return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
	case SK_RESOURCE_STATE_PRESENT:
		return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	case SK_RESOURCE_STATE_UNORDERED_ACCESS:
		return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	case SK_RESOURCE_STATE_ACCELERATION_STRUCTURE:
		return VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	}
	return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
}

/* ------------------------------------------------------------------ */
/* Object naming                                                       */
/* ------------------------------------------------------------------ */

void sk_vk_set_object_name(sk_vk_device_t* device, VkObjectType type, u64 handle, const_chr_t name) {
	if (device == NULL || !device->debug_utils_extension_present || name == NULL || name[0] == '\0' || device->device == VK_NULL_HANDLE) {
		return;
	}

	VkDebugUtilsObjectNameInfoEXT name_info = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
	name_info.objectType = type;
	name_info.objectHandle = handle;
	name_info.pObjectName = name;
	vkSetDebugUtilsObjectNameEXT(device->device, &name_info);
}

/* ------------------------------------------------------------------ */
/* Descriptor set layout / pipeline layout                             */
/* ------------------------------------------------------------------ */

static VkShaderStageFlags binding_stage_flags(u32 stages) {
	if (stages == 0u) {
		return VK_SHADER_STAGE_ALL;
	}
	return sk_vk_convert_shader_stage_flags(stages);
}

VkResult sk_vk_create_descriptor_set_layout(sk_vk_device_t* device, const sk_descriptor_set_layout_binding_t* bindings, u32 binding_count, bool push_descriptor,
											VkDescriptorSetLayout* out_layout, bool* out_has_runtime_array) {
	VkDescriptorSetLayoutBinding* vk_bindings = (VkDescriptorSetLayoutBinding*)device->allocator->alloc(device->allocator->instance,
																										(size_t)binding_count * sizeof(VkDescriptorSetLayoutBinding));
	VkDescriptorBindingFlags* bindless_flags = (VkDescriptorBindingFlags*)device->allocator->alloc(device->allocator->instance,
																								   (size_t)binding_count * sizeof(VkDescriptorBindingFlags));
	if (vk_bindings == NULL || bindless_flags == NULL) {
		device->allocator->free(device->allocator->instance, vk_bindings);
		device->allocator->free(device->allocator->instance, bindless_flags);
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	bool has_runtime_array = false;
	u32 binding_index = 0u;
	for (u32 i = 0u; i < binding_count; ++i) {
		const sk_descriptor_set_layout_binding_t* binding = &bindings[i];
		if (binding->descriptor_type == SK_DESCRIPTOR_TYPE_NONE) {
			continue;
		}

		u32 descriptor_count = binding->descriptor_count;
		if (binding->render_type == SK_RENDER_TYPE_RUNTIME_ARRAY) {
			descriptor_count = SK_VK_MAX_BINDLESS_RESOURCES;
			has_runtime_array = true;
		}

		vk_bindings[binding_index].binding = binding->binding;
		vk_bindings[binding_index].descriptorType = sk_vk_convert_descriptor_type(binding->descriptor_type);
		vk_bindings[binding_index].descriptorCount = descriptor_count;
		vk_bindings[binding_index].stageFlags = binding_stage_flags(binding->shader_stages);
		vk_bindings[binding_index].pImmutableSamplers = NULL;
		bindless_flags[binding_index] = 0;
		++binding_index;
	}

	if (has_runtime_array) {
		/* Apply UPDATE_AFTER_BIND to every binding in the set, not just runtime arrays. */
		for (u32 i = 0u, fi = 0u; i < binding_count; ++i) {
			if (bindings[i].descriptor_type == SK_DESCRIPTOR_TYPE_NONE) {
				continue;
			}
			if (bindings[i].render_type == SK_RENDER_TYPE_RUNTIME_ARRAY) {
				bindless_flags[fi] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
			} else {
				bindless_flags[fi] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
			}
			++fi;
		}
	}

	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT};
	VkDescriptorSetLayoutCreateInfo set_layout_create_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	set_layout_create_info.bindingCount = binding_index;
	set_layout_create_info.pBindings = vk_bindings;

	if (has_runtime_array) {
		extended_info.bindingCount = binding_index;
		extended_info.pBindingFlags = bindless_flags;
		set_layout_create_info.pNext = &extended_info;
		set_layout_create_info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
	}

	if (push_descriptor) {
		set_layout_create_info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	}

	VkResult result = vkCreateDescriptorSetLayout(device->device, &set_layout_create_info, NULL, out_layout);

	device->allocator->free(device->allocator->instance, vk_bindings);
	device->allocator->free(device->allocator->instance, bindless_flags);

	if (has_runtime_array && out_has_runtime_array != NULL) {
		*out_has_runtime_array = true;
	}
	return result;
}

static void grow_set_layouts(sk_vk_device_t* device, u32 set_index, VkDescriptorSetLayout** set_layouts, u32* set_layout_count) {
	while (*set_layout_count <= set_index) {
		VkDescriptorSetLayoutCreateInfo empty_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		VkDescriptorSetLayout empty_layout = VK_NULL_HANDLE;
		vkCreateDescriptorSetLayout(device->device, &empty_info, NULL, &empty_layout);
		VkDescriptorSetLayout* grown = (VkDescriptorSetLayout*)device->allocator->realloc(device->allocator->instance, *set_layouts,
																						  (size_t)(*set_layout_count + 1u) * sizeof(VkDescriptorSetLayout));
		if (grown == NULL) {
			return;
		}
		*set_layouts = grown;
		(*set_layouts)[*set_layout_count] = empty_layout;
		++(*set_layout_count);
	}
}

static void destroy_set_layouts(sk_vk_device_t* device, VkDescriptorSetLayout* layouts, u32 layout_count) {
	for (u32 i = 0u; i < layout_count; ++i) {
		vkDestroyDescriptorSetLayout(device->device, layouts[i], NULL);
	}
	device->allocator->free(device->allocator->instance, layouts);
}

VkResult sk_vk_create_pipeline_layout(sk_vk_device_t* device, const sk_pipeline_desc_t* pipeline_desc, bool push_descriptor, const sk_descriptor_set_override_t* overrides,
									  u32 override_count, VkPipelineLayout* out_layout, VkDescriptorSetLayout** out_set_layouts, u32* out_set_layout_count) {
	VkDescriptorSetLayout* set_layouts = NULL;
	u32 set_layout_count = 0u;

	VkPushConstantRange* push_constant_ranges = (VkPushConstantRange*)device->allocator->alloc(device->allocator->instance,
																							   (size_t)pipeline_desc->push_constant_count * sizeof(VkPushConstantRange));
	if (pipeline_desc->push_constant_count > 0u && push_constant_ranges == NULL) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	for (u32 i = 0u; i < pipeline_desc->push_constant_count; ++i) {
		const sk_push_constant_range_t* range = &pipeline_desc->push_constants[i];
		push_constant_ranges[i].stageFlags = binding_stage_flags(range->shader_stages);
		push_constant_ranges[i].offset = range->offset;
		push_constant_ranges[i].size = range->size;
	}

	for (u32 i = 0u; i < pipeline_desc->descriptor_count; ++i) {
		const sk_descriptor_set_layout_t* set_layout = &pipeline_desc->descriptors[i];
		u32 set_index = set_layout->set;

		bool skip = false;
		for (u32 o = 0u; o < override_count; ++o) {
			if (overrides[o].set_index == set_index) {
				skip = true;
				break;
			}
		}
		if (skip) {
			continue;
		}

		grow_set_layouts(device, set_index, &set_layouts, &set_layout_count);
		if (set_layout_count <= set_index) {
			destroy_set_layouts(device, set_layouts, set_layout_count);
			device->allocator->free(device->allocator->instance, push_constant_ranges);
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		vkDestroyDescriptorSetLayout(device->device, set_layouts[set_index], NULL);
		VkResult result = sk_vk_create_descriptor_set_layout(device, set_layout->bindings, set_layout->binding_count, push_descriptor, &set_layouts[set_index], NULL);
		if (result != VK_SUCCESS) {
			destroy_set_layouts(device, set_layouts, set_layout_count);
			device->allocator->free(device->allocator->instance, push_constant_ranges);
			return result;
		}
	}

	for (u32 o = 0u; o < override_count; ++o) {
		sk_vk_descriptor_set_t* descriptor_set = (sk_vk_descriptor_set_t*)sk_descriptor_set_t_to_ptr(overrides[o].descriptor_set);
		if (descriptor_set == NULL) {
			continue;
		}

		grow_set_layouts(device, overrides[o].set_index, &set_layouts, &set_layout_count);
		if (set_layout_count <= overrides[o].set_index) {
			destroy_set_layouts(device, set_layouts, set_layout_count);
			device->allocator->free(device->allocator->instance, push_constant_ranges);
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		vkDestroyDescriptorSetLayout(device->device, set_layouts[overrides[o].set_index], NULL);
		VkResult result = sk_vk_create_descriptor_set_layout(device, descriptor_set->desc.bindings, descriptor_set->desc.binding_count, false, &set_layouts[overrides[o].set_index],
															 NULL);
		if (result != VK_SUCCESS) {
			destroy_set_layouts(device, set_layouts, set_layout_count);
			device->allocator->free(device->allocator->instance, push_constant_ranges);
			return result;
		}
	}

	VkPipelineLayoutCreateInfo layout_create_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layout_create_info.setLayoutCount = set_layout_count;
	layout_create_info.pSetLayouts = set_layouts;
	layout_create_info.pPushConstantRanges = push_constant_ranges;
	layout_create_info.pushConstantRangeCount = pipeline_desc->push_constant_count;

	VkResult result = vkCreatePipelineLayout(device->device, &layout_create_info, NULL, out_layout);

	device->allocator->free(device->allocator->instance, push_constant_ranges);
	if (result != VK_SUCCESS) {
		destroy_set_layouts(device, set_layouts, set_layout_count);
		return result;
	}

	*out_set_layouts = set_layouts;
	*out_set_layout_count = set_layout_count;
	return VK_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Swapchain support                                                   */
/* ------------------------------------------------------------------ */

void sk_vk_query_swapchain_support(VkPhysicalDevice physical, VkSurfaceKHR surface, sk_vk_swapchain_support_t* out, const sk_allocator_t* allocator) {
	memset(out, 0, sizeof(*out));
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &out->capabilities);

	u32 format_count = 0u;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, NULL);
	if (format_count > 0u) {
		out->formats = (VkSurfaceFormatKHR*)allocator->alloc(allocator->instance, (size_t)format_count * sizeof(VkSurfaceFormatKHR));
		if (out->formats != NULL) {
			out->format_count = format_count;
			vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, out->formats);
		}
	}

	u32 present_mode_count = 0u;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &present_mode_count, NULL);
	if (present_mode_count > 0u) {
		out->present_modes = (VkPresentModeKHR*)allocator->alloc(allocator->instance, (size_t)present_mode_count * sizeof(VkPresentModeKHR));
		if (out->present_modes != NULL) {
			out->present_mode_count = present_mode_count;
			vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &present_mode_count, out->present_modes);
		}
	}
}

void sk_vk_destroy_swapchain_support(sk_vk_swapchain_support_t* support, const sk_allocator_t* allocator) {
	allocator->free(allocator->instance, support->formats);
	allocator->free(allocator->instance, support->present_modes);
	memset(support, 0, sizeof(*support));
}

VkSurfaceFormatKHR sk_vk_choose_surface_format(const sk_vk_swapchain_support_t* support, VkSurfaceFormatKHR desired) {
	for (u32 i = 0u; i < support->format_count; ++i) {
		if (support->formats[i].format == desired.format && support->formats[i].colorSpace == desired.colorSpace) {
			return support->formats[i];
		}
	}
	if (support->format_count > 0u) {
		return support->formats[0];
	}
	return desired;
}

VkPresentModeKHR sk_vk_choose_present_mode(const sk_vk_swapchain_support_t* support, VkPresentModeKHR desired) {
	for (u32 i = 0u; i < support->present_mode_count; ++i) {
		if (support->present_modes[i] == desired) {
			return support->present_modes[i];
		}
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

static u32 clamp_u32(u32 value, u32 min_value, u32 max_value) {
	if (value < min_value) {
		return min_value;
	}
	if (value > max_value) {
		return max_value;
	}
	return value;
}

VkExtent2D sk_vk_choose_extent(const sk_vk_swapchain_support_t* support, u32 width, u32 height) {
	if (support->capabilities.currentExtent.width != UINT32_MAX) {
		return support->capabilities.currentExtent;
	}

	VkExtent2D actual_extent = {width, height};
	actual_extent.width = clamp_u32(actual_extent.width, support->capabilities.minImageExtent.width, support->capabilities.maxImageExtent.width);
	actual_extent.height = clamp_u32(actual_extent.height, support->capabilities.minImageExtent.height, support->capabilities.maxImageExtent.height);
	return actual_extent;
}

/* ------------------------------------------------------------------ */
/* Instance extensions / platform surface (SDL parity)                 */
/* ------------------------------------------------------------------ */

bool sk_vk_platform_get_required_instance_extensions(const_chr_t* names, u32 max_names, u32* out_count) {
#if defined(_WIN32)
	static const_chr_t required[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
#elif defined(__APPLE__)
	static const_chr_t required[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
#else
	static const_chr_t required[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
#endif
	const u32 required_count = (u32)(sizeof(required) / sizeof(required[0]));

	if (names != NULL) {
		u32 copy_count = required_count;
		if (copy_count > max_names) {
			copy_count = max_names;
		}
		for (u32 i = 0u; i < copy_count; ++i) {
			names[i] = required[i];
		}
	}
	if (out_count != NULL) {
		*out_count = required_count;
	}
	return true;
}

bool sk_vk_platform_get_presentation_support(VkInstance instance, VkPhysicalDevice physical, u32 family_index) {
	(void)instance;
	(void)physical;
	(void)family_index;
#if defined(_WIN32)
	PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR presentation_support = SK_PTR_TO_FN(PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR,
																						   vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR"));
	if (presentation_support == NULL) {
		return false;
	}
	return presentation_support(physical, family_index) == VK_TRUE;
#elif defined(__APPLE__)
	(void)instance;
	(void)physical;
	(void)family_index;
	return true;
#else
	PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR presentation_support = SK_PTR_TO_FN(PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR,
																						  vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceXlibPresentationSupportKHR"));
	if (presentation_support == NULL) {
		return false;
	}
	Display* display = XOpenDisplay(NULL);
	if (display == NULL) {
		/* Headless / no X server: assume the graphics family can present so
		 * adapter enumeration still yields a usable device. The real check
		 * happens with the actual surface at swapchain creation. */
		return true;
	}
	VkBool32 supported = presentation_support(physical, family_index, display, 0);
	XCloseDisplay(display);
	return supported == VK_TRUE;
#endif
}

bool sk_vk_platform_create_surface(sk_vk_device_t* device, void_ptr_t window, VkSurfaceKHR* out_surface, void_ptr_t* out_display) {
	if (out_display != NULL) {
		*out_display = NULL;
	}
#if defined(_WIN32)
	PFN_vkCreateWin32SurfaceKHR create_surface = SK_PTR_TO_FN(PFN_vkCreateWin32SurfaceKHR, vkGetInstanceProcAddr(device->instance, "vkCreateWin32SurfaceKHR"));
	if (create_surface == NULL) {
		return false;
	}
	VkWin32SurfaceCreateInfoKHR create_info = {.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
	create_info.hinstance = GetModuleHandle(NULL);
	create_info.hwnd = (HWND)(uintptr_t)window;
	return create_surface(device->instance, &create_info, NULL, out_surface) == VK_SUCCESS;
#elif defined(__APPLE__)
	PFN_vkCreateMetalSurfaceEXT create_surface = SK_PTR_TO_FN(PFN_vkCreateMetalSurfaceEXT, vkGetInstanceProcAddr(device->instance, "vkCreateMetalSurfaceEXT"));
	if (create_surface == NULL) {
		return false;
	}
	void* view = sk_vk_apple_view_from_window(window);
	if (view == NULL) {
		return false;
	}
	VkMetalSurfaceCreateInfoEXT create_info = {.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
	create_info.pLayer = view;
	return create_surface(device->instance, &create_info, NULL, out_surface) == VK_SUCCESS;
#else
	PFN_vkCreateXlibSurfaceKHR create_surface = SK_PTR_TO_FN(PFN_vkCreateXlibSurfaceKHR, vkGetInstanceProcAddr(device->instance, "vkCreateXlibSurfaceKHR"));
	if (create_surface == NULL) {
		return false;
	}
	Display* display = XOpenDisplay(NULL);
	if (display == NULL) {
		return false;
	}
	Window x11_window = SK_CONST_CAST(Window, window);
	VkXlibSurfaceCreateInfoKHR create_info = {.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
	create_info.dpy = display;
	create_info.window = x11_window;
	VkResult result = create_surface(device->instance, &create_info, NULL, out_surface);
	if (result != VK_SUCCESS) {
		XCloseDisplay(display);
		return false;
	}
	/* The surface keeps the Display alive internally; the caller must hold the
	 * connection open (and close it) for the surface lifetime. */
	if (out_display != NULL) {
		*out_display = (void_ptr_t)display;
	}
	return true;
#endif
}

void sk_vk_platform_destroy_surface_handle(void_ptr_t display) {
#if !defined(_WIN32) && !defined(__APPLE__)
	if (display != NULL) {
		XCloseDisplay((Display*)display);
	}
#else
	(void)display;
#endif
}

/* ------------------------------------------------------------------ */
/* Extension / layer queries                                           */
/* ------------------------------------------------------------------ */

bool sk_vk_query_layer_properties(const_chr_t* required_layers, u32 layer_count) {
	u32 count = 0u;
	vkEnumerateInstanceLayerProperties(&count, NULL);
	VkLayerProperties* layers = (VkLayerProperties*)sk_allocator_default()->alloc(NULL, (size_t)count * sizeof(VkLayerProperties));
	if (count > 0u && layers == NULL) {
		return false;
	}
	if (count > 0u) {
		vkEnumerateInstanceLayerProperties(&count, layers);
	}

	for (u32 i = 0u; i < layer_count; ++i) {
		bool found = false;
		for (u32 j = 0u; j < count; ++j) {
			if (strcmp(layers[j].layerName, required_layers[i]) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			sk_allocator_default()->free(NULL, layers);
			return false;
		}
	}
	sk_allocator_default()->free(NULL, layers);
	return true;
}

bool sk_vk_query_instance_extensions(const_chr_t* required_extensions, u32 extension_count) {
	u32 count = 0u;
	vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
	VkExtensionProperties* extensions = (VkExtensionProperties*)sk_allocator_default()->alloc(NULL, (size_t)count * sizeof(VkExtensionProperties));
	if (count > 0u && extensions == NULL) {
		return false;
	}
	if (count > 0u) {
		vkEnumerateInstanceExtensionProperties(NULL, &count, extensions);
	}

	bool all_found = true;
	for (u32 i = 0u; i < extension_count; ++i) {
		bool found = false;
		for (u32 j = 0u; j < count; ++j) {
			if (strcmp(extensions[j].extensionName, required_extensions[i]) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			all_found = false;
			break;
		}
	}
	sk_allocator_default()->free(NULL, extensions);
	return all_found;
}

bool sk_vk_device_extension_present(VkPhysicalDevice physical, const_chr_t extension_name) {
	u32 count = 0u;
	vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL);
	VkExtensionProperties* extensions = (VkExtensionProperties*)sk_allocator_default()->alloc(NULL, (size_t)count * sizeof(VkExtensionProperties));
	if (count > 0u && extensions == NULL) {
		return false;
	}
	if (count > 0u) {
		vkEnumerateDeviceExtensionProperties(physical, NULL, &count, extensions);
	}

	bool found = false;
	for (u32 i = 0u; i < count; ++i) {
		if (strcmp(extensions[i].extensionName, extension_name) == 0) {
			found = true;
			break;
		}
	}
	sk_allocator_default()->free(NULL, extensions);
	return found;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS
#include "test.h"

SK_TEST(vk_util_format_conversion_roundtrip) {
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_FORMAT_R8G8B8A8_UNORM, (unsigned)sk_vk_to_vk_format(SK_PIXEL_FORMAT_RGBA8_UNORM));
	TEST_ASSERT_EQUAL_INT((int)SK_PIXEL_FORMAT_RGBA8_UNORM, (int)sk_vk_to_format(VK_FORMAT_R8G8B8A8_UNORM));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_FORMAT_B8G8R8A8_UNORM, (unsigned)sk_vk_to_vk_format(SK_PIXEL_FORMAT_BGRA8_UNORM));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_FORMAT_D32_SFLOAT, (unsigned)sk_vk_to_vk_format(SK_PIXEL_FORMAT_D32_FLOAT));
	TEST_ASSERT_EQUAL_INT((int)SK_PIXEL_FORMAT_D32_FLOAT_S8_UINT, (int)sk_vk_to_format(VK_FORMAT_D32_SFLOAT_S8_UINT));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_FORMAT_ASTC_12x12_SRGB_BLOCK, (unsigned)sk_vk_to_vk_format(SK_PIXEL_FORMAT_ASTC_12x12_SRGB));
	TEST_ASSERT_EQUAL_INT((int)SK_PIXEL_FORMAT_UNKNOWN, (int)sk_vk_to_format(VK_FORMAT_MAX_ENUM));
}

SK_TEST(vk_util_depth_format_detection) {
	TEST_ASSERT_TRUE(sk_vk_is_depth_format(SK_PIXEL_FORMAT_D24_UNORM_S8_UINT));
	TEST_ASSERT_TRUE(sk_vk_is_depth_format(SK_PIXEL_FORMAT_D32_FLOAT));
	TEST_ASSERT_FALSE(sk_vk_is_depth_format(SK_PIXEL_FORMAT_RGBA8_UNORM));
	TEST_ASSERT_TRUE(sk_vk_is_depth_format_vk(VK_FORMAT_D32_SFLOAT_S8_UINT));
	TEST_ASSERT_FALSE(sk_vk_is_depth_format_vk(VK_FORMAT_R8G8B8A8_UNORM));
}

SK_TEST(vk_util_descriptor_type_conversion) {
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, (unsigned)sk_vk_convert_descriptor_type(SK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, (unsigned)sk_vk_convert_descriptor_type(SK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (unsigned)sk_vk_convert_descriptor_type(SK_DESCRIPTOR_TYPE_SAMPLED_IMAGE));
}

SK_TEST(vk_util_shader_stage_flags) {
	VkShaderStageFlags flags = sk_vk_convert_shader_stage_flags((u32)SK_SHADER_STAGE_VERTEX | (u32)SK_SHADER_STAGE_PIXEL | (u32)SK_SHADER_STAGE_RAYGEN);
	TEST_ASSERT_TRUE((flags & VK_SHADER_STAGE_VERTEX_BIT) != 0u);
	TEST_ASSERT_TRUE((flags & VK_SHADER_STAGE_FRAGMENT_BIT) != 0u);
	TEST_ASSERT_TRUE((flags & VK_SHADER_STAGE_RAYGEN_BIT_KHR) != 0u);
	TEST_ASSERT_TRUE((flags & VK_SHADER_STAGE_COMPUTE_BIT) == 0u);
}

SK_TEST(vk_util_build_as_flags) {
	VkBuildAccelerationStructureFlagsKHR flags = sk_vk_convert_build_as_flags((u32)SK_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
																			  (u32)SK_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE);
	TEST_ASSERT_TRUE((flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR) != 0u);
	TEST_ASSERT_TRUE((flags & VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR) != 0u);
	TEST_ASSERT_TRUE((flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) == 0u);
}

SK_TEST(vk_util_sample_count_conversion) {
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_SAMPLE_COUNT_1_BIT, (unsigned)sk_vk_cast_sample_count(1u));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_SAMPLE_COUNT_4_BIT, (unsigned)sk_vk_cast_sample_count(4u));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_SAMPLE_COUNT_64_BIT, (unsigned)sk_vk_cast_sample_count(64u));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_SAMPLE_COUNT_1_BIT, (unsigned)sk_vk_cast_sample_count(0u));
}

SK_TEST(vk_util_resource_state_mapping) {
	TEST_ASSERT_TRUE((sk_vk_get_access_flags_from_state(SK_RESOURCE_STATE_COPY_DEST) & VK_ACCESS_TRANSFER_WRITE_BIT) != 0u);
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_PIPELINE_STAGE_TRANSFER_BIT, (unsigned)sk_vk_get_pipeline_stage_from_state(SK_RESOURCE_STATE_COPY_SOURCE));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, (unsigned)sk_vk_cast_state(SK_RESOURCE_STATE_PRESENT, VK_IMAGE_LAYOUT_UNDEFINED));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_IMAGE_LAYOUT_UNDEFINED, (unsigned)sk_vk_cast_state(SK_RESOURCE_STATE_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED));
}

SK_TEST(vk_util_buffer_usage_flags) {
	VkBufferUsageFlags flags = sk_vk_get_buffer_usage_flags((u32)SK_RESOURCE_USAGE_VERTEX_BUFFER | (u32)SK_RESOURCE_USAGE_INDEX_BUFFER | (u32)SK_RESOURCE_USAGE_CONSTANT_BUFFER |
																(u32)SK_RESOURCE_USAGE_COPY_DEST,
															false);
	TEST_ASSERT_TRUE((flags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0u);
	TEST_ASSERT_TRUE((flags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0u);
	TEST_ASSERT_TRUE((flags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0u);
	TEST_ASSERT_TRUE((flags & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0u);
	TEST_ASSERT_TRUE((flags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == 0u);

	VkBufferUsageFlags with_bda = sk_vk_get_buffer_usage_flags((u32)SK_RESOURCE_USAGE_ACCELERATION_STRUCTURE, true);
	TEST_ASSERT_TRUE((with_bda & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR) != 0u);
	TEST_ASSERT_TRUE((with_bda & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u);
}

SK_TEST(vk_util_primitive_topology_conversion) {
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, (unsigned)sk_vk_convert_primitive_topology(SK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_PRIMITIVE_TOPOLOGY_POINT_LIST, (unsigned)sk_vk_convert_primitive_topology(SK_PRIMITIVE_TOPOLOGY_POINT_LIST));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_POLYGON_MODE_LINE, (unsigned)sk_vk_convert_polygon_mode(SK_POLYGON_MODE_LINE));
	TEST_ASSERT_EQUAL_UINT((unsigned)VK_CULL_MODE_FRONT_BIT, (unsigned)sk_vk_convert_cull_mode(SK_CULL_MODE_FRONT));
}
#endif /* SK_TESTS */
