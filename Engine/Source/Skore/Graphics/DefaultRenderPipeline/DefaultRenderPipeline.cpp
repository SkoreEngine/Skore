#include "DefaultRenderPipeline.hpp"

#include "Effects/XeGTAO.hpp"
#include "Passes/GBufferPass.hpp"
#include "Passes/LightingPass.hpp"
#include "Passes/PostProcessRenderPass.hpp"
#include "Passes/ShadowPass.hpp"
#include "Passes/SkyRenderPass.hpp"

#include "Skore/Core/Registry.hpp"
#include "Skore/Graphics/RenderGraph.hpp"

namespace Skore
{
    void DefaultRenderPipeline::BuildRenderGraph(RenderGraph& rg)
    {
        RenderGraphResource* aoOutput = rg.Create(RenderGraphResourceCreation{
            .name = "aoOutput",
            .type = RenderGraphResourceType::Texture,
            .scale = {1, 1},
            .format = Format::R32U
        });

        //shadow texture
        RenderGraphResource* shadowMap = rg.Create(RenderGraphResourceCreation{
            .name = "shadowMap",
            .type = RenderGraphResourceType::Reference,
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
            .format = outputFormat
        });

        GBufferOutput gbufferOutput = GBufferPassSetup(rg);

        //GTAO
        XeGTAOSetup(rg, gbufferOutput.depth, gbufferOutput.gbuffer3, aoOutput);

        //setup shadowMap pass
        ShadowPassSetup(rg, shadowMap);

        //setup lighting pass
        LightingPassSetup(rg,
                          gbufferOutput.gbuffer1,
                          gbufferOutput.gbuffer2,
                          gbufferOutput.gbuffer3,
                          gbufferOutput.emissive,
                          aoOutput,
                          shadowMap,
                          gbufferOutput.depth,
                          lightOutput);

        //sky render
        SkyRenderPassSetup(rg, lightOutput, gbufferOutput.depth);

        //post-processing output
        PostProcessRenderPassSetup(rg, lightOutput, colorOutput);

 #if SK_ENABLE_TAA
        //TAA
        //output color
        RenderGraphResource* historyBuffer = rg.Create(RenderGraphResourceCreation{
            .name = "historyBuffer",
            .type = RenderGraphResourceType::Texture,
            .scale = {1, 1},
            .format = Format::RGBA16F
        });


        RenderGraphResource*  nearestSampler = rg.Create(RenderGraphResourceCreation{
            .name = "nearestSampler",
            .type = RenderGraphResourceType::Sampler,
            .samplerCreation = {
                .filter = SamplerFilter::Linear
            }
        });

        rg.AddPass("TAAResolve", RenderGraphPassType::Compute)
          .Shader("Skore://Shaders/Passes/ResolveTAA.comp")
          .Read(nearestSampler)
          .Read(gbufferOutput.depth)
          .Read("velocity", gbufferOutput.velocity)
          .Read("historyBuffer", historyBuffer)
          .Read("color", colorOutput)
          .Write("historyOutput", historyBuffer)
          .Write("colorOutput", colorOutput)
          .Dispatch(8, 8, 1);
#endif

        //define outputs
        rg.ColorOutput(colorOutput);
        rg.DepthOutput(gbufferOutput.depth);
    }

    void RegisterDefaultRenderPipeline()
    {
        Registry::Type<DefaultRenderPipeline>();
    }
}
