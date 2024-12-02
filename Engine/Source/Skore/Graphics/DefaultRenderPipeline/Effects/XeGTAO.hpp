#pragma once
#include "Skore/Graphics/RenderGraph.hpp"

namespace Skore
{
    SK_API void XeGTAOSetup(RenderGraph& rg, RenderGraphResource* depth, RenderGraphResource* normals, RenderGraphResource* aoOutput);
}
