#pragma once
#include "Skore/Common.hpp"
#include "Skore/Graphics/RenderGraph.hpp"


namespace Skore
{
    SK_API void TAASetup(RenderGraph& rg, RenderGraphResource* velocity, RenderGraphResource* depth, RenderGraphResource* colorOutput);
}
