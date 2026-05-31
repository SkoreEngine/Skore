#pragma once

#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	constexpr u32 MaxLinearDepthMips = 12;

	struct DepthLinearizePass : RenderPipelinePass
	{
		SK_CLASS(DepthLinearizePass, RenderPipelinePass);

		struct SpdConstants
		{
			u32 mips;
			u32 numWorkGroupsPerSlice;
			u32 workGroupOffset[2];
			f32 nearClip;
			f32 farClip;
			u32 inputSize[2];
		};

		GPUPipeline*      pipeline = nullptr;
		GPUBuffer*        atomicBuffer = nullptr;
		GPUTexture*       linearDepthTexture = nullptr;
		GPUTextureView*   mipViews[MaxLinearDepthMips + 1] = {};
		GPUDescriptorSet* descriptorSet = nullptr;
		u32               mipCount = 0;

		RenderPipelinePassSetup GetPassSetup() override;
		void DestroyMipResources();
		void CreateMipResources();
		void Init() override;
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void OnResize(Extent size) override;
		void Destroy() override;
	};
}
