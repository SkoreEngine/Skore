#pragma once

#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	struct CameraMotionVectorPass : RenderPipelinePass
	{
		SK_CLASS(CameraMotionVectorPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		RenderPipelinePassSetup GetPassSetup() override;
		void Init() override;
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void Destroy() override;
	};
}
