#include "Skore/Graphics/Devices/Vulkan/VulkanDevice.hpp"

#define VMA_ASSERT_LEAK(expr)

#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION

#include <iostream>
#include <queue>

#include "vk_mem_alloc.h"
#include "Skore/Graphics/Devices/Vulkan/VulkanUtils.hpp"

#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/Resources.hpp"

#include "vulkan/vk_enum_string_helper.h"

#include <concurrentqueue.h>

#include "Skore/App.hpp"
#include "Skore/Events.hpp"
#include "Skore/OpenXRManager.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"

#include "Skore/Graphics/Devices/Vulkan/VulkanPlatform.hpp"


namespace Skore
{
	struct VulkanGraphicsRequirements
	{
		u64 minApiVersionSupported = U64_MAX;
		u64 maxApiVersionSupported = U64_MAX;
	};

	Array<String>              OpenXRManagerGetInstanceExtensions();
	VkPhysicalDevice           OpenXRManagerGetPhysicalDevice(VkInstance vulkanInstance);
	Array<String>              OpenXRManagerGetDeviceExtensions();
	VulkanGraphicsRequirements OpenXRManagerGetGraphicsRequirements();

	//huge object, maybe decrease a bit?
	struct VulkanResourceDestructor
	{
		u64                          frame = 0;
		VulkanTexture*               texture = nullptr;
		VulkanTextureView*           textureView = nullptr;
		VkBuffer                     buffer = nullptr;
		VmaAllocation                allocation = nullptr;
		VkDescriptorSet              descriptorSet = nullptr;
		Array<VkDescriptorSetLayout> descriptorSetLayouts;
		VkDescriptorPool             descriptorPool = nullptr;
		VkPipeline                   pipeline = nullptr;
		VkPipelineLayout             pipelineLayout = nullptr;
		VkSampler                    sampler = nullptr;
		VkRenderPass                 renderPass = nullptr;
		VkFramebuffer                framebuffer = nullptr;
		VkQueryPool                  queryPool = nullptr;
		VkAccelerationStructureKHR   accelerationStructure = nullptr;
	};

	namespace
	{
		const char*              validationLayers[1] = {"VK_LAYER_KHRONOS_validation"};
		VkDebugUtilsMessengerEXT debugUtilsMessengerExt{};
		Logger&                  logger = Logger::GetLogger("Skore::Vulkan");

		moodycamel::ConcurrentQueue<VulkanResourceDestructor> destructorQueues[SK_FRAMES_IN_FLIGHT] = {
			moodycamel::ConcurrentQueue<VulkanResourceDestructor>(50),
			moodycamel::ConcurrentQueue<VulkanResourceDestructor>(50)
		};

		void EnqueueDestructor(VulkanResourceDestructor destructor)
		{
			destructor.frame = App::Frame() % SK_FRAMES_IN_FLIGHT;
			destructorQueues[destructor.frame].enqueue(destructor);
		}
	}

	void VulkanAdapter::RatePhysicalDevice(VulkanDevice* vulkanDevice)
	{
		score += deviceProperties.properties.limits.maxImageDimension2D / 1024;

		if (deviceProperties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			score += 3000;
		}
		else if (deviceProperties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
		{
			score += 500;
		}

		u32 queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		Array<VkQueueFamilyProperties> queueFamilies;
		queueFamilies.Resize(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.Data());

		bool hasGraphicsQueue = false;
		bool hasComputeQueue = false;
		bool hasTransferQueue = false;
		bool hasPresentQueue = false;

		for (u32 i = 0; i < queueFamilyCount; ++i)
		{
			const auto& queueFamily = queueFamilies[i];
			bool        hasPresentFamily = Platform::GetPhysicalDevicePresentationSupport(vulkanDevice->instance, device, i);
			bool        hasGraphicsFamily = queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT;

			if (hasGraphicsFamily && graphicsFamily == U32_MAX)
			{
				graphicsFamily = i;
			}

			//if the device has a dedicated family for present, use it.
			//if (hasPresentFamily && (presentFamily == U32_MAX || !hasGraphicsFamily))
			if (hasPresentFamily && presentFamily == U32_MAX)
			{
				presentFamily = i;
			}

			//score device by queue
			if (hasPresentFamily)
			{
				hasPresentQueue = true;
			}

			if (hasGraphicsFamily)
			{
				hasGraphicsQueue = true;
			}
			if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
			{
				hasComputeQueue = true;
			}
			if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
			{
				hasTransferQueue = true;
			}
		}

		//dedicated async compute: has compute but no graphics
		for (u32 i = 0; i < queueFamilyCount; ++i)
		{
			const auto& queueFamily = queueFamilies[i];
			if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
			{
				computeFamily = i;
				break;
			}
		}
		if (computeFamily == U32_MAX)
		{
			computeFamily = graphicsFamily;
		}

		//dedicated transfer: has transfer but no graphics and no compute
		for (u32 i = 0; i < queueFamilyCount; ++i)
		{
			const auto& queueFamily = queueFamilies[i];
			if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
				&& !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
				&& !(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT))
			{
				transferFamily = i;
				break;
			}
		}
		if (transferFamily == U32_MAX)
		{
			transferFamily = graphicsFamily;
		}

		if (hasComputeQueue)
		{
			score += 100;
		}
		if (hasTransferQueue)
		{
			score += 100;
		}

		if (!hasGraphicsQueue || !hasPresentQueue)
		{
			score = 0;
		}
	}

	const RenderPassDesc& VulkanRenderPass::GetDesc() const
	{
		return desc;
	}

	void VulkanRenderPass::Destroy()
	{
		VulkanResourceDestructor destructor;
		destructor.renderPass = renderPass;
		EnqueueDestructor(destructor);
		DestroyAndFree(this);
	}

	const FramebufferDesc& VulkanFramebuffer::GetDesc() const
	{
		return desc;
	}

	void VulkanFramebuffer::Destroy()
	{
		VulkanResourceDestructor destructor;
		destructor.framebuffer = framebuffer;
		EnqueueDestructor(destructor);
		DestroyAndFree(this);
	}

	Extent VulkanFramebuffer::GetExtent()
	{
		return extent;
	}

	const SwapchainDesc& VulkanSwapchain::GetDesc() const
	{
		return desc;
	}

	DeviceResult VulkanSwapchain::AcquireNextImage()
	{
		VkResult result = vkAcquireNextImageKHR(vulkanDevice->device, swapchainKHR, UINT64_MAX, imageAvailableSemaphores[vulkanDevice->currentFrame], VK_NULL_HANDLE, &imageIndex);
		if (result != VK_SUCCESS)
		{
			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			{
				return DeviceResult::SwapchainOutOfDate;
			}
			logger.Error("failed to acquire swap chain image! {}", string_VkResult(result));
			return DeviceResult::Error;
		}
		return DeviceResult::Success;
	}

	u32 VulkanSwapchain::GetCurrentImageIndex() const
	{
		return imageIndex;
	}

	Extent VulkanSwapchain::GetExtent()
	{
		return {extent.width, extent.height};
	}

	void VulkanSwapchain::Destroy()
	{
		vulkanDevice->swapchains.Remove(this);
		DestroyInternal();
		DestroyAndFree(this);

	}

	u32 VulkanSwapchain::GetImageCount() const
	{
		return textures.Size();
	}

	TextureFormat VulkanSwapchain::GetFormat() const
	{
		return ToTextureFormat(format);
	}

	Span<GPUTexture*> VulkanSwapchain::GetTextures() const
	{
		return textures;
	}

	bool VulkanSwapchain::CreateInternal()
	{
		imageIndex = 0;

		VkResult res = VK_SUCCESS;

		if (!Platform::CreateWindowSurface(desc.window, vulkanDevice->instance, &surfaceKHR))
		{
			return false;
		}

		VulkanSwapChainSupportDetails details = QuerySwapChainSupport(vulkanDevice->selectedAdapter->device, surfaceKHR);
		VkSurfaceFormatKHR            surfaceFormat = ChooseSwapSurfaceFormat(details, {ToVkFormat(desc.desiredFormat), VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
		VkPresentModeKHR              presentMode = ChooseSwapPresentMode(details, desc.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR);
		extent = ChooseSwapExtent(details, Platform::GetWindowSize(desc.window));


		if (extent.width == 0 || extent.height == 0)
		{
			SK_ASSERT(false, "swapchain cannot be created with 0 width or height");
			return false;
		}

		logger.Debug("Swapchain created with extent {}, {} ", extent.width, extent.height);

		u32 imageCount = details.capabilities.minImageCount + 1;
		if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount)
		{
			imageCount = details.capabilities.maxImageCount;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(vulkanDevice->selectedAdapter->device, vulkanDevice->selectedAdapter->presentFamily, surfaceKHR, &presentSupport);
		if (!presentSupport)
		{
			logger.Error("PhysicalDeviceSurfaceSupportKHR not supported");
			return false;
		}

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surfaceKHR;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

		u32 queueFamilyIndices[] = {vulkanDevice->selectedAdapter->graphicsFamily, vulkanDevice->selectedAdapter->presentFamily};
		if (vulkanDevice->selectedAdapter->graphicsFamily != vulkanDevice->selectedAdapter->presentFamily)
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0;
			createInfo.pQueueFamilyIndices = nullptr;
		}

		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

		FixedArray<VkCompositeAlphaFlagBitsKHR, 4> compositeAlphaFlags = {
			VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		};
		for (auto& compositeAlphaFlag : compositeAlphaFlags)
		{
			if (details.capabilities.supportedCompositeAlpha & compositeAlphaFlag)
			{
				compositeAlpha = compositeAlphaFlag;
				break;
			}
		}

		createInfo.preTransform = details.capabilities.currentTransform;
		createInfo.compositeAlpha = compositeAlpha;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		res = vkCreateSwapchainKHR(vulkanDevice->device, &createInfo, nullptr, &swapchainKHR);
		if (res != VK_SUCCESS)
		{
			logger.Error("error on vkCreateSwapchainKHR {} ", string_VkResult(res));
			return false;
		}

		vkGetSwapchainImagesKHR(vulkanDevice->device, swapchainKHR, &imageCount, nullptr);

		format = surfaceFormat.format;
		textures.Resize(imageCount);

		Array<VkImage> vkImages;
		vkImages.Resize(imageCount);
		vkGetSwapchainImagesKHR(vulkanDevice->device, swapchainKHR, &imageCount, vkImages.Data());

		TextureDesc textureDesc;
		textureDesc.extent = {extent.width, extent.height, 1};
		textureDesc.format = ToTextureFormat(format);
		textureDesc.usage = ResourceUsage::RenderTarget;

		for (u32 i = 0; i < imageCount; ++i)
		{
			VulkanTexture* texture = Alloc<VulkanTexture>();
			texture->desc = textureDesc;
			texture->vulkanDevice = vulkanDevice;
			texture->allocation = nullptr;
			texture->image = vkImages[i];

			texture->textureView = Graphics::CreateTextureView({
				.texture = texture,
			});

			textures[i] = texture;
		}

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			vkCreateSemaphore(vulkanDevice->device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
		}

		renderFinishedSemaphores.Resize(imageCount);
		for (u32 i = 0; i < imageCount; ++i)
		{
			vkCreateSemaphore(vulkanDevice->device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
		}

		return true;
	}

	void VulkanSwapchain::DestroyInternal()
	{
		for (u32 i = 0; i < textures.Size(); ++i)
		{
			textures[i]->Destroy();
		}

		textures.Clear();

		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroySemaphore(vulkanDevice->device, imageAvailableSemaphores[i], nullptr);
		}

		for (u32 i = 0; i < renderFinishedSemaphores.Size(); ++i)
		{
			vkDestroySemaphore(vulkanDevice->device, renderFinishedSemaphores[i], nullptr);
		}

		vkDestroySwapchainKHR(vulkanDevice->device, swapchainKHR, nullptr);
		vkDestroySurfaceKHR(vulkanDevice->instance, surfaceKHR, nullptr);
	}

	void VulkanDescriptorSet::InternalUpdateTexture(u32 binding, GPUTexture* texture, GPUTextureView* textureView, u32 arrayElement)
	{
		DescriptorSetLayoutBinding& layout = desc.bindings[binding];

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = descriptorSet;
		write.dstBinding = binding;
		write.descriptorType = ConvertDescriptorType(layout.descriptorType);
		write.dstArrayElement = arrayElement;
		write.descriptorCount = 1;

		VkDescriptorImageInfo imageInfo{};
		bool                  depthFormat = false;

		if (texture)
		{
			VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(texture);
			imageInfo.imageView = vulkanTexture->GetImageView();
			depthFormat = vulkanTexture->isDepth;
		}
		else if (textureView)
		{
			VulkanTextureView* vulkanTextureView = static_cast<VulkanTextureView*>(textureView);
			depthFormat = vulkanTextureView->texture->isDepth;
			imageInfo.imageView = vulkanTextureView->imageView;
		}

		imageInfo.imageLayout = depthFormat
			                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
			                        : layout.descriptorType == DescriptorType::StorageImage
			                        ? VK_IMAGE_LAYOUT_GENERAL
			                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(vulkanDevice->device, 1, &write, 0, nullptr);
	}

	void VulkanCommandBuffer::Begin()
	{
		VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(commandBuffer, &beginInfo);
	}

	void VulkanCommandBuffer::End()
	{
		vkEndCommandBuffer(commandBuffer);
	}

	void VulkanCommandBuffer::Reset()
	{
		vkResetCommandBuffer(commandBuffer, 0);
	}

	void VulkanCommandBuffer::SetViewport(const ViewportInfo& viewportInfo)
	{
		VkViewport viewport{};
		viewport.x = viewportInfo.x;
		viewport.y = viewportInfo.y;
		viewport.width = viewportInfo.width;
		viewport.height = viewportInfo.height;
		viewport.minDepth = viewportInfo.minDepth;
		viewport.maxDepth = viewportInfo.maxDepth;
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	}

	void VulkanCommandBuffer::SetScissor(Vec2 position, Extent size)
	{
		VkRect2D scissor{};
		scissor.offset = {static_cast<int32_t>(position.x), static_cast<int32_t>(position.y)};
		scissor.extent = {(size.width), (size.height)};
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	void VulkanCommandBuffer::BindPipeline(GPUPipeline* pipeline)
	{
		VulkanPipeline* vulkanPipeline = static_cast<VulkanPipeline*>(pipeline);
		vkCmdBindPipeline(commandBuffer, vulkanPipeline->bindPoint, vulkanPipeline->pipeline);
	}

	void VulkanCommandBuffer::BindDescriptorSet(GPUPipeline* pipeline, u32 setIndex, GPUDescriptorSet* descriptorSet, Span<u32> dynamicOffsets)
	{
		VulkanPipeline* vulkanPipeline = static_cast<VulkanPipeline*>(pipeline);
		vkCmdBindDescriptorSets(
			commandBuffer,
			vulkanPipeline->bindPoint,
			vulkanPipeline->pipelineLayout,
			setIndex,
			1,
			&static_cast<VulkanDescriptorSet*>(descriptorSet)->descriptorSet,
			dynamicOffsets.Size(),
			dynamicOffsets.Data()
		);
	}

	void VulkanCommandBuffer::BindVertexBuffer(u32 firstBinding, GPUBuffer* buffers, usize offset)
	{
		vkCmdBindVertexBuffers(commandBuffer, firstBinding, 1, &static_cast<VulkanBuffer*>(buffers)->buffer, &offset);
	}

	void VulkanCommandBuffer::BindIndexBuffer(GPUBuffer* buffer, usize offset, IndexType indexType)
	{
		VkIndexType vkIndexType = VK_INDEX_TYPE_UINT32;

		if (indexType == IndexType::Uint16)
		{
			vkIndexType = VK_INDEX_TYPE_UINT16;
		}
		vkCmdBindIndexBuffer(commandBuffer, static_cast<VulkanBuffer*>(buffer)->buffer, offset, vkIndexType);
	}

	static DescriptorSetLayoutBinding* FindDescriptorBinding(VulkanPipeline* vulkanPipeline, u32 set, u32 binding)
	{
		for (DescriptorSetLayout& descriptorSet : vulkanPipeline->pipelineDesc.descriptors)
		{
			if (descriptorSet.set == set)
			{
				if (binding < descriptorSet.bindings.Size())
				{
					return &descriptorSet.bindings[binding];
				}
				return nullptr;
			}
		}
		return nullptr;
	}

	void VulkanCommandBuffer::SetTexture(GPUPipeline* pipeline, u32 set, u32 binding, GPUTexture* texture, u32 arrayElement)
	{
		SK_ASSERT(texture, "texture is required");
		InternalSetTexture(pipeline, set, binding, texture, nullptr, arrayElement);
	}

	void VulkanCommandBuffer::SetTextureView(GPUPipeline* pipeline, u32 set, u32 binding, GPUTextureView* textureView, u32 arrayElement)
	{
		SK_ASSERT(textureView, "textureView is required");
		InternalSetTexture(pipeline, set, binding, nullptr, textureView, arrayElement);
	}

	void VulkanCommandBuffer::SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range)
	{
		VulkanPipeline*             vulkanPipeline = static_cast<VulkanPipeline*>(pipeline);
		DescriptorSetLayoutBinding* layout = FindDescriptorBinding(vulkanPipeline, set, binding);
		if (layout == nullptr || layout->descriptorType == DescriptorType::None) return;

		VkDescriptorBufferInfo      bufferInfo;
		bufferInfo.buffer = static_cast<VulkanBuffer*>(buffer)->buffer;
		bufferInfo.offset = offset;
		bufferInfo.range = range == 0 ? VK_WHOLE_SIZE : range;

		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = nullptr;
		write.dstBinding = binding;
		write.descriptorType = ConvertDescriptorType(layout->descriptorType);
		write.dstArrayElement = 0;
		write.descriptorCount = 1;

		write.pImageInfo = nullptr;
		write.pBufferInfo = &bufferInfo;

		vkCmdPushDescriptorSetKHR(commandBuffer, vulkanPipeline->bindPoint, vulkanPipeline->pipelineLayout, set, 1, &write);
	}

	void VulkanCommandBuffer::SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range, u32 arrayIndex)
	{
		VulkanPipeline*             vulkanPipeline = static_cast<VulkanPipeline*>(pipeline);
		DescriptorSetLayoutBinding* layout = FindDescriptorBinding(vulkanPipeline, set, binding);
		if (layout == nullptr || layout->descriptorType == DescriptorType::None) return;

		VkDescriptorBufferInfo      bufferInfo;
		bufferInfo.buffer = static_cast<VulkanBuffer*>(buffer)->buffer;
		bufferInfo.offset = offset;
		bufferInfo.range = range == 0 ? VK_WHOLE_SIZE : range;

		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = nullptr;
		write.dstBinding = binding;
		write.descriptorType = ConvertDescriptorType(layout->descriptorType);
		write.dstArrayElement = arrayIndex;
		write.descriptorCount = 1;

		write.pImageInfo = nullptr;
		write.pBufferInfo = &bufferInfo;

		vkCmdPushDescriptorSetKHR(commandBuffer, vulkanPipeline->bindPoint, vulkanPipeline->pipelineLayout, set, 1, &write);
	}

	void VulkanCommandBuffer::SetSampler(GPUPipeline* pipeline, u32 set, u32 binding, GPUSampler* sampler)
	{
		VulkanPipeline*             vulkanPipeline = static_cast<VulkanPipeline*>(pipeline);
		DescriptorSetLayoutBinding* layout = FindDescriptorBinding(vulkanPipeline, set, binding);
		if (layout == nullptr || layout->descriptorType == DescriptorType::None) return;

		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = VK_NULL_HANDLE;
		imageInfo.sampler = static_cast<VulkanSampler*>(sampler)->sampler;

		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = nullptr;
		write.dstBinding = binding;
		write.descriptorType = ConvertDescriptorType(layout->descriptorType);
		write.dstArrayElement = 0;
		write.descriptorCount = 1;

		write.pImageInfo = &imageInfo;
		write.pBufferInfo = nullptr;

		vkCmdPushDescriptorSetKHR(commandBuffer, vulkanPipeline->bindPoint, vulkanPipeline->pipelineLayout, set, 1, &write);
	}

	void VulkanCommandBuffer::SetAccelerationStructure(GPUPipeline* pipeline, u32 set, u32 binding, GPUTopLevelAS* topLevelAS)
	{
		SK_ASSERT(topLevelAS, "top level acceleration structure is required");

		VulkanPipeline*             vulkanPipeline = static_cast<VulkanPipeline*>(pipeline);
		DescriptorSetLayoutBinding* layout = FindDescriptorBinding(vulkanPipeline, set, binding);
		if (layout == nullptr || layout->descriptorType == DescriptorType::None) return;

		VulkanTopLevelAS* vkTlas = static_cast<VulkanTopLevelAS*>(topLevelAS);

		VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
		accelerationStructureWrite.accelerationStructureCount = 1;
		accelerationStructureWrite.pAccelerationStructures = &vkTlas->accelerationStructure;

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.pNext = &accelerationStructureWrite;
		write.dstSet = nullptr;
		write.dstBinding = binding;
		write.descriptorType = ConvertDescriptorType(layout->descriptorType);
		write.dstArrayElement = 0;
		write.descriptorCount = 1;

		vkCmdPushDescriptorSetKHR(commandBuffer, vulkanPipeline->bindPoint, vulkanPipeline->pipelineLayout, set, 1, &write);
	}

	void VulkanCommandBuffer::PushConstants(GPUPipeline* pipeline, ShaderStage stages, u32 offset, u32 size, const void* data)
	{
		vkCmdPushConstants(commandBuffer, static_cast<VulkanPipeline*>(pipeline)->pipelineLayout, ConvertShaderStageFlags(stages), offset, size, data);
	}

	void VulkanCommandBuffer::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
	{
		vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void VulkanCommandBuffer::DrawIndirectCount(GPUBuffer* buffer, u64 offset, GPUBuffer* countBuffer, u64 countBufferOffset, u32 maxDrawCount, u32 stride)
	{
		const VulkanBuffer& vulkanBuffer = *static_cast<const VulkanBuffer*>(buffer);
		const VulkanBuffer& vulkanCountBuffer = *static_cast<const VulkanBuffer*>(countBuffer);
		vkCmdDrawIndirectCountKHR(commandBuffer, vulkanBuffer.buffer, offset, vulkanCountBuffer.buffer, countBufferOffset, maxDrawCount, stride);
	}

	void VulkanCommandBuffer::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
	{
		vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	void VulkanCommandBuffer::DrawIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride)
	{
		vkCmdDrawIndirect(commandBuffer, static_cast<VulkanBuffer*>(buffer)->buffer, offset, drawCount, stride);
	}

	void VulkanCommandBuffer::DrawIndexedIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride)
	{
		vkCmdDispatchIndirect(commandBuffer, static_cast<VulkanBuffer*>(buffer)->buffer, offset);
	}

	void VulkanCommandBuffer::DrawIndexedIndirectCount(GPUBuffer* buffer, u64 offset, GPUBuffer* countBuffer, u64 countBufferOffset, u32 maxDrawCount, u32 stride)
	{
		const VulkanBuffer& vulkanBuffer = *static_cast<const VulkanBuffer*>(buffer);
		const VulkanBuffer& vulkanCountBuffer = *static_cast<const VulkanBuffer*>(countBuffer);
		vkCmdDrawIndexedIndirectCountKHR(commandBuffer, vulkanBuffer.buffer, offset, vulkanCountBuffer.buffer, countBufferOffset, maxDrawCount, stride);
	}

	void VulkanCommandBuffer::Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ)
	{
		vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
	}

	void VulkanCommandBuffer::DispatchIndirect(GPUBuffer* buffer, usize offset)
	{
		vkCmdDispatchIndirect(commandBuffer, static_cast<VulkanBuffer*>(buffer)->buffer, offset);
	}

	void VulkanCommandBuffer::TraceRays(GPUPipeline* pipeline, u32 width, u32 height, u32 depth)
	{
		VulkanPipeline* vkPipeline = static_cast<VulkanPipeline*>(pipeline);
		vkCmdTraceRaysKHR(commandBuffer,
			&vkPipeline->sbtRaygenRegion,
			&vkPipeline->sbtMissRegion,
			&vkPipeline->sbtHitRegion,
			&vkPipeline->sbtCallableRegion,
			width, height, depth);
	}

	void VulkanCommandBuffer::BuildBottomLevelAS(GPUBottomLevelAS* bottomLevelAS, const AccelerationStructureBuildInfo& buildInfo)
	{
		VulkanBottomLevelAS* blas = static_cast<VulkanBottomLevelAS*>(bottomLevelAS);

		VkAccelerationStructureBuildGeometryInfoKHR buildGeomInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildGeomInfo.flags = blas->buildFlags;
		buildGeomInfo.mode = buildInfo.update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildGeomInfo.dstAccelerationStructure = blas->accelerationStructure;
		buildGeomInfo.geometryCount = static_cast<u32>(blas->geometries.Size());
		buildGeomInfo.pGeometries = blas->geometries.Data();

		if (buildInfo.update)
		{
			buildGeomInfo.srcAccelerationStructure = blas->accelerationStructure;
		}

		if (buildInfo.scratchBuffer)
		{
			VkDeviceAddress scratchAddr = GetBufferDeviceAddress(
				vulkanDevice->device,
				static_cast<VulkanBuffer*>(buildInfo.scratchBuffer)->buffer) + buildInfo.scratchOffset;
			VkDeviceAddress alignment = vulkanDevice->selectedAdapter->accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
			buildGeomInfo.scratchData.deviceAddress = (scratchAddr + alignment - 1) & ~(alignment - 1);
		}

		const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = blas->buildRangeInfos.Data();
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeomInfo, &pRangeInfos);
	}

	void VulkanCommandBuffer::BuildTopLevelAS(GPUTopLevelAS* topLevelAS, const AccelerationStructureBuildInfo& buildInfo)
	{
		VulkanTopLevelAS* tlas = static_cast<VulkanTopLevelAS*>(topLevelAS);

		VkDeviceAddress instanceAddress = GetBufferDeviceAddress(vulkanDevice->device, tlas->instanceBuffer);

		VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		geometry.geometry.instances.arrayOfPointers = VK_FALSE;
		geometry.geometry.instances.data.deviceAddress = instanceAddress;

		VkAccelerationStructureBuildGeometryInfoKHR buildGeomInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		buildGeomInfo.flags = tlas->buildFlags;
		buildGeomInfo.mode = buildInfo.update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildGeomInfo.dstAccelerationStructure = tlas->accelerationStructure;
		buildGeomInfo.geometryCount = 1;
		buildGeomInfo.pGeometries = &geometry;

		if (buildInfo.update)
		{
			buildGeomInfo.srcAccelerationStructure = tlas->accelerationStructure;
		}

		if (buildInfo.scratchBuffer)
		{
			VkDeviceAddress scratchAddr = GetBufferDeviceAddress(
				vulkanDevice->device,
				static_cast<VulkanBuffer*>(buildInfo.scratchBuffer)->buffer) + buildInfo.scratchOffset;
			VkDeviceAddress alignment = vulkanDevice->selectedAdapter->accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
			buildGeomInfo.scratchData.deviceAddress = (scratchAddr + alignment - 1) & ~(alignment - 1);
		}

		VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
		rangeInfo.primitiveCount = tlas->instanceCount;
		const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

		vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeomInfo, &pRangeInfo);
	}

	void VulkanCommandBuffer::CopyAccelerationStructure(GPUBottomLevelAS* src, GPUBottomLevelAS* dst, bool compress)
	{
		VkCopyAccelerationStructureInfoKHR copyInfo{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
		copyInfo.src = static_cast<VulkanBottomLevelAS*>(src)->accelerationStructure;
		copyInfo.dst = static_cast<VulkanBottomLevelAS*>(dst)->accelerationStructure;
		copyInfo.mode = compress ? VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR : VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
		vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);

		if (compress)
		{
			static_cast<VulkanBottomLevelAS*>(dst)->compacted = true;
		}
	}

	void VulkanCommandBuffer::CopyAccelerationStructure(GPUTopLevelAS* src, GPUTopLevelAS* dst, bool compress)
	{
		VkCopyAccelerationStructureInfoKHR copyInfo{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
		copyInfo.src = static_cast<VulkanTopLevelAS*>(src)->accelerationStructure;
		copyInfo.dst = static_cast<VulkanTopLevelAS*>(dst)->accelerationStructure;
		copyInfo.mode = compress ? VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR : VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
		vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);
	}

	void VulkanCommandBuffer::BeginRenderPass(const BeginRenderPassInfo& info)
	{
		VulkanRenderPass*  vulkanRenderPass = static_cast<VulkanRenderPass*>(info.renderPass);
		VulkanFramebuffer* vulkanFramebuffer = static_cast<VulkanFramebuffer*>(info.framebuffer);

		const Array<GPUTextureView*>& attachments = vulkanFramebuffer->GetDesc().attachments;

		if (info.clearValues)
		{
			for (u32 i = 0; i < vulkanFramebuffer->clearValues.Size(); ++i)
			{
				VkClearValue& clearValue = vulkanFramebuffer->clearValues[i];

				if (IsDepthFormat(ToVkFormat(attachments[i]->GetTexture()->GetDesc().format)))
				{
					clearValue.depthStencil.depth = info.clearValues->depth;
					clearValue.depthStencil.stencil = info.clearValues->stencil;
				}
				else
				{
					clearValue.color.float32[0] = info.clearValues->color.x;
					clearValue.color.float32[1] = info.clearValues->color.y;
					clearValue.color.float32[2] = info.clearValues->color.z;
					clearValue.color.float32[3] = info.clearValues->color.w;
				}
			}
		}


		if (vulkanFramebuffer->extent.width == 0 || vulkanFramebuffer->extent.height == 0)
		{
			SK_ASSERT(false, "Invalid render pass extent");
		}

		VkRenderPassBeginInfo renderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
		renderPassBeginInfo.renderPass = vulkanRenderPass->renderPass;
		renderPassBeginInfo.framebuffer = vulkanFramebuffer->framebuffer;
		renderPassBeginInfo.renderArea.offset = {0, 0};
		renderPassBeginInfo.renderArea.extent = {vulkanFramebuffer->extent.width, vulkanFramebuffer->extent.height};

		if (info.clearValues)
		{
			renderPassBeginInfo.clearValueCount = static_cast<u32>(vulkanFramebuffer->clearValues.Size());
			renderPassBeginInfo.pClearValues = vulkanFramebuffer->clearValues.Data();
		}

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

	void VulkanCommandBuffer::EndRenderPass()
	{
		vkCmdEndRenderPass(commandBuffer);
	}

	void VulkanCommandBuffer::CopyBuffer(GPUBuffer* srcBuffer, GPUBuffer* dstBuffer, usize size, usize srcOffset, usize dstOffset)
	{
		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = srcOffset;
		copyRegion.dstOffset = dstOffset;
		copyRegion.size = size;

		vkCmdCopyBuffer(
			commandBuffer,
			static_cast<VulkanBuffer*>(srcBuffer)->buffer,
			static_cast<VulkanBuffer*>(dstBuffer)->buffer,
			1,
			&copyRegion
		);
	}

	void VulkanCommandBuffer::CopyBufferToTexture(const BufferTextureCopy& copy)
	{
		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = copy.bufferOffset;
		copyRegion.bufferRowLength = 0;   // Tightly packed
		copyRegion.bufferImageHeight = 0; // Tightly packed

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = copy.mipLevel;
		copyRegion.imageSubresource.baseArrayLayer = copy.arrayLayer;
		copyRegion.imageSubresource.layerCount = 1;

		copyRegion.imageOffset = {copy.textureOffset.x, copy.textureOffset.y, copy.textureOffset.z};
		copyRegion.imageExtent = {copy.extent.width, copy.extent.height, copy.extent.depth};

		vkCmdCopyBufferToImage(
			commandBuffer,
			static_cast<VulkanBuffer*>(copy.buffer)->buffer,
			static_cast<VulkanTexture*>(copy.texture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion
		);
	}

	void VulkanCommandBuffer::CopyTextureToBuffer(const BufferTextureCopy& copy)
	{
		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = copy.bufferOffset;
		copyRegion.bufferRowLength = 0;   // Tightly packed
		copyRegion.bufferImageHeight = 0; // Tightly packed

		copyRegion.imageSubresource.aspectMask = GetImageAspectFlags(ToVkFormat(static_cast<VulkanTexture*>(copy.texture)->desc.format));
		copyRegion.imageSubresource.mipLevel = copy.mipLevel;
		copyRegion.imageSubresource.baseArrayLayer = copy.arrayLayer;
		copyRegion.imageSubresource.layerCount = 1;

		copyRegion.imageOffset = {copy.textureOffset.x, copy.textureOffset.y, copy.textureOffset.z};
		copyRegion.imageExtent = {copy.extent.width, copy.extent.height, copy.extent.depth};

		vkCmdCopyImageToBuffer(
			commandBuffer,
			static_cast<VulkanTexture*>(copy.texture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			static_cast<VulkanBuffer*>(copy.buffer)->buffer,
			1,
			&copyRegion
		);
	}

	void VulkanCommandBuffer::CopyTexture(const TextureCopy& copy)
	{
		VkImageCopy copyRegion{};

		copyRegion.srcSubresource.aspectMask = GetImageAspectFlags(ToVkFormat(static_cast<VulkanTexture*>(copy.srcTexture)->desc.format));
		copyRegion.srcSubresource.mipLevel = copy.srcMipLevel;
		copyRegion.srcSubresource.baseArrayLayer = copy.srcArrayLayer;
		copyRegion.srcSubresource.layerCount = 1;
		copyRegion.srcOffset = {0, 0, 0};

		copyRegion.dstSubresource.aspectMask = GetImageAspectFlags(ToVkFormat(static_cast<VulkanTexture*>(copy.dstTexture)->desc.format));
		copyRegion.dstSubresource.mipLevel = copy.dstMipLevel;
		copyRegion.dstSubresource.baseArrayLayer = copy.dstArrayLayer;
		copyRegion.dstSubresource.layerCount = 1;
		copyRegion.dstOffset = {0, 0, 0};

		copyRegion.extent = {copy.extent.width, copy.extent.height, copy.extent.depth};

		vkCmdCopyImage(
			commandBuffer,
			static_cast<VulkanTexture*>(copy.srcTexture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			static_cast<VulkanTexture*>(copy.dstTexture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion
		);
	}

	void VulkanCommandBuffer::BlitTexture(const TextureBlit& blit)
	{
		VkImageBlit blitRegion{};

		blitRegion.srcSubresource.aspectMask = GetImageAspectFlags(ToVkFormat(static_cast<VulkanTexture*>(blit.srcTexture)->desc.format));
		blitRegion.srcSubresource.mipLevel = blit.srcMipLevel;
		blitRegion.srcSubresource.baseArrayLayer = blit.srcArrayLayer;
		blitRegion.srcSubresource.layerCount = 1;

		blitRegion.srcOffsets[0] = {0, 0, 0};
		blitRegion.srcOffsets[1] = {
			static_cast<int32_t>(blit.srcExtent.width),
			static_cast<int32_t>(blit.srcExtent.height),
			static_cast<int32_t>(blit.srcExtent.depth)
		};

		blitRegion.dstSubresource.aspectMask = GetImageAspectFlags(ToVkFormat(static_cast<VulkanTexture*>(blit.dstTexture)->desc.format));
		blitRegion.dstSubresource.mipLevel = blit.dstMipLevel;
		blitRegion.dstSubresource.baseArrayLayer = blit.dstArrayLayer;
		blitRegion.dstSubresource.layerCount = 1;

		blitRegion.dstOffsets[0] = {0, 0, 0};
		blitRegion.dstOffsets[1] = {
			static_cast<int32_t>(blit.dstExtent.width),
			static_cast<int32_t>(blit.dstExtent.height),
			static_cast<int32_t>(blit.dstExtent.depth)
		};

		vkCmdBlitImage(
			commandBuffer,
			static_cast<VulkanTexture*>(blit.srcTexture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			static_cast<VulkanTexture*>(blit.dstTexture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&blitRegion,
			VK_FILTER_LINEAR
		);
	}

	void VulkanCommandBuffer::FillBuffer(GPUBuffer* buffer, usize offset, usize size, u32 data)
	{
		VulkanBuffer* vulkanBuffer = static_cast<VulkanBuffer*>(buffer);
		vkCmdFillBuffer(commandBuffer, vulkanBuffer->buffer, offset, size, data);
	}

	void VulkanCommandBuffer::UpdateBuffer(GPUBuffer* buffer, usize offset, usize size, const void* data)
	{
		VulkanBuffer* vulkanBuffer = static_cast<VulkanBuffer*>(buffer);
		vkCmdUpdateBuffer(commandBuffer, vulkanBuffer->buffer, offset, size, data);
	}

	void VulkanCommandBuffer::ClearColorTexture(GPUTexture* texture, Vec4 clearValue, u32 mipLevel, u32 arrayLayer)
	{
		VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(texture);

		VkClearColorValue clearColor;
		clearColor.float32[0] = clearValue.x;
		clearColor.float32[1] = clearValue.y;
		clearColor.float32[2] = clearValue.z;
		clearColor.float32[3] = clearValue.w;

		VkImageSubresourceRange range{};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = mipLevel;
		range.levelCount = 1;
		range.baseArrayLayer = arrayLayer;
		range.layerCount = 1;

		vkCmdClearColorImage(
			commandBuffer,
			vulkanTexture->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&clearColor,
			1,
			&range
		);
	}

	void VulkanCommandBuffer::ClearDepthStencilTexture(GPUTexture* texture, f32 depth, u32 stencil, u32 mipLevel, u32 arrayLayer)
	{
		VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(texture);

		VkClearDepthStencilValue clearDepthStencil;
		clearDepthStencil.depth = depth;
		clearDepthStencil.stencil = stencil;

		VkImageSubresourceRange range{};
		range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (IsDepthFormat(ToVkFormat(vulkanTexture->desc.format)))
		{
			range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		range.baseMipLevel = mipLevel;
		range.levelCount = 1;
		range.baseArrayLayer = arrayLayer;
		range.layerCount = 1;

		vkCmdClearDepthStencilImage(
			commandBuffer,
			vulkanTexture->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&clearDepthStencil,
			1,
			&range
		);
	}

	namespace
	{
		// The transfer queue family only exposes TOP_OF_PIPE/BOTTOM_OF_PIPE/TRANSFER/HOST stages
		// and TRANSFER_*/HOST_*/MEMORY_* access. The compute queue additionally exposes COMPUTE_SHADER
		// and DRAW_INDIRECT. Asking for shader stages or SHADER_READ on these queues is a validation
		// error. We clamp here so callers can transition resources to ShaderReadOnly on a transfer/
		// compute queue without crafting queue-specific barriers — the access is dropped to 0, so
		// the actual visibility for shaders must be re-established by a barrier on the consuming
		// (graphics) queue before first use.
		VkPipelineStageFlags ClampStageForQueue(VkPipelineStageFlags stage, QueueType queue)
		{
			if (queue == QueueType::Transfer)
			{
				const VkPipelineStageFlags allowed =
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT |
					VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT |
					VK_PIPELINE_STAGE_TRANSFER_BIT |
					VK_PIPELINE_STAGE_HOST_BIT |
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
				VkPipelineStageFlags clamped = stage & allowed;
				return clamped ? clamped : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			}
			if (queue == QueueType::Compute)
			{
				const VkPipelineStageFlags allowed =
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT |
					VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT |
					VK_PIPELINE_STAGE_TRANSFER_BIT |
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
					VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
					VK_PIPELINE_STAGE_HOST_BIT |
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
				VkPipelineStageFlags clamped = stage & allowed;
				return clamped ? clamped : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			}
			return stage;
		}

		VkAccessFlags ClampAccessForQueue(VkAccessFlags access, QueueType queue)
		{
			if (queue == QueueType::Transfer)
			{
				const VkAccessFlags allowed =
					VK_ACCESS_TRANSFER_READ_BIT |
					VK_ACCESS_TRANSFER_WRITE_BIT |
					VK_ACCESS_HOST_READ_BIT |
					VK_ACCESS_HOST_WRITE_BIT |
					VK_ACCESS_MEMORY_READ_BIT |
					VK_ACCESS_MEMORY_WRITE_BIT;
				return access & allowed;
			}
			if (queue == QueueType::Compute)
			{
				const VkAccessFlags allowed =
					VK_ACCESS_TRANSFER_READ_BIT |
					VK_ACCESS_TRANSFER_WRITE_BIT |
					VK_ACCESS_SHADER_READ_BIT |
					VK_ACCESS_SHADER_WRITE_BIT |
					VK_ACCESS_UNIFORM_READ_BIT |
					VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
					VK_ACCESS_HOST_READ_BIT |
					VK_ACCESS_HOST_WRITE_BIT |
					VK_ACCESS_MEMORY_READ_BIT |
					VK_ACCESS_MEMORY_WRITE_BIT;
				return access & allowed;
			}
			return access;
		}
	}

	void VulkanCommandBuffer::ResourceBarrier(GPUBuffer* buffer, ResourceState oldState, ResourceState newState)
	{
		if (oldState == newState)
		{
			return;
		}

		VulkanBuffer* vulkanBuffer = static_cast<VulkanBuffer*>(buffer);

		VkBufferMemoryBarrier bufferBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
		bufferBarrier.srcAccessMask = ClampAccessForQueue(GetAccessFlagsFromResourceState(oldState), queueType);
		bufferBarrier.dstAccessMask = ClampAccessForQueue(GetAccessFlagsFromResourceState(newState), queueType);
		bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarrier.buffer = vulkanBuffer->buffer;
		bufferBarrier.offset = 0;
		bufferBarrier.size = vulkanBuffer->desc.size;

		VkPipelineStageFlags srcStageMask = ClampStageForQueue(GetPipelineStageFromResourceState(oldState), queueType);
		VkPipelineStageFlags dstStageMask = ClampStageForQueue(GetPipelineStageFromResourceState(newState), queueType);

		vkCmdPipelineBarrier(
			commandBuffer,
			srcStageMask,
			dstStageMask,
			0,
			0, nullptr,
			1, &bufferBarrier,
			0, nullptr
		);
	}

	void VulkanCommandBuffer::ResourceBarrier(GPUTexture* texture, ResourceState oldState, ResourceState newState, u32 mipLevel, u32 arrayLayer)
	{
		ResourceBarrier(texture, oldState, newState, mipLevel, 1, arrayLayer, 1);
	}

	void VulkanCommandBuffer::ResourceBarrier(GPUTexture* texture, ResourceState oldState, ResourceState newState, u32 mipLevel, u32 levelCount, u32 arrayLayer, u32 layerCount)
	{
		VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(texture);

		VkImageMemoryBarrier imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		imageBarrier.oldLayout = CastState(oldState);
		imageBarrier.newLayout = CastState(newState);
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.image = vulkanTexture->image;

		imageBarrier.subresourceRange.aspectMask = GetImageAspectFlags(ToVkFormat(vulkanTexture->desc.format));
		imageBarrier.subresourceRange.baseMipLevel = mipLevel;
		imageBarrier.subresourceRange.levelCount = levelCount;
		imageBarrier.subresourceRange.baseArrayLayer = arrayLayer;
		imageBarrier.subresourceRange.layerCount = layerCount;

		imageBarrier.srcAccessMask = 0;

		switch (imageBarrier.oldLayout)
		{
			case VK_IMAGE_LAYOUT_PREINITIALIZED:
				imageBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				imageBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				imageBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				break;
			case VK_IMAGE_LAYOUT_GENERAL:
				imageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
				break;
			default:
				// Other source layouts aren't handled (yet)
				break;
		}

		switch (imageBarrier.newLayout)
		{
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				break;

			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				imageBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				if (imageBarrier.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED)
				{
					imageBarrier.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				}
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				imageBarrier.dstAccessMask = imageBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				if (imageBarrier.srcAccessMask == 0 && imageBarrier.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED)
				{
					imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				}
				imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				break;
			case VK_IMAGE_LAYOUT_GENERAL:
				imageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
				break;
			default:
				break;
		}

		imageBarrier.srcAccessMask = ClampAccessForQueue(imageBarrier.srcAccessMask, queueType);
		imageBarrier.dstAccessMask = ClampAccessForQueue(imageBarrier.dstAccessMask, queueType);

		//TODO remove VK_PIPELINE_STAGE_ALL_COMMANDS_BIT and use correct VkPipelineStageFlags
		VkPipelineStageFlags srcStageMask = ClampStageForQueue(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queueType);
		VkPipelineStageFlags dstStageMask = ClampStageForQueue(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queueType);

		vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
	}

	void VulkanCommandBuffer::ResourceBarrier(GPUBottomLevelAS* bottomLevelAS, ResourceState oldState, ResourceState newState)
	{
		// For acceleration structures in Vulkan, we use memory barriers with appropriate access flags
		// Acceleration structures typically use the VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR and
		// VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR access flags

		VkAccessFlags srcAccessMask = GetAccessFlagsFromResourceState(oldState);
		VkAccessFlags dstAccessMask = GetAccessFlagsFromResourceState(newState);

		// For acceleration structures, we need to add specific access flags
		if (oldState == ResourceState::ShaderReadOnly || oldState == ResourceState::General)
		{
			srcAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		}

		if (oldState == ResourceState::CopyDest || oldState == ResourceState::General)
		{
			srcAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		}

		if (newState == ResourceState::ShaderReadOnly || newState == ResourceState::General)
		{
			dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		}

		if (newState == ResourceState::CopyDest || newState == ResourceState::General)
		{
			dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		}

		VkMemoryBarrier memoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		memoryBarrier.srcAccessMask = srcAccessMask;
		memoryBarrier.dstAccessMask = dstAccessMask;

		VkPipelineStageFlags srcStageMask = GetPipelineStageFromResourceState(oldState);
		VkPipelineStageFlags dstStageMask = GetPipelineStageFromResourceState(newState);

		// Add ray tracing pipeline stages for acceleration structures
		if (oldState == ResourceState::ShaderReadOnly || oldState == ResourceState::General)
		{
			srcStageMask |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		}

		if (newState == ResourceState::ShaderReadOnly || newState == ResourceState::General)
		{
			dstStageMask |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		}

		vkCmdPipelineBarrier(
			commandBuffer,
			srcStageMask,
			dstStageMask,
			0,
			1, &memoryBarrier,
			0, nullptr,
			0, nullptr
		);
	}

	void VulkanCommandBuffer::ResourceBarrier(GPUTopLevelAS* topLevelAS, ResourceState oldState, ResourceState newState)
	{
		if (oldState == newState)
		{
			return;
		}

		// Top-level AS barriers are similar to bottom-level AS barriers
		// They use the same memory barrier approach with acceleration structure access flags

		VkAccessFlags srcAccessMask = GetAccessFlagsFromResourceState(oldState);
		VkAccessFlags dstAccessMask = GetAccessFlagsFromResourceState(newState);

		// For acceleration structures, we need to add specific access flags
		if (oldState == ResourceState::ShaderReadOnly || oldState == ResourceState::General)
		{
			srcAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		}

		if (oldState == ResourceState::CopyDest || oldState == ResourceState::General)
		{
			srcAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		}

		if (newState == ResourceState::ShaderReadOnly || newState == ResourceState::General)
		{
			dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		}

		if (newState == ResourceState::CopyDest || newState == ResourceState::General)
		{
			dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		}

		VkMemoryBarrier memoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		memoryBarrier.srcAccessMask = srcAccessMask;
		memoryBarrier.dstAccessMask = dstAccessMask;

		VkPipelineStageFlags srcStageMask = GetPipelineStageFromResourceState(oldState);
		VkPipelineStageFlags dstStageMask = GetPipelineStageFromResourceState(newState);

		// Add ray tracing pipeline stages for acceleration structures
		if (oldState == ResourceState::ShaderReadOnly || oldState == ResourceState::General)
		{
			srcStageMask |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		}

		if (newState == ResourceState::ShaderReadOnly || newState == ResourceState::General)
		{
			dstStageMask |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		}

		vkCmdPipelineBarrier(
			commandBuffer,
			srcStageMask,
			dstStageMask,
			0,
			1, &memoryBarrier,
			0, nullptr,
			0, nullptr
		);
	}

	void VulkanCommandBuffer::MemoryBarrier()
	{
		// Create a full memory barrier to ensure all memory operations are visible
		VkMemoryBarrier memoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		memoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		memoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			1, &memoryBarrier,
			0, nullptr,
			0, nullptr
		);
	}

	void VulkanCommandBuffer::BeginQuery(GPUQueryPool* queryPool, u32 query)
	{
		VulkanQueryPool* vulkanQueryPool = static_cast<VulkanQueryPool*>(queryPool);

		VkQueryControlFlags flags = 0;
		if (vulkanQueryPool->desc.type == QueryType::Occlusion)
		{
			flags |= VK_QUERY_CONTROL_PRECISE_BIT;
		}

		vkCmdBeginQuery(commandBuffer, vulkanQueryPool->queryPool, query, flags);
	}

	void VulkanCommandBuffer::EndQuery(GPUQueryPool* queryPool, u32 query)
	{
		VulkanQueryPool* vulkanQueryPool = static_cast<VulkanQueryPool*>(queryPool);

		if (vulkanQueryPool->desc.type == QueryType::Occlusion ||
			vulkanQueryPool->desc.type == QueryType::PipelineStatistics)
		{
			vkCmdEndQuery(commandBuffer, vulkanQueryPool->queryPool, query);
		}
	}

	void VulkanCommandBuffer::ResetQueryPool(GPUQueryPool* queryPool, u32 firstQuery, u32 queryCount)
	{
		VulkanQueryPool* vulkanQueryPool = static_cast<VulkanQueryPool*>(queryPool);
		vkCmdResetQueryPool(commandBuffer, vulkanQueryPool->queryPool, firstQuery, queryCount);
	}

	void VulkanCommandBuffer::WriteTimestamp(GPUQueryPool* queryPool, u32 query)
	{
		VulkanQueryPool* vulkanQueryPool = static_cast<VulkanQueryPool*>(queryPool);

		if (vulkanQueryPool->desc.type == QueryType::Timestamp)
		{
			vkCmdWriteTimestamp(
				commandBuffer,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				vulkanQueryPool->queryPool,
				query
			);
		}
	}

	void VulkanCommandBuffer::CopyQueryPoolResults(GPUQueryPool* queryPool, u32 firstQuery, u32 queryCount, GPUBuffer* dstBuffer, usize dstOffset, usize stride)
	{
		VulkanQueryPool* vulkanQueryPool = static_cast<VulkanQueryPool*>(queryPool);
		VulkanBuffer*    vulkanBuffer = static_cast<VulkanBuffer*>(dstBuffer);

		VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;

		flags |= VK_QUERY_RESULT_WAIT_BIT;

		if (vulkanQueryPool->desc.allowPartialResults)
		{
			flags |= VK_QUERY_RESULT_PARTIAL_BIT;
		}

		// If availability data is needed
		if (vulkanQueryPool->desc.returnAvailability)
		{
			flags |= VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
		}

		vkCmdCopyQueryPoolResults(
			commandBuffer,
			vulkanQueryPool->queryPool,
			firstQuery,
			queryCount,
			vulkanBuffer->buffer,
			dstOffset,
			stride,
			flags
		);
	}

	void VulkanCommandBuffer::BeginDebugMarker(StringView name, const Vec4& color)
	{
		// Check if debug extension is available
		if (vkCmdBeginDebugUtilsLabelEXT)
		{
			VkDebugUtilsLabelEXT labelInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
			labelInfo.pLabelName = name.CStr();
			labelInfo.color[0] = color.x;
			labelInfo.color[1] = color.y;
			labelInfo.color[2] = color.z;
			labelInfo.color[3] = color.w;

			vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &labelInfo);
		}
	}

	void VulkanCommandBuffer::EndDebugMarker()
	{
		if (vkCmdEndDebugUtilsLabelEXT)
		{
			vkCmdEndDebugUtilsLabelEXT(commandBuffer);
		}
	}

	void VulkanCommandBuffer::InsertDebugMarker(StringView name, const Vec4& color)
	{
		if (vkCmdInsertDebugUtilsLabelEXT)
		{
			VkDebugUtilsLabelEXT labelInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
			labelInfo.pLabelName = name.CStr();
			labelInfo.color[0] = color.x;
			labelInfo.color[1] = color.y;
			labelInfo.color[2] = color.z;
			labelInfo.color[3] = color.w;

			vkCmdInsertDebugUtilsLabelEXT(commandBuffer, &labelInfo);
		}
	}


	void VulkanCommandBuffer::Destroy()
	{
		if (commandPool)
		{
			vkDestroyCommandPool(vulkanDevice->device, commandPool, nullptr);
		}
		DestroyAndFree(this);
	}

	bool VulkanQueryPool::GetResults(u32 firstQuery, u32 queryCount, void* data, usize stride, bool wait)
	{
		if (queryPool == VK_NULL_HANDLE)
		{
			return false;
		}

		VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;

		if (wait)
		{
			flags |= VK_QUERY_RESULT_WAIT_BIT;
		}

		if (desc.allowPartialResults)
		{
			flags |= VK_QUERY_RESULT_PARTIAL_BIT;
		}

		if (desc.returnAvailability)
		{
			flags |= VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
		}

		VkResult result = vkGetQueryPoolResults(
			vulkanDevice->device,
			queryPool,
			firstQuery,
			queryCount,
			stride * queryCount,
			data,
			stride,
			flags
		);

		return (result == VK_SUCCESS || (!wait && result == VK_NOT_READY));
	}

	void VulkanQueryPool::Destroy()
	{
		if (queryPool != VK_NULL_HANDLE)
		{
			VulkanResourceDestructor destructor;
			destructor.queryPool = queryPool;
			EnqueueDestructor(destructor);
			queryPool = VK_NULL_HANDLE;
		}
		DestroyAndFree(this);
	}

	void VulkanQueue::Destroy()
	{
		vkDestroyFence(vulkanDevice->device, fence, nullptr);
		DestroyAndFree(this);
	}

	void VulkanQueue::Submit(GPUCommandBuffer* cmd)
	{
		VulkanCommandBuffer* vulkanCommandBuffer = static_cast<VulkanCommandBuffer*>(cmd);

		VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &vulkanCommandBuffer->commandBuffer;
		Submit(&submitInfo, VK_NULL_HANDLE);
	}

	void VulkanQueue::SubmitAndWait(GPUCommandBuffer* cmd)
	{
		VulkanCommandBuffer* vulkanCommandBuffer = static_cast<VulkanCommandBuffer*>(cmd);

		VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &vulkanCommandBuffer->commandBuffer;
		Submit(&submitInfo, fence);

		vkWaitForFences(vulkanDevice->device, 1, &fence, VK_TRUE, UINT64_MAX);
		vkResetFences(vulkanDevice->device, 1, &fence);
	}

	VkResult VulkanQueue::Submit(VkSubmitInfo* submitInfo, VkFence vkFence) const
	{
		VkResult res;
		{
			std::unique_lock lock(context->mutex);
			res = vkQueueSubmit(context->vkQueue, 1, submitInfo, vkFence);
		}

		if (res != VK_SUCCESS)
		{
			logger.Error("failed to submit command buffer to queue, error {} ", string_VkResult(res));
			Graphics::ShowSubmitError();
			std::terminate();
		}

		return res;
	}


	void VulkanCommandBuffer::InternalSetTexture(GPUPipeline* pipeline, u32 set, u32 binding, GPUTexture* texture, GPUTextureView* textureView, u32 arrayElement)
	{
		VulkanPipeline* vulkanPipeline = static_cast<VulkanPipeline*>(pipeline);

		DescriptorSetLayoutBinding* layout = FindDescriptorBinding(vulkanPipeline, set, binding);
		if (layout == nullptr || layout->descriptorType == DescriptorType::None) return;

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = nullptr;
		write.dstBinding = binding;
		write.descriptorType = ConvertDescriptorType(layout->descriptorType);
		write.dstArrayElement = arrayElement;
		write.descriptorCount = 1;

		bool depthFormat = false;

		VkDescriptorImageInfo imageInfo = {};

		if (texture)
		{
			VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(texture);
			imageInfo.imageView = vulkanTexture->GetImageView();
			depthFormat = vulkanTexture->isDepth;
		}
		else if (textureView)
		{
			VulkanTextureView* vulkanTextureView = static_cast<VulkanTextureView*>(textureView);
			imageInfo.imageView = vulkanTextureView->imageView;
			depthFormat = vulkanTextureView->texture->isDepth;
		}

		imageInfo.imageLayout = depthFormat
													? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
													: layout->descriptorType == DescriptorType::StorageImage
													? VK_IMAGE_LAYOUT_GENERAL
													: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		write.pImageInfo = &imageInfo;

		SK_ASSERT(imageInfo.imageView, "imageView is required");

		vkCmdPushDescriptorSetKHR(commandBuffer, vulkanPipeline->bindPoint, vulkanPipeline->pipelineLayout, set, 1, &write);
	}

	VulkanDevice::~VulkanDevice()
	{
		for (u32 i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			FlushDestructors(i);
		}

		vmaDestroyAllocator(vmaAllocator);

		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyCommandPool(device, commandPool, nullptr);

		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroyFence(device, inFlightFences[i], nullptr);
			commandBuffers[i]->Destroy();
		}

		if (validationLayersEnabled)
		{
			vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessengerExt, nullptr);
		}

		vkDestroyDevice(device, nullptr);
		vkDestroyInstance(instance, nullptr);

		Event::Unbind<OnEndFrame, &VulkanDevice::ExecuteFrame>(this);
	}

	Span<GPUAdapter*> VulkanDevice::GetAdapters()
	{
		return adapters;
	}

	bool VulkanDevice::SelectAdapter(GPUAdapter* adapter)
	{
		VulkanAdapter* vulkanAdapter = static_cast<VulkanAdapter*>(adapter);
		//update limits
		properties.limits.maxAttachmentSamples = GetMaxUsableSampleCount(vulkanAdapter->deviceProperties.properties);
		properties.limits.minMemoryMapAlignment = vulkanAdapter->deviceProperties.properties.limits.minMemoryMapAlignment;
		properties.limits.minUniformBufferOffsetAlignment = vulkanAdapter->deviceProperties.properties.limits.minUniformBufferOffsetAlignment;
		properties.limits.timestampPeriod = vulkanAdapter->deviceProperties.properties.limits.timestampPeriod;

		Array<const char*>        extensions{};
		HashSet<String>           extensionsAdded{};
		VkPhysicalDeviceFeatures2 deviceFeatures2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

		deviceFeatures2.features.samplerAnisotropy = vulkanAdapter->deviceFeatures.features.samplerAnisotropy;
		deviceFeatures2.features.sampleRateShading = vulkanAdapter->deviceFeatures.features.sampleRateShading;
		deviceFeatures2.features.depthClamp = true;


		HashSet<String> availableExtensions = GetDeviceExtensions(vulkanAdapter->device);
		auto            AddIfPresent = [&](StringView extension, VoidPtr feature = nullptr)
		{
			if (availableExtensions.Has(extension))
			{
				if (extensionsAdded.Find(extension) == extensionsAdded.end())
				{
					if (feature)
					{
						VulkanBaseInStructure* toAdd = static_cast<VulkanBaseInStructure*>(feature);
						toAdd->pNext = deviceFeatures2.pNext;
						deviceFeatures2.pNext = toAdd;
					}

					extensions.EmplaceBack(extension.CStr());
					extensionsAdded.Insert(extension);
				}
				return true;
			}
			return false;
		};

		auto AddToChain = [&](VoidPtr feature)
		{
			if (feature)
			{
				VulkanBaseInStructure* toAdd = static_cast<VulkanBaseInStructure*>(feature);
				toAdd->pNext = deviceFeatures2.pNext;
				deviceFeatures2.pNext = toAdd;
			}
		};

		VkPhysicalDeviceMaintenance4Features maintenance4Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES_KHR};
		maintenance4Features.maintenance4 = true;

		VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
		indexingFeatures.descriptorBindingPartiallyBound = vulkanAdapter->indexingFeatures.descriptorBindingPartiallyBound;
		indexingFeatures.runtimeDescriptorArray = vulkanAdapter->indexingFeatures.runtimeDescriptorArray;
		indexingFeatures.shaderSampledImageArrayNonUniformIndexing = vulkanAdapter->indexingFeatures.shaderSampledImageArrayNonUniformIndexing;
		indexingFeatures.shaderStorageBufferArrayNonUniformIndexing = vulkanAdapter->indexingFeatures.shaderStorageBufferArrayNonUniformIndexing;
		indexingFeatures.shaderUniformBufferArrayNonUniformIndexing = vulkanAdapter->indexingFeatures.shaderUniformBufferArrayNonUniformIndexing;
		indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = vulkanAdapter->indexingFeatures.descriptorBindingSampledImageUpdateAfterBind;
		indexingFeatures.descriptorBindingStorageImageUpdateAfterBind = vulkanAdapter->indexingFeatures.descriptorBindingStorageImageUpdateAfterBind;
		indexingFeatures.descriptorBindingStorageBufferUpdateAfterBind = vulkanAdapter->indexingFeatures.descriptorBindingStorageBufferUpdateAfterBind;
		indexingFeatures.descriptorBindingUniformBufferUpdateAfterBind = vulkanAdapter->indexingFeatures.descriptorBindingUniformBufferUpdateAfterBind;

		features.bindlessTextureSupported = indexingFeatures.shaderSampledImageArrayNonUniformIndexing &&
			indexingFeatures.descriptorBindingPartiallyBound &&
			indexingFeatures.runtimeDescriptorArray &&
			indexingFeatures.descriptorBindingSampledImageUpdateAfterBind &&
			indexingFeatures.descriptorBindingStorageImageUpdateAfterBind;

		features.bindlessBufferSupported = indexingFeatures.shaderStorageBufferArrayNonUniformIndexing &&
			indexingFeatures.descriptorBindingPartiallyBound &&
			indexingFeatures.runtimeDescriptorArray &&
			indexingFeatures.descriptorBindingStorageBufferUpdateAfterBind &&
			indexingFeatures.descriptorBindingStorageImageUpdateAfterBind &&
			indexingFeatures.descriptorBindingUniformBufferUpdateAfterBind;


		features.multiviewEnabled = vulkanAdapter->multiviewFeatures.multiview;

		VkPhysicalDeviceRayQueryFeaturesKHR deviceRayQueryFeaturesKhr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
		deviceRayQueryFeaturesKhr.rayQuery = true;

		VkPhysicalDeviceAccelerationStructureFeaturesKHR deviceAccelerationStructureFeaturesKhr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
		deviceAccelerationStructureFeaturesKhr.accelerationStructure = true;

		VkPhysicalDeviceRayTracingPipelineFeaturesKHR deviceRayTracingPipelineFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
		deviceRayTracingPipelineFeatures.rayTracingPipeline = true;

		VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
		bufferDeviceAddressFeatures.bufferDeviceAddress = true;

		VkPhysicalDeviceShaderDrawParametersFeatures drawParametersFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES};
		drawParametersFeatures.shaderDrawParameters = vulkanAdapter->drawParametersFeatures.shaderDrawParameters;

		VkPhysicalDeviceMultiviewFeatures multiviewFeatures{};
		multiviewFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
		multiviewFeatures.multiview = true;

		VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentricFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR};
		barycentricFeatures.fragmentShaderBarycentric = true;

		if (!AddIfPresent(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			return false;
		}

		if (!AddIfPresent(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME))
		{
			return false;
		}

		AddIfPresent(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);

		features.resolveDepth = AddIfPresent(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);

		AddIfPresent(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
		AddIfPresent(VK_KHR_MAINTENANCE_4_EXTENSION_NAME, &maintenance4Features);

		features.bufferDeviceAddress = AddIfPresent(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, &bufferDeviceAddressFeatures);
		features.drawIndirectCount = AddIfPresent(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
		features.memoryBudget = AddIfPresent(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
		features.fragmentShaderBarycentric = AddIfPresent(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME, &barycentricFeatures);

		features.rayTracing = AddIfPresent(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) && AddIfPresent(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

		if (features.rayTracing)
		{
			AddIfPresent(VK_KHR_RAY_QUERY_EXTENSION_NAME);
			AddIfPresent(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
		}

		AddIfPresent(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);

		AddIfPresent(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
		//AddIfPresent(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
		//AddIfPresent(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);

		if (features.bindlessTextureSupported)
		{
			AddToChain(&indexingFeatures);
		}

		if (features.rayTracing)
		{
			AddToChain(&deviceRayQueryFeaturesKhr);
			AddToChain(&deviceAccelerationStructureFeaturesKhr);
			AddToChain(&deviceRayTracingPipelineFeatures);
		}

		if (features.multiviewEnabled)
		{
			AddToChain(&multiviewFeatures);
		}

		if (drawParametersFeatures.shaderDrawParameters)
		{
			AddToChain(&drawParametersFeatures);
		}

#ifdef SK_APPLE
		AddIfPresent(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

		float queuePriority = 1.0f;

		HashSet<u32> uniqueQueueFamilies;
		uniqueQueueFamilies.Emplace(vulkanAdapter->graphicsFamily);
		uniqueQueueFamilies.Emplace(vulkanAdapter->presentFamily);
		uniqueQueueFamilies.Emplace(vulkanAdapter->computeFamily);
		uniqueQueueFamilies.Emplace(vulkanAdapter->transferFamily);

		Array<VkDeviceQueueCreateInfo> queueCreateInfos{};
		queueCreateInfos.Reserve(uniqueQueueFamilies.Size());
		for (u32 family : uniqueQueueFamilies)
		{
			VkDeviceQueueCreateInfo& info = queueCreateInfos.EmplaceBack();
			info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			info.queueFamilyIndex = family;
			info.queueCount = 1;
			info.pQueuePriorities = &queuePriority;
		}

		if (vulkanAdapter->deviceFeatures.features.fillModeNonSolid)
		{
			deviceFeatures2.features.fillModeNonSolid = VK_TRUE;
		}

		if (vulkanAdapter->deviceFeatures.features.wideLines)
		{
			deviceFeatures2.features.wideLines = VK_TRUE;
		}

		if (vulkanAdapter->deviceFeatures.features.fragmentStoresAndAtomics)
		{
			deviceFeatures2.features.fragmentStoresAndAtomics = VK_TRUE;
		}

		if (vulkanAdapter->deviceFeatures.features.vertexPipelineStoresAndAtomics)
		{
			deviceFeatures2.features.vertexPipelineStoresAndAtomics = VK_TRUE;
		}

		Array<String> xrExtensions = OpenXRManagerGetDeviceExtensions();

		for (const String& extension : xrExtensions)
		{
			logger.Info("extension: {}", extension);
			if (!AddIfPresent(extension))
			{
				logger.Warn("Failed to find required OpenXR extension: {}", extension);
			}
		}

		VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		createInfo.pNext = &deviceFeatures2;
		createInfo.pQueueCreateInfos = queueCreateInfos.Data();
		createInfo.queueCreateInfoCount = queueCreateInfos.Size();
		createInfo.enabledExtensionCount = static_cast<u32>(extensions.Size());
		createInfo.ppEnabledExtensionNames = extensions.Data();

		VkResult res = vkCreateDevice(vulkanAdapter->device, &createInfo, nullptr, &device);
		if (res != VK_SUCCESS)
		{
			logger.Error("Failed to create logical device for device {}, error {}", adapter->GetName(), string_VkResult(res));
			return false;
		}

		selectedAdapter = vulkanAdapter;

		{
			VkQueue queue;
			vkGetDeviceQueue(device, vulkanAdapter->graphicsFamily, 0, &queue);
			graphicsQueue.context = std::make_shared<VulkanQueueContext>(queue);
		}

		if (vulkanAdapter->presentFamily == vulkanAdapter->graphicsFamily)
		{
			presentQueue.context = graphicsQueue.context;
		}
		else
		{
			VkQueue queue;
			vkGetDeviceQueue(device, vulkanAdapter->presentFamily, 0, &queue);
			presentQueue.context = std::make_shared<VulkanQueueContext>(queue);
		}

		if (vulkanAdapter->computeFamily == vulkanAdapter->graphicsFamily)
		{
			computeQueue.context = graphicsQueue.context;
		}
		else if (vulkanAdapter->computeFamily == vulkanAdapter->presentFamily)
		{
			computeQueue.context = presentQueue.context;
		}
		else
		{
			VkQueue queue;
			vkGetDeviceQueue(device, vulkanAdapter->computeFamily, 0, &queue);
			computeQueue.context = std::make_shared<VulkanQueueContext>(queue);
		}

		if (vulkanAdapter->transferFamily == vulkanAdapter->graphicsFamily)
		{
			transferQueue.context = graphicsQueue.context;
		}
		else if (vulkanAdapter->transferFamily == vulkanAdapter->presentFamily)
		{
			transferQueue.context = presentQueue.context;
		}
		else if (vulkanAdapter->transferFamily == vulkanAdapter->computeFamily)
		{
			transferQueue.context = computeQueue.context;
		}
		else
		{
			VkQueue queue;
			vkGetDeviceQueue(device, vulkanAdapter->transferFamily, 0, &queue);
			transferQueue.context = std::make_shared<VulkanQueueContext>(queue);
		}

		VmaVulkanFunctions vmaVulkanFunctions{};
		vmaVulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vmaVulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.physicalDevice = selectedAdapter->device;
		allocatorInfo.device = device;
		allocatorInfo.instance = instance;

		if (features.bufferDeviceAddress)
		{
			allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		}

		if (features.memoryBudget)
		{
			allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
		}

		allocatorInfo.pVulkanFunctions = &vmaVulkanFunctions;
		vmaCreateAllocator(&allocatorInfo, &vmaAllocator);

		//TODO - descriptor pool management needs to be improved.
		Array<VkDescriptorPoolSize> sizes;
		sizes.Reserve(7);

		sizes.EmplaceBack(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5000u);
		sizes.EmplaceBack(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5000u);
		sizes.EmplaceBack(VK_DESCRIPTOR_TYPE_SAMPLER, 5000u);
		sizes.EmplaceBack(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 5000u);
		sizes.EmplaceBack(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5000u);
		sizes.EmplaceBack(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5000u);
		sizes.EmplaceBack(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5000u);

		if (features.rayTracing)
		{
			sizes.EmplaceBack(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 64u);
		}

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = sizes.Size();
		poolInfo.pPoolSizes = sizes.Data();
		poolInfo.maxSets = 5000;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
			{
				logger.Error("Failed to create frame objects");
				return false;
			}
		}

		VkCommandPoolCreateInfo commandPoolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		commandPoolInfo.queueFamilyIndex = selectedAdapter->graphicsFamily;
		commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool);

		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			VulkanCommandBuffer* vulkanCommandBuffer = Alloc<VulkanCommandBuffer>();
			vulkanCommandBuffer->vulkanDevice = this;
			vulkanCommandBuffer->commandPool = nullptr;

			VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = commandPool;
			allocInfo.commandBufferCount = 1;

			vkAllocateCommandBuffers(device, &allocInfo, &vulkanCommandBuffer->commandBuffer);

			commandBuffers[i] = vulkanCommandBuffer;
		}

		Event::Bind<OnEndFrame, &VulkanDevice::ExecuteFrame>(this);

		logger.Info("Vulkan API {}.{}.{} Device: {} ",
		            VK_VERSION_MAJOR(selectedAdapter->deviceProperties.properties.apiVersion),
		            VK_VERSION_MINOR(selectedAdapter->deviceProperties.properties.apiVersion),
		            VK_VERSION_PATCH(selectedAdapter->deviceProperties.properties.apiVersion),
		            selectedAdapter->deviceProperties.properties.deviceName);

		return true;
	}

	const DeviceProperties& VulkanDevice::GetProperties()
	{
		return properties;
	}

	const DeviceFeatures& VulkanDevice::GetFeatures()
	{
		return features;
	}

	GraphicsAPI VulkanDevice::GetAPI() const
	{
		return GraphicsAPI::Vulkan;
	}

	void VulkanDevice::WaitIdle()
	{
		vkDeviceWaitIdle(device);
	}

	GPUSwapchain* VulkanDevice::CreateSwapchain(const SwapchainDesc& desc)
	{
		VulkanSwapchain* swapchain = Alloc<VulkanSwapchain>(desc, this);
		if (!swapchain->CreateInternal())
		{
			swapchain->Destroy();
			return nullptr;
		}

		swapchains.EmplaceBack(swapchain);
		return swapchain;
	}

	GPURenderPass* VulkanDevice::CreateRenderPass(const RenderPassDesc& desc)
	{
		bool hasDepth = false;

		Array<VkAttachmentDescription2> attachmentDescriptions;
		Array<VkAttachmentReference2>   colorAttachmentReference{};
		Array<VkAttachmentReference2>   resolveColorAttachmentReferences;
		VkAttachmentReference2          depthReference{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};

		VkAttachmentReference2                     depthReferenceResolve{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
		VkSubpassDescriptionDepthStencilResolveKHR depthResolveSubpass{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE_KHR};

		depthResolveSubpass.depthResolveMode = VK_RESOLVE_MODE_MAX_BIT;
		depthResolveSubpass.stencilResolveMode = VK_RESOLVE_MODE_MAX_BIT;
		u32 attachmentCount = 0;
		u32 samplesCount = 1;

		auto AddAttachment = [&](const AttachmentDesc& attachment, bool& isDepthFormat)
		{
			VkFormat format = ToVkFormat(attachment.format);
			isDepthFormat = IsDepthFormat(format);

			samplesCount = std::max(samplesCount, attachment.sampleCount);

			VkAttachmentDescription2 attachmentDescription{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
			attachmentDescription.format = format;
			attachmentDescription.samples = CastSampleCount(attachment.sampleCount);

			attachmentDescription.loadOp = CastLoadOp(attachment.loadOp);
			attachmentDescription.storeOp = CastStoreOp(attachment.storeOp);

			attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			attachmentDescription.initialLayout = CastState(attachment.initialState);

			if (!isDepthFormat)
			{
				attachmentDescription.finalLayout = CastState(attachment.finalState, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			}
			else
			{
				attachmentDescription.finalLayout = CastState(attachment.finalState, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			}

			attachmentDescriptions.EmplaceBack(attachmentDescription);
		};

		for (const auto& attachment : desc.attachments)
		{
			bool isDepthFormat = false;

			AddAttachment(attachment, isDepthFormat);

			if (!isDepthFormat)
			{
				VkAttachmentReference2 reference{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
				reference.attachment = attachmentCount;
				reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorAttachmentReference.EmplaceBack(reference);
			}
			else
			{
				depthReference.attachment = attachmentCount;
				depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				hasDepth = true;
			}
			attachmentCount++;
		}

		bool hasDepthResolve = false;

		for (const AttachmentDesc& attachment : desc.resolveAttachments)
		{
			bool isDepthFormat = false;
			AddAttachment(attachment, isDepthFormat);
			if (!isDepthFormat)
			{
				VkAttachmentReference2 reference{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
				reference.attachment = attachmentCount;
				reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				resolveColorAttachmentReferences.EmplaceBack(reference);
			}
			else
			{
				depthReferenceResolve.attachment = attachmentCount;
				depthReferenceResolve.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				depthResolveSubpass.pDepthStencilResolveAttachment = &depthReferenceResolve;
				hasDepthResolve = true;
			}
			attachmentCount++;
		}

		VkSubpassDescription2KHR subPass{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
		subPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subPass.colorAttachmentCount = colorAttachmentReference.Size();
		subPass.pColorAttachments = colorAttachmentReference.Data();
		if (hasDepth)
		{
			subPass.pDepthStencilAttachment = &depthReference;
		}

		if (!resolveColorAttachmentReferences.Empty())
		{
			subPass.pResolveAttachments = resolveColorAttachmentReferences.Data();
		}

		if (hasDepthResolve)
		{
			subPass.pNext = &depthResolveSubpass;
		}

		VkRenderPassCreateInfo2 renderPassCreateInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
		renderPassCreateInfo.attachmentCount = attachmentDescriptions.Size();
		renderPassCreateInfo.pAttachments = attachmentDescriptions.Data();
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subPass;
		renderPassCreateInfo.dependencyCount = 0;

		// VkSubpassDependency2 dependency{VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2};
		// dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		// if (!resolveColorAttachmentReferences.Empty())
		// {
		// 	renderPassCreateInfo.pDependencies = &dependency;
		// 	renderPassCreateInfo.dependencyCount = 1;
		// }


		VkRenderPass vkRenderPass;
		VkResult     res = vkCreateRenderPass2KHR(device, &renderPassCreateInfo, nullptr, &vkRenderPass);
		if (res != VK_SUCCESS)
		{
			logger.Error("error on create render pass {} ", string_VkResult(res));
			return nullptr;
		}

		VulkanRenderPass* vulkanRenderPass = Alloc<VulkanRenderPass>();
		vulkanRenderPass->vulkanDevice = this;
		vulkanRenderPass->desc = desc;
		vulkanRenderPass->renderPass = vkRenderPass;
		vulkanRenderPass->samplesCount = samplesCount;

		return vulkanRenderPass;
	}

	GPUFramebuffer* VulkanDevice::CreateFramebuffer(const FramebufferDesc& desc)
	{
		VulkanRenderPass* vulkanRenderPass = static_cast<VulkanRenderPass*>(desc.renderPass);

		Array<VkImageView> imageViews;
		imageViews.Reserve(desc.attachments.Size());

		Extent3D extent;

		for (const auto& attachment : desc.attachments)
		{
			if (VulkanTextureView* vulkanTextureView = static_cast<VulkanTextureView*>(attachment))
			{
				extent = vulkanTextureView->GetTexture()->GetDesc().extent;
				imageViews.EmplaceBack(vulkanTextureView->imageView);
			}
		}

		VkFramebuffer vkFramebuffer = {};

		VkFramebufferCreateInfo framebufferCreateInfo{};
		framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferCreateInfo.renderPass = vulkanRenderPass->renderPass;
		framebufferCreateInfo.width = extent.width;
		framebufferCreateInfo.height = extent.height;
		framebufferCreateInfo.layers = 1;
		framebufferCreateInfo.attachmentCount = imageViews.Size();
		framebufferCreateInfo.pAttachments = imageViews.Data();

		if (VkResult res = vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &vkFramebuffer); res != VK_SUCCESS)
		{
			logger.Error("error on create framebuffer {} ", string_VkResult(res));
			return nullptr;
		}

		VulkanFramebuffer* vulkanFramebuffer = Alloc<VulkanFramebuffer>();
		vulkanFramebuffer->vulkanDevice = this;
		vulkanFramebuffer->desc = desc;
		vulkanFramebuffer->extent = {extent.width, extent.height};
		vulkanFramebuffer->framebuffer = vkFramebuffer;
		vulkanFramebuffer->clearValues.Resize(desc.attachments.Size());

		return vulkanFramebuffer;
	}

	GPUCommandBuffer* VulkanDevice::CreateCommandBuffer(const QueueType& queueType)
	{
		VulkanCommandBuffer* vulkanCommandBuffer = Alloc<VulkanCommandBuffer>();
		vulkanCommandBuffer->vulkanDevice = this;
		vulkanCommandBuffer->queueType = queueType;

		VkCommandPoolCreateInfo commandPoolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		if (queueType == QueueType::Compute)
		{
			commandPoolInfo.queueFamilyIndex = selectedAdapter->computeFamily;
		}
		else if (queueType == QueueType::Transfer)
		{
			commandPoolInfo.queueFamilyIndex = selectedAdapter->transferFamily;
		}
		else
		{
			commandPoolInfo.queueFamilyIndex = selectedAdapter->graphicsFamily;
		}

		vkCreateCommandPool(device, &commandPoolInfo, nullptr, &vulkanCommandBuffer->commandPool);

		VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = vulkanCommandBuffer->commandPool;
		allocInfo.commandBufferCount = 1;

		vkAllocateCommandBuffers(device, &allocInfo, &vulkanCommandBuffer->commandBuffer);

		return vulkanCommandBuffer;
	}

	void* VulkanBuffer::Map()
	{
		if (mappedData)
			return mappedData;

		if (!desc.hostVisible)
			return nullptr;

		vmaMapMemory(vulkanDevice->vmaAllocator, allocation, &mappedData);
		return mappedData;
	}

	void VulkanBuffer::Unmap()
	{
		if (!mappedData || desc.persistentMapped)
			return;

		vmaUnmapMemory(vulkanDevice->vmaAllocator, allocation);
		mappedData = nullptr;
	}

	void VulkanBuffer::Destroy()
	{
		VulkanResourceDestructor destructor;
		destructor.buffer = buffer;
		destructor.allocation = allocation;
		EnqueueDestructor(destructor);

		if (mappedData && desc.persistentMapped)
		{
			vmaUnmapMemory(vulkanDevice->vmaAllocator, allocation);
		}
		DestroyAndFree(this);
	}

	const BufferDesc& VulkanBuffer::GetDesc() const
	{
		return desc;
	}

	VoidPtr VulkanBuffer::GetMappedData()
	{
		return mappedData;
	}

	const TextureDesc& VulkanTexture::GetDesc() const
	{
		return desc;
	}

	void VulkanTexture::Destroy()
	{
		VulkanResourceDestructor destructor;
		destructor.texture = this;
		EnqueueDestructor(destructor);
	}

	GPUTextureView* VulkanTexture::GetTextureView() const
	{
		return textureView;
	}

	VkImageView VulkanTexture::GetImageView() const
	{
		if (textureView)
		{
			return static_cast<VulkanTextureView*>(textureView)->imageView;
		}
		return nullptr;
	}

	const TextureViewDesc& VulkanTextureView::GetDesc()
	{
		return desc;
	}

	GPUTexture* VulkanTextureView::GetTexture()
	{
		return texture;
	}

	void VulkanTextureView::Destroy()
	{
		VulkanResourceDestructor destructor;
		destructor.textureView = this;
		EnqueueDestructor(destructor);
	}

	const SamplerDesc& VulkanSampler::GetDesc() const
	{
		return desc;
	}

	void VulkanSampler::Destroy()
	{
		VulkanResourceDestructor destructor;
		destructor.sampler = sampler;
		EnqueueDestructor(destructor);
		DestroyAndFree(this);
	}

	// Create functions
	GPUBuffer* VulkanDevice::CreateBuffer(const BufferDesc& desc)
	{
		VkBufferCreateInfo bufferCreateInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bufferCreateInfo.size = desc.size;
		bufferCreateInfo.usage = GetBufferUsageFlags(desc.usage, GetFeatures().bufferDeviceAddress);

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		if (desc.hostVisible)
		{
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

			if (desc.persistentMapped)
				allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}

		VkBuffer      vkBuffer;
		VmaAllocation vmaAllocation;

		VkResult result = vmaCreateBuffer(vmaAllocator, &bufferCreateInfo, &allocInfo, &vkBuffer, &vmaAllocation, nullptr);
		if (result != VK_SUCCESS)
		{
			logger.Error("error on create buffer: {} ", string_VkResult(result));
			return nullptr;
		}

		VulkanBuffer* buffer = Alloc<VulkanBuffer>();
		buffer->vulkanDevice = this;
		buffer->desc = desc;
		buffer->buffer = vkBuffer;
		buffer->allocation = vmaAllocation;

		if (desc.hostVisible && desc.persistentMapped)
		{
			vmaMapMemory(vmaAllocator, buffer->allocation, &buffer->mappedData);
		}

		SetObjectName(*this, VK_OBJECT_TYPE_BUFFER, PtrToInt(buffer->buffer), desc.debugName);

		return buffer;
	}

	GPUTexture* VulkanDevice::CreateTexture(const TextureDesc& desc)
	{
		VkImageCreateInfo imageCreateInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};

		if (desc.extent.depth > 1)
		{
			imageCreateInfo.imageType = VK_IMAGE_TYPE_3D;
		}
		else
		{
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		}

		imageCreateInfo.extent.width = desc.extent.width;
		imageCreateInfo.extent.height = desc.extent.height;
		imageCreateInfo.extent.depth = desc.extent.depth;

		imageCreateInfo.format = ToVkFormat(desc.format);
		imageCreateInfo.mipLevels = desc.mipLevels;
		imageCreateInfo.arrayLayers = desc.arrayLayers;
		imageCreateInfo.samples = CastSampleCount(desc.sampleCount);
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = GetImageUsageFlags(desc.usage);
		imageCreateInfo.flags |= desc.cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		VkImage       vkImage;
		VmaAllocation vmaAllocation;

		VkResult result = vmaCreateImage(vmaAllocator, &imageCreateInfo, &allocInfo, &vkImage, &vmaAllocation, nullptr);
		if (result != VK_SUCCESS)
		{
			logger.Error("error on create texture: {} ", string_VkResult(result));
			return nullptr;
		}

		VulkanTexture* texture = Alloc<VulkanTexture>();
		texture->vulkanDevice = this;
		texture->desc = desc;
		texture->image = vkImage;
		texture->allocation = vmaAllocation;
		texture->isDepth = IsDepthFormat(imageCreateInfo.format);

		TextureViewDesc textureViewDesc;
		textureViewDesc.texture = texture;
		textureViewDesc.type = GetTextureViewType(desc.cubemap, desc.extent.depth, desc.extent.height, desc.arrayLayers);

		texture->textureView = CreateTextureView(textureViewDesc);

		SetObjectName(*this, VK_OBJECT_TYPE_IMAGE, PtrToInt(texture->image), desc.debugName);

		return texture;
	}

	GPUTextureView* VulkanDevice::CreateTextureView(const TextureViewDesc& desc)
	{
		VulkanTexture* texture = static_cast<VulkanTexture*>(desc.texture);

		VkImageViewCreateInfo imageViewCreateInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		imageViewCreateInfo.image = texture->image;
		imageViewCreateInfo.viewType = GetImageViewType(desc.type);
		imageViewCreateInfo.format = ToVkFormat(texture->desc.format);

		// Default component mapping (identity)
		imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		imageViewCreateInfo.subresourceRange.aspectMask = GetImageAspectFlags(ToVkFormat(texture->desc.format));

		// Set mip level range
		u32 mipLevelCount = desc.mipLevelCount;
		if (mipLevelCount == U32_MAX || mipLevelCount > texture->desc.mipLevels - desc.baseMipLevel)
		{
			mipLevelCount = texture->desc.mipLevels - desc.baseMipLevel;
		}

		imageViewCreateInfo.subresourceRange.baseMipLevel = desc.baseMipLevel;
		imageViewCreateInfo.subresourceRange.levelCount = mipLevelCount;

		u32 arrayLayerCount = desc.arrayLayerCount;
		if (arrayLayerCount == U32_MAX || arrayLayerCount > texture->desc.arrayLayers - desc.baseArrayLayer)
		{
			arrayLayerCount = texture->desc.arrayLayers - desc.baseArrayLayer;
		}

		imageViewCreateInfo.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
		imageViewCreateInfo.subresourceRange.layerCount = arrayLayerCount;

		VkImageView vkImageView;
		VkResult    result = vkCreateImageView(device, &imageViewCreateInfo, nullptr, &vkImageView);
		if (result != VK_SUCCESS)
		{
			logger.Error("error on create image view: {} ", string_VkResult(result));
			return nullptr;
		}

		VulkanTextureView* textureView = Alloc<VulkanTextureView>();
		textureView->vulkanDevice = this;
		textureView->desc = desc;
		textureView->imageView = vkImageView;
		textureView->texture = texture;

		SetObjectName(*this, VK_OBJECT_TYPE_IMAGE_VIEW, PtrToInt(textureView->imageView), desc.debugName);

		return textureView;
	}

	GPUSampler* VulkanDevice::CreateSampler(const SamplerDesc& desc)
	{
		VkSamplerCreateInfo createInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
		createInfo.minFilter = desc.minFilter == FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		createInfo.magFilter = desc.magFilter == FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		createInfo.mipmapMode = desc.mipmapFilter == FilterMode::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		createInfo.addressModeU = ConvertAddressMode(desc.addressModeU);
		createInfo.addressModeV = ConvertAddressMode(desc.addressModeV);
		createInfo.addressModeW = ConvertAddressMode(desc.addressModeW);
		createInfo.mipLodBias = desc.mipLodBias;
		createInfo.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
		createInfo.maxAnisotropy = desc.maxAnisotropy;
		createInfo.compareEnable = (desc.compareEnable || desc.minFilter == FilterMode::Linear || desc.magFilter == FilterMode::Linear) ? VK_TRUE : VK_FALSE;
		createInfo.compareOp = ConvertCompareOp(desc.compareOp);
		createInfo.minLod = desc.minLod;
		createInfo.maxLod = desc.maxLod;
		createInfo.borderColor = ConvertBorderColor(desc.borderColor);
		createInfo.unnormalizedCoordinates = VK_FALSE;

		VkSampler vkSampler;
		VkResult  result = vkCreateSampler(device, &createInfo, nullptr, &vkSampler);

		if (result != VK_SUCCESS)
		{
			logger.Error("error on create sampler {} ", string_VkResult(result));
			return nullptr;
		}
		VulkanSampler* sampler = Alloc<VulkanSampler>();
		sampler->vulkanDevice = this;
		sampler->desc = desc;
		sampler->sampler = vkSampler;

		SetObjectName(*this, VK_OBJECT_TYPE_SAMPLER, PtrToInt(sampler->sampler), desc.debugName);

		return sampler;
	}

	bool VulkanSwapchain::ValidSwapchain() const
	{
		return !Platform::IsWindowMinimized(desc.window);
	}

	PipelineBindPoint VulkanPipeline::GetBindPoint() const
	{
		switch (bindPoint)
		{
			case VK_PIPELINE_BIND_POINT_GRAPHICS: return PipelineBindPoint::Graphics;
			case VK_PIPELINE_BIND_POINT_COMPUTE: return PipelineBindPoint::Compute;
			case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR: return PipelineBindPoint::RayTracing;
		}
		return PipelineBindPoint::Graphics;
	}

	const PipelineDesc& VulkanPipeline::GetPipelineDesc() const
	{
		return pipelineDesc;
	}

	void VulkanPipeline::Destroy()
	{
		VulkanResourceDestructor destructor;
		destructor.pipeline = pipeline;
		destructor.pipelineLayout = pipelineLayout;
		destructor.descriptorSetLayouts = Traits::Move(descriptorSetLayouts);
		destructor.buffer = sbtBuffer;
		destructor.allocation = sbtAllocation;
		EnqueueDestructor(destructor);

		DestroyAndFree(this);
	}

	GPUPipeline* VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
	{
		SK_ASSERT(desc.renderPass, "render pass is required");
		SK_ASSERT(desc.shader, "shader variant is required");

		VulkanRenderPass* vulkanRenderPass = static_cast<VulkanRenderPass*>(desc.renderPass);

		RID variant = ShaderResource::GetVariant(desc.shader, desc.variant);
		SK_ASSERT(variant, "variant not found");

		ResourceObject variantObject = Resources::Read(variant);

		PipelineDesc           pipelineDesc;
		Array<ShaderStageInfo> stages;

		GetShaderInfoFromResource(variant, &pipelineDesc, &stages);

		u32 stride = desc.vertexInputStride != U32_MAX ? desc.vertexInputStride : pipelineDesc.stride;

		VkPipelineLayout             vkPipelineLayout;
		VkPipeline                   vkPipeline;
		Array<VkDescriptorSetLayout> descriptorSetLayouts;

		CreatePipelineLayout(device, pipelineDesc.descriptors, pipelineDesc.pushConstants, &vkPipelineLayout, descriptorSetLayouts, desc.allowImmediateSet, desc.descriptorSetsOverride);

		Array<VkPipelineColorBlendAttachmentState> attachments{};
		Array<VkVertexInputAttributeDescription>   attributeDescriptions{};

		Array<VkShaderModule> shaderModules{};
		shaderModules.Resize(stages.Size());
		Array<VkPipelineShaderStageCreateInfo> shaderStages{};
		shaderStages.Resize(stages.Size());

		Span bytes = variantObject.GetBlob(ShaderVariantResource::Spriv);

		for (u32 i = 0; i < stages.Size(); ++i)
		{
			const ShaderStageInfo& shaderStageAsset = stages[i];

			Span<u8> data = Span<u8>{
				bytes.begin() + shaderStageAsset.offset,
				bytes.begin() + shaderStageAsset.offset + shaderStageAsset.size
			};

			VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			createInfo.codeSize = data.Size();
			createInfo.pCode = reinterpret_cast<const u32*>(data.Data());
			vkCreateShaderModule(device, &createInfo, nullptr, &shaderModules[i]);

			shaderStages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[i].module = shaderModules[i];
			shaderStages[i].pName = shaderStageAsset.entryPoint.CStr();
			shaderStages[i].stage = static_cast<VkShaderStageFlagBits>(ConvertShaderStageFlags(shaderStageAsset.stage));
		}


		for (const auto& input : pipelineDesc.inputVariables)
		{
			attributeDescriptions.EmplaceBack(VkVertexInputAttributeDescription{
				.location = input.location,
				.binding = 0,
				.format = ToVkFormat(input.format),
				.offset = input.offset
			});
		}


		VkVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = stride;
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;


		VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		if (bindingDescription.stride > 0)
		{
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		}
		else
		{
			vertexInputInfo.vertexBindingDescriptionCount = 0;
		}

		if (!attributeDescriptions.Empty())
		{
			vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.Size();
			vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.Data();
		}
		else
		{
			vertexInputInfo.vertexAttributeDescriptionCount = 0;
		}

		// Set up input assembly state
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		inputAssemblyInfo.topology = ConvertPrimitiveTopology(desc.topology);
		inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

		// Set up rasterization state
		VkPipelineRasterizationStateCreateInfo rasterizationInfo{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rasterizationInfo.depthClampEnable = desc.rasterizerState.depthClampEnable ? VK_TRUE : VK_FALSE;
		rasterizationInfo.rasterizerDiscardEnable = desc.rasterizerState.rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;
		rasterizationInfo.polygonMode = ConvertPolygonMode(desc.rasterizerState.polygonMode);
		rasterizationInfo.cullMode = ConvertCullMode(desc.rasterizerState.cullMode);
		rasterizationInfo.frontFace = ConvertFrontFace(desc.rasterizerState.frontFace);
		rasterizationInfo.depthBiasEnable = desc.rasterizerState.depthBiasEnable ? VK_TRUE : VK_FALSE;
		rasterizationInfo.depthBiasConstantFactor = desc.rasterizerState.depthBiasConstantFactor;
		rasterizationInfo.depthBiasClamp = desc.rasterizerState.depthBiasClamp;
		rasterizationInfo.depthBiasSlopeFactor = desc.rasterizerState.depthBiasSlopeFactor;
		rasterizationInfo.lineWidth = desc.rasterizerState.lineWidth;


		VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterStateCI{};
		conservativeRasterStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
		conservativeRasterStateCI.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;	//TODO cast ConservativeRasterizationMode
		conservativeRasterStateCI.extraPrimitiveOverestimationSize = selectedAdapter->conservativeRasterProps.maxExtraPrimitiveOverestimationSize;

		if (desc.conservativeRasterizationMode != ConservativeRasterizationMode::Disabled)
		{
			rasterizationInfo.pNext = &conservativeRasterStateCI;
		}

		// Set up multisample state
		VkPipelineMultisampleStateCreateInfo multisampleInfo{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		if (vulkanRenderPass->samplesCount > 1)
		{
			//multisampleInfo.sampleShadingEnable = VK_TRUE;
			multisampleInfo.rasterizationSamples = CastSampleCount(vulkanRenderPass->samplesCount);
			//multisampleInfo.minSampleShading = 0.2f;
		}
		else
		{
			multisampleInfo.sampleShadingEnable = selectedAdapter->deviceFeatures.features.sampleRateShading;
			multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			multisampleInfo.minSampleShading = 1.0f;
		}

		multisampleInfo.pSampleMask = nullptr;
		multisampleInfo.alphaToCoverageEnable = VK_FALSE;
		multisampleInfo.alphaToOneEnable = VK_FALSE;

		// Set up depth stencil state
		VkPipelineDepthStencilStateCreateInfo depthStencilInfo{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		depthStencilInfo.depthTestEnable = desc.depthStencilState.depthTestEnable ? VK_TRUE : VK_FALSE;
		depthStencilInfo.depthWriteEnable = desc.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
		depthStencilInfo.depthCompareOp = ConvertCompareOp(desc.depthStencilState.depthCompareOp);
		depthStencilInfo.depthBoundsTestEnable = desc.depthStencilState.depthBoundsTestEnable ? VK_TRUE : VK_FALSE;
		depthStencilInfo.stencilTestEnable = desc.depthStencilState.stencilTestEnable ? VK_TRUE : VK_FALSE;

		// Front stencil state
		const auto& frontStencil = desc.depthStencilState.front;
		depthStencilInfo.front.failOp = ConvertStencilOp(frontStencil.failOp);
		depthStencilInfo.front.passOp = ConvertStencilOp(frontStencil.passOp);
		depthStencilInfo.front.depthFailOp = ConvertStencilOp(frontStencil.depthFailOp);
		depthStencilInfo.front.compareOp = ConvertCompareOp(frontStencil.compareOp);

		depthStencilInfo.front.compareMask = frontStencil.compareMask;
		depthStencilInfo.front.writeMask = frontStencil.writeMask;
		depthStencilInfo.front.reference = frontStencil.reference;

		// Back stencil state
		const auto& backStencil = desc.depthStencilState.back;
		depthStencilInfo.back.failOp = ConvertStencilOp(backStencil.failOp);
		depthStencilInfo.back.passOp = ConvertStencilOp(backStencil.passOp);
		depthStencilInfo.back.depthFailOp = ConvertStencilOp(backStencil.depthFailOp);
		depthStencilInfo.back.compareOp = ConvertCompareOp(backStencil.compareOp);
		depthStencilInfo.back.compareMask = backStencil.compareMask;
		depthStencilInfo.back.writeMask = backStencil.writeMask;
		depthStencilInfo.back.reference = backStencil.reference;
		depthStencilInfo.minDepthBounds = desc.depthStencilState.minDepthBounds;
		depthStencilInfo.maxDepthBounds = desc.depthStencilState.maxDepthBounds;

		// Set up color blend state
		Array<VkPipelineColorBlendAttachmentState> colorBlendAttachments(desc.blendStates.Size());
		for (u32 i = 0; i < desc.blendStates.Size(); ++i)
		{
			const auto& blendState = desc.blendStates[i];
			colorBlendAttachments[i].blendEnable = blendState.blendEnable ? VK_TRUE : VK_FALSE;
			colorBlendAttachments[i].srcColorBlendFactor = ConvertBlendFactor(blendState.srcColorBlendFactor);
			colorBlendAttachments[i].dstColorBlendFactor = ConvertBlendFactor(blendState.dstColorBlendFactor);
			colorBlendAttachments[i].colorBlendOp = ConvertBlendOp(blendState.colorBlendOp);
			colorBlendAttachments[i].srcAlphaBlendFactor = ConvertBlendFactor(blendState.srcAlphaBlendFactor);
			colorBlendAttachments[i].dstAlphaBlendFactor = ConvertBlendFactor(blendState.dstAlphaBlendFactor);
			colorBlendAttachments[i].alphaBlendOp = ConvertBlendOp(blendState.alphaBlendOp);

			// Color write mask
			colorBlendAttachments[i].colorWriteMask = 0;
			if ((static_cast<u32>(blendState.colorWriteMask) & static_cast<u32>(ColorMask::Red)) != 0)
				colorBlendAttachments[i].colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
			if ((static_cast<u32>(blendState.colorWriteMask) & static_cast<u32>(ColorMask::Green)) != 0)
				colorBlendAttachments[i].colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
			if ((static_cast<u32>(blendState.colorWriteMask) & static_cast<u32>(ColorMask::Blue)) != 0)
				colorBlendAttachments[i].colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
			if ((static_cast<u32>(blendState.colorWriteMask) & static_cast<u32>(ColorMask::Alpha)) != 0)
				colorBlendAttachments[i].colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
		}

		VkPipelineColorBlendStateCreateInfo colorBlendInfo{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		colorBlendInfo.logicOpEnable = VK_FALSE;
		colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;
		colorBlendInfo.attachmentCount = static_cast<u32>(colorBlendAttachments.Size());
		colorBlendInfo.pAttachments = colorBlendAttachments.Data();
		colorBlendInfo.blendConstants[0] = 0.0f;
		colorBlendInfo.blendConstants[1] = 0.0f;
		colorBlendInfo.blendConstants[2] = 0.0f;
		colorBlendInfo.blendConstants[3] = 0.0f;

		// Set up dynamic state (viewport and scissor are dynamic)
		VkDynamicState                   dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamicStateInfo{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
		dynamicStateInfo.dynamicStateCount = 2;
		dynamicStateInfo.pDynamicStates = dynamicStates;

		// Set up viewport state (placeholder values, will be set dynamically)
		VkPipelineViewportStateCreateInfo viewportStateInfo{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		viewportStateInfo.viewportCount = 1;
		viewportStateInfo.pViewports = nullptr;
		viewportStateInfo.scissorCount = 1;
		viewportStateInfo.pScissors = nullptr;

		// Create the graphics pipeline
		VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		pipelineInfo.stageCount = static_cast<u32>(shaderStages.Size());
		pipelineInfo.pStages = shaderStages.Data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
		pipelineInfo.pViewportState = &viewportStateInfo;
		pipelineInfo.pRasterizationState = &rasterizationInfo;
		pipelineInfo.pMultisampleState = &multisampleInfo;
		pipelineInfo.pDepthStencilState = &depthStencilInfo;
		pipelineInfo.pColorBlendState = &colorBlendInfo;
		pipelineInfo.pDynamicState = &dynamicStateInfo;
		pipelineInfo.layout = vkPipelineLayout;
		pipelineInfo.renderPass = vulkanRenderPass->renderPass;

		VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vkPipeline);
		if (result != VK_SUCCESS)
		{
			logger.Error("Error on create graphics pipeline {}", string_VkResult(result));
			return nullptr;
		}

		for (VkShaderModule shaderModule : shaderModules)
		{
			vkDestroyShaderModule(device, shaderModule, nullptr);
		}

		VulkanPipeline* pipeline = Alloc<VulkanPipeline>();
		pipeline->vulkanDevice = this;
		pipeline->pipeline = vkPipeline;
		pipeline->pipelineDesc = pipelineDesc;
		pipeline->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		pipeline->pipelineLayout = vkPipelineLayout;
		pipeline->descriptorSetLayouts = descriptorSetLayouts;

		return pipeline;
	}

	GPUPipeline* VulkanDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
	{
		SK_ASSERT(desc.shader, "shader variant is required");

		RID variant = ShaderResource::GetVariant(desc.shader, desc.variant);
		SK_ASSERT(variant, "variant not found");

		//TODO - need to get from variantObject
		PipelineDesc           pipelineDesc;
		Array<ShaderStageInfo> stages;

		GetShaderInfoFromResource(variant, &pipelineDesc, &stages);

		VkPipelineLayout             vkPipelineLayout;
		VkPipeline                   vkPipeline;
		Array<VkDescriptorSetLayout> descriptorSetLayouts;

		//Create pipeline layout (shared with graphics pipeline creation)
		CreatePipelineLayout(device, pipelineDesc.descriptors, pipelineDesc.pushConstants, &vkPipelineLayout, descriptorSetLayouts, desc.allowImmediateSet, desc.descriptorSetsOverride);

		// Find the compute shader stage
		ShaderStageInfo computeStageInfo;
		bool            foundComputeStage = false;

		for (u32 i = 0; i < stages.Size(); ++i)
		{
			const ShaderStageInfo& stageInfo = stages[i];
			if (stageInfo.stage == ShaderStage::Compute)
			{
				computeStageInfo = stageInfo;
				foundComputeStage = true;
				break;
			}
		}

		if (!foundComputeStage)
		{
			logger.Error("Compute shader not found in shader variant");
			vkDestroyPipelineLayout(device, vkPipelineLayout, nullptr);
			return nullptr;
		}

		// Create compute shader module
		VkShaderModule computeShaderModule;

		ResourceObject variantObject = Resources::Read(variant);
		Span           bytes = variantObject.GetBlob(ShaderVariantResource::Spriv);

		Span<u8> shaderData = Span<u8>{
			bytes.begin() + computeStageInfo.offset,
			bytes.begin() + computeStageInfo.offset + computeStageInfo.size
		};

		VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		createInfo.codeSize = shaderData.Size();
		createInfo.pCode = reinterpret_cast<const u32*>(shaderData.Data());

		VkResult moduleResult = vkCreateShaderModule(device, &createInfo, nullptr, &computeShaderModule);
		if (moduleResult != VK_SUCCESS)
		{
			logger.Error("Failed to create compute shader module: {}", string_VkResult(moduleResult));
			vkDestroyPipelineLayout(device, vkPipelineLayout, nullptr);
			return nullptr;
		}

		// Set up pipeline shader stage
		VkPipelineShaderStageCreateInfo shaderStageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		shaderStageInfo.module = computeShaderModule;
		shaderStageInfo.pName = computeStageInfo.entryPoint.CStr();

		// Create compute  pipeline
		VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
		pipelineInfo.stage = shaderStageInfo;
		pipelineInfo.layout = vkPipelineLayout;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineInfo.basePipelineIndex = -1;

		VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vkPipeline);

		// Clean up shader module
		vkDestroyShaderModule(device, computeShaderModule, nullptr);

		if (result != VK_SUCCESS)
		{
			logger.Error("Error on create compute pipeline: {}", string_VkResult(result));
			vkDestroyPipelineLayout(device, vkPipelineLayout, nullptr);
			return nullptr;
		}

		// Create and return the pipeline object
		VulkanPipeline* pipeline = Alloc<VulkanPipeline>();
		pipeline->vulkanDevice = this;
		pipeline->pipeline = vkPipeline;
		pipeline->pipelineDesc = pipelineDesc;
		pipeline->bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		pipeline->pipelineLayout = vkPipelineLayout;
		pipeline->descriptorSetLayouts = descriptorSetLayouts;

		SetObjectName(*this, VK_OBJECT_TYPE_PIPELINE, PtrToInt(pipeline->pipeline), desc.debugName);

		return pipeline;
	}

	GPUPipeline* VulkanDevice::CreateRayTracingPipeline(const RayTracingPipelineDesc& desc)
	{
		SK_ASSERT(desc.shader, "shader is required");

		RID variant = ShaderResource::GetVariant(desc.shader, desc.variant);
		SK_ASSERT(variant, "variant not found");

		PipelineDesc           pipelineDesc;
		Array<ShaderStageInfo> stages;

		GetShaderInfoFromResource(variant, &pipelineDesc, &stages);

		VkPipelineLayout             vkPipelineLayout;
		Array<VkDescriptorSetLayout> descriptorSetLayouts;

		CreatePipelineLayout(device, pipelineDesc.descriptors, pipelineDesc.pushConstants, &vkPipelineLayout, descriptorSetLayouts, false, desc.descriptorSetsOverride);

		ResourceObject variantObject = Resources::Read(variant);
		Span           bytes = variantObject.GetBlob(ShaderVariantResource::Spriv);

		// Create shader modules and stage create infos
		Array<VkShaderModule>                     shaderModules;
		Array<VkPipelineShaderStageCreateInfo>     shaderStages;
		Array<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
		Array<u32>                                 groupHitSlot;

		// Track group indices for SBT
		u32 raygenCount = 0;
		u32 missCount = 0;
		u32 hitCount = 0;
		u32 callableCount = 0;
		u32 maxHitSlot = 0;

		for (u32 i = 0; i < stages.Size(); ++i)
		{
			const ShaderStageInfo& stageInfo = stages[i];
			u32 hitSlot = 0;

			Span<u8> shaderData = Span<u8>{
				bytes.begin() + stageInfo.offset,
				bytes.begin() + stageInfo.offset + stageInfo.size
			};

			VkShaderModuleCreateInfo moduleCreateInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			moduleCreateInfo.codeSize = shaderData.Size();
			moduleCreateInfo.pCode = reinterpret_cast<const u32*>(shaderData.Data());

			VkShaderModule shaderModule;
			VkResult moduleResult = vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule);
			if (moduleResult != VK_SUCCESS)
			{
				logger.Error("Failed to create RT shader module for stage {}: {}", i, string_VkResult(moduleResult));
				for (auto& mod : shaderModules) vkDestroyShaderModule(device, mod, nullptr);
				vkDestroyPipelineLayout(device, vkPipelineLayout, nullptr);
				return nullptr;
			}
			shaderModules.EmplaceBack(shaderModule);

			VkPipelineShaderStageCreateInfo stageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
			stageCreateInfo.module = shaderModule;
			stageCreateInfo.pName = stageInfo.entryPoint.CStr();

			VkRayTracingShaderGroupCreateInfoKHR groupInfo{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
			groupInfo.generalShader = VK_SHADER_UNUSED_KHR;
			groupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
			groupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
			groupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;

			switch (stageInfo.stage)
			{
				case ShaderStage::RayGen:
					stageCreateInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
					groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
					groupInfo.generalShader = i;
					raygenCount++;
					break;
				case ShaderStage::Miss:
					stageCreateInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
					groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
					groupInfo.generalShader = i;
					missCount++;
					break;
				case ShaderStage::ClosestHit:
					stageCreateInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
					groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
					groupInfo.closestHitShader = i;
					hitSlot = stageInfo.hitGroup;
					if (hitSlot > maxHitSlot) maxHitSlot = hitSlot;
					hitCount++;
					break;
				case ShaderStage::AnyHit:
					stageCreateInfo.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
					groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
					groupInfo.anyHitShader = i;
					hitSlot = stageInfo.hitGroup;
					if (hitSlot > maxHitSlot) maxHitSlot = hitSlot;
					hitCount++;
					break;
				case ShaderStage::Intersection:
					stageCreateInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
					groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
					groupInfo.intersectionShader = i;
					hitSlot = stageInfo.hitGroup;
					if (hitSlot > maxHitSlot) maxHitSlot = hitSlot;
					hitCount++;
					break;
				case ShaderStage::Callable:
					stageCreateInfo.stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
					groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
					groupInfo.generalShader = i;
					callableCount++;
					break;
				default:
					logger.Warn("Unknown RT shader stage in pipeline");
					continue;
			}

			shaderStages.EmplaceBack(stageCreateInfo);
			shaderGroups.EmplaceBack(groupInfo);
			groupHitSlot.EmplaceBack(hitSlot);
		}

		// Create ray tracing pipeline
		VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
		pipelineCreateInfo.stageCount = static_cast<u32>(shaderStages.Size());
		pipelineCreateInfo.pStages = shaderStages.Data();
		pipelineCreateInfo.groupCount = static_cast<u32>(shaderGroups.Size());
		pipelineCreateInfo.pGroups = shaderGroups.Data();
		pipelineCreateInfo.maxPipelineRayRecursionDepth = desc.maxRecursionDepth;
		pipelineCreateInfo.layout = vkPipelineLayout;

		VkPipeline vkPipeline;
		VkResult result = vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &vkPipeline);

		// Clean up shader modules
		for (auto& mod : shaderModules)
		{
			vkDestroyShaderModule(device, mod, nullptr);
		}

		if (result != VK_SUCCESS)
		{
			logger.Error("Failed to create ray tracing pipeline: {}", string_VkResult(result));
			vkDestroyPipelineLayout(device, vkPipelineLayout, nullptr);
			return nullptr;
		}

		// Build SBT
		const auto& rtProps = selectedAdapter->rayTracingPipelineProperties;
		u32 handleSize = rtProps.shaderGroupHandleSize;
		u32 handleAlignment = rtProps.shaderGroupHandleAlignment;
		u32 baseAlignment = rtProps.shaderGroupBaseAlignment;
		u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

		u32 totalGroupCount = static_cast<u32>(shaderGroups.Size());

		// Get shader group handles
		u32 handleStorageSize = totalGroupCount * handleSize;
		Array<u8> handleStorage;
		handleStorage.Resize(handleStorageSize);
		result = vkGetRayTracingShaderGroupHandlesKHR(device, vkPipeline, 0, totalGroupCount, handleStorageSize, handleStorage.Data());
		if (result != VK_SUCCESS)
		{
			logger.Error("Failed to get RT shader group handles: {}", string_VkResult(result));
			vkDestroyPipeline(device, vkPipeline, nullptr);
			vkDestroyPipelineLayout(device, vkPipelineLayout, nullptr);
			return nullptr;
		}

		// Calculate SBT region sizes (each aligned to baseAlignment)
		auto alignUp = [](u32 value, u32 alignment) -> u32
		{
			return (value + alignment - 1) & ~(alignment - 1);
		};

		u32 hitSlotCount = hitCount > 0 ? maxHitSlot + 1 : 0;

		u32 raygenRegionSize = alignUp(raygenCount * handleSizeAligned, baseAlignment);
		u32 missRegionSize = alignUp(missCount * handleSizeAligned, baseAlignment);
		u32 hitRegionSize = alignUp(hitSlotCount * handleSizeAligned, baseAlignment);
		u32 callableRegionSize = alignUp(callableCount * handleSizeAligned, baseAlignment);
		u32 sbtSize = raygenRegionSize + missRegionSize + hitRegionSize + callableRegionSize;

		// Create SBT buffer
		VkBufferCreateInfo sbtBufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		sbtBufInfo.size = sbtSize;
		sbtBufInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		VmaAllocationCreateInfo sbtAllocInfo{};
		sbtAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		sbtAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer sbtBuffer;
		VmaAllocation sbtAllocation;
		VmaAllocationInfo sbtAllocResult{};
		result = vmaCreateBuffer(vmaAllocator, &sbtBufInfo, &sbtAllocInfo, &sbtBuffer, &sbtAllocation, &sbtAllocResult);
		if (result != VK_SUCCESS)
		{
			logger.Error("Failed to create SBT buffer: {}", string_VkResult(result));
			vkDestroyPipeline(device, vkPipeline, nullptr);
			vkDestroyPipelineLayout(device, vkPipelineLayout, nullptr);
			return nullptr;
		}

		// Copy handles into SBT buffer
		u8* sbtData = static_cast<u8*>(sbtAllocResult.pMappedData);
		memset(sbtData, 0, sbtSize);

		u32 groupIndex = 0;
		u32 sbtOffset = 0;

		// Raygen region
		for (u32 i = 0; i < raygenCount && groupIndex < totalGroupCount; ++groupIndex)
		{
			if (shaderGroups[groupIndex].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR &&
				shaderStages[groupIndex].stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR)
			{
				memcpy(sbtData + sbtOffset + i * handleSizeAligned, handleStorage.Data() + groupIndex * handleSize, handleSize);
				i++;
			}
		}
		sbtOffset += raygenRegionSize;

		// Miss region
		groupIndex = 0;
		for (u32 i = 0; i < missCount && groupIndex < totalGroupCount; ++groupIndex)
		{
			if (shaderGroups[groupIndex].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR &&
				shaderStages[groupIndex].stage == VK_SHADER_STAGE_MISS_BIT_KHR)
			{
				memcpy(sbtData + sbtOffset + i * handleSizeAligned, handleStorage.Data() + groupIndex * handleSize, handleSize);
				i++;
			}
		}
		sbtOffset += missRegionSize;

		// Hit region
		for (groupIndex = 0; groupIndex < totalGroupCount; ++groupIndex)
		{
			if (shaderGroups[groupIndex].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR ||
				shaderGroups[groupIndex].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR)
			{
				memcpy(sbtData + sbtOffset + groupHitSlot[groupIndex] * handleSizeAligned, handleStorage.Data() + groupIndex * handleSize, handleSize);
			}
		}
		sbtOffset += hitRegionSize;

		// Callable region
		groupIndex = 0;
		for (u32 i = 0; i < callableCount && groupIndex < totalGroupCount; ++groupIndex)
		{
			if (shaderGroups[groupIndex].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR &&
				shaderStages[groupIndex].stage == VK_SHADER_STAGE_CALLABLE_BIT_KHR)
			{
				memcpy(sbtData + sbtOffset + i * handleSizeAligned, handleStorage.Data() + groupIndex * handleSize, handleSize);
				i++;
			}
		}

		VkDeviceAddress sbtAddress = GetBufferDeviceAddress(device, sbtBuffer);

		VulkanPipeline* pipeline = Alloc<VulkanPipeline>();
		pipeline->vulkanDevice = this;
		pipeline->pipeline = vkPipeline;
		pipeline->pipelineDesc = pipelineDesc;
		pipeline->bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
		pipeline->pipelineLayout = vkPipelineLayout;
		pipeline->descriptorSetLayouts = descriptorSetLayouts;
		pipeline->sbtBuffer = sbtBuffer;
		pipeline->sbtAllocation = sbtAllocation;

		u32 regionOffset = 0;
		pipeline->sbtRaygenRegion.deviceAddress = sbtAddress + regionOffset;
		pipeline->sbtRaygenRegion.stride = handleSizeAligned;
		pipeline->sbtRaygenRegion.size = handleSizeAligned;
		regionOffset += raygenRegionSize;

		pipeline->sbtMissRegion.deviceAddress = missCount > 0 ? sbtAddress + regionOffset : 0;
		pipeline->sbtMissRegion.stride = handleSizeAligned;
		pipeline->sbtMissRegion.size = missRegionSize;
		regionOffset += missRegionSize;

		pipeline->sbtHitRegion.deviceAddress = hitCount > 0 ? sbtAddress + regionOffset : 0;
		pipeline->sbtHitRegion.stride = handleSizeAligned;
		pipeline->sbtHitRegion.size = hitRegionSize;
		regionOffset += hitRegionSize;

		pipeline->sbtCallableRegion.deviceAddress = callableCount > 0 ? sbtAddress + regionOffset : 0;
		pipeline->sbtCallableRegion.stride = handleSizeAligned;
		pipeline->sbtCallableRegion.size = callableRegionSize;

		SetObjectName(*this, VK_OBJECT_TYPE_PIPELINE, PtrToInt(pipeline->pipeline), desc.debugName);

		return pipeline;
	}

	const DescriptorSetDesc& VulkanDescriptorSet::GetDesc() const
	{
		return desc;
	}

	void VulkanDescriptorSet::Update(const DescriptorUpdate& update)
	{
		// Prepare the descriptor write
		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = descriptorSet;
		write.dstBinding = update.binding;
		write.descriptorType = ConvertDescriptorType(update.type);
		write.dstArrayElement = update.arrayElement;
		write.descriptorCount = 1;

		VkDescriptorBufferInfo                       bufferInfo{};
		VkDescriptorImageInfo                        imageInfo{};
		VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};

		switch (update.type)
		{
			case DescriptorType::SampledImage:
			case DescriptorType::StorageImage:
			{
				SK_ASSERT(update.texture || update.textureView, "texture or texture view is required");

				bool depthFormat = false;

				if (update.texture)
				{
					VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(update.texture);
					imageInfo.imageView = vulkanTexture->GetImageView();
					depthFormat = vulkanTexture->isDepth;
				}
				else if (update.textureView)
				{
					VulkanTextureView* vulkanTextureView = static_cast<VulkanTextureView*>(update.textureView);
					depthFormat = vulkanTextureView->texture->isDepth;
					imageInfo.imageView = vulkanTextureView->imageView;
				}

				imageInfo.imageLayout = depthFormat
					                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
					                        : update.type == DescriptorType::StorageImage
					                        ? VK_IMAGE_LAYOUT_GENERAL
					                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				write.pImageInfo = &imageInfo;
				break;
			}
			case DescriptorType::Sampler:
			{
				SK_ASSERT(update.sampler, "sampler is required");
				imageInfo.sampler = static_cast<VulkanSampler*>(update.sampler)->sampler;
				write.pImageInfo = &imageInfo;
				break;
			}
			case DescriptorType::UniformBuffer:
			case DescriptorType::StorageBuffer:
			case DescriptorType::UniformBufferDynamic:
			case DescriptorType::StorageBufferDynamic:
			{
				SK_ASSERT(update.buffer, "buffer is required");
				bufferInfo.buffer = static_cast<VulkanBuffer*>(update.buffer)->buffer;
				bufferInfo.offset = update.bufferOffset;
				bufferInfo.range = update.bufferRange > 0 ? update.bufferRange : VK_WHOLE_SIZE;
				write.pBufferInfo = &bufferInfo;
				break;
			}
			case DescriptorType::AccelerationStructure:
			{
				SK_ASSERT(update.topLevelAS, "top level acceleration structure is required");
				VulkanTopLevelAS* vkTlas = static_cast<VulkanTopLevelAS*>(update.topLevelAS);
				accelerationStructureWrite.accelerationStructureCount = 1;
				accelerationStructureWrite.pAccelerationStructures = &vkTlas->accelerationStructure;
				write.pNext = &accelerationStructureWrite;
				break;
			}
			default:
			{
				SK_ASSERT(false, "unsupported descriptor type");
				return;
			}
		}

		vkUpdateDescriptorSets(vulkanDevice->device, 1, &write, 0, nullptr);
	}

	void VulkanDescriptorSet::UpdateBuffer(u32 binding, GPUBuffer* buffer, usize offset, usize size)
	{
		DescriptorSetLayoutBinding& layout = desc.bindings[binding];

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = descriptorSet;
		write.dstBinding = binding;
		write.descriptorType = ConvertDescriptorType(layout.descriptorType);
		write.dstArrayElement = 0;
		write.descriptorCount = 1;

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = static_cast<VulkanBuffer*>(buffer)->buffer;
		bufferInfo.offset = offset;
		bufferInfo.range = size > 0 ? size : VK_WHOLE_SIZE;

		write.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(vulkanDevice->device, 1, &write, 0, nullptr);
	}

	void VulkanDescriptorSet::UpdateTexture(u32 binding, GPUTexture* texture)
	{
		UpdateTexture(binding, texture, 0);
	}

	void VulkanDescriptorSet::UpdateTexture(u32 binding, GPUTexture* texture, u32 arrayElement)
	{
		SK_ASSERT(texture, "texture is required");
		InternalUpdateTexture(binding, texture, nullptr, arrayElement);
	}

	void VulkanDescriptorSet::UpdateTextureView(u32 binding, GPUTextureView* textureView, u32 arrayElement)
	{
		SK_ASSERT(textureView, "texture view is required");
		InternalUpdateTexture(binding, nullptr, textureView, arrayElement);
	}

	void VulkanDescriptorSet::UpdateTextureView(u32 binding, GPUTextureView* textureView)
	{
		UpdateTextureView(binding, textureView, 0);
	}

	void VulkanDescriptorSet::UpdateSampler(u32 binding, GPUSampler* sampler)
	{
		UpdateSampler(binding, sampler, 0);
	}

	void VulkanDescriptorSet::UpdateSampler(u32 binding, GPUSampler* sampler, u32 arrayElement)
	{
		SK_ASSERT(sampler, "sampler is required");

		DescriptorSetLayoutBinding& layout = desc.bindings[binding];

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = descriptorSet;
		write.dstBinding = binding;
		write.descriptorType = ConvertDescriptorType(layout.descriptorType);
		write.dstArrayElement = arrayElement;
		write.descriptorCount = 1;

		VkDescriptorImageInfo imageInfo{};
		write.pImageInfo = &imageInfo;
		imageInfo.sampler = static_cast<VulkanSampler*>(sampler)->sampler;

		vkUpdateDescriptorSets(vulkanDevice->device, 1, &write, 0, nullptr);
	}

	void VulkanDescriptorSet::Destroy()
	{
		VulkanResourceDestructor destructor;
		destructor.descriptorSet = descriptorSet;
		destructor.descriptorSetLayouts.EmplaceBack(descriptorSetLayout);
		destructor.descriptorPool = dedicatedPool;
		EnqueueDestructor(destructor);

		DestroyAndFree(this);
	}

	GPUDescriptorSet* VulkanDevice::CreateDescriptorSet(const DescriptorSetDesc& desc)
	{
		VkDescriptorSetLayout layout;
		bool                  hasRuntimeArray = false;
		CreateDescriptorSetLayout(device, desc.bindings, &layout, &hasRuntimeArray);

		VkDescriptorPool      dedicatedPool = nullptr;

		if (hasRuntimeArray)
		{
			// Create a dedicated pool sized exactly for this descriptor set's runtime arrays
			HashMap<VkDescriptorType, u32> typeCounts;
			for (const auto& binding : desc.bindings)
			{
				VkDescriptorType vkType = ConvertDescriptorType(binding.descriptorType);
				u32 count = binding.renderType == RenderType::RuntimeArray ? MaxBindlessResources : binding.count;
				typeCounts[vkType] += count;
			}

			Array<VkDescriptorPoolSize> poolSizes;
			for (auto it = typeCounts.begin(); it != typeCounts.end(); ++it)
			{
				poolSizes.EmplaceBack(VkDescriptorPoolSize{it->first, it->second});
			}

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = poolSizes.Size();
			poolInfo.pPoolSizes = poolSizes.Data();
			poolInfo.maxSets = 1;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
			vkCreateDescriptorPool(device, &poolInfo, nullptr, &dedicatedPool);
		}

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = dedicatedPool ? dedicatedPool : descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkDescriptorSet descriptorSet;

		{
			std::unique_lock lock(descriptorPoolMutex);
			VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);

			if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
			{
				logger.Error("VK_ERROR_OUT_OF_POOL_MEMORY");
				if (dedicatedPool)
				{
					vkDestroyDescriptorPool(device, dedicatedPool, nullptr);
				}
				return {};
			}

			if (result != VK_SUCCESS)
			{
				logger.Error("Error on vkAllocateDescriptorSets");
			}
		}

		VulkanDescriptorSet* vulkanDescriptorSet = Alloc<VulkanDescriptorSet>();
		vulkanDescriptorSet->vulkanDevice = this;
		vulkanDescriptorSet->desc = desc;
		vulkanDescriptorSet->descriptorSet = descriptorSet;
		vulkanDescriptorSet->descriptorSetLayout = layout;
		vulkanDescriptorSet->dedicatedPool = dedicatedPool;

		SetObjectName(*this, VK_OBJECT_TYPE_DESCRIPTOR_SET, PtrToInt(descriptorSet), desc.debugName);

		return {vulkanDescriptorSet};
	}

	GPUDescriptorSet* VulkanDevice::CreateDescriptorSet(RID shader, StringView variant, u32 set)
	{
		SK_ASSERT(shader, "shader is required");
		RID variantRID = ShaderResource::GetVariant(shader, variant);
		SK_ASSERT(variantRID, "variant not found");

		PipelineDesc pipelineDesc;
		GetShaderInfoFromResource(variantRID, &pipelineDesc, nullptr);

		for (const auto& descriptor : pipelineDesc.descriptors)
		{
			if (descriptor.set == set)
			{
				DescriptorSetDesc desc;
				desc.bindings = descriptor.bindings;
				return CreateDescriptorSet(desc);
			}
		}
		return nullptr;
	}

	GPUQueryPool* VulkanDevice::CreateQueryPool(const QueryPoolDesc& desc)
	{
		VkQueryPoolCreateInfo queryPoolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
		queryPoolInfo.queryCount = desc.queryCount;

		// Convert query type
		switch (desc.type)
		{
			case QueryType::Timestamp:
				queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
				break;
			case QueryType::Occlusion:
				queryPoolInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
				break;
			case QueryType::PipelineStatistics:
				queryPoolInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
				// Set up pipeline statistics flags
				queryPoolInfo.pipelineStatistics = 0;
				if (desc.pipelineStatistics && PipelineStatisticFlag::InputAssemblyVertices)
					queryPoolInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT;
				if (desc.pipelineStatistics && PipelineStatisticFlag::InputAssemblyPrimitives)
					queryPoolInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT;
				if (desc.pipelineStatistics && PipelineStatisticFlag::VertexShaderInvocations)
					queryPoolInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT;
				if (desc.pipelineStatistics && PipelineStatisticFlag::GeometryShaderInvocations)
					queryPoolInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT;
				if (desc.pipelineStatistics && PipelineStatisticFlag::GeometryShaderPrimitives)
					queryPoolInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT;
				if (desc.pipelineStatistics && PipelineStatisticFlag::ClippingInvocations)
					queryPoolInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
				if (desc.pipelineStatistics && PipelineStatisticFlag::ClippingPrimitives)
					queryPoolInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT;
				if (desc.pipelineStatistics && PipelineStatisticFlag::FragmentShaderInvocations)
					queryPoolInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
				if (desc.pipelineStatistics && PipelineStatisticFlag::ComputeShaderInvocations)
					queryPoolInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
				break;
			default:
				logger.Error("Unsupported query type");
				return nullptr;
		}

		VkQueryPool queryPool = VK_NULL_HANDLE;
		VkResult    result = vkCreateQueryPool(device, &queryPoolInfo, nullptr, &queryPool);
		if (result != VK_SUCCESS)
		{
			logger.Error("Failed to create query pool: {}", string_VkResult(result));
			return nullptr;
		}

		VulkanQueryPool* vulkanQueryPool = Alloc<VulkanQueryPool>();
		vulkanQueryPool->vulkanDevice = this;
		vulkanQueryPool->queryPool = queryPool;
		vulkanQueryPool->desc = desc;

		SetObjectName(*this, VK_OBJECT_TYPE_QUERY_POOL, PtrToInt(queryPool), desc.debugName);

		return vulkanQueryPool;
	}

	static void ConvertGeometries(Span<GeometryDesc> geometries,
	                              Array<VkAccelerationStructureGeometryKHR>& outGeometries,
	                              Array<VkAccelerationStructureBuildRangeInfoKHR>& outRangeInfos,
	                              Array<u32>& outMaxPrimitiveCounts)
	{
		outGeometries.Resize(geometries.Size());
		outRangeInfos.Resize(geometries.Size());
		outMaxPrimitiveCounts.Resize(geometries.Size());

		for (u32 i = 0; i < geometries.Size(); ++i)
		{
			const GeometryDesc& geom = geometries[i];
			VkAccelerationStructureGeometryKHR& vkGeom = outGeometries[i];
			VkAccelerationStructureBuildRangeInfoKHR& rangeInfo = outRangeInfos[i];

			vkGeom = {};
			vkGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
			rangeInfo = {};

			if (geom.type == GeometryType::Triangles)
			{
				const GeometryTrianglesDesc& tri = geom.triangles;

				vkGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
				vkGeom.flags = tri.opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;

				VkAccelerationStructureGeometryTrianglesDataKHR& triData = vkGeom.geometry.triangles;
				triData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
				triData.vertexFormat = ToVkFormat(tri.vertexFormat);
				triData.vertexStride = tri.vertexStride;
				triData.maxVertex = tri.vertexCount > 0 ? tri.vertexCount - 1 : 0;

				if (tri.vertexBuffer)
				{
					triData.vertexData.deviceAddress = GetBufferDeviceAddress(
						static_cast<VulkanBuffer*>(tri.vertexBuffer)->vulkanDevice->device,
						static_cast<VulkanBuffer*>(tri.vertexBuffer)->buffer) + tri.vertexOffset;
				}

				if (tri.indexBuffer)
				{
					triData.indexType = tri.indexType == IndexType::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
					triData.indexData.deviceAddress = GetBufferDeviceAddress(
						static_cast<VulkanBuffer*>(tri.indexBuffer)->vulkanDevice->device,
						static_cast<VulkanBuffer*>(tri.indexBuffer)->buffer) + tri.indexOffset;

					u32 primitiveCount = tri.indexCount / 3;
					outMaxPrimitiveCounts[i] = primitiveCount;
					rangeInfo.primitiveCount = primitiveCount;
				}
				else
				{
					triData.indexType = VK_INDEX_TYPE_NONE_KHR;
					u32 primitiveCount = tri.vertexCount / 3;
					outMaxPrimitiveCounts[i] = primitiveCount;
					rangeInfo.primitiveCount = primitiveCount;
				}

				if (tri.transformBuffer)
				{
					triData.transformData.deviceAddress = GetBufferDeviceAddress(
						static_cast<VulkanBuffer*>(tri.transformBuffer)->vulkanDevice->device,
						static_cast<VulkanBuffer*>(tri.transformBuffer)->buffer) + tri.transformOffset;
				}
			}
			else
			{
				const GeometryAABBsDesc& aabb = geom.aabbs;

				vkGeom.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
				vkGeom.flags = aabb.opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;

				VkAccelerationStructureGeometryAabbsDataKHR& aabbData = vkGeom.geometry.aabbs;
				aabbData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
				aabbData.stride = aabb.aabbStride;

				if (aabb.aabbBuffer)
				{
					aabbData.data.deviceAddress = GetBufferDeviceAddress(
						static_cast<VulkanBuffer*>(aabb.aabbBuffer)->vulkanDevice->device,
						static_cast<VulkanBuffer*>(aabb.aabbBuffer)->buffer) + aabb.aabbOffset;
				}

				outMaxPrimitiveCounts[i] = aabb.aabbCount;
				rangeInfo.primitiveCount = aabb.aabbCount;
			}
		}
	}

	const BottomLevelASDesc& VulkanBottomLevelAS::GetDesc() const
	{
		return desc;
	}

	bool VulkanBottomLevelAS::IsCompacted() const
	{
		return compacted;
	}

	usize VulkanBottomLevelAS::GetCompactedSize() const
	{
		return 0;
	}

	void VulkanBottomLevelAS::Destroy()
	{
		VulkanResourceDestructor destructor;
		destructor.accelerationStructure = accelerationStructure;
		destructor.buffer = buffer;
		destructor.allocation = allocation;
		EnqueueDestructor(destructor);
		DestroyAndFree(this);
	}

	const TopLevelASDesc& VulkanTopLevelAS::GetDesc() const
	{
		return desc;
	}

	void VulkanTopLevelAS::UpdateInstance(u32 index, const InstanceDesc& inst)
	{
		if (!instanceMappedData || index >= maxInstanceCount)
		{
			return;
		}

		auto* vkInstances = static_cast<VkAccelerationStructureInstanceKHR*>(instanceMappedData);
		VkAccelerationStructureInstanceKHR& vkInst = vkInstances[index];
		vkInst = {};

		const Mat4& m = inst.transform;
		for (u32 row = 0; row < 3; ++row)
		{
			for (u32 col = 0; col < 4; ++col)
			{
				vkInst.transform.matrix[row][col] = m[col][row];
			}
		}

		vkInst.instanceCustomIndex = inst.instanceID & 0x00FFFFFF;
		vkInst.mask = inst.instanceMask;
		vkInst.instanceShaderBindingTableRecordOffset = inst.instanceShaderBindingTableRecordOffset & 0x00FFFFFF;

		VkGeometryInstanceFlagsKHR flags = 0;
		if (inst.frontCounterClockwise)
			flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
		if (inst.forceOpaque)
			flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
		if (inst.forceNonOpaque)
			flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
		vkInst.flags = flags;

		if (inst.bottomLevelAS)
		{
			vkInst.accelerationStructureReference = static_cast<VulkanBottomLevelAS*>(inst.bottomLevelAS)->deviceAddress;
		}
	}

	void VulkanTopLevelAS::SetInstanceCount(u32 count)
	{
		instanceCount = count < maxInstanceCount ? count : maxInstanceCount;
	}

	bool VulkanTopLevelAS::UpdateInstances(Span<InstanceDesc> instances)
	{
		if (instances.Size() > maxInstanceCount || !instanceMappedData)
		{
			return false;
		}

		for (u32 i = 0; i < instances.Size(); ++i)
		{
			UpdateInstance(i, instances[i]);
		}

		instanceCount = static_cast<u32>(instances.Size());
		return true;
	}

	void VulkanTopLevelAS::Destroy()
	{
		{
			VulkanResourceDestructor destructor;
			destructor.accelerationStructure = accelerationStructure;
			destructor.buffer = buffer;
			destructor.allocation = allocation;
			EnqueueDestructor(destructor);
		}

		if (instanceBuffer)
		{
			VulkanResourceDestructor destructor;
			destructor.buffer = instanceBuffer;
			destructor.allocation = instanceAllocation;
			EnqueueDestructor(destructor);
		}

		DestroyAndFree(this);
	}

	GPUBottomLevelAS* VulkanDevice::CreateBottomLevelAS(const BottomLevelASDesc& desc)
	{
		Array<VkAccelerationStructureGeometryKHR>      vkGeometries;
		Array<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos;
		Array<u32>                                      maxPrimitiveCounts;
		ConvertGeometries(desc.geometries, vkGeometries, rangeInfos, maxPrimitiveCounts);

		VkBuildAccelerationStructureFlagsKHR buildFlags = ConvertBuildASFlags(desc.flags);

		// Query sizes
		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildInfo.flags = buildFlags;
		buildInfo.geometryCount = static_cast<u32>(vkGeometries.Size());
		buildInfo.pGeometries = vkGeometries.Data();

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, maxPrimitiveCounts.Data(), &sizeInfo);

		// Create backing buffer
		VkBufferCreateInfo bufferCreateInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bufferCreateInfo.size = sizeInfo.accelerationStructureSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		VkBuffer asBuffer;
		VmaAllocation asAllocation;
		VkResult result = vmaCreateBuffer(vmaAllocator, &bufferCreateInfo, &allocInfo, &asBuffer, &asAllocation, nullptr);
		if (result != VK_SUCCESS)
		{
			logger.Error("Failed to create BLAS backing buffer: {}", string_VkResult(result));
			return nullptr;
		}

		// Create acceleration structure
		VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
		createInfo.buffer = asBuffer;
		createInfo.size = sizeInfo.accelerationStructureSize;
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

		VkAccelerationStructureKHR accelerationStructure;
		result = vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &accelerationStructure);
		if (result != VK_SUCCESS)
		{
			logger.Error("Failed to create BLAS: {}", string_VkResult(result));
			vmaDestroyBuffer(vmaAllocator, asBuffer, asAllocation);
			return nullptr;
		}

		// Get device address
		VkAccelerationStructureDeviceAddressInfoKHR addressInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
		addressInfo.accelerationStructure = accelerationStructure;
		VkDeviceAddress deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);

		VulkanBottomLevelAS* blas = Alloc<VulkanBottomLevelAS>();
		blas->vulkanDevice = this;
		blas->desc = desc;
		blas->desc.geometries = {}; // Clear non-owning span
		blas->accelerationStructure = accelerationStructure;
		blas->buffer = asBuffer;
		blas->allocation = asAllocation;
		blas->deviceAddress = deviceAddress;
		blas->buildFlags = buildFlags;
		blas->geometries = std::move(vkGeometries);
		blas->buildRangeInfos = std::move(rangeInfos);
		blas->maxPrimitiveCounts = std::move(maxPrimitiveCounts);

		SetObjectName(*this, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, PtrToInt(accelerationStructure), desc.debugName);

		return blas;
	}

	GPUTopLevelAS* VulkanDevice::CreateTopLevelAS(const TopLevelASDesc& desc)
	{
		VkBuildAccelerationStructureFlagsKHR buildFlags = ConvertBuildASFlags(desc.flags);
		u32 instanceCount = static_cast<u32>(desc.instances.Size());
		u32 capacity = desc.maxInstances > instanceCount ? desc.maxInstances : instanceCount;

		// Set up geometry for instances
		VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		geometry.geometry.instances.arrayOfPointers = VK_FALSE;

		// Query sizes
		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		buildInfo.flags = buildFlags;
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = &geometry;

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &capacity, &sizeInfo);

		// Create backing buffer
		VkBufferCreateInfo bufferCreateInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bufferCreateInfo.size = sizeInfo.accelerationStructureSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		VkBuffer asBuffer;
		VmaAllocation asAllocation;
		VkResult result = vmaCreateBuffer(vmaAllocator, &bufferCreateInfo, &allocInfo, &asBuffer, &asAllocation, nullptr);
		if (result != VK_SUCCESS)
		{
			logger.Error("Failed to create TLAS backing buffer: {}", string_VkResult(result));
			return nullptr;
		}

		// Create acceleration structure
		VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
		createInfo.buffer = asBuffer;
		createInfo.size = sizeInfo.accelerationStructureSize;
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

		VkAccelerationStructureKHR accelerationStructure;
		result = vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &accelerationStructure);
		if (result != VK_SUCCESS)
		{
			logger.Error("Failed to create TLAS: {}", string_VkResult(result));
			vmaDestroyBuffer(vmaAllocator, asBuffer, asAllocation);
			return nullptr;
		}

		// Get device address
		VkAccelerationStructureDeviceAddressInfoKHR addressInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
		addressInfo.accelerationStructure = accelerationStructure;
		VkDeviceAddress deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);

		// Create host-visible instance buffer
		VkBufferCreateInfo instanceBufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		instanceBufInfo.size = sizeof(VkAccelerationStructureInstanceKHR) * (capacity > 0 ? capacity : 1);
		instanceBufInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		VmaAllocationCreateInfo instanceAllocInfo{};
		instanceAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		instanceAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer instanceBuffer;
		VmaAllocation instanceAllocation;
		VmaAllocationInfo instanceAllocResult{};
		result = vmaCreateBuffer(vmaAllocator, &instanceBufInfo, &instanceAllocInfo, &instanceBuffer, &instanceAllocation, &instanceAllocResult);
		if (result != VK_SUCCESS)
		{
			logger.Error("Failed to create TLAS instance buffer: {}", string_VkResult(result));
			vkDestroyAccelerationStructureKHR(device, accelerationStructure, nullptr);
			vmaDestroyBuffer(vmaAllocator, asBuffer, asAllocation);
			return nullptr;
		}

		VulkanTopLevelAS* tlas = Alloc<VulkanTopLevelAS>();
		tlas->vulkanDevice = this;
		tlas->desc = desc;
		tlas->desc.instances = {}; // Clear non-owning span
		tlas->accelerationStructure = accelerationStructure;
		tlas->buffer = asBuffer;
		tlas->allocation = asAllocation;
		tlas->deviceAddress = deviceAddress;
		tlas->buildFlags = buildFlags;
		tlas->instanceBuffer = instanceBuffer;
		tlas->instanceAllocation = instanceAllocation;
		tlas->instanceMappedData = instanceAllocResult.pMappedData;
		tlas->instanceCount = instanceCount;
		tlas->maxInstanceCount = capacity > 0 ? capacity : 1;

		// Fill instance data
		tlas->UpdateInstances(desc.instances);

		SetObjectName(*this, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, PtrToInt(accelerationStructure), desc.debugName);

		return tlas;
	}

	GPUQueue* VulkanDevice::CreateQueue(const QueueDesc& desc)
	{
		VulkanQueue* vulkanQueue = Alloc<VulkanQueue>();
		vulkanQueue->vulkanDevice = this;

		switch (desc.type)
		{
			case QueueType::Compute:
				vulkanQueue->context = computeQueue.context;
				break;
			case QueueType::Transfer:
				vulkanQueue->context = transferQueue.context;
				break;
			default:
				vulkanQueue->context = graphicsQueue.context;
				break;
		}

		VkFenceCreateInfo fenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		vkCreateFence(device, &fenceCreateInfo, nullptr, &vulkanQueue->fence);

		return vulkanQueue;
	}

	usize VulkanDevice::GetBottomLevelASSize(const BottomLevelASDesc& desc)
	{
		Array<VkAccelerationStructureGeometryKHR>      vkGeometries;
		Array<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos;
		Array<u32>                                      maxPrimitiveCounts;
		ConvertGeometries(desc.geometries, vkGeometries, rangeInfos, maxPrimitiveCounts);

		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildInfo.flags = ConvertBuildASFlags(desc.flags);
		buildInfo.geometryCount = static_cast<u32>(vkGeometries.Size());
		buildInfo.pGeometries = vkGeometries.Data();

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, maxPrimitiveCounts.Data(), &sizeInfo);

		return sizeInfo.accelerationStructureSize;
	}

	usize VulkanDevice::GetTopLevelASSize(const TopLevelASDesc& desc)
	{
		u32 instanceCount = static_cast<u32>(desc.instances.Size());

		VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		geometry.geometry.instances.arrayOfPointers = VK_FALSE;

		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		buildInfo.flags = ConvertBuildASFlags(desc.flags);
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = &geometry;

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &instanceCount, &sizeInfo);

		return sizeInfo.accelerationStructureSize;
	}

	usize VulkanDevice::GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc)
	{
		Array<VkAccelerationStructureGeometryKHR>      vkGeometries;
		Array<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos;
		Array<u32>                                      maxPrimitiveCounts;
		ConvertGeometries(desc.geometries, vkGeometries, rangeInfos, maxPrimitiveCounts);

		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildInfo.flags = ConvertBuildASFlags(desc.flags);
		buildInfo.geometryCount = static_cast<u32>(vkGeometries.Size());
		buildInfo.pGeometries = vkGeometries.Data();

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, maxPrimitiveCounts.Data(), &sizeInfo);

		usize alignment = selectedAdapter->accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
		return sizeInfo.buildScratchSize + alignment;
	}

	usize VulkanDevice::GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc)
	{
		u32 instanceCount = static_cast<u32>(desc.instances.Size());
		u32 capacity = desc.maxInstances > instanceCount ? desc.maxInstances : instanceCount;

		VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		geometry.geometry.instances.arrayOfPointers = VK_FALSE;

		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		buildInfo.flags = ConvertBuildASFlags(desc.flags);
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = &geometry;

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &capacity, &sizeInfo);

		usize alignment = selectedAdapter->accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
		return sizeInfo.buildScratchSize + alignment;
	}

	void VulkanDevice::ExecuteFrame()
	{
		FlushDestructors(currentFrame);

		vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &inFlightFences[currentFrame]);

		VulkanCommandBuffer* cmd = commandBuffers[currentFrame];
		cmd->Begin();
		onRecordRenderCommands.Invoke(cmd);
		RenderResourceCache::Flush(cmd);
		cmd->End();

		VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};

		if (!swapchains.Empty())
		{
			static Array<VkSemaphore> waitSemaphores;
			static Array<VkSemaphore> signalSemaphores;

			waitSemaphores.Clear();
			signalSemaphores.Clear();

			for (auto& swapchain : swapchains)
			{
				if (!swapchain->ValidSwapchain())
				{
					continue;
				}

				waitSemaphores.PushBack(swapchain->imageAvailableSemaphores[currentFrame]);
				signalSemaphores.PushBack(swapchain->renderFinishedSemaphores[swapchain->imageIndex]);
			}

			submitInfo.waitSemaphoreCount = waitSemaphores.Size();
			submitInfo.pWaitSemaphores = waitSemaphores.Data();

			submitInfo.signalSemaphoreCount = signalSemaphores.Size();
			submitInfo.pSignalSemaphores = signalSemaphores.Data();
		}

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[currentFrame]->commandBuffer;
		submitInfo.pWaitDstStageMask = waitStages;

		graphicsQueue.Submit(&submitInfo, inFlightFences[currentFrame]);

		for (VulkanSwapchain* swapchain : swapchains)
		{
			if (!swapchain->ValidSwapchain()) continue;

			VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = &swapchain->renderFinishedSemaphores[swapchain->imageIndex];
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = &swapchain->swapchainKHR;
			presentInfo.pImageIndices = &swapchain->imageIndex;

			VkResult res;
			{
				std::unique_lock lock(presentQueue.context->mutex);
				res = vkQueuePresentKHR(presentQueue.context->vkQueue, &presentInfo);
			}

			if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
			{
				//require resize?
			}
			else if (res != VK_SUCCESS)
			{
				logger.Error("failed to execute vkQueuePresentKHR, error {} ", string_VkResult(res));
				Graphics::ShowSubmitError();
				std::terminate();
			}
		}

		currentFrame = (currentFrame + 1) % SK_FRAMES_IN_FLIGHT;
	}

	void VulkanDevice::FlushDestructors(u8 frame)
	{
		VulkanResourceDestructor destructor{};
		while (destructorQueues[frame].try_dequeue(destructor))
		{
			if (destructor.buffer && destructor.allocation)
			{
				vmaDestroyBuffer(vmaAllocator, destructor.buffer, destructor.allocation);
			}

			for (VkDescriptorSetLayout layout : destructor.descriptorSetLayouts)
			{
				vkDestroyDescriptorSetLayout(device, layout, nullptr);
			}

			if (destructor.descriptorPool)
			{
				// Dedicated pool â€” destroying the pool implicitly frees all sets allocated from it
				vkDestroyDescriptorPool(device, destructor.descriptorPool, nullptr);
			}
			else if (destructor.descriptorSet)
			{
				std::unique_lock lock(descriptorPoolMutex);
				vkFreeDescriptorSets(device, descriptorPool, 1, &destructor.descriptorSet);
			}

			if (destructor.textureView)
			{
				if (destructor.textureView->viewDescriptorSet)
				{
					std::unique_lock lock(descriptorPoolMutex);
					vkFreeDescriptorSets(device, descriptorPool, 1, &destructor.textureView->viewDescriptorSet);
				}
				vkDestroyImageView(device, destructor.textureView->imageView, nullptr);
				DestroyAndFree(destructor.textureView);
			}

			if (destructor.texture)
			{
				if (destructor.texture->textureView)
				{
					destructor.texture->textureView->Destroy();
				}
				if (destructor.texture->allocation)
				{
					vmaDestroyImage(vmaAllocator, destructor.texture->image, destructor.texture->allocation);
				}
				DestroyAndFree(destructor.texture);
			}

			if (destructor.pipeline)
			{
				vkDestroyPipeline(device, destructor.pipeline, nullptr);
			}

			if (destructor.pipelineLayout)
			{
				vkDestroyPipelineLayout(device, destructor.pipelineLayout, nullptr);
			}

			if (destructor.sampler)
			{
				vkDestroySampler(device, destructor.sampler, nullptr);
			}

			if (destructor.renderPass)
			{
				vkDestroyRenderPass(device, destructor.renderPass, nullptr);
			}

			if (destructor.framebuffer)
			{
				vkDestroyFramebuffer(device, destructor.framebuffer, nullptr);
			}

			if (destructor.queryPool)
			{
				vkDestroyQueryPool(device, destructor.queryPool, nullptr);
			}

			if (destructor.accelerationStructure)
			{
				vkDestroyAccelerationStructureKHR(device, destructor.accelerationStructure, nullptr);
			}
		}
	}

	void VulkanDevice::GetMemoryBudgets(Array<MemoryHeapBudget>& outBudgets)
	{
		outBudgets.Clear();
		if (!vmaAllocator || !selectedAdapter) return;

		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(selectedAdapter->device, &memProps);

		VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
		vmaGetHeapBudgets(vmaAllocator, budgets);

		outBudgets.Reserve(memProps.memoryHeapCount);
		for (u32 i = 0; i < memProps.memoryHeapCount; ++i)
		{
			MemoryHeapBudget b;
			b.usage       = static_cast<u64>(budgets[i].usage);
			b.budget      = static_cast<u64>(budgets[i].budget);
			b.deviceLocal = (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
			outBudgets.EmplaceBack(b);
		}
	}


	GPUDevice* InitVulkan(const DeviceInitDesc& initDesc)
	{
		if (volkInitialize() != VK_SUCCESS)
		{
			logger.Error("vulkan cannot be initialized");
			return nullptr;
		}

		Platform::SetVulkanLoader(vkGetInstanceProcAddr);

		OpenXRManagerGetGraphicsRequirements();

		VkApplicationInfo applicationInfo{};
		applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		applicationInfo.pApplicationName = "Skore Engine";
		applicationInfo.applicationVersion = 0;
		applicationInfo.pEngineName = "Skore Engine";
		applicationInfo.engineVersion = 0;
		applicationInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &applicationInfo;

		bool validationLayersEnabled = initDesc.enableDebugLayers && QueryLayerProperties(Span<const char*>{validationLayers, 1});

		VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};

		if (validationLayersEnabled)
		{
			debugUtilsMessengerCreateInfo.messageSeverity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

			debugUtilsMessengerCreateInfo.messageType =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

			debugUtilsMessengerCreateInfo.pfnUserCallback = &DebugCallback;

			createInfo.enabledLayerCount = 1;
			createInfo.ppEnabledLayerNames = validationLayers;
			createInfo.pNext = &debugUtilsMessengerCreateInfo;

			VkValidationFeatureEnableEXT enables[] = {
				VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT
			};

			VkValidationFeaturesEXT validationFeatures{};
			validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
			validationFeatures.enabledValidationFeatureCount = 1;
			validationFeatures.pEnabledValidationFeatures = enables;

			validationFeatures.pNext = createInfo.pNext;
			createInfo.pNext = &validationFeatures;

			logger.Info("Validation layers enabled");
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

		Array<const char*> requiredExtensions;

		auto instanceExtensions = Platform::GetRequiredInstanceExtensions();
		requiredExtensions.Assign(instanceExtensions.begin(), instanceExtensions.end());


		bool debugUtilsExtensionPresent = initDesc.enableDebugLayers && QueryInstanceExtensions({VK_EXT_DEBUG_UTILS_EXTENSION_NAME});
		if (debugUtilsExtensionPresent)
		{
			requiredExtensions.EmplaceBack(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		Array<String> xrExtensions = OpenXRManagerGetInstanceExtensions();

		for (const auto& extension : xrExtensions)
		{
			requiredExtensions.EmplaceBack(extension.CStr());
		}

		if (!QueryInstanceExtensions(requiredExtensions))
		{
			logger.Error("Required extensions not found");
			return nullptr;
		}

#ifdef SK_MACOS
		if (QueryInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		{
			requiredExtensions.EmplaceBack(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}
#endif

		createInfo.enabledExtensionCount = requiredExtensions.Size();
		createInfo.ppEnabledExtensionNames = requiredExtensions.Data();

		VkInstance instance = nullptr;
		VkResult   res = vkCreateInstance(&createInfo, nullptr, &instance);
		if (res != VK_SUCCESS)
		{
			logger.Error("Error on create vkCreateInstance {} ", string_VkResult(res));
			return nullptr;
		}

		volkLoadInstance(instance);

		VulkanDevice* vulkanDevice = Alloc<VulkanDevice>();
		vulkanDevice->instance = instance;
		vulkanDevice->validationLayersEnabled = validationLayersEnabled;
		vulkanDevice->debugUtilsExtensionPresent = debugUtilsExtensionPresent;

		if (validationLayersEnabled)
		{
			vkCreateDebugUtilsMessengerEXT(instance, &debugUtilsMessengerCreateInfo, nullptr, &debugUtilsMessengerExt);
		}

		Array<VkPhysicalDevice> devices;

		if (OpenXRManager::IsEnabled())
		{
			devices.Resize(1);
			devices[0] = OpenXRManagerGetPhysicalDevice(instance);
		}
		else
		{
			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
			devices.Resize(deviceCount);
			vkEnumeratePhysicalDevices(instance, &deviceCount, devices.Data());
		}

		vulkanDevice->adapters.Resize(devices.Size());

		for (int i = 0; i < devices.Size(); ++i)
		{
			VulkanAdapter* vulkanAdapter = Alloc<VulkanAdapter>();
			vulkanAdapter->device = devices[i];

			vkGetPhysicalDeviceProperties2(vulkanAdapter->device, &vulkanAdapter->deviceProperties);
			vkGetPhysicalDeviceFeatures2(vulkanAdapter->device, &vulkanAdapter->deviceFeatures);

			vulkanAdapter->RatePhysicalDevice(vulkanDevice);
			vulkanDevice->adapters[i] = vulkanAdapter;
		}


		return vulkanDevice;
	}
}
