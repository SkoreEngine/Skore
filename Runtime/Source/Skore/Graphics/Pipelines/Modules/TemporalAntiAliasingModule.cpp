

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{

	struct TemporalAntiAliasingPass : RenderPipelinePass
	{
		SK_CLASS(TemporalAntiAliasingPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		GPUTexture* historyTextures[2] = {nullptr, nullptr};
		u8 previousHistoryTextureIndex = 0;
		u8 currentHistoryTextureIndex = 0;
		bool firstFrame = true;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::PostProcess;
			setup.requireJitter = true;
			setup.requireMotionVector = true;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "MotionVector", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ColorAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ResolvedHDR", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void DestroyTextures() const
		{
			for (int i = 0; i < 2; ++i)
			{
				if (historyTextures[i]) historyTextures[i]->Destroy();
			}
		}

		void CreateTextures()
		{
			DestroyTextures();

			for (int i = 0; i < 2; ++i)
			{
				historyTextures[i] = Graphics::CreateTexture(TextureDesc{
					.extent = context->GetOutputSize(),
					.format = TextureFormat::R16G16B16A16_FLOAT,
					.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess | ResourceUsage::CopySource,
					.debugName = "HistoryTexture_" + ToString(i)
				});

				Graphics::SetTextureState(historyTextures[i], ResourceState::Undefined, ResourceState::ShaderReadOnly);
			}

		}

		void Init() override
		{
			CreateTextures();

			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/TAA.shader"),
				.variant = "ResolveTemporal",
				.allowImmediateSet = true
			});
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			GPUTexture* texture = context->GetTexture("ColorAttachment");

			struct PushConstants
			{
				IVec2 resolution;
				f32   resetHistory;
				f32   pad;
			};

			PushConstants pc;
			pc.resolution = IVec2{static_cast<i32>(context->GetOutputSize().width), static_cast<i32>(context->GetOutputSize().height)};
			pc.resetHistory = firstFrame ? 1.0f : 0.0f;

			previousHistoryTextureIndex = currentHistoryTextureIndex;
			currentHistoryTextureIndex = (currentHistoryTextureIndex + 1) % 2;

			cmd->ResourceBarrier(historyTextures[currentHistoryTextureIndex], ResourceState::ShaderReadOnly, ResourceState::General, 0, 0);

			cmd->BindPipeline(pipeline);
			cmd->SetTexture(pipeline, 0, 0, historyTextures[currentHistoryTextureIndex], 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture("MotionVector"), 0);
			cmd->SetTexture(pipeline, 0, 2, historyTextures[previousHistoryTextureIndex], 0);
			cmd->SetTexture(pipeline, 0, 3, texture, 0);
			cmd->SetTexture(pipeline, 0, 4, context->GetTexture(OutputDepthName), 0);
			cmd->SetSampler(pipeline, 0, 5, Graphics::GetLinearSampler());
			cmd->SetSampler(pipeline, 0, 6, Graphics::GetNearestClampToEdgeSampler());

			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(PushConstants), &pc);

			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);

			firstFrame = false;
			context->SetTexture("HistoryTexture", historyTextures[currentHistoryTextureIndex]);
			cmd->ResourceBarrier(historyTextures[currentHistoryTextureIndex], ResourceState::General, ResourceState::CopySource, 0, 0);

			cmd->ResourceBarrier(texture, ResourceState::ShaderReadOnly, ResourceState::CopyDest, 0, 0);
			cmd->CopyTexture({
				.srcTexture = historyTextures[currentHistoryTextureIndex],
				.dstTexture = texture,
				.extent = Extent3D{context->GetOutputSize().width, context->GetOutputSize().height, 1},
			});
			cmd->ResourceBarrier(texture, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, 0);
			cmd->ResourceBarrier(historyTextures[currentHistoryTextureIndex], ResourceState::CopySource, ResourceState::ShaderReadOnly, 0, 0);
		}

		void OnResize(Extent size) override
		{
			CreateTextures();
			firstFrame = true;
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};


	struct TemporalAntiAliasingModule : RenderPipelineModule
	{
		SK_CLASS(TemporalAntiAliasingModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(TemporalAntiAliasingPass));
			return setup;
		}

		//enabled only when the project settings select TAA as the anti-aliasing method
		bool IsEnabled() override
		{
			RID settings = Settings::Get(TypeInfo<ProjectSettings>::ID(), sktypeid(DefaultRenderPipelineSettings));
			if (ResourceObject settingsObject = Resources::Read(settings))
			{
				return settingsObject.GetEnum<DefaultAntiAliasingMethod>(DefaultRenderPipelineSettings::AntiAliasingMethod) == DefaultAntiAliasingMethod::TAA;
			}
			return false;
		}
	};


	void RegisterTAARenderModule()
	{
		Reflection::Type<TemporalAntiAliasingPass>();
		Reflection::Type<TemporalAntiAliasingModule>();
	}
}
