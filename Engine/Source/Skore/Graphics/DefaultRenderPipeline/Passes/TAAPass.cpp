#include "TAAPass.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Assets/ShaderAsset.hpp"
#include "Skore/IO/Asset.hpp"

namespace Skore
{
    struct TAAPass : RenderGraphPassHandler
    {
        RenderGraphResource* velocity;
        RenderGraphResource* depth;
        RenderGraphResource* colorOutput;
        RenderGraphResource* historyBuffer;
        RenderGraphResource* outputBuffer;

        PipelineState resolveTemporal;
        PipelineState updateHistory;

        BindingSet* resolveTemporalBindingSet;
        BindingSet* updateHistoryBindingSet;


        Sampler nearestSampler;

        TAAPass(RenderGraphResource* velocity, RenderGraphResource* depth, RenderGraphResource* colorOutput, RenderGraphResource* historyBuffer, RenderGraphResource* outputBuffer)
            : velocity(velocity),
              depth(depth),
              colorOutput(colorOutput),
              historyBuffer(historyBuffer),
              outputBuffer(outputBuffer) {}

        void Init() override
        {
            ShaderState* resolveTemporalState = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Passes/TAA.comp")->GetState("ResolveTemporal");
            ShaderState* updateHistoryState = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Passes/TAA.comp")->GetState("UpdateHistory");

            resolveTemporal = Graphics::CreateComputePipelineState({
                .shaderState = resolveTemporalState
            });

            updateHistory = Graphics::CreateComputePipelineState({
                .shaderState = updateHistoryState
            });

            resolveTemporalBindingSet = Graphics::CreateBindingSet(resolveTemporalState);
            updateHistoryBindingSet = Graphics::CreateBindingSet(updateHistoryState);

            nearestSampler = Graphics::CreateSampler({
                .filter = SamplerFilter::Nearest,
                .addressMode = TextureAddressMode::ClampToBorder
            });
        }

        void Render(RenderCommands& cmd) override
        {
            //resolve temporal
            cmd.ResourceBarrier({
                .texture = colorOutput->texture,
                .oldLayout = ResourceLayout::General,
                .newLayout = ResourceLayout::ShaderReadOnly,
            });

            cmd.ResourceBarrier({
                .texture = outputBuffer->texture,
                .oldLayout = ResourceLayout::ShaderReadOnly,
                .newLayout = ResourceLayout::General,
            });

            resolveTemporalBindingSet->GetVar("velocity")->SetTexture(velocity->texture);
            resolveTemporalBindingSet->GetVar("depth")->SetTexture(depth->texture);
            resolveTemporalBindingSet->GetVar("color")->SetTexture(colorOutput->texture);
            resolveTemporalBindingSet->GetVar("historyBuffer")->SetTexture(historyBuffer->texture);
            resolveTemporalBindingSet->GetVar("outputBuffer")->SetTexture(outputBuffer->texture);
            resolveTemporalBindingSet->GetVar("nearestSampler")->SetSampler(nearestSampler);


            cmd.BindPipelineState(resolveTemporal);
            cmd.BindBindingSet(resolveTemporal, resolveTemporalBindingSet);
            cmd.Dispatch(std::ceil(outputBuffer->textureCreation.extent.width / 8.f), std::ceil(outputBuffer->textureCreation.extent.height / 8.f), 1);

            cmd.ResourceBarrier({
                .texture = outputBuffer->texture,
                .oldLayout = ResourceLayout::General,
                .newLayout = ResourceLayout::ShaderReadOnly,
            });

            cmd.ResourceBarrier({
                .texture = colorOutput->texture,
                .oldLayout = ResourceLayout::ShaderReadOnly,
                .newLayout = ResourceLayout::General,
            });

            //update history

            cmd.ResourceBarrier({
                .texture = historyBuffer->texture,
                .oldLayout = ResourceLayout::ShaderReadOnly,
                .newLayout = ResourceLayout::General,
            });

            updateHistoryBindingSet->GetVar("colorOutput")->SetTexture(colorOutput->texture);
            updateHistoryBindingSet->GetVar("outputBuffer")->SetTexture(historyBuffer->texture);
            updateHistoryBindingSet->GetVar("historyBuffer")->SetTexture(outputBuffer->texture);

            cmd.BindPipelineState(updateHistory);
            cmd.BindBindingSet(updateHistory, updateHistoryBindingSet);
            cmd.Dispatch(std::ceil(colorOutput->textureCreation.extent.width / 8.f), std::ceil(colorOutput->textureCreation.extent.height / 8.f), 1);

            cmd.ResourceBarrier({
                .texture = historyBuffer->texture,
                .oldLayout = ResourceLayout::General,
                .newLayout = ResourceLayout::ShaderReadOnly,
            });
        }

        void Destroy() override
        {
            Graphics::DestroyComputePipelineState(resolveTemporal);
            Graphics::DestroyComputePipelineState(updateHistory);
            Graphics::DestroyBindingSet(resolveTemporalBindingSet);
            Graphics::DestroyBindingSet(updateHistoryBindingSet);
            Graphics::DestroySampler(nearestSampler);
        }
    };


    void TAASetup(RenderGraph& rg, RenderGraphResource* velocity, RenderGraphResource* depth, RenderGraphResource* colorOutput)
    {
        RenderGraphResource* historyBuffer = rg.Create(RenderGraphResourceCreation{
            .name = "historyBuffer",
            .type = RenderGraphResourceType::Texture,
            .scale = {1, 1},
            .format = Format::RGBA16F
        });

        RenderGraphResource* outputBuffer = rg.Create(RenderGraphResourceCreation{
            .name = "outputBuffer",
            .type = RenderGraphResourceType::Texture,
            .scale = {1, 1},
            .format = Format::RGBA16F
        });

        rg.AddPass("TAAPass", RenderGraphPassType::Compute)
          .Read(depth)
          .Read(velocity)
          .Write(colorOutput)
          .Handler<TAAPass>(velocity, depth, colorOutput, historyBuffer, outputBuffer);
    }
}
