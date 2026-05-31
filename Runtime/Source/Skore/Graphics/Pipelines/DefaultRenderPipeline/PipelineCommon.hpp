#pragma once

#include "Skore/Common.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"

namespace Skore
{
	struct DefaultRenderPipeline;
	struct SwapchainRenderModule;
	class CascadeShadowPassBase;


	struct DepthPrePassModule;
	struct TemporalAntiAliasingModule;
	struct FXAARenderModule;
	struct BloomModule;
	struct IndirectLightingModule;

	struct CascadeShadowPass;
	struct LightSetupPass;
	struct CullingPass;
	struct DeferredGBufferPass;
	struct DeferredLightingPass;
	struct ForwardPass;
	struct DepthLinearizePass;
	struct CompositePass;
	struct CameraMotionVectorPass;
	struct PostProcessPass;

	constexpr const char* OutputColorName = "OutputColorAttachment";
	constexpr const char* OutputDepthName = "OutputDepthAttachment";
	constexpr const char* ShadowMapInstanceDataName = "ShadowMapInstanceData";
	constexpr const char* LinearDepthMipChainName = "LinearDepthMipChain";

	struct PipelineRenderStage
	{
		constexpr static i32 Culling = 100;
		constexpr static i32 ShadowPass = 200;
		constexpr static i32 DepthPrePass = 300;
		constexpr static i32 GBuffer = 400;
		constexpr static i32 DepthLinearize = 500;
		constexpr static i32 Lighting = 600;
		constexpr static i32 Forward = 700;
		constexpr static i32 Indirect = 800;
		constexpr static i32 Composite = 900;
		constexpr static i32 PostProcess = 1000;
		constexpr static i32 UI = 1100;
		constexpr static i32 Swapchain = 1200;
	};

	struct LightData
	{
		u32  type;
		Vec3 position;
		Vec4 direction;
		Vec4 color;
		f32  intensity;
		f32  range;
		f32  innerConeAngle;
		f32  outerConeAngle;
	};

	struct LightBuffer
	{
		u32       lightCount;
		u32       shadowLightIndex;
		Vec2		  pad0;
		Vec4      cascadeSplits;
		Mat4      cascadeViewProjMat[MaxNumCascades];
		LightData lights[MAX_LIGHTS];
	};

	struct LightFlags
	{
		enum
		{
			None                 = 0,
			HasAmbientTexture    = 1 << 1,
			HasAmbientColor      = 1 << 2,
			HasReflectionTexture = 1 << 3,
			HasSSAOTexture       = 1 << 4,
			SSREnabled           = 1 << 5
		};
	};

	struct LightPassInstanceData
	{
		GPUBuffer*        lightBuffer = nullptr;
		u64               lightBufferAlignedSize = 0;

		Vec3  ambientLight;
		float ambientMultiplier;
		float reflectionMultiplier;

		u32 indirectLightFlags = LightFlags::None;

		GPUTexture* cubeMapSkyTexture = nullptr;
		GPUTexture* diffuseIrradianceTexture = nullptr;
		GPUTexture* specularMapTexture = nullptr;

		static void RegisterType(NativeReflectType<LightPassInstanceData>& type);
	};

	struct ScenePipelineCullingData
	{
		GPUBuffer* indirectDrawBuffer[SK_FRAMES_IN_FLIGHT];
		u32 countBufferOffset = 0;
	};

	struct SceneCullingData
	{
		Array<ScenePipelineCullingData> pipelines;
		GPUBuffer* countBuffer[SK_FRAMES_IN_FLIGHT];
	};
}
