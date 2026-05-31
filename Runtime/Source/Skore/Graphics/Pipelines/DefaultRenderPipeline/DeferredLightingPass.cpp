#include "DeferredLightingPass.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "PipelineCommon.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	RenderPipelinePassSetup DeferredLightingPass::GetPassSetup()
	{
		RenderPipelinePassSetup setup;
		setup.type = RenderPipelinePassType::Compute;
		setup.stage = PipelineRenderStage::Lighting;

		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});

		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Read});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferRoughnessAO", .access = RenderPipelineTextureAccess::Read});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Read});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferEmissive", .access = RenderPipelineTextureAccess::Read});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});

		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightAttachment", .access = RenderPipelineTextureAccess::Write});

		return setup;
	}

	void DeferredLightingPass::UpdateState()
	{
		lightAttachment = context->GetTexture("LightAttachment");

		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			gBufferDescriptorSets[f]->UpdateTexture(0, lightAttachment);
			gBufferDescriptorSets[f]->UpdateTexture(1, context->GetTexture("GBufferAlbedoMetallic"));
			gBufferDescriptorSets[f]->UpdateTexture(2, context->GetTexture("GBufferRoughnessAO"));
			gBufferDescriptorSets[f]->UpdateTexture(3, context->GetTexture("GBufferNormals"));
			gBufferDescriptorSets[f]->UpdateTexture(4, context->GetTexture("GBufferEmissive"));
			gBufferDescriptorSets[f]->UpdateTexture(5, context->GetTexture(LinearDepthMipChainName));
		}
	}

	void DeferredLightingPass::Init()
	{
		pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
			.shader = Resources::FindByPath("Skore://Shaders/DeferredLighting.comp"),
			.descriptorSetsOverride = {
				DescriptorSetOverride{
					.set = 1,
					.descriptorSet = context->GetSceneDescriptorSet(0)
				}
			}
		});

		// Per-pipeline (space2): only the GBuffer textures + output. Scene UBO and lights
		// come from the shared scene set (space1).
		DescriptorSetDesc gBufferDescSetDesc{
			.bindings = {
				DescriptorSetLayoutBinding{.binding = 0, .descriptorType = DescriptorType::StorageImage},
				DescriptorSetLayoutBinding{.binding = 1, .descriptorType = DescriptorType::SampledImage},
				DescriptorSetLayoutBinding{.binding = 2, .descriptorType = DescriptorType::SampledImage},
				DescriptorSetLayoutBinding{.binding = 3, .descriptorType = DescriptorType::SampledImage},
				DescriptorSetLayoutBinding{.binding = 4, .descriptorType = DescriptorType::SampledImage},
				DescriptorSetLayoutBinding{.binding = 5, .descriptorType = DescriptorType::SampledImage}
			}
		};

		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			gBufferDescriptorSets[f] = Graphics::CreateDescriptorSet(gBufferDescSetDesc);
		}

		UpdateState();
	}

	void DeferredLightingPass::Render(Scene* scene, GPUCommandBuffer* cmd)
	{
		lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

		u32 frame = context->GetCurrentFrame();
		cmd->BindPipeline(pipeline);
		cmd->BindDescriptorSet(pipeline, 0, gBufferDescriptorSets[frame]);
		cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet());
		cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
	}

	void DeferredLightingPass::OnResize(Extent size)
	{
		UpdateState();
	}

	void DeferredLightingPass::Destroy()
	{
		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			gBufferDescriptorSets[f]->Destroy();
		}
		pipeline->Destroy();
	}
}
