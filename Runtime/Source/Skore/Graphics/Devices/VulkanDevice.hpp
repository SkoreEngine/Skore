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
#include <utility>

#include "Skore/Graphics/Device.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"
#include "Skore/Core/FixedArray.hpp"

namespace Skore
{
	class VulkanDevice;

	class VulkanBuffer final : public GPUBuffer
	{
	public:
		void*             Map() override;
		void              Unmap() override;
		void              Destroy() override;
		const BufferDesc& GetDesc() const override;
		VoidPtr           GetMappedData() override;

		VulkanDevice* vulkanDevice;
		BufferDesc    desc;
		VkBuffer      buffer;
		VmaAllocation allocation;
		void*         mappedData{nullptr};
	};

	class VulkanTexture final : public GPUTexture
	{
	public:
		const TextureDesc& GetDesc() const override;
		void               Destroy() override;
		GPUTextureView*    GetTextureView() const override;

		VkImageView GetImageView() const;

		VulkanDevice*   vulkanDevice;
		TextureDesc     desc;
		VkImage         image;
		VmaAllocation   allocation;
		GPUTextureView* textureView;
		bool            isDepth = false;
	};

	class VulkanTextureView final : public GPUTextureView
	{
	public:
		const TextureViewDesc& GetDesc() override;
		GPUTexture*            GetTexture() override;
		void                   Destroy() override;

		VulkanDevice*   vulkanDevice;
		TextureViewDesc desc;
		VkImageView     imageView;
		VulkanTexture*  texture;

		VkDescriptorSet viewDescriptorSet;
	};

	class VulkanSampler final : public GPUSampler
	{
	public:
		const SamplerDesc& GetDesc() const override;
		void               Destroy() override;

		VulkanDevice* vulkanDevice;
		SamplerDesc   desc;
		VkSampler     sampler;
	};

	class VulkanQueryPool final : public GPUQueryPool
	{
	public:
		VkQueryPool   queryPool = VK_NULL_HANDLE;
		QueryPoolDesc desc;
		VulkanDevice* vulkanDevice = nullptr;

		bool GetResults(u32 firstQuery, u32 queryCount, void* data, usize stride, bool wait) override;

		const QueryPoolDesc& GetDesc() const override
		{
			return desc;
		}

		void Destroy() override;
	};

	struct VulkanAdapter final : GPUAdapter
	{
		u32              score;
		VkPhysicalDevice device;

		u32 graphicsFamily = U32_MAX;
		u32 presentFamily = U32_MAX;

		VkPhysicalDeviceProperties2 deviceProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};

		VkPhysicalDeviceRayQueryFeaturesKHR              deviceRayQueryFeaturesKhr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR deviceAccelerationStructureFeaturesKhr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &deviceRayQueryFeaturesKhr};
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR    deviceRayTracingPipelineFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &deviceAccelerationStructureFeaturesKhr};
		VkPhysicalDeviceBufferDeviceAddressFeatures      bufferDeviceAddressFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, &deviceRayTracingPipelineFeatures};
		VkPhysicalDeviceShaderDrawParametersFeatures     drawParametersFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES, &bufferDeviceAddressFeatures};
		VkPhysicalDeviceDescriptorIndexingFeatures       indexingFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, &drawParametersFeatures};
		VkPhysicalDeviceMaintenance4FeaturesKHR          maintenance4Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES_KHR, &indexingFeatures};
		VkPhysicalDeviceFeatures2                        deviceFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &maintenance4Features};

		void RatePhysicalDevice(VulkanDevice* vulkanDevice);

		u32 GetScore() override
		{
			return score;
		}

		StringView GetName() override
		{
			return deviceProperties.properties.deviceName;
		}
	};

	class VulkanRenderPass final : public GPURenderPass
	{
	public:
		const RenderPassDesc& GetDesc() const override;
		void                  Destroy() override;

		VulkanDevice*       vulkanDevice;
		RenderPassDesc      desc;
		VkRenderPass        renderPass;
		VkFramebuffer       framebuffer;
		VkExtent2D          extent;
		bool                hasDepth;
		Array<VkClearValue> clearValues;
		Array<VkFormat>     formats;
	};

	class VulkanSwapchain final : public GPUSwapchain
	{
	public:
		VulkanSwapchain(SwapchainDesc desc, VulkanDevice* vulkanDevice) : desc(std::move(desc)), vulkanDevice(vulkanDevice) {}

		const SwapchainDesc& GetDesc() const override;
		bool                 AcquireNextImage(u32 currentFrame) override;
		GPURenderPass*       GetCurrentRenderPass() override;
		bool                 Resize() override;
		void                 Destroy() override;
		u32                  GetImageCount() const override;

		bool CreateInternal();
		void DestroyInternal();

		SwapchainDesc            desc;
		VulkanDevice*            vulkanDevice;
		VkSurfaceKHR             surfaceKHR;
		VkSwapchainKHR           swapchainKHR;
		VkExtent2D               extent;
		VkFormat                 format;
		Array<VkImage>           images;
		Array<VkImageView>       imageViews;
		Array<VulkanRenderPass*> renderPasses;
		u32                      imageIndex;

		FixedArray<VkSemaphore, SK_FRAMES_IN_FLIGHT> imageAvailableSemaphores{};
	};

	class VulkanPipeline final : public GPUPipeline
	{
	public:
		PipelineBindPoint   GetBindPoint() const override;
		const PipelineDesc& GetPipelineDesc() const override;
		void                Destroy() override;

		VulkanDevice*       vulkanDevice;
		VkPipelineBindPoint bindPoint;
		PipelineDesc        pipelineDesc;
		VkPipeline          pipeline;
		VkPipelineLayout    pipelineLayout;
	};

	class VulkanDescriptorSet final : public GPUDescriptorSet
	{
	public:
		const DescriptorSetDesc& GetDesc() const override;

		void Update(const DescriptorUpdate& update) override;
		void UpdateBuffer(u32 binding, GPUBuffer* buffer, usize offset, usize size) override;
		void UpdateTexture(u32 binding, GPUTexture* texture) override;
		void UpdateTexture(u32 binding, GPUTexture* texture, u32 arrayElement) override;
		void UpdateTextureView(u32 binding, GPUTextureView* textureView, u32 arrayElement) override;
		void UpdateTextureView(u32 binding, GPUTextureView* textureView) override;
		void UpdateSampler(u32 binding, GPUSampler* sampler) override;
		void UpdateSampler(u32 binding, GPUSampler* sampler, u32 arrayElement) override;
		void Destroy() override;

		VulkanDevice*         vulkanDevice;
		DescriptorSetDesc     desc;
		VkDescriptorSet       descriptorSet;
		VkDescriptorSetLayout descriptorSetLayout;
	private:
		void InternalUpdateTexture(u32 binding,  GPUTexture* texture, GPUTextureView* textureView, u32 arrayElement);
	};

	class VulkanCommandBuffer : public GPUCommandBuffer
	{
	public:
		void Begin() override;
		void End() override;
		void Reset() override;
		void SubmitAndWait() override;
		void SetViewport(const ViewportInfo& viewportInfo) override;
		void SetScissor(Vec2 position, Extent size) override;
		void BindPipeline(GPUPipeline* pipeline) override;
		void BindDescriptorSet(GPUPipeline* pipeline, u32 setIndex, GPUDescriptorSet* descriptorSet, Span<u32> dynamicOffsets) override;
		void BindVertexBuffer(u32 firstBinding, GPUBuffer* buffers, usize offset) override;
		void BindIndexBuffer(GPUBuffer* buffer, usize offset, IndexType indexType) override;
		void PushConstants(GPUPipeline* pipeline, ShaderStage stages, u32 offset, u32 size, const void* data) override;
		void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;
		void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override;
		void DrawIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride) override;
		void DrawIndexedIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride) override;
		void Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override;
		void DispatchIndirect(GPUBuffer* buffer, usize offset) override;
		void TraceRays(GPUPipeline* pipeline, u32 width, u32 height, u32 depth) override;
		void BuildBottomLevelAS(GPUBottomLevelAS* bottomLevelAS, const AccelerationStructureBuildInfo& buildInfo) override;
		void BuildTopLevelAS(GPUTopLevelAS* topLevelAS, const AccelerationStructureBuildInfo& buildInfo) override;
		void CopyAccelerationStructure(GPUBottomLevelAS* src, GPUBottomLevelAS* dst, bool compress) override;
		void CopyAccelerationStructure(GPUTopLevelAS* src, GPUTopLevelAS* dst, bool compress) override;
		void BeginRenderPass(GPURenderPass* renderPass, Vec4 clearColor, f32 clearDepth, u32 clearStencil) override;
		void EndRenderPass() override;
		void CopyBuffer(GPUBuffer* srcBuffer, GPUBuffer* dstBuffer, usize size, usize srcOffset, usize dstOffset) override;
		void CopyBufferToTexture(GPUBuffer* srcBuffer, GPUTexture* dstTexture, Extent3D extent, u32 mipLevel, u32 arrayLayer, u64 bufferOffset) override;
		void CopyTextureToBuffer(GPUTexture* srcTexture, GPUBuffer* dstBuffer, Extent3D extent, u32 mipLevel, u32 arrayLayer) override;
		void CopyTexture(GPUTexture* srcTexture, GPUTexture* dstTexture, Extent3D extent, u32 srcMipLevel, u32 srcArrayLayer, u32 dstMipLevel, u32 dstArrayLayer) override;
		void BlitTexture(GPUTexture* srcTexture, GPUTexture* dstTexture, Extent3D srcExtent, Extent3D dstExtent, u32 srcMipLevel, u32 srcArrayLayer, u32 dstMipLevel, u32 dstArrayLayer) override;
		void FillBuffer(GPUBuffer* buffer, usize offset, usize size, u32 data) override;
		void UpdateBuffer(GPUBuffer* buffer, usize offset, usize size, const void* data) override;
		void ClearColorTexture(GPUTexture* texture, Vec4 clearValue, u32 mipLevel, u32 arrayLayer) override;
		void ClearDepthStencilTexture(GPUTexture* texture, f32 depth, u32 stencil, u32 mipLevel, u32 arrayLayer) override;
		void ResourceBarrier(GPUBuffer* buffer, ResourceState oldState, ResourceState newState) override;
		void ResourceBarrier(GPUTexture* texture, ResourceState oldState, ResourceState newState, u32 mipLevel, u32 arrayLayer) override;
		void ResourceBarrier(GPUTexture* texture, ResourceState oldState, ResourceState newState, u32 mipLevel, u32 levelCount, u32 arrayLayer, u32 layerCount) override;
		void ResourceBarrier(GPUBottomLevelAS* bottomLevelAS, ResourceState oldState, ResourceState newState) override;
		void ResourceBarrier(GPUTopLevelAS* topLevelAS, ResourceState oldState, ResourceState newState) override;
		void MemoryBarrier() override;
		void BeginQuery(GPUQueryPool* queryPool, u32 query) override;
		void EndQuery(GPUQueryPool* queryPool, u32 query) override;
		void ResetQueryPool(GPUQueryPool* queryPool, u32 firstQuery, u32 queryCount) override;
		void WriteTimestamp(GPUQueryPool* queryPool, u32 query) override;
		void CopyQueryPoolResults(GPUQueryPool* queryPool, u32 firstQuery, u32 queryCount, GPUBuffer* dstBuffer, usize dstOffset, usize stride) override;
		void BeginDebugMarker(StringView name, const Vec4& color) override;
		void EndDebugMarker() override;
		void InsertDebugMarker(StringView name, const Vec4& color) override;
		void Destroy() override;

		VulkanDevice*   vulkanDevice;
		VkCommandBuffer commandBuffer;
	};

	class VulkanDevice : public GPUDevice
	{
	public:
		~VulkanDevice() override;

		Span<GPUAdapter*>       GetAdapters() override;
		bool                    SelectAdapter(GPUAdapter* adapter) override;
		const DeviceProperties& GetProperties() override;
		const DeviceFeatures&   GetFeatures() override;
		GraphicsAPI             GetAPI() const override;
		void                    WaitIdle() override;
		GPUSwapchain*           CreateSwapchain(const SwapchainDesc& desc) override;
		GPURenderPass*          CreateRenderPass(const RenderPassDesc& desc) override;
		GPUCommandBuffer*       CreateCommandBuffer() override;
		GPUBuffer*              CreateBuffer(const BufferDesc& desc) override;
		GPUTexture*             CreateTexture(const TextureDesc& desc) override;
		GPUTextureView*         CreateTextureView(const TextureViewDesc& desc) override;
		GPUSampler*             CreateSampler(const SamplerDesc& desc) override;
		GPUPipeline*            CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
		GPUPipeline*            CreateComputePipeline(const ComputePipelineDesc& desc) override;
		GPUPipeline*            CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) override;
		GPUDescriptorSet*       CreateDescriptorSet(const DescriptorSetDesc& desc) override;
		GPUDescriptorSet*       CreateDescriptorSet(RID shader, StringView variant, u32 set) override;
		GPUQueryPool*           CreateQueryPool(const QueryPoolDesc& desc) override;
		GPUBottomLevelAS*       CreateBottomLevelAS(const BottomLevelASDesc& desc) override;
		GPUTopLevelAS*          CreateTopLevelAS(const TopLevelASDesc& desc) override;
		usize                   GetBottomLevelASSize(const BottomLevelASDesc& desc) override;
		usize                   GetTopLevelASSize(const TopLevelASDesc& desc) override;
		usize                   GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc) override;
		usize                   GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc) override;
		bool                    SubmitAndPresent(GPUSwapchain* swapchain, GPUCommandBuffer* commandBuffer, u32 currentFrame) override;

		DeviceFeatures     features;
		DeviceProperties   properties;
		VkInstance         instance;
		VulkanAdapter*     selectedAdapter;
		bool               validationLayersEnabled;
		bool               debugUtilsExtensionPresent;
		Array<GPUAdapter*> adapters;
		VkDevice           device;
		VmaAllocator       vmaAllocator;
		VkDescriptorPool   descriptorPool;
		VkCommandPool      commandPool;

		VkQueue graphicsQueue;
		VkQueue presentQueue;

		FixedArray<VkFence, SK_FRAMES_IN_FLIGHT>     inFlightFences{};
		FixedArray<VkSemaphore, SK_FRAMES_IN_FLIGHT> renderFinishedSemaphores{};
	};
}
