#include "CullingPass.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderProxy.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{

    struct CullingPassData
    {
        u32 instanceCount;
        u32 _pad0;
        u32 _pad1;
        u32 _pad2;
    };

    struct CullingPass : RenderGraphPassHandler
    {
        RenderProxy*  renderProxy = nullptr;
        PipelineState pipelineState;
        BindingSet* bindingSet;

        RenderGraphResource* drawIndirectCommands;
        RenderGraphResource* drawIndirectCount;

        CullingPass(RenderGraphResource* drawIndirectCommands, RenderGraphResource* drawIndirectCount)
            : drawIndirectCommands(drawIndirectCommands),
              drawIndirectCount(drawIndirectCount)
        {

        }

        void Init() override
        {
            if (rg->GetScene())
            {
                renderProxy = rg->GetScene()->GetProxy<RenderProxy>();
            }

            ShaderState* shaderState = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Passes/CullingPass.comp")->GetDefaultState();
            pipelineState = Graphics::CreateComputePipelineState({
                .shaderState = shaderState
            });

            bindingSet = Graphics::CreateBindingSet(shaderState);
        }

        void Render(RenderCommands& cmd) override
        {
            if (!renderProxy) return;

            RenderInstances& instances = renderProxy->GetInstances();
            instances.Flush(cmd);

            if (!drawIndirectCommands->buffer)
            {
                drawIndirectCommands->buffer = Graphics::CreateBuffer({
                    .usage = BufferUsage::StorageBuffer | BufferUsage::IndirectBuffer,
                    .size = instances.maxInstanceCount * sizeof(DrawIndexedIndirectArguments),
                    .allocation = BufferAllocation::GPUOnly
                });
            }

            CullingPassData data{
                .instanceCount = instances.instanceCount
            };

            bindingSet->GetVar("instances")->SetBuffer(instances.instanceBuffer);
            bindingSet->GetVar("drawCount")->SetBuffer(drawIndirectCount->buffer);
            bindingSet->GetVar("drawCommands")->SetBuffer(drawIndirectCommands->buffer);
            bindingSet->GetVar("data")->SetValue(&data, sizeof(CullingPassData));


            cmd.BindPipelineState(pipelineState);
            cmd.BindBindingSet(pipelineState, bindingSet);

            cmd.Dispatch(std::ceil(instances.instanceCount / 128), 1, 1);
        }

        void Destroy() override
        {
            Graphics::DestroyComputePipelineState(pipelineState);
            Graphics::DestroyBindingSet(bindingSet);
        }
    };

    CullingOutput CullingPassSetup(RenderGraph& rg)
    {
        RenderGraphResource* drawIndirectCommands = rg.Create(RenderGraphResourceCreation{
            .name = "drawIndirectCommands",
            .type = RenderGraphResourceType::Buffer
        });

        RenderGraphResource* drawIndirectCount = rg.Create(RenderGraphResourceCreation{
            .name = "drawIndirectCount",
            .type = RenderGraphResourceType::Buffer,
            .bufferCreation = {
                .usage = BufferUsage::StorageBuffer,
                .size = sizeof(u32),
                .allocation = BufferAllocation::GPUOnly
            }
        });

        rg.AddPass("CullingPass", RenderGraphPassType::Compute)
          .Write(drawIndirectCommands)
          .Handler<CullingPass>(drawIndirectCommands, drawIndirectCount);

        return CullingOutput{
            .drawIndirectCommands = drawIndirectCommands,
            .drawIndirectCount = drawIndirectCount
        };
    }
}
