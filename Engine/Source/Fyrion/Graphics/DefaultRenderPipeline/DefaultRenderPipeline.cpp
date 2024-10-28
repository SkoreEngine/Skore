#include "DRPTypes.hpp"
#include "GBufferPass.hpp"
#include "LightingPass.hpp"
#include "PostProcessRenderPass.hpp"

#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Graphics/RenderGraph.hpp"
#include "Fyrion/Graphics/RenderPipeline.hpp"

namespace Fyrion
{
    class DefaultRenderPipeline : public RenderPipeline
    {
    public:
        FY_BASE_TYPES(RenderPipeline);

        GBufferPass           gBufferPass;
        LightingPass          lightingPass;
        PostProcessRenderPass postProcessRenderPass;

        void BuildRenderGraph(RenderGraph& rg) override
        {
            //gbuffer textures
            RenderGraphResource* gbuffer1 = rg.Create(RenderGraphResourceCreation{
                .name = "gbuffer1",
                .type = RenderGraphResourceType::Attachment,
                .scale = {1, 1},
                .format = Format::RGBA
            });

            RenderGraphResource* gbuffer2 = rg.Create(RenderGraphResourceCreation{
                .name = "gbuffer2",
                .type = RenderGraphResourceType::Attachment,
                .scale = {1, 1},
                .format = Format::RG
            });

            RenderGraphResource* gbuffer3 = rg.Create(RenderGraphResourceCreation{
                .name = "gbuffer3",
                .type = RenderGraphResourceType::Attachment,
                .scale = {1, 1},
                .format = Format::RG16F
            });

            RenderGraphResource* depth = rg.Create(RenderGraphResourceCreation{
                .name = "depth",
                .type = RenderGraphResourceType::Attachment,
                .scale = {1, 1},
                .format = Format::Depth,
            });

            //light textures
            RenderGraphResource* lightOutput = rg.Create(RenderGraphResourceCreation{
                .name = "lightOutput",
                .type = RenderGraphResourceType::Texture,
                .scale = {1, 1},
                .format = Format::RGBA16F
            });

            //output color
            RenderGraphResource* colorOutput = rg.Create(RenderGraphResourceCreation{
                .name = "colorOutput",
                .type = RenderGraphResourceType::Texture,
                .scale = {1, 1},
                .format = Format::RGBA
            });

            //setup gbuffer
            rg.AddPass("GBuffer", RenderGraphPassType::Graphics)
              .Write(gbuffer1)
              .Write(gbuffer2)
              .Write(gbuffer3)
              .Write(depth)
              .ClearColor(Color::BLACK.ToVec4())
              .ClearDepth(true)
              .Handler(&gBufferPass);

            //setup lighting pass
            lightingPass.gbuffer1 = gbuffer1;
            lightingPass.gbuffer2 = gbuffer2;
            lightingPass.gbuffer3 = gbuffer3;
            lightingPass.depth = depth;
            lightingPass.lightOutput = lightOutput;

            rg.AddPass("LightingPass", RenderGraphPassType::Compute)
              .Read(gbuffer1)
              .Read(gbuffer2)
              .Read(gbuffer3)
              .Read(depth)
              .Write(lightOutput)
              .Handler(&lightingPass);

            //post-processing output
            postProcessRenderPass.lightColor = lightOutput;
            postProcessRenderPass.outputColor = colorOutput;

            rg.AddPass("PostProcessRenderPass", RenderGraphPassType::Compute)
              .Read(lightOutput)
              .Write(colorOutput)
              .Handler(&postProcessRenderPass);

            //define outputs
            rg.ColorOutput(colorOutput);
            rg.DepthOutput(depth);
        }
    };

    void RegisterDefaultRenderPipeline()
    {
        Registry::Type<DefaultRenderPipeline>();
    }
}
