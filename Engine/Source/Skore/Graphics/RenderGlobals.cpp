#include "RenderGlobals.hpp"

#include <Shaders/Bindings.h>

#include "Graphics.hpp"
#include "GraphicsTypes.hpp"
#include "RenderProxy.hpp"
#include "Assets/MeshAsset.hpp"
#include "Skore/Core/HashMap.hpp"


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
        u32  _pad0{};
    };

    namespace
    {
        DescriptorSet bindlessResources;
        DescriptorSet materialDescriptor;

        Buffer globalVertexBuffer;
        Buffer globalIndexBuffer;

        Sampler materialSampler;

        HashMap<UUID, u32> materials;
        u32                currentBindlessIndex = 1;

        Buffer materialStorageBuffer;
        u32    currentMaterialCount = 0;

        HashMap<UUID, SharedPtr<MeshLookupData>> meshLookupData;

        u64 globalVertexBufferOffset = 0;
        u64 globalIndexBufferOffset = 0;
    }

    void RenderGlobalsInit()
    {
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

        materialSampler = Graphics::CreateSampler(SamplerCreation{
            .filter = SamplerFilter::Linear,
            .addressMode = TextureAddressMode::Repeat,
            .comparedEnabled = true,
            .anisotropyEnable = true
        });

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

        MaterialConstants materialConstants{
            .baseColorAlphaCutOff = {1, 1, 1, 0.5},
            .metallicRoughness = {0.0, 1.0, 0.0, 0.0}
        };

        Graphics::UpdateBufferData({
            .buffer = materialStorageBuffer,
            .data = &materialConstants,
            .size = sizeof(MaterialConstants),
        });
        currentMaterialCount++;

        globalVertexBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer,
            .size = 80097152, //TODO find a good number here
            .allocation = BufferAllocation::GPUOnly
        });

        globalIndexBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::IndexBuffer,
            .size = 80097152, //TODO find a good number here
            .allocation = BufferAllocation::GPUOnly
        });
    }

    void RenderGlobalsShutdown()
    {
        Graphics::DestroyDescriptorSet(bindlessResources);
        Graphics::DestroyDescriptorSet(materialDescriptor);
        Graphics::DestroySampler(materialSampler);
        Graphics::DestroyBuffer(materialStorageBuffer);
        Graphics::DestroyBuffer(globalVertexBuffer);
        Graphics::DestroyBuffer(globalIndexBuffer);
    }


    DescriptorSet RenderGlobals::GetBindlessResources()
    {
        return bindlessResources;
    }

    DescriptorSet RenderGlobals::GetMaterialDescriptor()
    {
        return materialDescriptor;
    }

    Buffer RenderGlobals::GetGlobalVertexBuffer()
    {
        return globalVertexBuffer;
    }

    Buffer RenderGlobals::GetGlobalIndexBuffer()
    {
        return globalIndexBuffer;
    }

    MeshLookupData* RenderGlobals::GetMeshLookupData(const MeshAsset* meshAsset)
    {
        auto it = meshLookupData.Find(meshAsset->GetUUID());
        if (it == meshLookupData.end())
        {
            it = meshLookupData.Emplace(meshAsset->GetUUID(), MakeShared<MeshLookupData>()).first;
            MeshLookupData* data = it->second.Get();

            data->vertexBufferOffset = globalVertexBufferOffset;
            data->indexBufferOffset = globalIndexBufferOffset;

            usize vertexSize = meshAsset->GetVertexSize();
            usize indexSize = meshAsset->GetIndexSize();

            Array<u8> buffer;

            //vertex data
            {
                buffer.Resize(meshAsset->GetVertexSize());
                meshAsset->LoadVertexData(buffer);

                //TODO RESIZE BUFFERS!!!
                Graphics::UpdateBufferData({
                    .buffer = globalVertexBuffer,
                    .data = buffer.Data(),
                    .size = vertexSize,
                    .dstOffset = data->vertexBufferOffset,
                });

                globalVertexBufferOffset += buffer.Size();
            }

            //index data
            {
                meshAsset->LoadIndexData(buffer);

                //TODO RESIZE BUFFERS!!!
                Graphics::UpdateBufferData({
                    .buffer = globalIndexBuffer,
                    .data = buffer.Data(),
                    .size = indexSize,
                    .dstOffset = data->indexBufferOffset,
                });

                globalIndexBufferOffset += indexSize;
            }
        }
        return it->second.Get();
    }


    u32 RenderGlobals::FindOrCreateMaterialInstance(const MaterialAsset* materialAsset)
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


            //TODO RESIZE BUFFER!!!
            u32 index = currentMaterialCount++;

            Graphics::UpdateBufferData({
                .buffer = materialStorageBuffer,
                .data = &materialConstants,
                .size = sizeof(MaterialConstants),
                .dstOffset = sizeof(MaterialConstants) * index
            });

            it = materials.Insert(materialAsset->GetUUID(), index).first;
        }

        return it->second;
    }
}
