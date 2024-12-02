#pragma once
#include "Skore/Graphics/RenderGraph.hpp"

namespace Skore
{
    SK_API void SkyRenderPassSetup(RenderGraph& rg, RenderGraphResource* colorTexture, RenderGraphResource* depth);
}
