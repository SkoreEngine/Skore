#include "PipelineCommon.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/BloomComponent.hpp"

namespace Skore
{
	struct BloomPass : DefaultPipelinePass
	{
		SK_CLASS(BloomPass, DefaultPipelinePass);

		struct BloomPushConstants
		{
			Vec2 texelSize;
			f32  threshold;
			f32  softKnee;
			f32  bloomRadius;
			f32  pad0;
			f32  pad1;
			f32  pad2;
		};

		void BuildRenderGraph(RenderGraph& renderGraph) override
		{
			Scene* scene = renderGraph.GetScene();
			if (scene == nullptr || !scene->HasIterable<BloomComponent>()) return;

			Extent outputSize = renderGraph.GetOutputSize();
			u32    halfWidth = Math::Max(outputSize.width / 2, 1u);
			u32    halfHeight = Math::Max(outputSize.height / 2, 1u);

			u32 mipCount = BloomMipCount(outputSize);
			if (mipCount < 2) return;

			f32 threshold = 1.0f;
			f32 softKnee = 0.5f;
			f32 bloomRadius = 1.0f;

			scene->Iterate<BloomComponent>([&](BloomComponent* bloom)
			{
				threshold = bloom->GetThreshold();
				softKnee = bloom->GetSoftKnee();
				bloomRadius = bloom->GetRadius();
			});

			renderGraph.Create(BloomTextureName, RenderGraphTextureDesc{
				.format = Format::RG11B10_FLOAT,
				.extent = Extent{0, 0},
				.scale = Vec2(0.5f),
				.mipLevels = mipCount,
				.usage = ResourceUsage::ShaderResource
			});

			String mipNames[MaxBloomMips];
			for (u32 m = 0; m < mipCount; ++m)
			{
				mipNames[m] = String("BloomMip_") + ToString(m);
				renderGraph.CreateView(mipNames[m], RenderGraphViewDesc{
					.texture = BloomTextureName,
					.viewType = TextureViewType::Type2D,
					.baseMipLevel = m,
					.mipLevelCount = 1
				});
			}

			auto mipExtent = [&](u32 mip) -> Extent
			{
				return Extent{Math::Max(halfWidth >> mip, 1u), Math::Max(halfHeight >> mip, 1u)};
			};

			{
				BloomPushConstants pc;
				pc.texelSize = Vec2(1.0f / static_cast<f32>(outputSize.width), 1.0f / static_cast<f32>(outputSize.height));
				pc.threshold = threshold;
				pc.softKnee = softKnee;
				pc.bloomRadius = bloomRadius;

				Extent dst = mipExtent(0);
				renderGraph
					.AddComputePass("BloomPrefilter", "Skore://Shaders/BloomPrefilter.comp")
					.Stage(RenderStage::Composite)
					.Read(ForwardColorName)
					.Write(mipNames[0])
					.Constants<BloomPushConstants>([pc](RenderGraph&, BloomPushConstants& constants)
					{
						constants = pc;
					})
					.Dispatch(dst.width, dst.height, 1);
			}

			for (u32 m = 1; m < mipCount; ++m)
			{
				Extent src = mipExtent(m - 1);
				Extent dst = mipExtent(m);

				BloomPushConstants pc;
				pc.texelSize = Vec2(1.0f / static_cast<f32>(src.width), 1.0f / static_cast<f32>(src.height));
				pc.threshold = threshold;
				pc.softKnee = softKnee;
				pc.bloomRadius = bloomRadius;

				renderGraph
					.AddComputePass(String("BloomDownsample_") + ToString(m), "Skore://Shaders/BloomDownsample.comp")
					.Stage(RenderStage::Composite)
					.Read(mipNames[m - 1])
					.Write(mipNames[m])
					.Constants<BloomPushConstants>([pc](RenderGraph&, BloomPushConstants& constants)
					{
						constants = pc;
					})
					.Dispatch(dst.width, dst.height, 1);
			}

			for (i32 m = static_cast<i32>(mipCount) - 2; m >= 0; --m)
			{
				u32 srcMip = static_cast<u32>(m + 1);
				u32 dstMip = static_cast<u32>(m);

				Extent src = mipExtent(srcMip);
				Extent dst = mipExtent(dstMip);

				BloomPushConstants pc;
				pc.texelSize = Vec2(1.0f / static_cast<f32>(src.width), 1.0f / static_cast<f32>(src.height));
				pc.threshold = threshold;
				pc.softKnee = softKnee;
				pc.bloomRadius = bloomRadius;

				renderGraph
					.AddComputePass(String("BloomUpsample_") + ToString(dstMip), "Skore://Shaders/BloomUpsample.comp")
					.Stage(RenderStage::Composite)
					.Read(mipNames[srcMip])
					.WriteRead(mipNames[dstMip])
					.Constants<BloomPushConstants>([pc](RenderGraph&, BloomPushConstants& constants)
					{
						constants = pc;
					})
					.Dispatch(dst.width, dst.height, 1);
			}
		}
	};

	void RegisterBloomPass()
	{
		Reflection::Type<BloomPass>();
	}
}
