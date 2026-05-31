#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"

namespace Skore
{
	constexpr const char* FXAAOutputName = "FXAAOutput";

	struct FXAAConstants
	{
		f32 subPix = 0.75f;
		f32 edgeThreshold = 0.063f;
	};

	struct FXAARenderPass : RenderPipelinePass
	{
		SK_CLASS(FXAARenderPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::PostProcess;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ColorAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = FXAAOutputName, .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void Init() override
		{
			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/fxaa/FXAA.comp"),
				.allowImmediateSet = true
			});
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			cmd->BindPipeline(pipeline);
			cmd->SetTexture(pipeline, 0, 0, context->GetTexture(FXAAOutputName), 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture("ColorAttachment"), 0);
			cmd->SetSampler(pipeline, 0, 2, Graphics::GetLinearClampToEdgeSampler());

			FXAAConstants pc;
			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(FXAAConstants), &pc);

			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);

			// Copy FXAA result back to the output color attachment
			GPUTexture* fxaaOutput = context->GetTexture(FXAAOutputName);
			GPUTexture* colorOutput = context->GetTexture("ColorAttachment");

			cmd->ResourceBarrier(fxaaOutput, ResourceState::General, ResourceState::CopySource, 0, 0);
			cmd->ResourceBarrier(colorOutput, ResourceState::ShaderReadOnly, ResourceState::CopyDest, 0, 0);
			cmd->CopyTexture({
				.srcTexture = fxaaOutput,
				.dstTexture = colorOutput,
				.extent = Extent3D{context->GetOutputSize().width, context->GetOutputSize().height, 1},
			});
			cmd->ResourceBarrier(colorOutput, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, 0);
			cmd->ResourceBarrier(fxaaOutput, ResourceState::CopySource, ResourceState::General, 0, 0);
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};

	struct FXAARenderModule : RenderPipelineModule
	{
		SK_CLASS(FXAARenderModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(FXAARenderPass));
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{
				.name = FXAAOutputName,
				.type = RenderPipelineResourceType::Texture,
				.format = TextureFormat::R16G16B16A16_FLOAT,
				.textureUsage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess | ResourceUsage::CopySource
			});
			return resources;
		}
	};

	void RegisterFXAARenderModule()
	{
		Reflection::Type<FXAARenderPass>();
		Reflection::Type<FXAARenderModule>();
	}
}
