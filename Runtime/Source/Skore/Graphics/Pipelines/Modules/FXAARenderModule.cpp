#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Resource/Resources.hpp"

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
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::Read});
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
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture(OutputColorName), 0);
			cmd->SetSampler(pipeline, 0, 2, Graphics::GetLinearClampToEdgeSampler());

			FXAAConstants pc;
			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(FXAAConstants), &pc);
			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};

	struct FXAAResolvePass : RenderPipelinePass
	{
		SK_CLASS(FXAAResolvePass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = PipelineRenderStage::PostProcess + 1;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::ReadWrite});
			return setup;
		}

		void Init() override
		{
			pipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/FullscreenTexture.raster"),
				.depthStencilState = {
					.depthTestEnable = false,
				},
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = renderPass,
				.allowImmediateSet = true
			});
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			cmd->BindPipeline(pipeline);
			cmd->SetTexture(pipeline, 0, 0, context->GetTexture(FXAAOutputName), 0);
			cmd->SetSampler(pipeline, 0, 1, Graphics::GetLinearClampToEdgeSampler());
			cmd->Draw(3, 1, 0, 0);
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
			setup.passes.EmplaceBack(sktypeid(FXAAResolvePass));
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{
				.name = FXAAOutputName,
				.type = RenderPipelineResourceType::Texture,
				.format = TextureFormat::R8G8B8A8_UNORM,
				.textureUsage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess
			});
			return resources;
		}

		bool IsEnabled() override
		{
			RID settings = Settings::Get(TypeInfo<ProjectSettings>::ID(), sktypeid(DefaultRenderPipelineSettings));
			if (ResourceObject settingsObject = Resources::Read(settings))
			{
				return settingsObject.GetEnum<DefaultAntiAliasingMethod>(DefaultRenderPipelineSettings::AntiAliasingMethod) == DefaultAntiAliasingMethod::FXAA;
			}
			return false;
		}
	};

	void RegisterFXAARenderModule()
	{
		Reflection::Type<FXAARenderPass>();
		Reflection::Type<FXAAResolvePass>();
		Reflection::Type<FXAARenderModule>();
	}
}
