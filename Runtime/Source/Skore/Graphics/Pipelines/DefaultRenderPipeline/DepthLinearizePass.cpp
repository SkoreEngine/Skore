#include "Skore/Core/Reflection.hpp"

#include <cmath>

#include "Skore/Graphics/Graphics.hpp"
#include "PipelineCommon.hpp"
#include "Skore/Resource/Resources.hpp"

#define A_CPU
#include "FidelityFX/ffx_a.h"
#include "FidelityFX/ffx_spd.h"

namespace Skore
{
	constexpr u32 MaxLinearDepthMips = 12;

	struct DepthLinearizePass : RenderPipelinePass
	{
		SK_CLASS(DepthLinearizePass, RenderPipelinePass);

		struct SpdConstants
		{
			u32 mips;
			u32 numWorkGroupsPerSlice;
			u32 workGroupOffset[2];
			f32 nearClip;
			f32 farClip;
			u32 inputSize[2];
		};

		GPUPipeline*      pipeline = nullptr;
		GPUBuffer*        atomicBuffer = nullptr;
		GPUTexture*       linearDepthTexture = nullptr;
		GPUTextureView*   mipViews[MaxLinearDepthMips + 1] = {};
		GPUDescriptorSet* descriptorSet = nullptr;
		u32               mipCount = 0;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::DepthLinearize;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void DestroyMipResources()
		{
			for (u32 i = 0; i < MaxLinearDepthMips + 1; ++i)
			{
				if (mipViews[i])
				{
					mipViews[i]->Destroy();
					mipViews[i] = nullptr;
				}
			}
			if (linearDepthTexture)
			{
				linearDepthTexture->Destroy();
				linearDepthTexture = nullptr;
			}
			if (atomicBuffer)
			{
				atomicBuffer->Destroy();
				atomicBuffer = nullptr;
			}
			if (descriptorSet)
			{
				descriptorSet->Destroy();
				descriptorSet = nullptr;
			}
		}

		void CreateMipResources()
		{
			DestroyMipResources();

			Extent outputSize = context->GetOutputSize();
			mipCount = static_cast<u32>(std::floor(std::log2(static_cast<f32>(Math::Max(outputSize.width, outputSize.height))))) + 1;
			mipCount = Math::Min(mipCount, MaxLinearDepthMips);

			if (mipCount < 2) return;

			linearDepthTexture = Graphics::CreateTexture(TextureDesc{
				.extent = {outputSize.width, outputSize.height, 1},
				.mipLevels = mipCount,
				.format = TextureFormat::R32_FLOAT,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
				.debugName = "LinearDepthMipChain"
			});

			atomicBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(u32) * 6,
				.usage = ResourceUsage::UnorderedAccess,
				.hostVisible = true,
				.debugName = "LinearDepthSPDAtomicBuffer"
			});

			VoidPtr mapped = atomicBuffer->Map();
			memset(mapped, 0, sizeof(u32) * 6);
			atomicBuffer->Unmap();

			RID shader = Resources::FindByPath("Skore://Shaders/FidelityFX/DepthLinearize_SPD.shader");
			descriptorSet = Graphics::CreateDescriptorSet(shader, "Default", 0);

			descriptorSet->UpdateTexture(0, context->GetTexture(OutputDepthName));
			descriptorSet->UpdateBuffer(3, atomicBuffer, 0, sizeof(u32) * 6);

			for (u32 mip = 0; mip < 13; ++mip)
			{
				TextureViewDesc viewDesc = {};
				viewDesc.type = TextureViewType::Type2DArray;
				viewDesc.texture = linearDepthTexture;
				viewDesc.baseMipLevel = Math::Min(mipCount - 1, mip);
				mipViews[mip] = Graphics::CreateTextureView(viewDesc);
				descriptorSet->UpdateTextureView(1, mipViews[mip], mip);
			}
			descriptorSet->UpdateTextureView(2, mipViews[6], 0);

			context->SetTexture(LinearDepthMipChainName, linearDepthTexture);
		}

		void Init() override
		{
			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/FidelityFX/DepthLinearize_SPD.shader"),
				.variant = "Default",
				.debugName = "DepthLinearizeSPDPipeline"
			});

			CreateMipResources();
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (mipCount < 2 || !linearDepthTexture) return;

			Extent outputSize = context->GetOutputSize();

			varAU2(dispatchThreadGroupCountXY);
			varAU2(workGroupOffset);
			varAU2(numWorkGroupsAndMips);
			varAU4(rectInfo) = initAU4(0, 0, outputSize.width, outputSize.height);
			SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

			SpdConstants pc;
			pc.numWorkGroupsPerSlice = numWorkGroupsAndMips[0];
			pc.mips = numWorkGroupsAndMips[1];
			pc.workGroupOffset[0] = workGroupOffset[0];
			pc.workGroupOffset[1] = workGroupOffset[1];
			pc.nearClip = context->camera.nearClip;
			pc.farClip = context->camera.farClip;
			pc.inputSize[0] = outputSize.width;
			pc.inputSize[1] = outputSize.height;

			// Reset atomic counter
			VoidPtr mapped = atomicBuffer->Map();
			memset(mapped, 0, sizeof(u32) * 6);
			atomicBuffer->Unmap();

			cmd->BindPipeline(pipeline);
			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(SpdConstants), &pc);
			cmd->BindDescriptorSet(pipeline, 0, descriptorSet, {});

			cmd->ResourceBarrier(linearDepthTexture, ResourceState::Undefined, ResourceState::General, 0, mipCount, 0, 1);
			cmd->Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1], 1);
			cmd->ResourceBarrier(linearDepthTexture, ResourceState::General, ResourceState::ShaderReadOnly, 0, mipCount, 0, 1);
		}

		void OnResize(Extent size) override
		{
			CreateMipResources();
		}

		void Destroy() override
		{
			DestroyMipResources();
			if (pipeline)
			{
				pipeline->Destroy();
				pipeline = nullptr;
			}
		}
	};

	void RegisterDepthLinearizePass()
	{
		Reflection::Type<DepthLinearizePass>();
	}
}
