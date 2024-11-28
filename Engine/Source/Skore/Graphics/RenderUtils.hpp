#pragma once
#include "GraphicsTypes.hpp"


namespace Skore
{
    class TextureDownscale
    {
    public:
        void Init(Texture texture);
        void Destroy() const;
        void Generate(RenderCommands& cmd);
    private:
        Texture       texture = {};
        PipelineState downscaleState = {};
        BindingSet*   bindingSet = {};
    };


    class EquirectangularToCubemap
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
        TextureDownscale downscale;
    };

    class DiffuseIrradianceGenerator
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

    class BRDFLUTGenerator
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

    class SpecularMapGenerator
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
}

namespace Skore::RenderUtils
{
    SK_FINLINE u32 CalcMips(Extent extent)
    {
        return std::max(static_cast<u32>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1, 12u);
    }

    SK_API AABB    CalculateMeshAABB(const Array<VertexStride>& vertices);
    SK_API void    CalcTangents(Array<VertexStride>& vertices, const Array<u32>& indices, bool useMikktspace = true);
    SK_API Texture GenerateBRDFLUT();
    SK_API void    GenerateCubemapMips(Texture texture, Extent extent, u32 mips);
}
