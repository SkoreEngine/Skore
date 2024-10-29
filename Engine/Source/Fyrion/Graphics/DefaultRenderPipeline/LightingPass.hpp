#pragma once
#include "DRPTypes.hpp"
#include "Fyrion/Graphics/RenderGraph.hpp"

namespace Fyrion
{
    struct LightingPass : RenderGraphPassHandler
    {
        PipelineState lightingPSO{};
        BindingSet*   bindingSet{};

        RenderService* renderService = nullptr;

        RenderGraphResource* gbuffer1;
        RenderGraphResource* gbuffer2;
        RenderGraphResource* gbuffer3;
        RenderGraphResource* lightOutput{};
        RenderGraphResource* depth;

        void Init() override
        {
            if (rg->GetScene())
            {
                renderService = rg->GetScene()->GetService<RenderService>();
            }

            ComputePipelineCreation creation{
                .shader = Assets::LoadByPath<ShaderAsset>("Fyrion://Shaders/Passes/LightingPass2.comp")
            };
            lightingPSO = Graphics::CreateComputePipelineState(creation);
            bindingSet = Graphics::CreateBindingSet(creation.shader);
        }

        void Render(RenderCommands& cmd) override
        {
            LightingData data;
            data.viewProjInverse = Math::Inverse(rg->GetCameraData().projection * rg->GetCameraData().view);
            data.SetViewPos(rg->GetCameraData().viewPos);

            if (renderService)
            {
                Span<LightRenderData> lights = renderService->GetLights();
                data.SetLightCount(static_cast<i32>(lights.Size()));

                for (int l = 0; l < lights.Size(); ++l)
                {
                    data.lights[l].SetType(lights[l].properties.type);
                    data.lights[l].SetDirection(lights[l].properties.direction);
                    data.lights[l].SetPosition(lights[l].properties.position);
                    data.lights[l].SetColor(lights[l].properties.color.ToVec3() * lights[l].properties.intensity);
                    data.lights[l].SetIndirectMultiplier(lights[l].properties.indirectMultiplier);
                    data.lights[l].SetRange(lights[l].properties.range);
                    data.lights[l].SetInnerCutoff(lights[l].properties.innerCutoff);
                    data.lights[l].SetOuterCutoff(lights[l].properties.outerCutoff);
                }
            }

            bindingSet->GetVar("gbuffer1")->SetTexture(gbuffer1->texture);
            bindingSet->GetVar("gbuffer2")->SetTexture(gbuffer2->texture);
            bindingSet->GetVar("gbuffer3")->SetTexture(gbuffer3->texture);
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
