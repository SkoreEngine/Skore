#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Graphics/Pipelines/Modules/CascadeShadowMapModule.hpp"

#include "Skore/Core/Algorithm.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "PipelineCommon.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/EnvironmentComponent.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"

namespace Skore
{
	struct LightSetupPass : RenderPipelinePass
	{
		SK_CLASS(LightSetupPass, RenderPipelinePass);

		ShadowMapInstanceData* shadowMapData = nullptr;
		LightPassInstanceData* lightInstanceData = nullptr;

		RID skyMaterialInUse = {};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup desc;
			desc.type = RenderPipelinePassType::Other;
			desc.stage = PipelineRenderStage::Lighting;
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

			// Light/shadow bindings live in the merged scene descriptor set (space1, bindings 1/2/3).
			// Per-frame in flight scene sets get distinct light-buffer offsets to avoid hazards.
			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				GPUDescriptorSet* sceneSet = context->GetSceneDescriptorSet(f);

				DescriptorUpdate lightUpdate;
				lightUpdate.type = DescriptorType::UniformBuffer;
				lightUpdate.binding = 2;
				lightUpdate.buffer = lightInstanceData->lightBuffer;
				lightUpdate.bufferOffset = lightInstanceData->lightBufferAlignedSize * f;
				lightUpdate.bufferRange = sizeof(LightBuffer);
				sceneSet->Update(lightUpdate);

				sceneSet->UpdateTexture(3, shadowMapData->shadowTexture);
				sceneSet->UpdateSampler(4, shadowMapData->shadowSampler);
			}
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			LightBuffer lightBufferData;
			lightBufferData.lightCount = 0;
			lightBufferData.cascadeSplits = {shadowMapData->cascadeSplits[0], shadowMapData->cascadeSplits[1], shadowMapData->cascadeSplits[2], shadowMapData->cascadeSplits[3]};
			memcpy(lightBufferData.cascadeViewProjMat, shadowMapData->cascadeViewProjMat.Data(), sizeof(Mat4) * shadowMapData->cascadeViewProjMat.Size());

			lightBufferData.shadowLightIndex = U32_MAX;

			lightInstanceData->ambientLight = Vec3(0.4);
			lightInstanceData->ambientMultiplier = 1.0;
			lightInstanceData->reflectionMultiplier = 1.0;
			lightInstanceData->indirectLightFlags = LightFlags::None;
			lightInstanceData->diffuseIrradianceTexture = Graphics::GetWhiteCubemapTexture();
			lightInstanceData->specularMapTexture = Graphics::GetWhiteCubemapTexture();
			lightInstanceData->cubeMapSkyTexture = Graphics::GetWhiteCubemapTexture();

			scene->Iterate<EnvironmentComponent>([&](EnvironmentComponent* env)
			{
				EnvironmentResourceCache* environmentCache = env->GetEnvironmentCache();

				switch (env->GetAmbientLightSource())
				{
					case AmbientLightSource::Skybox:
						if (environmentCache && environmentCache->diffuseIrradianceTexture && environmentCache->cubeMapSkyTexture)
						{
							if (lightInstanceData->diffuseIrradianceTexture != environmentCache->diffuseIrradianceTexture)
							{
								lightInstanceData->diffuseIrradianceTexture = environmentCache->diffuseIrradianceTexture;
							}

							if (lightInstanceData->cubeMapSkyTexture != environmentCache->cubeMapSkyTexture)
							{
								lightInstanceData->cubeMapSkyTexture = environmentCache->cubeMapSkyTexture;
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
						if (environmentCache && environmentCache->specularMapTexture)
						{
							if (lightInstanceData->specularMapTexture != environmentCache->specularMapTexture)
							{
								lightInstanceData->specularMapTexture = environmentCache->specularMapTexture;
							}
							lightInstanceData->indirectLightFlags |= LightFlags::HasReflectionTexture;
							lightInstanceData->reflectionMultiplier = env->GetReflectedLightIntensity();
						}
						break;
					case ReflectedLightSource::Disabled:
						break;
				}
			});

			for (i32 i = 0; i < MAX_LIGHTS; i++)
			{
				lightBufferData.lights[i] = LightData{};
			}

			u32 lightIndex = 0;
			scene->Iterate<LightComponent>([&](LightComponent* light)
			{
				if (lightIndex >= MAX_LIGHTS)
				{
					return;
				}

				if (light->GetLightType() == LightType::Directional && light->GetEnableShadows() && lightBufferData.shadowLightIndex == U32_MAX)
				{
					lightBufferData.shadowLightIndex = lightIndex;
				}

				Mat4 transform = light->GetEntity()->GetWorldTransform();

				LightData& shaderLight = lightBufferData.lights[lightIndex];
				shaderLight.type = static_cast<u32>(light->GetLightType());
				shaderLight.position = Mat4::GetTranslation(transform);

				Vec3 forward = Vec3(-transform[2][0], -transform[2][1], -transform[2][2]);
				shaderLight.direction = Vec4(Vec3::Normalize(forward), 0.0f);

				shaderLight.color = Vec4(light->GetColor().ToVec3(), 0.0f);
				shaderLight.intensity = light->GetIntensity();
				shaderLight.range = light->GetRange();
				shaderLight.innerConeAngle = light->GetInnerConeAngleRadians();
				shaderLight.outerConeAngle = light->GetOuterConeAngleRadians();

				lightIndex++;
			});

			lightBufferData.lightCount = lightIndex;

			u32 frame = context->GetCurrentFrame();
			char* lightMem = static_cast<char*>(lightInstanceData->lightBuffer->GetMappedData()) + lightInstanceData->lightBufferAlignedSize * frame;
			new(lightMem) LightBuffer(lightBufferData);
		}

		void Destroy() override
		{
			lightInstanceData->lightBuffer->Destroy();
		}
	};

	void RegisterLightSetupPass()
	{
		Reflection::Type<LightSetupPass>();
	}
}
