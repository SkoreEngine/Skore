#include "Skore/Graphics/Pipelines/PipelineCommon.hpp"
#include "Skore/Graphics/Pipelines/Modules/DepthPrePassModule.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	constexpr const char* PathTracerAccumulationName = "PathTracerAccumulation";

	struct PathTracerPushConstants
	{
		u32 sampleIndex;
	};

	struct RTLightData
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

	struct RTLightBuffer
	{
		u32         lightCount;
		Vec3        ambientLight;
		i32         skyTextureIndex;
		Vec3        pad;
		RTLightData lights[MAX_LIGHTS];
	};

	struct PathTracerPass : RenderPipelinePass
	{
		SK_CLASS(PathTracerPass, RenderPipelinePass);

		GPUPipeline*      rtPipeline = nullptr;
		GPUDescriptorSet* rtDescriptorSet = nullptr;
		GPUBuffer*        lightBuffer = nullptr;

		u32  sampleCount = 0;
		Mat4 prevView = Mat4(1.0f);
		Vec3 prevCameraPos = {};

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Raytrace;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::Write});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = PathTracerAccumulationName, .access = RenderPipelineTextureAccess::ReadWrite});
			return setup;
		}

		void Init() override
		{
			RID shaderRID = Resources::FindByPath("Skore://Shaders/PathTracer.rt");
			if (!shaderRID) return;

			rtPipeline = Graphics::CreateRayTracingPipeline(RayTracingPipelineDesc{
				.shader = shaderRID,
				.variant = "Default",
				.maxRecursionDepth = 1,
				.debugName = "PathTracerPipeline"
			});

			rtDescriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
				.bindings = {
					DescriptorSetLayoutBinding{
						.binding = 0,
						.descriptorType = DescriptorType::AccelerationStructure
					},
					DescriptorSetLayoutBinding{
						.binding = 1,
						.descriptorType = DescriptorType::StorageImage
					},
					DescriptorSetLayoutBinding{
						.binding = 2,
						.descriptorType = DescriptorType::StorageBuffer
					},
					DescriptorSetLayoutBinding{
						.binding = 3,
						.descriptorType = DescriptorType::UniformBuffer
					},
					DescriptorSetLayoutBinding{
						.binding = 4,
						.descriptorType = DescriptorType::StorageImage
					}
				},
				.debugName = "PathTracerDescriptorSet"
			});

			lightBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(RTLightBuffer),
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "PathTracerLightBuffer"
			});

			DescriptorUpdate lightBufUpdate;
			lightBufUpdate.type = DescriptorType::UniformBuffer;
			lightBufUpdate.binding = 3;
			lightBufUpdate.buffer = lightBuffer;
			rtDescriptorSet->Update(lightBufUpdate);
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			if (!rtPipeline || !rtDescriptorSet) return;
			if (!objects || !objects->tlas) return;

			GPUDescriptorSet* geometrySet = RenderResourceCache::GetGlobalDescriptorSet();
			if (!geometrySet || objects->instanceDataCount == 0) return;

			Extent outputSize = context->GetOutputSize();
			if (outputSize.width == 0 || outputSize.height == 0) return;

			GPUTexture* outputTexture = context->GetTexture(OutputColorName);
			GPUTexture* accumulationTexture = context->GetTexture(PathTracerAccumulationName);
			if (!outputTexture || !accumulationTexture) return;

			// Camera change detection â€” reset accumulation if camera moved
			Mat4 currentView = context->camera.view;
			Vec3 currentCameraPos = context->camera.cameraPosition;

			if (!(currentView == prevView) || !(currentCameraPos == prevCameraPos))
			{
				sampleCount = 0;
				prevView = currentView;
				prevCameraPos = currentCameraPos;
			}

			cmd->ResourceBarrier(outputTexture, ResourceState::Undefined, ResourceState::General, 0, 0);
			cmd->ResourceBarrier(accumulationTexture, ResourceState::Undefined, ResourceState::General, 0, 0);

			DescriptorUpdate tlasUpdate;
			tlasUpdate.type = DescriptorType::AccelerationStructure;
			tlasUpdate.binding = 0;
			tlasUpdate.topLevelAS = objects->tlas;
			rtDescriptorSet->Update(tlasUpdate);

			DescriptorUpdate imageUpdate;
			imageUpdate.type = DescriptorType::StorageImage;
			imageUpdate.binding = 1;
			imageUpdate.texture = outputTexture;
			rtDescriptorSet->Update(imageUpdate);

			rtDescriptorSet->UpdateBuffer(2, objects->instanceDataBuffer, 0, objects->instanceDataCount * sizeof(InstanceData));

			DescriptorUpdate accumUpdate;
			accumUpdate.type = DescriptorType::StorageImage;
			accumUpdate.binding = 4;
			accumUpdate.texture = accumulationTexture;
			rtDescriptorSet->Update(accumUpdate);

			// Populate light buffer from scene lights
			{
				RTLightBuffer lightBufferData{};
				lightBufferData.ambientLight = Vec3(0.03f);
				lightBufferData.skyTextureIndex = -1;

				for (EnvironmentObject* env : objects->environmentObjects)
				{
					if (env->GetAmbientLightSource() == AmbientLightSource::Color)
					{
						lightBufferData.ambientLight = env->GetAmbientLightColor().ToVec3() * env->GetAmbientLightIntensity();
					}

					if (env->GetVisible() && env->GetUseAsSkybox() && env->GetMaterialCache() && env->GetMaterialCache()->skyMaterialTexture)
					{
						lightBufferData.skyTextureIndex = static_cast<i32>(env->GetMaterialCache()->skyMaterialTexture->textureIndex);
					}
				}

				u32 lightIndex = 0;

				auto addLights = [&](const Array<LightObject*>& lights)
				{
					for (LightObject* light : lights)
					{
						if (lightIndex >= MAX_LIGHTS) break;
						if (!light->GetVisible()) continue;

						Mat4 transform = light->GetTransform();
						RTLightData& shaderLight = lightBufferData.lights[lightIndex];
						shaderLight.type = static_cast<u32>(light->GetType());
						shaderLight.position = Mat4::GetTranslation(transform);
						Vec3 forward = Vec3(-transform[2][0], -transform[2][1], -transform[2][2]);
						shaderLight.direction = Vec4(Vec3::Normalize(forward), light->GetSourceRadius());
						shaderLight.color = Vec4(light->GetColor().ToVec3(), 0.0f);
						shaderLight.intensity = light->GetIntensity();
						shaderLight.range = light->GetRange();
						shaderLight.innerConeAngle = light->GetInnerConeAngle();
						shaderLight.outerConeAngle = light->GetOuterConeAngle();
						lightIndex++;
					}
				};

				addLights(objects->lights);

				lightBufferData.lightCount = lightIndex;

				new(lightBuffer->GetMappedData()) RTLightBuffer(lightBufferData);
			}

			cmd->BindPipeline(rtPipeline);
			cmd->BindDescriptorSet(rtPipeline, 0, context->GetSceneDescriptorSet(), {});
			cmd->BindDescriptorSet(rtPipeline, 1, rtDescriptorSet, {});
			cmd->BindDescriptorSet(rtPipeline, 2, geometrySet, {});

			PathTracerPushConstants pc;
			pc.sampleIndex = sampleCount;
			cmd->PushConstants(rtPipeline, ShaderStage::RayGen, 0, sizeof(PathTracerPushConstants), &pc);

			cmd->TraceRays(rtPipeline, outputSize.width, outputSize.height, 1);

			sampleCount++;

			cmd->ResourceBarrier(outputTexture, ResourceState::General, ResourceState::ShaderReadOnly, 0, 0);
		}

		void Destroy() override
		{
			if (lightBuffer) lightBuffer->Destroy();
			if (rtDescriptorSet) rtDescriptorSet->Destroy();
			if (rtPipeline) rtPipeline->Destroy();
		}
	};

	struct PathTracerModule : RenderPipelineModule
	{
		SK_CLASS(PathTracerModule, RenderPipelineModule);

		RenderPipelineModuleSetup GetSetup() override
		{
			return {.passes = {sktypeid(PathTracerPass)}};
		}

		Array<RenderPipelineResource> GetResources() override
		{
			context->SetColorOutput(OutputColorName);

			RenderPipelineResource colorOutput{};
			colorOutput.name = OutputColorName;
			colorOutput.type = RenderPipelineResourceType::Attachment;
			colorOutput.format = TextureFormat::R8G8B8A8_UNORM;
			colorOutput.textureUsage = ResourceUsage::RenderTarget | ResourceUsage::UnorderedAccess | ResourceUsage::ShaderResource;

			RenderPipelineResource accumulation{};
			accumulation.name = PathTracerAccumulationName;
			accumulation.type = RenderPipelineResourceType::Texture;
			accumulation.format = TextureFormat::R32G32B32A32_FLOAT;
			accumulation.textureUsage = ResourceUsage::UnorderedAccess | ResourceUsage::ShaderResource;

			return {colorOutput, accumulation};
		}
	};

	struct PathTracerPipeline : RenderPipeline
	{
		SK_CLASS(PathTracerPipeline, RenderPipeline);

		RenderPipelineSetup GetPipelineSetup() override
		{
			RenderPipelineSetup setup;
			setup.modules.EmplaceBack(sktypeid(DepthPrePassModule));
			setup.modules.EmplaceBack(sktypeid(PathTracerModule));
			return setup;
		}
	};

	void RegisterPathTracerPipeline()
	{
		Reflection::Type<PathTracerPass>();
		Reflection::Type<PathTracerModule>();
		Reflection::Type<PathTracerPipeline>();
	}
}