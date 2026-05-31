#pragma once

#include "PipelineCommon.hpp"

namespace Skore
{
	struct CullingPass : RenderPipelinePass
	{
		SK_CLASS(CullingPass, RenderPipelinePass);
		Scene* cachedPipelineOwner = nullptr;

		GPUPipeline* pipeline{};
		GPUDescriptorSet* descriptorSet[SK_FRAMES_IN_FLIGHT]{};

		RenderPipelinePassSetup GetPassSetup() override;
		void CreateDescriptorSet();
		void Init() override;
		void PrepareBuffers(Scene* scene, SceneCullingData* data);
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
	};
}
