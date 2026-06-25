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

	TEST_CASE("RenderGraph::ExecuteTransitionsBuffers")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);

		rg.Create("LightList", RenderGraphBufferDesc{.size = 256, .hostVisible = false});

		rg.AddComputePass("BuildLightList", "Shaders/BuildLightList")
			.Write("LightList")
			.Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});

		rg.AddComputePass("Shade", "Shaders/Shade")
			.Read("LightList")
			.Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		rg.Execute(cmd);
		cmd->End();

		GPUBuffer* lightList = rg.GetBuffer("LightList");
		REQUIRE(lightList != nullptr);
		CHECK(HasUsage(lightList->GetDesc().usage, ResourceUsage::UnorderedAccess));
		CHECK(HasUsage(lightList->GetDesc().usage, ResourceUsage::ShaderResource));

		TestGPUBuffer* testLightList = static_cast<TestGPUBuffer*>(lightList);
		CHECK(testLightList->state == ResourceState::ShaderReadOnly);

		TestGPUCommandBuffer* testCmd = static_cast<TestGPUCommandBuffer*>(cmd);
		CHECK(testCmd->stats.bufferBarrierCount == 2);
		CHECK(testCmd->stats.textureBarrierCount == 0);
	}

	TEST_CASE("RenderGraph::ExecuteBarriersSameStateWriteHazards")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);
		rg.SetOutputSize(Extent{64, 64});

		rg.Create("Accum", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});
		rg.Create("Counter", RenderGraphBufferDesc{.size = 16, .hostVisible = false});

		rg.AddComputePass("Clear", "Shaders/Clear")
			.Write("Accum")
			.Write("Counter")
			.Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});

		rg.AddComputePass("Accumulate", "Shaders/Accumulate")
			.Write("Accum")
			.Write("Counter")
			.Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		rg.Execute(cmd);
		cmd->End();

		TestGPUTexture* accum = static_cast<TestGPUTexture*>(rg.GetTexture("Accum"));
		REQUIRE(accum != nullptr);
		CHECK(accum->GetSubresourceState(0, 0) == ResourceState::General);
		CHECK(accum->barrierHistory.Size() == 2);
		CHECK(accum->barrierHistory[1].oldState == ResourceState::General);
		CHECK(accum->barrierHistory[1].newState == ResourceState::General);
		CHECK(accum->mismatchCount == 0);

		TestGPUBuffer* counter = static_cast<TestGPUBuffer*>(rg.GetBuffer("Counter"));
		REQUIRE(counter != nullptr);
		CHECK(counter->state == ResourceState::General);

		TestGPUCommandBuffer* testCmd = static_cast<TestGPUCommandBuffer*>(cmd);
		CHECK(testCmd->stats.bufferBarrierCount == 2);
	}

	TEST_CASE("RenderGraph::ExecuteTransitionsAllTextureSubresources")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);
		rg.SetOutputSize(Extent{64, 64});

		rg.Create("MipChain", RenderGraphTextureDesc{
			.format = Format::RGBA16_FLOAT,
			.arrayLayers = 2,
			.mipLevels = 3
		});

		rg.AddComputePass("BuildMipChain", "Shaders/BuildMipChain")
			.Write("MipChain")
			.Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});

		rg.AddComputePass("SampleMipChain", "Shaders/SampleMipChain")
			.Read("MipChain")
			.Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		rg.Execute(cmd);
		cmd->End();

		TestGPUTexture* mipChain = static_cast<TestGPUTexture*>(rg.GetTexture("MipChain"));
		REQUIRE(mipChain != nullptr);
		CHECK(mipChain->AllSubresourcesInState(ResourceState::ShaderReadOnly));
		REQUIRE(mipChain->barrierHistory.Size() == 2);
		CHECK(mipChain->barrierHistory[0].levelCount == 3);
		CHECK(mipChain->barrierHistory[0].layerCount == 2);
		CHECK(mipChain->barrierHistory[1].levelCount == 3);
		CHECK(mipChain->barrierHistory[1].layerCount == 2);
		CHECK(mipChain->mismatchCount == 0);
	}

	TEST_CASE("RenderGraph::ExecuteTransitionsTextureViewSubresources")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);
		rg.SetOutputSize(Extent{64, 64});

		rg.Create("MipChain", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT, .mipLevels = 3});
		rg.CreateView("Mip1", RenderGraphViewDesc{
			.texture = "MipChain",
			.baseMipLevel = 1,
			.mipLevelCount = 1,
			.baseArrayLayer = 0,
			.arrayLayerCount = 1
		});

		rg.AddComputePass("BuildMip1", "Shaders/BuildMip1")
			.Write("Mip1")
			.Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});

		CHECK(HasUsage(rg.InferTextureUsage("MipChain"), ResourceUsage::UnorderedAccess));

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		rg.Execute(cmd);
		cmd->End();

		TestGPUTexture* mipChain = static_cast<TestGPUTexture*>(rg.GetTexture("MipChain"));
		REQUIRE(mipChain != nullptr);
		CHECK(mipChain->GetSubresourceState(0, 0) == ResourceState::Undefined);
		CHECK(mipChain->GetSubresourceState(1, 0) == ResourceState::General);
		CHECK(mipChain->GetSubresourceState(2, 0) == ResourceState::Undefined);
		REQUIRE(mipChain->barrierHistory.Size() == 1);
		CHECK(mipChain->barrierHistory[0].baseMipLevel == 1);
		CHECK(mipChain->barrierHistory[0].levelCount == 1);
		CHECK(mipChain->mismatchCount == 0);
	}

	TEST_CASE("RenderGraph::ExecuteSortsTextureViewAliasBeforeParentRead")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);
		rg.SetOutputSize(Extent{64, 64});

		rg.Create("MipChain", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT, .mipLevels = 3});
		rg.CreateView("Mip1", RenderGraphViewDesc{
			.texture = "MipChain",
			.baseMipLevel = 1,
			.mipLevelCount = 1,
			.baseArrayLayer = 0,
			.arrayLayerCount = 1
		});

		Array<int> executionOrder;

		rg.AddComputePass("SampleMipChain", "Shaders/SampleMipChain")
			.Read("MipChain")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(2); });

		rg.AddComputePass("BuildMip1", "Shaders/BuildMip1")
			.Write("Mip1")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(1); });

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		rg.Execute(cmd);
		cmd->End();

		REQUIRE(executionOrder.Size() == 2);
		CHECK(executionOrder[0] == 1);
		CHECK(executionOrder[1] == 2);

		TestGPUTexture* mipChain = static_cast<TestGPUTexture*>(rg.GetTexture("MipChain"));
		REQUIRE(mipChain != nullptr);
		CHECK(mipChain->AllSubresourcesInState(ResourceState::ShaderReadOnly));
		CHECK(mipChain->mismatchCount == 0);
	}

	TEST_CASE("RenderGraph::ExecuteSortsWriterBeforeReader")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);
		rg.SetOutputSize(Extent{64, 64});

		rg.Create("Light", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});

		Array<int> executionOrder;

		rg.AddComputePass("Composite", "Shaders/Composite")
			.Read("Light")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(2); });

		rg.AddComputePass("Lighting", "Shaders/Lighting")
			.Write("Light")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(1); });

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		rg.Execute(cmd);
		cmd->End();

		REQUIRE(executionOrder.Size() == 2);
		CHECK(executionOrder[0] == 1);
		CHECK(executionOrder[1] == 2);

		TestGPUTexture* light = static_cast<TestGPUTexture*>(rg.GetTexture("Light"));
		REQUIRE(light != nullptr);
		CHECK(light->mismatchCount == 0);
	}

	TEST_CASE("RenderGraph::ExecuteSortsDependencyChain")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);
		rg.SetOutputSize(Extent{64, 64});

		rg.Create("A", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});
		rg.Create("B", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});
		rg.Create("C", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});

		Array<int> executionOrder;

		rg.AddComputePass("Final", "Shaders/Final")
			.Read("C")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(4); });

		rg.AddComputePass("BuildC", "Shaders/BuildC")
			.Read("B")
			.Write("C")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(3); });

		rg.AddComputePass("BuildB", "Shaders/BuildB")
			.Read("A")
			.Write("B")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(2); });

		rg.AddComputePass("BuildA", "Shaders/BuildA")
			.Write("A")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(1); });

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		rg.Execute(cmd);
		cmd->End();

		REQUIRE(executionOrder.Size() == 4);
		CHECK(executionOrder[0] == 1);
		CHECK(executionOrder[1] == 2);
		CHECK(executionOrder[2] == 3);
		CHECK(executionOrder[3] == 4);
	}

	TEST_CASE("RenderGraph::ExecuteCachesTopologicalSort")
	{
		TestRenderDevice device;
		RenderGraph      rg(&device);
		rg.SetOutputSize(Extent{64, 64});

		rg.Create("Light", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});
		rg.Create("Color", RenderGraphTextureDesc{.format = Format::RGBA16_FLOAT});

		Array<int> executionOrder;

		auto declareBaseGraph = [&]
		{
			rg.AddComputePass("Composite", "Shaders/Composite")
				.Read("Light")
				.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(2); });

			rg.AddComputePass("Lighting", "Shaders/Lighting")
				.Write("Light")
				.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(1); });
		};

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		auto execute = [&]
		{
			cmd->Begin();
			rg.Execute(cmd);
			cmd->End();
		};

		declareBaseGraph();
		execute();

		REQUIRE(executionOrder.Size() == 2);
		CHECK(executionOrder[0] == 1);
		CHECK(executionOrder[1] == 2);
		CHECK(rg.GetTopologyBuildCount() == 1);

		rg.Begin(nullptr);
		executionOrder.Clear();
		declareBaseGraph();
		execute();

		REQUIRE(executionOrder.Size() == 2);
		CHECK(executionOrder[0] == 1);
		CHECK(executionOrder[1] == 2);
		CHECK(rg.GetTopologyBuildCount() == 1);

		rg.Begin(nullptr);
		executionOrder.Clear();

		rg.AddComputePass("Composite", "Shaders/Composite")
			.Read("Light")
			.Write("Color")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(2); });

		rg.AddComputePass("Lighting", "Shaders/Lighting")
			.Write("Light")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(1); });

		rg.AddComputePass("Tonemap", "Shaders/Tonemap")
			.Read("Color")
			.Render([&](RenderGraph&, Scene*, GPUCommandBuffer*) { executionOrder.EmplaceBack(3); });

		execute();

		REQUIRE(executionOrder.Size() == 3);
		CHECK(executionOrder[0] == 1);
		CHECK(executionOrder[1] == 2);
		CHECK(executionOrder[2] == 3);
		CHECK(rg.GetTopologyBuildCount() == 2);
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
