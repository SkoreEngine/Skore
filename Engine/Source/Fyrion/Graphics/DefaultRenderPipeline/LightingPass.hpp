#pragma once
#include "Fyrion/Graphics/RenderGraph.hpp"

namespace Fyrion
{

    struct LightingData
    {
        Mat4 viewProjInverse;
    };

    struct LightingPass : RenderGraphPassHandler
    {
        PipelineState lightingPSO{};
        BindingSet*   bindingSet{};

        RenderGraphResource* gbuffer1;
        RenderGraphResource* gbuffer2;
        RenderGraphResource* gbuffer3;
        RenderGraphResource* lightOutput{};
        RenderGraphResource* depth;
        RenderGraphResource* posTest;

        void Init() override
        {
            ComputePipelineCreation creation{
                .shader = Assets::LoadByPath<ShaderAsset>("Fyrion://Shaders/Passes/LightingPass2.comp")
            };
            lightingPSO = Graphics::CreateComputePipelineState(creation);
            bindingSet = Graphics::CreateBindingSet(creation.shader);
        }

        void Render(RenderCommands& cmd) override
        {
            LightingData data =  {
                .viewProjInverse = Math::Inverse(rg->GetCameraData().projection * rg->GetCameraData().view),
            };

            bindingSet->GetVar("gbuffer1")->SetTexture(gbuffer1->texture);
            bindingSet->GetVar("gbuffer2")->SetTexture(gbuffer2->texture);
            bindingSet->GetVar("gbuffer3")->SetTexture(gbuffer3->texture);
            bindingSet->GetVar("posTest")->SetTexture(posTest->texture);
            bindingSet->GetVar("depth")->SetTexture(depth->texture);
            bindingSet->GetVar("data")->SetValue(&data, sizeof(LightingData));

            bindingSet->GetVar("lightOutput")->SetTexture(lightOutput->texture);

            cmd.BindPipelineState(lightingPSO);
            cmd.BindBindingSet(lightingPSO, bindingSet);

            cmd.Dispatch(std::ceil(gbuffer1->textureCreation.extent.width / 16.f),
                         std::ceil(gbuffer1->textureCreation.extent.height / 16.f),
                         1.f);
        }

        void Destroy() override
        {
            Graphics::DestroyBindingSet(bindingSet);
            Graphics::DestroyComputePipelineState(lightingPSO);
        }
    };
}
