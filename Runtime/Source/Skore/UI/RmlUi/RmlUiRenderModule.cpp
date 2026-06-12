#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"

#include "RmlUiManager.hpp"
#include "RenderInterfaceSkore.hpp"

#include <RmlUi/Core/Context.h>

namespace Skore
{
	struct RmlUiRenderPass : RenderPipelinePass
	{
		SK_CLASS(RmlUiRenderPass, RenderPipelinePass);

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = PipelineRenderStage::UI;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::ReadWrite});
			return setup;
		}

		bool IsEnabled() override
		{
			return RmlUiManager::GetRenderInterface() != nullptr && RmlUiManager::GetContexts().Size() > 0;
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			RenderInterfaceSkore* renderInterface = RmlUiManager::GetRenderInterface();
			if (!renderInterface)
			{
				return;
			}

			const Extent logicalSize = context->GetOutputSize();
			Extent       physicalSize = logicalSize;
			if (GPUTexture* colorOutput = context->GetColorOutput())
			{
				const Extent3D& texExtent = colorOutput->GetDesc().extent;
				physicalSize = {texExtent.width, texExtent.height};
			}

			const f32 densityRatio = logicalSize.width > 0 ? static_cast<f32>(physicalSize.width) / static_cast<f32>(logicalSize.width) : 1.0f;

			Span<Rml::Context*> contexts = RmlUiManager::GetContexts();
			for (usize i = 0; i < contexts.Size(); ++i)
			{
				contexts[i]->SetDimensions(Rml::Vector2i(static_cast<int>(physicalSize.width), static_cast<int>(physicalSize.height)));
				contexts[i]->SetDensityIndependentPixelRatio(densityRatio);
				contexts[i]->Update();
				renderInterface->BeginFrame(cmd, renderPass, physicalSize);
				contexts[i]->Render();
				renderInterface->EndFrame();
			}
		}
	};

	struct RmlUiRenderPipelineModule : RenderPipelineModule
	{
		SK_CLASS(RmlUiRenderPipelineModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(RmlUiRenderPass));
			return setup;
		}
	};

	void RegisterRmlUiRenderModule()
	{
		Reflection::Type<RmlUiRenderPass>();
		Reflection::Type<RmlUiRenderPipelineModule>();
	}
}
