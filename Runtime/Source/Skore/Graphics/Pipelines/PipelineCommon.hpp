#pragma once

#include "Skore/Common.hpp"

namespace Skore
{
	struct DefaultRenderPipeline;
	struct SwapchainRenderModule;
	class CascadeShadowPassBase;


	struct DepthPrePassModule;
	struct TemporalAntiAliasingModule;
	struct FXAARenderModule;
	struct BloomModule;

	constexpr const char* OutputColorName = "OutputColorAttachment";
	constexpr const char* OutputDepthName = "OutputDepthAttachment";
	constexpr const char* ShadowMapInstanceDataName = "ShadowMapInstanceData";
	constexpr const char* LinearDepthMipChainName = "LinearDepthMipChain";

	struct DefaultPipelineRenderStage
	{
		constexpr static i32 ShadowPass = 100;
		constexpr static i32 DepthPrePass = 200;
		constexpr static i32 GBuffer = 300;
		constexpr static i32 DepthLinearize = 400;
		constexpr static i32 Lighting = 500;
		constexpr static i32 Forward = 600;
		constexpr static i32 Indirect = 700;
		constexpr static i32 Composite = 800;
		constexpr static i32 PostProcess = 900;
		constexpr static i32 UI = 1000;
		constexpr static i32 Swapchain = 1100;
	};
}