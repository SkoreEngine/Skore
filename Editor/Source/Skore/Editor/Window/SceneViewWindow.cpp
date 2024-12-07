#include "SceneViewWindow.hpp"

#include "imgui_internal.h"
#include "Skore/Engine.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Editor/Editor.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"

#include "Skore/ImGui/ImGui.hpp"
#include "Skore/ImGui/Lib/ImGuizmo.h"
#include "Skore/IO/Input.hpp"
#include "Skore/Scene/Component/TransformComponent.hpp"
#include "Skore/Graphics/RenderProxy.hpp"
#include "Skore/Graphics/DefaultRenderPipeline/DefaultRenderPipeline.hpp"

namespace Skore
{
    MenuItemContext SceneViewWindow::menuItemContext = {};

    SceneViewWindow::SceneViewWindow() : sceneEditor(Editor::GetSceneEditor()), guizmoOperation(ImGuizmo::TRANSLATE)
    {
        Event::Bind<OnRecordRenderCommands, &SceneViewWindow::RecordRenderCommands>(this);
    }

    SceneViewWindow::~SceneViewWindow()
    {
        Event::Unbind<OnRecordRenderCommands, &SceneViewWindow::RecordRenderCommands>(this);

        if (renderGraph)
        {
            DestroyAndFree(renderGraph);
        }
    }

    void SceneViewWindow::Draw(u32 id, bool& open)
    {
        bool openSceneOptions = false;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar;
        auto&            style = ImGui::GetStyle();
        ImGui::StyleVar  windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
        {
            flags |= ImGuiWindowFlags_NoMove;
        }

        ImGui::Begin(id, ICON_FA_BORDER_ALL " Scene Viewport", &open, flags);
        {
            bool   moving = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            bool   canChangeGuizmo = !moving && !ImGui::GetIO().WantCaptureKeyboard;
            bool   hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
            ImVec2 size = ImGui::GetWindowSize();
            ImVec2 initCursor = ImGui::GetCursorScreenPos();
            ImVec2 buttonSize = ImVec2(25 * style.ScaleFactor, 22 * style.ScaleFactor);
            ImVec2 cursor{};

            {
                ImGui::StyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(style.ScaleFactor * 2, style.ScaleFactor * 2));
                ImGui::StyleVar space(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
                ImGui::BeginChild(id + 1000, ImVec2(0, buttonSize.y + (5 * style.ScaleFactor)), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar);

                //ImGui::StyleVar itemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                ImGui::BeginHorizontal("horizontal-sceneview-top", ImVec2(ImGui::GetContentRegionAvail().x, buttonSize.y));

                if (ImGui::SelectionButton(ICON_FA_ARROW_POINTER, guizmoOperation == 0, buttonSize)
                    || (canChangeGuizmo && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Q))))
                {
                    guizmoOperation = 0;
                }

                if (ImGui::SelectionButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, guizmoOperation == ImGuizmo::TRANSLATE, buttonSize)
                    || (canChangeGuizmo && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_W))))
                {
                    guizmoOperation = ImGuizmo::TRANSLATE;
                }

                if (ImGui::SelectionButton(ICON_FA_ROTATE, guizmoOperation == ImGuizmo::ROTATE, buttonSize)
                    || (canChangeGuizmo && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_E))))
                {
                    guizmoOperation = ImGuizmo::ROTATE;
                }
                if (ImGui::SelectionButton(ICON_FA_EXPAND, guizmoOperation == ImGuizmo::SCALE, buttonSize)
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

                bool isSimulating = sceneEditor.IsSimulating();

                if (!isSimulating)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(139, 194, 74, 255));
                }

                if (windowStartedSimulation && !isSimulating)
                {
                    windowStartedSimulation = false;
                }

                ImGui::BeginDisabled(isSimulating);

                if (ImGui::Button(ICON_FA_PLAY, buttonSize))
                {
                    sceneEditor.StartSimulation();
                    windowStartedSimulation = true;
                }

                ImGui::EndDisabled();

                if (!isSimulating)
                {
                    ImGui::PopStyleColor();
                }

                ImGui::BeginDisabled(!sceneEditor.IsSimulating() || !windowStartedSimulation);

                if (isSimulating)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(199, 84, 80, 255));
                }

                if (ImGui::Button(ICON_FA_STOP, buttonSize))
                {
                    sceneEditor.StopSimulation();
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

            freeViewCamera.Process(Engine::DeltaTime());

            auto diffCursor = cursor - initCursor;
            size -= diffCursor;
            Rect bb{(i32)cursor.x, (i32)cursor.y, u32(cursor.x + size.x), u32(cursor.y + size.y)};

            f32 scale = 1.0;
            Extent extent = {static_cast<u32>(size.x * scale), static_cast<u32>(size.y * scale)};


            if (bool renderGraphDirty = renderGraph == nullptr || sceneEditor.GetActiveScene() != renderGraph->GetScene() || renderDirty)
            {
                if (renderGraph)
                {
                    DestroyAndFree(renderGraph);
                }
                renderGraph = Alloc<RenderGraph>(RenderGraphCreation{
                    .drawToSwapChain = false
                });
                defaultRenderPipeline.BuildRenderGraph(*renderGraph);
                renderGraph->Create(sceneEditor.GetActiveScene(), extent);

                renderDirty = false;
            }

            if (extent != renderGraph->GetViewportExtent())
            {
                renderGraph->Resize(extent);
            }

            RenderProxy* renderProxy = nullptr;
            if (Scene* scene = sceneEditor.GetActiveScene())
            {
                renderProxy = scene->GetProxy<RenderProxy>();
            }

            if (sceneEditor.IsSimulating() && renderProxy != nullptr && renderProxy->GetCamera() != nullptr)
            {
                const CameraData* gameCamera = renderProxy->GetCamera();

                cameraData.view = gameCamera->view;
                cameraData.projectionType = gameCamera->projectionType;
                cameraData.fov = gameCamera->fov;
                cameraData.viewPos = gameCamera->viewPos;
                cameraData.nearClip = gameCamera->nearClip;
                cameraData.farClip = gameCamera->farClip;
            }
            else
            {
                cameraData.view = freeViewCamera.GetView();
                cameraData.projectionType = CameraProjection::Perspective;
                cameraData.fov = 60.f;
                cameraData.viewPos = freeViewCamera.GetPosition();
                cameraData.nearClip = 0.1f;
                cameraData.farClip = 300.f;
            }

            if (cameraData.projectionType == CameraProjection::Perspective)
            {
                cameraData.projection = Math::Perspective(Math::Radians(cameraData.fov),
                                                          (f32)extent.width / (f32)extent.height,
                                                          cameraData.nearClip,
                                                          cameraData.farClip);
            }

            //check parameter to apply jitter
            if (defaultRenderPipeline.antiAliasing == AntiAliasingType::TAA)
            {
                static u32 jitterIndex = 0;
                static u32 jitterPeriod = 4;
                Vec2 jitterValues = Halton23Sequence(jitterIndex);

                jitterIndex = ( jitterIndex + 1 ) % jitterPeriod;
                Vec2 jitterOffsets = Vec2{jitterValues.x * 2 - 1.0f, jitterValues.y * 2 - 1.0f };

                static f32 jitterScale = 1.0f;

                jitterOffsets.x *= jitterScale;
                jitterOffsets.y *= jitterScale;

                cameraData.previousJitter = cameraData.jitter;
                cameraData.jitter = Vec2{jitterOffsets.x / static_cast<f32>(extent.width), jitterOffsets.y / static_cast<f32>(extent.height)};

                Mat4 jitterMattrix = Math::Translate(Mat4{1.0}, Vec3{cameraData.jitter.x, cameraData.jitter.y, 0.f});
                cameraData.projection = jitterMattrix * cameraData.projection;
            }

            cameraData.lastProjView = cameraData.projView;
            cameraData.projView = cameraData.projection * cameraData.view;
            cameraData.viewInverse = Math::Inverse(cameraData.view);
            cameraData.projectionInverse = Math::Inverse(cameraData.projection);

            renderGraph->SetCameraData(cameraData);

            if (open)
            {
                ImGui::DrawTexture(renderGraph->GetColorOutput(), bb);
            }

            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(cursor.x, cursor.y, size.x, size.y);

            if (!sceneEditor.IsSimulating())
            {
                for (auto it : sceneEditor.selectedObjects)
                {
                    if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(it.first))
                    {
                        if (TransformComponent* transformComponent = object->GetComponent<TransformComponent>())
                        {
                            Mat4 worldMatrix = transformComponent->GetWorldTransform();

                            static float snap[3] = {0.0f, 0.0f, 0.0f};

                            ImGuizmo::Manipulate(&cameraData.view[0][0],
                                                 &cameraData.projection[0][0],
                                                 static_cast<ImGuizmo::OPERATION>(guizmoOperation),
                                                 ImGuizmo::LOCAL,
                                                 &worldMatrix[0][0],
                                                 nullptr,
                                                 snap);

                            if (ImGuizmo::IsUsing())
                            {
                                if (!usingGuizmo)
                                {
                                    usingGuizmo = true;
                                    gizmoInitialTransform = transformComponent->GetTransform();

                                    // if (object->GetPrototype() != nullptr && !object->IsComponentOverride(transformComponent))
                                    // {
                                    //     gizmoTransaction->CreateAction<OverridePrototypeComponentAction>(sceneEditor, object, static_cast<Component*>(transformComponent))->Commit();
                                    // }
                                }

                                if (object->GetParent() != nullptr)
                                {
                                    if (TransformComponent* parentTransform = object->GetParent()->GetComponent<TransformComponent>())
                                    {
                                        worldMatrix = Math::Inverse(parentTransform->GetWorldTransform()) * worldMatrix;
                                    }
                                }

                                Vec3 position, rotation, scale;
                                Math::Decompose(worldMatrix, position, rotation, scale);
                                auto deltaRotation = rotation - Math::EulerAngles(transformComponent->GetRotation());
                                transformComponent->SetTransform(position, Math::EulerAngles(transformComponent->GetRotation()) + deltaRotation, scale);
                            }
                            else if (usingGuizmo)
                            {
                                sceneEditor.UpdateTransform(object, gizmoInitialTransform, transformComponent);
                                usingGuizmo = false;
                            }
                        }
                    }
                }
            }

            if (const ImGuiPayload* payload = ImGui::GetDragDropPayload())
            {
                AssetPayload* assetPayload = static_cast<AssetPayload*>(ImGui::GetDragDropPayload()->Data);
                f32           pad = 4.0f;
                if (assetPayload && assetPayload->assetType == GetTypeID<Scene>() && ImGui::BeginDragDropTargetCustom(ImRect(bb.x + pad, bb.y + pad, bb.width - pad, bb.height - pad), id))
                {
                    if (ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
                    {
                        sceneEditor.CreateGameObject(assetPayload->assetFile->uuid, false);
                    }
                    ImGui::EndDragDropTarget();
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

        auto popupRes = ImGui::BeginPopupMenu("scene-options-modal", 0, false);
        if (popupRes)
        {
            bool taaEnabled = defaultRenderPipeline.antiAliasing == AntiAliasingType::TAA;

            ImGui::Checkbox("TAA Enabled", &taaEnabled);
            if (!taaEnabled && defaultRenderPipeline.antiAliasing == AntiAliasingType::TAA)
            {
                defaultRenderPipeline.antiAliasing = AntiAliasingType::None;
                renderDirty = true;
            }

            if (taaEnabled && defaultRenderPipeline.antiAliasing != AntiAliasingType::TAA)
            {
                defaultRenderPipeline.antiAliasing = AntiAliasingType::TAA;
                renderDirty = true;
            }

        }
        ImGui::EndPopupMenu(popupRes);


        ImGui::End();
    }

    void SceneViewWindow::AddMenuItem(const MenuItemCreation& menuItem)
    {
        menuItemContext.AddMenuItem(menuItem);
    }

    void SceneViewWindow::OpenSceneView(const MenuItemEventData& eventData)
    {
        Editor::OpenWindow<SceneViewWindow>();
    }

    void SceneViewWindow::DuplicateSceneObject(const MenuItemEventData& eventData)
    {
        static_cast<SceneViewWindow*>(eventData.drawData)->sceneEditor.DuplicateSelected();
    }

    void SceneViewWindow::DeleteSceneObject(const MenuItemEventData& eventData)
    {
        static_cast<SceneViewWindow*>(eventData.drawData)->sceneEditor.DestroySelectedObjects();
    }

    bool SceneViewWindow::CheckSelectedObject(const MenuItemEventData& eventData)
    {
        return static_cast<SceneViewWindow*>(eventData.drawData)->sceneEditor.IsValidSelection();
    }

    void SceneViewWindow::RecordRenderCommands(RenderCommands& cmd, f64 deltaTime)
    {
        if (renderGraph != nullptr && sceneEditor.GetActiveScene() == renderGraph->GetScene())
        {
            renderGraph->RecordCommands(cmd, deltaTime);
        }
    }


    void SceneViewWindow::RegisterType(NativeTypeHandler<SceneViewWindow>& type)
    {
        Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Scene Viewport", .action = OpenSceneView});

        AddMenuItem(MenuItemCreation{.itemName = "Duplicate", .priority = 210, .itemShortcut = {.ctrl = true, .presKey = Key::D}, .action = DuplicateSceneObject, .enable = CheckSelectedObject});
        AddMenuItem(MenuItemCreation{.itemName = "Delete", .priority = 220, .itemShortcut = {.presKey = Key::Delete}, .action = DeleteSceneObject, .enable = CheckSelectedObject});

        type.Attribute<EditorWindowProperties>(EditorWindowProperties{
            .dockPosition = DockPosition::Center,
            .createOnInit = true
        });
    }
}
