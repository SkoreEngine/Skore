#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Platform/PlatformTypes.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Span.hpp"

namespace Skore
{
    struct RenderGraphResource;
    class MaterialAsset;
    class MeshAsset;
    struct ShaderAsset;
    struct ShaderState;
    struct RenderCommands;

    SK_HANDLER(Adapter);
    SK_HANDLER(Swapchain);
    SK_HANDLER(RenderPass);
    SK_HANDLER(PipelineState);
    SK_HANDLER(DescriptorSet);
    SK_HANDLER(Texture);
    SK_HANDLER(TextureView);
    SK_HANDLER(Buffer);
    SK_HANDLER(Sampler);
    SK_HANDLER(GPUQueue);

    enum class Format
    {
        R,
        R8U,
        R16F,
        R32U,
        R32F,
        RG,
        RG16F,
        RG32F,
        RG32U,
        RGB,
        RGB16F,
        RGB32F,
        RGBA,
        RGBA16F,
        RGBA32F,
        BGRA,
        R11G11B10UF,
        RGB9E5,
        BC1U,
        BC1U_SRGB,
        BC3U,
        BC4U,
        BC5U,
        BC6H_UF16,
        Depth,
        Undefined,
        //TODO : add ohter formats
    };

    enum class RenderApiType
    {
        None   = 0,
        Vulkan = 1,
        OpenGL = 2,
        D3D12  = 3,
        Metal  = 4,
        WebGPU = 5
    };

    enum class ResourceLayout
    {
        Undefined              = 0,
        General                = 1,
        ColorAttachment        = 2,
        DepthStencilAttachment = 3,
        DepthStencilReadOnly   = 4,
        ShaderReadOnly         = 5,
        CopyDest               = 6,
        CopySource             = 7,
        Present                = 8
    };

    enum class ViewType
    {
        Type1D          = 0,
        Type2D          = 1,
        Type3D          = 2,
        TypeCube        = 3,
        Type1DArray     = 4,
        Type2DArray     = 5,
        TypeCubeArray   = 6,
        Undefined       = 7,
    };

    enum class ShaderStage : u32
    {
        Unknown         = 0,
        Vertex          = 1 << 0,
        Hull            = 1 << 1,
        Domain          = 1 << 2,
        Geometry        = 1 << 3,
        Pixel           = 1 << 4,
        Compute         = 1 << 5,
        Amplification   = 1 << 6,
        Mesh            = 1 << 7,
        RayGen          = 1 << 8,
        RayMiss         = 1 << 9,
        RayClosestHit   = 1 << 10,
        RayAnyHit       = 1 << 11,
        RayIntersection = 1 << 12,
        Callable        = 1 << 13,
        All             = 1 << 14
    };

    ENUM_FLAGS(ShaderStage, u32)

    enum class BufferUsage : u32
    {
        None                         = 1 << 0,
        VertexBuffer                 = 1 << 1,
        IndexBuffer                  = 1 << 2,
        UniformBuffer                = 1 << 3,
        StorageBuffer                = 1 << 4,
        IndirectBuffer               = 1 << 5,
        AccelerationStructureBuild   = 1 << 6,
        AccelerationStructureStorage = 1 << 7,
        All                          = VertexBuffer | IndexBuffer | UniformBuffer | StorageBuffer | IndirectBuffer | AccelerationStructureBuild | AccelerationStructureStorage
    };

    ENUM_FLAGS(BufferUsage, u32)

    enum class BufferAllocation
    {
        GPUOnly       = 1,
        TransferToGPU = 2,
        TransferToCPU = 3
    };

    enum class TextureUsage : u32
    {
        None           = 0 << 0,
        ShaderResource = 1 << 0,
        DepthStencil   = 1 << 2,
        RenderPass     = 1 << 3,
        Storage        = 1 << 4,
        TransferDst    = 1 << 5,
        TransferSrc    = 1 << 6,
    };

    ENUM_FLAGS(TextureUsage, u32)

    enum class TextureAspect :u32
    {
        None    = 0x00000000,
        Color   = 0x00000001,
        Depth   = 0x00000002,
        Stencil = 0x00000004,
    };

    ENUM_FLAGS(TextureAspect, u32)

    enum class SamplerFilter
    {
        Nearest  = 0,
        Linear   = 1,
        CubicImg = 2,
    };

    enum class TextureAddressMode
    {
        Repeat            = 0,
        MirroredRepeat    = 1,
        ClampToEdge       = 2,
        ClampToBorder     = 3,
        MirrorClampToEdge = 4,
    };

    enum class CompareOp
    {
        Never          = 0,
        Less           = 1,
        Equal          = 2,
        LessOrEqual    = 3,
        Greater        = 4,
        NotEqual       = 5,
        GreaterOrEqual = 6,
        Always         = 7
    };

    enum class LoadOp
    {
        Load = 0,
        Clear = 1,
        DontCare = 2
    };

    enum class StoreOp
    {
        Store = 0,
        DontCare = 1
    };

    enum class BorderColor
    {
        FloatTransparentBlack = 0,
        IntTransparentBlack   = 1,
        FloatOpaqueBlack      = 2,
        IntOpaqueBlack        = 3,
        FloatOpaqueWhite      = 4,
        IntOpaqueWhite        = 4,
    };

    enum class CullMode
    {
        None  = 0,
        Front = 1,
        Back  = 2
    };


    enum class PolygonMode
    {
        Fill  = 0,
        Line  = 1,
        Point = 2,
    };

    enum class PrimitiveTopology
    {
        PointList                  = 0,
        LineList                   = 1,
        LineStrip                  = 2,
        TriangleList               = 3,
        TriangleStrip              = 4,
        TriangleFan                = 5,
        LineListWithAdjacency      = 6,
        LineStripWithAdjacency     = 7,
        TriangleListWithAdjacency  = 8,
        TriangleStripWithAdjacency = 9,
        PatchList                  = 10,
    };

    enum class DescriptorType
    {
        SampledImage          = 0,
        Sampler               = 1,
        StorageImage          = 2,
        UniformBuffer         = 3,
        StorageBuffer         = 4,
        AccelerationStructure = 5
    };

    enum class RenderType
    {
        None,
        Void,
        Bool,
        Int,
        Float,
        Vector,
        Matrix,
        Image,
        Sampler,
        SampledImage,
        Array,
        RuntimeArray,
        Struct
    };

    enum class SamplerMipmapMode
    {
        Nearest,
        Linear
    };

    enum class RenderGraphResourceType
    {
        None        = 0,
        Buffer      = 1,
        Texture     = 2,
        TextureView = 3,
        Attachment  = 4,
        Sampler     = 5,
        Reference   = 6,
    };

    enum class RenderGraphPassType
    {
        Other    = 0,
        Graphics = 1,
        Compute  = 2
    };

    enum class AlphaMode
    {
        None        = 0,
        Opaque      = 1,
        Mask        = 2,
        Blend       = 3
    };

    enum class GraphicsTaskType
    {
        Graphics = 1,
        Compute  = 2,
        Transfer = 3,
        Destroy  = 4
    };

    enum class LightType
    {
        Directional = 0,
        Point       = 1,
        Spot        = 2,
        Area        = 3
    };

    enum class CameraProjection : i32
    {
        Perspective = 1,
        Orthogonal  = 2
    };


    struct LightProperties
    {
        LightType type;
        Vec3      direction;
        Vec3      position;
        Color     color;
        f32       intensity;
        f32       indirectMultiplier;
        f32       range;
        f32       innerCutoff;
        f32       outerCutoff;
        bool      castShadows;
    };

    struct SwapchainCreation
    {
        Window window{};
        bool vsync{true};
    };

    struct AttachmentCreation
    {
        Texture        texture{};
        TextureView    textureView{};
        ResourceLayout initialLayout{ResourceLayout::Undefined};
        ResourceLayout finalLayout{ResourceLayout::Undefined};
        LoadOp         loadOp{LoadOp::Clear};
        StoreOp        storeOp{StoreOp::Store};
    };

    struct RenderPassCreation
    {
        Span<AttachmentCreation> attachments{};
    };

    struct BufferCreation
    {
        BufferUsage      usage{BufferUsage::None};
        usize            size{};
        BufferAllocation allocation{BufferAllocation::GPUOnly};
    };

    struct TextureCreation
    {
        Extent3D     extent{};
        Format       format{Format::RGBA};
        TextureUsage usage{};
        u32          mipLevels{1};
        u32          arrayLayers{1};
        ViewType     defaultView{ViewType::Type2D};
        StringView   name{};
    };

    struct TextureViewCreation
    {
        Texture  texture{};
        ViewType viewType{ViewType::Type2D};
        u32      baseMipLevel = 0;
        u32      levelCount = 1;
        u32      baseArrayLayer = 0;
        u32      layerCount = 1;
    };

    struct TextureGetDataInfo
    {
        Texture        texture{};
        Format         format{};
        Extent         extent{};
        ResourceLayout textureLayout{};
    };

    struct SamplerCreation
    {
        SamplerFilter      filter{SamplerFilter::Linear};
        TextureAddressMode addressMode{TextureAddressMode::Repeat};
        bool               comparedEnabled = false;
        CompareOp          compareOperator{CompareOp::Always};
        f32                mipLodBias = 0.0f;
        f32                minLod{};
        f32                maxLod{F32_MAX};
        bool               anisotropyEnable = true;
        BorderColor        borderColor{BorderColor::IntOpaqueBlack};
        SamplerMipmapMode  samplerMipmapMode = SamplerMipmapMode::Linear;
    };

    struct VertexInputAttribute
    {
        u32    location;
        u32    binding;
        Format format;
        u32    offset;
    };

    struct GraphicsPipelineCreation
    {
        ShaderState*               shaderState{};
        Span<Format>               attachments{};
        Format                     depthFormat = Format::Undefined;
        RenderPass                 renderPass{};
        bool                       depthWrite{false};
        bool                       stencilTest{false};
        bool                       blendEnabled{false};
        f32                        minDepthBounds{1.0};
        f32                        maxDepthBounds{0.0};
        CullMode                   cullMode{CullMode::None};
        CompareOp                  compareOperator{CompareOp::Always};
        PolygonMode                polygonMode{PolygonMode::Fill};
        PrimitiveTopology          primitiveTopology{PrimitiveTopology::TriangleList};
        Span<VertexInputAttribute> inputs{};
        u32                        stride{};
        PipelineState              pipelineState{};
    };

    struct ComputePipelineCreation
    {
        ShaderState*  shaderState{};
        PipelineState pipelineState{};
    };


    struct ClearDepthStencilValue
    {
        f32 depth{1.0};
        u32 stencil{0};
    };

    struct BeginRenderPassInfo
    {
        RenderPass              renderPass{};
        Vec4*                   clearValue{};
        ClearDepthStencilValue* depthStencil{};
    };

    struct ViewportInfo
    {
        f32 x{};
        f32 y{};
        f32 width{};
        f32 height{};
        f32 minDepth{};
        f32 maxDepth{};
    };

    struct ResourceBarrierInfo
    {
        Texture texture{};
        ResourceLayout oldLayout{};
        ResourceLayout newLayout{};
        u32 mipLevel{0};
        u32 levelCount{1};
        u32 baseArrayLayer{0};
        u32 layerCount{0};
    };

    struct InterfaceVariable
    {
        u32    location{};
        u32    offset{};
        String name{};
        Format format{};
        u32    size{};

        static void RegisterType(NativeTypeHandler<InterfaceVariable>& type);
    };

    struct TypeDescription
    {
        String                 name{};
        RenderType             type{};
        u32                    size{};
        u32                    offset{};
        Array<TypeDescription> members{};

        static void RegisterType(NativeTypeHandler<TypeDescription>& type);
    };

    struct DescriptorBinding
    {
        u32                    binding{};
        u32                    count{};
        String                 name{};
        DescriptorType         descriptorType{};
        RenderType             renderType{};
        ShaderStage            shaderStage{ShaderStage::All};
        ViewType               viewType{};
        Array<TypeDescription> members{};
        u32                    size{};

        static void RegisterType(NativeTypeHandler<DescriptorBinding>& type);
    };

    struct DescriptorLayout
    {
        u32                      set{};
        Array<DescriptorBinding> bindings{};

        static void RegisterType(NativeTypeHandler<DescriptorLayout>& type);
    };

    struct DescriptorSetCreation
    {
        bool bindless = false;
        Array<DescriptorBinding> bindings{};
    };


    struct DescriptorSetWriteInfo
    {
        u32            binding{};
        DescriptorType descriptorType{};
        u32            arrayElement{};
        Texture        texture{};
        TextureView    textureView{};
        Sampler        sampler{};
        Buffer         buffer{};
    };

    struct ShaderPushConstant
    {
        String      name{};
        u32         offset{};
        u32         size{};
        ShaderStage stage{};

        static void RegisterType(NativeTypeHandler<ShaderPushConstant>& type);
    };

    struct ShaderStageInfo
    {
        ShaderStage stage{};
        String      entryPoint{};
        u32         offset{};
        u32         size{};

        static void RegisterType(NativeTypeHandler<ShaderStageInfo>& type);
    };

    struct ShaderInfo
    {
        Array<InterfaceVariable>  inputVariables{};
        Array<InterfaceVariable>  outputVariables{};
        Array<DescriptorLayout>   descriptors{};
        Array<ShaderPushConstant> pushConstants{};
        u32                       stride{};

        static void RegisterType(NativeTypeHandler<ShaderInfo>& type);
    };

    struct BufferDataInfo
    {
        Buffer          buffer{};
        const void*     data{};
        usize           size{};
        usize           srcOffset{};
        usize           dstOffset{};
    };

    struct BufferImageCopy
    {
        u64      bufferOffset{};
        u32      bufferRowLength{};
        u32      bufferImageHeight{};
        u32      textureMipLevel{};
        u32      textureArrayLayer{};
        u32      layerCount{1};
        Offset3D imageOffset{};
        Extent3D imageExtent;
    };

    struct TextureSubresourceLayers
    {
        TextureAspect textureAspect;
        u32           mipLevel = 0;
        u32           baseArrayLayer = 0;
        u32           layerCount = 1;
    };

    struct TextureCopy
    {
        TextureSubresourceLayers srcSubresource{};
        Offset3D                 srcOffset{};
        TextureSubresourceLayers dstSubresource{};
        Offset3D                 dstOffset{};
        Extent3D                 extent{};
    };


    struct TextureDataRegion
    {
        usize    dataOffset{};
        u32      layerCount{};
        u32      mipLevel{};
        u32      arrayLayer{};
        u32      levelCount{};
        Extent3D extent{};
    };

    struct TextureDataInfo
    {
        Texture                 texture{};
        const u8*               data{nullptr};
        usize                   size{};
        Span<TextureDataRegion> regions{};
    };

    struct VertexStride final
    {
        Vec3 position{};
        Vec3 normal{};
        Vec3 color{};
        Vec2 uv{};
        Vec4 tangent{};
    };

    inline bool operator==(const VertexStride& r, const VertexStride& l)
    {
        return r.position == l.position && r.normal == l.normal && r.uv == l.uv && r.color == l.color && r.tangent == l.tangent;
    }

    template<>
    struct Hash<VertexStride>
    {
        static constexpr bool hasHash = true;
        static usize Value(const VertexStride& value)
        {
            return (Hash<Vec3>::Value(value.position) ^ Hash<Vec3>::Value(value.normal) << 1) >> 1 ^ Hash<Vec2>::Value(value.uv) << 1;
        }
    };

    struct MeshPrimitive final
    {
        u32 firstIndex{};
        u32 indexCount{};
        u32 materialIndex{};

        static void RegisterType(NativeTypeHandler<MeshPrimitive>& type);
    };


    struct BufferCopyInfo
    {
        usize srcOffset;
        usize dstOffset;
        usize size;
    };

    struct CameraData
    {
        Mat4             view{1.0};
        Mat4             viewInverse{1.0};
        Mat4             projection{1.0};
        Mat4             projectionInverse{1.0};
        Mat4             projView{1.0};
        Mat4             lastProjView{1.0};
        Vec3             viewPos{};
        CameraProjection projectionType = CameraProjection::Perspective;
        f32              fov = 60;
        f32              nearClip{};
        f32              farClip{};
        Vec2             jitter{};
        Vec2             previousJitter{};
    };

    struct LightRenderData
    {
        VoidPtr         pointer;
        LightProperties properties;
    };

    struct TextureArrayElement
    {
        Texture texture;
        usize   index;
    };

    struct BindingVar;

    struct SK_API BindingVar
    {
        virtual ~BindingVar() = default;

        virtual void  SetTexture(const Texture& texture) = 0;
        virtual void  SetTextureArray(Span<Texture> textureArray) = 0;
        virtual void  SetTextureAt(const Texture& texture, usize index) = 0;
        virtual void  SetTextureViewArray(Span<TextureView> textureViews) = 0;
        virtual void  SetTextureView(const TextureView& textureView) = 0;
        virtual void  SetSampler(const Sampler& sampler) = 0;
        virtual void  SetBuffer(const Buffer& buffer) = 0;
        virtual void  SetValue(ConstPtr ptr, usize size) = 0;
    };

    struct SK_API BindingSet
    {
        virtual ~BindingSet() = default;

        virtual BindingVar* GetVar(const StringView& name) = 0;
        virtual void        Reload() = 0;
        virtual void        RemoveShaderDependency() = 0;
    };

    struct ResourceTextureViewCreation
    {
        RenderGraphResource* texture;
        ViewType             viewType{ViewType::Type2D};
        u32                  baseMipLevel = 0;
        u32                  levelCount = 1;
        u32                  baseArrayLayer = 0;
        u32                  layerCount = 1;


        TextureViewCreation ToTextureViewCreation() const
        {
            return TextureViewCreation {
                .viewType = viewType,
                .baseMipLevel = baseMipLevel,
                .levelCount = levelCount,
                .baseArrayLayer = baseArrayLayer,
                .layerCount = layerCount,
            };
        }

    };

    struct RenderGraphResourceCreation
    {
        String                      name{};
        RenderGraphResourceType     type{};
        Extent3D                    size{};
        Vec2                        scale{};
        Format                      format{};
        u32                         mipLevels{1};
        BufferCreation              bufferCreation{};
        SamplerCreation             samplerCreation{};
        ResourceTextureViewCreation textureViewCreation{};
    };

    struct RenderGraphPassCreation
    {
        String                             name{};
        Array<RenderGraphResourceCreation> inputs{};
        Array<RenderGraphResourceCreation> outputs{};
        RenderGraphPassType                type{};
    };

    struct RenderCommands
    {
        virtual      ~RenderCommands() = default;
        virtual void Begin() = 0;
        virtual void End() = 0;
        virtual void BeginRenderPass(const BeginRenderPassInfo& beginRenderPassInfo) = 0;
        virtual void EndRenderPass() = 0;
        virtual void SetViewport(const ViewportInfo& viewportInfo) = 0;
        virtual void BindVertexBuffer(const Buffer& gpuBuffer) = 0;
        virtual void BindIndexBuffer(const Buffer& gpuBuffer) = 0;
        virtual void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) = 0;
        virtual void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) = 0;
        virtual void PushConstants(const PipelineState& pipeline, ShaderStage stages, const void* data, usize size) = 0;
        virtual void BindBindingSet(const PipelineState& pipeline, BindingSet* bindingSet) = 0;
        virtual void BindDescriptorSet(const PipelineState& pipeline, DescriptorSet descriptorSet, u32 space) = 0;
        virtual void DrawIndexedIndirect(const Buffer& buffer, usize offset, u32 drawCount, u32 stride) = 0;
        virtual void BindPipelineState(const PipelineState& pipeline) = 0;
        virtual void Dispatch(u32 x, u32 y, u32 z) = 0;
        virtual void TraceRays(PipelineState pipeline, u32 x, u32 y, u32 z) = 0;
        virtual void SetScissor(const Rect& rect) = 0;
        virtual void BeginLabel(const StringView& name, const Vec4& color) = 0;
        virtual void EndLabel() = 0;
        virtual void ResourceBarrier(const ResourceBarrierInfo& resourceBarrierInfo) = 0;
        virtual void CopyBuffer(Buffer srcBuffer, Buffer dstBuffer, const Span<BufferCopyInfo>& info) = 0;
        virtual void CopyBufferToTexture(Buffer srcBuffer, Texture texture, const Span<BufferImageCopy>& regions) = 0;
        virtual void CopyTextureToBuffer(Texture srcTexture, ResourceLayout textureLayout, Buffer destBuffer, const Span<BufferImageCopy>& regions) = 0;
        virtual void CopyTexture(Texture srcTexture, ResourceLayout srcTextureLayout, Texture dstTexture, ResourceLayout dstTextureLayout, const Span<TextureCopy>& regions) = 0;
        virtual void SubmitAndWait(GPUQueue queue) = 0;

    };

    struct DeviceFeatures
    {
        bool raytraceSupported{};
        bool bindlessSupported{};
        bool multiDrawIndirectSupported{};
    };


    inline u32 GetFormatSize(Format format)
    {
        switch (format)
        {
            case Format::R: return 8;
            case Format::R16F: return 16;
            case Format::R32F: return 32;
            case Format::RG: return 8 * 2;
            case Format::RG16F: return 16 * 2;
            case Format::RG32F: return 32 * 2;
            case Format::RGB: return 8 * 3;
            case Format::RGB16F: return 16 * 3;
            case Format::RGB32F: return 32 * 3;
            case Format::RGBA: return 8 * 4;
            case Format::RGBA16F: return sizeof(Vec4) / 2;
            case Format::RGBA32F: return sizeof(Vec4);
            case Format::BGRA: return 8 * 4;
            case Format::R8U: return 8;
            case Format::R32U: return 32;
            case Format::RG32U: return 64;
            case Format::R11G11B10UF:
            case Format::RGB9E5:
            case Format::BC1U:
            case Format::BC1U_SRGB:
            case Format::BC3U:
            case Format::BC4U:
            case Format::BC5U:
            case Format::BC6H_UF16:
            case Format::Depth:
            case Format::Undefined:
                break;
        }
        SK_ASSERT(false, "format not found");
        return 0;
    }

    constexpr bool IsFormatBlockCompressed(Format format)
    {
        switch (format)
        {
            case Format::BC1U:
            case Format::BC1U_SRGB:
            case Format::BC3U:
            case Format::BC4U:
            case Format::BC5U:
            case Format::BC6H_UF16:
                return true;
            default:
                return false;
        }
    }

    constexpr u32 GetFormatBlockSize(Format format)
    {
        if (IsFormatBlockCompressed(format))
        {
            return 4u;
        }
        return 1u;
    }

    template<typename ...T>
    usize CountWrites(Span<DescriptorSetWriteInfo> writes, const T&... types)
    {
        usize ret = 0;
        for (DescriptorSetWriteInfo& write: writes)
        {
            if (((write.descriptorType == types) || ...))
            {
                ret++;
            }
        }
        return ret;
    }

    typedef void (*FnGraphicsTask)(VoidPtr userData, RenderCommands& cmd, GPUQueue queue);

}
