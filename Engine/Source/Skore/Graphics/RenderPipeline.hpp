#pragma once
#include "Skore/IO/Asset.hpp"

namespace Skore
{
    class RenderGraph;

    struct RenderPipeline : Asset
    {
        virtual ~RenderPipeline() = default;
        virtual void BuildRenderGraph(RenderGraph& rg) = 0;
    };
}
