#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "PipelineCommon.hpp"
#include "Skore/Graphics/Pipelines/Modules/XeGTAORenderModule.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	struct CompositePass : RenderPipelinePass
	{
		SK_CLASS(CompositePass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::Composite;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferRoughnessAO", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ReflectionAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "IrradianceVolumeAttachment", .access = RenderPipelineTextureAccess::Read});

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ColorAttachment", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void Init() override
		{
			lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/DefaultComposite.comp"),
				.variant = "Default",
				.allowImmediateSet = true
			});
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			struct DefaultCompositePushConstants
			{
				Vec3  ambientLight;
				float ambientMultiplier;

				Vec2  outputSize;
				u32   flags;
				float farClip;
			};

			DefaultCompositePushConstants pc;

			pc.ambientLight = lightInstanceData->ambientLight;
			pc.ambientMultiplier = lightInstanceData->ambientMultiplier;
			pc.outputSize = {static_cast<f32>(context->GetOutputSize().width), static_cast<f32>(context->GetOutputSize().height)};
			pc.flags = lightInstanceData->indirectLightFlags;
			pc.farClip = context->camera.farClip;

			GPUTexture* ssaoTexture = context->GetTexture(XeGTAOOutputName);
			if (ssaoTexture != nullptr)
			{
				pc.flags |= LightFlags::HasSSAOTexture;
			}
			else
			{
				ssaoTexture = Graphics::GetWhiteTexture();
			}

			GPUTexture* reflectionTexture = context->GetTexture("ReflectionAttachment");
			if (reflectionTexture != nullptr)
			{
				pc.flags |= LightFlags::HasReflectionTexture;
			}
			else
			{
				reflectionTexture = Graphics::GetWhiteTexture();
			}

			cmd->BindPipeline(pipeline);

			cmd->SetTexture(pipeline, 0, 0, context->GetTexture("ColorAttachment"), 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture("LightAttachment"), 0);
			cmd->SetTexture(pipeline, 0, 2, context->GetTexture("GBufferAlbedoMetallic"), 0);
			cmd->SetTexture(pipeline, 0, 3, context->GetTexture("GBufferRoughnessAO"), 0);
			cmd->SetTexture(pipeline, 0, 4, context->GetTexture("GBufferNormals"), 0);
			cmd->SetTexture(pipeline, 0, 5, context->GetTexture(OutputDepthName), 0);
			cmd->SetTexture(pipeline, 0, 6, lightInstanceData->diffuseIrradianceTexture, 0);
			cmd->SetTexture(pipeline, 0, 7, ssaoTexture, 0);
			cmd->SetTexture(pipeline, 0, 8, reflectionTexture, 0);
			cmd->SetSampler(pipeline, 0, 9, Graphics::GetLinearSampler());

			GPUTexture* indirectDiffuseTexture = context->GetTexture("IrradianceVolumeAttachment");
			if (indirectDiffuseTexture == nullptr)
			{
				indirectDiffuseTexture = Graphics::GetWhiteTexture();
			}
			cmd->SetTexture(pipeline, 0, 10, indirectDiffuseTexture, 0);

			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(DefaultCompositePushConstants), &pc);
			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};

	void RegisterCompositePass()
	{
		Reflection::Type<CompositePass>();
	}
}
