#include "doctest.h"

#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Core/Math.hpp"
#include "Graphics/TestRenderDevice.hpp"

using namespace Skore;

namespace
{
	bool HasUsage(ResourceUsage usage, ResourceUsage flag)
	{
		return (usage & flag) == flag;
	}

	TEST_CASE("RenderGraph::InferTextureUsage")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);

		rg.Create("GBufferAlbedo", RenderGraphTextureDesc{.format = Format::RGBA8_UNORM});
		rg.Create("Depth", RenderGraphTextureDesc{.format = Format::D32_FLOAT});
		rg.Create("Light", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});
		rg.Create("Color", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT, .usage = ResourceUsage::CopyDest});

		rg.AddGraphicsPass("GBuffer")
			.Write("GBufferAlbedo")
			.Write("Depth");

		rg.AddComputePass("Lighting", "Shaders/DeferredLighting")
			.Read("GBufferAlbedo")
			.Read("Depth")
			.Write("Light");

		rg.AddComputePass("Composite", "Shaders/Composite")
			.Read("Light")
			.Write("Color");

		ResourceUsage albedo = rg.InferTextureUsage("GBufferAlbedo");
		CHECK(HasUsage(albedo, ResourceUsage::RenderTarget));
		CHECK(HasUsage(albedo, ResourceUsage::ShaderResource));
		CHECK(!HasUsage(albedo, ResourceUsage::UnorderedAccess));

		ResourceUsage depth = rg.InferTextureUsage("Depth");
		CHECK(HasUsage(depth, ResourceUsage::DepthStencil));
		CHECK(HasUsage(depth, ResourceUsage::ShaderResource));
		CHECK(!HasUsage(depth, ResourceUsage::RenderTarget));

		ResourceUsage light = rg.InferTextureUsage("Light");
		CHECK(HasUsage(light, ResourceUsage::UnorderedAccess));
		CHECK(HasUsage(light, ResourceUsage::ShaderResource));
		CHECK(!HasUsage(light, ResourceUsage::RenderTarget));

		ResourceUsage color = rg.InferTextureUsage("Color");
		CHECK(HasUsage(color, ResourceUsage::UnorderedAccess));
		CHECK(HasUsage(color, ResourceUsage::CopyDest));
	}

	TEST_CASE("RenderGraph::WriteReadInferredUsage")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);

		rg.Create("Accum", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});

		rg.AddComputePass("Build", "Shaders/Build").Write("Accum");
		rg.AddGraphicsPass("Forward").WriteRead("Accum");
		rg.AddComputePass("Resolve", "Shaders/Resolve").Read("Accum");

		ResourceUsage accum = rg.InferTextureUsage("Accum");
		CHECK(HasUsage(accum, ResourceUsage::UnorderedAccess));
		CHECK(HasUsage(accum, ResourceUsage::RenderTarget));
		CHECK(HasUsage(accum, ResourceUsage::ShaderResource));
	}

	TEST_CASE("RenderGraph::InstanceBlackboard")
	{
		struct CullingData
		{
			u32        drawCount;
			GPUBuffer* indirectBuffer;
		};

		TestRenderDevice device;
		RenderGraph      rg(&device);

		CullingData* data = rg.CreateInstance<CullingData>("SceneCullingData");
		REQUIRE(data != nullptr);
		data->drawCount = 128;
		data->indirectBuffer = nullptr;

		CullingData* fetched = rg.GetInstanceData<CullingData>("SceneCullingData");
		CHECK(fetched == data);
		CHECK(fetched->drawCount == 128);

		CHECK(rg.CreateInstance<CullingData>("SceneCullingData") == data);
		CHECK(rg.GetInstanceData<CullingData>("Missing") == nullptr);
	}

	TEST_CASE("RenderGraph::FrameRing")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);

		CHECK(rg.GetCurrentFrame() == 0);

		rg.Begin(nullptr);
		CHECK(rg.GetCurrentFrame() == 1);
		CHECK(rg.GetPrevFrame() == 0);

		rg.Begin(nullptr);
		CHECK(rg.GetCurrentFrame() == 0);
		CHECK(rg.GetPrevFrame() == 1);
	}

	TEST_CASE("RenderGraph::BuilderChainingAndAccessors")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);

		rg.SetOutputSize(Extent{1920, 1080});
		CHECK(rg.GetOutputSize().width == 1920);
		CHECK(rg.GetOutputSize().height == 1080);

		rg.Create("Color", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});

		RenderGraphPass& pass = rg.AddComputePass("Pass", "Shaders/Pass");
		CHECK(&pass.Read("Color") == &pass);
		CHECK(&pass.Dispatch(1, 1, 1) == &pass);

		CHECK(rg.GetTexture("Color") == nullptr);
		CHECK(rg.GetTexture("DoesNotExist") == nullptr);
	}

	TEST_CASE("RenderGraph::PipelineCacheBuildsOnce")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);

		int          builds = 0;
		GPUPipeline* sentinel = reinterpret_cast<GPUPipeline*>(0x1234);

		GPUPipeline* a = rg.GetOrCreatePipeline("gbuffer/opaque", [&] { ++builds; return sentinel; });
		GPUPipeline* b = rg.GetOrCreatePipeline("gbuffer/opaque", [&] { ++builds; return sentinel; });
		GPUPipeline* c = rg.GetOrCreatePipeline("gbuffer/masked", [&] { ++builds; return sentinel; });

		CHECK(a == sentinel);
		CHECK(b == sentinel);
		CHECK(c == sentinel);
		CHECK(builds == 2);
	}

	TEST_CASE("RenderGraph::ExecuteAllocatesAndTransitions")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);
		rg.SetOutputSize(Extent{256, 256});

		rg.Create("Light", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});

		int dispatches = 0;

		rg.AddComputePass("Lighting", "Shaders/Lighting")
			.Write("Light")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer* cmd) { ++dispatches; cmd->Dispatch(32, 32, 1); });

		rg.AddComputePass("Composite", "Shaders/Composite")
			.Read("Light")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer* cmd) { ++dispatches; cmd->Dispatch(32, 32, 1); });

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		rg.Execute(cmd);
		cmd->End();

		CHECK(device.GetCreatedTextureCount() == 1);
		CHECK(dispatches == 2);

		GPUTexture* light = rg.GetTexture("Light");
		REQUIRE(light != nullptr);
		CHECK(HasUsage(light->GetDesc().usage, ResourceUsage::UnorderedAccess));
		CHECK(HasUsage(light->GetDesc().usage, ResourceUsage::ShaderResource));
		CHECK(!HasUsage(light->GetDesc().usage, ResourceUsage::RenderTarget));

		TestGPUTexture* testLight = static_cast<TestGPUTexture*>(light);
		CHECK(testLight->GetSubresourceState(0, 0) == ResourceState::ShaderReadOnly);
		CHECK(testLight->barrierHistory.Size() == 2);
		CHECK(testLight->mismatchCount == 0);
	}

	TEST_CASE("RenderGraph::DeferredPipelineExample")
	{
		struct LightInstanceData
		{
			GPUBuffer* lightBuffer;
			u32        lightCount;
		};

		struct CompositeConstants
		{
			Vec2 outputSize;
			f32  exposure;
			f32  pad;
		};

		TestRenderDevice device;
		RenderGraph      rg(&device);
		rg.SetOutputSize(Extent{1280, 720});

		rg.CreateInstance<LightInstanceData>("LightInstanceData");

		rg.Create("GBufferAlbedo", RenderGraphTextureDesc{.format = Format::RGBA8_UNORM});
		rg.Create("GBufferNormals", RenderGraphTextureDesc{.format = Format::RG16_FLOAT});
		rg.Create("Depth", RenderGraphTextureDesc{.format = Format::D32_FLOAT});
		rg.Create("Light", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});
		rg.Create("Color", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT, .pingPong = true});

		rg.SetColorOutput("Color");
		rg.SetDepthOutput("Depth");

		rg.AddPass("LightSetup")
			.Write("LightInstanceData")
			.Render([](RenderGraph& rg, Scene* scene, GPUCommandBuffer* cmd)
			{
				LightInstanceData* lights = rg.GetInstanceData<LightInstanceData>("LightInstanceData");
				lights->lightCount = 0;
			});

		rg.AddGraphicsPass("GBuffer")
			.InvertViewport()
			.Write("GBufferAlbedo")
			.Write("GBufferNormals")
			.Write("Depth")
			.Render([](RenderGraph& rg, Scene* scene, GPUCommandBuffer* cmd) {});

		rg.AddComputePass("DeferredLighting", "Shaders/DeferredLighting")
			.Read("LightInstanceData")
			.Read("GBufferAlbedo")
			.Read("GBufferNormals")
			.Read("Depth")
			.Write("Light")
			.Dispatch((1280 + 7) / 8, (720 + 7) / 8, 1);

		rg.AddComputePass("Composite", "Shaders/Composite")
			.Read("Light")
			.Write("Color")
			.Constants<CompositeConstants>([](RenderGraph& rg, CompositeConstants& c)
			{
				c.outputSize = Vec2(1280.0f, 720.0f);
				c.exposure = 1.0f;
			})
			.Dispatch((1280 + 7) / 8, (720 + 7) / 8, 1);

		CHECK(HasUsage(rg.InferTextureUsage("GBufferNormals"), ResourceUsage::RenderTarget));
		CHECK(HasUsage(rg.InferTextureUsage("GBufferNormals"), ResourceUsage::ShaderResource));
		CHECK(HasUsage(rg.InferTextureUsage("Light"), ResourceUsage::UnorderedAccess));
		CHECK(rg.GetInstanceData<LightInstanceData>("LightInstanceData") != nullptr);
	}
}
