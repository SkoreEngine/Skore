#include "PipelineCommon.hpp"

#include "Skore/Core/Algorithm.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/EnvironmentComponent.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"

namespace Skore
{
	struct LightSetupPass : DefaultPipelinePass
	{
		SK_CLASS(LightSetupPass, DefaultPipelinePass);

		GPUBuffer*     lightBuffer = nullptr;
		u64            lightBufferAlignedSize = 0;
		BRDFLUTTexture brdfLut;

		GPUBuffer*          lightStorageBuffer = nullptr;
		u32                 lightStorageCapacity = 0;
		Array<LightData> lightData;
		u32                 shadowLightIndex = U32_MAX;

		~LightSetupPass() override
		{
			if (lightBuffer) lightBuffer->Destroy();
			if (lightStorageBuffer) lightStorageBuffer->Destroy();
			brdfLut.Destroy();
		}

		void EnsureResources()
		{
			if (lightBuffer != nullptr) return;

			u64 uboAlignment = Graphics::GetDevice()->GetProperties().limits.minUniformBufferOffsetAlignment;
			lightBufferAlignedSize = AlignedSize(static_cast<u64>(sizeof(LightBuffer)), uboAlignment);

			lightBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = lightBufferAlignedSize * SK_FRAMES_IN_FLIGHT,
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "LightBuffer"
			});

			brdfLut.Init({512, 512});
		}

		void EnsureLightStorage(u32 lightCount)
		{
			u32 required = Math::Max(lightCount, 1u);
			if (lightStorageBuffer != nullptr && required <= lightStorageCapacity) return;

			u32 newCapacity = lightStorageCapacity == 0 ? 16u : lightStorageCapacity;
			while (newCapacity < required) newCapacity *= 2;

			if (lightStorageBuffer) lightStorageBuffer->Destroy();

			lightStorageCapacity = newCapacity;
			lightStorageBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(LightData) * lightStorageCapacity,
				.usage = ResourceUsage::ShaderResource,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "LightStorageBuffer"
			});
		}

		void BuildRenderGraph(RenderGraph& renderGraph) override
		{
			EnsureResources();

			LightInstanceData* instanceData = renderGraph.CreateInstance<LightInstanceData>(LightInstanceDataName);
			instanceData->lightBuffer = lightBuffer;
			instanceData->lightBufferAlignedSize = lightBufferAlignedSize;
			instanceData->ambientLight = Vec3(0.4);
			instanceData->ambientMultiplier = 1.0f;
			instanceData->reflectionMultiplier = 1.0f;
			instanceData->flags = LightFlags::None;
			instanceData->cubeMapSkyTexture = Graphics::GetWhiteCubemapTexture();
			instanceData->diffuseIrradianceTexture = Graphics::GetWhiteCubemapTexture();
			instanceData->specularMapTexture = Graphics::GetWhiteCubemapTexture();
			instanceData->sceneBindingsReady = false;

			GPUDescriptorSet* sceneSet = renderGraph.GetSceneDescriptorSet();
			if (sceneSet == nullptr) return;

			Scene* scene = renderGraph.GetScene();
			if (scene != nullptr)
			{
				scene->Iterate<EnvironmentComponent>([&](EnvironmentComponent* env)
				{
					EnvironmentResourceCache* environmentCache = env->GetEnvironmentCache();

					switch (env->GetAmbientLightSource())
					{
						case AmbientLightSource::Skybox:
							if (environmentCache && environmentCache->diffuseIrradianceTexture && environmentCache->cubeMapSkyTexture)
							{
								instanceData->diffuseIrradianceTexture = environmentCache->diffuseIrradianceTexture;
								instanceData->cubeMapSkyTexture = environmentCache->cubeMapSkyTexture;
								instanceData->ambientMultiplier = env->GetAmbientLightIntensity();
								instanceData->flags |= LightFlags::HasAmbientTexture;
							}
							break;
						case AmbientLightSource::Color:
							instanceData->ambientMultiplier = env->GetAmbientLightIntensity();
							instanceData->ambientLight = env->GetAmbientLightColor().ToVec3();
							instanceData->flags |= LightFlags::HasAmbientColor;
							break;
						case AmbientLightSource::Disabled:
							break;
					}

					switch (env->GetReflectedLightSource())
					{
						case ReflectedLightSource::Skybox:
							if (environmentCache && environmentCache->specularMapTexture)
							{
								instanceData->specularMapTexture = environmentCache->specularMapTexture;
								instanceData->reflectionMultiplier = env->GetReflectedLightIntensity();
								instanceData->flags |= LightFlags::HasReflectionTexture;
							}
							break;
						case ReflectedLightSource::Disabled:
							break;
					}
				});
			}

			lightData.Clear();
			shadowLightIndex = U32_MAX;
			if (scene != nullptr)
			{
				scene->Iterate<LightComponent>([&](LightComponent* light)
				{
					u32 lightIndex = static_cast<u32>(lightData.Size());

					if (light->GetLightType() == LightType::Directional && light->GetEnableShadows() && shadowLightIndex == U32_MAX)
					{
						shadowLightIndex = lightIndex;
					}

					Mat4 transform = light->GetEntity()->GetWorldTransform();

					LightData shaderLight{};
					shaderLight.type = static_cast<u32>(light->GetLightType());
					shaderLight.position = Mat4::GetTranslation(transform);

					Vec3 forward = Vec3(-transform[2][0], -transform[2][1], -transform[2][2]);
					shaderLight.direction = Vec4(Vec3::Normalize(forward), 0.0f);

					shaderLight.color = Vec4(light->GetColor().ToVec3(), 0.0f);
					shaderLight.intensity = light->GetIntensity();
					shaderLight.range = light->GetRange();
					shaderLight.innerConeAngle = light->GetInnerConeAngleRadians();
					shaderLight.outerConeAngle = light->GetOuterConeAngleRadians();

					lightData.EmplaceBack(shaderLight);
				});
			}

			EnsureLightStorage(static_cast<u32>(lightData.Size()));

			u32 frame = renderGraph.GetCurrentFrame();

			DescriptorUpdate lightUpdate;
			lightUpdate.type = DescriptorType::UniformBuffer;
			lightUpdate.binding = 2;
			lightUpdate.buffer = lightBuffer;
			lightUpdate.bufferOffset = lightBufferAlignedSize * frame;
			lightUpdate.bufferRange = sizeof(LightBuffer);
			sceneSet->Update(lightUpdate);

			DescriptorUpdate lightStorageUpdate;
			lightStorageUpdate.type = DescriptorType::StorageBuffer;
			lightStorageUpdate.binding = 10;
			lightStorageUpdate.buffer = lightStorageBuffer;
			lightStorageUpdate.bufferOffset = 0;
			lightStorageUpdate.bufferRange = sizeof(LightData) * lightStorageCapacity;
			sceneSet->Update(lightStorageUpdate);

			ShadowMapInstanceData* shadowData = renderGraph.GetInstanceData<ShadowMapInstanceData>(ShadowMapInstanceDataName);
			GPUTexture*               shadowTexture = renderGraph.GetTexture(ShadowMapTextureName);

			if (shadowTexture != nullptr && shadowData != nullptr && shadowData->shadowSampler != nullptr)
			{
				DescriptorUpdate shadowTextureUpdate;
				shadowTextureUpdate.type = DescriptorType::SampledImage;
				shadowTextureUpdate.binding = 3;
				shadowTextureUpdate.texture = shadowTexture;
				sceneSet->Update(shadowTextureUpdate);

				DescriptorUpdate shadowSamplerUpdate;
				shadowSamplerUpdate.type = DescriptorType::Sampler;
				shadowSamplerUpdate.binding = 4;
				shadowSamplerUpdate.sampler = shadowData->shadowSampler;
				sceneSet->Update(shadowSamplerUpdate);
			}
			else
			{
				return;
			}

			DescriptorUpdate irradianceUpdate;
			irradianceUpdate.type = DescriptorType::SampledImage;
			irradianceUpdate.binding = 5;
			irradianceUpdate.texture = instanceData->diffuseIrradianceTexture;
			sceneSet->Update(irradianceUpdate);

			DescriptorUpdate specularUpdate;
			specularUpdate.type = DescriptorType::SampledImage;
			specularUpdate.binding = 7;
			specularUpdate.texture = instanceData->specularMapTexture;
			sceneSet->Update(specularUpdate);

			DescriptorUpdate brdfUpdate;
			brdfUpdate.type = DescriptorType::SampledImage;
			brdfUpdate.binding = 8;
			brdfUpdate.texture = brdfLut.GetTexture() ? brdfLut.GetTexture() : Graphics::GetWhiteTexture();
			sceneSet->Update(brdfUpdate);

			DescriptorUpdate samplerUpdate;
			samplerUpdate.type = DescriptorType::Sampler;
			samplerUpdate.binding = 9;
			samplerUpdate.sampler = Graphics::GetLinearClampToEdgeSampler();
			sceneSet->Update(samplerUpdate);

			instanceData->sceneBindingsReady = true;

			renderGraph
				.AddPass("LightSetup")
				.Stage(RenderStage::Lighting)
				.Write(LightInstanceDataName)
				.Render([this](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
				{
					RenderGraph& rg = *pass.GetGraph();

					LightInstanceData* instanceData = rg.GetInstanceData<LightInstanceData>(LightInstanceDataName);
					if (instanceData == nullptr) return;

					LightBuffer lightBufferData{};
					lightBufferData.lightCount = static_cast<u32>(lightData.Size());
					lightBufferData.shadowLightIndex = shadowLightIndex;
					lightBufferData.flags = instanceData->flags;
					lightBufferData.ambientMultiplier = instanceData->ambientMultiplier;
					lightBufferData.ambientLight = instanceData->ambientLight;
					lightBufferData.reflectionMultiplier = instanceData->reflectionMultiplier;

					ShadowMapInstanceData* shadowData = rg.GetInstanceData<ShadowMapInstanceData>(ShadowMapInstanceDataName);
					if (shadowData != nullptr && shadowData->hasShadowLight)
					{
						lightBufferData.cascadeSplits = Vec4{shadowData->cascadeSplits[0], shadowData->cascadeSplits[1], shadowData->cascadeSplits[2], shadowData->cascadeSplits[3]};
						memcpy(lightBufferData.cascadeViewProjMat, shadowData->cascadeViewProjMat, sizeof(lightBufferData.cascadeViewProjMat));
					}
					else
					{
						lightBufferData.shadowLightIndex = U32_MAX;
					}

					u32   frame = rg.GetCurrentFrame();
					char* lightMem = static_cast<char*>(lightBuffer->GetMappedData()) + lightBufferAlignedSize * frame;
					new(lightMem) LightBuffer(lightBufferData);

					if (lightStorageBuffer != nullptr && !lightData.Empty())
					{
						memcpy(lightStorageBuffer->GetMappedData(), lightData.Data(), sizeof(LightData) * lightData.Size());
					}
				});
		}
	};

	void RegisterLightSetupPass()
	{
		Reflection::Type<LightSetupPass>();
	}
}
