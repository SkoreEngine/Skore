#pragma once

#ifdef SK_GPU_TESTS

#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

// Shared support for real (non-mocked) Vulkan tests that render offscreen, without a window or
// swapchain. Enabled only when the SK_ENABLE_GPU_TESTS CMake option is on (defines SK_GPU_TESTS).
//
// The heavy, process-wide state (SDL video, engine reflection types, the DXC shader compiler and a
// headless graphics device) is created once on first use via Graphics::InitHeadless. Tests then use
// the regular Graphics:: API (CreateTexture, CreateGraphicsPipeline, SubmitGPUWork, ...). Mutable
// resource-database state is scoped per test through ResourceScope, matching the setup/teardown
// discipline the rest of the suite uses so test cases stay isolated regardless of execution order.
namespace Skore::GpuTest
{
	// True when a graphics device could be created. GPU test cases should early-out (or skip) when
	// this is false so the suite still passes on machines without a usable GPU.
	bool IsAvailable();

	struct ResourceScope
	{
		ResourceScope();
		~ResourceScope();

		ResourceScope(const ResourceScope&) = delete;
		ResourceScope& operator=(const ResourceScope&) = delete;
	};

	// Compiles a vertex + pixel HLSL file into a ShaderResource RID usable with
	// CreateGraphicsPipeline. Must be called inside an active ResourceScope. Returns an invalid RID
	// on failure.
	RID CompileGraphicsShader(StringView absolutePath, StringView vsEntry = "MainVS", StringView psEntry = "MainPS");
}

#endif
