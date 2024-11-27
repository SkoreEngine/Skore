#pragma once
#include "Skore/Graphics/RenderGraph.hpp"

namespace Skore
{
    void XeGTAOSetup(RenderGraph& rg, RenderGraphResource* depth, RenderGraphResource* normals, RenderGraphResource* aoOutput);
}
