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

#pragma once

#include "volk.h"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"

namespace Skore
{
	class VulkanDevice;

	struct VulkanBaseInStructure
	{
		VkStructureType sType;
		VoidPtr         pNext;
	};

	struct VulkanSwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR  capabilities{};
		Array<VkSurfaceFormatKHR> formats{};
		Array<VkPresentModeKHR>   presentModes{};
	};

	VkBool32 DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
	                       VkDebugUtilsMessageTypeFlagsEXT             messageType,
	                       const VkDebugUtilsMessengerCallbackDataEXT* callbackDataExt,
	                       void*                                       userData);

	VkBufferUsageFlags            GetBufferUsageFlags(ResourceUsage usage, bool supportBufferDeviceAddress);
	VkImageUsageFlags             GetImageUsageFlags(ResourceUsage usage);
	VkImageAspectFlags            GetImageAspectFlags(VkFormat format);
	bool                          IsDepthFormat(VkFormat format);
	VkImageViewType               GetImageViewType(TextureViewType viewType);
	VkSamplerAddressMode          ConvertAddressMode(AddressMode mode);
	VkCompareOp                   ConvertCompareOp(CompareOp op);
	VkBorderColor                 ConvertBorderColor(BorderColor color);
	VkDescriptorType              ConvertDescriptorType(DescriptorType type);
	VkImageLayout                 CastState(const ResourceState& resourceLayout, VkImageLayout defaultUndefined = VK_IMAGE_LAYOUT_UNDEFINED);
	VkAttachmentLoadOp            CastLoadOp(AttachmentLoadOp loadOp);
	VkAttachmentStoreOp           CastStoreOp(AttachmentStoreOp storeOp);
	VkShaderStageFlags            ConvertShaderStageFlags(ShaderStage stages);
	VkPrimitiveTopology           ConvertPrimitiveTopology(PrimitiveTopology topology);
	VkPolygonMode                 ConvertPolygonMode(PolygonMode mode);
	VkCullModeFlags               ConvertCullMode(CullMode mode);
	VkFrontFace                   ConvertFrontFace(FrontFace frontFace);
	VkBlendFactor                 ConvertBlendFactor(BlendFactor factor);
	VkBlendOp                     ConvertBlendOp(BlendOp op);
	VkStencilOp                   ConvertStencilOp(StencilOp op);
	VkQueryType                   ConvertQueryType(QueryType type);
	void                          SetObjectName(VulkanDevice& device, VkObjectType type, u64 handle, StringView name);
	void                          CreateDescriptorSetLayout(VkDevice vkDevice, Span<DescriptorSetLayoutBinding> bindings, VkDescriptorSetLayout* descriptorSetLayout, bool* hasRuntimeArrays);
	VkResult                      CreatePipelineLayout(VkDevice vkDevice, Span<DescriptorSetLayout> descriptors, Span<PushConstantRange> pushConstants, VkPipelineLayout* vkPipelineLayout);
	VkFormat                      ToVkFormat(TextureFormat format);
	TextureFormat                 ToTextureFormat(VkFormat format);
	VulkanSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
	VkSurfaceFormatKHR            ChooseSwapSurfaceFormat(const VulkanSwapChainSupportDetails& supportDetails, VkSurfaceFormatKHR desiredFormat);
	VkPresentModeKHR              ChooseSwapPresentMode(const VulkanSwapChainSupportDetails& supportDetails, VkPresentModeKHR desiredPresentMode);
	VkExtent2D                    ChooseSwapExtent(const VulkanSwapChainSupportDetails& supportDetails, Extent extent);
	bool                          QueryLayerProperties(const Span<const char*>& requiredLayers);
	bool                          QueryDeviceExtensions(const Span<VkExtensionProperties>& extensions, const StringView& checkForExtension);
	HashSet<String>               GetDeviceExtensions(VkPhysicalDevice device);
	bool                          QueryInstanceExtensions(const Span<const char*>& requiredExtensions);
	VkAccessFlags                 GetAccessFlagsFromResourceState(ResourceState state);
	VkPipelineStageFlags          GetPipelineStageFromResourceState(ResourceState state);
	bool                          GetShaderInfoFromResource(RID rid, PipelineDesc* pipelineDesc, Array<ShaderStageInfo>* stages);
}
