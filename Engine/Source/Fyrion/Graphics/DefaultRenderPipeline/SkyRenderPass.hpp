#pragma once

#include "Fyrion/Graphics/Assets/ShaderAsset.hpp"

namespace Fyrion
{
    struct SkyboxRenderData
    {
        Mat4 viewInverse;
        Mat4 projInverse;
        Vec4 skyboxProperties; //rgb color, a = hasSkyboxTexture.
    };

    struct SkyRenderPass : RenderGraphPassHandler
    {
        PipelineState pipelineState = {};
        BindingSet*   bindingSet = {};

        RenderGraphResource* depth = nullptr;
        RenderGraphResource* colorTexture = nullptr;

        RenderProxy* renderProxy = nullptr;

        void Init() override
        {
            if (rg->GetScene())
            {
                renderProxy = rg->GetScene()->GetProxy<RenderProxy>();
            }

            ShaderAsset* shaderAsset = Assets::LoadByPath<ShaderAsset>("Fyrion://Shaders/Passes/SkyboxRender.comp");

            pipelineState = Graphics::CreateComputePipelineState({
                .shader = shaderAsset
            });

            bindingSet = Graphics::CreateBindingSet(shaderAsset);
        }

        void Render(RenderCommands& cmd) override
        {
            TextureAsset* skyTexture = renderProxy ? renderProxy->GetPanoramaSky() : nullptr;

            SkyboxRenderData data{
                .viewInverse = rg->GetCameraData().viewInverse,
                .projInverse = rg->GetCameraData().projectionInverse,
                .skyboxProperties = Math::MakeVec4(Color::CORNFLOWER_BLUE.ToVec3(), skyTexture != nullptr ? 1.0 : 0.0)
            };

            if (skyTexture)
            {
                bindingSet->GetVar("panoramicTexture")->SetTexture(skyTexture->GetTexture());
            }

            bindingSet->GetVar("colorTexture")->SetTexture(colorTexture->texture);
            bindingSet->GetVar("depthTexture")->SetTexture(depth->texture);
            bindingSet->GetVar("data")->SetValue(&data, sizeof(data));

            cmd.BindPipelineState(pipelineState);
            cmd.BindBindingSet(pipelineState, bindingSet);

            cmd.Dispatch(std::ceil(colorTexture->textureCreation.extent.width / 16.f),
                         std::ceil(colorTexture->textureCreation.extent.height / 16.f),
                         1.f);
        }

        void Destroy() override
        {
            Graphics::DestroyComputePipelineState(pipelineState);
            Graphics::DestroyBindingSet(bindingSet);
        }
    };
}
