#pragma once
#include "Skore/Graphics/RenderGraph.hpp"


namespace Skore
{


    struct PostProcessRenderPass : RenderGraphPassHandler
    {
        PipelineState pipelineState;
        BindingSet* bindingSet;

        RenderGraphResource* lightColor;
        RenderGraphResource* outputColor;

        void Init() override
        {
            ShaderAsset* shader = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Passes/PostProcessRender.comp");

            pipelineState = Graphics::CreateComputePipelineState({
                .shaderState = shader->GetDefaultState()
            });

            bindingSet = Graphics::CreateBindingSet(shader->GetDefaultState());
        }

        void Render(RenderCommands& cmd) override
        {
            bindingSet->GetVar("inputTexture")->SetTexture(lightColor->texture);
            bindingSet->GetVar("outputTexture")->SetTexture(outputColor->texture);

            cmd.BindPipelineState(pipelineState);
            cmd.BindBindingSet(pipelineState, bindingSet);

            cmd.Dispatch(std::ceil(lightColor->textureCreation.extent.width / 8.f),
                         std::ceil(lightColor->textureCreation.extent.height / 8.f),
                         1.f);
        }

        void Destroy() override
        {
            Graphics::DestroyBindingSet(bindingSet);
            Graphics::DestroyComputePipelineState(pipelineState);
        }
    };

}
