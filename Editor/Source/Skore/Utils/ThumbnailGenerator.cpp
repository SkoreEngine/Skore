#include "Skore/Utils/ThumbnailGenerator.hpp"

#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/PipelineCommon.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/Camera.hpp"
#include "Skore/Scene/Components/EnvironmentComponent.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"
#include "Skore/Scene/Components/Transform.hpp"

namespace Skore
{
	struct DefaultRenderPipeline;


	struct OutputToBufferPass : RenderPipelinePass
	{
		SK_CLASS(OutputToBufferPass, RenderPipelinePass);

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Graphics;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "OutputBuffer", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd) override
		{
			GPUTexture* outputTexture = context->GetTexture(OutputColorName);
			GPUBuffer* outputBuffer = context->GetBuffer("OutputBuffer");

			cmd->ResourceBarrier(outputTexture, ResourceState::ShaderReadOnly, ResourceState::CopySource, 0, 0);
			cmd->CopyTextureToBuffer(outputTexture, outputBuffer, 0, outputTexture->GetDesc().extent, 0, 0);
			cmd->ResourceBarrier(outputTexture, ResourceState::CopySource, ResourceState::ColorAttachment, 0, 0);
		}
	};


	struct OutputToBufferModule : RenderPipelineModule
	{
		SK_CLASS(OutputToBufferModule, RenderPipelineModule);

		Array<RenderPipelineResource> GetResources() override
		{
			const Extent extent = context->GetOutputSize();
			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{.name = OutputColorName, .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R8G8B8A8_UNORM, .samples = 1, .textureUsage = ResourceUsage::CopySource | ResourceUsage::UnorderedAccess});
			resources.EmplaceBack(RenderPipelineResource{.name = "OutputBuffer", .type = RenderPipelineResourceType::Buffer, .size = extent.width * extent.height * 4, .usage = ResourceUsage::CopyDest, .persistentMapped = true});
			return resources;
		}


		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(OutputToBufferPass));
			return setup;
		}
	};


	void ThumbnailGenerator::GenerateThumbnail(RID asset)
	{
		Scene* scene = Alloc<Scene>();

		SetupScene(scene);

		if (scene->FindFirstComponent(sktypeid(LightComponent)) == nullptr)
		{
			Entity*    lightEntity = scene->CreateEntity();
			Transform* envTransform = lightEntity->AddComponent<Transform>();
			envTransform->SetRotationEuler(Vec3(-45.0f, -20.0f, -20.0f));

			LightComponent* light = lightEntity->AddComponent<LightComponent>();
			light->SetIntensity(2.0);
		}

		if (scene->FindFirstComponent(sktypeid(EnvironmentComponent)) == nullptr)
		{
			Entity* env = scene->CreateEntity();
			EnvironmentComponent* envComp = env->AddComponent<EnvironmentComponent>();
			envComp->SetSkyboxMaterial(Resources::FindByPath("Skore://Materials/DefaultSkyMaterial.material"));
		}

		{
			GPUCommandBuffer* cmd = Graphics::GetFreeCommandBuffer();
			cmd->Begin();
			scene->renderObjects.DoUpdate(cmd);
			cmd->End();
			Graphics::SubmitGPUWork(cmd, true);
			Graphics::AddFreeCommandBuffer(cmd);
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

		RenderPipelineContextSettings settings;
		settings.initialOutputSize = thumbnailSize;
		settings.userData = this;
		Array<TypeID> modules;
		modules.EmplaceBack(sktypeid(OutputToBufferModule));
		RenderPipelineContext* renderPipelineContext = RenderPipeline::CreateContext(sktypeid(DefaultRenderPipeline), modules, settings);

		Vec2 nearFar = {0.1, 100.0f};
		if (aabb)
		{
			nearFar = Math::GerNearFarFromAABB(aabb, camera);
		}

		renderPipelineContext->UpdateCamera(nearFar.x, nearFar.y, fov, Projection::Perspective, camera, cameraPos);

		GPUCommandBuffer* cmd = Graphics::GetFreeCommandBuffer();
		cmd->Begin();
		renderPipelineContext->Execute(cmd, &scene->renderObjects);
		cmd->End();
		Graphics::SubmitGPUWork(cmd, true);
		Graphics::AddFreeCommandBuffer(cmd);

		GPUBuffer* buffer = renderPipelineContext->GetBuffer("OutputBuffer");
		Span data(static_cast<u8*>(buffer->GetMappedData()), thumbnailSize.width * thumbnailSize.height * 4);
		ResourceAssets::UpdateThumbnail(asset, data);

		DestroyAndFree(scene);

		renderPipelineContext->Destroy();
	}

	void RegisterThumbnailGenerationTypes()
	{
		Reflection::Type<OutputToBufferPass>();
		Reflection::Type<OutputToBufferModule>();
	}
}
