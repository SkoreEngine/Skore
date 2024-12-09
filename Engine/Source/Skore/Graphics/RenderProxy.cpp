#include "RenderProxy.hpp"

#include "Assets/MaterialAsset.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Assets/TextureAsset.hpp"

#include "Shaders/Bindings.h"

namespace Skore
{

    struct alignas(16) TextureIndices
    {
        u32 baseColorIndex;
        u32 normalIndex;
        u32 roughnessIndex;
        u32 metallicIndex;
        u32 metallicRoughnessIndex;
        u32 emissiveIndex;
    };


    struct alignas(16) MaterialData
    {
        Vec4 baseColorAlphaCutOff;
        Vec4 uvScaleNormalMultiplierAlphaMode;
        Vec4 metallicRoughness;
        Vec4 emissiveFactor;
        TextureIndices textureIndices{};
    };


    MaterialInstance::~MaterialInstance()
    {
        Graphics::DestroyBindingSet(bindingSet);
    }

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
    }

    RenderProxy::~RenderProxy()
    {
        Graphics::WaitQueue();

        Graphics::DestroyDescriptorSet(bindlessResources);
        Graphics::DestroySampler(materialSampler);

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

    MaterialInstance* RenderProxy::FindOrCreateMaterialInstance(const MaterialAsset* materialAsset)
    {
        auto it = materials.Find(materialAsset->GetUUID());
        if (it == materials.end())
        {
            it = materials.Emplace(materialAsset->GetUUID(), MakeShared<MaterialInstance>()).first;
            MaterialInstance* instance = it->second.Get();


            MaterialData materialData{
                .baseColorAlphaCutOff = Math::MakeVec4(materialAsset->GetBaseColor().ToVec3(), materialAsset->GetAlphaCutoff()),
                .uvScaleNormalMultiplierAlphaMode = Math::MakeVec4(materialAsset->GetUvScale(), Math::MakeVec2(materialAsset->GetNormalMultiplier(), static_cast<f32>(materialAsset->GetAlphaMode()))),
                .metallicRoughness = Vec4{materialAsset->GetRoughness(), materialAsset->GetMetallic(), 0.0f, 0.0f},
                .emissiveFactor = Math::MakeVec4(materialAsset->GetEmissiveFactor(), 0.0),
                .textureIndices = {}
            };

            instance->bindingSet = Graphics::CreateBindingSet(Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Passes/GBufferRender.raster")->GetDefaultState());

            Array<DescriptorSetWriteInfo> infos;

            if (materialAsset->GetBaseColorTexture())
            {
                materialData.textureIndices.baseColorIndex = currentBindlessIndex++;
                infos.EmplaceBack(DescriptorSetWriteInfo{
                    .binding = 0,
                    .descriptorType = DescriptorType::SampledImage,
                    .arrayElement = materialData.textureIndices.baseColorIndex,
                    .texture = materialAsset->GetBaseColorTexture()->GetTexture(),
                });
            }

            instance->bindingSet->GetVar("defaultSampler")->SetSampler(materialSampler);


            if (materialAsset->GetNormalTexture())
            {
                materialData.textureIndices.normalIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialData.textureIndices.normalIndex,
                                                     .texture = materialAsset->GetNormalTexture()->GetTexture(),
                                                 }
                                             });
            }

            if (materialAsset->GetMetallicTexture())
            {
                materialData.textureIndices.metallicIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialData.textureIndices.metallicIndex,
                                                     .texture = materialAsset->GetMetallicTexture()->GetTexture(),
                                                 }
                                             });
            }

            if (materialAsset->GetRoughnessTexture())
            {
                materialData.textureIndices.roughnessIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialData.textureIndices.roughnessIndex,
                                                     .texture = materialAsset->GetRoughnessTexture()->GetTexture(),
                                                 }
                                             });
            }

            if (materialAsset->GetMetallicRoughnessTexture())
            {
                materialData.textureIndices.metallicRoughnessIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialData.textureIndices.metallicRoughnessIndex,
                                                     .texture = materialAsset->GetMetallicRoughnessTexture()->GetTexture(),
                                                 }
                                             });
            }

            if (materialAsset->GetEmissiveTexture())
            {
                materialData.textureIndices.emissiveIndex = currentBindlessIndex++;
                Graphics::WriteDescriptorSet(bindlessResources, {
                                                 DescriptorSetWriteInfo{
                                                     .binding = 0,
                                                     .descriptorType = DescriptorType::SampledImage,
                                                     .arrayElement = materialData.textureIndices.emissiveIndex,
                                                     .texture = materialAsset->GetEmissiveTexture()->GetTexture(),
                                                 }
                                             });
            }


            if (!infos.Empty())
            {
                Graphics::WriteDescriptorSet(bindlessResources, infos);
            }


            instance->bindingSet->GetVar("material")->SetValue(&materialData, sizeof(MaterialData));

        }
        return it->second.Get();
    }


    void RenderProxy::RegisterType(NativeTypeHandler<RenderProxy>& type)
    {

    }
}
