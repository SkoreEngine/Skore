// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "VulkanUtils.hpp"

#include <algorithm>

#include "VulkanDevice.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::Vulkan");
	}

	// Convert ResourceUsage enum to VkBufferUsageFlags
	VkBufferUsageFlags GetBufferUsageFlags(ResourceUsage usage, bool supportBufferDeviceAddress)
	{
		VkBufferUsageFlags usageFlags = 0;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::VertexBuffer)) != 0)
			usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::IndexBuffer)) != 0)
			usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::ConstantBuffer)) != 0)
			usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::ShaderResource)) != 0)
			usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::UnorderedAccess)) != 0)
			usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::CopyDest)) != 0)
			usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::CopySource)) != 0)
			usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		if (supportBufferDeviceAddress)
			usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		return usageFlags;
	}

	// Convert ResourceUsage enum to VkImageUsageFlags
	VkImageUsageFlags GetImageUsageFlags(ResourceUsage usage)
	{
		VkImageUsageFlags usageFlags = 0;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::ShaderResource)) != 0)
			usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::RenderTarget)) != 0)
			usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::DepthStencil)) != 0)
			usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::UnorderedAccess)) != 0)
			usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::CopyDest)) != 0)
			usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		if ((static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::CopySource)) != 0)
			usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		return usageFlags;
	}

	// Convert VK format to image aspect flags
	VkImageAspectFlags GetImageAspectFlags(VkFormat format)
	{
		if (format == VK_FORMAT_D16_UNORM ||
			format == VK_FORMAT_D32_SFLOAT ||
			format == VK_FORMAT_D24_UNORM_S8_UINT ||
			format == VK_FORMAT_D32_SFLOAT_S8_UINT)
		{
			VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;

			// Add stencil aspect if format includes stencil
			if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT)
			{
				aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}

			return aspectFlags;
		}

		return VK_IMAGE_ASPECT_COLOR_BIT;
	}

	bool IsDepthFormat(VkFormat format)
	{
		return format == VK_FORMAT_D16_UNORM ||
			format == VK_FORMAT_D32_SFLOAT ||
			format == VK_FORMAT_D24_UNORM_S8_UINT ||
			format == VK_FORMAT_D32_SFLOAT_S8_UINT;
	}


	VkImageViewType GetImageViewType(TextureViewType viewType)
	{
		switch (viewType)
		{
			case TextureViewType::Type1D:
				return VK_IMAGE_VIEW_TYPE_1D;
			case TextureViewType::Type1DArray:
				return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			case TextureViewType::Type2D:
				return VK_IMAGE_VIEW_TYPE_2D;
			case TextureViewType::Type2DArray:
				return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case TextureViewType::Type3D:
				return VK_IMAGE_VIEW_TYPE_3D;
			case TextureViewType::TypeCube:
				return VK_IMAGE_VIEW_TYPE_CUBE;
			case TextureViewType::TypeCubeArray:
				return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			default:
				return VK_IMAGE_VIEW_TYPE_2D;
		}
	}

	VkSamplerAddressMode ConvertAddressMode(AddressMode mode)
	{
		switch (mode)
		{
			case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case AddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			case AddressMode::MirrorClampToEdge: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
			default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		}
	}

	VkCompareOp ConvertCompareOp(CompareOp op)
	{
		switch (op)
		{
			case CompareOp::Never: return VK_COMPARE_OP_NEVER;
			case CompareOp::Less: return VK_COMPARE_OP_LESS;
			case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
			case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
			case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
			case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
			case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
			default: return VK_COMPARE_OP_NEVER;
		}
	}

	VkBorderColor ConvertBorderColor(BorderColor color)
	{
		switch (color)
		{
			case BorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			case BorderColor::OpaqueBlack: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			case BorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			default: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		}
	}

	VkDescriptorType ConvertDescriptorType(DescriptorType type)
	{
		switch (type)
		{
			case DescriptorType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
			case DescriptorType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			case DescriptorType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			case DescriptorType::UniformTexelBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			case DescriptorType::StorageTexelBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
			case DescriptorType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case DescriptorType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			case DescriptorType::UniformBufferDynamic: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			case DescriptorType::StorageBufferDynamic: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
			case DescriptorType::InputAttachment: return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			case DescriptorType::AccelerationStructure: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			default: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		}
	}

	VkImageLayout CastState(const ResourceState& resourceLayout, VkImageLayout defaultUndefined)
	{
		switch (resourceLayout)
		{
			case ResourceState::Undefined: return defaultUndefined;
			case ResourceState::General: return VK_IMAGE_LAYOUT_GENERAL;
			case ResourceState::ColorAttachment: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			case ResourceState::DepthStencilAttachment: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			case ResourceState::DepthStencilReadOnly: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			case ResourceState::ShaderReadOnly: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			case ResourceState::CopyDest: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			case ResourceState::CopySource: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			case ResourceState::Present: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}
		SK_ASSERT(false, "castLayout not found");
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}

	VkAttachmentLoadOp CastLoadOp(AttachmentLoadOp loadOp)
	{
		switch (loadOp)
		{
			case AttachmentLoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
			case AttachmentLoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
			case AttachmentLoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		}
		SK_ASSERT(false, "VulkanUtils.hpp: castLoadOp not found");
		return VK_ATTACHMENT_LOAD_OP_MAX_ENUM;
	}

	VkAttachmentStoreOp CastStoreOp(AttachmentStoreOp storeOp)
	{
		switch (storeOp)
		{
			case AttachmentStoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
			case AttachmentStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
		SK_ASSERT(false, "VulkanUtils.hpp: castStoreOp not found");
		return VK_ATTACHMENT_STORE_OP_MAX_ENUM;
	}

	VkShaderStageFlags ConvertShaderStageFlags(ShaderStage stages)
	{
		VkShaderStageFlags stageFlags = 0;

		if (stages && ShaderStage::Vertex)
			stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
		if (stages && ShaderStage::Hull)
			stageFlags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		if (stages && ShaderStage::Domain)
			stageFlags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		if (stages && ShaderStage::Geometry)
			stageFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
		if (stages && ShaderStage::Pixel)
			stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
		if (stages && ShaderStage::Compute)
			stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
		if (stages && ShaderStage::RayGen)
			stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		if (stages && ShaderStage::AnyHit)
			stageFlags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
		if (stages && ShaderStage::ClosestHit)
			stageFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		if (stages && ShaderStage::Miss)
			stageFlags |= VK_SHADER_STAGE_MISS_BIT_KHR;
		if (stages && ShaderStage::Intersection)
			stageFlags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
		if (stages && ShaderStage::Callable)
			stageFlags |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
		if (stages && ShaderStage::All)
			stageFlags |= VK_SHADER_STAGE_ALL;

		return stageFlags;
	}

	VkPrimitiveTopology ConvertPrimitiveTopology(PrimitiveTopology topology)
	{
		switch (topology)
		{
			case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}
	}

	VkPolygonMode ConvertPolygonMode(PolygonMode mode)
	{
		switch (mode)
		{
			case PolygonMode::Fill: return VK_POLYGON_MODE_FILL;
			case PolygonMode::Line: return VK_POLYGON_MODE_LINE;
			case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
			default: return VK_POLYGON_MODE_FILL;
		}
	}

	VkCullModeFlags ConvertCullMode(CullMode mode)
	{
		switch (mode)
		{
			case CullMode::None: return VK_CULL_MODE_NONE;
			case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
			case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
			default: return VK_CULL_MODE_BACK_BIT;
		}
	}

	VkFrontFace ConvertFrontFace(FrontFace frontFace)
	{
		return frontFace == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
	}

	VkBlendFactor ConvertBlendFactor(BlendFactor factor)
	{
		switch (factor)
		{
			case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
			case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
			case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
			case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
			case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
			case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
			case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
			case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
			case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
			case BlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
			case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
			case BlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
			case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
			case BlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
			default: return VK_BLEND_FACTOR_ONE;
		}
	}

	VkBlendOp ConvertBlendOp(BlendOp op)
	{
		switch (op)
		{
			case BlendOp::Add: return VK_BLEND_OP_ADD;
			case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
			case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
			case BlendOp::Min: return VK_BLEND_OP_MIN;
			case BlendOp::Max: return VK_BLEND_OP_MAX;
			default: return VK_BLEND_OP_ADD;
		}
	}

	VkStencilOp ConvertStencilOp(StencilOp op)
	{
		switch (op)
		{
			case StencilOp::Keep: return VK_STENCIL_OP_KEEP;
			case StencilOp::Zero: return VK_STENCIL_OP_ZERO;
			case StencilOp::Replace: return VK_STENCIL_OP_REPLACE;
			case StencilOp::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
			case StencilOp::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
			case StencilOp::Invert: return VK_STENCIL_OP_INVERT;
			case StencilOp::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
			case StencilOp::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
			default: return VK_STENCIL_OP_KEEP;
		}
	}

	VkQueryType ConvertQueryType(QueryType type)
	{
		switch (type)
		{
			case QueryType::Occlusion: return VK_QUERY_TYPE_OCCLUSION;
			case QueryType::Timestamp: return VK_QUERY_TYPE_TIMESTAMP;
			case QueryType::PipelineStatistics: return VK_QUERY_TYPE_PIPELINE_STATISTICS;
			default: return VK_QUERY_TYPE_OCCLUSION;
		}
	}

	void SetObjectName(VulkanDevice& device, VkObjectType type, u64 handle, StringView name)
	{
		if (!device.debugUtilsExtensionPresent || name.Empty())
		{
			return;
		}

		VkDebugUtilsObjectNameInfoEXT nameInfo = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
		nameInfo.objectType = type;
		nameInfo.objectHandle = handle;
		nameInfo.pObjectName = name.CStr();
		vkSetDebugUtilsObjectNameEXT(device.device, &nameInfo);
	}

	void CreateDescriptorSetLayout(VkDevice vkDevice, Span<DescriptorSetLayoutBinding> bindings, VkDescriptorSetLayout* descriptorSetLayout, bool* hasRuntimeArrays)
	{
		Array<VkDescriptorSetLayoutBinding> vkBindings{};
		vkBindings.Resize(bindings.Size());

		bool hasRuntimeArrayValue{};

		for (int i = 0; i < vkBindings.Size(); ++i)
		{
			vkBindings[i].binding = bindings[i].binding;
			vkBindings[i].descriptorCount = bindings[i].renderType != RenderType::RuntimeArray ? bindings[i].count : MaxBindlessResources;
			vkBindings[i].descriptorType = ConvertDescriptorType(bindings[i].descriptorType);
			vkBindings[i].stageFlags = ConvertShaderStageFlags(bindings[i].shaderStage);

			if (bindings[i].renderType == RenderType::RuntimeArray)
			{
				hasRuntimeArrayValue = true;
			}
		}

		Array<VkDescriptorBindingFlags> bindlessFlags{};

		if (hasRuntimeArrayValue)
		{
			bindlessFlags.Resize(bindings.Size());
			for (int i = 0; i < bindings.Size(); ++i)
			{
				if (bindings[i].renderType == RenderType::RuntimeArray)
				{
					bindlessFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
					//VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT ??
				}
			}
		}

		VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr};
		VkDescriptorSetLayoutCreateInfo                setLayoutCreateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

		setLayoutCreateInfo.bindingCount = vkBindings.Size();
		setLayoutCreateInfo.pBindings = vkBindings.Data();

		if (hasRuntimeArrayValue)
		{
			extendedInfo.bindingCount = bindlessFlags.Size();
			extendedInfo.pBindingFlags = bindlessFlags.Data();

			setLayoutCreateInfo.pNext = &extendedInfo;
			setLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
		}

		vkCreateDescriptorSetLayout(vkDevice, &setLayoutCreateInfo, nullptr, descriptorSetLayout);

		if (hasRuntimeArrayValue && hasRuntimeArrays)
		{
			*hasRuntimeArrays = hasRuntimeArrayValue;
		}
	}

	VkResult CreatePipelineLayout(VkDevice vkDevice, Span<DescriptorSetLayout> descriptors, Span<PushConstantRange> pushConstants, VkPipelineLayout* vkPipelineLayout)
	{
		Array<VkDescriptorSetLayout> descriptorSetLayouts{};
		descriptorSetLayouts.Resize(descriptors.Size());

		Array<VkPushConstantRange> pushConstantRanges{};

		for (const PushConstantRange& pushConstant : pushConstants)
		{
			pushConstantRanges.EmplaceBack(VkPushConstantRange{
				.stageFlags = ConvertShaderStageFlags(pushConstant.stages),
				.offset = pushConstant.offset,
				.size = pushConstant.size
			});
		}

		for (int i = 0; i < descriptors.Size(); ++i)
		{
			CreateDescriptorSetLayout(vkDevice, descriptors[i].bindings, &descriptorSetLayouts[i], nullptr);
		}

		VkPipelineLayoutCreateInfo layoutCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		layoutCreateInfo.setLayoutCount = descriptorSetLayouts.Size();
		layoutCreateInfo.pSetLayouts = descriptorSetLayouts.Data();
		layoutCreateInfo.pPushConstantRanges = pushConstantRanges.Data();
		layoutCreateInfo.pushConstantRangeCount = pushConstantRanges.Size();

		VkResult result = vkCreatePipelineLayout(vkDevice, &layoutCreateInfo, nullptr, vkPipelineLayout);

		for (int i = 0; i < descriptorSetLayouts.Size(); ++i)
		{
			vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayouts[i], nullptr);
		}

		return result;
	}


	VkBool32 DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
	                       VkDebugUtilsMessageTypeFlagsEXT             messageType,
	                       const VkDebugUtilsMessengerCallbackDataEXT* callbackDataExt,
	                       void*                                       userData)
	{
		switch (messageSeverity)
		{
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
				logger.Trace("{}", callbackDataExt->pMessage);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
				logger.Info("{}", callbackDataExt->pMessage);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
				logger.Warn("{}", callbackDataExt->pMessage);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
				logger.Error("{}", callbackDataExt->pMessage);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
				break;
		}

		return VK_FALSE;
	}

	VkFormat ToVkFormat(TextureFormat format)
	{
		switch (format)
		{
			case TextureFormat::Unknown: return VK_FORMAT_UNDEFINED;

			// 8-bit formats
			case TextureFormat::R8_UNORM: return VK_FORMAT_R8_UNORM;
			case TextureFormat::R8_SNORM: return VK_FORMAT_R8_SNORM;
			case TextureFormat::R8_UINT: return VK_FORMAT_R8_UINT;
			case TextureFormat::R8_SINT: return VK_FORMAT_R8_SINT;
			case TextureFormat::R8_SRGB: return VK_FORMAT_R8_SRGB;

			// 16-bit formats
			case TextureFormat::R16_UNORM: return VK_FORMAT_R16_UNORM;
			case TextureFormat::R16_SNORM: return VK_FORMAT_R16_SNORM;
			case TextureFormat::R16_UINT: return VK_FORMAT_R16_UINT;
			case TextureFormat::R16_SINT: return VK_FORMAT_R16_SINT;
			case TextureFormat::R16_FLOAT: return VK_FORMAT_R16_SFLOAT;
			case TextureFormat::R8G8_UNORM: return VK_FORMAT_R8G8_UNORM;
			case TextureFormat::R8G8_SNORM: return VK_FORMAT_R8G8_SNORM;
			case TextureFormat::R8G8_UINT: return VK_FORMAT_R8G8_UINT;
			case TextureFormat::R8G8_SINT: return VK_FORMAT_R8G8_SINT;
			case TextureFormat::R8G8_SRGB: return VK_FORMAT_R8G8_SRGB;
			case TextureFormat::R16G16B16_UNORM: return VK_FORMAT_R16G16B16_UNORM;
			case TextureFormat::R16G16B16_SNORM: return VK_FORMAT_R16G16B16_SNORM;
			case TextureFormat::R16G16B16_UINT: return VK_FORMAT_R16G16B16_UINT;
			case TextureFormat::R16G16B16_SINT: return VK_FORMAT_R16G16B16_SINT;
			case TextureFormat::R16G16B16_FLOAT: return VK_FORMAT_R16G16B16_SFLOAT;

			// 32-bit formats
			case TextureFormat::R32_UINT: return VK_FORMAT_R32_UINT;
			case TextureFormat::R32_SINT: return VK_FORMAT_R32_SINT;
			case TextureFormat::R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
			case TextureFormat::R16G16_UNORM: return VK_FORMAT_R16G16_UNORM;
			case TextureFormat::R16G16_SNORM: return VK_FORMAT_R16G16_SNORM;
			case TextureFormat::R16G16_UINT: return VK_FORMAT_R16G16_UINT;
			case TextureFormat::R16G16_SINT: return VK_FORMAT_R16G16_SINT;
			case TextureFormat::R16G16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
			case TextureFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
			case TextureFormat::R8G8B8A8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
			case TextureFormat::R8G8B8A8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
			case TextureFormat::R8G8B8A8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
			case TextureFormat::R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
			case TextureFormat::B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
			case TextureFormat::B8G8R8A8_SNORM: return VK_FORMAT_B8G8R8A8_SNORM;
			case TextureFormat::B8G8R8A8_UINT: return VK_FORMAT_B8G8R8A8_UINT;
			case TextureFormat::B8G8R8A8_SINT: return VK_FORMAT_B8G8R8A8_SINT;
			case TextureFormat::B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
			case TextureFormat::R10G10B10A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			case TextureFormat::R10G10B10A2_UINT: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
			case TextureFormat::R11G11B10_FLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			case TextureFormat::R9G9B9E5_FLOAT: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;

			// 64-bit formats
			case TextureFormat::R32G32_UINT: return VK_FORMAT_R32G32_UINT;
			case TextureFormat::R32G32_SINT: return VK_FORMAT_R32G32_SINT;
			case TextureFormat::R32G32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
			case TextureFormat::R16G16B16A16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
			case TextureFormat::R16G16B16A16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
			case TextureFormat::R16G16B16A16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
			case TextureFormat::R16G16B16A16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
			case TextureFormat::R16G16B16A16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;

			// 96-bit formats
			case TextureFormat::R32G32B32_UINT: return VK_FORMAT_R32G32B32_UINT;
			case TextureFormat::R32G32B32_SINT: return VK_FORMAT_R32G32B32_SINT;
			case TextureFormat::R32G32B32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;

			// 128-bit formats
			case TextureFormat::R32G32B32A32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
			case TextureFormat::R32G32B32A32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
			case TextureFormat::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;

			// Depth/stencil formats
			case TextureFormat::D16_UNORM: return VK_FORMAT_D16_UNORM;
			case TextureFormat::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
			case TextureFormat::D32_FLOAT: return VK_FORMAT_D32_SFLOAT;
			case TextureFormat::D32_FLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;

			// BC compressed formats
			case TextureFormat::BC1_UNORM: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
			case TextureFormat::BC1_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
			case TextureFormat::BC2_UNORM: return VK_FORMAT_BC2_UNORM_BLOCK;
			case TextureFormat::BC2_SRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
			case TextureFormat::BC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
			case TextureFormat::BC3_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
			case TextureFormat::BC4_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
			case TextureFormat::BC4_SNORM: return VK_FORMAT_BC4_SNORM_BLOCK;
			case TextureFormat::BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
			case TextureFormat::BC5_SNORM: return VK_FORMAT_BC5_SNORM_BLOCK;
			case TextureFormat::BC6H_UF16: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
			case TextureFormat::BC6H_SF16: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
			case TextureFormat::BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
			case TextureFormat::BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;

			// ETC compressed formats
			case TextureFormat::ETC1_UNORM: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK; // ETC1 is supported through ETC2 in Vulkan
			case TextureFormat::ETC2_UNORM: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
			case TextureFormat::ETC2_SRGB: return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
			case TextureFormat::ETC2A_UNORM: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
			case TextureFormat::ETC2A_SRGB: return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;

			// ASTC compressed formats
			case TextureFormat::ASTC_4x4_UNORM: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
			case TextureFormat::ASTC_4x4_SRGB: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
			case TextureFormat::ASTC_5x4_UNORM: return VK_FORMAT_ASTC_5x4_UNORM_BLOCK;
			case TextureFormat::ASTC_5x4_SRGB: return VK_FORMAT_ASTC_5x4_SRGB_BLOCK;
			case TextureFormat::ASTC_5x5_UNORM: return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
			case TextureFormat::ASTC_5x5_SRGB: return VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
			case TextureFormat::ASTC_6x5_UNORM: return VK_FORMAT_ASTC_6x5_UNORM_BLOCK;
			case TextureFormat::ASTC_6x5_SRGB: return VK_FORMAT_ASTC_6x5_SRGB_BLOCK;
			case TextureFormat::ASTC_6x6_UNORM: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
			case TextureFormat::ASTC_6x6_SRGB: return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
			case TextureFormat::ASTC_8x5_UNORM: return VK_FORMAT_ASTC_8x5_UNORM_BLOCK;
			case TextureFormat::ASTC_8x5_SRGB: return VK_FORMAT_ASTC_8x5_SRGB_BLOCK;
			case TextureFormat::ASTC_8x6_UNORM: return VK_FORMAT_ASTC_8x6_UNORM_BLOCK;
			case TextureFormat::ASTC_8x6_SRGB: return VK_FORMAT_ASTC_8x6_SRGB_BLOCK;
			case TextureFormat::ASTC_8x8_UNORM: return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
			case TextureFormat::ASTC_8x8_SRGB: return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
			case TextureFormat::ASTC_10x5_UNORM: return VK_FORMAT_ASTC_10x5_UNORM_BLOCK;
			case TextureFormat::ASTC_10x5_SRGB: return VK_FORMAT_ASTC_10x5_SRGB_BLOCK;
			case TextureFormat::ASTC_10x6_UNORM: return VK_FORMAT_ASTC_10x6_UNORM_BLOCK;
			case TextureFormat::ASTC_10x6_SRGB: return VK_FORMAT_ASTC_10x6_SRGB_BLOCK;
			case TextureFormat::ASTC_10x8_UNORM: return VK_FORMAT_ASTC_10x8_UNORM_BLOCK;
			case TextureFormat::ASTC_10x8_SRGB: return VK_FORMAT_ASTC_10x8_SRGB_BLOCK;
			case TextureFormat::ASTC_10x10_UNORM: return VK_FORMAT_ASTC_10x10_UNORM_BLOCK;
			case TextureFormat::ASTC_10x10_SRGB: return VK_FORMAT_ASTC_10x10_SRGB_BLOCK;
			case TextureFormat::ASTC_12x10_UNORM: return VK_FORMAT_ASTC_12x10_UNORM_BLOCK;
			case TextureFormat::ASTC_12x10_SRGB: return VK_FORMAT_ASTC_12x10_SRGB_BLOCK;
			case TextureFormat::ASTC_12x12_UNORM: return VK_FORMAT_ASTC_12x12_UNORM_BLOCK;
			case TextureFormat::ASTC_12x12_SRGB: return VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
			default: return VK_FORMAT_UNDEFINED;
		}
	}

	TextureFormat ToTextureFormat(VkFormat format)
	{
		switch (format)
		{
			case VK_FORMAT_UNDEFINED:
				return TextureFormat::Unknown;

			// 8-bit formats
			case VK_FORMAT_R8_UNORM:
				return TextureFormat::R8_UNORM;
			case VK_FORMAT_R8_SNORM:
				return TextureFormat::R8_SNORM;
			case VK_FORMAT_R8_UINT:
				return TextureFormat::R8_UINT;
			case VK_FORMAT_R8_SINT:
				return TextureFormat::R8_SINT;
			case VK_FORMAT_R8_SRGB:
				return TextureFormat::R8_SRGB;

			// 16-bit formats
			case VK_FORMAT_R16_UNORM:
				return TextureFormat::R16_UNORM;
			case VK_FORMAT_R16_SNORM:
				return TextureFormat::R16_SNORM;
			case VK_FORMAT_R16_UINT:
				return TextureFormat::R16_UINT;
			case VK_FORMAT_R16_SINT:
				return TextureFormat::R16_SINT;
			case VK_FORMAT_R16_SFLOAT:
				return TextureFormat::R16_FLOAT;
			case VK_FORMAT_R8G8_UNORM:
				return TextureFormat::R8G8_UNORM;
			case VK_FORMAT_R8G8_SNORM:
				return TextureFormat::R8G8_SNORM;
			case VK_FORMAT_R8G8_UINT:
				return TextureFormat::R8G8_UINT;
			case VK_FORMAT_R8G8_SINT:
				return TextureFormat::R8G8_SINT;
			case VK_FORMAT_R8G8_SRGB:
				return TextureFormat::R8G8_SRGB;

			// 32-bit formats
			case VK_FORMAT_R32_UINT:
				return TextureFormat::R32_UINT;
			case VK_FORMAT_R32_SINT:
				return TextureFormat::R32_SINT;
			case VK_FORMAT_R32_SFLOAT:
				return TextureFormat::R32_FLOAT;
			case VK_FORMAT_R16G16_UNORM:
				return TextureFormat::R16G16_UNORM;
			case VK_FORMAT_R16G16_SNORM:
				return TextureFormat::R16G16_SNORM;
			case VK_FORMAT_R16G16_UINT:
				return TextureFormat::R16G16_UINT;
			case VK_FORMAT_R16G16_SINT:
				return TextureFormat::R16G16_SINT;
			case VK_FORMAT_R16G16_SFLOAT:
				return TextureFormat::R16G16_FLOAT;
			case VK_FORMAT_R8G8B8A8_UNORM:
				return TextureFormat::R8G8B8A8_UNORM;
			case VK_FORMAT_R8G8B8A8_SNORM:
				return TextureFormat::R8G8B8A8_SNORM;
			case VK_FORMAT_R8G8B8A8_UINT:
				return TextureFormat::R8G8B8A8_UINT;
			case VK_FORMAT_R8G8B8A8_SINT:
				return TextureFormat::R8G8B8A8_SINT;
			case VK_FORMAT_R8G8B8A8_SRGB:
				return TextureFormat::R8G8B8A8_SRGB;
			case VK_FORMAT_B8G8R8A8_UNORM:
				return TextureFormat::B8G8R8A8_UNORM;
			case VK_FORMAT_B8G8R8A8_SNORM:
				return TextureFormat::B8G8R8A8_SNORM;
			case VK_FORMAT_B8G8R8A8_UINT:
				return TextureFormat::B8G8R8A8_UINT;
			case VK_FORMAT_B8G8R8A8_SINT:
				return TextureFormat::B8G8R8A8_SINT;
			case VK_FORMAT_B8G8R8A8_SRGB:
				return TextureFormat::B8G8R8A8_SRGB;
			case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
				return TextureFormat::R10G10B10A2_UNORM;
			case VK_FORMAT_A2B10G10R10_UINT_PACK32:
				return TextureFormat::R10G10B10A2_UINT;
			case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
				return TextureFormat::R11G11B10_FLOAT;
			case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
				return TextureFormat::R9G9B9E5_FLOAT;

			// 64-bit formats
			case VK_FORMAT_R32G32_UINT:
				return TextureFormat::R32G32_UINT;
			case VK_FORMAT_R32G32_SINT:
				return TextureFormat::R32G32_SINT;
			case VK_FORMAT_R32G32_SFLOAT:
				return TextureFormat::R32G32_FLOAT;
			case VK_FORMAT_R16G16B16A16_UNORM:
				return TextureFormat::R16G16B16A16_UNORM;
			case VK_FORMAT_R16G16B16A16_SNORM:
				return TextureFormat::R16G16B16A16_SNORM;
			case VK_FORMAT_R16G16B16A16_UINT:
				return TextureFormat::R16G16B16A16_UINT;
			case VK_FORMAT_R16G16B16A16_SINT:
				return TextureFormat::R16G16B16A16_SINT;
			case VK_FORMAT_R16G16B16A16_SFLOAT:
				return TextureFormat::R16G16B16A16_FLOAT;

			// 96-bit formats
			case VK_FORMAT_R32G32B32_UINT:
				return TextureFormat::R32G32B32_UINT;
			case VK_FORMAT_R32G32B32_SINT:
				return TextureFormat::R32G32B32_SINT;
			case VK_FORMAT_R32G32B32_SFLOAT:
				return TextureFormat::R32G32B32_FLOAT;

			// 128-bit formats
			case VK_FORMAT_R32G32B32A32_UINT:
				return TextureFormat::R32G32B32A32_UINT;
			case VK_FORMAT_R32G32B32A32_SINT:
				return TextureFormat::R32G32B32A32_SINT;
			case VK_FORMAT_R32G32B32A32_SFLOAT:
				return TextureFormat::R32G32B32A32_FLOAT;

			// Depth/stencil formats
			case VK_FORMAT_D16_UNORM:
				return TextureFormat::D16_UNORM;
			case VK_FORMAT_D24_UNORM_S8_UINT:
				return TextureFormat::D24_UNORM_S8_UINT;
			case VK_FORMAT_D32_SFLOAT:
				return TextureFormat::D32_FLOAT;
			case VK_FORMAT_D32_SFLOAT_S8_UINT:
				return TextureFormat::D32_FLOAT_S8_UINT;

			// BC compressed formats
			case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
				return TextureFormat::BC1_UNORM;
			case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
				return TextureFormat::BC1_SRGB;
			case VK_FORMAT_BC2_UNORM_BLOCK:
				return TextureFormat::BC2_UNORM;
			case VK_FORMAT_BC2_SRGB_BLOCK:
				return TextureFormat::BC2_SRGB;
			case VK_FORMAT_BC3_UNORM_BLOCK:
				return TextureFormat::BC3_UNORM;
			case VK_FORMAT_BC3_SRGB_BLOCK:
				return TextureFormat::BC3_SRGB;
			case VK_FORMAT_BC4_UNORM_BLOCK:
				return TextureFormat::BC4_UNORM;
			case VK_FORMAT_BC4_SNORM_BLOCK:
				return TextureFormat::BC4_SNORM;
			case VK_FORMAT_BC5_UNORM_BLOCK:
				return TextureFormat::BC5_UNORM;
			case VK_FORMAT_BC5_SNORM_BLOCK:
				return TextureFormat::BC5_SNORM;
			case VK_FORMAT_BC6H_UFLOAT_BLOCK:
				return TextureFormat::BC6H_UF16;
			case VK_FORMAT_BC6H_SFLOAT_BLOCK:
				return TextureFormat::BC6H_SF16;
			case VK_FORMAT_BC7_UNORM_BLOCK:
				return TextureFormat::BC7_UNORM;
			case VK_FORMAT_BC7_SRGB_BLOCK:
				return TextureFormat::BC7_SRGB;

			// ETC compressed formats
			case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
				return TextureFormat::ETC2_UNORM;
			case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
				return TextureFormat::ETC2_SRGB;
			case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
				return TextureFormat::ETC2A_UNORM;
			case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
				return TextureFormat::ETC2A_SRGB;

			// ASTC compressed formats
			case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
				return TextureFormat::ASTC_4x4_UNORM;
			case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
				return TextureFormat::ASTC_4x4_SRGB;
			case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
				return TextureFormat::ASTC_5x4_UNORM;
			case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
				return TextureFormat::ASTC_5x4_SRGB;
			case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
				return TextureFormat::ASTC_5x5_UNORM;
			case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
				return TextureFormat::ASTC_5x5_SRGB;
			case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
				return TextureFormat::ASTC_6x5_UNORM;
			case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
				return TextureFormat::ASTC_6x5_SRGB;
			case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
				return TextureFormat::ASTC_6x6_UNORM;
			case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
				return TextureFormat::ASTC_6x6_SRGB;
			case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
				return TextureFormat::ASTC_8x5_UNORM;
			case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
				return TextureFormat::ASTC_8x5_SRGB;
			case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
				return TextureFormat::ASTC_8x6_UNORM;
			case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
				return TextureFormat::ASTC_8x6_SRGB;
			case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
				return TextureFormat::ASTC_8x8_UNORM;
			case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
				return TextureFormat::ASTC_8x8_SRGB;
			case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
				return TextureFormat::ASTC_10x5_UNORM;
			case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
				return TextureFormat::ASTC_10x5_SRGB;
			case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
				return TextureFormat::ASTC_10x6_UNORM;
			case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
				return TextureFormat::ASTC_10x6_SRGB;
			case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
				return TextureFormat::ASTC_10x8_UNORM;
			case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
				return TextureFormat::ASTC_10x8_SRGB;
			case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
				return TextureFormat::ASTC_10x10_UNORM;
			case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
				return TextureFormat::ASTC_10x10_SRGB;
			case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
				return TextureFormat::ASTC_12x10_UNORM;
			case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
				return TextureFormat::ASTC_12x10_SRGB;
			case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
				return TextureFormat::ASTC_12x12_UNORM;
			case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
				return TextureFormat::ASTC_12x12_SRGB;

			default:
				return TextureFormat::Unknown;
		}
	}

	VulkanSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
	{
		VulkanSwapChainSupportDetails details;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
		u32 formatCount{};
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
		if (formatCount != 0)
		{
			details.formats.Resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.Data());
		}
		u32 presentModeCount{};
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
		if (presentModeCount != 0)
		{
			details.presentModes.Resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.Data());
		}
		return details;
	}

	VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const VulkanSwapChainSupportDetails& supportDetails, VkSurfaceFormatKHR desiredFormat)
	{
		for (const auto& availableFormat : supportDetails.formats)
		{
			if (availableFormat.format == desiredFormat.format && availableFormat.colorSpace == desiredFormat.colorSpace)
			{
				return availableFormat;
			}
		}
		return supportDetails.formats[0];
	}

	VkPresentModeKHR ChooseSwapPresentMode(const VulkanSwapChainSupportDetails& supportDetails, VkPresentModeKHR desiredPresentMode)
	{
		for (const auto& availablePresentMode : supportDetails.presentModes)
		{
			if (availablePresentMode == desiredPresentMode)
			{
				return availablePresentMode;
			}
		}
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D ChooseSwapExtent(const VulkanSwapChainSupportDetails& supportDetails, Extent extent)
	{
		if (supportDetails.capabilities.currentExtent.width != U32_MAX)
		{
			return supportDetails.capabilities.currentExtent;
		}

		VkExtent2D actualExtent = {extent.width, extent.height};
		actualExtent.width = std::clamp(actualExtent.width, supportDetails.capabilities.minImageExtent.width, supportDetails.capabilities.maxImageExtent.width);
		actualExtent.height = std::clamp(actualExtent.height, supportDetails.capabilities.minImageExtent.height, supportDetails.capabilities.maxImageExtent.height);
		return actualExtent;
	}

	bool QueryLayerProperties(const Span<const char*>& requiredLayers)
	{
		u32 extensionCount = 0;
		vkEnumerateInstanceLayerProperties(&extensionCount, nullptr);
		Array<VkLayerProperties> extensions(extensionCount);
		vkEnumerateInstanceLayerProperties(&extensionCount, extensions.Data());

		for (const StringView& reqExtension : requiredLayers)
		{
			bool found = false;
			for (const auto& layer : extensions)
			{
				if (layer.layerName == StringView(reqExtension))
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				return false;
			}
		}

		return true;
	}

	bool QueryDeviceExtensions(const Span<VkExtensionProperties>& extensions, const StringView& checkForExtension)
	{
		for (const VkExtensionProperties& extension : extensions)
		{
			if (StringView(extension.extensionName) == checkForExtension)
			{
				return true;
			}
		}
		return false;
	}

	HashSet<String> GetDeviceExtensions(VkPhysicalDevice device)
	{
		u32 extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
		Array<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.Data());

		HashSet<String> ret;
		for (u32 i = 0; i < extensionCount; ++i)
		{
			ret.Insert(availableExtensions[i].extensionName);
		}
		return ret;
	}

	bool QueryInstanceExtensions(const Span<const char*>& requiredExtensions)
	{
		u32 extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		Array<VkExtensionProperties> extensions(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.Data());

		for (const auto reqExtension : requiredExtensions)
		{
			bool found = false;
			for (const auto& extension : extensions)
			{
				if (StringView(extension.extensionName) == StringView(reqExtension))
				{
					found = true;
				}
			}

			if (!found)
			{
				return false;
			}
		}
		return true;
	}

	VkAccessFlags GetAccessFlagsFromResourceState(ResourceState state)
	{
		switch (state)
		{
			case ResourceState::Undefined:
				return 0;

			case ResourceState::General:
				return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

			case ResourceState::ColorAttachment:
				return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			case ResourceState::DepthStencilAttachment:
				return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			case ResourceState::DepthStencilReadOnly:
				return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

			case ResourceState::ShaderReadOnly:
				return VK_ACCESS_SHADER_READ_BIT;

			case ResourceState::CopyDest:
				return VK_ACCESS_TRANSFER_WRITE_BIT;

			case ResourceState::CopySource:
				return VK_ACCESS_TRANSFER_READ_BIT;

			case ResourceState::Present:
				return 0; // No access needed for present

			default:
				SK_ASSERT(false, "Unhandled resource state in GetAccessFlagsFromResourceState");
				return 0;
		}
	}

	VkPipelineStageFlags GetPipelineStageFromResourceState(ResourceState state)
	{
		switch (state)
		{
			case ResourceState::Undefined:
				return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

			case ResourceState::General:
				return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

			case ResourceState::ColorAttachment:
				return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

			case ResourceState::DepthStencilAttachment:
			case ResourceState::DepthStencilReadOnly:
				return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

			case ResourceState::ShaderReadOnly:
				return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			case ResourceState::CopyDest:
			case ResourceState::CopySource:
				return VK_PIPELINE_STAGE_TRANSFER_BIT;

			case ResourceState::Present:
				return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

			default:
				SK_ASSERT(false, "Unhandled resource state in GetPipelineStageFromResourceState");
				return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		}
	}

	bool GetShaderInfoFromResource(RID rid, PipelineDesc* pipelineDesc, Array<ShaderStageInfo>* stages)
	{
		ResourceObject variantObject = Resources::Read(rid);
		if (!variantObject)
		{
			SK_ASSERT(false, "shader variant instance not found");
			return false;
		}

		if (pipelineDesc)
		{
			if (RID rid = variantObject.GetSubObject(ShaderVariantResource::PipelineDesc))
			{
				Resources::FromResource(rid, pipelineDesc);
			}
			else
			{
				SK_ASSERT(false, "pipeline desc not found");
				return false;
			}
		}

		if (stages)
		{
			variantObject.IterateSubObjectSet(ShaderVariantResource::Stages, true, [&](RID rid)
			{
				ShaderStageInfo stageInfo;
				Resources::FromResource(rid, &stageInfo);
				stages->EmplaceBack(stageInfo);

				return true;
			});

			if (stages->Empty())
			{
				SK_ASSERT(false, "stages not found");
				return false;
			}
		}
		return true;
	}
}
