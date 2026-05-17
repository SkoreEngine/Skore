#pragma once

#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	struct DepthPrePassPass : RenderPipelinePass
	{
		SK_CLASS(DepthPrePassPass, RenderPipelinePass);

		RenderPipelinePassSetup GetPassSetup() override;

		void Init() override;
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void Destroy() override;

	private:
		Array<GPUPipeline*>  depthPipelines;
		Scene*               cachedPipelineOwner = nullptr;

		void CleanupPipelines();
	};

	struct DepthPrePassModule : RenderPipelineModule
	{
		SK_CLASS(DepthPrePassModule, RenderPipelineModule);

		RenderPipelineModuleSetup     GetSetup() override;
		Array<RenderPipelineResource> GetResources() override;
	};
}
