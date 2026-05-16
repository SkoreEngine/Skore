#pragma once

#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	struct ShadowMapInstanceData
	{
		u32 shadowMapSize = 2048;
		u32 numCascades = 4;

		GPUTexture* shadowTexture = nullptr;
		GPUSampler* shadowSampler = nullptr;
		Array<f32>  cascadeSplits;
		Array<Mat4> cascadeViewProjMat;

		static void RegisterType(NativeReflectType<ShadowMapInstanceData>& type);
	};

	class CascadeShadowPassBase : public RenderPipelinePass
	{
	public:
		SK_CLASS(CascadeShadowPassBase, RenderPipelinePass);

		RenderPipelinePassSetup GetPassSetup() override;

		void Init() override;
		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override;
		void Destroy() override;

		virtual void RenderCascade(RenderSceneObjects* objects, GPUCommandBuffer* cmd, const Mat4& viewProj, u32 cascadeIndex) = 0;

	protected:
		ShadowMapInstanceData* shadowMapData = nullptr;

		f32 m_lambda = 0.85f;

		Array<Vec4> m_cascadeOffsets;
		Array<Vec4> m_cascadeScales;

		RenderSceneObjects* cachedPipelineObjects = nullptr;
		Array<GPUPipeline*> shadowMapPipelines;
		GPUBuffer*          shadowMapUniformBuffer = nullptr;
		u64                 m_uniformBufferAlignedSize = 0;

		Array<GPUTextureView*>   m_shadowMapTextureViews;
		Array<GPURenderPass*>    m_shadowMapRenderPass;
		Array<GPUFramebuffer*>   m_shadowMapFramebuffers;
		Array<GPUDescriptorSet*> m_shadowMapDescriptorSets[SK_FRAMES_IN_FLIGHT];
	};

	class CascadeShadowMapModuleBase : public RenderPipelineModule
	{
	public:
		SK_CLASS(CascadeShadowMapModuleBase, RenderPipelineModule);

		void Init() override;
		void Destroy() override;

		Array<RenderPipelineResource> GetResources() override;
		RenderPipelineModuleSetup     GetSetup() override;

		virtual TypeID GetCascadeShadowPassTypeId() = 0;
	private:
		ShadowMapInstanceData* shadowMapData = nullptr;
	};
}
