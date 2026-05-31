#pragma once

#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Graphics/Pipelines/Modules/CascadeShadowMapModule.hpp"
#include "PipelineCommon.hpp"

namespace Skore
{
	struct LightSetupPass : RenderPipelinePass
	{
		SK_CLASS(LightSetupPass, RenderPipelinePass);

		ShadowMapInstanceData* shadowMapData = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;

		RID skyMaterialInUse = {};

		RenderPipelinePassSetup GetPassSetup() override;
		void Init() override;
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void Destroy() override;
	};
}
