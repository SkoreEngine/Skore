#include "RenderUtils.hpp"

#include <algorithm>
#include <mikktspace.h>

#include "Graphics.hpp"
#include "RenderPipeline.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/IO/Asset.hpp"
#include "Skore/Graphics/Assets/ShaderAsset.hpp"


namespace Skore
{

    namespace
    {
        Logger& logger = Logger::GetLogger("Skore::RenderUtils", LogLevel::Debug);

        struct SpecularMapFilterSettings
        {
            alignas(16) f32 roughness;
        };

        struct UserData
        {
            Array<VertexStride>& vertices;
            const Array<u32>&    indices;
        };


        i32 GetNumFaces(const SMikkTSpaceContext* pContext)
        {
            UserData& mesh = *static_cast<UserData*>(pContext->m_pUserData);
            return (i32)mesh.indices.Size() / 3;
        }

        i32 GetNumVerticesOfFace(const SMikkTSpaceContext* pContext, const int iFace)
        {
            return 3;
        }

        u32 GetVertexIndex(const SMikkTSpaceContext* context, i32 iFace, i32 iVert)
        {
            UserData& mesh = *static_cast<UserData*>(context->m_pUserData);
            auto      faceSize = GetNumVerticesOfFace(context, iFace);
            auto      indicesIndex = (iFace * faceSize) + iVert;
            return mesh.indices[indicesIndex];
        }

        void GetPosition(const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert)
        {
            UserData& mesh = *static_cast<UserData*>(pContext->m_pUserData);

            VertexStride& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
            fvPosOut[0] = v.position.x;
            fvPosOut[1] = v.position.y;
            fvPosOut[2] = v.position.z;
        }

        void GetNormal(const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert)
        {
            UserData&     mesh = *static_cast<UserData*>(pContext->m_pUserData);
            VertexStride& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
            fvNormOut[0] = v.normal.x;
            fvNormOut[1] = v.normal.y;
            fvNormOut[2] = v.normal.z;
        }

        void GetTexCoord(const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert)
        {
            UserData&     mesh = *static_cast<UserData*>(pContext->m_pUserData);
            VertexStride& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
            fvTexcOut[0] = v.uv.x;
            fvTexcOut[1] = v.uv.y;
        }

        void SetTangentSpaceBasic(const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
        {
            UserData&     mesh = *static_cast<UserData*>(pContext->m_pUserData);
            VertexStride& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
            v.tangent.x = fvTangent[0];
            v.tangent.y = fvTangent[1];
            v.tangent.z = fvTangent[2];
            v.tangent.w = -fSign;
        }

        Vec3 CalculateTangent(const VertexStride& v1, const VertexStride& v2, const VertexStride& v3)
        {
            Vec3 edge1 = v2.position - v1.position;
            Vec3 edge2 = v3.position - v1.position;
            Vec2 deltaUV1 = v2.uv - v1.uv;
            Vec2 deltaUV2 = v3.uv - v1.uv;

            Vec3 tangent{};

            float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
            tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
            tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
            tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

            return tangent;
        }

        void CalculateTangents(Array<VertexStride>& vertices, const Array<u32>& indices)
        {
            //Calculate tangents
            for (usize i = 0; i < indices.Size(); i += 3)
            {
                u32 idx0 = indices[i + 0];
                u32 idx1 = indices[i + 1];
                u32 idx2 = indices[i + 2];

                vertices[idx0].tangent = Vec4{CalculateTangent(vertices[idx0], vertices[idx1], vertices[idx2]), 1.0};
                vertices[idx1].tangent = Vec4{CalculateTangent(vertices[idx1], vertices[idx2], vertices[idx0]), 1.0};
                vertices[idx2].tangent = Vec4{CalculateTangent(vertices[idx2], vertices[idx0], vertices[idx1]), 1.0};
            }
        }


        void CalculateTangents(Array<VertexStride>& vertices)
        {
            //Calculate tangents
            for (usize i = 0; i < vertices.Size(); i += 3)
            {
                u32 idx0 = i + 0;
                u32 idx1 = i + 1;
                u32 idx2 = i + 2;
                vertices[idx0].tangent = Vec4{CalculateTangent(vertices[idx0], vertices[idx1], vertices[idx2]), 1.0};
                vertices[idx1].tangent = Vec4{CalculateTangent(vertices[idx1], vertices[idx2], vertices[idx0]), 1.0};
                vertices[idx2].tangent = Vec4{CalculateTangent(vertices[idx2], vertices[idx0], vertices[idx1]), 1.0};
            }
        }
    }


    AABB RenderUtils::CalculateMeshAABB(const Array<VertexStride>& vertices)
    {
        AABB boundingBox{};

        if (!vertices.Empty())
        {
            boundingBox.min = vertices[0].position;
            boundingBox.max = vertices[0].position;

            for (const auto& dataIt : vertices)
            {
                boundingBox.min = Math::Min(boundingBox.min, dataIt.position);
                boundingBox.max = Math::Max(boundingBox.max, dataIt.position);
            }
        }
        return boundingBox;
    }

    void RenderUtils::CalcTangents(Array<VertexStride>& vertices, const Array<u32>& indices, bool useMikktspace)
    {
        if (useMikktspace)
        {
            UserData userData{
                .vertices = vertices,
                .indices = indices
            };

            SMikkTSpaceInterface anInterface{
                .m_getNumFaces = GetNumFaces,
                .m_getNumVerticesOfFace = GetNumVerticesOfFace,
                .m_getPosition = GetPosition,
                .m_getNormal = GetNormal,
                .m_getTexCoord = GetTexCoord,
                .m_setTSpaceBasic = SetTangentSpaceBasic
            };

            SMikkTSpaceContext mikkTSpaceContext{
                .m_pInterface = &anInterface,
                .m_pUserData = &userData
            };

            genTangSpaceDefault(&mikkTSpaceContext);
        }
        else
        {
            CalculateTangents(vertices, indices);
        }
    }

    void EquirectangularToCubemap::Init(Extent extent, Format format)
    {
        this->format = format;
        this->extent = extent;

        texture = Graphics::CreateTexture(TextureCreation{
            .extent = {extent.width, extent.height, 1},
            .format = format,
            .usage = TextureUsage::Storage | TextureUsage::ShaderResource,
            .mipLevels = RenderUtils::CalcMips(extent),
            .arrayLayers = 6,
            .name = "EquirectangularToCubemap"
        });

        Graphics::UpdateTextureLayout(texture, ResourceLayout::Undefined, ResourceLayout::ShaderReadOnly);

        textureArrayView = Graphics::CreateTextureView(TextureViewCreation{
            .texture = texture,
            .viewType = ViewType::Type2DArray,
            .layerCount = 6,
        });

        ShaderAsset* shaderAsset = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Utils/EquirectToCube.comp");

        pipelineState = Graphics::CreateComputePipelineState({
            .shaderState = shaderAsset->GetDefaultState()
        });

        bindingSet = Graphics::CreateBindingSet(shaderAsset->GetDefaultState());
        downscale.Init(texture);
    }

    void EquirectangularToCubemap::Destroy()
    {
        downscale.Destroy();
        Graphics::DestroyBindingSet(bindingSet);
        Graphics::DestroyComputePipelineState(pipelineState);
        Graphics::DestroyTextureView(textureArrayView);
        Graphics::DestroyTexture(texture);
    }

    void EquirectangularToCubemap::Convert(RenderCommands& cmd, Texture originTexture)
    {
        bindingSet->GetVar("inputTexture")->SetTexture(originTexture);
        bindingSet->GetVar("outputTexture")->SetTextureView(textureArrayView);

        cmd.ResourceBarrier(ResourceBarrierInfo{
            .texture = texture,
            .oldLayout = ResourceLayout::ShaderReadOnly,
            .newLayout = ResourceLayout::General,
            .layerCount = 6
        });

        cmd.BindPipelineState(pipelineState);
        cmd.BindBindingSet(pipelineState, bindingSet);

        cmd.Dispatch(std::ceil(extent.width / 32.f),
                     std::ceil(extent.height / 32.f),
                     6.0f);

        cmd.ResourceBarrier(ResourceBarrierInfo{
            .texture = texture,
            .oldLayout = ResourceLayout::General,
            .newLayout = ResourceLayout::ShaderReadOnly,
            .layerCount = 6
        });

        downscale.Generate(cmd);
    }

    Texture EquirectangularToCubemap::GetTexture() const
    {
        return texture;
    }

    void DiffuseIrradianceGenerator::Init(Extent extent)
    {
        this->extent = extent;

        texture = Graphics::CreateTexture(TextureCreation{
            .extent = {extent.width, extent.height, 1},
            .format = Format::RGBA16F,
            .usage = TextureUsage::Storage | TextureUsage::ShaderResource,
            .arrayLayers = 6,
            .name = "Irradiance"
        });

        Graphics::UpdateTextureLayout(texture, ResourceLayout::Undefined, ResourceLayout::ShaderReadOnly);

        textureArrayView = Graphics::CreateTextureView(TextureViewCreation{
            .texture = texture,
            .viewType = ViewType::Type2DArray,
            .layerCount = 6,
        });

        ShaderAsset* shaderAsset = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Utils/IRMap.comp");

        pipelineState = Graphics::CreateComputePipelineState({
            .shaderState = shaderAsset->GetDefaultState()
        });

        bindingSet = Graphics::CreateBindingSet(shaderAsset->GetDefaultState());
    }

    void DiffuseIrradianceGenerator::Generate(RenderCommands& cmd, Texture cubemap)
    {
        bindingSet->GetVar("inputTexture")->SetTexture(cubemap);
        bindingSet->GetVar("outputTexture")->SetTextureView(textureArrayView);

        cmd.ResourceBarrier(ResourceBarrierInfo{
            .texture = texture,
            .oldLayout = ResourceLayout::ShaderReadOnly,
            .newLayout = ResourceLayout::General,
            .layerCount = 6
        });

        cmd.BindPipelineState(pipelineState);
        cmd.BindBindingSet(pipelineState, bindingSet);

        cmd.Dispatch(std::ceil(extent.width / 32.f),
                     std::ceil(extent.height / 32.f),
                     6.0f);

        cmd.ResourceBarrier(ResourceBarrierInfo{
            .texture = texture,
            .oldLayout = ResourceLayout::General,
            .newLayout = ResourceLayout::ShaderReadOnly,
            .layerCount = 6
        });
    }

    Texture DiffuseIrradianceGenerator::GetTexture() const
    {
        return texture;
    }

    void BRDFLUTGenerator::Init(Extent extent)
    {
        texture = Graphics::CreateTexture(TextureCreation{
            .extent = {extent.width, extent.height, 1},
            .format = Format::RG16F,
            .usage = TextureUsage::Storage | TextureUsage::ShaderResource,
            .arrayLayers = 1,
            .name = "BRDFLUT"
        });

        sampler = Graphics::CreateSampler(SamplerCreation{
            .addressMode = TextureAddressMode::ClampToEdge,
        });

        ShaderAsset* shader = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Utils/GenBRDFLUT.comp");

        PipelineState pipelineState = Graphics::CreateComputePipelineState({
            .shaderState = shader->GetDefaultState()
        });

        BindingSet* bindingSet = Graphics::CreateBindingSet(shader->GetDefaultState());
        bindingSet->GetVar("texture")->SetTexture(texture);

        RenderCommands& cmd = Graphics::GetCmd();
        cmd.Begin();

        cmd.ResourceBarrier(ResourceBarrierInfo{
            .texture = texture,
            .oldLayout = ResourceLayout::Undefined,
            .newLayout = ResourceLayout::General,
        });

        cmd.BindPipelineState(pipelineState);
        cmd.BindBindingSet(pipelineState, bindingSet);
        cmd.Dispatch(extent.width / 32.f,
                     extent.height / 32.f,
                     1);

        cmd.ResourceBarrier(ResourceBarrierInfo{
            .texture = texture,
            .oldLayout = ResourceLayout::General,
            .newLayout = ResourceLayout::ShaderReadOnly,
        });

        cmd.SubmitAndWait(Graphics::GetMainQueue());

        Graphics::DestroyComputePipelineState(pipelineState);
        Graphics::DestroyBindingSet(bindingSet);
    }

    void BRDFLUTGenerator::Destroy()
    {
        Graphics::DestroyTexture(texture);
        Graphics::DestroySampler(sampler);
    }

    Texture BRDFLUTGenerator::GetTexture() const
    {
        return texture;
    }

    Sampler BRDFLUTGenerator::GetSampler() const
    {
        return sampler;
    }

    void SpecularMapGenerator::Init(Extent extent, u32 mips)
    {
        this->extent = extent;
        this->mips = mips;

        texture = Graphics::CreateTexture(TextureCreation{
            .extent = {extent.width, extent.height, 1},
            .format = Format::RGBA16F,
            .usage = TextureUsage::Storage | TextureUsage::ShaderResource,
            .mipLevels = mips,
            .arrayLayers = 6,
            .name = "SpecularMap"
        });

        Graphics::UpdateTextureLayout(texture, ResourceLayout::Undefined, ResourceLayout::ShaderReadOnly);

        ShaderAsset* shaderAsset = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Utils/SpecularMap.comp");

        pipelineState = Graphics::CreateComputePipelineState({
            .shaderState = shaderAsset->GetDefaultState()
        });

        bindingSets.Resize(mips);
        textureViews.Resize(mips);

        for (u32 i = 0; i < mips; ++i)
        {
            bindingSets[i] = Graphics::CreateBindingSet(shaderAsset->GetDefaultState());
            textureViews[i] = Graphics::CreateTextureView(TextureViewCreation{
                .texture = texture,
                .viewType = ViewType::Type2DArray,
                .baseMipLevel = i,
                .layerCount = 6,
            });
        }
    }

    void SpecularMapGenerator::Generate(RenderCommands& cmd, Texture cubemap)
    {
        for (int i = 0; i < mips; ++i)
        {
            bindingSets[i]->GetVar("inputTexture")->SetTexture(cubemap);
            bindingSets[i]->GetVar("outputTexture")->SetTextureView(textureViews[i]);
        }

        cmd.BindPipelineState(pipelineState);

        for (u32 mip = 0; mip < mips; ++mip)
        {
            cmd.ResourceBarrier(ResourceBarrierInfo{
                .texture = texture,
                .oldLayout = ResourceLayout::ShaderReadOnly,
                .newLayout = ResourceLayout::General,
                .mipLevel = mip,
                .layerCount = 6
            });

            u32 mipWidth  = extent.width * std::pow(0.5, mip);
            u32 mipHeight = extent.height * std::pow(0.5, mip);

            SpecularMapFilterSettings settings{
                .roughness = Max(static_cast<float>(mip) / static_cast<float>(mips - 1), 0.01f)
            };

            cmd.PushConstants(pipelineState, ShaderStage::Compute, &settings, sizeof(settings));
            cmd.BindBindingSet(pipelineState, bindingSets[mip]);
            cmd.Dispatch(std::ceil(mipWidth / 32.f),
                         std::ceil(mipHeight / 32.f),
                         6);

            cmd.ResourceBarrier(ResourceBarrierInfo{
                .texture = texture,
                .oldLayout = ResourceLayout::General,
                .newLayout = ResourceLayout::ShaderReadOnly,
                .mipLevel = mip,
                .layerCount = 6
            });
        }
    }

    Texture SpecularMapGenerator::GetTexture() const
    {
        return texture;
    }


    void TextureBlockCompressor::Init(Format format, Texture src)
    {
        this->src = src;

        TextureCreation creation = Graphics::GetTextureCreationInfo(src);

        shaderState = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Utils/BlockCompress.comp")->GetState("BC1Compress");
        pipelineState = Graphics::CreateComputePipelineState({
            .shaderState = shaderState
        });
        bindingSet = Graphics::CreateBindingSet(shaderState);

        sampler = Graphics::CreateSampler({
            .filter = SamplerFilter::Linear,
            .addressMode = TextureAddressMode::ClampToBorder
        });

        const u32 blockSize = GetFormatBlockSize(format);
        rawExtent.width = std::max(1u, creation.extent.width / blockSize);
        rawExtent.height = std::max(1u, creation.extent.height / blockSize);

        rawDest = Graphics::CreateTexture(TextureCreation{
            .extent = {rawExtent.width, rawExtent.height, 1},
            .format = Format::RG32U,
            .usage = TextureUsage::Storage | TextureUsage::TransferSrc
        });

        Graphics::UpdateTextureLayout(rawDest, ResourceLayout::Undefined, ResourceLayout::General);

        bindingSet->GetVar("input")->SetTexture(this->src);
        bindingSet->GetVar("output")->SetTexture(rawDest);
        bindingSet->GetVar("defaultSampler")->SetSampler(sampler);
    }

    void TextureBlockCompressor::Compress(RenderCommands& cmd)
    {
        TextureCreation creation = Graphics::GetTextureCreationInfo(src);

        u32 mips = 1;

        cmd.BindPipelineState(pipelineState);

        for (u32 mip = 0; mip < mips; ++mip)
        {
            const u32 width = std::max(1u, creation.extent.width >> mip);
            const u32 height = std::max(1u, creation.extent.height >> mip);

            cmd.BindBindingSet(pipelineState, bindingSet);
            cmd.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, creation.arrayLayers);
        }
    }

    Texture TextureBlockCompressor::GetRawTexture() const
    {
        return rawDest;
    }

    Format TextureBlockCompressor::GetRawFormat() const
    {
        return Format::RG32U;
    }

    Extent TextureBlockCompressor::GetRawExtent() const
    {
        return rawExtent;
    }

    void TextureBlockCompressor::Destroy() const
    {
        Graphics::DestroyComputePipelineState(pipelineState);
        Graphics::DestroyBindingSet(bindingSet);
        Graphics::DestroyTexture(rawDest);
        Graphics::DestroySampler(sampler);
    }


    void TextureDownscale::Init(Texture texture)
    {
        atomicCounter = Graphics::CreateBuffer(BufferCreation{
            .usage = BufferUsage::StorageBuffer,
            .size = sizeof(u32),
            .allocation = BufferAllocation::GPUOnly
        });

        linearSampler = Graphics::CreateSampler({});


        u32 value = 0;
        Graphics::UpdateBufferData({
            .buffer = atomicCounter,
            .data = &value,
            .size = sizeof(u32)
        });

        this->texture = texture;
        TextureCreation textureCreation = Graphics::GetTextureCreationInfo(texture);
        u32 mipStart = 0;
        u32 outputMipCount = textureCreation.mipLevels - (mipStart + 1);
        u32 width = textureCreation.extent.width;
        u32 height = textureCreation.extent.height >> mipStart;
        threadGroupX = (width + 63) >> 6;  // as per document documentation (page 22)
        threadGroupy = (height + 63) >> 6; // as per document documentation (page 22)
        SK_ASSERT(width <= 4096 && height <= 4096 && outputMipCount <= 12, "Cannot downscale this texture !");
        SK_ASSERT(mipStart < outputMipCount, "Mip start is incorrect");

        arrayLayers = textureCreation.arrayLayers;
        mipData.mipInfo.x = outputMipCount;
        mipData.mipInfo.y = static_cast<float>(threadGroupX * threadGroupy);
        mipData.mipInfo.z = textureCreation.extent.width;
        mipData.mipInfo.w = textureCreation.extent.height;

        ShaderState* state = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Utils/SpdDownsample.comp")->GetDefaultState();
        downscaleState = Graphics::CreateComputePipelineState({
            .shaderState = state
        });

        bindingSets.Resize(textureCreation.arrayLayers);

        for (u32 arr = 0; arr < textureCreation.arrayLayers; ++arr)
        {
            bindingSets[arr] = Graphics::CreateBindingSet(state);

            Array<TextureView> views;
            for (int mip = 1; mip < textureCreation.mipLevels; mip++)
            {
                TextureViewCreation viewCreation{};
                viewCreation.texture = texture;
                viewCreation.baseMipLevel = mip;
                viewCreation.baseArrayLayer = arr;
                viewCreation.viewType = ViewType::Type2D;
                views.EmplaceBack(Graphics::CreateTextureView(viewCreation));
            }

            TextureViewCreation viewCreation{};
            viewCreation.texture = texture;
            viewCreation.baseMipLevel = 0;
            viewCreation.baseArrayLayer = arr;
            viewCreation.viewType = ViewType::Type2D;
            TextureView inputView = Graphics::CreateTextureView(viewCreation);

            bindingSets[arr]->GetVar("GAtomicCounter")->SetBuffer(atomicCounter);
            bindingSets[arr]->GetVar("tex")->SetTextureView(inputView);
            bindingSets[arr]->GetVar("TextureUAVMips")->SetTextureViewArray(views);
            bindingSets[arr]->GetVar("Tex_Sampler")->SetSampler(linearSampler);

            allViews.Insert(allViews.end(), views.begin(), views.end());
            allViews.EmplaceBack(inputView);
        }
    }

    void TextureDownscale::Destroy() const
    {
        for (BindingSet* bindingSet : bindingSets)
        {
            Graphics::DestroyBindingSet(bindingSet);
        }

        for (TextureView view : allViews)
        {
            Graphics::DestroyTextureView(view);
        }

        Graphics::DestroyComputePipelineState(downscaleState);
        Graphics::DestroySampler(linearSampler);
        Graphics::DestroyBuffer(atomicCounter);

    }

    void TextureDownscale::Generate(RenderCommands& cmd)
    {
        TextureCreation textureCreation = Graphics::GetTextureCreationInfo(texture);

        cmd.BindPipelineState(downscaleState);

        for (u32 arr = 0; arr < arrayLayers; ++arr)
        {
            for (u32 m = 1; m < textureCreation.mipLevels; ++m)
            {
                cmd.ResourceBarrier(ResourceBarrierInfo{
                    .texture = texture,
                    .oldLayout = ResourceLayout::ShaderReadOnly,
                    .newLayout = ResourceLayout::General,
                    .mipLevel = m,
                    .baseArrayLayer = arr
                });
            }


            cmd.BindBindingSet(downscaleState, bindingSets[arr]);
            cmd.PushConstants(downscaleState, ShaderStage::Compute, &mipData, sizeof(DownscaleData));
            cmd.Dispatch(threadGroupX, threadGroupy, 1);

            for (u32 m = 1; m < textureCreation.mipLevels; ++m)
            {
                cmd.ResourceBarrier(ResourceBarrierInfo{
                    .texture = texture,
                    .oldLayout = ResourceLayout::General,
                    .newLayout = ResourceLayout::ShaderReadOnly,
                    .mipLevel = m,
                    .baseArrayLayer = arr
                });
            }
        }
    }

    void SpecularMapGenerator::Destroy()
    {
        Graphics::DestroyTexture(texture);
        Graphics::DestroyComputePipelineState(pipelineState);

        for (u32 i = 0; i < mips; ++i)
        {
            Graphics::DestroyBindingSet(bindingSets[i]);
            Graphics::DestroyTextureView(textureViews[i]);
        }
    }

    void DiffuseIrradianceGenerator::Destroy()
    {
        Graphics::DestroyComputePipelineState(pipelineState);
        Graphics::DestroyTexture(texture);
        Graphics::DestroyTextureView(textureArrayView);
        Graphics::DestroyBindingSet(bindingSet);
    }
}
