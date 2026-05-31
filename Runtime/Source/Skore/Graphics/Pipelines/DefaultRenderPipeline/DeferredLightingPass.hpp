#pragma once

#include "PipelineCommon.hpp"

namespace Skore
{
	struct DeferredLightingPass : RenderPipelinePass
	{
		SK_CLASS(DeferredLightingPass, RenderPipelinePass);

		GPUDescriptorSet* gBufferDescriptorSets[SK_FRAMES_IN_FLIGHT] = {};
		GPUPipeline* pipeline = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;
		GPUTexture* lightAttachment = nullptr;

		RenderPipelinePassSetup GetPassSetup() override;
		void UpdateState();
		void Init() override;
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void OnResize(Extent size) override;
		void Destroy() override;
	};
}
