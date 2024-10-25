#pragma once
#include "Fyrion/IO/Asset.hpp"

namespace Fyrion
{
    class RenderGraph;

    struct RenderPipeline : Asset
    {
        virtual ~RenderPipeline() = default;
        virtual void BuildRenderGraph(RenderGraph& rg) = 0;
    };
}
