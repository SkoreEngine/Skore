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

#include "VulkanDevice.hpp"

#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION

#include <iostream>
#include <queue>

#include "vk_mem_alloc.h"
#include "VulkanUtils.hpp"
#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/Resources.hpp"

#include "vulkan/vk_enum_string_helper.h"


namespace Skore
{
	namespace
	{
		const char*              validationLayers[1] = {"VK_LAYER_KHRONOS_validation"};
		VkDebugUtilsMessengerEXT debugUtilsMessengerExt{};
		Logger&                  logger = Logger::GetLogger("Skore::Vulkan");
	}

	void VulkanAdapter::RatePhysicalDevice(VulkanDevice* vulkanDevice)
	{
		score += deviceProperties.properties.limits.maxImageDimension2D / 1024;

		if (deviceProperties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			score += 1000;
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
			bool        hasPresentFamily = SDL_Vulkan_GetPresentationSupport(vulkanDevice->instance, device, i);
			bool        hasGraphicsFamily = queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT;

			if (hasGraphicsFamily && graphicsFamily == U32_MAX)
			{
				graphicsFamily = i;
			}

			//if the device has a dedicated family for present, use it.
			if (hasPresentFamily && (presentFamily == U32_MAX || !hasGraphicsFamily))
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
		vkDestroyFramebuffer(vulkanDevice->device, framebuffer, nullptr);
		vkDestroyRenderPass(vulkanDevice->device, renderPass, nullptr);
		DestroyAndFree(this);
	}

	const SwapchainDesc& VulkanSwapchain::GetDesc() const
	{
		return desc;
	}

	bool VulkanSwapchain::AcquireNextImage(u32 currentFrame)
	{
		//TODO move this.
		vkWaitForFences(vulkanDevice->device, 1, &vulkanDevice->inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
		vkResetFences(vulkanDevice->device, 1, &vulkanDevice->inFlightFences[currentFrame]);

		VkResult result = vkAcquireNextImageKHR(
			vulkanDevice->device,
			swapchainKHR,
			UINT64_MAX,
			imageAvailableSemaphores[currentFrame],
			VK_NULL_HANDLE,
			&imageIndex
		);

		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			logger.Error("failed to acquire swap chain image! {}", string_VkResult(result));
			return false;
		}

		return true;
	}

	GPURenderPass* VulkanSwapchain::GetCurrentRenderPass()
	{
		return renderPasses[imageIndex];
	}

	bool VulkanSwapchain::Resize()
	{
		i32 width;
		i32 height;
		SDL_GetWindowSize(static_cast<SDL_Window*>(desc.windowHandle), &width, &height);
		if (extent.width == 0 || extent.height == 0)
		{
			return true;
		}

		vkDeviceWaitIdle(vulkanDevice->device);
		DestroyInternal();
		return CreateInternal();
	}

	void VulkanSwapchain::Destroy()
	{
		DestroyInternal();
		DestroyAndFree(this);
	}

	u32 VulkanSwapchain::GetImageCount() const
	{
		return images.Size();
	}

	bool VulkanSwapchain::CreateInternal()
	{
		imageIndex = 0;

		VkResult res = VK_SUCCESS;

		if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(desc.windowHandle), vulkanDevice->instance, nullptr, &surfaceKHR))
		{
			logger.Error("Vulkan surface cannot be created, error {}", SDL_GetError());
			return false;
		}

		i32 width;
		i32 height;
		SDL_GetWindowSize(static_cast<SDL_Window*>(desc.windowHandle), &width, &height);

		VulkanSwapChainSupportDetails details = QuerySwapChainSupport(vulkanDevice->selectedAdapter->device, surfaceKHR);
		VkSurfaceFormatKHR            surfaceFormat = ChooseSwapSurfaceFormat(details, {ToVkFormat(desc.format), VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
		VkPresentModeKHR              presentMode = ChooseSwapPresentMode(details, desc.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR);
		extent = ChooseSwapExtent(details, {static_cast<u32>(width), static_cast<u32>(height)});


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
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

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

		createInfo.preTransform = details.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
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
		images.Resize(imageCount);
		imageViews.Resize(imageCount);
		renderPasses.Resize(imageCount);

		vkGetSwapchainImagesKHR(vulkanDevice->device, swapchainKHR, &imageCount, images.Data());

		for (size_t i = 0; i < imageCount; i++)
		{
			VkImageViewCreateInfo imageViewCreateInfo{};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.image = images[i];
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewCreateInfo.format = surfaceFormat.format;
			imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
			imageViewCreateInfo.subresourceRange.levelCount = 1;
			imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
			imageViewCreateInfo.subresourceRange.layerCount = 1;
			vkCreateImageView(vulkanDevice->device, &imageViewCreateInfo, nullptr, &imageViews[i]);
		}

		for (int i = 0; i < imageCount; ++i)
		{
			VulkanRenderPass* vulkanRenderPass = Alloc<VulkanRenderPass>();
			vulkanRenderPass->vulkanDevice = vulkanDevice;
			vulkanRenderPass->extent = extent;
			vulkanRenderPass->clearValues.Resize(1);
			vulkanRenderPass->formats.EmplaceBack(surfaceFormat.format);

			renderPasses[i] = vulkanRenderPass;

			VkAttachmentDescription attachmentDescription{};
			VkAttachmentReference   colorAttachmentReference{};

			attachmentDescription.format = surfaceFormat.format;
			attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachmentReference.attachment = 0;

			VkSubpassDescription subPass = {};
			subPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subPass.colorAttachmentCount = 1;
			subPass.pColorAttachments = &colorAttachmentReference;

			VkRenderPassCreateInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &attachmentDescription;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subPass;
			renderPassInfo.dependencyCount = 0;
			vkCreateRenderPass(vulkanDevice->device, &renderPassInfo, nullptr, &vulkanRenderPass->renderPass);

			VkFramebufferCreateInfo framebufferCreateInfo{};
			framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferCreateInfo.renderPass = vulkanRenderPass->renderPass;
			framebufferCreateInfo.width = vulkanRenderPass->extent.width;
			framebufferCreateInfo.height = vulkanRenderPass->extent.height;
			framebufferCreateInfo.layers = 1u;
			framebufferCreateInfo.attachmentCount = 1;
			framebufferCreateInfo.pAttachments = &imageViews[i];
			vkCreateFramebuffer(vulkanDevice->device, &framebufferCreateInfo, nullptr, &vulkanRenderPass->framebuffer);
		}

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			vkCreateSemaphore(vulkanDevice->device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
		}

		return true;
	}

	void VulkanSwapchain::DestroyInternal()
	{
		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroySemaphore(vulkanDevice->device, imageAvailableSemaphores[i], nullptr);
		}

		for (VkImageView imageView : imageViews)
		{
			vkDestroyImageView(vulkanDevice->device, imageView, nullptr);
		}

		for (VulkanRenderPass* renderPass : renderPasses)
		{
			renderPass->Destroy();
		}

		renderPasses.Clear();

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
		bool depthFormat = false;

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

	void VulkanCommandBuffer::SubmitAndWait()
	{
		VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		vkQueueSubmit(vulkanDevice->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(vulkanDevice->graphicsQueue);
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

	void VulkanCommandBuffer::PushConstants(GPUPipeline* pipeline, ShaderStage stages, u32 offset, u32 size, const void* data)
	{
		vkCmdPushConstants(commandBuffer, static_cast<VulkanPipeline*>(pipeline)->pipelineLayout, ConvertShaderStageFlags(stages), offset, size, data);
	}

	void VulkanCommandBuffer::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
	{
		vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
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

	void VulkanCommandBuffer::Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ)
	{
		vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
	}

	void VulkanCommandBuffer::DispatchIndirect(GPUBuffer* buffer, usize offset)
	{
		vkCmdDispatchIndirect(commandBuffer, static_cast<VulkanBuffer*>(buffer)->buffer, offset);
	}

	void VulkanCommandBuffer::TraceRays(GPUPipeline* pipeline, u32 width, u32 height, u32 depth) {}
	void VulkanCommandBuffer::BuildBottomLevelAS(GPUBottomLevelAS* bottomLevelAS, const AccelerationStructureBuildInfo& buildInfo) {}
	void VulkanCommandBuffer::BuildTopLevelAS(GPUTopLevelAS* topLevelAS, const AccelerationStructureBuildInfo& buildInfo) {}
	void VulkanCommandBuffer::CopyAccelerationStructure(GPUBottomLevelAS* src, GPUBottomLevelAS* dst, bool compress) {}
	void VulkanCommandBuffer::CopyAccelerationStructure(GPUTopLevelAS* src, GPUTopLevelAS* dst, bool compress) {}

	void VulkanCommandBuffer::BeginRenderPass(GPURenderPass* renderPass, Vec4 clearColor, f32 clearDepth, u32 clearStencil)
	{
		VulkanRenderPass* vulkanRenderPass = static_cast<VulkanRenderPass*>(renderPass);

		for (u32 i = 0; i < vulkanRenderPass->clearValues.Size(); ++i)
		{
			VkClearValue& clearValue = vulkanRenderPass->clearValues[i];
			if (IsDepthFormat(vulkanRenderPass->formats[i]))
			{
				clearValue.depthStencil.depth = clearDepth;
				clearValue.depthStencil.stencil = clearStencil;
			}
			else
			{
				clearValue.color.float32[0] = clearColor.x;
				clearValue.color.float32[1] = clearColor.y;
				clearValue.color.float32[2] = clearColor.z;
				clearValue.color.float32[3] = clearColor.w;
			}
		}

		if (vulkanRenderPass->extent.width == 0 || vulkanRenderPass->extent.height == 0)
		{
			SK_ASSERT(false, "Invalid render pass extent");
		}

		VkRenderPassBeginInfo renderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
		renderPassBeginInfo.renderPass = vulkanRenderPass->renderPass;
		renderPassBeginInfo.framebuffer = vulkanRenderPass->framebuffer;
		renderPassBeginInfo.renderArea.offset = {0, 0};
		renderPassBeginInfo.renderArea.extent = vulkanRenderPass->extent;
		renderPassBeginInfo.clearValueCount = static_cast<u32>(vulkanRenderPass->clearValues.Size());
		renderPassBeginInfo.pClearValues = vulkanRenderPass->clearValues.Data();

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

	void VulkanCommandBuffer::CopyBufferToTexture(GPUBuffer* srcBuffer, GPUTexture* dstTexture, Extent3D extent, u32 mipLevel, u32 arrayLayer, u64 bufferOffset)
	{
		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = bufferOffset;
		copyRegion.bufferRowLength = 0;   // Tightly packed
		copyRegion.bufferImageHeight = 0; // Tightly packed

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = mipLevel;
		copyRegion.imageSubresource.baseArrayLayer = arrayLayer;
		copyRegion.imageSubresource.layerCount = 1;

		copyRegion.imageOffset = {0, 0, 0};
		copyRegion.imageExtent = {extent.width, extent.height, extent.depth};

		vkCmdCopyBufferToImage(
			commandBuffer,
			static_cast<VulkanBuffer*>(srcBuffer)->buffer,
			static_cast<VulkanTexture*>(dstTexture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion
		);
	}

	void VulkanCommandBuffer::CopyTextureToBuffer(GPUTexture* srcTexture, GPUBuffer* dstBuffer, Extent3D extent, u32 mipLevel, u32 arrayLayer)
	{
		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;   // Tightly packed
		copyRegion.bufferImageHeight = 0; // Tightly packed

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = mipLevel;
		copyRegion.imageSubresource.baseArrayLayer = arrayLayer;
		copyRegion.imageSubresource.layerCount = 1;

		copyRegion.imageOffset = {0, 0, 0};
		copyRegion.imageExtent = {extent.width, extent.height, extent.depth};

		vkCmdCopyImageToBuffer(
			commandBuffer,
			static_cast<VulkanTexture*>(srcTexture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			static_cast<VulkanBuffer*>(dstBuffer)->buffer,
			1,
			&copyRegion
		);
	}

	void VulkanCommandBuffer::CopyTexture(GPUTexture* srcTexture, GPUTexture* dstTexture, Extent3D extent, u32 srcMipLevel, u32 srcArrayLayer, u32 dstMipLevel, u32 dstArrayLayer)
	{
		VkImageCopy copyRegion{};

		// Set up source subresource
		copyRegion.srcSubresource.aspectMask = GetImageAspectFlags(ToVkFormat(static_cast<VulkanTexture*>(srcTexture)->desc.format));
		copyRegion.srcSubresource.mipLevel = srcMipLevel;
		copyRegion.srcSubresource.baseArrayLayer = srcArrayLayer;
		copyRegion.srcSubresource.layerCount = 1;
		copyRegion.srcOffset = {0, 0, 0};

		// Set up destination subresource
		copyRegion.dstSubresource.aspectMask = GetImageAspectFlags(ToVkFormat(static_cast<VulkanTexture*>(dstTexture)->desc.format));
		copyRegion.dstSubresource.mipLevel = dstMipLevel;
		copyRegion.dstSubresource.baseArrayLayer = dstArrayLayer;
		copyRegion.dstSubresource.layerCount = 1;
		copyRegion.dstOffset = {0, 0, 0};

		// Set up the extent
		copyRegion.extent = {extent.width, extent.height, extent.depth};

		// Execute the copy command
		vkCmdCopyImage(
			commandBuffer,
			static_cast<VulkanTexture*>(srcTexture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			static_cast<VulkanTexture*>(dstTexture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion
		);
	}

	void VulkanCommandBuffer::BlitTexture(GPUTexture* srcTexture, GPUTexture* dstTexture, Extent3D srcExtent, Extent3D dstExtent, u32 srcMipLevel, u32 srcArrayLayer, u32 dstMipLevel,
	                                      u32         dstArrayLayer)
	{
		VkImageBlit blitRegion{};

		// Set up source subresource
		blitRegion.srcSubresource.aspectMask = GetImageAspectFlags(ToVkFormat(static_cast<VulkanTexture*>(srcTexture)->desc.format));
		blitRegion.srcSubresource.mipLevel = srcMipLevel;
		blitRegion.srcSubresource.baseArrayLayer = srcArrayLayer;
		blitRegion.srcSubresource.layerCount = 1;

		// Source offsets define the region to blit from
		blitRegion.srcOffsets[0] = {0, 0, 0};
		blitRegion.srcOffsets[1] = {
			static_cast<int32_t>(srcExtent.width),
			static_cast<int32_t>(srcExtent.height),
			static_cast<int32_t>(srcExtent.depth)
		};

		// Set up destination subresource
		blitRegion.dstSubresource.aspectMask = GetImageAspectFlags(ToVkFormat(static_cast<VulkanTexture*>(dstTexture)->desc.format));
		blitRegion.dstSubresource.mipLevel = dstMipLevel;
		blitRegion.dstSubresource.baseArrayLayer = dstArrayLayer;
		blitRegion.dstSubresource.layerCount = 1;

		// Destination offsets define the region to blit to
		blitRegion.dstOffsets[0] = {0, 0, 0};
		blitRegion.dstOffsets[1] = {
			static_cast<int32_t>(dstExtent.width),
			static_cast<int32_t>(dstExtent.height),
			static_cast<int32_t>(dstExtent.depth)
		};

		// Execute the blit command with linear filtering for smooth scaling
		vkCmdBlitImage(
			commandBuffer,
			static_cast<VulkanTexture*>(srcTexture)->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			static_cast<VulkanTexture*>(dstTexture)->image,
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

	void VulkanCommandBuffer::ResourceBarrier(GPUBuffer* buffer, ResourceState oldState, ResourceState newState)
	{
		if (oldState == newState)
		{
			return;
		}

		VulkanBuffer* vulkanBuffer = static_cast<VulkanBuffer*>(buffer);

		VkBufferMemoryBarrier bufferBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
		bufferBarrier.srcAccessMask = GetAccessFlagsFromResourceState(oldState);
		bufferBarrier.dstAccessMask = GetAccessFlagsFromResourceState(newState);
		bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarrier.buffer = vulkanBuffer->buffer;
		bufferBarrier.offset = 0;
		bufferBarrier.size = vulkanBuffer->desc.size;

		VkPipelineStageFlags srcStageMask = GetPipelineStageFromResourceState(oldState);
		VkPipelineStageFlags dstStageMask = GetPipelineStageFromResourceState(newState);

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
		if (oldState == newState)
		{
			return;
		}

		VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(texture);

		VkImageMemoryBarrier imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		imageBarrier.srcAccessMask = GetAccessFlagsFromResourceState(oldState);
		imageBarrier.dstAccessMask = GetAccessFlagsFromResourceState(newState);
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

		VkPipelineStageFlags srcStageMask = GetPipelineStageFromResourceState(oldState);
		VkPipelineStageFlags dstStageMask = GetPipelineStageFromResourceState(newState);

		vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
	}

	void VulkanCommandBuffer::ResourceBarrier(GPUBottomLevelAS* bottomLevelAS, ResourceState oldState, ResourceState newState)
	{
		if (oldState == newState)
		{
			return;
		}

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
			vkDestroyQueryPool(vulkanDevice->device, queryPool, nullptr);
			queryPool = VK_NULL_HANDLE;
		}
		DestroyAndFree(this);
	}


	VulkanDevice::~VulkanDevice()
	{
		vmaDestroyAllocator(vmaAllocator);

		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyCommandPool(device, commandPool, nullptr);

		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroyFence(device, inFlightFences[i], nullptr);
			vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
		}

		if (validationLayersEnabled)
		{
			vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessengerExt, nullptr);
		}

		vkDestroyDevice(device, nullptr);
		vkDestroyInstance(instance, nullptr);
	}

	Span<GPUAdapter*> VulkanDevice::GetAdapters()
	{
		return adapters;
	}

	bool VulkanDevice::SelectAdapter(GPUAdapter* adapter)
	{
		VulkanAdapter* vulkanAdapter = static_cast<VulkanAdapter*>(adapter);

		Array<const char*>        extensions{};
		VkPhysicalDeviceFeatures2 deviceFeatures2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

		HashSet<String> availableExtensions = GetDeviceExtensions(vulkanAdapter->device);
		auto            AddIfPresent = [&](StringView extension, VoidPtr feature = nullptr)
		{
			if (availableExtensions.Has(extension))
			{
				if (feature)
				{
					VulkanBaseInStructure* toAdd = static_cast<VulkanBaseInStructure*>(feature);
					toAdd->pNext = deviceFeatures2.pNext;
					deviceFeatures2.pNext = toAdd;
				}

				extensions.EmplaceBack(extension.CStr());
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
		indexingFeatures.shaderSampledImageArrayNonUniformIndexing = vulkanAdapter->indexingFeatures.shaderSampledImageArrayNonUniformIndexing;
		indexingFeatures.descriptorBindingPartiallyBound = vulkanAdapter->indexingFeatures.descriptorBindingPartiallyBound;
		indexingFeatures.runtimeDescriptorArray = vulkanAdapter->indexingFeatures.runtimeDescriptorArray;
		indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = vulkanAdapter->indexingFeatures.descriptorBindingSampledImageUpdateAfterBind;
		indexingFeatures.descriptorBindingStorageImageUpdateAfterBind = vulkanAdapter->indexingFeatures.descriptorBindingStorageImageUpdateAfterBind;

		features.bindlessSupported = indexingFeatures.shaderSampledImageArrayNonUniformIndexing &&
			indexingFeatures.descriptorBindingPartiallyBound &&
			indexingFeatures.runtimeDescriptorArray &&
			indexingFeatures.descriptorBindingSampledImageUpdateAfterBind &&
			indexingFeatures.descriptorBindingStorageImageUpdateAfterBind;

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

		if (!AddIfPresent(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			return false;
		}

		if (!AddIfPresent(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME))
		{
			return false;
		}

		//not sure about it.
		//AddIfPresent(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);

		AddIfPresent(VK_KHR_MAINTENANCE_4_EXTENSION_NAME, &maintenance4Features);

		features.bufferDeviceAddress = AddIfPresent(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, &bufferDeviceAddressFeatures);
		features.drawIndirectCount = AddIfPresent(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);

		features.rayTracing = AddIfPresent(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) && AddIfPresent(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

		AddIfPresent(VK_KHR_RAY_QUERY_EXTENSION_NAME);
		AddIfPresent(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
		AddIfPresent(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
		AddIfPresent(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
		AddIfPresent(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);

		if (features.bindlessSupported)
		{
			AddToChain(&indexingFeatures);
		}

		if (features.rayTracing)
		{
			AddToChain(&deviceRayQueryFeaturesKhr);
			AddToChain(&deviceAccelerationStructureFeaturesKhr);
			AddToChain(&deviceRayTracingPipelineFeatures);
		}

		if (drawParametersFeatures.shaderDrawParameters)
		{
			AddToChain(&drawParametersFeatures);
		}

#ifdef SK_APPLE
		AddIfPresent(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

		float queuePriority = 1.0f;

		Array<VkDeviceQueueCreateInfo> queueCreateInfos{};
		if (vulkanAdapter->graphicsFamily != vulkanAdapter->presentFamily)
		{
			queueCreateInfos.Resize(2);

			queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfos[0].queueFamilyIndex = vulkanAdapter->graphicsFamily;
			queueCreateInfos[0].queueCount = 1;
			queueCreateInfos[0].pQueuePriorities = &queuePriority;

			queueCreateInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfos[1].queueFamilyIndex = vulkanAdapter->presentFamily;
			queueCreateInfos[1].queueCount = 1;
			queueCreateInfos[1].pQueuePriorities = &queuePriority;
		}
		else
		{
			queueCreateInfos.Resize(1);
			queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfos[0].queueFamilyIndex = vulkanAdapter->graphicsFamily;
			queueCreateInfos[0].queueCount = 1;
			queueCreateInfos[0].pQueuePriorities = &queuePriority;
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

		vkGetDeviceQueue(device, vulkanAdapter->graphicsFamily, 0, &graphicsQueue);
		vkGetDeviceQueue(device, vulkanAdapter->presentFamily, 0, &presentQueue);

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

		allocatorInfo.pVulkanFunctions = &vmaVulkanFunctions;
		vmaCreateAllocator(&allocatorInfo, &vmaAllocator);

		VkDescriptorPoolSize sizes[5] = {
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 500},
			{VK_DESCRIPTOR_TYPE_SAMPLER, 500},
			{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 500},
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 500},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 500}
		};

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = 5;
		poolInfo.pPoolSizes = sizes;
		poolInfo.maxSets = 500;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
				vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
			{
				logger.Error("Failed to create frame objects");
				return false;
			}
		}

		VkCommandPoolCreateInfo commandPoolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		commandPoolInfo.queueFamilyIndex = vulkanAdapter->graphicsFamily;
		vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool);

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
		return swapchain;
	}

	GPURenderPass* VulkanDevice::CreateRenderPass(const RenderPassDesc& desc)
	{
		bool hasDepth = false;

		Array<VkAttachmentDescription2> attachmentDescriptions;
		Array<VkAttachmentReference2>   colorAttachmentReference{};
		VkAttachmentReference2          depthReference{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
		Array<VkImageView>              imageViews{};
		Array<VkFormat>                 formats;
		Extent3D                        framebufferSize{};

		for (int i = 0; i < desc.attachments.Size(); ++i)
		{
			const AttachmentDesc& attachment = desc.attachments[i];

			VkFormat format;

			if (VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(attachment.texture))
			{
				imageViews.EmplaceBack(static_cast<VulkanTextureView*>(vulkanTexture->textureView)->imageView);
				format = ToVkFormat(vulkanTexture->desc.format);
				framebufferSize = vulkanTexture->desc.extent;
			}
			else if (VulkanTextureView* vulkanTextureView = static_cast<VulkanTextureView*>(attachment.textureView))
			{
				imageViews.EmplaceBack(vulkanTextureView->imageView);
				format = ToVkFormat(vulkanTextureView->texture->desc.format);
				framebufferSize = vulkanTextureView->texture->desc.extent;
			}
			else
			{
				SK_ASSERT(false, "texture or texture view must be provieded");
			}

			bool isDepthFormat = IsDepthFormat(format);

			VkAttachmentDescription2 attachmentDescription{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
			attachmentDescription.format = format;
			attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;

			attachmentDescription.loadOp = CastLoadOp(attachment.loadOp);
			attachmentDescription.storeOp = CastStoreOp(attachment.storeOp);

			attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			attachmentDescription.initialLayout = CastState(attachment.initialState);

			if (!isDepthFormat)
			{
				attachmentDescription.finalLayout = CastState(attachment.finalState, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				VkAttachmentReference2 reference{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
				reference.attachment = i;
				reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorAttachmentReference.EmplaceBack(reference);
			}
			else
			{
				attachmentDescription.finalLayout = CastState(attachment.finalState, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
				depthReference.attachment = i;
				depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				hasDepth = true;
			}
			attachmentDescriptions.EmplaceBack(attachmentDescription);
			formats.EmplaceBack(format);
		}

		VkSubpassDescription2 subPass{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
		subPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subPass.colorAttachmentCount = colorAttachmentReference.Size();
		subPass.pColorAttachments = colorAttachmentReference.Data();
		if (hasDepth)
		{
			subPass.pDepthStencilAttachment = &depthReference;
		}

		VkRenderPassCreateInfo2 renderPassCreateInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
		renderPassCreateInfo.attachmentCount = attachmentDescriptions.Size();
		renderPassCreateInfo.pAttachments = attachmentDescriptions.Data();
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subPass;
		renderPassCreateInfo.dependencyCount = 0;

		VkRenderPass  vkRenderPass;
		VkFramebuffer vkFramebuffer;

		VkResult res = vkCreateRenderPass2KHR(device, &renderPassCreateInfo, nullptr, &vkRenderPass);
		if (res != VK_SUCCESS)
		{
			logger.Error("error on create render pass {} ", string_VkResult(res));
			return nullptr;
		}

		VkFramebufferCreateInfo framebufferCreateInfo{};
		framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferCreateInfo.renderPass = vkRenderPass;
		framebufferCreateInfo.width = framebufferSize.width;
		framebufferCreateInfo.height = framebufferSize.height;
		framebufferCreateInfo.layers = std::max(framebufferSize.depth, 1u);
		framebufferCreateInfo.attachmentCount = imageViews.Size();
		framebufferCreateInfo.pAttachments = imageViews.Data();
		res = vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &vkFramebuffer);
		if (res != VK_SUCCESS)
		{
			logger.Error("error on create render pass framebuffer {} ", string_VkResult(res));
			return nullptr;
		}

		VulkanRenderPass* vulkanRenderPass = Alloc<VulkanRenderPass>();
		vulkanRenderPass->vulkanDevice = this;
		vulkanRenderPass->desc = desc;
		vulkanRenderPass->renderPass = vkRenderPass;
		vulkanRenderPass->framebuffer = vkFramebuffer;
		vulkanRenderPass->hasDepth = hasDepth;
		vulkanRenderPass->formats = formats;
		vulkanRenderPass->clearValues.Resize(desc.attachments.Size());
		vulkanRenderPass->extent = {framebufferSize.width, framebufferSize.height};

		return vulkanRenderPass;
	}

	GPUCommandBuffer* VulkanDevice::CreateCommandBuffer()
	{
		VulkanCommandBuffer* vulkanCommandBuffer = Alloc<VulkanCommandBuffer>();
		vulkanCommandBuffer->vulkanDevice = this;

		VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = commandPool;
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
		if (mappedData && desc.persistentMapped)
		{
			vmaUnmapMemory(vulkanDevice->vmaAllocator, allocation);
		}

		vmaDestroyBuffer(vulkanDevice->vmaAllocator, buffer, allocation);
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
		if (textureView)
		{
			textureView->Destroy();
		}

		vmaDestroyImage(vulkanDevice->vmaAllocator, image, allocation);
		DestroyAndFree(this);
	}

	GPUTextureView* VulkanTexture::GetTextureView() const
	{
		return textureView;
	}

	VkImageView VulkanTexture::GetImageView() const
	{
		if  (textureView)
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
		vkDestroyImageView(vulkanDevice->device, imageView, nullptr);
		DestroyAndFree(this);
	}

	const SamplerDesc& VulkanSampler::GetDesc() const
	{
		return desc;
	}

	void VulkanSampler::Destroy()
	{
		vkDestroySampler(vulkanDevice->device, sampler, nullptr);
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
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
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
		createInfo.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
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
		if (pipeline)
		{
			vkDestroyPipeline(vulkanDevice->device, pipeline, nullptr);
		}

		if (pipelineLayout)
		{
			vkDestroyPipelineLayout(vulkanDevice->device, pipelineLayout, nullptr);
		}


		DestroyAndFree(this);
	}

	GPUPipeline* VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
	{
		SK_ASSERT(desc.renderPass, "render pass is required");
		SK_ASSERT(desc.shader, "shader variant is required");

		RID variant = ShaderResource::GetVariant(desc.shader, desc.variant);
		SK_ASSERT(variant, "variant not found");

		ResourceObject variantObject = Resources::Read(variant);

		PipelineDesc          pipelineDesc;
		Array<ShaderStageInfo> stages;

		GetShaderInfoFromResource(variant, &pipelineDesc, &stages);

		u32 stride = desc.vertexInputStride != U32_MAX ? desc.vertexInputStride : pipelineDesc.stride;

		VkPipelineLayout vkPipelineLayout;
		VkPipeline       vkPipeline;

		CreatePipelineLayout(device, pipelineDesc.descriptors, pipelineDesc.pushConstants, &vkPipelineLayout);

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

		// Set up multisample state (fixed settings for now)
		VkPipelineMultisampleStateCreateInfo multisampleInfo{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		multisampleInfo.sampleShadingEnable = VK_FALSE;
		multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampleInfo.minSampleShading = 1.0f;
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
		pipelineInfo.renderPass = static_cast<VulkanRenderPass*>(desc.renderPass)->renderPass;

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

		return pipeline;
	}

	GPUPipeline* VulkanDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
	{
		SK_ASSERT(desc.shader, "shader variant is required");

		RID variant = ShaderResource::GetVariant(desc.shader, desc.variant);
		SK_ASSERT(variant, "variant not found");

		//TODO - need to get from variantObject
		PipelineDesc          pipelineDesc;
		Array<ShaderStageInfo> stages;

		GetShaderInfoFromResource(variant, &pipelineDesc, &stages);

		VkPipelineLayout vkPipelineLayout;
		VkPipeline       vkPipeline;

		// Create pipeline layout (shared with graphics pipeline creation)
		CreatePipelineLayout(device, pipelineDesc.descriptors, pipelineDesc.pushConstants, &vkPipelineLayout);

		// Find the compute shader stage
		ShaderStageInfo computeStageInfo;
		bool foundComputeStage = false;
		
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
		Span bytes = variantObject.GetBlob(ShaderVariantResource::Spriv);
		
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

		SetObjectName(*this, VK_OBJECT_TYPE_PIPELINE, PtrToInt(pipeline->pipeline), desc.debugName);

		return pipeline;
	}

	GPUPipeline* VulkanDevice::CreateRayTracingPipeline(const RayTracingPipelineDesc& desc)
	{
		return nullptr;
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
		if (descriptorSetLayout)
		{
			vkDestroyDescriptorSetLayout(vulkanDevice->device, descriptorSetLayout, nullptr);
		}

		if (descriptorSet)
		{
			vkFreeDescriptorSets(vulkanDevice->device, vulkanDevice->descriptorPool, 1, &descriptorSet);
		}

		DestroyAndFree(this);
	}

	GPUDescriptorSet* VulkanDevice::CreateDescriptorSet(const DescriptorSetDesc& desc)
	{
		VkDescriptorSetLayout layout;
		bool                  hasRuntimeArray = false;
		CreateDescriptorSetLayout(device, desc.bindings, &layout, &hasRuntimeArray);

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkDescriptorSetVariableDescriptorCountAllocateInfoEXT countInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT};
		u32                                                   maxBinding = MaxBindlessResources - 1;
		countInfo.descriptorSetCount = 1;
		countInfo.pDescriptorCounts = &maxBinding;

		if (hasRuntimeArray)
		{
			allocInfo.pNext = &countInfo;
		}

		VkDescriptorSet descriptorSet;

		VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);

		if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
		{
			//TODO -- needs a pool of "descriptor pool"
			logger.Error("VK_ERROR_OUT_OF_POOL_MEMORY");
			return {};
		}

		if (result != VK_SUCCESS)
		{
			logger.Error("Error on vkAllocateDescriptorSets");
		}

		VulkanDescriptorSet* vulkanDescriptorSet = Alloc<VulkanDescriptorSet>();
		vulkanDescriptorSet->vulkanDevice = this;
		vulkanDescriptorSet->desc = desc;
		vulkanDescriptorSet->descriptorSet = descriptorSet;
		vulkanDescriptorSet->descriptorSetLayout = layout;

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

		for (const auto& descriptor: pipelineDesc.descriptors)
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

	GPUBottomLevelAS* VulkanDevice::CreateBottomLevelAS(const BottomLevelASDesc& desc)
	{
		return nullptr;
	}

	GPUTopLevelAS* VulkanDevice::CreateTopLevelAS(const TopLevelASDesc& desc)
	{
		return nullptr;
	}

	usize VulkanDevice::GetBottomLevelASSize(const BottomLevelASDesc& desc)
	{
		return 0;
	}

	usize VulkanDevice::GetTopLevelASSize(const TopLevelASDesc& desc)
	{
		return 0;
	}

	usize VulkanDevice::GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc)
	{
		return 0;
	}

	usize VulkanDevice::GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc)
	{
		return 0;
	}

	bool VulkanDevice::SubmitAndPresent(GPUSwapchain* swapchain, GPUCommandBuffer* commandBuffer, u32 currentFrame)
	{
		VulkanSwapchain*     vulkanSwapchain = static_cast<VulkanSwapchain*>(swapchain);
		VulkanCommandBuffer* vulkanCommandBuffer = static_cast<VulkanCommandBuffer*>(commandBuffer);

		VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

		VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &vulkanSwapchain->imageAvailableSemaphores[currentFrame];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderFinishedSemaphores[currentFrame];
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &vulkanCommandBuffer->commandBuffer;
		submitInfo.pWaitDstStageMask = waitStages;

		VkResult res = vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);
		if (res != VK_SUCCESS)
		{
			logger.Error("failed to submit command buffer to queue, error {} ", string_VkResult(res));
			return false;
		}

		VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame];

		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &vulkanSwapchain->swapchainKHR;
		presentInfo.pImageIndices = &vulkanSwapchain->imageIndex;

		res = vkQueuePresentKHR(presentQueue, &presentInfo);

		if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
		{
			swapchain->Resize();
		}
		else if (res != VK_SUCCESS)
		{
			logger.Error("failed to execute vkQueuePresentKHR, error {} ", string_VkResult(res));
			return false;
		}
		return true;
	}


	GPUDevice* InitVulkan(const DeviceInitDesc& initDesc)
	{
		if (volkInitialize() != VK_SUCCESS)
		{
			logger.Error("vulkan cannot be initialized");
			return nullptr;
		}

		SDL_Vulkan_LoadLibrary(nullptr);

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
			debugUtilsMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

			debugUtilsMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

			debugUtilsMessengerCreateInfo.pfnUserCallback = &DebugCallback;

			createInfo.enabledLayerCount = 1;
			createInfo.ppEnabledLayerNames = validationLayers;
			createInfo.pNext = &debugUtilsMessengerCreateInfo;
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

		u32  extensionCount;
		auto extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);

		Array<const char*> requiredExtensions{};
		requiredExtensions.Append(extensions, extensionCount);

		bool debugUtilsExtensionPresent = initDesc.enableDebugLayers && QueryInstanceExtensions({VK_EXT_DEBUG_UTILS_EXTENSION_NAME});
		if (debugUtilsExtensionPresent)
		{
			requiredExtensions.EmplaceBack(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
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

		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		Array<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.Data());

		vulkanDevice->adapters.Resize(deviceCount);

		for (int i = 0; i < deviceCount; ++i)
		{
			VulkanAdapter* vulkanAdapter = Alloc<VulkanAdapter>();
			vulkanAdapter->device = devices[i];

			vkGetPhysicalDeviceProperties2(vulkanAdapter->device, &vulkanAdapter->deviceProperties);
			vkGetPhysicalDeviceFeatures2(vulkanAdapter->device, &vulkanAdapter->deviceFeatures);


			vulkanAdapter->RatePhysicalDevice(vulkanDevice);

			vulkanDevice->adapters[i] = vulkanAdapter;
		}

		Sort(vulkanDevice->adapters.begin(), vulkanDevice->adapters.end(), [](GPUAdapter* left, GPUAdapter* right)
		{
			return static_cast<VulkanAdapter*>(left)->score > static_cast<VulkanAdapter*>(right)->score;
		});

		return vulkanDevice;
	}
}
