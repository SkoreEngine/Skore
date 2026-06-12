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

		// Only part of the graph while there is something to draw; toggling rebuilds the graph.
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

			// The framework has already begun this pass's render pass and set a full-output viewport.
			Extent              extent = context->GetOutputSize();
			Span<Rml::Context*> contexts = RmlUiManager::GetContexts();

			for (usize i = 0; i < contexts.Size(); ++i)
			{
				renderInterface->BeginFrame(cmd, renderPass, extent);
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
