#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/IO/Input.hpp"

namespace Skore
{
	struct SwapchainRenderPipelinePass : RenderPipelinePass
	{
		SK_CLASS(SwapchainRenderPipelinePass, RenderPipelinePass);

		GPUSwapchain* swapchain = nullptr;

		void CreateSwapchain()
		{
			SwapchainDesc swapchainDesc;
			swapchainDesc.desiredFormat = TextureFormat::B8G8R8A8_UNORM;
			swapchainDesc.vsync = true;
			swapchainDesc.window = Graphics::GetWindow();
			swapchainDesc.debugName = "Swapchain";

			swapchain = Graphics::CreateSwapchain(swapchainDesc);

			context->SetOutputAttachments(OutputColorName, swapchain->GetTextures(), ResourceState::Present);
			context->SetOutputSize(Platform::GetWindowSize(swapchainDesc.window));
		}

		void DestroySwapchain() const
		{
			swapchain->Destroy();
		}

		void TryResizeSwapchain(bool force)
		{
			Extent extent = Platform::GetWindowSize(Graphics::GetWindow());

			if (extent.width == 0 || extent.height == 0)
			{
				context->DisableContext(true);
				return;
			}

			context->DisableContext(false);

			Extent outputSize = context->GetOutputSize();
			if (!force && (outputSize.width == extent.width && outputSize.height == extent.height))
			{
				return;
			}

			Graphics::WaitIdle();

			DestroySwapchain();
			CreateSwapchain();
		}

		void OnResizeEvent(VoidPtr userData)
		{
			TryResizeSwapchain(false);
		}

		void OnRestoredEvent(VoidPtr userData)
		{
			context->DisableContext(false);
			TryResizeSwapchain(true);
		}

		void OnMinimizedEvent(VoidPtr userData) const
		{
			context->DisableContext(true);
		}

		void Create() override
		{
			Event::Bind<OnWindowResized, &SwapchainRenderPipelinePass::OnResizeEvent>(this);
			Event::Bind<OnWindowMinimized, &SwapchainRenderPipelinePass::OnMinimizedEvent>(this);
			Event::Bind<OnWindowRestored, &SwapchainRenderPipelinePass::OnRestoredEvent>(this);

			RenderPipeline::SetMainContext(context);

			CreateSwapchain();
		}

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Other;
			setup.stage =  PipelineRenderStage::Swapchain;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::Read});
			return setup;
		}

		void Update() override
		{
			if (swapchain->AcquireNextImage() == DeviceResult::SwapchainOutOfDate)
			{
				TryResizeSwapchain(true);
			}
			context->SetCurrentOutputIndex(swapchain->GetCurrentImageIndex());
		}

		void Destroy() override
		{
			DestroySwapchain();
			Event::Unbind<OnWindowResized, &SwapchainRenderPipelinePass::OnResizeEvent>(this);
			Event::Unbind<OnWindowMinimized, &SwapchainRenderPipelinePass::OnMinimizedEvent>(this);
			Event::Unbind<OnWindowRestored, &SwapchainRenderPipelinePass::OnRestoredEvent>(this);
		}
	};

	struct SwapchainRenderPipelineModule : RenderPipelineModule
	{
		SK_CLASS(SwapchainRenderPipelineModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(SwapchainRenderPipelinePass));
			return setup;
		}
	};

	void RegisterSwapchainRenderModule()
	{
		Reflection::Type<SwapchainRenderPipelineModule>();
		Reflection::Type<SwapchainRenderPipelinePass>();
	}
}
