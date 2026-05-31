#pragma once

#include "Skore/Graphics/RenderTools.hpp"
#include "PipelineCommon.hpp"

namespace Skore
{
	struct ReflectionPass : RenderPipelinePass
	{
		SK_CLASS(ReflectionPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;
		GPUDescriptorSet* descriptorSet[SK_FRAMES_IN_FLIGHT] = {};
		LightPassInstanceData* lightInstanceData = nullptr;
		BRDFLUTTexture brdfLUT;

		RenderPipelinePassSetup GetPassSetup() override;
		void Init() override;
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void Destroy() override;
	};
}
