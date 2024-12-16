#include "DefaultRenderPipeline.hpp"

#include "Effects/XeGTAO.hpp"
#include "Passes/CullingPass.hpp"
#include "Passes/GBufferPass.hpp"
#include "Passes/LightingPass.hpp"
#include "Passes/PostProcessRenderPass.hpp"
#include "Passes/ShadowPass.hpp"
#include "Passes/SkyRenderPass.hpp"
#include "Passes/TAAPass.hpp"

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

        CullingOutput cullingOutput = CullingPassSetup(rg);
        GBufferOutput gbufferOutput = GBufferPassSetup(rg, cullingOutput);

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

        if (antiAliasing == AntiAliasingType::TAA)
        {
            //TAA
            TAASetup(rg, gbufferOutput.velocity, gbufferOutput.depth, colorOutput);
        }

        //define outputs
        rg.ColorOutput(colorOutput);
        rg.DepthOutput(gbufferOutput.depth);
    }

    void RegisterDefaultRenderPipeline()
    {
        Registry::Type<DefaultRenderPipeline>();
    }
}
