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

#include "SceneViewRenderer.hpp"

#include "imgui.h"
#include "SceneEditor.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Components/StaticMeshRenderer.hpp"


namespace Skore
{
	SceneViewRenderer::~SceneViewRenderer()
	{
		if (gridPipeline) gridPipeline->Destroy();
		if (maskPipeline) maskPipeline->Destroy();
		if (unlitPipeline) unlitPipeline->Destroy();
		if (compositeMaskPipeline) compositeMaskPipeline->Destroy();
		if (maskTexture) maskTexture->Destroy();
		if (maskRenderPass) maskRenderPass->Destroy();
		if (compositeMaskTexture) compositeMaskTexture->Destroy();
		if (compositeMaskRenderPass) compositeMaskRenderPass->Destroy();
		if (maskDescriptorSet) maskDescriptorSet->Destroy();
		if (compositeMaskDescriptorSet) compositeMaskDescriptorSet->Destroy();
		if (debugPhysicsPipeline) debugPhysicsPipeline->Destroy();
	}

	void SceneViewRenderer::Resize(Extent extent)
	{
		currentExtent = extent;

		if (!maskDescriptorSet)
		{
			maskDescriptorSet = Graphics::CreateDescriptorSet({
				.bindings = {
					DescriptorSetLayoutBinding{
						.binding = 0,
						.descriptorType = DescriptorType::Sampler
					},
					DescriptorSetLayoutBinding{
						.binding = 1,
						.descriptorType = DescriptorType::SampledImage
					}
				}
			});

			maskDescriptorSet->UpdateSampler(0, Graphics::GetLinearSampler());
		}

		if (!compositeMaskDescriptorSet)
		{
			compositeMaskDescriptorSet = Graphics::CreateDescriptorSet({
				.bindings = {
					DescriptorSetLayoutBinding{
						.binding = 0,
						.descriptorType = DescriptorType::Sampler
					},
					DescriptorSetLayoutBinding{
						.binding = 1,
						.descriptorType = DescriptorType::SampledImage
					}
				}
			});

			compositeMaskDescriptorSet->UpdateSampler(0, Graphics::GetLinearSampler());
		}

		if (maskTexture)
		{
			maskTexture->Destroy();
		}

		if (maskRenderPass)
		{
			maskRenderPass->Destroy();
		}

		if (compositeMaskTexture)
		{
			compositeMaskTexture->Destroy();
		}

		if (compositeMaskRenderPass)
		{
			compositeMaskRenderPass->Destroy();
		}

		maskTexture = Graphics::CreateTexture({
			.extent = extent,
			.format = TextureFormat::R8G8B8A8_UNORM,
			.usage = ResourceUsage::RenderTarget | ResourceUsage::ShaderResource,
			.debugName = "SelectPickerMask_Texture"
		});

		maskRenderPass = Graphics::CreateRenderPass({
			.attachments = {
				AttachmentDesc{
					.texture = maskTexture,
					.initialState = ResourceState::Undefined,
					.finalState = ResourceState::ColorAttachment,
					.loadOp = AttachmentLoadOp::Clear,
					.storeOp = AttachmentStoreOp::Store,
				},
			}
		});

		compositeMaskTexture = Graphics::CreateTexture({
			.extent = extent,
			.format = TextureFormat::R8G8B8A8_UNORM,
			.usage = ResourceUsage::RenderTarget | ResourceUsage::ShaderResource,
			.debugName = "SelectPickerMask_Texture"
		});

		compositeMaskRenderPass = Graphics::CreateRenderPass({
			.attachments = {
				AttachmentDesc{
					.texture = compositeMaskTexture,
					.initialState = ResourceState::Undefined,
					.finalState = ResourceState::ColorAttachment,
					.loadOp = AttachmentLoadOp::Clear,
					.storeOp = AttachmentStoreOp::Store,
				},
			}
		});

		maskDescriptorSet->UpdateTexture(1, maskTexture);
		compositeMaskDescriptorSet->UpdateTexture(1, compositeMaskTexture);
	}

	void SceneViewRenderer::Render(SceneEditor* sceneEditor, GPURenderPass* renderPass, GPUDescriptorSet* sceneDescriptorSet, GPUCommandBuffer* cmd)
	{
		if (drawSelectionOutline)
		{
			if (!unlitPipeline)
			{
				GraphicsPipelineDesc pipelineDesc = {
					.shader = Resources::FindByPath("Skore://Shaders/Unlit.raster"),
					.blendStates = {
						BlendStateDesc{
						}
					},
					.renderPass = maskRenderPass,
					.vertexInputStride = sizeof(MeshStaticVertex)
				};
				unlitPipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
			}

			if (!maskPipeline)
			{
				GraphicsPipelineDesc pipelineDesc = {
					.shader = Resources::FindByPath("Skore://Shaders/MaskShader.raster"),
					.blendStates = {
						BlendStateDesc{
						}
					},
					.renderPass = compositeMaskRenderPass,
				};
				maskPipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
			}

			RenderStorage* storage = sceneEditor->GetCurrentScene()->GetRenderStorage();

			Span<RID> selectedEntities = sceneEditor->GetSelectedEntities();

			if (!selectedEntities.Empty())
			{
				cmd->BeginDebugMarker("Selection Mask", Vec4{0, 0, 0, 1});
				cmd->BeginRenderPass(maskRenderPass, {1.0, 1.0, 1.0, 0.0}, 1.0f, 0);

				ViewportInfo viewportInfo{};
				viewportInfo.x = 0.;
				viewportInfo.y = 0.;
				viewportInfo.y = 0.f;
				viewportInfo.width = (f32)currentExtent.width;
				viewportInfo.height = (f32)currentExtent.height;
				viewportInfo.minDepth = 0.;
				viewportInfo.maxDepth = 1.;

				cmd->SetViewport(viewportInfo);
				cmd->SetScissor({0, 0}, currentExtent);

				cmd->BindPipeline(unlitPipeline);
				cmd->BindDescriptorSet(unlitPipeline, 0, sceneDescriptorSet, {});

				std::function<void(Entity* entity)> drawEntity;
				drawEntity = [&](Entity* entity)
				{
					if (!entity) return;
					do
					{
						if (StaticMeshRenderer* staticMeshRenderer = entity->GetComponent<StaticMeshRenderer>())
						{
							if (auto it = storage->staticMeshes.find(staticMeshRenderer); it !=  storage->staticMeshes.end())
							{
								MeshRenderData& meshRenderData = it->second;

								if (meshRenderData.mesh && meshRenderData.visible)
								{
									if (!meshRenderData.mesh->vertexBuffer)
									{
										break;
									}

									if (!meshRenderData.mesh->indexBuffer)
									{
										break;
									}

									cmd->BindVertexBuffer(0, {meshRenderData.mesh->vertexBuffer}, {0});
									cmd->BindIndexBuffer(meshRenderData.mesh->indexBuffer, 0, IndexType::Uint32);
									cmd->PushConstants(unlitPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &meshRenderData.transform);

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
					} while (false);

					for (Entity* child: entity->GetChildren())
					{
						drawEntity(child);
					}
				};

				for (RID selected : selectedEntities)
				{
					if (Entity* entity = sceneEditor->GetCurrentScene()->FindEntityByRID(selected))
					{
						//skip root, may be slow
						if (sceneEditor->GetCurrentScene()->GetRootEntity() != entity)
						{
							drawEntity(entity);
						}
					}
				}

				cmd->EndRenderPass();

				cmd->ResourceBarrier(maskTexture, ResourceState::ColorAttachment, ResourceState::ShaderReadOnly, 0, 0);

				cmd->BeginRenderPass(compositeMaskRenderPass, {0.0, 0.0, 0.0, 0.0}, 1.0f, 0);

				viewportInfo.y = (f32)currentExtent.height;
				viewportInfo.width = (f32)currentExtent.width;
				viewportInfo.height = -(f32)currentExtent.height;


				cmd->SetViewport(viewportInfo);
				cmd->SetScissor({0, 0}, currentExtent);

				cmd->BindPipeline(maskPipeline);
				cmd->BindDescriptorSet(maskPipeline, 0, maskDescriptorSet, {});


				struct TextureInfo
				{
					Vec4 textureInfo{0, 0, 0, 0};
				};

				TextureInfo info;
				info.textureInfo = Vec4(1.0f / currentExtent.width, 1.0f / currentExtent.height, 0, 0);
				cmd->PushConstants(maskPipeline, ShaderStage::Vertex, 0, sizeof(TextureInfo), &info);

				cmd->Draw(6, 1, 0, 0);

				cmd->EndRenderPass();
				cmd->ResourceBarrier(compositeMaskTexture, ResourceState::ColorAttachment, ResourceState::ShaderReadOnly, 0, 0);

				cmd->EndDebugMarker();
			}
		}
	}

	void SceneViewRenderer::Blit(SceneEditor* sceneEditor, GPURenderPass* renderPass, GPUDescriptorSet* sceneDescriptorSet,  GPUCommandBuffer* cmd)
	{
		if (drawSelectionOutline)
		{
			if (!compositeMaskPipeline)
			{
				GraphicsPipelineDesc pipelineDesc = {
					.shader = Resources::FindByPath("Skore://Shaders/CompositeMaskShader.raster"),
					.blendStates = {
						BlendStateDesc{
						}
					},
					.renderPass = renderPass,
				};
				compositeMaskPipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
			}

			if (!sceneEditor->GetSelectedEntities().Empty())
			{
				cmd->BindPipeline(compositeMaskPipeline);
				cmd->BindDescriptorSet(compositeMaskPipeline, 0, compositeMaskDescriptorSet, {});
				cmd->Draw(6, 1, 0, 0);
			}
		}

		if (drawGrid)
		{
			if (gridPipeline == nullptr)
			{
				GraphicsPipelineDesc pipelineDesc = {
					.shader = Resources::FindByPath("Skore://Shaders/DrawGrid.raster"),
					.depthStencilState = {
						.depthTestEnable = true,
						.depthWriteEnable = true,
						.depthCompareOp = CompareOp::Less
					},
					.blendStates = {
						BlendStateDesc{
							.blendEnable = true,
							.colorBlendOp = BlendOp::Add,
							.alphaBlendOp = BlendOp::Max,
						}
					},
					.renderPass = renderPass
				};
				gridPipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
			}

			cmd->BindPipeline(gridPipeline);
			cmd->BindDescriptorSet(gridPipeline, 0, sceneDescriptorSet, {});
			cmd->Draw(6, 1, 0, 0);
		}

		if (drawDebugPhysics)
		{
			if (!debugPhysicsPipeline)
			{
				debugPhysicsPipeline = Graphics::CreateGraphicsPipeline({
					.shader = Resources::FindByPath("Skore://Shaders/DebugPhysics.raster"),
					.rasterizerState = {
						.polygonMode = PolygonMode::Line,
						.lineWidth = 2.0f * ImGui::GetStyle().ScaleFactor,
					},
					.depthStencilState = {
						.depthTestEnable = true,
						.depthWriteEnable = true,
						.depthCompareOp = CompareOp::Less
					},
					.blendStates = {
						BlendStateDesc{}
					},
					.renderPass = renderPass,
					.vertexInputStride = DebugPhysicsVertexSize
				});
			}

			if (Scene* scene = sceneEditor->GetCurrentScene())
			{
				cmd->BindPipeline(debugPhysicsPipeline);
				cmd->BindDescriptorSet(debugPhysicsPipeline, 0, sceneDescriptorSet, {});
				scene->GetPhysicsScene()->DrawDebugEntities(cmd, debugPhysicsPipeline);
			}
		}
	}
}
