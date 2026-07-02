#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"

namespace Skore
{
	class GPUBuffer;
	class GPUSampler;
	class GPUTexture;

	constexpr const char* ForwardColorName = "ForwardColor";
	constexpr const char* ForwardDepthName = "ForwardDepth";
	constexpr const char* PostProcessOutputName = "OutputColor";
	constexpr const char* LightInstanceDataNewName = "LightInstanceData";
	constexpr const char* SceneCullingDataNewName = "SceneCullingData";
	constexpr const char* ShadowMapInstanceDataNewName = "ShadowMapInstanceData";
	constexpr const char* ShadowMapTextureName = "ShadowMap";
	constexpr const char* BloomTextureNewName = "BloomMipChain";
	constexpr const char* BloomFallbackName = "BloomFallback";

	constexpr u32 MaxCullingPipelinesNew = 256;
	constexpr u32 MaxShadowCullSlotsNew = 256;
	constexpr u32 MaxBloomMipsNew = 7;

	//shared by BloomPassNew (chain creation) and PostProcessPassNew (bloom input selection):
	//both must agree on whether the output is large enough for a bloom chain
	inline u32 BloomMipCountNew(Extent outputSize)
	{
		u32 halfWidth = Math::Max(outputSize.width / 2, 1u);
		u32 halfHeight = Math::Max(outputSize.height / 2, 1u);
		u32 mipCount = static_cast<u32>(std::floor(std::log2(static_cast<f32>(Math::Min(halfWidth, halfHeight)))));
		return Math::Min(mipCount, MaxBloomMipsNew);
	}

	//filled by CullingPassNew (offsets at build time, buffers at record time once they exist);
	//consumed by ForwardPassNew's opaque DrawIndexedIndirectCount loop
	struct SceneCullingDataNew
	{
		GPUBuffer* indirectDrawBuffer = nullptr;
		GPUBuffer* countBuffer = nullptr;
		u32        pipelineCount = 0;
		u32        drawOffsets[MaxCullingPipelinesNew] = {};
		u32        maxDraws[MaxCullingPipelinesNew] = {};
	};

	//filled by CascadeShadowPassNew at build time; LightSetupPassNew copies the cascade data into
	//the light UBO and binds the shadow map + comparison sampler into the scene set (bindings 3/4)
	struct ShadowMapInstanceDataNew
	{
		u32         shadowMapSize = 2048;
		u32         numCascades = MaxNumCascades;
		bool        hasShadowLight = false;
		GPUSampler* shadowSampler = nullptr;
		f32         cascadeSplits[MaxNumCascades] = {};
		Mat4        cascadeViewProjMat[MaxNumCascades] = {};
	};

	struct LightFlagsNew
	{
		enum
		{
			None                 = 0,
			HasAmbientTexture    = 1 << 1,
			HasAmbientColor      = 1 << 2,
			HasReflectionTexture = 1 << 3,
		};
	};

	//GPU layouts mirrored in Assets/ShadersNew/LightsNew.hlsli (LightBuffer UBO at b2, lights buffer at t10, space1).
	struct LightDataNew
	{
		u32  type = 0;
		Vec3 position{};
		Vec4 direction{};
		Vec4 color{};
		f32  intensity = 0.0f;
		f32  range = 0.0f;
		f32  innerConeAngle = 0.0f;
		f32  outerConeAngle = 0.0f;
	};

	struct LightBufferNew
	{
		u32 lightCount = 0;
		u32 shadowLightIndex = U32_MAX;
		u32 flags = LightFlagsNew::None;
		f32 ambientMultiplier = 1.0f;

		Vec3 ambientLight{};
		f32  reflectionMultiplier = 1.0f;

		Vec4 cascadeSplits{};
		Mat4 cascadeViewProjMat[MaxNumCascades] = {};
	};

	struct LightInstanceDataNew
	{
		GPUBuffer* lightBuffer = nullptr;
		u64        lightBufferAlignedSize = 0;

		Vec3 ambientLight{};
		f32  ambientMultiplier = 1.0f;
		f32  reflectionMultiplier = 1.0f;
		u32  flags = LightFlagsNew::None;

		//true once LightSetupPassNew has written all scene-set bindings this frame;
		//drawing with the forward shader before that would consume invalid descriptors
		bool sceneBindingsReady = false;

		GPUTexture* cubeMapSkyTexture = nullptr;
		GPUTexture* diffuseIrradianceTexture = nullptr;
		GPUTexture* specularMapTexture = nullptr;
	};
}
