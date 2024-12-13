#include "RenderProxy.hpp"

#include "RenderGlobals.hpp"
#include "Assets/MaterialAsset.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Assets/TextureAsset.hpp"

namespace Skore
{
    struct InstanceData
    {
        Mat4 current;
        Mat4 previous;
        u32  materialIndex;
        u32  vertexOffset;
        u32  _pad0;
        u32  _pad1;
    };

    RenderProxy::RenderProxy()
    {
        toCubemap.Init({512, 512}, Format::RGBA16F);
        diffuseIrradianceGenerator.Init({64, 64});
        specularMapGenerator.Init({128, 128}, 6);

        instanceBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer,
            .size = sizeof(InstanceData) * 500000,
            .allocation = BufferAllocation::TransferToCPU
        });

        indirectDrawBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer | BufferUsage::IndirectBuffer,
            .size = sizeof(DrawIndexedIndirectArguments) * 500000,
            .allocation = BufferAllocation::GPUOnly
        });

        // instanceArrayBuffer = Graphics::CreateArrayBuffer({
        //     .usage = BufferUsage::StorageBuffer | BufferUsage::IndirectBuffer,
        //     .initialSize = sizeof(DrawIndexedIndirectArguments) * 5000,
        //     .allocation = BufferAllocation::GPUOnly
        // });
    }

    RenderProxy::~RenderProxy()
    {
        Graphics::WaitQueue();
        Graphics::DestroyBuffer(instanceBuffer);
        Graphics::DestroyBuffer(indirectDrawBuffer);

        specularMapGenerator.Destroy();
        diffuseIrradianceGenerator.Destroy();
        toCubemap.Destroy();
    }

    void RenderProxy::AddMesh(VoidPtr pointer, MeshAsset* mesh, Span<MaterialAsset*> materials, const Mat4& matrix)
    {
        //TODO need to update cache.

        auto it = meshRendersLookup.Find(pointer);
        if (it == meshRendersLookup.end())
        {
            meshRendersLookup.Emplace(pointer, meshRenders.Size()).first;

            MeshRenderData& render = meshRenders.EmplaceBack();
            render.pointer = pointer;
            render.mesh = mesh;
            render.drawCalls.Resize(materials.Size());

            MeshLookupData* meshLookupData = RenderGlobals::GetMeshLookupData(mesh);

            for (int i = 0; i < materials.Size(); ++i)
            {
                MeshPrimitive& primitive = mesh->primitives[i];

                render.drawCalls[i].instanceIndex = instanceBufferCurrentIndex++;

                InstanceData& instanceData = *reinterpret_cast<InstanceData*>(
                    static_cast<u8*>(Graphics::GetBufferMappedMemory(instanceBuffer)) +
                    render.drawCalls[i].instanceIndex * sizeof(InstanceData));

                instanceData.current = matrix;
                instanceData.previous = matrix;
                instanceData.materialIndex = RenderGlobals::FindOrCreateMaterialInstance(materials[i]);
                instanceData.vertexOffset = meshLookupData->vertexBufferOffset;

                DrawIndexedIndirectArguments indirectArguments{
                    .indexCountPerInstance = primitive.indexCount,
                    .instanceCount = 1,
                    .startIndexLocation = primitive.firstIndex + static_cast<u32>(meshLookupData->indexBufferOffset / sizeof(u32)),
                    .startInstanceLocation = static_cast<u32>(render.drawCalls[i].instanceIndex),
                };

                Graphics::UpdateBufferData({
                    .buffer = indirectDrawBuffer,
                    .data = &indirectArguments,
                    .size = sizeof(DrawIndexedIndirectArguments),
                    .dstOffset = render.drawCalls[i].instanceIndex * sizeof(DrawIndexedIndirectArguments)
                });
                indirectDrawCount++;
            }
        }
    }

    void RenderProxy::UpdateMeshTransform(VoidPtr pointer, const Mat4& matrix)
    {
        auto it = meshRendersLookup.Find(pointer);
        if (it != meshRendersLookup.end())
        {
            for (RenderDrawCall& drawCall : meshRenders[it->second].drawCalls)
            {
                InstanceData& instanceData = *reinterpret_cast<InstanceData*>(
                    static_cast<u8*>(Graphics::GetBufferMappedMemory(instanceBuffer)) +
                    drawCall.instanceIndex * sizeof(InstanceData));

                instanceData.previous = instanceData.current;
                instanceData.current = matrix;
            }
        }
    }

    void RenderProxy::RemoveMesh(VoidPtr pointer)
    {
        if (auto it = meshRendersLookup.Find(pointer); it != meshRendersLookup.end())
        {
            if (!meshRenders.Empty())
            {
                MeshRenderData& back = meshRenders.Back();
                meshRendersLookup[back.pointer] = it->second;
                meshRenders[it->second] = Traits::Move(back);
                meshRenders.PopBack();
            }
            meshRendersLookup.Erase(it);


            //TODO: need to free drawcall.index
        }
    }


    Span<MeshRenderData> RenderProxy::GetMeshesToRender()
    {
        return meshRenders;
    }

    void RenderProxy::AddLight(VoidPtr address, const LightProperties& directionalLight)
    {
        auto it = lightsLookup.Find(address);
        if (it == lightsLookup.end())
        {
            it = lightsLookup.Emplace(address, lights.Size()).first;
            lights.EmplaceBack();
        }

        lights[it->second] = LightRenderData{address, directionalLight};


        if (directionalLight.type == LightType::Directional && directionalLight.castShadows && directionalShadowCaster == U32_MAX)
        {
            directionalShadowCaster = it->second;
        }
    }

    void RenderProxy::RemoveLight(VoidPtr address)
    {
        if (auto it = lightsLookup.Find(address); it != lightsLookup.end())
        {
            if (!lights.Empty())
            {
                LightRenderData& back = lights.Back();
                lightsLookup[back.pointer] = it->second;
                lights[it->second] = Traits::Move(back);
                lights.PopBack();
            }

            lightsLookup.Erase(it);

            //replace directional shadow caster
            if (it->second == directionalShadowCaster)
            {
                directionalShadowCaster = U32_MAX;
                for (int i = 0; i < lights.Size(); ++i)
                {
                    if (lights[i].properties.type == LightType::Directional && lights[i].properties.castShadows)
                    {
                        directionalShadowCaster = i;
                        break;
                    }
                }
            }
        }
    }

    Span<LightRenderData> RenderProxy::GetLights()
    {
        return lights;
    }

    Optional<LightProperties> RenderProxy::GetDirectionalShadowCaster()
    {
        if (directionalShadowCaster != U32_MAX)
        {
            return std::make_optional(lights[directionalShadowCaster].properties);
        }
        return {};
    }

    void RenderProxy::SetPanoramaSky(TextureAsset* panoramaSky)
    {
        if (panoramaSky != nullptr && this->panoramaSky != panoramaSky)
        {
            Texture texture = panoramaSky->GetTexture();

            RenderCommands& cmd = Graphics::GetCmd();
            cmd.Begin();
            toCubemap.Convert(cmd, texture);
            cmd.SubmitAndWait(Graphics::GetMainQueue());

            Texture cubemap = toCubemap.GetTexture();

            cmd.Begin();
            diffuseIrradianceGenerator.Generate(cmd, cubemap);
            specularMapGenerator.Generate(cmd, cubemap);
            cmd.SubmitAndWait(Graphics::GetMainQueue());

            //toCubemap.Destroy();
        }

        this->panoramaSky = panoramaSky;
    }

    TextureAsset* RenderProxy::GetPanoramaSky() const
    {
        return panoramaSky;
    }

    Texture RenderProxy::GetDiffuseIrradiance()
    {
        return diffuseIrradianceGenerator.GetTexture();
    }

    Texture RenderProxy::GetSpecularMap()
    {
        return specularMapGenerator.GetTexture();
    }

    Texture RenderProxy::GetSkyCubeMap()
    {
        return toCubemap.GetTexture();
    }

    void RenderProxy::AddCamera(VoidPtr pointer, const CameraData& camera)
    {
        cameraData = MakeOptional<CameraStorage>(CameraStorage{pointer, camera});
    }

    void RenderProxy::RemoveCamera(VoidPtr pointer)
    {
        if (cameraData && cameraData->ptr == pointer)
        {
            cameraData = {};
        }
    }

    const CameraData* RenderProxy::GetCamera() const
    {
        if (cameraData)
        {
            return &cameraData->data;
        }
        return nullptr;
    }


    void RenderProxy::RegisterType(NativeTypeHandler<RenderProxy>& type)
    {

    }
}
