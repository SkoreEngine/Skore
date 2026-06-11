#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "PipelineCommon.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/BloomComponent.hpp"

namespace Skore
{
	struct PostProcessPass : RenderPipelinePass
	{
		SK_CLASS(PostProcessPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		struct PostProcessPushConstants
		{
			f32 bloomIntensity;
		};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::PostProcess;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ColorAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ResolvedHDR", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "BloomTexture", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void Init() override
		{
			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/PostProcess.comp"),
				.allowImmediateSet = true
			});
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			PostProcessPushConstants pc;
			pc.bloomIntensity = 0.0f;

			scene->Iterate<BloomComponent>([&](BloomComponent* bloom)
			{
				pc.bloomIntensity = bloom->GetIntensity();
			});
			GPUTexture* bloomTexture = context->GetTexture("BloomTexture");

			cmd->BindPipeline(pipeline);
			cmd->SetTexture(pipeline, 0, 0, context->GetTexture(OutputColorName), 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture("ColorAttachment"), 0);
			cmd->SetSampler(pipeline, 0, 2, Graphics::GetNearestClampToEdgeSampler());
			cmd->SetTexture(pipeline, 0, 3, bloomTexture ? bloomTexture : Graphics::GetWhiteTexture(), 0);
			cmd->SetSampler(pipeline, 0, 4, Graphics::GetLinearClampToEdgeSampler());

			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(PostProcessPushConstants), &pc);
			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};

	void RegisterPostProcessPass()
	{
		Reflection::Type<PostProcessPass>();
	}
}
