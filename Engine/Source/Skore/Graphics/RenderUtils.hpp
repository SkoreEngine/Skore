#pragma once
#include "GraphicsTypes.hpp"


namespace Skore
{
    class SK_API TextureDownscale
    {
    public:
        void Init(Texture texture);
        void Destroy() const;
        void Generate(RenderCommands& cmd);

    private:
        struct DownscaleData
        {
            Vec4 mipInfo;
        };

        Texture            texture = {};
        PipelineState      downscaleState = {};
        Array<BindingSet*> bindingSets = {};
        Array<TextureView> allViews{};
        DownscaleData      mipData{};
        Sampler            linearSampler{};
        u32                arrayLayers{};
        u32                threadGroupX{};
        u32                threadGroupy{};
        Buffer             atomicCounter{};
    };

    class SK_API EquirectangularToCubemap
    {
    public:
        void Init(Extent extent, Format format);
        void Destroy();
        void Convert(RenderCommands& cmd, Texture originTexture);

        Texture GetTexture() const;

    private:
        Format           format{};
        Extent           extent{};
        Texture          texture = {};
        TextureView      textureArrayView = {};
        PipelineState    pipelineState = {};
        BindingSet*      bindingSet = nullptr;
        TextureDownscale downscale{};
    };

    class SK_API DiffuseIrradianceGenerator
    {
    public:
        void    Init(Extent extent);
        void    Destroy();
        void    Generate(RenderCommands& cmd, Texture cubemap);
        Texture GetTexture() const;

    private:
        Extent        extent{};
        Texture       texture{};
        TextureView   textureArrayView{};
        PipelineState pipelineState{};
        BindingSet*   bindingSet{};
    };

    class SK_API BRDFLUTGenerator
    {
    public:
        void    Init(Extent extent);
        void    Destroy();
        Texture GetTexture() const;
        Sampler GetSampler() const;

    private:
        Texture texture{};
        Sampler sampler{};
    };

    class SK_API SpecularMapGenerator
    {
    public:
        void Init(Extent extent, u32 mips);
        void Destroy();
        void Generate(RenderCommands& cmd, Texture cubemap);

        Texture GetTexture() const;

    private:
        u32           mips{};
        Extent        extent{};
        Texture       texture{};
        PipelineState pipelineState{};

        Array<BindingSet*> bindingSets;
        Array<TextureView> textureViews;
    };


    class SK_API TextureBlockCompressor
    {
    public:
        void    Init(Format format, Texture src);
        void    Compress(RenderCommands& cmd);
        Texture GetRawTexture() const;
        Format  GetRawFormat() const;
        Extent  GetRawExtent() const;
        void    Destroy() const;
    private:
        ShaderState*  shaderState = {};
        PipelineState pipelineState = {};
        BindingSet* bindingSet{};
        Texture src = {};
        Texture rawDest = {};
        Sampler sampler = {};
        Extent rawExtent = {};
    };
}

namespace Skore::RenderUtils
{
    SK_FINLINE u32 CalcMips(Extent extent)
    {
        return std::min(static_cast<u32>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1, 12u);
    }
    SK_API AABB CalculateMeshAABB(const Array<VertexStride>& vertices);
    SK_API void CalcTangents(Array<VertexStride>& vertices, const Array<u32>& indices, bool useMikktspace = true);
}
