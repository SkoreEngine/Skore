#include "Skore/Graphics/Pipelines/Modules/XeGTAORenderModule.hpp"

#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/SSAOComponent.hpp"

namespace Skore
{
#define XE_GTAO_DEFAULT_RADIUS_MULTIPLIER               (1.457f  )  // allows us to use different value as compared to ground truth radius to counter inherent screen space biases
#define XE_GTAO_DEFAULT_FALLOFF_RANGE                   (0.615f  )  // distant samples contribute less
#define XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER       (2.0f    )  // small crevices more important than big surfaces
#define XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION      (0.0f    )  // the new 'thickness heuristic' approach
#define XE_GTAO_DEFAULT_FINAL_VALUE_POWER               (2.2f    )  // modifies the final ambient occlusion value using power function - this allows some of the above heuristics to do different things
#define XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET       (3.30f   )  // main trade-off between performance (memory bandwidth) and quality (temporal stability is the first affected, thin objects next)

#define XE_GTAO_OCCLUSION_TERM_SCALE                    (1.5f)      // for packing in UNORM (because raw, pre-denoised occlusion term can overshoot 1 but will later average out to 1)

#define XE_GTAO_NUMTHREADS_X                        8                   // these can be changed
#define XE_GTAO_NUMTHREADS_Y                        8                   // these can be changed

	struct XeGTAOSettings
	{
		int   QualityLevel = 2;  // 0: low; 1: medium; 2: high; 3: ultra
		int   DenoisePasses = 1; // 0: disabled; 1: sharp; 2: medium; 3: soft
		float Radius = 0.3f;     // [0.0,  ~ ]   World (view) space size of the occlusion sphere.

		// auto-tune-d settings
		float RadiusMultiplier = XE_GTAO_DEFAULT_RADIUS_MULTIPLIER;
		float FalloffRange = XE_GTAO_DEFAULT_FALLOFF_RANGE;
		float SampleDistributionPower = XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER;
		float ThinOccluderCompensation = XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION;
		float FinalValuePower = XE_GTAO_DEFAULT_FINAL_VALUE_POWER;
		float DepthMIPSamplingOffset = XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET;
	};

	const static XeGTAOSettings XeGTAODefaultSettings = {};


	struct XeGTAOConstants
	{
		IVec2 ViewportSize;
		Vec2  ViewportPixelSize; // .zw == 1.0 / ViewportSize.xy

		Vec2 DepthUnpackConsts;
		Vec2 CameraTanHalfFOV;

		Vec2 NDCToViewMul;
		Vec2 NDCToViewAdd;

		Vec2 NDCToViewMul_x_PixelSize;
		f32  EffectRadius; // world (viewspace) maximum size of the shadow
		f32  EffectFalloffRange;

		f32 RadiusMultiplier;
		i32 DepthMipLevels;
		f32 FinalValuePower;
		f32 DenoiseBlurBeta;

		f32 SampleDistributionPower;
		f32 ThinOccluderCompensation;
		f32 DepthMIPSamplingOffset;
		i32 NoiseIndex; // frameIndex % 64 if using TAA or 0 otherwise

		Mat4 View;
	};

	// If using TAA then set noiseIndex to frameIndex % 64 - otherwise use 0
	inline void XeGTAOUpdateConstants(XeGTAOConstants&    consts,
	                                const XeGTAOSettings& settings,
	                                const RenderPipelineContext* context,
	                                i32 depthMipLevels
	)
	{

		i32 viewportWidth = context->GetOutputSize().width;
		i32 viewportHeight = context->GetOutputSize().height;
		u32 frameCounter = App::Frame();

		consts.ViewportSize = {viewportWidth, viewportHeight};
		consts.ViewportPixelSize = {1.0f / (float)viewportWidth, 1.0f / (float)viewportHeight};

		float depthLinearizeMul = -context->camera.projection.a[3 + 2 * 4];
		float depthLinearizeAdd = context->camera.projection.a[2 + 2 * 4];

		// correct the handedness issue. need to make sure this below is correct, but I think it is.
		if (depthLinearizeMul * depthLinearizeAdd < 0)
			depthLinearizeAdd = -depthLinearizeAdd;
		consts.DepthUnpackConsts = {depthLinearizeMul, depthLinearizeAdd};

		float tanHalfFOVY = 1.0f / (context->camera.projection.a[1 + 1 * 4]); // = tanf( drawContext.Camera.GetYFOV( ) * 0.5f );
		float tanHalfFOVX = 1.0F / (context->camera.projection.a[0 + 0 * 4]); // = tanHalfFOVY * drawContext.Camera.GetAspect( );
		consts.CameraTanHalfFOV = {tanHalfFOVX, tanHalfFOVY};

		consts.NDCToViewMul = {consts.CameraTanHalfFOV.x * 2.0f, consts.CameraTanHalfFOV.y * -2.0f};
		consts.NDCToViewAdd = {consts.CameraTanHalfFOV.x * -1.0f, consts.CameraTanHalfFOV.y * 1.0f};

		consts.NDCToViewMul_x_PixelSize = {consts.NDCToViewMul.x * consts.ViewportPixelSize.x, consts.NDCToViewMul.y * consts.ViewportPixelSize.y};

		consts.EffectRadius = settings.Radius;

		consts.EffectFalloffRange = settings.FalloffRange;
		consts.DenoiseBlurBeta = (settings.DenoisePasses == 0) ? (1e4f) : (1.2f); // high value disables denoise - more elegant & correct way would be do set all edges to 0

		consts.RadiusMultiplier = settings.RadiusMultiplier;
		consts.SampleDistributionPower = settings.SampleDistributionPower;
		consts.ThinOccluderCompensation = settings.ThinOccluderCompensation;
		consts.FinalValuePower = settings.FinalValuePower;
		consts.DepthMIPSamplingOffset = settings.DepthMIPSamplingOffset;
		consts.NoiseIndex = (settings.DenoisePasses > 0) ? (frameCounter % 64) : (0);
		consts.DepthMipLevels = depthMipLevels;

		consts.View = context->camera.view;
	}

	class XeGTAOSetup : public RenderPipelinePass
	{
	public:
		SK_CLASS(XeGTAOSetup, RenderPipelinePass);

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Other;
			setup.stage = PipelineRenderStage::Indirect;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "XeGTAOConstants", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});
			return setup;
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			GPUTexture* linearDepth = context->GetTexture(LinearDepthMipChainName);
			i32 depthMipLevels = linearDepth ? static_cast<i32>(linearDepth->GetDesc().mipLevels) : 5;

			XeGTAOSettings settings = XeGTAODefaultSettings;
			scene->Iterate<SSAOComponent>([&](SSAOComponent* ssao)
			{
				settings.Radius = ssao->GetRadius();
				settings.FalloffRange = ssao->GetFalloffRange();
				settings.FinalValuePower = ssao->GetFinalValuePower();
				settings.DenoisePasses = ssao->GetDenoisePasses();
			});

			XeGTAOUpdateConstants(*static_cast<XeGTAOConstants*>(context->GetBuffer("XeGTAOConstants")->GetMappedData()),
														settings,
														context,
														depthMipLevels
			);
		}
	};


	class XeGTAOMainPass : public RenderPipelinePass
	{
	public:
		SK_CLASS(XeGTAOMainPass, RenderPipelinePass);

		GPUDescriptorSet* descriptorSet = nullptr;
		GPUPipeline* pipeline = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = PipelineRenderStage::Indirect;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "XeGTAOConstants", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = XeGTAOOutputName, .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "XeGTAOWorkingEdges", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void UpdateDescriptorSet() const
		{
			descriptorSet->UpdateTexture(0, context->GetTexture(XeGTAOOutputName), 0);
			descriptorSet->UpdateTexture(1, context->GetTexture("XeGTAOWorkingEdges"), 0);
			descriptorSet->UpdateBuffer(2, context->GetBuffer("XeGTAOConstants"), 0, sizeof(XeGTAOConstants));
			descriptorSet->UpdateTexture(3, context->GetTexture(LinearDepthMipChainName), 0);
			descriptorSet->UpdateTexture(4, context->GetTexture("GBufferNormals"), 0);
			descriptorSet->UpdateSampler(5, Graphics::GetNearestClampToEdgeSampler());
		}

		void Init() override
		{
			descriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
				.bindings = {
					DescriptorSetLayoutBinding{
						.binding = 0,
						.descriptorType = DescriptorType::StorageImage
					},
					DescriptorSetLayoutBinding{
						.binding = 1,
						.descriptorType = DescriptorType::StorageImage
					},
					DescriptorSetLayoutBinding{
						.binding = 2,
						.descriptorType = DescriptorType::UniformBuffer
					},
					DescriptorSetLayoutBinding{
						.binding = 3,
						.descriptorType = DescriptorType::SampledImage
					},
					DescriptorSetLayoutBinding{
						.binding = 4,
						.descriptorType = DescriptorType::SampledImage
					},
					DescriptorSetLayoutBinding{
						.binding = 5,
						.descriptorType = DescriptorType::Sampler
					},
				},
				.debugName = "XeGTAODescriptorSet"
			});

			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/XeGTAO.shader"),
				.variant = "XrGTAOUltra",
			});

			UpdateDescriptorSet();
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			cmd->BindPipeline(pipeline);
			cmd->BindDescriptorSet(pipeline, 0, descriptorSet, {});
			cmd->Dispatch((context->GetOutputSize().width + (XE_GTAO_NUMTHREADS_X - 1)) / XE_GTAO_NUMTHREADS_X, (context->GetOutputSize().height + (XE_GTAO_NUMTHREADS_Y - 1)) / XE_GTAO_NUMTHREADS_Y, 1);
		}

		void OnResize(Extent size) override
		{
			UpdateDescriptorSet();
		}

		void Destroy() override
		{
			descriptorSet->Destroy();
			pipeline->Destroy();
		}
	};


	class XeGTAORenderModule : public RenderPipelineModule
	{
	public:
		SK_CLASS(XeGTAORenderModule, RenderPipelineModule);


		//enabled only while the scene has an SSAOComponent
		bool IsEnabled() override
		{
			Scene* scene = context->GetScene();
			return scene && scene->HasIterable<SSAOComponent>();
		}

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(XeGTAOSetup));
			setup.passes.EmplaceBack(sktypeid(XeGTAOMainPass));
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{.name = XeGTAOOutputName, .type = RenderPipelineResourceType::Texture, .format = TextureFormat::R8_UINT, .textureUsage = ResourceUsage::ShaderResource |  ResourceUsage::UnorderedAccess});
			resources.EmplaceBack(RenderPipelineResource{.name = "XeGTAOWorkingEdges", .type = RenderPipelineResourceType::Texture, .format = TextureFormat::R8_UNORM, .textureUsage = ResourceUsage::ShaderResource |  ResourceUsage::UnorderedAccess});


			resources.EmplaceBack(RenderPipelineResource{.name = "XeGTAOConstants", .type = RenderPipelineResourceType::Buffer, .size = sizeof(XeGTAOConstants), .usage = ResourceUsage::ConstantBuffer, .persistentMapped = true});

			return resources;
		}

	private:
	};

	void RegisterXeGTAORenderModule()
	{
		Reflection::Type<XeGTAORenderModule>();
		Reflection::Type<XeGTAOSetup>();
		Reflection::Type<XeGTAOMainPass>();

	}
}