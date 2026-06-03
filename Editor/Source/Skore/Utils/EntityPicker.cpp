#include "Skore/Utils/EntityPicker.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/SceneEditor.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::EntityPicker");

	struct PickerPushConstants
	{
		Mat4 viewProjection;
		Mat4 world;
		u64  entityID;
		u32  vertexByteOffset;
		u32  vertexLayoutIndex;
	};

	EntityPicker::~EntityPicker()
	{
		DestroyObjects();
	}

	void EntityPicker::Resize(Extent extent)
	{
		if (currentExtent != extent)
		{
			DestroyObjects();
			currentExtent = extent;

			texture = Graphics::CreateTexture({
				.extent = currentExtent,
				.format = TextureFormat::R32G32_UINT,
				.usage = ResourceUsage::RenderTarget | ResourceUsage::CopySource,
				.debugName = "EntityPicker_Texture"
			});

			depth = Graphics::CreateTexture({
				.extent = currentExtent,
				.format = TextureFormat::D32_FLOAT,
				.usage = ResourceUsage::DepthStencil,
				.debugName = "EntityPicker_DepthTexture"
			});

			imageBuffer = Graphics::CreateBuffer({
				.size = currentExtent.width * currentExtent.height * 8,
				.usage = ResourceUsage::CopyDest,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "EntityPicker_ImageBuffer"
			});

			renderPass = Graphics::CreateRenderPass({
				.attachments = {
					AttachmentDesc{
						.initialState = ResourceState::Undefined,
						.finalState = ResourceState::ColorAttachment,
						.loadOp = AttachmentLoadOp::Clear,
						.storeOp = AttachmentStoreOp::Store,
						.format = TextureFormat::R32G32_UINT
					},
					AttachmentDesc{
						.initialState = ResourceState::Undefined,
						.finalState = ResourceState::DepthStencilAttachment,
						.loadOp = AttachmentLoadOp::Clear,
						.storeOp = AttachmentStoreOp::Store,
						.format = TextureFormat::D32_FLOAT,
					}
				}
			});

			framebuffer = Graphics::CreateFramebuffer({
				.renderPass = renderPass,
				.attachments = {texture->GetTextureView(), depth->GetTextureView()}
			});

			// Picker pipelines are created lazily per vertex stride
		}
	}

	Entity* EntityPicker::PickEntity(Mat4 viewProjection, SceneEditor* sceneEditor, Vec2 mousePosition)
	{

		if (static_cast<u32>(mousePosition.x + 1) > currentExtent.width || static_cast<u32>(mousePosition.y + 1) > currentExtent.height) return {};

		if (sceneEditor && sceneEditor->GetCurrentScene())
		{
			PickerPushConstants pushConstants = {};
			pushConstants.viewProjection = viewProjection;

			Scene*              scene = sceneEditor->GetCurrentScene();
			RenderSceneObjects* objects = &scene->renderObjects;
			if (objects->GetVisiblePipelineCount() == 0)
			{
				return {};
			}

			if (cachedPipelineOwner != nullptr && cachedPipelineOwner != scene)
			{
				CleanupPipelines();
			}
			cachedPipelineOwner = scene;

			// Grow picker pipelines to match scene pipeline count
			objects->ForEachVisiblePipeline([&](u32 index, const DrawPipeline& pipeline)
			{
				if (index >= pickerPipelines.Size())
				{
					GPUPipeline* p = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
						.shader = Resources::FindByPath("Skore://Shaders/EntityPicking.raster"),
						.depthStencilState = {
							.depthTestEnable = true,
							.depthWriteEnable = true,
							.depthCompareOp = CompareOp::GreaterEqual // reverse-Z
						},
						.blendStates = {
							BlendStateDesc{}
						},
						.renderPass = renderPass,
						.descriptorSetsOverride = {
							DescriptorSetOverride{
								.set = 0,
								.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
							}
						}
					});
					pickerPipelines.EmplaceBack(p);
				}
			});

			GPUCommandBuffer* cmd = Graphics::GetFreeCommandBuffer();

			cmd->Begin();
			cmd->BeginDebugMarker("Entity Picker", Vec4{0, 0, 0, 1});


			ClearValues clearValues;
			clearValues.color = Vec4(0.0f, 0.0f, 0.0f, 0.0f);

			BeginRenderPassInfo beginInfo;
			beginInfo.renderPass = renderPass;
			beginInfo.framebuffer = framebuffer;
			beginInfo.clearValues = &clearValues;
			cmd->BeginRenderPass(beginInfo);

			ViewportInfo viewportInfo{};
			viewportInfo.x = 0.;
			viewportInfo.y = 0.;
			viewportInfo.y = (f32)currentExtent.height;
			viewportInfo.width = (f32)currentExtent.width;
			viewportInfo.height = -(f32)currentExtent.height;

			viewportInfo.minDepth = 0.;
			viewportInfo.maxDepth = 1.;

			cmd->SetViewport(viewportInfo);
			cmd->SetScissor({0, 0}, currentExtent);

			cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

			objects->ForEachVisiblePipeline([&](u32 index, const DrawPipeline& drawPipeline)
			{
				GPUPipeline* pipeline = pickerPipelines[index];
				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());

				for (const Drawcall& drawcall : drawPipeline.drawcalls)
				{
					pushConstants.world = drawcall.transform;
					pushConstants.entityID = drawcall.userData;
					pushConstants.vertexByteOffset = drawcall.vertexByteOffset;
					pushConstants.vertexLayoutIndex = drawcall.vertexLayoutIndex;
					cmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(PickerPushConstants), &pushConstants);

					cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
				}
			});

			cmd->EndRenderPass();

			cmd->ResourceBarrier(texture, ResourceState::ColorAttachment, ResourceState::CopySource, 0, 0);
			cmd->CopyTextureToBuffer({
				.buffer = imageBuffer,
				.texture = texture,
				.extent = Extent3D(currentExtent.width, currentExtent.height, 1),
			});
			cmd->ResourceBarrier(imageBuffer, ResourceState::Undefined, ResourceState::CopyDest);
			cmd->EndDebugMarker();
			cmd->End();

			Graphics::SubmitGPUWork(cmd, true);
			Graphics::AddFreeCommandBuffer(cmd);

			u32   bytesPerPixel = 8;
			u8*   data = static_cast<u8*>(imageBuffer->GetMappedData());
			usize pixelOffset = (mousePosition.y * currentExtent.width + mousePosition.x) * bytesPerPixel;

			if (imageBuffer->GetDesc().size <= pixelOffset)
			{
				return {};
			}

			u64 entityPtr = *reinterpret_cast<u64*>(data + pixelOffset);
			return static_cast<Entity*>(IntToPtr(entityPtr));
		}
		return {};
	}

	void EntityPicker::CleanupPipelines()
	{
		for (GPUPipeline* p : pickerPipelines)
		{
			if (p) p->Destroy();
		}
		pickerPipelines.Clear();
	}

	void EntityPicker::DestroyObjects()
	{
		if (texture) texture->Destroy();
		if (depth) depth->Destroy();
		if (renderPass) renderPass->Destroy();
		if (framebuffer) framebuffer->Destroy();
		for (GPUPipeline* p : pickerPipelines)
		{
			if (p) p->Destroy();
		}
		pickerPipelines.Clear();
		if (imageBuffer) imageBuffer->Destroy();

		texture = nullptr;
		imageBuffer = nullptr;
		renderPass = nullptr;
	}
}
