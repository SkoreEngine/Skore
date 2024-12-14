#include "GBufferPass.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGlobals.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderProxy.hpp"
#include "Skore/Graphics/Assets/MeshAsset.hpp"
#include "Skore/Graphics/Assets/ShaderAsset.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
    struct alignas(16) SceneData
    {
        Mat4 viewProjection;
        Mat4 prevViewProjection;
        Vec2 currentJitter;
        Vec2 previousJitter;
    };

    struct PushConst
    {
        Mat4 matrix;
        Mat4 prevMatrix;
        u32 materialIndex;
        u32 vertexOffset;
        u32 _pad1;
        u32 _pad2;
    };

    struct GBufferPass : RenderGraphPassHandler
    {
        PipelineState pipelineState{};
        BindingSet*   bindingSet{};
        RenderProxy*  renderProxy = nullptr;

        void Init() override
        {
            if (rg->GetScene())
            {
                renderProxy = rg->GetScene()->GetProxy<RenderProxy>();
            }

            GraphicsPipelineCreation graphicsPipelineCreation{
                .shaderState = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Passes/GBufferRender.raster")->GetDefaultState(),
                .renderPass = pass->GetRenderPass(),
                .depthWrite = true,
                .cullMode = CullMode::Back,
                .compareOperator = CompareOp::Less,
            };

            pipelineState = Graphics::CreateGraphicsPipelineState(graphicsPipelineCreation);
            bindingSet = Graphics::CreateBindingSet(graphicsPipelineCreation.shaderState);
        }

        void Render(RenderCommands& cmd) override
        {
            if (renderProxy == nullptr)
            {
                return;
            }

            const CameraData& cameraData = rg->GetCameraData();

            SceneData data{
                .viewProjection = cameraData.projView,
                .prevViewProjection = cameraData.lastProjView,
                .currentJitter = cameraData.jitter,
                .previousJitter = cameraData.previousJitter
            };

            bindingSet->GetVar("scene")->SetValue(&data, sizeof(SceneData));
            bindingSet->GetVar("vertices")->SetBuffer(RenderGlobals::GetGlobalVertexBuffer());
            bindingSet->GetVar("instances")->SetBuffer(renderProxy->instanceBuffer);
            bindingSet->GetVar("transformBuffer")->SetBuffer(renderProxy->transformBuffer);
            bindingSet->GetVar("prevTransformBuffer")->SetBuffer(renderProxy->prevTransformBuffer);

            cmd.BindPipelineState(pipelineState);
            cmd.BindBindingSet(pipelineState, bindingSet);
            cmd.BindDescriptorSet(pipelineState, RenderGlobals::GetMaterialDescriptor(), 1);
            cmd.BindDescriptorSet(pipelineState, RenderGlobals::GetBindlessResources(), 2);
            cmd.BindIndexBuffer(RenderGlobals::GetGlobalIndexBuffer());
            cmd.DrawIndexedIndirect(renderProxy->indirectDrawBuffer, 0, renderProxy->instanceCount, sizeof(DrawIndexedIndirectArguments));
        }

        void PostRender(RenderCommands& cmd) override
        {
            if (renderProxy)
            {
                BufferCopyInfo bufferCopyInfo{
                    .size = sizeof(Mat4) * renderProxy->instanceCount
                };

                cmd.CopyBuffer(renderProxy->transformBuffer, renderProxy->prevTransformBuffer, &bufferCopyInfo);
            }
        }

        void Destroy() override
        {
            Graphics::DestroyBindingSet(bindingSet);
            Graphics::DestroyGraphicsPipelineState(pipelineState);
        }
    };


    GBufferOutput GBufferPassSetup(RenderGraph& rg)
    {
        GBufferOutput output{};

        //gbuffer textures
        output.gbuffer1 = rg.Create(RenderGraphResourceCreation{
            .name = "gbuffer1",
            .type = RenderGraphResourceType::Attachment,
            .scale = {1, 1},
            .format = Format::RGBA
        });

        output.gbuffer2 = rg.Create(RenderGraphResourceCreation{
            .name = "gbuffer2",
            .type = RenderGraphResourceType::Attachment,
            .scale = {1, 1},
            .format = Format::RG
        });

        output.gbuffer3 = rg.Create(RenderGraphResourceCreation{
            .name = "gbuffer3",
            .type = RenderGraphResourceType::Attachment,
            .scale = {1, 1},
            .format = Format::RG16F
        });

        output.emissive = rg.Create(RenderGraphResourceCreation{
            .name = "emissive",
            .type = RenderGraphResourceType::Attachment,
            .scale = {1, 1},
            .format = Format::R11G11B10UF
        });

        output.velocity = rg.Create(RenderGraphResourceCreation{
            .name = "velocity",
            .type = RenderGraphResourceType::Attachment,
            .scale = {1, 1},
            .format = Format::RG16F
        });

        output.depth = rg.Create(RenderGraphResourceCreation{
            .name = "depth",
            .type = RenderGraphResourceType::Attachment,
            .scale = {1, 1},
            .format = Format::Depth,
        });

        rg.AddPass("GBuffer", RenderGraphPassType::Graphics)
          .Write(output.gbuffer1)
          .Write(output.gbuffer2)
          .Write(output.gbuffer3)
          .Write(output.emissive)
          .Write(output.velocity)
          .Write(output.depth)
          .ClearColor(Color::BLACK.ToVec4())
          .ClearDepth(true)
          .Handler<GBufferPass>();

        return output;
    }
}
