#pragma once
#include "CullingPass.hpp"
#include "Skore/Common.hpp"
#include "Skore/Graphics/RenderGraph.hpp"


namespace Skore
{
    struct GBufferOutput
    {
        RenderGraphResource* gbuffer1;
        RenderGraphResource* gbuffer2;
        RenderGraphResource* gbuffer3;
        RenderGraphResource* emissive;
        RenderGraphResource* velocity;
        RenderGraphResource* depth;
    };

    SK_API GBufferOutput GBufferPassSetup(RenderGraph& rg, const CullingOutput& cullingOutput);
}
