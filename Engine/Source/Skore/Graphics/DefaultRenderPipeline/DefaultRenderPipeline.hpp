#pragma once
#include "Skore/Graphics/GraphicsTypes.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"


namespace Skore
{
    enum class AntiAliasingType
    {
        None     = 0,
        FXAA     = 1,
        SMAA     = 2,
        SMAA_1TX = 3,
        SMAA_2TX = 4,
        TAA      = 5
    };


    class SK_API DefaultRenderPipeline : public RenderPipeline
    {
    public:
        SK_BASE_TYPES(RenderPipeline);

        Format           outputFormat = Format::RGBA;
        AntiAliasingType antiAliasing = AntiAliasingType::TAA;

        void BuildRenderGraph(RenderGraph& rg) override;
    };
}
