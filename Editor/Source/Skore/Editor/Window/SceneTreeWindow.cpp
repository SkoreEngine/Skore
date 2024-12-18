#include "SceneTreeWindow.hpp"

#include "imgui_internal.h"
#include "Skore/Editor/Editor.hpp"
#include "Skore/Editor/Scene/SceneEditor.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"


namespace Skore
{
    MenuItemContext SceneTreeWindow::menuItemContext = {};
    SceneTreeWindow::SceneTreeWindow() : sceneEditor(Editor::GetSceneEditor()) {}


    void SceneTreeWindow::DrawGameObject(GameObject& gameObject)
    {
        bool root = gameObject.GetParent() == nullptr;
        ImGuiID treeId = static_cast<ImGuiID>(HashValue(reinterpret_cast<usize>(&gameObject)));



        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        if (!root)
        {
            DrawMovePayload(treeId + 4, gameObject.GetParent(), gameObject.GetIndex());
        }

        Span<GameObject*> children = gameObject.GetChildren();

        stringCache.Clear();
        stringCache += root ? ICON_FA_CUBES : ICON_FA_CUBE;
        stringCache += " ";
        stringCache += root ? StringView{sceneEditor.GetAssetFile()->fileName} : gameObject.GetName();

        bool isSelected = sceneEditor.IsSelected(gameObject);

        auto treeFlags = isSelected ? ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_SpanAllColumns : ImGuiTreeNodeFlags_SpanAllColumns;
        bool open = false;

        if (root)
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        }

        if (sceneEditor.IsParentOfSelected(gameObject))
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }

        if (gameObject.GetPrefab() != nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(143, 131, 34, 255));
        }

        ImVec2 cursorPos = ImGui::GetCursorPos();

        if (isSelected && renamingSelected)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetTreeNodeToLabelSpacing());

            if (!renamingFocus)
            {
                renamingStringCache = gameObject.GetName();
                ImGui::SetKeyboardFocusHere();
            }

            ImGui::StyleVar framePadding(ImGuiStyleVar_FramePadding, ImVec2{0, 0});

            ImGui::Text(ICON_FA_CUBE);
            ImGui::SameLine();

            auto size = ImGui::CalcTextSize(" ");
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + size.x);

            ImGui::InputText(66554433, renamingStringCache);

            if (!ImGui::IsItemActive() && renamingFocus)
            {
                renamingSelected = false;
                renamingFocus = false;
                sceneEditor.RenameObject(gameObject, renamingStringCache);
            }

            if (!renamingFocus && renamingSelected)
            {
                renamingFocus = true;
            }

            ImGui::SetCursorPos(cursorPos);

            if (children.Size() > 0)
            {
                open = ImGui::TreeNode(treeId, " ", 0);
            }
        }
        else if (children.Size() > 0)
        {
            open = ImGui::TreeNode(treeId, stringCache.CStr(), treeFlags);
        }
        else
        {
            ImGui::TreeLeaf(treeId, stringCache.CStr(), treeFlags);
        }

        if (gameObject.GetPrefab() != nullptr)
        {
            ImGui::PopStyleColor();
        }

        bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        bool ctrlDown = ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_RightCtrl));

        if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && isHovered)
        {
            if (!sceneEditor.IsSelected(gameObject))
            {
                EditorTransaction* transaction = Editor::CreateTransaction();
                if (!ctrlDown)
                {
                    sceneEditor.ClearSelection(transaction);
                }
                sceneEditor.SelectObject(gameObject, transaction);
            }
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
        {
            selectedCache.Clear();
            for(auto& it: sceneEditor.selectedObjects)
            {
                if (GameObject* object = sceneEditor.GetActiveScene()->FindObjectByUUID(it.first))
                {
                    selectedCache.EmplaceBack(object);
                }
            }

            GameObjectPayload payload{
                .objects = selectedCache
            };

            ImGui::SetDragDropPayload(SK_GAME_OBJECT_PAYLOAD, &payload, sizeof(GameObjectPayload));
            ImGui::Text("%s", gameObject.GetName().CStr());
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && isHovered)
        {
            newObjectIsSelected = true;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_GAME_OBJECT_PAYLOAD))
            {
                GameObjectPayload& gameObjectPayload = *static_cast<GameObjectPayload*>(payload->Data);
                sceneEditor.ChangeParent(&gameObject, gameObjectPayload.objects);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::TableNextColumn();
        if (!root)
        {
            ImGui::Text("  " ICON_FA_EYE);
        }

        if (open)
        {
            for (GameObject* child : children)
            {
                DrawGameObject(*child);
            }
            ImGui::TreePop();
        }
    }

    void SceneTreeWindow::Draw(u32 id, bool& open)
    {
        newObjectIsSelected = false;
        auto& style = ImGui::GetStyle();
        auto  originalWindowPadding = style.WindowPadding;

        ImGui::StyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin(id, ICON_FA_LIST " Scene Tree", &open, ImGuiWindowFlags_NoScrollbar);
        bool openPopup = false;

        {
            ImGui::StyleVar childWindowPadding(ImGuiStyleVar_WindowPadding, originalWindowPadding);

            ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar;
            ImGui::BeginChild("top-fields", ImVec2(0, (25 * style.ScaleFactor) + originalWindowPadding.y), false, flags);

            if (ImGui::Button(ICON_FA_PLUS))
            {
                openPopup = true;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::SearchInputText(id + 10, searchObject);
            ImGui::EndChild();
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + originalWindowPadding.y);

        {
            ImGui::StyleVar   cellPadding(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
            ImGui::StyleVar   frameRounding(ImGuiStyleVar_FrameRounding, 0);
            ImGui::StyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
            ImGui::StyleColor borderColor(ImGuiCol_Border, IM_COL32(45, 46, 48, 255));

            if (ImGui::BeginChild("scene-tree-view-child", ImVec2(0, 0), false))
            {
                static ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody;

                if (ImGui::BeginTable("scene-tree-view-table", 2, tableFlags))
                {
                    ImGui::TableSetupColumn("  Name", ImGuiTableColumnFlags_NoHide);
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 35 * style.ScaleFactor);
                    ImGui::TableHeadersRow();

                    if (Scene* scene = sceneEditor.GetActiveScene())
                    {
                        ImGui::BeginTreeNode();
                        DrawGameObject(scene->GetRootObject());

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        DrawMovePayload(98765, &scene->GetRootObject(), U32_MAX);

                        ImGui::EndTreeNode();
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }

        bool closePopup = false;

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        {
            if (menuItemContext.ExecuteHotKeys(this))
            {
                closePopup = true;
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                if (!newObjectIsSelected)
                {
                    sceneEditor.ClearSelection(Editor::CreateTransaction());
                    renamingSelected = false;
                }
                openPopup = true;
            }
        }

        if (openPopup)
        {
            ImGui::OpenPopup("scene-tree-popup");
        }

        bool popupRes = ImGui::BeginPopupMenu("scene-tree-popup");
        if (popupRes)
        {
            menuItemContext.Draw(this);
            if (closePopup)
            {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopupMenu(popupRes);
        ImGui::End();
    }

    void SceneTreeWindow::OpenSceneTree(const MenuItemEventData& eventData)
    {
        Editor::OpenWindow<SceneTreeWindow>();
    }

    void SceneTreeWindow::AddSceneObject(const MenuItemEventData& eventData)
    {
        static_cast<SceneTreeWindow*>(eventData.drawData)->sceneEditor.CreateGameObject({}, true);
    }

    void SceneTreeWindow::AddSceneObjectFromAsset(const MenuItemEventData& eventData) {}

    void SceneTreeWindow::AddComponent(const MenuItemEventData& eventData) {}

    void SceneTreeWindow::RenameSceneObject(const MenuItemEventData& eventData)
    {
        static_cast<SceneTreeWindow*>(eventData.drawData)->renamingSelected = true;
    }

    void SceneTreeWindow::DuplicateSceneObject(const MenuItemEventData& eventData)
    {
        static_cast<SceneTreeWindow*>(eventData.drawData)->sceneEditor.DuplicateSelected();
    }

    void SceneTreeWindow::DeleteSceneObject(const MenuItemEventData& eventData)
    {
        static_cast<SceneTreeWindow*>(eventData.drawData)->sceneEditor.DestroySelectedObjects();
    }

    bool SceneTreeWindow::CheckSelectedObject(const MenuItemEventData& eventData)
    {
        return static_cast<SceneTreeWindow*>(eventData.drawData)->sceneEditor.IsValidSelection();
    }

    void SceneTreeWindow::DrawMovePayload(u32 id, GameObject* parent, usize index) const
    {
        ImVec2 screenPos = ImVec2(ImGui::GetWindowPos().x, ImGui::GetCursorScreenPos().y);
        if (ImGui::BeginDragDropTargetCustom(ImRect(screenPos, screenPos + ImVec2(ImGui::GetContentRegionMax().x, std::ceil(1 * ImGui::GetStyle().ScaleFactor))), id))
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_GAME_OBJECT_PAYLOAD))
            {
                GameObjectPayload& gameObjectPayload = *static_cast<GameObjectPayload*>(payload->Data);
                sceneEditor.MoveEntities(parent, index, gameObjectPayload.objects);

            }
            ImGui::EndDragDropTarget();
        }
    }

    void SceneTreeWindow::AddMenuItem(const MenuItemCreation& menuItem)
    {
        menuItemContext.AddMenuItem(menuItem);
    }

    void SceneTreeWindow::RegisterType(NativeTypeHandler<SceneTreeWindow>& type)
    {
        Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Scene Tree", .action = OpenSceneTree});

        AddMenuItem(MenuItemCreation{.itemName = "Add Empty Object", .priority = 0, .itemShortcut = {.ctrl = true, .presKey = Key::Space}, .action = AddSceneObject});
        AddMenuItem(MenuItemCreation{.itemName = "Add Object From Asset", .priority = 10, .action = AddSceneObjectFromAsset});
        AddMenuItem(MenuItemCreation{.itemName = "Add Component", .priority = 20, .action = AddComponent});
        AddMenuItem(MenuItemCreation{.itemName = "Rename", .priority = 200, .itemShortcut = {.presKey = Key::F2}, .action = RenameSceneObject, .enable = CheckSelectedObject});
        AddMenuItem(MenuItemCreation{.itemName = "Duplicate", .priority = 210, .itemShortcut = {.ctrl = true, .presKey = Key::D}, .action = DuplicateSceneObject, .enable = CheckSelectedObject});
        AddMenuItem(MenuItemCreation{.itemName = "Delete", .priority = 220, .itemShortcut = {.presKey = Key::Delete}, .action = DeleteSceneObject, .enable = CheckSelectedObject});

        type.Attribute<EditorWindowProperties>(EditorWindowProperties{
            .dockPosition = DockPosition::TopRight,
            .createOnInit = true
        });
    }
}
