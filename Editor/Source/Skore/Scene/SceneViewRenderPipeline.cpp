#include "SceneViewRenderPipeline.hpp"

#include "imgui.h"
#include "Skore/Selection.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Graphics/Pipeline/PipelineCommon.hpp"
#include "Skore/Navigation/NavigationCommon.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Physics.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneEditor.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Window/SceneViewWindow.hpp"

namespace Skore
{
	namespace
	{
		constexpr const char* SelectionMaskName = "SelectionMask";
		constexpr const char* SelectionOutlineName = "SelectionOutline";

		struct UnlitPushConstants
		{
			Mat4 world;
			u32  vertexByteOffset;
			u32  vertexLayoutIndex;
			u32  pad[2];
		};

		void DestroyCached(GPUPipeline*& pipeline)
		{
			if (pipeline)
			{
				pipeline->Destroy();
				pipeline = nullptr;
			}
		}
	}

	SceneViewRenderPipeline::~SceneViewRenderPipeline()
	{
		DestroyCached(unlitPipeline.pipeline);
		DestroyCached(outlineDistancePipeline.pipeline);
		DestroyCached(outlineCompositePipeline.pipeline);
		DestroyCached(gridPipeline.pipeline);
		DestroyCached(debugPhysicsPipeline.pipeline);
		DestroyCached(drawNavMeshPipeline.pipeline);
		if (navMeshVertexBuffer) navMeshVertexBuffer->Destroy();
	}

	void SceneViewRenderPipeline::BuildRenderGraph(RenderGraph& renderGraph)
	{
		DefaultRenderPipeline::BuildRenderGraph(renderGraph);

		if (window == nullptr || window->GetSceneEditor() == nullptr || window->IsSceneInteractionDisabled())
		{
			return;
		}

		bool hasSelection = window->drawSelectionOutline && !window->GetSceneEditor()->GetSelectedEntities().Empty();

		if (hasSelection)
		{
			renderGraph.Create(SelectionMaskName, RenderGraphTextureDesc{
				.format = Format::RGBA8_UNORM,
				.extent = Extent{0, 0},
				.clearColor = Vec4(1.0f, 1.0f, 1.0f, 0.0f)
			});

			renderGraph.Create(SelectionOutlineName, RenderGraphTextureDesc{
				.format = Format::RGBA8_UNORM,
				.extent = Extent{0, 0},
				.clearColor = Vec4(0.0f, 0.0f, 0.0f, 0.0f)
			});

			renderGraph
				.AddGraphicsPass("SelectionMask")
				.Stage(RenderStage::Tools)
				.Write(SelectionMaskName)
				.InvertViewport(true)
				.Render([this](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
				{
					RenderSelectionMask(pass, scene, cmd);
				});

			renderGraph
				.AddGraphicsPass("SelectionOutlineDistance")
				.Stage(RenderStage::Tools)
				.Read(SelectionMaskName)
				.Write(SelectionOutlineName)
				.Render([this](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
				{
					RenderOutlineDistance(pass, cmd);
				});
		}

		RenderGraphPass& toolsPass = renderGraph
			.AddGraphicsPass("SceneViewTools")
			.Stage(RenderStage::Tools)
			.WriteRead(PostProcessOutputName)
			.WriteRead(ForwardDepthName)
			.Render([this](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
			{
				RenderTools(pass, scene, cmd);
			});

		if (hasSelection)
		{
			toolsPass.Read(SelectionOutlineName);
		}
	}

	void SceneViewRenderPipeline::RenderSelectionMask(RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
	{
		if (scene == nullptr || pass.GetRenderPass() == nullptr) return;

		RenderGraph& renderGraph = *pass.GetGraph();

		GPUDescriptorSet* sceneSet = renderGraph.GetSceneDescriptorSet();
		GPUDescriptorSet* globalSet = RenderResourceCache::GetGlobalDescriptorSet();
		if (sceneSet == nullptr || globalSet == nullptr) return;

		if (unlitPipeline.renderPass != pass.GetRenderPass())
		{
			DestroyCached(unlitPipeline.pipeline);
			unlitPipeline.renderPass = pass.GetRenderPass();
			unlitPipeline.pipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/Editor/Unlit.raster"),
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = pass.GetRenderPass(),
				.descriptorSetsOverride = {
					DescriptorSetOverride{.set = 1, .descriptorSet = renderGraph.GetSceneDescriptorSet(0)},
					DescriptorSetOverride{.set = 2, .descriptorSet = globalSet}
				}
			});
		}

		if (unlitPipeline.pipeline == nullptr) return;

		cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);
		cmd->BindPipeline(unlitPipeline.pipeline);
		cmd->BindDescriptorSet(unlitPipeline.pipeline, 1, sceneSet);
		cmd->BindDescriptorSet(unlitPipeline.pipeline, 2, globalSet);

		std::function<void(Entity*)> drawEntity = [&](Entity* entity)
		{
			for (Component* component : entity->GetComponents())
			{
				if (RendererComponent* drawableComponent = component->SafeCast<RendererComponent>())
				{
					drawableComponent->ForEachVisibleDrawcallRef([&](u32 pipelineIndex, const Drawcall& drawcall)
					{
						UnlitPushConstants pushConstants{};
						pushConstants.world = drawcall.transform;
						pushConstants.vertexByteOffset = drawcall.vertexByteOffset;
						pushConstants.vertexLayoutIndex = drawcall.vertexLayoutIndex;
						cmd->PushConstants(unlitPipeline.pipeline, ShaderStage::Vertex, 0, sizeof(UnlitPushConstants), &pushConstants);
						cmd->DrawIndexed(drawcall.indexCount, 1, (drawcall.indexByteOffset / sizeof(u32)) + drawcall.firstIndex, 0, 0);
					});
				}
			}

			for (Entity* child : entity->GetChildren())
			{
				drawEntity(child);
			}
		};

		Scene* currentScene = window->GetSceneEditor()->GetCurrentScene();
		if (currentScene == nullptr) return;

		for (RID selected : window->GetSceneEditor()->GetSelectedEntities())
		{
			if (Entity* entity = currentScene->FindEntityByRID(selected))
			{
				drawEntity(entity);
			}
		}
	}

	void SceneViewRenderPipeline::RenderOutlineDistance(RenderGraphPass& pass, GPUCommandBuffer* cmd)
	{
		if (pass.GetRenderPass() == nullptr) return;

		RenderGraph& renderGraph = *pass.GetGraph();

		GPUTexture* maskTexture = renderGraph.GetTexture(SelectionMaskName);
		if (maskTexture == nullptr) return;

		if (outlineDistancePipeline.renderPass != pass.GetRenderPass())
		{
			DestroyCached(outlineDistancePipeline.pipeline);
			outlineDistancePipeline.renderPass = pass.GetRenderPass();
			outlineDistancePipeline.pipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/Editor/MaskShader.raster"),
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = pass.GetRenderPass(),
				.allowImmediateSet = true
			});
		}

		if (outlineDistancePipeline.pipeline == nullptr) return;

		Extent extent = renderGraph.GetOutputSize();

		Vec4 textureInfo = Vec4(1.0f / extent.width, 1.0f / extent.height, 0.0f, 0.0f);

		cmd->BindPipeline(outlineDistancePipeline.pipeline);
		cmd->SetSampler(outlineDistancePipeline.pipeline, 0, 0, Graphics::GetLinearSampler());
		cmd->SetTexture(outlineDistancePipeline.pipeline, 0, 1, maskTexture, 0);
		cmd->PushConstants(outlineDistancePipeline.pipeline, ShaderStage::Vertex, 0, sizeof(Vec4), &textureInfo);
		cmd->Draw(6, 1, 0, 0);
	}

	void SceneViewRenderPipeline::RenderTools(RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
	{
		if (pass.GetRenderPass() == nullptr) return;

		RenderGraph& renderGraph = *pass.GetGraph();

		GPUDescriptorSet* sceneSet = renderGraph.GetSceneDescriptorSet();
		if (sceneSet == nullptr) return;

		Extent extent = renderGraph.GetOutputSize();

		RenderSceneObjects* objects = scene ? &scene->renderObjects : nullptr;

		if (window->drawGrid)
		{
			if (gridPipeline.renderPass != pass.GetRenderPass())
			{
				DestroyCached(gridPipeline.pipeline);
				gridPipeline.renderPass = pass.GetRenderPass();
				gridPipeline.pipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/Editor/DrawGrid.raster"),
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
					.renderPass = pass.GetRenderPass(),
					.descriptorSetsOverride = {
						DescriptorSetOverride{.set = 1, .descriptorSet = renderGraph.GetSceneDescriptorSet(0)}
					}
				});
			}

			if (gridPipeline.pipeline)
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

				cmd->BindPipeline(gridPipeline.pipeline);
				cmd->BindDescriptorSet(gridPipeline.pipeline, 1, sceneSet, {});
				cmd->PushConstants(gridPipeline.pipeline, ShaderStage::Pixel, 0, sizeof(GridPushConstants), &gridParams);
				cmd->Draw(6, 1, 0, 0);
			}
		}

		if (window->drawSelectionOutline && !window->GetSceneEditor()->GetSelectedEntities().Empty())
		{
			if (GPUTexture* outlineTexture = renderGraph.GetTexture(SelectionOutlineName))
			{
				if (outlineCompositePipeline.renderPass != pass.GetRenderPass())
				{
					DestroyCached(outlineCompositePipeline.pipeline);
					outlineCompositePipeline.renderPass = pass.GetRenderPass();
					outlineCompositePipeline.pipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
						.shader = Resources::FindByPath("Skore://Shaders/Editor/CompositeMaskShader.raster"),
						.depthStencilState = {
							.depthTestEnable = false,
							.depthWriteEnable = false,
						},
						.blendStates = {
							BlendStateDesc{
								.blendEnable = true
							}
						},
						.renderPass = pass.GetRenderPass(),
						.allowImmediateSet = true
					});
				}

				if (outlineCompositePipeline.pipeline)
				{
					cmd->BindPipeline(outlineCompositePipeline.pipeline);
					cmd->SetSampler(outlineCompositePipeline.pipeline, 0, 0, Graphics::GetLinearSampler());
					cmd->SetTexture(outlineCompositePipeline.pipeline, 0, 1, outlineTexture, 0);
					cmd->Draw(6, 1, 0, 0);
				}
			}
		}

		if (window->drawDebugPhysics)
		{
			if (Scene* currentScene = window->GetSceneEditor()->GetCurrentScene())
			{
				if (debugPhysicsPipeline.renderPass != pass.GetRenderPass())
				{
					DestroyCached(debugPhysicsPipeline.pipeline);
					debugPhysicsPipeline.renderPass = pass.GetRenderPass();
					debugPhysicsPipeline.pipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
						.shader = Resources::FindByPath("Skore://Shaders/Editor/DebugPhysics.raster"),
						.rasterizerState = {
							.polygonMode = PolygonMode::Line,
							.lineWidth = 2.0f * ImGui::GetStyle().ScaleFactor,
						},
						.blendStates = {
							BlendStateDesc{}
						},
						.renderPass = pass.GetRenderPass(),
						.vertexInputStride = DebugPhysicsVertexSize,
						.descriptorSetsOverride = {
							DescriptorSetOverride{.set = 1, .descriptorSet = renderGraph.GetSceneDescriptorSet(0)}
						}
					});
				}

				if (debugPhysicsPipeline.pipeline)
				{
					cmd->BindPipeline(debugPhysicsPipeline.pipeline);
					cmd->BindDescriptorSet(debugPhysicsPipeline.pipeline, 1, sceneSet, {});

					DrawPhysicsShapeEvent drawData{cmd, debugPhysicsPipeline.pipeline};
					EntityEventDesc eventDesc{.type = EntityEventType::DrawPhysicsShape, .eventData = &drawData};

					if (window->showAllPhysicsShapes)
					{
						currentScene->NotifyEvent(eventDesc, true);
					}
					else
					{
						for (Entity* entity : Selection::ResolveEntities(currentScene))
						{
							entity->NotifyEvent(eventDesc, false);
						}
					}
				}
			}
		}

		if (window->drawNavMesh)
		{
			if (Scene* currentScene = window->GetSceneEditor()->GetCurrentScene())
			{
				u32 currentVersion = currentScene->navigationScene.GetNavMeshVersion();
				if (currentVersion != navMeshVersion)
				{
					navMeshVersion = currentVersion;

					Array<NavMeshDebugVertex> vertices;
					currentScene->navigationScene.GetNavMeshTriangles(vertices);
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
					if (drawNavMeshPipeline.renderPass != pass.GetRenderPass())
					{
						DestroyCached(drawNavMeshPipeline.pipeline);
						drawNavMeshPipeline.renderPass = pass.GetRenderPass();
						drawNavMeshPipeline.pipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
							.shader = Resources::FindByPath("Skore://Shaders/Editor/DrawNavMesh.raster"),
							.depthStencilState = {
								.depthTestEnable = true,
								.depthWriteEnable = true,
							},
							.blendStates = {
								BlendStateDesc{
									.blendEnable = true,
								}
							},
							.renderPass = pass.GetRenderPass(),
							.vertexInputStride = sizeof(NavMeshDebugVertex),
							.descriptorSetsOverride = {
								DescriptorSetOverride{.set = 1, .descriptorSet = renderGraph.GetSceneDescriptorSet(0)}
							}
						});
					}

					if (drawNavMeshPipeline.pipeline)
					{
						cmd->BindPipeline(drawNavMeshPipeline.pipeline);
						cmd->BindDescriptorSet(drawNavMeshPipeline.pipeline, 1, sceneSet, {});
						cmd->BindVertexBuffer(0, navMeshVertexBuffer, 0);
						cmd->Draw(navMeshVertexCount, 1, 0, 0);
					}
				}
			}
		}

		if (window->drawMeshAABB && objects)
		{
			objects->ForEachVisiblePipeline([&](u32, const DrawPipeline& drawPipeline)
			{
				for (const Drawcall& drawCall : drawPipeline.drawcalls)
				{
					debugDraw.DrawAABB(drawCall.aabb, 2.0);
				}
			});
		}

		if (Scene* currentScene = window->GetSceneEditor()->GetCurrentScene())
		{
			if (window->drawComponentGizmos)
			{
				DrawGizmosEvent gizmosEvent;
				gizmosEvent.debugDraw = &debugDraw;
				gizmosEvent.viewportAspect = extent.height > 0 ? (f32)extent.width / (f32)extent.height : 1.0f;

				EntityEventDesc eventDesc{.type = EntityEventType::DrawGizmos, .eventData = &gizmosEvent};

				for (Entity* entity : Selection::ResolveEntities(currentScene))
				{
					entity->NotifyEvent(eventDesc, false);
				}
			}
		}

		debugDraw.Flush(cmd, pass.GetRenderPass(), renderGraph.camera.viewProjection, extent);
	}

	void RegisterSceneViewRenderPipeline()
	{
		Reflection::Type<SceneViewRenderPipeline>();
	}
}
