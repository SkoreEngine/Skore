#include "Skore/Graphics/Pipelines/PipelineCommon.hpp"
#include "Skore/Graphics/Pipelines/Modules/CascadeShadowMapModule.hpp"
#include "Skore/Graphics/Pipelines/Modules/DepthPrePassModule.hpp"
#include "Skore/Graphics/Pipelines/Modules/XeGTAORenderModule.hpp"
#include "Skore/Core/Algorithm.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/Scene/Scene.hpp"

#define A_CPU
#include "FidelityFX/ffx_a.h"
#include "FidelityFX/ffx_spd.h"

namespace Skore
{
	constexpr u32 MaxLinearDepthMips = 12;

	TextureFormat mainDepthFormat = TextureFormat::D32_FLOAT;

	struct LightData
	{
		u32  type;
		Vec3 position;
		Vec4 direction;
		Vec4 color;
		f32  intensity;
		f32  range;
		f32  innerConeAngle;
		f32  outerConeAngle;
	};

	struct LightBuffer
	{
		u32       lightCount;
		u32       shadowLightIndex;
		Vec2		  pad0;
		Vec4      cascadeSplits;
		Mat4      cascadeViewProjMat[MaxNumCascades];
		LightData lights[MAX_LIGHTS];
	};

	struct LightFlags
	{
		enum
		{
			None                 = 0,
			HasAmbientTexture    = 1 << 1,
			HasAmbientColor      = 1 << 2,
			HasReflectionTexture = 1 << 3,
			HasSSAOTexture       = 1 << 4,
			SSREnabled           = 1 << 5
		};
	};

	struct LightPassInstanceData
	{
		GPUBuffer*        lightBuffer = nullptr;
		u64               lightBufferAlignedSize = 0;

		GPUDescriptorSet* directLightingDescriptorSet[SK_FRAMES_IN_FLIGHT] = {};

		Vec3  ambientLight;
		float ambientMultiplier;
		float reflectionMultiplier;

		u32 indirectLightFlags = LightFlags::None;

		GPUTexture* skyTexture = nullptr;
		GPUTexture* diffuseIrradianceTexture = nullptr;
		GPUTexture* specularMapTexture = nullptr;

		static void RegisterType(NativeReflectType<LightPassInstanceData>& type)
		{
			type.Field<&LightPassInstanceData::lightBuffer>("lightBuffer");
		}
	};

	struct DefaultCascadeShadowPass : CascadeShadowPassBase
	{
		SK_CLASS(DefaultCascadeShadowPass, CascadeShadowPassBase);

		void RenderCascade(RenderSceneObjects* objects, GPUCommandBuffer* cmd, const Mat4& viewProj, u32 cascadeIndex) override
		{
			while (shadowMapPipelines.Size() < objects->shadowPipelines.Size())
			{
				const DrawPipelineDesc& desc = objects->shadowPipelines[shadowMapPipelines.Size()].desc;
				RID shadowShader = Resources::FindByPath("Skore://Shaders/ShadowMap.shader");

				Array<String> macros;
				if (desc.hasBones) macros.EmplaceBack("HAS_BONES");
				if (desc.hasUV1)   macros.EmplaceBack("HAS_UV1");
				if (desc.hasColor) macros.EmplaceBack("HAS_COLOR");

				GPUPipeline* p = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
					.shader = shadowShader,
					.variant = ShaderResource::GetVariantName(macros),
					.rasterizerState = {
						.cullMode = desc.cullMode,
						.depthClampEnable = true,
					},
					.depthStencilState = {
						.depthTestEnable = true,
						.depthWriteEnable = true,
						.depthCompareOp = CompareOp::LessEqual
					},
					.blendStates = {
						BlendStateDesc{}
					},
					.renderPass = m_shadowMapRenderPass[0],
					.vertexInputStride = desc.vertexStride
				});
				shadowMapPipelines.EmplaceBack(p);
			}

			for (u32 i = 0; i < objects->shadowPipelines.Size(); i++)
			{
				GPUPipeline* pipeline = shadowMapPipelines[i];
				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 0, m_shadowMapDescriptorSets[context->GetCurrentFrame()][cascadeIndex], {});

				for (const Drawcall& drawcall : objects->shadowPipelines[i].drawcalls)
				{
					if (drawcall.bones)
					{
						cmd->BindDescriptorSet(pipeline, 1, drawcall.bones);
					}

					cmd->BindVertexBuffer(0, drawcall.vertexBuffer, 0);
					cmd->BindIndexBuffer(drawcall.indexBuffer, 0, IndexType::Uint32);
					cmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &drawcall.transform);
					cmd->DrawIndexed(drawcall.indexCount, 1, drawcall.firstIndex, 0, 0);
				}
			}
		}
	};

	struct DefaultCascadeShadowMapModule : CascadeShadowMapModuleBase
	{
		SK_CLASS(DefaultCascadeShadowMapModule, CascadeShadowMapModuleBase);

		TypeID GetCascadeShadowPassTypeId() override
		{
			return sktypeid(DefaultCascadeShadowPass);
		}
	};

	struct DefaultLightSetupPass : RenderPipelinePass
	{

		SK_CLASS(DefaultLightSetupPass, RenderPipelinePass);

		ShadowMapInstanceData* shadowMapData = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;

		RID skyMaterialInUse = {};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup desc;
			desc.type = RenderPipelinePassType::Other;
			desc.stage = DefaultPipelineRenderStage::Lighting;
			desc.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = ShadowMapInstanceDataName, .access = RenderPipelineTextureAccess::Read});
			desc.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Write});
			return desc;
		}


		void Init() override
		{
			shadowMapData = context->GetInstanceData<ShadowMapInstanceData>(ShadowMapInstanceDataName);
			lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

			u64 uboAlignment = Graphics::GetDevice()->GetProperties().limits.minUniformBufferOffsetAlignment;
			lightInstanceData->lightBufferAlignedSize = AlignedSize(static_cast<u64>(sizeof(LightBuffer)), uboAlignment);

			lightInstanceData->lightBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = lightInstanceData->lightBufferAlignedSize * SK_FRAMES_IN_FLIGHT,
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "LightBuffer"
			});

			DescriptorSetDesc directLightDesc = {
				.bindings = {
					DescriptorSetLayoutBinding{.binding = 0, .descriptorType = DescriptorType::UniformBuffer},
					DescriptorSetLayoutBinding{.binding = 1, .descriptorType = DescriptorType::SampledImage},
					DescriptorSetLayoutBinding{.binding = 2, .descriptorType = DescriptorType::Sampler}
				},
				.debugName = "DirectLightDescriptorSet",
			};

			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				lightInstanceData->directLightingDescriptorSet[f] = Graphics::CreateDescriptorSet(directLightDesc);

				DescriptorUpdate descriptorUpdate;
				descriptorUpdate.type = DescriptorType::UniformBuffer;
				descriptorUpdate.binding = 0;
				descriptorUpdate.buffer = lightInstanceData->lightBuffer;
				descriptorUpdate.bufferOffset = lightInstanceData->lightBufferAlignedSize * f;
				descriptorUpdate.bufferRange = sizeof(LightBuffer);
				lightInstanceData->directLightingDescriptorSet[f]->Update(descriptorUpdate);
				lightInstanceData->directLightingDescriptorSet[f]->UpdateTexture(1, shadowMapData->shadowTexture);
				lightInstanceData->directLightingDescriptorSet[f]->UpdateSampler(2, shadowMapData->shadowSampler);
			}
		}

		virtual void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd)
		{
			LightBuffer lightBufferData;
			lightBufferData.lightCount = 0;
			lightBufferData.cascadeSplits = {shadowMapData->cascadeSplits[0], shadowMapData->cascadeSplits[1], shadowMapData->cascadeSplits[2], shadowMapData->cascadeSplits[3]};
			memcpy(lightBufferData.cascadeViewProjMat, shadowMapData->cascadeViewProjMat.Data(), sizeof(Mat4) * shadowMapData->cascadeViewProjMat.Size());

			lightBufferData.shadowLightIndex = U32_MAX;

			lightInstanceData->ambientLight = Vec3(0.4);
			lightInstanceData->ambientMultiplier = 1.0;
			lightInstanceData->reflectionMultiplier = 1.0;
			lightInstanceData->diffuseIrradianceTexture = Graphics::GetWhiteCubemapTexture();
			lightInstanceData->specularMapTexture = Graphics::GetWhiteCubemapTexture();

			for (EnvironmentObject* env : objects->environmentObjects)
			{
				if (!env->GetVisible())
				{
					continue;
				}

				MaterialResourceCache* materialCache = env->GetMaterialCache();

				switch (env->GetAmbientLightSource())
				{
					case AmbientLightSource::Skybox:
						if (materialCache)
						{
							if (lightInstanceData->diffuseIrradianceTexture != materialCache->diffuseIrradianceTexture)
							{
								lightInstanceData->diffuseIrradianceTexture = materialCache->diffuseIrradianceTexture;
							}
							lightInstanceData->ambientMultiplier = env->GetAmbientLightIntensity();
							lightInstanceData->indirectLightFlags |= LightFlags::HasAmbientTexture;
						}
						break;
					case AmbientLightSource::Color:
						lightInstanceData->ambientMultiplier = env->GetAmbientLightIntensity();
						lightInstanceData->ambientLight = env->GetAmbientLightColor().ToVec3();
						lightInstanceData->indirectLightFlags |= LightFlags::HasAmbientColor;
						break;
					case AmbientLightSource::Disabled:
						break;
				}

				switch (env->GetReflectedLightSource())
				{
					case ReflectedLightSource::Skybox:
						if (materialCache)
						{
							if (lightInstanceData->specularMapTexture != materialCache->specularMapTexture)
							{
								lightInstanceData->specularMapTexture = materialCache->specularMapTexture;
							}
							lightInstanceData->indirectLightFlags |= LightFlags::HasReflectionTexture;
							lightInstanceData->reflectionMultiplier = env->GetReflectedLightIntensity();
						}
						break;
					case ReflectedLightSource::Disabled:
						break;
				}
			}

			for (i32 i = 0; i < MAX_LIGHTS; i++)
			{
				lightBufferData.lights[i] = LightData{};
			}

			if (objects && !objects->lights.Empty())
			{
				u32 lightIndex = 0;
				for (LightObject* light : objects->lights)
				{
					if (lightIndex >= MAX_LIGHTS)
					{
						break;
					}

					if (!light->GetVisible())
					{
						continue;
					}

					if (light->GetType() == LightType::Directional && light->GetEnableShadows() && lightBufferData.shadowLightIndex == U32_MAX)
					{
						lightBufferData.shadowLightIndex = lightIndex;
					}

					Mat4 transform = light->GetTransform();

					LightData& shaderLight = lightBufferData.lights[lightIndex];
					shaderLight.type = static_cast<u32>(light->GetType());
					shaderLight.position = Mat4::GetTranslation(transform);

					Vec3 forward = Vec3(-transform[2][0], -transform[2][1], -transform[2][2]);
					shaderLight.direction = Vec4(Vec3::Normalize(forward), 0.0f);

					shaderLight.color = Vec4(light->GetColor().ToVec3(), 0.0f);
					shaderLight.intensity = light->GetIntensity();
					shaderLight.range = light->GetRange();
					shaderLight.innerConeAngle = light->GetInnerConeAngle();
					shaderLight.outerConeAngle = light->GetOuterConeAngle();

					lightIndex++;
				}

				lightBufferData.lightCount = lightIndex;
			}

			u32 frame = context->GetCurrentFrame();
			char* lightMem = static_cast<char*>(lightInstanceData->lightBuffer->GetMappedData()) + lightInstanceData->lightBufferAlignedSize * frame;
			new(lightMem) LightBuffer(lightBufferData);
		}

		void Destroy() override
		{
			lightInstanceData->lightBuffer->Destroy();
			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				lightInstanceData->directLightingDescriptorSet[f]->Destroy();
			}
		}
	};

	struct DefaultDeferredGBufferPass : RenderPipelinePass
	{
		SK_CLASS(DefaultDeferredGBufferPass, RenderPipelinePass);

		Array<GPUPipeline*> opaquePipelines = {};
		RenderSceneObjects* cachedPipelineOwner = nullptr;

		struct MeshPushConstants
		{
			Mat4 world;
			u32  materialIndex;
			u32  pad;
		};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = DefaultPipelineRenderStage::GBuffer;
			setup.invertViewport = true;

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferRoughnessAO", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferEmissive", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::Write});

			return setup;
		}

		void CleanupPipelines()
		{
			for (GPUPipeline* pipeline : opaquePipelines)
			{
				pipeline->Destroy();
			}
			opaquePipelines.Clear();
		}

		void Init() override
		{
			//TODO
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			if (cachedPipelineOwner != nullptr && cachedPipelineOwner != objects)
			{
				CleanupPipelines();
			}

			while (opaquePipelines.Size() < objects->opaquePipelines.Size())
			{
				const DrawPipelineDesc& desc = objects->opaquePipelines[opaquePipelines.Size()].desc;

				RID deferredGBuffer = Resources::FindByPath("Skore://Shaders/DeferredGBuffer.shader");

				Array<String> macros;
				if (desc.hasBones)  macros.EmplaceBack("HAS_BONES");
				if (desc.hasUV1)    macros.EmplaceBack("HAS_UV1");
				if (desc.hasColor)  macros.EmplaceBack("HAS_COLOR");

				DepthStencilStateDesc depthStencilState;
				depthStencilState.depthTestEnable = true;
				depthStencilState.depthWriteEnable = true;
				depthStencilState.depthCompareOp = CompareOp::Less;

				GraphicsPipelineDesc gpuDesc = GraphicsPipelineDesc{
					.shader = deferredGBuffer,
					.variant = ShaderResource::GetVariantName(macros),
					.rasterizerState = {
						.cullMode = desc.cullMode,
					},
					.depthStencilState = depthStencilState,
					.blendStates = {
						BlendStateDesc{},
						BlendStateDesc{},
						BlendStateDesc{},
						BlendStateDesc{}
					},
					.renderPass = renderPass,
					.vertexInputStride = desc.vertexStride
				};

				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 2,
					.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
				});

				opaquePipelines.EmplaceBack(Graphics::CreateGraphicsPipeline(gpuDesc));
			}

			cachedPipelineOwner = objects;

			for (u32 i = 0; i < objects->opaquePipelines.Size(); i++)
			{
				GPUPipeline* pipeline = opaquePipelines[i];

				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 0, context->GetSceneDescriptorSet());
				cmd->BindDescriptorSet(pipeline, 2, RenderResourceCache::GetGlobalDescriptorSet());

				for (const Drawcall& drawcall : objects->opaquePipelines[i].drawcalls)
				{
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
						cmd->BindDescriptorSet(pipeline, 1, drawcall.bones);
					}

					cmd->BindVertexBuffer(0, drawcall.vertexBuffer, 0);
					cmd->BindIndexBuffer(drawcall.indexBuffer, 0, IndexType::Uint32);

					MeshPushConstants pc;
					pc.world = drawcall.transform;
					pc.materialIndex = drawcall.material ? drawcall.material->materialIndex : 0;

					cmd->PushConstants(pipeline, ShaderStage::Vertex | ShaderStage::Pixel, 0, sizeof(MeshPushConstants), &pc);
					cmd->DrawIndexed(drawcall.indexCount, 1, drawcall.firstIndex, 0, 0);
				}
			}
		}

		void Destroy() override
		{
			CleanupPipelines();
		}
	};

	struct DefaultDeferredLightingPass : RenderPipelinePass
	{
		SK_CLASS(DefaultDeferredLightingPass, RenderPipelinePass);

		GPUDescriptorSet* gBufferDescriptorSets[SK_FRAMES_IN_FLIGHT] = {};
		GPUPipeline* pipeline = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;
		GPUTexture* lightAttachment = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = DefaultPipelineRenderStage::Lighting;

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferRoughnessAO", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferEmissive", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightAttachment", .access = RenderPipelineTextureAccess::Write});

			return setup;
		}

		void UpdateState()
		{
			lightAttachment = context->GetTexture("LightAttachment");

			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				gBufferDescriptorSets[f]->UpdateBuffer( 0, context->sceneBuffer, context->GetSceneBufferSize() * f, context->GetSceneBufferSize());
				gBufferDescriptorSets[f]->UpdateTexture(1, lightAttachment);
				gBufferDescriptorSets[f]->UpdateTexture(2, context->GetTexture("GBufferAlbedoMetallic"));
				gBufferDescriptorSets[f]->UpdateTexture(3, context->GetTexture("GBufferRoughnessAO"));
				gBufferDescriptorSets[f]->UpdateTexture(4, context->GetTexture("GBufferNormals"));
				gBufferDescriptorSets[f]->UpdateTexture(5, context->GetTexture("GBufferEmissive"));
				gBufferDescriptorSets[f]->UpdateTexture(6, context->GetTexture(LinearDepthMipChainName));
			}
		}

		void Init() override
		{
			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/DeferredLighting.comp")
			});

			DescriptorSetDesc gBufferDescSetDesc{
				.bindings = {
					DescriptorSetLayoutBinding{.binding = 0, .descriptorType = DescriptorType::UniformBuffer},
					DescriptorSetLayoutBinding{.binding = 1, .descriptorType = DescriptorType::StorageImage},
					DescriptorSetLayoutBinding{.binding = 2, .descriptorType = DescriptorType::SampledImage},
					DescriptorSetLayoutBinding{.binding = 3, .descriptorType = DescriptorType::SampledImage},
					DescriptorSetLayoutBinding{.binding = 4, .descriptorType = DescriptorType::SampledImage},
					DescriptorSetLayoutBinding{.binding = 5, .descriptorType = DescriptorType::SampledImage},
					DescriptorSetLayoutBinding{.binding = 6, .descriptorType = DescriptorType::SampledImage}
				}
			};

			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				gBufferDescriptorSets[f] = Graphics::CreateDescriptorSet(gBufferDescSetDesc);
			}

			UpdateState();
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

			u32 frame = context->GetCurrentFrame();
			cmd->BindPipeline(pipeline);
			cmd->BindDescriptorSet(pipeline, 0, gBufferDescriptorSets[frame]);
			cmd->BindDescriptorSet(pipeline, 1, lightInstanceData->directLightingDescriptorSet[frame]);
			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void OnResize(Extent size) override
		{
			UpdateState();
		}

		void Destroy() override
		{
			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				gBufferDescriptorSets[f]->Destroy();
			}
			pipeline->Destroy();
		}
	};

	struct DefaultForwardPass : RenderPipelinePass
	{
		SK_CLASS(DefaultForwardPass, RenderPipelinePass);

		GPUPipeline* skyboxMaterialPipeline = nullptr;
		GPUPipeline* particlePipeline = nullptr;
		Array<GPUPipeline*> transparencyPipelines;
		RenderSceneObjects* cachedPipelineOwner = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;

		struct MeshPushConstants
		{
			Mat4 world;
			u32  materialIndex;
		};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.stage = DefaultPipelineRenderStage::Forward;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightAttachment", .access = RenderPipelineTextureAccess::ReadWrite});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::ReadWrite});
			setup.invertViewport = true;
			return setup;
		}

		void Init() override
		{
			lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");
			{
				DepthStencilStateDesc depthStencilState;
				depthStencilState.depthTestEnable = true;
				depthStencilState.depthCompareOp = CompareOp::LessEqual;

				skyboxMaterialPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/SkyboxRender.raster"),
					.rasterizerState = {
						.cullMode = CullMode::Front
					},
					.depthStencilState = depthStencilState,
					.blendStates = {
						BlendStateDesc{}
					},
					.renderPass = renderPass,
				});
			}

			{
				particlePipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/ParticleRender.raster"),
					.rasterizerState = {
						.cullMode = CullMode::None
					},
					.depthStencilState = {
						.depthTestEnable = true,
						.depthWriteEnable = false,
						.depthCompareOp = CompareOp::Less
					},
					.blendStates = {
						BlendStateDesc{
							.blendEnable = true,
							.srcColorBlendFactor = BlendFactor::One,
							.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha,
							.colorBlendOp = BlendOp::Add,
							.srcAlphaBlendFactor = BlendFactor::One,
							.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha,
							.alphaBlendOp = BlendOp::Add
						},
					},
					.renderPass = renderPass
				});
			}
		}

		void CleanupPipelines()
		{
			for (GPUPipeline* pipeline : transparencyPipelines)
			{
				pipeline->Destroy();
			}
			transparencyPipelines.Clear();
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			u32 frame = context->GetCurrentFrame();

			if (cachedPipelineOwner != objects)
			{
				CleanupPipelines();
			}
			cachedPipelineOwner = objects;

			while (transparencyPipelines.Size() < objects->transparentPipelines.Size())
			{
				const DrawPipelineDesc& desc = objects->transparentPipelines[transparencyPipelines.Size()].desc;

				RID forwardShader = Resources::FindByPath("Skore://Shaders/ForwardMeshRender.shader");

				Array<String> macros;
				if (desc.hasBones)  macros.EmplaceBack("HAS_BONES");
				if (desc.hasColor)  macros.EmplaceBack("HAS_COLOR");

				DepthStencilStateDesc depthStencilState;
				depthStencilState.depthTestEnable = true;
				depthStencilState.depthWriteEnable = true;
				depthStencilState.depthCompareOp = CompareOp::Less;

				GraphicsPipelineDesc gpuDesc = GraphicsPipelineDesc{
					.shader = forwardShader,
					.variant = ShaderResource::GetVariantName(macros),
					.rasterizerState = {
						.cullMode = desc.cullMode,
					},
					.depthStencilState = depthStencilState,
					.blendStates = {
						BlendStateDesc{
							.blendEnable = true,
							.srcColorBlendFactor = BlendFactor::One,
							.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha,
							.colorBlendOp = BlendOp::Add,
							.srcAlphaBlendFactor = BlendFactor::One,
							.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha,
							.alphaBlendOp = BlendOp::Add
						},
					},
					.renderPass = renderPass,
					.vertexInputStride = desc.vertexStride
				};

				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{
					.set = 2,
					.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
				});

				transparencyPipelines.EmplaceBack(Graphics::CreateGraphicsPipeline(gpuDesc));
			}

			for (u32 i = 0; i < objects->transparentPipelines.Size(); i++)
			{
				GPUPipeline* pipeline = transparencyPipelines[i];

				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 0, context->GetSceneDescriptorSet());
				cmd->BindDescriptorSet(pipeline, 1, lightInstanceData->directLightingDescriptorSet[frame]);
				cmd->BindDescriptorSet(pipeline, 2, RenderResourceCache::GetGlobalDescriptorSet());

				for (const Drawcall& drawcall : objects->transparentPipelines[i].drawcalls)
				{
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

					cmd->BindVertexBuffer(0, drawcall.vertexBuffer, 0);
					cmd->BindIndexBuffer(drawcall.indexBuffer, 0, IndexType::Uint32);

					MeshPushConstants pc;
					pc.world = drawcall.transform;
					pc.materialIndex = drawcall.material ? drawcall.material->materialIndex : 0;
					cmd->PushConstants(pipeline, ShaderStage::Vertex | ShaderStage::Pixel, 0, sizeof(MeshPushConstants), &pc);
					cmd->DrawIndexed(drawcall.indexCount, 1, drawcall.firstIndex, 0, 0);
				}
			}

			for (ParticleObject* p : objects->particleEmitters)
			{
				if (!p->GetVisible() || !p->particleBuffer || !p->particleDescriptorSet) continue;

				u32 maxParticles = p->GetMaxParticles();

				cmd->BindPipeline(particlePipeline);
				cmd->BindDescriptorSet(particlePipeline, 0, context->GetSceneDescriptorSet());
				cmd->BindDescriptorSet(particlePipeline, 1, p->particleDescriptorSet);
				cmd->Draw(maxParticles * 6, 1, 0, 0);
			}

			for (EnvironmentObject* environmentObject : objects->environmentObjects)
			{
				if (environmentObject->GetVisible() && environmentObject->GetMaterialCache() != nullptr && environmentObject->GetUseAsSkybox())
				{
					MaterialResourceCache* skyMaterial = environmentObject->GetMaterialCache();

					cmd->BindPipeline(skyboxMaterialPipeline);
					Mat4 viewProj = context->camera.projection * Mat4(Mat34(context->camera.view));

					cmd->PushConstants(skyboxMaterialPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &viewProj);
					cmd->BindDescriptorSet(skyboxMaterialPipeline, 0, skyMaterial->descriptorSet, {});
					cmd->Draw(36, 1, 0, 0);

					break;
				}
			}
		}

		void Destroy() override
		{
			skyboxMaterialPipeline->Destroy();
			particlePipeline->Destroy();

			CleanupPipelines();
		}
	};


	struct DefaultDepthLinearizePass : RenderPipelinePass
	{
		SK_CLASS(DefaultDepthLinearizePass, RenderPipelinePass);

		struct SpdConstants
		{
			u32 mips;
			u32 numWorkGroupsPerSlice;
			u32 workGroupOffset[2];
			f32 nearClip;
			f32 farClip;
			u32 inputSize[2];
		};

		GPUPipeline*      pipeline = nullptr;
		GPUBuffer*        atomicBuffer = nullptr;
		GPUTexture*       linearDepthTexture = nullptr;
		GPUTextureView*   mipViews[MaxLinearDepthMips + 1] = {};
		GPUDescriptorSet* descriptorSet = nullptr;
		u32               mipCount = 0;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = DefaultPipelineRenderStage::DepthLinearize;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void DestroyMipResources()
		{
			for (u32 i = 0; i < MaxLinearDepthMips + 1; ++i)
			{
				if (mipViews[i])
				{
					mipViews[i]->Destroy();
					mipViews[i] = nullptr;
				}
			}
			if (linearDepthTexture)
			{
				linearDepthTexture->Destroy();
				linearDepthTexture = nullptr;
			}
			if (atomicBuffer)
			{
				atomicBuffer->Destroy();
				atomicBuffer = nullptr;
			}
			if (descriptorSet)
			{
				descriptorSet->Destroy();
				descriptorSet = nullptr;
			}
		}

		void CreateMipResources()
		{
			DestroyMipResources();

			Extent outputSize = context->GetOutputSize();
			mipCount = static_cast<u32>(std::floor(std::log2(static_cast<f32>(Math::Max(outputSize.width, outputSize.height))))) + 1;
			mipCount = Math::Min(mipCount, MaxLinearDepthMips);

			if (mipCount < 2) return;

			linearDepthTexture = Graphics::CreateTexture(TextureDesc{
				.extent = {outputSize.width, outputSize.height, 1},
				.mipLevels = mipCount,
				.format = TextureFormat::R32_FLOAT,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
				.debugName = "LinearDepthMipChain"
			});

			atomicBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(u32) * 6,
				.usage = ResourceUsage::UnorderedAccess,
				.hostVisible = true,
				.debugName = "LinearDepthSPDAtomicBuffer"
			});

			VoidPtr mapped = atomicBuffer->Map();
			memset(mapped, 0, sizeof(u32) * 6);
			atomicBuffer->Unmap();

			RID shader = Resources::FindByPath("Skore://Shaders/FidelityFX/DepthLinearize_SPD.shader");
			descriptorSet = Graphics::CreateDescriptorSet(shader, "Default", 0);

			descriptorSet->UpdateTexture(0, context->GetTexture(OutputDepthName));
			descriptorSet->UpdateBuffer(3, atomicBuffer, 0, sizeof(u32) * 6);

			for (u32 mip = 0; mip < 13; ++mip)
			{
				TextureViewDesc viewDesc = {};
				viewDesc.type = TextureViewType::Type2DArray;
				viewDesc.texture = linearDepthTexture;
				viewDesc.baseMipLevel = Math::Min(mipCount - 1, mip);
				mipViews[mip] = Graphics::CreateTextureView(viewDesc);
				descriptorSet->UpdateTextureView(1, mipViews[mip], mip);
			}
			descriptorSet->UpdateTextureView(2, mipViews[6], 0);

			context->SetTexture(LinearDepthMipChainName, linearDepthTexture);
		}

		void Init() override
		{
			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/FidelityFX/DepthLinearize_SPD.shader"),
				.variant = "Default",
				.debugName = "DepthLinearizeSPDPipeline"
			});

			CreateMipResources();
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			if (mipCount < 2 || !linearDepthTexture) return;

			Extent outputSize = context->GetOutputSize();

			varAU2(dispatchThreadGroupCountXY);
			varAU2(workGroupOffset);
			varAU2(numWorkGroupsAndMips);
			varAU4(rectInfo) = initAU4(0, 0, outputSize.width, outputSize.height);
			SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

			SpdConstants pc;
			pc.numWorkGroupsPerSlice = numWorkGroupsAndMips[0];
			pc.mips = numWorkGroupsAndMips[1];
			pc.workGroupOffset[0] = workGroupOffset[0];
			pc.workGroupOffset[1] = workGroupOffset[1];
			pc.nearClip = context->camera.nearClip;
			pc.farClip = context->camera.farClip;
			pc.inputSize[0] = outputSize.width;
			pc.inputSize[1] = outputSize.height;

			// Reset atomic counter
			VoidPtr mapped = atomicBuffer->Map();
			memset(mapped, 0, sizeof(u32) * 6);
			atomicBuffer->Unmap();

			cmd->BindPipeline(pipeline);
			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(SpdConstants), &pc);
			cmd->BindDescriptorSet(pipeline, 0, descriptorSet, {});

			cmd->ResourceBarrier(linearDepthTexture, ResourceState::ShaderReadOnly, ResourceState::General, 0, mipCount, 0, 1);
			cmd->Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1], 1);
			cmd->ResourceBarrier(linearDepthTexture, ResourceState::General, ResourceState::ShaderReadOnly, 0, mipCount, 0, 1);
		}

		void OnResize(Extent size) override
		{
			CreateMipResources();
		}

		void Destroy() override
		{
			DestroyMipResources();
			if (pipeline)
			{
				pipeline->Destroy();
				pipeline = nullptr;
			}
		}
	};

	struct DefaultReflectionPass : RenderPipelinePass
	{
		SK_CLASS(DefaultReflectionPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;
		BRDFLUTTexture brdfLUT;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = DefaultPipelineRenderStage::Indirect;

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ColorAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferRoughnessAO", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ReflectionAttachment", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void Init() override
		{
			brdfLUT.Init({512, 512});
			lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/ReflectionPass.comp"),
				.allowImmediateSet = true
			});
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			struct ReflectionPushConstants
			{
				Vec3  cameraPosition;
				float reflectionMultiplier;

				Vec2  outputSize;
				float farClip;
				u32   flags;

				Mat4 proj;
				Mat4 view;
				Mat4 invView;

				int   maxIterations; // e.g. 64  — max total cells crossed
				int   maxMipLevel;   // e.g. 6   — highest (coarsest) mip to use
				float thickness;     // e.g. 0.1 — depth tolerance for a hit
				float rayBias;       // e.g. 0.001
			};

			ReflectionPushConstants pc;
			pc.cameraPosition = context->camera.cameraPosition;
			pc.reflectionMultiplier = lightInstanceData->reflectionMultiplier;
			pc.outputSize = {static_cast<f32>(context->GetOutputSize().width), static_cast<f32>(context->GetOutputSize().height)};
			pc.farClip = context->camera.farClip;
			pc.flags = lightInstanceData->indirectLightFlags;
			pc.proj = context->camera.projection;
			pc.view = context->camera.view;
			pc.invView = context->camera.invView;

			//TODO - WIP
			//pc.flags |= LightFlags::SSREnabled;
			pc.maxIterations = 128;
			pc.maxMipLevel = 4;
			pc.thickness = 0.1;
			pc.rayBias = 0.001;

			cmd->BindPipeline(pipeline);

			cmd->SetTexture(pipeline, 0, 0, context->GetTexture("ReflectionAttachment"), 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetPrevTexture("ColorAttachment"), 0);
			cmd->SetTexture(pipeline, 0, 2, context->GetTexture("GBufferAlbedoMetallic"), 0);
			cmd->SetTexture(pipeline, 0, 3, context->GetTexture("GBufferRoughnessAO"), 0);
			cmd->SetTexture(pipeline, 0, 4, context->GetTexture("GBufferNormals"), 0);
			cmd->SetTexture(pipeline, 0, 5, context->GetTexture(LinearDepthMipChainName), 0);
			cmd->SetTexture(pipeline, 0, 6, lightInstanceData->specularMapTexture, 0);
			cmd->SetTexture(pipeline, 0, 7, brdfLUT.GetTexture(), 0);
			cmd->SetSampler(pipeline, 0, 8, brdfLUT.GetSampler());
			cmd->SetSampler(pipeline, 0, 9, Graphics::GetLinearSampler());
			cmd->SetSampler(pipeline, 0, 10, Graphics::GetNearestClampToEdgeSampler());

			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(ReflectionPushConstants), &pc);
			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			pipeline->Destroy();
			brdfLUT.Destroy();
		}
	};

	struct DefaultCompositePass : RenderPipelinePass
	{
		SK_CLASS(DefaultCompositePass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = DefaultPipelineRenderStage::Composite;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightInstanceData", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "LightAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferAlbedoMetallic", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferRoughnessAO", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "GBufferNormals", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ReflectionAttachment", .access = RenderPipelineTextureAccess::Read});

			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ColorAttachment", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void Init() override
		{
			lightInstanceData = context->GetInstanceData<LightPassInstanceData>("LightInstanceData");

			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/DefaultComposite.comp"),
				.variant = "Default",
				.allowImmediateSet = true
			});
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			struct DefaultCompositePushConstants
			{
				Vec3  ambientLight;
				float ambientMultiplier;

				Vec2  outputSize;
				u32   flags;
				float farClip;
			};

			DefaultCompositePushConstants pc;

			pc.ambientLight = lightInstanceData->ambientLight;
			pc.ambientMultiplier = lightInstanceData->ambientMultiplier;
			pc.outputSize = {static_cast<f32>(context->GetOutputSize().width), static_cast<f32>(context->GetOutputSize().height)};
			pc.flags = lightInstanceData->indirectLightFlags;
			pc.farClip = context->camera.farClip;

			GPUTexture* ssaoTexture = context->GetTexture(XeGTAOOutputName);
			if (ssaoTexture != nullptr)
			{
				pc.flags |= LightFlags::HasSSAOTexture;
			}
			else
			{
				ssaoTexture = Graphics::GetWhiteTexture();
			}

			GPUTexture* reflectionTexture = context->GetTexture("ReflectionAttachment");
			if (reflectionTexture != nullptr)
			{
				pc.flags |= LightFlags::HasReflectionTexture;
			}
			else
			{
				reflectionTexture = Graphics::GetWhiteTexture();
			}

			cmd->BindPipeline(pipeline);

			cmd->SetTexture(pipeline, 0, 0, context->GetTexture("ColorAttachment"), 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture("LightAttachment"), 0);
			cmd->SetTexture(pipeline, 0, 2, context->GetTexture("GBufferAlbedoMetallic"), 0);
			cmd->SetTexture(pipeline, 0, 3, context->GetTexture("GBufferRoughnessAO"), 0);
			cmd->SetTexture(pipeline, 0, 4, context->GetTexture("GBufferNormals"), 0);
			cmd->SetTexture(pipeline, 0, 5, context->GetTexture(OutputDepthName), 0);
			cmd->SetTexture(pipeline, 0, 6, lightInstanceData->diffuseIrradianceTexture, 0);
			cmd->SetTexture(pipeline, 0, 7, ssaoTexture, 0);
			cmd->SetTexture(pipeline, 0, 8, reflectionTexture, 0);
			cmd->SetSampler(pipeline, 0, 9, Graphics::GetLinearSampler());

			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(DefaultCompositePushConstants), &pc);
			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};

	struct DefaultCameraMotionVectorPass : RenderPipelinePass
	{
		SK_CLASS(DefaultCameraMotionVectorPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = DefaultPipelineRenderStage::PostProcess;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "MotionVector", .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = LinearDepthMipChainName, .access = RenderPipelineTextureAccess::Read});
			return setup;
		}

		void Init() override
		{
			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/ComputeCameraMotion.comp"),
				.allowImmediateSet = true
			});
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			struct PushConstants
			{
				Mat4  projectionNoJitter;
				Mat4  viewInverse;
				Mat4  previousViewProjectionNoJitter;
				IVec2 resolution;
				Vec2  pad;
			};

			PushConstants pc;
			pc.projectionNoJitter = context->camera.projectionNoJitter;
			pc.viewInverse = context->camera.invView;
			pc.previousViewProjectionNoJitter = context->camera.previousViewProjectionNoJitter;
			pc.resolution = IVec2{static_cast<i32>(context->GetOutputSize().width), static_cast<i32>(context->GetOutputSize().height)};
			pc.pad = Vec2(0.0f);

			cmd->BindPipeline(pipeline);
			cmd->SetTexture(pipeline, 0, 0, context->GetTexture("MotionVector"), 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture(LinearDepthMipChainName), 0);
			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(PushConstants), &pc);

			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};


	struct DefaultPostProcessPass : RenderPipelinePass
	{
		SK_CLASS(DefaultPostProcessPass, RenderPipelinePass);

		GPUPipeline* pipeline = nullptr;

		struct PostProcessPushConstants
		{
			f32 bloomIntensity;
		};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Compute;
			setup.stage = DefaultPipelineRenderStage::PostProcess;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ColorAttachment", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "ResolvedHDR", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "BloomTexture", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void Init() override
		{
			pipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/PostProcess.comp"),
				.allowImmediateSet = true
			});
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			PostProcessPushConstants pc;
			pc.bloomIntensity = 0.04f;

			cmd->BindPipeline(pipeline);
			cmd->SetTexture(pipeline, 0, 0, context->GetTexture(OutputColorName), 0);
			cmd->SetTexture(pipeline, 0, 1, context->GetTexture("ColorAttachment"), 0);
			cmd->SetSampler(pipeline, 0, 2, Graphics::GetNearestClampToEdgeSampler());

			GPUTexture* bloomTexture = context->GetTexture("BloomTexture");
			if (bloomTexture)
			{
				cmd->SetTexture(pipeline, 0, 3, bloomTexture, 0);
				cmd->SetSampler(pipeline, 0, 4, Graphics::GetLinearClampToEdgeSampler());
			}

			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(PostProcessPushConstants), &pc);
			cmd->Dispatch((context->GetOutputSize().width + 7) / 8, (context->GetOutputSize().height + 7) / 8, 1);
		}

		void Destroy() override
		{
			pipeline->Destroy();
		}
	};

	struct DefaultDeferredRenderModule : RenderPipelineModule
	{
		SK_CLASS(DefaultDeferredRenderModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(DefaultLightSetupPass));
			setup.passes.EmplaceBack(sktypeid(DefaultDeferredGBufferPass));
			setup.passes.EmplaceBack(sktypeid(DefaultDeferredLightingPass));
			setup.passes.EmplaceBack(sktypeid(DefaultForwardPass));
			setup.passes.EmplaceBack(sktypeid(DefaultDepthLinearizePass));
			setup.passes.EmplaceBack(sktypeid(DefaultReflectionPass));
			setup.passes.EmplaceBack(sktypeid(DefaultCompositePass));
			setup.passes.EmplaceBack(sktypeid(DefaultCameraMotionVectorPass));
			setup.passes.EmplaceBack(sktypeid(DefaultPostProcessPass));
			return setup;
		}

		Array<RenderPipelineResource> GetResources() override
		{
			context->SetDepthOutput(OutputDepthName);

			Array<RenderPipelineResource> resources;

			//resources
			resources.EmplaceBack(RenderPipelineResource{.name = "LightInstanceData", .type = RenderPipelineResourceType::Instance, .instanceTypeId = sktypeid(LightPassInstanceData)});

			//GBuffer attachments
			resources.EmplaceBack(RenderPipelineResource{.name = "GBufferAlbedoMetallic", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R8G8B8A8_UNORM});
			resources.EmplaceBack(RenderPipelineResource{.name = "GBufferRoughnessAO", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R8G8_UNORM});
			resources.EmplaceBack(RenderPipelineResource{.name = "GBufferNormals", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16_FLOAT});
			resources.EmplaceBack(RenderPipelineResource{.name = "GBufferEmissive", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R11G11B10_FLOAT});

			//lighting
			resources.EmplaceBack(RenderPipelineResource{.name = "LightAttachment", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16B16A16_FLOAT, .textureUsage = ResourceUsage::ShaderResource |  ResourceUsage::UnorderedAccess | ResourceUsage::CopyDest});
			resources.EmplaceBack(RenderPipelineResource{.name = "ColorAttachment", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16B16A16_FLOAT, .textureUsage = ResourceUsage::ShaderResource |  ResourceUsage::UnorderedAccess | ResourceUsage::CopyDest, .pingPong = true});

			//reflection
			resources.EmplaceBack(RenderPipelineResource{.name = "ReflectionAttachment", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16B16A16_FLOAT, .textureUsage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess});

			//velocity
			resources.EmplaceBack(RenderPipelineResource{.name = "MotionVector", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R16G16_FLOAT, .textureUsage = ResourceUsage::ShaderResource |  ResourceUsage::UnorderedAccess});

			resources.EmplaceBack(RenderPipelineResource{.name = OutputDepthName, .type = RenderPipelineResourceType::Attachment, .format = mainDepthFormat});
			return resources;
		}
	};


	struct DefaultRenderPipeline : RenderPipeline
	{
		SK_CLASS(DefaultRenderPipeline, RenderPipeline);

		RenderPipelineSetup GetPipelineSetup() override
		{
			RenderPipelineSetup setup;
			setup.modules.EmplaceBack(sktypeid(DefaultCascadeShadowMapModule));
			setup.modules.EmplaceBack(sktypeid(DepthPrePassModule));
			setup.modules.EmplaceBack(sktypeid(DefaultDeferredRenderModule));

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

		Reflection::Type<DefaultCascadeShadowPass>();
		Reflection::Type<DefaultLightSetupPass>();
		Reflection::Type<DefaultDeferredGBufferPass>();
		Reflection::Type<DefaultDeferredLightingPass>();
		Reflection::Type<DefaultReflectionPass>();
		Reflection::Type<DefaultCompositePass>();
		Reflection::Type<DefaultPostProcessPass>();
		Reflection::Type<DefaultForwardPass>();

		Reflection::Type<DefaultDepthLinearizePass>();
		Reflection::Type<DefaultCameraMotionVectorPass>();

		Reflection::Type<DefaultCascadeShadowMapModule>();
		Reflection::Type<DefaultDeferredRenderModule>();

		Reflection::Type<DefaultRenderPipeline>();
	}
}