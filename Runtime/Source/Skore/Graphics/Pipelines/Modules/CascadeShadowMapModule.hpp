#pragma once

#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	class RenderSceneObjects;

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
		void Render(Scene* scene, GPUCommandBuffer* cmd) override;
		void Destroy() override;

		// Tests if a world-space AABB can cast a shadow into the given cascade.
		// Skips the near plane: casters between the light and the cascade slice
		// are outside the orthographic near plane but their shadows still land in it.
		// Retained for any subclass that wants to do CPU-side fallback culling.
		static bool IsAABBVisibleInCascade(const Frustum& cascadeFrustum, const AABB& aabb);

	protected:
		void PrepareShadowCullBuffers(RenderSceneObjects* objects, u32 frame, u32 numShadowPipes, u32 totalSlots);

		ShadowMapInstanceData* shadowMapData = nullptr;

		Array<Vec4>    m_cascadeOffsets;
		Array<Vec4>    m_cascadeScales;
		Array<Frustum> m_cascadeCullingFrustums;
		// Cached per-cascade culling planes (5 each: L, R, B, T, Far — near is skipped).
		// Persists across frames so skipped cascades keep their last-rendered planes.
		Array<FixedArray<Vec4, 5>> m_cascadeCullingPlanes;

		// Staggered cascade updates: cascade N renders every (1 << N) frames,
		// or sooner if the cascade's frustum/light has moved enough to invalidate
		// the cached shadow map.
		Array<Vec3> m_lastFrustumCenter;
		Array<Vec3> m_lastLightDir;
		u64         m_frameCounter = 0;
		u32         m_maxUpdatePeriod = 8;
		f32         m_centerMoveThreshold = 0.1f;     // fraction of cascade radius
		f32         m_lightRotationDotThreshold = 0.999f; // ≈2.5°

		Scene* cachedPipelineObjects = nullptr;
		Array<GPUPipeline*> shadowMapPipelines;
		GPUBuffer*          shadowMapUniformBuffer = nullptr;
		u64                 m_uniformBufferAlignedSize = 0;

		Array<GPUTextureView*>   m_shadowMapTextureViews;
		Array<GPURenderPass*>    m_shadowMapRenderPass;
		Array<GPUFramebuffer*>   m_shadowMapFramebuffers;
		Array<GPUDescriptorSet*> m_shadowMapDescriptorSets[SK_FRAMES_IN_FLIGHT];

		// GPU-driven shadow culling. One compute dispatch produces a per-(cascade,shadowPipeline)
		// indirect-draw buffer + count buffer, then DrawIndexedIndirectCount renders each slot.
		GPUPipeline*      m_shadowCullPipeline = nullptr;
		GPUDescriptorSet* m_shadowCullDescriptorSet[SK_FRAMES_IN_FLIGHT] = {};
		GPUBuffer*        m_shadowCullDataBuffer = nullptr; // per-cascade frustum planes + pipeline count, ring of SK_FRAMES_IN_FLIGHT
		u64               m_shadowCullDataAlignedSize = 0;

		// indirectDrawBuffers[frame][cascade * shadowPipelineCount + shadowPipelineIndex]
		Array<GPUBuffer*> m_shadowIndirectDrawBuffers[SK_FRAMES_IN_FLIGHT];
		GPUBuffer*        m_shadowIndirectCountBuffer[SK_FRAMES_IN_FLIGHT] = {};
		u32               m_shadowIndirectPipelineCount = 0;
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
