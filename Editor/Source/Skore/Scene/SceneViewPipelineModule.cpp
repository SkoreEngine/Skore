#include "imgui.h"
#include "Skore/Scene/SceneEditor.hpp"
#include "Skore/Selection.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/DebugDraw.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/IO/Input.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Navigation/Navigation.hpp"
#include "Skore/Navigation/NavigationCommon.hpp"
#include "Skore/Window/SceneViewWindow.hpp"

namespace Skore
{
	struct SceneViewDrawOutlinePass : RenderPipelinePass
	{
		GPURenderPass* maskRenderPass = nullptr;
		GPURenderPass* compositeMaskRenderPass = nullptr;
		GPURenderPass* outlineRenderPass = nullptr;

		SceneViewWindow* sceneViewWindow = nullptr;
		GPUTexture*      maskTexture = nullptr;
		GPUFramebuffer*  maskFramebuffer = nullptr;

		GPUFramebuffer* compositeMaskFramebuffer = nullptr;
		GPUTexture*     compositeMaskTexture = nullptr;

		Array<GPUPipeline*> unlitPipelines;
		Scene*              cachedPipelineOwner = nullptr;
		GPUPipeline*      maskPipeline = nullptr;
		GPUDescriptorSet* maskDescriptorSet = nullptr;
		GPUPipeline*      compositeMaskPipeline = nullptr;
		GPUFramebuffer*   outlineFramebuffer = nullptr;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Other;
			setup.stage = PipelineRenderStage::Tools;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "SelectionOutline", .access = RenderPipelineTextureAccess::Write});
			return setup;
		}

		void Init() override
		{
			sceneViewWindow = static_cast<SceneViewWindow*>(context->settings.userData);

			maskRenderPass = Graphics::CreateRenderPass({
				.attachments = {
					AttachmentDesc{
						.initialState = ResourceState::Undefined,
						.finalState = ResourceState::ColorAttachment,
						.loadOp = AttachmentLoadOp::Clear,
						.storeOp = AttachmentStoreOp::Store,
						.format = TextureFormat::R8G8B8A8_UNORM,
					},
				}
			});

			compositeMaskRenderPass = Graphics::CreateRenderPass({
				.attachments = {
					AttachmentDesc{
						.initialState = ResourceState::Undefined,
						.finalState = ResourceState::ColorAttachment,
						.loadOp = AttachmentLoadOp::Clear,
						.storeOp = AttachmentStoreOp::Store,
						.format = TextureFormat::R8G8B8A8_UNORM,
					},
				}
			});

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

			outlineRenderPass = Graphics::CreateRenderPass({
				.attachments = {
					AttachmentDesc{
						.initialState = ResourceState::ShaderReadOnly,
						.finalState = ResourceState::ColorAttachment,
						.loadOp = AttachmentLoadOp::Load,
						.storeOp = AttachmentStoreOp::Store,
						.format = TextureFormat::R8G8B8A8_UNORM,
					},
				}
			});

			{
				GraphicsPipelineDesc pipelineDesc = {
					.shader = Resources::FindByPath("Skore://Shaders/CompositeMaskShader.raster"),
					.blendStates = {
						BlendStateDesc{
						}
					},
					.renderPass = outlineRenderPass,
					.allowImmediateSet = true,
				};
				compositeMaskPipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
			}

			OnResize(context->GetOutputSize());
		}

		void OnResize(Extent size) override
		{
			if (maskTexture)
			{
				maskTexture->Destroy();
			}

			if (compositeMaskTexture)
			{
				compositeMaskTexture->Destroy();
			}

			if (maskFramebuffer)
			{
				maskFramebuffer->Destroy();
			}

			if (compositeMaskFramebuffer)
			{
				compositeMaskFramebuffer->Destroy();
			}

			if (outlineFramebuffer)
			{
				outlineFramebuffer->Destroy();
			}

			maskTexture = Graphics::CreateTexture({
				.extent = size,
				.format = TextureFormat::R8G8B8A8_UNORM,
				.usage = ResourceUsage::RenderTarget | ResourceUsage::ShaderResource,
				.debugName = "SelectPickerMask_Texture"
			});

			maskFramebuffer = Graphics::CreateFramebuffer({
				.renderPass = maskRenderPass,
				.attachments = {
					maskTexture->GetTextureView()
				}
			});

			compositeMaskTexture = Graphics::CreateTexture({
				.extent = size,
				.format = TextureFormat::R8G8B8A8_UNORM,
				.usage = ResourceUsage::RenderTarget | ResourceUsage::ShaderResource | ResourceUsage::CopySource,
				.debugName = "SelectPickerMask_Texture"
			});

			compositeMaskFramebuffer = Graphics::CreateFramebuffer({
				.renderPass = compositeMaskRenderPass,
				.attachments = {
					compositeMaskTexture->GetTextureView()
				}
			});

			maskDescriptorSet->UpdateTexture(1, maskTexture);

			GPUTexture* outlineTexture = context->GetTexture("SelectionOutline");
			outlineFramebuffer = Graphics::CreateFramebuffer({
				.renderPass = outlineRenderPass,
				.attachments = {
					outlineTexture->GetTextureView()
				}
			});
		}

		void CleanupPipelines()
		{
			for (GPUPipeline* p : unlitPipelines) p->Destroy();
			unlitPipelines.Clear();
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			if (!scene) return;
			RenderSceneObjects* objects = &scene->renderObjects;

			if (cachedPipelineOwner != nullptr && cachedPipelineOwner != scene)
			{
				CleanupPipelines();
			}
			cachedPipelineOwner = scene;

			Span<RID> selectedEntities = sceneViewWindow->GetSceneEditor()->GetSelectedEntities();
			if (!selectedEntities.Empty())
			{
				// Lazily create unlit pipelines matching visible pipeline strides
				objects->ForEachVisiblePipeline([&](u32 index, const DrawPipeline& pipeline)
				{
					if (index >= unlitPipelines.Size())
					{
						GPUPipeline* p = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
							.shader = Resources::FindByPath("Skore://Shaders/Unlit.raster"),
							.blendStates = {
								BlendStateDesc{}
							},
							.renderPass = maskRenderPass,
							.descriptorSetsOverride = {
								DescriptorSetOverride{
									.set = 0,
									.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
								},
								DescriptorSetOverride{
									.set = 1,
									.descriptorSet = context->GetSceneDescriptorSet(0)
								}
							}
						});
						unlitPipelines.EmplaceBack(p);
					}
				});

				cmd->BeginDebugMarker("SceneViewDrawOutlinePass", Vec4{0, 0, 0, 1});

				GPUTexture* outlineTexture = context->GetTexture("SelectionOutline");

				cmd->ResourceBarrier(outlineTexture, ResourceState::Undefined, ResourceState::CopyDest, 0, 0);
				cmd->ClearColorTexture(outlineTexture, Vec4(0.0, 0.0, 0.0, 0.0), 0, 0);
				cmd->ResourceBarrier(outlineTexture, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, 0);


				std::function<void(Entity* entity)> drawEntity;
				struct UnlitPushConstants
				{
					Mat4 world;
					u32  vertexByteOffset;
					u32  vertexLayoutIndex;
					u32  pad[2];
				};

				cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

				drawEntity = [&](Entity* entity)
				{
					for (Component* component : entity->GetComponents())
					{
						if (RendererComponent* drawableComponent = component->SafeCast<RendererComponent>())
						{
							drawableComponent->ForEachVisibleDrawcallRef([&](u32 pipelineIndex, const Drawcall& drawcall)
							{
								GPUPipeline* pipeline = unlitPipelines[pipelineIndex];
								cmd->BindPipeline(pipeline);
								cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet(), {});
								cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet(), {});

								UnlitPushConstants pc{};
								pc.world = drawcall.transform;
								pc.vertexByteOffset = drawcall.vertexByteOffset;
								pc.vertexLayoutIndex = drawcall.vertexLayoutIndex;
								cmd->PushConstants(pipeline, ShaderStage::Vertex, 0, sizeof(UnlitPushConstants), &pc);
								cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
							});
						}
					}

					for (Entity* child : entity->GetChildren())
					{
						drawEntity(child);
					}
				};

				for (RID selected : selectedEntities)
				{
					if (Entity* entity = sceneViewWindow->GetSceneEditor()->GetCurrentScene()->FindEntityByRID(selected))
					{
						//skip root, may be slow
						//if (sceneViewWindow->GetSceneEditor()->GetCurrentScene()->GetRootEntity() != entity)
						{
							Extent currentExtent = context->GetOutputSize();

							ViewportInfo viewportInfo{};
							viewportInfo.x = 0.;
							viewportInfo.y = 0.;
							viewportInfo.y = 0.f;
							viewportInfo.width = (f32)currentExtent.width;
							viewportInfo.height = (f32)currentExtent.height;
							viewportInfo.minDepth = 0.;
							viewportInfo.maxDepth = 1.;

							{
								ClearValues clearValue;
								clearValue.color = Vec4(1.0, 1.0, 1.0, 0.0);

								BeginRenderPassInfo beginRenderPassInfo;
								beginRenderPassInfo.renderPass = maskRenderPass;
								beginRenderPassInfo.framebuffer = maskFramebuffer;
								beginRenderPassInfo.clearValues = &clearValue;
								cmd->BeginRenderPass(beginRenderPassInfo);

								cmd->SetViewport(viewportInfo);
								cmd->SetScissor({0, 0}, currentExtent);

								drawEntity(entity);

								cmd->EndRenderPass();
								cmd->ResourceBarrier(maskTexture, ResourceState::ColorAttachment, ResourceState::ShaderReadOnly, 0, 0);
							}

							{
								ClearValues clearValue;
								clearValue.color = Vec4(0.0, 0.0, 0.0, 0.0);

								BeginRenderPassInfo beginInfo;
								beginInfo.renderPass = compositeMaskRenderPass;
								beginInfo.framebuffer = compositeMaskFramebuffer;
								beginInfo.clearValues = &clearValue;
								cmd->BeginRenderPass(beginInfo);

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
							}

							{
								BeginRenderPassInfo beginInfo;
								beginInfo.renderPass = outlineRenderPass;
								beginInfo.framebuffer = outlineFramebuffer;

								cmd->BeginRenderPass(beginInfo);

								viewportInfo.y = 0.f;
								viewportInfo.width = (f32)currentExtent.width;
								viewportInfo.height = (f32)currentExtent.height;

								cmd->SetViewport(viewportInfo);
								cmd->SetScissor({0, 0}, currentExtent);

								cmd->BindPipeline(compositeMaskPipeline);
								cmd->SetSampler(compositeMaskPipeline, 0, 0, Graphics::GetLinearSampler());
								cmd->SetTexture(compositeMaskPipeline, 0, 1, compositeMaskTexture, 0);
								cmd->Draw(6, 1, 0, 0);

								cmd->EndRenderPass();

								cmd->ResourceBarrier(outlineTexture, ResourceState::ColorAttachment, ResourceState::ShaderReadOnly, 0, 0);
							}

						}
					}
				}
				cmd->EndDebugMarker();
			}
		}

		void Destroy() override
		{
			maskRenderPass->Destroy();
			compositeMaskRenderPass->Destroy();
			outlineRenderPass->Destroy();

			maskTexture->Destroy();
			maskFramebuffer->Destroy();
			CleanupPipelines();
			compositeMaskTexture->Destroy();
			compositeMaskFramebuffer->Destroy();
			maskPipeline->Destroy();
			maskDescriptorSet->Destroy();
			outlineFramebuffer->Destroy();
			compositeMaskPipeline->Destroy();
		}
	};

	struct SceneViewPipelinePass : RenderPipelinePass
	{
		SceneViewWindow* sceneViewWindow = nullptr;
		GPUPipeline*     gridPipeline = nullptr;
		GPUPipeline*     debugPhysicsPipeline = nullptr;
		GPUPipeline*     fullscreenPipeline = nullptr;
		GPUPipeline*     drawNavMeshPipeline = nullptr;

		GPURenderPass * sceneViewRenderPass = nullptr;
		GPUFramebuffer* sceneViewFramebuffer = nullptr;

		GPUBuffer* navMeshVertexBuffer = nullptr;
		u32        navMeshVertexCount = 0;
		u32        navMeshVersion = 0;

		DebugDraw debugDraw;

		RenderPipelinePassSetup GetPassSetup() override
		{
			RenderPipelinePassSetup setup;
			setup.type = RenderPipelinePassType::Other;
			setup.stage =  PipelineRenderStage::Tools;
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = "SelectionOutline", .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputColorName, .access = RenderPipelineTextureAccess::Read});
			setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = OutputDepthName, .access = RenderPipelineTextureAccess::Read});
			return setup;
		}

		void CreateFramebuffer()
		{
			if (sceneViewFramebuffer) sceneViewFramebuffer->Destroy();

			sceneViewFramebuffer = Graphics::CreateFramebuffer({
				.renderPass = sceneViewRenderPass,
				.attachments = {
					context->GetTexture(OutputColorName)->GetTextureView(),
					context->GetTexture(OutputDepthName)->GetTextureView()
				}
			});
		}

		void OnResize(Extent size) override
		{
			CreateFramebuffer();
		}

		void Init() override
		{
			sceneViewWindow = static_cast<SceneViewWindow*>(context->settings.userData);

			sceneViewRenderPass = Graphics::CreateRenderPass({
				.attachments = {
					AttachmentDesc{
						.initialState = ResourceState::ShaderReadOnly,
						.finalState = ResourceState::ColorAttachment,
						.loadOp = AttachmentLoadOp::Load,
						.storeOp = AttachmentStoreOp::Store,
						.format = context->GetTexture(OutputColorName)->GetDesc().format,
					},
					AttachmentDesc{
						.initialState = ResourceState::DepthStencilReadOnly,
						.finalState = ResourceState::DepthStencilAttachment,
						.loadOp = AttachmentLoadOp::Load,
						.storeOp = AttachmentStoreOp::Store,
						.format = context->GetTexture(OutputDepthName)->GetDesc().format,
					},
				}
			});

			CreateFramebuffer();

			{
				GraphicsPipelineDesc pipelineDesc = {
					.shader = Resources::FindByPath("Skore://Shaders/DrawGrid.raster"),
					.depthStencilState = {
						.depthTestEnable = true,
						.depthWriteEnable = false,
						.depthCompareOp = CompareOp::Greater // reverse-Z
					},
					.blendStates = {
						BlendStateDesc{
							.blendEnable = true,
							.colorBlendOp = BlendOp::Add,
							.alphaBlendOp = BlendOp::Max,
						}
					},
					.renderPass = sceneViewRenderPass,
					.descriptorSetsOverride = {
						DescriptorSetOverride{.set = 1, .descriptorSet = context->GetSceneDescriptorSet(0)}
					}
				};
				gridPipeline = Graphics::CreateGraphicsPipeline(pipelineDesc);
			}

			{
				debugPhysicsPipeline = Graphics::CreateGraphicsPipeline({
					.shader = Resources::FindByPath("Skore://Shaders/DebugPhysics.raster"),
					.rasterizerState = {
						.polygonMode = PolygonMode::Line,
						.lineWidth = 2.0f * ImGui::GetStyle().ScaleFactor,
					},
					.blendStates = {
						BlendStateDesc{}
					},
					.renderPass = sceneViewRenderPass,
					.vertexInputStride = DebugPhysicsVertexSize,
					.descriptorSetsOverride = {
						DescriptorSetOverride{.set = 1, .descriptorSet = context->GetSceneDescriptorSet(0)}
					}
				});
			}

			{
				drawNavMeshPipeline = Graphics::CreateGraphicsPipeline({
					.shader = Resources::FindByPath("Skore://Shaders/Editor/DrawNavMesh.raster"),
					.depthStencilState {
						.depthTestEnable = true,
						.depthWriteEnable = true,
					},
					.blendStates = {
						BlendStateDesc{
							.blendEnable = true,
						}
					},
					.renderPass = sceneViewRenderPass,
					.vertexInputStride = sizeof(NavMeshDebugVertex),
					.descriptorSetsOverride = {
						DescriptorSetOverride{.set = 1, .descriptorSet = context->GetSceneDescriptorSet(0)}
					}
				});
			}

			{
				fullscreenPipeline = Graphics::CreateGraphicsPipeline({
					.shader = Resources::FindByPath("Skore://Shaders/FullscreenTexture.raster"),
					.depthStencilState {
						.depthTestEnable = false,
					},
					.blendStates = {
						BlendStateDesc{}
					},
					.renderPass = sceneViewRenderPass,
					.allowImmediateSet = true
				});
			}
		}

		void Render(Scene* scene, GPUCommandBuffer* cmd) override
		{
			RenderSceneObjects* objects = scene ? &scene->renderObjects : nullptr;

			cmd->BeginDebugMarker("SceneViewDrawPass", Vec4{0, 0, 0, 1});

			ClearValues clearValue;
			clearValue.color = Vec4(0.0, 0.0, 0.0, 0.0);

			BeginRenderPassInfo beginRenderPassInfo;
			beginRenderPassInfo.renderPass = sceneViewRenderPass;
			beginRenderPassInfo.framebuffer = sceneViewFramebuffer;
			beginRenderPassInfo.clearValues = &clearValue;
			cmd->BeginRenderPass(beginRenderPassInfo);

			Extent currentExtent = context->GetOutputSize();

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

			if (!sceneViewWindow->windowStartedSimulation)
			{
				if (sceneViewWindow->drawGrid)
				{
					struct GridPushConstants
					{
						f32  cellSize;
						f32  gridExtent;
						f32  pad[2];
						Vec4 thinLinesColor;
						Vec4 thickLinesColor;
						Vec4 originAxisXColor;
						Vec4 originAxisZColor;
					};

					GridPushConstants gridParams;
					gridParams.cellSize = 1.0f;
					gridParams.gridExtent = 1000.0f;
					gridParams.pad[0] = 0.0f;
					gridParams.pad[1] = 0.0f;
					gridParams.thinLinesColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
					gridParams.thickLinesColor = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
					gridParams.originAxisXColor = Vec4(0.8f, 0.2f, 0.2f, 1.0f);
					gridParams.originAxisZColor = Vec4(0.2f, 0.2f, 0.8f, 1.0f);

					cmd->BindPipeline(gridPipeline);
					cmd->BindDescriptorSet(gridPipeline, 1, context->GetSceneDescriptorSet(), {});
					cmd->PushConstants(gridPipeline, ShaderStage::Pixel, 0, sizeof(GridPushConstants), &gridParams);
					cmd->Draw(6, 1, 0, 0);
				}

				if (sceneViewWindow->drawDebugPhysics)
				{
					if (Scene* scene = sceneViewWindow->sceneEditor->GetCurrentScene())
					{
						cmd->BindPipeline(debugPhysicsPipeline);
						cmd->BindDescriptorSet(debugPhysicsPipeline, 1, context->GetSceneDescriptorSet(), {});

						DrawPhysicsShapeEvent drawData{cmd, debugPhysicsPipeline};
						EntityEventDesc eventDesc{.type = EntityEventType::DrawPhysicsShape, .eventData = &drawData};

						if (sceneViewWindow->showAllPhysicsShapes)
						{
							scene->NotifyEvent(eventDesc, true);
						}
						else
						{
							for (Entity* entity : Selection::ResolveEntities(scene))
							{
								entity->NotifyEvent(eventDesc, false);
							}
						}
					}
				}

				if (sceneViewWindow->drawSelectionOutline && !sceneViewWindow->GetSceneEditor()->GetSelectedEntities().Empty())
				{
					GPUTexture* outlineTexture = context->GetTexture("SelectionOutline");

					cmd->BindPipeline(fullscreenPipeline);
					cmd->SetTexture(fullscreenPipeline, 0, 0, outlineTexture, 0);
					cmd->SetSampler(fullscreenPipeline, 0, 1, Graphics::GetLinearSampler());
					cmd->Draw(6, 1, 0, 0);
				}

				if (sceneViewWindow->drawNavMesh)
				{
					if (Scene* scene = sceneViewWindow->sceneEditor->GetCurrentScene())
					{
						u32 currentVersion = scene->navigationScene.GetNavMeshVersion();
						if (currentVersion != navMeshVersion)
						{
							navMeshVersion = currentVersion;

							Array<NavMeshDebugVertex> vertices;
							scene->navigationScene.GetNavMeshTriangles(vertices);
							navMeshVertexCount = static_cast<u32>(vertices.Size());

							if (navMeshVertexCount > 0)
							{
								usize requiredSize = navMeshVertexCount * sizeof(NavMeshDebugVertex);
								if (!navMeshVertexBuffer || navMeshVertexBuffer->GetDesc().size < requiredSize)
								{
									if (navMeshVertexBuffer) navMeshVertexBuffer->Destroy();
									navMeshVertexBuffer = Graphics::CreateBuffer(BufferDesc{
										.size = requiredSize,
										.usage = ResourceUsage::VertexBuffer,
										.hostVisible = true,
										.persistentMapped = true
									});
								}
								memcpy(navMeshVertexBuffer->GetMappedData(), vertices.Data(), requiredSize);
							}
						}

						if (navMeshVertexBuffer && navMeshVertexCount > 0)
						{
							cmd->BindPipeline(drawNavMeshPipeline);
							cmd->BindDescriptorSet(drawNavMeshPipeline, 1, context->GetSceneDescriptorSet(), {});
							cmd->BindVertexBuffer(0, navMeshVertexBuffer, 0);
							cmd->Draw(navMeshVertexCount, 1, 0, 0);
						}
					}
				}

				if (sceneViewWindow->drawMeshAABB && objects)
				{
					objects->ForEachVisiblePipeline([&](u32, const DrawPipeline& drawPipeline)
					{
						for (const Drawcall& drawCall : drawPipeline.drawcalls)
						{
							debugDraw.DrawAABB(drawCall.aabb, 2.0);
						}
					});

					//debugDraw.DrawFrustum(context->camera.frustum, 2, Color::YELLOW);
				}

				if (sceneViewWindow->drawComponentGizmos)
				{
					if (Scene* currentScene = sceneViewWindow->sceneEditor->GetCurrentScene())
					{
						DrawGizmosEvent gizmosEvent;
						gizmosEvent.debugDraw = &debugDraw;
						gizmosEvent.viewportAspect = currentExtent.height > 0 ? (f32)currentExtent.width / (f32)currentExtent.height : 1.0f;

						EntityEventDesc eventDesc{.type = EntityEventType::DrawGizmos, .eventData = &gizmosEvent};

						for (Entity* entity : Selection::ResolveEntities(currentScene))
						{
							entity->NotifyEvent(eventDesc, false);
						}
					}
				}

				debugDraw.Flush(cmd, sceneViewRenderPass, context->camera.viewProjection, currentExtent);
			}


			cmd->EndRenderPass();
			cmd->EndDebugMarker();
			//how to take a screenshot from here?

			cmd->ResourceBarrier(context->GetTexture(OutputColorName), ResourceState::ColorAttachment, ResourceState::ShaderReadOnly, 0, 0);
			cmd->ResourceBarrier(context->GetTexture(OutputDepthName), ResourceState::DepthStencilReadOnly, ResourceState::DepthStencilReadOnly, 0, 0);
		}

		void Destroy() override
		{
			gridPipeline->Destroy();
			debugPhysicsPipeline->Destroy();
			fullscreenPipeline->Destroy();
			drawNavMeshPipeline->Destroy();
			if (navMeshVertexBuffer) navMeshVertexBuffer->Destroy();
			sceneViewFramebuffer->Destroy();
			sceneViewRenderPass->Destroy();
		}
	};


	struct SceneViewPipelineModule : RenderPipelineModule
	{
		Array<RenderPipelineResource> GetResources() override
		{
			context->SetColorOutput(OutputColorName);
			context->SetDepthOutput(OutputDepthName);

			Array<RenderPipelineResource> resources;
			resources.EmplaceBack(RenderPipelineResource{.name = OutputColorName, .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R8G8B8A8_UNORM,
				.textureUsage = ResourceUsage::ShaderResource |  ResourceUsage::UnorderedAccess | ResourceUsage::CopyDest
			});
			resources.EmplaceBack(RenderPipelineResource{.name = OutputDepthName, .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::D32_FLOAT, });
			resources.EmplaceBack(RenderPipelineResource{.name = "SelectionOutline", .type = RenderPipelineResourceType::Attachment, .format = TextureFormat::R8G8B8A8_UNORM, .textureUsage = ResourceUsage::CopyDest});
			return resources;
		}

		RenderPipelineModuleSetup GetSetup() override
		{
			RenderPipelineModuleSetup setup;
			setup.passes.EmplaceBack(sktypeid(SceneViewPipelinePass));
			setup.passes.EmplaceBack(sktypeid(SceneViewDrawOutlinePass));
			return setup;
		}
	};

	void RegisterSceneViewPipelineModule()
	{
		Reflection::Type<SceneViewPipelinePass>();
		Reflection::Type<SceneViewDrawOutlinePass>();
		Reflection::Type<SceneViewPipelineModule>();
	}
}
