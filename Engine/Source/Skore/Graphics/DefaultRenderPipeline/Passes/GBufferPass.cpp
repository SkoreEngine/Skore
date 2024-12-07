#include "GBufferPass.hpp"

#include "Skore/Graphics/Graphics.hpp"
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
            const CameraData& cameraData = rg->GetCameraData();

            SceneData data{
                .viewProjection = cameraData.projView,
                .prevViewProjection = cameraData.lastProjView,
                .currentJitter = cameraData.jitter,
                .previousJitter = cameraData.previousJitter
            };
            bindingSet->GetVar("scene")->SetValue(&data, sizeof(SceneData));

            cmd.BindPipelineState(pipelineState);
            cmd.BindBindingSet(pipelineState, bindingSet);

            if (renderProxy != nullptr)
            {
                for (MeshRenderData& meshRenderData : renderProxy->GetMeshesToRender())
                {
                    if (MeshAsset* mesh = meshRenderData.mesh)
                    {
                        Span<MeshPrimitive> primitives = mesh->GetPrimitives();

                        cmd.BindVertexBuffer(mesh->GetVertexBuffer());
                        cmd.BindIndexBuffer(mesh->GetIndexBuffer());


                        PushConst pushConst;
                        pushConst.matrix = meshRenderData.matrix;
                        pushConst.prevMatrix = meshRenderData.prevMatrix;

                        meshRenderData.prevMatrix = meshRenderData.matrix;

                        cmd.PushConstants(pipelineState, ShaderStage::Vertex, &pushConst, sizeof(PushConst));


                        for (MeshPrimitive& primitive : primitives)
                        {
                            if (MaterialAsset* material = meshRenderData.materials[primitive.materialIndex])
                            {
                                cmd.BindBindingSet(pipelineState, material->GetBindingSet());
                                cmd.DrawIndexed(primitive.indexCount, 1, primitive.firstIndex, 0, 0);
                            }
                        }
                    }
                }
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
