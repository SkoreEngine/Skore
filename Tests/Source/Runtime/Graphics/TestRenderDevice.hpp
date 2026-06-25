#pragma once

#include "Skore/Graphics/Device.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	class TestRenderDevice;

	struct TestBarrierRecord
	{
		ResourceState oldState;
		ResourceState newState;
		u32           baseMipLevel;
		u32           levelCount;
		u32           baseArrayLayer;
		u32           layerCount;
	};

	struct TestCommandStats
	{
		u32 drawCount = 0;
		u32 dispatchCount = 0;
		u32 traceRaysCount = 0;
		u32 renderPassCount = 0;
		u32 copyCount = 0;
		u32 clearCount = 0;
		u32 textureBarrierCount = 0;
		u32 bufferBarrierCount = 0;
	};

	class TestGPUBuffer final : public GPUBuffer
	{
	public:
		void*             Map() override;
		void              Unmap() override;
		void              Destroy() override;
		const BufferDesc& GetDesc() const override;
		VoidPtr           GetMappedData() override;

		TestRenderDevice* device = nullptr;
		BufferDesc        desc;
		Array<u8>         storage;
		ResourceState     state = ResourceState::Undefined;
		bool              mapped = false;
	};

	class TestGPUTexture final : public GPUTexture
	{
	public:
		const TextureDesc& GetDesc() const override;
		GPUTextureView*    GetTextureView() const override;
		void               Destroy() override;

		u32 GetMipLevels() const;
		u32 GetArrayLayers() const;

		// State of a single (mip, array layer) subresource. This is the data tests inspect.
		ResourceState GetSubresourceState(u32 mipLevel, u32 arrayLayer = 0) const;
		bool          AllSubresourcesInState(ResourceState state) const;

		// Applies a recorded barrier to the tracked layout. Called by the command buffer.
		void ApplyBarrier(ResourceState oldState, ResourceState newState, u32 baseMipLevel, u32 levelCount, u32 baseArrayLayer, u32 layerCount);

		u32 SubresourceIndex(u32 mipLevel, u32 arrayLayer) const;

		TestRenderDevice*        device = nullptr;
		TextureDesc              desc;
		GPUTextureView*          textureView = nullptr;
		Array<ResourceState>     subresourceStates; // indexed by SubresourceIndex(mip, layer)
		Array<TestBarrierRecord> barrierHistory;
		u32                      mismatchCount = 0; // times a barrier's declared oldState != the tracked state
	};

	class TestGPUTextureView final : public GPUTextureView
	{
	public:
		const TextureViewDesc& GetDesc() override;
		GPUTexture*            GetTexture() override;
		void                   Destroy() override;

		TestRenderDevice* device = nullptr;
		TextureViewDesc   desc;
		TestGPUTexture*   texture = nullptr;
	};

	class TestGPUSampler final : public GPUSampler
	{
	public:
		const SamplerDesc& GetDesc() const override;
		void               Destroy() override;

		TestRenderDevice* device = nullptr;
		SamplerDesc       desc;
	};

	class TestGPUPipeline final : public GPUPipeline
	{
	public:
		PipelineBindPoint   GetBindPoint() const override;
		const PipelineDesc& GetPipelineDesc() const override;
		void                Destroy() override;

		TestRenderDevice* device = nullptr;
		PipelineBindPoint bindPoint = PipelineBindPoint::Graphics;
		PipelineDesc      pipelineDesc;
	};

	class TestGPUDescriptorSet final : public GPUDescriptorSet
	{
	public:
		const DescriptorSetDesc& GetDesc() const override;
		void                     Update(const DescriptorUpdate& update) override;
		void                     UpdateBuffer(u32 binding, GPUBuffer* buffer, usize offset, usize size) override;
		void                     UpdateTexture(u32 binding, GPUTexture* texture) override;
		void                     UpdateTexture(u32 binding, GPUTexture* texture, u32 arrayElement) override;
		void                     UpdateTextureView(u32 binding, GPUTextureView* textureView) override;
		void                     UpdateTextureView(u32 binding, GPUTextureView* textureView, u32 arrayElement) override;
		void                     UpdateSampler(u32 binding, GPUSampler* sampler) override;
		void                     UpdateSampler(u32 binding, GPUSampler* sampler, u32 arrayElement) override;
		void                     Destroy() override;

		TestRenderDevice* device = nullptr;
		DescriptorSetDesc desc;
		u32               updateCount = 0;
	};

	class TestGPURenderPass final : public GPURenderPass
	{
	public:
		const RenderPassDesc& GetDesc() const override;
		void                  Destroy() override;

		TestRenderDevice* device = nullptr;
		RenderPassDesc    desc;
	};

	class TestGPUFramebuffer final : public GPUFramebuffer
	{
	public:
		const FramebufferDesc& GetDesc() const override;
		void                   Destroy() override;
		Extent                 GetExtent() override;

		TestRenderDevice* device = nullptr;
		FramebufferDesc   desc;
		Extent            extent;
	};

	class TestGPUQueryPool final : public GPUQueryPool
	{
	public:
		const QueryPoolDesc& GetDesc() const override;
		bool                 GetResults(u32 firstQuery, u32 queryCount, void* data, usize stride, bool wait) override;
		void                 Destroy() override;

		TestRenderDevice* device = nullptr;
		QueryPoolDesc     desc;
	};

	class TestGPUBottomLevelAS final : public GPUBottomLevelAS
	{
	public:
		const BottomLevelASDesc& GetDesc() const override;
		bool                     IsCompacted() const override;
		usize                    GetCompactedSize() const override;
		void                     Destroy() override;

		TestRenderDevice* device = nullptr;
		BottomLevelASDesc desc;
	};

	class TestGPUTopLevelAS final : public GPUTopLevelAS
	{
	public:
		const TopLevelASDesc& GetDesc() const override;
		bool                  UpdateInstances(Span<InstanceDesc> instances) override;
		void                  UpdateInstance(u32 index, const InstanceDesc& instance) override;
		void                  SetInstanceCount(u32 count) override;
		void                  Destroy() override;

		TestRenderDevice* device = nullptr;
		TopLevelASDesc    desc;
		u32               instanceCount = 0;
	};

	class TestGPUSwapchain final : public GPUSwapchain
	{
	public:
		const SwapchainDesc& GetDesc() const override;
		DeviceResult         AcquireNextImage() override;
		Extent               GetExtent() override;
		void                 Destroy() override;
		u32                  GetImageCount() const override;
		Span<GPUTexture*>    GetTextures() const override;
		u32                  GetCurrentImageIndex() const override;
		Format               GetFormat() const override;

		TestRenderDevice*  device = nullptr;
		SwapchainDesc      desc;
		Extent             extent{1, 1};
		Array<GPUTexture*> textures;
		u32                imageIndex = 0;
	};

	class TestGPUQueue final : public GPUQueue
	{
	public:
		void Destroy() override;
		void Submit(GPUCommandBuffer* cmd) override;
		void SubmitAndWait(GPUCommandBuffer* cmd) override;

		TestRenderDevice* device = nullptr;
		QueueDesc         desc;
		u32               submitCount = 0;
	};

	class TestGPUCommandBuffer final : public GPUCommandBuffer
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
		void PushConstants(GPUPipeline* pipeline, ShaderStage stages, u32 offset, u32 size, const void* data) override;
		void SetTexture(GPUPipeline* pipeline, u32 set, u32 binding, GPUTexture* texture, u32 arrayElement) override;
		void SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range) override;
		void SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range, u32 arrayIndex) override;
		void SetSampler(GPUPipeline* pipeline, u32 set, u32 binding, GPUSampler* sampler) override;
		void SetTextureView(GPUPipeline* pipeline, u32 set, u32 binding, GPUTextureView* textureView, u32 arrayElement) override;
		void SetAccelerationStructure(GPUPipeline* pipeline, u32 set, u32 binding, GPUTopLevelAS* topLevelAS) override;
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

		TestRenderDevice* device = nullptr;
		QueueType         queueType = QueueType::Graphics;
		TestCommandStats  stats;
		bool              recording = false;
		bool              renderPassActive = false;
		GPUPipeline*      boundPipeline = nullptr;
	};

	// CPU-side fake GPUDevice for unit tests. Creates no real GPU resources; instead it
	// records bookkeeping (most importantly each texture's ResourceState per mip/array layer)
	// so render-graph barrier and layout logic can be asserted without a live backend.
	//
	// The device owns every object it creates and frees them in its destructor, so tests do
	// not need to call Destroy() on individual resources. Barriers are applied immediately at
	// record time on the command buffer, which is deterministic for single-threaded tests.
	class TestRenderDevice final : public GPUDevice
	{
	public:
		TestRenderDevice();
		~TestRenderDevice() override;

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

		usize GetBottomLevelASSize(const BottomLevelASDesc& desc) override;
		usize GetTopLevelASSize(const TopLevelASDesc& desc) override;
		usize GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc) override;
		usize GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc) override;

		void GetMemoryBudgets(Array<MemoryHeapBudget>& outBudgets) override;

		// Test introspection.
		u32 GetCreatedTextureCount() const;
		u32 GetCreatedBufferCount() const;

		DeviceProperties properties{};
		DeviceFeatures   features{};

	private:
		Array<TestGPUBuffer*>        buffers;
		Array<TestGPUTexture*>       textures;
		Array<TestGPUTextureView*>   textureViews;
		Array<TestGPUSampler*>       samplers;
		Array<TestGPUPipeline*>      pipelines;
		Array<TestGPUDescriptorSet*> descriptorSets;
		Array<TestGPURenderPass*>    renderPasses;
		Array<TestGPUFramebuffer*>   framebuffers;
		Array<TestGPUQueryPool*>     queryPools;
		Array<TestGPUBottomLevelAS*> bottomLevelStructures;
		Array<TestGPUTopLevelAS*>    topLevelStructures;
		Array<TestGPUSwapchain*>     swapchains;
		Array<TestGPUQueue*>         queues;
		Array<TestGPUCommandBuffer*> commandBuffers;
	};
}
