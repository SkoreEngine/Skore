#pragma once
#include <mutex>
#include <utility>

#include "Skore/Graphics/Device.hpp"

#define VMA_ASSERT_LEAK(expr)

#include "volk.h"
#include "vk_mem_alloc.h"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/FixedArray.hpp"
#include "Skore/Graphics/Graphics.hpp"

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

	struct VulkanQueueContext
	{
		VkQueue vkQueue;
		std::mutex mutex;
	};

	struct VulkanQueue : GPUQueue
	{
		VulkanDevice* vulkanDevice;
		VkFence fence;
		std::shared_ptr<VulkanQueueContext> context;

		void Destroy() override;
		void Submit(GPUCommandBuffer* cmd) override;
		void SubmitAndWait(GPUCommandBuffer* cmd) override;

		VkResult Submit(VkSubmitInfo* submitInfo, VkFence fence) const;
	};

	struct VulkanAdapter final : GPUAdapter
	{
		u32              score;
		VkPhysicalDevice device;

		u32 graphicsFamily = U32_MAX;
		u32 computeFamily = U32_MAX;
		u32 transferFamily = U32_MAX;
		u32 presentFamily = U32_MAX;

		VkPhysicalDeviceAccelerationStructurePropertiesKHR			accelerationStructureProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR					rayTracingPipelineProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR, &accelerationStructureProperties};
		VkPhysicalDeviceConservativeRasterizationPropertiesEXT	conservativeRasterProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT, &rayTracingPipelineProperties};
		VkPhysicalDeviceProperties2															deviceProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &conservativeRasterProps};

		VkPhysicalDeviceRayQueryFeaturesKHR              deviceRayQueryFeaturesKhr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR deviceAccelerationStructureFeaturesKhr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &deviceRayQueryFeaturesKhr};
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR    deviceRayTracingPipelineFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &deviceAccelerationStructureFeaturesKhr};
		VkPhysicalDeviceBufferDeviceAddressFeatures      bufferDeviceAddressFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, &deviceRayTracingPipelineFeatures};
		VkPhysicalDeviceShaderDrawParametersFeatures     drawParametersFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES, &bufferDeviceAddressFeatures};
		VkPhysicalDeviceDescriptorIndexingFeatures       indexingFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, &drawParametersFeatures};
		VkPhysicalDeviceMaintenance4FeaturesKHR          maintenance4Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES_KHR, &indexingFeatures};
		VkPhysicalDeviceMultiviewFeatures                multiviewFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES, &maintenance4Features};
		VkPhysicalDeviceFeatures2                        deviceFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &multiviewFeatures};


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

		VulkanDevice*  vulkanDevice;
		RenderPassDesc desc;
		VkRenderPass   renderPass;
		u32            samplesCount = 1;
	};

	struct VulkanFramebuffer : GPUFramebuffer
	{
		const FramebufferDesc& GetDesc() const override;
		void                   Destroy() override;
		Extent                 GetExtent() override;

		VulkanDevice*   vulkanDevice;
		FramebufferDesc desc;
		VkFramebuffer   framebuffer;

		Extent extent;
		Array<VkClearValue> clearValues;
	};

	class VulkanSwapchain final : public GPUSwapchain
	{
	public:
		VulkanSwapchain(SwapchainDesc desc, VulkanDevice* vulkanDevice) : desc(std::move(desc)), vulkanDevice(vulkanDevice) {}

		const SwapchainDesc& GetDesc() const override;
		DeviceResult         AcquireNextImage() override;
		u32                  GetCurrentImageIndex() const override;
		Extent               GetExtent() override;
		void                 Destroy() override;
		u32                  GetImageCount() const override;
		TextureFormat        GetFormat() const override;
		Span<GPUTexture*>    GetTextures() const override;

		bool CreateInternal();
		void DestroyInternal();

		SwapchainDesc      desc;
		VulkanDevice*      vulkanDevice;
		VkSurfaceKHR       surfaceKHR;
		VkSwapchainKHR     swapchainKHR;
		VkExtent2D         extent;
		VkFormat           format;
		Array<GPUTexture*> textures;
		u32                imageIndex;

		FixedArray<VkSemaphore, SK_FRAMES_IN_FLIGHT> imageAvailableSemaphores{};
		Array<VkSemaphore> renderFinishedSemaphores{};

		bool ValidSwapchain() const;
	};

	class VulkanBottomLevelAS final : public GPUBottomLevelAS
	{
	public:
		const BottomLevelASDesc& GetDesc() const override;
		bool                    IsCompacted() const override;
		usize                   GetCompactedSize() const override;
		void                    Destroy() override;

		VulkanDevice*                vulkanDevice;
		BottomLevelASDesc            desc;
		VkAccelerationStructureKHR   accelerationStructure = VK_NULL_HANDLE;
		VkBuffer                     buffer = VK_NULL_HANDLE;
		VmaAllocation                allocation = nullptr;
		VkDeviceAddress              deviceAddress = 0;
		VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
		bool                         compacted = false;

		Array<VkAccelerationStructureGeometryKHR>       geometries;
		Array<VkAccelerationStructureBuildRangeInfoKHR>  buildRangeInfos;
		Array<u32>                                       maxPrimitiveCounts;
	};

	class VulkanTopLevelAS final : public GPUTopLevelAS
	{
	public:
		const TopLevelASDesc& GetDesc() const override;
		bool                  UpdateInstances(Span<InstanceDesc> instances) override;
		void                  UpdateInstance(u32 index, const InstanceDesc& instance) override;
		void                  SetInstanceCount(u32 count) override;
		void                  Destroy() override;

		VulkanDevice*                vulkanDevice;
		TopLevelASDesc               desc;
		VkAccelerationStructureKHR   accelerationStructure = VK_NULL_HANDLE;
		VkBuffer                     buffer = VK_NULL_HANDLE;
		VmaAllocation                allocation = nullptr;
		VkDeviceAddress              deviceAddress = 0;
		VkBuildAccelerationStructureFlagsKHR buildFlags = 0;

		VkBuffer                     instanceBuffer = VK_NULL_HANDLE;
		VmaAllocation                instanceAllocation = nullptr;
		void*                        instanceMappedData = nullptr;
		u32                          instanceCount = 0;
		u32                          maxInstanceCount = 0;
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

		Array<VkDescriptorSetLayout> descriptorSetLayouts;

		// Ray tracing SBT
		VkBuffer                          sbtBuffer = VK_NULL_HANDLE;
		VmaAllocation                     sbtAllocation = nullptr;
		VkStridedDeviceAddressRegionKHR   sbtRaygenRegion{};
		VkStridedDeviceAddressRegionKHR   sbtMissRegion{};
		VkStridedDeviceAddressRegionKHR   sbtHitRegion{};
		VkStridedDeviceAddressRegionKHR   sbtCallableRegion{};
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
		VkDescriptorPool      dedicatedPool = nullptr;
	private:
		void InternalUpdateTexture(u32 binding,  GPUTexture* texture, GPUTextureView* textureView, u32 arrayElement);
	};

	class VulkanCommandBuffer : public GPUCommandBuffer
	{
	public:
		void Begin() override;
		void End() override;
		void Reset() override;
		void SetViewport(const ViewportInfo& viewportInfo) override;
		void SetScissor(Vec2 position, Extent size) override;
		void BindPipeline(GPUPipeline* pipeline) override;
		void BindDescriptorSet(GPUPipeline* pipeline, u32 setIndex, GPUDescriptorSet* descriptorSet, Span<u32> dynamicOffsets) override;
		void BindVertexBuffer(u32 firstBinding, GPUBuffer* buffers, usize offset) override;
		void BindIndexBuffer(GPUBuffer* buffer, usize offset, IndexType indexType) override;
		void SetTexture(GPUPipeline* pipeline, u32 set, u32 binding, GPUTexture* texture, u32 arrayElement) override;
		void SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range) override;
		void SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range, u32 arrayIndex) override;
		void SetSampler(GPUPipeline* pipeline, u32 set, u32 binding, GPUSampler* sampler) override;
		void SetTextureView(GPUPipeline* pipeline, u32 set, u32 binding, GPUTextureView* textureView, u32 arrayElement) override;
		void SetAccelerationStructure(GPUPipeline* pipeline, u32 set, u32 binding, GPUTopLevelAS* topLevelAS) override;
		void PushConstants(GPUPipeline* pipeline, ShaderStage stages, u32 offset, u32 size, const void* data) override;
		void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;
		void DrawIndirectCount(GPUBuffer* buffer, u64 offset, GPUBuffer* countBuffer, u64 countBufferOffset, u32 maxDrawCount, u32 stride) override;
		void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override;
		void DrawIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride) override;
		void DrawIndexedIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride) override;
		void DrawIndexedIndirectCount(GPUBuffer* buffer, u64 offset, GPUBuffer* countBuffer, u64 countBufferOffset, u32 maxDrawCount, u32 stride) override;
		void Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override;
		void DispatchIndirect(GPUBuffer* buffer, usize offset) override;
		void TraceRays(GPUPipeline* pipeline, u32 width, u32 height, u32 depth) override;
		void BuildBottomLevelAS(GPUBottomLevelAS* bottomLevelAS, const AccelerationStructureBuildInfo& buildInfo) override;
		void BuildTopLevelAS(GPUTopLevelAS* topLevelAS, const AccelerationStructureBuildInfo& buildInfo) override;
		void CopyAccelerationStructure(GPUBottomLevelAS* src, GPUBottomLevelAS* dst, bool compress) override;
		void CopyAccelerationStructure(GPUTopLevelAS* src, GPUTopLevelAS* dst, bool compress) override;
		void BeginRenderPass(const BeginRenderPassInfo& info) override;
		void EndRenderPass() override;
		void CopyBuffer(GPUBuffer* srcBuffer, GPUBuffer* dstBuffer, usize size, usize srcOffset, usize dstOffset) override;
		void CopyBufferToTexture(const BufferTextureCopy& copy) override;
		void CopyTextureToBuffer(const BufferTextureCopy& copy) override;
		void CopyTexture(const TextureCopy& copy) override;
		void BlitTexture(const TextureBlit& blit) override;
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
		VkCommandPool   commandPool;
		QueueType       queueType = QueueType::Graphics;

		void InternalSetTexture(GPUPipeline* pipeline, u32 set, u32 binding, GPUTexture* texture, GPUTextureView* textureView, u32 arrayElement);
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
		GPUFramebuffer*         CreateFramebuffer(const FramebufferDesc& desc) override;
		GPUCommandBuffer*       CreateCommandBuffer(const QueueType& queueType) override;
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
		GPUQueue*               CreateQueue(const QueueDesc& desc) override;
		usize                   GetBottomLevelASSize(const BottomLevelASDesc& desc) override;
		usize                   GetTopLevelASSize(const TopLevelASDesc& desc) override;
		usize                   GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc) override;
		usize                   GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc) override;

		DeviceFeatures     features;
		DeviceProperties   properties;
		VkInstance         instance;
		VulkanAdapter*     selectedAdapter;
		bool               validationLayersEnabled;
		bool               debugUtilsExtensionPresent;
		Array<GPUAdapter*> adapters;
		VkDevice           device;
		VmaAllocator       vmaAllocator;

		std::mutex       descriptorPoolMutex;
		VkDescriptorPool descriptorPool;

		VulkanQueue graphicsQueue;
		VulkanQueue presentQueue;
		VulkanQueue computeQueue;
		VulkanQueue transferQueue;

		u32 currentFrame = 0;

		FixedArray<VkFence, SK_FRAMES_IN_FLIGHT>     inFlightFences{};

		VkCommandPool                                         commandPool;
		FixedArray<VulkanCommandBuffer*, SK_FRAMES_IN_FLIGHT> commandBuffers;

		Array<VulkanSwapchain*> swapchains;

		void FlushDestructors(u8 frame);

		void GetMemoryBudgets(Array<MemoryHeapBudget>& outBudgets) override;

		EventHandler<OnRecordRenderCommands> onRecordRenderCommands{};

		void ExecuteFrame();
	};
}
