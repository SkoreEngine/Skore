#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	class GPUTextureView;
	class GPUAdapter;
	class GPUDevice;
	class GPUSwapchain;
	class GPUCommandBuffer;
	class GPUBuffer;
	class GPUMemory;
	class GPUTexture;
	class GPUSampler;
	class GPUPipeline;
	class GPUDescriptorSet;
	class GPURenderPass;
	class GPUQueryPool;
	class GPUBottomLevelAS;
	class GPUTopLevelAS;
	class GPUFramebuffer;
	class GPUQueue;

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

	enum class DeviceResult
	{
		Success,
		SwapchainOutOfDate,
		Error
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

	enum class BarrierSyncScope
	{
		Automatic,
		Graphics,
		Compute,
		Raytrace,
		Transfer
	};

	struct BufferBarrierDesc
	{
		GPUBuffer*       buffer = nullptr;
		ResourceState    oldState = ResourceState::Undefined;
		ResourceState    newState = ResourceState::Undefined;
		BarrierSyncScope srcScope = BarrierSyncScope::Automatic;
		BarrierSyncScope dstScope = BarrierSyncScope::Automatic;
	};

	struct TextureBarrierDesc
	{
		GPUTexture*      texture = nullptr;
		ResourceState    oldState = ResourceState::Undefined;
		ResourceState    newState = ResourceState::Undefined;
		BarrierSyncScope srcScope = BarrierSyncScope::Automatic;
		BarrierSyncScope dstScope = BarrierSyncScope::Automatic;
		u32              baseMipLevel = 0;
		u32              mipLevelCount = 1;
		u32              baseArrayLayer = 0;
		u32              arrayLayerCount = 1;
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
		IndirectBuffer        = 1 << 7,
		CopyDest              = 1 << 8,
		CopySource            = 1 << 9,
		AccelerationStructure = 1 << 10,
		RayTracing            = 1 << 11
	};

	ENUM_FLAGS(ResourceUsage, u32);

	enum class Format
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
		RG8_UNORM,
		RG8_SNORM,
		RG8_UINT,
		RG8_SINT,
		RG8_SRGB,

		RGB16_UNORM,
		RGB16_SNORM,
		RGB16_UINT,
		RGB16_SINT,
		RGB16_FLOAT,

		// 32-bit formats
		R32_UINT,
		R32_SINT,
		R32_FLOAT,
		RG16_UNORM,
		RG16_SNORM,
		RG16_UINT,
		RG16_SINT,
		RG16_FLOAT,
		RGBA8_UNORM,
		RGBA8_SNORM,
		RGBA8_UINT,
		RGBA8_SINT,
		RGBA8_SRGB,
		BGRA8_UNORM,
		BGRA8_SNORM,
		BGRA8_UINT,
		BGRA8_SINT,
		BGRA8_SRGB,
		RGB10A2_UNORM,
		RGB10A2_UINT,
		RG11B10_FLOAT,
		RGB9E5_FLOAT,

		// 64-bit formats
		RG32_UINT,
		RG32_SINT,
		RG32_FLOAT,
		RGBA16_UNORM,
		RGBA16_SNORM,
		RGBA16_UINT,
		RGBA16_SINT,
		RGBA16_FLOAT,

		// 96-bit formats
		RGB32_UINT,
		RGB32_SINT,
		RGB32_FLOAT,

		// 128-bit formats
		RGBA32_UINT,
		RGBA32_SINT,
		RGBA32_FLOAT,

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

	enum class ConservativeRasterizationMode
	{
		Disabled = 0,
		Overestimate,
		Underestimate
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
		None,
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

	ENUM_FLAGS(BuildAccelerationStructureFlags, u32);

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

	enum class QueueType : u32
	{
		None,
		Graphics = 1 << 0,
		Compute  = 1 << 1,
		Transfer = 1 << 2,
		All = Graphics | Compute | Transfer
	};

	ENUM_FLAGS(QueueType, u32);


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
		bool bindlessTextureSupported;
		bool multiviewEnabled;
		bool bindlessSamplerSupported;
		bool bindlessBufferSupported;
		bool bufferDeviceAddress;
		bool drawIndirectCount;
		bool rayTracing;
		bool resolveDepth;
		bool memoryBudget;
		bool fragmentShaderBarycentric;
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
		u32 maxAttachmentSamples;
		u64 minMemoryMapAlignment;
		u64 minUniformBufferOffsetAlignment;
		f32 timestampPeriod;
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
		u32           sampleCount{1};
		Format format{Format::RGBA8_UNORM};
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

	struct TextureMemoryRequirements
	{
		u64 size{};
		u64 alignment{};
		u32 memoryTypeBits{};
	};

	struct SamplerDesc
	{
		FilterMode  minFilter{FilterMode::Linear};
		FilterMode  magFilter{FilterMode::Linear};
		FilterMode  mipmapFilter{FilterMode::Linear};
		AddressMode addressModeU{AddressMode::Repeat};
		AddressMode addressModeV{AddressMode::Repeat};
		AddressMode addressModeW{AddressMode::Repeat};
		f32         mipLodBias{0.0f};
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
		Format format{};
		u32           size{};
	};



	struct BlendStateDesc
	{
		bool        blendEnable{false};
		BlendFactor srcColorBlendFactor{BlendFactor::SrcAlpha};
		BlendFactor dstColorBlendFactor{BlendFactor::OneMinusSrcAlpha};
		BlendOp     colorBlendOp{BlendOp::Add};
		BlendFactor srcAlphaBlendFactor{BlendFactor::One};
		BlendFactor dstAlphaBlendFactor{BlendFactor::Zero};
		BlendOp     alphaBlendOp{BlendOp::Max};
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
		CompareOp          depthCompareOp{CompareOp::Greater}; // reverse-Z default (was Less)
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
		Extent3D                   numThreads{1, 1, 1};
	};

	struct AttachmentDesc
	{
		ResourceState     initialState{ResourceState::Undefined};
		ResourceState     finalState{ResourceState::Undefined};
		AttachmentLoadOp  loadOp{AttachmentLoadOp::Clear};
		AttachmentStoreOp storeOp{AttachmentStoreOp::Store};
		AttachmentLoadOp  stencilLoadOp{AttachmentLoadOp::DontCare};
		AttachmentStoreOp stencilStoreOp{AttachmentStoreOp::DontCare};

		u32           sampleCount{1};
		Format format{Format::RGBA8_UNORM};

	};

	struct RenderPassDesc
	{
		Array<AttachmentDesc> attachments;
		Array<AttachmentDesc> resolveAttachments;
		String                debugName;
	};

	struct ClearValues
	{
		Vec4 color = Vec4(0, 0, 0, 0);
		f32  depth = 0.0; // reverse-Z: far plane = 0 (was 1.0)
		f32  stencil = 0.0;
	};

	struct BeginRenderPassInfo
	{
		GPURenderPass*  renderPass = nullptr;
		GPUFramebuffer* framebuffer = nullptr;
		ClearValues* clearValues = nullptr;
	};

	struct FramebufferDesc
	{
		GPURenderPass*         renderPass;
		Array<GPUTextureView*> attachments;
		String                 debugName;
	};

	struct DescriptorSetOverride
	{
		u32 set;
		GPUDescriptorSet* descriptorSet;
	};

	struct GraphicsPipelineDesc
	{
		RID                           shader;
		String                        variant = "Default";
		PrimitiveTopology             topology{PrimitiveTopology::TriangleList};
		RasterizerStateDesc           rasterizerState;
		DepthStencilStateDesc         depthStencilState;
		Array<BlendStateDesc>         blendStates;
		GPURenderPass*                renderPass{nullptr};
		String                        debugName;
		GPUPipeline*                  previousState = nullptr;
		u32                           vertexInputStride = U32_MAX;
		bool                          allowImmediateSet = false;
		Array<DescriptorSetOverride>  descriptorSetsOverride;
		ConservativeRasterizationMode conservativeRasterizationMode = ConservativeRasterizationMode::Disabled;
	};

	struct ComputePipelineDesc
	{
		RID                          shader;
		String                       variant = "Default";
		bool                         allowImmediateSet = false;
		String                       debugName;
		GPUPipeline*                 previousState = nullptr;
		Array<DescriptorSetOverride> descriptorSetsOverride;
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
		Format vertexFormat{Format::RGB32_FLOAT};

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
		u32                             maxInstances{0};
		BuildAccelerationStructureFlags flags{BuildAccelerationStructureFlags::PreferFastTrace};
		String                          debugName;
	};

	struct QueueDesc
	{
		QueueType type = QueueType::None;
	};

	struct AccelerationStructureBuildInfo
	{
		bool       update{false};
		GPUBuffer* scratchBuffer{nullptr};
		usize      scratchOffset{0};
	};

	struct RayTracingPipelineDesc
	{
		RID                          shader;
		String                       variant = "Default";
		u32                          maxRecursionDepth{1};
		String                       debugName;
		Array<DescriptorSetOverride> descriptorSetsOverride;
	};

	struct SwapchainDesc
	{
		Format desiredFormat{Format::BGRA8_UNORM};
		bool          vsync = true;
		Window				window = {};
		String        debugName;
	};

	struct ViewportInfo
	{
		f32 x{};
		f32 y{};
		f32 width{};
		f32 height{};
		f32 minDepth{};
		f32 maxDepth{1.0};
	};

	struct BufferTextureCopy
	{
		GPUBuffer*  buffer{nullptr};
		GPUTexture* texture{nullptr};
		Extent3D    extent{};
		u32         mipLevel{0};
		u32         arrayLayer{0};
		u64         bufferOffset{0};
		Offset3D    textureOffset{};
	};

	struct TextureCopy
	{
		GPUTexture* srcTexture{nullptr};
		GPUTexture* dstTexture{nullptr};
		Extent3D    extent{};
		u32         srcMipLevel{0};
		u32         srcArrayLayer{0};
		u32         dstMipLevel{0};
		u32         dstArrayLayer{0};
	};

	struct TextureBlit
	{
		GPUTexture* srcTexture{nullptr};
		GPUTexture* dstTexture{nullptr};
		Extent3D    srcExtent{};
		Extent3D    dstExtent{};
		u32         srcMipLevel{0};
		u32         srcArrayLayer{0};
		u32         dstMipLevel{0};
		u32         dstArrayLayer{0};
	};

	struct MemoryHeapBudget
	{
		u64  usage = 0;           // estimated bytes currently used by the program in this heap
		u64  budget = 0;          // estimated bytes available to the program in this heap
		bool deviceLocal = false; // true for dedicated VRAM heaps, false for host/shared heaps
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
		virtual GPUFramebuffer*         CreateFramebuffer(const FramebufferDesc& desc) = 0;
		virtual GPUCommandBuffer*       CreateCommandBuffer(const QueueType& queueType) = 0;
		virtual GPUBuffer*              CreateBuffer(const BufferDesc& desc) = 0;
		virtual GPUTexture*             CreateTexture(const TextureDesc& desc) = 0;
		virtual GPUTextureView*         CreateTextureView(const TextureViewDesc& desc) = 0;

		virtual TextureMemoryRequirements GetTextureMemoryRequirements(const TextureDesc& desc) = 0;
		virtual GPUMemory*              CreateMemory(u64 size, u64 alignment, u32 memoryTypeBits) = 0;
		virtual GPUTexture*             CreateAliasedTexture(const TextureDesc& desc, GPUMemory* memory, u64 offset) = 0;

		virtual GPUSampler*             CreateSampler(const SamplerDesc& desc) = 0;
		virtual GPUPipeline*            CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
		virtual GPUPipeline*            CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
		virtual GPUPipeline*            CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) = 0;
		virtual GPUDescriptorSet*       CreateDescriptorSet(const DescriptorSetDesc& desc) = 0;
		virtual GPUDescriptorSet*       CreateDescriptorSet(RID shader, StringView variant, u32 set) = 0;
		virtual GPUQueryPool*           CreateQueryPool(const QueryPoolDesc& desc) = 0;
		virtual GPUBottomLevelAS*       CreateBottomLevelAS(const BottomLevelASDesc& desc) = 0;
		virtual GPUTopLevelAS*          CreateTopLevelAS(const TopLevelASDesc& desc) = 0;
		virtual GPUQueue*               CreateQueue(const QueueDesc& desc) = 0;

		virtual usize GetBottomLevelASSize(const BottomLevelASDesc& desc) = 0;
		virtual usize GetTopLevelASSize(const TopLevelASDesc& desc) = 0;
		virtual usize GetAccelerationStructureBuildScratchSize(const BottomLevelASDesc& desc) = 0;
		virtual usize GetAccelerationStructureBuildScratchSize(const TopLevelASDesc& desc) = 0;

		virtual void GetMemoryBudgets(Array<MemoryHeapBudget>& outBudgets) = 0;
	};

	class SK_API GPUSwapchain
	{
	public:
		virtual ~GPUSwapchain() = default;

		virtual const SwapchainDesc& GetDesc() const = 0;

		virtual DeviceResult      AcquireNextImage() = 0;
		virtual Extent            GetExtent() = 0;
		virtual void              Destroy() = 0;
		virtual u32               GetImageCount() const = 0;
		virtual Span<GPUTexture*> GetTextures() const = 0;
		virtual u32               GetCurrentImageIndex() const = 0;
		virtual Format     GetFormat() const = 0;
	};

	class SK_API GPUCommandBuffer
	{
	public:
		virtual ~GPUCommandBuffer() = default;

		virtual void Begin() = 0;
		virtual void End() = 0;
		virtual void Reset() = 0;

		virtual void SetViewport(const ViewportInfo& viewportInfo) = 0;
		virtual void SetScissor(Vec2 position, Extent size) = 0;

		virtual void BindPipeline(GPUPipeline* pipeline) = 0;

		virtual void BindDescriptorSet(GPUPipeline* pipeline, u32 setIndex, GPUDescriptorSet* descriptorSet)
		{
			BindDescriptorSet(pipeline, setIndex, descriptorSet, {});
		}

		virtual void BindDescriptorSet(GPUPipeline* pipeline, u32 setIndex, GPUDescriptorSet* descriptorSet, Span<u32> dynamicOffsets) = 0;
		virtual void BindVertexBuffer(u32 firstBinding, GPUBuffer* buffers, usize offset) = 0;
		virtual void BindIndexBuffer(GPUBuffer* buffer, usize offset, IndexType indexType) = 0;

		virtual void PushConstants(GPUPipeline* pipeline, ShaderStage stages, u32 offset, u32 size, const void* data) = 0;

		virtual void SetTexture(GPUPipeline* pipeline, u32 set, u32 binding, GPUTexture* texture, u32 arrayElement) = 0;
		virtual void SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range) = 0;
		virtual void SetBuffer(GPUPipeline* pipeline, u32 set, u32 binding, GPUBuffer* buffer, u64 offset, u64 range, u32 arrayIndex) = 0;
		virtual void SetSampler(GPUPipeline* pipeline, u32 set, u32 binding, GPUSampler* sampler) = 0;
		virtual void SetTextureView(GPUPipeline* pipeline, u32 set, u32 binding, GPUTextureView* textureView, u32 arrayElement) = 0;
		virtual void SetAccelerationStructure(GPUPipeline* pipeline, u32 set, u32 binding, GPUTopLevelAS* topLevelAS) = 0;

		virtual void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) = 0;
		virtual void DrawIndirectCount(GPUBuffer* buffer, u64 offset, GPUBuffer* countBuffer, u64 countBufferOffset, u32 maxDrawCount, u32 stride) = 0;
		virtual void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) = 0;
		virtual void DrawIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride) = 0;
		virtual void DrawIndexedIndirect(GPUBuffer* buffer, usize offset, u32 drawCount, u32 stride) = 0;
		virtual void DrawIndexedIndirectCount(GPUBuffer* buffer, u64 offset, GPUBuffer* countBuffer, u64 countBufferOffset, u32 maxDrawCount, u32 stride) = 0;
		virtual void Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) = 0;
		virtual void DispatchIndirect(GPUBuffer* buffer, usize offset) = 0;

		virtual void TraceRays(GPUPipeline* pipeline, u32 width, u32 height, u32 depth) = 0;

		virtual void BuildBottomLevelAS(GPUBottomLevelAS* bottomLevelAS, const AccelerationStructureBuildInfo& buildInfo) = 0;
		virtual void BuildTopLevelAS(GPUTopLevelAS* topLevelAS, const AccelerationStructureBuildInfo& buildInfo) = 0;
		virtual void CopyAccelerationStructure(GPUBottomLevelAS* src, GPUBottomLevelAS* dst, bool compress) = 0;
		virtual void CopyAccelerationStructure(GPUTopLevelAS* src, GPUTopLevelAS* dst, bool compress) = 0;

		virtual void BeginRenderPass(const BeginRenderPassInfo& info) = 0;
		virtual void EndRenderPass() = 0;

		virtual void CopyBuffer(GPUBuffer* srcBuffer, GPUBuffer* dstBuffer, usize size, usize srcOffset, usize dstOffset) = 0;
		virtual void CopyBufferToTexture(const BufferTextureCopy& copy) = 0;
		virtual void CopyTextureToBuffer(const BufferTextureCopy& copy) = 0;
		virtual void CopyTexture(const TextureCopy& copy) = 0;
		virtual void BlitTexture(const TextureBlit& blit) = 0;
		virtual void FillBuffer(GPUBuffer* buffer, usize offset, usize size, u32 data) = 0;
		virtual void UpdateBuffer(GPUBuffer* buffer, usize offset, usize size, const void* data) = 0;
		virtual void ClearColorTexture(GPUTexture* texture, Vec4 clearValue, u32 mipLevel, u32 arrayLayer) = 0;
		virtual void ClearDepthStencilTexture(GPUTexture* texture, f32 depth, u32 stencil, u32 mipLevel, u32 arrayLayer) = 0;

		virtual void ResourceBarrier(const BufferBarrierDesc& barrier) = 0;
		virtual void ResourceBarrier(const TextureBarrierDesc& barrier) = 0;

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

	class SK_API GPUMemory
	{
	public:
		virtual ~GPUMemory() = default;

		virtual u64  GetSize() const = 0;
		virtual void Destroy() = 0;
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

	class SK_API GPUFramebuffer
	{
	public:
		virtual                        ~GPUFramebuffer() = default;
		virtual const FramebufferDesc& GetDesc() const = 0;
		virtual void                   Destroy() = 0;
		virtual Extent                 GetExtent() = 0;
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
		virtual void UpdateInstance(u32 index, const InstanceDesc& instance) = 0;
		virtual void SetInstanceCount(u32 count) = 0;
		virtual void Destroy() = 0;
	};

	class SK_API GPUQueue
	{
	public:
		virtual ~GPUQueue() = default;
		virtual void Destroy() = 0;

		virtual void Submit(GPUCommandBuffer* cmd) = 0;
		virtual void SubmitAndWait(GPUCommandBuffer* cmd) = 0;
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

	struct DrawIndexedIndirectArguments
	{
		u32 indexCountPerInstance;
		u32 instanceCount;
		u32 startIndexLocation;
		i32 baseVertexLocation;
		u32 startInstanceLocation;
	};

	inline u32 GetFormatSize(Format format)
	{
		switch (format)
		{
			// 8-bit formats
			case Format::R8_UNORM:
			case Format::R8_SNORM:
			case Format::R8_UINT:
			case Format::R8_SINT:
			case Format::R8_SRGB:
				return 1;

			// 16-bit formats
			case Format::R16_UNORM:
			case Format::R16_SNORM:
			case Format::R16_UINT:
			case Format::R16_SINT:
			case Format::R16_FLOAT:
			case Format::RG8_UNORM:
			case Format::RG8_SNORM:
			case Format::RG8_UINT:
			case Format::RG8_SINT:
			case Format::RG8_SRGB:
				return 2;

			// 24-bit formats (3 bytes)
			case Format::RGB16_UNORM:
			case Format::RGB16_SNORM:
			case Format::RGB16_UINT:
			case Format::RGB16_SINT:
			case Format::RGB16_FLOAT:
				return 6; // 3 components, 2 bytes each

			// 32-bit formats (4 bytes)
			case Format::R32_UINT:
			case Format::R32_SINT:
			case Format::R32_FLOAT:
			case Format::RG16_UNORM:
			case Format::RG16_SNORM:
			case Format::RG16_UINT:
			case Format::RG16_SINT:
			case Format::RG16_FLOAT:
			case Format::RGBA8_UNORM:
			case Format::RGBA8_SNORM:
			case Format::RGBA8_UINT:
			case Format::RGBA8_SINT:
			case Format::RGBA8_SRGB:
			case Format::BGRA8_UNORM:
			case Format::BGRA8_SNORM:
			case Format::BGRA8_UINT:
			case Format::BGRA8_SINT:
			case Format::BGRA8_SRGB:
			case Format::RGB10A2_UNORM:
			case Format::RGB10A2_UINT:
			case Format::RG11B10_FLOAT:
			case Format::RGB9E5_FLOAT:
				return 4;

			// 64-bit formats (8 bytes)
			case Format::RG32_UINT:
			case Format::RG32_SINT:
			case Format::RG32_FLOAT:
			case Format::RGBA16_UNORM:
			case Format::RGBA16_SNORM:
			case Format::RGBA16_UINT:
			case Format::RGBA16_SINT:
			case Format::RGBA16_FLOAT:
				return 8;

			// 96-bit formats (12 bytes)
			case Format::RGB32_UINT:
			case Format::RGB32_SINT:
			case Format::RGB32_FLOAT:
				return 12;

			// 128-bit formats (16 bytes)
			case Format::RGBA32_UINT:
			case Format::RGBA32_SINT:
			case Format::RGBA32_FLOAT:
				return 16;

			// Depth/stencil formats
			case Format::D16_UNORM:
				return 2;
			case Format::D24_UNORM_S8_UINT:
				return 4;
			case Format::D32_FLOAT:
				return 4;
			case Format::D32_FLOAT_S8_UINT:
				return 5; // Sometimes implemented as 8 bytes with padding

			// BC compressed formats (block size 4x4 texels)
			// BC1: 64 bits per 4x4 block = 0.5 bytes per texel
			case Format::BC1_UNORM:
			case Format::BC1_SRGB:
				return 8; // 8 bytes per 4x4 block

			// BC2/BC3: 128 bits per 4x4 block = 1 byte per texel
			case Format::BC2_UNORM:
			case Format::BC2_SRGB:
			case Format::BC3_UNORM:
			case Format::BC3_SRGB:
			case Format::BC4_UNORM:
			case Format::BC4_SNORM:
			case Format::BC5_UNORM:
			case Format::BC5_SNORM:
			case Format::BC6H_UF16:
			case Format::BC6H_SF16:
			case Format::BC7_UNORM:
			case Format::BC7_SRGB:
				return 16; // 16 bytes per 4x4 block

			// ETC compressed formats
			case Format::ETC1_UNORM:
			case Format::ETC2_UNORM:
			case Format::ETC2_SRGB:
				return 8; // 8 bytes per 4x4 block
			case Format::ETC2A_UNORM:
			case Format::ETC2A_SRGB:
				return 16; // 16 bytes per 4x4 block

			// ASTC compressed formats (block sizes vary)
			case Format::ASTC_4x4_UNORM:
			case Format::ASTC_4x4_SRGB:
			case Format::ASTC_5x4_UNORM:
			case Format::ASTC_5x4_SRGB:
			case Format::ASTC_5x5_UNORM:
			case Format::ASTC_5x5_SRGB:
			case Format::ASTC_6x5_UNORM:
			case Format::ASTC_6x5_SRGB:
			case Format::ASTC_6x6_UNORM:
			case Format::ASTC_6x6_SRGB:
			case Format::ASTC_8x5_UNORM:
			case Format::ASTC_8x5_SRGB:
			case Format::ASTC_8x6_UNORM:
			case Format::ASTC_8x6_SRGB:
			case Format::ASTC_8x8_UNORM:
			case Format::ASTC_8x8_SRGB:
			case Format::ASTC_10x5_UNORM:
			case Format::ASTC_10x5_SRGB:
			case Format::ASTC_10x6_UNORM:
			case Format::ASTC_10x6_SRGB:
			case Format::ASTC_10x8_UNORM:
			case Format::ASTC_10x8_SRGB:
			case Format::ASTC_10x10_UNORM:
			case Format::ASTC_10x10_SRGB:
			case Format::ASTC_12x10_UNORM:
			case Format::ASTC_12x10_SRGB:
			case Format::ASTC_12x12_UNORM:
			case Format::ASTC_12x12_SRGB:
				return 16; // 16 bytes per block, regardless of block size

			case Format::Unknown:
			default:
				return 0;
		}
	}

	inline i8 GetFormatNumChannels(Format format)
	{
		switch (format)
		{
			// Single channel formats
			case Format::R8_UNORM:
			case Format::R8_SNORM:
			case Format::R8_UINT:
			case Format::R8_SINT:
			case Format::R8_SRGB:
			case Format::R16_UNORM:
			case Format::R16_SNORM:
			case Format::R16_UINT:
			case Format::R16_SINT:
			case Format::R16_FLOAT:
			case Format::R32_UINT:
			case Format::R32_SINT:
			case Format::R32_FLOAT:
				return 1;

			// Two channel formats
			case Format::RG8_UNORM:
			case Format::RG8_SNORM:
			case Format::RG8_UINT:
			case Format::RG8_SINT:
			case Format::RG8_SRGB:
			case Format::RG16_UNORM:
			case Format::RG16_SNORM:
			case Format::RG16_UINT:
			case Format::RG16_SINT:
			case Format::RG16_FLOAT:
			case Format::RG32_UINT:
			case Format::RG32_SINT:
			case Format::RG32_FLOAT:
				return 2;

			// Three channel formats
			case Format::RGB16_UNORM:
			case Format::RGB16_SNORM:
			case Format::RGB16_UINT:
			case Format::RGB16_SINT:
			case Format::RGB16_FLOAT:
			case Format::RGB32_UINT:
			case Format::RGB32_SINT:
			case Format::RGB32_FLOAT:
			case Format::RG11B10_FLOAT:
				return 3;

			// Four channel formats
			case Format::RGBA8_UNORM:
			case Format::RGBA8_SNORM:
			case Format::RGBA8_UINT:
			case Format::RGBA8_SINT:
			case Format::RGBA8_SRGB:
			case Format::BGRA8_UNORM:
			case Format::BGRA8_SNORM:
			case Format::BGRA8_UINT:
			case Format::BGRA8_SINT:
			case Format::BGRA8_SRGB:
			case Format::RGB10A2_UNORM:
			case Format::RGB10A2_UINT:
			case Format::RGBA16_UNORM:
			case Format::RGBA16_SNORM:
			case Format::RGBA16_UINT:
			case Format::RGBA16_SINT:
			case Format::RGBA16_FLOAT:
			case Format::RGBA32_UINT:
			case Format::RGBA32_SINT:
			case Format::RGBA32_FLOAT:
				return 4;

			// Special formats with packed channels
			case Format::RGB9E5_FLOAT:
				return 3;

			// Depth/stencil formats
			case Format::D16_UNORM:
				return 1;
			case Format::D24_UNORM_S8_UINT:
			case Format::D32_FLOAT_S8_UINT:
				return 2; // Depth + stencil
			case Format::D32_FLOAT:
				return 1;

			// Compressed formats
			// BC1, BC2, BC3 typically represent RGBA data
			case Format::BC1_UNORM:
			case Format::BC1_SRGB:
			case Format::BC2_UNORM:
			case Format::BC2_SRGB:
			case Format::BC3_UNORM:
			case Format::BC3_SRGB:
			case Format::BC7_UNORM:
			case Format::BC7_SRGB:
				return 4;

			// BC4 represents a single channel
			case Format::BC4_UNORM:
			case Format::BC4_SNORM:
				return 1;

			// BC5 represents two channels (typically RG)
			case Format::BC5_UNORM:
			case Format::BC5_SNORM:
				return 2;

			// BC6H represents RGB data
			case Format::BC6H_UF16:
			case Format::BC6H_SF16:
				return 3;

			// ETC formats
			case Format::ETC1_UNORM:
			case Format::ETC2_UNORM:
			case Format::ETC2_SRGB:
				return 3; // RGB
			case Format::ETC2A_UNORM:
			case Format::ETC2A_SRGB:
				return 4; // RGBA

			// ASTC formats (all represent RGBA data)
			case Format::ASTC_4x4_UNORM:
			case Format::ASTC_4x4_SRGB:
			case Format::ASTC_5x4_UNORM:
			case Format::ASTC_5x4_SRGB:
			case Format::ASTC_5x5_UNORM:
			case Format::ASTC_5x5_SRGB:
			case Format::ASTC_6x5_UNORM:
			case Format::ASTC_6x5_SRGB:
			case Format::ASTC_6x6_UNORM:
			case Format::ASTC_6x6_SRGB:
			case Format::ASTC_8x5_UNORM:
			case Format::ASTC_8x5_SRGB:
			case Format::ASTC_8x6_UNORM:
			case Format::ASTC_8x6_SRGB:
			case Format::ASTC_8x8_UNORM:
			case Format::ASTC_8x8_SRGB:
			case Format::ASTC_10x5_UNORM:
			case Format::ASTC_10x5_SRGB:
			case Format::ASTC_10x6_UNORM:
			case Format::ASTC_10x6_SRGB:
			case Format::ASTC_10x8_UNORM:
			case Format::ASTC_10x8_SRGB:
			case Format::ASTC_10x10_UNORM:
			case Format::ASTC_10x10_SRGB:
			case Format::ASTC_12x10_UNORM:
			case Format::ASTC_12x10_SRGB:
			case Format::ASTC_12x12_UNORM:
			case Format::ASTC_12x12_SRGB:
				return 4;

			case Format::Unknown:
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

	inline bool IsDepthFormat(Format format)
	{
		return format == Format::D16_UNORM || format == Format::D24_UNORM_S8_UINT || format == Format::D32_FLOAT || format == Format::D32_FLOAT_S8_UINT;
	}

	inline StringView ResourceStateToString(ResourceState state)
	{
		switch (state)
		{
			case ResourceState::Undefined:
				return "Undefined";
			case ResourceState::General:
				return "General";
			case ResourceState::ColorAttachment:
				return "ColorAttachment";
			case ResourceState::DepthStencilAttachment:
				return "DepthStencilAttachment";
			case ResourceState::DepthStencilReadOnly:
				return "DepthStencilReadOnly";
			case ResourceState::ShaderReadOnly:
				return "ShaderReadOnly";
			case ResourceState::CopyDest:
				return "CopyDest";
			case ResourceState::CopySource:
				return "CopySource";
			case ResourceState::Present:
				return "Present";
		}
		return "NotFound";
	}
}
