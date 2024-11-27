#pragma once
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/Assets/MeshAsset.hpp"
#include "Skore/Graphics/Assets/ShaderAsset.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Graphics/RenderProxy.hpp"


namespace Skore
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
