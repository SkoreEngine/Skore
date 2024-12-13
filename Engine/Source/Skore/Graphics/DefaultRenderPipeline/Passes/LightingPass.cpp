#include "LightingPass.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderProxy.hpp"
#include "Skore/Graphics/DefaultRenderPipeline/DefaultRenderPipelineTypes.hpp"
#include "Skore/Scene/Scene.hpp"


namespace Skore
{
    struct LightingPass : RenderGraphPassHandler
    {
        PipelineState lightingPSO{};
        BindingSet*   bindingSet{};

        RenderProxy* renderProxy = nullptr;

        RenderGraphResource* gbuffer1;
        RenderGraphResource* gbuffer2;
        RenderGraphResource* gbuffer3;
        RenderGraphResource* emissive;
        RenderGraphResource* aoTexture;
        RenderGraphResource* shadowMap;
        RenderGraphResource* depth;
        RenderGraphResource* lightOutput;

        LightingPass(RenderGraphResource* gbuffer1, RenderGraphResource* gbuffer2, RenderGraphResource* gbuffer3, RenderGraphResource* emissive, RenderGraphResource* aoTexture,
                     RenderGraphResource* shadowMap,
                     RenderGraphResource* depth, RenderGraphResource* lightOutput)
            : gbuffer1(gbuffer1),
              gbuffer2(gbuffer2),
              gbuffer3(gbuffer3),
              emissive(emissive),
              aoTexture(aoTexture),
              shadowMap(shadowMap),
              depth(depth),
              lightOutput(lightOutput) {}


        BRDFLUTGenerator brdflutGenerator;
        Sampler          shadowMapSampler;
        Sampler          brdfLutSampler;
        Sampler          aoSampler;


        void Init() override
        {
            if (rg->GetScene())
            {
                renderProxy = rg->GetScene()->GetProxy<RenderProxy>();
            }

            ComputePipelineCreation creation{
                .shaderState = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Passes/LightingPass.comp")->GetDefaultState()
            };
            lightingPSO = Graphics::CreateComputePipelineState(creation);
            bindingSet = Graphics::CreateBindingSet(creation.shaderState);

            brdflutGenerator.Init({512, 512});

            shadowMapSampler = Graphics::CreateSampler({
                .filter = SamplerFilter::Linear,
                .addressMode = TextureAddressMode::ClampToEdge,
                .comparedEnabled = true,
                .compareOperator = CompareOp::LessOrEqual,
                .borderColor = BorderColor::FloatOpaqueWhite
            });

            brdfLutSampler = Graphics::CreateSampler({
                .addressMode = TextureAddressMode::ClampToEdge,
                .anisotropyEnable = false,
                .borderColor = BorderColor::FloatTransparentBlack,
            });

            aoSampler = Graphics::CreateSampler({
                .filter = SamplerFilter::Nearest,
                .samplerMipmapMode = SamplerMipmapMode::Nearest
            });
        }

        void Render(RenderCommands& cmd) override
        {
            if (!renderProxy) return;

            LightingData data;
            data.viewProjInverse = Math::Inverse(rg->GetCameraData().projection * rg->GetCameraData().view);
            data.view = rg->GetCameraData().view;
            data.SetViewPos(rg->GetCameraData().viewPos);

            if (renderProxy->cubemapTest.has_value())
            {
                data.data1.x = 1.0f;
                bindingSet->GetVar("cubemapTest")->SetTexture(*renderProxy->cubemapTest);
            }
            else
            {
                bindingSet->GetVar("cubemapTest")->SetTexture(renderProxy->GetSkyCubeMap());
            }


            Span<LightRenderData> lights = renderProxy->GetLights();
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


            ShadowMapDataInfo* shadowMapDataInfo = static_cast<ShadowMapDataInfo*>(shadowMap->reference);

            for (int i = 0; i < SK_SHADOW_MAP_CASCADE_COUNT; ++i)
            {
                data.cascadeSplits[i] = shadowMapDataInfo->cascadeSplit[i];
                data.cascadeViewProjMat[i] = shadowMapDataInfo->cascadeViewProjMat[i];
            }

            bindingSet->GetVar("gbuffer1")->SetTexture(gbuffer1->texture);
            bindingSet->GetVar("gbuffer2")->SetTexture(gbuffer2->texture);
            bindingSet->GetVar("gbuffer3")->SetTexture(gbuffer3->texture);
            bindingSet->GetVar("emissiveTexture")->SetTexture(emissive->texture);
            bindingSet->GetVar("diffuseIrradiance")->SetTexture(renderProxy->GetDiffuseIrradiance());
            bindingSet->GetVar("specularMap")->SetTexture(renderProxy->GetSpecularMap());
            bindingSet->GetVar("aoTexture")->SetTexture(aoTexture->texture);
            bindingSet->GetVar("aoSampler")->SetSampler(aoSampler);
            bindingSet->GetVar("brdfLUT")->SetTexture(brdflutGenerator.GetTexture());
            bindingSet->GetVar("brdfLUTSampler")->SetSampler(brdfLutSampler);
            bindingSet->GetVar("shadowMapTexture")->SetTexture(shadowMap->texture);
            bindingSet->GetVar("shadowMapSampler")->SetSampler(shadowMapSampler);
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
            Graphics::DestroySampler(shadowMapSampler);
            Graphics::DestroySampler(brdfLutSampler);
            Graphics::DestroySampler(aoSampler);
            Graphics::DestroyBindingSet(bindingSet);
            Graphics::DestroyComputePipelineState(lightingPSO);

            brdflutGenerator.Destroy();
        }
    };

    void LightingPassSetup(RenderGraph& rg, RenderGraphResource* gbuffer1, RenderGraphResource* gbuffer2, RenderGraphResource* gbuffer3, RenderGraphResource* emissive, RenderGraphResource* aoTexture,
                           RenderGraphResource* shadowMap,
                           RenderGraphResource* depth, RenderGraphResource* lightOutput)
    {
        rg.AddPass("LightingPass", RenderGraphPassType::Compute)
          .Read(gbuffer1)
          .Read(gbuffer2)
          .Read(gbuffer3)
          .Read(emissive)
          .Read(shadowMap)
          .Read(depth)
          .Write(lightOutput)
          .Handler<LightingPass>(gbuffer1, gbuffer2, gbuffer3, emissive, aoTexture, shadowMap, depth, lightOutput);
    }
}
