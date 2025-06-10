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

#include "Graphics.hpp"

#include <SDL3/SDL.h>

#include "Devices/VulkanDevice.hpp"
#include "Skore/App.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/Event.hpp"

namespace Skore
{
	namespace
	{
		GPUDevice*                                         device = nullptr;
		SDL_Window*                                        window;
		GPUSwapchain*                                      swapchain;
		EventHandler<OnUIRender>                           onUIRender{};
		EventHandler<OnRecordRenderCommands>               onRecordRenderCommands{};
		u32                                                currentFrame = 0;
		FixedArray<GPUCommandBuffer*, SK_FRAMES_IN_FLIGHT> commandBuffers;
		bool                                               windowMinimized = false;
		GPUCommandBuffer*                                  resourceCommandBuffer = nullptr;

		GPUSampler* linearSampler = nullptr;
		GPUTexture* whiteTexture = nullptr;

		void ShowGPUErrorMessage()
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
			                         "No suitable graphics found",
			                         "We couldn't detect a compatible GPU on your device. This application requires hardware acceleration to run properly.",
			                         window);
		}

		void ShowGraphicsStartError()
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
			                         "Graphics Initialization Error",
			                         "The application could not initialize the graphics system.\n"
			                         "This may be due to missing or incompatible graphics hardware.\n\n"
			                         "Please ensure your device meets the minimum system requirements \nand that your graphics drivers are up to date.",
			                         window);
		}

		void ShowSubmitError()
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
			                         "Frame Submission Error",
			                         "Failed to submit the rendering frame to the graphics device.\n\n"
			                         "This could be caused by a graphics driver issue, insufficient video memory,\n"
			                         "or a problem in the application.\n\n"
			                         "If the problem persists, please submit a bug report with details about your\n"
			                         "system configuration and the steps that led to this error.",
			                         window);
		}
	}

	GPUDevice* InitVulkan(const DeviceInitDesc& initDesc);

	SK_API SDL_Window* GraphicsGetWindow()
	{
		return window;
	}

	SK_API GPUSwapchain* GraphicsGetSwapchain()
	{
		return swapchain;
	}

	bool GraphicsInit(const AppConfig& appConfig)
	{
		DeviceInitDesc desc;
		desc.enableDebugLayers = true;

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

		Extent winSize = {appConfig.width, appConfig.height};

		//TODO improve window creation.
		SDL_WindowFlags flags = 0;
		flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
		f32 scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

		winSize.width *= scale;
		winSize.height *= scale;

		if (appConfig.fullscreen)
		{
			flags |= SDL_WINDOW_FULLSCREEN;
		}
		else if (appConfig.maximized)
		{
			flags |= SDL_WINDOW_MAXIMIZED;
		}

		switch (Graphics::GetAPI())
		{
			case GraphicsAPI::Vulkan:
				flags |= SDL_WINDOW_VULKAN;
				break;
			case GraphicsAPI::D3D12:
				break;
			case GraphicsAPI::Metal:
				flags |= SDL_WINDOW_METAL;
				break;
			case GraphicsAPI::None:
				break;
		}


		window = SDL_CreateWindow(appConfig.title.CStr(), winSize.width, winSize.height, flags);

		SwapchainDesc swapchainDesc;
		swapchainDesc.format = TextureFormat::B8G8R8A8_UNORM;
		swapchainDesc.vsync = true;
		swapchainDesc.windowHandle = window;
		swapchainDesc.debugName = "Swapchain";

		swapchain = Graphics::CreateSwapchain(swapchainDesc);
		if (!swapchain)
		{
			ShowGraphicsStartError();
			return false;
		}

		for (int i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			commandBuffers[i] = device->CreateCommandBuffer();
		}

		linearSampler = device->CreateSampler(SamplerDesc{
		});

		resourceCommandBuffer = device->CreateCommandBuffer();

		whiteTexture = device->CreateTexture(TextureDesc{
			.debugName = "DefaultWhiteTexture"
		});

		u8 whiteData[4] = {255, 255, 255, 255};

		Graphics::UploadTextureData(TextureDataInfo{
			.texture = whiteTexture,
			.data = whiteData,
			.size = 4
		});

		return true;
	}

	bool GraphicsUpdate()
	{
		if (windowMinimized) return true;

		if (!swapchain->AcquireNextImage(currentFrame))
		{
			return false;
		}

		GPUCommandBuffer* cmd = commandBuffers[currentFrame];
		cmd->Begin();

		onRecordRenderCommands.Invoke(cmd);

		cmd->BeginRenderPass(swapchain->GetCurrentRenderPass(), {0, 0, 0, 1}, 1, 0);

		//render here.

		onUIRender.Invoke();
		cmd->EndRenderPass();

		cmd->End();

		if (!device->SubmitAndPresent(swapchain, cmd, currentFrame))
		{
			ShowSubmitError();
			return false;
		}

		currentFrame = (currentFrame + 1) % SK_FRAMES_IN_FLIGHT;
		return true;
	}

	bool GraphicsHandleEvents(SDL_Event* event)
	{
		switch (event->type)
		{
			case SDL_EVENT_WINDOW_RESIZED:
			{
				if (!swapchain->Resize())
				{
					ShowSubmitError();
					return false;
				}
				break;
			}
			case SDL_EVENT_WINDOW_MAXIMIZED:
			{
				break;
			}
			case SDL_EVENT_WINDOW_MINIMIZED:
			{
				windowMinimized = true;
				break;
			}
			case SDL_EVENT_WINDOW_RESTORED:
			{
				windowMinimized = false;
				break;
			}
		}
		return true;
	}

	void GraphicsShutdown()
	{
		linearSampler->Destroy();
		resourceCommandBuffer->Destroy();
		whiteTexture->Destroy();

		for (u32 i = 0; i < SK_FRAMES_IN_FLIGHT; ++i)
		{
			commandBuffers[i]->Destroy();
		}

		if (swapchain)
		{
			swapchain->Destroy();
		}

		DestroyAndFree(device);

		if (window)
		{
			SDL_DestroyWindow(window);
		}
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
		if (device)
		{
			device->WaitIdle();
		}
	}

	GPUCommandBuffer* Graphics::GetCurrentCommandBuffer()
	{
		return commandBuffers[currentFrame];
	}

	GPUCommandBuffer* Graphics::GetResourceCommandBuffer()
	{
		return resourceCommandBuffer;
	}

	GPUSwapchain* Graphics::CreateSwapchain(const SwapchainDesc& desc)
	{
		return device->CreateSwapchain(desc);
	}

	GPURenderPass* Graphics::CreateRenderPass(const RenderPassDesc& desc)
	{
		return device->CreateRenderPass(desc);
	}

	GPUCommandBuffer* Graphics::CreateCommandBuffer()
	{
		return device->CreateCommandBuffer();
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


		resourceCommandBuffer->Begin();
		resourceCommandBuffer->CopyBuffer(tempBuffer, bufferUploadInfo.buffer, bufferUploadInfo.size, 0, bufferUploadInfo.dstOffset);
		resourceCommandBuffer->End();

		resourceCommandBuffer->SubmitAndWait();

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

		resourceCommandBuffer->Begin();

		if (textureDataInfo.regions.Empty())
		{
			resourceCommandBuffer->ResourceBarrier(textureDataInfo.texture, ResourceState::Undefined, ResourceState::CopyDest, 0, 0);
			resourceCommandBuffer->CopyBufferToTexture(tempBuffer, textureDataInfo.texture, textureDataInfo.texture->GetDesc().extent, 0, 0, 0);
			resourceCommandBuffer->ResourceBarrier(textureDataInfo.texture, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, 0);
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
						resourceCommandBuffer->ResourceBarrier(textureDataInfo.texture, ResourceState::Undefined, ResourceState::CopyDest, region.mipLevel, region.arrayLayer);

						resourceCommandBuffer->CopyBufferToTexture(
							tempBuffer,
							textureDataInfo.texture,
							region.extent,
							region.mipLevel + level,
							region.arrayLayer + layer,
							region.dataOffset
						);

						resourceCommandBuffer->ResourceBarrier(textureDataInfo.texture, ResourceState::CopyDest, ResourceState::ShaderReadOnly, region.mipLevel, region.arrayLayer);
					}
				}
			}
		}

		resourceCommandBuffer->End();
		resourceCommandBuffer->SubmitAndWait();

		tempBuffer->Destroy();
	}

	void Graphics::SetTextureState(GPUTexture* texture, ResourceState oldState, ResourceState newState)
	{
		GPUCommandBuffer* cmd = CreateCommandBuffer();

		const TextureDesc& desc = texture->GetDesc();

		cmd->Begin();
		cmd->ResourceBarrier(texture, oldState, newState, 0, desc.mipLevels, 0, desc.arrayLayers);
		cmd->End();
		cmd->SubmitAndWait();

		cmd->Destroy();
	}

	GPUSampler* Graphics::GetLinearSampler()
	{
		return linearSampler;
	}

	GPUTexture* Graphics::GetWhiteTexture()
	{
		return whiteTexture;
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
}
