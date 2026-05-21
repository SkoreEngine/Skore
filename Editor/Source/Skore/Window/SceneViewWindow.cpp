#include "Skore/Window/SceneViewWindow.hpp"

#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/IO/Input.hpp"
#include "ImGuizmo.h"
#include "imgui_internal.h"
#include "Skore/Events.hpp"
#include "Skore/Audio/AudioEngine.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Scene/SceneManager.hpp"
#include "Skore/Scene/Components/Camera.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Utils/StaticContent.hpp"

namespace Skore
{
	struct DefaultRenderPipeline;

	MenuItemContext SceneViewWindow::menuItemContext = {};

	static SceneViewWindow* lastWindow = nullptr;
	static Logger& logger = Logger::GetLogger("Skore::SceneViewWindow");

	namespace
	{
		const float snapTension = 0.5f;

		//from imguizmo
		void ComputeSnap(f32* value, f32 snap)
		{
			if (snap <= FLT_EPSILON)
			{
				return;
			}

			f32 modulo = fmodf(*value, snap);
			f32 moduloRatio = fabsf(modulo) / snap;
			if (moduloRatio < snapTension)
			{
				*value -= modulo;
			}
			else if (moduloRatio > (1.f - snapTension))
			{
				*value = *value - modulo + snap * ((*value < 0.f) ? -1.f : 1.f);
			}
		}
		void ComputeSnap(Vec3& value, const Vec3& snap)
		{
			for (int i = 0; i < 3; i++)
			{
				ComputeSnap(&value[i], snap[i]);
			}
		}
	}

	SceneViewWindow::~SceneViewWindow()
	{
		if (logoTexture) logoTexture->Destroy();
		if (lastWindow == this)
		{
			lastWindow = nullptr;
		}

		if (renderPipelineContext)
		{
			renderPipelineContext->Destroy();
		}
	}

	void SceneViewWindow::Init(u32 id, VoidPtr userData)
	{
		freeViewCamera.SetPosition(Vec3{0.0f, 10.0f, 0.0f});
		guizmoOperation = ImGuizmo::TRANSLATE;
		logoTexture = StaticContent::GetTexture("Content/Images/LogoSmall.jpeg");
	}

	void SceneViewWindow::Draw3DViewport(u32 id)
	{
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

		f32    screenScale = 1;
		Extent extent = {static_cast<u32>(size.x * screenScale), static_cast<u32>(size.y * screenScale)};

		Rect bb{(i32)cursor.x, (i32)cursor.y, u32(cursor.x + size.x), u32(cursor.y + size.y)};
		mousePosRelativeToWindow = (Input::GetMousePosition() - Vec2{(f32)bb.x, (f32)bb.y}) * screenScale;

		bool   sizeUpdated = renderPipelineContext == nullptr || renderPipelineContext->GetOutputSize() != extent;


		if (renderPipelineContext == nullptr)
		{
			RenderPipelineContextSettings settings;
			settings.initialOutputSize = extent;
			settings.userData = this;

			Array<TypeID> extraModules = {};
			extraModules.EmplaceBack(sktypeid(SceneViewPipelineModule));

			renderPipelineContext = RenderPipeline::CreateContext(sktypeid(DefaultRenderPipeline), extraModules, settings);
		}

		if (sizeUpdated)
		{
			aspectRatio = static_cast<f32>(extent.width) / static_cast<f32>(extent.height);
			entityPicker.Resize(extent);
			renderPipelineContext->SetOutputSize(extent);
		}

		if (selectedTextureToShow.Empty())
		{
			ImGuiDrawTextureView(renderPipelineContext->GetColorOutput()->GetTextureView(), bb, ImVec4(1, 1, 1, 1), Graphics::GetNearestClampToEdgeSampler());
		}
		else
		{
			ImGuiDrawTextureView(renderPipelineContext->GetTexture(selectedTextureToShow)->GetTextureView(), bb, ImVec4(1, 1, 1, 1), Graphics::GetNearestClampToEdgeSampler());
		}

		// if (windowStartedSimulation)
		// {
		// 	ImGui::GetForegroundDrawList()->AddRect(
		// 		ImVec2(bb.x + 1, bb.y + 1),
		// 		ImVec2(bb.width -1 , bb.height - 1),
		// 		IM_COL32(255, 255, 0, 255),
		// 		0.f,
		// 		0,
		// 		2
		// 	);
		// }

		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(cursor.x, cursor.y, size.x, size.y);

		if (sceneEditor && sceneEditor->GetCurrentScene() && !sceneEditor->IsSimulationRunning())
		{
			auto guizmoMove = [&](Entity* entity) -> bool
			{
				if (Transform* transform = entity->GetComponent<Transform>())
				{
					Mat4 globalMatrix = entity->GetWorldTransform();

					float snap[3] = {0.0f, 0.0f, 0.0f};

					if (guizmoSnapEnabled || ctrlDown)
					{
						snap[0] = guizmoSnap.x;
						snap[1] = guizmoSnap.y;
						snap[2] = guizmoSnap.z;
					}

					ImGuizmo::Manipulate(&renderPipelineContext->camera.view[0][0],
					                     &renderPipelineContext->camera.projectionNoJitter[0][0],
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
						}

						globalMatrix = Mat4::Inverse(transform->GetParentWorldTransform()) * globalMatrix;

						Vec3 position, rotation, scale;
						Mat4::Decompose(globalMatrix, position, rotation, scale);
						auto deltaRotation = rotation - Quat::EulerAngles(transform->GetRotation());
						transform->SetTransform(position, Quat::EulerAngles(transform->GetRotation()) + deltaRotation, scale);

						return true;
					}
				}
				return false;
			};

			for (RID selectedEntity : sceneEditor->GetSelectedEntities())
			{
				if (Entity* entity = sceneEditor->GetCurrentScene()->FindEntityByRID(selectedEntity))
				{
					if (!guizmoMove(entity) && usingGuizmo)
					{
						if (Transform* transform = entity->GetComponent<Transform>())
						{
							if (RID rid = transform->GetRID())
							{
								UndoRedoScope* scope = Editor::CreateUndoRedoScope("Entity Transform Update");

								ResourceObject transformObject = Resources::Write(rid);

								if (transform->GetPosition() != transformObject.GetVec3(Transform::Position))
								{
									transformObject.SetVec3(Transform::Position, transform->GetPosition());
								}

								if (transform->GetRotation() != transformObject.GetQuat(Transform::Rotation))
								{
									transformObject.SetQuat(Transform::Rotation, transform->GetRotation());
								}

								if (transform->GetScale() != transformObject.GetVec3(Transform::Scale))
								{
									transformObject.SetVec3(Transform::Scale, transform->GetScale());
								}

								transformObject.Commit(scope);

								usingGuizmo = false;
							}
						}
					}
				}
			}
		}

		bool selectedIcon = false;

		ImDrawList* drawList = ImGui::GetCurrentWindow()->DrawList;

		if (!windowStartedSimulation && drawIcons)
		{
			if (Scene* scene = sceneEditor->GetCurrentScene())
			{
				const f32 iconScale = 2.0;

				auto DrawIcon = [&](Vec3 position, const char* icon, Color color, RID rid)
				{
					Vec2 screenPos = {};
					if (Math::ScreenToWorld(position, Extent{(u32)size.x, (u32)size.y}, renderPipelineContext->camera.projectionNoJitter * renderPipelineContext->camera.view, screenPos))
					{
						f32    iconSizeRect = iconSize * iconScale;
						ImVec2 rectMin = ImVec2(cursor.x + screenPos.x - iconSizeRect / 2.0f, cursor.y + screenPos.y - iconSizeRect / 2.0f);
						ImVec2 rectMax = ImVec2(cursor.x + screenPos.x + iconSizeRect / 2.0f, cursor.y + screenPos.y + iconSizeRect / 2.0f);

						drawList->AddText(rectMin, IM_COL32(color.red,
						                                    color.green,
						                                    color.blue,
						                                    255), icon);

						if (rid && ImGui::IsMouseHoveringRect(rectMin, rectMax) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
						{
							sceneEditor->SelectEntity(rid, !ctrlDown);
							selectedIcon = true;
						}
					}
				};

				ImGui::SetWindowFontScale(iconScale);

				scene->Iterate<Camera>([&](Camera* camera)
				{
					Entity* entity = camera->GetEntity();
					DrawIcon(entity->GetWorldPosition(), ICON_FA_CAMERA, Color::WHITE, entity->GetRID());
				});

				scene->Iterate<LightComponent>([&](LightComponent* light)
				{
					const char* icon = "";
					switch (light->GetLightType())
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
					Entity* entity = light->GetEntity();
					DrawIcon(entity->GetWorldPosition(), icon, light->GetColor(), entity->GetRID());
				});

				ImGui::SetWindowFontScale(1.0);
			}
		}

		bool isImgHovered = ImGui::IsMouseHoveringRect(ImVec2(bb.x, bb.y), ImVec2(bb.width, bb.height), false);
		Input::DisableInputs(!isImgHovered && !movingScene);

		if (!windowStartedSimulation &&
			!ImGuizmo::IsUsing() &&
			isImgHovered &&
			!selectedIcon &&
			ImGui::IsWindowHovered() &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (RID selectedEntity = entityPicker.PickEntity(renderPipelineContext->camera.projectionNoJitter * renderPipelineContext->camera.view, sceneEditor, mousePosRelativeToWindow))
			{

				if (Resources::GetPrototype(selectedEntity))
				{
					RID parentNoPrototype = Resources::GetParent(selectedEntity);
					while (Resources::GetPrototype(Resources::GetParent(parentNoPrototype)))
					{
						parentNoPrototype = Resources::GetParent(parentNoPrototype);
					}
					selectedEntity = parentNoPrototype;
				}


				if (sceneEditor->IsSelected(selectedEntity) || sceneEditor->IsSelected(selectedEntity) || selectedEntity == sceneEditor->GetOpenedResource())
				{
					sceneEditor->SelectEntity(selectedEntity, !ctrlDown);
				}
				else
				{
					sceneEditor->SelectEntity(selectedEntity, !ctrlDown);
				}
			}
			else
			{
				sceneEditor->ClearSelection();
			}
		}

		bool previewRendered = false;

		if (sceneEditor && sceneEditor->GetOpenedResource() && !windowStartedSimulation)
		{
			if (const ImGuiPayload* payload = ImGui::GetDragDropPayload())
			{
				if (payload->IsDataType(SK_ASSET_PAYLOAD))
				{
					if (AssetPayload* assetPayload = static_cast<AssetPayload*>(ImGui::GetDragDropPayload()->Data))
					{
						f32 pad = 4.0f;

						RID entityRID = {};

						if (assetPayload->asset && Resources::GetType(assetPayload->asset)->GetID() == TypeInfo<EntityResource>::ID())
						{
							entityRID = assetPayload->asset;
						}
						else if (assetPayload->asset && Resources::GetType(assetPayload->asset)->GetID() == TypeInfo<DCCAsset>::ID())
						{
							ResourceObject dccObject = Resources::Read(assetPayload->asset);
							if (dccObject)
							{
								entityRID = dccObject.GetSubObject(DCCAsset::RootEntity);
							}
						}
						else if (assetPayload->asset && Resources::GetType(assetPayload->asset)->GetID() == TypeInfo<MeshResource>::ID())
						{
							if (!previewEntityRID)
							{
								RID meshRenderer = Resources::Create<StaticMeshRenderer>(UUID::RandomUUID());
								ResourceObject staticMeshRenderObject = Resources::Write(meshRenderer);
								staticMeshRenderObject.SetReference(staticMeshRenderObject.GetIndex("mesh"), assetPayload->asset);
								staticMeshRenderObject.Commit();

								previewEntityRID = Resources::Create<EntityResource>(UUID::RandomUUID());

								ResourceObject entityObject = Resources::Write(previewEntityRID);
								entityObject.SetString(EntityResource::Name, ResourceAssets::GetAssetName(assetPayload->asset));
								entityObject.AddToSubObjectList(EntityResource::Components, Resources::Create<Transform>(UUID::RandomUUID()));
								entityObject.AddToSubObjectList(EntityResource::Components, meshRenderer);
								entityObject.Commit();

							}
							entityRID = previewEntityRID;
						}

						if (entityRID && ImGui::BeginDragDropTargetCustom(ImRect(bb.x + pad, bb.y + pad, bb.width - pad, bb.height - pad), id))
						{
							previewRendered = true;
							if (previewEntity == nullptr)
							{
								previewEntity = sceneEditor->GetCurrentScene()->CreateEntityFromRID(entityRID);
							}

							if (Transform* transform = previewEntity->GetComponent<Transform>(); transform != nullptr && viewType == ViewType_3D)
							{
								f32  x = (2.0f * mousePosRelativeToWindow.x) / extent.width - 1.0f;
								f32  y = 1.0f - (2.0f * mousePosRelativeToWindow.y) / extent.height;
								Vec4 rayNDC = Vec4(x, y, -1.0f, 1.0f);

								Mat4 inverseVP = Mat4::Inverse(renderPipelineContext->camera.viewProjectionNoJitter);
								Vec4 rayWorld = inverseVP * rayNDC;
								rayWorld /= rayWorld.w;

								Vec3  cameraPos = freeViewCamera.GetPosition();
								Vec3  rayDirection = Vec3::Normalize(Vec3(rayWorld) - cameraPos);
								float t = -cameraPos.y / rayDirection.y;

								Vec3 entityPos = Vec3(cameraPos.x + t * rayDirection.x, 0, cameraPos.z + t * rayDirection.z);

								if (guizmoSnapEnabled || ctrlDown)
								{
									ComputeSnap(entityPos, guizmoSnap);
								}

								transform->SetPosition(entityPos);

								if (ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD, ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_AcceptNoPreviewTooltip))
								{
									UndoRedoScope* scope = Editor::CreateUndoRedoScope("Place Entity");

									RID newEntity;
									if (previewEntityRID)
									{
										newEntity = sceneEditor->Clone(sceneEditor->GetOpenedResource(), previewEntityRID, scope);
									}
									else
									{
										newEntity = sceneEditor->CreateFromAsset(sceneEditor->GetOpenedResource(), entityRID, scope);
									}

									RID transformRID = EntityResource::GetOrCreateComponent(newEntity, TypeInfo<Transform>::ID(), scope);
									ResourceObject transformObject = Resources::Write(transformRID);
									transformObject.SetVec3(Transform::Position, entityPos);
									transformObject.Commit(scope);
									sceneEditor->SelectEntity(newEntity, true, scope);
								}
							}
							else
							{
								if (ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD, ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_AcceptNoPreviewTooltip))
								{
									UndoRedoScope* scope = Editor::CreateUndoRedoScope("Place Entity");
									RID newEntity = sceneEditor->CreateFromAsset(sceneEditor->GetOpenedResource(), entityRID, scope);
									sceneEditor->SelectEntity(newEntity, true, scope);
								}
							}

							ImGui::EndDragDropTarget();
						}
					}
				}
			}
		}

		if (!previewRendered)
		{
			if (previewEntity)
			{
				previewEntity->DestroyImmediate();
				previewEntity = nullptr;
			}

			if (previewEntityRID)
			{
				Resources::Destroy(previewEntityRID);
				previewEntityRID = {};
			}
		}
	}

	void SceneViewWindow::Draw2DViewport(u32 id)
	{
#if 0
		if (sceneViewRenderer.drawGrid)
		{
			const float   GRID_STEP = 64.0f;
			static ImVec2 scrolling(0.0f, 0.0f);
			ImVec2        canvas_p0 = ImGui::GetCursorScreenPos();    // ImDrawList API uses screen coordinates!
			ImVec2        canvas_sz = ImGui::GetContentRegionAvail(); // Resize canvas to what's available

			if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
			if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;
			ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			for (float x = fmodf(scrolling.x, GRID_STEP); x < canvas_sz.x; x += GRID_STEP)
			{
				draw_list->AddLine(ImVec2(canvas_p0.x + x, canvas_p0.y), ImVec2(canvas_p0.x + x, canvas_p1.y), IM_COL32(200, 200, 200, 40));
			}

			for (float y = fmodf(scrolling.y, GRID_STEP); y < canvas_sz.y; y += GRID_STEP)
			{
				draw_list->AddLine(ImVec2(canvas_p0.x, canvas_p0.y + y), ImVec2(canvas_p1.x, canvas_p0.y + y), IM_COL32(200, 200, 200, 40));
			}
		}
#endif

	}

	void SceneViewWindow::Draw(u32 id, bool& open)
	{
		lastWindow = this;

		if (iconSize == 0)
		{
			iconSize = ImGui::CalcTextSize(ICON_FA_SUN).x;
		}

		sceneEditor = workspace->GetSceneEditor();
		ctrlDown = ImGui::IsKeyDown((ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown((ImGuiKey_RightCtrl));

		bool openSceneOptions = false;
		bool openCameraOptions = false;
		bool openViewportSettings = false;
		bool openGridSnapSettings = false;

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar;
		auto&            style = ImGui::GetStyle();
		ScopedStyleVar   windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
		{
			flags |= ImGuiWindowFlags_NoMove;
		}

		ImGuiBegin(id, ICON_FA_BORDER_ALL " Scene Viewport", &open, flags);

		hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

		{
			const bool moving = ImGui::IsMouseDown(ImGuiMouseButton_Right);
			const bool canChangeOptions = !moving && !ImGui::GetIO().WantCaptureKeyboard;

			size = FromImVec2(ImGui::GetContentRegionAvail());
			initCursor = FromImVec2(ImGui::GetCursorScreenPos());

			{
				ImVec2 buttonSize = ImVec2(25 * style.ScaleFactor, 22 * style.ScaleFactor);
				ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(style.ScaleFactor * 2, style.ScaleFactor * 2));
				ScopedStyleVar space(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
				ImGui::BeginChild(id + 1000, ImVec2(0, buttonSize.y + (5 * style.ScaleFactor)), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar);

				//ScopedStyleVar itemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::BeginHorizontal("horizontal-sceneview-top", ImVec2(ImGui::GetContentRegionAvail().x, buttonSize.y));

				if (ImGuiSelectionButton(ICON_FA_ARROW_POINTER, guizmoOperation == 0, buttonSize) || (canChangeOptions && ImGui::IsKeyDown((ImGuiKey_Q))))
				{
					guizmoOperation = 0;
				}

				if (ImGuiSelectionButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, guizmoOperation == ImGuizmo::TRANSLATE, buttonSize)
					|| (canChangeOptions && ImGui::IsKeyDown((ImGuiKey_W))))
				{
					guizmoOperation = ImGuizmo::TRANSLATE;
				}

				if (ImGuiSelectionButton(ICON_FA_ROTATE, guizmoOperation == ImGuizmo::ROTATE, buttonSize)
					|| (canChangeOptions && ImGui::IsKeyDown((ImGuiKey_E))))
				{
					guizmoOperation = ImGuizmo::ROTATE;
				}
				if (ImGuiSelectionButton(ICON_FA_EXPAND, guizmoOperation == ImGuizmo::SCALE, buttonSize)
					|| (canChangeOptions && ImGui::IsKeyDown((ImGuiKey_R))))
				{
					guizmoOperation = ImGuizmo::SCALE;
				}


				if (guizmoMode == 0)
				{
					if (ImGui::Button(ICON_FA_CUBE "###local", buttonSize) || (canChangeOptions && ImGui::IsKeyPressed((ImGuiKey_T))))
					{
						guizmoMode = 1;
					}
				}
				else if (guizmoMode == 1)
				{
					if (ImGui::Button(ICON_FA_GLOBE "###global", buttonSize) || (canChangeOptions && ImGui::IsKeyPressed((ImGuiKey_T))))
					{
						guizmoMode = 0;
					}
				}

				if (ImGuiSelectionButton(ICON_FA_MAGNET, guizmoSnapEnabled, buttonSize) || (canChangeOptions && ImGui::IsKeyPressed((ImGuiKey_Y))))
				{
					guizmoSnapEnabled = !guizmoSnapEnabled;
				}

				if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					openGridSnapSettings = true;
				}

				if (ImGuiSelectionButton(ICON_FA_TABLE_CELLS, drawGrid, buttonSize) || (canChangeOptions && ImGui::IsKeyPressed((ImGuiKey_G))))
				{
					drawGrid = !drawGrid;
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
					RenderPipeline::SetMainContext(renderPipelineContext);
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
					RenderPipeline::SetMainContext(nullptr);
				}

				if (isSimulating)
				{
					ImGui::PopStyleColor();
				}

				ImGui::EndDisabled();

				ImGui::Spring(1.f);

				if (ImGuiSelectionButton(ICON_CUSTOM_2D, viewType == ViewType_2D, buttonSize))
				{
					viewType = ViewType_2D;
				}

				if (ImGuiSelectionButton(ICON_CUSTOM_3D, viewType == ViewType_3D, buttonSize))
				{
					viewType = ViewType_3D;
				}

				if (AudioEngine::IsSoundEnabled())
				{
					if (ImGui::Button(ICON_FA_VOLUME_HIGH, buttonSize))
					{
						AudioEngine::SetSoundEnabled(false);
					}
				}
				else
				{
					if (ImGui::Button(ICON_FA_VOLUME_XMARK, buttonSize))
					{
						AudioEngine::SetSoundEnabled(true);
					}
				}

				if (ImGui::Button(ICON_FA_CAMERA, buttonSize))
				{
					openCameraOptions = true;
				}

				if (ImGui::Button(ICON_FA_SLIDERS, buttonSize))
				{
					openViewportSettings = true;
				}

				ImGui::EndHorizontal();

				//ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
				ImGui::Dummy(ImVec2(0, 2));
				cursor.y = ImGui::GetCursorScreenPos().y;
				ImGui::EndChild();
				cursor.x = ImGui::GetCursorScreenPos().x;
			}
		}

		Vec2 diffCursor = cursor - initCursor;
		size -= diffCursor;

		if (size > 0.0f)
		{
			if (viewType == ViewType_3D)
			{
				Draw3DViewport(id);
			}
			else if (viewType == ViewType_2D)
			{
				Draw2DViewport(id);
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
			if (Graphics::GetDevice()->GetFeatures().rayTracing)
			{
				// if (ImGui::Checkbox("PathTracer Enabled", &pathTracerEnabled))
				// {
				// }
			}

			if (ImGui::BeginMenu("Debug Options"))
			{
				bool changed = false;

				static int e = -1;
				changed |= ImGui::RadioButton("Display Normal", &e, -1);

				Array<RenderPipelineContext::PipelineResourceStorage> resources = renderPipelineContext->GetResources();

				for (int i = 0; i < resources.Size(); ++i)
				{
					if (resources[i].HasTexture() && !resources[i].desc.name.Empty())
					{
						String formattedName = "Display " + FormatName(resources[i].desc.name);
						changed |= ImGui::RadioButton(formattedName.CStr(), &e, i);
					}
				}

				if (changed)
				{
					if (e == -1)
					{
						selectedTextureToShow.Clear();
					}
					else
					{
						selectedTextureToShow = resources[e].desc.name;
					}
				}

				ImGui::EndMenu();
			}

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
				ImGui::SliderFloat("##fov", &cameraFov, 4.0f, 120.0f, "%.0f");

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

				ImGui::TableNextColumn();
				ImGui::Text("Camera Position");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);

				Vec3 position = freeViewCamera.GetPosition();
				if (ImGui::InputFloat3("###cameraPos", &position.x))
				{
					freeViewCamera.SetPosition(position);
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

				addTableBoolOption("Selection Outline", &drawSelectionOutline);
				addTableBoolOption("Draw Icons", &drawIcons);
				addTableBoolOption("Draw Debug Physics", &drawDebugPhysics);
				addTableBoolOption("Show All Physics Shapes", &showAllPhysicsShapes);
				addTableBoolOption("Draw Mesh AABB", &drawMeshAABB);
				addTableBoolOption("Draw NavMesh", &drawNavMesh);
				addTableBoolOption("Lock Camera Frustum", &lockCameraFrustum);

				ImGui::EndTable();
			}
		}
		ImGuiEndPopupMenu(popupRes);


		if (openGridSnapSettings)
		{
			ImGui::OpenPopup("grid-snap-options-modal");
		}

		popupRes = ImGuiBeginPopupMenu("grid-snap-options-modal", 0, false);
		if (popupRes)
		{
			if (ImGui::BeginTable("grids-nap-options-modal-table", 2, flags))
			{
				ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, 150 * style.ScaleFactor);
				ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthFixed, 180 * style.ScaleFactor);

				//speed
				ImGui::TableNextColumn();
				ImGui::Text("Size");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);

				ImGui::InputFloat3("###settings-grid", &guizmoSnap.x);

				//ImGui::InputFloat3("##speed", &freeViewCamera.cameraSpeed, 1.f, 100.0f, "%.0f");


				ImGui::EndTable();
			}
		}
		ImGuiEndPopupMenu(popupRes);
		ImGui::End();
	}

	void SceneViewWindow::Render(GPUCommandBuffer* cmd)
	{
		Scene* scene = sceneEditor->GetCurrentScene();
		if (!scene) scene = &emptyScene;

		if (renderPipelineContext)
		{
			if (!sceneEditor->IsSimulationRunning())
			{
				renderPipelineContext->UpdateCamera(0.1f, 300.0f, cameraFov, Projection::Perspective, {freeViewCamera.GetView()}, freeViewCamera.GetPosition(), !lockCameraFrustum);
			}
			renderPipelineContext->Execute(cmd, scene);
		}
	}

	void SceneViewWindow::ViewEntity(Entity* entity)
	{
		if (lastWindow && entity)
		{
			Vec3 center = {};
			f32  radius = 0;

			if (AABB aabb = entity->GetBounds())
			{
				center = aabb.GetCenter();
				radius = aabb.GetRadius();
			}
			else
			{
				center = entity->GetWorldPosition();
				radius = 1.0f;
			}

			f32  distance = radius / tan(Math::Radians(lastWindow->cameraFov) * 0.8f);

			f32 elevation = Math::Radians(30.0f);
			f32 azimuth = Math::Radians(45.0f);

			Vec3 cameraOffset = Vec3(
				distance * Math::Cos(elevation) * Math::Sin(azimuth),
				distance * Math::Sin(elevation),
				distance * Math::Cos(elevation) * Math::Cos(azimuth)
			);

			lastWindow->freeViewCamera.SetPosition(center + cameraOffset);
			lastWindow->freeViewCamera.SetPitchYaw(elevation, -azimuth);
		}
	}

	void SceneViewWindow::ViewEntity(RID rid)
	{
		if (lastWindow)
		{
			ViewEntity(lastWindow->sceneEditor->GetCurrentScene()->FindEntityByRID(rid));
		}
	}

	void SceneViewWindow::OpenSceneView(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<SceneViewWindow>();
	}

	void SceneViewWindow::DuplicateSceneEntity(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->GetSceneEditor()->DuplicateSelected();
	}

	void SceneViewWindow::DeleteSceneEntity(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->GetSceneEditor()->DestroySelected();
	}

	bool SceneViewWindow::CheckSelectedEntity(const MenuItemEventData& eventData)
	{
		return Editor::GetActiveWorkspace()->GetSceneEditor()->HasSelectedEntities();
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
			.workspaceTypes = {WorkspaceTypes::Scene}
		});
	}
}
