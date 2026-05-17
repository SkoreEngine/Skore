
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/PipelineCommon.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::BloomModule");
	}

	constexpr const char* BloomTextureName = "BloomTexture";
	constexpr u32         MaxBloomMips = 7;

	struct BloomPass : RenderPipelinePass
	{
		SK_CLASS(BloomPass, RenderPipelinePass);

		GPUPipeline* prefilterPipeline = nullptr;
		GPUPipeline* downsamplePipeline = nullptr;
		GPUPipeline* upsamplePipeline = nullptr;

		GPUTexture*     bloomMipChain = nullptr;
		GPUTextureView* mipViews[MaxBloomMips] = {};
		u32             mipCount = 0;

		f32 threshold = 1.0f;
		f32 softKnee = 0.5f;
		f32 bloomRadius = 1.0f;

		struct BloomPushConstants
		{
			Vec2 texelSize;
			f32  threshold;
			f32  softKnee;
			f32  bloomRadius;
			f32  pad0;
			f32  pad1;
			f32  pad2;
		};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = DefaultPipelineRenderStage::PostProcess;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = BloomTextureName, .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void DestroyMipChain()
		{
			for (u32 i = 0; i < MaxBloomMips; ++i)
			{
				if (mipViews[i])
				{
					mipViews[i]->Destroy();
					mipViews[i] = nullptr;
				}
			}
			if (bloomMipChain)
			{
				bloomMipChain->Destroy();
				bloomMipChain = nullptr;
			}
		}

		void CreateMipChain()
		{
			DestroyMipChain();

			Extent outputSize = context->GetOutputSize();
			u32 mipWidth = outputSize.width / 2;
			u32 mipHeight = outputSize.height / 2;

			mipCount = static_cast<u32>(std::floor(std::log2(static_cast<f32>(Math::Min(mipWidth, mipHeight)))));
			mipCount = Math::Min(mipCount, MaxBloomMips);

			if (mipCount < 2)
			{
				logger.Warn("Output resolution too small for bloom");
				mipCount = 0;
				return;
			}

			bloomMipChain = Graphics::CreateTexture(TextureDesc{
				.extent = {mipWidth, mipHeight, 1},
				.mipLevels = mipCount,
				.format = TextureFormat::R11G11B10_FLOAT,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
				.debugName = "BloomMipChain"
			});

			for (u32 i = 0; i < mipCount; ++i)
			{
				mipViews[i] = Graphics::CreateTextureView(TextureViewDesc{
					.texture = bloomMipChain,
					.type = TextureViewType::Type2D,
					.baseMipLevel = i,
					.mipLevelCount = 1,
					.debugName = "BloomMip_" + ToString(i)
				});
			}

			// Initialize all mips to ShaderReadOnly
			for (u32 i = 0; i < mipCount; ++i)
			{
				Graphics::SetTextureState(bloomMipChain, ResourceState::Undefined, ResourceState::ShaderReadOnly);
			}
		}

		void Init() override
		{
			RID bloomShader = Resources::FindByPath("Skore://Shaders/Bloom.shader");

			prefilterPipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = bloomShader,
				.variant = "BloomPrefilter",
				.allowImmediateSet = true
			});

			downsamplePipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = bloomShader,
				.variant = "BloomDownsample",
				.allowImmediateSet = true
			});

			upsamplePipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = bloomShader,
				.variant = "BloomUpsample",
				.allowImmediateSet = true
			});

			CreateMipChain();
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (mipCount < 2) return;

			GPUTexture* lightAttachment = context->GetTexture("LightAttachment");
			Extent outputSize = context->GetOutputSize();

			BloomPushConstants pc;
			pc.threshold = threshold;
			pc.softKnee = softKnee;
			pc.bloomRadius = bloomRadius;

			// ----------------------------------------------------------------
			// Prefilter: full-res LightAttachment → half-res mip 0
			// ----------------------------------------------------------------
			{
				u32 mipWidth = outputSize.width / 2;
				u32 mipHeight = outputSize.height / 2;

				pc.texelSize = Vec2(1.0f / static_cast<f32>(outputSize.width), 1.0f / static_cast<f32>(outputSize.height));

				cmd->ResourceBarrier(bloomMipChain, ResourceState::ShaderReadOnly, ResourceState::General, 0, 0);

				cmd->BindPipeline(prefilterPipeline);
				cmd->SetTexture(prefilterPipeline, 0, 0, lightAttachment, 0);
				cmd->SetSampler(prefilterPipeline, 0, 1, Graphics::GetLinearClampToEdgeSampler());
				cmd->SetTextureView(prefilterPipeline, 0, 2, mipViews[0], 0);
				//cmd->SetTexture(prefilterPipeline, 0, 3, lightAttachment, 0); // unused in prefilter but must be bound
				cmd->PushConstants(prefilterPipeline, ShaderStage::Compute, 0, sizeof(BloomPushConstants), &pc);
				cmd->Dispatch((mipWidth + 7) / 8, (mipHeight + 7) / 8, 1);
			}

			// ----------------------------------------------------------------
			// Downsample chain: mip 0 → mip 1 → ... → mip (N-1)
			// ----------------------------------------------------------------
			for (u32 i = 1; i < mipCount; ++i)
			{
				u32 srcWidth = Math::Max(1u, (outputSize.width / 2) >> (i - 1));
				u32 srcHeight = Math::Max(1u, (outputSize.height / 2) >> (i - 1));
				u32 dstWidth = Math::Max(1u, (outputSize.width / 2) >> i);
				u32 dstHeight = Math::Max(1u, (outputSize.height / 2) >> i);

				pc.texelSize = Vec2(1.0f / static_cast<f32>(srcWidth), 1.0f / static_cast<f32>(srcHeight));

				cmd->ResourceBarrier(bloomMipChain, ResourceState::General, ResourceState::ShaderReadOnly, i - 1, 0);
				cmd->ResourceBarrier(bloomMipChain, ResourceState::ShaderReadOnly, ResourceState::General, i, 0);

				cmd->BindPipeline(downsamplePipeline);
				cmd->SetTextureView(downsamplePipeline, 0, 0, mipViews[i - 1], 0);
				cmd->SetSampler(downsamplePipeline, 0, 1, Graphics::GetLinearClampToEdgeSampler());
				cmd->SetTextureView(downsamplePipeline, 0, 2, mipViews[i], 0);
				//cmd->SetTextureView(downsamplePipeline, 0, 3, mipViews[i - 1], 0); // unused but must be bound
				cmd->PushConstants(downsamplePipeline, ShaderStage::Compute, 0, sizeof(BloomPushConstants), &pc);
				cmd->Dispatch((dstWidth + 7) / 8, (dstHeight + 7) / 8, 1);
			}

			// ----------------------------------------------------------------
			// Upsample chain: mip (N-1) → mip (N-2) → ... → mip 0
			// ----------------------------------------------------------------
			for (i32 i = static_cast<i32>(mipCount) - 2; i >= 0; --i)
			{
				u32 srcMip = static_cast<u32>(i + 1);
				u32 dstMip = static_cast<u32>(i);

				u32 srcWidth = Math::Max(1u, (outputSize.width / 2) >> srcMip);
				u32 srcHeight = Math::Max(1u, (outputSize.height / 2) >> srcMip);
				u32 dstWidth = Math::Max(1u, (outputSize.width / 2) >> dstMip);
				u32 dstHeight = Math::Max(1u, (outputSize.height / 2) >> dstMip);

				pc.texelSize = Vec2(1.0f / static_cast<f32>(srcWidth), 1.0f / static_cast<f32>(srcHeight));

				cmd->ResourceBarrier(bloomMipChain, ResourceState::General, ResourceState::ShaderReadOnly, srcMip, 0);
				if (i < static_cast<i32>(mipCount) - 2)
				{
					// dstMip was left in ShaderReadOnly from downsample; transition to General for write
					cmd->ResourceBarrier(bloomMipChain, ResourceState::ShaderReadOnly, ResourceState::General, dstMip, 0);
				}
				else
				{
					// Last downsample mip (N-1) was just transitioned to ShaderReadOnly above
					// dstMip (N-2) is still in ShaderReadOnly from downsample loop
					cmd->ResourceBarrier(bloomMipChain, ResourceState::ShaderReadOnly, ResourceState::General, dstMip, 0);
				}

				cmd->BindPipeline(upsamplePipeline);
				cmd->SetTextureView(upsamplePipeline, 0, 0, mipViews[srcMip], 0);
				cmd->SetSampler(upsamplePipeline, 0, 1, Graphics::GetLinearClampToEdgeSampler());
				cmd->SetTextureView(upsamplePipeline, 0, 2, mipViews[dstMip], 0);
				cmd->PushConstants(upsamplePipeline, ShaderStage::Compute, 0, sizeof(BloomPushConstants), &pc);
				cmd->Dispatch((dstWidth + 7) / 8, (dstHeight + 7) / 8, 1);
			}

			// Final barrier: mip 0 → ShaderReadOnly so composite can sample it
			cmd->ResourceBarrier(bloomMipChain, ResourceState::General, ResourceState::ShaderReadOnly, 0, 0);

			context->SetTexture(BloomTextureName, bloomMipChain);
		}

		void OnResize(Extent size) override
		{
			CreateMipChain();
		}

		void Destroy() override
		{
			prefilterPipeline->Destroy();
			downsamplePipeline->Destroy();
			upsamplePipeline->Destroy();
			DestroyMipChain();
		}
	};


	struct BloomModule : RenderPipelineModule
	{
		SK_CLASS(BloomModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(BloomPass));
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{
				.name = BloomTextureName,
				.type = RenderPipelineResourceType::None
			});
			return resources;
		}
	};


	void RegisterBloomModule()
	{
		Reflection::Type<BloomPass>();
		Reflection::Type<BloomModule>();
	}
}
