#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Graphics/Pipelines/Modules/CascadeShadowMapModule.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/EnvironmentComponent.hpp"

namespace Skore
{
	static u32 previewMsaaSamples = 4;

	constexpr const char* PreviewColorMSName = "PreviewColorMS";
	constexpr const char* PreviewDepthMSName = "PreviewDepthMS";

	struct PreviewForwardPass : RenderPipelinePass
	{
		SK_CLASS(PreviewForwardPass, RenderPipelinePass);

		Array<GPUPipeline*>    opaquePipelines;
		GPUPipeline*           skyboxPipeline = nullptr;
		GPUDescriptorSet*      iblDescriptorSet = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;
		Scene*                 cachedPipelineOwner = nullptr;

		GPUTexture* boundDiffuseIrradiance = nullptr;
		GPUTexture* boundSpecularMap = nullptr;

		struct MeshPushConstants
		{
			Mat4 world;
			u32  materialIndex;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  ambientFlags;
			Vec3 ambientLight;
			f32  ambientMultiplier;
			f32  reflectionMultiplier;
		};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = PipelineRenderStage::Forward;
			setup.invertViewport = true;

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = PreviewColorMSName, .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = PreviewDepthMSName, .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});

			setup.resolve.EmplaceBack(OutputColorName);
			return setup;
		}

		void Init() override
		{
			lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

			iblDescriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
				.bindings = {
					DescriptorSetLayoutBinding{.binding = 0, .descriptorType = DescriptorType::SampledImage},
					DescriptorSetLayoutBinding{.binding = 1, .descriptorType = DescriptorType::SampledImage},
					DescriptorSetLayoutBinding{.binding = 2, .descriptorType = DescriptorType::Sampler}
				},
				.debugName = "PreviewIBLDescriptorSet"
			});

			iblDescriptorSet->UpdateTexture(0, Graphics::GetWhiteCubemapTexture());
			iblDescriptorSet->UpdateTexture(1, Graphics::GetWhiteCubemapTexture());
			iblDescriptorSet->UpdateSampler(2, Graphics::GetLinearSampler());

			DepthStencilStateDesc skyboxDepth;
			skyboxDepth.depthTestEnable = true;
			skyboxDepth.depthWriteEnable = false;
			skyboxDepth.depthCompareOp = CompareOp::GreaterEqual;

			skyboxPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/Editor/PreviewSky.raster"),
				.rasterizerState = {
					.cullMode = CullMode::Front
				},
				.depthStencilState = skyboxDepth,
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = renderPass
			});
		}

		void CleanupPipelines()
		{
			for (GPUPipeline* pipeline : opaquePipelines)
			{
				pipeline->Destroy();
			}
			opaquePipelines.Clear();
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!scene) return;
			RenderSceneObjects* objects = &scene->renderObjects;

			if (cachedPipelineOwner != nullptr && cachedPipelineOwner != scene)
			{
				CleanupPipelines();
			}
			cachedPipelineOwner = scene;

			if (boundDiffuseIrradiance != lightInstanceData->diffuseIrradianceTexture)
			{
				boundDiffuseIrradiance = lightInstanceData->diffuseIrradianceTexture;
				iblDescriptorSet->UpdateTexture(0, boundDiffuseIrradiance);
			}
			if (boundSpecularMap != lightInstanceData->specularMapTexture)
			{
				boundSpecularMap = lightInstanceData->specularMapTexture;
				iblDescriptorSet->UpdateTexture(1, boundSpecularMap);
			}

			while (opaquePipelines.Size() < objects->opaquePipelines.Size())
			{
				const DrawPipelineDesc& desc = objects->opaquePipelines[opaquePipelines.Size()].desc;

				Array<String> macros;
				if (desc.hasBones) macros.EmplaceBack("HAS_BONES");

				DepthStencilStateDesc depthStencilState;
				depthStencilState.depthTestEnable = true;
				depthStencilState.depthWriteEnable = true;
				depthStencilState.depthCompareOp = CompareOp::Greater;

				GraphicsPipelineDesc gpuDesc = GraphicsPipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/Editor/PreviewForward.shader"),
					.variant = ShaderResource::GetVariantName(macros),
					.rasterizerState = {
						.cullMode = desc.cullMode,
					},
					.depthStencilState = depthStencilState,
					.blendStates = {
						BlendStateDesc{}
					},
					.renderPass = renderPass,
				};

				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 0,
					.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
				});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 1,
					.descriptorSet = context->GetSceneDescriptorSet(0)
				});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 2,
					.descriptorSet = iblDescriptorSet
				});

				opaquePipelines.EmplaceBack(Graphics::CreateGraphicsPipeline(gpuDesc));
			}

			cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

			for (u32 i = 0; i < objects->opaquePipelines.Size(); i++)
			{
				GPUPipeline* pipeline = opaquePipelines[i];

				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
				cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet());
				cmd->BindDescriptorSet(pipeline, 2, iblDescriptorSet);

				for (const Drawcall& drawcall : objects->opaquePipelines[i].drawcalls)
				{
					if (!drawcall.material || drawcall.material->materialIndex == U32_MAX)
					{
						continue;
					}

					if (!drawcall.aabb.IsOnFrustum(context->camera.frustum))
					{
						continue;
					}

					if ((drawcall.layerMask & context->camera.cullingMask) == 0)
					{
						continue;
					}

					if (drawcall.bones)
					{
						cmd->BindDescriptorSet(pipeline, 3, drawcall.bones);
					}

					MeshPushConstants pc;
					pc.world = drawcall.transform;
					pc.materialIndex = drawcall.material->materialIndex;
					pc.vertexByteOffset = drawcall.vertexByteOffset;
					pc.vertexLayoutIndex = drawcall.vertexLayoutIndex;
					pc.ambientFlags = lightInstanceData->indirectLightFlags;
					pc.ambientLight = lightInstanceData->ambientLight;
					pc.ambientMultiplier = lightInstanceData->ambientMultiplier;
					pc.reflectionMultiplier = lightInstanceData->reflectionMultiplier;

					cmd->PushConstants(pipeline, ShaderStage::Vertex | ShaderStage::Pixel, 0, sizeof(MeshPushConstants), &pc);
					cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
				}
			}

			EnvironmentResourceCache* skyEnv = nullptr;
			scene->Iterate<EnvironmentComponent>([&](EnvironmentComponent* env)
			{
				if (skyEnv != nullptr) return;
				EnvironmentResourceCache* cache = env->GetEnvironmentCache();
				if (cache && cache->descriptorSet) skyEnv = cache;
			});

			if (skyEnv)
			{
				cmd->BindPipeline(skyboxPipeline);
				Mat4 viewProj = context->camera.projection * Mat4(Mat34(context->camera.view));

				cmd->PushConstants(skyboxPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &viewProj);
				cmd->BindDescriptorSet(skyboxPipeline, 3, skyEnv->descriptorSet, {});
				cmd->Draw(36, 1, 0, 0);
			}
		}

		void Destroy() override
		{
			CleanupPipelines();
			skyboxPipeline->Destroy();
			iblDescriptorSet->Destroy();
		}
	};

	struct PreviewForwardModule : RenderPipelineModule
	{
		SK_CLASS(PreviewForwardModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(LightSetupPass));
			setup.passes.EmplaceBack(sktypeid(PreviewForwardPass));
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			const DeviceProperties& properties = Graphics::GetDevice()->GetProperties();
			previewMsaaSamples = Math::Min(properties.limits.maxAttachmentSamples, 4u);

			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{.name = "LightInstanceData", .type = RenderPipelineResourceType::Instance, .instanceTypeId = sktypeid(LightPassInstanceData)});
			resources.EmplaceBack(RenderPipelineResource{.name = PreviewColorMSName, .type = RenderPipelineResourceType::Attachment, .format = Format::RGBA8_UNORM, .samples = previewMsaaSamples});
			resources.EmplaceBack(RenderPipelineResource{.name = PreviewDepthMSName, .type = RenderPipelineResourceType::Attachment, .format = Format::D32_FLOAT, .samples = previewMsaaSamples});
			return resources;
		}
	};

	struct PreviewCascadeShadowModule : CascadeShadowMapModuleBase
	{
		SK_CLASS(PreviewCascadeShadowModule, CascadeShadowMapModuleBase);

		TypeID GetCascadeShadowPassTypeId() override
		{
			return sktypeid(CascadeShadowPass);
		}
	};

	struct PreviewRenderPipeline : RenderPipeline
	{
		SK_CLASS(PreviewRenderPipeline, RenderPipeline);

		RenderPipelineSetup GetPipelineSetup() override
		{
			RenderPipelineSetup setup;
			setup.modules.EmplaceBack(sktypeid(PreviewCascadeShadowModule));
			setup.modules.EmplaceBack(sktypeid(PreviewForwardModule));
			return setup;
		}
	};

	void RegisterPreviewRenderPipeline()
	{
		Reflection::Type<PreviewForwardPass>();
		Reflection::Type<PreviewForwardModule>();
		Reflection::Type<PreviewCascadeShadowModule>();
		Reflection::Type<PreviewRenderPipeline>();
	}
}
