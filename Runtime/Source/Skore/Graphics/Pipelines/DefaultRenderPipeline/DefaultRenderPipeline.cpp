#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Pipelines/Modules/CascadeShadowMapModule.hpp"
#include "Skore/Graphics/Pipelines/Modules/XeGTAORenderModule.hpp"
#include "PipelineCommon.hpp"

namespace Skore
{
	TextureFormat mainDepthFormat = TextureFormat::D32_FLOAT;

	struct DeferredRenderModule : RenderPipelineModule
	{
		SK_CLASS(DeferredRenderModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(LightSetupPass));
			setup.passes.EmplaceBack(sktypeid(CullingPass));
			setup.passes.EmplaceBack(sktypeid(DeferredGBufferPass));
			setup.passes.EmplaceBack(sktypeid(DeferredLightingPass));
			setup.passes.EmplaceBack(sktypeid(ForwardPass));
			setup.passes.EmplaceBack(sktypeid(DepthLinearizePass));
			setup.passes.EmplaceBack(sktypeid(CompositePass));
			setup.passes.EmplaceBack(sktypeid(CameraMotionVectorPass));
			setup.passes.EmplaceBack(sktypeid(PostProcessPass));
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			context->SetDepthOutput(OutputDepthName);

			Array<RenderPipelineResource> resources;

			//resources
			resources.EmplaceBack(RenderPipelineResource{.name = "LightInstanceData", .type = RenderPipelineResourceType::Instance, .instanceTypeId = sktypeid(LightPassInstanceData)});
			resources.EmplaceBack(RenderPipelineResource{.name = "SceneCullingData", .type = RenderPipelineResourceType::Instance, .instanceTypeId = sktypeid(SceneCullingData)});

			//GBuffer attachments
			resources.EmplaceBack(RenderPipelineResource{.name = "GBufferAlbedoMetallic", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R8G8B8A8_UNORM});
			resources.EmplaceBack(RenderPipelineResource{.name = "GBufferRoughnessAO", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R8G8_UNORM});
			resources.EmplaceBack(RenderPipelineResource{.name = "GBufferNormals", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16_FLOAT});
			resources.EmplaceBack(RenderPipelineResource{.name = "GBufferEmissive", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R11G11B10_FLOAT});

			//lighting
			resources.EmplaceBack(RenderPipelineResource{.name = "LightAttachment", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16B16A16_FLOAT, .textureUsage = ResourceUsage::ShaderResource |  ResourceUsage::UnorderedAccess | ResourceUsage::CopyDest});
			resources.EmplaceBack(RenderPipelineResource{.name = "ColorAttachment", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16B16A16_FLOAT, .textureUsage = ResourceUsage::ShaderResource |  ResourceUsage::UnorderedAccess | ResourceUsage::CopyDest, .pingPong = true});

			//velocity
			resources.EmplaceBack(RenderPipelineResource{.name = "MotionVector", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16_FLOAT, .textureUsage = ResourceUsage::ShaderResource |  ResourceUsage::UnorderedAccess});

			resources.EmplaceBack(RenderPipelineResource{.name = OutputDepthName, .type = RenderPipelineResourceType::Attachment, .format = mainDepthFormat});
			return resources;
		}
	};

	struct CascadeShadowMapModule : CascadeShadowMapModuleBase
	{
		SK_CLASS(CascadeShadowMapModule, CascadeShadowMapModuleBase);

		TypeID GetCascadeShadowPassTypeId() override
		{
			return sktypeid(CascadeShadowPass);
		}
	};

	struct DefaultRenderPipeline : RenderPipeline
	{
		SK_CLASS(DefaultRenderPipeline, RenderPipeline);

		RenderPipelineSetup GetPipelineSetup() override
		{
			RenderPipelineSetup setup;
			setup.modules.EmplaceBack(sktypeid(CascadeShadowMapModule));
			//setup.modules.EmplaceBack(sktypeid(DepthPrePassModule));
			setup.modules.EmplaceBack(sktypeid(DeferredRenderModule));
			setup.modules.EmplaceBack(sktypeid(IndirectLightingModule));

			//TODO - check if TAA is enabled.
			setup.modules.EmplaceBack(sktypeid(TemporalAntiAliasingModule));
			//TODO - check if FXAA is enabled
			//setup.modules.EmplaceBack(sktypeid(FXAARenderModule));
			setup.modules.EmplaceBack(sktypeid(BloomModule));
			//TODO - check if XeGTAO is enabled
			setup.modules.EmplaceBack(sktypeid(XeGTAORenderModule));

			return setup;
		}
	};

	void RegisterDefaultRenderPipeline()
	{
		Reflection::Type<LightPassInstanceData>();
		Reflection::Type<SceneCullingData>();

		Reflection::Type<CascadeShadowMapModule>();
		Reflection::Type<DeferredRenderModule>();
		Reflection::Type<DefaultRenderPipeline>();
	}
}
