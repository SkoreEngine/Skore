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

        RenderGraphResource* emissive = rg.Create(RenderGraphResourceCreation{
            .name = "emissive",
            .type = RenderGraphResourceType::Attachment,
            .scale = {1, 1},
            .format = Format::R11G11B10UF
        });

        RenderGraphResource* depth = rg.Create(RenderGraphResourceCreation{
            .name = "depth",
            .type = RenderGraphResourceType::Attachment,
            .scale = {1, 1},
            .format = Format::Depth,
        });

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

        //setup gbuffer
        GBufferPassSetup(rg, gbuffer1, gbuffer2, gbuffer3, emissive, depth);

        //GTAO
        XeGTAOSetup(rg, depth, gbuffer3, aoOutput);

        //setup shadowMap pass
        ShadowPassSetup(rg, shadowMap);

        //setup lighting pass
        LightingPassSetup(rg,
                          gbuffer1,
                          gbuffer2,
                          gbuffer3,
                          emissive,
                          aoOutput,
                          shadowMap,
                          depth,
                          lightOutput);

        //sky render
        SkyRenderPassSetup(rg, lightOutput, depth);

        //post-processing output
        PostProcessRenderPassSetup(rg, lightOutput, colorOutput);

        //define outputs
        rg.ColorOutput(colorOutput);
        rg.DepthOutput(depth);
    }

    void RegisterDefaultRenderPipeline()
    {
        Registry::Type<DefaultRenderPipeline>();
    }
}
