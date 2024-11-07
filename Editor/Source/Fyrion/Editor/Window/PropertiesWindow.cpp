#include "PropertiesWindow.hpp"

#include "Fyrion/Core/StringUtils.hpp"
#include "Fyrion/Editor/Editor.hpp"
#include "Fyrion/Editor/ImGui/ImGuiEditor.hpp"
#include "Fyrion/ImGui/IconsFontAwesome6.h"
#include "Fyrion/ImGui/ImGui.hpp"
#include "Fyrion/Scene/Component/Component.hpp"

namespace Fyrion
{
    PropertiesWindow::PropertiesWindow() : sceneEditor(Editor::GetSceneEditor())
    {
        Event::Bind<OnGameObjectSelection, &PropertiesWindow::GameObjectSelection>(this);
        Event::Bind<OnAssetSelection, &PropertiesWindow::AssetSelection>(this);
    }

    PropertiesWindow::~PropertiesWindow()
    {
        Event::Unbind<OnGameObjectSelection, &PropertiesWindow::GameObjectSelection>(this);
        Event::Unbind<OnAssetSelection, &PropertiesWindow::AssetSelection>(this);
    }

    void PropertiesWindow::DrawSceneObject(u32 id, GameObject* gameObject)
    {
        bool root = &sceneEditor.GetScene()->GetRootObject() == gameObject;

        ImGuiStyle& style = ImGui::GetStyle();
        bool        readOnly = false;

        ImGuiInputTextFlags nameFlags = 0;
        if (readOnly || root)
        {
            nameFlags |= ImGuiInputTextFlags_ReadOnly;
        }

        if (ImGui::BeginTable("#object-table", 2))
        {
            ImGui::BeginDisabled(readOnly);

            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();

            ImGui::Text("Name");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);

            stringCache = root ? StringView{sceneEditor.GetAssetFile()->fileName} : gameObject->GetName();

            u32 hash = HashValue(reinterpret_cast<usize>(&gameObject));

            if (ImGui::InputText(hash, stringCache, nameFlags))
            {
                renamingCache = stringCache;
                renamingFocus = true;
                renamingObject = gameObject;
            }

            if (!ImGui::IsItemActive() && renamingFocus)
            {
                sceneEditor.RenameObject(*renamingObject, renamingCache);
                renamingObject = {};
                renamingFocus = false;
                renamingCache.Clear();
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("UUID");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);

            String uuid = gameObject->GetUUID().ToString();
            ImGui::InputText(hash + 10, uuid, ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            ImGui::EndTable();
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * style.ScaleFactor);

        f32  width = ImGui::GetContentRegionAvail().x;
        auto size = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;

        ImGui::BeginHorizontal("horizontal-01", ImVec2(width, size));

        ImGui::Spring(1.f);
        bool addComponent = false;

        ImGui::BeginDisabled(readOnly);

        if (ImGui::BorderedButton("Add Component", ImVec2(width * 2 / 3, size)))
        {
            addComponent = true;
        }

        ImGui::EndDisabled();

        ImVec2 max = ImGui::GetItemRectMax();
        ImVec2 min = ImGui::GetItemRectMin();

        ImGui::Spring(1.f);

        ImGui::EndHorizontal();

        if (gameObject->GetInstance() != nullptr)
        {
            ImGui::BeginHorizontal(9999, ImVec2(width, size));
            ImGui::Spring(1.f);

            if (ImGui::BorderedButton("Open Instance", ImVec2((width * 2) / 3, size)))
            {
                //TODO defer open scene.
            }

            ImGui::Spring(1.f);
            ImGui::EndHorizontal();
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * style.ScaleFactor);

        bool openComponentSettings = false;

        for (Component* component : gameObject->GetComponents())
        {
            bool propClicked = false;
            bool open = ImGui::CollapsingHeaderProps(HashValue(reinterpret_cast<usize>(component)), FormatName(component->typeHandler->GetSimpleName()).CStr(), &propClicked);
            if (propClicked)
            {
                openComponentSettings = true;
                selectedComponent = component;
            }

            if (open)
            {
                // ImGui::BeginDisabled(object.GetPrototype() != nullptr && !object.IsComponentOverride(component));
                ImGui::Indent();
                ImGui::DrawType(ImGui::DrawTypeDesc{
                    .itemId = reinterpret_cast<usize>(component),
                    .typeHandler = component->typeHandler,
                    .instance = component,
                    .flags = readOnly ? ImGui::ImGuiDrawTypeFlags_ReadOnly : 0u,
                    .userData = this,
                    .callback = [](ImGui::DrawTypeDesc& desc)
                    {
                        PropertiesWindow* propertiesWindow = static_cast<PropertiesWindow*>(desc.userData);

                        propertiesWindow->sceneEditor.UpdateComponent(propertiesWindow->selectedObject,
                                                                      static_cast<Component*>(desc.instance));
                    },
                });
                ImGui::Unindent();
                // ImGui::EndDisabled();
            }
        }

        if (addComponent)
        {
            ImGui::OpenPopup("add-component-popup");
        }

        ImGui::SetNextWindowPos(ImVec2(min.x, max.y + 5));
        auto sizePopup = max.x - min.x;
        ImGui::SetNextWindowSize(ImVec2(sizePopup, 0), ImGuiCond_Appearing);

        auto popupRes = ImGui::BeginPopupMenu("add-component-popup", 0, false);
        if (popupRes)
        {
            ImGui::SetNextItemWidth(sizePopup - style.WindowPadding.x * 2);
            ImGui::SearchInputText(id + 100, searchComponentString);
            ImGui::Separator();

            TypeHandler* componentHandler = Registry::FindType<Component>();
            if (componentHandler)
            {
                for (DerivedType& derivedType : componentHandler->GetDerivedTypes())
                {
                    TypeHandler* typeHandler = Registry::FindTypeById(derivedType.typeId);
                    if (typeHandler)
                    {
                        if (const ComponentDesc* componentDesc = typeHandler->GetAttribute<ComponentDesc>())
                        {
                            if (!componentDesc->allowMultiple)
                            {
                                if (gameObject->GetComponent(typeHandler->GetTypeInfo().typeId))
                                {
                                    continue;
                                }
                            }
                        }

                        String name = FormatName(typeHandler->GetSimpleName());
                        if (ImGui::Selectable(name.CStr()))
                        {
                            sceneEditor.AddComponent(gameObject, typeHandler);
                        }
                    }
                }
            }
        }
        ImGui::EndPopupMenu(popupRes);

        if (openComponentSettings)
        {
            ImGui::OpenPopup("open-component-settings");
        }

        bool popupOpenSettings = ImGui::BeginPopupMenu("open-component-settings", 0, false);
        if (popupOpenSettings && selectedComponent)
        {
            if (ImGui::MenuItem("Reset"))
            {
                sceneEditor.ResetComponent(gameObject, selectedComponent);
                ImGui::CloseCurrentPopup();
            }


            if (gameObject->GetInstance() != nullptr && gameObject->IsComponentOverride(selectedComponent))
            {
                if (ImGui::MenuItem("Remove instance override"))
                {
                    sceneEditor.RemoveComponentOverride(gameObject, selectedComponent);
                }
            }

            if (ImGui::MenuItem("Remove"))
            {
                sceneEditor.RemoveComponent(gameObject, selectedComponent);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopupMenu(popupOpenSettings);
    }

    void PropertiesWindow::DrawAsset(u32 id, AssetFile* assetFile)
    {
        ImGuiStyle& style = ImGui::GetStyle();


        if (ImGui::BeginTable("#object-table", 2))
        {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();

            ImGui::Text("Name");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);

            stringCache = assetFile->GetName();

            u32 hash = HashValue(reinterpret_cast<usize>(&assetFile->fileName));

            if (ImGui::InputText(hash, stringCache))
            {
                renamingCache = stringCache;
                renamingFocus = true;
            }

            if (!ImGui::IsItemActive() && renamingFocus)
            {
                AssetEditor::Rename(assetFile, renamingCache);
                renamingFocus = false;
                renamingCache.Clear();
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("UUID");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);

            String uuid = assetFile->uuid.ToString();
            ImGui::InputText(hash + 10, uuid, ImGuiInputTextFlags_ReadOnly);
            ImGui::EndTable();
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * style.ScaleFactor);

        TypeHandler* typeHandler = Registry::FindTypeById(assetFile->handler->GetAssetTypeID());

        if (ImGui::CollapsingHeader(FormatName(typeHandler->GetSimpleName()).CStr()), ImGuiTreeNodeFlags_DefaultOpen)
        {
            ImGui::Indent();
            ImGui::DrawType(ImGui::DrawTypeDesc{
                .itemId = assetFile->hash,
                .typeHandler = typeHandler,
                .instance = Assets::Load(assetFile->uuid),
                .flags = ImGui::ImGuiDrawTypeFlags_None,
                .userData = this,
                .callback = [](ImGui::DrawTypeDesc& desc)
                {
                    PropertiesWindow* propertiesWindow = static_cast<PropertiesWindow*>(desc.userData);
                    AssetEditor::UpdateAssetValue(propertiesWindow->selectedAsset, static_cast<Asset*>(desc.instance));
                },
            });
            ImGui::Unindent();
        }
    }

    void PropertiesWindow::Draw(u32 id, bool& open)
    {
        ImGui::Begin(id, ICON_FA_CIRCLE_INFO " Properties", &open, ImGuiWindowFlags_NoScrollbar);
        if (selectedObject)
        {
            DrawSceneObject(id, selectedObject);
        }
        else if (selectedAsset)
        {
            DrawAsset(id, selectedAsset);
        }
        ImGui::End();
    }


    void PropertiesWindow::ClearSelection()
    {
        selectedObject = nullptr;
        selectedComponent = nullptr;
    }

    void PropertiesWindow::OpenProperties(const MenuItemEventData& eventData)
    {
        Editor::OpenWindow<PropertiesWindow>();
    }

    void PropertiesWindow::GameObjectSelection(GameObject* object)
    {
        if (object == nullptr && selectedObject == nullptr) return;
        ClearSelection();
        selectedObject = object;
    }

    void PropertiesWindow::AssetSelection(AssetFile* assetFile)
    {
        if (assetFile == nullptr && selectedAsset == nullptr) return;
        ClearSelection();
        selectedAsset = assetFile;
    }


    void PropertiesWindow::RegisterType(NativeTypeHandler<PropertiesWindow>& type)
    {
        Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Properties", .action = OpenProperties});

        type.Attribute<EditorWindowProperties>(EditorWindowProperties{
            .dockPosition = DockPosition::BottomRight,
            .createOnInit = true
        });
    }
}
