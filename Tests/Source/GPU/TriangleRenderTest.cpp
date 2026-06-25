#ifdef SK_GPU_TESTS

#include "doctest.h"

#include "GpuTestEnvironment.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/IO/Path.hpp"

using namespace Skore;

namespace
{
	constexpr u32 kWidth = 64;
	constexpr u32 kHeight = 64;

	TEST_CASE("Graphics::Vulkan::RenderTriangleToTexture")
	{
		if (!GpuTest::IsAvailable())
		{
			MESSAGE("Vulkan device not available - skipping GPU test");
			return;
		}

		GpuTest::ResourceScope resourceScope;

		RID shader = GpuTest::CompileGraphicsShader(Path::Join(SK_EDITOR_TEST_FILES, "Shaders/Triangle.raster"));
		REQUIRE(shader);

		GPUTexture* target = Graphics::CreateTexture(TextureDesc{
			.extent = {kWidth, kHeight, 1},
			.format = Format::RGBA8_UNORM,
			.usage = ResourceUsage::RenderTarget | ResourceUsage::CopySource,
			.debugName = "TriangleTarget"
		});
		REQUIRE(target != nullptr);

		GPURenderPass* renderPass = Graphics::CreateRenderPass(RenderPassDesc{
			.attachments = {
				AttachmentDesc{
					.initialState = ResourceState::Undefined,
					.finalState = ResourceState::ColorAttachment,
					.loadOp = AttachmentLoadOp::Clear,
					.storeOp = AttachmentStoreOp::Store,
					.format = Format::RGBA8_UNORM,
				}
			},
			.debugName = "TrianglePass"
		});
		REQUIRE(renderPass != nullptr);

		GPUFramebuffer* framebuffer = Graphics::CreateFramebuffer(FramebufferDesc{
			.renderPass = renderPass,
			.attachments = {target->GetTextureView()},
			.debugName = "TriangleFramebuffer"
		});
		REQUIRE(framebuffer != nullptr);

		GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.shader = shader;
		pipelineDesc.variant = "Default";
		pipelineDesc.topology = PrimitiveTopology::TriangleList;
		pipelineDesc.renderPass = renderPass;
		pipelineDesc.rasterizerState.cullMode = CullMode::None;
		pipelineDesc.depthStencilState.depthTestEnable = false;
		pipelineDesc.depthStencilState.depthWriteEnable = false;
		pipelineDesc.blendStates = {BlendStateDesc{}};
		pipelineDesc.debugName = "TrianglePipeline";

		GPUPipeline* pipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
		REQUIRE(pipeline != nullptr);

		GPUBuffer* readback = Graphics::CreateBuffer(BufferDesc{
			.size = kWidth * kHeight * 4,
			.usage = ResourceUsage::CopyDest,
			.hostVisible = true,
			.persistentMapped = true,
			.debugName = "TriangleReadback"
		});
		REQUIRE(readback != nullptr);

		ClearValues clear;
		clear.color = Vec4(0.0f, 0.0f, 0.0f, 1.0f);

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->BeginRenderPass(BeginRenderPassInfo{
				.renderPass = renderPass,
				.framebuffer = framebuffer,
				.clearValues = &clear
			});
			cmd->SetViewport(ViewportInfo{
				.x = 0.0f,
				.y = 0.0f,
				.width = static_cast<f32>(kWidth),
				.height = static_cast<f32>(kHeight),
				.minDepth = 0.0f,
				.maxDepth = 1.0f
			});
			cmd->SetScissor(Vec2(0, 0), Extent{kWidth, kHeight});
			cmd->BindPipeline(pipeline);
			cmd->Draw(3, 1, 0, 0);
			cmd->EndRenderPass();

			cmd->ResourceBarrier(TextureBarrierDesc{
				.texture = target,
				.oldState = ResourceState::ColorAttachment,
				.newState = ResourceState::CopySource
			});

			cmd->CopyTextureToBuffer(BufferTextureCopy{
				.buffer = readback,
				.texture = target,
				.extent = Extent3D{kWidth, kHeight, 1}
			});
		});

		const u8* pixels = static_cast<const u8*>(readback->Map());
		REQUIRE(pixels != nullptr);

		auto pixelAt = [&](u32 x, u32 y) -> const u8* {
			return pixels + (y * kWidth + x) * 4;
		};

		// Center of the target is inside the triangle: expect solid red.
		const u8* center = pixelAt(kWidth / 2, kHeight / 2);
		CHECK(center[0] > 200); // R
		CHECK(center[1] < 64);  // G
		CHECK(center[2] < 64);  // B
		CHECK(center[3] > 200); // A

		// Top-left corner is outside the triangle: expect the clear color (opaque black).
		const u8* corner = pixelAt(0, 0);
		CHECK(corner[0] < 16);
		CHECK(corner[1] < 16);
		CHECK(corner[2] < 16);

		readback->Unmap();

		readback->Destroy();
		pipeline->Destroy();
		framebuffer->Destroy();
		renderPass->Destroy();
		target->Destroy();
	}
}

#endif
