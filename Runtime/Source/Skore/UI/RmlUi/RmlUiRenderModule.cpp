#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Scene/Scene.hpp"

#include "RmlUiManager.hpp"
#include "RenderInterfaceSkore.hpp"
#include "UIDocument.hpp"

#include <RmlUi/Core/Context.h>

#include "Skore/Graphics/Graphics.hpp"

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
			Scene* scene = context->GetScene();
			return RmlUiManager::GetRenderInterface() != nullptr && scene != nullptr && scene->HasIterable<UIDocument>();
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			RenderInterfaceSkore* renderInterface = RmlUiManager::GetRenderInterface();
			if (!renderInterface || !scene)
			{
				return;
			}
			const Extent size = context->GetOutputSize();

			scene->Iterate<UIDocument>([&](UIDocument* document)
			{
				Rml::Context* rmlContext = document->GetContext();
				if (!rmlContext)
				{
					return;
				}

				rmlContext->SetDimensions(Rml::Vector2i(static_cast<int>(size.width), static_cast<int>(size.height)));
				rmlContext->SetDensityIndependentPixelRatio(Platform::GetWindowDPI(Graphics::GetWindow()));
				rmlContext->Update();
				renderInterface->BeginFrame(cmd, renderPass, size);
				rmlContext->Render();
				renderInterface->EndFrame();
			});
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
