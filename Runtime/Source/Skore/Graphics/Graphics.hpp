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

#include "Device.hpp"
#include "Skore/Core/Event.hpp"


namespace Skore
{
	using OnRecordRenderCommands = EventType<"Skore::OnRecordRenderCommands"_h, void(GPUCommandBuffer* commandBuffer)>;

	struct SK_API Graphics
	{
		static Span<GPUAdapter*> GetAdapters();
		static GPUDevice*        GetDevice();
		static DeviceProperties  GetProperties();
		static GraphicsAPI       GetAPI();
		static void              WaitIdle();

		static GPUCommandBuffer* GetCurrentCommandBuffer();
		static GPUCommandBuffer* GetResourceCommandBuffer();

		static GPUSwapchain*     CreateSwapchain(const SwapchainDesc& desc);
		static GPURenderPass*    CreateRenderPass(const RenderPassDesc& desc);
		static GPUCommandBuffer* CreateCommandBuffer();
		static GPUBuffer*        CreateBuffer(const BufferDesc& desc);
		static GPUTexture*       CreateTexture(const TextureDesc& desc);
		static GPUTextureView*   CreateTextureView(const TextureViewDesc& desc);
		static GPUSampler*       CreateSampler(const SamplerDesc& desc);
		static GPUPipeline*      CreateGraphicsPipeline(const GraphicsPipelineDesc& desc);
		static GPUPipeline*      CreateComputePipeline(const ComputePipelineDesc& desc);
		static GPUPipeline*      CreateRayTracingPipeline(const RayTracingPipelineDesc& desc);
		static GPUDescriptorSet* CreateDescriptorSet(const DescriptorSetDesc& desc);
		static GPUDescriptorSet* CreateDescriptorSet(RID shader, StringView variant, u32 set);
		static GPUQueryPool*     CreateQueryPool(const QueryPoolDesc& desc);
		static GPUBottomLevelAS* CreateBottomLevelAS(const BottomLevelASDesc& desc);
		static GPUTopLevelAS*    CreateTopLevelAS(const TopLevelASDesc& desc);

		static void UploadBufferData(const BufferUploadInfo& bufferUploadInfo);
		static void UploadTextureData(const TextureDataInfo& textureDataInfo);
		static void SetTextureState(GPUTexture* texture, ResourceState oldState, ResourceState newState);

		static GPUSampler* GetLinearSampler();
		static GPUTexture* GetWhiteTexture();

		static usize GetBottomLevelASSize(const BottomLevelASDesc& desc);
		static usize GetTopLevelASSize(const TopLevelASDesc& desc);
		static usize GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc);
		static usize GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc);
	};
}
