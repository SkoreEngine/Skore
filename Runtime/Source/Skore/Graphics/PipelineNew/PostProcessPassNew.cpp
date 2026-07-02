#include "PipelineCommonNew.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipelineNew.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/BloomComponent.hpp"

namespace Skore
{
	//Tonemap leaf: sharpen + bloom + ACES-tonemap the HDR forward color into the LDR display output.
	struct PostProcessPassNew : DefaultPipelinePassNew
	{
		SK_CLASS(PostProcessPassNew, DefaultPipelinePassNew);

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
			const bool bloomEnabled = scene != nullptr && scene->HasIterable<BloomComponent>() && BloomMipCountNew(outputSize) >= 2;

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
				.AddComputePass("PostProcess", "Skore://ShadersNew/PostProcessNew.comp")
				.Stage(RenderStage::PostProcess)
				.Write(PostProcessOutputName)
				.Read(ForwardColorName)
				.Read(bloomEnabled ? BloomTextureNewName : BloomFallbackName)
				.Constants<PostProcessPushConstants>([bloomIntensity](RenderGraph&, PostProcessPushConstants& constants)
				{
					constants.bloomIntensity = bloomIntensity;
				})
				.Dispatch(outputSize.width, outputSize.height, 1);

			//shader-less pass: its Read dependency makes the graph transition the output to
			//ShaderReadOnly so the viewport can sample it after Execute.
			renderGraph
				.AddComputePass("OutputColorToSampled", RID{})
				.Stage(RenderStage::UI)
				.Read(PostProcessOutputName);
		}
	};

	void RegisterPostProcessPassNew()
	{
		Reflection::Type<PostProcessPassNew>();
	}
}
