#include "RenderProxy.hpp"

#include "Assets/MaterialAsset.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Assets/TextureAsset.hpp"

#include "Shaders/Bindings.h"

namespace Skore
{
    struct alignas(16) MaterialConstants
    {
        Vec4 baseColorAlphaCutOff;
        Vec4 uvScaleNormalMultiplierAlphaMode;
        Vec4 metallicRoughness;
        Vec4 emissiveFactor;
        u32  baseColorIndex{};
        u32  normalIndex{};
        u32  roughnessIndex{};
        u32  metallicIndex{};
        u32  metallicRoughnessIndex{};
        u32  emissiveIndex{};
        u32  occlusionIndex{};
        u32 _pad0{};
    };

    RenderProxy::RenderProxy()
    {
        toCubemap.Init({512, 512}, Format::RGBA16F);
        diffuseIrradianceGenerator.Init({64, 64});
        specularMapGenerator.Init({128, 128}, 6);

        bindlessResources = Graphics::CreateDescriptorSet({
            .bindless = true,
            .bindings = {
                DescriptorBinding{
                    .binding = SK_BINDLESS_TEXTURES_SLOT,
                    .count = MaxBindlessResources,
                    .descriptorType = DescriptorType::SampledImage,
                }
            }
        });

        Graphics::WriteDescriptorSet(bindlessResources, {
                                         DescriptorSetWriteInfo{
                                             .binding = 0,
                                             .descriptorType = DescriptorType::SampledImage,
                                             .arrayElement = 0,
                                             .texture = Graphics::GetDefaultTexture(),
                                         }
                                     });

        materialSampler = Graphics::CreateSampler({});

        materialStorageBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer,
            .size = 1000 * sizeof(MaterialConstants),
            .allocation = BufferAllocation::TransferToGPU
        });

        materialDescriptor = Graphics::CreateDescriptorSet({
            .bindings = {
                DescriptorBinding{
                    .binding = 0,
                    .count = 1,
                    .descriptorType = DescriptorType::StorageBuffer
                },
                DescriptorBinding{
                    .binding = 1,
                    .count = 1,
                    .descriptorType = DescriptorType::Sampler
                },
            }
        });

        Graphics::WriteDescriptorSet(materialDescriptor, {
                                         DescriptorSetWriteInfo{
                                             .binding = 0,
                                             .descriptorType = DescriptorType::StorageBuffer,
                                             .buffer = materialStorageBuffer
                                         },
                                         DescriptorSetWriteInfo{
                                             .binding = 1,
                                             .descriptorType = DescriptorType::Sampler,
                                             .sampler = materialSampler
                                         },
                                     });

        MaterialConstants materialConstants {
            .baseColorAlphaCutOff = {1, 1, 1, 0.5},
            .metallicRoughness = {0.0, 1.0, 0.0, 0.0}
        };

        Graphics::UpdateBufferData({
            .buffer = materialStorageBuffer,
            .data = &materialConstants,
            .size = sizeof(MaterialConstants),
            .offset = 0
        });
        currentMaterialCount++;

    }

    RenderProxy::~RenderProxy()
    {
        Graphics::WaitQueue();

        Graphics::DestroyDescriptorSet(bindlessResources);
        Graphics::DestroyDescriptorSet(materialDescriptor);
        Graphics::DestroySampler(materialSampler);
        Graphics::DestroyBuffer(materialStorageBuffer);

        specularMapGenerator.Destroy();
        diffuseIrradianceGenerator.Destroy();
        toCubemap.Destroy();
    }

    void RenderProxy::SetMesh(VoidPtr pointer, MeshAsset* mesh, Span<MaterialAsset*> materials, const Mat4& matrix)
    {
        if (mesh == nullptr)
        {
            RemoveMesh(pointer);
            return;
        }

        auto it = meshRendersLookup.Find(pointer);
        if (it == meshRendersLookup.end())
        {
            it = meshRendersLookup.Emplace(pointer, meshRenders.Size()).first;
            MeshRenderData& render = meshRenders.EmplaceBack();
            render.prevMatrix = matrix;
        }

        meshRenders[it->second].pointer = pointer;
        meshRenders[it->second].mesh = mesh;

        meshRenders[it->second].materials.Resize(materials.Size());

        for (int i = 0; i < materials.Size(); ++i)
        {
            meshRenders[it->second].materials[i] = FindOrCreateMaterialInstance(materials[i]);
        }

        //meshRenders[it->second].materials = materials;
        meshRenders[it->second].matrix = matrix;
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

    u32 RenderProxy::FindOrCreateMaterialInstance(const MaterialAsset* materialAsset)
    {
        if (materialAsset == nullptr)
        {
            return U32_MAX;
        }

        auto it = materials.Find(materialAsset->GetUUID());
        if (it == materials.end())
        {
            MaterialConstants materialConstants{
                .baseColorAlphaCutOff = Math::MakeVec4(materialAsset->GetBaseColor().ToVec3(), materialAsset->GetAlphaCutoff()),
                .uvScaleNormalMultiplierAlphaMode = Math::MakeVec4(materialAsset->GetUvScale(), Math::MakeVec2(materialAsset->GetNormalMultiplier(), static_cast<f32>(materialAsset->GetAlphaMode()))),
                .metallicRoughness = Vec4{materialAsset->GetRoughness(), materialAsset->GetMetallic(), 0.0f, 0.0f},
                .emissiveFactor = Math::MakeVec4(materialAsset->GetEmissiveFactor(), 0.0),
            };

            Array<DescriptorSetWriteInfo> infos;
            if (materialAsset->GetBaseColorTexture())
            {
                materialConstants.baseColorIndex = currentBindlessIndex++;
                infos.EmplaceBack(DescriptorSetWriteInfo{
                    .binding = 0,
                    .descriptorType = DescriptorType::SampledImage,
                    .arrayElement = materialConstants.baseColorIndex,
                    .texture = materialAsset->GetBaseColorTexture()->GetTexture(),
                });
            }

            if (materialAsset->GetNormalTexture())
            {
                materialConstants.normalIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialConstants.normalIndex,
                                                     .texture = materialAsset->GetNormalTexture()->GetTexture(),
                                                 }
                                             });
            }

            if (materialAsset->GetMetallicTexture())
            {
                materialConstants.metallicIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialConstants.metallicIndex,
                                                     .texture = materialAsset->GetMetallicTexture()->GetTexture(),
                                                 }
                                             });
            }

            if (materialAsset->GetRoughnessTexture())
            {
                materialConstants.roughnessIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialConstants.roughnessIndex,
                                                     .texture = materialAsset->GetRoughnessTexture()->GetTexture(),
                                                 }
                                             });
            }

            if (materialAsset->GetMetallicRoughnessTexture())
            {
                materialConstants.metallicRoughnessIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialConstants.metallicRoughnessIndex,
                                                     .texture = materialAsset->GetMetallicRoughnessTexture()->GetTexture(),
                                                 }
                                             });
            }

            if (materialAsset->GetEmissiveTexture())
            {
                materialConstants.emissiveIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialConstants.emissiveIndex,
                                                     .texture = materialAsset->GetEmissiveTexture()->GetTexture(),
                                                 }
                                             });
            }

            if (!infos.Empty())
            {
                Graphics::WriteDescriptorSet(bindlessResources, infos);
            }

            u32 index = currentMaterialCount++;

            Graphics::UpdateBufferData({
                .buffer = materialStorageBuffer,
                .data = &materialConstants,
                .size = sizeof(MaterialConstants),
                .offset = sizeof(MaterialConstants) * index
            });

            it = materials.Insert(materialAsset->GetUUID(), index).first;
        }

        return it->second;
    }


    void RenderProxy::RegisterType(NativeTypeHandler<RenderProxy>& type)
    {

    }
}
