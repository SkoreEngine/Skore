#pragma once
#include "Skore/Common.hpp"
#include "Skore/Graphics/RenderGraph.hpp"

namespace Skore
{

    struct CullingOutput
    {
        RenderGraphResource* drawIndirectCommands;
        RenderGraphResource* drawIndirectCount;
    };


    SK_API CullingOutput CullingPassSetup(RenderGraph& rg);
}
