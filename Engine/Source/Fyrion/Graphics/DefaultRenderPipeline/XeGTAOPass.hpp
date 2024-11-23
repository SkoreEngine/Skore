#pragma once


#include "Fyrion/Graphics/Assets/ShaderAsset.hpp"

namespace Fyrion
{
    struct XeGTAOPass : RenderGraphPassHandler
    {

        void Init() override
        {
            // ShaderState* prefilterShaderState = Assets::LoadByPath<ShaderAsset>("Fyrion://Shaders/Effects/XeGTAO/vaGTAO.comp")->GetState("Prefilter");
            //
            // Graphics::CreateComputePipelineState({
            //     .shaderState = prefilterShaderState
            // });

        }

        void Update(f64 deltaTime) override
        {

        }
    };
}

