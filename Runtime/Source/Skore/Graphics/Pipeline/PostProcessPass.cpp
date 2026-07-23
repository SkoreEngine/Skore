#include "PipelineCommon.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/BloomComponent.hpp"

namespace Skore
{
	struct PostProcessPass : DefaultPipelinePass
	{
		SK_CLASS(PostProcessPass, DefaultPipelinePass);

		struct PostProcessPushConstants
		{
			f32 bloomIntensity;
		};

		void BuildRenderGraph(RenderGraph& renderGraph) override
		{
			renderGraph.Create(PostProcessOutputName, RenderGraphTextureDesc{
				.format = Format::RGBA8_UNORM,
				.extent = Extent{0, 0},
				.usage = ResourceUsage::ShaderResource
			});

			renderGraph.SetColorOutput(PostProcessOutputName);

			Extent outputSize = renderGraph.GetOutputSize();

			Scene*     scene = renderGraph.GetScene();
			const bool bloomEnabled = scene != nullptr && scene->HasIterable<BloomComponent>() && BloomMipCount(outputSize) >= 2;

			f32 bloomIntensity = 0.0f;
			if (bloomEnabled)
			{
				scene->Iterate<BloomComponent>([&](BloomComponent* bloom)
				{
					bloomIntensity = bloom->GetIntensity();
				});
			}
			else
			{
				renderGraph.Import(BloomFallbackName, {Graphics::GetWhiteTexture()}, ResourceState::ShaderReadOnly);
			}

			renderGraph
				.AddComputePass("PostProcess", "Skore://Shaders/PostProcess.comp")
				.Stage(RenderStage::PostProcess)
				.Write(PostProcessOutputName)
				.Read(ForwardColorName)
				.Read(bloomEnabled ? BloomTextureName : BloomFallbackName)
				.Constants<PostProcessPushConstants>([bloomIntensity](RenderGraph&, PostProcessPushConstants& constants)
				{
					constants.bloomIntensity = bloomIntensity;
				})
				.Dispatch(outputSize.width, outputSize.height, 1);

			//runs after the UI stage so overlays (RmlUi, profiler, editor tools) draw before
			//the output is transitioned for sampling
			renderGraph
				.AddComputePass("OutputColorToSampled", RID{})
				.Stage(RenderStage::Swapchain)
				.Read(PostProcessOutputName);
		}
	};

	void RegisterPostProcessPass()
	{
		Reflection::Type<PostProcessPass>();
	}
}
