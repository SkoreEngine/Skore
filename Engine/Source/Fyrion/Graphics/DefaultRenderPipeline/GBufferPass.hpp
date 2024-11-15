#pragma once
#include "Fyrion/Graphics/Graphics.hpp"
#include "Fyrion/Graphics/RenderGraph.hpp"
#include "Fyrion/Graphics/Assets/MeshAsset.hpp"
#include "Fyrion/Graphics/Assets/ShaderAsset.hpp"
#include "Fyrion/Scene/Scene.hpp"
#include "Fyrion/Scene/Service/RenderService.hpp"


namespace Fyrion
{
    struct SceneData
    {
        Mat4 viewProjection;
    };

    struct GBufferPass : RenderGraphPassHandler
    {

        PipelineState  pipelineState{};
        BindingSet*    bindingSet{};
        RenderService* renderService = nullptr;

        void Init() override
        {
            if (rg->GetScene())
            {
                renderService = rg->GetScene()->GetService<RenderService>();
            }

            GraphicsPipelineCreation graphicsPipelineCreation{
                .shader = Assets::LoadByPath<ShaderAsset>("Fyrion://Shaders/Passes/GBufferRender.raster"),
                .renderPass = pass->GetRenderPass(),
                .depthWrite = true,
                .cullMode = CullMode::Back,
                .compareOperator = CompareOp::Less,
            };

            pipelineState = Graphics::CreateGraphicsPipelineState(graphicsPipelineCreation);
            bindingSet = Graphics::CreateBindingSet(graphicsPipelineCreation.shader);
        }

        void Render(RenderCommands& cmd) override
        {
            const CameraData& cameraData = rg->GetCameraData();

            SceneData data{.viewProjection = cameraData.projView};
            bindingSet->GetVar("scene")->SetValue(&data, sizeof(SceneData));

            cmd.BindPipelineState(pipelineState);
            cmd.BindBindingSet(pipelineState, bindingSet);

            if (renderService != nullptr)
            {
                for (MeshRenderData& meshRenderData : renderService->GetMeshesToRender())
                {
                    if (MeshAsset* mesh = meshRenderData.mesh)
                    {
                        Span<MeshPrimitive> primitives = mesh->GetPrimitives();

                        cmd.BindVertexBuffer(mesh->GetVertexBuffer());
                        cmd.BindIndexBuffer(mesh->GetIndexBuffer());

                        cmd.PushConstants(pipelineState, ShaderStage::Vertex, &meshRenderData.matrix, sizeof(Mat4));

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
}
