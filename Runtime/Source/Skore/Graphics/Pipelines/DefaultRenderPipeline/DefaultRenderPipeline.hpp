#pragma once

#include "Skore/Graphics/Pipelines/Modules/CascadeShadowMapModule.hpp"

namespace Skore
{
	struct DeferredRenderModule : RenderPipelineModule
	{
		SK_CLASS(DeferredRenderModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override;
		Array<RenderPipelineResource> GetResources() override;
	};

	struct CascadeShadowMapModule : CascadeShadowMapModuleBase
	{
		SK_CLASS(CascadeShadowMapModule, CascadeShadowMapModuleBase);

		TypeID GetCascadeShadowPassTypeId() override;
	};

	struct DefaultRenderPipeline : RenderPipeline
	{
		SK_CLASS(DefaultRenderPipeline, RenderPipeline);

		RenderPipelineSetup GetPipelineSetup() override;
	};
}
