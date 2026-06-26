
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	void RegisterAnimationTypes()
	{
		auto animationParameterType = Reflection::Type<AnimationParameterType>();
		animationParameterType.Value<AnimationParameterType::Float>("Float");
		animationParameterType.Value<AnimationParameterType::Int>("Int");
		animationParameterType.Value<AnimationParameterType::Bool>("Bool");
		animationParameterType.Value<AnimationParameterType::Trigger>("Trigger");

		auto animationLayerBlendMode = Reflection::Type<AnimationLayerBlendMode>();
		animationLayerBlendMode.Value<AnimationLayerBlendMode::Override>("Override");
		animationLayerBlendMode.Value<AnimationLayerBlendMode::Additive>("Additive");

		auto rootMotionMode = Reflection::Type<RootMotionMode>();
		rootMotionMode.Value<RootMotionMode::None>("None");
		rootMotionMode.Value<RootMotionMode::Transform>("Transform");
		rootMotionMode.Value<RootMotionMode::Velocity>("Velocity");

		auto rootMotionAxes = Reflection::Type<RootMotionAxes>();
		rootMotionAxes.Value<RootMotionAxes::XZ>("XZ");
		rootMotionAxes.Value<RootMotionAxes::XYZ>("XYZ");

		auto animationTransitionCondition = Reflection::Type<AnimationTransitionCondition>();
		animationTransitionCondition.Value<AnimationTransitionCondition::Greater>();
		animationTransitionCondition.Value<AnimationTransitionCondition::Less>();
		animationTransitionCondition.Value<AnimationTransitionCondition::Equal>();
		animationTransitionCondition.Value<AnimationTransitionCondition::NotEqual>();
		animationTransitionCondition.Value<AnimationTransitionCondition::True>();
		animationTransitionCondition.Value<AnimationTransitionCondition::False>();

		Resources::Type<AnimationParameterResource>()
			.Field<AnimationParameterResource::Name>(ResourceFieldType::String)
			.Field<AnimationParameterResource::Type>(ResourceFieldType::Enum, TypeInfo<AnimationParameterType>::ID())
			.Field<AnimationParameterResource::FloatValue>(ResourceFieldType::Float)
			.Field<AnimationParameterResource::IntValue>(ResourceFieldType::Int)
			.Field<AnimationParameterResource::BoolValue>(ResourceFieldType::Bool)
			.Build();

		Resources::Type<AnimationTransitionConditionResource>()
			.Field<AnimationTransitionConditionResource::Parameter>(ResourceFieldType::Reference, sktypeid(AnimationParameterResource))
			.Field<AnimationTransitionConditionResource::Condition>(ResourceFieldType::Enum, TypeInfo<AnimationTransitionCondition>::ID())
			.Field<AnimationTransitionConditionResource::Value>(ResourceFieldType::Float)
			.Build();

		Resources::Type<AnimationTransitionResource>()
			.Field<AnimationTransitionResource::From>(ResourceFieldType::Reference, {}, ResourceFieldProperties::NoUI)
			.Field<AnimationTransitionResource::To>(ResourceFieldType::Reference, {}, ResourceFieldProperties::NoUI)
			.Field<AnimationTransitionResource::CrossFadeTime>(ResourceFieldType::Float)
			.Field<AnimationTransitionResource::HasExitTime>(ResourceFieldType::Bool)
			.Field<AnimationTransitionResource::ExitTime>(ResourceFieldType::Float)
			.Field<AnimationTransitionResource::FixedDuration>(ResourceFieldType::Bool)
			.Field<AnimationTransitionResource::TransitionOffset>(ResourceFieldType::Float)
			.Field<AnimationTransitionResource::Conditions>(ResourceFieldType::SubObjectList, sktypeid(AnimationTransitionConditionResource))
			.Build();

		Resources::Type<AnimationStateResource>()
			.Field<AnimationStateResource::Name>(ResourceFieldType::String)
			.Field<AnimationStateResource::Animation>(ResourceFieldType::Reference, sktypeid(AnimationClipResource))
			.Field<AnimationStateResource::Position>(ResourceFieldType::Vec2, {}, ResourceFieldProperties::NoUI)
			.Field<AnimationStateResource::Speed>(ResourceFieldType::Float)
			.Build();

		Resources::Type<AnimationLayerResource>()
			.Field<AnimationLayerResource::Name>(ResourceFieldType::String)
			.Field<AnimationLayerResource::States>(ResourceFieldType::SubObjectList)
			.Field<AnimationLayerResource::Transitions>(ResourceFieldType::SubObjectList)
			.Field<AnimationLayerResource::DefaultState>(ResourceFieldType::Reference, sktypeid(AnimationStateResource))
			.Field<AnimationLayerResource::Weight>(ResourceFieldType::Float)
			.Field<AnimationLayerResource::Avatar>(ResourceFieldType::Reference, sktypeid(AnimationAvatarResource))
			.Field<AnimationLayerResource::BlendMode>(ResourceFieldType::Enum, TypeInfo<AnimationLayerBlendMode>::ID())
			.Field<AnimationLayerResource::RootMotion>(ResourceFieldType::Enum, TypeInfo<RootMotionMode>::ID())
			.Field<AnimationLayerResource::RootMotionAxis>(ResourceFieldType::Enum, TypeInfo<RootMotionAxes>::ID())
			.Field<AnimationLayerResource::ApplyRotation>(ResourceFieldType::Bool)
			.Build();

		Resources::Type<AnimationAvatarBoneResource>()
			.Field<AnimationAvatarBoneResource::BoneName>(ResourceFieldType::String)
			.Field<AnimationAvatarBoneResource::Enabled>(ResourceFieldType::Bool)
			.Field<AnimationAvatarBoneResource::Children>(ResourceFieldType::SubObjectList)
			.Build();

		Resources::Type<AnimationAvatarResource>()
			.Field<AnimationAvatarResource::Name>(ResourceFieldType::String)
			.Field<AnimationAvatarResource::RootBone>(ResourceFieldType::SubObject, sktypeid(AnimationAvatarBoneResource), ResourceFieldProperties::NoUI)
			.Build();

		Resources::Type<AnimationControllerResource>()
			.Field<AnimationControllerResource::Name>(ResourceFieldType::String)
			.Field<AnimationControllerResource::PreviewEntity>(ResourceFieldType::Reference, sktypeid(EntityResource))
			.Field<AnimationControllerResource::Layers>(ResourceFieldType::SubObjectList)
			.Field<AnimationControllerResource::Parameters>(ResourceFieldType::SubObjectList)
			.Field<AnimationControllerResource::Avatars>(ResourceFieldType::SubObjectList)
			.Build();
	}

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

		auto deviceResult = Reflection::Type<DeviceResult>();
		deviceResult.Value<DeviceResult::Success>("Success");
		deviceResult.Value<DeviceResult::SwapchainOutOfDate>("SwapchainOutOfDate");
		deviceResult.Value<DeviceResult::Error>("Error");

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
		resourceState.Value<ResourceState::IndirectArgument>("IndirectArgument");

		auto barrierSyncScope = Reflection::Type<BarrierSyncScope>();
		barrierSyncScope.Value<BarrierSyncScope::Automatic>("Automatic");
		barrierSyncScope.Value<BarrierSyncScope::Graphics>("Graphics");
		barrierSyncScope.Value<BarrierSyncScope::Compute>("Compute");
		barrierSyncScope.Value<BarrierSyncScope::Raytrace>("Raytrace");
		barrierSyncScope.Value<BarrierSyncScope::Transfer>("Transfer");

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
		shaderStage.Value<ShaderStage::None>("None");
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

		auto queueType = Reflection::Type<QueueType>();
		queueType.Value<QueueType::None>("None");
		queueType.Value<QueueType::Graphics>("Graphics");
		queueType.Value<QueueType::Compute>("Compute");
		queueType.Value<QueueType::Transfer>("Transfer");
		queueType.Value<QueueType::All>("All");

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

		auto pipelineStatisticFlag = Reflection::Type<PipelineStatisticFlag>();
		pipelineStatisticFlag.Value<PipelineStatisticFlag::InputAssemblyVertices>("InputAssemblyVertices");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::InputAssemblyPrimitives>("InputAssemblyPrimitives");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::VertexShaderInvocations>("VertexShaderInvocations");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::GeometryShaderInvocations>("GeometryShaderInvocations");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::GeometryShaderPrimitives>("GeometryShaderPrimitives");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::ClippingInvocations>("ClippingInvocations");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::ClippingPrimitives>("ClippingPrimitives");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::FragmentShaderInvocations>("FragmentShaderInvocations");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::TesselationControlShaderPatches>("TesselationControlShaderPatches");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::TesselationEvaluationShaderInvocations>("TesselationEvaluationShaderInvocations");
		pipelineStatisticFlag.Value<PipelineStatisticFlag::ComputeShaderInvocations>("ComputeShaderInvocations");

		auto textureFormat = Reflection::Type<Format>();
		textureFormat.Value<Format::Unknown>("Unknown");
		textureFormat.Value<Format::R8_UNORM>("R8_UNORM");
		textureFormat.Value<Format::R8_SNORM>("R8_SNORM");
		textureFormat.Value<Format::R8_UINT>("R8_UINT");
		textureFormat.Value<Format::R8_SINT>("R8_SINT");
		textureFormat.Value<Format::R8_SRGB>("R8_SRGB");
		textureFormat.Value<Format::R16_UNORM>("R16_UNORM");
		textureFormat.Value<Format::R16_SNORM>("R16_SNORM");
		textureFormat.Value<Format::R16_UINT>("R16_UINT");
		textureFormat.Value<Format::R16_SINT>("R16_SINT");
		textureFormat.Value<Format::R16_FLOAT>("R16_FLOAT");
		textureFormat.Value<Format::RG8_UNORM>("RG8_UNORM");
		textureFormat.Value<Format::RG8_SNORM>("RG8_SNORM");
		textureFormat.Value<Format::RG8_UINT>("RG8_UINT");
		textureFormat.Value<Format::RG8_SINT>("RG8_SINT");
		textureFormat.Value<Format::RG8_SRGB>("RG8_SRGB");
		textureFormat.Value<Format::RGB16_UNORM>("RGB16_UNORM");
		textureFormat.Value<Format::RGB16_SNORM>("RGB16_SNORM");
		textureFormat.Value<Format::RGB16_UINT>("RGB16_UINT");
		textureFormat.Value<Format::RGB16_SINT>("RGB16_SINT");
		textureFormat.Value<Format::RGB16_FLOAT>("RGB16_FLOAT");
		textureFormat.Value<Format::R32_UINT>("R32_UINT");
		textureFormat.Value<Format::R32_SINT>("R32_SINT");
		textureFormat.Value<Format::R32_FLOAT>("R32_FLOAT");
		textureFormat.Value<Format::RG16_UNORM>("RG16_UNORM");
		textureFormat.Value<Format::RG16_SNORM>("RG16_SNORM");
		textureFormat.Value<Format::RG16_UINT>("RG16_UINT");
		textureFormat.Value<Format::RG16_SINT>("RG16_SINT");
		textureFormat.Value<Format::RG16_FLOAT>("RG16_FLOAT");
		textureFormat.Value<Format::RGBA8_UNORM>("RGBA8_UNORM");
		textureFormat.Value<Format::RGBA8_SNORM>("RGBA8_SNORM");
		textureFormat.Value<Format::RGBA8_UINT>("RGBA8_UINT");
		textureFormat.Value<Format::RGBA8_SINT>("RGBA8_SINT");
		textureFormat.Value<Format::RGBA8_SRGB>("RGBA8_SRGB");
		textureFormat.Value<Format::BGRA8_UNORM>("BGRA8_UNORM");
		textureFormat.Value<Format::BGRA8_SNORM>("BGRA8_SNORM");
		textureFormat.Value<Format::BGRA8_UINT>("BGRA8_UINT");
		textureFormat.Value<Format::BGRA8_SINT>("BGRA8_SINT");
		textureFormat.Value<Format::BGRA8_SRGB>("BGRA8_SRGB");
		textureFormat.Value<Format::RGB10A2_UNORM>("RGB10A2_UNORM");
		textureFormat.Value<Format::RGB10A2_UINT>("RGB10A2_UINT");
		textureFormat.Value<Format::RG11B10_FLOAT>("RG11B10_FLOAT");
		textureFormat.Value<Format::RGB9E5_FLOAT>("RGB9E5_FLOAT");
		textureFormat.Value<Format::RG32_UINT>("RG32_UINT");
		textureFormat.Value<Format::RG32_SINT>("RG32_SINT");
		textureFormat.Value<Format::RG32_FLOAT>("RG32_FLOAT");
		textureFormat.Value<Format::RGBA16_UNORM>("RGBA16_UNORM");
		textureFormat.Value<Format::RGBA16_SNORM>("RGBA16_SNORM");
		textureFormat.Value<Format::RGBA16_UINT>("RGBA16_UINT");
		textureFormat.Value<Format::RGBA16_SINT>("RGBA16_SINT");
		textureFormat.Value<Format::RGBA16_FLOAT>("RGBA16_FLOAT");
		textureFormat.Value<Format::RGB32_UINT>("RGB32_UINT");
		textureFormat.Value<Format::RGB32_SINT>("RGB32_SINT");
		textureFormat.Value<Format::RGB32_FLOAT>("RGB32_FLOAT");
		textureFormat.Value<Format::RGBA32_UINT>("RGBA32_UINT");
		textureFormat.Value<Format::RGBA32_SINT>("RGBA32_SINT");
		textureFormat.Value<Format::RGBA32_FLOAT>("RGBA32_FLOAT");
		textureFormat.Value<Format::D16_UNORM>("D16_UNORM");
		textureFormat.Value<Format::D24_UNORM_S8_UINT>("D24_UNORM_S8_UINT");
		textureFormat.Value<Format::D32_FLOAT>("D32_FLOAT");
		textureFormat.Value<Format::D32_FLOAT_S8_UINT>("D32_FLOAT_S8_UINT");
		textureFormat.Value<Format::BC1_UNORM>("BC1_UNORM");
		textureFormat.Value<Format::BC1_SRGB>("BC1_SRGB");
		textureFormat.Value<Format::BC2_UNORM>("BC2_UNORM");
		textureFormat.Value<Format::BC2_SRGB>("BC2_SRGB");
		textureFormat.Value<Format::BC3_UNORM>("BC3_UNORM");
		textureFormat.Value<Format::BC3_SRGB>("BC3_SRGB");
		textureFormat.Value<Format::BC4_UNORM>("BC4_UNORM");
		textureFormat.Value<Format::BC4_SNORM>("BC4_SNORM");
		textureFormat.Value<Format::BC5_UNORM>("BC5_UNORM");
		textureFormat.Value<Format::BC5_SNORM>("BC5_SNORM");
		textureFormat.Value<Format::BC6H_UF16>("BC6H_UF16");
		textureFormat.Value<Format::BC6H_SF16>("BC6H_SF16");
		textureFormat.Value<Format::BC7_UNORM>("BC7_UNORM");
		textureFormat.Value<Format::BC7_SRGB>("BC7_SRGB");
		textureFormat.Value<Format::ETC1_UNORM>("ETC1_UNORM");
		textureFormat.Value<Format::ETC2_UNORM>("ETC2_UNORM");
		textureFormat.Value<Format::ETC2_SRGB>("ETC2_SRGB");
		textureFormat.Value<Format::ETC2A_UNORM>("ETC2A_UNORM");
		textureFormat.Value<Format::ETC2A_SRGB>("ETC2A_SRGB");
		textureFormat.Value<Format::ASTC_4x4_UNORM>("ASTC_4x4_UNORM");
		textureFormat.Value<Format::ASTC_4x4_SRGB>("ASTC_4x4_SRGB");
		textureFormat.Value<Format::ASTC_5x4_UNORM>("ASTC_5x4_UNORM");
		textureFormat.Value<Format::ASTC_5x4_SRGB>("ASTC_5x4_SRGB");
		textureFormat.Value<Format::ASTC_5x5_UNORM>("ASTC_5x5_UNORM");
		textureFormat.Value<Format::ASTC_5x5_SRGB>("ASTC_5x5_SRGB");
		textureFormat.Value<Format::ASTC_6x5_UNORM>("ASTC_6x5_UNORM");
		textureFormat.Value<Format::ASTC_6x5_SRGB>("ASTC_6x5_SRGB");
		textureFormat.Value<Format::ASTC_6x6_UNORM>("ASTC_6x6_UNORM");
		textureFormat.Value<Format::ASTC_6x6_SRGB>("ASTC_6x6_SRGB");
		textureFormat.Value<Format::ASTC_8x5_UNORM>("ASTC_8x5_UNORM");
		textureFormat.Value<Format::ASTC_8x5_SRGB>("ASTC_8x5_SRGB");
		textureFormat.Value<Format::ASTC_8x6_UNORM>("ASTC_8x6_UNORM");
		textureFormat.Value<Format::ASTC_8x6_SRGB>("ASTC_8x6_SRGB");
		textureFormat.Value<Format::ASTC_8x8_UNORM>("ASTC_8x8_UNORM");
		textureFormat.Value<Format::ASTC_8x8_SRGB>("ASTC_8x8_SRGB");
		textureFormat.Value<Format::ASTC_10x5_UNORM>("ASTC_10x5_UNORM");
		textureFormat.Value<Format::ASTC_10x5_SRGB>("ASTC_10x5_SRGB");
		textureFormat.Value<Format::ASTC_10x6_UNORM>("ASTC_10x6_UNORM");
		textureFormat.Value<Format::ASTC_10x6_SRGB>("ASTC_10x6_SRGB");
		textureFormat.Value<Format::ASTC_10x8_UNORM>("ASTC_10x8_UNORM");
		textureFormat.Value<Format::ASTC_10x8_SRGB>("ASTC_10x8_SRGB");
		textureFormat.Value<Format::ASTC_10x10_UNORM>("ASTC_10x10_UNORM");
		textureFormat.Value<Format::ASTC_10x10_SRGB>("ASTC_10x10_SRGB");
		textureFormat.Value<Format::ASTC_12x10_UNORM>("ASTC_12x10_UNORM");
		textureFormat.Value<Format::ASTC_12x10_SRGB>("ASTC_12x10_SRGB");
		textureFormat.Value<Format::ASTC_12x12_UNORM>("ASTC_12x12_UNORM");
		textureFormat.Value<Format::ASTC_12x12_SRGB>("ASTC_12x12_SRGB");

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
		deviceFeatures.Field<&DeviceFeatures::bindlessTextureSupported>("bindlessSupported");
		deviceFeatures.Field<&DeviceFeatures::bufferDeviceAddress>("bufferDeviceAddress");
		deviceFeatures.Field<&DeviceFeatures::drawIndirectCount>("drawIndirectCount");
		deviceFeatures.Field<&DeviceFeatures::rayTracing>("rayTracing");
		deviceFeatures.Field<&DeviceFeatures::multiviewEnabled>("multiviewEnabled");
		deviceFeatures.Field<&DeviceFeatures::bindlessSamplerSupported>("bindlessSamplerSupported");
		deviceFeatures.Field<&DeviceFeatures::bindlessBufferSupported>("bindlessBufferSupported");
		deviceFeatures.Field<&DeviceFeatures::resolveDepth>("resolveDepth");
		deviceFeatures.Field<&DeviceFeatures::fragmentShaderBarycentric>("fragmentShaderBarycentric");

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
		deviceLimits.Field<&DeviceLimits::maxAttachmentSamples>("maxAttachmentSamples");


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
		textureDesc.Field<&TextureDesc::sampleCount>("sampleCount");
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
		pipelineDesc.Field<&PipelineDesc::numThreads>("numThreads");

		auto attachmentDesc = Reflection::Type<AttachmentDesc>();
		attachmentDesc.Field<&AttachmentDesc::initialState>("initialState");
		attachmentDesc.Field<&AttachmentDesc::finalState>("finalState");
		attachmentDesc.Field<&AttachmentDesc::loadOp>("loadOp");
		attachmentDesc.Field<&AttachmentDesc::storeOp>("storeOp");
		attachmentDesc.Field<&AttachmentDesc::stencilLoadOp>("stencilLoadOp");
		attachmentDesc.Field<&AttachmentDesc::stencilStoreOp>("stencilStoreOp");
		attachmentDesc.Field<&AttachmentDesc::sampleCount>("sampleCount");
		attachmentDesc.Field<&AttachmentDesc::format>("format");

		auto renderPassDesc = Reflection::Type<RenderPassDesc>();
		renderPassDesc.Field<&RenderPassDesc::attachments>("attachments");
		renderPassDesc.Field<&RenderPassDesc::resolveAttachments>("resolveAttachments");
		renderPassDesc.Field<&RenderPassDesc::debugName>("debugName");

		auto graphicsPipelineDesc = Reflection::Type<GraphicsPipelineDesc>();
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::shader>("shader");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::variant>("variant");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::topology>("topology");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::rasterizerState>("rasterizerState");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::depthStencilState>("depthStencilState");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::blendStates>("blendStates");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::renderPass>("renderPass");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::debugName>("debugName");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::previousState>("previousState");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::vertexInputStride>("vertexInputStride");
		graphicsPipelineDesc.Field<&GraphicsPipelineDesc::allowImmediateSet>("allowImmediateSet");

		auto computePipelineDesc = Reflection::Type<ComputePipelineDesc>();
		computePipelineDesc.Field<&ComputePipelineDesc::shader>("shader");
		computePipelineDesc.Field<&ComputePipelineDesc::variant>("variant");
		computePipelineDesc.Field<&ComputePipelineDesc::allowImmediateSet>("allowImmediateSet");
		computePipelineDesc.Field<&ComputePipelineDesc::debugName>("debugName");
		computePipelineDesc.Field<&ComputePipelineDesc::previousState>("previousState");

		auto descriptorSetDesc = Reflection::Type<DescriptorSetDesc>();
		descriptorSetDesc.Field<&DescriptorSetDesc::bindings>("bindings");
		descriptorSetDesc.Field<&DescriptorSetDesc::debugName>("debugName");

		auto queryPoolDesc = Reflection::Type<QueryPoolDesc>();
		queryPoolDesc.Field<&QueryPoolDesc::type>("type");
		queryPoolDesc.Field<&QueryPoolDesc::queryCount>("queryCount");
		queryPoolDesc.Field<&QueryPoolDesc::allowPartialResults>("allowPartialResults");
		queryPoolDesc.Field<&QueryPoolDesc::returnAvailability>("returnAvailability");
		queryPoolDesc.Field<&QueryPoolDesc::pipelineStatistics>("pipelineStatistics");
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
		rayTracingPipelineDesc.Field<&RayTracingPipelineDesc::shader>("shader");
		rayTracingPipelineDesc.Field<&RayTracingPipelineDesc::variant>("variant");
		rayTracingPipelineDesc.Field<&RayTracingPipelineDesc::maxRecursionDepth>("maxRecursionDepth");
		rayTracingPipelineDesc.Field<&RayTracingPipelineDesc::debugName>("debugName");

		auto swapchainDesc = Reflection::Type<SwapchainDesc>();
		swapchainDesc.Field<&SwapchainDesc::desiredFormat>("desiredFormat");
		swapchainDesc.Field<&SwapchainDesc::vsync>("vsync");
		swapchainDesc.Field<&SwapchainDesc::window>("window");
		swapchainDesc.Field<&SwapchainDesc::debugName>("debugName");

		auto framebufferDesc = Reflection::Type<FramebufferDesc>();
		framebufferDesc.Field<&FramebufferDesc::renderPass>("renderPass");
		framebufferDesc.Field<&FramebufferDesc::attachments>("attachments");
		framebufferDesc.Field<&FramebufferDesc::debugName>("debugName");

		auto clearValues = Reflection::Type<ClearValues>();
		clearValues.Field<&ClearValues::color>("color");
		clearValues.Field<&ClearValues::depth>("depth");
		clearValues.Field<&ClearValues::stencil>("stencil");

		auto beginRenderPassInfo = Reflection::Type<BeginRenderPassInfo>();
		beginRenderPassInfo.Field<&BeginRenderPassInfo::renderPass>("renderPass");
		beginRenderPassInfo.Field<&BeginRenderPassInfo::framebuffer>("framebuffer");
		beginRenderPassInfo.Field<&BeginRenderPassInfo::clearValues>("clearValues");

		auto viewportInfo = Reflection::Type<ViewportInfo>();
		viewportInfo.Field<&ViewportInfo::x>("x");
		viewportInfo.Field<&ViewportInfo::y>("y");
		viewportInfo.Field<&ViewportInfo::width>("width");
		viewportInfo.Field<&ViewportInfo::height>("height");
		viewportInfo.Field<&ViewportInfo::minDepth>("minDepth");
		viewportInfo.Field<&ViewportInfo::maxDepth>("maxDepth");

		auto bufferTextureCopy = Reflection::Type<BufferTextureCopy>();
		bufferTextureCopy.Field<&BufferTextureCopy::buffer>("buffer");
		bufferTextureCopy.Field<&BufferTextureCopy::texture>("texture");
		bufferTextureCopy.Field<&BufferTextureCopy::extent>("extent");
		bufferTextureCopy.Field<&BufferTextureCopy::mipLevel>("mipLevel");
		bufferTextureCopy.Field<&BufferTextureCopy::arrayLayer>("arrayLayer");
		bufferTextureCopy.Field<&BufferTextureCopy::bufferOffset>("bufferOffset");
		bufferTextureCopy.Field<&BufferTextureCopy::textureOffset>("textureOffset");

		auto textureCopy = Reflection::Type<TextureCopy>();
		textureCopy.Field<&TextureCopy::srcTexture>("srcTexture");
		textureCopy.Field<&TextureCopy::dstTexture>("dstTexture");
		textureCopy.Field<&TextureCopy::extent>("extent");
		textureCopy.Field<&TextureCopy::srcMipLevel>("srcMipLevel");
		textureCopy.Field<&TextureCopy::srcArrayLayer>("srcArrayLayer");
		textureCopy.Field<&TextureCopy::dstMipLevel>("dstMipLevel");
		textureCopy.Field<&TextureCopy::dstArrayLayer>("dstArrayLayer");

		auto textureBlit = Reflection::Type<TextureBlit>();
		textureBlit.Field<&TextureBlit::srcTexture>("srcTexture");
		textureBlit.Field<&TextureBlit::dstTexture>("dstTexture");
		textureBlit.Field<&TextureBlit::srcExtent>("srcExtent");
		textureBlit.Field<&TextureBlit::dstExtent>("dstExtent");
		textureBlit.Field<&TextureBlit::srcMipLevel>("srcMipLevel");
		textureBlit.Field<&TextureBlit::srcArrayLayer>("srcArrayLayer");
		textureBlit.Field<&TextureBlit::dstMipLevel>("dstMipLevel");
		textureBlit.Field<&TextureBlit::dstArrayLayer>("dstArrayLayer");

		auto bufferBarrierDesc = Reflection::Type<BufferBarrierDesc>();
		bufferBarrierDesc.Field<&BufferBarrierDesc::buffer>("buffer");
		bufferBarrierDesc.Field<&BufferBarrierDesc::oldState>("oldState");
		bufferBarrierDesc.Field<&BufferBarrierDesc::newState>("newState");
		bufferBarrierDesc.Field<&BufferBarrierDesc::srcScope>("srcScope");
		bufferBarrierDesc.Field<&BufferBarrierDesc::dstScope>("dstScope");

		auto textureBarrierDesc = Reflection::Type<TextureBarrierDesc>();
		textureBarrierDesc.Field<&TextureBarrierDesc::texture>("texture");
		textureBarrierDesc.Field<&TextureBarrierDesc::oldState>("oldState");
		textureBarrierDesc.Field<&TextureBarrierDesc::newState>("newState");
		textureBarrierDesc.Field<&TextureBarrierDesc::srcScope>("srcScope");
		textureBarrierDesc.Field<&TextureBarrierDesc::dstScope>("dstScope");
		textureBarrierDesc.Field<&TextureBarrierDesc::baseMipLevel>("baseMipLevel");
		textureBarrierDesc.Field<&TextureBarrierDesc::mipLevelCount>("mipLevelCount");
		textureBarrierDesc.Field<&TextureBarrierDesc::baseArrayLayer>("baseArrayLayer");
		textureBarrierDesc.Field<&TextureBarrierDesc::arrayLayerCount>("arrayLayerCount");

		auto bufferUploadInfo = Reflection::Type<BufferUploadInfo>();
		bufferUploadInfo.Field<&BufferUploadInfo::buffer>("buffer");
		bufferUploadInfo.Field<&BufferUploadInfo::size>("size");
		bufferUploadInfo.Field<&BufferUploadInfo::srcOffset>("srcOffset");
		bufferUploadInfo.Field<&BufferUploadInfo::dstOffset>("dstOffset");

		auto textureDataRegion = Reflection::Type<TextureDataRegion>();
		textureDataRegion.Field<&TextureDataRegion::dataOffset>("dataOffset");
		textureDataRegion.Field<&TextureDataRegion::layerCount>("layerCount");
		textureDataRegion.Field<&TextureDataRegion::levelCount>("levelCount");
		textureDataRegion.Field<&TextureDataRegion::mipLevel>("mipLevel");
		textureDataRegion.Field<&TextureDataRegion::arrayLayer>("arrayLayer");
		textureDataRegion.Field<&TextureDataRegion::extent>("extent");

		auto textureDataInfo = Reflection::Type<TextureDataInfo>();
		textureDataInfo.Field<&TextureDataInfo::texture>("texture");
		textureDataInfo.Field<&TextureDataInfo::size>("size");
		textureDataInfo.Field<&TextureDataInfo::regions>("regions");

		// GPU interface types
		auto gpuAdapter = Reflection::Type<GPUAdapter>();
		gpuAdapter.Function<&GPUAdapter::GetScore>("GetScore");
		gpuAdapter.Function<&GPUAdapter::GetName>("GetName");

		auto gpuBuffer = Reflection::Type<GPUBuffer>();
		gpuBuffer.Function<&GPUBuffer::Unmap>("Unmap");
		gpuBuffer.Function<&GPUBuffer::Destroy>("Destroy");
		gpuBuffer.Function<&GPUBuffer::GetDesc>("GetDesc");

		auto gpuTexture = Reflection::Type<GPUTexture>();
		gpuTexture.Function<&GPUTexture::GetDesc>("GetDesc");
		gpuTexture.Function<&GPUTexture::GetTextureView>("GetTextureView");
		gpuTexture.Function<&GPUTexture::Destroy>("Destroy");

		auto gpuTextureView = Reflection::Type<GPUTextureView>();
		gpuTextureView.Function<&GPUTextureView::GetDesc>("GetDesc");
		gpuTextureView.Function<&GPUTextureView::Destroy>("Destroy");
		gpuTextureView.Function<&GPUTextureView::GetTexture>("GetTexture");

		auto gpuSampler = Reflection::Type<GPUSampler>();
		gpuSampler.Function<&GPUSampler::GetDesc>("GetDesc");
		gpuSampler.Function<&GPUSampler::Destroy>("Destroy");

		auto gpuPipeline = Reflection::Type<GPUPipeline>();
		gpuPipeline.Function<&GPUPipeline::GetBindPoint>("GetBindPoint");
		gpuPipeline.Function<&GPUPipeline::GetPipelineDesc>("GetPipelineDesc");
		gpuPipeline.Function<&GPUPipeline::Destroy>("Destroy");

		auto gpuRenderPass = Reflection::Type<GPURenderPass>();
		gpuRenderPass.Function<&GPURenderPass::GetDesc>("GetDesc");
		gpuRenderPass.Function<&GPURenderPass::Destroy>("Destroy");

		auto gpuFramebuffer = Reflection::Type<GPUFramebuffer>();
		gpuFramebuffer.Function<&GPUFramebuffer::GetDesc>("GetDesc");
		gpuFramebuffer.Function<&GPUFramebuffer::Destroy>("Destroy");
		gpuFramebuffer.Function<&GPUFramebuffer::GetExtent>("GetExtent");

		auto gpuQueryPool = Reflection::Type<GPUQueryPool>();
		gpuQueryPool.Function<&GPUQueryPool::GetDesc>("GetDesc");
		gpuQueryPool.Function<&GPUQueryPool::Destroy>("Destroy");

		auto gpuBottomLevelAS = Reflection::Type<GPUBottomLevelAS>();
		gpuBottomLevelAS.Function<&GPUBottomLevelAS::GetDesc>("GetDesc");
		gpuBottomLevelAS.Function<&GPUBottomLevelAS::IsCompacted>("IsCompacted");
		gpuBottomLevelAS.Function<&GPUBottomLevelAS::GetCompactedSize>("GetCompactedSize");
		gpuBottomLevelAS.Function<&GPUBottomLevelAS::Destroy>("Destroy");

		auto gpuTopLevelAS = Reflection::Type<GPUTopLevelAS>();
		gpuTopLevelAS.Function<&GPUTopLevelAS::GetDesc>("GetDesc");
		gpuTopLevelAS.Function<&GPUTopLevelAS::UpdateInstances>("UpdateInstances", "instances");
		gpuTopLevelAS.Function<&GPUTopLevelAS::Destroy>("Destroy");

		auto gpuDescriptorSet = Reflection::Type<GPUDescriptorSet>();
		gpuDescriptorSet.Function<&GPUDescriptorSet::GetDesc>("GetDesc");
		gpuDescriptorSet.Function<&GPUDescriptorSet::Update>("Update", "update");
		gpuDescriptorSet.Function<static_cast<void(GPUDescriptorSet::*)(u32, GPUBuffer*, usize, usize)>(&GPUDescriptorSet::UpdateBuffer)>("UpdateBuffer", "binding", "buffer", "offset", "size");
		gpuDescriptorSet.Function<static_cast<void(GPUDescriptorSet::*)(u32, GPUTexture*)>(&GPUDescriptorSet::UpdateTexture)>("UpdateTexture", "binding", "texture");
		gpuDescriptorSet.Function<static_cast<void(GPUDescriptorSet::*)(u32, GPUTexture*, u32)>(&GPUDescriptorSet::UpdateTexture)>("UpdateTextureArray", "binding", "texture", "arrayElement");
		gpuDescriptorSet.Function<static_cast<void(GPUDescriptorSet::*)(u32, GPUTextureView*)>(&GPUDescriptorSet::UpdateTextureView)>("UpdateTextureView", "binding", "textureView");
		gpuDescriptorSet.Function<static_cast<void(GPUDescriptorSet::*)(u32, GPUTextureView*, u32)>(&GPUDescriptorSet::UpdateTextureView)>("UpdateTextureViewArray", "binding", "textureView", "arrayElement");
		gpuDescriptorSet.Function<static_cast<void(GPUDescriptorSet::*)(u32, GPUSampler*)>(&GPUDescriptorSet::UpdateSampler)>("UpdateSampler", "binding", "sampler");
		gpuDescriptorSet.Function<static_cast<void(GPUDescriptorSet::*)(u32, GPUSampler*, u32)>(&GPUDescriptorSet::UpdateSampler)>("UpdateSamplerArray", "binding", "sampler", "arrayElement");
		gpuDescriptorSet.Function<&GPUDescriptorSet::Destroy>("Destroy");

		auto gpuSwapchain = Reflection::Type<GPUSwapchain>();
		gpuSwapchain.Function<&GPUSwapchain::GetDesc>("GetDesc");
		gpuSwapchain.Function<&GPUSwapchain::AcquireNextImage>("AcquireNextImage");
		gpuSwapchain.Function<&GPUSwapchain::GetExtent>("GetExtent");
		gpuSwapchain.Function<&GPUSwapchain::Destroy>("Destroy");
		gpuSwapchain.Function<&GPUSwapchain::GetImageCount>("GetImageCount");
		gpuSwapchain.Function<&GPUSwapchain::GetTextures>("GetTextures");
		gpuSwapchain.Function<&GPUSwapchain::GetCurrentImageIndex>("GetCurrentImageIndex");
		gpuSwapchain.Function<&GPUSwapchain::GetFormat>("GetFormat");

		auto gpuDevice = Reflection::Type<GPUDevice>();
		gpuDevice.Function<&GPUDevice::GetAdapters>("GetAdapters");
		gpuDevice.Function<&GPUDevice::SelectAdapter>("SelectAdapter", "adapter");
		gpuDevice.Function<&GPUDevice::GetProperties>("GetProperties");
		gpuDevice.Function<&GPUDevice::GetFeatures>("GetFeatures");
		gpuDevice.Function<&GPUDevice::GetAPI>("GetAPI");
		gpuDevice.Function<&GPUDevice::WaitIdle>("WaitIdle");
		gpuDevice.Function<&GPUDevice::CreateSwapchain>("CreateSwapchain", "desc");
		gpuDevice.Function<&GPUDevice::CreateRenderPass>("CreateRenderPass", "desc");
		gpuDevice.Function<&GPUDevice::CreateFramebuffer>("CreateFramebuffer", "desc");
		gpuDevice.Function<&GPUDevice::CreateCommandBuffer>("CreateCommandBuffer", "queueType");
		gpuDevice.Function<&GPUDevice::CreateBuffer>("CreateBuffer", "desc");
		gpuDevice.Function<&GPUDevice::CreateTexture>("CreateTexture", "desc");
		gpuDevice.Function<&GPUDevice::CreateTextureView>("CreateTextureView", "desc");
		gpuDevice.Function<&GPUDevice::CreateSampler>("CreateSampler", "desc");
		gpuDevice.Function<&GPUDevice::CreateGraphicsPipeline>("CreateGraphicsPipeline", "desc");
		gpuDevice.Function<&GPUDevice::CreateComputePipeline>("CreateComputePipeline", "desc");
		gpuDevice.Function<&GPUDevice::CreateRayTracingPipeline>("CreateRayTracingPipeline", "desc");
		gpuDevice.Function<static_cast<GPUDescriptorSet*(GPUDevice::*)(const DescriptorSetDesc&)>(&GPUDevice::CreateDescriptorSet)>("CreateDescriptorSet", "desc");
		gpuDevice.Function<static_cast<GPUDescriptorSet*(GPUDevice::*)(RID, StringView, u32)>(&GPUDevice::CreateDescriptorSet)>("CreateDescriptorSetFromShader", "shader", "variant", "set");
		gpuDevice.Function<&GPUDevice::CreateQueryPool>("CreateQueryPool", "desc");
		gpuDevice.Function<&GPUDevice::CreateBottomLevelAS>("CreateBottomLevelAS", "desc");
		gpuDevice.Function<&GPUDevice::CreateTopLevelAS>("CreateTopLevelAS", "desc");
		gpuDevice.Function<static_cast<usize(GPUDevice::*)(const BottomLevelASDesc&)>(&GPUDevice::GetBottomLevelASSize)>("GetBottomLevelASSize", "desc");
		gpuDevice.Function<static_cast<usize(GPUDevice::*)(const TopLevelASDesc&)>(&GPUDevice::GetTopLevelASSize)>("GetTopLevelASSize", "desc");
		gpuDevice.Function<static_cast<usize(GPUDevice::*)(const BottomLevelASDesc&)>(&GPUDevice::GetAccelerationStructureBuildScratchSize)>("GetBottomLevelASBuildScratchSize", "desc");
		gpuDevice.Function<static_cast<usize(GPUDevice::*)(const TopLevelASDesc&)>(&GPUDevice::GetAccelerationStructureBuildScratchSize)>("GetTopLevelASBuildScratchSize", "desc");

		auto gpuCommandBuffer = Reflection::Type<GPUCommandBuffer>();
		gpuCommandBuffer.Function<&GPUCommandBuffer::Begin>("Begin");
		gpuCommandBuffer.Function<&GPUCommandBuffer::End>("End");
		gpuCommandBuffer.Function<&GPUCommandBuffer::Reset>("Reset");
		gpuCommandBuffer.Function<&GPUCommandBuffer::SetViewport>("SetViewport", "viewportInfo");
		gpuCommandBuffer.Function<&GPUCommandBuffer::SetScissor>("SetScissor", "position", "size");
		gpuCommandBuffer.Function<&GPUCommandBuffer::BindPipeline>("BindPipeline", "pipeline");
		gpuCommandBuffer.Function<static_cast<void(GPUCommandBuffer::*)(GPUPipeline*, u32, GPUDescriptorSet*)>(&GPUCommandBuffer::BindDescriptorSet)>("BindDescriptorSet", "pipeline", "setIndex", "descriptorSet");
		gpuCommandBuffer.Function<static_cast<void(GPUCommandBuffer::*)(GPUPipeline*, u32, GPUDescriptorSet*, Span<u32>)>(&GPUCommandBuffer::BindDescriptorSet)>("BindDescriptorSet", "pipeline", "setIndex", "descriptorSet", "dynamicOffsets");
		gpuCommandBuffer.Function<&GPUCommandBuffer::BindVertexBuffer>("BindVertexBuffer", "firstBinding", "buffers", "offset");
		gpuCommandBuffer.Function<&GPUCommandBuffer::BindIndexBuffer>("BindIndexBuffer", "buffer", "offset", "indexType");
		gpuCommandBuffer.Function<&GPUCommandBuffer::SetTexture>("SetTexture", "pipeline", "set", "binding", "texture", "arrayElement");
		//gpuCommandBuffer.Function<&GPUCommandBuffer::SetBuffer>("SetBuffer", "pipeline", "set", "binding", "buffer", "offset", "range");
		gpuCommandBuffer.Function<&GPUCommandBuffer::SetSampler>("SetSampler", "pipeline", "set", "binding", "sampler");
		gpuCommandBuffer.Function<&GPUCommandBuffer::SetTextureView>("SetTextureView", "pipeline", "set", "binding", "textureView", "arrayElement");
		gpuCommandBuffer.Function<&GPUCommandBuffer::SetAccelerationStructure>("SetAccelerationStructure", "pipeline", "set", "binding", "topLevelAS");
		gpuCommandBuffer.Function<&GPUCommandBuffer::Draw>("Draw", "vertexCount", "instanceCount", "firstVertex", "firstInstance");
		gpuCommandBuffer.Function<&GPUCommandBuffer::DrawIndexed>("DrawIndexed", "indexCount", "instanceCount", "firstIndex", "vertexOffset", "firstInstance");
		gpuCommandBuffer.Function<&GPUCommandBuffer::DrawIndirect>("DrawIndirect", "buffer", "offset", "drawCount", "stride");
		gpuCommandBuffer.Function<&GPUCommandBuffer::DrawIndexedIndirect>("DrawIndexedIndirect", "buffer", "offset", "drawCount", "stride");
		gpuCommandBuffer.Function<&GPUCommandBuffer::Dispatch>("Dispatch", "groupCountX", "groupCountY", "groupCountZ");
		gpuCommandBuffer.Function<&GPUCommandBuffer::DispatchIndirect>("DispatchIndirect", "buffer", "offset");
		gpuCommandBuffer.Function<&GPUCommandBuffer::TraceRays>("TraceRays", "pipeline", "width", "height", "depth");
		gpuCommandBuffer.Function<&GPUCommandBuffer::BuildBottomLevelAS>("BuildBottomLevelAS", "bottomLevelAS", "buildInfo");
		gpuCommandBuffer.Function<&GPUCommandBuffer::BuildTopLevelAS>("BuildTopLevelAS", "topLevelAS", "buildInfo");
		gpuCommandBuffer.Function<static_cast<void(GPUCommandBuffer::*)(GPUBottomLevelAS*, GPUBottomLevelAS*, bool)>(&GPUCommandBuffer::CopyAccelerationStructure)>("CopyBottomLevelAS", "src", "dst", "compress");
		gpuCommandBuffer.Function<static_cast<void(GPUCommandBuffer::*)(GPUTopLevelAS*, GPUTopLevelAS*, bool)>(&GPUCommandBuffer::CopyAccelerationStructure)>("CopyTopLevelAS", "src", "dst", "compress");
		gpuCommandBuffer.Function<&GPUCommandBuffer::BeginRenderPass>("BeginRenderPass", "info");
		gpuCommandBuffer.Function<&GPUCommandBuffer::EndRenderPass>("EndRenderPass");
		gpuCommandBuffer.Function<&GPUCommandBuffer::CopyBuffer>("CopyBuffer", "srcBuffer", "dstBuffer", "size", "srcOffset", "dstOffset");
		gpuCommandBuffer.Function<&GPUCommandBuffer::CopyBufferToTexture>("CopyBufferToTexture", "copy");
		gpuCommandBuffer.Function<&GPUCommandBuffer::CopyTextureToBuffer>("CopyTextureToBuffer", "copy");
		gpuCommandBuffer.Function<&GPUCommandBuffer::CopyTexture>("CopyTexture", "copy");
		gpuCommandBuffer.Function<&GPUCommandBuffer::BlitTexture>("BlitTexture", "blit");
		gpuCommandBuffer.Function<&GPUCommandBuffer::FillBuffer>("FillBuffer", "buffer", "offset", "size", "data");
		gpuCommandBuffer.Function<&GPUCommandBuffer::ClearColorTexture>("ClearColorTexture", "texture", "clearValue", "mipLevel", "arrayLayer");
		gpuCommandBuffer.Function<&GPUCommandBuffer::ClearDepthStencilTexture>("ClearDepthStencilTexture", "texture", "depth", "stencil", "mipLevel", "arrayLayer");
		gpuCommandBuffer.Function<static_cast<void(GPUCommandBuffer::*)(const BufferBarrierDesc&)>(&GPUCommandBuffer::ResourceBarrier)>("ResourceBarrierBuffer", "barrier");
		gpuCommandBuffer.Function<static_cast<void(GPUCommandBuffer::*)(const TextureBarrierDesc&)>(&GPUCommandBuffer::ResourceBarrier)>("ResourceBarrierTexture", "barrier");
		gpuCommandBuffer.Function<static_cast<void(GPUCommandBuffer::*)(GPUBottomLevelAS*, ResourceState, ResourceState)>(&GPUCommandBuffer::ResourceBarrier)>("ResourceBarrierBottomLevelAS", "bottomLevelAS", "oldState", "newState");
		gpuCommandBuffer.Function<static_cast<void(GPUCommandBuffer::*)(GPUTopLevelAS*, ResourceState, ResourceState)>(&GPUCommandBuffer::ResourceBarrier)>("ResourceBarrierTopLevelAS", "topLevelAS", "oldState", "newState");
		gpuCommandBuffer.Function<&GPUCommandBuffer::MemoryBarrier>("MemoryBarrier");
		gpuCommandBuffer.Function<&GPUCommandBuffer::BeginQuery>("BeginQuery", "queryPool", "query");
		gpuCommandBuffer.Function<&GPUCommandBuffer::EndQuery>("EndQuery", "queryPool", "query");
		gpuCommandBuffer.Function<&GPUCommandBuffer::ResetQueryPool>("ResetQueryPool", "queryPool", "firstQuery", "queryCount");
		gpuCommandBuffer.Function<&GPUCommandBuffer::WriteTimestamp>("WriteTimestamp", "queryPool", "query");
		gpuCommandBuffer.Function<&GPUCommandBuffer::CopyQueryPoolResults>("CopyQueryPoolResults", "queryPool", "firstQuery", "queryCount", "dstBuffer", "dstOffset", "stride");
		gpuCommandBuffer.Function<&GPUCommandBuffer::BeginDebugMarker>("BeginDebugMarker", "name", "color");
		gpuCommandBuffer.Function<&GPUCommandBuffer::EndDebugMarker>("EndDebugMarker");
		gpuCommandBuffer.Function<&GPUCommandBuffer::InsertDebugMarker>("InsertDebugMarker", "name", "color");
		gpuCommandBuffer.Function<&GPUCommandBuffer::Destroy>("Destroy");
	}

	void RegisterGraphicsCommonTypes()
	{
		auto lightType = Reflection::Type<LightType>();
		lightType.Value<LightType::Directional>();
		lightType.Value<LightType::Point>();
		lightType.Value<LightType::Spot>();


	}

	void RegisterRenderPipelineBaseTypes()
	{
		auto renderPipelinePassType = Reflection::Type<RenderPipelinePassType>();
		renderPipelinePassType.Value<RenderPipelinePassType::Other>("Other");
		renderPipelinePassType.Value<RenderPipelinePassType::Graphics>("Graphics");
		renderPipelinePassType.Value<RenderPipelinePassType::Compute>("Compute");
		renderPipelinePassType.Value<RenderPipelinePassType::Raytrace>("Raytrace");

		auto renderPipelineTextureAccess = Reflection::Type<RenderPipelineTextureAccess>();
		renderPipelineTextureAccess.Value<RenderPipelineTextureAccess::None>("None");
		renderPipelineTextureAccess.Value<RenderPipelineTextureAccess::Read>("Read");
		renderPipelineTextureAccess.Value<RenderPipelineTextureAccess::Write>("Write");
		renderPipelineTextureAccess.Value<RenderPipelineTextureAccess::ReadWrite>("ReadWrite");

		auto renderPipelineResourceType = Reflection::Type<RenderPipelineResourceType>();
		renderPipelineResourceType.Value<RenderPipelineResourceType::None>("None");
		renderPipelineResourceType.Value<RenderPipelineResourceType::Texture>("Texture");
		renderPipelineResourceType.Value<RenderPipelineResourceType::TextureView>("TextureView");
		renderPipelineResourceType.Value<RenderPipelineResourceType::Attachment>("Attachment");
		renderPipelineResourceType.Value<RenderPipelineResourceType::Buffer>("Buffer");
		renderPipelineResourceType.Value<RenderPipelineResourceType::Reference>("Reference");
		renderPipelineResourceType.Value<RenderPipelineResourceType::Instance>("Instance");

		auto renderPipelineResource = Reflection::Type<RenderPipelineResource>();
		renderPipelineResource.Field<&RenderPipelineResource::name>("name");
		renderPipelineResource.Field<&RenderPipelineResource::type>("type");
		renderPipelineResource.Field<&RenderPipelineResource::format>("format");
		renderPipelineResource.Field<&RenderPipelineResource::extent>("extent");
		renderPipelineResource.Field<&RenderPipelineResource::scale>("scale");
		renderPipelineResource.Field<&RenderPipelineResource::arrayLayers>("arrayLayers");
		renderPipelineResource.Field<&RenderPipelineResource::samples>("samples");
		renderPipelineResource.Field<&RenderPipelineResource::mipLevels>("mipLevels");
		renderPipelineResource.Field<&RenderPipelineResource::textureUsage>("textureUsage");
		renderPipelineResource.Field<&RenderPipelineResource::textureName>("textureName");
		renderPipelineResource.Field<&RenderPipelineResource::viewType>("viewType");
		renderPipelineResource.Field<&RenderPipelineResource::baseMipLevel>("baseMipLevel");
		renderPipelineResource.Field<&RenderPipelineResource::levelCount>("levelCount");
		renderPipelineResource.Field<&RenderPipelineResource::baseArrayLayer>("baseArrayLayer");
		renderPipelineResource.Field<&RenderPipelineResource::layerCount>("layerCount");
		renderPipelineResource.Field<&RenderPipelineResource::clearColor>("clearColor");
		renderPipelineResource.Field<&RenderPipelineResource::size>("size");
		renderPipelineResource.Field<&RenderPipelineResource::usage>("usage");
		renderPipelineResource.Field<&RenderPipelineResource::hostVisible>("hostVisible");
		renderPipelineResource.Field<&RenderPipelineResource::persistentMapped>("persistentMapped");
		renderPipelineResource.Field<&RenderPipelineResource::instanceTypeId>("instanceTypeId");

		auto renderPipelinePassDependency = Reflection::Type<RenderPipelinePassDependency>();
		renderPipelinePassDependency.Field<&RenderPipelinePassDependency::name>("name");
		renderPipelinePassDependency.Field<&RenderPipelinePassDependency::access>("access");

		auto renderPipelinePassSetup = Reflection::Type<RenderPipelinePassSetup>();
		renderPipelinePassSetup.Field<&RenderPipelinePassSetup::type>("type");
		renderPipelinePassSetup.Field<&RenderPipelinePassSetup::invertViewport>("invertViewport");
		renderPipelinePassSetup.Field<&RenderPipelinePassSetup::dependencies>("dependencies");

		auto renderPipelineModuleSetup = Reflection::Type<RenderPipelineModuleSetup>();
		renderPipelineModuleSetup.Field<&RenderPipelineModuleSetup::passes>("passes");

		auto renderPipelineSetup = Reflection::Type<RenderPipelineSetup>();
		renderPipelineSetup.Field<&RenderPipelineSetup::modules>("modules");

		auto renderPipelineContextSettings = Reflection::Type<RenderPipelineContextSettings>();
		renderPipelineContextSettings.Field<&RenderPipelineContextSettings::initialOutputSize>("initialOutputSize");

		// Base classes with virtual methods
		auto renderPipelinePass = Reflection::Type<RenderPipelinePass>();
		renderPipelinePass.Field<&RenderPipelinePass::context>("context");
		renderPipelinePass.Field<&RenderPipelinePass::renderPass>("renderPass");
		renderPipelinePass.Function<&RenderPipelinePass::GetPassSetup>("GetPassSetup").Attribute<VirtualMethod>();
		renderPipelinePass.Function<&RenderPipelinePass::Create>("Create").Attribute<VirtualMethod>();
		renderPipelinePass.Function<&RenderPipelinePass::Init>("Init").Attribute<VirtualMethod>();
		renderPipelinePass.Function<&RenderPipelinePass::Update>("Update").Attribute<VirtualMethod>();
		renderPipelinePass.Function<&RenderPipelinePass::Render>("Render", "objects", "cmd").Attribute<VirtualMethod>();
		renderPipelinePass.Function<&RenderPipelinePass::Destroy>("Destroy").Attribute<VirtualMethod>();
		renderPipelinePass.Function<&RenderPipelinePass::OnResize>("OnResize", "size").Attribute<VirtualMethod>();

		auto renderPipelineModule = Reflection::Type<RenderPipelineModule>();
		renderPipelineModule.Field<&RenderPipelineModule::context>("context");
		renderPipelineModule.Function<&RenderPipelineModule::GetResources>("GetResources").Attribute<VirtualMethod>();
		renderPipelineModule.Function<&RenderPipelineModule::GetSetup>("GetSetup").Attribute<VirtualMethod>();
		renderPipelineModule.Function<&RenderPipelineModule::Init>("Init").Attribute<VirtualMethod>();
		renderPipelineModule.Function<&RenderPipelineModule::Destroy>("Destroy").Attribute<VirtualMethod>();
		renderPipelineModule.Function<&RenderPipelineModule::Update>("Update", "objects").Attribute<VirtualMethod>();

		auto renderPipeline = Reflection::Type<RenderPipeline>();
		renderPipeline.Function<&RenderPipeline::GetPipelineSetup>("GetPipelineSetup").Attribute<VirtualMethod>();

		Reflection::Type<RenderPipelineContext>();
	}

	void RegisterPipelineTypes();

	void RegisterGraphicsTypes()
	{
		RegisterDeviceTypes();
		RegisterGraphicsCommonTypes();

		Reflection::Type<Graphics>();

		auto gpuWorkType = Reflection::Type<GpuWorkType>();
		gpuWorkType.Value<GpuWorkType::Graphics>("Graphics");
		gpuWorkType.Value<GpuWorkType::Compute>("Compute");
		gpuWorkType.Value<GpuWorkType::Transfer>("Transfer");

		auto projection = Reflection::Type<Projection>();
		projection.Value<Projection::Perspective>();
		projection.Value<Projection::Orthogonal>();

		Reflection::Type<ShaderStageInfo>();
		Reflection::Type<FontAtlasData>();
		Reflection::Type<FontMetrics>();
		Reflection::Type<FontKerning>();
		Reflection::Type<FontGlyph>();
		Reflection::Type<FontResourceData>();

		auto primitive = Reflection::Type<MeshPrimitive>();
		primitive.Field<&MeshPrimitive::firstIndex>("firstIndex");
		primitive.Field<&MeshPrimitive::indexCount>("indexCount");
		primitive.Field<&MeshPrimitive::materialIndex>("materialIndex");
		//
		auto materialType = Reflection::Type<MaterialResource::MaterialType>();
		materialType.Value<MaterialResource::MaterialType::Opaque>("Opaque");
		materialType.Value<MaterialResource::MaterialType::SkyboxEquirectangular>("SkyboxEquirectangular");
		//
		auto alphaMode = Reflection::Type<MaterialResource::MaterialAlphaMode>();
		alphaMode.Value<MaterialResource::MaterialAlphaMode::None>("None");
		alphaMode.Value<MaterialResource::MaterialAlphaMode::Opaque>("Opaque");
		alphaMode.Value<MaterialResource::MaterialAlphaMode::Mask>("Mask");
		alphaMode.Value<MaterialResource::MaterialAlphaMode::Blend>("Blend");
		//
		auto textureChannel = Reflection::Type<TextureChannel>();
		textureChannel.Value<TextureChannel::Red>("Red");
		textureChannel.Value<TextureChannel::Green>("Green");
		textureChannel.Value<TextureChannel::Blue>("Blue");
		textureChannel.Value<TextureChannel::Alpha>("Alpha");

		auto ambientLightSource = Reflection::Type<AmbientLightSource>();
		ambientLightSource.Value<AmbientLightSource::Skybox>("Skybox");
		ambientLightSource.Value<AmbientLightSource::Color>("Color");
		ambientLightSource.Value<AmbientLightSource::Disabled>("Disabled");

		auto reflectedLightSource = Reflection::Type<ReflectedLightSource>();
		reflectedLightSource.Value<ReflectedLightSource::Skybox>("Skybox");
		reflectedLightSource.Value<ReflectedLightSource::Disabled>("Disabled");

		Resources::Type<ShaderVariantResource>()
			.Field<ShaderVariantResource::Name>(ResourceFieldType::String)
			.Field<ShaderVariantResource::Spriv>(ResourceFieldType::Blob)
			.Field<ShaderVariantResource::PipelineDesc>(ResourceFieldType::SubObject)
			.Field<ShaderVariantResource::Stages>(ResourceFieldType::SubObjectList)
			.Build();

		Resources::Type<ShaderResource>()
			.Field<ShaderResource::Name>(ResourceFieldType::String)
			.Field<ShaderResource::Variants>(ResourceFieldType::SubObjectList)
			.Field<ShaderResource::RayHitGroup>(ResourceFieldType::UInt)
			.Build();

		Resources::Type<TextureImageResource>()
			.Field<TextureImageResource::Extent>(ResourceFieldType::Vec2)
			.Field<TextureImageResource::Mip>(ResourceFieldType::UInt)
			.Field<TextureImageResource::ArrayLayer>(ResourceFieldType::UInt)
			.Field<TextureImageResource::DataOffset>(ResourceFieldType::UInt)
			.Field<TextureImageResource::DataSize>(ResourceFieldType::UInt)
			.Field<TextureImageResource::UncompressedSize>(ResourceFieldType::UInt)
			.Build();

		Resources::Type<TextureResource>()
			.Field<TextureResource::Name>(ResourceFieldType::String)
			.Field<TextureResource::Format>(ResourceFieldType::Enum, TypeInfo<Format>::ID())
			.Field<TextureResource::MipLevels>(ResourceFieldType::UInt)
			.Field<TextureResource::WrapMode>(ResourceFieldType::Enum, TypeInfo<AddressMode>::ID())
			.Field<TextureResource::FilterMode>(ResourceFieldType::Enum, TypeInfo<FilterMode>::ID())
			.Field<TextureResource::CompressionMode>(ResourceFieldType::Enum, TypeInfo<CompressionMode>::ID())
			.Field<TextureResource::TotalUncompressedSize>(ResourceFieldType::UInt)
			.Field<TextureResource::Images>(ResourceFieldType::SubObjectList)
			.Field<TextureResource::PixelData>(ResourceFieldType::Buffer)
			.Build();

		Resources::Type<MaterialResource>()
			.Field<MaterialResource::Name>(ResourceFieldType::String)
			.Field<MaterialResource::Type>(ResourceFieldType::Enum, TypeInfo<MaterialResource::MaterialType>::ID())
			.Field<MaterialResource::BaseColor>(ResourceFieldType::Color)
			.Field<MaterialResource::BaseColorTexture>(ResourceFieldType::Reference, TypeInfo<TextureResource>::ID())
			.Field<MaterialResource::NormalTexture>(ResourceFieldType::Reference, TypeInfo<TextureResource>::ID())
			.Field<MaterialResource::NormalMultiplier>(ResourceFieldType::Float)
			.Field<MaterialResource::Metallic>(ResourceFieldType::Float)
			.Field<MaterialResource::MetallicTexture>(ResourceFieldType::Reference, TypeInfo<TextureResource>::ID())
			.Field<MaterialResource::MetallicTextureChannel>(ResourceFieldType::Enum, TypeInfo<TextureChannel>::ID())
			.Field<MaterialResource::Roughness>(ResourceFieldType::Float)
			.Field<MaterialResource::RoughnessTexture>(ResourceFieldType::Reference, TypeInfo<TextureResource>::ID())
			.Field<MaterialResource::RoughnessTextureChannel>(ResourceFieldType::Enum, TypeInfo<TextureChannel>::ID())
			.Field<MaterialResource::EmissiveColor>(ResourceFieldType::Color)
			.Field<MaterialResource::EmissiveFactor>(ResourceFieldType::Float)
			.Field<MaterialResource::EmissiveTexture>(ResourceFieldType::Reference, TypeInfo<TextureResource>::ID())
			.Field<MaterialResource::OcclusionTexture>(ResourceFieldType::Reference, TypeInfo<TextureResource>::ID())
			.Field<MaterialResource::OcclusionStrength>(ResourceFieldType::Float)
			.Field<MaterialResource::OcclusionTextureChannel>(ResourceFieldType::Enum, TypeInfo<TextureChannel>::ID())
			.Field<MaterialResource::AlphaCutoff>(ResourceFieldType::Float)
			.Field<MaterialResource::AlphaMode>(ResourceFieldType::Enum, TypeInfo<MaterialResource::MaterialAlphaMode>::ID())
			.Field<MaterialResource::UvScale>(ResourceFieldType::Vec2)
			.Field<MaterialResource::SphericalTexture>(ResourceFieldType::Reference, TypeInfo<TextureResource>::ID())
			.Field<MaterialResource::Exposure>(ResourceFieldType::Float)
			.Field<MaterialResource::BackgroundColor>(ResourceFieldType::Color)
			.Build();

		Resources::Type<MaterialGraphPinValueResource>()
			.Field<MaterialGraphPinValueResource::PinIndex>(ResourceFieldType::UInt)
			.Field<MaterialGraphPinValueResource::Value>(ResourceFieldType::Vec4)
			.Build();

		Resources::Type<MaterialGraphNodeResource>()
			.Field<MaterialGraphNodeResource::Type>(ResourceFieldType::String)
			.Field<MaterialGraphNodeResource::Position>(ResourceFieldType::Vec2)
			.Field<MaterialGraphNodeResource::Value>(ResourceFieldType::Vec4)
			.Field<MaterialGraphNodeResource::Texture>(ResourceFieldType::Reference, TypeInfo<TextureResource>::ID())
			.Field<MaterialGraphNodeResource::InputValues>(ResourceFieldType::SubObjectList, TypeInfo<MaterialGraphPinValueResource>::ID())
			.Build();

		Resources::Type<MaterialGraphConnectionResource>()
			.Field<MaterialGraphConnectionResource::OutputNode>(ResourceFieldType::Reference, TypeInfo<MaterialGraphNodeResource>::ID())
			.Field<MaterialGraphConnectionResource::OutputPin>(ResourceFieldType::UInt)
			.Field<MaterialGraphConnectionResource::InputNode>(ResourceFieldType::Reference, TypeInfo<MaterialGraphNodeResource>::ID())
			.Field<MaterialGraphConnectionResource::InputPin>(ResourceFieldType::UInt)
			.Build();

		Resources::Type<MaterialGraphResource>()
			.Field<MaterialGraphResource::Name>(ResourceFieldType::String)
			.Field<MaterialGraphResource::Nodes>(ResourceFieldType::SubObjectList, TypeInfo<MaterialGraphNodeResource>::ID())
			.Field<MaterialGraphResource::Connections>(ResourceFieldType::SubObjectList, TypeInfo<MaterialGraphConnectionResource>::ID())
			.Field<MaterialGraphResource::OutputNode>(ResourceFieldType::Reference, TypeInfo<MaterialGraphNodeResource>::ID())
			.Build();

		Resources::Type<VertexAttributeResource>()
			.Field<VertexAttributeResource::Name>(ResourceFieldType::String)
			.Field<VertexAttributeResource::Offset>(ResourceFieldType::UInt)
			.Field<VertexAttributeResource::Size>(ResourceFieldType::UInt)
			.Build();

		Resources::Type<VertexLayoutResource>()
			.Field<VertexLayoutResource::Attributes>(ResourceFieldType::SubObjectList)
			.Field<VertexLayoutResource::Stride>(ResourceFieldType::UInt)
			.Build();

		Resources::Type<MeshLodResource>()
			.Field<MeshLodResource::LodNumber>(ResourceFieldType::UInt)
			.Field<MeshLodResource::VerticesOffset>(ResourceFieldType::UInt)
			.Field<MeshLodResource::VerticesCount>(ResourceFieldType::UInt)
			.Field<MeshLodResource::IndicesOffset>(ResourceFieldType::UInt)
			.Field<MeshLodResource::IndicesCount>(ResourceFieldType::UInt)
			.Field<MeshLodResource::PrimitiveCount>(ResourceFieldType::UInt)
			.Field<MeshLodResource::PrimitiveOffset>(ResourceFieldType::UInt)
			.Field<MeshLodResource::Distance>(ResourceFieldType::Float)
			.Field<MeshLodResource::ScreenSize>(ResourceFieldType::Float)
			.Build();

		Resources::Type<MeshResource>()
			.Field<MeshResource::Name>(ResourceFieldType::String)
			.Field<MeshResource::Materials>(ResourceFieldType::ReferenceArray)
			.Field<MeshResource::Skin>(ResourceFieldType::SubObject)
			.Field<MeshResource::AABBMin>(ResourceFieldType::Vec3)
			.Field<MeshResource::AABBMax>(ResourceFieldType::Vec3)
			.Field<MeshResource::LightmapSizeHint>(ResourceFieldType::Vec2)
			.Field<MeshResource::VertexLayout>(ResourceFieldType::SubObject)
			.Field<MeshResource::MeshLODs>(ResourceFieldType::SubObjectList)
			.Field<MeshResource::MeshData>(ResourceFieldType::Buffer)
			.Build();

		Resources::Type<AnimationKeyFrameResource>()
			.Field<AnimationKeyFrameResource::Time>(ResourceFieldType::Float)
			.Field<AnimationKeyFrameResource::Position>(ResourceFieldType::Vec3)
			.Field<AnimationKeyFrameResource::Rotation>(ResourceFieldType::Quat)
			.Field<AnimationKeyFrameResource::Scale>(ResourceFieldType::Vec3)
			.Build();

		Resources::Type<AnimationChannelResource>()
			.Field<AnimationChannelResource::Name>(ResourceFieldType::String)
			.Field<AnimationChannelResource::KeyFrames>(ResourceFieldType::SubObjectList)
			.Field<AnimationChannelResource::BufferOffset>(ResourceFieldType::UInt)
			.Build();

		Resources::Type<AnimationClipResource>()
			.Field<AnimationClipResource::Name>(ResourceFieldType::String)
			.Field<AnimationClipResource::Duration>(ResourceFieldType::Float)
			.Field<AnimationClipResource::NumFrames>(ResourceFieldType::UInt)
			.Field<AnimationClipResource::FrameRate>(ResourceFieldType::Float)
			.Field<AnimationClipResource::TimeBegin>(ResourceFieldType::Float)
			.Field<AnimationClipResource::TimeEnd>(ResourceFieldType::Float)
			.Field<AnimationClipResource::Channels>(ResourceFieldType::SubObjectList)
			.Field<AnimationClipResource::KeyFramesBuffer>(ResourceFieldType::Buffer)
			.Build();

		Resources::Type<FontResource>()
			.Field<FontResource::Name>(ResourceFieldType::String)
			.Field<FontResource::FontData>(ResourceFieldType::Blob)
			.Field<FontResource::FontDataUncompressedSize>(ResourceFieldType::UInt)
			.Build();

		Resources::Type<GraphicsSettings>()
			.Field<GraphicsSettings::EnableValidationLayers>(ResourceFieldType::Bool)
			.Attribute<EditableSettings>(EditableSettings{
				.path = "Engine/Rendering",
				.type = TypeInfo<ProjectSettings>::ID(),
				.order = 15
			})
			.Build();

		Resources::Type<DCCAsset>()
			.Field<DCCAsset::Name>(ResourceFieldType::String)
			.Field<DCCAsset::Meshes>(ResourceFieldType::SubObjectList)
			.Field<DCCAsset::Textures>(ResourceFieldType::SubObjectList)
			.Field<DCCAsset::Materials>(ResourceFieldType::SubObjectList)
			.Field<DCCAsset::AnimationClips>(ResourceFieldType::SubObjectList)
			.Field<DCCAsset::RootEntity>(ResourceFieldType::SubObject)
			.Build();

		Resources::Type<SkinBindResource>()
			.Field<SkinBindResource::Pose>(ResourceFieldType::Mat4)
			.Build();

		Resources::Type<SkinResource>()
			.Field<SkinResource::Name>(ResourceFieldType::String)
			.Field<SkinResource::Binds>(ResourceFieldType::SubObjectList)
			.Build();

		//default material values
		{
			RID rid = Resources::Create<MaterialResource>();
			ResourceObject object = Resources::Write(rid);

			object.SetEnum(MaterialResource::Type, MaterialResource::MaterialType::Opaque);
			object.SetColor(MaterialResource::BaseColor, Color::WHITE);
			object.SetFloat(MaterialResource::NormalMultiplier, 1.0);
			object.SetFloat(MaterialResource::Metallic, 0.0);
			object.SetFloat(MaterialResource::Roughness, 1.0);

			object.SetColor(MaterialResource::EmissiveColor, Color::BLACK);
			object.SetFloat(MaterialResource::EmissiveFactor, 1.0);

			object.SetFloat(MaterialResource::OcclusionStrength, 1.0);

			object.SetFloat(MaterialResource::AlphaCutoff, 0.5);
			object.SetVec2(MaterialResource::UvScale, Vec2(1.0, 1.0));

			object.SetFloat(MaterialResource::Exposure, 1.0);

			object.Commit();
			Resources::FindType<MaterialResource>()->SetDefaultValue(rid);
		}

		RegisterRenderPipelineBaseTypes();
		RegisterPipelineTypes();
		RegisterAnimationTypes();
	}
}