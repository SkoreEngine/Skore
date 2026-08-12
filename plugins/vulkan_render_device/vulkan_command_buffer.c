/*
 * vulkan_command_buffer.c — command recording for the Vulkan render device.
 *
 * Port of main's VulkanCommandBuffer methods (plus the queue-type-aware
 * barrier clamping from VulkanDevice.cpp) onto the C RHI surface. Every
 * recording call is a thin wrapper around the matching vkCmd*; resources are
 * resolved from their typed handles (callers resolved RIDs outside the RHI).
 */

#include "vulkan_render_device_internal.h"
#include "vulkan_utils.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Barrier helper shims (mirror main's VulkanDevice.cpp anonymous ns)  */
/* ------------------------------------------------------------------ */

static VkPipelineStageFlags clamp_stage_for_queue(VkPipelineStageFlags stage, u32 queue_type) {
	if (queue_type == (u32)SK_QUEUE_TYPE_TRANSFER) {
		const VkPipelineStageFlags allowed = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
											 VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkPipelineStageFlags clamped = stage & allowed;
		return clamped != 0u ? clamped : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	if (queue_type == (u32)SK_QUEUE_TYPE_COMPUTE) {
		const VkPipelineStageFlags allowed = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
											 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_HOST_BIT |
											 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkPipelineStageFlags clamped = stage & allowed;
		return clamped != 0u ? clamped : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	return stage;
}

static VkAccessFlags clamp_access_for_queue(VkAccessFlags access, u32 queue_type) {
	if (queue_type == (u32)SK_QUEUE_TYPE_TRANSFER) {
		const VkAccessFlags allowed = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT |
									  VK_ACCESS_MEMORY_WRITE_BIT;
		return access & allowed;
	}
	if (queue_type == (u32)SK_QUEUE_TYPE_COMPUTE) {
		const VkAccessFlags allowed = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
									  VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT |
									  VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		return access & allowed;
	}
	return access;
}

static VkAccessFlags shader_read_write_access_for_scope(u32 scope) {
	switch (scope) {
	case SK_BARRIER_SYNC_TRANSFER:
		return VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	case SK_BARRIER_SYNC_GRAPHICS:
	case SK_BARRIER_SYNC_COMPUTE:
	case SK_BARRIER_SYNC_RAYTRACE:
		return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	case SK_BARRIER_SYNC_AUTOMATIC:
	default:
		break;
	}
	return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
}

static VkPipelineStageFlags shader_stage_for_scope(u32 scope) {
	switch (scope) {
	case SK_BARRIER_SYNC_GRAPHICS:
		return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	case SK_BARRIER_SYNC_COMPUTE:
		return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	case SK_BARRIER_SYNC_RAYTRACE:
		return VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
	case SK_BARRIER_SYNC_TRANSFER:
		return VK_PIPELINE_STAGE_TRANSFER_BIT;
	case SK_BARRIER_SYNC_AUTOMATIC:
	default:
		break;
	}
	return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
}

static VkAccessFlags access_flags_from_state_scope(sk_resource_state_t state, u32 scope) {
	if (scope == (u32)SK_BARRIER_SYNC_AUTOMATIC) {
		return sk_vk_get_access_flags_from_state(state);
	}
	if (state == SK_RESOURCE_STATE_UNDEFINED || state == SK_RESOURCE_STATE_PRESENT) {
		return 0;
	}
	if (state == SK_RESOURCE_STATE_GENERAL) {
		return shader_read_write_access_for_scope(scope);
	}
	return sk_vk_get_access_flags_from_state(state);
}

static VkPipelineStageFlags pipeline_stage_from_state_scope(sk_resource_state_t state, u32 scope) {
	if (scope == (u32)SK_BARRIER_SYNC_AUTOMATIC) {
		return sk_vk_get_pipeline_stage_from_state(state);
	}
	if (state == SK_RESOURCE_STATE_UNDEFINED) {
		return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	if (state == SK_RESOURCE_STATE_PRESENT) {
		return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}
	if (state == SK_RESOURCE_STATE_GENERAL) {
		return shader_stage_for_scope(scope);
	}
	return sk_vk_get_pipeline_stage_from_state(state);
}

static VkAccessFlags buffer_access_flags_from_state_scope(sk_resource_state_t state, u32 scope) {
	VkAccessFlags access = access_flags_from_state_scope(state, scope);
	if (state == SK_RESOURCE_STATE_SHADER_READ) {
		if (scope == (u32)SK_BARRIER_SYNC_AUTOMATIC || scope == (u32)SK_BARRIER_SYNC_GRAPHICS) {
			access |= VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		} else if (scope == (u32)SK_BARRIER_SYNC_COMPUTE || scope == (u32)SK_BARRIER_SYNC_RAYTRACE) {
			access |= VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		}
	}
	return access;
}

static VkPipelineStageFlags buffer_pipeline_stage_from_state_scope(sk_resource_state_t state, u32 scope) {
	VkPipelineStageFlags stage = pipeline_stage_from_state_scope(state, scope);
	if (state == SK_RESOURCE_STATE_SHADER_READ) {
		if (scope == (u32)SK_BARRIER_SYNC_AUTOMATIC || scope == (u32)SK_BARRIER_SYNC_GRAPHICS) {
			stage |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
		} else if (scope == (u32)SK_BARRIER_SYNC_COMPUTE || scope == (u32)SK_BARRIER_SYNC_RAYTRACE) {
			stage |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
		}
	}
	return stage;
}

static VkPipelineBindPoint sk_vkrd_bind_point(sk_pipeline_bind_point_t bind_point) {
	switch (bind_point) {
	case SK_PIPELINE_BIND_POINT_GRAPHICS:
		return VK_PIPELINE_BIND_POINT_GRAPHICS;
	case SK_PIPELINE_BIND_POINT_COMPUTE:
		return VK_PIPELINE_BIND_POINT_COMPUTE;
	case SK_PIPELINE_BIND_POINT_RAY_TRACING:
		return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
	}
	return VK_PIPELINE_BIND_POINT_GRAPHICS;
}

static VkDeviceAddress sk_vkrd_buffer_device_address(sk_vk_device_t* device, sk_buffer_t buffer_handle) {
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buffer_handle);
	if (buffer == NULL) {
		return 0;
	}
	return sk_vk_get_buffer_device_address(device->device, buffer->buffer);
}

/* ------------------------------------------------------------------ */
/* Command buffer lifecycle                                            */
/* ------------------------------------------------------------------ */

sk_command_buffer_t sk_vkrd_create_command_buffer(sk_render_device_t dev, const sk_command_buffer_desc_t* desc) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	if (device == NULL || !device->device_created) {
		return sk_command_buffer_t_zero();
	}

	u32 queue_type = desc != NULL ? desc->queue_type : (u32)SK_QUEUE_TYPE_GRAPHICS;
	VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	if (desc != NULL && desc->level == SK_COMMAND_BUFFER_LEVEL_SECONDARY) {
		level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
	}

	u32 family_index = sk_vk_device_graphics_family(device);
	if ((queue_type & (u32)SK_QUEUE_TYPE_COMPUTE) != 0u && (queue_type & (u32)SK_QUEUE_TYPE_GRAPHICS) == 0u) {
		family_index = sk_vk_device_compute_family(device);
	} else if ((queue_type & (u32)SK_QUEUE_TYPE_TRANSFER) != 0u && (queue_type & (u32)SK_QUEUE_TYPE_GRAPHICS) == 0u && (queue_type & (u32)SK_QUEUE_TYPE_COMPUTE) == 0u) {
		family_index = sk_vk_device_transfer_family(device);
	}

	VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = family_index;

	VkCommandPool command_pool = VK_NULL_HANDLE;
	if (vkCreateCommandPool(device->device, &pool_info, NULL, &command_pool) != VK_SUCCESS) {
		return sk_command_buffer_t_zero();
	}

	VkCommandBufferAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	alloc_info.level = level;
	alloc_info.commandPool = command_pool;
	alloc_info.commandBufferCount = 1u;

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	if (vkAllocateCommandBuffers(device->device, &alloc_info, &command_buffer) != VK_SUCCESS) {
		vkDestroyCommandPool(device->device, command_pool, NULL);
		return sk_command_buffer_t_zero();
	}

	sk_vk_command_buffer_t* command_buffer_obj = (sk_vk_command_buffer_t*)device->allocator->alloc(device->allocator->instance, sizeof(sk_vk_command_buffer_t));
	if (command_buffer_obj == NULL) {
		vkDestroyCommandPool(device->device, command_pool, NULL);
		return sk_command_buffer_t_zero();
	}
	memset(command_buffer_obj, 0, sizeof(*command_buffer_obj));
	command_buffer_obj->device = device;
	command_buffer_obj->command_buffer = command_buffer;
	command_buffer_obj->command_pool = command_pool;
	command_buffer_obj->queue_type = queue_type;

	if (desc != NULL && desc->debug_name != NULL) {
		VkDebugUtilsObjectNameInfoEXT name_info = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
		name_info.objectType = VK_OBJECT_TYPE_COMMAND_BUFFER;
		name_info.objectHandle = (u64)command_buffer;
		name_info.pObjectName = desc->debug_name;
		if (device->debug_utils_extension_present) {
			vkSetDebugUtilsObjectNameEXT(device->device, &name_info);
		}
	}

	return sk_command_buffer_t_from_ptr(command_buffer_obj);
}

void sk_vkrd_destroy_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (device == NULL || command_buffer == NULL) {
		return;
	}
	if (command_buffer->command_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(device->device, command_buffer->command_pool, NULL);
	}
	device->allocator->free(device->allocator->instance, command_buffer);
}

i32 sk_vkrd_begin_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_command_buffer_begin_info_t* info) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (device == NULL || command_buffer == NULL) {
		return -1;
	}

	VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	VkCommandBufferInheritanceInfo inheritance_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};

	if (info != NULL) {
		if ((info->usage_flags & (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT) != 0u) {
			begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		}
		if ((info->usage_flags & (u32)SK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE) != 0u) {
			begin_info.flags |= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		}
		if ((info->usage_flags & (u32)SK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE) != 0u) {
			begin_info.flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
			sk_vk_render_pass_t* render_pass = (sk_vk_render_pass_t*)sk_render_pass_t_to_ptr(info->render_pass);
			sk_vk_framebuffer_t* framebuffer = (sk_vk_framebuffer_t*)sk_framebuffer_t_to_ptr(info->framebuffer);
			inheritance_info.renderPass = render_pass != NULL ? render_pass->render_pass : VK_NULL_HANDLE;
			inheritance_info.subpass = info->subpass;
			inheritance_info.framebuffer = framebuffer != NULL ? framebuffer->framebuffer : VK_NULL_HANDLE;
			begin_info.pInheritanceInfo = &inheritance_info;
		}
	} else {
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	}

	VkResult result = vkBeginCommandBuffer(command_buffer->command_buffer, &begin_info);
	return result == VK_SUCCESS ? 0 : -1;
}

void sk_vkrd_end_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkEndCommandBuffer(command_buffer->command_buffer);
}

void sk_vkrd_reset_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkResetCommandBuffer(command_buffer->command_buffer, 0u);
}

void sk_vkrd_execute_commands(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_command_buffer_t* secondary_cmds, u32 secondary_count) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL || secondary_cmds == NULL || secondary_count == 0u) {
		return;
	}

	VkCommandBuffer* secondary = (VkCommandBuffer*)sk_allocator_default()->alloc(NULL, (size_t)secondary_count * sizeof(VkCommandBuffer));
	if (secondary == NULL) {
		return;
	}
	for (u32 i = 0u; i < secondary_count; ++i) {
		sk_vk_command_buffer_t* secondary_obj = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(secondary_cmds[i]);
		secondary[i] = secondary_obj != NULL ? secondary_obj->command_buffer : VK_NULL_HANDLE;
	}
	vkCmdExecuteCommands(command_buffer->command_buffer, secondary_count, secondary);
	sk_allocator_default()->free(NULL, secondary);
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

void sk_vkrd_set_viewport(sk_render_device_t dev, sk_command_buffer_t cmd_handle, u32 first_viewport, const sk_viewport_t* viewports, u32 viewport_count) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL || viewports == NULL || viewport_count == 0u) {
		return;
	}

	VkViewport* vk_viewports = (VkViewport*)sk_allocator_default()->alloc(NULL, (size_t)viewport_count * sizeof(VkViewport));
	if (vk_viewports == NULL) {
		return;
	}
	for (u32 i = 0u; i < viewport_count; ++i) {
		vk_viewports[i].x = viewports[i].x;
		vk_viewports[i].y = viewports[i].y;
		vk_viewports[i].width = viewports[i].width;
		vk_viewports[i].height = viewports[i].height;
		vk_viewports[i].minDepth = viewports[i].min_depth;
		vk_viewports[i].maxDepth = viewports[i].max_depth;
	}
	vkCmdSetViewport(command_buffer->command_buffer, first_viewport, viewport_count, vk_viewports);
	sk_allocator_default()->free(NULL, vk_viewports);
}

void sk_vkrd_set_scissor(sk_render_device_t dev, sk_command_buffer_t cmd_handle, u32 first_scissor, const sk_rect2d_t* scissors, u32 scissor_count) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL || scissors == NULL || scissor_count == 0u) {
		return;
	}

	VkRect2D* vk_scissors = (VkRect2D*)sk_allocator_default()->alloc(NULL, (size_t)scissor_count * sizeof(VkRect2D));
	if (vk_scissors == NULL) {
		return;
	}
	for (u32 i = 0u; i < scissor_count; ++i) {
		vk_scissors[i].offset.x = (i32)scissors[i].x;
		vk_scissors[i].offset.y = (i32)scissors[i].y;
		vk_scissors[i].extent.width = scissors[i].width;
		vk_scissors[i].extent.height = scissors[i].height;
	}
	vkCmdSetScissor(command_buffer->command_buffer, first_scissor, scissor_count, vk_scissors);
	sk_allocator_default()->free(NULL, vk_scissors);
}

void sk_vkrd_set_blend_constants(sk_render_device_t dev, sk_command_buffer_t cmd_handle, f32 r, f32 g, f32 b, f32 a) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	float constants[4] = {r, g, b, a};
	vkCmdSetBlendConstants(command_buffer->command_buffer, constants);
}

void sk_vkrd_set_stencil_reference(sk_render_device_t dev, sk_command_buffer_t cmd_handle, u32 reference) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdSetStencilReference(command_buffer->command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

void sk_vkrd_set_depth_bias(sk_render_device_t dev, sk_command_buffer_t cmd_handle, f32 constant_factor, f32 clamp, f32 slope_factor) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdSetDepthBias(command_buffer->command_buffer, constant_factor, clamp, slope_factor);
}

void sk_vkrd_set_depth_bounds(sk_render_device_t dev, sk_command_buffer_t cmd_handle, f32 min_depth, f32 max_depth) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdSetDepthBounds(command_buffer->command_buffer, min_depth, max_depth);
}

void sk_vkrd_bind_pipeline(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline_handle) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_pipeline_t* pipeline = (sk_vk_pipeline_t*)sk_pipeline_t_to_ptr(pipeline_handle);
	if (command_buffer == NULL || pipeline == NULL) {
		return;
	}
	vkCmdBindPipeline(command_buffer->command_buffer, sk_vkrd_bind_point(bind_point), pipeline->pipeline);
}

void sk_vkrd_bind_descriptor_set(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline_handle, u32 set_index,
								 sk_descriptor_set_t desc_set_handle, const u32* dynamic_offsets, u32 offset_count) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_pipeline_t* pipeline = (sk_vk_pipeline_t*)sk_pipeline_t_to_ptr(pipeline_handle);
	sk_vk_descriptor_set_t* descriptor_set = (sk_vk_descriptor_set_t*)sk_descriptor_set_t_to_ptr(desc_set_handle);
	if (command_buffer == NULL || pipeline == NULL || descriptor_set == NULL) {
		return;
	}
	vkCmdBindDescriptorSets(command_buffer->command_buffer, sk_vkrd_bind_point(bind_point), pipeline->pipeline_layout, set_index, 1u, &descriptor_set->descriptor_set, offset_count,
							dynamic_offsets);
}

void sk_vkrd_bind_vertex_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle, u32 first_binding, const sk_buffer_t* buffers, const u64* offsets, u32 buffer_count) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL || buffers == NULL || buffer_count == 0u) {
		return;
	}

	VkBuffer* vk_buffers = (VkBuffer*)sk_allocator_default()->alloc(NULL, (size_t)buffer_count * sizeof(VkBuffer));
	VkDeviceSize* vk_offsets = (VkDeviceSize*)sk_allocator_default()->alloc(NULL, (size_t)buffer_count * sizeof(VkDeviceSize));
	if (vk_buffers == NULL || vk_offsets == NULL) {
		sk_allocator_default()->free(NULL, vk_buffers);
		sk_allocator_default()->free(NULL, vk_offsets);
		return;
	}
	for (u32 i = 0u; i < buffer_count; ++i) {
		sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buffers[i]);
		vk_buffers[i] = buffer != NULL ? buffer->buffer : VK_NULL_HANDLE;
		vk_offsets[i] = (VkDeviceSize)offsets[i];
	}
	vkCmdBindVertexBuffers(command_buffer->command_buffer, first_binding, buffer_count, vk_buffers, vk_offsets);
	sk_allocator_default()->free(NULL, vk_buffers);
	sk_allocator_default()->free(NULL, vk_offsets);
}

void sk_vkrd_bind_index_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t buf_handle, u64 offset, sk_index_type_t index_type) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buf_handle);
	if (command_buffer == NULL || buffer == NULL) {
		return;
	}
	VkIndexType vk_index_type = index_type == SK_INDEX_TYPE_UINT16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
	vkCmdBindIndexBuffer(command_buffer->command_buffer, buffer->buffer, offset, vk_index_type);
}

void sk_vkrd_push_constants(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_pipeline_t pipeline_handle, u32 shader_stages, u32 offset, u32 size, const void* data) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_pipeline_t* pipeline = (sk_vk_pipeline_t*)sk_pipeline_t_to_ptr(pipeline_handle);
	if (command_buffer == NULL || pipeline == NULL || data == NULL) {
		return;
	}
	VkShaderStageFlags stages = sk_vk_convert_shader_stage_flags(shader_stages);
	if (shader_stages == 0u) {
		stages = VK_SHADER_STAGE_ALL;
	}
	vkCmdPushConstants(command_buffer->command_buffer, pipeline->pipeline_layout, stages, offset, size, data);
}

/* ------------------------------------------------------------------ */
/* Draw / dispatch                                                     */
/* ------------------------------------------------------------------ */

void sk_vkrd_draw(sk_render_device_t dev, sk_command_buffer_t cmd_handle, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdDraw(command_buffer->command_buffer, vertex_count, instance_count, first_vertex, first_instance);
}

void sk_vkrd_draw_indexed(sk_render_device_t dev, sk_command_buffer_t cmd_handle, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdDrawIndexed(command_buffer->command_buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}

static VkBuffer sk_vkrd_buffer_vk_handle(sk_buffer_t buf_handle) {
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buf_handle);
	return buffer != NULL ? buffer->buffer : VK_NULL_HANDLE;
}

void sk_vkrd_draw_indirect(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t buffer_handle, u64 offset, u32 draw_count, u32 stride) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdDrawIndirect(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(buffer_handle), offset, draw_count, stride);
}

void sk_vkrd_draw_indexed_indirect(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t buffer_handle, u64 offset, u32 draw_count, u32 stride) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdDrawIndexedIndirect(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(buffer_handle), offset, draw_count, stride);
}

void sk_vkrd_draw_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t buffer_handle, u64 offset, sk_buffer_t count_buffer_handle, u64 count_offset,
								 u32 max_draw_count, u32 stride) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdDrawIndirectCountKHR(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(buffer_handle), offset, sk_vkrd_buffer_vk_handle(count_buffer_handle), count_offset,
							  max_draw_count, stride);
}

void sk_vkrd_draw_indexed_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t buffer_handle, u64 offset, sk_buffer_t count_buffer_handle,
										 u64 count_offset, u32 max_draw_count, u32 stride) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdDrawIndexedIndirectCountKHR(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(buffer_handle), offset, sk_vkrd_buffer_vk_handle(count_buffer_handle), count_offset,
									 max_draw_count, stride);
}

void sk_vkrd_dispatch(sk_render_device_t dev, sk_command_buffer_t cmd_handle, u32 group_count_x, u32 group_count_y, u32 group_count_z) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdDispatch(command_buffer->command_buffer, group_count_x, group_count_y, group_count_z);
}

void sk_vkrd_dispatch_indirect(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t buffer_handle, u64 offset) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdDispatchIndirect(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(buffer_handle), offset);
}

void sk_vkrd_trace_rays(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_trace_rays_info_t* info) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (device == NULL || command_buffer == NULL || info == NULL) {
		return;
	}

	VkStridedDeviceAddressRegionKHR regions[4] = {0};
	const sk_shader_binding_table_region_t* region_descs[4] = {&info->raygen, &info->miss, &info->hit, &info->callable};
	for (u32 i = 0u; i < 4u; ++i) {
		if (sk_buffer_t_is_valid(region_descs[i]->buffer)) {
			regions[i].deviceAddress = sk_vkrd_buffer_device_address(device, region_descs[i]->buffer) + region_descs[i]->offset;
			regions[i].stride = region_descs[i]->stride;
			regions[i].size = region_descs[i]->size;
		}
	}

	vkCmdTraceRaysKHR(command_buffer->command_buffer, &regions[0], &regions[1], &regions[2], &regions[3], info->width, info->height, info->depth);
}

/* ------------------------------------------------------------------ */
/* Render pass                                                         */
/* ------------------------------------------------------------------ */

void sk_vkrd_begin_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_begin_render_pass_info_t* info) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL || info == NULL) {
		return;
	}
	sk_vk_render_pass_t* render_pass = (sk_vk_render_pass_t*)sk_render_pass_t_to_ptr(info->render_pass);
	sk_vk_framebuffer_t* framebuffer = (sk_vk_framebuffer_t*)sk_framebuffer_t_to_ptr(info->framebuffer);
	if (render_pass == NULL || framebuffer == NULL) {
		return;
	}

	if (info->clear_values != NULL) {
		for (u32 i = 0u; i < framebuffer->clear_value_count; ++i) {
			VkClearValue* clear_value = &framebuffer->clear_values[i];
			bool is_depth = false;
			if (i < framebuffer->desc.attachment_count) {
				sk_vk_texture_view_t* view = (sk_vk_texture_view_t*)sk_texture_view_t_to_ptr(framebuffer->desc.attachments[i]);
				is_depth = view != NULL && view->texture != NULL && view->texture->is_depth;
			}
			if (is_depth) {
				clear_value->depthStencil.depth = info->clear_values->depth;
				clear_value->depthStencil.stencil = info->clear_values->stencil;
			} else {
				clear_value->color.float32[0] = info->clear_values->color.r;
				clear_value->color.float32[1] = info->clear_values->color.g;
				clear_value->color.float32[2] = info->clear_values->color.b;
				clear_value->color.float32[3] = info->clear_values->color.a;
			}
		}
	}

	VkRenderPassBeginInfo render_pass_begin_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
	render_pass_begin_info.renderPass = render_pass->render_pass;
	render_pass_begin_info.framebuffer = framebuffer->framebuffer;
	render_pass_begin_info.renderArea.offset.x = 0;
	render_pass_begin_info.renderArea.offset.y = 0;
	render_pass_begin_info.renderArea.extent.width = framebuffer->extent.width;
	render_pass_begin_info.renderArea.extent.height = framebuffer->extent.height;

	if (info->clear_values != NULL) {
		render_pass_begin_info.clearValueCount = framebuffer->clear_value_count;
		render_pass_begin_info.pClearValues = framebuffer->clear_values;
	}

	vkCmdBeginRenderPass(command_buffer->command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

void sk_vkrd_end_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd_handle) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdEndRenderPass(command_buffer->command_buffer);
}

void sk_vkrd_clear_attachments(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_clear_attachment_t* attachments, u32 attachment_count) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL || attachments == NULL || attachment_count == 0u) {
		return;
	}

	VkClearAttachment* vk_attachments = (VkClearAttachment*)sk_allocator_default()->alloc(NULL, (size_t)attachment_count * sizeof(VkClearAttachment));
	VkClearRect* vk_rects = (VkClearRect*)sk_allocator_default()->alloc(NULL, (size_t)attachment_count * sizeof(VkClearRect));
	if (vk_attachments == NULL || vk_rects == NULL) {
		sk_allocator_default()->free(NULL, vk_attachments);
		sk_allocator_default()->free(NULL, vk_rects);
		return;
	}

	for (u32 i = 0u; i < attachment_count; ++i) {
		const sk_clear_attachment_t* attachment = &attachments[i];
		vk_attachments[i].aspectMask = attachment->is_depth_stencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
		vk_attachments[i].colorAttachment = attachment->color_attachment;
		if (attachment->is_depth_stencil) {
			vk_attachments[i].clearValue.depthStencil.depth = attachment->clear.depth;
			vk_attachments[i].clearValue.depthStencil.stencil = attachment->clear.stencil;
		} else {
			vk_attachments[i].clearValue.color.float32[0] = attachment->clear.color.r;
			vk_attachments[i].clearValue.color.float32[1] = attachment->clear.color.g;
			vk_attachments[i].clearValue.color.float32[2] = attachment->clear.color.b;
			vk_attachments[i].clearValue.color.float32[3] = attachment->clear.color.a;
		}

		vk_rects[i].layerCount = 1u;
		vk_rects[i].baseArrayLayer = 0u;
		if (attachment->rect.width == 0u || attachment->rect.height == 0u) {
			vk_rects[i].rect.offset.x = 0;
			vk_rects[i].rect.offset.y = 0;
			vk_rects[i].rect.extent.width = UINT32_MAX;
			vk_rects[i].rect.extent.height = UINT32_MAX;
		} else {
			vk_rects[i].rect.offset.x = attachment->rect.x;
			vk_rects[i].rect.offset.y = attachment->rect.y;
			vk_rects[i].rect.extent.width = attachment->rect.width;
			vk_rects[i].rect.extent.height = attachment->rect.height;
		}
	}

	vkCmdClearAttachments(command_buffer->command_buffer, attachment_count, vk_attachments, attachment_count, vk_rects);
	sk_allocator_default()->free(NULL, vk_attachments);
	sk_allocator_default()->free(NULL, vk_rects);
}

/* ------------------------------------------------------------------ */
/* Copy / transfer                                                     */
/* ------------------------------------------------------------------ */

void sk_vkrd_copy_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t src_handle, sk_buffer_t dst_handle, u64 size, u64 src_offset, u64 dst_offset) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	VkBufferCopy copy_region = {0};
	copy_region.srcOffset = src_offset;
	copy_region.dstOffset = dst_offset;
	copy_region.size = size;
	vkCmdCopyBuffer(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(src_handle), sk_vkrd_buffer_vk_handle(dst_handle), 1u, &copy_region);
}

void sk_vkrd_copy_buffer_to_texture(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_buffer_texture_copy_t* copy_info) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_texture_t* texture = (sk_vk_texture_t*)sk_texture_t_to_ptr(copy_info->texture);
	if (command_buffer == NULL || copy_info == NULL || texture == NULL) {
		return;
	}

	VkBufferImageCopy copy_region = {0};
	copy_region.bufferOffset = copy_info->buffer_offset;
	copy_region.bufferRowLength = copy_info->buffer_row_length;
	copy_region.bufferImageHeight = copy_info->buffer_image_height;
	copy_region.imageSubresource.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(texture->desc.format));
	copy_region.imageSubresource.mipLevel = copy_info->mip_level;
	copy_region.imageSubresource.baseArrayLayer = copy_info->array_layer;
	copy_region.imageSubresource.layerCount = 1u;
	copy_region.imageOffset.x = copy_info->texture_offset.x;
	copy_region.imageOffset.y = copy_info->texture_offset.y;
	copy_region.imageOffset.z = copy_info->texture_offset.z;
	copy_region.imageExtent.width = copy_info->texture_extent.width;
	copy_region.imageExtent.height = copy_info->texture_extent.height;
	copy_region.imageExtent.depth = copy_info->texture_extent.depth;

	vkCmdCopyBufferToImage(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(copy_info->buffer), texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy_region);
}

void sk_vkrd_copy_texture_to_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_buffer_texture_copy_t* copy_info) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_texture_t* texture = (sk_vk_texture_t*)sk_texture_t_to_ptr(copy_info->texture);
	if (command_buffer == NULL || copy_info == NULL || texture == NULL) {
		return;
	}

	VkBufferImageCopy copy_region = {0};
	copy_region.bufferOffset = copy_info->buffer_offset;
	copy_region.bufferRowLength = copy_info->buffer_row_length;
	copy_region.bufferImageHeight = copy_info->buffer_image_height;
	copy_region.imageSubresource.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(texture->desc.format));
	copy_region.imageSubresource.mipLevel = copy_info->mip_level;
	copy_region.imageSubresource.baseArrayLayer = copy_info->array_layer;
	copy_region.imageSubresource.layerCount = 1u;
	copy_region.imageOffset.x = copy_info->texture_offset.x;
	copy_region.imageOffset.y = copy_info->texture_offset.y;
	copy_region.imageOffset.z = copy_info->texture_offset.z;
	copy_region.imageExtent.width = copy_info->texture_extent.width;
	copy_region.imageExtent.height = copy_info->texture_extent.height;
	copy_region.imageExtent.depth = copy_info->texture_extent.depth;

	vkCmdCopyImageToBuffer(command_buffer->command_buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sk_vkrd_buffer_vk_handle(copy_info->buffer), 1u, &copy_region);
}

void sk_vkrd_copy_texture(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_texture_copy_t* copy_info) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_texture_t* src = (sk_vk_texture_t*)sk_texture_t_to_ptr(copy_info->src);
	sk_vk_texture_t* dst = (sk_vk_texture_t*)sk_texture_t_to_ptr(copy_info->dst);
	if (command_buffer == NULL || copy_info == NULL || src == NULL || dst == NULL) {
		return;
	}

	VkImageCopy copy_region = {0};
	copy_region.srcSubresource.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(src->desc.format));
	copy_region.srcSubresource.mipLevel = copy_info->src_mip_level;
	copy_region.srcSubresource.baseArrayLayer = copy_info->src_array_layer;
	copy_region.srcSubresource.layerCount = 1u;
	copy_region.srcOffset.x = copy_info->src_offset.x;
	copy_region.srcOffset.y = copy_info->src_offset.y;
	copy_region.srcOffset.z = copy_info->src_offset.z;
	copy_region.dstSubresource.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(dst->desc.format));
	copy_region.dstSubresource.mipLevel = copy_info->dst_mip_level;
	copy_region.dstSubresource.baseArrayLayer = copy_info->dst_array_layer;
	copy_region.dstSubresource.layerCount = 1u;
	copy_region.dstOffset.x = copy_info->dst_offset.x;
	copy_region.dstOffset.y = copy_info->dst_offset.y;
	copy_region.dstOffset.z = copy_info->dst_offset.z;
	copy_region.extent.width = copy_info->extent.width;
	copy_region.extent.height = copy_info->extent.height;
	copy_region.extent.depth = copy_info->extent.depth;

	vkCmdCopyImage(command_buffer->command_buffer, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy_region);
}

void sk_vkrd_blit_texture(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_texture_blit_t* blit_info) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_texture_t* src = (sk_vk_texture_t*)sk_texture_t_to_ptr(blit_info->src);
	sk_vk_texture_t* dst = (sk_vk_texture_t*)sk_texture_t_to_ptr(blit_info->dst);
	if (command_buffer == NULL || blit_info == NULL || src == NULL || dst == NULL) {
		return;
	}

	VkImageBlit blit_region = {0};
	blit_region.srcSubresource.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(src->desc.format));
	blit_region.srcSubresource.mipLevel = blit_info->src_mip_level;
	blit_region.srcSubresource.baseArrayLayer = blit_info->src_array_layer;
	blit_region.srcSubresource.layerCount = 1u;
	blit_region.srcOffsets[0].x = blit_info->src_offsets[0].x;
	blit_region.srcOffsets[0].y = blit_info->src_offsets[0].y;
	blit_region.srcOffsets[0].z = blit_info->src_offsets[0].z;
	blit_region.srcOffsets[1].x = blit_info->src_offsets[1].x;
	blit_region.srcOffsets[1].y = blit_info->src_offsets[1].y;
	blit_region.srcOffsets[1].z = blit_info->src_offsets[1].z;
	blit_region.dstSubresource.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(dst->desc.format));
	blit_region.dstSubresource.mipLevel = blit_info->dst_mip_level;
	blit_region.dstSubresource.baseArrayLayer = blit_info->dst_array_layer;
	blit_region.dstSubresource.layerCount = 1u;
	blit_region.dstOffsets[0].x = blit_info->dst_offsets[0].x;
	blit_region.dstOffsets[0].y = blit_info->dst_offsets[0].y;
	blit_region.dstOffsets[0].z = blit_info->dst_offsets[0].z;
	blit_region.dstOffsets[1].x = blit_info->dst_offsets[1].x;
	blit_region.dstOffsets[1].y = blit_info->dst_offsets[1].y;
	blit_region.dstOffsets[1].z = blit_info->dst_offsets[1].z;

	VkFilter filter = blit_info->filter == SK_FILTER_MODE_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	vkCmdBlitImage(command_buffer->command_buffer, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &blit_region, filter);
}

void sk_vkrd_resolve_texture(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const sk_texture_resolve_t* resolve_info) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_texture_t* src = (sk_vk_texture_t*)sk_texture_t_to_ptr(resolve_info->src);
	sk_vk_texture_t* dst = (sk_vk_texture_t*)sk_texture_t_to_ptr(resolve_info->dst);
	if (command_buffer == NULL || resolve_info == NULL || src == NULL || dst == NULL) {
		return;
	}

	VkImageResolve resolve_region = {0};
	resolve_region.srcSubresource.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(src->desc.format));
	resolve_region.srcSubresource.mipLevel = resolve_info->src_mip_level;
	resolve_region.srcSubresource.baseArrayLayer = resolve_info->src_array_layer;
	resolve_region.srcSubresource.layerCount = 1u;
	resolve_region.dstSubresource.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(dst->desc.format));
	resolve_region.dstSubresource.mipLevel = resolve_info->dst_mip_level;
	resolve_region.dstSubresource.baseArrayLayer = resolve_info->dst_array_layer;
	resolve_region.dstSubresource.layerCount = 1u;
	resolve_region.extent.width = dst->desc.extent.width;
	resolve_region.extent.height = dst->desc.extent.height;
	resolve_region.extent.depth = dst->desc.extent.depth;

	vkCmdResolveImage(command_buffer->command_buffer, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &resolve_region);
}

/* vkCmdUpdateBuffer: dataSize must be > 0, a multiple of 4, and <= 65536. */
enum { SK_VK_CMD_UPDATE_BUFFER_MAX_BYTES = 65536u };

static u32 sk_vkrd_update_buffer_chunk_plan(u64 size, u64* out_sizes, u32 max_chunks) {
	u32 count = 0u;
	u64 remaining = size;
	while (remaining > 0u && count < max_chunks) {
		u64 chunk = remaining > (u64)SK_VK_CMD_UPDATE_BUFFER_MAX_BYTES ? (u64)SK_VK_CMD_UPDATE_BUFFER_MAX_BYTES : remaining;
		out_sizes[count] = chunk;
		count += 1u;
		remaining -= chunk;
	}
	return count;
}

void sk_vkrd_update_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t buf_handle, u64 offset, u64 size, const void* data) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	const u8* bytes = (const u8*)data;
	u64 remaining = size;
	u64 dst = offset;
	if (command_buffer == NULL || data == NULL || size == 0u) {
		return;
	}
	while (remaining > 0u) {
		u64 chunk;
		if (sk_vkrd_update_buffer_chunk_plan(remaining, &chunk, 1u) != 1u) {
			break;
		}
		if ((chunk & 3ull) != 0ull) {
			u8 pad[4] = {0u, 0u, 0u, 0u};
			memcpy(pad, bytes, chunk);
			vkCmdUpdateBuffer(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(buf_handle), dst, 4u, pad);
			break;
		}
		vkCmdUpdateBuffer(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(buf_handle), dst, chunk, bytes);
		bytes += chunk;
		dst += chunk;
		remaining -= chunk;
	}
}

void sk_vkrd_fill_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t buf_handle, u64 offset, u64 size, u32 data) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	vkCmdFillBuffer(command_buffer->command_buffer, sk_vkrd_buffer_vk_handle(buf_handle), offset, size, data);
}

void sk_vkrd_clear_texture(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_texture_t tex_handle, const sk_clear_values_t* clear, u32 base_mip_level, u32 mip_level_count,
						   u32 base_array_layer, u32 array_layer_count) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_texture_t* texture = (sk_vk_texture_t*)sk_texture_t_to_ptr(tex_handle);
	if (command_buffer == NULL || texture == NULL || clear == NULL) {
		return;
	}

	VkImageSubresourceRange range = {0};
	range.baseMipLevel = base_mip_level;
	range.levelCount = mip_level_count;
	range.baseArrayLayer = base_array_layer;
	range.layerCount = array_layer_count;

	if (texture->is_depth) {
		range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (texture->desc.format == SK_PIXEL_FORMAT_D24_UNORM_S8_UINT || texture->desc.format == SK_PIXEL_FORMAT_D32_FLOAT_S8_UINT) {
			range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		VkClearDepthStencilValue clear_value = {0};
		clear_value.depth = clear->depth;
		clear_value.stencil = clear->stencil;
		vkCmdClearDepthStencilImage(command_buffer->command_buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, 1u, &range);
	} else {
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkClearColorValue clear_value = {0};
		clear_value.float32[0] = clear->color.r;
		clear_value.float32[1] = clear->color.g;
		clear_value.float32[2] = clear->color.b;
		clear_value.float32[3] = clear->color.a;
		vkCmdClearColorImage(command_buffer->command_buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, 1u, &range);
	}
}

/* ------------------------------------------------------------------ */
/* Barriers                                                            */
/* ------------------------------------------------------------------ */

void sk_vkrd_resource_barrier_buffer(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_buffer_t buf_handle, sk_resource_state_t old_state, sk_resource_state_t new_state,
									 u32 src_scope, u32 dst_scope) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_buffer_t* buffer = (sk_vk_buffer_t*)sk_buffer_t_to_ptr(buf_handle);
	if (command_buffer == NULL || buffer == NULL) {
		return;
	}

	VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
	barrier.srcAccessMask = clamp_access_for_queue(buffer_access_flags_from_state_scope(old_state, src_scope), command_buffer->queue_type);
	barrier.dstAccessMask = clamp_access_for_queue(buffer_access_flags_from_state_scope(new_state, dst_scope), command_buffer->queue_type);
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer->buffer;
	barrier.offset = 0u;
	barrier.size = buffer->desc.size;

	VkPipelineStageFlags src_stage_mask = clamp_stage_for_queue(buffer_pipeline_stage_from_state_scope(old_state, src_scope), command_buffer->queue_type);
	VkPipelineStageFlags dst_stage_mask = clamp_stage_for_queue(buffer_pipeline_stage_from_state_scope(new_state, dst_scope), command_buffer->queue_type);

	vkCmdPipelineBarrier(command_buffer->command_buffer, src_stage_mask, dst_stage_mask, 0u, 0u, NULL, 1u, &barrier, 0u, NULL);
}

void sk_vkrd_resource_barrier_texture(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_texture_t tex_handle, sk_resource_state_t old_state, sk_resource_state_t new_state,
									  u32 base_mip_level, u32 mip_level_count, u32 base_array_layer, u32 array_layer_count, u32 src_scope, u32 dst_scope) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_texture_t* texture = (sk_vk_texture_t*)sk_texture_t_to_ptr(tex_handle);
	if (command_buffer == NULL || texture == NULL) {
		return;
	}

	VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
	barrier.oldLayout = sk_vk_cast_state(old_state, VK_IMAGE_LAYOUT_UNDEFINED);
	barrier.newLayout = sk_vk_cast_state(new_state, VK_IMAGE_LAYOUT_UNDEFINED);
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = texture->image;
	barrier.subresourceRange.aspectMask = sk_vk_get_image_aspect_flags(sk_vk_to_vk_format(texture->desc.format));
	barrier.subresourceRange.baseMipLevel = base_mip_level;
	barrier.subresourceRange.levelCount = mip_level_count;
	barrier.subresourceRange.baseArrayLayer = base_array_layer;
	barrier.subresourceRange.layerCount = array_layer_count;

	barrier.srcAccessMask = clamp_access_for_queue(access_flags_from_state_scope(old_state, src_scope), command_buffer->queue_type);
	barrier.dstAccessMask = clamp_access_for_queue(access_flags_from_state_scope(new_state, dst_scope), command_buffer->queue_type);

	VkPipelineStageFlags src_stage_mask = clamp_stage_for_queue(pipeline_stage_from_state_scope(old_state, src_scope), command_buffer->queue_type);
	VkPipelineStageFlags dst_stage_mask = clamp_stage_for_queue(pipeline_stage_from_state_scope(new_state, dst_scope), command_buffer->queue_type);

	vkCmdPipelineBarrier(command_buffer->command_buffer, src_stage_mask, dst_stage_mask, 0u, 0u, NULL, 0u, NULL, 1u, &barrier);
}

static void sk_vkrd_as_resource_barrier(sk_vk_command_buffer_t* command_buffer, sk_resource_state_t old_state, sk_resource_state_t new_state) {
	VkAccessFlags src_access = sk_vk_get_access_flags_from_state(old_state);
	VkAccessFlags dst_access = sk_vk_get_access_flags_from_state(new_state);

	if (old_state == SK_RESOURCE_STATE_SHADER_READ || old_state == SK_RESOURCE_STATE_GENERAL) {
		src_access |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	}
	if (old_state == SK_RESOURCE_STATE_COPY_DEST || old_state == SK_RESOURCE_STATE_GENERAL) {
		src_access |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	}
	if (new_state == SK_RESOURCE_STATE_SHADER_READ || new_state == SK_RESOURCE_STATE_GENERAL) {
		dst_access |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	}
	if (new_state == SK_RESOURCE_STATE_COPY_DEST || new_state == SK_RESOURCE_STATE_GENERAL) {
		dst_access |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	}

	VkMemoryBarrier memory_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
	memory_barrier.srcAccessMask = src_access;
	memory_barrier.dstAccessMask = dst_access;

	VkPipelineStageFlags src_stage_mask = sk_vk_get_pipeline_stage_from_state(old_state);
	VkPipelineStageFlags dst_stage_mask = sk_vk_get_pipeline_stage_from_state(new_state);

	if (old_state == SK_RESOURCE_STATE_SHADER_READ || old_state == SK_RESOURCE_STATE_GENERAL) {
		src_stage_mask |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	}
	if (new_state == SK_RESOURCE_STATE_SHADER_READ || new_state == SK_RESOURCE_STATE_GENERAL) {
		dst_stage_mask |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	}

	vkCmdPipelineBarrier(command_buffer->command_buffer, src_stage_mask, dst_stage_mask, 0u, 1u, &memory_barrier, 0u, NULL, 0u, NULL);
}

void sk_vkrd_resource_barrier_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_blas_t blas_handle, sk_resource_state_t old_state,
											  sk_resource_state_t new_state, u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)blas_handle;
	(void)src_scope;
	(void)dst_scope;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	sk_vkrd_as_resource_barrier(command_buffer, old_state, new_state);
}

void sk_vkrd_resource_barrier_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_tlas_t tlas_handle, sk_resource_state_t old_state,
										   sk_resource_state_t new_state, u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)tlas_handle;
	(void)src_scope;
	(void)dst_scope;
	if (old_state == new_state) {
		return;
	}
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	sk_vkrd_as_resource_barrier(command_buffer, old_state, new_state);
}

void sk_vkrd_memory_barrier(sk_render_device_t dev, sk_command_buffer_t cmd_handle) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (command_buffer == NULL) {
		return;
	}
	VkMemoryBarrier memory_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
	memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	vkCmdPipelineBarrier(command_buffer->command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 1u, &memory_barrier, 0u, NULL, 0u, NULL);
}

/* ------------------------------------------------------------------ */
/* Debug labels                                                        */
/* ------------------------------------------------------------------ */

void sk_vkrd_begin_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const_chr_t name, f32 r, f32 g, f32 b, f32 a) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (device == NULL || command_buffer == NULL || name == NULL || !device->debug_utils_extension_present) {
		return;
	}
	VkDebugUtilsLabelEXT label_info = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
	label_info.pLabelName = name;
	label_info.color[0] = r;
	label_info.color[1] = g;
	label_info.color[2] = b;
	label_info.color[3] = a;
	vkCmdBeginDebugUtilsLabelEXT(command_buffer->command_buffer, &label_info);
}

void sk_vkrd_end_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd_handle) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (device == NULL || command_buffer == NULL || !device->debug_utils_extension_present) {
		return;
	}
	vkCmdEndDebugUtilsLabelEXT(command_buffer->command_buffer);
}

void sk_vkrd_insert_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd_handle, const_chr_t name, f32 r, f32 g, f32 b, f32 a) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	if (device == NULL || command_buffer == NULL || name == NULL || !device->debug_utils_extension_present) {
		return;
	}
	VkDebugUtilsLabelEXT label_info = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
	label_info.pLabelName = name;
	label_info.color[0] = r;
	label_info.color[1] = g;
	label_info.color[2] = b;
	label_info.color[3] = a;
	vkCmdInsertDebugUtilsLabelEXT(command_buffer->command_buffer, &label_info);
}

/* ------------------------------------------------------------------ */
/* Queries (command recording)                                         */
/* ------------------------------------------------------------------ */

static sk_vk_query_pool_t* sk_vkrd_query_pool(sk_query_pool_t pool_handle) {
	return (sk_vk_query_pool_t*)sk_query_pool_t_to_ptr(pool_handle);
}

void sk_vkrd_reset_query_pool(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_query_pool_t pool_handle, u32 first_query, u32 query_count) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_query_pool_t* query_pool = sk_vkrd_query_pool(pool_handle);
	if (command_buffer == NULL || query_pool == NULL) {
		return;
	}
	vkCmdResetQueryPool(command_buffer->command_buffer, query_pool->query_pool, first_query, query_count);
}

void sk_vkrd_begin_query(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_query_pool_t pool_handle, u32 query) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_query_pool_t* query_pool = sk_vkrd_query_pool(pool_handle);
	if (command_buffer == NULL || query_pool == NULL) {
		return;
	}
	VkQueryControlFlags flags = 0u;
	if (query_pool->desc.type == SK_QUERY_TYPE_OCCLUSION) {
		flags |= VK_QUERY_CONTROL_PRECISE_BIT;
	}
	vkCmdBeginQuery(command_buffer->command_buffer, query_pool->query_pool, query, flags);
}

void sk_vkrd_end_query(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_query_pool_t pool_handle, u32 query) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_query_pool_t* query_pool = sk_vkrd_query_pool(pool_handle);
	if (command_buffer == NULL || query_pool == NULL) {
		return;
	}
	if (query_pool->desc.type == SK_QUERY_TYPE_OCCLUSION || query_pool->desc.type == SK_QUERY_TYPE_PIPELINE_STATISTICS) {
		vkCmdEndQuery(command_buffer->command_buffer, query_pool->query_pool, query);
	}
}

void sk_vkrd_write_timestamp(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_query_pool_t pool_handle, u32 query) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_query_pool_t* query_pool = sk_vkrd_query_pool(pool_handle);
	if (command_buffer == NULL || query_pool == NULL) {
		return;
	}
	if (query_pool->desc.type == SK_QUERY_TYPE_TIMESTAMP) {
		vkCmdWriteTimestamp(command_buffer->command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool->query_pool, query);
	}
}

void sk_vkrd_copy_query_pool_results(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_query_pool_t pool_handle, u32 first_query, u32 query_count,
									 sk_buffer_t dst_buffer_handle, u64 dst_offset, u64 stride) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_query_pool_t* query_pool = sk_vkrd_query_pool(pool_handle);
	if (command_buffer == NULL || query_pool == NULL) {
		return;
	}

	VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT;
	if (query_pool->desc.allow_partial_results) {
		flags |= VK_QUERY_RESULT_PARTIAL_BIT;
	}
	if (query_pool->desc.return_availability) {
		flags |= VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
	}

	vkCmdCopyQueryPoolResults(command_buffer->command_buffer, query_pool->query_pool, first_query, query_count, sk_vkrd_buffer_vk_handle(dst_buffer_handle), dst_offset, stride,
							  flags);
}

/* ------------------------------------------------------------------ */
/* Acceleration structure build / copy                                 */
/* ------------------------------------------------------------------ */

static VkDeviceAddress sk_vkrd_aligned_scratch(sk_vk_device_t* device, sk_buffer_t scratch_handle, u64 scratch_offset) {
	VkDeviceAddress scratch_addr = sk_vkrd_buffer_device_address(device, scratch_handle) + scratch_offset;
	VkDeviceAddress alignment = device->selected_adapter->acceleration_structure_props.minAccelerationStructureScratchOffsetAlignment;
	if (alignment > 1u) {
		scratch_addr = (scratch_addr + alignment - 1u) & ~(alignment - 1u);
	}
	return scratch_addr;
}

void sk_vkrd_build_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_blas_t blas_handle, const sk_blas_desc_t* desc,
								   const sk_as_build_info_t* build_info) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_blas_t* blas = (sk_vk_blas_t*)sk_blas_t_to_ptr(blas_handle);
	(void)desc;
	if (device == NULL || command_buffer == NULL || blas == NULL || build_info == NULL) {
		return;
	}

	VkAccelerationStructureBuildGeometryInfoKHR build_geom_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	build_geom_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_geom_info.flags = blas->build_flags;
	build_geom_info.mode = build_info->is_update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_geom_info.dstAccelerationStructure = blas->acceleration_structure;
	build_geom_info.geometryCount = blas->geometry_count;
	build_geom_info.pGeometries = blas->geometries;

	if (build_info->is_update) {
		build_geom_info.srcAccelerationStructure = blas->acceleration_structure;
	}
	if (sk_buffer_t_is_valid(build_info->scratch_buffer)) {
		build_geom_info.scratchData.deviceAddress = sk_vkrd_aligned_scratch(device, build_info->scratch_buffer, build_info->scratch_offset);
	}

	const VkAccelerationStructureBuildRangeInfoKHR* p_range_infos = blas->range_infos;
	vkCmdBuildAccelerationStructuresKHR(command_buffer->command_buffer, 1u, &build_geom_info, &p_range_infos);
}

void sk_vkrd_build_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_tlas_t tlas_handle, const sk_tlas_desc_t* desc, const sk_as_build_info_t* build_info) {
	sk_vk_device_t* device = (sk_vk_device_t*)sk_render_device_t_to_ptr(dev);
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_tlas_t* tlas = (sk_vk_tlas_t*)sk_tlas_t_to_ptr(tlas_handle);
	(void)desc;
	if (device == NULL || command_buffer == NULL || tlas == NULL || build_info == NULL) {
		return;
	}

	VkDeviceAddress instance_address = sk_vkrd_buffer_device_address(device, sk_buffer_t_from_ptr(tlas->instance_buffer));

	VkAccelerationStructureGeometryKHR geometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry.geometry.instances.arrayOfPointers = VK_FALSE;
	geometry.geometry.instances.data.deviceAddress = instance_address;

	VkAccelerationStructureBuildGeometryInfoKHR build_geom_info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	build_geom_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	build_geom_info.flags = tlas->build_flags;
	build_geom_info.mode = build_info->is_update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_geom_info.dstAccelerationStructure = tlas->acceleration_structure;
	build_geom_info.geometryCount = 1u;
	build_geom_info.pGeometries = &geometry;

	if (build_info->is_update) {
		build_geom_info.srcAccelerationStructure = tlas->acceleration_structure;
	}
	if (sk_buffer_t_is_valid(build_info->scratch_buffer)) {
		build_geom_info.scratchData.deviceAddress = sk_vkrd_aligned_scratch(device, build_info->scratch_buffer, build_info->scratch_offset);
	}

	VkAccelerationStructureBuildRangeInfoKHR range_info = {0};
	range_info.primitiveCount = tlas->instance_count;
	const VkAccelerationStructureBuildRangeInfoKHR* p_range_info = &range_info;

	vkCmdBuildAccelerationStructuresKHR(command_buffer->command_buffer, 1u, &build_geom_info, &p_range_info);
}

void sk_vkrd_copy_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_blas_t src_handle, sk_blas_t dst_handle, bool compress) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_blas_t* src = (sk_vk_blas_t*)sk_blas_t_to_ptr(src_handle);
	sk_vk_blas_t* dst = (sk_vk_blas_t*)sk_blas_t_to_ptr(dst_handle);
	if (command_buffer == NULL || src == NULL || dst == NULL) {
		return;
	}

	VkCopyAccelerationStructureInfoKHR copy_info = {.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
	copy_info.src = src->acceleration_structure;
	copy_info.dst = dst->acceleration_structure;
	copy_info.mode = compress ? VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR : VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
	vkCmdCopyAccelerationStructureKHR(command_buffer->command_buffer, &copy_info);

	if (compress) {
		dst->compacted = true;
	}
}

void sk_vkrd_copy_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd_handle, sk_tlas_t src_handle, sk_tlas_t dst_handle, bool compress) {
	(void)dev;
	sk_vk_command_buffer_t* command_buffer = (sk_vk_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd_handle);
	sk_vk_tlas_t* src = (sk_vk_tlas_t*)sk_tlas_t_to_ptr(src_handle);
	sk_vk_tlas_t* dst = (sk_vk_tlas_t*)sk_tlas_t_to_ptr(dst_handle);
	if (command_buffer == NULL || src == NULL || dst == NULL) {
		return;
	}

	VkCopyAccelerationStructureInfoKHR copy_info = {.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
	copy_info.src = src->acceleration_structure;
	copy_info.dst = dst->acceleration_structure;
	copy_info.mode = compress ? VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR : VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
	vkCmdCopyAccelerationStructureKHR(command_buffer->command_buffer, &copy_info);
}

#ifdef SK_TESTS
#include "test.h"

SK_TEST(vkrd_update_buffer_chunks_stay_within_vk_limit) {
	u64 sizes[16];
	u32 n = sk_vkrd_update_buffer_chunk_plan(0ull, sizes, 16u);
	TEST_ASSERT_EQUAL_UINT32(0u, n);

	n = sk_vkrd_update_buffer_chunk_plan(4ull, sizes, 16u);
	TEST_ASSERT_EQUAL_UINT32(1u, n);
	TEST_ASSERT_EQUAL_UINT64(4ull, sizes[0]);

	n = sk_vkrd_update_buffer_chunk_plan((u64)SK_VK_CMD_UPDATE_BUFFER_MAX_BYTES, sizes, 16u);
	TEST_ASSERT_EQUAL_UINT32(1u, n);
	TEST_ASSERT_EQUAL_UINT64((u64)SK_VK_CMD_UPDATE_BUFFER_MAX_BYTES, sizes[0]);

	n = sk_vkrd_update_buffer_chunk_plan((u64)SK_VK_CMD_UPDATE_BUFFER_MAX_BYTES + 4ull, sizes, 16u);
	TEST_ASSERT_EQUAL_UINT32(2u, n);
	TEST_ASSERT_EQUAL_UINT64((u64)SK_VK_CMD_UPDATE_BUFFER_MAX_BYTES, sizes[0]);
	TEST_ASSERT_EQUAL_UINT64(4ull, sizes[1]);

	/* Player UI uploads from player.log: 344960 and 102192. */
	n = sk_vkrd_update_buffer_chunk_plan(344960ull, sizes, 16u);
	TEST_ASSERT_TRUE(n > 1u);
	{
		u64 sum = 0ull;
		for (u32 i = 0u; i < n; ++i) {
			TEST_ASSERT_TRUE(sizes[i] <= (u64)SK_VK_CMD_UPDATE_BUFFER_MAX_BYTES);
			TEST_ASSERT_EQUAL_UINT64(0ull, sizes[i] & 3ull);
			sum += sizes[i];
		}
		TEST_ASSERT_EQUAL_UINT64(344960ull, sum);
	}

	n = sk_vkrd_update_buffer_chunk_plan(102192ull, sizes, 16u);
	TEST_ASSERT_TRUE(n > 1u);
	{
		u64 sum = 0ull;
		for (u32 i = 0u; i < n; ++i) {
			TEST_ASSERT_TRUE(sizes[i] <= (u64)SK_VK_CMD_UPDATE_BUFFER_MAX_BYTES);
			TEST_ASSERT_EQUAL_UINT64(0ull, sizes[i] & 3ull);
			sum += sizes[i];
		}
		TEST_ASSERT_EQUAL_UINT64(102192ull, sum);
	}
}

#endif /* SK_TESTS */
