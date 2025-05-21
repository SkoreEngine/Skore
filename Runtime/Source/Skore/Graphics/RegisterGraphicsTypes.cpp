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


#include "GraphicsAssets.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void RegisterDeviceTypes()
	{
		auto graphicsAPI = Reflection::Type<GraphicsAPI>();
		graphicsAPI.Value<GraphicsAPI::Vulkan>("Vulkan");
		graphicsAPI.Value<GraphicsAPI::D3D12>("D3D12");
		graphicsAPI.Value<GraphicsAPI::Metal>("Metal");
		graphicsAPI.Value<GraphicsAPI::None>("None");

		auto deviceType = Reflection::Type<DeviceType>();
		deviceType.Value<DeviceType::Discrete>("Discrete");
		deviceType.Value<DeviceType::Integrated>("Integrated");
		deviceType.Value<DeviceType::Virtual>("Virtual");
		deviceType.Value<DeviceType::CPU>("CPU");
		deviceType.Value<DeviceType::Other>("Other");

		auto resourceState = Reflection::Type<ResourceState>();
		resourceState.Value<ResourceState::Undefined>("Undefined");
		resourceState.Value<ResourceState::General>("General");
		resourceState.Value<ResourceState::ColorAttachment>("ColorAttachment");
		resourceState.Value<ResourceState::DepthStencilAttachment>("DepthStencilAttachment");
		resourceState.Value<ResourceState::DepthStencilReadOnly>("DepthStencilReadOnly");
		resourceState.Value<ResourceState::ShaderReadOnly>("ShaderReadOnly");
		resourceState.Value<ResourceState::CopyDest>("CopyDest");
		resourceState.Value<ResourceState::CopySource>("CopySource");
		resourceState.Value<ResourceState::Present>("Present");

		auto resourceUsage = Reflection::Type<ResourceUsage>();
		resourceUsage.Value<ResourceUsage::None>("None");
		resourceUsage.Value<ResourceUsage::ShaderResource>("ShaderResource");
		resourceUsage.Value<ResourceUsage::RenderTarget>("RenderTarget");
		resourceUsage.Value<ResourceUsage::DepthStencil>("DepthStencil");
		resourceUsage.Value<ResourceUsage::UnorderedAccess>("UnorderedAccess");
		resourceUsage.Value<ResourceUsage::VertexBuffer>("VertexBuffer");
		resourceUsage.Value<ResourceUsage::IndexBuffer>("IndexBuffer");
		resourceUsage.Value<ResourceUsage::ConstantBuffer>("ConstantBuffer");
		resourceUsage.Value<ResourceUsage::CopyDest>("CopyDest");
		resourceUsage.Value<ResourceUsage::CopySource>("CopySource");
		resourceUsage.Value<ResourceUsage::AccelerationStructure>("AccelerationStructure");
		resourceUsage.Value<ResourceUsage::RayTracing>("RayTracing");

		auto shaderStage = Reflection::Type<ShaderStage>();
		shaderStage.Value<ShaderStage::Vertex>("Vertex");
		shaderStage.Value<ShaderStage::Hull>("Hull");
		shaderStage.Value<ShaderStage::Domain>("Domain");
		shaderStage.Value<ShaderStage::Geometry>("Geometry");
		shaderStage.Value<ShaderStage::Pixel>("Pixel");
		shaderStage.Value<ShaderStage::Compute>("Compute");
		shaderStage.Value<ShaderStage::Amplification>("Amplification");
		shaderStage.Value<ShaderStage::Mesh>("Mesh");
		shaderStage.Value<ShaderStage::RayGen>("RayGen");
		shaderStage.Value<ShaderStage::AnyHit>("AnyHit");
		shaderStage.Value<ShaderStage::ClosestHit>("ClosestHit");
		shaderStage.Value<ShaderStage::Miss>("Miss");
		shaderStage.Value<ShaderStage::Intersection>("Intersection");
		shaderStage.Value<ShaderStage::Callable>("Callable");
		shaderStage.Value<ShaderStage::All>("All");

		auto indexType = Reflection::Type<IndexType>();
		indexType.Value<IndexType::Uint16>("Uint16");
		indexType.Value<IndexType::Uint32>("Uint32");

		auto primitiveTopology = Reflection::Type<PrimitiveTopology>();
		primitiveTopology.Value<PrimitiveTopology::PointList>("PointList");
		primitiveTopology.Value<PrimitiveTopology::LineList>("LineList");
		primitiveTopology.Value<PrimitiveTopology::LineStrip>("LineStrip");
		primitiveTopology.Value<PrimitiveTopology::TriangleList>("TriangleList");
		primitiveTopology.Value<PrimitiveTopology::TriangleStrip>("TriangleStrip");

		auto filterMode = Reflection::Type<FilterMode>();
		filterMode.Value<FilterMode::Nearest>("Nearest");
		filterMode.Value<FilterMode::Linear>("Linear");

		auto addressMode = Reflection::Type<AddressMode>();
		addressMode.Value<AddressMode::Repeat>("Repeat");
		addressMode.Value<AddressMode::MirroredRepeat>("MirroredRepeat");
		addressMode.Value<AddressMode::ClampToEdge>("ClampToEdge");
		addressMode.Value<AddressMode::ClampToBorder>("ClampToBorder");
		addressMode.Value<AddressMode::MirrorClampToEdge>("MirrorClampToEdge");

		auto compareOp = Reflection::Type<CompareOp>();
		compareOp.Value<CompareOp::Never>("Never");
		compareOp.Value<CompareOp::Less>("Less");
		compareOp.Value<CompareOp::Equal>("Equal");
		compareOp.Value<CompareOp::LessEqual>("LessEqual");
		compareOp.Value<CompareOp::Greater>("Greater");
		compareOp.Value<CompareOp::NotEqual>("NotEqual");
		compareOp.Value<CompareOp::GreaterEqual>("GreaterEqual");
		compareOp.Value<CompareOp::Always>("Always");

		auto borderColor = Reflection::Type<BorderColor>();
		borderColor.Value<BorderColor::TransparentBlack>("TransparentBlack");
		borderColor.Value<BorderColor::OpaqueBlack>("OpaqueBlack");
		borderColor.Value<BorderColor::OpaqueWhite>("OpaqueWhite");

		auto blendFactor = Reflection::Type<BlendFactor>();
		blendFactor.Value<BlendFactor::Zero>("Zero");
		blendFactor.Value<BlendFactor::One>("One");
		blendFactor.Value<BlendFactor::SrcColor>("SrcColor");
		blendFactor.Value<BlendFactor::OneMinusSrcColor>("OneMinusSrcColor");
		blendFactor.Value<BlendFactor::DstColor>("DstColor");
		blendFactor.Value<BlendFactor::OneMinusDstColor>("OneMinusDstColor");
		blendFactor.Value<BlendFactor::SrcAlpha>("SrcAlpha");
		blendFactor.Value<BlendFactor::OneMinusSrcAlpha>("OneMinusSrcAlpha");
		blendFactor.Value<BlendFactor::DstAlpha>("DstAlpha");
		blendFactor.Value<BlendFactor::OneMinusDstAlpha>("OneMinusDstAlpha");
		blendFactor.Value<BlendFactor::ConstantColor>("ConstantColor");
		blendFactor.Value<BlendFactor::OneMinusConstantColor>("OneMinusConstantColor");
		blendFactor.Value<BlendFactor::ConstantAlpha>("ConstantAlpha");
		blendFactor.Value<BlendFactor::OneMinusConstantAlpha>("OneMinusConstantAlpha");
		blendFactor.Value<BlendFactor::SrcAlphaSaturate>("SrcAlphaSaturate");

		auto blendOp = Reflection::Type<BlendOp>();
		blendOp.Value<BlendOp::Add>("Add");
		blendOp.Value<BlendOp::Subtract>("Subtract");
		blendOp.Value<BlendOp::ReverseSubtract>("ReverseSubtract");
		blendOp.Value<BlendOp::Min>("Min");
		blendOp.Value<BlendOp::Max>("Max");

		auto colorMask = Reflection::Type<ColorMask>();
		colorMask.Value<ColorMask::Red>("Red");
		colorMask.Value<ColorMask::Green>("Green");
		colorMask.Value<ColorMask::Blue>("Blue");
		colorMask.Value<ColorMask::Alpha>("Alpha");
		colorMask.Value<ColorMask::All>("All");

		auto cullMode = Reflection::Type<CullMode>();
		cullMode.Value<CullMode::None>("None");
		cullMode.Value<CullMode::Front>("Front");
		cullMode.Value<CullMode::Back>("Back");

		auto frontFace = Reflection::Type<FrontFace>();
		frontFace.Value<FrontFace::Clockwise>("Clockwise");
		frontFace.Value<FrontFace::CounterClockwise>("CounterClockwise");

		auto stencilOp = Reflection::Type<StencilOp>();
		stencilOp.Value<StencilOp::Keep>("Keep");
		stencilOp.Value<StencilOp::Zero>("Zero");
		stencilOp.Value<StencilOp::Replace>("Replace");
		stencilOp.Value<StencilOp::IncrementClamp>("IncrementClamp");
		stencilOp.Value<StencilOp::DecrementClamp>("DecrementClamp");
		stencilOp.Value<StencilOp::Invert>("Invert");
		stencilOp.Value<StencilOp::IncrementWrap>("IncrementWrap");
		stencilOp.Value<StencilOp::DecrementWrap>("DecrementWrap");

		auto polygonMode = Reflection::Type<PolygonMode>();
		polygonMode.Value<PolygonMode::Fill>("Fill");
		polygonMode.Value<PolygonMode::Line>("Line");
		polygonMode.Value<PolygonMode::Point>("Point");

		auto queryType = Reflection::Type<QueryType>();
		queryType.Value<QueryType::Occlusion>("Occlusion");
		queryType.Value<QueryType::PipelineStatistics>("PipelineStatistics");
		queryType.Value<QueryType::Timestamp>("Timestamp");

		auto attachmentLoadOp = Reflection::Type<AttachmentLoadOp>();
		attachmentLoadOp.Value<AttachmentLoadOp::Load>("Load");
		attachmentLoadOp.Value<AttachmentLoadOp::Clear>("Clear");
		attachmentLoadOp.Value<AttachmentLoadOp::DontCare>("DontCare");

		auto attachmentStoreOp = Reflection::Type<AttachmentStoreOp>();
		attachmentStoreOp.Value<AttachmentStoreOp::Store>("Store");
		attachmentStoreOp.Value<AttachmentStoreOp::DontCare>("DontCare");

		auto pipelineBindPoint = Reflection::Type<PipelineBindPoint>();
		pipelineBindPoint.Value<PipelineBindPoint::Graphics>("Graphics");
		pipelineBindPoint.Value<PipelineBindPoint::Compute>("Compute");
		pipelineBindPoint.Value<PipelineBindPoint::RayTracing>("RayTracing");

		auto geometryType = Reflection::Type<GeometryType>();
		geometryType.Value<GeometryType::Triangles>("Triangles");
		geometryType.Value<GeometryType::AABBs>("AABBs");

		auto buildAccelerationStructureFlags = Reflection::Type<BuildAccelerationStructureFlags>();
		buildAccelerationStructureFlags.Value<BuildAccelerationStructureFlags::None>("None");
		buildAccelerationStructureFlags.Value<BuildAccelerationStructureFlags::AllowUpdate>("AllowUpdate");
		buildAccelerationStructureFlags.Value<BuildAccelerationStructureFlags::AllowCompaction>("AllowCompaction");
		buildAccelerationStructureFlags.Value<BuildAccelerationStructureFlags::PreferFastTrace>("PreferFastTrace");
		buildAccelerationStructureFlags.Value<BuildAccelerationStructureFlags::PreferFastBuild>("PreferFastBuild");
		buildAccelerationStructureFlags.Value<BuildAccelerationStructureFlags::MinimizeMemory>("MinimizeMemory");
		buildAccelerationStructureFlags.Value<BuildAccelerationStructureFlags::PerformUpdate>("PerformUpdate");

		auto renderType = Reflection::Type<RenderType>();
		renderType.Value<RenderType::None>("None");
		renderType.Value<RenderType::Void>("Void");
		renderType.Value<RenderType::Bool>("Bool");
		renderType.Value<RenderType::Int>("Int");
		renderType.Value<RenderType::Float>("Float");
		renderType.Value<RenderType::Vector>("Vector");
		renderType.Value<RenderType::Matrix>("Matrix");
		renderType.Value<RenderType::Image>("Image");
		renderType.Value<RenderType::Sampler>("Sampler");
		renderType.Value<RenderType::SampledImage>("SampledImage");
		renderType.Value<RenderType::Array>("Array");
		renderType.Value<RenderType::RuntimeArray>("RuntimeArray");
		renderType.Value<RenderType::Struct>("Struct");

		auto viewType = Reflection::Type<TextureViewType>();
		viewType.Value<TextureViewType::Type1D>("Type1D");
		viewType.Value<TextureViewType::Type2D>("Type2D");
		viewType.Value<TextureViewType::Type3D>("Type3D");
		viewType.Value<TextureViewType::TypeCube>("TypeCube");
		viewType.Value<TextureViewType::Type1DArray>("Type1DArray");
		viewType.Value<TextureViewType::Type2DArray>("Type2DArray");
		viewType.Value<TextureViewType::TypeCubeArray>("TypeCubeArray");
		viewType.Value<TextureViewType::Undefined>("Undefined");

		auto textureFormat = Reflection::Type<TextureFormat>();
		textureFormat.Value<TextureFormat::Unknown>("Unknown");
		textureFormat.Value<TextureFormat::R8_UNORM>("R8_UNORM");
		textureFormat.Value<TextureFormat::R8_SNORM>("R8_SNORM");
		textureFormat.Value<TextureFormat::R8_UINT>("R8_UINT");
		textureFormat.Value<TextureFormat::R8_SINT>("R8_SINT");
		textureFormat.Value<TextureFormat::R8_SRGB>("R8_SRGB");
		textureFormat.Value<TextureFormat::R16_UNORM>("R16_UNORM");
		textureFormat.Value<TextureFormat::R16_SNORM>("R16_SNORM");
		textureFormat.Value<TextureFormat::R16_UINT>("R16_UINT");
		textureFormat.Value<TextureFormat::R16_SINT>("R16_SINT");
		textureFormat.Value<TextureFormat::R16_FLOAT>("R16_FLOAT");
		textureFormat.Value<TextureFormat::R8G8_UNORM>("R8G8_UNORM");
		textureFormat.Value<TextureFormat::R8G8_SNORM>("R8G8_SNORM");
		textureFormat.Value<TextureFormat::R8G8_UINT>("R8G8_UINT");
		textureFormat.Value<TextureFormat::R8G8_SINT>("R8G8_SINT");
		textureFormat.Value<TextureFormat::R8G8_SRGB>("R8G8_SRGB");
		textureFormat.Value<TextureFormat::R16G16B16_UNORM>("R16G16B16_UNORM");
		textureFormat.Value<TextureFormat::R16G16B16_SNORM>("R16G16B16_SNORM");
		textureFormat.Value<TextureFormat::R16G16B16_UINT>("R16G16B16_UINT");
		textureFormat.Value<TextureFormat::R16G16B16_SINT>("R16G16B16_SINT");
		textureFormat.Value<TextureFormat::R16G16B16_FLOAT>("R16G16B16_FLOAT");
		textureFormat.Value<TextureFormat::R32_UINT>("R32_UINT");
		textureFormat.Value<TextureFormat::R32_SINT>("R32_SINT");
		textureFormat.Value<TextureFormat::R32_FLOAT>("R32_FLOAT");
		textureFormat.Value<TextureFormat::R16G16_UNORM>("R16G16_UNORM");
		textureFormat.Value<TextureFormat::R16G16_SNORM>("R16G16_SNORM");
		textureFormat.Value<TextureFormat::R16G16_UINT>("R16G16_UINT");
		textureFormat.Value<TextureFormat::R16G16_SINT>("R16G16_SINT");
		textureFormat.Value<TextureFormat::R16G16_FLOAT>("R16G16_FLOAT");
		textureFormat.Value<TextureFormat::R8G8B8A8_UNORM>("R8G8B8A8_UNORM");
		textureFormat.Value<TextureFormat::R8G8B8A8_SNORM>("R8G8B8A8_SNORM");
		textureFormat.Value<TextureFormat::R8G8B8A8_UINT>("R8G8B8A8_UINT");
		textureFormat.Value<TextureFormat::R8G8B8A8_SINT>("R8G8B8A8_SINT");
		textureFormat.Value<TextureFormat::R8G8B8A8_SRGB>("R8G8B8A8_SRGB");
		textureFormat.Value<TextureFormat::B8G8R8A8_UNORM>("B8G8R8A8_UNORM");
		textureFormat.Value<TextureFormat::B8G8R8A8_SNORM>("B8G8R8A8_SNORM");
		textureFormat.Value<TextureFormat::B8G8R8A8_UINT>("B8G8R8A8_UINT");
		textureFormat.Value<TextureFormat::B8G8R8A8_SINT>("B8G8R8A8_SINT");
		textureFormat.Value<TextureFormat::B8G8R8A8_SRGB>("B8G8R8A8_SRGB");
		textureFormat.Value<TextureFormat::R10G10B10A2_UNORM>("R10G10B10A2_UNORM");
		textureFormat.Value<TextureFormat::R10G10B10A2_UINT>("R10G10B10A2_UINT");
		textureFormat.Value<TextureFormat::R11G11B10_FLOAT>("R11G11B10_FLOAT");
		textureFormat.Value<TextureFormat::R9G9B9E5_FLOAT>("R9G9B9E5_FLOAT");
		textureFormat.Value<TextureFormat::R32G32_UINT>("R32G32_UINT");
		textureFormat.Value<TextureFormat::R32G32_SINT>("R32G32_SINT");
		textureFormat.Value<TextureFormat::R32G32_FLOAT>("R32G32_FLOAT");
		textureFormat.Value<TextureFormat::R16G16B16A16_UNORM>("R16G16B16A16_UNORM");
		textureFormat.Value<TextureFormat::R16G16B16A16_SNORM>("R16G16B16A16_SNORM");
		textureFormat.Value<TextureFormat::R16G16B16A16_UINT>("R16G16B16A16_UINT");
		textureFormat.Value<TextureFormat::R16G16B16A16_SINT>("R16G16B16A16_SINT");
		textureFormat.Value<TextureFormat::R16G16B16A16_FLOAT>("R16G16B16A16_FLOAT");
		textureFormat.Value<TextureFormat::R32G32B32_UINT>("R32G32B32_UINT");
		textureFormat.Value<TextureFormat::R32G32B32_SINT>("R32G32B32_SINT");
		textureFormat.Value<TextureFormat::R32G32B32_FLOAT>("R32G32B32_FLOAT");
		textureFormat.Value<TextureFormat::R32G32B32A32_UINT>("R32G32B32A32_UINT");
		textureFormat.Value<TextureFormat::R32G32B32A32_SINT>("R32G32B32A32_SINT");
		textureFormat.Value<TextureFormat::R32G32B32A32_FLOAT>("R32G32B32A32_FLOAT");
		textureFormat.Value<TextureFormat::D16_UNORM>("D16_UNORM");
		textureFormat.Value<TextureFormat::D24_UNORM_S8_UINT>("D24_UNORM_S8_UINT");
		textureFormat.Value<TextureFormat::D32_FLOAT>("D32_FLOAT");
		textureFormat.Value<TextureFormat::D32_FLOAT_S8_UINT>("D32_FLOAT_S8_UINT");
		textureFormat.Value<TextureFormat::BC1_UNORM>("BC1_UNORM");
		textureFormat.Value<TextureFormat::BC1_SRGB>("BC1_SRGB");
		textureFormat.Value<TextureFormat::BC2_UNORM>("BC2_UNORM");
		textureFormat.Value<TextureFormat::BC2_SRGB>("BC2_SRGB");
		textureFormat.Value<TextureFormat::BC3_UNORM>("BC3_UNORM");
		textureFormat.Value<TextureFormat::BC3_SRGB>("BC3_SRGB");
		textureFormat.Value<TextureFormat::BC4_UNORM>("BC4_UNORM");
		textureFormat.Value<TextureFormat::BC4_SNORM>("BC4_SNORM");
		textureFormat.Value<TextureFormat::BC5_UNORM>("BC5_UNORM");
		textureFormat.Value<TextureFormat::BC5_SNORM>("BC5_SNORM");
		textureFormat.Value<TextureFormat::BC6H_UF16>("BC6H_UF16");
		textureFormat.Value<TextureFormat::BC6H_SF16>("BC6H_SF16");
		textureFormat.Value<TextureFormat::BC7_UNORM>("BC7_UNORM");
		textureFormat.Value<TextureFormat::BC7_SRGB>("BC7_SRGB");
		textureFormat.Value<TextureFormat::ETC1_UNORM>("ETC1_UNORM");
		textureFormat.Value<TextureFormat::ETC2_UNORM>("ETC2_UNORM");
		textureFormat.Value<TextureFormat::ETC2_SRGB>("ETC2_SRGB");
		textureFormat.Value<TextureFormat::ETC2A_UNORM>("ETC2A_UNORM");
		textureFormat.Value<TextureFormat::ETC2A_SRGB>("ETC2A_SRGB");
		textureFormat.Value<TextureFormat::ASTC_4x4_UNORM>("ASTC_4x4_UNORM");
		textureFormat.Value<TextureFormat::ASTC_4x4_SRGB>("ASTC_4x4_SRGB");
		textureFormat.Value<TextureFormat::ASTC_5x4_UNORM>("ASTC_5x4_UNORM");
		textureFormat.Value<TextureFormat::ASTC_5x4_SRGB>("ASTC_5x4_SRGB");
		textureFormat.Value<TextureFormat::ASTC_5x5_UNORM>("ASTC_5x5_UNORM");
		textureFormat.Value<TextureFormat::ASTC_5x5_SRGB>("ASTC_5x5_SRGB");
		textureFormat.Value<TextureFormat::ASTC_6x5_UNORM>("ASTC_6x5_UNORM");
		textureFormat.Value<TextureFormat::ASTC_6x5_SRGB>("ASTC_6x5_SRGB");
		textureFormat.Value<TextureFormat::ASTC_6x6_UNORM>("ASTC_6x6_UNORM");
		textureFormat.Value<TextureFormat::ASTC_6x6_SRGB>("ASTC_6x6_SRGB");
		textureFormat.Value<TextureFormat::ASTC_8x5_UNORM>("ASTC_8x5_UNORM");
		textureFormat.Value<TextureFormat::ASTC_8x5_SRGB>("ASTC_8x5_SRGB");
		textureFormat.Value<TextureFormat::ASTC_8x6_UNORM>("ASTC_8x6_UNORM");
		textureFormat.Value<TextureFormat::ASTC_8x6_SRGB>("ASTC_8x6_SRGB");
		textureFormat.Value<TextureFormat::ASTC_8x8_UNORM>("ASTC_8x8_UNORM");
		textureFormat.Value<TextureFormat::ASTC_8x8_SRGB>("ASTC_8x8_SRGB");
		textureFormat.Value<TextureFormat::ASTC_10x5_UNORM>("ASTC_10x5_UNORM");
		textureFormat.Value<TextureFormat::ASTC_10x5_SRGB>("ASTC_10x5_SRGB");
		textureFormat.Value<TextureFormat::ASTC_10x6_UNORM>("ASTC_10x6_UNORM");
		textureFormat.Value<TextureFormat::ASTC_10x6_SRGB>("ASTC_10x6_SRGB");
		textureFormat.Value<TextureFormat::ASTC_10x8_UNORM>("ASTC_10x8_UNORM");
		textureFormat.Value<TextureFormat::ASTC_10x8_SRGB>("ASTC_10x8_SRGB");
		textureFormat.Value<TextureFormat::ASTC_10x10_UNORM>("ASTC_10x10_UNORM");
		textureFormat.Value<TextureFormat::ASTC_10x10_SRGB>("ASTC_10x10_SRGB");
		textureFormat.Value<TextureFormat::ASTC_12x10_UNORM>("ASTC_12x10_UNORM");
		textureFormat.Value<TextureFormat::ASTC_12x10_SRGB>("ASTC_12x10_SRGB");
		textureFormat.Value<TextureFormat::ASTC_12x12_UNORM>("ASTC_12x12_UNORM");
		textureFormat.Value<TextureFormat::ASTC_12x12_SRGB>("ASTC_12x12_SRGB");

		// Register struct types
		auto deviceInitDesc = Reflection::Type<DeviceInitDesc>();
		deviceInitDesc.Field<&DeviceInitDesc::enableDebugLayers>("enableDebugLayers");

		auto deviceFeatures = Reflection::Type<DeviceFeatures>();
		deviceFeatures.Field<&DeviceFeatures::tessellationShader>("tessellationShader");
		deviceFeatures.Field<&DeviceFeatures::geometryShader>("geometryShader");
		deviceFeatures.Field<&DeviceFeatures::computeShader>("computeShader");
		deviceFeatures.Field<&DeviceFeatures::multiViewport>("multiViewport");
		deviceFeatures.Field<&DeviceFeatures::textureCompressionBC>("textureCompressionBC");
		deviceFeatures.Field<&DeviceFeatures::textureCompressionETC2>("textureCompressionETC2");
		deviceFeatures.Field<&DeviceFeatures::textureCompressionASTC>("textureCompressionASTC");
		deviceFeatures.Field<&DeviceFeatures::independentBlend>("independentBlend");
		deviceFeatures.Field<&DeviceFeatures::bindlessSupported>("bindlessSupported");
		deviceFeatures.Field<&DeviceFeatures::bufferDeviceAddress>("bufferDeviceAddress");
		deviceFeatures.Field<&DeviceFeatures::drawIndirectCount>("drawIndirectCount");
		deviceFeatures.Field<&DeviceFeatures::rayTracing>("rayTracing");

		auto deviceLimits = Reflection::Type<DeviceLimits>();
		deviceLimits.Field<&DeviceLimits::maxTextureSize>("maxTextureSize");
		deviceLimits.Field<&DeviceLimits::maxTexture3DSize>("maxTexture3DSize");
		deviceLimits.Field<&DeviceLimits::maxCubeMapSize>("maxCubeMapSize");
		// deviceLimits.Field<&DeviceLimits::maxViewportDimensions>("maxViewportDimensions");
		// deviceLimits.Field<&DeviceLimits::maxComputeWorkGroupCount>("maxComputeWorkGroupCount");
		// deviceLimits.Field<&DeviceLimits::maxComputeWorkGroupSize>("maxComputeWorkGroupSize");
		deviceLimits.Field<&DeviceLimits::maxComputeInvocations>("maxComputeInvocations");
		deviceLimits.Field<&DeviceLimits::maxVertexInputBindings>("maxVertexInputBindings");
		deviceLimits.Field<&DeviceLimits::maxVertexInputAttributes>("maxVertexInputAttributes");

		auto deviceProperties = Reflection::Type<DeviceProperties>();
		deviceProperties.Field<&DeviceProperties::deviceType>("deviceType");
		deviceProperties.Field<&DeviceProperties::deviceName>("deviceName");
		deviceProperties.Field<&DeviceProperties::vendorName>("vendorName");
		deviceProperties.Field<&DeviceProperties::driverVersion>("driverVersion");
		deviceProperties.Field<&DeviceProperties::features>("features");
		deviceProperties.Field<&DeviceProperties::limits>("limits");

		auto bufferDesc = Reflection::Type<BufferDesc>();
		bufferDesc.Field<&BufferDesc::size>("size");
		bufferDesc.Field<&BufferDesc::usage>("usage");
		bufferDesc.Field<&BufferDesc::hostVisible>("hostVisible");
		bufferDesc.Field<&BufferDesc::persistentMapped>("persistentMapped");
		bufferDesc.Field<&BufferDesc::debugName>("debugName");

		auto textureDesc = Reflection::Type<TextureDesc>();
		textureDesc.Field<&TextureDesc::extent>("extent");
		textureDesc.Field<&TextureDesc::mipLevels>("mipLevels");
		textureDesc.Field<&TextureDesc::arrayLayers>("arrayLayers");
		textureDesc.Field<&TextureDesc::format>("format");
		textureDesc.Field<&TextureDesc::usage>("usage");
		textureDesc.Field<&TextureDesc::cubemap>("cubemap");
		textureDesc.Field<&TextureDesc::debugName>("debugName");

		auto textureViewDesc = Reflection::Type<TextureViewDesc>();
		textureViewDesc.Field<&TextureViewDesc::texture>("texture");
		textureViewDesc.Field<&TextureViewDesc::type>("type");
		textureViewDesc.Field<&TextureViewDesc::baseMipLevel>("baseMipLevel");
		textureViewDesc.Field<&TextureViewDesc::mipLevelCount>("mipLevelCount");
		textureViewDesc.Field<&TextureViewDesc::baseArrayLayer>("baseArrayLayer");
		textureViewDesc.Field<&TextureViewDesc::arrayLayerCount>("arrayLayerCount");
		textureViewDesc.Field<&TextureViewDesc::debugName>("debugName");

		auto samplerDesc = Reflection::Type<SamplerDesc>();
		samplerDesc.Field<&SamplerDesc::minFilter>("minFilter");
		samplerDesc.Field<&SamplerDesc::magFilter>("magFilter");
		samplerDesc.Field<&SamplerDesc::mipmapFilter>("mipmapFilter");
		samplerDesc.Field<&SamplerDesc::addressModeU>("addressModeU");
		samplerDesc.Field<&SamplerDesc::addressModeV>("addressModeV");
		samplerDesc.Field<&SamplerDesc::addressModeW>("addressModeW");
		samplerDesc.Field<&SamplerDesc::mipLodBias>("mipLodBias");
		samplerDesc.Field<&SamplerDesc::anisotropyEnable>("anisotropyEnable");
		samplerDesc.Field<&SamplerDesc::maxAnisotropy>("maxAnisotropy");
		samplerDesc.Field<&SamplerDesc::compareEnable>("compareEnable");
		samplerDesc.Field<&SamplerDesc::compareOp>("compareOp");
		samplerDesc.Field<&SamplerDesc::minLod>("minLod");
		samplerDesc.Field<&SamplerDesc::maxLod>("maxLod");
		samplerDesc.Field<&SamplerDesc::borderColor>("borderColor");
		samplerDesc.Field<&SamplerDesc::debugName>("debugName");

		auto blendStateDesc = Reflection::Type<BlendStateDesc>();
		blendStateDesc.Field<&BlendStateDesc::blendEnable>("blendEnable");
		blendStateDesc.Field<&BlendStateDesc::srcColorBlendFactor>("srcColorBlendFactor");
		blendStateDesc.Field<&BlendStateDesc::dstColorBlendFactor>("dstColorBlendFactor");
		blendStateDesc.Field<&BlendStateDesc::colorBlendOp>("colorBlendOp");
		blendStateDesc.Field<&BlendStateDesc::srcAlphaBlendFactor>("srcAlphaBlendFactor");
		blendStateDesc.Field<&BlendStateDesc::dstAlphaBlendFactor>("dstAlphaBlendFactor");
		blendStateDesc.Field<&BlendStateDesc::alphaBlendOp>("alphaBlendOp");
		blendStateDesc.Field<&BlendStateDesc::colorWriteMask>("colorWriteMask");

		auto rasterizerStateDesc = Reflection::Type<RasterizerStateDesc>();
		rasterizerStateDesc.Field<&RasterizerStateDesc::polygonMode>("polygonMode");
		rasterizerStateDesc.Field<&RasterizerStateDesc::cullMode>("cullMode");
		rasterizerStateDesc.Field<&RasterizerStateDesc::frontFace>("frontFace");
		rasterizerStateDesc.Field<&RasterizerStateDesc::depthClampEnable>("depthClampEnable");
		rasterizerStateDesc.Field<&RasterizerStateDesc::rasterizerDiscardEnable>("rasterizerDiscardEnable");
		rasterizerStateDesc.Field<&RasterizerStateDesc::depthBiasEnable>("depthBiasEnable");
		rasterizerStateDesc.Field<&RasterizerStateDesc::depthBiasConstantFactor>("depthBiasConstantFactor");
		rasterizerStateDesc.Field<&RasterizerStateDesc::depthBiasClamp>("depthBiasClamp");
		rasterizerStateDesc.Field<&RasterizerStateDesc::depthBiasSlopeFactor>("depthBiasSlopeFactor");
		rasterizerStateDesc.Field<&RasterizerStateDesc::lineWidth>("lineWidth");

		auto stencilOpStateDesc = Reflection::Type<StencilOpStateDesc>();
		stencilOpStateDesc.Field<&StencilOpStateDesc::failOp>("failOp");
		stencilOpStateDesc.Field<&StencilOpStateDesc::passOp>("passOp");
		stencilOpStateDesc.Field<&StencilOpStateDesc::depthFailOp>("depthFailOp");
		stencilOpStateDesc.Field<&StencilOpStateDesc::compareOp>("compareOp");
		stencilOpStateDesc.Field<&StencilOpStateDesc::compareMask>("compareMask");
		stencilOpStateDesc.Field<&StencilOpStateDesc::writeMask>("writeMask");
		stencilOpStateDesc.Field<&StencilOpStateDesc::reference>("reference");

		auto depthStencilStateDesc = Reflection::Type<DepthStencilStateDesc>();
		depthStencilStateDesc.Field<&DepthStencilStateDesc::depthTestEnable>("depthTestEnable");
		depthStencilStateDesc.Field<&DepthStencilStateDesc::depthWriteEnable>("depthWriteEnable");
		depthStencilStateDesc.Field<&DepthStencilStateDesc::depthCompareOp>("depthCompareOp");
		depthStencilStateDesc.Field<&DepthStencilStateDesc::depthBoundsTestEnable>("depthBoundsTestEnable");
		depthStencilStateDesc.Field<&DepthStencilStateDesc::stencilTestEnable>("stencilTestEnable");
		depthStencilStateDesc.Field<&DepthStencilStateDesc::front>("front");
		depthStencilStateDesc.Field<&DepthStencilStateDesc::back>("back");
		depthStencilStateDesc.Field<&DepthStencilStateDesc::minDepthBounds>("minDepthBounds");
		depthStencilStateDesc.Field<&DepthStencilStateDesc::maxDepthBounds>("maxDepthBounds");

		auto pushConstantRange = Reflection::Type<PushConstantRange>();
		pushConstantRange.Field<&PushConstantRange::name>("name");
		pushConstantRange.Field<&PushConstantRange::offset>("offset");
		pushConstantRange.Field<&PushConstantRange::size>("size");
		pushConstantRange.Field<&PushConstantRange::stages>("stages");

		auto descriptorType = Reflection::Type<DescriptorType>();
		descriptorType.Value<DescriptorType::Sampler>("Sampler");
		descriptorType.Value<DescriptorType::SampledImage>("SampledImage");
		descriptorType.Value<DescriptorType::StorageImage>("StorageImage");
		descriptorType.Value<DescriptorType::UniformTexelBuffer>("UniformTexelBuffer");
		descriptorType.Value<DescriptorType::StorageTexelBuffer>("StorageTexelBuffer");
		descriptorType.Value<DescriptorType::UniformBuffer>("UniformBuffer");
		descriptorType.Value<DescriptorType::StorageBuffer>("StorageBuffer");
		descriptorType.Value<DescriptorType::UniformBufferDynamic>("UniformBufferDynamic");
		descriptorType.Value<DescriptorType::StorageBufferDynamic>("StorageBufferDynamic");
		descriptorType.Value<DescriptorType::InputAttachment>("InputAttachment");
		descriptorType.Value<DescriptorType::AccelerationStructure>("AccelerationStructure");

		auto descriptorSetLayoutBinding = Reflection::Type<DescriptorSetLayoutBinding>();
		descriptorSetLayoutBinding.Field<&DescriptorSetLayoutBinding::binding>("binding");
		descriptorSetLayoutBinding.Field<&DescriptorSetLayoutBinding::count>("count");
		descriptorSetLayoutBinding.Field<&DescriptorSetLayoutBinding::name>("name");
		descriptorSetLayoutBinding.Field<&DescriptorSetLayoutBinding::descriptorType>("descriptorType");
		descriptorSetLayoutBinding.Field<&DescriptorSetLayoutBinding::renderType>("renderType");
		descriptorSetLayoutBinding.Field<&DescriptorSetLayoutBinding::shaderStage>("shaderStage");
		descriptorSetLayoutBinding.Field<&DescriptorSetLayoutBinding::viewType>("viewType");
		descriptorSetLayoutBinding.Field<&DescriptorSetLayoutBinding::size>("size");

		auto interfaceVariable = Reflection::Type<InterfaceVariable>();
		interfaceVariable.Field<&InterfaceVariable::location>("location");
		interfaceVariable.Field<&InterfaceVariable::offset>("offset");
		interfaceVariable.Field<&InterfaceVariable::name>("name");
		interfaceVariable.Field<&InterfaceVariable::format>("format");
		interfaceVariable.Field<&InterfaceVariable::size>("size");

		auto descriptorSetLayout = Reflection::Type<DescriptorSetLayout>();
		descriptorSetLayout.Field<&DescriptorSetLayout::set>("set");
		descriptorSetLayout.Field<&DescriptorSetLayout::bindings>("bindings");
		descriptorSetLayout.Field<&DescriptorSetLayout::debugName>("debugName");

		auto pipelineDesc = Reflection::Type<PipelineDesc>();
		pipelineDesc.Field<&PipelineDesc::inputVariables>("inputVariables");
		pipelineDesc.Field<&PipelineDesc::outputVariables>("outputVariables");
		pipelineDesc.Field<&PipelineDesc::descriptors>("descriptors");
		pipelineDesc.Field<&PipelineDesc::pushConstants>("pushConstants");
		pipelineDesc.Field<&PipelineDesc::stride>("stride");

		auto attachmentDesc = Reflection::Type<AttachmentDesc>();
		attachmentDesc.Field<&AttachmentDesc::texture>("texture");
		attachmentDesc.Field<&AttachmentDesc::textureView>("textureView");
		attachmentDesc.Field<&AttachmentDesc::initialState>("initialState");
		attachmentDesc.Field<&AttachmentDesc::finalState>("finalState");
		attachmentDesc.Field<&AttachmentDesc::loadOp>("loadOp");
		attachmentDesc.Field<&AttachmentDesc::storeOp>("storeOp");
		attachmentDesc.Field<&AttachmentDesc::stencilLoadOp>("stencilLoadOp");
		attachmentDesc.Field<&AttachmentDesc::stencilStoreOp>("stencilStoreOp");

		auto renderPassDesc = Reflection::Type<RenderPassDesc>();
		renderPassDesc.Field<&RenderPassDesc::attachments>("attachments");
		renderPassDesc.Field<&RenderPassDesc::debugName>("debugName");

		auto graphicsPipelineDesc = Reflection::Type<GraphicsPipelineDesc>();
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::shaderVariant>("shaderVariant");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::topology>("topology");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::rasterizerState>("rasterizerState");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::depthStencilState>("depthStencilState");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::blendStates>("blendStates");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::renderPass>("renderPass");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::debugName>("debugName");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::previousState>("previousState");

		auto computePipelineDesc = Reflection::Type<ComputePipelineDesc>();
		computePipelineDesc.Field<&ComputePipelineDesc::shaderVariant>("shaderVariant");
		computePipelineDesc.Field<&ComputePipelineDesc::debugName>("debugName");
		computePipelineDesc.Field<&ComputePipelineDesc::previousState>("previousState");

		auto descriptorSetDesc = Reflection::Type<DescriptorSetDesc>();
		descriptorSetDesc.Field<&DescriptorSetDesc::bindings>("bindings");
		descriptorSetDesc.Field<&DescriptorSetDesc::debugName>("debugName");

		auto queryPoolDesc = Reflection::Type<QueryPoolDesc>();
		queryPoolDesc.Field<&QueryPoolDesc::type>("type");
		queryPoolDesc.Field<&QueryPoolDesc::queryCount>("queryCount");
		queryPoolDesc.Field<&QueryPoolDesc::debugName>("debugName");

		auto descriptorUpdate = Reflection::Type<DescriptorUpdate>();
		descriptorUpdate.Field<&DescriptorUpdate::type>("type");
		descriptorUpdate.Field<&DescriptorUpdate::binding>("binding");
		descriptorUpdate.Field<&DescriptorUpdate::arrayElement>("arrayElement");
		descriptorUpdate.Field<&DescriptorUpdate::buffer>("buffer");
		descriptorUpdate.Field<&DescriptorUpdate::bufferOffset>("bufferOffset");
		descriptorUpdate.Field<&DescriptorUpdate::bufferRange>("bufferRange");
		descriptorUpdate.Field<&DescriptorUpdate::texture>("texture");
		descriptorUpdate.Field<&DescriptorUpdate::sampler>("sampler");
		descriptorUpdate.Field<&DescriptorUpdate::topLevelAS>("topLevelAS");

		auto geometryTrianglesDesc = Reflection::Type<GeometryTrianglesDesc>();
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::vertexBuffer>("vertexBuffer");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::vertexOffset>("vertexOffset");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::vertexCount>("vertexCount");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::vertexStride>("vertexStride");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::vertexFormat>("vertexFormat");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::indexBuffer>("indexBuffer");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::indexOffset>("indexOffset");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::indexCount>("indexCount");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::indexType>("indexType");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::transformBuffer>("transformBuffer");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::transformOffset>("transformOffset");
		geometryTrianglesDesc.Field<&GeometryTrianglesDesc::opaque>("opaque");

		auto geometryAABBsDesc = Reflection::Type<GeometryAABBsDesc>();
		geometryAABBsDesc.Field<&GeometryAABBsDesc::aabbBuffer>("aabbBuffer");
		geometryAABBsDesc.Field<&GeometryAABBsDesc::aabbOffset>("aabbOffset");
		geometryAABBsDesc.Field<&GeometryAABBsDesc::aabbCount>("aabbCount");
		geometryAABBsDesc.Field<&GeometryAABBsDesc::aabbStride>("aabbStride");
		geometryAABBsDesc.Field<&GeometryAABBsDesc::opaque>("opaque");

		auto geometryDesc = Reflection::Type<GeometryDesc>();
		geometryDesc.Field<&GeometryDesc::type>("type");
		geometryDesc.Field<&GeometryDesc::triangles>("triangles");
		geometryDesc.Field<&GeometryDesc::aabbs>("aabbs");

		auto bottomLevelASDesc = Reflection::Type<BottomLevelASDesc>();
		bottomLevelASDesc.Field<&BottomLevelASDesc::geometries>("geometries");
		bottomLevelASDesc.Field<&BottomLevelASDesc::flags>("flags");
		bottomLevelASDesc.Field<&BottomLevelASDesc::debugName>("debugName");

		auto instanceDesc = Reflection::Type<InstanceDesc>();
		instanceDesc.Field<&InstanceDesc::bottomLevelAS>("bottomLevelAS");
		instanceDesc.Field<&InstanceDesc::transform>("transform");
		instanceDesc.Field<&InstanceDesc::instanceID>("instanceID");
		instanceDesc.Field<&InstanceDesc::instanceMask>("instanceMask");
		instanceDesc.Field<&InstanceDesc::instanceShaderBindingTableRecordOffset>("instanceShaderBindingTableRecordOffset");
		instanceDesc.Field<&InstanceDesc::frontCounterClockwise>("frontCounterClockwise");
		instanceDesc.Field<&InstanceDesc::forceOpaque>("forceOpaque");
		instanceDesc.Field<&InstanceDesc::forceNonOpaque>("forceNonOpaque");

		auto topLevelASDesc = Reflection::Type<TopLevelASDesc>();
		topLevelASDesc.Field<&TopLevelASDesc::instances>("instances");
		topLevelASDesc.Field<&TopLevelASDesc::flags>("flags");
		topLevelASDesc.Field<&TopLevelASDesc::debugName>("debugName");

		auto accelerationStructureBuildInfo = Reflection::Type<AccelerationStructureBuildInfo>();
		accelerationStructureBuildInfo.Field<&AccelerationStructureBuildInfo::update>("update");
		accelerationStructureBuildInfo.Field<&AccelerationStructureBuildInfo::scratchBuffer>("scratchBuffer");
		accelerationStructureBuildInfo.Field<&AccelerationStructureBuildInfo::scratchOffset>("scratchOffset");

		auto rayTracingPipelineDesc = Reflection::Type<RayTracingPipelineDesc>();
		rayTracingPipelineDesc.Field<&RayTracingPipelineDesc::shaderAsset>("shaderAsset");
		rayTracingPipelineDesc.Field<&RayTracingPipelineDesc::maxRecursionDepth>("maxRecursionDepth");
		rayTracingPipelineDesc.Field<&RayTracingPipelineDesc::debugName>("debugName");

		auto swapchainDesc = Reflection::Type<SwapchainDesc>();
		swapchainDesc.Field<&SwapchainDesc::format>("format");
		swapchainDesc.Field<&SwapchainDesc::vsync>("vsync");
		swapchainDesc.Field<&SwapchainDesc::windowHandle>("windowHandle");
		swapchainDesc.Field<&SwapchainDesc::debugName>("debugName");
	}

	void RegisterGraphicsTypes()
	{
		RegisterDeviceTypes();

		auto primitive = Reflection::Type<MeshAsset::Primitive>();
		primitive.Field<&MeshAsset::Primitive::firstIndex>("firstIndex");
		primitive.Field<&MeshAsset::Primitive::indexCount>("indexCount");
		primitive.Field<&MeshAsset::Primitive::materialIndex>("materialIndex");

		Reflection::Type<TextureAssetImage>();
		Reflection::Type<TextureAsset>();
		Reflection::Type<MeshAsset>();
		Reflection::Type<MaterialAsset>();
		Reflection::Type<ShaderAsset>();
		Reflection::Type<ShaderVariant>();

		auto materialType = Reflection::Type<MaterialAsset::MaterialType>();
		materialType.Value<MaterialAsset::MaterialType::Opaque>("Opaque");
		materialType.Value<MaterialAsset::MaterialType::SkyboxEquirectangular>("SkyboxEquirectangular");

		auto alphaMode = Reflection::Type<MaterialAsset::AlphaMode>();
		alphaMode.Value<MaterialAsset::AlphaMode::None>("None");
		alphaMode.Value<MaterialAsset::AlphaMode::Opaque>("Opaque");
		alphaMode.Value<MaterialAsset::AlphaMode::Mask>("Mask");
		alphaMode.Value<MaterialAsset::AlphaMode::Blend>("Blend");

		auto textureChannel = Reflection::Type<TextureChannel>();
		textureChannel.Value<TextureChannel::Red>("Red");
		textureChannel.Value<TextureChannel::Green>("Green");
		textureChannel.Value<TextureChannel::Blue>("Blue");
		textureChannel.Value<TextureChannel::Alpha>("Alpha");
	}
}
