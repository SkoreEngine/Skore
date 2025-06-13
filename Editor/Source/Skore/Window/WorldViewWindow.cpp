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

#include "WorldViewWindow.hpp"

#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/IO/Input.hpp"
#include "ImGuizmo.h"
#include "imgui_internal.h"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/World/WorldCommon.hpp"

namespace Skore
{
	MenuItemContext WorldViewWindow::menuItemContext = {};

	static Logger& logger = Logger::GetLogger("Skore::WorldViewWindow");

	WorldViewWindow::~WorldViewWindow()
	{
		Event::Unbind<OnRecordRenderCommands, &WorldViewWindow::RecordRenderCommands>(this);

		if (sceneTexture) sceneTexture->Destroy();
		if (sceneRenderPass) sceneRenderPass->Destroy();
	}

	void WorldViewWindow::Init(u32 id, VoidPtr userData)
	{
		sceneRendererViewport.Init();

		guizmoOperation = ImGuizmo::TRANSLATE;
		Event::Bind<OnRecordRenderCommands, &WorldViewWindow::RecordRenderCommands>(this);
	}

	void WorldViewWindow::Draw(u32 id, bool& open)
	{
		WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor();

		bool openSceneOptions = false;

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar;
		auto&            style = ImGui::GetStyle();
		ScopedStyleVar   windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
		{
			flags |= ImGuiWindowFlags_NoMove;
		}

		ImGuiBegin(id, ICON_FA_BORDER_ALL " World Viewport", &open, flags);
		{
			bool   moving = ImGui::IsMouseDown(ImGuiMouseButton_Right);
			bool   canChangeGuizmo = !moving && !ImGui::GetIO().WantCaptureKeyboard;
			bool   hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
			ImVec2 size = ImGui::GetWindowSize();
			ImVec2 initCursor = ImGui::GetCursorScreenPos();
			ImVec2 buttonSize = ImVec2(25 * style.ScaleFactor, 22 * style.ScaleFactor);
			ImVec2 cursor{};

			{
				ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(style.ScaleFactor * 2, style.ScaleFactor * 2));
				ScopedStyleVar space(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
				ImGui::BeginChild(id + 1000, ImVec2(0, buttonSize.y + (5 * style.ScaleFactor)), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar);

				//ScopedStyleVar itemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::BeginHorizontal("horizontal-sceneview-top", ImVec2(ImGui::GetContentRegionAvail().x, buttonSize.y));

				if (ImGuiSelectionButton(ICON_FA_ARROW_POINTER, guizmoOperation == 0, buttonSize) || (canChangeGuizmo && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Q))))
				{
					guizmoOperation = 0;
				}

				if (ImGuiSelectionButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, guizmoOperation == ImGuizmo::TRANSLATE, buttonSize)
					|| (canChangeGuizmo && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_W))))
				{
					guizmoOperation = ImGuizmo::TRANSLATE;
				}

				if (ImGuiSelectionButton(ICON_FA_ROTATE, guizmoOperation == ImGuizmo::ROTATE, buttonSize)
					|| (canChangeGuizmo && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_E))))
				{
					guizmoOperation = ImGuizmo::ROTATE;
				}
				if (ImGuiSelectionButton(ICON_FA_EXPAND, guizmoOperation == ImGuizmo::SCALE, buttonSize)
					|| (canChangeGuizmo && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_R))))
				{
					guizmoOperation = ImGuizmo::SCALE;
				}

				if (ImGui::Button(ICON_FA_ELLIPSIS, buttonSize))
				{
					openSceneOptions = true;
				}

				//				auto operation = m_guizmoOperation;

				//                        if (entityManager.isAlive(sceneWindowIt.second.selectedEntity)) {
				//                            bool isReadOnly = entityManager.isReadOnly(sceneWindow.selectedEntity);
				//                            if (isReadOnly) {
				//                                operation = 0;
				//                            }
				//                        }

				ImGui::Spring(1.f);

				bool isSimulating = worldEditor && worldEditor->IsSimulationRunning();

				if (!isSimulating)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(139, 194, 74, 255));
				}

				if (windowStartedSimulation && !isSimulating)
				{
					windowStartedSimulation = false;
				}

				ImGui::BeginDisabled(isSimulating);

				if (ImGui::Button(ICON_FA_PLAY, buttonSize) && worldEditor)
				{
					worldEditor->StartSimulation();
					windowStartedSimulation = true;
				}

				ImGui::EndDisabled();

				if (!isSimulating)
				{
					ImGui::PopStyleColor();
				}

				ImGui::BeginDisabled(worldEditor && !worldEditor->IsSimulationRunning()|| !windowStartedSimulation);

				if (isSimulating)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(199, 84, 80, 255));
				}

				if (ImGui::Button(ICON_FA_STOP, buttonSize) && worldEditor)
				{
					worldEditor->StopSimulation();
					windowStartedSimulation = false;
				}

				if (isSimulating)
				{
					ImGui::PopStyleColor();
				}

				ImGui::EndDisabled();

				ImGui::Spring(1.f);

				ImGui::EndHorizontal();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);

				cursor.y = ImGui::GetCursorScreenPos().y;
				ImGui::EndChild();
				cursor.x = ImGui::GetCursorScreenPos().x;
			}

			if (movingScene)
			{
				ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
				ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoKeyboard;
			}
			else
			{
				ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
				ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;
			}

			if (!movingScene)
			{
				movingScene = !windowStartedSimulation && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && Input::IsMouseDown(MouseButton::Right);
			}

			if (movingScene)
			{
				bool rightDown = Input::IsMouseDown(MouseButton::Right);
				freeViewCamera.SetActive(rightDown);
				movingScene = rightDown;
			}

			freeViewCamera.Process(App::DeltaTime());
			view = freeViewCamera.GetView();

			auto diffCursor = cursor - initCursor;
			size -= diffCursor;
			Rect bb{(i32)cursor.x, (i32)cursor.y, u32(cursor.x + size.x), u32(cursor.y + size.y)};

			f32    screenScale = 1;
			Extent extent = {static_cast<u32>(size.x * screenScale), static_cast<u32>(size.y * screenScale)};

			if (extent != sceneRendererViewport.GetExtent())
			{
				f32 aspect = static_cast<f32>(extent.width) / static_cast<f32>(extent.height);
				projection = Math::Perspective(Math::Radians(60.0f), aspect, 0.1f, 300.0f);

				sceneRendererViewport.Resize(extent);

				if (sceneTexture)
				{
					sceneTexture->Destroy();
				}

				if (sceneRenderPass)
				{
					sceneRenderPass->Destroy();
				}

				sceneTexture = Graphics::CreateTexture(TextureDesc{
					.extent = {extent.width, extent.height, 1},
					.format = TextureFormat::R8G8B8A8_UNORM,
					.usage = ResourceUsage::RenderTarget | ResourceUsage::ShaderResource,
					.debugName = "Scene Viewport Texture"
				});

				sceneRenderPass = Graphics::CreateRenderPass(RenderPassDesc{
					.attachments = {
						AttachmentDesc{
							.texture = sceneTexture,
							.finalState = ResourceState::ColorAttachment,
						}
					}
				});

				sceneExtent = extent;
			}

			ImGuiDrawTextureView(sceneTexture->GetTextureView(), bb);

			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(cursor.x, cursor.y, size.x, size.y);

			if (worldEditor && !worldEditor->IsSimulationRunning())
			{
				for (RID selectedEntity: worldEditor->GetSelectedEntities())
				{
//					RID transform = GetTransformComponent(selectedEntity);
					//if (!transform) continue;


		            // Mat4 worldMatrix = entity->GetWorldTransform();
		            //
		            // static float snap[3] = {0.0f, 0.0f, 0.0f};
		            //
		            // ImGuizmo::Manipulate(&view[0][0],
		            //                      &projection[0][0],
		            //                      static_cast<ImGuizmo::OPERATION>(guizmoOperation),
		            //                      ImGuizmo::LOCAL,
		            //                      &worldMatrix[0][0],
		            //                      nullptr,
		            //                      snap);
		            //
		            // if (ImGuizmo::IsUsing())
		            // {
		            //     if (!usingGuizmo)
		            //     {
		            //         usingGuizmo = true;
		            //         gizmoInitialTransform = entity->GetTransform();
		            //
		            //         // if (entity->GetPrototype() != nullptr && !entity->IsComponentOverride(transformComponent))
		            //         // {
		            //         //     gizmoTransaction->CreateAction<OverridePrototypeComponentAction>(sceneEditor, entity, static_cast<Component*>(transformComponent))->Commit();
		            //         // }
		            //     }
		            //
		            //     if (Entity* parent =  entity->GetParent())
		            //     {
		            //     	worldMatrix = Math::Inverse(parent->GetWorldTransform()) * worldMatrix;
		            //     }
		            //
		            //     Vec3 position, rotation, scale;
		            //     Math::Decompose(worldMatrix, position, rotation, scale);
		            //     auto deltaRotation = rotation - Math::EulerAngles(entity->GetRotation());
		            // 	entity->SetTransform(position, Math::EulerAngles(entity->GetRotation()) + deltaRotation, scale);
		            // }
		            // else if (usingGuizmo)
		            // {
		            //     worldEditor->UpdateTransform(entity, gizmoInitialTransform, entity->GetTransform());
		            //     usingGuizmo = false;
		            // }
				}
			}

			//TODO - 2d grid
			// const float GRID_STEP = 64.0f;
			// static ImVec2 scrolling(0.0f, 0.0f);
			// ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();      // ImDrawList API uses screen coordinates!
			// ImVec2 canvas_sz = ImGui::GetContentRegionAvail();   // Resize canvas to what's available
			//
			// if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
			// if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;
			// ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);
			//
			// ImDrawList* draw_list = ImGui::GetWindowDrawList();
			// for (float x = fmodf(scrolling.x, GRID_STEP); x < canvas_sz.x; x += GRID_STEP)
			// 	draw_list->AddLine(ImVec2(canvas_p0.x + x, canvas_p0.y), ImVec2(canvas_p0.x + x, canvas_p1.y), IM_COL32(200, 200, 200, 40));
			// for (float y = fmodf(scrolling.y, GRID_STEP); y < canvas_sz.y; y += GRID_STEP)
			// 	draw_list->AddLine(ImVec2(canvas_p0.x, canvas_p0.y + y), ImVec2(canvas_p1.x, canvas_p0.y + y), IM_COL32(200, 200, 200, 40));

			if (worldEditor)
			{
				if (const ImGuiPayload* payload = ImGui::GetDragDropPayload())
				{
					// AssetPayload* assetPayload = static_cast<AssetPayload*>(ImGui::GetDragDropPayload()->Data);
					// f32           pad = 4.0f;
					// if (assetPayload && assetPayload->assetType == TypeInfo<Scene>::ID() && ImGui::BeginDragDropTargetCustom(ImRect(bb.x + pad, bb.y + pad, bb.width - pad, bb.height - pad), id))
					// {
					// 	if (ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
					// 	{
					// 		Entity::Instantiate(assetPayload->assetFile->GetUUID(), sceneEditor->GetRoot(), assetPayload->assetFile->GetFileName());
					// 		sceneEditor->MarkDirty();
					// 	}
					// 	ImGui::EndDragDropTarget();
					// }
				}
			}
		}

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
		{
			menuItemContext.ExecuteHotKeys(this);
		}


		if (openSceneOptions)
		{
			ImGui::OpenPopup("scene-options-modal");
		}

		auto popupRes = ImGuiBeginPopupMenu("scene-options-modal", 0, false);
		if (popupRes)
		{
			// if (!defaultRenderPipeline.pathTracerEnabled)
			// {
			//     bool taaEnabled = defaultRenderPipeline.antiAliasing == AntiAliasingType::TAA;
			//
			//     ImGui::Checkbox("TAA Enabled", &taaEnabled);
			//     if (!taaEnabled && defaultRenderPipeline.antiAliasing == AntiAliasingType::TAA)
			//     {
			//         defaultRenderPipeline.antiAliasing = AntiAliasingType::None;
			//         renderDirty = true;
			//     }
			//
			//     if (taaEnabled && defaultRenderPipeline.antiAliasing != AntiAliasingType::TAA)
			//     {
			//         defaultRenderPipeline.antiAliasing = AntiAliasingType::TAA;
			//         renderDirty = true;
			//     }
			// }
			//
			// if (Graphics::GetDeviceFeatures().raytraceEnabled)
			// {
			//     if (ImGui::Checkbox("PathTracer Enabled", &defaultRenderPipeline.pathTracerEnabled))
			//     {
			//         renderDirty = true;
			//     }
			//
			//     if (!defaultRenderPipeline.pathTracerEnabled)
			//     {
			//         if (ImGui::BeginMenu("DDGI"))
			//         {
			//             if (ImGui::Checkbox("DDGI Enabled", &defaultRenderPipeline.ddgiEnabled))
			//             {
			//                 renderDirty = true;
			//             }
			//
			//             if (ImGui::Checkbox("DDGI Show Debug Probes", &defaultRenderPipeline.ddgiShowDebugProbes))
			//             {
			//                 renderDirty = true;
			//             }
			//
			//             ImGui::EndMenu();
			//         }
			//     }
			// }
		}
		ImGuiEndPopupMenu(popupRes);


		ImGui::End();
	}


	void WorldViewWindow::OpenSceneView(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<WorldViewWindow>();
	}

	void WorldViewWindow::DuplicateSceneEntity(const MenuItemEventData& eventData) {}

	void WorldViewWindow::DeleteSceneEntity(const MenuItemEventData& eventData) {}

	bool WorldViewWindow::CheckSelectedEntity(const MenuItemEventData& eventData)
	{
		return false;
	}

	void WorldViewWindow::RecordRenderCommands(GPUCommandBuffer* cmd)
	{
		//SceneEditor*   sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		//Scene*         scene = sceneEditor->GetCurrentScene();
		//RenderStorage* storage = scene ? scene->GetRenderStorage() : nullptr;

		RenderStorage* storage = nullptr;

		sceneRendererViewport.SetCamera(0.1f, 300.0f, view, projection, freeViewCamera.GetPosition());
		sceneRendererViewport.Render(storage, cmd);

		cmd->BeginRenderPass(sceneRenderPass, Vec4(0.27f, 0.27f, 0.27f, 1.0f), 1, 0);

		ViewportInfo viewportInfo{};
		viewportInfo.x = 0.;
		viewportInfo.y = 0.;
		viewportInfo.y = 0.;
		viewportInfo.width = (f32)sceneExtent.width;
		viewportInfo.height = (f32)sceneExtent.height;
		viewportInfo.minDepth = 0.;
		viewportInfo.maxDepth = 1.;

		cmd->SetViewport(viewportInfo);
		cmd->SetScissor({0, 0}, sceneExtent);

		sceneRendererViewport.Blit(sceneRenderPass, cmd);

		cmd->EndRenderPass();

		cmd->ResourceBarrier(sceneTexture, ResourceState::ColorAttachment, ResourceState::ShaderReadOnly, 0, 0);
	}

	void WorldViewWindow::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuItemContext.AddMenuItem(menuItem);
	}

	void WorldViewWindow::RegisterType(NativeReflectType<WorldViewWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Scene Viewport", .action = OpenSceneView});

		AddMenuItem(MenuItemCreation{.itemName = "Duplicate", .priority = 210, .itemShortcut = {.ctrl = true, .presKey = Key::D}, .action = DuplicateSceneEntity, .enable = CheckSelectedEntity});
		AddMenuItem(MenuItemCreation{.itemName = "Delete", .priority = 220, .itemShortcut = {.presKey = Key::Delete}, .action = DeleteSceneEntity, .enable = CheckSelectedEntity});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::Center,
			.createOnInit = true
		});
	}
}
