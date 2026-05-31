#include "ReflectionPass.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "PipelineCommon.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	RenderPipelinePassSetup ReflectionPass::GetPassSetup()
	{
		RenderPipelinePassSetup setup;
		setup.type = RenderPipelinePassType::Compute;
		setup.stage = PipelineRenderStage::Indirect;

		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ColorAttachment", .access = RenderPipelineTextureAccess::Read});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Read});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferRoughnessAO", .access = RenderPipelineTextureAccess::Read});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Read});
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});

		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ReflectionAttachment", .access = RenderPipelineTextureAccess::Write});
		return setup;
	}

	void ReflectionPass::Init()
	{
		brdfLUT.Init({512, 512});
		lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

		RID shader = Resources::FindByPath("Skore://Shaders/ReflectionPass.comp");

		pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
			.shader = shader
		});

		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			descriptorSet[f] = Graphics::CreateDescriptorSet(shader, "Default", 0);
		}
	}

	void ReflectionPass::Render(Scene* scene, GPUCommandBuffer* cmd)
	{
		if (scene->renderObjects.tlas == nullptr) return;

		struct ReflectionPushConstants
		{
			Vec3  cameraPosition;
			float reflectionMultiplier;

			Vec2  outputSize;
			float farClip;
			u32   flags;

			Mat4 proj;
			Mat4 view;
			Mat4 viewProj;
			Mat4 invView;

			int   maxIterations; // e.g. 64  — max total cells crossed
			int   maxMipLevel;   // e.g. 6   — highest (coarsest) mip to use
			float thickness;     // e.g. 0.1 — depth tolerance for a hit
			float rayBias;       // e.g. 0.001
			float nearClip;
		};

		ReflectionPushConstants pc;
		pc.cameraPosition = context->camera.cameraPosition;
		pc.reflectionMultiplier = lightInstanceData->reflectionMultiplier;
		pc.outputSize = {static_cast<f32>(context->GetOutputSize().width), static_cast<f32>(context->GetOutputSize().height)};
		pc.farClip = context->camera.farClip;
		pc.nearClip = context->camera.nearClip;
		pc.flags = lightInstanceData->indirectLightFlags;
		pc.proj = context->camera.projection;
		pc.view = context->camera.view;
		pc.viewProj = context->camera.previousViewProjection;
		pc.invView = context->camera.invView;

		////TODO - WIP
		pc.flags |= LightFlags::None;
		pc.maxIterations = 0;
		pc.thickness = 10.0;
		pc.rayBias = 0.01;

		GPUDescriptorSet* set = descriptorSet[context->GetCurrentFrame()];

		set->UpdateTexture(0, context->GetTexture("ReflectionAttachment"));
		set->UpdateTexture(1, context->GetPrevTexture("ColorAttachment"));
		set->UpdateTexture(2, context->GetTexture("GBufferAlbedoMetallic"));
		set->UpdateTexture(3, context->GetTexture("GBufferRoughnessAO"));
		set->UpdateTexture(4, context->GetTexture("GBufferNormals"));
		set->UpdateTexture(5, context->GetTexture(LinearDepthMipChainName));
		set->UpdateTexture(6, lightInstanceData->specularMapTexture);
		set->UpdateTexture(7, brdfLUT.GetTexture());
		set->UpdateSampler(8, brdfLUT.GetSampler());
		set->UpdateSampler(9, Graphics::GetLinearSampler());
		set->UpdateSampler(10, Graphics::GetNearestClampToEdgeSampler());
		set->Update(DescriptorUpdate{.type = DescriptorType::AccelerationStructure, .binding = 11, .topLevelAS = scene->renderObjects.tlas});

		cmd->BindPipeline(pipeline);
		cmd->BindDescriptorSet(pipeline, 0, set);

		cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(ReflectionPushConstants), &pc);
		cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
	}

	void ReflectionPass::Destroy()
	{
		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			descriptorSet[f]->Destroy();
		}
		pipeline->Destroy();
		brdfLUT.Destroy();
	}
}
