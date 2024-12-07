#include "SkyRenderPass.hpp"

#include "Skore/Core/Math.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderProxy.hpp"
#include "Skore/Graphics/Assets/TextureAsset.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
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
        Sampler       sampler;

        RenderGraphResource* colorTexture = nullptr;
        RenderGraphResource* depth = nullptr;

        SkyRenderPass(RenderGraphResource* colorTexture, RenderGraphResource* depth)
            : colorTexture(colorTexture),
              depth(depth) {}

        RenderProxy* renderProxy = nullptr;

        void Init() override
        {
            if (rg->GetScene())
            {
                renderProxy = rg->GetScene()->GetProxy<RenderProxy>();
            }

            ShaderAsset* shaderAsset = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Passes/SkyboxRender.comp");

            pipelineState = Graphics::CreateComputePipelineState({
                .shaderState = shaderAsset->GetDefaultState()
            });

            bindingSet = Graphics::CreateBindingSet(shaderAsset->GetDefaultState());

            sampler = Graphics::CreateSampler({
                .filter = SamplerFilter::Linear,
                .addressMode = TextureAddressMode::Repeat
            });
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
            bindingSet->GetVar("samplerState")->SetSampler(sampler);

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
            Graphics::DestroySampler(sampler);
        }
    };

    void SkyRenderPassSetup(RenderGraph& rg, RenderGraphResource* colorTexture, RenderGraphResource* depth)
    {
        rg.AddPass("SkyRenderPass", RenderGraphPassType::Compute)
          .Read(colorTexture)
          .Read(depth)
          .Write(colorTexture)
          .Handler<SkyRenderPass>(colorTexture, depth);
    }
}
