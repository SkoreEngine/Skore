#pragma once

#include "PipelineCommon.hpp"

namespace Skore
{
	struct DeferredGBufferPass : RenderPipelinePass
	{
		SK_CLASS(DeferredGBufferPass, RenderPipelinePass);

		Array<GPUPipeline*> opaquePipelines = {};
		Scene*              cachedPipelineOwner = nullptr;

		struct MeshPushConstants
		{
			Mat4 world;
			u32  materialIndex;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  pad;
		};

		RenderPipelinePassSetup GetPassSetup() override;
		void CleanupPipelines();
		void Init() override;
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void Destroy() override;
	};
}
