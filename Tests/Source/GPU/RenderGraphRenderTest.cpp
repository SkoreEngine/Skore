#ifdef SK_GPU_TESTS

#include "doctest.h"

#include "GpuTestEnvironment.hpp"

#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/IO/Path.hpp"

using namespace Skore;

namespace
{
	constexpr u32 kWidth = 64;
	constexpr u32 kHeight = 64;

	// Adds a graphics pass that draws the SV_VertexID triangle into the named target. The render
	// pass + framebuffer are owned by the graph; the pipeline is built lazily against the graph's
	// render pass on first execution and cached in the caller-provided pointer so it can be
	// destroyed once at the end of the test (the render graph does not own GetOrCreatePipeline-style
	// pipelines).
	void AddTrianglePass(RenderGraph& rg, RID shader, GPUPipeline*& pipeline, StringView target)
	{
		RenderGraphPass& pass = rg.AddGraphicsPass("Triangle");
		RenderGraphPass* passPtr = &pass;

		pass.Write(target)
			.Render([&pipeline, shader, passPtr](RenderGraph&, Scene*, GPUCommandBuffer* cmd)
			{
				if (pipeline == nullptr)
				{
					GraphicsPipelineDesc pipelineDesc;
					pipelineDesc.shader = shader;
					pipelineDesc.variant = "Default";
					pipelineDesc.topology = PrimitiveTopology::TriangleList;
					pipelineDesc.renderPass = passPtr->GetRenderPass();
					pipelineDesc.rasterizerState.cullMode = CullMode::None;
					pipelineDesc.depthStencilState.depthTestEnable = false;
					pipelineDesc.depthStencilState.depthWriteEnable = false;
					pipelineDesc.blendStates = {BlendStateDesc{}};
					pipelineDesc.debugName = "RenderGraphTrianglePipeline";
					pipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
				}

				cmd->BindPipeline(pipeline);
				cmd->Draw(3, 1, 0, 0);
			});
	}

	RenderGraphTextureDesc ColorTargetDesc(Extent extent)
	{
		return RenderGraphTextureDesc{
			.format = Format::RGBA8_UNORM,
			.extent = extent,
			.usage = ResourceUsage::CopySource,
			.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f)
		};
	}

	// Copies the graph-managed color target (left in ColorAttachment by Execute) into a host buffer.
	void ReadbackColor(RenderGraph& rg, StringView name, GPUBuffer* readback, Extent extent)
	{
		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			GPUTexture* color = rg.GetTexture(name);
			cmd->ResourceBarrier(TextureBarrierDesc{
				.texture = color,
				.oldState = ResourceState::ColorAttachment,
				.newState = ResourceState::CopySource
			});
			cmd->CopyTextureToBuffer(BufferTextureCopy{
				.buffer = readback,
				.texture = color,
				.extent = Extent3D{extent.width, extent.height, 1}
			});
		});
	}

	// The Triangle shader fills the middle of the target with solid red and leaves the corners at the
	// clear color (opaque black).
	void CheckTriangleReadback(const u8* pixels, u32 width, u32 height)
	{
		auto pixelAt = [&](u32 x, u32 y) -> const u8* {
			return pixels + (y * width + x) * 4;
		};

		const u8* center = pixelAt(width / 2, height / 2);
		CHECK(center[0] > 200); // R
		CHECK(center[1] < 64);  // G
		CHECK(center[2] < 64);  // B
		CHECK(center[3] > 200); // A

		const u8* corner = pixelAt(0, 0);
		CHECK(corner[0] < 16);
		CHECK(corner[1] < 16);
		CHECK(corner[2] < 16);
	}

	GPUBuffer* CreateReadbackBuffer(StringView debugName)
	{
		return Graphics::CreateBuffer(BufferDesc{
			.size = kWidth * kHeight * 4,
			.usage = ResourceUsage::CopyDest,
			.hostVisible = true,
			.persistentMapped = true,
			.debugName = debugName
		});
	}

	// Drives several Begin/Execute frames through the same graph description and asserts the final
	// frame still produces a correct image. Exercises the frame ring (currentFrame toggling between
	// the two in-flight slots), the pass pool recycling and the render pass / framebuffer caches.
	TEST_CASE("Graphics::Vulkan::RenderGraphMultiFrameStableTriangle")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileGraphicsShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/Triangle.raster"));
		REQUIRE(shader);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateReadbackBuffer("RenderGraphStableReadback");
		REQUIRE(readback != nullptr);

		GPUPipeline* pipeline = nullptr;

		constexpr u32 kFrameCount = 6;
		for (u32 frame = 0; frame < kFrameCount; ++frame)
		{
			rg.Begin(nullptr);

			rg.Create("Color", ColorTargetDesc(Extent{kWidth, kHeight}));
			rg.SetColorOutput("Color");
			AddTrianglePass(rg, shader, pipeline, "Color");

			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});

			// The graph-managed texture survives across every frame, so the handle never changes.
			CHECK(rg.GetTexture("Color") != nullptr);
		}

		ReadbackColor(rg, "Color", readback, Extent{kWidth, kHeight});

		const u8* pixels = static_cast<const u8*>(readback->Map());
		REQUIRE(pixels != nullptr);
		CheckTriangleReadback(pixels, kWidth, kHeight);
		readback->Unmap();

		if (pipeline != nullptr) pipeline->Destroy();
		readback->Destroy();
	}

	// Simulates a feature being toggled off mid-run: a transient texture is written by an extra pass
	// for a couple of frames, the pass is then dropped, and after SK_FRAMES_IN_FLIGHT frames the
	// graph collects the now-unreferenced texture. Re-adding the pass recreates it. The persistent
	// color output keeps rendering correctly through all the resource churn, and the per-test
	// validation hook (ResourceScope) guards against any stale-handle / barrier mistakes.
	TEST_CASE("Graphics::Vulkan::RenderGraphTransientResourceLifecycle")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileGraphicsShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/Triangle.raster"));
		REQUIRE(shader);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUPipeline* pipeline = nullptr;

		auto declareColor = [&]()
		{
			rg.Create("Color", ColorTargetDesc(Extent{kWidth, kHeight}));
			rg.SetColorOutput("Color");
			AddTrianglePass(rg, shader, pipeline, "Color");
		};

		// WriteRead (instead of Write) marks the first access as a read too, which keeps the texture
		// out of the transient memory-aliasing group - aliased resources are intentionally never
		// collected, so this keeps the lifecycle deterministic. The pass itself does no drawing; it
		// only exists to reference the resource so the graph keeps it alive.
		auto declareTransient = [&]()
		{
			rg.Create("Transient", RenderGraphTextureDesc{.format = Format::RGBA8_UNORM, .extent = Extent{kWidth, kHeight}});
			rg.AddGraphicsPass("TransientPass")
				.WriteRead("Transient")
				.Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});
		};

		auto submit = [&]()
		{
			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		};

		// Frames 1-2: transient is actively referenced, so its GPU texture exists.
		for (int i = 0; i < 2; ++i)
		{
			rg.Begin(nullptr);
			declareColor();
			declareTransient();
			submit();
		}
		CHECK(rg.GetTexture("Transient") != nullptr);

		// Frame 3: stop referencing the transient. It is one generation stale, still within the
		// in-flight window, so it must not be collected yet (its memory could still be in use by a
		// previously submitted frame).
		rg.Begin(nullptr);
		declareColor();
		submit();
		CHECK(rg.GetTexture("Transient") != nullptr);

		// Frame 4: now two generations stale -> CollectUnusedResources (run inside Begin) destroys it.
		rg.Begin(nullptr);
		CHECK(rg.GetTexture("Transient") == nullptr);
		declareColor();
		submit();
		CHECK(rg.GetTexture("Transient") == nullptr);

		// Frame 5: re-introducing the pass recreates the resource from scratch mid-run.
		rg.Begin(nullptr);
		declareColor();
		declareTransient();
		submit();
		CHECK(rg.GetTexture("Transient") != nullptr);

		// The persistent output is unaffected by the transient coming and going.
		GPUBuffer* readback = CreateReadbackBuffer("RenderGraphLifecycleReadback");
		REQUIRE(readback != nullptr);

		ReadbackColor(rg, "Color", readback, Extent{kWidth, kHeight});

		const u8* pixels = static_cast<const u8*>(readback->Map());
		REQUIRE(pixels != nullptr);
		CheckTriangleReadback(pixels, kWidth, kHeight);
		readback->Unmap();

		if (pipeline != nullptr) pipeline->Destroy();
		readback->Destroy();
	}

	// An output-following texture (extent {0,0} -> sized from the output) must be torn down and
	// recreated at the new resolution when the graph is resized mid-run, and keep rendering.
	TEST_CASE("Graphics::Vulkan::RenderGraphResizeRecreatesOutput")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileGraphicsShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/Triangle.raster"));
		REQUIRE(shader);

		RenderGraph rg;

		GPUPipeline* pipeline = nullptr;

		GPUBuffer* readback = CreateReadbackBuffer("RenderGraphResizeReadback");
		REQUIRE(readback != nullptr);

		auto renderFrame = [&](Extent size)
		{
			rg.Begin(nullptr);
			rg.SetOutputSize(size);
			rg.Create("Color", ColorTargetDesc(Extent{0, 0})); // follows the output size
			rg.SetColorOutput("Color");
			AddTrianglePass(rg, shader, pipeline, "Color");
			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		};

		auto verifyAt = [&](Extent size)
		{
			GPUTexture* color = rg.GetTexture("Color");
			REQUIRE(color != nullptr);
			CHECK(color->GetDesc().extent.width == size.width);
			CHECK(color->GetDesc().extent.height == size.height);

			ReadbackColor(rg, "Color", readback, size);

			const u8* pixels = static_cast<const u8*>(readback->Map());
			REQUIRE(pixels != nullptr);
			CheckTriangleReadback(pixels, size.width, size.height);
			readback->Unmap();
		};

		renderFrame(Extent{kWidth, kHeight});
		renderFrame(Extent{kWidth, kHeight});
		verifyAt(Extent{kWidth, kHeight});

		// Shrink: DestroyOutputFollowingResources frees the old texture/view, then it is recreated
		// at the new size on the next Execute.
		renderFrame(Extent{kWidth / 2, kHeight / 2});
		verifyAt(Extent{kWidth / 2, kHeight / 2});

		// Grow again to make sure repeated resizes keep working.
		renderFrame(Extent{kWidth, kHeight});
		verifyAt(Extent{kWidth, kHeight});

		if (pipeline != nullptr) pipeline->Destroy();
		readback->Destroy();
	}

	GPUBuffer* CreateFloatReadbackBuffer(StringView debugName)
	{
		return Graphics::CreateBuffer(BufferDesc{
			.size = kWidth * kHeight * 4 * sizeof(f32),
			.usage = ResourceUsage::CopyDest,
			.hostVisible = true,
			.persistentMapped = true,
			.debugName = debugName
		});
	}

	// Copies an RGBA32F storage texture (left in General by a compute pass) into a host buffer.
	void ReadbackStorage(RenderGraph& rg, StringView name, GPUBuffer* readback, Extent extent)
	{
		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			GPUTexture* texture = rg.GetTexture(name);
			cmd->ResourceBarrier(TextureBarrierDesc{
				.texture = texture,
				.oldState = ResourceState::General,
				.newState = ResourceState::CopySource
			});
			cmd->CopyTextureToBuffer(BufferTextureCopy{
				.buffer = readback,
				.texture = texture,
				.extent = Extent3D{extent.width, extent.height, 1}
			});
		});
	}

	// A compute pass writing a UAV, wired through a graph-managed descriptor set. The output texture
	// is created by the graph, so the descriptor set can only be pointed at it from inside the pass
	// callback (after CreateResourceTextures has run); the graph supplies and caches the descriptor
	// set per shader/frame via GetDescriptorSet, and owns + auto-creates the compute pipeline.
	TEST_CASE("Graphics::Vulkan::RenderGraphComputeWritesUav")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/FillCompute.comp"));
		REQUIRE(shader);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateFloatReadbackBuffer("RenderGraphComputeReadback");
		REQUIRE(readback != nullptr);

		constexpr u32 kFrameCount = 3;
		for (u32 frame = 0; frame < kFrameCount; ++frame)
		{
			rg.Begin(nullptr);

			rg.Create("Storage", RenderGraphTextureDesc{
				.format = Format::RGBA32_FLOAT,
				.extent = Extent{kWidth, kHeight},
				.usage = ResourceUsage::CopySource
			});

			RenderGraphPass& pass = rg.AddComputePass("Fill", shader);
			RenderGraphPass* passPtr = &pass;
			pass.Write("Storage")
				.Render([shader, passPtr](RenderGraph& graph, Scene*, GPUCommandBuffer* cmd)
				{
					GPUDescriptorSet* descriptorSet = graph.GetDescriptorSet(shader, "Default", 0);
					descriptorSet->UpdateTexture(0, graph.GetTexture("Storage"));
					cmd->BindDescriptorSet(passPtr->GetPipeline(), 0, descriptorSet);
					cmd->Dispatch(kWidth / 8, kHeight / 8, 1);
				});

			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		}

		ReadbackStorage(rg, "Storage", readback, Extent{kWidth, kHeight});

		const f32* pixels = static_cast<const f32*>(readback->Map());
		REQUIRE(pixels != nullptr);

		auto pixelAt = [&](u32 x, u32 y) -> const f32* {
			return pixels + (y * kWidth + x) * 4;
		};

		// The shader writes float4(uv.x, uv.y, 0.25, 1) with uv = (id + 0.5) / 64.
		const f32* center = pixelAt(kWidth / 2, kHeight / 2);
		CHECK(center[0] == doctest::Approx(0.5078125f).epsilon(0.01));
		CHECK(center[1] == doctest::Approx(0.5078125f).epsilon(0.01));
		CHECK(center[2] == doctest::Approx(0.25f).epsilon(0.01));
		CHECK(center[3] == doctest::Approx(1.0f));

		const f32* corner = pixelAt(0, 0);
		CHECK(corner[0] == doctest::Approx(0.0078125f).epsilon(0.01));
		CHECK(corner[1] == doctest::Approx(0.0078125f).epsilon(0.01));
		CHECK(corner[2] == doctest::Approx(0.25f).epsilon(0.01));

		readback->Unmap();
		readback->Destroy();
	}

	// Ping-pong / history buffer: a pingPong texture owns two GPU textures and the graph hands out
	// the current slot via GetTexture and the previous frame's slot via GetPrevTexture. A compute
	// pass reads the previous slot and writes the current one, so a per-frame increment accumulates
	// across frames. Both slots are seeded first so the very first read never touches uninitialised
	// memory.
	TEST_CASE("Graphics::Vulkan::RenderGraphPingPongHistoryAccumulates")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID seedShader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/HistorySeed.comp"));
		RID accumulateShader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/HistoryAccumulate.comp"));
		REQUIRE(seedShader);
		REQUIRE(accumulateShader);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		auto createHistory = [&]()
		{
			rg.Create("History", RenderGraphTextureDesc{
				.format = Format::RGBA32_FLOAT,
				.extent = Extent{kWidth, kHeight},
				.usage = ResourceUsage::CopySource,
				.pingPong = true
			});
		};

		auto submit = [&]()
		{
			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		};

		// Seed both ping-pong slots (one per frame) to a known zero state.
		for (int i = 0; i < 2; ++i)
		{
			rg.Begin(nullptr);
			createHistory();

			RenderGraphPass& pass = rg.AddComputePass("Seed", seedShader);
			RenderGraphPass* passPtr = &pass;
			pass.Write("History")
				.Render([seedShader, passPtr](RenderGraph& graph, Scene*, GPUCommandBuffer* cmd)
				{
					GPUDescriptorSet* descriptorSet = graph.GetDescriptorSet(seedShader, "Default", 0);
					descriptorSet->UpdateTexture(0, graph.GetTexture("History"));
					cmd->BindDescriptorSet(passPtr->GetPipeline(), 0, descriptorSet);
					cmd->Dispatch(kWidth / 8, kHeight / 8, 1);
				});

			submit();
		}

		// Accumulate: every frame reads the previous slot and writes the current one (+0.2).
		constexpr u32 kAccumulateFrames = 4;
		for (u32 frame = 0; frame < kAccumulateFrames; ++frame)
		{
			rg.Begin(nullptr);
			createHistory();

			RenderGraphPass& pass = rg.AddComputePass("Accumulate", accumulateShader);
			RenderGraphPass* passPtr = &pass;
			pass.Write("History")
				.Render([accumulateShader, passPtr](RenderGraph& graph, Scene*, GPUCommandBuffer* cmd)
				{
					GPUDescriptorSet* descriptorSet = graph.GetDescriptorSet(accumulateShader, "Default", 0);
					descriptorSet->UpdateTexture(0, graph.GetPrevTexture("History")); // previous frame
					descriptorSet->UpdateTexture(1, graph.GetTexture("History"));     // current frame
					cmd->BindDescriptorSet(passPtr->GetPipeline(), 0, descriptorSet);
					cmd->Dispatch(kWidth / 8, kHeight / 8, 1);
				});

			submit();
		}

		// The two ping-pong slots are distinct GPU textures.
		CHECK(rg.GetTexture("History") != rg.GetPrevTexture("History"));

		GPUBuffer* readback = CreateFloatReadbackBuffer("RenderGraphHistoryReadback");
		REQUIRE(readback != nullptr);

		ReadbackStorage(rg, "History", readback, Extent{kWidth, kHeight});

		const f32* pixels = static_cast<const f32*>(readback->Map());
		REQUIRE(pixels != nullptr);

		const f32  expected = 0.2f * kAccumulateFrames;
		const f32* center = pixels + (kHeight / 2 * kWidth + kWidth / 2) * 4;
		CHECK(center[0] == doctest::Approx(expected).epsilon(0.001));
		CHECK(center[1] == doctest::Approx(expected).epsilon(0.001));
		CHECK(center[2] == doctest::Approx(expected).epsilon(0.001));
		CHECK(center[3] == doctest::Approx(1.0f));

		readback->Unmap();
		readback->Destroy();
	}

	// Renders the triangle into an engine-owned texture handed to the graph via SetOutputAttachments
	// (Import). After the last pass the graph must transition the imported texture back to the state
	// the caller declared it needs, so the readback copy below runs with no manual barrier.
	TEST_CASE("Graphics::Vulkan::RenderGraphImportedOutputTransitionsToRequiredState")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileGraphicsShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/Triangle.raster"));
		REQUIRE(shader);

		GPUTexture* external = Graphics::CreateTexture(TextureDesc{
			.extent = {kWidth, kHeight, 1},
			.format = Format::RGBA8_UNORM,
			.usage = ResourceUsage::RenderTarget | ResourceUsage::CopySource,
			.debugName = "ImportedOutput"
		});
		REQUIRE(external != nullptr);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateReadbackBuffer("RenderGraphImportReadback");
		REQUIRE(readback != nullptr);

		// Import's contract is that the texture is already in the declared state; a freshly created
		// texture is Undefined, so transition it to CopySource before handing it to the graph.
		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->ResourceBarrier(TextureBarrierDesc{
				.texture = external,
				.oldState = ResourceState::Undefined,
				.newState = ResourceState::CopySource
			});
		});

		GPUPipeline* pipeline = nullptr;

		constexpr u32 kFrameCount = 3;
		for (u32 frame = 0; frame < kFrameCount; ++frame)
		{
			rg.Begin(nullptr);
			rg.SetOutputAttachments("Output", {external}, ResourceState::CopySource);
			rg.SetColorOutput("Output");
			AddTrianglePass(rg, shader, pipeline, "Output");

			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		}

		// No barrier here on purpose: the graph already left the imported texture in CopySource.
		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->CopyTextureToBuffer(BufferTextureCopy{
				.buffer = readback,
				.texture = external,
				.extent = Extent3D{kWidth, kHeight, 1}
			});
		});

		const u8* pixels = static_cast<const u8*>(readback->Map());
		REQUIRE(pixels != nullptr);
		CheckTriangleReadback(pixels, kWidth, kHeight);
		readback->Unmap();

		if (pipeline != nullptr) pipeline->Destroy();
		readback->Destroy();
		external->Destroy();
	}

	// Adds a graphics pass that writes two color attachments in one pass (MRT). Same lazy pipeline
	// build as AddTrianglePass, but the pipeline needs one blend state per attachment.
	void AddMRTTrianglePass(RenderGraph& rg, RID shader, GPUPipeline*& pipeline, StringView targetA, StringView targetB)
	{
		RenderGraphPass& pass = rg.AddGraphicsPass("TriangleMRT");
		RenderGraphPass* passPtr = &pass;

		pass.Write(targetA)
			.Write(targetB)
			.Render([&pipeline, shader, passPtr](RenderGraph&, Scene*, GPUCommandBuffer* cmd)
			{
				if (pipeline == nullptr)
				{
					GraphicsPipelineDesc pipelineDesc;
					pipelineDesc.shader = shader;
					pipelineDesc.variant = "Default";
					pipelineDesc.topology = PrimitiveTopology::TriangleList;
					pipelineDesc.renderPass = passPtr->GetRenderPass();
					pipelineDesc.rasterizerState.cullMode = CullMode::None;
					pipelineDesc.depthStencilState.depthTestEnable = false;
					pipelineDesc.depthStencilState.depthWriteEnable = false;
					pipelineDesc.blendStates = {BlendStateDesc{}, BlendStateDesc{}};
					pipelineDesc.debugName = "RenderGraphTriangleMRTPipeline";
					pipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
				}

				cmd->BindPipeline(pipeline);
				cmd->Draw(3, 1, 0, 0);
			});
	}

	// A single graphics pass writing two color targets - exercises render pass / framebuffer creation
	// with multiple attachments. Target 0 receives red, target 1 receives green.
	TEST_CASE("Graphics::Vulkan::RenderGraphMultipleRenderTargets")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileGraphicsShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/TriangleMRT.raster"));
		REQUIRE(shader);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readbackA = CreateReadbackBuffer("RenderGraphMRTReadbackA");
		GPUBuffer* readbackB = CreateReadbackBuffer("RenderGraphMRTReadbackB");
		REQUIRE(readbackA != nullptr);
		REQUIRE(readbackB != nullptr);

		GPUPipeline* pipeline = nullptr;

		rg.Begin(nullptr);
		rg.Create("ColorA", ColorTargetDesc(Extent{kWidth, kHeight}));
		rg.Create("ColorB", ColorTargetDesc(Extent{kWidth, kHeight}));
		AddMRTTrianglePass(rg, shader, pipeline, "ColorA", "ColorB");

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			rg.Execute(cmd);
		});

		ReadbackColor(rg, "ColorA", readbackA, Extent{kWidth, kHeight});
		ReadbackColor(rg, "ColorB", readbackB, Extent{kWidth, kHeight});

		const u8* a = static_cast<const u8*>(readbackA->Map());
		const u8* b = static_cast<const u8*>(readbackB->Map());
		REQUIRE(a != nullptr);
		REQUIRE(b != nullptr);

		auto pixelAt = [](const u8* pixels, u32 x, u32 y) -> const u8* {
			return pixels + (y * kWidth + x) * 4;
		};

		const u8* centerA = pixelAt(a, kWidth / 2, kHeight / 2);
		CHECK(centerA[0] > 200); // red
		CHECK(centerA[1] < 64);
		CHECK(centerA[2] < 64);

		const u8* centerB = pixelAt(b, kWidth / 2, kHeight / 2);
		CHECK(centerB[0] < 64);
		CHECK(centerB[1] > 200); // green
		CHECK(centerB[2] < 64);

		const u8* cornerA = pixelAt(a, 0, 0);
		const u8* cornerB = pixelAt(b, 0, 0);
		CHECK(cornerA[0] < 16);
		CHECK(cornerA[1] < 16);
		CHECK(cornerB[0] < 16);
		CHECK(cornerB[1] < 16);

		readbackA->Unmap();
		readbackB->Unmap();

		if (pipeline != nullptr) pipeline->Destroy();
		readbackA->Destroy();
		readbackB->Destroy();
	}

	// The fully declarative compute path: the output is an imported texture (so the descriptor set can
	// be bound up front), the graph binds the descriptor set via DescriptorSet() and auto-dispatches,
	// dividing the requested 64x64 by the shader's [numthreads(8,8,1)]. A wrong group count would
	// leave part of the image unwritten, so checking the far corner verifies full coverage.
	TEST_CASE("Graphics::Vulkan::RenderGraphComputeAutoDispatchImportedOutput")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/FillSolid.comp"));
		REQUIRE(shader);

		GPUTexture* external = Graphics::CreateTexture(TextureDesc{
			.extent = {kWidth, kHeight, 1},
			.format = Format::RGBA32_FLOAT,
			.usage = ResourceUsage::UnorderedAccess | ResourceUsage::CopySource,
			.debugName = "ComputeImportedOutput"
		});
		REQUIRE(external != nullptr);

		GPUDescriptorSet* descriptorSet = Graphics::GetDevice()->CreateDescriptorSet(shader, "Default", 0);
		REQUIRE(descriptorSet != nullptr);
		descriptorSet->UpdateTexture(0, external);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateFloatReadbackBuffer("RenderGraphAutoDispatchReadback");
		REQUIRE(readback != nullptr);

		// Match the declared imported state (the texture starts Undefined after creation).
		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->ResourceBarrier(TextureBarrierDesc{
				.texture = external,
				.oldState = ResourceState::Undefined,
				.newState = ResourceState::CopySource
			});
		});

		constexpr u32 kFrameCount = 3;
		for (u32 frame = 0; frame < kFrameCount; ++frame)
		{
			rg.Begin(nullptr);
			rg.SetOutputAttachments("Output", {external}, ResourceState::CopySource);
			rg.AddComputePass("Fill", shader)
				.Write("Output")
				.DescriptorSet(0, descriptorSet)
				.Dispatch(kWidth, kHeight, 1);

			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		}

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->CopyTextureToBuffer(BufferTextureCopy{
				.buffer = readback,
				.texture = external,
				.extent = Extent3D{kWidth, kHeight, 1}
			});
		});

		const f32* pixels = static_cast<const f32*>(readback->Map());
		REQUIRE(pixels != nullptr);

		auto checkFillColor = [&](u32 x, u32 y)
		{
			const f32* texel = pixels + (y * kWidth + x) * 4;
			CHECK(texel[0] == doctest::Approx(0.2f).epsilon(0.01));
			CHECK(texel[1] == doctest::Approx(0.4f).epsilon(0.01));
			CHECK(texel[2] == doctest::Approx(0.6f).epsilon(0.01));
			CHECK(texel[3] == doctest::Approx(1.0f));
		};

		checkFillColor(0, 0);
		checkFillColor(kWidth / 2, kHeight / 2);
		checkFillColor(kWidth - 1, kHeight - 1); // far corner: only written if the group count is right

		readback->Unmap();

		descriptorSet->Destroy();
		readback->Destroy();
		external->Destroy();
	}

	// Three transient color targets with disjoint, single-pass lifetimes. The aliasing planner packs
	// them into shared GPU memory and the graph inserts memory barriers as each alias is activated;
	// this verifies the real aliased-texture path (creation + barriers) stays validation-clean and
	// still produces correct output, beyond what the mock-device alias tests can check.
	TEST_CASE("Graphics::Vulkan::RenderGraphMemoryAliasingReuse")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateReadbackBuffer("RenderGraphAliasReadback");
		REQUIRE(readback != nullptr);

		auto aliasTarget = [](Vec4 clearColor)
		{
			return RenderGraphTextureDesc{
				.format = Format::RGBA8_UNORM,
				.extent = Extent{kWidth, kHeight},
				.usage = ResourceUsage::CopySource,
				.clearColor = clearColor
			};
		};

		RenderGraphAliasReport report;

		constexpr u32 kFrameCount = 2;
		for (u32 frame = 0; frame < kFrameCount; ++frame)
		{
			rg.Begin(nullptr);
			rg.Create("AliasA", aliasTarget(Vec4(1.0f, 0.0f, 0.0f, 1.0f)));
			rg.Create("AliasB", aliasTarget(Vec4(0.0f, 1.0f, 0.0f, 1.0f)));
			rg.Create("AliasC", aliasTarget(Vec4(0.0f, 0.0f, 1.0f, 1.0f)));

			rg.AddGraphicsPass("ClearA").Write("AliasA").Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});
			rg.AddGraphicsPass("ClearB").Write("AliasB").Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});
			rg.AddGraphicsPass("ClearC").Write("AliasC").Render([](RenderGraph&, Scene*, GPUCommandBuffer*) {});

			report = rg.AnalyzeMemoryAliasing();

			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		}

		// All three are transient, write-first and never overlap -> they share one memory region.
		CHECK(report.resources.Size() == 3);
		CHECK(report.aliasedBytes < report.standaloneBytes);

		// ClearC runs last and owns the shared memory at frame end, so AliasC reads back as blue.
		ReadbackColor(rg, "AliasC", readback, Extent{kWidth, kHeight});

		const u8* pixels = static_cast<const u8*>(readback->Map());
		REQUIRE(pixels != nullptr);
		CHECK(pixels[0] < 16);  // R
		CHECK(pixels[1] < 16);  // G
		CHECK(pixels[2] > 200); // B
		readback->Unmap();

		readback->Destroy();
	}

	// Regression for RenderGraphPass::Constants<T>: the graph must push constants with the shader's
	// actual stage (taken from reflection), not ShaderStage::All - otherwise vkCmdPushConstants trips
	// VUID-vkCmdPushConstants-offset-01795 against the compute-only range. Also checks the value
	// arrives intact by reading the written colour back.
	TEST_CASE("Graphics::Vulkan::RenderGraphComputePushConstants")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/FillPushConstant.comp"));
		REQUIRE(shader);

		GPUTexture* external = Graphics::CreateTexture(TextureDesc{
			.extent = {kWidth, kHeight, 1},
			.format = Format::RGBA32_FLOAT,
			.usage = ResourceUsage::UnorderedAccess | ResourceUsage::CopySource,
			.debugName = "PushConstantOutput"
		});
		REQUIRE(external != nullptr);

		GPUDescriptorSet* descriptorSet = Graphics::GetDevice()->CreateDescriptorSet(shader, "Default", 0);
		REQUIRE(descriptorSet != nullptr);
		descriptorSet->UpdateTexture(0, external);

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->ResourceBarrier(TextureBarrierDesc{.texture = external, .oldState = ResourceState::Undefined, .newState = ResourceState::CopySource});
		});

		struct PushConstants
		{
			Vec4 color;
		};

		const Vec4 fillColor = Vec4(0.1f, 0.2f, 0.3f, 1.0f);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateFloatReadbackBuffer("RenderGraphPushConstantReadback");
		REQUIRE(readback != nullptr);

		constexpr u32 kFrameCount = 2;
		for (u32 frame = 0; frame < kFrameCount; ++frame)
		{
			rg.Begin(nullptr);
			rg.SetOutputAttachments("Output", {external}, ResourceState::CopySource);
			rg.AddComputePass("Fill", shader)
				.Write("Output")
				.DescriptorSet(0, descriptorSet)
				.Constants<PushConstants>([&](RenderGraph&, PushConstants& constants) { constants.color = fillColor; })
				.Dispatch(kWidth, kHeight, 1);

			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		}

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->CopyTextureToBuffer(BufferTextureCopy{
				.buffer = readback,
				.texture = external,
				.extent = Extent3D{kWidth, kHeight, 1}
			});
		});

		const f32* pixels = static_cast<const f32*>(readback->Map());
		REQUIRE(pixels != nullptr);

		const f32* center = pixels + (kHeight / 2 * kWidth + kWidth / 2) * 4;
		CHECK(center[0] == doctest::Approx(fillColor.x).epsilon(0.01));
		CHECK(center[1] == doctest::Approx(fillColor.y).epsilon(0.01));
		CHECK(center[2] == doctest::Approx(fillColor.z).epsilon(0.01));
		CHECK(center[3] == doctest::Approx(fillColor.w).epsilon(0.01));
		readback->Unmap();

		descriptorSet->Destroy();
		readback->Destroy();
		external->Destroy();
	}

	// A perFrame buffer is backed by SK_FRAMES_IN_FLIGHT distinct GPU buffers; GetBuffer returns the
	// slot for the current frame. Writing a sentinel to one frame's slot and reading it back two
	// frames later (when the ring returns to the same slot) proves the slots are independent.
	TEST_CASE("Graphics::Vulkan::RenderGraphPerFrameBufferRotates")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		auto runFrame = [&]() -> GPUBuffer*
		{
			rg.Begin(nullptr);
			rg.Create("PerFrame", RenderGraphBufferDesc{
				.size = sizeof(u32),
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true,
				.perFrame = true
			});

			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});

			return rg.GetBuffer("PerFrame");
		};

		// Frame 1 (current slot A): stamp a sentinel.
		GPUBuffer* slotA = runFrame();
		REQUIRE(slotA != nullptr);
		*static_cast<u32*>(slotA->GetMappedData()) = 0xA11CEu;

		// Frame 2 (the other slot): a different buffer that must not see the sentinel.
		GPUBuffer* slotB = runFrame();
		REQUIRE(slotB != nullptr);
		CHECK(slotB != slotA);
		*static_cast<u32*>(slotB->GetMappedData()) = 0xB0B0u;

		// Frame 3 (ring wraps back to slot A): same buffer, sentinel intact.
		GPUBuffer* slotC = runFrame();
		REQUIRE(slotC != nullptr);
		CHECK(slotC == slotA);
		CHECK(*static_cast<u32*>(slotC->GetMappedData()) == 0xA11CEu);
	}

	// Adds a graphics pass that renders into a multisampled color target and resolves it into a
	// single-sample target in the same pass. The render pass therefore needs a color attachment
	// (4x) plus a resolve attachment (1x); the pipeline picks up the sample count from the render
	// pass, so it still uses a single blend state for the one color attachment.
	void AddMSAATrianglePass(RenderGraph& rg, RID shader, GPUPipeline*& pipeline, StringView msaaTarget, StringView resolveTarget)
	{
		RenderGraphPass& pass = rg.AddGraphicsPass("TriangleMSAA");
		RenderGraphPass* passPtr = &pass;

		pass.Write(msaaTarget)
			.Write(resolveTarget)
			.Resolve(resolveTarget)
			.Render([&pipeline, shader, passPtr](RenderGraph&, Scene*, GPUCommandBuffer* cmd)
			{
				if (pipeline == nullptr)
				{
					GraphicsPipelineDesc pipelineDesc;
					pipelineDesc.shader = shader;
					pipelineDesc.variant = "Default";
					pipelineDesc.topology = PrimitiveTopology::TriangleList;
					pipelineDesc.renderPass = passPtr->GetRenderPass();
					pipelineDesc.rasterizerState.cullMode = CullMode::None;
					pipelineDesc.depthStencilState.depthTestEnable = false;
					pipelineDesc.depthStencilState.depthWriteEnable = false;
					pipelineDesc.blendStates = {BlendStateDesc{}};
					pipelineDesc.debugName = "RenderGraphTriangleMSAAPipeline";
					pipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
				}

				cmd->BindPipeline(pipeline);
				cmd->Draw(3, 1, 0, 0);
			});
	}

	// Multisampled render + in-pass resolve. The triangle is rasterized into a 4x MSAA target and
	// resolved into a single-sample target that is read back; the interior stays solid red and the
	// corners stay at the clear color, which proves the resolve attachment was wired correctly.
	TEST_CASE("Graphics::Vulkan::RenderGraphMsaaResolve")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileGraphicsShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/Triangle.raster"));
		REQUIRE(shader);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateReadbackBuffer("RenderGraphMsaaReadback");
		REQUIRE(readback != nullptr);

		GPUPipeline* pipeline = nullptr;

		rg.Begin(nullptr);
		rg.Create("MSAAColor", RenderGraphTextureDesc{
			.format = Format::RGBA8_UNORM,
			.extent = Extent{kWidth, kHeight},
			.samples = 4,
			.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f)
		});
		rg.Create("ResolveColor", RenderGraphTextureDesc{
			.format = Format::RGBA8_UNORM,
			.extent = Extent{kWidth, kHeight},
			.usage = ResourceUsage::CopySource,
			.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f)
		});
		rg.SetColorOutput("ResolveColor");
		AddMSAATrianglePass(rg, shader, pipeline, "MSAAColor", "ResolveColor");

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			rg.Execute(cmd);
		});

		ReadbackColor(rg, "ResolveColor", readback, Extent{kWidth, kHeight});

		const u8* pixels = static_cast<const u8*>(readback->Map());
		REQUIRE(pixels != nullptr);
		CheckTriangleReadback(pixels, kWidth, kHeight);
		readback->Unmap();

		if (pipeline != nullptr) pipeline->Destroy();
		readback->Destroy();
	}

	// The real aliasing correctness test (vs. RenderGraphMemoryAliasingReuse, which only checks the
	// plan + that the path is validation-clean). A producer/consumer chain - Produce(A) -> B=A+0.25 ->
	// C=B+0.25 - is declared in reverse. A and C have disjoint lifetimes and are aliased onto the same
	// memory, while B (live across both) gets its own. The test asserts the planner really placed A and
	// C at the same bucket+offset using the device's actual memory requirements, and that C reads back
	// as the correct chained value - which is only possible if A's data survived its read before its
	// memory was reused for C.
	TEST_CASE("Graphics::Vulkan::RenderGraphAliasedChainPreservesData")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID seedShader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/HistorySeed.comp"));
		RID stepShader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/AliasChainStep.comp"));
		REQUIRE(seedShader);
		REQUIRE(stepShader);

		GPUDevice* device = Graphics::GetDevice();

		// Dedicated descriptor sets, not rg.GetDescriptorSet(): ProcessB and ProcessC share stepShader,
		// and the graph's per-(shader,variant,set,frame) cache would hand them the same set - the second
		// update would clobber the first before the first dispatch runs.
		GPUDescriptorSet* dsProduce = device->CreateDescriptorSet(seedShader, "Default", 0);
		GPUDescriptorSet* dsProcessB = device->CreateDescriptorSet(stepShader, "Default", 0);
		GPUDescriptorSet* dsProcessC = device->CreateDescriptorSet(stepShader, "Default", 0);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateFloatReadbackBuffer("RenderGraphAliasChainReadback");
		REQUIRE(readback != nullptr);

		// Identical usage on all three so their memory requirements match and the planner can overlap
		// A and C (CopySource for readback, ShaderResource because the chain samples them).
		auto aliasTarget = []()
		{
			return RenderGraphTextureDesc{
				.format = Format::RGBA32_FLOAT,
				.extent = Extent{kWidth, kHeight},
				.usage = ResourceUsage::CopySource | ResourceUsage::ShaderResource
			};
		};

		auto bindStep = [](GPUDescriptorSet* ds, RenderGraphPass* passPtr, StringView input, StringView output)
		{
			return [ds, passPtr, input, output](RenderGraph& graph, Scene*, GPUCommandBuffer* cmd)
			{
				ds->UpdateTexture(0, graph.GetTexture(input));
				ds->UpdateSampler(1, Graphics::GetNearestSampler());
				ds->UpdateTexture(2, graph.GetTexture(output));
				cmd->BindDescriptorSet(passPtr->GetPipeline(), 0, ds);
				cmd->Dispatch(kWidth / 8, kHeight / 8, 1);
			};
		};

		RenderGraphAliasReport report;

		constexpr u32 kFrameCount = 2;
		for (u32 frame = 0; frame < kFrameCount; ++frame)
		{
			rg.Begin(nullptr);
			rg.Create("AliasA", aliasTarget());
			rg.Create("AliasB", aliasTarget());
			rg.Create("AliasC", aliasTarget());

			// Declared in reverse to also exercise the topological sort.
			RenderGraphPass& passC = rg.AddComputePass("ProcessC", stepShader);
			passC.Read("AliasB").Write("AliasC").Render(bindStep(dsProcessC, &passC, "AliasB", "AliasC"));

			RenderGraphPass& passB = rg.AddComputePass("ProcessB", stepShader);
			passB.Read("AliasA").Write("AliasB").Render(bindStep(dsProcessB, &passB, "AliasA", "AliasB"));

			RenderGraphPass& passA = rg.AddComputePass("Produce", seedShader);
			RenderGraphPass* passAPtr = &passA;
			passA.Write("AliasA").Render([dsProduce, passAPtr](RenderGraph& graph, Scene*, GPUCommandBuffer* cmd)
			{
				dsProduce->UpdateTexture(0, graph.GetTexture("AliasA"));
				cmd->BindDescriptorSet(passAPtr->GetPipeline(), 0, dsProduce);
				cmd->Dispatch(kWidth / 8, kHeight / 8, 1);
			});

			report = rg.AnalyzeMemoryAliasing();

			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		}

		auto indexOf = [&](const char* name) -> i32
		{
			for (u32 i = 0; i < report.resourceNames.Size(); ++i)
			{
				if (report.resourceNames[i].Compare(name) == 0) return static_cast<i32>(i);
			}
			return -1;
		};

		const i32 ia = indexOf("AliasA");
		const i32 ib = indexOf("AliasB");
		const i32 ic = indexOf("AliasC");
		REQUIRE(ia >= 0);
		REQUIRE(ib >= 0);
		REQUIRE(ic >= 0);

		const RenderGraphAliasPlacement& pa = report.plan.placements[ia];
		const RenderGraphAliasPlacement& pb = report.plan.placements[ib];
		const RenderGraphAliasPlacement& pc = report.plan.placements[ic];

		// A and C have disjoint lifetimes -> the planner placed them on the exact same memory.
		CHECK(pa.bucket == pc.bucket);
		CHECK(pa.offset == pc.offset);
		// B is live across both, so it must not overlap them.
		const bool bSharesAMemory = pb.bucket == pa.bucket && pb.offset == pa.offset;
		CHECK_FALSE(bSharesAMemory);
		CHECK(report.aliasedBytes < report.standaloneBytes);

		// Data correctness: C = seed(0) + 0.25 + 0.25 = 0.5 everywhere. If A's contents had been
		// clobbered by C reusing its memory before B sampled A, this would be wrong.
		ReadbackStorage(rg, "AliasC", readback, Extent{kWidth, kHeight});

		const f32* pixels = static_cast<const f32*>(readback->Map());
		REQUIRE(pixels != nullptr);
		const f32* center = pixels + (kHeight / 2 * kWidth + kWidth / 2) * 4;
		CHECK(center[0] == doctest::Approx(0.5f).epsilon(0.001));
		CHECK(center[1] == doctest::Approx(0.5f).epsilon(0.001));
		CHECK(center[2] == doctest::Approx(0.5f).epsilon(0.001));
		CHECK(center[3] == doctest::Approx(1.0f));
		readback->Unmap();

		dsProduce->Destroy();
		dsProcessB->Destroy();
		dsProcessC->Destroy();
		readback->Destroy();
	}

	// A transfer pass (AddPass) that copies one graph texture into another. The graph moves Src to
	// CopySource and Dst to CopyDest (with the Transfer sync scope) before the callback runs the
	// actual CopyTexture, and orders the producing graphics pass first via the Src dependency.
	TEST_CASE("Graphics::Vulkan::RenderGraphTransferPassCopiesTexture")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileGraphicsShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/Triangle.raster"));
		REQUIRE(shader);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateReadbackBuffer("RenderGraphTransferReadback");
		REQUIRE(readback != nullptr);

		GPUPipeline* pipeline = nullptr;

		rg.Begin(nullptr);
		rg.Create("Src", ColorTargetDesc(Extent{kWidth, kHeight}));
		// CopySource for the readback + ShaderResource so the texture is view-compatible (the device
		// creates a default view for every texture, which a transfer-only image can't have).
		rg.Create("Dst", RenderGraphTextureDesc{
			.format = Format::RGBA8_UNORM,
			.extent = Extent{kWidth, kHeight},
			.usage = ResourceUsage::CopySource | ResourceUsage::ShaderResource
		});

		AddTrianglePass(rg, shader, pipeline, "Src");

		rg.AddPass("CopySrcToDst")
			.Read("Src")
			.Write("Dst")
			.Render([](RenderGraph& graph, Scene*, GPUCommandBuffer* cmd)
			{
				cmd->CopyTexture(TextureCopy{
					.srcTexture = graph.GetTexture("Src"),
					.dstTexture = graph.GetTexture("Dst"),
					.extent = Extent3D{kWidth, kHeight, 1}
				});
			});

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			rg.Execute(cmd);
		});

		// Dst is left in CopyDest by the transfer pass.
		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			GPUTexture* dst = rg.GetTexture("Dst");
			cmd->ResourceBarrier(TextureBarrierDesc{
				.texture = dst,
				.oldState = ResourceState::CopyDest,
				.newState = ResourceState::CopySource
			});
			cmd->CopyTextureToBuffer(BufferTextureCopy{
				.buffer = readback,
				.texture = dst,
				.extent = Extent3D{kWidth, kHeight, 1}
			});
		});

		const u8* pixels = static_cast<const u8*>(readback->Map());
		REQUIRE(pixels != nullptr);
		CheckTriangleReadback(pixels, kWidth, kHeight);
		readback->Unmap();

		if (pipeline != nullptr) pipeline->Destroy();
		readback->Destroy();
	}

	// Renders into a specific mip level through a CreateView. The graph builds the framebuffer from
	// the mip-1 view (so the viewport is the mip's 32x32 size) and transitions only that subresource;
	// the readback reads mip 1 back and expects the triangle, proving subresource-targeted rendering.
	TEST_CASE("Graphics::Vulkan::RenderGraphRendersIntoMipView")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileGraphicsShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/Triangle.raster"));
		REQUIRE(shader);

		constexpr u32 kMip = 1;
		constexpr u32 kMipWidth = kWidth >> kMip;
		constexpr u32 kMipHeight = kHeight >> kMip;

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateReadbackBuffer("RenderGraphMipViewReadback");
		REQUIRE(readback != nullptr);

		GPUPipeline* pipeline = nullptr;

		rg.Begin(nullptr);
		rg.Create("MipTexture", RenderGraphTextureDesc{
			.format = Format::RGBA8_UNORM,
			.extent = Extent{kWidth, kHeight},
			.mipLevels = 2,
			.usage = ResourceUsage::CopySource,
			.clearColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f)
		});
		rg.CreateView("Mip1", RenderGraphViewDesc{
			.texture = "MipTexture",
			.baseMipLevel = kMip,
			.mipLevelCount = 1,
			.baseArrayLayer = 0,
			.arrayLayerCount = 1
		});

		AddTrianglePass(rg, shader, pipeline, "Mip1");

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			rg.Execute(cmd);
		});

		// Read mip 1 (32x32) specifically; mip 0 is never written and is left untouched.
		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			GPUTexture* texture = rg.GetTexture("MipTexture");
			cmd->ResourceBarrier(TextureBarrierDesc{
				.texture = texture,
				.oldState = ResourceState::ColorAttachment,
				.newState = ResourceState::CopySource,
				.baseMipLevel = kMip,
				.mipLevelCount = 1
			});
			cmd->CopyTextureToBuffer(BufferTextureCopy{
				.buffer = readback,
				.texture = texture,
				.extent = Extent3D{kMipWidth, kMipHeight, 1},
				.mipLevel = kMip
			});
		});

		const u8* pixels = static_cast<const u8*>(readback->Map());
		REQUIRE(pixels != nullptr);
		CheckTriangleReadback(pixels, kMipWidth, kMipHeight);
		readback->Unmap();

		if (pipeline != nullptr) pipeline->Destroy();
		readback->Destroy();
	}

	// Indirect compute dispatch: the group counts come from a buffer instead of the pass. The buffer is
	// transitioned to the new ResourceState::IndirectArgument (VK_ACCESS_INDIRECT_COMMAND_READ /
	// DRAW_INDIRECT stage) before the graph issues DispatchIndirect. (8,8,1) groups cover the 64x64
	// image, so a wrong indirect read would leave part of it unwritten.
	TEST_CASE("Graphics::Vulkan::RenderGraphComputeDispatchIndirect")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/FillSolid.comp"));
		REQUIRE(shader);

		GPUTexture* external = Graphics::CreateTexture(TextureDesc{
			.extent = {kWidth, kHeight, 1},
			.format = Format::RGBA32_FLOAT,
			.usage = ResourceUsage::UnorderedAccess | ResourceUsage::CopySource,
			.debugName = "DispatchIndirectOutput"
		});
		REQUIRE(external != nullptr);

		GPUDescriptorSet* descriptorSet = Graphics::GetDevice()->CreateDescriptorSet(shader, "Default", 0);
		REQUIRE(descriptorSet != nullptr);
		descriptorSet->UpdateTexture(0, external);

		GPUBuffer* indirectBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = sizeof(u32) * 3,
			.usage = ResourceUsage::IndirectBuffer,
			.hostVisible = true,
			.persistentMapped = true,
			.debugName = "DispatchIndirectArgs"
		});
		REQUIRE(indirectBuffer != nullptr);

		// VkDispatchIndirectCommand { x, y, z } group counts: (64/8, 64/8, 1) covers the image exactly.
		u32* args = static_cast<u32*>(indirectBuffer->GetMappedData());
		args[0] = kWidth / 8;
		args[1] = kHeight / 8;
		args[2] = 1;

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->ResourceBarrier(TextureBarrierDesc{.texture = external, .oldState = ResourceState::Undefined, .newState = ResourceState::CopySource});
		});

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateFloatReadbackBuffer("RenderGraphDispatchIndirectReadback");
		REQUIRE(readback != nullptr);

		rg.Begin(nullptr);
		rg.SetOutputAttachments("Output", {external}, ResourceState::CopySource);
		rg.AddComputePass("Fill", shader)
			.Write("Output")
			.DescriptorSet(0, descriptorSet)
			.DispatchIndirect(indirectBuffer);

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->ResourceBarrier(BufferBarrierDesc{
				.buffer = indirectBuffer,
				.oldState = ResourceState::Undefined,
				.newState = ResourceState::IndirectArgument
			});
			rg.Execute(cmd);
		});

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->CopyTextureToBuffer(BufferTextureCopy{
				.buffer = readback,
				.texture = external,
				.extent = Extent3D{kWidth, kHeight, 1}
			});
		});

		const f32* pixels = static_cast<const f32*>(readback->Map());
		REQUIRE(pixels != nullptr);

		auto checkFillColor = [&](u32 x, u32 y)
		{
			const f32* texel = pixels + (y * kWidth + x) * 4;
			CHECK(texel[0] == doctest::Approx(0.2f).epsilon(0.01));
			CHECK(texel[1] == doctest::Approx(0.4f).epsilon(0.01));
			CHECK(texel[2] == doctest::Approx(0.6f).epsilon(0.01));
			CHECK(texel[3] == doctest::Approx(1.0f));
		};

		checkFillColor(0, 0);
		checkFillColor(kWidth / 2, kHeight / 2);
		checkFillColor(kWidth - 1, kHeight - 1);
		readback->Unmap();

		descriptorSet->Destroy();
		indirectBuffer->Destroy();
		readback->Destroy();
		external->Destroy();
	}

	// Auto descriptor set: a compute pass's Read/Write/WriteRead resources are mapped onto the shader's
	// set-0 bindings in declaration order, with no manual DescriptorSet() and no Render() callback. Seed
	// fills "In" through its single storage-image binding; Step samples "In" (binding 0), takes the
	// graph's default sampler (binding 1, no resource consumed) and writes "Out" (binding 2), so the read
	// back Out is In + 0.25 on rgb. Ordering matters: a wrong dependency->binding mapping would sample the
	// output or write the input and the values would not match.
	TEST_CASE("Graphics::Vulkan::RenderGraphComputeAutoDescriptorSetFromDependencies")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID seedShader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/FillSolid.comp"));
		RID stepShader = GpuTest::CompileComputeShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/AliasChainStep.comp"));
		REQUIRE(seedShader);
		REQUIRE(stepShader);

		RenderGraph rg;
		rg.SetOutputSize(Extent{kWidth, kHeight});

		GPUBuffer* readback = CreateFloatReadbackBuffer("RenderGraphAutoDescriptorReadback");
		REQUIRE(readback != nullptr);

		constexpr u32 kFrameCount = 3;
		for (u32 frame = 0; frame < kFrameCount; ++frame)
		{
			rg.Begin(nullptr);

			rg.Create("In", RenderGraphTextureDesc{
				.format = Format::RGBA32_FLOAT,
				.extent = Extent{kWidth, kHeight},
				.usage = ResourceUsage::UnorderedAccess | ResourceUsage::ShaderResource
			});
			rg.Create("Out", RenderGraphTextureDesc{
				.format = Format::RGBA32_FLOAT,
				.extent = Extent{kWidth, kHeight},
				.usage = ResourceUsage::UnorderedAccess | ResourceUsage::CopySource
			});

			rg.AddComputePass("Seed", seedShader)
				.Write("In")
				.Dispatch(kWidth, kHeight, 1);

			rg.AddComputePass("Step", stepShader)
				.Read("In")
				.Write("Out")
				.Dispatch(kWidth, kHeight, 1);

			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				rg.Execute(cmd);
			});
		}

		ReadbackStorage(rg, "Out", readback, Extent{kWidth, kHeight});

		const f32* pixels = static_cast<const f32*>(readback->Map());
		REQUIRE(pixels != nullptr);

		auto checkPixel = [&](u32 x, u32 y)
		{
			const f32* texel = pixels + (y * kWidth + x) * 4;
			CHECK(texel[0] == doctest::Approx(0.45f).epsilon(0.01));
			CHECK(texel[1] == doctest::Approx(0.65f).epsilon(0.01));
			CHECK(texel[2] == doctest::Approx(0.85f).epsilon(0.01));
			CHECK(texel[3] == doctest::Approx(1.0f));
		};

		checkPixel(0, 0);
		checkPixel(kWidth / 2, kHeight / 2);
		checkPixel(kWidth - 1, kHeight - 1);

		readback->Unmap();
		readback->Destroy();
	}
}

#endif
