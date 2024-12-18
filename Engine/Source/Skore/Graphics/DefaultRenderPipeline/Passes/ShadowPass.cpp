#include "ShadowPass.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderProxy.hpp"
#include "Skore/Graphics/Assets/MeshAsset.hpp"
#include "Skore/Graphics/DefaultRenderPipeline/DefaultRenderPipelineTypes.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
    struct ShadowPass : RenderGraphPassHandler
    {
        float cascadeSplitLambda = 0.75f;

        PipelineState pipelineState{};

        RenderGraphResource* shadowMap;

        TextureView shadowMapTextureViews[SK_SHADOW_MAP_CASCADE_COUNT];
        RenderPass  shadowMapPass[SK_SHADOW_MAP_CASCADE_COUNT];

        ShadowMapDataInfo shadowMapDataInfo{};

        RenderProxy* renderProxy = nullptr;

        ShadowPass(RenderGraphResource* shadowMap)
            : shadowMap(shadowMap) {}

        struct ShadowPushConsts
        {
            Mat4 model;
            Mat4 viewProjection;
        };

        void Init() override
        {
            if (rg->GetScene())
            {
                renderProxy = rg->GetScene()->GetProxy<RenderProxy>();
            }

            shadowMap->texture = Graphics::CreateTexture(TextureCreation{
                .extent = {SK_SHADOW_MAP_DIM, SK_SHADOW_MAP_DIM},
                .format = Format::Depth,
                .usage = TextureUsage::DepthStencil | TextureUsage::ShaderResource,
                .arrayLayers = SK_SHADOW_MAP_CASCADE_COUNT
            });

            shadowMap->reference = &shadowMapDataInfo;

            Graphics::UpdateTextureLayout(shadowMap->texture, ResourceLayout::Undefined, ResourceLayout::DepthStencilReadOnly);

            for (u32 i = 0; i < SK_SHADOW_MAP_CASCADE_COUNT; ++i)
            {
                shadowMapTextureViews[i] = Graphics::CreateTextureView(TextureViewCreation{
                    .texture = shadowMap->texture,
                    .baseArrayLayer = i,
                });

                AttachmentCreation attachmentCreation{
                    .textureView = shadowMapTextureViews[i],
                    .finalLayout = ResourceLayout::DepthStencilAttachment,
                };
                shadowMapPass[i] = Graphics::CreateRenderPass(RenderPassCreation{
                    .attachments = {&attachmentCreation, 1}
                });
            }

            GraphicsPipelineCreation graphicsPipelineCreation{
                .shaderState = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Passes/ShadowMap.raster")->GetDefaultState(),
                .renderPass = shadowMapPass[0],
                .depthWrite = true,
                .cullMode = CullMode::Front,
                .compareOperator = CompareOp::LessOrEqual,
                .stride = sizeof(VertexStride)
            };

            pipelineState = Graphics::CreateGraphicsPipelineState(graphicsPipelineCreation);
        }

        void Render(RenderCommands& cmd) override
        {
            if (!renderProxy) return;

            cmd.BeginLabel("Skore::ShadowPass", Vec4{0, 0, 0, 1});

            float cascadeSplits[SK_SHADOW_MAP_CASCADE_COUNT];

            if (auto light = renderProxy->GetDirectionalShadowCaster(); light && light->castShadows)
            {
                const CameraData& cameraData = rg->GetCameraData();

                float nearClip = cameraData.nearClip;
                float farClip = cameraData.farClip;
                float clipRange = farClip - nearClip;

                float minZ = nearClip;
                float maxZ = nearClip + clipRange;

                float range = maxZ - minZ;
                float ratio = maxZ / minZ;

                // Calculate split depths based on view camera frustum
                // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
                for (uint32_t i = 0; i < SK_SHADOW_MAP_CASCADE_COUNT; i++)
                {
                    float p = (i + 1) / static_cast<float>(SK_SHADOW_MAP_CASCADE_COUNT);
                    float log = minZ * std::pow(ratio, p);
                    float uniform = minZ + range * p;
                    float d = cascadeSplitLambda * (log - uniform) + uniform;
                    cascadeSplits[i] = (d - nearClip) / clipRange;
                }

                // Calculate orthographic projection matrix for each cascade
                float lastSplitDist = 0.0;
                for (uint32_t i = 0; i < SK_SHADOW_MAP_CASCADE_COUNT; i++)
                {
                    float splitDist = cascadeSplits[i];

                    Vec3 frustumCorners[8] = {
                        Vec3{-1.0f, 1.0f, 0.0f},
                        Vec3{1.0f, 1.0f, 0.0f},
                        Vec3{1.0f, -1.0f, 0.0f},
                        Vec3{-1.0f, -1.0f, 0.0f},
                        Vec3{-1.0f, 1.0f, 1.0f},
                        Vec3{1.0f, 1.0f, 1.0f},
                        Vec3{1.0f, -1.0f, 1.0f},
                        Vec3{-1.0f, -1.0f, 1.0f},
                    };

                    // Project frustum corners into world space
                    Mat4 invCam = Math::Inverse(cameraData.projection * cameraData.view);
                    for (uint32_t j = 0; j < 8; j++)
                    {
                        Vec4 invCorner = invCam * Vec4(frustumCorners[j], 1.0f);
                        frustumCorners[j] = Math::MakeVec3(invCorner / invCorner.w);
                    }

                    for (uint32_t j = 0; j < 4; j++)
                    {
                        Vec3 dist = frustumCorners[j + 4] - frustumCorners[j];
                        frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
                        frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
                    }

                    // Get frustum center
                    Vec3 frustumCenter = Vec3();
                    for (uint32_t j = 0; j < 8; j++)
                    {
                        frustumCenter += frustumCorners[j];
                    }
                    frustumCenter /= 8.0f;

                    float radius = 0.0f;
                    for (uint32_t j = 0; j < 8; j++)
                    {
                        float distance = Math::Len(frustumCorners[j] - frustumCenter);
                        radius = Math::Max(radius, distance);
                    }
                    radius = std::ceil(radius * 16.0f) / 16.0f;

                    Vec3 maxExtents = Vec3{radius, radius, radius};
                    Vec3 minExtents = -maxExtents;

                    Vec3 lightDir = Math::Normalize(-light->direction);
                    Mat4 lightViewMatrix = Math::LookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, Vec3{0.0f, 1.0f, 0.0f});
                    Mat4 lightOrthoMatrix = Math::Ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

                    // Store split distance and matrix in cascade
                    shadowMapDataInfo.cascadeSplit[i] = (cameraData.nearClip + splitDist * clipRange) * -1.0f;
                    shadowMapDataInfo.cascadeViewProjMat[i] = lightOrthoMatrix * lightViewMatrix;

                    lastSplitDist = cascadeSplits[i];

                    ClearDepthStencilValue depthStencilValue{
                    };

                    //render
                    cmd.BeginRenderPass(BeginRenderPassInfo{
                        .renderPass = shadowMapPass[i],
                        .depthStencil = &depthStencilValue
                    });

                    cmd.SetViewport(ViewportInfo{
                        .x = 0.0f,
                        .y = 0.0f,
                        .width = SK_SHADOW_MAP_DIM,
                        .height = SK_SHADOW_MAP_DIM,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f,
                    });

                    cmd.SetScissor(Rect{0, 0, SK_SHADOW_MAP_DIM, SK_SHADOW_MAP_DIM});
                    cmd.BindPipelineState(pipelineState);

                    for (MeshRenderData& meshRenderData : renderProxy->GetMeshesToRender())
                    {
                        if (MeshAsset* mesh = meshRenderData.mesh)
                        {
                            Span<MeshPrimitive> primitives = mesh->GetPrimitives();

                            cmd.BindVertexBuffer(mesh->GetVertexBuffer());
                            cmd.BindIndexBuffer(mesh->GetIndexBuffer());

                            ShadowPushConsts pushConsts{
                                .model = meshRenderData.matrix,
                                .viewProjection = shadowMapDataInfo.cascadeViewProjMat[i],
                            };

                            cmd.PushConstants(pipelineState, ShaderStage::Vertex, &pushConsts, sizeof(ShadowPushConsts));

                            for (MeshPrimitive& primitive : primitives)
                            {
                                if (meshRenderData.materials[primitive.materialIndex] != U32_MAX)
                                {
                                    cmd.DrawIndexed(primitive.indexCount, 1, primitive.firstIndex, 0, 0);
                                }
                            }
                        }
                    }
                    cmd.EndRenderPass();

                    cmd.ResourceBarrier(ResourceBarrierInfo{
                        .texture = shadowMap->texture,
                        .oldLayout = ResourceLayout::DepthStencilAttachment,
                        .newLayout = ResourceLayout::DepthStencilReadOnly,
                        .baseArrayLayer = i
                    });
                }
            }
            cmd.EndLabel();
        }

        void Destroy() override
        {
            for (u32 i = 0; i < SK_SHADOW_MAP_CASCADE_COUNT; ++i)
            {
                Graphics::DestroyRenderPass(shadowMapPass[i]);
                Graphics::DestroyTextureView(shadowMapTextureViews[i]);
            }

            Graphics::DestroyTexture(shadowMap->texture);
            Graphics::DestroyGraphicsPipelineState(pipelineState);
        }
    };

    void ShadowPassSetup(RenderGraph& rg, RenderGraphResource* shadowMap)
    {
        rg.AddPass("ShadowMap", RenderGraphPassType::Other)
          .Write(shadowMap)
          .Handler<ShadowPass>(shadowMap);
    }
}
