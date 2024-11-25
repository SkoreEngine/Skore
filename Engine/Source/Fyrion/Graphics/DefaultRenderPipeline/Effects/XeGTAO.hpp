#pragma once
#include "Fyrion/Graphics/RenderGraph.hpp"

namespace Fyrion
{
    void XeGTAOSetup(RenderGraph& rg, RenderGraphResource* depth, RenderGraphResource* normals, RenderGraphResource* aoOutput);
}
