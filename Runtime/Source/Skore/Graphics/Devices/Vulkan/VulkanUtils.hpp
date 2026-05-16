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
	void                          CreateDescriptorSetLayout(VkDevice vkDevice, Span<DescriptorSetLayoutBinding> bindings, VkDescriptorSetLayout* descriptorSetLayout, bool* hasRuntimeArrays, bool pushDescriptor = false);
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
	VkSampleCountFlagBits         CastSampleCount(u32 sampleCount);
	u32														CastSampleCount(VkSampleCountFlagBits sampleCount);
	VkSampleCountFlagBits					GetMaxUsableSampleCount(const VkPhysicalDeviceProperties& properties);

	VkResult CreatePipelineLayout(VkDevice                      vkDevice,
	                              Span<DescriptorSetLayout>     descriptors,
	                              Span<PushConstantRange>       pushConstants,
	                              VkPipelineLayout*             vkPipelineLayout,
	                              Array<VkDescriptorSetLayout>& descriptorSetLayouts,
	                              bool                          pushDescriptor = false,
	                              Span<DescriptorSetOverride>   overrides = {});

	VkBuildAccelerationStructureFlagsKHR ConvertBuildASFlags(BuildAccelerationStructureFlags flags);
	VkDeviceAddress                      GetBufferDeviceAddress(VkDevice device, VkBuffer buffer);
}
