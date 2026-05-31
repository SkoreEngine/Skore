#pragma once

#include "PipelineCommon.hpp"

namespace Skore
{
	struct ForwardPass : RenderPipelinePass
	{
		SK_CLASS(ForwardPass, RenderPipelinePass);

		GPUPipeline* skyboxMaterialPipeline = nullptr;
		GPUPipeline* particlePipeline = nullptr;
		Array<GPUPipeline*> transparencyPipelines;
		Scene*              cachedPipelineOwner = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;

		struct MeshPushConstants
		{
			Mat4 world;
			u32  materialIndex;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  pad;
		};

		RenderPipelinePassSetup GetPassSetup() override;
		void Init() override;
		void CleanupPipelines();
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void Destroy() override;
	};
}
