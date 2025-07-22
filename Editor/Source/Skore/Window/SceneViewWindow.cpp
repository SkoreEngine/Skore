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

#include "SceneViewWindow.hpp"

#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/IO/Input.hpp"
#include "ImGuizmo.h"
#include "imgui_internal.h"
#include "Skore/Events.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	MenuItemContext SceneViewWindow::menuItemContext = {};

	static Logger& logger = Logger::GetLogger("Skore::SceneViewWindow");


	SceneViewWindow::~SceneViewWindow()
	{
		Event::Unbind<OnRecordRenderCommands, &SceneViewWindow::RecordRenderCommands>(this);

		if (sceneTexture) sceneTexture->Destroy();
		if (sceneRenderPass) sceneRenderPass->Destroy();
	}

	void SceneViewWindow::Init(u32 id, VoidPtr userData)
	{
		sceneRendererViewport.Init();

		guizmoOperation = ImGuizmo::TRANSLATE;
		Event::Bind<OnRecordRenderCommands, &SceneViewWindow::RecordRenderCommands>(this);
	}

	void SceneViewWindow::Draw(u32 id, bool& open)
	{
		const float iconSize = ImGui::CalcTextSize(ICON_FA_SUN).x;

		SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		bool ctrlDown = ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_RightCtrl));

		bool openSceneOptions = false;
		bool openCameraOptions = false;
		bool openViewportSettings = false;

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar;
		auto&            style = ImGui::GetStyle();
		ScopedStyleVar   windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
		{
			flags |= ImGuiWindowFlags_NoMove;
		}

		ImGuiBegin(id, ICON_FA_BORDER_ALL " Scene Viewport", &open, flags);
		const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

		{
			const bool moving = ImGui::IsMouseDown(ImGuiMouseButton_Right);
			const bool canChangeOptions = !moving && !ImGui::GetIO().WantCaptureKeyboard;

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

				if (ImGuiSelectionButton(ICON_FA_ARROW_POINTER, guizmoOperation == 0, buttonSize) || (canChangeOptions && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Q))))
				{
					guizmoOperation = 0;
				}

				if (ImGuiSelectionButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, guizmoOperation == ImGuizmo::TRANSLATE, buttonSize)
					|| (canChangeOptions && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_W))))
				{
					guizmoOperation = ImGuizmo::TRANSLATE;
				}

				if (ImGuiSelectionButton(ICON_FA_ROTATE, guizmoOperation == ImGuizmo::ROTATE, buttonSize)
					|| (canChangeOptions && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_E))))
				{
					guizmoOperation = ImGuizmo::ROTATE;
				}
				if (ImGuiSelectionButton(ICON_FA_EXPAND, guizmoOperation == ImGuizmo::SCALE, buttonSize)
					|| (canChangeOptions && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_R))))
				{
					guizmoOperation = ImGuizmo::SCALE;
				}


				if (guizmoMode == 0)
				{
					if (ImGui::Button(ICON_FA_CUBE "###local", buttonSize) || (canChangeOptions && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_T))))
					{
						guizmoMode = 1;
					}
				}
				else if (guizmoMode == 1)
				{
					if (ImGui::Button(ICON_FA_GLOBE "###global", buttonSize) || (canChangeOptions && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_T))))
					{
						guizmoMode = 0;
					}
				}

				if (ImGuiSelectionButton(ICON_FA_MAGNET, guizmoSnapEnabled, buttonSize) || (canChangeOptions && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Y))))
				{
					guizmoSnapEnabled = !guizmoSnapEnabled;
				}

				if (ImGuiSelectionButton(ICON_FA_TABLE_CELLS, sceneViewRenderer.drawGrid, buttonSize) || (canChangeOptions && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_G))))
				{
					sceneViewRenderer.drawGrid = !sceneViewRenderer.drawGrid;
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

				bool isSimulating = sceneEditor && sceneEditor->IsSimulationRunning();

				if (!isSimulating)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(139, 194, 74, 255));
				}

				if (windowStartedSimulation && !isSimulating)
				{
					windowStartedSimulation = false;
				}

				ImGui::BeginDisabled(isSimulating);

				if (ImGui::Button(ICON_FA_PLAY, buttonSize) && sceneEditor)
				{
					sceneEditor->StartSimulation();
					windowStartedSimulation = true;
				}

				ImGui::EndDisabled();

				if (!isSimulating)
				{
					ImGui::PopStyleColor();
				}

				ImGui::BeginDisabled(sceneEditor && !sceneEditor->IsSimulationRunning() || !windowStartedSimulation);

				if (isSimulating)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(199, 84, 80, 255));
				}

				if (ImGui::Button(ICON_FA_STOP, buttonSize) && sceneEditor)
				{
					sceneEditor->StopSimulation();
					windowStartedSimulation = false;
				}

				if (isSimulating)
				{
					ImGui::PopStyleColor();
				}

				ImGui::EndDisabled();

				ImGui::Spring(1.f);

				// if (!view2d)
				// {
				// 	if (ImGui::Button("3D##3d-view", buttonSize))
				// 	{
				// 		view2d = true;
				// 	}
				// }
				// else
				// {
				// 	if (ImGui::Button("2D##2d-view", buttonSize))
				// 	{
				// 		view2d = false;
				// 	}
				// }

				if (ImGui::Button(ICON_FA_CAMERA, buttonSize))
				{
					openCameraOptions = true;
				}

				if (ImGui::Button(ICON_FA_SLIDERS, buttonSize))
				{
					openViewportSettings = true;
				}

				ImGui::EndHorizontal();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);

				cursor.y = ImGui::GetCursorScreenPos().y;
				ImGui::EndChild();
				cursor.x = ImGui::GetCursorScreenPos().x;
			}


			if (!movingScene)
			{
				movingScene = !windowStartedSimulation && hovered && Input::IsMouseDown(MouseButton::Right);
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
			Vec2 mousePos = Input::GetMousePosition() - Vec2{(f32)bb.x, (f32)bb.y};

			f32    screenScale = 1;
			Extent extent = {static_cast<u32>(size.x * screenScale), static_cast<u32>(size.y * screenScale)};

			if (extent != sceneRendererViewport.GetExtent())
			{
				aspectRatio = static_cast<f32>(extent.width) / static_cast<f32>(extent.height);
				projection = Math::Perspective(Math::Radians(cameraFov), aspectRatio, 0.1f, 300.0f);

				sceneRendererViewport.Resize(extent);
				entityPicker.Resize(extent);
				sceneViewRenderer.Resize(extent);

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
						},
						AttachmentDesc{
							.texture = sceneRendererViewport.GetDepthTexture(),
							.initialState = ResourceState::DepthStencilReadOnly,
							.finalState = ResourceState::DepthStencilAttachment,
							.loadOp = AttachmentLoadOp::Load,
							.storeOp = AttachmentStoreOp::Store,
						}
					}
				});

				sceneExtent = extent;
			}

			ImGuiDrawTextureView(sceneTexture->GetTextureView(), bb);

			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(cursor.x, cursor.y, size.x, size.y);

			if (sceneEditor && sceneEditor->GetCurrentScene() && !sceneEditor->IsSimulationRunning())
			{
				auto guizmoMove = [&](Entity* entity) -> bool
				{
					Mat4 globalMatrix = entity->GetGlobalTransform();

					float snap[3] = {0.0f, 0.0f, 0.0f};

					if (guizmoSnapEnabled)
					{
						snap[0] = guizmoSnap.x;
						snap[1] = guizmoSnap.y;
						snap[2] = guizmoSnap.z;
					}

					ImGuizmo::Manipulate(&view[0][0],
					                     &projection[0][0],
					                     static_cast<ImGuizmo::OPERATION>(guizmoOperation),
					                     static_cast<ImGuizmo::MODE>(guizmoMode),
					                     &globalMatrix[0][0],
					                     nullptr,
					                     snap);

					if (ImGuizmo::IsUsing())
					{
						if (!usingGuizmo)
						{
							usingGuizmo = true;
							gizmoInitialTransform = entity->GetTransform();

							// if (entity->GetPrototype() != nullptr && !entity->IsComponentOverride(transformComponent))
							// {
							//     gizmoTransaction->CreateAction<OverridePrototypeComponentAction>(sceneEditor, entity, static_cast<Component*>(transformComponent))->Commit();
							// }
						}

						if (Entity* parent = entity->GetParent())
						{
							globalMatrix = Math::Inverse(parent->GetGlobalTransform()) * globalMatrix;
						}

						Vec3 position, rotation, scale;
						Math::Decompose(globalMatrix, position, rotation, scale);
						auto deltaRotation = rotation - Math::EulerAngles(entity->GetRotation());
						entity->SetTransform(position, Math::EulerAngles(entity->GetRotation()) + deltaRotation, scale);

						return true;
					}
					return false;
				};

				for (RID selectedEntity : sceneEditor->GetSelectedEntities())
				{
					if (Entity* entity = sceneEditor->GetCurrentScene()->FindEntityByRID(selectedEntity))
					{
						if (!guizmoMove(entity) && usingGuizmo)
						{
							if (RID rid = entity->GetTransformRID())
							{
								Transform& transform = entity->GetTransform();

								UndoRedoScope* scope = Editor::CreateUndoRedoScope("Entity Transform Update");

								ResourceObject transformObject = Resources::Write(rid);

								if (transform.position != transformObject.GetVec3(Transform::Position))
								{
									transformObject.SetVec3(Transform::Position, transform.position);
								}

								if (transform.rotation != transformObject.GetQuat(Transform::Rotation))
								{
									transformObject.SetQuat(Transform::Rotation, transform.rotation);
								}

								if (transform.scale != transformObject.GetVec3(Transform::Scale))
								{
									transformObject.SetVec3(Transform::Scale, transform.scale);
								}

								transformObject.Commit(scope);

								usingGuizmo = false;
							}
						}
					}
				}
			}

			bool selectedLight = false;

			ImDrawList* drawList = ImGui::GetCurrentWindow()->DrawList;

			if (!windowStartedSimulation && drawIcons)
			{
				if (Scene* scene = sceneEditor->GetCurrentScene())
				{
					RenderStorage* storage = scene->GetRenderStorage();

					const f32 iconScale = 2.0;

					auto DrawIcon = [&](Vec3 position, const char* icon, Color color, RID rid)
					{
						Vec2 screenPos = {};
						if (Math::ScreenToWorld(position, Extent{(u32)size.x, (u32)size.y}, projection * view, screenPos))
						{
							f32 iconSizeRect = iconSize * iconScale;
							ImVec2 rectMin = ImVec2(cursor.x + screenPos.x - iconSizeRect / 2.0f, cursor.y + screenPos.y - iconSizeRect / 2.0f);
							ImVec2 rectMax = ImVec2(cursor.x + screenPos.x + iconSizeRect / 2.0f, cursor.y + screenPos.y + iconSizeRect / 2.0f);

							drawList->AddText(rectMin, IM_COL32(color.red,
							                                    color.green,
							                                    color.blue,
							                                    255), icon);

							if (ImGui::IsMouseHoveringRect(rectMin, rectMax) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
							{
								sceneEditor->SelectEntity(rid, !ctrlDown);
								selectedLight = true;
							}
						}
					};

					ImGui::SetWindowFontScale(iconScale);

					for (const auto& itCamera: storage->cameras)
					{
						DrawIcon(itCamera.second.position, ICON_FA_CAMERA, Color::WHITE, RID(itCamera.second.id));
					}

					for (const auto& itLight: storage->lights)
					{
						const char* icon = "";
						switch (itLight.second.type)
						{
							case LightType::Point:
								icon = ICON_FA_LIGHTBULB;
								break;
							case LightType::Spot:
								icon = ICON_FA_LIGHTBULB;
								break;
							case LightType::Directional:
								icon = ICON_FA_SUN;
								break;
						}
						DrawIcon(Math::GetTranslation(itLight.second.transform), icon, itLight.second.color, RID(itLight.second.id));
					}
					ImGui::SetWindowFontScale(1.0);
				}
			}

			bool isImgHovered = ImGui::IsMouseHoveringRect(ImVec2(bb.x, bb.y), ImVec2(bb.width, bb.height), false);
			Input::DisableInputs(!isImgHovered);

			if (!windowStartedSimulation &&
				!ImGuizmo::IsUsing() &&
				isImgHovered &&
				!selectedLight &&
				ImGui::IsWindowHovered() &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{

				if (RID selectedEntity = entityPicker.PickEntity(projection * view, sceneEditor, mousePos))
				{
					RID parentNoPrototype = Resources::GetParent(selectedEntity);

					while (Resources::GetPrototype(Resources::GetParent(parentNoPrototype)))
					{
						parentNoPrototype = Resources::GetParent(parentNoPrototype);
					}

					if (sceneEditor->IsSelected(parentNoPrototype) || sceneEditor->IsSelected(selectedEntity) || parentNoPrototype == sceneEditor->GetRootEntity())
					{
						sceneEditor->SelectEntity(selectedEntity, !ctrlDown);
					}
					else
					{
						sceneEditor->SelectEntity(parentNoPrototype, !ctrlDown);
					}
				}
				else
				{
					sceneEditor->ClearSelection();
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

			bool previewRendered = false;

			if (sceneEditor && !windowStartedSimulation)
			{
				if (const ImGuiPayload* payload = ImGui::GetDragDropPayload())
				{
					if (payload->IsDataType(SK_ASSET_PAYLOAD))
					{
						if (AssetPayload* assetPayload = static_cast<AssetPayload*>(ImGui::GetDragDropPayload()->Data))
						{
							f32 pad = 4.0f;

							RID assetType = {};

							if (assetPayload->asset && Resources::GetType(assetPayload->asset)->GetID() == TypeInfo<EntityResource>::ID())
							{
								assetType = assetPayload->asset;
							}
							else if (assetPayload->asset && Resources::GetType(assetPayload->asset)->GetID() == TypeInfo<DCCAssetResource>::ID())
							{
								if (ResourceObject dccAssetObject = Resources::Read(assetPayload->asset))
								{
									assetType = dccAssetObject.GetSubObject(DCCAssetResource::Entity);
								}
							}

							if (assetType && ImGui::BeginDragDropTargetCustom(ImRect(bb.x + pad, bb.y + pad, bb.width - pad, bb.height - pad), id))
							{
								previewRendered = true;
								if (previewEntity == nullptr)
								{
									previewEntity = sceneEditor->GetCurrentScene()->GetRootEntity()->CreateChildFromAsset(assetType);
								}

								f32 x = (2.0f * mousePos.x) / extent.width - 1.0f;
								f32 y = 1.0f - (2.0f * mousePos.y) / extent.height;
								Vec4 rayNDC = Vec4(x, y, -1.0f, 1.0f);

								Mat4 inverseVP = Math::Inverse(projection * view);
								Vec4 rayWorld = inverseVP * rayNDC;
								rayWorld /= rayWorld.w;

								Vec3 cameraPos = freeViewCamera.GetPosition();
								Vec3 rayDirection = Math::Normalize(Vec3(rayWorld) - cameraPos);
								float t = -cameraPos.y / rayDirection.y;

								Vec3 entityPos = Vec3(cameraPos.x + t * rayDirection.x, 0, cameraPos.z + t * rayDirection.z);

								if (previewEntity)
								{
									previewEntity->SetPosition(entityPos);
								}

								if (ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD, ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_AcceptNoPreviewTooltip))
								{
									sceneEditor->CreateFromAsset(assetType, false, entityPos);
								}
								ImGui::EndDragDropTarget();
							}
						}
					}
				}
			}

			if (!previewRendered && previewEntity)
			{
				previewEntity->DestroyImmediate();
				previewEntity = nullptr;
			}
		}

		if (hovered)
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


		if (openCameraOptions)
		{
			ImGui::OpenPopup("camera-options-modal");
		}

		popupRes = ImGuiBeginPopupMenu("camera-options-modal", 0, false);
		if (popupRes)
		{
			if (ImGui::BeginTable("table-camera-options-modal", 2, flags))
			{
				ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, 150 * style.ScaleFactor);
				ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthFixed, 180 * style.ScaleFactor);

				//field of view
				ImGui::TableNextColumn();
				ImGui::Text("Field of View");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##fov", &cameraFov, 4.0f, 120.0f, "%.0f"))
				{
					projection = Math::Perspective(Math::Radians(cameraFov), aspectRatio, 0.1f, 300.0f);
				}

				//speed
				ImGui::TableNextColumn();
				ImGui::Text("Speed");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				ImGui::SliderFloat("##speed", &freeViewCamera.cameraSpeed, 1.f, 100.0f, "%.0f");


				//enable smooth
				ImGui::TableNextColumn();
				ImGui::Text("Smooth Camera");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);

				bool checked = freeViewCamera.smoothingFactor > 0.0f;
				if (ImGui::Checkbox("##smooth", &checked))
				{
					freeViewCamera.smoothingFactor = checked ? 0.7f : 0.0f;
					freeViewCamera.movementSmoothingFactor = checked ? 0.85f : 0.0f;
				}


				ImGui::EndTable();
			}
		}
		ImGuiEndPopupMenu(popupRes);


		if (openViewportSettings)
		{
			ImGui::OpenPopup("viewport-options-modal");
		}

		auto addTableBoolOption = [&](const char* label, bool* value) -> bool
		{
			ImGui::TableNextColumn();
			ImGui::Text(label);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			static char buffer[50];
			sprintf(buffer, "###%s", label);

			if (ImGui::Checkbox(buffer, value))
			{
				return true;
			}
			return false;
		};

		popupRes = ImGuiBeginPopupMenu("viewport-options-modal", 0, false);
		if (popupRes)
		{
			if (ImGui::BeginTable("table-viewport-options-modal", 2, flags))
			{
				ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, 150 * style.ScaleFactor);
				ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthFixed, 180 * style.ScaleFactor);

				addTableBoolOption("Selection Outline", &sceneViewRenderer.drawSelectionOutline);
				addTableBoolOption("Draw Icons", &drawIcons);
				addTableBoolOption("Draw Debug Physics", &sceneViewRenderer.drawDebugPhysics);

				ImGui::EndTable();
			}
		}
		ImGuiEndPopupMenu(popupRes);


		ImGui::End();
	}


	void SceneViewWindow::OpenSceneView(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<SceneViewWindow>();
	}

	void SceneViewWindow::DuplicateSceneEntity(const MenuItemEventData& eventData)
	{
		Editor::GetCurrentWorkspace().GetSceneEditor()->DuplicateSelected();
	}

	void SceneViewWindow::DeleteSceneEntity(const MenuItemEventData& eventData)
	{
		Editor::GetCurrentWorkspace().GetSceneEditor()->DestroySelected();
	}

	bool SceneViewWindow::CheckSelectedEntity(const MenuItemEventData& eventData)
	{
		return Editor::GetCurrentWorkspace().GetSceneEditor()->HasSelectedEntities();
	}

	void SceneViewWindow::RecordRenderCommands(GPUCommandBuffer* cmd)
	{
		SceneEditor*   sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		Scene*         scene = sceneEditor->GetCurrentScene();
		RenderStorage* storage = scene ? scene->GetRenderStorage() : nullptr;

		if (!windowStartedSimulation || storage == nullptr || storage->cameras.empty())
		{
			sceneRendererViewport.SetCamera(0.1f, 300.0f, view, projection, freeViewCamera.GetPosition());
		}
		else
		{
			if (std::optional<CameraRenderData> camera = storage->GetCurrentCamera())
			{
				Mat4 currentProjection = {};
				if (camera->projection == Camera::Projection::Perspective)
				{
					currentProjection = Math::Perspective(Math::Radians(camera->fov), aspectRatio, camera->nearPlane, camera->farPlane);
				}
				else
				{
					//get current values here?
					currentProjection = Math::Ortho(0, 0, 10, 10, camera->nearPlane, camera->farPlane);
				}
				sceneRendererViewport.SetCamera(camera->nearPlane, camera->farPlane, camera->viewMatrix, currentProjection, camera->position);
			}
		}

		sceneRendererViewport.Render(storage, cmd);

		if (!windowStartedSimulation)
		{
			sceneViewRenderer.Render(sceneEditor, sceneRenderPass, sceneRendererViewport.GetSceneDescriptorSet(), cmd);
		}

		cmd->BeginRenderPass(sceneRenderPass, Vec4(0.27f, 0.27f, 0.27f, 1.0f), 1, 0);

		sceneRendererViewport.Blit(sceneRenderPass, cmd);

		if (!windowStartedSimulation)
		{
			sceneViewRenderer.Blit(sceneEditor, sceneRenderPass, sceneRendererViewport.GetSceneDescriptorSet(), cmd);
		}

		cmd->EndRenderPass();
		cmd->ResourceBarrier(sceneTexture, ResourceState::ColorAttachment, ResourceState::ShaderReadOnly, 0, 0);
	}

	void SceneViewWindow::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuItemContext.AddMenuItem(menuItem);
	}

	void SceneViewWindow::RegisterType(NativeReflectType<SceneViewWindow>& type)
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
