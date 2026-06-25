#include "doctest.h"

#include "TestRenderDevice.hpp"

using namespace Skore;

namespace
{
	TEST_CASE("TestRenderDevice::TextureStartsUndefined")
	{
		TestRenderDevice device;

		TestGPUTexture* texture = static_cast<TestGPUTexture*>(device.CreateTexture(TextureDesc{
			.extent = {64, 64, 1},
			.mipLevels = 4,
			.arrayLayers = 2,
			.format = Format::RGBA8_UNORM,
		}));

		CHECK(texture->GetMipLevels() == 4);
		CHECK(texture->GetArrayLayers() == 2);
		CHECK(texture->AllSubresourcesInState(ResourceState::Undefined));
		CHECK(texture->GetSubresourceState(0, 0) == ResourceState::Undefined);
		CHECK(texture->GetSubresourceState(3, 1) == ResourceState::Undefined);
	}

	TEST_CASE("TestRenderDevice::BarrierTransitionsSingleSubresource")
	{
		TestRenderDevice device;

		TestGPUTexture* texture = static_cast<TestGPUTexture*>(device.CreateTexture(TextureDesc{
			.mipLevels = 3,
			.arrayLayers = 1,
			.format = Format::RGBA16_FLOAT,
		}));

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		cmd->ResourceBarrier(texture, ResourceState::Undefined, ResourceState::General, 1, 0);
		cmd->End();

		CHECK(texture->GetSubresourceState(0, 0) == ResourceState::Undefined);
		CHECK(texture->GetSubresourceState(1, 0) == ResourceState::General);
		CHECK(texture->GetSubresourceState(2, 0) == ResourceState::Undefined);
		CHECK(texture->mismatchCount == 0);
		CHECK(texture->barrierHistory.Size() == 1);
	}

	TEST_CASE("TestRenderDevice::BarrierTransitionsMipRange")
	{
		TestRenderDevice device;

		TestGPUTexture* texture = static_cast<TestGPUTexture*>(device.CreateTexture(TextureDesc{
			.mipLevels = 5,
			.arrayLayers = 1,
			.format = Format::RGBA8_UNORM,
		}));

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();

		// Transition every mip to ShaderReadOnly using the "all remaining levels" form.
		cmd->ResourceBarrier(texture, ResourceState::Undefined, ResourceState::ShaderReadOnly, 0, U32_MAX, 0, U32_MAX);
		CHECK(texture->AllSubresourcesInState(ResourceState::ShaderReadOnly));

		// Now move just mip 2 into CopyDest, leaving the others untouched.
		cmd->ResourceBarrier(texture, ResourceState::ShaderReadOnly, ResourceState::CopyDest, 2, 1, 0, 1);
		cmd->End();

		CHECK(texture->GetSubresourceState(1, 0) == ResourceState::ShaderReadOnly);
		CHECK(texture->GetSubresourceState(2, 0) == ResourceState::CopyDest);
		CHECK(texture->GetSubresourceState(3, 0) == ResourceState::ShaderReadOnly);
		CHECK(texture->mismatchCount == 0);
	}

	TEST_CASE("TestRenderDevice::BarrierDetectsStateMismatch")
	{
		TestRenderDevice device;

		TestGPUTexture* texture = static_cast<TestGPUTexture*>(device.CreateTexture(TextureDesc{
			.mipLevels = 1,
			.arrayLayers = 1,
			.format = Format::RGBA8_UNORM,
		}));

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		// Tracked state is Undefined, but the barrier declares it comes from ShaderReadOnly.
		cmd->ResourceBarrier(texture, ResourceState::ShaderReadOnly, ResourceState::General, 0, 0);
		cmd->End();

		CHECK(texture->mismatchCount == 1);
		CHECK(texture->GetSubresourceState(0, 0) == ResourceState::General);
	}

	TEST_CASE("TestRenderDevice::BufferStorageRoundTrips")
	{
		TestRenderDevice device;

		TestGPUBuffer* buffer = static_cast<TestGPUBuffer*>(device.CreateBuffer(BufferDesc{
			.size = 16,
			.usage = ResourceUsage::ConstantBuffer,
		}));

		CHECK(buffer->storage.Size() == 16);

		u32 values[2] = {7, 42};
		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		cmd->UpdateBuffer(buffer, 0, sizeof(values), values);
		cmd->End();

		const u32* mapped = static_cast<const u32*>(buffer->Map());
		CHECK(mapped[0] == 7);
		CHECK(mapped[1] == 42);
		buffer->Unmap();
	}

	TEST_CASE("TestRenderDevice::CommandStatsRecorded")
	{
		TestRenderDevice device;

		GPUCommandBuffer* cmd = device.CreateCommandBuffer(QueueType::Graphics);
		cmd->Begin();
		cmd->BeginRenderPass(BeginRenderPassInfo{});
		cmd->Draw(3, 1, 0, 0);
		cmd->Draw(3, 1, 0, 0);
		cmd->EndRenderPass();
		cmd->Dispatch(8, 8, 1);
		cmd->End();

		TestGPUCommandBuffer* testCmd = static_cast<TestGPUCommandBuffer*>(cmd);
		CHECK(testCmd->stats.renderPassCount == 1);
		CHECK(testCmd->stats.drawCount == 2);
		CHECK(testCmd->stats.dispatchCount == 1);
		CHECK(testCmd->renderPassActive == false);
	}
}
