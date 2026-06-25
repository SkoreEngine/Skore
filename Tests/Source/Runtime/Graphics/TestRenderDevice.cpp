#include "TestRenderDevice.hpp"

namespace Skore
{
	//---------------------------------------------------------------------------------------------
	// Buffer
	//---------------------------------------------------------------------------------------------

	void* TestGPUBuffer::Map()
	{
		mapped = true;
		return storage.Data();
	}

	void TestGPUBuffer::Unmap()
	{
		mapped = false;
	}

	void TestGPUBuffer::Destroy() {}

	const BufferDesc& TestGPUBuffer::GetDesc() const
	{
		return desc;
	}

	VoidPtr TestGPUBuffer::GetMappedData()
	{
		return storage.Data();
	}

	//---------------------------------------------------------------------------------------------
	// Texture
	//---------------------------------------------------------------------------------------------

	const TextureDesc& TestGPUTexture::GetDesc() const
	{
		return desc;
	}

	GPUTextureView* TestGPUTexture::GetTextureView() const
	{
		return textureView;
	}

	void TestGPUTexture::Destroy() {}

	u32 TestGPUTexture::GetMipLevels() const
	{
		return desc.mipLevels > 0 ? desc.mipLevels : 1;
	}

	u32 TestGPUTexture::GetArrayLayers() const
	{
		return desc.arrayLayers > 0 ? desc.arrayLayers : 1;
	}

	u32 TestGPUTexture::SubresourceIndex(u32 mipLevel, u32 arrayLayer) const
	{
		return mipLevel * GetArrayLayers() + arrayLayer;
	}

	ResourceState TestGPUTexture::GetSubresourceState(u32 mipLevel, u32 arrayLayer) const
	{
		u32 index = SubresourceIndex(mipLevel, arrayLayer);
		if (index >= subresourceStates.Size())
		{
			return ResourceState::Undefined;
		}
		return subresourceStates[index];
	}

	bool TestGPUTexture::AllSubresourcesInState(ResourceState state) const
	{
		for (ResourceState subresourceState : subresourceStates)
		{
			if (subresourceState != state)
			{
				return false;
			}
		}
		return true;
	}

	void TestGPUTexture::ApplyBarrier(ResourceState oldState, ResourceState newState, u32 baseMipLevel, u32 levelCount, u32 baseArrayLayer, u32 layerCount)
	{
		u32 mipLevels = GetMipLevels();
		u32 arrayLayers = GetArrayLayers();

		if (levelCount == U32_MAX || baseMipLevel + levelCount > mipLevels)
		{
			levelCount = mipLevels - baseMipLevel;
		}
		if (layerCount == U32_MAX || baseArrayLayer + layerCount > arrayLayers)
		{
			layerCount = arrayLayers - baseArrayLayer;
		}

		for (u32 mip = baseMipLevel; mip < baseMipLevel + levelCount; ++mip)
		{
			for (u32 layer = baseArrayLayer; layer < baseArrayLayer + layerCount; ++layer)
			{
				u32 index = SubresourceIndex(mip, layer);
				// Undefined as a source means "discard"; any tracked state is valid to come from.
				if (oldState != ResourceState::Undefined && subresourceStates[index] != oldState)
				{
					++mismatchCount;
				}
				subresourceStates[index] = newState;
			}
		}

		barrierHistory.EmplaceBack(TestBarrierRecord{oldState, newState, baseMipLevel, levelCount, baseArrayLayer, layerCount});
	}

	//---------------------------------------------------------------------------------------------
	// Texture view
	//---------------------------------------------------------------------------------------------

	const TextureViewDesc& TestGPUTextureView::GetDesc()
	{
		return desc;
	}

	GPUTexture* TestGPUTextureView::GetTexture()
	{
		return texture;
	}

	void TestGPUTextureView::Destroy() {}

	//---------------------------------------------------------------------------------------------
	// Sampler
	//---------------------------------------------------------------------------------------------

	const SamplerDesc& TestGPUSampler::GetDesc() const
	{
		return desc;
	}

	void TestGPUSampler::Destroy() {}

	//---------------------------------------------------------------------------------------------
	// Pipeline
	//---------------------------------------------------------------------------------------------

	PipelineBindPoint TestGPUPipeline::GetBindPoint() const
	{
		return bindPoint;
	}

	const PipelineDesc& TestGPUPipeline::GetPipelineDesc() const
	{
		return pipelineDesc;
	}

	void TestGPUPipeline::Destroy() {}

	//---------------------------------------------------------------------------------------------
	// Descriptor set
	//---------------------------------------------------------------------------------------------

	const DescriptorSetDesc& TestGPUDescriptorSet::GetDesc() const
	{
		return desc;
	}

	void TestGPUDescriptorSet::Update(const DescriptorUpdate& update)
	{
		++updateCount;
	}

	void TestGPUDescriptorSet::UpdateBuffer(u32 binding, GPUBuffer* buffer, usize offset, usize size)
	{
		++updateCount;
	}

	void TestGPUDescriptorSet::UpdateTexture(u32 binding, GPUTexture* texture)
	{
		++updateCount;
	}

	void TestGPUDescriptorSet::UpdateTexture(u32 binding, GPUTexture* texture, u32 arrayElement)
	{
		++updateCount;
	}

	void TestGPUDescriptorSet::UpdateTextureView(u32 binding, GPUTextureView* textureView)
	{
		++updateCount;
	}

	void TestGPUDescriptorSet::UpdateTextureView(u32 binding, GPUTextureView* textureView, u32 arrayElement)
	{
		++updateCount;
	}

	void TestGPUDescriptorSet::UpdateSampler(u32 binding, GPUSampler* sampler)
	{
		++updateCount;
	}

	void TestGPUDescriptorSet::UpdateSampler(u32 binding, GPUSampler* sampler, u32 arrayElement)
	{
		++updateCount;
	}

	void TestGPUDescriptorSet::Destroy() {}

	//---------------------------------------------------------------------------------------------
	// Render pass
	//---------------------------------------------------------------------------------------------

	const RenderPassDesc& TestGPURenderPass::GetDesc() const
	{
		return desc;
	}

	void TestGPURenderPass::Destroy() {}

	//---------------------------------------------------------------------------------------------
	// Framebuffer
	//---------------------------------------------------------------------------------------------

	const FramebufferDesc& TestGPUFramebuffer::GetDesc() const
	{
		return desc;
	}

	void TestGPUFramebuffer::Destroy() {}

	Extent TestGPUFramebuffer::GetExtent()
	{
		return extent;
	}

	//---------------------------------------------------------------------------------------------
	// Query pool
	//---------------------------------------------------------------------------------------------

	const QueryPoolDesc& TestGPUQueryPool::GetDesc() const
	{
		return desc;
	}

	bool TestGPUQueryPool::GetResults(u32 firstQuery, u32 queryCount, void* data, usize stride, bool wait)
	{
		return false;
	}

	void TestGPUQueryPool::Destroy() {}

	//---------------------------------------------------------------------------------------------
	// Acceleration structures
	//---------------------------------------------------------------------------------------------

	const BottomLevelASDesc& TestGPUBottomLevelAS::GetDesc() const
	{
		return desc;
	}

	bool TestGPUBottomLevelAS::IsCompacted() const
	{
		return false;
	}

	usize TestGPUBottomLevelAS::GetCompactedSize() const
	{
		return 0;
	}

	void TestGPUBottomLevelAS::Destroy() {}

	const TopLevelASDesc& TestGPUTopLevelAS::GetDesc() const
	{
		return desc;
	}

	bool TestGPUTopLevelAS::UpdateInstances(Span<InstanceDesc> instances)
	{
		instanceCount = static_cast<u32>(instances.Size());
		return true;
	}

	void TestGPUTopLevelAS::UpdateInstance(u32 index, const InstanceDesc& instance) {}

	void TestGPUTopLevelAS::SetInstanceCount(u32 count)
	{
		instanceCount = count;
	}

	void TestGPUTopLevelAS::Destroy() {}

	//---------------------------------------------------------------------------------------------
	// Swapchain
	//---------------------------------------------------------------------------------------------

	const SwapchainDesc& TestGPUSwapchain::GetDesc() const
	{
		return desc;
	}

	DeviceResult TestGPUSwapchain::AcquireNextImage()
	{
		imageIndex = textures.Empty() ? 0 : (imageIndex + 1) % static_cast<u32>(textures.Size());
		return DeviceResult::Success;
	}

	Extent TestGPUSwapchain::GetExtent()
	{
		return extent;
	}

	void TestGPUSwapchain::Destroy() {}

	u32 TestGPUSwapchain::GetImageCount() const
	{
		return static_cast<u32>(textures.Size());
	}

	Span<GPUTexture*> TestGPUSwapchain::GetTextures() const
	{
		return textures;
	}

	u32 TestGPUSwapchain::GetCurrentImageIndex() const
	{
		return imageIndex;
	}

	Format TestGPUSwapchain::GetFormat() const
	{
		return desc.desiredFormat;
	}

	//---------------------------------------------------------------------------------------------
	// Queue
	//---------------------------------------------------------------------------------------------

	void TestGPUQueue::Destroy() {}

	void TestGPUQueue::Submit(GPUCommandBuffer* cmd)
	{
		++submitCount;
	}

	void TestGPUQueue::SubmitAndWait(GPUCommandBuffer* cmd)
	{
		++submitCount;
	}

	//---------------------------------------------------------------------------------------------
	// Command buffer
	//---------------------------------------------------------------------------------------------

	void TestGPUCommandBuffer::Begin()
	{
		recording = true;
		stats = TestCommandStats{};
		renderPassActive = false;
		boundPipeline = nullptr;
	}

	void TestGPUCommandBuffer::End()
	{
		recording = false;
	}

	void TestGPUCommandBuffer::Reset()
	{
		stats = TestCommandStats{};
		renderPassActive = false;
		boundPipeline = nullptr;
	}

	void TestGPUCommandBuffer::SetViewport(const ViewportInfo& viewportInfo) {}

	void TestGPUCommandBuffer::SetScissor(Vec2 position, Extent size) {}

	void TestGPUCommandBuffer::BindPipeline(GPUPipeline* pipeline)
	{
		boundPipeline = pipeline;
	}

	void TestGPUCommandBuffer::BindDescriptorSet(GPUPipeline* pipeline, u32 setIndex, GPUDescriptorSet* descriptorSet, Span<u32> dynamicOffsets) {}

	void TestGPUCommandBuffer::BindVertexBuffer(u32 firstBinding, GPUBuffer* buffers, usize offset) {}

	void TestGPUCommandBuffer::BindIndexBuffer(GPUBuffer* buffer, usize offset, IndexType indexType) {}

	void TestGPUCommandBuffer::PushConstants(GPUPipeline* pipeline, ShaderStage stages, u32 offset, u32 size, const void* data) {}

	void TestGPUCommandBuffer::SetTexture(GPUPipeline* pipeline, u32 set, u32 binding, GPUTexture* texture, u32 arrayElement) {}

	void TestGPUCommandBuffer::SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range) {}

	void TestGPUCommandBuffer::SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range, u32 arrayIndex) {}

	void TestGPUCommandBuffer::SetSampler(GPUPipeline* pipeline, u32 set, u32 binding, GPUSampler* sampler) {}

	void TestGPUCommandBuffer::SetTextureView(GPUPipeline* pipeline, u32 set, u32 binding, GPUTextureView* textureView, u32 arrayElement) {}

	void TestGPUCommandBuffer::SetAccelerationStructure(GPUPipeline* pipeline, u32 set, u32 binding, GPUTopLevelAS* topLevelAS) {}

	void TestGPUCommandBuffer::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
	{
		++stats.drawCount;
	}

	void TestGPUCommandBuffer::DrawIndirectCount(GPUBuffer* buffer, u64 offset, GPUBuffer* countBuffer, u64 countBufferOffset, u32 maxDrawCount, u32 stride)
	{
		++stats.drawCount;
	}

	void TestGPUCommandBuffer::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
	{
		++stats.drawCount;
	}

	void TestGPUCommandBuffer::DrawIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride)
	{
		++stats.drawCount;
	}

	void TestGPUCommandBuffer::DrawIndexedIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride)
	{
		++stats.drawCount;
	}

	void TestGPUCommandBuffer::DrawIndexedIndirectCount(GPUBuffer* buffer, u64 offset, GPUBuffer* countBuffer, u64 countBufferOffset, u32 maxDrawCount, u32 stride)
	{
		++stats.drawCount;
	}

	void TestGPUCommandBuffer::Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ)
	{
		++stats.dispatchCount;
	}

	void TestGPUCommandBuffer::DispatchIndirect(GPUBuffer* buffer, usize offset)
	{
		++stats.dispatchCount;
	}

	void TestGPUCommandBuffer::TraceRays(GPUPipeline* pipeline, u32 width, u32 height, u32 depth)
	{
		++stats.traceRaysCount;
	}

	void TestGPUCommandBuffer::BuildBottomLevelAS(GPUBottomLevelAS* bottomLevelAS, const AccelerationStructureBuildInfo& buildInfo) {}

	void TestGPUCommandBuffer::BuildTopLevelAS(GPUTopLevelAS* topLevelAS, const AccelerationStructureBuildInfo& buildInfo) {}

	void TestGPUCommandBuffer::CopyAccelerationStructure(GPUBottomLevelAS* src, GPUBottomLevelAS* dst, bool compress) {}

	void TestGPUCommandBuffer::CopyAccelerationStructure(GPUTopLevelAS* src, GPUTopLevelAS* dst, bool compress) {}

	void TestGPUCommandBuffer::BeginRenderPass(const BeginRenderPassInfo& info)
	{
		renderPassActive = true;
		++stats.renderPassCount;
	}

	void TestGPUCommandBuffer::EndRenderPass()
	{
		renderPassActive = false;
	}

	void TestGPUCommandBuffer::CopyBuffer(GPUBuffer* srcBuffer, GPUBuffer* dstBuffer, usize size, usize srcOffset, usize dstOffset)
	{
		++stats.copyCount;
	}

	void TestGPUCommandBuffer::CopyBufferToTexture(const BufferTextureCopy& copy)
	{
		++stats.copyCount;
	}

	void TestGPUCommandBuffer::CopyTextureToBuffer(const BufferTextureCopy& copy)
	{
		++stats.copyCount;
	}

	void TestGPUCommandBuffer::CopyTexture(const TextureCopy& copy)
	{
		++stats.copyCount;
	}

	void TestGPUCommandBuffer::BlitTexture(const TextureBlit& blit)
	{
		++stats.copyCount;
	}

	void TestGPUCommandBuffer::FillBuffer(GPUBuffer* buffer, usize offset, usize size, u32 data) {}

	void TestGPUCommandBuffer::UpdateBuffer(GPUBuffer* buffer, usize offset, usize size, const void* data)
	{
		TestGPUBuffer* testBuffer = static_cast<TestGPUBuffer*>(buffer);
		if (testBuffer != nullptr && data != nullptr && offset + size <= testBuffer->storage.Size())
		{
			const u8* src = static_cast<const u8*>(data);
			for (usize i = 0; i < size; ++i)
			{
				testBuffer->storage[offset + i] = src[i];
			}
		}
	}

	void TestGPUCommandBuffer::ClearColorTexture(GPUTexture* texture, Vec4 clearValue, u32 mipLevel, u32 arrayLayer)
	{
		++stats.clearCount;
	}

	void TestGPUCommandBuffer::ClearDepthStencilTexture(GPUTexture* texture, f32 depth, u32 stencil, u32 mipLevel, u32 arrayLayer)
	{
		++stats.clearCount;
	}

	void TestGPUCommandBuffer::ResourceBarrier(const BufferBarrierDesc& barrier)
	{
		++stats.bufferBarrierCount;
		if (TestGPUBuffer* testBuffer = static_cast<TestGPUBuffer*>(barrier.buffer))
		{
			testBuffer->state = barrier.newState;
		}
	}

	void TestGPUCommandBuffer::ResourceBarrier(const TextureBarrierDesc& barrier)
	{
		++stats.textureBarrierCount;
		if (TestGPUTexture* testTexture = static_cast<TestGPUTexture*>(barrier.texture))
		{
			testTexture->ApplyBarrier(
				barrier.oldState,
				barrier.newState,
				barrier.baseMipLevel,
				barrier.mipLevelCount,
				barrier.baseArrayLayer,
				barrier.arrayLayerCount
			);
		}
	}

	void TestGPUCommandBuffer::ResourceBarrier(GPUBottomLevelAS* bottomLevelAS, ResourceState oldState, ResourceState newState) {}

	void TestGPUCommandBuffer::ResourceBarrier(GPUTopLevelAS* topLevelAS, ResourceState oldState, ResourceState newState) {}

	void TestGPUCommandBuffer::MemoryBarrier() {}

	void TestGPUCommandBuffer::BeginQuery(GPUQueryPool* queryPool, u32 query) {}

	void TestGPUCommandBuffer::EndQuery(GPUQueryPool* queryPool, u32 query) {}

	void TestGPUCommandBuffer::ResetQueryPool(GPUQueryPool* queryPool, u32 firstQuery, u32 queryCount) {}

	void TestGPUCommandBuffer::WriteTimestamp(GPUQueryPool* queryPool, u32 query) {}

	void TestGPUCommandBuffer::CopyQueryPoolResults(GPUQueryPool* queryPool, u32 firstQuery, u32 queryCount, GPUBuffer* dstBuffer, usize dstOffset, usize stride) {}

	void TestGPUCommandBuffer::BeginDebugMarker(StringView name, const Vec4& color) {}

	void TestGPUCommandBuffer::EndDebugMarker() {}

	void TestGPUCommandBuffer::InsertDebugMarker(StringView name, const Vec4& color) {}

	void TestGPUCommandBuffer::Destroy() {}

	//---------------------------------------------------------------------------------------------
	// Device
	//---------------------------------------------------------------------------------------------

	TestRenderDevice::TestRenderDevice()
	{
		properties.deviceType = DeviceType::Other;
		properties.deviceName = "TestRenderDevice";
		properties.vendorName = "Skore";
		properties.driverVersion = "0.0.0";

		features.computeShader = true;
		features.independentBlend = true;
		features.bindlessTextureSupported = true;
		features.bindlessSamplerSupported = true;
		features.bindlessBufferSupported = true;

		DeviceLimits& limits = properties.limits;
		limits.maxTextureSize = 16384;
		limits.maxTexture3DSize = 2048;
		limits.maxCubeMapSize = 16384;
		limits.maxViewportDimensions[0] = 16384;
		limits.maxViewportDimensions[1] = 16384;
		limits.timestampPeriod = 1.0f;

		properties.features = features;
	}

	TestRenderDevice::~TestRenderDevice()
	{
		for (TestGPUCommandBuffer* commandBuffer : commandBuffers) delete commandBuffer;
		for (TestGPUQueue* queue : queues) delete queue;
		for (TestGPUSwapchain* swapchain : swapchains) delete swapchain;
		for (TestGPUTopLevelAS* topLevelAS : topLevelStructures) delete topLevelAS;
		for (TestGPUBottomLevelAS* bottomLevelAS : bottomLevelStructures) delete bottomLevelAS;
		for (TestGPUQueryPool* queryPool : queryPools) delete queryPool;
		for (TestGPUFramebuffer* framebuffer : framebuffers) delete framebuffer;
		for (TestGPURenderPass* renderPass : renderPasses) delete renderPass;
		for (TestGPUDescriptorSet* descriptorSet : descriptorSets) delete descriptorSet;
		for (TestGPUPipeline* pipeline : pipelines) delete pipeline;
		for (TestGPUSampler* sampler : samplers) delete sampler;
		for (TestGPUTextureView* textureView : textureViews) delete textureView;
		for (TestGPUTexture* texture : textures) delete texture;
		for (TestGPUBuffer* buffer : buffers) delete buffer;
	}

	Span<GPUAdapter*> TestRenderDevice::GetAdapters()
	{
		return {};
	}

	bool TestRenderDevice::SelectAdapter(GPUAdapter* adapter)
	{
		return true;
	}

	const DeviceProperties& TestRenderDevice::GetProperties()
	{
		return properties;
	}

	const DeviceFeatures& TestRenderDevice::GetFeatures()
	{
		return features;
	}

	GraphicsAPI TestRenderDevice::GetAPI() const
	{
		return GraphicsAPI::None;
	}

	void TestRenderDevice::WaitIdle() {}

	GPUSwapchain* TestRenderDevice::CreateSwapchain(const SwapchainDesc& desc)
	{
		TestGPUSwapchain* swapchain = new TestGPUSwapchain();
		swapchain->device = this;
		swapchain->desc = desc;

		TextureDesc textureDesc;
		textureDesc.format = desc.desiredFormat;
		textureDesc.usage = ResourceUsage::RenderTarget | ResourceUsage::CopyDest;
		for (u32 i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			swapchain->textures.EmplaceBack(CreateTexture(textureDesc));
		}

		swapchains.EmplaceBack(swapchain);
		return swapchain;
	}

	GPURenderPass* TestRenderDevice::CreateRenderPass(const RenderPassDesc& desc)
	{
		TestGPURenderPass* renderPass = new TestGPURenderPass();
		renderPass->device = this;
		renderPass->desc = desc;
		renderPasses.EmplaceBack(renderPass);
		return renderPass;
	}

	GPUFramebuffer* TestRenderDevice::CreateFramebuffer(const FramebufferDesc& desc)
	{
		TestGPUFramebuffer* framebuffer = new TestGPUFramebuffer();
		framebuffer->device = this;
		framebuffer->desc = desc;
		if (!desc.attachments.Empty() && desc.attachments[0] != nullptr)
		{
			const TextureDesc& textureDesc = desc.attachments[0]->GetTexture()->GetDesc();
			framebuffer->extent = Extent{textureDesc.extent.width, textureDesc.extent.height};
		}
		framebuffers.EmplaceBack(framebuffer);
		return framebuffer;
	}

	GPUCommandBuffer* TestRenderDevice::CreateCommandBuffer(const QueueType& queueType)
	{
		TestGPUCommandBuffer* commandBuffer = new TestGPUCommandBuffer();
		commandBuffer->device = this;
		commandBuffer->queueType = queueType;
		commandBuffers.EmplaceBack(commandBuffer);
		return commandBuffer;
	}

	GPUBuffer* TestRenderDevice::CreateBuffer(const BufferDesc& desc)
	{
		TestGPUBuffer* buffer = new TestGPUBuffer();
		buffer->device = this;
		buffer->desc = desc;
		buffer->storage.Resize(desc.size, 0);
		buffers.EmplaceBack(buffer);
		return buffer;
	}

	GPUTexture* TestRenderDevice::CreateTexture(const TextureDesc& desc)
	{
		TestGPUTexture* texture = new TestGPUTexture();
		texture->device = this;
		texture->desc = desc;

		u32 mipLevels = texture->GetMipLevels();
		u32 arrayLayers = texture->GetArrayLayers();
		texture->subresourceStates.Resize(mipLevels * arrayLayers, ResourceState::Undefined);

		TextureViewDesc viewDesc;
		viewDesc.texture = texture;
		viewDesc.type = GetTextureViewType(desc.cubemap, desc.extent.depth, desc.extent.height, desc.arrayLayers);
		texture->textureView = CreateTextureView(viewDesc);

		textures.EmplaceBack(texture);
		return texture;
	}

	GPUTextureView* TestRenderDevice::CreateTextureView(const TextureViewDesc& desc)
	{
		TestGPUTextureView* textureView = new TestGPUTextureView();
		textureView->device = this;
		textureView->desc = desc;
		textureView->texture = static_cast<TestGPUTexture*>(desc.texture);
		textureViews.EmplaceBack(textureView);
		return textureView;
	}

	GPUSampler* TestRenderDevice::CreateSampler(const SamplerDesc& desc)
	{
		TestGPUSampler* sampler = new TestGPUSampler();
		sampler->device = this;
		sampler->desc = desc;
		samplers.EmplaceBack(sampler);
		return sampler;
	}

	GPUPipeline* TestRenderDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
	{
		TestGPUPipeline* pipeline = new TestGPUPipeline();
		pipeline->device = this;
		pipeline->bindPoint = PipelineBindPoint::Graphics;
		pipelines.EmplaceBack(pipeline);
		return pipeline;
	}

	GPUPipeline* TestRenderDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
	{
		TestGPUPipeline* pipeline = new TestGPUPipeline();
		pipeline->device = this;
		pipeline->bindPoint = PipelineBindPoint::Compute;
		pipelines.EmplaceBack(pipeline);
		return pipeline;
	}

	GPUPipeline* TestRenderDevice::CreateRayTracingPipeline(const RayTracingPipelineDesc& desc)
	{
		TestGPUPipeline* pipeline = new TestGPUPipeline();
		pipeline->device = this;
		pipeline->bindPoint = PipelineBindPoint::RayTracing;
		pipelines.EmplaceBack(pipeline);
		return pipeline;
	}

	GPUDescriptorSet* TestRenderDevice::CreateDescriptorSet(const DescriptorSetDesc& desc)
	{
		TestGPUDescriptorSet* descriptorSet = new TestGPUDescriptorSet();
		descriptorSet->device = this;
		descriptorSet->desc = desc;
		descriptorSets.EmplaceBack(descriptorSet);
		return descriptorSet;
	}

	GPUDescriptorSet* TestRenderDevice::CreateDescriptorSet(RID shader, StringView variant, u32 set)
	{
		TestGPUDescriptorSet* descriptorSet = new TestGPUDescriptorSet();
		descriptorSet->device = this;
		descriptorSets.EmplaceBack(descriptorSet);
		return descriptorSet;
	}

	GPUQueryPool* TestRenderDevice::CreateQueryPool(const QueryPoolDesc& desc)
	{
		TestGPUQueryPool* queryPool = new TestGPUQueryPool();
		queryPool->device = this;
		queryPool->desc = desc;
		queryPools.EmplaceBack(queryPool);
		return queryPool;
	}

	GPUBottomLevelAS* TestRenderDevice::CreateBottomLevelAS(const BottomLevelASDesc& desc)
	{
		TestGPUBottomLevelAS* bottomLevelAS = new TestGPUBottomLevelAS();
		bottomLevelAS->device = this;
		bottomLevelAS->desc = desc;
		bottomLevelStructures.EmplaceBack(bottomLevelAS);
		return bottomLevelAS;
	}

	GPUTopLevelAS* TestRenderDevice::CreateTopLevelAS(const TopLevelASDesc& desc)
	{
		TestGPUTopLevelAS* topLevelAS = new TestGPUTopLevelAS();
		topLevelAS->device = this;
		topLevelAS->desc = desc;
		topLevelAS->instanceCount = static_cast<u32>(desc.instances.Size());
		topLevelStructures.EmplaceBack(topLevelAS);
		return topLevelAS;
	}

	GPUQueue* TestRenderDevice::CreateQueue(const QueueDesc& desc)
	{
		TestGPUQueue* queue = new TestGPUQueue();
		queue->device = this;
		queue->desc = desc;
		queues.EmplaceBack(queue);
		return queue;
	}

	usize TestRenderDevice::GetBottomLevelASSize(const BottomLevelASDesc& desc)
	{
		return 1024;
	}

	usize TestRenderDevice::GetTopLevelASSize(const TopLevelASDesc& desc)
	{
		return 1024;
	}

	usize TestRenderDevice::GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc)
	{
		return 1024;
	}

	usize TestRenderDevice::GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc)
	{
		return 1024;
	}

	void TestRenderDevice::GetMemoryBudgets(Array<MemoryHeapBudget>& outBudgets)
	{
		outBudgets.Clear();
		MemoryHeapBudget& budget = outBudgets.EmplaceBack();
		budget.usage = 0;
		budget.budget = usize(1) << 30;
		budget.deviceLocal = true;
	}

	u32 TestRenderDevice::GetCreatedTextureCount() const
	{
		return static_cast<u32>(textures.Size());
	}

	u32 TestRenderDevice::GetCreatedBufferCount() const
	{
		return static_cast<u32>(buffers.Size());
	}
}
