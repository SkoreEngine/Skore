#include "Skore/Graphics/Graphics.hpp"

#include <future>
#include <SDL3/SDL.h>

#include "Skore/Graphics/DrawList.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/App.hpp"
#include "Skore/OpenXRManager.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Platform/Platform.hpp"

namespace Skore
{
	void RenderToolsInit();
	void RenderToolsShutdown();
	void RenderResourceCacheInit();
	void RenderResourceCacheShutdown();
	void RenderSceneObjectsInit();
	void RenderSceneObjectsShutdown();

	GPUSwapchain* OpenXRManagerCreateSwapchain();

	namespace
	{
		GPUDevice* device = nullptr;


		Logger& logger = Logger::GetLogger("Skore::Graphics");

		GPUSampler* linearSampler = nullptr;
		GPUSampler* linearClampToEdgeSampler = nullptr;
		GPUSampler* nearestClampToEdgeSampler = nullptr;
		GPUTexture* whiteTexture = nullptr;
		GPUTexture* whiteCubemapTexture = nullptr;

		Array<GPUCommandBuffer*> freeCommandBuffers;
		std::mutex               freeCommandBuffersMutex{};
		std::thread::id          thisId;

		Window window;

		void ShowGPUErrorMessage()
		{
			Platform::ShowSimpleMessageBox(MessageBoxType::Error,
			                              "No suitable graphics found",
			                              "We couldn't detect a compatible GPU on your device. This application requires hardware acceleration to run properly.",
			                              window);
		}

		void ShowGraphicsStartError()
		{
			Platform::ShowSimpleMessageBox(MessageBoxType::Error,
			                              "Graphics Initialization Error",
			                              "The application could not initialize the graphics system.\n"
			                              "This may be due to missing or incompatible graphics hardware.\n\n"
			                              "Please ensure your device meets the minimum system requirements \nand that your graphics drivers are up to date.",
			                              window);
		}
	}

	GPUDevice* InitVulkan(const DeviceInitDesc& initDesc);

	bool GraphicsInit(const AppConfig& appConfig)
	{
		thisId = std::this_thread::get_id();

		DeviceInitDesc desc;
		desc.enableDebugLayers = false;

		RID settings = Settings::Get(TypeInfo<ProjectSettings>::ID(), sktypeid(GraphicsSettings));
		if (ResourceObject settingsObject = Resources::Read(settings))
		{
			desc.enableDebugLayers = settingsObject.GetBool(GraphicsSettings::EnableValidationLayers);
		}

		device = InitVulkan(desc);

		if (!device)
		{
			ShowGPUErrorMessage();
			return false;
		}

		Array adapters = device->GetAdapters();

		if (adapters.Empty())
		{
			ShowGPUErrorMessage();
			return false;
		}

		Sort(adapters.begin(), adapters.end(), [](GPUAdapter* left, GPUAdapter* right)
		{
			return left->GetScore() > right->GetScore();
		});

		if (!device->SelectAdapter(adapters[0]))
		{
			ShowGraphicsStartError();
			return false;
		}

		WindowFlags flags = WindowFlags::None;

		if (appConfig.fullscreen)
			flags |= WindowFlags::Fullscreen;

		if (appConfig.maximized)
			flags |= WindowFlags::Maximized;

		window = Platform::CreateWindow(appConfig.title, appConfig.width, appConfig.height, flags);

		linearSampler = device->CreateSampler(SamplerDesc{
		});

		linearClampToEdgeSampler = Graphics::CreateSampler(SamplerDesc{
			.addressModeU = AddressMode::ClampToEdge,
			.addressModeV = AddressMode::ClampToEdge,
			.addressModeW = AddressMode::ClampToEdge,
		});

		nearestClampToEdgeSampler = Graphics::CreateSampler(SamplerDesc{
			.minFilter = FilterMode::Nearest,
			.magFilter = FilterMode::Nearest,
			.mipmapFilter = FilterMode::Nearest,
			.addressModeU = AddressMode::ClampToEdge,
			.addressModeV = AddressMode::ClampToEdge,
			.addressModeW = AddressMode::ClampToEdge,
		});

		for (u32 i = 0; i < 3; ++i)
		{
			freeCommandBuffers.EmplaceBack(device->CreateCommandBuffer(QueueType::Graphics));
		}

		whiteTexture = device->CreateTexture(TextureDesc{
			.debugName = "DefaultWhiteTexture"
		});

		u8 whiteData[4] = {255, 255, 255, 255};

		Graphics::UploadTextureData(TextureDataInfo{
			.texture = whiteTexture,
			.data = whiteData,
			.size = 4
		});

		whiteCubemapTexture = device->CreateTexture(TextureDesc{
			.arrayLayers = 6,
			.cubemap = true,
			.debugName = "DefaultWhiteCubemapTexture",
		});

		{
			Array<TextureDataRegion> regions{};
			for (u32 i = 0; i < 6; ++i)
			{
				TextureDataRegion region;
				region.extent = Extent{1, 1};
				region.arrayLayer = i;
				regions.EmplaceBack(region);
			}

			TextureDataInfo info;
			info.texture = whiteCubemapTexture;
			info.data = whiteData;
			info.size = 4;
			info.regions = regions;
			Graphics::UploadTextureData(info);
		}

		RenderResourceCacheInit();
		RenderSceneObjectsInit();
		RenderToolsInit();

		return true;
	}


	void GraphicsShutdown()
	{
		RenderToolsShutdown();
		RenderResourceCacheShutdown();
		RenderSceneObjectsShutdown();

		linearSampler->Destroy();
		linearClampToEdgeSampler->Destroy();
		nearestClampToEdgeSampler->Destroy();
		whiteTexture->Destroy();
		whiteCubemapTexture->Destroy();

		for (auto& cmd : freeCommandBuffers)
		{
			cmd->Destroy();
		}

		DestroyAndFree(device);

		Platform::DestroyWindow(window);
	}

	Span<GPUAdapter*> Graphics::GetAdapters()
	{
		return device->GetAdapters();
	}

	GPUDevice* Graphics::GetDevice()
	{
		return device;
	}

	DeviceProperties Graphics::GetProperties()
	{
		return device->GetProperties();
	}

	GraphicsAPI Graphics::GetAPI()
	{
		return device->GetAPI();
	}

	void Graphics::WaitIdle()
	{
		if (device && thisId == std::this_thread::get_id())
		{
			device->WaitIdle();
		}
	}

	void Graphics::GetMemoryBudgets(Array<MemoryHeapBudget>& outBudgets)
	{
		if (device)
		{
			device->GetMemoryBudgets(outBudgets);
		}
		else
		{
			outBudgets.Clear();
		}
	}

	Window Graphics::GetWindow()
	{
		return window;
	}

	GPUCommandBuffer* Graphics::GetFreeCommandBuffer()
	{
		std::unique_lock lock(freeCommandBuffersMutex);

		if (!freeCommandBuffers.Empty())
		{
			GPUCommandBuffer* cmd = freeCommandBuffers.Back();
			freeCommandBuffers.PopBack();
			return cmd;
		}

		return device->CreateCommandBuffer(QueueType::Graphics);
	}

	void Graphics::AddFreeCommandBuffer(GPUCommandBuffer* cmd)
	{
		std::unique_lock lock(freeCommandBuffersMutex);
		freeCommandBuffers.EmplaceBack(cmd);
	}

	GPUSwapchain* Graphics::CreateSwapchain(const SwapchainDesc& desc)
	{
		return device->CreateSwapchain(desc);
	}

	GPURenderPass* Graphics::CreateRenderPass(const RenderPassDesc& desc)
	{
		return device->CreateRenderPass(desc);
	}

	GPUFramebuffer* Graphics::CreateFramebuffer(const FramebufferDesc& desc)
	{
		return device->CreateFramebuffer(desc);
	}

	GPUCommandBuffer* Graphics::CreateCommandBuffer(const QueueType& queueType)
	{
		return device->CreateCommandBuffer(queueType);
	}

	GPUBuffer* Graphics::CreateBuffer(const BufferDesc& desc)
	{
		return device->CreateBuffer(desc);
	}

	GPUTexture* Graphics::CreateTexture(const TextureDesc& desc)
	{
		return device->CreateTexture(desc);
	}

	GPUTextureView* Graphics::CreateTextureView(const TextureViewDesc& desc)
	{
		return device->CreateTextureView(desc);
	}

	GPUSampler* Graphics::CreateSampler(const SamplerDesc& desc)
	{
		return device->CreateSampler(desc);
	}

	GPUPipeline* Graphics::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
	{
		return device->CreateGraphicsPipeline(desc);
	}

	GPUPipeline* Graphics::CreateComputePipeline(const ComputePipelineDesc& desc)
	{
		return device->CreateComputePipeline(desc);
	}

	GPUPipeline* Graphics::CreateRayTracingPipeline(const RayTracingPipelineDesc& desc)
	{
		return device->CreateRayTracingPipeline(desc);
	}

	GPUDescriptorSet* Graphics::CreateDescriptorSet(const DescriptorSetDesc& desc)
	{
		return device->CreateDescriptorSet(desc);
	}

	GPUDescriptorSet* Graphics::CreateDescriptorSet(RID shader, StringView variant, u32 set)
	{
		return device->CreateDescriptorSet(shader, variant, set);
	}

	GPUQueryPool* Graphics::CreateQueryPool(const QueryPoolDesc& desc)
	{
		return device->CreateQueryPool(desc);
	}

	GPUBottomLevelAS* Graphics::CreateBottomLevelAS(const BottomLevelASDesc& desc)
	{
		return device->CreateBottomLevelAS(desc);
	}

	GPUTopLevelAS* Graphics::CreateTopLevelAS(const TopLevelASDesc& desc)
	{
		return device->CreateTopLevelAS(desc);
	}

	GPUQueue* Graphics::CreateQueue(const QueueDesc& desc)
	{
		return device->CreateQueue(desc);
	}

	void Graphics::UploadBufferData(const BufferUploadInfo& bufferUploadInfo)
	{
		GPUBuffer* tempBuffer = CreateBuffer(BufferDesc{
			.size = bufferUploadInfo.size,
			.usage = ResourceUsage::CopySource,
			.persistentMapped = true
		});

		VoidPtr mem = tempBuffer->Map();
		memcpy(mem, static_cast<const char*>(bufferUploadInfo.data) + bufferUploadInfo.srcOffset, bufferUploadInfo.size);
		tempBuffer->Unmap();

		GPUCommandBuffer* cmd = GetFreeCommandBuffer();
		cmd->Begin();
		cmd->CopyBuffer(tempBuffer, bufferUploadInfo.buffer, bufferUploadInfo.size, 0, bufferUploadInfo.dstOffset);
		cmd->End();
		SubmitGPUWork(cmd, true);
		AddFreeCommandBuffer(cmd);

		tempBuffer->Destroy();
	}

	void Graphics::UploadTextureData(const TextureDataInfo& textureDataInfo)
	{
		if (!textureDataInfo.texture || !textureDataInfo.data || textureDataInfo.size == 0)
		{
			return;
		}

		GPUBuffer* tempBuffer = CreateBuffer(BufferDesc{
			.size = textureDataInfo.size,
			.usage = ResourceUsage::CopySource,
			.hostVisible = true,
			.persistentMapped = true
		});

		VoidPtr mem = tempBuffer->GetMappedData();
		memcpy(mem, textureDataInfo.data, textureDataInfo.size);

		GPUCommandBuffer* cmd = GetFreeCommandBuffer();
		cmd->Begin();

		if (textureDataInfo.regions.Empty())
		{
			cmd->ResourceBarrier(textureDataInfo.texture, ResourceState::Undefined, ResourceState::CopyDest, 0, 0);
			cmd->CopyBufferToTexture({
				.buffer = tempBuffer,
				.texture = textureDataInfo.texture,
				.extent = textureDataInfo.texture->GetDesc().extent,
			});
			cmd->ResourceBarrier(textureDataInfo.texture, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, 0);
		}
		else
		{
			// Upload specific regions
			for (const TextureDataRegion& region : textureDataInfo.regions)
			{
				for (u32 layer = 0; layer < region.layerCount; ++layer)
				{
					for (u32 level = 0; level < region.levelCount; ++level)
					{
						cmd->ResourceBarrier(textureDataInfo.texture, ResourceState::Undefined, ResourceState::CopyDest, region.mipLevel, region.arrayLayer);
						cmd->CopyBufferToTexture({
							.buffer = tempBuffer,
							.texture = textureDataInfo.texture,
							.extent = region.extent,
							.mipLevel = region.mipLevel + level,
							.arrayLayer = region.arrayLayer + layer,
							.bufferOffset = region.dataOffset,
						});
						cmd->ResourceBarrier(textureDataInfo.texture, ResourceState::CopyDest, ResourceState::ShaderReadOnly, region.mipLevel, region.arrayLayer);
					}
				}
			}
		}

		cmd->End();

		SubmitGPUWork(cmd, true);
		AddFreeCommandBuffer(cmd);

		tempBuffer->Destroy();
	}

	void Graphics::SetTextureState(GPUTexture* texture, ResourceState oldState, ResourceState newState)
	{
		GPUCommandBuffer* cmd = GetFreeCommandBuffer();

		const TextureDesc& desc = texture->GetDesc();

		cmd->Begin();
		cmd->ResourceBarrier(texture, oldState, newState, 0, desc.mipLevels, 0, desc.arrayLayers);
		cmd->End();
		SubmitGPUWork(cmd, true);
		AddFreeCommandBuffer(cmd);
	}

	GPUSampler* Graphics::GetLinearSampler()
	{
		return linearSampler;
	}

	GPUSampler* Graphics::GetLinearClampToEdgeSampler()
	{
		return linearClampToEdgeSampler;
	}

	GPUSampler* Graphics::GetNearestClampToEdgeSampler()
	{
		return nearestClampToEdgeSampler;
	}

	GPUTexture* Graphics::GetWhiteTexture()
	{
		return whiteTexture;
	}

	GPUTexture* Graphics::GetWhiteCubemapTexture()
	{
		return whiteCubemapTexture;
	}

	usize Graphics::GetBottomLevelASSize(const BottomLevelASDesc& desc)
	{
		return device->GetBottomLevelASSize(desc);
	}

	usize Graphics::GetTopLevelASSize(const TopLevelASDesc& desc)
	{
		return device->GetTopLevelASSize(desc);
	}

	usize Graphics::GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc)
	{
		return device->GetAccelerationStructureBuildScratchSize(desc);
	}

	usize Graphics::GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc)
	{
		return device->GetAccelerationStructureBuildScratchSize(desc);
	}

	void Graphics::ShowSubmitError()
	{
		Platform::ShowSimpleMessageBox(MessageBoxType::Error,
		                              "Frame Submission Error",
		                              "Failed to submit the rendering frame to the graphics device.\n\n"
		                              "This could be caused by a graphics driver issue, insufficient video memory,\n"
		                              "or a problem in the application.\n\n"
		                              "If the problem persists, please submit a bug report with details about your\n"
		                              "system configuration and the steps that led to this error.",
		                              window);
	}

	bool Graphics::SubmitGPUWork(GPUCommandBuffer* cmd, bool blocking)
	{
		if (blocking)
		{
			if (thisId == std::this_thread::get_id())
			{
				device->SubmitGPUWork(cmd, {}, true);
				return true;
			}

			auto              promPtr = std::make_shared<std::promise<bool>>();
			std::future<bool> fut = promPtr->get_future();

			auto callback = [promPtr](bool success)
			{
				promPtr->set_value(success);
			};

			device->SubmitGPUWork(cmd, callback, false);

			return fut.get();
		}

		device->SubmitGPUWork(cmd, {}, false);
		return true;
	}

	void Graphics::RegisterType(NativeReflectType<Graphics>& type)
	{
		type.Function<&Graphics::GetAdapters>("GetAdapters");
		type.Function<&Graphics::GetDevice>("GetDevice");
		type.Function<&Graphics::GetProperties>("GetProperties");
		type.Function<&Graphics::GetAPI>("GetAPI");
		type.Function<&Graphics::WaitIdle>("WaitIdle");
		type.Function<&Graphics::GetWindow>("GetWindow");
		type.Function<&Graphics::GetFreeCommandBuffer>("GetFreeCommandBuffer");
		type.Function<&Graphics::AddFreeCommandBuffer>("AddFreeCommandBuffer", "cmd");
		type.Function<&Graphics::CreateSwapchain>("CreateSwapchain", "desc");
		type.Function<&Graphics::CreateRenderPass>("CreateRenderPass", "desc");
		type.Function<&Graphics::CreateFramebuffer>("CreateFramebuffer", "desc");
		type.Function<&Graphics::CreateCommandBuffer>("CreateCommandBuffer", "queueType");
		type.Function<&Graphics::CreateBuffer>("CreateBuffer", "desc");
		type.Function<&Graphics::CreateTexture>("CreateTexture", "desc");
		type.Function<&Graphics::CreateTextureView>("CreateTextureView", "desc");
		type.Function<&Graphics::CreateSampler>("CreateSampler", "desc");
		type.Function<&Graphics::CreateGraphicsPipeline>("CreateGraphicsPipeline", "desc");
		type.Function<&Graphics::CreateComputePipeline>("CreateComputePipeline", "desc");
		type.Function<&Graphics::CreateRayTracingPipeline>("CreateRayTracingPipeline", "desc");
		type.Function<static_cast<GPUDescriptorSet*(*)(const DescriptorSetDesc&)>(&Graphics::CreateDescriptorSet)>("CreateDescriptorSet", "desc");
		type.Function<static_cast<GPUDescriptorSet*(*)(RID, StringView, u32)>(&Graphics::CreateDescriptorSet)>("CreateDescriptorSetFromShader", "shader", "variant", "set");
		type.Function<&Graphics::CreateQueryPool>("CreateQueryPool", "desc");
		type.Function<&Graphics::CreateBottomLevelAS>("CreateBottomLevelAS", "desc");
		type.Function<&Graphics::CreateTopLevelAS>("CreateTopLevelAS", "desc");
		type.Function<&Graphics::UploadBufferData>("UploadBufferData", "bufferUploadInfo");
		type.Function<&Graphics::UploadTextureData>("UploadTextureData", "textureDataInfo");
		type.Function<&Graphics::SetTextureState>("SetTextureState", "texture", "oldState", "newState");
		type.Function<&Graphics::GetLinearSampler>("GetLinearSampler");
		type.Function<&Graphics::GetWhiteTexture>("GetWhiteTexture");
		type.Function<&Graphics::GetWhiteCubemapTexture>("GetWhiteCubemapTexture");
		type.Function<static_cast<usize(*)(const BottomLevelASDesc&)>(&Graphics::GetBottomLevelASSize)>("GetBottomLevelASSize", "desc");
		type.Function<&Graphics::GetTopLevelASSize>("GetTopLevelASSize", "desc");
		type.Function<static_cast<usize(*)(const BottomLevelASDesc&)>(&Graphics::GetAccelerationStructureBuildScratchSize)>("GetAccelerationStructureBuildScratchSizeForBottomLevel", "desc");
		type.Function<static_cast<usize(*)(const TopLevelASDesc&)>(&Graphics::GetAccelerationStructureBuildScratchSize)>("GetAccelerationStructureBuildScratchSizeForTopLevel", "desc");
		type.Function<&Graphics::ShowSubmitError>("ShowSubmitError");
		type.Function<&Graphics::SubmitGPUWork>("SubmitGPUWork", "cmd", "blocking");
	}
}