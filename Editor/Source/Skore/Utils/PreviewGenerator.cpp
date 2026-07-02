#include "Skore/Utils/PreviewGenerator.hpp"

#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipeline/PipelineCommon.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/Camera.hpp"
#include "Skore/Scene/Components/EnvironmentComponent.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"
#include "Skore/Scene/Components/Transform.hpp"

namespace Skore
{
	namespace
	{
		constexpr const char* ThumbnailBufferName = "ThumbnailBuffer";
	}

	//default pipeline plus a final pass that copies the output color into a
	//host-visible buffer so the thumbnail can be read back on the CPU
	struct ThumbnailRenderPipeline : DefaultRenderPipeline
	{
		SK_CLASS(ThumbnailRenderPipeline, DefaultRenderPipeline);

		void BuildRenderGraph(RenderGraph& renderGraph) override
		{
			DefaultRenderPipeline::BuildRenderGraph(renderGraph);

			Extent extent = renderGraph.GetOutputSize();

			renderGraph.Create(ThumbnailBufferName, RenderGraphBufferDesc{
				.size = static_cast<usize>(extent.width) * extent.height * 4,
				.usage = ResourceUsage::CopyDest,
				.hostVisible = true,
				.persistentMapped = true
			});

			renderGraph
				.AddPass("OutputToBuffer")
				.Stage(RenderStage::Swapchain)
				.Read(PostProcessOutputName)
				.Write(ThumbnailBufferName)
				.Render([](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
				{
					RenderGraph& rg = *pass.GetGraph();

					GPUTexture* outputTexture = rg.GetTexture(PostProcessOutputName);
					GPUBuffer*  outputBuffer = rg.GetBuffer(ThumbnailBufferName);
					if (outputTexture == nullptr || outputBuffer == nullptr) return;

					cmd->CopyTextureToBuffer({
						.buffer = outputBuffer,
						.texture = outputTexture,
						.extent = outputTexture->GetDesc().extent,
					});
				});
		}
	};


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

		RenderPipelineContext* renderPipelineContext = RenderPipelineContext::Create(sktypeid(ThumbnailRenderPipeline));
		RenderGraph&              renderGraph = renderPipelineContext->GetRenderGraph();
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
			renderPipelineContext->Execute(cmd, scene);
			scene->renderObjects.End(cmd);
		});

		GPUBuffer* buffer = renderGraph.GetBuffer(ThumbnailBufferName);
		Span data(static_cast<u8*>(buffer->GetMappedData()), thumbnailSize.width * thumbnailSize.height * 4);
		ResourceAssets::UpdateThumbnail(asset, data);

		DestroyAndFree(scene);

		DestroyAndFree(renderPipelineContext);
	}

	void RegisterThumbnailGenerationTypes()
	{
		Reflection::Type<ThumbnailRenderPipeline>();
	}
}
