#pragma once

#include "PipelineCommon.hpp"

namespace Skore
{
	struct CompositePass : RenderPipelinePass
	{
		SK_CLASS(CompositePass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;

		RenderPipelinePassSetup GetPassSetup() override;
		void Init() override;
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void Destroy() override;
	};
}
