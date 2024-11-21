#pragma once
#include "Fyrion/Graphics/Graphics.hpp"
#include "Fyrion/Graphics/RenderGraph.hpp"
#include "Fyrion/Graphics/Assets/MeshAsset.hpp"
#include "Fyrion/Graphics/Assets/ShaderAsset.hpp"
#include "Fyrion/Scene/Scene.hpp"
#include "Fyrion/Graphics/RenderProxy.hpp"


namespace Fyrion
{
    struct SceneData
    {
        Mat4 viewProjection;
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
                .shaderState = Assets::LoadByPath<ShaderAsset>("Fyrion://Shaders/Passes/GBufferRender.raster")->GetDefaultState(),
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

            SceneData data{.viewProjection = cameraData.projView};
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
