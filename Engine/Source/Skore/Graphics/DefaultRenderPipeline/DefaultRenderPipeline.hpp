#pragma once
#include "Skore/Graphics/GraphicsTypes.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"


namespace Skore
{
    class SK_API DefaultRenderPipeline : public RenderPipeline
    {
    public:
        SK_BASE_TYPES(RenderPipeline);

        Format outputFormat = Format::RGBA;

        void BuildRenderGraph(RenderGraph& rg) override;
    };
}
