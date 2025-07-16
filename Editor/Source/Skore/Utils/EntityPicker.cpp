// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "EntityPicker.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
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
		u32  padding[2];
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
						.texture = texture,
						.initialState = ResourceState::Undefined,
						.finalState = ResourceState::ColorAttachment,
						.loadOp = AttachmentLoadOp::Clear,
						.storeOp = AttachmentStoreOp::Store,
					},
					AttachmentDesc{
						.texture = depth,
						.initialState = ResourceState::Undefined,
						.finalState = ResourceState::DepthStencilAttachment,
						.loadOp = AttachmentLoadOp::Clear,
						.storeOp = AttachmentStoreOp::Store,
					}
				}
			});

			GraphicsPipelineDesc pipelineDesc = {
				.shader = Resources::FindByPath("Skore://Shaders/EntityPicking.raster"),
				.depthStencilState = {
					.depthTestEnable = true,
					.depthWriteEnable = true,
					.depthCompareOp = CompareOp::LessEqual
				},
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = renderPass,
				.vertexInputStride = sizeof(MeshStaticVertex)
			};

			opaquePipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
			pipelineDesc.vertexInputStride = sizeof(MeshSkeletalVertex);
			skinnedPipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
		}
	}

	RID EntityPicker::PickEntity(Mat4 viewProjection, SceneEditor* sceneEditor, Vec2 mousePosition)
	{
		if (sceneEditor && sceneEditor->GetCurrentScene())
		{
			PickerPushConstants pushConstants = {};
			pushConstants.viewProjection = viewProjection;

			Scene* scene = sceneEditor->GetCurrentScene();
			RenderStorage* storage = scene->GetRenderStorage();

			GPUCommandBuffer* cmd = Graphics::GetResourceCommandBuffer();

			cmd->Begin();
			cmd->BeginDebugMarker("Entity Picker", Vec4{0, 0, 0, 1});
			cmd->BeginRenderPass(renderPass, {0.0, 0.0, 0.0, 0.0}, 1.0f, 0);

			ViewportInfo viewportInfo{};
			viewportInfo.x = 0.;
			viewportInfo.y = 0.;
			viewportInfo.y = (f32)currentExtent.height;
			viewportInfo.width = (f32)currentExtent.width;
			viewportInfo.height = -(f32)currentExtent.height;

			// viewportInfo.width = currentExtent.width;
			// viewportInfo.height = currentExtent.height;

			viewportInfo.minDepth = 0.;
			viewportInfo.maxDepth = 1.;

			cmd->SetViewport(viewportInfo);
			cmd->SetScissor({0, 0}, currentExtent);


			if (!storage->staticMeshes.empty())
			{
				cmd->BindPipeline(opaquePipeline);

				for (auto& meshIt : storage->staticMeshes)
				{
					MeshRenderData& meshRenderData = meshIt.second;

					if (meshRenderData.mesh && meshRenderData.visible)
					{
						if (!meshRenderData.mesh->vertexBuffer)
						{
							continue;
						}

						if (!meshRenderData.mesh->indexBuffer)
						{
							continue;
						}

						cmd->BindVertexBuffer(0, {meshRenderData.mesh->vertexBuffer}, {0});
						cmd->BindIndexBuffer(meshRenderData.mesh->indexBuffer, 0, IndexType::Uint32);

						pushConstants.world = meshRenderData.transform;
						pushConstants.entityID = meshRenderData.id;

						cmd->PushConstants(opaquePipeline, ShaderStage::Vertex, 0, sizeof(PickerPushConstants), &pushConstants);

						for (MeshPrimitive& primitive : meshRenderData.mesh->primitives)
						{
							GPUDescriptorSet* materialDs = meshRenderData.GetMaterial(primitive.materialIndex);
							if (materialDs == nullptr)
							{
								continue;
							}
							cmd->DrawIndexed(primitive.indexCount, 1, primitive.firstIndex, 0, 0);
						}
					}
				}
			}

			cmd->EndRenderPass();

			cmd->ResourceBarrier(texture, ResourceState::ColorAttachment, ResourceState::CopySource, 0, 0);
			cmd->CopyTextureToBuffer(texture, imageBuffer, Extent3D(currentExtent.width, currentExtent.height, 1), 0, 0);
			cmd->ResourceBarrier(imageBuffer, ResourceState::Undefined, ResourceState::CopyDest);
			cmd->EndDebugMarker();
			cmd->End();
			cmd->SubmitAndWait();

			u32 bytesPerPixel = 8;
			u8* data = static_cast<u8*>(imageBuffer->GetMappedData());
			usize pixelOffset = (mousePosition.y * currentExtent.width + mousePosition.x) * bytesPerPixel;
			u64 entityId = *reinterpret_cast<u64*>(data + pixelOffset);

			return {entityId};
		}

		return {};
	}

	void EntityPicker::DestroyObjects()
	{
		if (texture) texture->Destroy();
		if (depth) depth->Destroy();
		if (renderPass) renderPass->Destroy();
		if (opaquePipeline) opaquePipeline->Destroy();
		if (skinnedPipeline) skinnedPipeline->Destroy();
		if (imageBuffer) imageBuffer->Destroy();

		texture = nullptr;
		imageBuffer = nullptr;
		renderPass = nullptr;
		opaquePipeline = nullptr;
		skinnedPipeline = nullptr;
	}
}
