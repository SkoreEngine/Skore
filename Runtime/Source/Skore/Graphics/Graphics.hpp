#pragma once

#include "Device.hpp"
#include "Skore/Core/Event.hpp"


namespace Skore
{
	using OnWindowResized = EventType<"Skore::OnWindowResized"_h, void(VoidPtr windowHandle)>;
	using OnWindowMinimized = EventType<"Skore::OnWindowMinimized"_h, void(VoidPtr windowHandle)>;
	using OnWindowRestored = EventType<"Skore::OnWindowRestored"_h, void(VoidPtr windowHandle)>;

	using OnRecordRenderCommands = EventType<"Skore::OnRecordRenderCommands"_h, void(GPUCommandBuffer* commandBuffer)>;

	enum class GpuWorkType
	{
		Graphics,
		Compute,
		Transfer
	};

	struct SK_API Graphics
	{
		static Span<GPUAdapter*> GetAdapters();
		static GPUDevice*        GetDevice();
		static DeviceProperties  GetProperties();
		static GraphicsAPI       GetAPI();
		static void              WaitIdle();
		static Window            GetWindow();

		static GPUCommandBuffer* GetFreeCommandBuffer();
		static void              AddFreeCommandBuffer(GPUCommandBuffer* cmd);

		static GPUSwapchain*     CreateSwapchain(const SwapchainDesc& desc);
		static GPURenderPass*    CreateRenderPass(const RenderPassDesc& desc);
		static GPUFramebuffer*   CreateFramebuffer(const FramebufferDesc& desc);
		static GPUCommandBuffer* CreateCommandBuffer(const QueueType& queueType);
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
		static GPUQueue*         CreateQueue(const QueueDesc& desc);


		static void UploadBufferData(const BufferUploadInfo& bufferUploadInfo);
		static void UploadTextureData(const TextureDataInfo& textureDataInfo);
		static void SetTextureState(GPUTexture* texture, ResourceState oldState, ResourceState newState);

		static GPUSampler* GetLinearSampler();
		static GPUSampler* GetLinearClampToEdgeSampler();
		static GPUSampler* GetNearestClampToEdgeSampler();
		static GPUTexture* GetWhiteTexture();
		static GPUTexture* GetWhiteCubemapTexture();

		static usize GetBottomLevelASSize(const BottomLevelASDesc& desc);
		static usize GetTopLevelASSize(const TopLevelASDesc& desc);
		static usize GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc);
		static usize GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc);

		static void ShowSubmitError();

		static bool SubmitGPUWork(GPUCommandBuffer* cmd, bool blocking);

		static void GetMemoryBudgets(Array<MemoryHeapBudget>& outBudgets);

		static void RegisterType(NativeReflectType<Graphics>& type);
	};
}
