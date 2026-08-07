#include "Skore/Utils/PreviewGenerator.hpp"

#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Graphics/Pipeline/PipelineCommon.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/EnvironmentComponent.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"
#include "Skore/Scene/Components/Transform.hpp"

namespace Skore
{
	namespace
	{
		constexpr const char* PreviewColorMSName = "PreviewColorMS";
		constexpr const char* PreviewDepthMSName = "PreviewDepthMS";
		constexpr const char* PreviewColorName = "PreviewColor";
		constexpr const char* ThumbnailBufferName = "ThumbnailBuffer";

		//standalone thumbnail renderer built directly on a RenderGraph instead of DefaultRenderPipeline:
		//shadows + light setup reuse the runtime passes, geometry renders through a dedicated preview
		//forward pass (MSAA, editor preview shaders, inline tonemapping, no post-process).
		struct PreviewRenderer
		{
			SK_NO_COPY_CONSTRUCTOR(PreviewRenderer);

			struct MeshPushConstants
			{
				Mat4 world;
				u32  materialIndex;
				u32  vertexByteOffset;
				u32  vertexLayoutIndex;
				u32  useInstanceData;
				u32  boneBufferIndex;
			};

			struct PipelineBucket
			{
				Array<GPUPipeline*> pipelines;
				Array<RID>          variants;

				void Cleanup()
				{
					for (GPUPipeline* pipeline : pipelines)
					{
						if (pipeline) pipeline->Destroy();
					}
					pipelines.Clear();
					variants.Clear();
				}
			};

			RenderGraph                 renderGraph;
			Array<DefaultPipelinePass*> scenePasses;
			PipelineBucket              opaqueBucket;
			PipelineBucket              transparentBucket;
			GPUPipeline*                skyPipeline = nullptr;

			PreviewRenderer()
			{
				//cascade shadows must build before the light setup so the shadow map and instance data
				//they create are available when the scene descriptor bindings are written
				for (const char* passType : {"Skore::CascadeShadowPass", "Skore::LightSetupPass"})
				{
					ReflectType* reflectType = Reflection::FindTypeByName(passType);
					if (reflectType == nullptr) continue;

					Object* object = reflectType->NewObject();
					if (object == nullptr) continue;

					if (DefaultPipelinePass* pass = object->SafeCast<DefaultPipelinePass>())
					{
						scenePasses.EmplaceBack(pass);
					}
					else
					{
						DestroyAndFree(object);
					}
				}
			}

			~PreviewRenderer()
			{
				opaqueBucket.Cleanup();
				transparentBucket.Cleanup();
				if (skyPipeline) skyPipeline->Destroy();

				for (DefaultPipelinePass* pass : scenePasses)
				{
					DestroyAndFree(pass);
				}
			}

			void Render(GPUCommandBuffer* cmd, Scene* scene)
			{
				//graph-owned resources (shadow map, scene buffers) only exist after a first execution,
				//so warm up with one pass; the render that lands in the thumbnail is the second one
				for (u32 i = 0; i < 2; ++i)
				{
					renderGraph.Begin(scene);
					BuildRenderGraph();
					renderGraph.Execute(cmd);
				}
			}

			void BuildRenderGraph()
			{
				for (DefaultPipelinePass* pass : scenePasses)
				{
					pass->BuildRenderGraph(renderGraph);
				}

				u32 samples = Math::Min(Graphics::GetDevice()->GetProperties().limits.maxAttachmentSamples, 4u);

				renderGraph.Create(PreviewColorMSName, RenderGraphTextureDesc{
					.format = Format::RGBA8_UNORM,
					.extent = Extent{0, 0},
					.samples = samples,
					.clearColor = Vec4(0.10f, 0.11f, 0.13f, 1.0f)
				});

				renderGraph.Create(PreviewDepthMSName, RenderGraphTextureDesc{
					.format = Format::D32_FLOAT,
					.extent = Extent{0, 0},
					.samples = samples
				});

				renderGraph.Create(PreviewColorName, RenderGraphTextureDesc{
					.format = Format::RGBA8_UNORM,
					.extent = Extent{0, 0},
					.usage = ResourceUsage::CopySource
				});

				renderGraph.SetColorOutput(PreviewColorName);
				renderGraph.SetDepthOutput(PreviewDepthMSName);

				renderGraph
					.AddGraphicsPass("PreviewForward")
					.Stage(RenderStage::Forward)
					.Read(LightInstanceDataName)
					.Read(ShadowMapTextureName)
					.Write(PreviewColorMSName)
					.Write(PreviewDepthMSName)
					.Write(PreviewColorName)
					.Resolve(PreviewColorName)
					.InvertViewport(true)
					.Render([this](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
					{
						RenderScene(pass, scene, cmd);
					});

				Extent extent = renderGraph.GetOutputSize();

				renderGraph.Create(ThumbnailBufferName, RenderGraphBufferDesc{
					.size = static_cast<usize>(extent.width) * extent.height * 4,
					.usage = ResourceUsage::CopyDest,
					.hostVisible = true,
					.persistentMapped = true
				});

				renderGraph
					.AddPass("CopyToThumbnailBuffer")
					.Stage(RenderStage::Swapchain)
					.Read(PreviewColorName)
					.Write(ThumbnailBufferName)
					.Render([](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
					{
						RenderGraph& rg = *pass.GetGraph();

						GPUTexture* outputTexture = rg.GetTexture(PreviewColorName);
						GPUBuffer*  outputBuffer = rg.GetBuffer(ThumbnailBufferName);
						if (outputTexture == nullptr || outputBuffer == nullptr) return;

						cmd->CopyTextureToBuffer({
							.buffer = outputBuffer,
							.texture = outputTexture,
							.extent = outputTexture->GetDesc().extent,
						});
					});
			}

			void RenderScene(RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
			{
				if (scene == nullptr || pass.GetRenderPass() == nullptr) return;

				RenderGraph& rg = *pass.GetGraph();

				GPUDescriptorSet* globalSet = RenderResourceCache::GetGlobalDescriptorSet();
				GPUDescriptorSet* sceneSet = rg.GetSceneDescriptorSet();
				if (globalSet == nullptr || sceneSet == nullptr) return;

				LightInstanceData* lightData = rg.GetInstanceData<LightInstanceData>(LightInstanceDataName);
				if (lightData == nullptr || !lightData->sceneBindingsReady) return;

				RenderSceneObjects* objects = &scene->renderObjects;

				GPURenderPass*    renderPass = pass.GetRenderPass();
				GPUDescriptorSet* pipelineSceneSet = rg.GetSceneDescriptorSet(0);
				GPUDescriptorSet* skinningSet = objects->GetSkinningDescriptorSet();

				EnsurePipelines(opaqueBucket, objects->opaquePipelines, renderPass, pipelineSceneSet, globalSet, skinningSet, false);
				EnsurePipelines(transparentBucket, objects->transparentPipelines, renderPass, pipelineSceneSet, globalSet, skinningSet, true);

				cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

				RenderBucket(cmd, rg, opaqueBucket, objects->opaquePipelines, sceneSet, globalSet, skinningSet);
				RenderSky(cmd, rg, scene, renderPass);
				RenderBucket(cmd, rg, transparentBucket, objects->transparentPipelines, sceneSet, globalSet, skinningSet);
			}

			void EnsurePipelines(PipelineBucket& bucket, Array<DrawPipeline>& drawPipelines, GPURenderPass* renderPass, GPUDescriptorSet* sceneSet, GPUDescriptorSet* globalSet, GPUDescriptorSet* skinningSet, bool blendEnable)
			{
				RID previewShader = Resources::FindByPath("Skore://Shaders/Editor/PreviewForward.shader");
				if (!previewShader) return;

				u32 count = static_cast<u32>(drawPipelines.Size());
				if (bucket.pipelines.Size() < count)
				{
					bucket.pipelines.Resize(count, nullptr);
					bucket.variants.Resize(count);
				}

				for (u32 i = 0; i < count; ++i)
				{
					const DrawPipelineDesc& desc = drawPipelines[i].desc;

					RID material = {};
					RID variant = {};
					if (desc.materialGraph)
					{
						variant = RenderResourceCache::EnsureMaterialVariant(previewShader, desc.materialGraph, "Default");
						if (variant) material = desc.materialGraph;
					}
					if (!variant)
					{
						variant = ShaderResource::GetVariant(previewShader, "Default");
					}

					if (bucket.pipelines[i] && bucket.variants[i] == variant)
					{
						continue;
					}

					if (bucket.pipelines[i]) bucket.pipelines[i]->Destroy();

					GraphicsPipelineDesc gpuDesc;
					gpuDesc.shader = previewShader;
					gpuDesc.variant = "Default";
					gpuDesc.material = material;
					gpuDesc.rasterizerState.cullMode = desc.cullMode;
					gpuDesc.depthStencilState.depthTestEnable = true;
					gpuDesc.depthStencilState.depthWriteEnable = desc.depthWrite;
					gpuDesc.depthStencilState.depthCompareOp = desc.depthTest;
					gpuDesc.blendStates = {BlendStateDesc{.blendEnable = blendEnable}};
					gpuDesc.renderPass = renderPass;
					gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 1, .descriptorSet = sceneSet});
					gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 2, .descriptorSet = globalSet});
					gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 3, .descriptorSet = skinningSet});

					bucket.pipelines[i] = Graphics::CreateGraphicsPipeline(gpuDesc);
					bucket.variants[i] = variant;
				}
			}

			void RenderBucket(GPUCommandBuffer* cmd, RenderGraph& rg, PipelineBucket& bucket, Array<DrawPipeline>& drawPipelines, GPUDescriptorSet* sceneSet, GPUDescriptorSet* globalSet, GPUDescriptorSet* skinningSet)
			{
				for (u32 i = 0; i < drawPipelines.Size(); ++i)
				{
					GPUPipeline* pipeline = i < bucket.pipelines.Size() ? bucket.pipelines[i] : nullptr;
					if (!pipeline) continue;

					ShaderStage pushConstantStages = ShaderStage::None;
					for (const PushConstantRange& range : pipeline->GetPipelineDesc().pushConstants)
					{
						pushConstantStages |= range.stages;
					}

					cmd->BindPipeline(pipeline);
					cmd->BindDescriptorSet(pipeline, 1, sceneSet);
					cmd->BindDescriptorSet(pipeline, 2, globalSet);
					cmd->BindDescriptorSet(pipeline, 3, skinningSet);

					for (const Drawcall& drawcall : drawPipelines[i].drawcalls)
					{
						if (!drawcall.material || drawcall.material->materialIndex == U32_MAX) continue;
						if ((drawcall.layerMask & rg.camera.cullingMask) == 0) continue;

						MeshPushConstants pc;
						pc.world = drawcall.transform;
						pc.materialIndex = drawcall.material->materialIndex;
						pc.vertexByteOffset = drawcall.vertexByteOffset;
						pc.vertexLayoutIndex = drawcall.vertexLayoutIndex;
						pc.useInstanceData = 0;
						pc.boneBufferIndex = drawcall.boneBufferIndex;

						if (pushConstantStages != ShaderStage::None)
						{
							cmd->PushConstants(pipeline, pushConstantStages, 0, sizeof(MeshPushConstants), &pc);
						}
						cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
					}
				}
			}

			void RenderSky(GPUCommandBuffer* cmd, RenderGraph& rg, Scene* scene, GPURenderPass* renderPass)
			{
				EnvironmentResourceCache* skyCache = nullptr;
				scene->Iterate<EnvironmentComponent>([&](EnvironmentComponent* env)
				{
					if (skyCache != nullptr || !env->GetUseSkyboxAsBackground()) return;
					EnvironmentResourceCache* cache = env->GetEnvironmentCache();
					if (cache && cache->descriptorSet) skyCache = cache;
				});

				if (skyCache == nullptr) return;

				if (skyPipeline == nullptr)
				{
					DepthStencilStateDesc depthStencilState;
					depthStencilState.depthTestEnable = true;
					depthStencilState.depthWriteEnable = false;
					depthStencilState.depthCompareOp = CompareOp::GreaterEqual; // reverse-Z: skybox sits at far (z=0)

					skyPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
						.shader = Resources::FindByPath("Skore://Shaders/Editor/PreviewSky.raster"),
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

				if (skyPipeline == nullptr) return;

				Mat4 viewProj = rg.camera.projection * Mat4(Mat34(rg.camera.view));

				cmd->BindPipeline(skyPipeline);
				cmd->PushConstants(skyPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &viewProj);
				cmd->BindDescriptorSet(skyPipeline, 3, skyCache->descriptorSet, {});
				cmd->Draw(36, 1, 0, 0);
			}
		};
	}


	void PreviewGenerator::SetupDefaultEnvironment(Scene* scene)
	{
		if (scene->FindFirstComponent(sktypeid(LightComponent)) == nullptr)
		{
			Entity*    lightEntity = scene->CreateEntity();
			Transform* envTransform = lightEntity->AddComponent<Transform>();
			envTransform->SetRotationEuler(Vec3(-45.0f, -20.0f, -20.0f));

			LightComponent* light = lightEntity->AddComponent<LightComponent>();
			light->SetIntensity(2.0);
			light->SetEnableShadows(true);
		}

		if (scene->FindFirstComponent(sktypeid(EnvironmentComponent)) == nullptr)
		{
			Entity* env = scene->CreateEntity();
			EnvironmentComponent* envComp = env->AddComponent<EnvironmentComponent>();
			envComp->SetPanoramicTexture(Resources::FindByPath("Skore://Materials/autumn_field_puresky_1k.texture"));
		}
	}

	void PreviewGenerator::PopulateScene(Scene* scene)
	{
		SetupScene(scene);
		SetupDefaultEnvironment(scene);
	}

	void PreviewGenerator::GenerateThumbnail()
	{
		Scene* scene = Alloc<Scene>();

		scene->renderObjects.asyncLoad = false;
		scene->renderObjects.requireTlas = false;
		scene->renderObjects.bindEvents = false;

		PopulateScene(scene);

		{
			scene->ExecuteEvents(false);
			Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
			{
				scene->renderObjects.Begin(cmd);
			});
		}

		Vec3 center = {};
		f32  radius = 0;
		f32 fov = 60.0f;

		AABB aabb = scene->GetBounds();

		if (aabb)
		{
			center = aabb.GetCenter();
			radius = aabb.GetRadius();
		}
		else
		{
			radius = 7.0f;
		}

		f32  distance = radius / tan(Math::Radians(fov) * PercentageInScreen());

		f32 elevation = Math::Radians(30.0f);
		f32 azimuth = Math::Radians(45.0f);

		Vec3 cameraOffset = Vec3(
			distance * Math::Cos(elevation) * Math::Sin(azimuth),
			distance * Math::Sin(elevation),
			distance * Math::Cos(elevation) * Math::Cos(azimuth)
		);

		Vec3 cameraPos = center + cameraOffset;

		Quat pitchRotation = Quat::AngleAxis(-elevation, Vec3{1.0f, 0.0f, 0.0f});
		Quat yawRotation = Quat::AngleAxis(azimuth, Vec3{0.0f, 1.0f, 0.0f});
		Quat cameraRotation = Quat::Normalized(yawRotation * pitchRotation);

		Mat4 camera = Mat4::Inverse(Mat4::Translate(Mat4{1.0}, cameraPos) * Quat::ToMatrix4(cameraRotation) * Mat4::Scale(Mat4{1.0}, Vec3(1.0)));

		PreviewRenderer* renderer = Alloc<PreviewRenderer>();

		RenderGraph& renderGraph = renderer->renderGraph;
		renderGraph.SetOutputSize(thumbnailSize);

		Vec2 nearFar = {0.1, 100.0f};
		if (aabb)
		{
			nearFar = Math::GerNearFarFromAABB(aabb, camera);
		}

		renderGraph.UpdateCamera(nearFar.x, nearFar.y, fov, Projection::Perspective, camera, cameraPos);

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			scene->renderObjects.Begin(cmd);
			renderer->Render(cmd, scene);
			scene->renderObjects.End(cmd);
		});

		if (GPUBuffer* buffer = renderGraph.GetBuffer(ThumbnailBufferName))
		{
			Span data(static_cast<u8*>(buffer->GetMappedData()), thumbnailSize.width * thumbnailSize.height * 4);
			ResourceAssets::UpdateThumbnail(asset, data);
		}

		DestroyAndFree(scene);

		DestroyAndFree(renderer);
	}
}
