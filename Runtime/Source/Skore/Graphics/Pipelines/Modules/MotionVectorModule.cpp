#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	struct CameraMotionVectorPass : RenderPipelinePass
	{
		SK_CLASS(CameraMotionVectorPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::PostProcess;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "MotionVector", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});
			return setup;
		}

		void Init() override
		{
			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/ComputeCameraMotion.comp"),
				.allowImmediateSet = true
			});
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			struct PushConstants
			{
				Mat4  projectionNoJitter;
				Mat4  viewInverse;
				Mat4  previousViewProjectionNoJitter;
				IVec2 resolution;
				Vec2  pad;
			};

			PushConstants pc;
			pc.projectionNoJitter = context->camera.projectionNoJitter;
			pc.viewInverse = context->camera.invView;
			pc.previousViewProjectionNoJitter = context->camera.previousViewProjectionNoJitter;
			pc.resolution = IVec2{static_cast<i32>(context->GetOutputSize().width), static_cast<i32>(context->GetOutputSize().height)};
			pc.pad = Vec2(0.0f);

			cmd->BindPipeline(pipeline);
			cmd->SetTexture(pipeline, 0, 0, context->GetTexture("MotionVector"), 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture(LinearDepthMipChainName), 0);
			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(PushConstants), &pc);

			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};

	struct MotionVectorModule : RenderPipelineModule
	{
		SK_CLASS(MotionVectorModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(CameraMotionVectorPass));
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{.name = "MotionVector", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16_FLOAT, .textureUsage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess});
			return resources;
		}

		bool IsEnabled() override
		{
			return context->IsMotionVectorRequired();
		}
	};

	void RegisterMotionVectorModule()
	{
		Reflection::Type<CameraMotionVectorPass>();
		Reflection::Type<MotionVectorModule>();
	}
}
