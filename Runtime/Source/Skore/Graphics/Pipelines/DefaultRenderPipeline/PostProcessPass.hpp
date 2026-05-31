#pragma once

#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	struct PostProcessPass : RenderPipelinePass
	{
		SK_CLASS(PostProcessPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		struct PostProcessPushConstants
		{
			f32 bloomIntensity;
		};

		RenderPipelinePassSetup GetPassSetup() override;
		void Init() override;
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void Destroy() override;
	};
}
