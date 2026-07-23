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
	constexpr const char* LightInstanceDataName = "LightInstanceData";
	constexpr const char* SceneCullingDataName = "SceneCullingData";
	constexpr const char* ShadowMapInstanceDataName = "ShadowMapInstanceData";
	constexpr const char* ShadowMapTextureName = "ShadowMap";
	constexpr const char* BloomTextureName = "BloomMipChain";
	constexpr const char* BloomFallbackName = "BloomFallback";

	constexpr u32 MaxCullingPipelines = 256;
	constexpr u32 MaxShadowCullSlots = 256;
	constexpr u32 MaxBloomMips = 7;

	inline u32 BloomMipCount(Extent outputSize)
	{
		u32 halfWidth = Math::Max(outputSize.width / 2, 1u);
		u32 halfHeight = Math::Max(outputSize.height / 2, 1u);
		u32 mipCount = static_cast<u32>(std::floor(std::log2(static_cast<f32>(Math::Min(halfWidth, halfHeight)))));
		return Math::Min(mipCount, MaxBloomMips);
	}

	struct SceneCullingData
	{
		GPUBuffer* indirectDrawBuffer = nullptr;
		GPUBuffer* countBuffer = nullptr;
		u32        pipelineCount = 0;
		u32        drawOffsets[MaxCullingPipelines] = {};
		u32        maxDraws[MaxCullingPipelines] = {};
	};

	struct ShadowMapInstanceData
	{
		u32         shadowMapSize = 2048;
		u32         numCascades = MaxNumCascades;
		bool        hasShadowLight = false;
		GPUSampler* shadowSampler = nullptr;
		f32         cascadeSplits[MaxNumCascades] = {};
		Mat4        cascadeViewProjMat[MaxNumCascades] = {};
	};

	struct LightFlags
	{
		enum
		{
			None                 = 0,
			HasAmbientTexture    = 1 << 1,
			HasAmbientColor      = 1 << 2,
			HasReflectionTexture = 1 << 3,
		};
	};

	struct LightData
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

	struct LightBuffer
	{
		u32 lightCount = 0;
		u32 shadowLightIndex = U32_MAX;
		u32 flags = LightFlags::None;
		f32 ambientMultiplier = 1.0f;

		Vec3 ambientLight{};
		f32  reflectionMultiplier = 1.0f;

		Vec4 cascadeSplits{};
		Mat4 cascadeViewProjMat[MaxNumCascades] = {};
	};

	struct LightInstanceData
	{
		GPUBuffer* lightBuffer = nullptr;
		u64        lightBufferAlignedSize = 0;

		Vec3 ambientLight{};
		f32  ambientMultiplier = 1.0f;
		f32  reflectionMultiplier = 1.0f;
		u32  flags = LightFlags::None;

		bool sceneBindingsReady = false;

		GPUTexture* cubeMapSkyTexture = nullptr;
		GPUTexture* diffuseIrradianceTexture = nullptr;
		GPUTexture* specularMapTexture = nullptr;
	};
}
