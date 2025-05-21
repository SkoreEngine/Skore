// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	struct ShaderVariant;
	class ShaderAsset;
}

namespace Skore
{
	class GPUTextureView;
	class GPUAdapter;
	class GPUDevice;
	class GPUSwapchain;
	class GPUCommandBuffer;
	class GPUBuffer;
	class GPUTexture;
	class GPUSampler;
	class GPUPipeline;
	class GPUDescriptorSet;
	class GPURenderPass;
	class GPUQueryPool;
	class GPUBottomLevelAS;
	class GPUTopLevelAS;

	enum class GraphicsAPI
	{
		Vulkan,
		D3D12,
		Metal,
		None
	};

	enum class DeviceType
	{
		Discrete,
		Integrated,
		Virtual,
		CPU,
		Other
	};

	enum class ShaderStage : u32
	{
		None          = 0,
		Vertex        = 1 << 0,
		Hull          = 1 << 1,
		Domain        = 1 << 2,
		Geometry      = 1 << 3,
		Pixel         = 1 << 4,
		Compute       = 1 << 5,
		Amplification = 1 << 6,
		Mesh          = 1 << 7,
		RayGen        = 1 << 8,
		AnyHit        = 1 << 9,
		ClosestHit    = 1 << 10,
		Miss          = 1 << 11,
		Intersection  = 1 << 12,
		Callable      = 1 << 13,
		All           = 1 << 14,
	};

	ENUM_FLAGS(ShaderStage, u32);

	enum class ResourceState
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

	enum class ResourceUsage : u32
	{
		None                  = 0,
		ShaderResource        = 1 << 0,
		RenderTarget          = 1 << 1,
		DepthStencil          = 1 << 2,
		UnorderedAccess       = 1 << 3,
		VertexBuffer          = 1 << 4,
		IndexBuffer           = 1 << 5,
		ConstantBuffer        = 1 << 6,
		CopyDest              = 1 << 7,
		CopySource            = 1 << 8,
		AccelerationStructure = 1 << 9,
		RayTracing            = 1 << 10
	};

	ENUM_FLAGS(ResourceUsage, u32);

	enum class TextureFormat
	{
		Unknown,

		// 8-bit formats
		R8_UNORM,
		R8_SNORM,
		R8_UINT,
		R8_SINT,
		R8_SRGB,

		// 16-bit formats
		R16_UNORM,
		R16_SNORM,
		R16_UINT,
		R16_SINT,
		R16_FLOAT,
		R8G8_UNORM,
		R8G8_SNORM,
		R8G8_UINT,
		R8G8_SINT,
		R8G8_SRGB,

		R16G16B16_UNORM,
		R16G16B16_SNORM,
		R16G16B16_UINT,
		R16G16B16_SINT,
		R16G16B16_FLOAT,

		// 32-bit formats
		R32_UINT,
		R32_SINT,
		R32_FLOAT,
		R16G16_UNORM,
		R16G16_SNORM,
		R16G16_UINT,
		R16G16_SINT,
		R16G16_FLOAT,
		R8G8B8A8_UNORM,
		R8G8B8A8_SNORM,
		R8G8B8A8_UINT,
		R8G8B8A8_SINT,
		R8G8B8A8_SRGB,
		B8G8R8A8_UNORM,
		B8G8R8A8_SNORM,
		B8G8R8A8_UINT,
		B8G8R8A8_SINT,
		B8G8R8A8_SRGB,
		R10G10B10A2_UNORM,
		R10G10B10A2_UINT,
		R11G11B10_FLOAT,
		R9G9B9E5_FLOAT,

		// 64-bit formats
		R32G32_UINT,
		R32G32_SINT,
		R32G32_FLOAT,
		R16G16B16A16_UNORM,
		R16G16B16A16_SNORM,
		R16G16B16A16_UINT,
		R16G16B16A16_SINT,
		R16G16B16A16_FLOAT,

		// 96-bit formats
		R32G32B32_UINT,
		R32G32B32_SINT,
		R32G32B32_FLOAT,

		// 128-bit formats
		R32G32B32A32_UINT,
		R32G32B32A32_SINT,
		R32G32B32A32_FLOAT,

		// Depth/stencil formats
		D16_UNORM,
		D24_UNORM_S8_UINT,
		D32_FLOAT,
		D32_FLOAT_S8_UINT,

		// BC compressed formats
		BC1_UNORM,
		BC1_SRGB,
		BC2_UNORM,
		BC2_SRGB,
		BC3_UNORM,
		BC3_SRGB,
		BC4_UNORM,
		BC4_SNORM,
		BC5_UNORM,
		BC5_SNORM,
		BC6H_UF16,
		BC6H_SF16,
		BC7_UNORM,
		BC7_SRGB,

		// ETC compressed formats
		ETC1_UNORM,
		ETC2_UNORM,
		ETC2_SRGB,
		ETC2A_UNORM,
		ETC2A_SRGB,

		// ASTC compressed formats
		ASTC_4x4_UNORM,
		ASTC_4x4_SRGB,
		ASTC_5x4_UNORM,
		ASTC_5x4_SRGB,
		ASTC_5x5_UNORM,
		ASTC_5x5_SRGB,
		ASTC_6x5_UNORM,
		ASTC_6x5_SRGB,
		ASTC_6x6_UNORM,
		ASTC_6x6_SRGB,
		ASTC_8x5_UNORM,
		ASTC_8x5_SRGB,
		ASTC_8x6_UNORM,
		ASTC_8x6_SRGB,
		ASTC_8x8_UNORM,
		ASTC_8x8_SRGB,
		ASTC_10x5_UNORM,
		ASTC_10x5_SRGB,
		ASTC_10x6_UNORM,
		ASTC_10x6_SRGB,
		ASTC_10x8_UNORM,
		ASTC_10x8_SRGB,
		ASTC_10x10_UNORM,
		ASTC_10x10_SRGB,
		ASTC_12x10_UNORM,
		ASTC_12x10_SRGB,
		ASTC_12x12_UNORM,
		ASTC_12x12_SRGB
	};

	enum class IndexType
	{
		Uint16,
		Uint32
	};

	enum class PrimitiveTopology
	{
		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip
	};

	enum class FilterMode
	{
		Nearest,
		Linear
	};

	enum class AddressMode
	{
		Repeat,
		MirroredRepeat,
		ClampToEdge,
		ClampToBorder,
		MirrorClampToEdge
	};

	enum class CompareOp
	{
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always
	};

	enum class BorderColor
	{
		TransparentBlack,
		OpaqueBlack,
		OpaqueWhite
	};

	enum class BlendFactor
	{
		Zero,
		One,
		SrcColor,
		OneMinusSrcColor,
		DstColor,
		OneMinusDstColor,
		SrcAlpha,
		OneMinusSrcAlpha,
		DstAlpha,
		OneMinusDstAlpha,
		ConstantColor,
		OneMinusConstantColor,
		ConstantAlpha,
		OneMinusConstantAlpha,
		SrcAlphaSaturate
	};

	enum class BlendOp
	{
		Add,
		Subtract,
		ReverseSubtract,
		Min,
		Max
	};

	enum class ColorMask
	{
		Red   = 1 << 0,
		Green = 1 << 1,
		Blue  = 1 << 2,
		Alpha = 1 << 3,
		All   = Red | Green | Blue | Alpha
	};

	enum class CullMode
	{
		None,
		Front,
		Back
	};

	enum class FrontFace
	{
		Clockwise,
		CounterClockwise
	};

	enum class StencilOp
	{
		Keep,
		Zero,
		Replace,
		IncrementClamp,
		DecrementClamp,
		Invert,
		IncrementWrap,
		DecrementWrap
	};

	enum class PolygonMode
	{
		Fill,
		Line,
		Point
	};

	enum class QueryType
	{
		Occlusion,
		PipelineStatistics,
		Timestamp
	};

	enum class AttachmentLoadOp
	{
		Load,
		Clear,
		DontCare
	};

	enum class AttachmentStoreOp
	{
		Store,
		DontCare
	};

	enum class DescriptorType
	{
		Sampler,
		SampledImage,
		StorageImage,
		UniformTexelBuffer,
		StorageTexelBuffer,
		UniformBuffer,
		StorageBuffer,
		UniformBufferDynamic,
		StorageBufferDynamic,
		InputAttachment,
		AccelerationStructure
	};

	enum class PipelineBindPoint
	{
		Graphics,
		Compute,
		RayTracing
	};

	enum class GeometryType
	{
		Triangles,
		AABBs
	};

	enum class BuildAccelerationStructureFlags
	{
		None            = 0,
		AllowUpdate     = 1 << 0,
		AllowCompaction = 1 << 1,
		PreferFastTrace = 1 << 2,
		PreferFastBuild = 1 << 3,
		MinimizeMemory  = 1 << 4,
		PerformUpdate   = 1 << 5
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

	enum class TextureViewType
	{
		Type1D        = 0,
		Type2D        = 1,
		Type3D        = 2,
		TypeCube      = 3,
		Type1DArray   = 4,
		Type2DArray   = 5,
		TypeCubeArray = 6,
		Undefined     = 7,
	};

	enum class PipelineStatisticFlag :u32
	{
		InputAssemblyVertices,
		InputAssemblyPrimitives,
		VertexShaderInvocations,
		GeometryShaderInvocations,
		GeometryShaderPrimitives,
		ClippingInvocations,
		ClippingPrimitives,
		FragmentShaderInvocations,
		TesselationControlShaderPatches,
		TesselationEvaluationShaderInvocations,
		ComputeShaderInvocations
	};

	ENUM_FLAGS(PipelineStatisticFlag, u32);


	struct DeviceInitDesc
	{
		bool enableDebugLayers = false;
	};

	struct DeviceFeatures
	{
		bool tessellationShader;
		bool geometryShader;
		bool computeShader;
		bool multiViewport;
		bool textureCompressionBC;
		bool textureCompressionETC2;
		bool textureCompressionASTC;
		bool independentBlend;
		bool bindlessSupported;
		bool bufferDeviceAddress;
		bool drawIndirectCount;
		bool rayTracing;
	};

	struct DeviceLimits
	{
		u32 maxTextureSize;
		u32 maxTexture3DSize;
		u32 maxCubeMapSize;
		u32 maxViewportDimensions[2];
		u32 maxComputeWorkGroupCount[3];
		u32 maxComputeWorkGroupSize[3];
		u32 maxComputeInvocations;
		u32 maxVertexInputBindings;
		u32 maxVertexInputAttributes;
	};

	struct DeviceProperties
	{
		DeviceType     deviceType;
		String         deviceName;
		String         vendorName;
		String         driverVersion;
		DeviceFeatures features;
		DeviceLimits   limits;
	};

	struct BufferDesc
	{
		usize         size;
		ResourceUsage usage;
		bool          hostVisible{true};
		bool          persistentMapped{false};
		String        debugName;
	};

	struct TextureDesc
	{
		Extent3D      extent{1, 1, 1};
		u32           mipLevels{1};
		u32           arrayLayers{1};
		TextureFormat format{TextureFormat::R8G8B8A8_UNORM};
		ResourceUsage usage{ResourceUsage::ShaderResource | ResourceUsage::CopyDest};
		bool          cubemap{false};
		String        debugName;
	};

	struct TextureViewDesc
	{
		GPUTexture*     texture;
		TextureViewType type = TextureViewType::Type2D;
		u32             baseMipLevel{0};
		u32             mipLevelCount{U32_MAX}; // U32_MAX means use all mip levels
		u32             baseArrayLayer{0};
		u32             arrayLayerCount{U32_MAX}; // U32_MAX means use all array layers
		String          debugName;
	};

	struct SamplerDesc
	{
		FilterMode  minFilter{FilterMode::Linear};
		FilterMode  magFilter{FilterMode::Linear};
		FilterMode  mipmapFilter{FilterMode::Linear};
		AddressMode addressModeU{AddressMode::Repeat};
		AddressMode addressModeV{AddressMode::Repeat};
		AddressMode addressModeW{AddressMode::Repeat};
		f32         mipLodBias{-1.0f};
		bool        anisotropyEnable{false};
		f32         maxAnisotropy{1.0f};
		bool        compareEnable{false};
		CompareOp   compareOp{CompareOp::Never};
		f32         minLod{0.0f};
		f32         maxLod{1000.0f};
		BorderColor borderColor{BorderColor::OpaqueBlack};
		String      debugName;
	};

	struct InterfaceVariable
	{
		u32           location{};
		u32           offset{};
		String        name{};
		TextureFormat format{};
		u32           size{};
	};

	struct BlendStateDesc
	{
		bool        blendEnable{false};
		BlendFactor srcColorBlendFactor{BlendFactor::One};
		BlendFactor dstColorBlendFactor{BlendFactor::Zero};
		BlendOp     colorBlendOp{BlendOp::Add};
		BlendFactor srcAlphaBlendFactor{BlendFactor::One};
		BlendFactor dstAlphaBlendFactor{BlendFactor::Zero};
		BlendOp     alphaBlendOp{BlendOp::Add};
		ColorMask   colorWriteMask{ColorMask::All};
	};

	struct RasterizerStateDesc
	{
		PolygonMode polygonMode{PolygonMode::Fill};
		CullMode    cullMode{CullMode::None};
		FrontFace   frontFace{FrontFace::CounterClockwise};
		bool        depthClampEnable{false};
		bool        rasterizerDiscardEnable{false};
		bool        depthBiasEnable{false};
		f32         depthBiasConstantFactor{0.0f};
		f32         depthBiasClamp{0.0f};
		f32         depthBiasSlopeFactor{0.0f};
		f32         lineWidth{1.0f};
	};

	struct StencilOpStateDesc
	{
		StencilOp failOp{StencilOp::Keep};
		StencilOp passOp{StencilOp::Keep};
		StencilOp depthFailOp{StencilOp::Keep};
		CompareOp compareOp{CompareOp::Always};
		u32       compareMask{0xFF};
		u32       writeMask{0xFF};
		u32       reference{0};
	};

	struct DepthStencilStateDesc
	{
		bool               depthTestEnable{true};
		bool               depthWriteEnable{true};
		CompareOp          depthCompareOp{CompareOp::Less};
		bool               depthBoundsTestEnable{false};
		bool               stencilTestEnable{false};
		StencilOpStateDesc front;
		StencilOpStateDesc back;
		f32                minDepthBounds{1.0f};
		f32                maxDepthBounds{0.0f};
	};

	struct PushConstantRange
	{
		String      name{};
		u32         offset{};
		u32         size{};
		ShaderStage stages{};
	};

	struct DescriptorSetLayoutBinding
	{
		u32             binding{};
		u32             count = 1;
		String          name{};
		DescriptorType  descriptorType{};
		RenderType      renderType{};
		ShaderStage     shaderStage{ShaderStage::All};
		TextureViewType viewType{};
		u32             size{};
	};

	struct DescriptorSetLayout
	{
		u32                               set;
		Array<DescriptorSetLayoutBinding> bindings;
		String                            debugName;
	};

	struct DescriptorSetDesc
	{
		Array<DescriptorSetLayoutBinding> bindings;
		String                            debugName;
	};

	struct PipelineDesc
	{
		Array<InterfaceVariable>   inputVariables{};
		Array<InterfaceVariable>   outputVariables{};
		Array<DescriptorSetLayout> descriptors{};
		Array<PushConstantRange>   pushConstants{};
		u32                        stride{};
	};

	struct AttachmentDesc
	{
		GPUTexture*       texture{};
		GPUTextureView*   textureView{};
		ResourceState     initialState{ResourceState::Undefined};
		ResourceState     finalState{ResourceState::Undefined};
		AttachmentLoadOp  loadOp{AttachmentLoadOp::Clear};
		AttachmentStoreOp storeOp{AttachmentStoreOp::Store};
		AttachmentLoadOp  stencilLoadOp{AttachmentLoadOp::DontCare};
		AttachmentStoreOp stencilStoreOp{AttachmentStoreOp::DontCare};
	};

	struct RenderPassDesc
	{
		Array<AttachmentDesc> attachments;
		String                debugName;
	};

	struct GraphicsPipelineDesc
	{
		ShaderVariant*        shaderVariant{};
		PrimitiveTopology     topology{PrimitiveTopology::TriangleList};
		RasterizerStateDesc   rasterizerState;
		DepthStencilStateDesc depthStencilState;
		Array<BlendStateDesc> blendStates;
		GPURenderPass*        renderPass{nullptr};
		String                debugName;
		GPUPipeline*          previousState = nullptr;
		u32                   vertexInputStride = U32_MAX;
	};

	struct ComputePipelineDesc
	{
		ShaderVariant* shaderVariant{};
		String         debugName;
		GPUPipeline*   previousState = nullptr;
	};

	struct QueryPoolDesc
	{
		QueryType             type;
		u32                   queryCount;
		bool                  allowPartialResults;
		bool                  returnAvailability;
		PipelineStatisticFlag pipelineStatistics;
		String                debugName;
	};

	struct DescriptorUpdate
	{
		DescriptorType type;
		u32            binding = U32_MAX;
		u32            arrayElement{0};

		GPUBuffer* buffer{nullptr};
		usize      bufferOffset{0};
		usize      bufferRange{0};

		GPUTexture*     texture{nullptr};
		GPUTextureView* textureView{nullptr};
		GPUSampler*     sampler{nullptr};
		GPUTopLevelAS*  topLevelAS{nullptr};
	};

	struct GeometryTrianglesDesc
	{
		GPUBuffer*    vertexBuffer{nullptr};
		usize         vertexOffset{0};
		u32           vertexCount{0};
		u32           vertexStride{0};
		TextureFormat vertexFormat{TextureFormat::R32G32B32_FLOAT};

		GPUBuffer* indexBuffer{nullptr};
		usize      indexOffset{0};
		u32        indexCount{0};
		IndexType  indexType{IndexType::Uint32};

		GPUBuffer* transformBuffer{nullptr};
		usize      transformOffset{0};

		bool opaque{true};
	};

	struct GeometryAABBsDesc
	{
		GPUBuffer* aabbBuffer{nullptr};
		usize      aabbOffset{0};
		u32        aabbCount{0};
		u32        aabbStride{0};

		bool opaque{true};
	};

	struct GeometryDesc
	{
		GeometryType          type{GeometryType::Triangles};
		GeometryTrianglesDesc triangles;
		GeometryAABBsDesc     aabbs;
	};

	struct BottomLevelASDesc
	{
		Span<GeometryDesc>              geometries;
		BuildAccelerationStructureFlags flags{BuildAccelerationStructureFlags::PreferFastTrace};
		String                          debugName;
	};

	struct InstanceDesc
	{
		GPUBottomLevelAS* bottomLevelAS{nullptr};
		Mat4              transform{1.0f};
		u32               instanceID{0};
		u32               instanceMask{0xFF};
		u32               instanceShaderBindingTableRecordOffset{0};
		bool              frontCounterClockwise{false};
		bool              forceOpaque{false};
		bool              forceNonOpaque{false};
	};

	struct TopLevelASDesc
	{
		Span<InstanceDesc>              instances;
		BuildAccelerationStructureFlags flags{BuildAccelerationStructureFlags::PreferFastTrace};
		String                          debugName;
	};

	struct AccelerationStructureBuildInfo
	{
		bool       update{false};
		GPUBuffer* scratchBuffer{nullptr};
		usize      scratchOffset{0};
	};

	struct RayTracingPipelineDesc
	{
		ShaderAsset* shaderAsset;
		u32          maxRecursionDepth{1};
		String       debugName;
	};

	struct SwapchainDesc
	{
		TextureFormat format{TextureFormat::B8G8R8A8_UNORM};
		bool          vsync = true;
		VoidPtr       windowHandle{nullptr};
		String        debugName;
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

	class SK_API GPUDevice
	{
	public:
		virtual ~GPUDevice() = default;

		virtual Span<GPUAdapter*>       GetAdapters() = 0;
		virtual bool                    SelectAdapter(GPUAdapter* adapter) = 0;
		virtual const DeviceProperties& GetProperties() = 0;
		virtual const DeviceFeatures&   GetFeatures() = 0;
		virtual GraphicsAPI             GetAPI() const = 0;
		virtual void                    WaitIdle() = 0;
		virtual GPUSwapchain*           CreateSwapchain(const SwapchainDesc& desc) = 0;
		virtual GPURenderPass*          CreateRenderPass(const RenderPassDesc& desc) = 0;
		virtual GPUCommandBuffer*       CreateCommandBuffer() = 0;
		virtual GPUBuffer*              CreateBuffer(const BufferDesc& desc) = 0;
		virtual GPUTexture*             CreateTexture(const TextureDesc& desc) = 0;
		virtual GPUTextureView*         CreateTextureView(const TextureViewDesc& desc) = 0;
		virtual GPUSampler*             CreateSampler(const SamplerDesc& desc) = 0;
		virtual GPUPipeline*            CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
		virtual GPUPipeline*            CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
		virtual GPUPipeline*            CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) = 0;
		virtual GPUDescriptorSet*       CreateDescriptorSet(const DescriptorSetDesc& desc) = 0;
		virtual GPUDescriptorSet*       CreateDescriptorSet(ShaderVariant* shaderVariant, u32 set) = 0;
		virtual GPUQueryPool*           CreateQueryPool(const QueryPoolDesc& desc) = 0;
		virtual GPUBottomLevelAS*       CreateBottomLevelAS(const BottomLevelASDesc& desc) = 0;
		virtual GPUTopLevelAS*          CreateTopLevelAS(const TopLevelASDesc& desc) = 0;
		virtual bool                    SubmitAndPresent(GPUSwapchain* swapchain, GPUCommandBuffer* commandBuffer, u32 currentFrame) = 0;

		virtual usize GetBottomLevelASSize(const BottomLevelASDesc& desc) = 0;
		virtual usize GetTopLevelASSize(const TopLevelASDesc& desc) = 0;
		virtual usize GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc) = 0;
		virtual usize GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc) = 0;
	};

	class SK_API GPUSwapchain
	{
	public:
		virtual ~GPUSwapchain() = default;

		virtual const SwapchainDesc& GetDesc() const = 0;

		virtual bool           AcquireNextImage(u32 currentFrame) = 0;
		virtual GPURenderPass* GetCurrentRenderPass() = 0;
		virtual bool           Resize() = 0;
		virtual void           Destroy() = 0;
		virtual u32            GetImageCount() const = 0;
	};

	class SK_API GPUCommandBuffer
	{
	public:
		virtual ~GPUCommandBuffer() = default;

		virtual void Begin() = 0;
		virtual void End() = 0;
		virtual void Reset() = 0;
		virtual void SubmitAndWait() = 0;

		virtual void SetViewport(const ViewportInfo& viewportInfo) = 0;
		virtual void SetScissor(Vec2 position, Extent size) = 0;


		virtual void BindPipeline(GPUPipeline* pipeline) = 0;
		virtual void BindDescriptorSet(GPUPipeline* pipeline, u32 setIndex, GPUDescriptorSet* descriptorSet, Span<u32> dynamicOffsets) = 0;
		virtual void BindVertexBuffer(u32 firstBinding, GPUBuffer* buffers, usize offset) = 0;
		virtual void BindIndexBuffer(GPUBuffer* buffer, usize offset, IndexType indexType) = 0;

		virtual void PushConstants(GPUPipeline* pipeline, ShaderStage stages, u32 offset, u32 size, const void* data) = 0;

		virtual void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) = 0;
		virtual void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) = 0;
		virtual void DrawIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride) = 0;
		virtual void DrawIndexedIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride) = 0;
		virtual void Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) = 0;
		virtual void DispatchIndirect(GPUBuffer* buffer, usize offset) = 0;

		virtual void TraceRays(GPUPipeline* pipeline, u32 width, u32 height, u32 depth) = 0;

		virtual void BuildBottomLevelAS(GPUBottomLevelAS* bottomLevelAS, const AccelerationStructureBuildInfo& buildInfo) = 0;
		virtual void BuildTopLevelAS(GPUTopLevelAS* topLevelAS, const AccelerationStructureBuildInfo& buildInfo) = 0;
		virtual void CopyAccelerationStructure(GPUBottomLevelAS* src, GPUBottomLevelAS* dst, bool compress) = 0;
		virtual void CopyAccelerationStructure(GPUTopLevelAS* src, GPUTopLevelAS* dst, bool compress) = 0;

		virtual void BeginRenderPass(GPURenderPass* renderPass, Vec4 clearColor, f32 clearDepth, u32 clearStencil) = 0;
		virtual void EndRenderPass() = 0;

		virtual void CopyBuffer(GPUBuffer* srcBuffer, GPUBuffer* dstBuffer, usize size, usize srcOffset, usize dstOffset) = 0;
		virtual void CopyBufferToTexture(GPUBuffer* srcBuffer, GPUTexture* dstTexture, Extent3D extent, u32 mipLevel, u32 arrayLayer, u64 bufferOffset) = 0;
		virtual void CopyTextureToBuffer(GPUTexture* srcTexture, GPUBuffer* dstBuffer, Extent3D extent, u32 mipLevel, u32 arrayLayer) = 0;
		virtual void CopyTexture(GPUTexture* srcTexture, GPUTexture* dstTexture, Extent3D extent, u32 srcMipLevel, u32 srcArrayLayer, u32 dstMipLevel, u32 dstArrayLayer) = 0;
		virtual void BlitTexture(GPUTexture* srcTexture, GPUTexture* dstTexture, Extent3D srcExtent, Extent3D dstExtent, u32 srcMipLevel, u32 srcArrayLayer, u32 dstMipLevel, u32 dstArrayLayer) = 0;
		virtual void FillBuffer(GPUBuffer* buffer, usize offset, usize size, u32 data) = 0;
		virtual void UpdateBuffer(GPUBuffer* buffer, usize offset, usize size, const void* data) = 0;
		virtual void ClearColorTexture(GPUTexture* texture, Vec4 clearValue, u32 mipLevel, u32 arrayLayer) = 0;
		virtual void ClearDepthStencilTexture(GPUTexture* texture, f32 depth, u32 stencil, u32 mipLevel, u32 arrayLayer) = 0;

		virtual void ResourceBarrier(GPUBuffer* buffer, ResourceState oldState, ResourceState newState) = 0;
		virtual void ResourceBarrier(GPUTexture* texture, ResourceState oldState, ResourceState newState, u32 mipLevel, u32 arrayLayer) = 0;
		virtual void ResourceBarrier(GPUTexture* texture, ResourceState oldState, ResourceState newState, u32 mipLevel, u32 levelCount, u32 arrayLayer, u32 layerCount) = 0;
		virtual void ResourceBarrier(GPUBottomLevelAS* bottomLevelAS, ResourceState oldState, ResourceState newState) = 0;
		virtual void ResourceBarrier(GPUTopLevelAS* topLevelAS, ResourceState oldState, ResourceState newState) = 0;
		virtual void MemoryBarrier() = 0;

		virtual void BeginQuery(GPUQueryPool* queryPool, u32 query) = 0;
		virtual void EndQuery(GPUQueryPool* queryPool, u32 query) = 0;
		virtual void ResetQueryPool(GPUQueryPool* queryPool, u32 firstQuery, u32 queryCount) = 0;
		virtual void WriteTimestamp(GPUQueryPool* queryPool, u32 query) = 0;
		virtual void CopyQueryPoolResults(GPUQueryPool* queryPool, u32 firstQuery, u32 queryCount, GPUBuffer* dstBuffer, usize dstOffset, usize stride) = 0;

		virtual void BeginDebugMarker(StringView name, const Vec4& color) = 0;
		virtual void EndDebugMarker() = 0;
		virtual void InsertDebugMarker(StringView name, const Vec4& color) = 0;
		virtual void Destroy() = 0;
	};

	class SK_API GPUAdapter
	{
	public:
		virtual            ~GPUAdapter() = default;
		virtual u32        GetScore() = 0;
		virtual StringView GetName() = 0;
	};

	class SK_API GPUBuffer
	{
	public:
		virtual ~GPUBuffer() = default;

		virtual void*   Map() = 0;
		virtual void    Unmap() = 0;
		virtual void    Destroy() = 0;
		virtual VoidPtr GetMappedData() = 0;

		virtual const BufferDesc& GetDesc() const = 0;
	};

	class SK_API GPUTexture
	{
	public:
		virtual                    ~GPUTexture() = default;
		virtual const TextureDesc& GetDesc() const = 0;
		virtual GPUTextureView*    GetTextureView() const = 0;
		virtual void               Destroy() = 0;
	};

	class SK_API GPUTextureView
	{
	public:
		virtual                        ~GPUTextureView() = default;
		virtual const TextureViewDesc& GetDesc() = 0;
		virtual void                   Destroy() = 0;
		virtual GPUTexture*            GetTexture() = 0;
	};

	class SK_API GPUSampler
	{
	public:
		virtual                    ~GPUSampler() = default;
		virtual const SamplerDesc& GetDesc() const = 0;
		virtual void               Destroy() = 0;
	};

	class SK_API GPUPipeline
	{
	public:
		virtual ~GPUPipeline() = default;

		virtual PipelineBindPoint   GetBindPoint() const = 0;
		virtual const PipelineDesc& GetPipelineDesc() const = 0;

		virtual void Destroy() = 0;
	};

	class SK_API GPUDescriptorSet
	{
	public:
		virtual ~GPUDescriptorSet() = default;

		virtual const DescriptorSetDesc& GetDesc() const = 0;

		virtual void Update(const DescriptorUpdate& update) = 0;
		virtual void UpdateBuffer(u32 binding, GPUBuffer* buffer, usize offset, usize size) = 0;
		virtual void UpdateTexture(u32 binding, GPUTexture* texture) = 0;
		virtual void UpdateTexture(u32 binding, GPUTexture* texture, u32 arrayElement) = 0;
		virtual void UpdateTextureView(u32 binding, GPUTextureView* textureView) = 0;
		virtual void UpdateTextureView(u32 binding, GPUTextureView* textureView, u32 arrayElement) = 0;
		virtual void UpdateSampler(u32 binding, GPUSampler* sampler) = 0;
		virtual void UpdateSampler(u32 binding, GPUSampler* sampler, u32 arrayElement) = 0;
		virtual void Destroy() = 0;
	};

	class SK_API GPURenderPass
	{
	public:
		virtual ~GPURenderPass() = default;

		virtual const RenderPassDesc& GetDesc() const = 0;
		virtual void                  Destroy() = 0;
	};

	class SK_API GPUQueryPool
	{
	public:
		virtual ~GPUQueryPool() = default;

		virtual const QueryPoolDesc& GetDesc() const = 0;

		virtual bool GetResults(u32 firstQuery, u32 queryCount, void* data, usize stride, bool wait) = 0;
		virtual void Destroy() = 0;
	};

	class SK_API GPUBottomLevelAS
	{
	public:
		virtual ~GPUBottomLevelAS() = default;

		virtual const BottomLevelASDesc& GetDesc() const = 0;

		virtual bool  IsCompacted() const = 0;
		virtual usize GetCompactedSize() const = 0;
		virtual void  Destroy() = 0;
	};

	class SK_API GPUTopLevelAS
	{
	public:
		virtual ~GPUTopLevelAS() = default;

		virtual const TopLevelASDesc& GetDesc() const = 0;

		virtual bool UpdateInstances(Span<InstanceDesc> instances) = 0;
		virtual void Destroy() = 0;
	};


	struct BufferUploadInfo
	{
		GPUBuffer*  buffer{};
		const void* data{};
		usize       size{};
		usize       srcOffset{};
		usize       dstOffset{};
	};

	struct TextureDataRegion
	{
		usize    dataOffset{};
		u32      layerCount = 1;
		u32      levelCount = 1;
		u32      mipLevel{};
		u32      arrayLayer{};
		Extent3D extent{};
	};

	struct TextureDataInfo
	{
		GPUTexture*             texture{};
		const u8*               data{nullptr};
		usize                   size{};
		Span<TextureDataRegion> regions{};
	};

	inline u32 GetTextureFormatSize(TextureFormat format)
	{
		switch (format)
		{
			// 8-bit formats
			case TextureFormat::R8_UNORM:
			case TextureFormat::R8_SNORM:
			case TextureFormat::R8_UINT:
			case TextureFormat::R8_SINT:
			case TextureFormat::R8_SRGB:
				return 1;

			// 16-bit formats
			case TextureFormat::R16_UNORM:
			case TextureFormat::R16_SNORM:
			case TextureFormat::R16_UINT:
			case TextureFormat::R16_SINT:
			case TextureFormat::R16_FLOAT:
			case TextureFormat::R8G8_UNORM:
			case TextureFormat::R8G8_SNORM:
			case TextureFormat::R8G8_UINT:
			case TextureFormat::R8G8_SINT:
			case TextureFormat::R8G8_SRGB:
				return 2;

			// 24-bit formats (3 bytes)
			case TextureFormat::R16G16B16_UNORM:
			case TextureFormat::R16G16B16_SNORM:
			case TextureFormat::R16G16B16_UINT:
			case TextureFormat::R16G16B16_SINT:
			case TextureFormat::R16G16B16_FLOAT:
				return 6; // 3 components, 2 bytes each

			// 32-bit formats (4 bytes)
			case TextureFormat::R32_UINT:
			case TextureFormat::R32_SINT:
			case TextureFormat::R32_FLOAT:
			case TextureFormat::R16G16_UNORM:
			case TextureFormat::R16G16_SNORM:
			case TextureFormat::R16G16_UINT:
			case TextureFormat::R16G16_SINT:
			case TextureFormat::R16G16_FLOAT:
			case TextureFormat::R8G8B8A8_UNORM:
			case TextureFormat::R8G8B8A8_SNORM:
			case TextureFormat::R8G8B8A8_UINT:
			case TextureFormat::R8G8B8A8_SINT:
			case TextureFormat::R8G8B8A8_SRGB:
			case TextureFormat::B8G8R8A8_UNORM:
			case TextureFormat::B8G8R8A8_SNORM:
			case TextureFormat::B8G8R8A8_UINT:
			case TextureFormat::B8G8R8A8_SINT:
			case TextureFormat::B8G8R8A8_SRGB:
			case TextureFormat::R10G10B10A2_UNORM:
			case TextureFormat::R10G10B10A2_UINT:
			case TextureFormat::R11G11B10_FLOAT:
			case TextureFormat::R9G9B9E5_FLOAT:
				return 4;

			// 64-bit formats (8 bytes)
			case TextureFormat::R32G32_UINT:
			case TextureFormat::R32G32_SINT:
			case TextureFormat::R32G32_FLOAT:
			case TextureFormat::R16G16B16A16_UNORM:
			case TextureFormat::R16G16B16A16_SNORM:
			case TextureFormat::R16G16B16A16_UINT:
			case TextureFormat::R16G16B16A16_SINT:
			case TextureFormat::R16G16B16A16_FLOAT:
				return 8;

			// 96-bit formats (12 bytes)
			case TextureFormat::R32G32B32_UINT:
			case TextureFormat::R32G32B32_SINT:
			case TextureFormat::R32G32B32_FLOAT:
				return 12;

			// 128-bit formats (16 bytes)
			case TextureFormat::R32G32B32A32_UINT:
			case TextureFormat::R32G32B32A32_SINT:
			case TextureFormat::R32G32B32A32_FLOAT:
				return 16;

			// Depth/stencil formats
			case TextureFormat::D16_UNORM:
				return 2;
			case TextureFormat::D24_UNORM_S8_UINT:
				return 4;
			case TextureFormat::D32_FLOAT:
				return 4;
			case TextureFormat::D32_FLOAT_S8_UINT:
				return 5; // Sometimes implemented as 8 bytes with padding

			// BC compressed formats (block size 4x4 texels)
			// BC1: 64 bits per 4x4 block = 0.5 bytes per texel
			case TextureFormat::BC1_UNORM:
			case TextureFormat::BC1_SRGB:
				return 8; // 8 bytes per 4x4 block

			// BC2/BC3: 128 bits per 4x4 block = 1 byte per texel
			case TextureFormat::BC2_UNORM:
			case TextureFormat::BC2_SRGB:
			case TextureFormat::BC3_UNORM:
			case TextureFormat::BC3_SRGB:
			case TextureFormat::BC4_UNORM:
			case TextureFormat::BC4_SNORM:
			case TextureFormat::BC5_UNORM:
			case TextureFormat::BC5_SNORM:
			case TextureFormat::BC6H_UF16:
			case TextureFormat::BC6H_SF16:
			case TextureFormat::BC7_UNORM:
			case TextureFormat::BC7_SRGB:
				return 16; // 16 bytes per 4x4 block

			// ETC compressed formats
			case TextureFormat::ETC1_UNORM:
			case TextureFormat::ETC2_UNORM:
			case TextureFormat::ETC2_SRGB:
				return 8; // 8 bytes per 4x4 block
			case TextureFormat::ETC2A_UNORM:
			case TextureFormat::ETC2A_SRGB:
				return 16; // 16 bytes per 4x4 block

			// ASTC compressed formats (block sizes vary)
			case TextureFormat::ASTC_4x4_UNORM:
			case TextureFormat::ASTC_4x4_SRGB:
			case TextureFormat::ASTC_5x4_UNORM:
			case TextureFormat::ASTC_5x4_SRGB:
			case TextureFormat::ASTC_5x5_UNORM:
			case TextureFormat::ASTC_5x5_SRGB:
			case TextureFormat::ASTC_6x5_UNORM:
			case TextureFormat::ASTC_6x5_SRGB:
			case TextureFormat::ASTC_6x6_UNORM:
			case TextureFormat::ASTC_6x6_SRGB:
			case TextureFormat::ASTC_8x5_UNORM:
			case TextureFormat::ASTC_8x5_SRGB:
			case TextureFormat::ASTC_8x6_UNORM:
			case TextureFormat::ASTC_8x6_SRGB:
			case TextureFormat::ASTC_8x8_UNORM:
			case TextureFormat::ASTC_8x8_SRGB:
			case TextureFormat::ASTC_10x5_UNORM:
			case TextureFormat::ASTC_10x5_SRGB:
			case TextureFormat::ASTC_10x6_UNORM:
			case TextureFormat::ASTC_10x6_SRGB:
			case TextureFormat::ASTC_10x8_UNORM:
			case TextureFormat::ASTC_10x8_SRGB:
			case TextureFormat::ASTC_10x10_UNORM:
			case TextureFormat::ASTC_10x10_SRGB:
			case TextureFormat::ASTC_12x10_UNORM:
			case TextureFormat::ASTC_12x10_SRGB:
			case TextureFormat::ASTC_12x12_UNORM:
			case TextureFormat::ASTC_12x12_SRGB:
				return 16; // 16 bytes per block, regardless of block size

			case TextureFormat::Unknown:
			default:
				return 0;
		}
	}

	inline i8 GetTextureFormatNumChannels(TextureFormat format)
	{
		switch (format)
		{
			// Single channel formats
			case TextureFormat::R8_UNORM:
			case TextureFormat::R8_SNORM:
			case TextureFormat::R8_UINT:
			case TextureFormat::R8_SINT:
			case TextureFormat::R8_SRGB:
			case TextureFormat::R16_UNORM:
			case TextureFormat::R16_SNORM:
			case TextureFormat::R16_UINT:
			case TextureFormat::R16_SINT:
			case TextureFormat::R16_FLOAT:
			case TextureFormat::R32_UINT:
			case TextureFormat::R32_SINT:
			case TextureFormat::R32_FLOAT:
				return 1;

			// Two channel formats
			case TextureFormat::R8G8_UNORM:
			case TextureFormat::R8G8_SNORM:
			case TextureFormat::R8G8_UINT:
			case TextureFormat::R8G8_SINT:
			case TextureFormat::R8G8_SRGB:
			case TextureFormat::R16G16_UNORM:
			case TextureFormat::R16G16_SNORM:
			case TextureFormat::R16G16_UINT:
			case TextureFormat::R16G16_SINT:
			case TextureFormat::R16G16_FLOAT:
			case TextureFormat::R32G32_UINT:
			case TextureFormat::R32G32_SINT:
			case TextureFormat::R32G32_FLOAT:
				return 2;

			// Three channel formats
			case TextureFormat::R16G16B16_UNORM:
			case TextureFormat::R16G16B16_SNORM:
			case TextureFormat::R16G16B16_UINT:
			case TextureFormat::R16G16B16_SINT:
			case TextureFormat::R16G16B16_FLOAT:
			case TextureFormat::R32G32B32_UINT:
			case TextureFormat::R32G32B32_SINT:
			case TextureFormat::R32G32B32_FLOAT:
			case TextureFormat::R11G11B10_FLOAT:
				return 3;

			// Four channel formats
			case TextureFormat::R8G8B8A8_UNORM:
			case TextureFormat::R8G8B8A8_SNORM:
			case TextureFormat::R8G8B8A8_UINT:
			case TextureFormat::R8G8B8A8_SINT:
			case TextureFormat::R8G8B8A8_SRGB:
			case TextureFormat::B8G8R8A8_UNORM:
			case TextureFormat::B8G8R8A8_SNORM:
			case TextureFormat::B8G8R8A8_UINT:
			case TextureFormat::B8G8R8A8_SINT:
			case TextureFormat::B8G8R8A8_SRGB:
			case TextureFormat::R10G10B10A2_UNORM:
			case TextureFormat::R10G10B10A2_UINT:
			case TextureFormat::R16G16B16A16_UNORM:
			case TextureFormat::R16G16B16A16_SNORM:
			case TextureFormat::R16G16B16A16_UINT:
			case TextureFormat::R16G16B16A16_SINT:
			case TextureFormat::R16G16B16A16_FLOAT:
			case TextureFormat::R32G32B32A32_UINT:
			case TextureFormat::R32G32B32A32_SINT:
			case TextureFormat::R32G32B32A32_FLOAT:
				return 4;

			// Special formats with packed channels
			case TextureFormat::R9G9B9E5_FLOAT:
				return 3;

			// Depth/stencil formats
			case TextureFormat::D16_UNORM:
				return 1;
			case TextureFormat::D24_UNORM_S8_UINT:
			case TextureFormat::D32_FLOAT_S8_UINT:
				return 2; // Depth + stencil
			case TextureFormat::D32_FLOAT:
				return 1;

			// Compressed formats
			// BC1, BC2, BC3 typically represent RGBA data
			case TextureFormat::BC1_UNORM:
			case TextureFormat::BC1_SRGB:
			case TextureFormat::BC2_UNORM:
			case TextureFormat::BC2_SRGB:
			case TextureFormat::BC3_UNORM:
			case TextureFormat::BC3_SRGB:
			case TextureFormat::BC7_UNORM:
			case TextureFormat::BC7_SRGB:
				return 4;

			// BC4 represents a single channel
			case TextureFormat::BC4_UNORM:
			case TextureFormat::BC4_SNORM:
				return 1;

			// BC5 represents two channels (typically RG)
			case TextureFormat::BC5_UNORM:
			case TextureFormat::BC5_SNORM:
				return 2;

			// BC6H represents RGB data
			case TextureFormat::BC6H_UF16:
			case TextureFormat::BC6H_SF16:
				return 3;

			// ETC formats
			case TextureFormat::ETC1_UNORM:
			case TextureFormat::ETC2_UNORM:
			case TextureFormat::ETC2_SRGB:
				return 3; // RGB
			case TextureFormat::ETC2A_UNORM:
			case TextureFormat::ETC2A_SRGB:
				return 4; // RGBA

			// ASTC formats (all represent RGBA data)
			case TextureFormat::ASTC_4x4_UNORM:
			case TextureFormat::ASTC_4x4_SRGB:
			case TextureFormat::ASTC_5x4_UNORM:
			case TextureFormat::ASTC_5x4_SRGB:
			case TextureFormat::ASTC_5x5_UNORM:
			case TextureFormat::ASTC_5x5_SRGB:
			case TextureFormat::ASTC_6x5_UNORM:
			case TextureFormat::ASTC_6x5_SRGB:
			case TextureFormat::ASTC_6x6_UNORM:
			case TextureFormat::ASTC_6x6_SRGB:
			case TextureFormat::ASTC_8x5_UNORM:
			case TextureFormat::ASTC_8x5_SRGB:
			case TextureFormat::ASTC_8x6_UNORM:
			case TextureFormat::ASTC_8x6_SRGB:
			case TextureFormat::ASTC_8x8_UNORM:
			case TextureFormat::ASTC_8x8_SRGB:
			case TextureFormat::ASTC_10x5_UNORM:
			case TextureFormat::ASTC_10x5_SRGB:
			case TextureFormat::ASTC_10x6_UNORM:
			case TextureFormat::ASTC_10x6_SRGB:
			case TextureFormat::ASTC_10x8_UNORM:
			case TextureFormat::ASTC_10x8_SRGB:
			case TextureFormat::ASTC_10x10_UNORM:
			case TextureFormat::ASTC_10x10_SRGB:
			case TextureFormat::ASTC_12x10_UNORM:
			case TextureFormat::ASTC_12x10_SRGB:
			case TextureFormat::ASTC_12x12_UNORM:
			case TextureFormat::ASTC_12x12_SRGB:
				return 4;

			case TextureFormat::Unknown:
			default:
				return 0;
		}
	}

	inline TextureViewType GetTextureViewType(bool isCube, u32 depth, u32 height, u32 arrayLayers)
	{
		if (isCube)
		{
			return TextureViewType::TypeCube;
		}

		if (depth > 1)
		{
			return TextureViewType::Type3D;
		}

		if (arrayLayers > 1)
		{
			return TextureViewType::Type2DArray;
		}
		return TextureViewType::Type2D;
	}
}
