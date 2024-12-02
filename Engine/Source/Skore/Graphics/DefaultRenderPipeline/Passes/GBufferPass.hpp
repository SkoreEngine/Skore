#pragma once
#include "Skore/Common.hpp"
#include "Skore/Graphics/RenderGraph.hpp"


namespace Skore
{
    SK_API void GBufferPassSetup(RenderGraph&         rg,
                                 RenderGraphResource* gbuffer1,
                                 RenderGraphResource* gbuffer2,
                                 RenderGraphResource* gbuffer3,
                                 RenderGraphResource* depth);
}
