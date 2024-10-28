#pragma once
#include "Fyrion/Graphics/RenderGraph.hpp"

namespace Fyrion
{

    struct Light
    {
        Vec4 directionType;
        Vec4 positionMultiplier;
        Vec4 color;
        Vec4 rangeTheta;

        void SetDirection(Vec3 direction)
        {
            direction = Math::Normalize(direction);
            directionType.x = direction.x;
            directionType.y = direction.y;
            directionType.z = direction.z;
        }

        void SetType(LightType type)
        {
            directionType.w = static_cast<f32>(type);
        }

        void SetPosition(Vec3 position)
        {
            positionMultiplier.x = position.x;
            positionMultiplier.y = position.y;
            positionMultiplier.z = position.z;
        }

        void SetIndirectMultiplier(f32 multiplier)
        {
            positionMultiplier.w = multiplier;
        }

        void SetColor(const Vec3 color)
        {
            this->color.x = color.x;
            this->color.y = color.y;
            this->color.z = color.z;
        }

        void SetRange(f32 range)
        {
            rangeTheta.x = range;
        }

        void SetCosThetaOuter(f32 value)
        {
            rangeTheta.y = value;
        }

        void SetCosThetaInner(f32 value)
        {
            rangeTheta.z = value;
        }
    };

    struct LightingData
    {
        Mat4  viewProjInverse = {};
        Vec4  data0 = {};
        Light lights[128];

        void SetViewPos(Vec3 viewPos)
        {
            data0.x = viewPos.x;
            data0.y = viewPos.y;
            data0.z = viewPos.z;
        }

        void SetLightCount(i32 count)
        {
            data0.w = static_cast<f32>(count);
        }
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

            Vec3 dir = {0.2f, 1.0f, 0.3f};

            LightingData data =  {
                .viewProjInverse = Math::Inverse(rg->GetCameraData().projection * rg->GetCameraData().view),
            };

            data.SetViewPos(rg->GetCameraData().viewPos);

            data.SetLightCount(1);
            data.lights[0].SetType(LightType::Directional);
            data.lights[0].SetDirection(dir);
            data.lights[0].SetColor(Vec3{1.0f, 1.0f, 1.0f} * 10);

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
