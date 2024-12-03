#pragma once
#include "Skore/Common.hpp"
#include "Skore/Graphics/RenderGraph.hpp"

namespace Skore
{
    SK_API void LightingPassSetup(RenderGraph& rg, RenderGraphResource* gbuffer1, RenderGraphResource* gbuffer2, RenderGraphResource* gbuffer3, RenderGraphResource* emissive, RenderGraphResource* aoTexture,
                                  RenderGraphResource* shadowMap, RenderGraphResource* depth, RenderGraphResource* lightOutput);
}
