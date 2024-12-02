#pragma once
#include "Skore/Graphics/RenderGraph.hpp"

namespace Skore
{
    SK_API void PostProcessRenderPassSetup(RenderGraph& rg, RenderGraphResource* lightOutput, RenderGraphResource* colorOutput);
}
